// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source attributes for the declaration reader. Include after CheckInputReader and check_input_type_result.
// check_input_attributes consumes only attributes, retains their exact token ranges in an arena, and never calls
// the declaration/constant readers. The caller evaluates each unresolved integer argument, supplies it through
// check_input_attribute_value, then applies type attributes with check_input_attribute_type. _Alignas(type-name)
// uses the type-name reader's alignment as that value. Record/member layout takes the retained alignment/packed
// fields directly. Other declaration attributes survive in records for diagnostics, linkage and ABI consumers.
//
// Records are cold metadata, in source order, with pointer/count/capacity and half-open token ranges. The growable
// array uses the output arena, never scratch; its old storage is abandoned geometrically, bounded by four records
// per retained attribute plus the initial allocation. No pointer survives a subsequent append: consumers use
// indices until scanning completes. String payloads are decoded once into the output arena. Neither token padding
// nor spare arena capacity is readable input. Attribute classification is deliberately an explicit vocabulary;
// advisory entries preserve their spelling/arguments and affect diagnostics or optimization, not C type identity.
// Unknown attributes fail explicitly instead of making arbitrary syntax invisible to the checker.
// Lists of two or more arguments retain exactly count - 1 comma token indices in the output arena. Counting
// and filling use two forward delimiter walks, skipping grouped expressions through the parser's parent links.
// check_input_attribute_argument consumes that table in constant time without reading tokens. The first/last
// bounds already describe zero/single arguments, so those allocate nothing. Copies/merges share the immutable
// boundaries for the arena's lifetime; shape checking and each once-only ICE evaluation reuse the same ranges.

#define BUSTER_CHECK_ATTRIBUTE_IMPLEMENTATION 1

typedef enum CheckInputAttributeKind
{
    CHECK_ATTRIBUTE_UNKNOWN,
    CHECK_ATTRIBUTE_PACKED,
    CHECK_ATTRIBUTE_ALIGNED,
    CHECK_ATTRIBUTE_ALIGNAS,
    CHECK_ATTRIBUTE_MODE,
    CHECK_ATTRIBUTE_VECTOR_SIZE,
    CHECK_ATTRIBUTE_EXT_VECTOR_TYPE,
    CHECK_ATTRIBUTE_TRANSPARENT_UNION,
    CHECK_ATTRIBUTE_MAY_ALIAS,
    CHECK_ATTRIBUTE_VISIBILITY,
    CHECK_ATTRIBUTE_ALIAS,
    CHECK_ATTRIBUTE_IFUNC,
    CHECK_ATTRIBUTE_SECTION,
    CHECK_ATTRIBUTE_WEAK,
    CHECK_ATTRIBUTE_WEAKREF,
    CHECK_ATTRIBUTE_COMMON,
    CHECK_ATTRIBUTE_NOCOMMON,
    CHECK_ATTRIBUTE_TLS_MODEL,
    CHECK_ATTRIBUTE_ASM_LABEL,
    CHECK_ATTRIBUTE_CALLING_CONVENTION,
    CHECK_ATTRIBUTE_NORETURN,
    CHECK_ATTRIBUTE_RETURNS_TWICE,
    CHECK_ATTRIBUTE_NOTHROW,
    CHECK_ATTRIBUTE_CONST,
    CHECK_ATTRIBUTE_PURE,
    CHECK_ATTRIBUTE_MALLOC,
    CHECK_ATTRIBUTE_ALLOC_SIZE,
    CHECK_ATTRIBUTE_ALLOC_ALIGN,
    CHECK_ATTRIBUTE_ASSUME_ALIGNED,
    CHECK_ATTRIBUTE_NONNULL,
    CHECK_ATTRIBUTE_RETURNS_NONNULL,
    CHECK_ATTRIBUTE_FORMAT,
    CHECK_ATTRIBUTE_FORMAT_ARG,
    CHECK_ATTRIBUTE_SENTINEL,
    CHECK_ATTRIBUTE_WARN_UNUSED_RESULT,
    CHECK_ATTRIBUTE_DEPRECATED,
    CHECK_ATTRIBUTE_UNAVAILABLE,
    CHECK_ATTRIBUTE_ERROR,
    CHECK_ATTRIBUTE_WARNING,
    CHECK_ATTRIBUTE_HOT,
    CHECK_ATTRIBUTE_COLD,
    CHECK_ATTRIBUTE_ALWAYS_INLINE,
    CHECK_ATTRIBUTE_NOINLINE,
    CHECK_ATTRIBUTE_GNU_INLINE,
    CHECK_ATTRIBUTE_ARTIFICIAL,
    CHECK_ATTRIBUTE_NODEBUG,
    CHECK_ATTRIBUTE_OVERLOADABLE,
    CHECK_ATTRIBUTE_FLATTEN,
    CHECK_ATTRIBUTE_LEAF,
    CHECK_ATTRIBUTE_USED,
    CHECK_ATTRIBUTE_UNUSED,
    CHECK_ATTRIBUTE_RETAIN,
    CHECK_ATTRIBUTE_EXTERNALLY_VISIBLE,
    CHECK_ATTRIBUTE_CONSTRUCTOR,
    CHECK_ATTRIBUTE_DESTRUCTOR,
    CHECK_ATTRIBUTE_CLEANUP,
    CHECK_ATTRIBUTE_ACCESS,
    CHECK_ATTRIBUTE_NONSTRING,
    CHECK_ATTRIBUTE_DESIGNATED_INIT,
    CHECK_ATTRIBUTE_FLAG_ENUM,
    CHECK_ATTRIBUTE_ENUM_EXTENSIBILITY,
    CHECK_ATTRIBUTE_TARGET,
    CHECK_ATTRIBUTE_TARGET_CLONES,
    CHECK_ATTRIBUTE_CPU_SPECIFIC,
    CHECK_ATTRIBUTE_CPU_DISPATCH,
    CHECK_ATTRIBUTE_MIN_VECTOR_WIDTH,
    CHECK_ATTRIBUTE_NO_SANITIZE,
    CHECK_ATTRIBUTE_NO_INSTRUMENT_FUNCTION,
    CHECK_ATTRIBUTE_NAKED,
    CHECK_ATTRIBUTE_NO_STACK_PROTECTOR,
    CHECK_ATTRIBUTE_FALLTHROUGH,
    CHECK_ATTRIBUTE_UNSEQUENCED,
    CHECK_ATTRIBUTE_REPRODUCIBLE,
    CHECK_ATTRIBUTE_NULLABILITY,
    CHECK_ATTRIBUTE_REGPARM,
    CHECK_ATTRIBUTE_ALIGN_VALUE,
    CHECK_ATTRIBUTE_NO_BUILTIN,
    CHECK_ATTRIBUTE_MUSTTAIL,
    CHECK_ATTRIBUTE_NOMERGE,
    CHECK_ATTRIBUTE_SUPPRESS,
    // clang's counted_by, sized_by, counted_by_or_null and sized_by_or_null (#148, c_check_counted_by_input.c).
    CHECK_ATTRIBUTE_COUNTED_BY,
    // clang's lifetimebound (its checks alone in C), loader_uninitialized (a definition without an initializer),
    // weak_import (a weak reference where the unit doesn't define its entity) and internal_linkage (#147).
    CHECK_ATTRIBUTE_LIFETIMEBOUND,
    CHECK_ATTRIBUTE_LOADER_UNINITIALIZED,
    CHECK_ATTRIBUTE_WEAK_IMPORT,
    CHECK_ATTRIBUTE_INTERNAL_LINKAGE,
} CheckInputAttributeKind;

typedef enum CheckInputAttributeError
{
    CHECK_ATTRIBUTE_ERROR_NONE,
    CHECK_ATTRIBUTE_ERROR_UNKNOWN,
    CHECK_ATTRIBUTE_ERROR_SYNTAX,
    CHECK_ATTRIBUTE_ERROR_ARGUMENTS,
    CHECK_ATTRIBUTE_ERROR_STRING,
    CHECK_ATTRIBUTE_ERROR_IDENTIFIER,
    CHECK_ATTRIBUTE_ERROR_INTEGER,
    CHECK_ATTRIBUTE_ERROR_ALIGNMENT,
    CHECK_ATTRIBUTE_ERROR_MODE,
    CHECK_ATTRIBUTE_ERROR_VECTOR,
    CHECK_ATTRIBUTE_ERROR_CONFLICT,
    CHECK_ATTRIBUTE_ERROR_SUBJECT,
    CHECK_ATTRIBUTE_ERROR_RESOURCE,
    CHECK_ATTRIBUTE_ERROR_PARAMETER,
} CheckInputAttributeError;

static const char* const check_input_attribute_messages[] __attribute__((unused)) =
{
    "",
    "semantic checking of this attribute is not implemented",
    "malformed attribute syntax",
    "incorrect number of attribute arguments",
    "attribute requires an ordinary string literal",
    "attribute requires an identifier argument",
    "attribute requires an integer constant expression",
    "requested alignment is not a supported power of two",
    "invalid machine mode for this type",
    "invalid vector element type or size",
    "conflicting declaration attributes",
    "attribute cannot be applied to this declaration",
    "out of memory while reading attributes",
    "attribute refers to an invalid function parameter",
};

typedef enum CheckInputAttributeSchema
{
    CHECK_ATTRIBUTE_NO_ARGUMENTS,
    CHECK_ATTRIBUTE_INTEGER,
    CHECK_ATTRIBUTE_STRING,
    CHECK_ATTRIBUTE_IDENTIFIER,
    CHECK_ATTRIBUTE_INTEGER_LIST,
    CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS,
    CHECK_ATTRIBUTE_STRING_LIST,
    CHECK_ATTRIBUTE_IDENTIFIER_LIST,
    // One expression, read where the declaration is (the counted_by family's, #148), never evaluated as a constant.
    CHECK_ATTRIBUTE_EXPRESSION,
} CheckInputAttributeSchema;

typedef enum CheckInputCallingConvention
{
    CHECK_CALL_DEFAULT,
    CHECK_CALL_SYSV,
    CHECK_CALL_MICROSOFT,
    CHECK_CALL_VECTOR,
    CHECK_CALL_REG,
    CHECK_CALL_PRESERVE_MOST,
    CHECK_CALL_PRESERVE_ALL,
    CHECK_CALL_PRESERVE_NONE,
    CHECK_CALL_IGNORED_X86_32,
} CheckInputCallingConvention;

typedef struct CheckInputAttribute CheckInputAttribute;
struct CheckInputAttribute
{
    uint32_t first;
    uint32_t end;
    uint32_t name;
    uint32_t name_token;
    uint32_t argument_first;
    uint32_t argument_end;
    uint32_t argument_count;
    CheckInputAttributeKind kind;
    CheckInputAttributeSchema schema;
    int64_t value;
    int64_t* argument_values;
    uint32_t* argument_ends;
    String8 text;
    bool resolved;
    bool standard;
    bool ignored;
    // Ignored by the subject check (check_input_attribute_subjects) of the declarator that read it last: a record that
    // declaration specifiers' attributes share among their declarators is checked again by each.
    bool subject_ignored;
    // A keyword's record (_Alignas, asm, a calling convention, _Noreturn, a nullability), not an attribute list's item.
    bool keyword;
    // cleanup's function declaration plus one, bound before later declarators can shadow its parsed argument.
    uint32_t cleanup_function;
    // cleanup's argument is an expression other than a name, which clang's handleCleanupAttr says isn't a function: a
    // name in parentheses or a literal (check_input_attribute_validate).
    bool cleanup_expression;
    // cleanup has another count of arguments than one, an error only on a variable it applies to, as clang checks the
    // subject first (check_input_cleanup_declaration).
    bool cleanup_count;
    // An attribute clang applies by its generated handler alone, which has no effect here (#147, CHECK_ATTRIBUTE_UNKNOWN's
    // kind): its argument count against clang's, an error after its subject's check (check_input_attribute_subjects),
    // as BUSTER_CHECK_COUNT_* and the bound.
    uint8_t count_check;
    uint32_t count_bound;
};

// An attribute's count of arguments against clang's (CheckInputAttribute.count_check): none wrong, another than exactly
// the bound, fewer than it, or more.
#define BUSTER_CHECK_COUNT_NONE 0
#define BUSTER_CHECK_COUNT_EXACT 1
#define BUSTER_CHECK_COUNT_LEAST 2
#define BUSTER_CHECK_COUNT_MOST 3

typedef struct CheckInputAttributes CheckInputAttributes;
struct CheckInputAttributes
{
    CheckInputAttribute* records;
    uint32_t count;
    uint32_t capacity;
    uint32_t error_first;
    uint32_t error_end;
    CheckInputAttributeError error;
    uint8_t alignment_log2;
    uint8_t required_alignment_log2;
    CheckInputCallingConvention calling_convention;
    bool packed;
    bool has_alignas;
    // A counted_by family attribute among the records (#148), which a member's declaration checks.
    bool counted;
};

typedef struct CheckInputAttributeRule CheckInputAttributeRule;
struct CheckInputAttributeRule
{
    const char* name;
    CheckInputAttributeKind kind;
    CheckInputAttributeSchema schema;
    uint8_t minimum;
    uint8_t maximum;
};

// These names are an authored classification table, not an imported compiler table. glibc's declarations use the
// diagnostic/optimization attributes below; retaining their exact arguments permits later warning passes without
// confusing them with layout, symbol, TLS or calling-convention attributes. All accepted argument shapes are
// checked. Code-generation/diagnostic consumers must consult the retained kinds; acceptance is not implementation
// of an optimization promise, cleanup action, diagnostic warning, target selection or non-default calling ABI.
static const CheckInputAttributeRule check_input_attribute_rules[] =
{
    { "packed", CHECK_ATTRIBUTE_PACKED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "aligned", CHECK_ATTRIBUTE_ALIGNED, CHECK_ATTRIBUTE_INTEGER, 0, 1 },
    { "mode", CHECK_ATTRIBUTE_MODE, CHECK_ATTRIBUTE_IDENTIFIER, 1, 1 },
    { "vector_size", CHECK_ATTRIBUTE_VECTOR_SIZE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "ext_vector_type", CHECK_ATTRIBUTE_EXT_VECTOR_TYPE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "transparent_union", CHECK_ATTRIBUTE_TRANSPARENT_UNION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "may_alias", CHECK_ATTRIBUTE_MAY_ALIAS, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "visibility", CHECK_ATTRIBUTE_VISIBILITY, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "alias", CHECK_ATTRIBUTE_ALIAS, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "ifunc", CHECK_ATTRIBUTE_IFUNC, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "section", CHECK_ATTRIBUTE_SECTION, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "weak", CHECK_ATTRIBUTE_WEAK, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "weakref", CHECK_ATTRIBUTE_WEAKREF, CHECK_ATTRIBUTE_STRING, 0, 1 },
    { "common", CHECK_ATTRIBUTE_COMMON, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "nocommon", CHECK_ATTRIBUTE_NOCOMMON, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "tls_model", CHECK_ATTRIBUTE_TLS_MODEL, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "cdecl", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "sysv_abi", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "ms_abi", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "stdcall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "fastcall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "thiscall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "pascal", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "vectorcall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "regcall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "preserve_most", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "preserve_all", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "preserve_none", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "regparm", CHECK_ATTRIBUTE_REGPARM, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "noreturn", CHECK_ATTRIBUTE_NORETURN, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "_Noreturn", CHECK_ATTRIBUTE_NORETURN, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "returns_twice", CHECK_ATTRIBUTE_RETURNS_TWICE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "nothrow", CHECK_ATTRIBUTE_NOTHROW, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "const", CHECK_ATTRIBUTE_CONST, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "pure", CHECK_ATTRIBUTE_PURE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "malloc", CHECK_ATTRIBUTE_MALLOC, CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS, 0, 2 },
    { "alloc_size", CHECK_ATTRIBUTE_ALLOC_SIZE, CHECK_ATTRIBUTE_INTEGER_LIST, 1, 2 },
    { "alloc_align", CHECK_ATTRIBUTE_ALLOC_ALIGN, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "assume_aligned", CHECK_ATTRIBUTE_ASSUME_ALIGNED, CHECK_ATTRIBUTE_INTEGER_LIST, 1, 2 },
    { "align_value", CHECK_ATTRIBUTE_ALIGN_VALUE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "nonnull", CHECK_ATTRIBUTE_NONNULL, CHECK_ATTRIBUTE_INTEGER_LIST, 0, 255 },
    { "returns_nonnull", CHECK_ATTRIBUTE_RETURNS_NONNULL, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "format", CHECK_ATTRIBUTE_FORMAT, CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS, 3, 3 },
    { "format_arg", CHECK_ATTRIBUTE_FORMAT_ARG, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "sentinel", CHECK_ATTRIBUTE_SENTINEL, CHECK_ATTRIBUTE_INTEGER_LIST, 0, 2 },
    { "warn_unused_result", CHECK_ATTRIBUTE_WARN_UNUSED_RESULT, CHECK_ATTRIBUTE_STRING, 0, 1 },
    { "nodiscard", CHECK_ATTRIBUTE_WARN_UNUSED_RESULT, CHECK_ATTRIBUTE_STRING, 0, 1 },
    { "deprecated", CHECK_ATTRIBUTE_DEPRECATED, CHECK_ATTRIBUTE_STRING_LIST, 0, 2 },
    { "unavailable", CHECK_ATTRIBUTE_UNAVAILABLE, CHECK_ATTRIBUTE_STRING, 0, 1 },
    { "error", CHECK_ATTRIBUTE_ERROR, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "warning", CHECK_ATTRIBUTE_WARNING, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "hot", CHECK_ATTRIBUTE_HOT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "cold", CHECK_ATTRIBUTE_COLD, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "always_inline", CHECK_ATTRIBUTE_ALWAYS_INLINE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "noinline", CHECK_ATTRIBUTE_NOINLINE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "gnu_inline", CHECK_ATTRIBUTE_GNU_INLINE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "artificial", CHECK_ATTRIBUTE_ARTIFICIAL, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "nodebug", CHECK_ATTRIBUTE_NODEBUG, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "overloadable", CHECK_ATTRIBUTE_OVERLOADABLE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "flatten", CHECK_ATTRIBUTE_FLATTEN, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "leaf", CHECK_ATTRIBUTE_LEAF, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "used", CHECK_ATTRIBUTE_USED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "unused", CHECK_ATTRIBUTE_UNUSED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "maybe_unused", CHECK_ATTRIBUTE_UNUSED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "retain", CHECK_ATTRIBUTE_RETAIN, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "lifetimebound", CHECK_ATTRIBUTE_LIFETIMEBOUND, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "loader_uninitialized", CHECK_ATTRIBUTE_LOADER_UNINITIALIZED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "weak_import", CHECK_ATTRIBUTE_WEAK_IMPORT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "internal_linkage", CHECK_ATTRIBUTE_INTERNAL_LINKAGE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "externally_visible", CHECK_ATTRIBUTE_EXTERNALLY_VISIBLE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "constructor", CHECK_ATTRIBUTE_CONSTRUCTOR, CHECK_ATTRIBUTE_INTEGER, 0, 1 },
    { "destructor", CHECK_ATTRIBUTE_DESTRUCTOR, CHECK_ATTRIBUTE_INTEGER, 0, 1 },
    { "cleanup", CHECK_ATTRIBUTE_CLEANUP, CHECK_ATTRIBUTE_IDENTIFIER, 1, 1 },
    { "access", CHECK_ATTRIBUTE_ACCESS, CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS, 2, 3 },
    { "nonstring", CHECK_ATTRIBUTE_NONSTRING, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "designated_init", CHECK_ATTRIBUTE_DESIGNATED_INIT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "flag_enum", CHECK_ATTRIBUTE_FLAG_ENUM, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "enum_extensibility", CHECK_ATTRIBUTE_ENUM_EXTENSIBILITY, CHECK_ATTRIBUTE_IDENTIFIER, 1, 1 },
    { "target", CHECK_ATTRIBUTE_TARGET, CHECK_ATTRIBUTE_STRING, 1, 1 },
    { "target_clones", CHECK_ATTRIBUTE_TARGET_CLONES, CHECK_ATTRIBUTE_STRING_LIST, 1, 255 },
    // clang's CPU dispatch multiversioning. Its code generation isn't supported yet, but retaining it lets the checker
    // report clang's attribute-compatibility errors at -fsyntax-only, as #839 requires.
    { "cpu_specific", CHECK_ATTRIBUTE_CPU_SPECIFIC, CHECK_ATTRIBUTE_IDENTIFIER_LIST, 1, 255 },
    { "cpu_dispatch", CHECK_ATTRIBUTE_CPU_DISPATCH, CHECK_ATTRIBUTE_IDENTIFIER_LIST, 1, 255 },
    { "min_vector_width", CHECK_ATTRIBUTE_MIN_VECTOR_WIDTH, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    // An expression each, which the member's declaration reads (check_input_counted_by_member).
    { "counted_by", CHECK_ATTRIBUTE_COUNTED_BY, CHECK_ATTRIBUTE_EXPRESSION, 1, 1 },
    { "counted_by_or_null", CHECK_ATTRIBUTE_COUNTED_BY, CHECK_ATTRIBUTE_EXPRESSION, 1, 1 },
    { "sized_by", CHECK_ATTRIBUTE_COUNTED_BY, CHECK_ATTRIBUTE_EXPRESSION, 1, 1 },
    { "sized_by_or_null", CHECK_ATTRIBUTE_COUNTED_BY, CHECK_ATTRIBUTE_EXPRESSION, 1, 1 },
    { "no_sanitize", CHECK_ATTRIBUTE_NO_SANITIZE, CHECK_ATTRIBUTE_STRING_LIST, 1, 255 },
    { "no_sanitize_address", CHECK_ATTRIBUTE_NO_SANITIZE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "no_address_safety_analysis", CHECK_ATTRIBUTE_NO_SANITIZE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "no_sanitize_thread", CHECK_ATTRIBUTE_NO_SANITIZE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "no_sanitize_undefined", CHECK_ATTRIBUTE_NO_SANITIZE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "no_instrument_function", CHECK_ATTRIBUTE_NO_INSTRUMENT_FUNCTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    // clang's: the library functions a definition's code may not become calls of, every one without arguments.
    { "no_builtin", CHECK_ATTRIBUTE_NO_BUILTIN, CHECK_ATTRIBUTE_STRING_LIST, 0, 255 },
    { "naked", CHECK_ATTRIBUTE_NAKED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "no_stack_protector", CHECK_ATTRIBUTE_NO_STACK_PROTECTOR, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "fallthrough", CHECK_ATTRIBUTE_FALLTHROUGH, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    // clang's, on a return statement alone (check_input_statement_attributes, c_check_musttail.c): GNU's spelling and
    // clang::'s scope.
    { "musttail", CHECK_ATTRIBUTE_MUSTTAIL, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    // clang's too, which a statement's attributes take and ignore (check_input_statement_attribute_kinds): nomerge, which
    // only keeps calls from merging, and suppress, the static analyzer's, of string literals. A declaration's are refused.
    { "nomerge", CHECK_ATTRIBUTE_NOMERGE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "suppress", CHECK_ATTRIBUTE_SUPPRESS, CHECK_ATTRIBUTE_STRING_LIST, 0, 255 },
    { "unsequenced", CHECK_ATTRIBUTE_UNSEQUENCED, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "reproducible", CHECK_ATTRIBUTE_REPRODUCIBLE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
};

static bool check_input_spelling(CheckInputReader* reader, uint32_t token, const uint8_t** bytes, uint32_t* length);

static String8 check_input_attribute_name(CheckInputReader* reader, uint32_t name)
{
    const InternTable* table = reader->pp->table;
    String8 result = { 0 };
    if (name != 0 && name < table->count)
    {
        const InternSpan span = table->spans[name];
        result = (String8) { (char*)table->records + span.offset, span.length };
        if (result.length >= 4 && result.pointer[0] == '_' && result.pointer[1] == '_' && result.pointer[result.length - 1] == '_' && result.pointer[result.length - 2] == '_')
        {
            result.pointer += 2;
            result.length -= 4;
        }
        else if (name >= C_NAME_CDECL && name <= C_NAME_VECTORCALL && result.length > 2 && result.pointer[0] == '_' && result.pointer[1] == '_')
        {
            result.pointer += 2;
            result.length -= 2;
        }
    }
    return result;
}

static bool check_input_attribute_equal(String8 text, const char* expected)
{
    const uint64_t length = strlen(expected);
    return text.length == length && (length == 0 || memcmp(text.pointer, expected, length) == 0);
}

static bool check_input_attribute_text_equal(String8 a, String8 b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.pointer, b.pointer, a.length) == 0);
}

static void check_input_attribute_error(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t first, uint32_t end, CheckInputAttributeError error)
{
    if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = error;
        attributes->error_first = first;
        attributes->error_end = end;
        check_input_report(reader, first, error == CHECK_ATTRIBUTE_ERROR_RESOURCE ? CHECK_INPUT_RESOURCE :
                           error == CHECK_ATTRIBUTE_ERROR_UNKNOWN ? CHECK_INPUT_UNSUPPORTED : CHECK_INPUT_DECLARATION);
    }
    reader->output->resource |= error == CHECK_ATTRIBUTE_ERROR_RESOURCE;
}

static bool check_input_attribute_append(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute attribute)
{
    bool valid = true;
    if (attributes->count == attributes->capacity)
    {
        const uint64_t capacity = attributes->capacity != 0 ? (uint64_t)attributes->capacity * 2 : 4;
        CheckInputAttribute* records = capacity <= UINT32_MAX ? arena_push_array(reader->arena, capacity, sizeof(CheckInputAttribute), 8) : 0;
        valid = records != 0;
        if (valid)
        {
            if (attributes->count != 0)
            {
                memcpy(records, attributes->records, (uint64_t)attributes->count * sizeof(CheckInputAttribute));
            }
            attributes->records = records;
            attributes->capacity = (uint32_t)capacity;
        }
        else
        {
            check_input_attribute_error(reader, attributes, attribute.first, attribute.end, CHECK_ATTRIBUTE_ERROR_RESOURCE);
        }
    }
    if (valid)
    {
        attributes->records[attributes->count++] = attribute;
        attributes->counted |= attribute.kind == CHECK_ATTRIBUTE_COUNTED_BY;
    }
    return valid;
}

// Copy a common specifier prefix before adding one declarator's postfix attributes. Otherwise two declaration
// accumulators would share writable capacity, and the next declarator could overwrite the previous one's records.
static bool check_input_attribute_copy(CheckInputReader* reader, const CheckInputAttributes* source, CheckInputAttributes* destination)
{
    *destination = *source;
    destination->records = 0;
    destination->capacity = 0;
    destination->count = 0;
    bool valid = source->error == CHECK_ATTRIBUTE_ERROR_NONE;
    for (uint32_t i = 0; valid && i < source->count; i += 1)
    {
        valid = check_input_attribute_append(reader, destination, source->records[i]);
    }
    return valid;
}

// Both input accumulators are complete and immutable; output must be distinct from them. This combines metadata
// after each syntactic position has applied its own type effects, so a vector is never applied twice. Argument
// arrays and decoded strings are shared immutable arena payloads. Writable record capacity is owned by output.
static bool check_input_attribute_merge(CheckInputReader* reader, const CheckInputAttributes* common, const CheckInputAttributes* suffix, CheckInputAttributes* output)
{
    bool valid = check_input_attribute_copy(reader, common, output) && suffix->error == CHECK_ATTRIBUTE_ERROR_NONE;
    for (uint32_t i = 0; valid && i < suffix->count; i += 1)
    {
        valid = check_input_attribute_append(reader, output, suffix->records[i]);
    }
    if (valid)
    {
        output->packed |= suffix->packed;
        output->has_alignas |= suffix->has_alignas;
        output->alignment_log2 = suffix->alignment_log2 > output->alignment_log2 ? suffix->alignment_log2 : output->alignment_log2;
        output->required_alignment_log2 = suffix->required_alignment_log2 > output->required_alignment_log2 ? suffix->required_alignment_log2 : output->required_alignment_log2;
        // The common one first, as their records are; a conflict is clang's error where they meet a function.
        output->calling_convention = output->calling_convention != CHECK_CALL_DEFAULT ? output->calling_convention : suffix->calling_convention;
    }
    return valid;
}

static uint32_t check_input_attribute_after_group(const Parser* parser, uint32_t token, uint32_t end)
{
    uint32_t result = token + 1;
    const uint8_t kind = parser->tokens.kinds[token];
    if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
    {
        const uint32_t close = parser->tree.parents[token];
        result = close > token && close < end ? close + 1 : end + 1;
    }
    return result;
}

// Half-open source range, excluding the separating comma. Only validated lists publish their boundary table.
static bool check_input_attribute_argument(const CheckInputAttribute* attribute, uint32_t wanted, uint32_t* first, uint32_t* end)
{
    uint32_t begin = attribute->argument_first;
    uint32_t after = attribute->argument_end;
    bool valid = wanted < attribute->argument_count && (attribute->argument_count <= 1 || attribute->argument_ends != 0);
    if (valid && wanted != 0)
    {
        begin = attribute->argument_ends[wanted - 1] + 1;
    }
    if (valid && wanted + 1 < attribute->argument_count)
    {
        after = attribute->argument_ends[wanted];
    }
    valid &= after > begin;
    *first = begin;
    *end = after;
    return valid;
}

// Ordinary string tokens may concatenate. The shared literal decoder retains embedded NULs and UTF-8/escapes;
// consumers must not treat these bounded payloads as C strings. Decoded bytes live in the output arena, while the
// spelling views and the one-part-per-token array are scratch storage and disappear after this operation.
static bool check_input_attribute_string(CheckInputReader* reader, uint32_t first, uint32_t end, String8* text)
{
    const uint64_t mark = reader->scratch->length;
    bool valid = first < end;
    CheckStringPart* parts = valid ? arena_push_array(reader->scratch, end - first, sizeof(CheckStringPart), 8) : 0;
    reader->output->resource |= valid && parts == 0;
    valid &= parts != 0;
    for (uint32_t i = first; valid && i < end; i += 1)
    {
        valid = reader->parser->tokens.kinds[i] == PARSE_KIND_STRING;
        if (valid)
        {
            valid = check_input_spelling(reader, i, &parts[i - first].bytes, &parts[i - first].length);
            reader->output->resource |= !valid;
        }
        if (valid)
        {
            const CheckStringSpan span = check_literal_string_span(&parts[i - first]);
            valid = span.valid && span.encoding == CHECK_STRING_ORDINARY;
        }
    }
    if (valid)
    {
        const CheckStringLiteral decoded = check_literal_strings(parts, end - first, reader->pp->options.language.standard >= 2023, reader->arena);
        reader->output->resource |= decoded.resource;
        valid = decoded.valid && decoded.byte_count != 0;
        if (valid)
        {
            *text = (String8) { (char*)decoded.bytes, decoded.byte_count - 1 };
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

// clang's name of a calling convention (FunctionType::getNameForCallConv): on x86-64 Linux, sysv_abi's and those it
// hasn't (which it takes for the default, CHECK_CALL_IGNORED_X86_32) are cdecl's.
static const char* check_input_convention_name(uint32_t call)
{
    const char* result = call == CHECK_CALL_MICROSOFT ? "ms_abi" : call == CHECK_CALL_VECTOR ? "vectorcall" : call == CHECK_CALL_REG ? "regcall" :
                         call == CHECK_CALL_PRESERVE_MOST ? "preserve_most" : call == CHECK_CALL_PRESERVE_ALL ? "preserve_all" :
                         call == CHECK_CALL_PRESERVE_NONE ? "preserve_none" : "cdecl";
    return result;
}

// clang's error for a calling convention a function meets with another (err_attributes_are_not_compatible): at
// `attribute`'s first token, which asks for `call` where the function has `previous`, "A and B attributes are not
// compatible" (check_input_convention_name).
static void check_input_attribute_conflict(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t call, uint32_t previous)
{
    if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_CONFLICT;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_CONVENTIONS, (call & 0xffu) | (previous & 0xffu) << 8);
    }
}

static CheckInputCallingConvention check_input_attribute_call(String8 name)
{
    CheckInputCallingConvention result = CHECK_CALL_IGNORED_X86_32;
    if (check_input_attribute_equal(name, "cdecl") || check_input_attribute_equal(name, "sysv_abi"))
    {
        result = CHECK_CALL_SYSV;
    }
    else if (check_input_attribute_equal(name, "ms_abi"))
    {
        result = CHECK_CALL_MICROSOFT;
    }
    else if (check_input_attribute_equal(name, "vectorcall"))
    {
        result = CHECK_CALL_VECTOR;
    }
    else if (check_input_attribute_equal(name, "regcall"))
    {
        result = CHECK_CALL_REG;
    }
    else if (check_input_attribute_equal(name, "preserve_most"))
    {
        result = CHECK_CALL_PRESERVE_MOST;
    }
    else if (check_input_attribute_equal(name, "preserve_all"))
    {
        result = CHECK_CALL_PRESERVE_ALL;
    }
    else if (check_input_attribute_equal(name, "preserve_none"))
    {
        result = CHECK_CALL_PRESERVE_NONE;
    }
    return result;
}

static bool check_input_attribute_value(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t index, int64_t value)
{
    bool valid = index < attributes->count;
    if (valid)
    {
        CheckInputAttribute* attribute = &attributes->records[index];
        CheckInputAttributeError error = CHECK_ATTRIBUTE_ERROR_INTEGER;
        if (attribute->kind == CHECK_ATTRIBUTE_ALIGNED || attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
        {
            valid = value >= 0 && (value != 0 || attribute->kind == CHECK_ATTRIBUTE_ALIGNAS) &&
                    (uint64_t)value <= (UINT64_C(1) << BUSTER_TYPE_MAX_ALIGNMENT_LOG2) && (value == 0 || ((uint64_t)value & ((uint64_t)value - 1)) == 0);
            error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT;
            if (valid && value != 0)
            {
                const uint8_t alignment = (uint8_t)__builtin_ctzll((uint64_t)value) + 1;
                attributes->alignment_log2 = alignment > attributes->alignment_log2 ? alignment : attributes->alignment_log2;
                if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
                {
                    attributes->required_alignment_log2 = alignment > attributes->required_alignment_log2 ? alignment : attributes->required_alignment_log2;
                }
            }
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
        {
            valid = value > 0 && (uint64_t)value <= BUSTER_TYPE_MAX_VECTOR_BITS;
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE)
        {
            // Checked with its element type (check_input_vector_size, #147), as clang's BuildVectorType checks it.
            valid = true;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_REGPARM)
        {
            // Clang's x86-64 target accepts all six integer argument registers here; i386's limit is three.
            valid = value >= 0 && value <= 6;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_CONSTRUCTOR || attribute->kind == CHECK_ATTRIBUTE_DESTRUCTOR)
        {
            // Clang accepts a signed or unsigned 32-bit priority and retains its uint32_t representation.
            valid = value >= INT32_MIN && value <= UINT32_MAX;
            value = (uint32_t)value;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_ALLOC_ALIGN || attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG)
        {
            valid = value > 0 && value <= UINT32_MAX;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_ALIGN_VALUE)
        {
            // A pointer's promised alignment: any power of two, past the alignments an object may have. Clang only
            // warns where its declaration isn't a pointer variable or typedef, and ignores it there.
            valid = value != 0 && ((uint64_t)value & ((uint64_t)value - 1)) == 0;
            error = CHECK_ATTRIBUTE_ERROR_ALIGNMENT;
        }
        else
        {
            valid = value >= 0 && value <= UINT32_MAX;
        }
        if (valid)
        {
            attribute->value = value;
            attribute->resolved = true;
        }
        else
        {
            check_input_attribute_error(reader, attributes, attribute->argument_first, attribute->argument_end, error);
        }
    }
    return valid;
}

// Numeric argument lists (format, nonnull, allocation size/alignment, sentinel) use the same declaration ICE
// evaluator as a single aligned argument. Retain one value per source argument; the leading identifier's slot is
// unused. The caller must evaluate each numeric argument once before publishing these records to consumers.
static bool check_input_attribute_argument_value(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t index, uint32_t argument, int64_t value)
{
    bool valid = index < attributes->count;
    if (valid)
    {
        CheckInputAttribute* attribute = &attributes->records[index];
        valid = argument < attribute->argument_count && !(attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && argument == 0);
        if (valid && attribute->schema == CHECK_ATTRIBUTE_INTEGER)
        {
            valid = check_input_attribute_value(reader, attributes, index, value);
            value = attribute->value;
        }
        else if (valid)
        {
            // nonnull's and alloc_size's parameter indices are any integers here: the function's check refuses one of no
            // parameter with clang's "parameter N is out of bounds" (check_input_attribute_function).
            const bool index_kind = attribute->kind == CHECK_ATTRIBUTE_NONNULL || attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE;
            valid = index_kind || (value >= 0 && value <= UINT32_MAX);
            if (valid && attribute->kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && argument == 0)
            {
                valid = value != 0 && ((uint64_t)value & ((uint64_t)value - 1)) == 0;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_SENTINEL && argument == 1)
            {
                valid = value <= 1;
            }
        }
        if (valid && attribute->argument_values == 0)
        {
            attribute->argument_values = arena_push_array(reader->arena, attribute->argument_count, sizeof(int64_t), 8);
            valid = attribute->argument_values != 0;
            reader->output->resource |= !valid;
            if (valid)
            {
                memset(attribute->argument_values, 0, (uint64_t)attribute->argument_count * sizeof(int64_t));
            }
        }
        if (valid)
        {
            attribute->argument_values[argument] = value;
            attribute->value = argument == 0 ? value : attribute->value;
            attribute->resolved |= argument + 1 == attribute->argument_count;
        }
        else if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
        {
            uint32_t first = attribute->argument_first;
            uint32_t end = attribute->argument_end;
            check_input_attribute_argument(attribute, argument, &first, &end);
            check_input_attribute_error(reader, attributes, first, end, reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : CHECK_ATTRIBUTE_ERROR_INTEGER);
        }
    }
    return valid;
}

static bool check_input_attribute_validate(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t minimum, uint32_t maximum)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    uint32_t begin = attribute->argument_first;
    uint32_t i = begin;
    uint32_t count = 0;
    while (valid && i < attribute->argument_end)
    {
        if (parser->tokens.kinds[i] == PARSE_KIND_COMMA)
        {
            valid = i > begin;
            count += 1;
            begin = ++i;
        }
        else
        {
            i = check_input_attribute_after_group(parser, i, attribute->argument_end);
            valid = i <= attribute->argument_end;
        }
    }
    valid &= begin == attribute->argument_first || begin < attribute->argument_end;
    count += begin < attribute->argument_end;
    attribute->argument_count = count;
    // Whether a cleanup attribute's count is an error depends on its declaration's subject.
    attribute->cleanup_count = valid && attribute->kind == CHECK_ATTRIBUTE_CLEANUP && count != 1;
    valid &= attribute->cleanup_count || (count >= minimum && (maximum == 255 || count <= maximum));
    CheckInputAttributeError error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
    if (valid && count > 1)
    {
        attribute->argument_ends = arena_push_array(reader->arena, count - 1, sizeof(uint32_t), 4);
        reader->output->resource |= attribute->argument_ends == 0;
        valid = attribute->argument_ends != 0;
    }
    begin = attribute->argument_first;
    for (uint32_t argument = 0; valid && argument < count; argument += 1)
    {
        const uint32_t first = begin;
        uint32_t end = attribute->argument_end;
        if (argument + 1 < count)
        {
            i = begin;
            while (i < attribute->argument_end && parser->tokens.kinds[i] != PARSE_KIND_COMMA)
            {
                i = check_input_attribute_after_group(parser, i, attribute->argument_end);
            }
            end = i;
            attribute->argument_ends[argument] = end;
            begin = end + 1;
        }
        // cleanup's argument is an expression, which its own check below reads.
        const bool identifier = (attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER && attribute->kind != CHECK_ATTRIBUTE_CLEANUP) ||
                                attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_LIST ||
                                (attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && argument == 0);
        const bool string = attribute->schema == CHECK_ATTRIBUTE_STRING || attribute->schema == CHECK_ATTRIBUTE_STRING_LIST;
        if (valid && identifier)
        {
            valid = first + 1 == end && parser->tokens.names[first] != 0;
            error = CHECK_ATTRIBUTE_ERROR_IDENTIFIER;
        }
        else if (valid && string)
        {
            String8 text = { 0 };
            valid = check_input_attribute_string(reader, first, end, &text);
            error = CHECK_ATTRIBUTE_ERROR_STRING;
            if (valid && argument == 0)
            {
                attribute->text = text;
            }
        }
        else if (valid)
        {
            // The shared ICE reader validates expression semantics once the declaration's scope is available.
            valid = first < end;
        }
    }
    if (valid && attribute->kind == CHECK_ATTRIBUTE_CLEANUP && !attribute->cleanup_count)
    {
        // clang parses the argument as an expression, whose names it looks up where it is, on ignored subjects and in
        // earlier duplicate attributes too: an undeclared one is the bindings' error. Its handleCleanupAttr takes a
        // function's name; any other expression isn't a function. Of those, a name in parentheses and a literal are taken
        // (cleanup_expression: the error is at the expression's first token, as clang's); any other is refused here.
        uint32_t token = attribute->argument_first;
        uint32_t last = attribute->argument_end - 1;
        while (token < last && parser->tokens.kinds[token] == PARSE_KIND_L_PAREN && parser->tree.parents[token] == last)
        {
            token += 1;
            last -= 1;
        }
        const uint8_t kind = parser->tokens.kinds[token];
        const bool named = token == last && kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[token] != 0;
        const bool literal = token == last && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_STRING || kind == PARSE_KIND_CHARACTER);
        const uint32_t name = named ? parser->tokens.names[token] : 0;
        const uint32_t handle = named ? check_input_library_binding(reader, name, token) : 0;
        valid = literal || (named && c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, token) != 0);
        reader->output->resource |= named && !valid;
        error = CHECK_ATTRIBUTE_ERROR_IDENTIFIER;
        attribute->cleanup_function = handle != 0 && token == attribute->argument_first ? reader->bindings.declaration[handle - 1] + 1u : 0;
        attribute->cleanup_expression = valid && (literal || (handle != 0 && token != attribute->argument_first));
        if (valid && handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF)
        {
            // Keep the typed rejection as the one primary; the generic malformed-attribute fallback must not add one.
            attributes->error = CHECK_ATTRIBUTE_ERROR_IDENTIFIER;
            attributes->error_first = token;
            attributes->error_end = token + 1u;
            check_input_report(reader, token, CHECK_INPUT_CLEANUP_FUNCTION);
            valid = false;
        }
    }
    if (valid && attribute->kind == CHECK_ATTRIBUTE_VISIBILITY)
    {
        // Clang warns and gives an unknown visibility value no effect. Preserve that deliberate disposition so
        // linkage consumers cannot accidentally interpret an arbitrary string as a supported visibility mode.
        attribute->ignored = !check_input_attribute_equal(attribute->text, "default") && !check_input_attribute_equal(attribute->text, "hidden") &&
                             !check_input_attribute_equal(attribute->text, "protected") && !check_input_attribute_equal(attribute->text, "internal");
    }
    if (valid && attribute->kind == CHECK_ATTRIBUTE_TLS_MODEL)
    {
        valid = check_input_attribute_equal(attribute->text, "global-dynamic") || check_input_attribute_equal(attribute->text, "local-dynamic") ||
                check_input_attribute_equal(attribute->text, "initial-exec") || check_input_attribute_equal(attribute->text, "local-exec");
        error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
    }
    if (!valid)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : error);
    }
    return valid;
}

// Declaration attributes clang drops when first added after a definition. Unscoped [[noreturn]] and [[_Noreturn]]
// are declaration attributes; GNU noreturn (including [[gnu::noreturn]]) changes the function type instead, and C's
// _Noreturn keyword is explicitly allowed after a definition. A scoped list item keeps first at its namespace and
// name_token at its name, so standard syntax alone does not identify the declaration attribute.
static bool check_input_attribute_late_kind(const CheckInputAttribute* attribute)
{
    const CheckInputAttributeKind kind = attribute->kind;
    const bool standard_noreturn = kind == CHECK_ATTRIBUTE_NORETURN && attribute->standard && !attribute->keyword &&
                                  attribute->first == attribute->name_token;
    return kind == CHECK_ATTRIBUTE_WEAK || kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
           kind == CHECK_ATTRIBUTE_USED || kind == CHECK_ATTRIBUTE_RETAIN || kind == CHECK_ATTRIBUTE_GNU_INLINE || standard_noreturn;
}

// One bit per late attribute class. Standard and GNU noreturn are different classes.
static uint8_t check_input_attribute_late_bit(const CheckInputAttribute* attribute)
{
    uint8_t bit = 0;
    if (check_input_attribute_late_kind(attribute))
    {
        switch (attribute->kind)
        {
            case CHECK_ATTRIBUTE_WEAK: bit = 1u << 0; break;
            case CHECK_ATTRIBUTE_CONSTRUCTOR: bit = 1u << 1; break;
            case CHECK_ATTRIBUTE_DESTRUCTOR: bit = 1u << 2; break;
            case CHECK_ATTRIBUTE_USED: bit = 1u << 3; break;
            case CHECK_ATTRIBUTE_RETAIN: bit = 1u << 4; break;
            case CHECK_ATTRIBUTE_GNU_INLINE: bit = 1u << 5; break;
            case CHECK_ATTRIBUTE_NORETURN: bit = 1u << 6; break;
            default: break;
        }
    }
    return bit;
}

static uint8_t check_input_attribute_late_mask(const CheckInputAttributes* attributes, uint8_t function_flags)
{
    uint8_t mask = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool missing_inline = attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE && (function_flags & CHECK_INPUT_FUNCTION_INLINE) == 0;
        mask |= !attribute->ignored && !missing_inline ? check_input_attribute_late_bit(attribute) : 0;
    }
    return mask;
}

// clang's musttail attribute (#147) as its attribute's reading has it, clang's errors at the attribute (its scope's name
// for clang::musttail): one of arguments, which it takes none of, and one of its place anywhere but a statement's
// attributes (CheckInputReader.statement_attributes), a declaration's (a label's among them). Its statement is a return's,
// or another's (check_input_statement_attributes). Whether it is valid; an error reported is the attributes' (no second
// one for them).
static bool check_input_attribute_musttail(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const bool arguments = attribute->argument_end > attribute->argument_first;
    const bool valid = !arguments && reader->statement_attributes;
    if (!valid)
    {
        check_input_report(reader, attribute->first, arguments ? CHECK_INPUT_MUSTTAIL_ARGUMENTS : CHECK_INPUT_MUSTTAIL_DECLARATION);
        if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
        {
            attributes->error = arguments ? CHECK_ATTRIBUTE_ERROR_ARGUMENTS : CHECK_ATTRIBUTE_ERROR_SUBJECT;
            attributes->error_first = attribute->first;
            attributes->error_end = attribute->end;
        }
    }
    return valid;
}

// The counted_by family's argument as clang's parser reads it (#148, ParseBoundsAttribute): one expression in
// parentheses. Without them, its count's error ("takes at least 1 argument"), at the name; with nothing in them, "argument
// required after attribute" at the `)`; with a second argument, "expected ')'" at the `,` (a note at the `(`). The
// expression itself the member's declaration checks (check_input_counted_by_member).
static bool check_input_attribute_counted_shape(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t first = attribute->argument_first;
    const uint32_t end = attribute->argument_end;
    const bool parenthesized = first > 0 && parser->tokens.kinds[first - 1] == PARSE_KIND_L_PAREN && end < parser->tokens.count &&
                               parser->tokens.kinds[end] == PARSE_KIND_R_PAREN;
    uint32_t comma = BUSTER_CHECK_NONE;
    for (uint32_t i = first; comma == BUSTER_CHECK_NONE && i < end; i = check_input_attribute_after_group(parser, i, end))
    {
        comma = parser->tokens.kinds[i] == PARSE_KIND_COMMA ? i : BUSTER_CHECK_NONE;
    }
    const bool valid = parenthesized && first < end && comma == BUSTER_CHECK_NONE;
    if (!parenthesized)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_LEAST | 1u << 8);
    }
    else if (first == end)
    {
        check_input_report(reader, end, CHECK_INPUT_COUNTED_REQUIRED);
    }
    else if (comma != BUSTER_CHECK_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, comma, CHECK_INPUT_COUNTED_PAREN);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].note_token = first - 1;
            reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_MATCH_PAREN;
        }
    }
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    attribute->argument_count = valid ? 1 : 0;
    return valid;
}

// The GNU attributes clang 23's Sema refuses on a label (#147), each with the end of its message, "'name' attribute ...":
// those whose subjects clang lists without a label among them, and fallthrough, which can't be applied to a declaration.
// clang takes the others there: unused, the ones it ignores without a word (aligned, deprecated, vector_size and the
// like), and those it ignores with a warning that they apply to other subjects (hot, cold, noreturn and the like), which
// the checker doesn't model. musttail's error is its own (check_input_attribute_musttail).
typedef struct CheckInputLabelRefusal CheckInputLabelRefusal;
struct CheckInputLabelRefusal
{
    const char* name;
    const char* message;
};

static const CheckInputLabelRefusal check_input_label_refusals[] = {
    { "alias", "only applies to functions and global variables" },
    { "assume", "cannot be applied to a declaration" },
    { "btf_decl_tag", "only applies to variables, functions, structs, unions, classes, non-static data members, and typedefs" },
    { "counted_by", "only applies to non-static data members" },
    { "error", "only applies to functions" },
    { "fallthrough", "cannot be applied to a declaration" },
    { "flatten", "only applies to functions" },
    { "lifetimebound", "only applies to parameters and implicit object parameters" },
    { "min_vector_width", "only applies to functions" },
    { "minsize", "only applies to functions and Objective-C methods" },
    { "mode", "only applies to variables, enums, typedefs, and non-static data members" },
    { "no_address_safety_analysis", "only applies to functions and global variables" },
    { "no_sanitize", "only applies to functions, Objective-C methods, and global variables" },
    { "no_sanitize_address", "only applies to functions and global variables" },
    { "no_sanitize_memory", "only applies to functions" },
    { "no_sanitize_thread", "only applies to functions" },
    { "no_split_stack", "only applies to functions" },
    { "nomerge", "only applies to functions, statements and variables" },
    { "overloadable", "only applies to functions" },
    { "preferred_type", "only applies to bit-field data members" },
    { "section", "only applies to functions, global variables, Objective-C methods, and Objective-C properties" },
    { "suppress", "only applies to variables, functions, structs, interfaces, and namespaces" },
    { "target", "only applies to functions" },
    { "target_clones", "only applies to functions" },
    { "tls_model", "only applies to thread-local variables" },
    { "warning", "only applies to functions" },
    { "weakref", "only applies to variables and functions" },
    { "zero_call_used_regs", "only applies to functions" },
};

// The end of clang's error for attribute `name` (its spelling without a GNU keyword's underscores) on a label, or null for
// an attribute clang takes there.
static const char* check_input_label_refusal(String8 name)
{
    const char* result = NULL;
    for (uint32_t i = 0; result == NULL && i < sizeof(check_input_label_refusals) / sizeof(check_input_label_refusals[0]); i += 1)
    {
        result = check_input_attribute_equal(name, check_input_label_refusals[i].name) ? check_input_label_refusals[i].message : NULL;
    }
    return result;
}

// clang 23's attribute vocabulary for C on x86-64 Linux (#147, c_check_attribute_clang.inc): the names of its GNU spellings,
// and its C23 spellings (`name`, `scope::name`), each in byte order.
typedef struct CheckInputClangAttribute CheckInputClangAttribute;
struct CheckInputClangAttribute
{
    const char* name;
    uint32_t length;
};

#define BUSTER_CHECK_CLANG_ENTRY(text) { text, sizeof(text) - 1 },
#define BUSTER_CHECK_CLANG_NONE(text)

static const CheckInputClangAttribute check_input_clang_gnu_attributes[] =
{
#define BUSTER_CHECK_CLANG_GNU_ATTRIBUTE BUSTER_CHECK_CLANG_ENTRY
#define BUSTER_CHECK_CLANG_C23_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SPELLING BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SCOPE BUSTER_CHECK_CLANG_NONE
#include "c_check_attribute_clang.inc"
#undef BUSTER_CHECK_CLANG_SCOPE
#undef BUSTER_CHECK_CLANG_SPELLING
#undef BUSTER_CHECK_CLANG_C23_ATTRIBUTE
#undef BUSTER_CHECK_CLANG_GNU_ATTRIBUTE
};

static const CheckInputClangAttribute check_input_clang_c23_attributes[] =
{
#define BUSTER_CHECK_CLANG_GNU_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_C23_ATTRIBUTE BUSTER_CHECK_CLANG_ENTRY
#define BUSTER_CHECK_CLANG_SPELLING BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SCOPE BUSTER_CHECK_CLANG_NONE
#include "c_check_attribute_clang.inc"
#undef BUSTER_CHECK_CLANG_SCOPE
#undef BUSTER_CHECK_CLANG_SPELLING
#undef BUSTER_CHECK_CLANG_C23_ATTRIBUTE
#undef BUSTER_CHECK_CLANG_GNU_ATTRIBUTE
};

// clang's attribute spellings and scopes (AttributeSpellingList.inc), every syntax's, which its suggestion for an unknown
// attribute's name or scope searches in this order.
static const CheckInputClangAttribute check_input_clang_spellings[] =
{
#define BUSTER_CHECK_CLANG_GNU_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_C23_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SPELLING BUSTER_CHECK_CLANG_ENTRY
#define BUSTER_CHECK_CLANG_SCOPE BUSTER_CHECK_CLANG_NONE
#include "c_check_attribute_clang.inc"
#undef BUSTER_CHECK_CLANG_SCOPE
#undef BUSTER_CHECK_CLANG_SPELLING
#undef BUSTER_CHECK_CLANG_C23_ATTRIBUTE
#undef BUSTER_CHECK_CLANG_GNU_ATTRIBUTE
};

static const CheckInputClangAttribute check_input_clang_scopes[] =
{
#define BUSTER_CHECK_CLANG_GNU_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_C23_ATTRIBUTE BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SPELLING BUSTER_CHECK_CLANG_NONE
#define BUSTER_CHECK_CLANG_SCOPE BUSTER_CHECK_CLANG_ENTRY
#include "c_check_attribute_clang.inc"
#undef BUSTER_CHECK_CLANG_SCOPE
#undef BUSTER_CHECK_CLANG_SPELLING
#undef BUSTER_CHECK_CLANG_C23_ATTRIBUTE
#undef BUSTER_CHECK_CLANG_GNU_ATTRIBUTE
};

#undef BUSTER_CHECK_CLANG_NONE
#undef BUSTER_CHECK_CLANG_ENTRY

// The C declarations clang's attribute subjects name (#147, c_check_attribute_subjects.inc, which
// tools/generate_attribute_subjects.c makes from clang's Attr.td): a declaration is one or more of them, an attribute applies
// to any of its row's. A function is FUNCTION_LIKE too, and PROTOTYPED with a prototype; a variable, a parameter, a member
// or a typedef of a function's type or a pointer to one is FUNCTION_LIKE (and FUNCTION_POINTER of the pointer), PROTOTYPED
// with its prototype (clang's getFunctionType). A variable of static or thread storage is GLOBAL (and SHARED without a
// thread's, EXTERNAL_GLOBAL at file scope without static), one of automatic storage LOCAL.
#define BUSTER_CHECK_SUBJECT_FUNCTION 0x1u
#define BUSTER_CHECK_SUBJECT_VARIABLE 0x2u
#define BUSTER_CHECK_SUBJECT_PARAMETER 0x4u
#define BUSTER_CHECK_SUBJECT_FIELD 0x8u
#define BUSTER_CHECK_SUBJECT_STRUCT 0x10u
#define BUSTER_CHECK_SUBJECT_UNION 0x20u
#define BUSTER_CHECK_SUBJECT_ENUM 0x40u
#define BUSTER_CHECK_SUBJECT_TYPEDEF 0x80u
#define BUSTER_CHECK_SUBJECT_ENUMERATOR 0x100u
#define BUSTER_CHECK_SUBJECT_LABEL 0x200u
#define BUSTER_CHECK_SUBJECT_PROTOTYPED 0x400u
#define BUSTER_CHECK_SUBJECT_FUNCTION_LIKE 0x800u
#define BUSTER_CHECK_SUBJECT_FUNCTION_POINTER 0x1000u
#define BUSTER_CHECK_SUBJECT_GLOBAL 0x2000u
#define BUSTER_CHECK_SUBJECT_SHARED 0x4000u
#define BUSTER_CHECK_SUBJECT_EXTERNAL_GLOBAL 0x8000u
#define BUSTER_CHECK_SUBJECT_LOCAL 0x10000u
#define BUSTER_CHECK_SUBJECT_NON_PARAMETER_VARIABLE 0x20000u
#define BUSTER_CHECK_SUBJECT_THREAD_LOCAL 0x40000u
#define BUSTER_CHECK_SUBJECT_INLINE 0x80000u
#define BUSTER_CHECK_SUBJECT_BIT_FIELD 0x100000u
#define BUSTER_CHECK_SUBJECT_NON_BIT_FIELD 0x200000u
// Of no generated row, for the handlers' own subjects (check_input_subject_handlers): a typedef of a union's type, which
// transparent_union takes beside a union, and a variable or parameter of a function pointer's type, which sentinel takes.
#define BUSTER_CHECK_SUBJECT_UNION_TYPEDEF 0x400000u
#define BUSTER_CHECK_SUBJECT_FUNCTION_POINTER_VARIABLE 0x800000u
// A variable's declaration that defines it, tentatively or not (#147): no table row's subject, but weak_import's handler's
// (clang's canBeWeakImported).
#define BUSTER_CHECK_SUBJECT_DEFINITION 0x1000000u

typedef struct CheckInputSubjectRow CheckInputSubjectRow;
struct CheckInputSubjectRow
{
    const char* spelling;
    uint32_t length;
    uint32_t subjects;
    bool error;
    const char* message;
};

// The subjects of attributes that clang's handlers check themselves, rather than its generated diagAppertainsToDecl,
// as rows, in byte order (check_input_subject_row): GCC's, GNU's spelling and gnu::'s, handleSentinelAttr's and
// handleTransparentUnionAttr's; and clang's own, GNU's spelling and clang::'s, handleWeakImportAttr's (#147), whose
// definitions it ignores with its own warning (check_input_attribute_subjects).
static const CheckInputSubjectRow check_input_subject_handlers[] =
{
    { "sentinel", 8, BUSTER_CHECK_SUBJECT_FUNCTION | BUSTER_CHECK_SUBJECT_FUNCTION_POINTER_VARIABLE, false, "functions, methods and blocks" },
    { "transparent_union", 17, BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_UNION_TYPEDEF, false, "unions" },
};

static const CheckInputSubjectRow check_input_subject_clang_handlers[] =
{
    { "weak_import", 11, BUSTER_CHECK_SUBJECT_FUNCTION | BUSTER_CHECK_SUBJECT_VARIABLE, false, "variables and functions" },
};

#define BUSTER_CHECK_FACT_GNU(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_FACT_C23(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_EXCLUSION(spelling, excluded)
static const CheckInputSubjectRow check_input_subject_gnu[] =
{
#define BUSTER_CHECK_SUBJECT_GNU(spelling, subjects, error, message) { spelling, sizeof(spelling) - 1, subjects, error, message },
#define BUSTER_CHECK_SUBJECT_C23(spelling, subjects, error, message)
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_SUBJECT_C23
#undef BUSTER_CHECK_SUBJECT_GNU
};

static const CheckInputSubjectRow check_input_subject_c23[] =
{
#define BUSTER_CHECK_SUBJECT_GNU(spelling, subjects, error, message)
#define BUSTER_CHECK_SUBJECT_C23(spelling, subjects, error, message) { spelling, sizeof(spelling) - 1, subjects, error, message },
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_SUBJECT_C23
#undef BUSTER_CHECK_SUBJECT_GNU
};
#undef BUSTER_CHECK_FACT_C23
#undef BUSTER_CHECK_FACT_GNU

// Each spelling's facts (c_check_attribute_subjects.inc's BUSTER_CHECK_FACT_* rows, of clang's ParsedAttrInfo and
// attribute kinds for x86-64 Linux): GCC knows the attribute, it isn't C's, clang's generated handler applies it with
// nothing more, it is a type's or a statement's, its arguments are its own parsing's, its last one is variadic, or clang
// ignores it wherever it is; and its least and most arguments (UINT32_MAX for any).
#define BUSTER_CHECK_FACT_GCC 0x1u
#define BUSTER_CHECK_FACT_NOT_C 0x2u
#define BUSTER_CHECK_FACT_SIMPLE 0x4u
#define BUSTER_CHECK_FACT_TYPE 0x8u
#define BUSTER_CHECK_FACT_STATEMENT 0x10u
#define BUSTER_CHECK_FACT_CUSTOM_PARSING 0x20u
#define BUSTER_CHECK_FACT_VARIADIC 0x40u
#define BUSTER_CHECK_FACT_IGNORED 0x80u

typedef struct CheckInputFactRow CheckInputFactRow;
struct CheckInputFactRow
{
    const char* spelling;
    uint32_t length;
    uint32_t flags;
    uint32_t minimum;
    uint32_t maximum;
};

#define BUSTER_CHECK_SUBJECT_GNU(spelling, subjects, error, message)
#define BUSTER_CHECK_SUBJECT_C23(spelling, subjects, error, message)
static const CheckInputFactRow check_input_fact_gnu[] =
{
#define BUSTER_CHECK_FACT_GNU(spelling, flags, minimum, maximum) { spelling, sizeof(spelling) - 1, flags, minimum, maximum },
#define BUSTER_CHECK_FACT_C23(spelling, flags, minimum, maximum)
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_FACT_C23
#undef BUSTER_CHECK_FACT_GNU
};

static const CheckInputFactRow check_input_fact_c23[] =
{
#define BUSTER_CHECK_FACT_GNU(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_FACT_C23(spelling, flags, minimum, maximum) { spelling, sizeof(spelling) - 1, flags, minimum, maximum },
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_FACT_C23
#undef BUSTER_CHECK_FACT_GNU
};
#undef BUSTER_CHECK_EXCLUSION

// Two attributes clang's generated diagMutualExclusion makes an error on one declaration (#147,
// c_check_attribute_subjects.inc's BUSTER_CHECK_EXCLUSION rows): `spelling`'s, which `excluded` already there excludes,
// by their GNU spellings, in byte order.
typedef struct CheckInputExclusionRow CheckInputExclusionRow;
struct CheckInputExclusionRow
{
    const char* spelling;
    uint32_t length;
    const char* excluded;
    uint32_t excluded_length;
};

static const CheckInputExclusionRow check_input_exclusions[] =
{
#define BUSTER_CHECK_FACT_GNU(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_FACT_C23(spelling, flags, minimum, maximum)
#define BUSTER_CHECK_EXCLUSION(spelling, excluded) { spelling, sizeof(spelling) - 1, excluded, sizeof(excluded) - 1 },
#include "c_check_attribute_subjects.inc"
#undef BUSTER_CHECK_EXCLUSION
#undef BUSTER_CHECK_FACT_C23
#undef BUSTER_CHECK_FACT_GNU
};
#undef BUSTER_CHECK_SUBJECT_C23
#undef BUSTER_CHECK_SUBJECT_GNU

// The row of `count` in `list` (in byte order) whose spelling is `text`, or null: a binary search.
static const CheckInputSubjectRow* check_input_subject_find(const CheckInputSubjectRow* list, uint32_t count, String8 text)
{
    const CheckInputSubjectRow* result = NULL;
    uint32_t lower = 0;
    uint32_t upper = count;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const uint32_t length = list[middle].length;
        const uint64_t common = text.length < length ? text.length : length;
        int order = common != 0 ? memcmp(text.pointer, list[middle].spelling, common) : 0;
        order = order != 0 ? order : (text.length > length) - (text.length < length);
        result = order == 0 ? list + middle : result;
        upper = order <= 0 ? middle : upper;
        lower = order > 0 ? middle + 1 : lower;
    }
    return result;
}

// The subject row of an attribute: a GNU spelling's `name` (its surrounding underscores gone) or a C23 one's, its scope
// normalized as clang's (gnu, clang for clang and _Clang), or null for none (an attribute whose subjects clang's own
// handler checks, a type's among them).
static const CheckInputSubjectRow* check_input_subject_row(String8 scope, String8 name, bool scoped, bool standard)
{
    const CheckInputSubjectRow* result = NULL;
    if (!standard)
    {
        result = check_input_subject_find(check_input_subject_gnu, BUSTER_ARRAY_LENGTH(check_input_subject_gnu), name);
    }
    else
    {
        char spelling[64];
        const bool gnu = (scope.length == 3 && memcmp(scope.pointer, "gnu", 3) == 0) || (scope.length == 7 && memcmp(scope.pointer, "__gnu__", 7) == 0);
        const bool clang = (scope.length == 5 && memcmp(scope.pointer, "clang", 5) == 0) || (scope.length == 6 && memcmp(scope.pointer, "_Clang", 6) == 0);
        const String8 normalized = gnu ? (String8){ "gnu", 3 } : clang ? (String8){ "clang", 5 } : scope;
        const uint64_t length = scoped ? normalized.length + 2 + name.length : name.length;
        if (length < sizeof(spelling))
        {
            uint64_t at = 0;
            if (scoped)
            {
                memcpy(spelling, normalized.pointer, normalized.length);
                memcpy(spelling + normalized.length, "::", 2);
                at = normalized.length + 2;
            }
            memcpy(spelling + at, name.pointer, name.length);
            result = check_input_subject_find(check_input_subject_c23, BUSTER_ARRAY_LENGTH(check_input_subject_c23), (String8){ spelling, length });
        }
    }
    const bool gnu_scope = !standard || (scoped && ((scope.length == 3 && memcmp(scope.pointer, "gnu", 3) == 0) || (scope.length == 7 && memcmp(scope.pointer, "__gnu__", 7) == 0)));
    const bool clang_scope = !standard || (scoped && ((scope.length == 5 && memcmp(scope.pointer, "clang", 5) == 0) || (scope.length == 6 && memcmp(scope.pointer, "_Clang", 6) == 0)));
    const CheckInputSubjectRow* gcc = gnu_scope ? check_input_subject_find(check_input_subject_handlers, BUSTER_ARRAY_LENGTH(check_input_subject_handlers), name) : NULL;
    const CheckInputSubjectRow* handler = gcc == NULL && clang_scope ?
                                          check_input_subject_find(check_input_subject_clang_handlers, BUSTER_ARRAY_LENGTH(check_input_subject_clang_handlers), name) : gcc;
    result = result != NULL ? result : handler;
    return result;
}

// The facts row (CheckInputFactRow) of `count` in `list` (in byte order) whose spelling is `text`, or null.
static const CheckInputFactRow* check_input_fact_find(const CheckInputFactRow* list, uint32_t count, String8 text)
{
    const CheckInputFactRow* result = NULL;
    uint32_t lower = 0;
    uint32_t upper = count;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const uint32_t length = list[middle].length;
        const uint64_t common = text.length < length ? text.length : length;
        int order = common != 0 ? memcmp(text.pointer, list[middle].spelling, common) : 0;
        order = order != 0 ? order : (text.length > length) - (text.length < length);
        result = order == 0 ? list + middle : result;
        upper = order <= 0 ? middle : upper;
        lower = order > 0 ? middle + 1 : lower;
    }
    return result;
}

// An attribute's facts row: a GNU spelling's `name` (its surrounding underscores gone), or a C23 one's with its scope
// normalized as clang's (gnu for gnu and __gnu__, clang for clang and _Clang), or null for none.
static const CheckInputFactRow* check_input_fact_row(String8 scope, String8 name, bool scoped, bool standard)
{
    const CheckInputFactRow* result = NULL;
    if (!standard)
    {
        result = check_input_fact_find(check_input_fact_gnu, BUSTER_ARRAY_LENGTH(check_input_fact_gnu), name);
    }
    else
    {
        char spelling[96];
        const bool gnu = (scope.length == 3 && memcmp(scope.pointer, "gnu", 3) == 0) || (scope.length == 7 && memcmp(scope.pointer, "__gnu__", 7) == 0);
        const bool clang = (scope.length == 5 && memcmp(scope.pointer, "clang", 5) == 0) || (scope.length == 6 && memcmp(scope.pointer, "_Clang", 6) == 0);
        const String8 normalized = gnu ? (String8){ "gnu", 3 } : clang ? (String8){ "clang", 5 } : scope;
        const uint64_t length = scoped ? normalized.length + 2 + name.length : name.length;
        if (length < sizeof(spelling))
        {
            uint64_t at = 0;
            if (scoped)
            {
                memcpy(spelling, normalized.pointer, normalized.length);
                memcpy(spelling + normalized.length, "::", 2);
                at = normalized.length + 2;
            }
            memcpy(spelling + at, name.pointer, name.length);
            result = check_input_fact_find(check_input_fact_c23, BUSTER_ARRAY_LENGTH(check_input_fact_c23), (String8){ spelling, length });
        }
    }
    return result;
}

// clang's parser's -Wgcc-compat warning for a function definition (#147, Parser::ParseFunctionDefinition): each GNU
// attribute after its declarator (`attributes`, the declarator's own) whose attribute GCC knows, at its name, named as
// written, "GCC does not allow 'X' attribute in this position on a function definition".
static void check_input_attribute_gcc_position(CheckInputReader* reader, const CheckInputAttributes* attributes)
{
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const CheckInputFactRow* fact = attribute->standard || attribute->keyword ? NULL :
                                        check_input_fact_row((String8){ "", 0 }, check_input_attribute_name(reader, attribute->name), false, false);
        if (fact != NULL && (fact->flags & BUSTER_CHECK_FACT_GCC) != 0)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_GCC_COMPAT,
                .kind = CHECK_WARNING_EVENT_GCC_ATTRIBUTE_POSITION,
                .token = attribute->name_token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                // The parser's, before Sema's diagnostics of the same attribute.
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = attribute->name_token,
                .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, NULL, 0);
        }
    }
}

// The subjects a declaration of `type` takes as a function-like one: FUNCTION_LIKE (with PROTOTYPED for a prototype) of a
// function's type or a pointer to one, FUNCTION_POINTER of the latter, as clang's getFunctionType reads a declaration's type.
static uint32_t check_input_type_subjects(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_strip_aligned(types, type);
    const bool pointer = type_is_valid(types, bare) && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER;
    const TypeId function = pointer ? type_strip_aligned(types, types->types.base[type_index(bare)]) : bare;
    const bool like = type_is_valid(types, function) && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
    const bool prototyped = like && (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    return (like ? BUSTER_CHECK_SUBJECT_FUNCTION_LIKE : 0) | (prototyped ? BUSTER_CHECK_SUBJECT_PROTOTYPED : 0) |
           (like && pointer ? BUSTER_CHECK_SUBJECT_FUNCTION_POINTER : 0);
}

// Whether `text` is one of the `count` names of `list`, in byte order: a binary search.
static bool check_input_attribute_listed(const CheckInputClangAttribute* list, uint32_t count, String8 text)
{
    bool result = false;
    uint32_t lower = 0;
    uint32_t upper = count;
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const uint32_t length = list[middle].length;
        const uint64_t common = text.length < length ? text.length : length;
        int order = common != 0 ? memcmp(text.pointer, list[middle].name, common) : 0;
        order = order != 0 ? order : (text.length > length) - (text.length < length);
        result |= order == 0;
        upper = order <= 0 ? middle : upper;
        lower = order > 0 ? middle + 1 : lower;
    }
    return result;
}

// Whether clang knows an attribute the checker has no rule for (#147): a GNU spelling's `name` (its surrounding underscores
// gone, as check_input_attribute_name reads it) in its GNU vocabulary, and a C23 one's in its C23 vocabulary, its scope
// normalized as clang's: gnu's and __gnu__'s as gnu, clang's, _Clang's and __clang__'s as clang. Such an attribute is
// refused by name, as one clang applies; one clang doesn't know it ignores, with its warning (check_input_attribute_read_item).
static bool check_input_attribute_clang_knows(String8 scope, String8 name, bool scoped, bool standard)
{
    bool result = false;
    if (!standard)
    {
        result = check_input_attribute_listed(check_input_clang_gnu_attributes, BUSTER_ARRAY_LENGTH(check_input_clang_gnu_attributes), name);
    }
    else
    {
        // A spelling of at most 64 bytes: clang's longest is 53 (c_check_attribute_clang.inc).
        char spelling[64];
        const bool gnu = check_input_attribute_equal(scope, "gnu");
        const bool clang = check_input_attribute_equal(scope, "clang") || check_input_attribute_equal(scope, "_Clang");
        const String8 normalized = gnu ? (String8){ "gnu", 3 } : clang ? (String8){ "clang", 5 } : scope;
        const uint64_t length = scoped ? normalized.length + 2 + name.length : name.length;
        if (length < sizeof(spelling))
        {
            uint64_t at = 0;
            if (scoped)
            {
                memcpy(spelling, normalized.pointer, normalized.length);
                memcpy(spelling + normalized.length, "::", 2);
                at = normalized.length + 2;
            }
            memcpy(spelling + at, name.pointer, name.length);
            result = check_input_attribute_listed(check_input_clang_c23_attributes, BUSTER_ARRAY_LENGTH(check_input_clang_c23_attributes),
                                                  (String8){ spelling, length });
        }
    }
    return result;
}

// clang's warning at `__clang__`'s expansion where a C23 attribute's scope or name goes (#147), which the parser made the
// identifier `_Clang` (parse_clang_scope_numbers): a token of that name whose source is a number.
static void check_input_attribute_clang_macro(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const PPTokens* output = &reader->pp->output;
    const uint32_t source = parser->tokens.sources[token];
    const bool rewritten = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && source < output->count && output->kinds[source] == C_TOKEN_NUMERIC_CONSTANT;
    if (rewritten)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
            .kind = CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = token + 1,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_event(reader, &event, NULL, 0);
    }
}

// Where a C23 attribute list stands (#147), as clang's parser gives its attributes: after a function declarator's
// parameters, an array declarator's bounds or a pointer's '*', a declarator chunk's type's; right after a declaration's
// specifiers, their type's; after a pointer's qualifiers, nothing's (clang's parser's error); anywhere else, the
// declaration's or the statement's. A function declarator's chunk is told from the others'.
#define BUSTER_CHECK_PLACE_DECLARATION 0u
#define BUSTER_CHECK_PLACE_CHUNK 1u
#define BUSTER_CHECK_PLACE_SPECIFIERS 2u
#define BUSTER_CHECK_PLACE_QUALIFIERS 3u
#define BUSTER_CHECK_PLACE_FUNCTION 4u

static uint32_t check_input_standard_place(const Parser* parser, uint32_t open)
{
    // A list right after another stands where that one does.
    bool listed = true;
    while (listed)
    {
        const uint8_t closer = open > 0 ? parser->tree.roles[open - 1] : 0;
        listed = open > 0 && parser->tokens.kinds[open - 1] == PARSE_KIND_R_SQUARE &&
                 (closer == PARSE_ROLE_CLOSE_ATTRIBUTE || closer == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER || closer == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT ||
                  closer == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT);
        // Its outer `[`, past its brackets.
        uint32_t opener = open;
        uint32_t depth = 0;
        bool scanning = listed;
        while (scanning && opener > 0)
        {
            opener -= 1;
            const uint8_t kind = parser->tokens.kinds[opener];
            depth = kind == PARSE_KIND_R_SQUARE ? depth + 1 : kind == PARSE_KIND_L_SQUARE && depth != 0 ? depth - 1 : depth;
            scanning = depth != 0;
        }
        listed = listed && !scanning;
        open = listed ? opener : open;
    }
    uint32_t before = open;
    while (before > 0 && (parser->tokens.kinds[before - 1] == PARSE_KIND_QUALIFIER || parser->tokens.kinds[before - 1] == PARSE_KIND_ATOMIC))
    {
        before -= 1;
    }
    const bool qualifiers = before != open && before > 0 && parser->tokens.kinds[before - 1] == PARSE_KIND_STAR;
    const uint8_t kind = open > 0 ? parser->tokens.kinds[open - 1] : PARSE_KIND_END;
    const uint8_t role = open > 0 ? parser->tree.roles[open - 1] : 0;
    const bool chunk = role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_SUBSCRIPT || kind == PARSE_KIND_STAR;
    const bool specifiers = role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM || role == PARSE_ROLE_CLOSE_SPECIFIER ||
                            role == PARSE_ROLE_TAG;
    return qualifiers ? BUSTER_CHECK_PLACE_QUALIFIERS : role == PARSE_ROLE_CLOSE_FUNCTION ? BUSTER_CHECK_PLACE_FUNCTION : chunk ? BUSTER_CHECK_PLACE_CHUNK :
           specifiers && open > 0 ? BUSTER_CHECK_PLACE_SPECIFIERS : BUSTER_CHECK_PLACE_DECLARATION;
}

// A warning event at an attribute's first token (a C23 one's scope's) naming it: as written, or a C23 one's spelling
// normalized with its scope (check_warning_attribute_name). It is published there, before an error of the same
// attribute's (check_input_report_argument's), as clang's handlers give them in turn.
static void check_input_attribute_named_event(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningId id, CheckWarningEventKind kind)
{
    const Parser* parser = reader->parser;
    const uint32_t scope_name = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
    const CheckWarningEvent event =
    {
        .id = id,
        .kind = kind,
        .token = attribute->first,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = attribute->first,
        .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                     .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
    };
    check_input_warning_event(reader, &event, NULL, 0);
}

// check_input_attribute_named_event once an attribute (CheckInputReader.attribute_warned): its declaration specifiers'
// record meets each declarator, and a type name's attributes are read by the constant evaluator and the walk.
static void check_input_attribute_named_once(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningEventKind kind)
{
    const uint32_t count = reader->parser->tokens.count;
    if (reader->attribute_warned == NULL)
    {
        reader->attribute_warned = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint64_t, (uint64_t)count / 64 + 1);
        if (reader->attribute_warned != NULL)
        {
            memset(reader->attribute_warned, 0, ((uint64_t)count / 64 + 1) * sizeof(uint64_t));
        }
    }
    const uint32_t token = attribute->first;
    const bool warned = reader->attribute_warned == NULL || token >= count || ((reader->attribute_warned[token >> 6] >> (token & 63)) & 1) != 0;
    if (!warned)
    {
        reader->attribute_warned[token >> 6] |= (uint64_t)1 << (token & 63);
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, kind);
    }
}

// clang's warning for a calling convention x86-64 hasn't (stdcall, fastcall, thiscall and pascal, which
// X86_64TargetInfo::checkCallingConvention warns of and ignores: CHECK_CALL_IGNORED_X86_32), once each attribute meets a
// function type: at its first token, named as written (a C23 one's normalized).
static void check_input_attribute_unsupported(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    check_input_attribute_named_once(reader, attribute, CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED);
}

// The count of an attribute's arguments, its commas outside nested brackets (#147).
static uint32_t check_input_attribute_count(const Parser* parser, const CheckInputAttribute* attribute)
{
    uint32_t count = attribute->argument_end > attribute->argument_first ? 1 : 0;
    uint32_t depth = 0;
    for (uint32_t t = attribute->argument_first; t < attribute->argument_end; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        depth = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? depth + 1 :
                (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) && depth != 0 ? depth - 1 : depth;
        count += depth == 0 && kind == PARSE_KIND_COMMA;
    }
    return count;
}

// `count` arguments against clang's for an attribute of `fact` (handleCommonAttributeFeatures, #147): BUSTER_CHECK_COUNT_*,
// with the count clang's message names in `bound`. One whose arguments clang parses its own way isn't counted.
static uint8_t check_input_attribute_count_check(const CheckInputFactRow* fact, uint32_t count, uint32_t* bound)
{
    const uint32_t minimum = fact != NULL ? fact->minimum : 0;
    const uint32_t maximum = fact != NULL ? fact->maximum : UINT32_MAX;
    const bool custom = fact == NULL || (fact->flags & BUSTER_CHECK_FACT_CUSTOM_PARSING) != 0;
    const uint8_t result = custom ? BUSTER_CHECK_COUNT_NONE : minimum == maximum && count != minimum ? BUSTER_CHECK_COUNT_EXACT :
                           count < minimum ? BUSTER_CHECK_COUNT_LEAST :
                           maximum != UINT32_MAX && count > maximum ? BUSTER_CHECK_COUNT_MOST : BUSTER_CHECK_COUNT_NONE;
    *bound = result == BUSTER_CHECK_COUNT_MOST ? maximum : minimum;
    return result;
}

// Whether clang applies an attribute of `name` in a way that changes nothing this checker models (#147): one its generated
// handler applies with nothing more (its facts' SIMPLE), no type's or statement's alone, but selectany (a COMDAT) and
// no_caller_saved_registers (a calling convention), which stay refused by name;
// and these whose own handlers check what is checked here: minsize, optnone (check_input_attribute_optnone), noescape
// (a pointer parameter's, check_input_attribute_subjects), annotate (a string first, check_input_attribute_read_item),
// uninitialized, gcc_struct (the default layout), no_sanitize_memory, analyzer_noreturn and carries_dependency.
static bool check_input_attribute_generic(String8 name, const CheckInputFactRow* fact)
{
    const bool simple = fact != NULL && (fact->flags & (BUSTER_CHECK_FACT_SIMPLE | BUSTER_CHECK_FACT_TYPE)) == BUSTER_CHECK_FACT_SIMPLE;
    const bool effect = check_input_attribute_equal(name, "selectany") || check_input_attribute_equal(name, "no_caller_saved_registers");
    const bool handled = check_input_attribute_equal(name, "minsize") || check_input_attribute_equal(name, "optnone") ||
                         check_input_attribute_equal(name, "noescape") || check_input_attribute_equal(name, "annotate") ||
                         check_input_attribute_equal(name, "uninitialized") || check_input_attribute_equal(name, "gcc_struct") ||
                         check_input_attribute_equal(name, "no_sanitize_memory") || check_input_attribute_equal(name, "analyzer_noreturn") ||
                         check_input_attribute_equal(name, "carries_dependency");
    return fact != NULL && ((simple && !effect) || handled);
}

// Whether the attribute at `first` is in a type name (#147): the group around it, past its list's parentheses and a
// declarator's grouping ones, is a cast's, a sizeof's or alignof's, a compound literal's, a specifier's (typeof, _Atomic, _Alignas)
// or a keyword's type argument (va_arg, offsetof, _Generic's association). A parameter's list (a function
// declarator's), a block, a statement, a declaration's end or a tag's own list is none. A scan back over at most 256
// tokens.
static bool check_input_attribute_in_type_name(const Parser* parser, uint32_t first)
{
    uint32_t depth = 0;
    uint32_t i = first;
    bool result = false;
    bool scanning = true;
    for (uint32_t steps = 0; scanning && i > 0 && steps < 256; steps += 1)
    {
        i -= 1;
        const uint8_t kind = parser->tokens.kinds[i];
        const uint8_t role = parser->tree.roles[i];
        const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE;
        const bool close = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE;
        const bool stop = kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE;
        // A declarator's grouping parentheses and attributes' own are no type name's bounds.
        const bool transparent = role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_OPEN_ATTRIBUTE || role == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER ||
                                 role == PARSE_ROLE_OPEN_ATTRIBUTE_LEFT || role == PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT || role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST;
        // A tag's own list, between its keyword and its name or body, is the tag's.
        const bool tag = open && depth == 0 && role == PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT;
        const bool enclosing = (open && depth == 0 && !transparent) || tag;
        result = enclosing && !tag && (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF || role == PARSE_ROLE_OPEN_COMPOUND ||
                               role == PARSE_ROLE_OPEN_SPECIFIER || role == PARSE_ROLE_OPEN_KEYWORD);
        depth = close ? depth + 1 : open && depth != 0 ? depth - 1 : depth;
        scanning = !stop && !enclosing;
    }
    return result;
}

static bool check_input_attribute_read_item(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t* cursor, uint32_t end, bool standard,
                                            uint32_t place)
{
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    CheckInputAttribute attribute = { .first = i, .name_token = i, .standard = standard };
    bool valid = i < end && parser->tokens.names[i] != 0;
    bool namespace_known = true;
    bool scoped = false;
    bool clang_scope = false;
    bool unknown = false;
    String8 scope = { 0 };
    uint32_t scope_name = 0;
    if (valid && standard)
    {
        check_input_attribute_clang_macro(reader, i);
    }
    if (valid && standard && i + 2 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_COLON && parser->tokens.kinds[i + 2] == PARSE_KIND_COLON)
    {
        scope_name = parser->tokens.names[i];
        scope = check_input_attribute_name(reader, scope_name);
        // clang's scope is spelled `clang` or `_Clang` (`__clang__`'s expansion's too, parse_clang_scope_numbers).
        clang_scope = check_input_attribute_equal(scope, "clang") || check_input_attribute_equal(scope, "_Clang");
        namespace_known = check_input_attribute_equal(scope, "gnu") || clang_scope;
        scoped = true;
        i += 3;
        attribute.name_token = i;
        valid = i < end && parser->tokens.names[i] != 0;
    }
    if (valid)
    {
        attribute.name = parser->tokens.names[i++];
        const String8 name = check_input_attribute_name(reader, attribute.name);
        const CheckInputAttributeRule* rule = 0;
        for (uint32_t r = 0; namespace_known && r < BUSTER_ARRAY_LENGTH(check_input_attribute_rules) && rule == 0; r += 1)
        {
            if (check_input_attribute_equal(name, check_input_attribute_rules[r].name))
            {
                rule = &check_input_attribute_rules[r];
            }
        }
        if (rule != 0 && standard && !scoped)
        {
            const bool standard_name = check_input_attribute_equal(name, "deprecated") || check_input_attribute_equal(name, "fallthrough") ||
                                       check_input_attribute_equal(name, "maybe_unused") || check_input_attribute_equal(name, "nodiscard") ||
                                       check_input_attribute_equal(name, "noreturn") || check_input_attribute_equal(name, "_Noreturn") ||
                                       check_input_attribute_equal(name, "unsequenced") || check_input_attribute_equal(name, "reproducible");
            rule = standard_name ? rule : 0;
        }
        // musttail is clang's own: GNU's spelling and clang::'s, but no gnu::musttail, which clang ignores as an unknown
        // attribute and this refuses as one.
        rule = rule != 0 && rule->kind == CHECK_ATTRIBUTE_MUSTTAIL && scoped && !clang_scope ? 0 : rule;
        // So are nomerge and suppress, which a statement's attributes alone take.
        const bool statement_only = rule != 0 && (rule->kind == CHECK_ATTRIBUTE_NOMERGE || rule->kind == CHECK_ATTRIBUTE_SUPPRESS);
        rule = statement_only && ((scoped && !clang_scope) || !reader->statement_attributes) ? 0 : rule;
        attribute.argument_first = i;
        attribute.argument_end = i;
        if (i < end && parser->tokens.kinds[i] == PARSE_KIND_L_PAREN)
        {
            const uint32_t close = parser->tree.parents[i];
            valid = close > i && close < end;
            attribute.argument_first = i + 1;
            attribute.argument_end = close;
            i = valid ? close + 1 : end;
        }
        attribute.end = i;
        valid &= i == end || parser->tokens.kinds[i] == PARSE_KIND_COMMA;
        // An attribute clang doesn't know it ignores with a warning, whatever this checker's rules say of the name (GNU's
        // externally_visible and access, C23's unsequenced). Its vocabulary is of spellings, a keyword's underscores
        // stripped from `__x__` alone: a calling convention's keyword in a list (`__vectorcall`) is none, though
        // check_input_attribute_name reads it as its convention's name for the keyword's own use.
        const String8 written = check_input_attribute_name(reader, attribute.name);
        const bool keyword = attribute.name >= C_NAME_CDECL && attribute.name <= C_NAME_VECTORCALL && written.length + 2 == reader->pp->table->spans[attribute.name].length;
        const String8 spelled = keyword ? (String8){ written.pointer - 2, written.length + 2 } : written;
        const bool known = valid && check_input_attribute_clang_knows(scope, spelled, scoped, standard);
        // A C23 attribute of a declaration (no type's) in a type's place (check_input_standard_place, clang's
        // processTypeAttrs): a gnu:: one after a declarator chunk, clang's warning that it is ignored, keeping nothing of
        // it; any other there, or any after the specifiers, its error.
        // A gnu:: one there, even one clang doesn't know, it warns of first: one of a type GCC doesn't take there
        // (-Wgcc-compat), which it then applies, and one of a declaration it ignores. noderef it ignores anywhere in
        // this syntax, and vector_size after the specifiers, as GCC does; nocf_check is no type's there.
        const CheckInputFactRow* fact = standard ? check_input_fact_row(scope, name, scoped, standard) : NULL;
        const bool gnu_scope = scoped && (check_input_attribute_equal(scope, "gnu") || check_input_attribute_equal(scope, "__gnu__"));
        const bool specifiers = place == BUSTER_CHECK_PLACE_SPECIFIERS;
        const bool nocf = specifiers && check_input_attribute_equal(name, "nocf_check");
        const bool typed = fact != NULL && (fact->flags & BUSTER_CHECK_FACT_TYPE) != 0 && !nocf;
        const bool chunk = place == BUSTER_CHECK_PLACE_CHUNK || place == BUSTER_CHECK_PLACE_FUNCTION;
        const bool chunk_gnu = valid && chunk && gnu_scope;
        const bool ignored_type = known && ((clang_scope && check_input_attribute_equal(name, "noderef")) || (specifiers && check_input_attribute_equal(name, "vector_size")));
        const bool misplaced = known && place != BUSTER_CHECK_PLACE_DECLARATION && fact != NULL && !typed;
        if (chunk_gnu && typed)
        {
            // clang's declaration path warns of a calling convention it hasn't before this, on a function's chunk.
            if (place == BUSTER_CHECK_PLACE_FUNCTION && rule != 0 && rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_call(name) == CHECK_CALL_IGNORED_X86_32)
            {
                check_input_attribute_unsupported(reader, &attribute);
            }
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_GCC_COMPAT, CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE);
        }
        // A GNU attribute of a declaration in a type name: clang ignores it with its warning, keeping nothing (#147).
        const CheckInputFactRow* gnu_fact = known && !standard ? check_input_fact_row((String8){ "", 0 }, name, false, false) : NULL;
        // The facts of one this checker has no rule for (#147), which decide how clang takes it.
        const CheckInputFactRow* generic_fact = known && rule == 0 ? (standard ? fact : gnu_fact) : NULL;
        const bool type_name = gnu_fact != NULL && (gnu_fact->flags & BUSTER_CHECK_FACT_TYPE) == 0 && check_input_attribute_in_type_name(parser, attribute.first);
        if (chunk_gnu && !typed)
        {
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_POSITION);
            unknown = true;
        }
        else if (valid && type_name)
        {
            check_input_attribute_named_once(reader, &attribute, CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE);
            unknown = true;
        }
        else if (valid && ignored_type)
        {
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
            unknown = true;
        }
        else if (valid && misplaced)
        {
            check_input_report(reader, attribute.first, CHECK_INPUT_ATTRIBUTE_TYPES);
            valid = false;
            attributes->error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
            attributes->error_first = attribute.first;
            attributes->error_end = attribute.end;
        }
        else if (valid && rule != 0 && known)
        {
            attribute.kind = rule->kind;
            attribute.schema = rule->schema;
            reader->cleanup_attributes |= rule->kind == CHECK_ATTRIBUTE_CLEANUP;
            reader->weak_attributes |= rule->kind == CHECK_ATTRIBUTE_WEAK || rule->kind == CHECK_ATTRIBUTE_WEAKREF;
            const bool late_kind = check_input_attribute_late_kind(&attribute);
            reader->late_attributes |= late_kind;
            reader->deprecated_attributes |= rule->kind == CHECK_ATTRIBUTE_DEPRECATED || rule->kind == CHECK_ATTRIBUTE_UNAVAILABLE;
            // A lane cannot publish attribute state to later declarations; source-order replay handles this case.
            check_input_lane_refuse(reader, reader->lane && late_kind, "a body declares an attribute with linked state");
            // An argument count other than clang's is its error, worded as clang's (#147): before the arguments are read,
            // where clang checks it after the subject. cleanup's is checked after its subject's already
            // (check_input_cleanup_declaration).
            uint32_t bound = 0;
            const CheckInputFactRow* counted = standard ? fact : check_input_fact_row((String8){ "", 0 }, name, false, false);
            const uint8_t count_check = rule->kind == CHECK_ATTRIBUTE_MUSTTAIL || rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION ||
                                        rule->kind == CHECK_ATTRIBUTE_CLEANUP || rule->kind == CHECK_ATTRIBUTE_COUNTED_BY ? BUSTER_CHECK_COUNT_NONE :
                                        check_input_attribute_count_check(counted, check_input_attribute_count(parser, &attribute), &bound);
            if (count_check != BUSTER_CHECK_COUNT_NONE)
            {
                check_input_report_argument(reader, attribute.first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, count_check | bound << 8);
                valid = false;
                attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
                attributes->error_first = attribute.first;
                attributes->error_end = attribute.end;
            }
            else if (rule->kind == CHECK_ATTRIBUTE_LIFETIMEBOUND && standard && place == BUSTER_CHECK_PLACE_FUNCTION)
            {
                // A C23 lifetimebound after a function's declarator is its type's, whose implicit object parameter C's
                // functions haven't (#147, clang's checkLifetimeBoundAttr).
                check_input_report(reader, attribute.first, CHECK_INPUT_LIFETIMEBOUND_OBJECT);
                valid = false;
                attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
                attributes->error_first = attribute.first;
                attributes->error_end = attribute.end;
            }
            else if (rule->kind == CHECK_ATTRIBUTE_MUSTTAIL)
            {
                valid = check_input_attribute_musttail(reader, attributes, &attribute);
            }
            else if (rule->kind == CHECK_ATTRIBUTE_COUNTED_BY)
            {
                valid = check_input_attribute_counted_shape(reader, attributes, &attribute);
            }
            else
            {
                valid = check_input_attribute_validate(reader, attributes, &attribute, rule->minimum, rule->maximum);
            }
        }
        else if (valid && known && generic_fact != NULL && (generic_fact->flags & BUSTER_CHECK_FACT_IGNORED) != 0)
        {
            // One clang ignores wherever it is, without a word (its IgnoredAttribute spellings, #147).
            unknown = true;
        }
        else if (valid && known && generic_fact != NULL && (generic_fact->flags & BUSTER_CHECK_FACT_NOT_C) != 0)
        {
            // Another language's: clang's warning that it ignores it, before anything else (#147).
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
            unknown = true;
        }
        else if (valid && known && rule == 0 && check_input_attribute_equal(name, "nocf_check"))
        {
            // clang takes nocf_check as a function type's, whose handler without -fcf-protection (which the driver refuses)
            // warns that it ignores it, wherever it is and whatever its arguments (#147).
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NOCF_CHECK_IGNORED);
            unknown = true;
        }
        else if (valid && known && check_input_attribute_generic(name, generic_fact))
        {
            // One clang's generated handler applies with nothing more, which changes nothing here (#147): kept for its
            // subject's check, then its argument count's against clang's.
            const uint32_t count = check_input_attribute_count(parser, &attribute);
            attribute.kind = CHECK_ATTRIBUTE_UNKNOWN;
            attribute.argument_count = count;
            attribute.count_check = check_input_attribute_count_check(generic_fact, count, &attribute.count_bound);
            // annotate's first argument is a string literal (handleAnnotateAttr), clang's error at it otherwise.
            if (count != 0 && check_input_attribute_equal(name, "annotate") && parser->tokens.kinds[attribute.argument_first] != PARSE_KIND_STRING)
            {
                check_input_report(reader, attribute.argument_first, CHECK_INPUT_ATTRIBUTE_ANNOTATE_STRING);
                valid = false;
                attributes->error = CHECK_ATTRIBUTE_ERROR_STRING;
                attributes->error_first = attribute.argument_first;
                attributes->error_end = attribute.end;
            }
        }
        else if (valid && known)
        {
            // One clang applies that this checker doesn't take: refused by name.
            check_input_attribute_error(reader, attributes, attribute.name_token, attribute.end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
        else if (valid)
        {
            // One clang doesn't know (#147), which it ignores with -Wunknown-attributes' warning at the attribute's first
            // token (its scope's), as the C23 one's spelling normalized: none of it is kept.
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE,
                .kind = CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE,
                .token = attribute.first,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = attribute.first + 1,
                .payload = { .name = attribute.name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                             .data = scope_name | (standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
            };
            check_input_warning_event(reader, &event, NULL, 0);
            unknown = true;
        }
        if (valid && !unknown)
        {
            const bool numeric = attribute.schema == CHECK_ATTRIBUTE_INTEGER || attribute.schema == CHECK_ATTRIBUTE_INTEGER_LIST ||
                                 attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
            attribute.resolved = !numeric || attribute.argument_count == 0 || (attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && attribute.argument_count == 1);
            if (attribute.kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
            {
                // The first convention x86-64 has is the one asked for; one that conflicts with an earlier one is clang's
                // error where they meet a function (check_input_attribute_conventions).
                const CheckInputCallingConvention call = check_input_attribute_call(name);
                attributes->calling_convention = attributes->calling_convention == CHECK_CALL_DEFAULT && call != CHECK_CALL_IGNORED_X86_32 ? call :
                                                 attributes->calling_convention;
                attribute.value = call;
            }
            attributes->packed |= attribute.kind == CHECK_ATTRIBUTE_PACKED;
            // The walk's section events start at the first section attribute (CheckInputReader.section_record_from).
            reader->section_record_from = attribute.kind == CHECK_ATTRIBUTE_SECTION && attribute.first < reader->section_record_from ? attribute.first :
                                          reader->section_record_from;
            const uint32_t index = attributes->count;
            valid = valid && check_input_attribute_append(reader, attributes, attribute);
            if (valid && attribute.kind == CHECK_ATTRIBUTE_ALIGNED && attribute.argument_count == 0)
            {
                valid = check_input_attribute_value(reader, attributes, index, UINT64_C(1) << BUSTER_TYPE_DEFAULT_ALIGNED_LOG2);
            }
        }
    }
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        check_input_attribute_error(reader, attributes, attribute.first, i > attribute.first ? i : attribute.first + 1, CHECK_ATTRIBUTE_ERROR_SYNTAX);
    }
    *cursor = i;
    return valid;
}

static bool check_input_attribute_starts(const Parser* parser, uint32_t token, uint32_t end)
{
    const uint8_t kind = token < end ? parser->tokens.kinds[token] : PARSE_KIND_END;
    const uint32_t name = token < end ? parser->tokens.names[token] : 0;
    return kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ASM || kind == PARSE_KIND_CALLING_CONVENTION ||
           name == C_NAME_NORETURN || name == C_NAME_NONNULL || name == C_NAME_NULLABLE || name == C_NAME_NULLABLE_RESULT || name == C_NAME_NULL_UNSPECIFIED ||
           (kind == PARSE_KIND_L_SQUARE && token + 1 < end && parser->tokens.kinds[token + 1] == PARSE_KIND_L_SQUARE);
}

// A successful no-op leaves cursor unchanged. Failure retains the first exact diagnostic range and never consumes
// a non-attribute token. The caller can report the specific message alongside its ordinary declaration failure.
static bool check_input_attributes(CheckInputReader* reader, uint32_t* cursor, uint32_t end, CheckInputAttributes* attributes)
{
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    bool valid = end <= parser->tokens.count && i <= end && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE;
    while (valid && check_input_attribute_starts(parser, i, end))
    {
        const uint32_t first = i;
        const uint8_t kind = parser->tokens.kinds[i];
        const uint32_t name = parser->tokens.names[i];
        const bool standard = kind == PARSE_KIND_L_SQUARE;
        const uint32_t place = standard ? check_input_standard_place(parser, i) : BUSTER_CHECK_PLACE_DECLARATION;
        if (kind == PARSE_KIND_ATTRIBUTE || standard)
        {
            const uint32_t open = standard ? i : i + 1;
            valid = open + 1 < end && parser->tokens.kinds[open] == (standard ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_PAREN);
            const uint32_t close = valid ? parser->tree.parents[open] : end;
            valid &= close > open + 1 && close < end && parser->tokens.kinds[open + 1] == (standard ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_PAREN);
            const uint32_t list_end = valid ? parser->tree.parents[open + 1] : end;
            valid &= list_end + 1 == close;
            i = open + 2;
            if (valid && place == BUSTER_CHECK_PLACE_QUALIFIERS)
            {
                // clang's declarator parser rejects a C23 list after a pointer's qualifiers (err_attributes_not_allowed).
                check_input_report(reader, first, CHECK_INPUT_ATTRIBUTE_LIST_PLACE);
                valid = false;
                attributes->error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
                attributes->error_first = first;
                attributes->error_end = close + 1;
            }
            while (valid && i < list_end)
            {
                if (parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                {
                    i += 1;
                }
                else
                {
                    valid = check_input_attribute_read_item(reader, attributes, &i, list_end, standard, place);
                }
            }
            i = valid ? close + 1 : i;
        }
        else
        {
            CheckInputAttribute attribute =
            {
                .first = i,
                .end = i + 1,
                .name = name,
                .name_token = i,
                .argument_first = i + 1,
                .argument_end = i + 1,
                .kind = kind == PARSE_KIND_ALIGNAS ? CHECK_ATTRIBUTE_ALIGNAS : kind == PARSE_KIND_ASM ? CHECK_ATTRIBUTE_ASM_LABEL :
                        kind == PARSE_KIND_CALLING_CONVENTION ? CHECK_ATTRIBUTE_CALLING_CONVENTION : name == C_NAME_NORETURN ? CHECK_ATTRIBUTE_NORETURN : CHECK_ATTRIBUTE_NULLABILITY,
                .schema = kind == PARSE_KIND_ALIGNAS ? CHECK_ATTRIBUTE_INTEGER : kind == PARSE_KIND_ASM ? CHECK_ATTRIBUTE_STRING : CHECK_ATTRIBUTE_NO_ARGUMENTS,
                .resolved = kind != PARSE_KIND_ALIGNAS,
                .keyword = true,
            };
            i += 1;
            if (kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ASM)
            {
                valid = i < end && parser->tokens.kinds[i] == PARSE_KIND_L_PAREN;
                const uint32_t close = valid ? parser->tree.parents[i] : end;
                valid &= close > i && close < end;
                attribute.argument_first = i + 1;
                attribute.argument_end = close;
                attribute.end = valid ? close + 1 : i;
                valid = valid && check_input_attribute_validate(reader, attributes, &attribute, 1, 1);
                i = attribute.end;
            }
            else if (kind == PARSE_KIND_CALLING_CONVENTION)
            {
                const CheckInputCallingConvention call = check_input_attribute_call(check_input_attribute_name(reader, name));
                attribute.value = call;
                attributes->calling_convention = attributes->calling_convention == CHECK_CALL_DEFAULT && call != CHECK_CALL_IGNORED_X86_32 ? call :
                                                 attributes->calling_convention;
            }
            valid = valid && check_input_attribute_append(reader, attributes, attribute);
        }
        if (valid)
        {
            attributes->has_alignas |= kind == PARSE_KIND_ALIGNAS;
            memset(reader->ignored + first, 1, i - first);
        }
        else if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
        {
            check_input_attribute_error(reader, attributes, first, i > first ? i : first + 1, CHECK_ATTRIBUTE_ERROR_SYNTAX);
        }
    }
    *cursor = i;
    return valid;
}

static TypeId check_input_attribute_mode(CheckInputReader* reader, const CheckInputAttribute* attribute, TypeId base)
{
    String8 mode = check_input_attribute_name(reader, reader->parser->tokens.names[attribute->argument_first]);
    uint32_t vector_count = 0;
    bool valid_mode = true;
    if (mode.length != 0 && mode.pointer[0] == 'V')
    {
        uint64_t position = 1;
        uint64_t count = 0;
        while (position < mode.length && mode.pointer[position] >= '0' && mode.pointer[position] <= '9')
        {
            const uint32_t digit = (uint32_t)(mode.pointer[position++] - '0');
            valid_mode &= count <= (UINT32_MAX - digit) / 10;
            count = valid_mode ? count * 10 + digit : 0;
        }
        valid_mode &= count != 0 && (count & (count - 1)) == 0 && position > 1 && position < mode.length;
        vector_count = valid_mode ? (uint32_t)count : 0;
        mode.pointer += position;
        mode.length -= position;
    }
    const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, base));
    const TypeId element_type = kind == BUSTER_TYPE_KIND_COMPLEX ? type_element(reader->types, base) : base;
    const uint8_t element_kind = type_kind(reader->types, type_strip_aligned(reader->types, element_type));
    // The base's signedness, which the mode keeps: an enum's underlying type's, and plain char's as the table says
    // (TypeTable.char_unsigned). A builtin type's flags don't say it.
    uint32_t width = 0;
    bool is_signed = true;
    const bool unsign = check_action_integer_type(reader->types, element_type, &width, &is_signed) && !is_signed;
    TypeId result = 0;
    if (element_kind == BUSTER_TYPE_KIND_INTEGER || element_kind == BUSTER_TYPE_KIND_ENUM)
    {
        result = check_input_attribute_equal(mode, "QI") || check_input_attribute_equal(mode, "byte") ? (unsign ? BUSTER_TYPE_UCHAR : BUSTER_TYPE_SCHAR) :
                 check_input_attribute_equal(mode, "HI") ? (unsign ? BUSTER_TYPE_USHORT : BUSTER_TYPE_SHORT) :
                 check_input_attribute_equal(mode, "SI") ? (unsign ? BUSTER_TYPE_UINT : BUSTER_TYPE_INT) :
                 check_input_attribute_equal(mode, "DI") || check_input_attribute_equal(mode, "word") || check_input_attribute_equal(mode, "pointer") ? (unsign ? BUSTER_TYPE_ULONG : BUSTER_TYPE_LONG) :
                 check_input_attribute_equal(mode, "TI") ? (unsign ? BUSTER_TYPE_UINT128 : BUSTER_TYPE_INT128) : 0;
    }
    else if (element_kind == BUSTER_TYPE_KIND_FLOATING)
    {
        result = check_input_attribute_equal(mode, "HF") ? BUSTER_TYPE_FLOAT16 : check_input_attribute_equal(mode, "SF") ? BUSTER_TYPE_FLOAT :
                 check_input_attribute_equal(mode, "DF") ? BUSTER_TYPE_DOUBLE : check_input_attribute_equal(mode, "XF") ? BUSTER_TYPE_LONG_DOUBLE :
                 check_input_attribute_equal(mode, "TF") ? BUSTER_TYPE_FLOAT128 : 0;
        if (result == 0 && kind == BUSTER_TYPE_KIND_COMPLEX)
        {
            const TypeId element = check_input_attribute_equal(mode, "HC") ? BUSTER_TYPE_FLOAT16 : check_input_attribute_equal(mode, "SC") ? BUSTER_TYPE_FLOAT :
                                   check_input_attribute_equal(mode, "DC") ? BUSTER_TYPE_DOUBLE : check_input_attribute_equal(mode, "XC") ? BUSTER_TYPE_LONG_DOUBLE :
                                   check_input_attribute_equal(mode, "TC") ? BUSTER_TYPE_FLOAT128 : 0;
            result = element != 0 ? check_input_type_result(reader, type_complex(reader->types, element)) : 0;
        }
    }
    if (result != 0 && vector_count != 0)
    {
        result = check_input_type_result(reader, type_vector(reader->types, result, vector_count, false));
    }
    return valid_mode && result != 0 ? type_qualify(reader->types, result, type_qualifiers(base)) : 0;
}

// vector_size's checks of its element type `element` and its size, as clang's BuildVectorType makes them, in its order
// (#147): an element of no builtin integer type but bool, real floating type or _BitInt, or of a _BitInt whose width is
// no power of 2; a size of no integer constant, negative (at the argument), 2^61 bytes or more, zero, no multiple of the
// element's, or of more than 2^32 - 1 elements. Each is clang's error at the attribute, false. A size this checker's
// types can't hold is refused after (type_vector_size).
static bool check_input_vector_size(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, TypeId element)
{
    const TypeTable* types = reader->types;
    const TypeId bare = type_unqualified(type_strip_aligned(types, element));
    const uint8_t kind = type_is_valid(types, bare) ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const bool bit_int = kind == BUSTER_TYPE_KIND_BIT_INT;
    const bool integer = kind == BUSTER_TYPE_KIND_FLOATING || bit_int || (kind == BUSTER_TYPE_KIND_INTEGER && bare != BUSTER_TYPE_BOOL);
    const uint64_t width = bit_int ? types->types.extent[type_index(bare)] : 0;
    const uint64_t element_size = integer ? type_size(types, bare) : 0;
    const int64_t value = attribute->value;
    const CheckInputMessage message = !integer ? CHECK_INPUT_VECTOR_ELEMENT :
                                      bit_int && (width & (width - 1)) != 0 ? CHECK_INPUT_VECTOR_BIT_INT :
                                      !attribute->resolved ? CHECK_INPUT_VECTOR_CONSTANT :
                                      value < 0 ? CHECK_INPUT_VECTOR_NEGATIVE :
                                      (uint64_t)value >= (UINT64_C(1) << 61) ? CHECK_INPUT_VECTOR_TOO_LARGE :
                                      value == 0 ? CHECK_INPUT_VECTOR_ZERO :
                                      element_size == 0 || (uint64_t)value % element_size != 0 ? CHECK_INPUT_VECTOR_MULTIPLE :
                                      (uint64_t)value / element_size > UINT32_MAX ? CHECK_INPUT_VECTOR_TOO_LARGE : CHECK_INPUT_UNSUPPORTED;
    const bool valid = message == CHECK_INPUT_UNSUPPORTED;
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        // Published after the attribute's warnings at its token (check_input_report_argument).
        const uint32_t token = message == CHECK_INPUT_VECTOR_NEGATIVE ? attribute->argument_first : attribute->first;
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, token, message, 0);
        if (reader->output->report_count > before && message == CHECK_INPUT_VECTOR_ELEMENT)
        {
            reader->output->reports[before].type = element;
            reader->output->reports[before].type_spelling = check_input_typedef_spelling(reader->output, reader->parser, attribute->first);
        }
        attributes->error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    return valid;
}

// Mode/vector attributes change the scalar element, while a typedef's alignment wraps the resulting complete type.
// An object's/member's alignment is declaration metadata: applying ALIGNED to its type would incorrectly alter
// pointer/array compatibility. The caller feeds the alignment to the layout/storage consumer instead. Of `deduced`
// specifiers, __auto_type's, an _Alignas waits for the deduced type (check_input_auto_type_finish), which `base` isn't.
static TypeId check_input_attribute_type(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId base, bool is_typedef, bool deduced)
{
    TypeId result = base;
    bool valid = base != 0 && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE;
    for (uint32_t i = 0; valid && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        CheckInputAttributeError error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
        if (attribute->kind == CHECK_ATTRIBUTE_MODE && type_kind(reader->types, type_strip_aligned(reader->types, result)) == BUSTER_TYPE_KIND_FUNCTION)
        {
            // A function's, which clang's subject check refuses before the mode is read (check_input_attribute_subjects).
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_MODE)
        {
            result = check_input_attribute_mode(reader, attribute, result);
            valid = result != 0;
            error = CHECK_ATTRIBUTE_ERROR_MODE;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE && !check_input_vector_size(reader, attributes, attribute, result))
        {
            valid = false;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
        {
            valid = attribute->resolved;
            if (valid)
            {
                result = attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE ? type_vector_size(reader->types, result, (uint64_t)attribute->value) :
                         type_vector(reader->types, result, (uint32_t)attribute->value, true);
                result = check_input_type_result(reader, result);
                valid = result != 0;
            }
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_ALIGNED || attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
        {
            valid = attribute->resolved && !(is_typedef && attribute->kind == CHECK_ATTRIBUTE_ALIGNAS);
            error = attribute->resolved ? CHECK_ATTRIBUTE_ERROR_SUBJECT : CHECK_ATTRIBUTE_ERROR_INTEGER;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION)
        {
            // Subject/layout validation and nominal activation belong to the source completion point. Prefix
            // attributes can reach this helper while their record is incomplete; do not mark them prematurely.
            // Clang ignores this attribute on a non-union type with a warning instead of rejecting the declaration.
            attribute->ignored |= type_kind(reader->types, type_strip_aligned(reader->types, result)) != BUSTER_TYPE_KIND_UNION;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_FALLTHROUGH)
        {
            // Fallthrough belongs to an attributed null statement, never a declaration or type. Its statement
            // consumer checks switch context separately; retaining it here must not silently accept a bad subject.
            valid = false;
        }
        if (!valid)
        {
            check_input_attribute_error(reader, attributes, attribute->first, attribute->end, reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : error);
        }
    }
    if (valid && attributes->required_alignment_log2 != 0 && !deduced)
    {
        valid = type_kind(reader->types, result) != BUSTER_TYPE_KIND_FUNCTION && attributes->required_alignment_log2 - 1 >= type_alignment_log2(reader->types, result);
        if (!valid)
        {
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = &attributes->records[i];
                if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_ALIGNMENT);
                }
            }
        }
    }
    if (valid && is_typedef && attributes->alignment_log2 != 0)
    {
        result = check_input_type_result(reader, type_aligned(reader->types, result, attributes->alignment_log2 - 1));
        valid = result != 0;
    }
    return valid ? result : 0;
}

static bool check_input_attribute_string_type(TypeTable* types, TypeId type)
{
    type = type_strip_aligned(types, type);
    bool result = type_kind(types, type) == BUSTER_TYPE_KIND_POINTER;
    if (result)
    {
        const TypeId element = type_strip_aligned(types, types->types.base[type_index(type)]);
        result = type_unqualified(element) == BUSTER_TYPE_CHAR;
    }
    return result;
}

// The convention (CheckInputCallingConvention) of a function type's ABI.
static uint32_t check_input_abi_call(TypeFunctionABI abi)
{
    const uint32_t result = abi == TYPE_FUNCTION_ABI_MICROSOFT ? CHECK_CALL_MICROSOFT : abi == TYPE_FUNCTION_ABI_VECTOR ? CHECK_CALL_VECTOR :
                            abi == TYPE_FUNCTION_ABI_REG ? CHECK_CALL_REG : abi == TYPE_FUNCTION_ABI_PRESERVE_MOST ? CHECK_CALL_PRESERVE_MOST :
                            abi == TYPE_FUNCTION_ABI_PRESERVE_ALL ? CHECK_CALL_PRESERVE_ALL : abi == TYPE_FUNCTION_ABI_PRESERVE_NONE ? CHECK_CALL_PRESERVE_NONE :
                            CHECK_CALL_SYSV;
    return result;
}

static TypeFunctionABI check_input_attribute_requested_abi(const CheckInputAttributes* attributes)
{
    const CheckInputCallingConvention call = attributes != 0 ? attributes->calling_convention : CHECK_CALL_DEFAULT;
    const TypeFunctionABI result = call == CHECK_CALL_SYSV ? TYPE_FUNCTION_ABI_SYSV : call == CHECK_CALL_MICROSOFT ? TYPE_FUNCTION_ABI_MICROSOFT :
                                   call == CHECK_CALL_VECTOR ? TYPE_FUNCTION_ABI_VECTOR : call == CHECK_CALL_REG ? TYPE_FUNCTION_ABI_REG :
                                   call == CHECK_CALL_PRESERVE_MOST ? TYPE_FUNCTION_ABI_PRESERVE_MOST :
                                   call == CHECK_CALL_PRESERVE_ALL ? TYPE_FUNCTION_ABI_PRESERVE_ALL :
                                   call == CHECK_CALL_PRESERVE_NONE ? TYPE_FUNCTION_ABI_PRESERVE_NONE : TYPE_FUNCTION_ABI_INVALID;
    return result;
}

// The canonical default and explicit sysv_abi function types are identical. Keep explicitness on the spelling
// instead: zero means implicit, and an ABI plus one names the first function's explicit convention. New outer
// function declarators start a new origin; pointer, array, aligned, grouping and typedef wrappers preserve it.
static uint8_t check_input_attribute_abi_origin(CheckInputReader* reader, CheckInputAttributes* attributes, uint8_t origin)
{
    uint8_t result = origin;
    const TypeFunctionABI requested = check_input_attribute_requested_abi(attributes);
    if (requested != TYPE_FUNCTION_ABI_INVALID && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        if (origin == 0 || origin == (uint8_t)requested + 1)
        {
            result = (uint8_t)requested + 1;
        }
        else
        {
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->value != CHECK_CALL_IGNORED_X86_32)
                {
                    check_input_attribute_conflict(reader, attributes, attribute, (uint32_t)attribute->value, check_input_abi_call((TypeFunctionABI)(origin - 1)));
                }
            }
        }
    }
    return result;
}

// The calling conventions among `attributes` as clang applies them to a function, when `type` is one or wraps one in
// pointers, arrays and aligned typedefs as check_input_attribute_abi finds it: in order, each that x86-64 hasn't warned of
// (check_input_attribute_unsupported) and taken for cdecl, and the first that differs from one before it clang's error
// (check_input_attribute_conflict).
static void check_input_attribute_conventions(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    TypeId function = type;
    bool wrapped = true;
    while (wrapped)
    {
        const uint8_t kind = type_kind(reader->types, function);
        wrapped = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED;
        function = wrapped ? reader->types->types.base[type_index(function)] : function;
    }
    const bool is_function = type_kind(reader->types, function) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t current = CHECK_CALL_DEFAULT;
    for (uint32_t i = 0; is_function && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
        {
            const uint32_t call = attribute->value == CHECK_CALL_IGNORED_X86_32 ? CHECK_CALL_SYSV : (uint32_t)attribute->value;
            if (attribute->value == CHECK_CALL_IGNORED_X86_32)
            {
                check_input_attribute_unsupported(reader, attribute);
            }
            if (current != CHECK_CALL_DEFAULT && call != current)
            {
                check_input_attribute_conflict(reader, attributes, attribute, call, current);
            }
            current = call;
        }
    }
}

// Calling conventions attach to the first function inside a completed declarator, through its pointer, array and
// aligned-typedef wrappers. Preserve all those wrappers and their qualifiers; attributes on a scalar subject are
// warning-only ignored metadata. A prefix caller waits for the full declarator before invoking this helper.
// Always inlined (#880), as profile-guided builds do; hot callers: check_input_declarator_step and
// check_input_declaration.
static inline __attribute__((always_inline)) TypeId check_input_attribute_abi(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    TypeId result = type;
    if (type != 0 && attributes != 0 && attributes->count != 0 && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        check_input_attribute_conventions(reader, attributes, type);
    }
    const TypeFunctionABI abi = check_input_attribute_requested_abi(attributes);
    const bool requested = abi != TYPE_FUNCTION_ABI_INVALID;
    if (type != 0 && requested && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        const uint64_t mark = reader->scratch->length;
        TypeId function = type;
        uint32_t depth = 0;
        bool wrapped = true;
        while (wrapped)
        {
            const uint8_t kind = type_kind(reader->types, function);
            wrapped = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED;
            if (wrapped)
            {
                function = reader->types->types.base[type_index(function)];
                depth += 1;
            }
        }
        const bool is_function = type_kind(reader->types, function) == BUSTER_TYPE_KIND_FUNCTION;
        TypeId* wrappers = is_function && depth != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, TypeId, depth) : 0;
        if (is_function && (depth == 0 || wrappers != 0))
        {
            const TypeFunctionABI previous = type_function_abi_kind(reader->types, function);
            uint32_t count = 0;
            const TypeId* parameters = type_parameters(reader->types, function, &count);
            TypeId cursor = type;
            for (uint32_t i = 0; i < depth; i += 1)
            {
                wrappers[i] = cursor;
                cursor = reader->types->types.base[type_index(cursor)];
            }
            result = previous == TYPE_FUNCTION_ABI_SYSV || previous == abi ?
                     type_function_abi(reader->types, type_return(reader->types, function), parameters, count,
                                       reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_KEY, abi) : 0;
            if (previous == TYPE_FUNCTION_ABI_SYSV || previous == abi)
            {
                result = check_input_type_result(reader, result);
            }
            for (uint32_t i = depth; result != 0 && i != 0; i -= 1)
            {
                const TypeId wrapper = wrappers[i - 1];
                const uint32_t index = type_index(wrapper);
                const uint8_t kind = reader->types->types.kind[index];
                const uint64_t extent = reader->types->types.extent[index];
                result = kind == BUSTER_TYPE_KIND_POINTER ? type_pointer(reader->types, result) :
                         kind == BUSTER_TYPE_KIND_ARRAY ? type_array(reader->types, result, extent) :
                         kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? type_incomplete_array(reader->types, result) :
                         kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY ? type_variable_array(reader->types, result, (uint32_t)extent) :
                         type_aligned(reader->types, result, (uint32_t)extent);
                result = check_input_type_result(reader, result);
                if (result != 0)
                {
                    result = type_qualify(reader->types, result, type_qualifiers(wrapper));
                }
            }
        }
        else if (is_function)
        {
            reader->output->resource = true;
            result = 0;
        }
        const TypeFunctionABI previous = is_function ? type_function_abi_kind(reader->types, function) : TYPE_FUNCTION_ABI_SYSV;
        for (uint32_t i = 0; i < attributes->count; i += 1)
        {
            CheckInputAttribute* attribute = attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
            {
                attribute->ignored |= !is_function;
                if (result == 0 && reader->output->resource)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_RESOURCE);
                }
                else if (result == 0 && attribute->value != CHECK_CALL_IGNORED_X86_32)
                {
                    // The function's type has another convention (a typedef's).
                    check_input_attribute_conflict(reader, attributes, attribute, (uint32_t)attribute->value, check_input_abi_call(previous));
                }
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return attributes == 0 || attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? result : 0;
}

// The declaration whose attributes check_input_attribute_definition checks: its name's token; whether it is a function's
// (not a typedef's), a typedef's, and at file scope; whether it defines a function (a body) or a variable (an
// initializer, or at file scope a tentative definition with external linkage); and whether a variable has external
// linkage.
typedef struct CheckInputAttributeSubject CheckInputAttributeSubject;
struct CheckInputAttributeSubject
{
    uint32_t name;
    bool function;
    bool is_typedef;
    bool file;
    bool function_definition;
    bool variable_definition;
    bool external;
};

// GNU's function multiversioning (#839): target("default") makes a function multiversioned even alone, and
// target_clones, cpu_specific and cpu_dispatch do too. clang's checkNonMultiVersionCompatAttributes keeps only used
// (for target alone), nonnull and nothrow; every other declaration attribute it retains is an error at the function's
// name. Attributes clang keeps in the function's type or ignores on a function (noreturn, the calling conventions,
// access, externally_visible, unsequenced and reproducible) are allowed here too.
typedef enum CheckInputMultiversionKind
{
    CHECK_MULTIVERSION_NONE,
    CHECK_MULTIVERSION_TARGET,
    CHECK_MULTIVERSION_TARGET_CLONES,
    CHECK_MULTIVERSION_CPU_SPECIFIC,
    CHECK_MULTIVERSION_CPU_DISPATCH,
} CheckInputMultiversionKind;

static const char* check_input_multiversion_name(CheckInputMultiversionKind kind)
{
    const char* result = "";
    switch (kind)
    {
        case CHECK_MULTIVERSION_TARGET: result = "target"; break;
        case CHECK_MULTIVERSION_TARGET_CLONES: result = "target_clones"; break;
        case CHECK_MULTIVERSION_CPU_SPECIFIC: result = "cpu_specific"; break;
        case CHECK_MULTIVERSION_CPU_DISPATCH: result = "cpu_dispatch"; break;
        case CHECK_MULTIVERSION_NONE: break;
    }
    return result;
}

// Whether clang's multiversioning check accepts `attribute` on a function of `kind` (clang's
// AttrCompatibleWithMultiVersion, and the attributes it doesn't retain as declaration attributes).
static bool check_input_attribute_multiversion_allowed(CheckInputMultiversionKind kind, const CheckInputAttribute* attribute, bool inline_function)
{
    bool result = false;
    switch (attribute->kind)
    {
        case CHECK_ATTRIBUTE_USED: result = kind == CHECK_MULTIVERSION_TARGET; break;
        case CHECK_ATTRIBUTE_NONNULL:
        case CHECK_ATTRIBUTE_NOTHROW:
        // Type-level attributes, which clang doesn't see in FunctionDecl::attrs().
        case CHECK_ATTRIBUTE_NORETURN:
        case CHECK_ATTRIBUTE_CALLING_CONVENTION:
        case CHECK_ATTRIBUTE_REGPARM:
        // Attributes clang ignores on a function (with a warning this checker doesn't model).
        case CHECK_ATTRIBUTE_ACCESS:
        case CHECK_ATTRIBUTE_EXTERNALLY_VISIBLE:
        case CHECK_ATTRIBUTE_UNSEQUENCED:
        case CHECK_ATTRIBUTE_REPRODUCIBLE:
            result = true;
            break;
        // clang ignores artificial on a function that isn't inline, and refuses it on an inline one.
        case CHECK_ATTRIBUTE_ARTIFICIAL: result = !inline_function; break;
        // Type attributes. aligned and may_alias are FunctionDecl attributes to clang, which refuses them there.
        case CHECK_ATTRIBUTE_PACKED:
        case CHECK_ATTRIBUTE_ALIGNAS:
        case CHECK_ATTRIBUTE_MODE:
        case CHECK_ATTRIBUTE_VECTOR_SIZE:
        case CHECK_ATTRIBUTE_EXT_VECTOR_TYPE:
        case CHECK_ATTRIBUTE_TRANSPARENT_UNION:
        case CHECK_ATTRIBUTE_ALIGN_VALUE:
        // Attributes whose subjects aren't functions; their own checks report a bad subject.
        case CHECK_ATTRIBUTE_COMMON:
        case CHECK_ATTRIBUTE_NOCOMMON:
        case CHECK_ATTRIBUTE_TLS_MODEL:
        case CHECK_ATTRIBUTE_CLEANUP:
        case CHECK_ATTRIBUTE_NONSTRING:
        case CHECK_ATTRIBUTE_DESIGNATED_INIT:
        case CHECK_ATTRIBUTE_FLAG_ENUM:
        case CHECK_ATTRIBUTE_ENUM_EXTENSIBILITY:
        case CHECK_ATTRIBUTE_FALLTHROUGH:
        case CHECK_ATTRIBUTE_MUSTTAIL:
            result = true;
            break;
        default:
            break;
    }
    return result;
}

// The first attribute clang's multiversioning check refuses on `attributes` of `kind`, or BUSTER_CHECK_NONE. `skip`
// marks attributes a later multiversioning attribute made incompatible (clang doesn't add them), and `active` is the
// multiversioning attribute itself.
static uint32_t check_input_attribute_multiversion_disallowed(const CheckInputAttributes* attributes, CheckInputMultiversionKind kind,
                                                             const bool* skip, uint32_t active, bool inline_function)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; attributes != NULL && result == BUSTER_CHECK_NONE && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool multiversion = attribute->kind == CHECK_ATTRIBUTE_TARGET || attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES ||
                                  attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH;
        if (!attribute->ignored && i != active && !(skip != NULL && skip[i]) && !multiversion &&
            !check_input_attribute_multiversion_allowed(kind, attribute, inline_function))
        {
            result = i;
        }
    }
    return result;
}

// `attributes`' first non-ignored target attribute, or BUSTER_CHECK_NONE.
static uint32_t check_input_attribute_target_index(const CheckInputAttributes* attributes)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; attributes != NULL && result == BUSTER_CHECK_NONE && i < attributes->count; i += 1)
    {
        if (!attributes->records[i].ignored && attributes->records[i].kind == CHECK_ATTRIBUTE_TARGET)
        {
            result = i;
        }
    }
    return result;
}

// Whether `attributes` make a function multiversioned on their own: a target("default"), target_clones, cpu_specific
// or cpu_dispatch attribute.
static bool check_input_attribute_multiversion_present(const CheckInputAttributes* attributes)
{
    bool result = false;
    for (uint32_t i = 0; attributes != NULL && !result && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored)
        {
            result = attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES || attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                     attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH ||
                     (attribute->kind == CHECK_ATTRIBUTE_TARGET && check_input_attribute_equal(attribute->text, "default"));
        }
    }
    return result;
}

// Whether `attributes` hold target_clones, cpu_specific or cpu_dispatch.
static bool check_input_attribute_other_multiversion(const CheckInputAttributes* attributes)
{
    bool result = false;
    for (uint32_t i = 0; attributes != NULL && !result && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        result = !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES || attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                                         attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH);
    }
    return result;
}

// clang's Sema checks for GNU's function multiversioning (#839), at -fsyntax-only: a target("default") function, or one
// with target_clones, cpu_specific or cpu_dispatch, refuses every other declaration attribute clang keeps but used (for
// target alone), nonnull and nothrow, at the function's name. A target attribute beside target_clones, cpu_specific or
// cpu_dispatch is incompatible with it: the later attribute's error, with clang's note at the earlier one. A target
// declaration that makes a function multiversioned with an earlier one checks the earlier declaration's attributes
// first, at its name, with clang's note at the new declaration. `previous` is the function's previous declaration's
// attributes, if any.
static void check_input_attribute_multiversion(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name,
                                               const CheckInputAttributes* previous, uint32_t previous_name, bool inline_function,
                                               bool previous_inline)
{
    // A subject error (no_builtin on a declaration, for one) doesn't keep clang from reporting the multiversioning
    // conflict as well (#839's re-review).
    if (attributes == NULL || (attributes->error != CHECK_ATTRIBUTE_ERROR_NONE && attributes->error != CHECK_ATTRIBUTE_ERROR_SUBJECT))
    {
        return;
    }
    bool multiversion = false;
    bool target = false;
    bool clones = false;
    bool cpu = false;
    for (uint32_t i = 0; i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored)
        {
            target |= attribute->kind == CHECK_ATTRIBUTE_TARGET;
            clones |= attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES;
            cpu |= attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH;
        }
    }
    multiversion = target || clones || cpu;
    if (!multiversion)
    {
        return;
    }
    const uint64_t mark = reader->scratch->length;
    const uint32_t count = attributes->count;
    bool* incompatible = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, count) : NULL;
    reader->output->resource |= count != 0 && incompatible == NULL;
    if (count != 0 && incompatible == NULL)
    {
        arena_pop_to(reader->scratch, mark);
        return;
    }
    if (count != 0)
    {
        memset(incompatible, 0, count);
    }
    uint32_t first_target = BUSTER_CHECK_NONE;
    uint32_t first_clones = BUSTER_CHECK_NONE;
    uint32_t first_cpu = BUSTER_CHECK_NONE;
    uint32_t first_dispatch = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->ignored)
        {
            continue;
        }
        // The earlier attribute clang still has: one a later multiversioning attribute made incompatible isn't added.
        const uint32_t target = first_target != BUSTER_CHECK_NONE && !incompatible[first_target] ? first_target : BUSTER_CHECK_NONE;
        const uint32_t clones = first_clones != BUSTER_CHECK_NONE && !incompatible[first_clones] ? first_clones : BUSTER_CHECK_NONE;
        const uint32_t cpu = first_cpu != BUSTER_CHECK_NONE && !incompatible[first_cpu] ? first_cpu : BUSTER_CHECK_NONE;
        const uint32_t dispatch = first_dispatch != BUSTER_CHECK_NONE && !incompatible[first_dispatch] ? first_dispatch : BUSTER_CHECK_NONE;
        uint32_t earlier = BUSTER_CHECK_NONE;
        uint32_t duplicate = BUSTER_CHECK_NONE;
        switch (attribute->kind)
        {
            case CHECK_ATTRIBUTE_TARGET:
                first_target = first_target == BUSTER_CHECK_NONE ? i : first_target;
                earlier = clones != BUSTER_CHECK_NONE ? clones : cpu != BUSTER_CHECK_NONE ? cpu : dispatch;
                break;
            case CHECK_ATTRIBUTE_TARGET_CLONES:
                duplicate = first_clones;
                first_clones = first_clones == BUSTER_CHECK_NONE ? i : first_clones;
                earlier = target != BUSTER_CHECK_NONE ? target : cpu != BUSTER_CHECK_NONE ? cpu : dispatch;
                break;
            case CHECK_ATTRIBUTE_CPU_SPECIFIC:
                duplicate = first_cpu;
                first_cpu = first_cpu == BUSTER_CHECK_NONE ? i : first_cpu;
                earlier = target != BUSTER_CHECK_NONE ? target : clones != BUSTER_CHECK_NONE ? clones : dispatch;
                break;
            case CHECK_ATTRIBUTE_CPU_DISPATCH:
                duplicate = first_dispatch;
                first_dispatch = first_dispatch == BUSTER_CHECK_NONE ? i : first_dispatch;
                earlier = target != BUSTER_CHECK_NONE ? target : clones != BUSTER_CHECK_NONE ? clones : cpu;
                break;
            default:
                break;
        }
        if (earlier != BUSTER_CHECK_NONE && !incompatible[i])
        {
            // An earlier different-kind attribute is still present: clang reports the incompatibility, not the
            // duplicate, for the repeated attribute (#839's re-review).
            check_input_report_multiversion(reader, attribute->name_token, CHECK_INPUT_MULTIVERSION_INCOMPATIBLE, 0, attribute->name_token,
                                            attributes->records[earlier].name_token, CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE, 0);
            incompatible[i] = true;
        }
        else if (duplicate != BUSTER_CHECK_NONE)
        {
            check_input_report_multiversion(reader, attribute->name_token, CHECK_INPUT_MULTIVERSION_DUPLICATE, 0, attribute->name_token,
                                            attributes->records[duplicate].name_token, CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE, 0);
            incompatible[i] = true;
        }
    }
    // The active multiversioning attribute: the first target("default"), target_clones, cpu_specific or cpu_dispatch
    // that clang adds. A target of another string alone isn't multiversioned, but two target strings are.
    uint32_t active = BUSTER_CHECK_NONE;
    CheckInputMultiversionKind active_kind = CHECK_MULTIVERSION_NONE;
    for (uint32_t i = 0; i < count && active == BUSTER_CHECK_NONE; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->ignored || incompatible[i])
        {
            continue;
        }
        if (attribute->kind == CHECK_ATTRIBUTE_TARGET)
        {
            active = check_input_attribute_equal(attribute->text, "default") ? i : active;
            active_kind = active != BUSTER_CHECK_NONE ? CHECK_MULTIVERSION_TARGET : active_kind;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES || attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                 attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH)
        {
            active = i;
            active_kind = attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES ? CHECK_MULTIVERSION_TARGET_CLONES :
                          attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ? CHECK_MULTIVERSION_CPU_SPECIFIC : CHECK_MULTIVERSION_CPU_DISPATCH;
        }
    }
    bool target_multiversion = active_kind == CHECK_MULTIVERSION_TARGET;
    if (active == BUSTER_CHECK_NONE && first_target != BUSTER_CHECK_NONE && !incompatible[first_target])
    {
        // Two target attributes of different strings make the function multiversioned without a "default".
        for (uint32_t i = first_target + 1; i < count && !target_multiversion; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            target_multiversion = !attribute->ignored && !incompatible[i] && attribute->kind == CHECK_ATTRIBUTE_TARGET &&
                                  !check_input_attribute_text_equal(attribute->text, attributes->records[first_target].text);
        }
        if (target_multiversion)
        {
            active = first_target;
            active_kind = CHECK_MULTIVERSION_TARGET;
        }
    }
    // A new target declaration that makes the function multiversioned with an earlier one checks the earlier
    // declaration's attributes first, at its name, and its own only if that one is clean.
    bool causes = false;
    if (previous != NULL && previous_name != BUSTER_CHECK_NONE && !check_input_attribute_other_multiversion(previous))
    {
        const uint32_t previous_target = check_input_attribute_target_index(previous);
        const uint32_t current_target = check_input_attribute_target_index(attributes);
        causes = current_target != BUSTER_CHECK_NONE &&
                 (check_input_attribute_equal(attributes->records[current_target].text, "default") ||
                  (previous_target != BUSTER_CHECK_NONE &&
                   !check_input_attribute_text_equal(attributes->records[current_target].text, previous->records[previous_target].text)));
    }
    if (causes)
    {
        const uint32_t bad = check_input_attribute_multiversion_disallowed(previous, CHECK_MULTIVERSION_TARGET, NULL, BUSTER_CHECK_NONE, previous_inline);
        if (bad != BUSTER_CHECK_NONE)
        {
            const uint32_t reports = reader->output->multiversion_report_count;
            check_input_report_multiversion(reader, previous_name, CHECK_INPUT_MULTIVERSION_ATTRIBUTE, CHECK_MULTIVERSION_TARGET,
                                            previous->records[bad].name_token, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, 0);
            if (reader->output->multiversion_report_count != reports)
            {
                // The earlier declaration's own deferred error is at this token too; clang emits both.
                reader->output->multiversion_reports[reports].repeated = true;
            }
            arena_pop_to(reader->scratch, mark);
            return;
        }
        // The new declaration makes the function multiversioned even where its own target isn't "default".
        if (active == BUSTER_CHECK_NONE && first_target != BUSTER_CHECK_NONE && !incompatible[first_target])
        {
            active = first_target;
            active_kind = CHECK_MULTIVERSION_TARGET;
        }
    }
    bool pair = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        pair |= incompatible[i];
    }
    if (active != BUSTER_CHECK_NONE)
    {
        const uint32_t bad = check_input_attribute_multiversion_disallowed(attributes, active_kind, incompatible, active, inline_function);
        if (bad != BUSTER_CHECK_NONE)
        {
            check_input_report_multiversion(reader, name, CHECK_INPUT_MULTIVERSION_ATTRIBUTE, (uint8_t)active_kind,
                                            attributes->records[bad].name_token, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, 0);
        }
        else if (cpu && !target && !clones && !pair)
        {
            // CPU dispatch's code generation isn't supported. With no target, target_clones or compatibility error to
            // report, retaining the declaration would let -c write a call to a plain symbol the source doesn't define
            // (#839's review). A disallowed attribute above is clang's multiversioning error, not this refusal.
            for (uint32_t i = 0; i < attributes->count; i += 1)
            {
                CheckInputAttribute* attribute = attributes->records + i;
                if (!attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH))
                {
                    check_input_report(reader, attribute->name_token, CHECK_INPUT_UNSUPPORTED);
                    attribute->ignored = true;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}

// clang's check of each attribute's subject (#147, its generated diagAppertainsToDecl, c_check_attribute_subjects.inc):
// a declaration that is none of `subjects`' kinds an attribute's row names gets clang's warning that the attribute only
// applies to those, which then keeps nothing of it, or clang's error of the same words, at the attribute's first token (a
// C23 one's scope). An attribute of no row (a type's, one whose own handler clang checks) is left as it is. A variable's,
// parameter's or field's nonstring on a `type` that is no character array or pointer warns at `begin`, as clang's handler
// does, its type spelled through the declarator `name`'s typedef. Whether no error was reported.
// Whether a variable, parameter or field of `type` may have nonstring, as clang's handleNonStringAttr asks: a pointer to
// a character type (char, signed char or unsigned char, qualified or not) or an array of one, through arrays of arrays.
static bool check_input_nonstring_type(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_strip_aligned(types, type);
    const uint8_t kind = type_is_valid(types, bare) ? type_kind(types, bare) : BUSTER_TYPE_KIND_VOID;
    const bool array = type_kind_is_array(kind);
    TypeId element = array || kind == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, types->types.base[type_index(bare)]) : 0;
    while (array && element != 0 && type_is_valid(types, element) && type_kind_is_array(type_kind(types, element)))
    {
        element = type_strip_aligned(types, types->types.base[type_index(element)]);
    }
    const TypeId plain = type_unqualified(element);
    return element != 0 && (plain == BUSTER_TYPE_CHAR || plain == BUSTER_TYPE_SCHAR || plain == BUSTER_TYPE_UCHAR);
}

// A declaration's first token as clang's Decl::getBeginLoc gives it, from its first token `token`: past leading
// __extension__ markers and C23 attribute specifiers (GNU attributes and _Alignas begin it).
static uint32_t check_input_attribute_begin(const Parser* parser, uint32_t token, uint32_t end)
{
    uint32_t begin = token;
    bool more = true;
    while (more && begin < end)
    {
        const uint8_t kind = parser->tokens.kinds[begin];
        const bool standard = kind == PARSE_KIND_L_SQUARE && check_input_attribute_starts(parser, begin, end);
        const uint32_t close = standard ? parser->tree.parents[begin] : begin;
        const bool skipped = standard && close > begin && close < end;
        more = kind == PARSE_KIND_EXTENSION || skipped;
        begin = kind == PARSE_KIND_EXTENSION ? begin + 1 : skipped ? close + 1 : begin;
    }
    return begin < end ? begin : token;
}

// The record before `index` among `attributes` (kept, no keyword's) that excludes the one at `index`, as clang's generated
// diagMutualExclusion has it (#147, check_input_exclusions, by their names without a GNU keyword's underscores or a C23
// scope), or UINT32_MAX. Most names exclude none: a binary search finds whether the one at `index` is in the table first.
static uint32_t check_input_attribute_excluding_in(CheckInputReader* reader, String8 name, const CheckInputAttributes* attributes, uint32_t limit);

static uint32_t check_input_attribute_excluding(CheckInputReader* reader, const CheckInputAttributes* attributes, uint32_t index)
{
    return check_input_attribute_excluding_in(reader, check_input_attribute_name(reader, attributes->records[index].name), attributes, index);
}

// The record before `limit` among `attributes` (kept, no keyword's) that excludes an attribute named `name`
// (check_input_attribute_excluding), or UINT32_MAX.
static uint32_t check_input_attribute_excluding_in(CheckInputReader* reader, String8 name, const CheckInputAttributes* attributes, uint32_t limit)
{
    uint32_t lower = 0;
    uint32_t upper = BUSTER_ARRAY_LENGTH(check_input_exclusions);
    while (lower < upper)
    {
        const uint32_t middle = lower + (upper - lower) / 2;
        const CheckInputExclusionRow* row = check_input_exclusions + middle;
        const uint64_t common = name.length < row->length ? name.length : row->length;
        int order = common != 0 ? memcmp(row->spelling, name.pointer, common) : 0;
        order = order != 0 ? order : (row->length > name.length) - (row->length < name.length);
        lower = order < 0 ? middle + 1 : lower;
        upper = order < 0 ? upper : middle;
    }
    uint32_t result = UINT32_MAX;
    for (uint32_t r = lower; result == UINT32_MAX && r < BUSTER_ARRAY_LENGTH(check_input_exclusions) && check_input_attribute_equal(name, check_input_exclusions[r].spelling);
         r += 1)
    {
        const String8 excluded = { (char*)check_input_exclusions[r].excluded, check_input_exclusions[r].excluded_length };
        for (uint32_t j = 0; result == UINT32_MAX && attributes != NULL && j < limit && j < attributes->count; j += 1)
        {
            const CheckInputAttribute* earlier = attributes->records + j;
            const bool kept = !earlier->ignored && !earlier->subject_ignored && !earlier->keyword;
            result = kept && check_input_attribute_equal(check_input_attribute_name(reader, earlier->name), excluded.pointer) &&
                     check_input_attribute_name(reader, earlier->name).length == excluded.length ? j : result;
        }
    }
    return result;
}

static bool check_input_attribute_subjects(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t declaration, TypeId type, uint32_t begin,
                                           uint32_t name)
{
    const Parser* parser = reader->parser;
    const uint32_t pointer_variable = BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_FUNCTION_POINTER;
    const uint32_t subjects = declaration | ((declaration & pointer_variable) == pointer_variable ? BUSTER_CHECK_SUBJECT_FUNCTION_POINTER_VARIABLE : 0);
    bool valid = true;
    for (uint32_t i = 0; valid && attributes != NULL && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const bool scoped = attribute->standard && attribute->first != attribute->name_token;
        const uint32_t scope_name = scoped ? parser->tokens.names[attribute->first] : 0;
        const String8 scope = scoped ? check_input_attribute_name(reader, scope_name) : (String8){ "", 0 };
        // transparent_union's handler ignores it off a union earlier (check_input_attribute_type), which still warns here.
        const bool transparent = attribute->kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION;
        const CheckInputSubjectRow* row = attribute->keyword || (attribute->ignored && !attribute->subject_ignored && !transparent) ? NULL :
                                          check_input_subject_row(scope, check_input_attribute_name(reader, attribute->name), scoped, attribute->standard);
        // A shared record another declarator ignored applies to this one when its subjects fit (checked again below).
        attribute->ignored &= !(attribute->subject_ignored && !transparent);
        attribute->subject_ignored = false;
        // A counted_by family attribute's name off a member (#148): clang's parser looks it up first, in the ordinary
        // scopes, the members being none's here; one that names nothing is its error, which ends the attribute.
        const uint32_t counted = attribute->kind == CHECK_ATTRIBUTE_COUNTED_BY && attribute->argument_first + 1 == attribute->argument_end &&
                                         parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER ? attribute->argument_first : BUSTER_CHECK_NONE;
        const bool undeclared = counted != BUSTER_CHECK_NONE && c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[counted]) == 0;
        if (row != NULL && (row->subjects & subjects) == 0 && row->error && undeclared)
        {
            check_input_report(reader, counted, CHECK_INPUT_UNDECLARED);
            valid = false;
        }
        else if (row != NULL && (row->subjects & subjects) == 0 && row->error)
        {
            check_input_report(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_SUBJECT);
            valid = false;
        }
        else if (row != NULL && (row->subjects & subjects) == 0)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                .kind = CHECK_WARNING_EVENT_ATTRIBUTE_SUBJECT,
                .token = attribute->first,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = attribute->first + 1,
                .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                             .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
            };
            check_input_warning_event(reader, &event, NULL, 0);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        else if (row != NULL && attribute->kind == CHECK_ATTRIBUTE_NONSTRING && type != 0 && begin < parser->tokens.count &&
                 !check_input_nonstring_type(reader->types, type))
        {
            // clang's handler's own warning, at the declaration's first token, after its subjects pass: the attribute named
            // as above (its scope's token, when it has one, in the payload's) and the type as clang spells it.
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                .kind = CHECK_WARNING_EVENT_NONSTRING_TYPE,
                .token = begin,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = begin + 1,
                .payload = { .first_type = type, .name = attribute->name, .token = scoped ? attribute->first : BUSTER_CHECK_WARNING_NO_SOURCE,
                             .data = name < parser->tokens.count ? check_input_typedef_spelling(reader->output, parser, name) : 0 },
            };
            check_input_warning_event(reader, &event, NULL, 0);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        // noescape on a parameter of no pointer type: its handler's warning, which ignores it (#147).
        const bool generic = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword;
        const String8 spelled = generic ? check_input_attribute_name(reader, attribute->name) : (String8){ "", 0 };
        if (generic && !attribute->ignored && check_input_attribute_equal(spelled, "noescape") && (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0 &&
            type != 0 && type_kind(reader->types, type_strip_aligned(reader->types, type)) != BUSTER_TYPE_KIND_POINTER)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NONNULL_POINTERS);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        // weak_import's and internal_linkage's handlers (#147), each of which ignores it with its warning: weak_import on a
        // variable's definition (a parameter's among them; a function's body isn't its yet); internal_linkage on a
        // parameter (its generated check takes it as a variable, its handler names its subjects otherwise), and on a
        // variable of local storage, at the variable's name.
        const bool weak_definition = !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_WEAK_IMPORT &&
                                     (declaration & (BUSTER_CHECK_SUBJECT_PARAMETER | BUSTER_CHECK_SUBJECT_DEFINITION)) != 0;
        const bool internal = !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_INTERNAL_LINKAGE;
        if (weak_definition || (internal && (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0))
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                                              weak_definition ? CHECK_WARNING_EVENT_ATTRIBUTE_DEFINITION : CHECK_WARNING_EVENT_ATTRIBUTE_VARIABLES_FUNCTIONS);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        else if (internal && (declaration & BUSTER_CHECK_SUBJECT_LOCAL) != 0 && name < parser->tokens.count)
        {
            check_input_warning(reader, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_INTERNAL_LINKAGE_LOCAL, name, CHECK_WARNING_COMPLETE_AT_TOKEN, name + 1);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        // After its subject's check, as clang's handleCommonAttributeFeatures has them (#147): an attribute already on the
        // declaration that excludes it (diagMutualExclusion), with the note at that one, then its count of arguments.
        const bool kept = valid && !attribute->ignored && !attribute->subject_ignored && !attribute->keyword;
        const uint32_t excluding = kept ? check_input_attribute_excluding(reader, attributes, i) : UINT32_MAX;
        if (excluding != UINT32_MAX)
        {
            const uint32_t before = reader->output->report_count;
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_EXCLUSION, 0);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].note_token = attributes->records[excluding].first;
                reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
            }
            valid = false;
        }
        else if (kept && attribute->count_check != BUSTER_CHECK_COUNT_NONE)
        {
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            valid = false;
        }
    }
    // optnone takes over always_inline and minsize, wherever they are: clang's warning at each, with a note at optnone.
    for (uint32_t i = 0; valid && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* optnone = attributes->records + i;
        const bool kept = !optnone->ignored && !optnone->subject_ignored && optnone->kind == CHECK_ATTRIBUTE_UNKNOWN && !optnone->keyword &&
                          check_input_attribute_equal(check_input_attribute_name(reader, optnone->name), "optnone");
        for (uint32_t j = 0; kept && j < attributes->count; j += 1)
        {
            CheckInputAttribute* other = attributes->records + j;
            const bool minsize = other->kind == CHECK_ATTRIBUTE_UNKNOWN && !other->keyword &&
                                 check_input_attribute_equal(check_input_attribute_name(reader, other->name), "minsize");
            if (!other->ignored && !other->subject_ignored && (other->kind == CHECK_ATTRIBUTE_ALWAYS_INLINE || minsize))
            {
                const Parser* parser = reader->parser;
                const uint32_t scope_name = other->standard && other->first != other->name_token ? parser->tokens.names[other->first] : 0;
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                    .kind = CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED,
                    .token = other->first,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = other->first,
                    .payload = { .name = other->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                                 .data = scope_name | (other->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
                };
                const CheckWarningNote note =
                {
                    .kind = CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE,
                    .token = optnone->first,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, &note, 1);
                other->ignored = true;
            }
        }
    }
    return valid;
}

// One pass over a declaration's attributes, for those whose place on a definition matters. clang's no_builtin is
// permitted on a function's definition alone: on another function's declaration it is an error at its name, and on
// anything else clang warns that it only applies to functions and ignores it. Its arguments are kept as written, as an
// argument that names no library function is, which clang warns about and ignores: no code here makes a call of a
// library function that the source doesn't.
// GNU's alias, ifunc and weakref attributes (#147), as clang's Sema takes them, in their order: an alias (or a weakref's
// target, which clang makes one) on a definition is an error at the attribute, "definition 'f' cannot also be an alias";
// and an alias, ifunc or weakref target after another that makes the declaration a definition (hasDefiningAttr), on a
// function, or on a variable with external linkage, is too (handleAliasAttr, handleIFuncAttr, checkAttributesAfterMerging).
// An ifunc anywhere but on a function is ignored (clang warns, which isn't modeled). A weakref on a typedef is an error;
// in a block it is too, and dropped; and one without a target is the next error, at the list's first attribute, after
// which clang drops it (ProcessDeclAttributeList). Whether the declaration's attributes are valid.
static bool check_input_attribute_definition(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttributeSubject subject)
{
    bool valid = true;
    bool defining = subject.function ? subject.function_definition : subject.variable_definition;
    bool weakref = false;
    bool targeted = false;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        // These spellings are standard C23 attributes, not GNU attributes clang's older C modes know: it warns and
        // ignores them, so they don't conflict with multiversioning (#839's review).
        if (!attribute->ignored)
        {
            // clang's C parser warns and ignores these spellings in every mode (#839's re-review).
            const String8 spelling = check_input_attribute_name(reader, attribute->name);
            attribute->ignored |= check_input_attribute_equal(spelling, "nodiscard") || check_input_attribute_equal(spelling, "maybe_unused") ||
                                  check_input_attribute_equal(spelling, "no_sanitize_undefined");
        }
        const CheckInputAttributeKind kind = attribute->ignored ? CHECK_ATTRIBUTE_UNKNOWN : attribute->kind;
        attribute->ignored |= kind == CHECK_ATTRIBUTE_IFUNC && !subject.function;
        const bool aliasing = kind == CHECK_ATTRIBUTE_ALIAS || (kind == CHECK_ATTRIBUTE_IFUNC && subject.function) ||
                              (kind == CHECK_ATTRIBUTE_WEAKREF && attribute->argument_count != 0 && !subject.is_typedef && subject.file);
        if (attribute->kind == CHECK_ATTRIBUTE_NO_BUILTIN)
        {
            attribute->ignored |= !subject.function;
            valid = !subject.function || subject.function_definition;
            if (!valid)
            {
                const uint32_t reports = reader->output->report_count;
                check_input_report(reader, attribute->name_token, CHECK_INPUT_NO_BUILTIN_DEFINITION);
                // clang reports the multiversioning conflict first; this declaration-only error follows it (#839).
                if (reader->output->report_count != reports && check_input_attribute_multiversion_present(attributes))
                {
                    reader->output->reports[reports].sort = subject.name + 1;
                }
            }
        }
        else if (kind == CHECK_ATTRIBUTE_WEAKREF && (subject.is_typedef || !subject.file))
        {
            check_input_report_subject(reader, attribute->name_token, subject.is_typedef ? CHECK_INPUT_WEAKREF_SUBJECT : CHECK_INPUT_WEAKREF_CONTEXT, subject.name);
            valid = false;
        }
        else if (aliasing && !subject.is_typedef && defining && (subject.function || subject.external || subject.variable_definition))
        {
            const uint32_t reports = reader->output->report_count;
            check_input_report_subject(reader, attribute->name_token, kind == CHECK_ATTRIBUTE_IFUNC ? CHECK_INPUT_IFUNC_DEFINITION : CHECK_INPUT_ALIAS_DEFINITION,
                                       subject.name);
            // clang's alias and ifunc definition errors are after its multiversioning check, though their attributes
            // are earlier in the declaration: order them after the function's name (#839).
            if (reader->output->report_count != reports && subject.function && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE &&
                check_input_attribute_multiversion_present(attributes))
            {
                reader->output->reports[reports].sort = subject.name + 1;
            }
            valid = false;
        }
        defining |= aliasing;
        weakref |= kind == CHECK_ATTRIBUTE_WEAKREF;
        targeted |= kind == CHECK_ATTRIBUTE_ALIAS || (kind == CHECK_ATTRIBUTE_WEAKREF && attribute->argument_count != 0);
    }
    if (valid && weakref && !targeted)
    {
        check_input_report_subject(reader, attributes->records[0].name_token, CHECK_INPUT_WEAKREF_ALIAS, subject.name);
        valid = false;
    }
    return valid;
}

// An alias, ifunc or weakref declaration defines its entity even though it has no body or initializer (clang's
// hasDefiningAttr). Keep that fact separate from `defined`, whose users also identify bodies (for example, the old-style
// parameter compatibility check).
static bool check_input_attribute_alias_definition(const CheckInputAttributes* attributes)
{
    bool alias = false;
    for (uint32_t i = 0; !alias && attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        alias = !attribute->ignored && (attribute->kind == CHECK_ATTRIBUTE_ALIAS || attribute->kind == CHECK_ATTRIBUTE_IFUNC ||
                                        (attribute->kind == CHECK_ATTRIBUTE_WEAKREF && attribute->argument_count != 0));
    }
    return alias;
}

// Whether clang drops `attribute` from a declaration after its entity's definition when the definition (and the
// declarations it inherits from) has none of its kind (#147, checkNewAttributesAfterDef), besides
// check_input_attribute_late_kind's: any declaration attribute but a type's (its facts', the machine mode), the alias
// family's, multiversioning's, overloadable, the keywords', a statement's or a local's, and C23's deprecated,
// maybe_unused and nodiscard, which may come after it.
static bool check_input_attribute_after_kind(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const CheckInputAttributeKind kind = attribute->kind;
    const bool excluded = attribute->keyword || check_input_attribute_late_kind(attribute) || kind == CHECK_ATTRIBUTE_UNKNOWN || kind == CHECK_ATTRIBUTE_PACKED ||
                          kind == CHECK_ATTRIBUTE_ALIGNAS || kind == CHECK_ATTRIBUTE_MODE || kind == CHECK_ATTRIBUTE_VECTOR_SIZE ||
                          kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE || kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION || kind == CHECK_ATTRIBUTE_ALIAS ||
                          kind == CHECK_ATTRIBUTE_IFUNC || kind == CHECK_ATTRIBUTE_WEAKREF || kind == CHECK_ATTRIBUTE_ASM_LABEL ||
                          kind == CHECK_ATTRIBUTE_CALLING_CONVENTION || kind == CHECK_ATTRIBUTE_NORETURN || kind == CHECK_ATTRIBUTE_OVERLOADABLE ||
                          kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES || kind == CHECK_ATTRIBUTE_CPU_SPECIFIC ||
                          kind == CHECK_ATTRIBUTE_CPU_DISPATCH || kind == CHECK_ATTRIBUTE_CLEANUP || kind == CHECK_ATTRIBUTE_FALLTHROUGH ||
                          kind == CHECK_ATTRIBUTE_UNSEQUENCED || kind == CHECK_ATTRIBUTE_REPRODUCIBLE || kind == CHECK_ATTRIBUTE_NULLABILITY ||
                          kind == CHECK_ATTRIBUTE_REGPARM || kind == CHECK_ATTRIBUTE_MUSTTAIL || kind == CHECK_ATTRIBUTE_NOMERGE ||
                          kind == CHECK_ATTRIBUTE_SUPPRESS ||
                          (attribute->standard && (kind == CHECK_ATTRIBUTE_DEPRECATED || kind == CHECK_ATTRIBUTE_UNUSED || kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT));
    const Parser* parser = reader->parser;
    const bool scoped = attribute->standard && attribute->first != attribute->name_token;
    const String8 scope = scoped ? check_input_attribute_name(reader, parser->tokens.names[attribute->first]) : (String8){ "", 0 };
    const CheckInputFactRow* fact = excluded ? NULL : check_input_fact_row(scope, check_input_attribute_name(reader, attribute->name), scoped, attribute->standard);
    return !excluded && (unsigned)kind < 128 && !(fact != NULL && (fact->flags & BUSTER_CHECK_FACT_TYPE) != 0);
}

// The declaration attribute kinds of `attributes` that clang checks after a definition (check_input_attribute_after_kind),
// as two words of bits, into `kinds`.
static void check_input_attribute_after_kinds(CheckInputReader* reader, const CheckInputAttributes* attributes, uint64_t* kinds)
{
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored && !attribute->subject_ignored && check_input_attribute_after_kind(reader, attribute))
        {
            kinds[(uint32_t)attribute->kind >> 6] |= (uint64_t)1 << ((uint32_t)attribute->kind & 63);
        }
    }
}

// The layout summary of `attributes` again after some were dropped: its packed and the largest of its aligned.
static void check_input_attribute_summary(CheckInputAttributes* attributes)
{
    attributes->packed = false;
    attributes->alignment_log2 = 0;
    for (uint32_t i = 0; i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored;
        attributes->packed |= kept && attribute->kind == CHECK_ATTRIBUTE_PACKED;
        const bool aligned = kept && (attribute->kind == CHECK_ATTRIBUTE_ALIGNED || attribute->kind == CHECK_ATTRIBUTE_ALIGNAS) && attribute->resolved && attribute->value > 0;
        const uint8_t alignment = aligned ? (uint8_t)__builtin_ctzll((uint64_t)attribute->value) + 1 : 0;
        attributes->alignment_log2 = alignment > attributes->alignment_log2 ? alignment : attributes->alignment_log2;
    }
}

// clang's error for an attribute of a redeclaration that an earlier declaration's of its entity excludes (#147, as its
// mergeDeclAttributes takes them in): at the attribute, with the note at the earlier one. On the serial path, where the
// declarations of `previous`'s entity before `token` are found by their linked object.
static void check_input_attribute_merge_exclusions(CheckInputReader* reader, uint32_t token, uint32_t previous)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : 0;
    const uint32_t object = reader->objects != NULL ? reader->objects[previous] : 0;
    bool reported = false;
    for (uint32_t i = 0; !reader->lane && !reported && object != 0 && attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored && !attribute->keyword;
        const String8 name = kept ? check_input_attribute_name(reader, attribute->name) : (String8){ "", 0 };
        bool listed = false;
        for (uint32_t r = 0; kept && !listed && r < BUSTER_ARRAY_LENGTH(check_input_exclusions); r += 1)
        {
            listed = name.length == check_input_exclusions[r].length && memcmp(name.pointer, check_input_exclusions[r].spelling, name.length) == 0;
        }
        const bool scalar = reader->parser->options.scalar;
        const CheckSparseWide all = reader->output->attributes;
        for (uint32_t first = listed ? check_input_attributed_next(all, 0, token, scalar) : token; !reported && first < token;
             first = check_input_attributed_next(all, first + 16, token, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, token, scalar);
            for (uint32_t selected = lanes; !reported && selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const CheckInputAttributes* earlier = reader->objects[d] == object ? check_input_attributes_at(reader->output, d) : 0;
                const uint32_t excluding = earlier != 0 ? check_input_attribute_excluding_in(reader, name, earlier, earlier->count) : UINT32_MAX;
                if (excluding != UINT32_MAX)
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_EXCLUSION, 0);
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].note_token = earlier->records[excluding].first;
                        reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
                    }
                    reported = true;
                }
            }
        }
    }
}

// The attributes clang drops from a declaration after its entity's definition, with its warning that an attribute
// declaration must precede the definition (Sema's checkNewAttributesAfterDef): weak, constructor, destructor, used,
// retain, gnu_inline and unscoped standard noreturn; and on the serial path any other declaration attribute that the
// definition and the declarations before it lack the kind of (check_input_attribute_after_kind, #147), whose layout
// it then no longer changes. Earlier accepted attributes remain in their declaration records. A specifier's list is
// each declarator's: one declarator's is copied before its records are marked.
static void check_input_attribute_late(CheckInputReader* reader, uint32_t token, uint32_t previous, uint32_t point)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : 0;
    const bool after = attributes != 0 && reader->definition_origins != NULL && reader->definition_origins[previous] &&
                       !check_input_bit(reader->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED);
    bool late = false;
    for (uint32_t i = 0; after && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        late |= !attribute->ignored && ((reader->late_attributes && check_input_attribute_late_kind(attribute)) ||
                                        (!reader->lane && !attribute->subject_ignored && check_input_attribute_after_kind(reader, attribute)));
    }
    CheckInputAttributes* copy = late ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1) : 0;
    const bool copied = copy != 0 && check_input_attribute_copy(reader, attributes, copy);
    reader->output->resource |= late && !copied;
    // Deferred bodies replay in source order for these attributes. In particular, do not inspect token columns
    // owned by other lanes. Build an object-indexed mask once on the serial path, then update it as declarations bind.
    if (late && reader->lane)
    {
        check_input_lane_refuse(reader, true, "a body declares a late attribute");
    }
    if (copied && !reader->lane && reader->late_attribute_masks == NULL)
    {
        const uint32_t count = reader->parser->tokens.count;
        reader->late_attribute_masks = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)count + 1);
        reader->late_attribute_kinds = reader->late_attribute_masks != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint64_t, 2 * ((uint64_t)count + 1)) : NULL;
        reader->output->resource |= reader->late_attribute_kinds == NULL;
        reader->late_attribute_masks = reader->late_attribute_kinds != NULL ? reader->late_attribute_masks : NULL;
        if (reader->late_attribute_masks != NULL)
        {
            memset(reader->late_attribute_masks, 0, (uint64_t)count + 1);
            memset(reader->late_attribute_kinds, 0, 2 * ((uint64_t)count + 1) * sizeof(uint64_t));
            const bool scalar = reader->parser->options.scalar;
            const CheckSparseWide attributes = reader->output->attributes;
            for (uint32_t first = check_input_attributed_next(attributes, 0, token, scalar); first < token; first = check_input_attributed_next(attributes, first + 16, token, scalar))
            {
                const uint32_t lanes = check_input_attributed_group(attributes, first, token, scalar);
                for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
                {
                    const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                    const uint32_t object = reader->objects[d];
                    if (object != 0 && object <= count)
                    {
                        reader->late_attribute_masks[object] |= check_input_attribute_late_mask(check_input_attributes_at(reader->output, d), reader->output->function_flags[d]);
                        check_input_attribute_after_kinds(reader, check_input_attributes_at(reader->output, d), reader->late_attribute_kinds + 2 * (uint64_t)object);
                    }
                }
            }
        }
    }
    bool dropped = false;
    for (uint32_t i = 0; copied && !reader->lane && reader->late_attribute_masks != NULL && i < copy->count; i += 1)
    {
        CheckInputAttribute* attribute = copy->records + i;
        const bool late_kind = reader->late_attributes && check_input_attribute_late_kind(attribute);
        const bool after_kind = !late_kind && !attribute->subject_ignored && check_input_attribute_after_kind(reader, attribute);
        const uint64_t* kinds = reader->late_attribute_kinds + 2 * (uint64_t)reader->objects[previous];
        const bool had = after_kind && ((kinds[(uint32_t)attribute->kind >> 6] >> ((uint32_t)attribute->kind & 63)) & 1) != 0;
        if (!attribute->ignored && (late_kind || (after_kind && !had)))
        {
            const bool inherited = late_kind && (reader->late_attribute_masks[reader->objects[previous]] & check_input_attribute_late_bit(attribute)) != 0;
            attribute->ignored = true;
            dropped = true;
            const bool missing_inline = attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE &&
                                        (reader->output->function_flags[token] & CHECK_INPUT_FUNCTION_INLINE) == 0;
            if (!inherited || missing_inline)
            {
                const CheckWarningEvent event = {
                    .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                    .kind = missing_inline ? CHECK_WARNING_EVENT_GNU_INLINE_WITHOUT_INLINE : CHECK_WARNING_EVENT_LATE_ATTRIBUTE,
                    .token = attribute->name_token,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = point,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                const CheckWarningNote note = {
                    .kind = CHECK_WARNING_NOTE_PREVIOUS_DEFINITION,
                    .token = reader->definition_origins[previous] - 1,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, missing_inline ? NULL : &note, missing_inline ? 0 : 1);
            }
        }
    }
    if (copied)
    {
        if (dropped)
        {
            check_input_attribute_summary(copy);
        }
        check_input_set_attributes_at(reader->output, token, copy);
    }
    check_input_attribute_merge_exclusions(reader, token, previous);
}

// Whether a parameter of `type` is one nonnull's index may name, as clang's isValidPointerAttrType has it: a pointer, or a
// transparent union with a pointer member.
static bool check_input_nonnull_pointer(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_strip_aligned(types, type);
    const uint8_t kind = type_is_valid(types, bare) ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    bool pointer = kind == BUSTER_TYPE_KIND_POINTER;
    if (kind == BUSTER_TYPE_KIND_UNION && type_is_transparent_union(types, bare))
    {
        const uint32_t tag = types->types.index[type_index(bare)];
        const uint32_t begin = types->tags.member_begin[tag];
        for (uint32_t m = 0; m < types->tags.member_count[tag]; m += 1)
        {
            const TypeId member = type_strip_aligned(types, types->members.type[begin + m]);
            pointer |= type_is_valid(types, member) && type_kind(types, member) == BUSTER_TYPE_KIND_POINTER;
        }
    }
    return pointer;
}

// One of nonnull's warnings (CHECK_WARNING_EVENT_NONNULL_*, #147), at the attribute's first token (a C23 one's scope).
static void check_input_nonnull_warning(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningEventKind kind)
{
    check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, kind);
}

// Whether an attribute was written by a macro's expansion (its first token's history), where clang's location isn't a
// file's (SourceLocation::isFileID).
static bool check_input_attribute_expanded(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const uint32_t source = attribute->first < reader->parser->tokens.count ? reader->parser->tokens.sources[attribute->first] : UINT32_MAX;
    const bool history = source < reader->pp->output.count && pp_history_ensure(reader->pp);
    return history && pp_history_at(reader->pp, source) != 0;
}

// nonnull on a parameter (#147, clang's handleNonNullAttrParameter): with arguments, unless the parameter is a function
// pointer's (whose own parameters they index), clang's warning that a parameter's takes none; without, its warning where
// the parameter's (adjusted) type is no pointer.
static void check_input_attribute_nonnull_parameter(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    const TypeTable* types = reader->types;
    const TypeId bare = type_strip_aligned(types, type);
    const bool function_pointer = type_is_valid(types, bare) && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER &&
                                  type_kind(types, type_strip_aligned(types, type_element(types, bare))) == BUSTER_TYPE_KIND_FUNCTION;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const bool nonnull = attribute->kind == CHECK_ATTRIBUTE_NONNULL && !attribute->ignored;
        if (nonnull && attribute->argument_count != 0 && !function_pointer)
        {
            check_input_nonnull_warning(reader, attribute, CHECK_WARNING_EVENT_NONNULL_PARAMETER_ARGUMENTS);
            attribute->ignored = true;
        }
        else if (nonnull && attribute->argument_count == 0 && !check_input_nonnull_pointer(types, type))
        {
            check_input_nonnull_warning(reader, attribute, CHECK_WARNING_EVENT_NONNULL_POINTERS);
            attribute->ignored = true;
        }
    }
}

// Run after a full declarator is available, including a function-pointer typedef. Prefix attributes cannot perform
// these checks before the function's parameters have been read. Warnings with no C semantic effect are retained as
// ignored metadata; malformed parameter indices/types are errors. Per-argument values must already be evaluated.
static bool check_input_attribute_function(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    bool valid = type != 0 && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE;
    TypeId function = valid ? type_strip_aligned(reader->types, type) : 0;
    while (function != 0 && type_kind(reader->types, function) == BUSTER_TYPE_KIND_POINTER)
    {
        function = type_strip_aligned(reader->types, reader->types->types.base[type_index(function)]);
    }
    uint32_t count = 0;
    const bool is_function = function != 0 && type_kind(reader->types, function) == BUSTER_TYPE_KIND_FUNCTION;
    const bool variadic = is_function && (reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
    const TypeId* parameters = is_function ? type_parameters(reader->types, function, &count) : 0;
    const TypeId result = is_function ? type_return(reader->types, function) : 0;
    const bool prototyped = is_function && (reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    for (uint32_t i = 0; valid && is_function && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        // One whose subjects are prototyped functions alone (format and the like) on a K&R function's type, which clang
        // ignores with its subject's warning before reading its arguments (check_input_attribute_subjects).
        const bool scoped = attribute->standard && attribute->first != attribute->name_token;
        const CheckInputSubjectRow* row = attribute->keyword || prototyped ? NULL :
            check_input_subject_row(scoped ? check_input_attribute_name(reader, reader->parser->tokens.names[attribute->first]) : (String8){ "", 0 },
                                    check_input_attribute_name(reader, attribute->name), scoped, attribute->standard);
        if (row != NULL && (row->subjects & (BUSTER_CHECK_SUBJECT_PROTOTYPED | BUSTER_CHECK_SUBJECT_FUNCTION | BUSTER_CHECK_SUBJECT_FUNCTION_LIKE)) == BUSTER_CHECK_SUBJECT_PROTOTYPED)
        {
            continue;
        }
        if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
        {
            check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_SUBJECT);
            valid = false;
        }
        const bool allocation = attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE || attribute->kind == CHECK_ATTRIBUTE_ALLOC_ALIGN;
        const bool nonnull = attribute->kind == CHECK_ATTRIBUTE_NONNULL;
        const bool format = attribute->kind == CHECK_ATTRIBUTE_FORMAT || attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG;
        bool recognized_format = true;
        if (attribute->kind == CHECK_ATTRIBUTE_FORMAT)
        {
            const String8 name = check_input_attribute_name(reader, reader->parser->tokens.names[attribute->argument_first]);
            recognized_format = check_input_attribute_equal(name, "printf") || check_input_attribute_equal(name, "scanf") ||
                                check_input_attribute_equal(name, "strftime") || check_input_attribute_equal(name, "strfmon") ||
                                check_input_attribute_equal(name, "gnu_printf") || check_input_attribute_equal(name, "gnu_scanf") ||
                                check_input_attribute_equal(name, "gnu_strftime") || check_input_attribute_equal(name, "gnu_strfmon") ||
                                check_input_attribute_equal(name, "ms_printf") || check_input_attribute_equal(name, "ms_scanf");
        }
        if (!recognized_format || (allocation && type_kind(reader->types, type_strip_aligned(reader->types, result)) != BUSTER_TYPE_KIND_POINTER))
        {
            attribute->ignored = true;
        }
        else if (allocation || nonnull || format)
        {
            uint32_t begin = attribute->kind == CHECK_ATTRIBUTE_FORMAT ? 1 : 0;
            uint32_t end = attribute->kind == CHECK_ATTRIBUTE_FORMAT ? 2 : attribute->argument_count;
            bool any_pointer = false;
            bool out_of_bounds = false;
            uint32_t failed = begin;
            for (uint32_t argument = begin; valid && argument < end; argument += 1)
            {
                const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
                valid = scalar ? attribute->resolved : attribute->argument_values != 0;
                const int64_t index = valid ? (scalar ? attribute->value : attribute->argument_values[argument]) : 0;
                const bool ellipsis_argument = nonnull && variadic && index > count;
                // clang's checkFunctionOrMethodParameterIndex: an index of no parameter is its error by the argument's place.
                out_of_bounds = valid && !(index > 0 && (index <= count || ellipsis_argument));
                valid &= !out_of_bounds;
                TypeId parameter = valid && !ellipsis_argument ? type_strip_aligned(reader->types, parameters[index - 1]) : 0;
                const uint8_t kind = parameter != 0 ? type_kind(reader->types, parameter) : BUSTER_TYPE_KIND_NONE;
                if (valid && allocation)
                {
                    valid = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_ENUM;
                }
                if (valid && format)
                {
                    valid = check_input_attribute_string_type(reader->types, parameter);
                }
                // nonnull's index of a parameter that is no pointer: clang's warning, and the index is dropped (#147).
                const bool pointer = ellipsis_argument || (parameter != 0 && check_input_nonnull_pointer(reader->types, parameter));
                if (valid && nonnull && !pointer)
                {
                    check_input_nonnull_warning(reader, attribute, CHECK_WARNING_EVENT_NONNULL_POINTERS);
                }
                any_pointer |= valid && pointer;
                failed = valid ? failed : argument;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT)
            {
                const int64_t first_checked = attribute->argument_values[2];
                const int64_t format_index = attribute->argument_values[1];
                out_of_bounds = first_checked > (int64_t)count + 1;
                valid = first_checked == 0 || (first_checked > format_index && first_checked <= (int64_t)count + 1);
                failed = 2;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG)
            {
                valid = check_input_attribute_string_type(reader->types, result);
            }
            // nonnull of no pointer index, or of none: clang's warning where the function has no pointer parameter and isn't
            // variadic, unless a macro wrote the attribute.
            if (valid && nonnull && !any_pointer)
            {
                bool pointers = variadic;
                for (uint32_t p = 0; p < count; p += 1)
                {
                    pointers |= check_input_nonnull_pointer(reader->types, parameters[p]);
                }
                if (!pointers && !check_input_attribute_expanded(reader, attribute))
                {
                    check_input_nonnull_warning(reader, attribute, CHECK_WARNING_EVENT_NONNULL_NO_POINTERS);
                }
                attribute->ignored |= attribute->argument_count != 0;
            }
            if (!valid && out_of_bounds && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
            {
                // clang's "'X' attribute parameter N is out of bounds", at the attribute.
                attributes->error = CHECK_ATTRIBUTE_ERROR_PARAMETER;
                attributes->error_first = attribute->first;
                attributes->error_end = attribute->end;
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_OUT_OF_BOUNDS, failed + 1);
            }
            else if (!valid)
            {
                uint32_t first = attribute->name_token;
                uint32_t after = attribute->end;
                check_input_attribute_argument(attribute, failed, &first, &after);
                check_input_attribute_error(reader, attributes, first, after, CHECK_ATTRIBUTE_ERROR_PARAMETER);
            }
        }
    }
    return valid;
}

static uint32_t check_input_skip_attributes(const Parser* parser, uint32_t token, uint32_t end);

// The declaration a deprecated or unavailable attribute's uses are counted by (#147): a tag's first declaration's name
// for a tag keyword's attributes (at `token`) or a tag's name, its object's first declaration for a function's or a
// variable's, and the declaration itself for a typedef or an enumerator.
static uint32_t check_input_deprecated_key(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t kind = parser->tokens.kinds[token];
    const uint32_t name = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ? check_input_skip_attributes(parser, token + 1, count) : token;
    const uint32_t declared = name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ? reader->output->declarations[name] : BUSTER_CHECK_NONE;
    const uint32_t object = declared < count && reader->objects != NULL ? reader->objects[declared] : 0;
    return object != 0 && object - 1 < count ? object - 1 : declared;
}

// The tag key (check_input_deprecated_key) of the enum whose body declares the enumerator at `enumerator`: its `{`'s, back
// past the earlier enumerators, and its keyword's before that; `count` for none.
static uint32_t check_input_deprecated_enum(const CheckInputReader* reader, uint32_t enumerator)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t i = enumerator;
    uint32_t depth = 0;
    bool scanning = true;
    while (scanning && i > 0)
    {
        i -= 1;
        const uint8_t kind = parser->tokens.kinds[i];
        scanning = !(kind == PARSE_KIND_L_BRACE && depth == 0);
        depth = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE ? depth + 1 :
                (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE) && depth != 0 ? depth - 1 : depth;
    }
    uint32_t keyword = i;
    while (!scanning && keyword > 0 && parser->tokens.kinds[keyword] != PARSE_KIND_ENUM)
    {
        keyword -= 1;
    }
    return !scanning && parser->tokens.kinds[keyword] == PARSE_KIND_ENUM ? check_input_deprecated_key(reader, keyword) : count;
}

// The deprecated or unavailable record of `attributes`, kept, or null.
static const CheckInputAttribute* check_input_deprecated_record(const CheckInputAttributes* attributes)
{
    const CheckInputAttribute* result = NULL;
    for (uint32_t i = 0; attributes != NULL && result == NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool marks = attribute->kind == CHECK_ATTRIBUTE_DEPRECATED || attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE;
        result = marks && !attribute->ignored && !attribute->subject_ignored ? attribute : NULL;
    }
    return result;
}

// clang's diagnostics of a use of what a deprecated or unavailable attribute marks (#147, DiagnoseUseOfDecl's): at each
// use of a function, a variable, a typedef, an enumerator or a tag after the declaration that marks it, "'X' is
// deprecated" (-Wdeprecated-declarations), or the error "'X' is unavailable", with the attribute's message after a colon
// and a note at the attribute. None in the body of a function itself deprecated, nor in a bare declaration of a tag. A
// field's use (`m.x`) isn't diagnosed: its declaration is found by the expression's type, which this pass doesn't have.
// The uses are the tokens the bindings resolved to another declaration than their own (CheckInput.declarations); the
// marks are found once, by the declaration a use counts by (check_input_deprecated_key).
static void check_input_deprecated_uses(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    uint32_t* marks = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    reader->output->resource |= marks == NULL;
    if (marks != NULL)
    {
        memset(marks, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        const bool scalar = parser->options.scalar;
        const CheckSparseWide all = reader->output->attributes;
        for (uint32_t first = check_input_attributed_next(all, 0, count, scalar); first < count; first = check_input_attributed_next(all, first + 16, count, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, count, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t key = check_input_deprecated_record(check_input_attributes_at(reader->output, d)) != NULL ? check_input_deprecated_key(reader, d) : count;
                if (key < count && (marks[key] == 0 || d + 1 < marks[key]))
                {
                    marks[key] = d + 1;
                }
            }
        }
        // The function whose body a use is in, if it is deprecated (as clang's context is), and where that body ends.
        uint32_t function = BUSTER_CHECK_NONE;
        uint32_t body_end = 0;
        bool deprecated_body = false;
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            const uint8_t role = parser->tree.roles[t];
            const bool inside = t < body_end;
            function = !inside && kind == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[t] == C_BINDING_FUNCTION ? t : function;
            if (!inside && role == PARSE_ROLE_OPEN_BODY && parser->tree.parents[t] < count)
            {
                // Deprecated by any of its declarations up to its definition's.
                const uint32_t function_key = function != BUSTER_CHECK_NONE ? check_input_deprecated_key(reader, function) : count;
                body_end = parser->tree.parents[t];
                deprecated_body = function_key < count && marks[function_key] != 0 && marks[function_key] - 1 <= function;
            }
            const uint32_t declared = kind == PARSE_KIND_IDENTIFIER ? reader->output->declarations[t] : BUSTER_CHECK_NONE;
            const bool tag = role == PARSE_ROLE_TAG;
            const uint8_t next = t + 1 < count ? parser->tokens.kinds[t + 1] : PARSE_KIND_END;
            // An overloadable function's name is its overload set's, whose resolution clang checks otherwise.
            const bool overloaded = reader->output->overload_heads != NULL && reader->output->overload_heads[t] != 0;
            const bool use = declared < count && declared != t && role != PARSE_ROLE_MEMBER && !overloaded &&
                             (tag ? next != PARSE_KIND_SEMI && next != PARSE_KIND_L_BRACE : reader->declaration_kinds[t] == 0);
            uint32_t key = use ? (tag ? declared : check_input_deprecated_key(reader, t)) : count;
            // An enumerator of a deprecated enum is deprecated by its enum (the note names the enum).
            if (use && key < count && marks[key] == 0 && reader->declaration_kinds[declared] == C_BINDING_ENUMERATOR)
            {
                key = check_input_deprecated_enum(reader, declared);
            }
            const uint32_t marking = key < count ? marks[key] : 0;
            const bool marked = marking != 0 && marking - 1 < t && !(inside && deprecated_body);
            // A use in the declaration of something itself deprecated, as clang's delayed diagnostics drop it: one before
            // the declaration's first declarator, which a deprecated attribute marks by its own token. Looked for only at a
            // marked use, which are few.
            uint32_t declarator = marked && !inside ? t + 1 : count;
            while (declarator < count && reader->declaration_kinds[declarator] == 0 && parser->tokens.kinds[declarator] != PARSE_KIND_SEMI &&
                   parser->tokens.kinds[declarator] != PARSE_KIND_L_BRACE)
            {
                declarator += 1;
            }
            const uint32_t declarator_key = declarator < count && reader->declaration_kinds[declarator] != 0 ? check_input_deprecated_key(reader, declarator) : count;
            const bool deprecated_declaration = declarator_key < count && marks[declarator_key] != 0 && marks[declarator_key] - 1 <= declarator;
            const CheckInputAttribute* attribute = marked && !deprecated_declaration ?
                                                   check_input_deprecated_record(check_input_attributes_at(reader->output, marking - 1)) : NULL;
            // The attribute's message, its first argument's string.
            const uint32_t message = attribute != NULL && attribute->argument_first < attribute->argument_end &&
                                     parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_STRING ? attribute->argument_first : BUSTER_CHECK_WARNING_NO_SOURCE;
            if (attribute != NULL && attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE)
            {
                // Its note is at the declaration, not the attribute.
                const uint32_t before = reader->output->report_count;
                check_input_report(reader, t, CHECK_INPUT_UNAVAILABLE_USE);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].note_token = marking - 1;
                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_UNAVAILABLE_HERE;
                    reader->output->reports[before].detail = message != BUSTER_CHECK_WARNING_NO_SOURCE ? message + 1 : 0;
                }
            }
            else if (attribute != NULL)
            {
                const CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
                    .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
                    .token = t,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = t + 1,
                    .payload = { .name = parser->tokens.names[t], .token = message },
                };
                const CheckWarningNote note =
                {
                    .kind = CHECK_WARNING_NOTE_DEPRECATED_HERE,
                    .token = attribute->first,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .name = parser->tokens.names[key], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, &note, 1);
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}

