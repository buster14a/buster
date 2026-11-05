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
    // An attribute of clang's thread-safety analysis (#147, check_input_thread_attribute), which changes nothing here past
    // its handler's checks.
    CHECK_ATTRIBUTE_THREAD_SAFETY,
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
    // A generic attribute's integer arguments (xray_log_args's, the function effects', the GPU kernels' sizes), evaluated
    // where its attributes are read (check_input_read_attributes), not where its handler checks them, which a tag's subjects
    // reach from the constant evaluator: whether the first is an integer constant (its value in `value`), and bit N of
    // `constants` the Nth's (its value in `argument_values`). Bit N of `wide` is where the GPU kernels' Nth (bit 0 the
    // first's) needs more than 32 bits (check_input_constant_u32), its value then those bits as clang prints them.
    bool constant_argument;
    uint8_t constants;
    uint8_t wide;
    // An argument's error clang's handler gives once the attribute's subject takes it (#147: tls_model's model, constructor's,
    // destructor's and assume_aligned's integers), which the attributes' reader made and moved here
    // (check_input_attribute_defer), for check_input_attribute_subjects to give; or NULL.
    struct CheckInputReport* deferred;
    // A type argument's type (#147: vec_type_hint's and preferred_type's, check_input_type_argument), read where its
    // attributes are read (check_input_read_attributes), or 0.
    TypeId argument_type;
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
    // Where the first warning of an attribute the list dropped completes, plus one, or 0 for none: one clang gives where it
    // processes the declaration's attributes, after its type's (#147, check_input_attribute_type_warnings): an unknown
    // one's (after its first token), another language's or a type name's declaration attribute's (at it, so one before).
    uint32_t unknown_first;
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
    // Other targets' conventions (#147), which x86-64's checkCallingConvention warns of and ignores; pcs's count and string
    // are checked where it meets a function or a tag, as clang's CheckCallingConvAttr checks them
    // (check_input_attribute_pcs); the kernels' are declarations' attributes, of functions alone.
    { "aarch64_sve_pcs", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "aarch64_vector_pcs", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "m68k_rtd", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "pcs", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_STRING_LIST, 0, 255 },
    { "amdgpu_kernel", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "device_kernel", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "nvptx_kernel", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
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
    // (A reader of no preprocessor, a unit test's, has no names.)
    const InternTable* table = reader->pp != NULL ? reader->pp->table : NULL;
    String8 result = { 0 };
    if (table != NULL && name != 0 && name < table->count)
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

// clang's errors of an attribute's arguments (#147), CHECK_INPUT_ATTRIBUTE_ARGUMENT's in its detail's low byte (a parameter's
// place from 1 above it), each its handler's, at the attribute's first token or at the argument as clang places it.
typedef enum CheckAttributeArgumentError
{
    // "'X' attribute requires integer constant" (aligned's and align_value's), at the argument.
    CHECK_ARGUMENT_INTEGER_CONSTANT,
    // "'X' attribute requires an integer constant", at the attribute.
    CHECK_ARGUMENT_AN_INTEGER_CONSTANT,
    // "'X' attribute requires parameter N to be an integer constant", at the attribute.
    CHECK_ARGUMENT_PARAMETER_INTEGER,
    // "ext_vector_type attribute requires an integer constant", at the attribute.
    CHECK_ARGUMENT_EXT_VECTOR_INTEGER,
    // "requested alignment is not a power of 2", at the attribute.
    CHECK_ARGUMENT_ALIGNMENT_POWER,
    // "zero vector size", at the attribute.
    CHECK_ARGUMENT_ZERO_VECTOR,
    // "'regparm' parameter must be between 0 and 6 inclusive", at the attribute.
    CHECK_ARGUMENT_REGPARM_RANGE,
    // "'X' attribute parameter N is out of bounds", at the attribute.
    CHECK_ARGUMENT_OUT_OF_BOUNDS,
    // "'sentinel' parameter 1 less than zero" and "'sentinel' parameter 2 not 0 or 1", at the attribute.
    CHECK_ARGUMENT_SENTINEL_NEGATIVE,
    CHECK_ARGUMENT_SENTINEL_NULL,
    // "'X' attribute argument may only refer to a function parameter of integer type", at the argument.
    CHECK_ARGUMENT_PARAMETER_TYPE,
    // "expected string literal as argument of 'X' attribute", at the argument.
    CHECK_ARGUMENT_STRING_LITERAL,
    // "'X' attribute requires a string", at the argument.
    CHECK_ARGUMENT_STRING,
    // "'X' attribute requires an identifier", at the argument.
    CHECK_ARGUMENT_IDENTIFIER,
    // "'X' attribute requires parameter N to be an identifier", at the attribute.
    CHECK_ARGUMENT_PARAMETER_IDENTIFIER,
    // mode's (check_input_attribute_mode), at the attribute, the mode's name after it (the number its offset): "unknown
    // machine mode 'M'", "unsupported machine mode 'M'", "mode 'M' is not supported for enumeration types", "mode attribute
    // only supported for integer and floating-point types", "type of machine mode does not match type of base type" and
    // "type of machine mode does not support base vector types".
    CHECK_ARGUMENT_UNKNOWN_MODE,
    CHECK_ARGUMENT_UNSUPPORTED_MODE,
    CHECK_ARGUMENT_MODE_ENUM_VECTOR,
    CHECK_ARGUMENT_MODE_PRIMITIVE,
    CHECK_ARGUMENT_MODE_TYPE,
    CHECK_ARGUMENT_MODE_COMPLEX_VECTOR,
    // tls_model's "tls_model must be ..." of another string, at the argument.
    CHECK_ARGUMENT_TLS_MODEL,
    // format's and format_arg's "format argument not a string type", format_arg's "function does not return string type"
    // and a strftime format's "strftime format attribute requires 3rd parameter to be 0", at the attribute.
    CHECK_ARGUMENT_FORMAT_STRING,
    CHECK_ARGUMENT_FORMAT_RESULT,
    CHECK_ARGUMENT_STRFTIME,
    // A type argument's parser errors (#147): "unknown type name 'T'" (the name after the attribute's first token by the
    // number), "expected a type" and "expected ')'", at the argument.
    CHECK_ARGUMENT_UNKNOWN_TYPE,
    CHECK_ARGUMENT_EXPECTED_TYPE,
    CHECK_ARGUMENT_EXPECTED_PAREN,
    // pcs's "invalid PCS type", at the attribute; a variadic function's convention that takes none, "variadic function
    // cannot use C calling convention" (the convention, CheckInputCallingConvention, the number).
    CHECK_ARGUMENT_PCS_TYPE,
    CHECK_ARGUMENT_VARIADIC_CONVENTION,
    // A GPU kernel's convention off a function: "'X' attribute only applies to functions", at the attribute.
    CHECK_ARGUMENT_KERNEL_FUNCTIONS,
    // always_destroy's and no_destroy's "X attribute can only be applied to a variable with static or thread storage
    // duration" (the number 1 for always_destroy's), at the variable's name; personality's "'personality' argument is not
    // a function", at it; model's "code model 'M' is not supported on this target", at the string; matrix_type's
    // "matrix types extension is disabled. Pass -fenable-matrix to enable it", at the attribute.
    CHECK_ARGUMENT_DESTROY_STORAGE,
    CHECK_ARGUMENT_PERSONALITY,
    CHECK_ARGUMENT_CODE_MODEL,
    CHECK_ARGUMENT_MATRIX_DISABLED,
    // A function effect before a declaration, "'X' attribute cannot be applied to a declaration", at the attribute; its
    // argument that is no integer constant, "X attribute requires an integer constant" (its name alone), at it.
    CHECK_ARGUMENT_INVALID_ON_DECLARATION,
    CHECK_ARGUMENT_EFFECT_CONSTANT,
    // The GPU kernels' (check_input_attribute_handled): "'X' attribute requires a non-negative integral compile time
    // constant expression" at the argument's expression (check_input_expression_location); "'X' attribute must be greater
    // than 0" at the attribute; AMDGPU's "'X' attribute requires parameter N to be an integer constant" (the applied
    // attribute's name, N from 0) at the attribute, and "'X' attribute argument is invalid: max must be 0 since min is 0"
    // (or "min must not be greater than max") there; a kernel's attribute on another function, "attribute 'X' can only be
    // applied to an OpenCL kernel function" or "'X' attribute only applies to kernel functions", at the function's name
    // (check_input_attribute_kernels); an argument of more than 32 bits, "integer constant expression evaluates to value V
    // that cannot be represented in a 32-bit unsigned integer type" (CheckInputReport.value), at its expression.
    CHECK_ARGUMENT_NON_NEGATIVE,
    CHECK_ARGUMENT_GREATER_ZERO,
    CHECK_ARGUMENT_PARAMETER_INTEGER_APPLIED,
    CHECK_ARGUMENT_INVALID_MAX_ZERO,
    CHECK_ARGUMENT_INVALID_MIN_MAX,
    CHECK_ARGUMENT_OPENCL_KERNEL,
    CHECK_ARGUMENT_KERNEL_ONLY,
    CHECK_ARGUMENT_TOO_LARGE,
    // An alignment specifier where clang's AddAlignedAttr refuses it (check_input_alignas_error), at its keyword: "'_Alignas'
    // attribute cannot be applied to a function parameter" (the number 0; 1 "a variable with 'register' storage class",
    // 3 "a bit-field"); "'_Alignas' attribute only applies to variables and fields"; and clang's CheckAlignasUnderalignment,
    // "requested alignment is less than minimum alignment of N for type 'T'" (CheckInputReport.type).
    CHECK_ARGUMENT_ALIGNAS_DECLARATION,
    CHECK_ARGUMENT_ALIGNAS_SUBJECT,
    CHECK_ARGUMENT_ALIGNAS_UNDERALIGNED,
    // Static analyses' and Objective-C's handlers (check_input_attribute_annotations): acquire_handle's on an integer
    // parameter, "attribute only applies to output parameters"; type_visibility's off a type, "'X' attribute only applies
    // to types and namespaces"; lifetime_capture_by's of no argument, "'X' attribute specifies no capturing entity", of an
    // argument that names no parameter, "'X' attribute argument 'A' is not a known function parameter; must be a function
    // parameter, 'this', 'global' or 'unknown'" (at it), of its own parameter's name, "'lifetime_capture_by' argument
    // references itself", of `this`, "'lifetime_capture_by' argument references unavailable implicit 'this'";
    // lifetime_capture_by_this's "'X' attribute requires an implicit object parameter"; ownership_returns' function of no
    // pointer result, "'X' attribute only applies to functions that return a pointer", an ownership's parameter of another
    // type, "'X' attribute only applies to pointer arguments" (1 "integer"), two of other kinds of one parameter, "'X' and
    // 'Y' attributes are not compatible" (Y's token in CheckInputReport.value); NSObject's typedef of no pointer to void or a
    // record, "'NSObject' attribute is for pointer types only", at its name; objc_precise_lifetime's, "objc_precise_lifetime
    // only applies to retainable types; type here is 'T'" (CheckInputReport.type).
    CHECK_ARGUMENT_OUTPUT_PARAMETER,
    CHECK_ARGUMENT_TYPES_NAMESPACES,
    CHECK_ARGUMENT_NO_CAPTURE,
    CHECK_ARGUMENT_CAPTURE_UNKNOWN,
    CHECK_ARGUMENT_CAPTURE_ITSELF,
    CHECK_ARGUMENT_CAPTURE_THIS,
    CHECK_ARGUMENT_IMPLICIT_OBJECT,
    CHECK_ARGUMENT_OWNERSHIP_RETURN,
    CHECK_ARGUMENT_OWNERSHIP_TYPE,
    CHECK_ARGUMENT_OWNERSHIP_CONFLICT,
    CHECK_ARGUMENT_NSOBJECT_POINTER,
    CHECK_ARGUMENT_PRECISE_LIFETIME,
    // arm_sve_vector_bits's and riscv_rvv_vector_bits's wherever a type takes them, "'X' attribute is not supported on
    // targets missing 'sve'" (1 'zve32x'), "...; specify an appropriate -march= or -mcpu="; clang_builtin_alias's "'X'
    // attribute can only be applied to a ARM, HLSL, SPIR-V or RISC-V builtin"; blocks' "'__block' is not allowed on a
    // nonlocal variable"; and the statements' (check_input_statement_attribute_kinds): an argument of no integer constant,
    // "expression is not an integer constant expression" (at it); code_align's "'X' attribute requires an integer argument
    // which is a constant power of two between 1 and 4096 inclusive; provided argument was V" (CheckInputReport.value);
    // a loop's off one, "'X' attribute only applies to 'for', 'while', and 'do' statements"; opencl_unroll_hint's "'X'
    // attribute requires a positive integral compile time constant expression"; atomic's "invalid argument 'A' to atomic
    // attribute; valid options are: ..." (the argument's token in CheckInputReport.value) and "'X' attribute only applies to
    // compound statements"; a second code_align, "conflicting loop attribute 'X'", with its note at the first.
    CHECK_ARGUMENT_TARGET_FEATURE,
    CHECK_ARGUMENT_BUILTIN_ALIAS,
    CHECK_ARGUMENT_BLOCK_NONLOCAL,
    CHECK_ARGUMENT_NOT_ICE,
    CHECK_ARGUMENT_CODE_ALIGN,
    CHECK_ARGUMENT_LOOP_ONLY,
    CHECK_ARGUMENT_POSITIVE,
    CHECK_ARGUMENT_ATOMIC_OPTION,
    CHECK_ARGUMENT_COMPOUND_ONLY,
    CHECK_ARGUMENT_LOOP_CONFLICT,
    // assume's (check_input_statement_attribute_kinds): off an empty statement, "'X' attribute only applies to empty
    // statements"; an assignment or a comma not in parentheses, "use of this expression in an 'assume' attribute requires
    // parentheses" (at its operator); a value of a structure or union, "value of type 'T' is not contextually convertible
    // to 'bool'" (CheckInputReport.type).
    CHECK_ARGUMENT_EMPTY_ONLY,
    CHECK_ARGUMENT_ASSUME_PARENTHESES,
    CHECK_ARGUMENT_NOT_BOOL,
    // A type argument's (#147, CheckInputAttribute.argument_type): vec_type_hint's of no extended vector's, floating or
    // integer type (bool's none), "a non-vector or non-vectorizable scalar type is an invalid argument to attribute
    // 'X'", at the attribute; preferred_type's incomplete one, "incomplete type 'T' where a complete type is required"
    // (CheckInputReport.type), at the type, with a tag's note at its declaration (CHECK_INPUT_NOTE_FORWARD_DECLARATION).
    CHECK_ARGUMENT_VEC_TYPE_HINT,
    CHECK_ARGUMENT_INCOMPLETE_TYPE,
    CHECK_ARGUMENT_COUNT,
} CheckAttributeArgumentError;

// An attribute's argument's error (above), the declaration's first attribute error: reported at `token`, the attribute
// named by its first token (the error's span the attribute's, or the argument's token where it is reported there).
static void check_input_attribute_argument_error(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t token,
                                                 CheckAttributeArgumentError error, uint32_t number)
{
    if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = token;
        attributes->error_end = token == attribute->first ? attribute->end : token + 1;
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, token, CHECK_INPUT_ATTRIBUTE_ARGUMENT, (uint32_t)error | number << 8);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].attribute = attribute->first;
        }
    }
}

// An alignment specifier's error (#147, C11 6.7.5p2: CHECK_ARGUMENT_ALIGNAS_*), at the keyword of the declaration's first
// (its specifiers', `common`, then its declarator's, `own`): on a parameter, a register variable or a bit-field
// (`declaration` 0, 1 or 3, as clang numbers them) "cannot be applied to", on a typedef or a function (BUSTER_CHECK_NONE)
// "only applies to variables and fields"; with `type` (and `declaration` its alignment in bytes) "requested alignment
// is less than minimum alignment" (`spelling` the declaration's typedef's, check_input_typedef_spelling).
static void check_input_alignas_error(CheckInputReader* reader, CheckInputAttributes* common, CheckInputAttributes* own, uint32_t declaration, TypeId type,
                                      uint32_t spelling)
{
    CheckInputAttribute* found = NULL;
    CheckInputAttributes* holder = NULL;
    for (uint32_t list = 0; found == NULL && list < 2; list += 1)
    {
        CheckInputAttributes* attributes = list == 0 ? common : own;
        for (uint32_t i = 0; attributes != NULL && found == NULL && i < attributes->count; i += 1)
        {
            found = attributes->records[i].kind == CHECK_ATTRIBUTE_ALIGNAS ? attributes->records + i : NULL;
            holder = found != NULL ? attributes : holder;
        }
    }
    const CheckAttributeArgumentError error = type != 0 ? CHECK_ARGUMENT_ALIGNAS_UNDERALIGNED :
                                              declaration == BUSTER_CHECK_NONE ? CHECK_ARGUMENT_ALIGNAS_SUBJECT : CHECK_ARGUMENT_ALIGNAS_DECLARATION;
    const uint32_t before = reader->output->report_count;
    if (found != NULL)
    {
        check_input_attribute_argument_error(reader, holder, found, found->first, error, declaration == BUSTER_CHECK_NONE ? 0 : declaration);
    }
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].type = type;
        reader->output->reports[before].type_spelling = spelling;
    }
}

// clang's CheckAlignasUnderalignment (#147, C11 6.7.5p4), on a declaration's or a member's own type, its declarator's
// (`type`, its name `name`), where one of its alignments is _Alignas's: the greatest of them all, GNU aligned's too, less
// than the type's is an error at the keyword, naming the type as clang prints it (its typedef's spelling). Whether it is
// fine. The specifiers' type isn't checked (check_input_specifiers): a declarator makes another type of it.
static bool check_input_alignas_underaligned(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type, uint32_t name)
{
    const bool checked = attributes != 0 && attributes->required_alignment_log2 != 0 && type != 0 && type_kind(reader->types, type) != BUSTER_TYPE_KIND_FUNCTION;
    const uint32_t natural = checked ? type_alignment_log2(reader->types, type) : 0;
    const bool underaligned = checked && attributes->alignment_log2 - 1 < natural;
    if (underaligned)
    {
        const uint32_t spelling = name < reader->parser->tokens.count ? check_input_typedef_spelling(reader->output, reader->parser, name) : 0;
        check_input_alignas_error(reader, attributes, NULL, 1u << natural, type, spelling);
    }
    return !underaligned;
}

// clang's Expr::getExprLoc of the expression from `first` to `end` (#147), where some handlers report an argument: a
// binary operator's (an assignment's and a comma's too) at its operator, a member access at the member's name, a postfix
// increment or decrement at its operator, a subscript at its base's, and any other (a conditional, a call, a cast, a
// prefix operator, a parenthesized expression, a primary one) at its first token. Its root is the token whose parent is
// outside it.
static uint32_t check_input_expression_location(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t result = first;
    uint32_t last = end;
    bool searching = first < end && end <= parser->tokens.count;
    while (searching)
    {
        uint32_t root = BUSTER_CHECK_NONE;
        for (uint32_t token = first; root == BUSTER_CHECK_NONE && token < last; token += 1)
        {
            const uint32_t parent = parser->tree.parents[token];
            root = parent < first || parent >= last ? token : BUSTER_CHECK_NONE;
        }
        const uint8_t role = root != BUSTER_CHECK_NONE ? parser->tree.roles[root] : PARSE_ROLE_NONE;
        uint32_t opener = BUSTER_CHECK_NONE;
        for (uint32_t token = first; role == PARSE_ROLE_CLOSE_SUBSCRIPT && opener == BUSTER_CHECK_NONE && token < root; token += 1)
        {
            opener = parser->tree.parents[token] == root && parser->tree.roles[token] == PARSE_ROLE_OPEN_SUBSCRIPT ? token : BUSTER_CHECK_NONE;
        }
        const bool binary = role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL ||
                            role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR ||
                            role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_COMMA;
        result = binary || role == PARSE_ROLE_POSTFIX ? root : role == PARSE_ROLE_MEMBER_OPERATOR && root + 1 < last ? root + 1 : first;
        searching = opener != BUSTER_CHECK_NONE && opener > first;
        last = opener;
    }
    return result;
}

// An argument of more than 32 bits (CheckInputAttribute.wide), clang's err_ice_too_large at its expression, from `first`
// to `end`, printing `value`: the declaration's first attribute error.
static void check_input_attribute_too_large(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t first, uint32_t end,
                                            int64_t value)
{
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, check_input_expression_location(reader->parser, first, end), CHECK_ARGUMENT_TOO_LARGE, 0);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].value = (uint64_t)value;
    }
}

// The report the attributes' reader made from `before` (its one), an argument's error clang's handler gives after its
// subject's check (CheckInputAttribute.deferred), moved off the unit's reports into the attribute, with the error it set on
// the declaration's attributes undone (`error`, `error_first` and `error_end` as they were); a use of an undeclared name,
// clang's parser's error, stays. Whether it moved one.
static bool check_input_attribute_defer(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t before,
                                        CheckInputAttributeError error, uint32_t error_first, uint32_t error_end)
{
    CheckInput* output = reader->output;
    const bool moving = output->report_count == before + 1 && output->reports[before].message != CHECK_INPUT_UNDECLARED && !output->resource;
    CheckInputReport* copy = moving ? arena_push(reader->arena, sizeof(CheckInputReport), 8) : NULL;
    output->resource |= moving && copy == NULL;
    if (copy != NULL)
    {
        *copy = output->reports[before];
        output->report_count = before;
        attributes->error = error;
        attributes->error_first = error_first;
        attributes->error_end = error_end;
        attribute->deferred = copy;
    }
    return copy != NULL;
}

// An integer argument (its `argument`'s place from 0, `first` to `end`) that is no integer constant, as clang's handlers
// report it (#147): an identifier that names nothing is clang's parser's "use of undeclared identifier" at it (a member's
// name after `.` or `->`, a tag's after its keyword and an argument that calls a builtin aside; one after a type's
// keyword or a sizeof, whose errors clang's parser gives first, stays the generic error), else each handler's wording, at
// the argument or at the attribute (assume_aligned's offset as a parameter's). Others stay the generic error. Whether it
// reported.
static bool check_input_attribute_constant_error(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t argument,
                                                 uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    const uint32_t before = reader->output->report_count;
    uint32_t undeclared = BUSTER_CHECK_NONE;
    bool builtin = false;
    bool typed = false;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint8_t token_kind = parser->tokens.kinds[token];
        typed |= undeclared == BUSTER_CHECK_NONE && (token_kind == PARSE_KIND_STRUCT || token_kind == PARSE_KIND_ENUM || token_kind == PARSE_KIND_SIZEOF ||
                                                     token_kind == PARSE_KIND_TYPEOF);
        const bool identifier = token_kind == PARSE_KIND_IDENTIFIER;
        const String8 spelled = identifier ? check_input_attribute_name(reader, parser->tokens.names[token]) : (String8){ 0 };
        builtin |= spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0;
        const uint8_t previous = token > 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
        const bool named = previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW || previous == PARSE_KIND_STRUCT ||
                           previous == PARSE_KIND_ENUM || c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) != 0;
        undeclared = undeclared == BUSTER_CHECK_NONE && identifier && !named ? token : undeclared;
    }
    const CheckInputAttributeKind kind = attribute->kind;
    if (attributes->error != CHECK_ATTRIBUTE_ERROR_NONE || reader->output->resource)
    {
    }
    else if (undeclared != BUSTER_CHECK_NONE && (builtin || typed))
    {
    }
    else if (undeclared != BUSTER_CHECK_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = first;
        attributes->error_end = end;
        check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
    }
    else if (kind == CHECK_ATTRIBUTE_ALIGNED || kind == CHECK_ATTRIBUTE_ALIGN_VALUE)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_INTEGER_CONSTANT, 0);
    }
    else if (kind == CHECK_ATTRIBUTE_REGPARM || kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
             (kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && argument == 0))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_AN_INTEGER_CONSTANT, 0);
    }
    else if (kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_EXT_VECTOR_INTEGER, 0);
    }
    else if (kind == CHECK_ATTRIBUTE_ALLOC_ALIGN || kind == CHECK_ATTRIBUTE_FORMAT_ARG || kind == CHECK_ATTRIBUTE_NONNULL || kind == CHECK_ATTRIBUTE_SENTINEL ||
             kind == CHECK_ATTRIBUTE_ALLOC_SIZE || kind == CHECK_ATTRIBUTE_FORMAT || kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, argument + 1);
    }
    return reader->output->report_count > before;
}

// The end of a target string's item from `cursor` (its next comma's, or the text's), and the item trimmed of the whitespace
// clang's StringRef::trim takes off, from *first to *last.
static uint32_t check_input_target_item(String8 text, uint32_t cursor, uint32_t* first, uint32_t* last)
{
    uint32_t end = cursor;
    while (end < text.length && text.pointer[end] != ',')
    {
        end += 1;
    }
    uint32_t begin = cursor;
    uint32_t stop = end;
    while (begin < stop && (text.pointer[begin] == ' ' || (text.pointer[begin] >= '\t' && text.pointer[begin] <= '\r')))
    {
        begin += 1;
    }
    while (stop > begin && (text.pointer[stop - 1] == ' ' || (text.pointer[stop - 1] >= '\t' && text.pointer[stop - 1] <= '\r')))
    {
        stop -= 1;
    }
    *first = begin;
    *last = stop;
    return end;
}

// Whether a target string's item from `first` to `last` starts with `prefix`, and the rest trimmed after its first `=`.
static bool check_input_target_value(String8 text, uint32_t first, uint32_t last, const char* prefix, uint32_t* value, uint32_t* value_end)
{
    const uint32_t length = (uint32_t)strlen(prefix);
    const bool starts = last - first >= length && memcmp(text.pointer + first, prefix, length) == 0;
    uint32_t begin = first;
    while (starts && text.pointer[begin] != '=')
    {
        begin += 1;
    }
    begin += starts ? 1u : 0u;
    uint32_t stop = last;
    while (starts && begin < stop && (text.pointer[begin] == ' ' || (text.pointer[begin] >= '\t' && text.pointer[begin] <= '\r')))
    {
        begin += 1;
    }
    *value = begin;
    *value_end = stop;
    return starts;
}

// target's string as clang's checkTargetAttr takes it (#147, its parseTargetAttr's items: trimmed, between commas), and
// its warning at the string where it doesn't, which ignores the attribute: fpmath= anywhere; an arch= or tune= of no
// x86-64 CPU (the first of each); a second arch= or tune= (the last one's); a feature it hasn't (a no- one's without it);
// a branch-protection= of anything. The warning's text is a piece of the string: its case and what it says in the data
// (bits 0-1 unsupported, duplicate or unknown; 2-3 none, CPU or tune CPU; its offset from bit 4 and its length from bit
// 18). Whether it warned.
static bool check_input_target_warning(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const String8 text = attribute->text;
    uint32_t data = UINT32_MAX;
    for (uint32_t i = 0; data == UINT32_MAX && i + 7 <= text.length; i += 1)
    {
        data = memcmp(text.pointer + i, "fpmath=", 7) == 0 ? (i << 4 | 7u << 18) : data;
    }
    uint32_t cpu = 0;
    uint32_t cpu_end = 0;
    uint32_t tune = 0;
    uint32_t tune_end = 0;
    uint32_t duplicate = UINT32_MAX;
    uint32_t feature = UINT32_MAX;
    uint32_t branch = UINT32_MAX;
    for (uint32_t cursor = 0; data == UINT32_MAX && !check_input_attribute_equal(text, "default") && cursor <= text.length;)
    {
        uint32_t first = 0;
        uint32_t last = 0;
        const uint32_t end = check_input_target_item(text, cursor, &first, &last);
        uint32_t value = 0;
        uint32_t value_end = 0;
        if (check_input_target_value(text, first, last, "fpmath=", &value, &value_end))
        {
        }
        else if (check_input_target_value(text, first, last, "branch-protection=", &value, &value_end))
        {
            branch = value < value_end ? first : UINT32_MAX;
        }
        else if (check_input_target_value(text, first, last, "arch=", &value, &value_end) || check_input_target_value(text, first, last, "tune=", &value, &value_end))
        {
            const bool arch = text.pointer[first] == 'a';
            const bool taken = arch ? cpu < cpu_end : tune < tune_end;
            duplicate = taken ? first : duplicate;
            cpu = arch && !taken ? value : cpu;
            cpu_end = arch && !taken ? value_end : cpu_end;
            tune = !arch && !taken ? value : tune;
            tune_end = !arch && !taken ? value_end : tune_end;
        }
        else if (feature == UINT32_MAX)
        {
            const bool off = last - first >= 3 && memcmp(text.pointer + first, "no-", 3) == 0;
            const uint32_t name = off ? first + 3 : first;
            const int32_t index = predefines_feature_index(text.pointer + name, last - name);
            feature = index < 0 || predefines_features[index].flag == PREDEFINES_FLAG_NONE ? (name << 4 | (last - name) << 18) : feature;
        }
        cursor = end + 1;
    }
    if (data == UINT32_MAX && cpu < cpu_end && predefines_cpu_index_of(text.pointer + cpu, cpu_end - cpu) < 0)
    {
        data = 2u | 1u << 2 | cpu << 4 | (cpu_end - cpu) << 18;
    }
    else if (data == UINT32_MAX && tune < tune_end && predefines_cpu_index_of(text.pointer + tune, tune_end - tune) < 0)
    {
        data = 2u | 2u << 2 | tune << 4 | (tune_end - tune) << 18;
    }
    else if (data == UINT32_MAX && duplicate != UINT32_MAX)
    {
        data = 1u | duplicate << 4 | 5u << 18;
    }
    else if (data == UINT32_MAX && feature != UINT32_MAX)
    {
        data = feature;
    }
    else if (data == UINT32_MAX && branch != UINT32_MAX)
    {
        data = branch << 4 | 17u << 18;
    }
    // A string too long for the data's fields keeps its warning out.
    const bool warned = data != UINT32_MAX && text.length < (1u << 14);
    if (warned)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
            .kind = CHECK_WARNING_EVENT_TARGET_UNSUPPORTED,
            .token = attribute->argument_first,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = attribute->argument_first,
            .payload = { .name = attribute->name, .token = attribute->argument_first, .data = data },
        };
        check_input_warning_event(reader, &event, NULL, 0);
    }
    return warned;
}

// How clang's parser reads each attribute's arguments (#147, c_check_attribute_arguments.inc, which
// tools/generate_attribute_arguments.c makes from clang's AttrParserStringSwitches.inc): by its name without a keyword's
// underscores, the lists that name it and the arguments it reads as string literals, by place (bit N the Nth).
#define BUSTER_CHECK_ARGUMENTS_IDENTIFIER 0x1u
#define BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER 0x2u
#define BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER 0x4u
#define BUSTER_CHECK_ARGUMENTS_TYPE 0x8u
#define BUSTER_CHECK_ARGUMENTS_UNEVALUATED 0x10u
#define BUSTER_CHECK_ARGUMENTS_FUNCTION_SCOPE 0x20u
#define BUSTER_CHECK_ARGUMENTS_LATE 0x40u
#define BUSTER_CHECK_ARGUMENTS_LATE_EXPERIMENTAL 0x80u

typedef struct CheckInputArgumentsRow CheckInputArgumentsRow;
struct CheckInputArgumentsRow
{
    const char* name;
    uint32_t flags;
    uint32_t strings;
};

static const CheckInputArgumentsRow check_input_arguments_rows[] = {
#define BUSTER_CHECK_ARGUMENTS(name, flags, strings) { name, flags, strings },
#include "c_check_attribute_arguments.inc"
#undef BUSTER_CHECK_ARGUMENTS
};

// The row of `name` (without a keyword's underscores), by a binary search of the rows' byte order, or NULL for none.
static const CheckInputArgumentsRow* check_input_arguments_row(String8 name)
{
    uint32_t low = 0;
    uint32_t high = BUSTER_ARRAY_LENGTH(check_input_arguments_rows);
    const CheckInputArgumentsRow* result = NULL;
    while (result == NULL && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const char* row = check_input_arguments_rows[middle].name;
        const uint64_t length = strlen(row);
        const int compared = memcmp(name.pointer, row, name.length < length ? name.length : length);
        const int order = compared != 0 ? compared : name.length < length ? -1 : name.length > length ? 1 : 0;
        result = order == 0 ? check_input_arguments_rows + middle : NULL;
        low = order > 0 ? middle + 1 : low;
        high = order < 0 ? middle : high;
    }
    return result;
}

// Whether clang's visibility attribute knows a visibility (VisibilityAttr::ConvertStrToVisibilityType).
static bool check_input_visibility_known(String8 text)
{
    return check_input_attribute_equal(text, "default") || check_input_attribute_equal(text, "hidden") || check_input_attribute_equal(text, "protected") ||
           check_input_attribute_equal(text, "internal");
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
        output->unknown_first = output->unknown_first == 0 || (suffix->unknown_first != 0 && suffix->unknown_first < output->unknown_first) ?
                                suffix->unknown_first : output->unknown_first;
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
        // clang's errors of a value its handler refuses (#147), else the generic one.
        const CheckInputAttributeKind kind = attribute->kind;
        const bool power = (kind == CHECK_ATTRIBUTE_ALIGNED || kind == CHECK_ATTRIBUTE_ALIGNAS || kind == CHECK_ATTRIBUTE_ALIGN_VALUE) &&
                           (value < 0 || (value == 0 && kind != CHECK_ATTRIBUTE_ALIGNAS) || ((uint64_t)value & ((uint64_t)value - 1)) != 0);
        if (valid)
        {
            attribute->value = value;
            attribute->resolved = true;
        }
        else if (power)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_ALIGNMENT_POWER, 0);
        }
        else if (kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE && value == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_ZERO_VECTOR, 0);
        }
        else if (kind == CHECK_ATTRIBUTE_REGPARM)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_REGPARM_RANGE, 0);
        }
        else if ((kind == CHECK_ATTRIBUTE_ALLOC_ALIGN || kind == CHECK_ATTRIBUTE_FORMAT_ARG) && value <= 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, 1);
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
            // nonnull's, alloc_size's and format's parameter indices are any integers here: the function's check refuses one
            // of no parameter with clang's "parameter N is out of bounds" (check_input_attribute_function; format's as
            // clang's checkUInt32Argument reads a negative int, its 32 bits unsigned). assume_aligned's offset is any
            // integer, as clang's AddAssumeAlignedAttr keeps it.
            const bool index_kind = attribute->kind == CHECK_ATTRIBUTE_NONNULL || attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE || attribute->kind == CHECK_ATTRIBUTE_FORMAT ||
                                    (attribute->kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && argument == 1);
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
        else if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE && !reader->output->resource && attribute->kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && argument == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_ALIGNMENT_POWER, 0);
        }
        else if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE && !reader->output->resource && attribute->kind == CHECK_ATTRIBUTE_SENTINEL)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, argument == 0 ? CHECK_ARGUMENT_SENTINEL_NEGATIVE : CHECK_ARGUMENT_SENTINEL_NULL, 0);
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
            // clang's errors of an argument that is no identifier (#147): mode's at it, format's at the attribute.
            if (!valid && attribute->kind == CHECK_ATTRIBUTE_MODE)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_IDENTIFIER, 0);
            }
            else if (!valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, argument + 1);
            }
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
            // clang's error of an argument that is no string literal, at it (#147): visibility's own wording, the others'
            // checkStringLiteralArgumentAttr's.
            // visibility's argument is an enumerator's, which clang's parser reads as an identifier: one is its own error.
            if (!valid)
            {
                const bool named = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
                check_input_attribute_argument_error(reader, attributes, attribute, first,
                                                     attribute->kind == CHECK_ATTRIBUTE_VISIBILITY && named ? CHECK_ARGUMENT_STRING : CHECK_ARGUMENT_STRING_LITERAL, 0);
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
        // A name that names nothing is clang's parser's error, which drops the attribute: no subject of it is checked.
        const bool undeclared = named && handle == 0 && c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) == 0;
        const bool shadowed = named && check_input_use_shadowed(reader, token, handle);
        valid = literal || (named && !shadowed && c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, token, check_input_use_point(reader, token)) != 0);
        attribute->ignored |= valid && undeclared;
        reader->output->resource |= named && !valid && !shadowed;
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
        // Clang warns and gives an unknown visibility value no effect (its warning: check_input_attribute_subjects).
        // Preserve that deliberate disposition so linkage consumers cannot accidentally interpret an arbitrary string as a
        // supported visibility mode.
        attribute->ignored = !check_input_visibility_known(attribute->text);
    }
    if (valid && attribute->kind == CHECK_ATTRIBUTE_TLS_MODEL)
    {
        valid = check_input_attribute_equal(attribute->text, "global-dynamic") || check_input_attribute_equal(attribute->text, "local-dynamic") ||
                check_input_attribute_equal(attribute->text, "initial-exec") || check_input_attribute_equal(attribute->text, "local-exec");
        error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        if (!valid)
        {
            // (After its subject's check: thread-local variables, an error off one.)
            const CheckInputAttributeError saved = attributes->error;
            const uint32_t saved_first = attributes->error_first;
            const uint32_t saved_end = attributes->error_end;
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->argument_first, CHECK_ARGUMENT_TLS_MODEL, 0);
            valid = check_input_attribute_defer(reader, attributes, attribute, before, saved, saved_first, saved_end);
        }
    }
    if (!valid)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : error);
    }
    return valid;
}

// What clang's parser makes of an attribute's arguments before its handler counts them (#147,
// ParseAttributeArgsCommon): CLEAN, no error, so the count's error waits for the subject's check; ERROR, its first
// argument's error, reported (a string literal's position that holds none, clang's "expected string literal", or an
// expression's name that names nothing, "use of undeclared identifier"), which drops the attribute; or UNSURE, an
// argument whose errors clang's parser gives otherwise (a type's, a call's, a builtin's or an empty one), for which the
// count's error stays where its arguments are read. Linear over the arguments' tokens: only an attribute whose count is
// wrong (or cleanup's) gets here.
#define BUSTER_CHECK_PARSED_CLEAN 0
#define BUSTER_CHECK_PARSED_ERROR 1
#define BUSTER_CHECK_PARSED_UNSURE 2

static uint8_t check_input_attribute_parsed(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t stop = attribute->argument_end;
    const uint8_t schema = attribute->schema;
    uint8_t result = stop > attribute->argument_first && parser->tokens.kinds[stop - 1] == PARSE_KIND_COMMA ? BUSTER_CHECK_PARSED_UNSURE : BUSTER_CHECK_PARSED_CLEAN;
    uint32_t first = attribute->argument_first;
    for (uint32_t argument = 0; result == BUSTER_CHECK_PARSED_CLEAN && first < stop; argument += 1)
    {
        uint32_t end = first;
        while (end < stop && parser->tokens.kinds[end] != PARSE_KIND_COMMA)
        {
            end = check_input_attribute_after_group(parser, end, stop);
        }
        // An identifier argument (an IdentifierArgument's or an EnumArgument's, visibility's; not cleanup's nor malloc's,
        // whose arguments clang parses as expressions) is taken as one; a string literal's position must hold one.
        const bool lone = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
        const bool identifier = lone && (schema == CHECK_ATTRIBUTE_IDENTIFIER_LIST ||
                                         (argument == 0 && attribute->kind != CHECK_ATTRIBUTE_MALLOC && attribute->kind != CHECK_ATTRIBUTE_CLEANUP &&
                                          (schema == CHECK_ATTRIBUTE_IDENTIFIER || schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS ||
                                           attribute->kind == CHECK_ATTRIBUTE_VISIBILITY)));
        const bool string = schema == CHECK_ATTRIBUTE_STRING_LIST || (argument == 0 && schema == CHECK_ATTRIBUTE_STRING);
        uint32_t undeclared = BUSTER_CHECK_NONE;
        bool unsure = first >= end;
        bool literal = !unsure;
        for (uint32_t token = first; !identifier && token < end; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            literal &= kind == PARSE_KIND_STRING;
            const String8 spelled = kind == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, parser->tokens.names[token]) : (String8){ 0 };
            const uint8_t previous = token > 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
            const uint8_t next = token + 1 < stop ? parser->tokens.kinds[token + 1] : PARSE_KIND_END;
            unsure |= kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_TYPEOF ||
                      kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT || (kind == PARSE_KIND_IDENTIFIER && next == PARSE_KIND_L_PAREN) ||
                      (spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0);
            const bool named = previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW ||
                               c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) != 0;
            undeclared = undeclared == BUSTER_CHECK_NONE && kind == PARSE_KIND_IDENTIFIER && !named ? token : undeclared;
        }
        if (identifier)
        {
        }
        else if (string && first < end && parser->tokens.kinds[first] != PARSE_KIND_STRING)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_STRING_LITERAL, 0);
            result = BUSTER_CHECK_PARSED_ERROR;
        }
        else if (unsure || (string && !literal))
        {
            result = BUSTER_CHECK_PARSED_UNSURE;
        }
        else if (!string && undeclared != BUSTER_CHECK_NONE)
        {
            if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
            {
                attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
                attributes->error_first = first;
                attributes->error_end = end;
                check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
            }
            result = BUSTER_CHECK_PARSED_ERROR;
        }
        first = end + 1;
    }
    return result;
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

static bool check_constant_type_start(const Parser* parser, uint32_t token);

// Whether a type argument starts at `token` (#147): a type's specifier or qualifier, or a typedef's name, as the parser
// read it (a GNU attribute's argument, parse_scalar_type_argument_attribute) or in a C23 attribute's tokens, which it
// leaves unread.
static bool check_input_attribute_type_start(CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint8_t kind = parser->tokens.kinds[token];
    const uint32_t handle = kind == PARSE_KIND_IDENTIFIER ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) : 0;
    return check_constant_type_start(parser, token) || kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER ||
           (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF);
}

// Whether a generic attribute takes a type whose kind its handler checks (#147, CheckInputAttribute.argument_type):
// vec_type_hint's and preferred_type's.
static bool check_input_type_argument(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const String8 name = check_input_attribute_name(reader, attribute->name);
    return check_input_attribute_equal(name, "vec_type_hint") || check_input_attribute_equal(name, "preferred_type");
}

// clang's parser's reading of attributes it applies nothing of (#147): a free-standing declaration's specifiers' (clang's
// ParsedFreeStandingDeclSpec), and a bare tag declaration's after its name. Of each GNU list's items and each alignment
// specifier's argument from `first` to `end` (a tag's tokens skipped, CheckInputReader.tag_ends), only what clang's parser
// checks: an argument that must be a string literal (CheckInputArgumentsRow.strings), "expected string literal as argument
// of 'X' attribute", and a name in an argument read as an expression that names nothing, "use of undeclared identifier"
// (an attribute clang doesn't know takes a lone identifier as its first argument), the first an error at it. Then, with
// `warn`, each one's warning: a GNU item's "attribute 'X' is ignored, place it after "struct" to apply attribute to type
// declaration" (`keyword` the tag's: 0 struct, 1 union, 2 enum), an alignment specifier's "'_Alignas' attribute ignored";
// at its token, or after the declaration's `;` (`delimiter`, after the storage class's warnings). Whether there was no
// error.
static bool check_input_attribute_ignored_lists(CheckInputReader* reader, uint32_t first, uint32_t end, bool warn, uint32_t keyword, uint32_t delimiter)
{
    const Parser* parser = reader->parser;
    const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
    bool valid = true;
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        uint32_t t = first;
        while (valid && t < end)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            const bool gnu = kind == PARSE_KIND_ATTRIBUTE && t + 2 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN &&
                             parser->tokens.kinds[t + 2] == PARSE_KIND_L_PAREN;
            const bool alignas = kind == PARSE_KIND_ALIGNAS && t + 1 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN;
            const uint32_t outer = gnu || alignas ? parser->tree.parents[t + 1] : BUSTER_CHECK_NONE;
            // Each item: its name, and its arguments (an alignment specifier's from `(`), from `k`.
            uint32_t k = gnu ? t + 3 : alignas ? t : end;
            const uint32_t items_end = gnu ? parser->tree.parents[t + 2] : alignas ? t + 1 : end;
            while (valid && k < items_end && k < end)
            {
                const bool comma = parser->tokens.kinds[k] == PARSE_KIND_COMMA;
                const uint32_t name = k;
                const bool argued = !comma && k + 1 < end && parser->tokens.kinds[k + 1] == PARSE_KIND_L_PAREN;
                const uint32_t arguments_first = argued ? k + 2 : BUSTER_CHECK_NONE;
                const uint32_t arguments_end = argued ? parser->tree.parents[k + 1] : BUSTER_CHECK_NONE;
                k = comma ? k + 1 : argued ? arguments_end + 1 : k + 1;
                const String8 spelled = !comma ? check_input_attribute_name(reader, parser->tokens.names[name]) : (String8){ 0 };
                const CheckInputArgumentsRow* row = gnu && !comma ? check_input_arguments_row(spelled) : NULL;
                const bool known = gnu && !comma && check_input_fact_row((String8){ "", 0 }, spelled, false, false) != NULL;
                const uint32_t flags = row != NULL ? row->flags : 0;
                const uint32_t strings = row != NULL ? row->strings : 0;
                const bool typed = (flags & BUSTER_CHECK_ARGUMENTS_TYPE) != 0 || (alignas && arguments_first < arguments_end &&
                                                                                 check_constant_type_start(parser, arguments_first));
                uint32_t index = 0;
                for (uint32_t argument = arguments_first; pass == 0 && valid && !typed && argument < arguments_end; index += 1)
                {
                    uint32_t stop = argument;
                    bool literals = true;
                    while (stop < arguments_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
                    {
                        literals &= parser->tokens.kinds[stop] == PARSE_KIND_STRING;
                        stop = check_input_attribute_after_group(parser, stop, arguments_end);
                    }
                    const bool lone = stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
                    const bool identifier = lone && ((index == 0 && ((flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0 || !known)) || (flags & identifiers) != 0);
                    const bool string = index < 32 && ((strings >> index) & 1u) != 0 && (flags & identifiers) == 0;
                    bool builtin = false;
                    for (uint32_t token = argument; token < stop; token += 1)
                    {
                        const String8 word = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, parser->tokens.names[token]) :
                                                                                                    (String8){ 0 };
                        builtin |= (word.length > 10 && memcmp(word.pointer, "__builtin_", 10) == 0) || parser->tokens.kinds[token] == PARSE_KIND_TYPE_ARGUMENT ||
                                   parser->tokens.kinds[token] == PARSE_KIND_GENERIC;
                    }
                    uint32_t undeclared = BUSTER_CHECK_NONE;
                    for (uint32_t token = argument; !identifier && !string && !builtin && undeclared == BUSTER_CHECK_NONE && token < stop; token += 1)
                    {
                        const uint8_t previous = token > 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
                        const uint32_t word = parser->tokens.names[token];
                        const bool named = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && previous != PARSE_KIND_PERIOD && previous != PARSE_KIND_ARROW &&
                                           previous != PARSE_KIND_STRUCT && previous != PARSE_KIND_ENUM;
                        const bool library = reader->library_declarations != NULL && reader->library_declarations[word] != 0;
                        undeclared = named && !library && c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, word) == 0 ? token : BUSTER_CHECK_NONE;
                    }
                    if (string && !identifier && !literals)
                    {
                        CheckInputAttributes holder = { 0 };
                        const CheckInputAttribute item = { .first = name, .name_token = name, .name = parser->tokens.names[name], .end = items_end };
                        check_input_attribute_argument_error(reader, &holder, &item, argument, CHECK_ARGUMENT_STRING_LITERAL, 0);
                        valid = false;
                    }
                    else if (undeclared != BUSTER_CHECK_NONE)
                    {
                        check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
                        valid = false;
                    }
                    argument = stop + 1;
                }
                if (pass == 1 && warn && !comma && (gnu || alignas))
                {
                    const CheckWarningEvent event =
                    {
                        .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                        .kind = gnu ? CHECK_WARNING_EVENT_TAG_ATTRIBUTE_PLACE : CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED,
                        .token = name,
                        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                        .completion_kind = delimiter != BUSTER_CHECK_NONE ? CHECK_WARNING_COMPLETE_AFTER_DELIMITER : CHECK_WARNING_COMPLETE_AT_TOKEN,
                        .completion = delimiter != BUSTER_CHECK_NONE ? delimiter : name + 1,
                        .payload = { .name = parser->tokens.names[name], .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = gnu ? keyword : 0 },
                    };
                    check_input_warning_event(reader, &event, NULL, 0);
                }
                k = alignas ? items_end : k;
            }
            // (Their tokens are read, as check_input_attributes marks a list it reads.)
            if (pass == 1 && valid && outer != BUSTER_CHECK_NONE && outer > t && outer < end)
            {
                memset(reader->ignored + t, 1, outer + 1 - t);
            }
            const bool tag = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM;
            const uint32_t skipped = tag && reader->tag_ends != NULL && reader->tag_ends[t] > t ? reader->tag_ends[t] :
                                     (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && parser->tree.parents[t] > t ? parser->tree.parents[t] + 1 : t + 1;
            t = outer != BUSTER_CHECK_NONE && outer > t ? outer + 1 : skipped;
        }
    }
    return valid;
}

// clang's parser's -Wgcc-compat warning for a function definition (#147, Parser::ParseFunctionDefinition): each GNU
// attribute after its declarator (`attributes`, the declarator's own) whose attribute GCC knows, at its name, named as
// written, "GCC does not allow 'X' attribute in this position on a function definition".
static void check_input_attribute_gcc_one(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
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
            // The parser's, before Sema's diagnostics of the same attribute (published at the token before it).
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = attribute->name_token != 0 ? attribute->name_token - 1 : 0,
            .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_once(reader, &event);
    }
}

static void check_input_attribute_gcc_position(CheckInputReader* reader, const CheckInputAttributes* attributes)
{
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        check_input_attribute_gcc_one(reader, attributes->records + i);
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
    // (A list that begins a declarator's group, `void ([[gnu::stdcall]] *p)(void)`, clang's parser rejects as one after a
    // pointer's qualifiers: #147.)
    const bool group = kind == PARSE_KIND_L_PAREN && role == PARSE_ROLE_OPEN_GROUP;
    return qualifiers || group ? BUSTER_CHECK_PLACE_QUALIFIERS : role == PARSE_ROLE_CLOSE_FUNCTION ? BUSTER_CHECK_PLACE_FUNCTION : chunk ? BUSTER_CHECK_PLACE_CHUNK :
           specifiers && open > 0 ? BUSTER_CHECK_PLACE_SPECIFIERS : BUSTER_CHECK_PLACE_DECLARATION;
}

// A warning event at `at` (an attribute's first token, a C23 one's scope's, or a token of it) naming it: as written, or a
// C23 one's spelling normalized with its scope (check_warning_attribute_name), `token` its related token or none. It is
// published there, or at a later related token (a warning at a declaration's name of an argument's, in its turn among the
// arguments'), before an error of the same attribute's (check_input_report_argument's), as clang's handlers give them in
// turn.
static void check_input_attribute_named_event_at(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningId id, CheckWarningEventKind kind,
                                                 uint32_t at, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t scope_name = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
    const CheckWarningEvent event =
    {
        .id = id,
        .kind = kind,
        .token = at,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = token != BUSTER_CHECK_WARNING_NO_SOURCE && token > at ? token : at,
        .payload = { .name = attribute->name, .token = token,
                     .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
    };
    check_input_warning_event(reader, &event, NULL, 0);
}

// check_input_attribute_named_event_at at the attribute's first token, with no related token.
static void check_input_attribute_named_event(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningId id, CheckWarningEventKind kind)
{
    check_input_attribute_named_event_at(reader, attribute, id, kind, attribute->first, BUSTER_CHECK_WARNING_NO_SOURCE);
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
// function type: at its first token, named as written (a C23 one's normalized), completing at `completion`: its
// declarator's type's warnings' (check_input_attribute_type_warnings), or that token.
static void check_input_attribute_unsupported_at(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t completion)
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
        const Parser* parser = reader->parser;
        const uint32_t scope_name = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
            .kind = CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED,
            .token = attribute->first,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion,
            .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                         .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
        };
        check_input_warning_event(reader, &event, NULL, 0);
    }
}

static void check_input_attribute_unsupported(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    check_input_attribute_unsupported_at(reader, attribute, attribute->first);
}

// Whether a convention attribute is a GPU kernel's (#147): a declaration's attribute of functions alone, whose subject
// check comes first, never a type's.
static bool check_input_attribute_kernel(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const String8 name = check_input_attribute_name(reader, attribute->name);
    return attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION &&
           (check_input_attribute_equal(name, "amdgpu_kernel") || check_input_attribute_equal(name, "device_kernel") ||
            check_input_attribute_equal(name, "nvptx_kernel"));
}

// pcs's arguments where it meets a function or a tag (#147, clang's CheckCallingConvAttr): exactly one, "aapcs" or
// "aapcs-vfp", else clang's errors at the attribute ("'pcs' attribute takes one argument", "invalid PCS type"). Whether
// they are.
// Whether a convention's arguments are as check_input_attribute_pcs wants them (#147), reporting nothing: pcs's one, "aapcs"
// or "aapcs-vfp"; any other's whatever they are.
static bool check_input_attribute_pcs_valid(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const bool pcs = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "pcs");
    return !pcs || (attribute->argument_count == 1 &&
                    (check_input_attribute_equal(attribute->text, "aapcs") || check_input_attribute_equal(attribute->text, "aapcs-vfp")));
}

static bool check_input_attribute_pcs(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const bool pcs = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "pcs");
    const bool counted = !pcs || attribute->argument_count == 1;
    const bool known = !counted || check_input_attribute_pcs_valid(reader, attribute);
    if (!counted && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT | 1u << 8);
    }
    else if (!known)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PCS_TYPE, 0);
    }
    return counted && known;
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

// clang's thread-safety analysis's attributes (#147), whose handlers (SemaDeclAttr.cpp's handleGuardedByAttr and the others)
// check, past their subjects and argument counts: at least one argument (LEAST_ONE); capability's name, a string literal
// (STRING); a trylock's first argument, an int or a bool (TRYLOCK); and a lock function's integer literal argument, an index
// of its parameters (INDEXED). Their arguments are expressions, which clang's parser reads where the attribute is: their names
// are the scope's there (check_input_thread_names). Their other warnings (-Wthread-safety-attributes) are off by default
// and aren't made here. Linear: an attribute is rare.
#define BUSTER_CHECK_THREAD_LEAST_ONE 0x1u
#define BUSTER_CHECK_THREAD_TRYLOCK 0x2u
#define BUSTER_CHECK_THREAD_INDEXED 0x4u
#define BUSTER_CHECK_THREAD_STRING 0x8u

typedef struct CheckInputThreadRow CheckInputThreadRow;
struct CheckInputThreadRow
{
    const char* name;
    uint8_t flags;
};

static const CheckInputThreadRow check_input_thread_rows[] =
{
    { "acquire_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "acquire_shared_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "acquired_after", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "acquired_before", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "assert_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "assert_exclusive_lock", BUSTER_CHECK_THREAD_INDEXED },
    { "assert_shared_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "assert_shared_lock", BUSTER_CHECK_THREAD_INDEXED },
    { "capability", BUSTER_CHECK_THREAD_STRING },
    { "exclusive_lock_function", BUSTER_CHECK_THREAD_INDEXED },
    { "exclusive_locks_required", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "exclusive_trylock_function", BUSTER_CHECK_THREAD_LEAST_ONE | BUSTER_CHECK_THREAD_TRYLOCK },
    { "guarded_by", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "guarded_var", 0 },
    { "lock_returned", 0 },
    { "lockable", 0 },
    { "locks_excluded", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "no_thread_safety_analysis", 0 },
    { "pt_guarded_by", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "pt_guarded_var", 0 },
    { "reentrant_capability", 0 },
    { "release_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "release_generic_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "release_shared_capability", BUSTER_CHECK_THREAD_INDEXED },
    { "requires_capability", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "requires_shared_capability", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "scoped_lockable", 0 },
    { "shared_capability", BUSTER_CHECK_THREAD_STRING },
    { "shared_lock_function", BUSTER_CHECK_THREAD_INDEXED },
    { "shared_locks_required", BUSTER_CHECK_THREAD_LEAST_ONE },
    { "shared_trylock_function", BUSTER_CHECK_THREAD_LEAST_ONE | BUSTER_CHECK_THREAD_TRYLOCK },
    { "try_acquire_capability", BUSTER_CHECK_THREAD_LEAST_ONE | BUSTER_CHECK_THREAD_TRYLOCK },
    { "try_acquire_shared_capability", BUSTER_CHECK_THREAD_LEAST_ONE | BUSTER_CHECK_THREAD_TRYLOCK },
    { "unlock_function", BUSTER_CHECK_THREAD_INDEXED },
};

static bool check_input_thread_attribute(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t declaration, TypeId type);

// The thread-safety row of an attribute's name without a keyword's underscores, or NULL.
static const CheckInputThreadRow* check_input_thread_row(String8 name)
{
    const CheckInputThreadRow* result = NULL;
    for (uint32_t i = 0; result == NULL && i < BUSTER_ARRAY_LENGTH(check_input_thread_rows); i += 1)
    {
        result = check_input_attribute_equal(name, check_input_thread_rows[i].name) ? check_input_thread_rows + i : NULL;
    }
    return result;
}


// Whether clang applies an attribute of `name` in a way that changes nothing this checker models (#147): one its generated
// handler applies with nothing more (its facts' SIMPLE), no type's or statement's alone, but selectany (a COMDAT) and
// no_caller_saved_registers (a calling convention), which stay refused by name;
// and these whose own handlers check what is checked here: minsize, optnone (check_input_attribute_optnone), noescape
// (a pointer parameter's, check_input_attribute_subjects), annotate (a string first, check_input_attribute_read_item),
// uninitialized, gcc_struct (the default layout), no_sanitize_memory, analyzer_noreturn and carries_dependency; the
// thread-safety analysis's (check_input_thread_rows); one of no C subject (`subjectless`: Objective-C's, C++'s and the like,
// whose subject's diagnostic is all clang gives in C); and these, whose handlers change nothing in C past their subjects,
// counts and arguments' kinds (#147): available_only_in_default_eval_method (whose `#pragma clang fp eval_method` the
// checker refuses), btf_decl_tag (BPF's debug information), randomize_layout and no_randomize_layout (without
// -frandomize-layout-seed), Objective-C's boxable and bridge_mutable, Fuchsia's handles (the analyzer's), suppress, Swift's
// bridge, unsafe_buffer_usage and AMDGPU's max_num_work_groups (a kernel's alone).
static bool check_input_attribute_generic(String8 name, const CheckInputFactRow* fact, bool subjectless)
{
    static const char* const unchanged[] = {
        "available_only_in_default_eval_method", "btf_decl_tag", "randomize_layout", "no_randomize_layout", "objc_boxable", "objc_bridge_mutable",
        "release_handle", "use_handle", "suppress", "swift_bridge", "unsafe_buffer_usage", "amdgpu_max_num_work_groups",
        // The retain-ownership attributes, whose handlers' checks are check_input_attribute_ownership's.
        "cf_consumed", "ns_consumed", "os_consumed", "cf_returns_retained", "cf_returns_not_retained", "ns_returns_retained",
        "ns_returns_not_retained", "ns_returns_autoreleased", "os_returns_retained", "os_returns_not_retained",
        "os_returns_retained_on_zero", "os_returns_retained_on_non_zero",
        // Debug information's and other languages' type attributes, and function types' effects (no warnings by default)
        // and CFI's, which change nothing here; and these, whose handlers' checks are check_input_attribute_handled's.
        "btf_type_tag", "swift_attr", "no_field_protection", "vec_type_hint", "allocating", "blocking", "nonallocating", "nonblocking",
        "cfi_unchecked_callee", "overflow_behavior", "init_priority", "always_destroy", "no_destroy", "nomerge", "abi_tag", "matrix_type",
        "xray_log_args", "personality", "function_return", "zero_call_used_regs", "model",
        // The GPU kernels' sizes and registers, of kernels alone (check_input_attribute_kernels).
        "reqd_work_group_size", "work_group_size_hint", "intel_reqd_sub_group_size", "amdgpu_flat_work_group_size", "amdgpu_waves_per_eu",
        "amdgpu_num_sgpr", "amdgpu_num_vgpr",
        // Static analyses' and other languages' annotations, whose handlers' checks in C are check_input_attribute_handled's.
        "acquire_handle", "mig_server_routine", "malloc_span", "iboutlet", "iboutletcollection", "enforce_read_only_placement", "type_visibility",
        "param_typestate", "return_typestate", "lifetime_capture_by", "lifetime_capture_by_global", "lifetime_capture_by_this",
        "lifetime_capture_by_unknown", "ownership_holds", "ownership_returns", "ownership_takes", "NSObject", "objc_independent_class",
        "objc_precise_lifetime",
        // Other targets' vector types, aliases of their builtins, statements' hints (check_input_statement_attribute_kinds),
        // and Blocks' byref variables, whose checks in C on x86-64 are check_input_attribute_annotations'.
        "arm_sve_vector_bits", "riscv_rvv_vector_bits", "clang_builtin_alias", "atomic", "code_align", "opencl_unroll_hint", "blocks", "assume",
        // A bit-field's preferred type, whose type check_input_attribute_annotations checks; MSVC's forceinline and
        // forceinline_calls, which it ignores off MSVC's ABI.
        "preferred_type", "forceinline", "forceinline_calls",
    };
    const bool simple = fact != NULL && (fact->flags & (BUSTER_CHECK_FACT_SIMPLE | BUSTER_CHECK_FACT_TYPE)) == BUSTER_CHECK_FACT_SIMPLE;
    const bool effect = check_input_attribute_equal(name, "selectany") || check_input_attribute_equal(name, "no_caller_saved_registers");
    bool handled = check_input_attribute_equal(name, "minsize") || check_input_attribute_equal(name, "optnone") ||
                   check_input_attribute_equal(name, "noescape") || check_input_attribute_equal(name, "annotate") ||
                   check_input_attribute_equal(name, "uninitialized") || check_input_attribute_equal(name, "gcc_struct") ||
                   check_input_attribute_equal(name, "no_sanitize_memory") || check_input_attribute_equal(name, "analyzer_noreturn") ||
                   check_input_attribute_equal(name, "carries_dependency") || check_input_thread_row(name) != NULL || subjectless;
    for (uint32_t i = 0; !handled && i < BUSTER_ARRAY_LENGTH(unchanged); i += 1)
    {
        handled = check_input_attribute_equal(name, unchanged[i]);
    }
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
                                            uint32_t place, bool definition)
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
        // The parser recognizes a definition's suffix before Sema processes its attributes. Keep its warning even
        // when a type attribute's arguments (for example regparm or pcs) prevent the declarator from completing.
        if (definition)
        {
            check_input_attribute_gcc_one(reader, &attribute);
        }
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
        // clang's parser reads an attribute's arguments by their kinds (#147, check_input_arguments_row: ParseAttributeArgsCommon),
        // before its place decides anything: an identifier first argument of one that takes an identifier is one (each
        // identifier argument of one whose identifiers all are); then an argument it reads as an unevaluated string literal
        // that is none is its error at the argument. One whose arguments clang parses its own way (availability's and the
        // like) is its own.
        const CheckInputFactRow* parsed = standard ? fact : known ? check_input_fact_row((String8){ "", 0 }, name, false, false) : NULL;
        // One of no C subject (#147, check_input_attribute_generic).
        const CheckInputSubjectRow* subject_row = known ? check_input_subject_row(scope, name, scoped, standard) : NULL;
        const bool subjectless = subject_row != NULL && subject_row->subjects == 0;
        const bool custom = parsed != NULL && (parsed->flags & BUSTER_CHECK_FACT_CUSTOM_PARSING) != 0;
        const CheckInputArgumentsRow* arguments = valid && known && !custom ? check_input_arguments_row(name) : NULL;
        const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
        uint32_t literal = BUSTER_CHECK_NONE;
        uint32_t index = 0;
        for (uint32_t argument = attribute.argument_first; arguments != NULL && literal == BUSTER_CHECK_NONE && argument < attribute.argument_end; index += 1)
        {
            uint32_t stop = argument;
            bool strings = true;
            while (stop < attribute.argument_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
            {
                strings &= parser->tokens.kinds[stop] == PARSE_KIND_STRING;
                stop = check_input_attribute_after_group(parser, stop, attribute.argument_end);
            }
            const bool lone = stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
            const bool identifier = lone && ((index == 0 && (arguments->flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0) || (arguments->flags & identifiers) != 0);
            const bool string = index < 32 && ((arguments->strings >> index) & 1u) != 0 && (arguments->flags & (identifiers | BUSTER_CHECK_ARGUMENTS_TYPE)) == 0;
            literal = string && !identifier && !strings ? argument : BUSTER_CHECK_NONE;
            argument = stop + 1;
        }
        if (literal != BUSTER_CHECK_NONE)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, literal, CHECK_ARGUMENT_STRING_LITERAL, 0);
            valid = false;
        }
        // A type argument's (ParseTypeName's): an identifier that names no type is "unknown type name"; any other start of
        // no type "expected a type" and, in a GNU list, "expected ')'" there; a type followed by a `,` is "expected ')'"
        // at it, with its note at the `(` in a GNU list (a C23 list's arguments clang reads as a balanced run then).
        const uint32_t type_argument = valid && arguments != NULL && (arguments->flags & BUSTER_CHECK_ARGUMENTS_TYPE) != 0 &&
                                       attribute.argument_first < attribute.argument_end ? attribute.argument_first : BUSTER_CHECK_NONE;
        const bool typed_argument = type_argument != BUSTER_CHECK_NONE && check_input_attribute_type_start(reader, type_argument);
        uint32_t comma = BUSTER_CHECK_NONE;
        for (uint32_t token = typed_argument ? type_argument : attribute.argument_end; comma == BUSTER_CHECK_NONE && token < attribute.argument_end;)
        {
            comma = parser->tokens.kinds[token] == PARSE_KIND_COMMA ? token : BUSTER_CHECK_NONE;
            token = check_input_attribute_after_group(parser, token, attribute.argument_end);
        }
        if (type_argument != BUSTER_CHECK_NONE && !typed_argument && parser->tokens.kinds[type_argument] == PARSE_KIND_IDENTIFIER)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, type_argument, CHECK_ARGUMENT_UNKNOWN_TYPE, type_argument - attribute.first);
            valid = false;
        }
        else if (type_argument != BUSTER_CHECK_NONE && !typed_argument)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, type_argument, CHECK_ARGUMENT_EXPECTED_TYPE, 0);
            const uint32_t before = reader->output->report_count;
            if (!standard)
            {
                check_input_report_argument(reader, type_argument, CHECK_INPUT_ATTRIBUTE_ARGUMENT, CHECK_ARGUMENT_EXPECTED_PAREN);
            }
            // (A second error at the same token, which the driver keeps, with its note at the arguments' `(`.)
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].attribute = attribute.first;
                reader->output->reports[before].repeated = true;
                reader->output->reports[before].note_token = attribute.argument_first - 1;
                reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_MATCH_PAREN;
            }
            valid = false;
        }
        else if (comma != BUSTER_CHECK_NONE)
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, &attribute, comma, CHECK_ARGUMENT_EXPECTED_PAREN, 0);
            if (reader->output->report_count > before && !standard)
            {
                reader->output->reports[before].note_token = attribute.argument_first - 1;
                reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_MATCH_PAREN;
            }
            valid = false;
        }
        // A C23 type attribute on a declaration (#147, clang's ProcessDeclAttribute): one of clang's that slide to the
        // specifiers (btf_type_tag) is applied there, with -Wdeprecated-attributes' warning when it is clang::'s; a
        // function type's effect (nonblocking and the others) before the declaration is clang's "cannot be applied to a
        // declaration" error (after its name it is the declarator's type's).
        uint32_t list_opener = attribute.first;
        while (standard && list_opener > 0 && parser->tree.depths[list_opener - 1] >= parser->tree.depths[attribute.first])
        {
            list_opener -= 1;
        }
        const uint8_t before_list = standard && list_opener >= 3 ? parser->tokens.kinds[list_opener - 3] : PARSE_KIND_SEMI;
        const bool leading = before_list == PARSE_KIND_SEMI || before_list == PARSE_KIND_L_BRACE || before_list == PARSE_KIND_R_BRACE ||
                             before_list == PARSE_KIND_COLON || before_list == PARSE_KIND_R_SQUARE;
        const bool declaration_place = valid && known && standard && place == BUSTER_CHECK_PLACE_DECLARATION && fact != NULL &&
                                       (fact->flags & BUSTER_CHECK_FACT_TYPE) != 0;
        const bool effect = check_input_attribute_equal(name, "nonblocking") || check_input_attribute_equal(name, "nonallocating") ||
                            check_input_attribute_equal(name, "blocking") || check_input_attribute_equal(name, "allocating");
        if (declaration_place && leading && effect)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, attribute.first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
            valid = false;
        }
        else if (declaration_place && clang_scope && check_input_attribute_equal(name, "btf_type_tag"))
        {
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_ON_DECLARATION);
        }
        if (valid && chunk_gnu && typed)
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
        if (valid && chunk_gnu && !typed)
        {
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_POSITION);
            unknown = true;
        }
        else if (valid && type_name)
        {
            check_input_attribute_named_once(reader, &attribute, CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE);
            attributes->unknown_first = attributes->unknown_first != 0 ? attributes->unknown_first : attribute.first;
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
            // An argument count other than clang's is its error, worded as clang's (#147). cleanup's is checked after its
            // subject's already (check_input_cleanup_declaration).
            uint32_t bound = 0;
            const CheckInputFactRow* counted = standard ? fact : check_input_fact_row((String8){ "", 0 }, name, false, false);
            const uint8_t count_check = rule->kind == CHECK_ATTRIBUTE_MUSTTAIL || rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION ||
                                        rule->kind == CHECK_ATTRIBUTE_CLEANUP || rule->kind == CHECK_ATTRIBUTE_COUNTED_BY ? BUSTER_CHECK_COUNT_NONE :
                                        check_input_attribute_count_check(counted, check_input_attribute_count(parser, &attribute), &bound);
            // The count's error comes after its subject's check (check_input_attribute_subjects), as clang's
            // handleCommonAttributeFeatures has it: `cold(1)` or `format(printf)` on a variable is the subject's warning
            // alone. Its arguments aren't read then, but clang's parser's errors of them come first
            // (check_input_attribute_parsed). Not a type's, whose count clang's type processing checks first, nor
            // sentinel's, whose subject its handler checks after the count. Kept as clang's generated handler's (the
            // generic kind), its own handler's checks and effects aren't made.
            const bool type_attribute = rule->kind == CHECK_ATTRIBUTE_NORETURN || rule->kind == CHECK_ATTRIBUTE_PACKED || rule->kind == CHECK_ATTRIBUTE_MAY_ALIAS ||
                                        rule->kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION || rule->kind == CHECK_ATTRIBUTE_NULLABILITY ||
                                        rule->kind == CHECK_ATTRIBUTE_SENTINEL;
            const uint8_t parsed = count_check != BUSTER_CHECK_COUNT_NONE && !type_attribute ? check_input_attribute_parsed(reader, attributes, &attribute) :
                                   BUSTER_CHECK_PARSED_UNSURE;
            if (parsed == BUSTER_CHECK_PARSED_ERROR)
            {
                valid = false;
            }
            else if (parsed == BUSTER_CHECK_PARSED_CLEAN)
            {
                attribute.kind = CHECK_ATTRIBUTE_UNKNOWN;
                attribute.schema = CHECK_ATTRIBUTE_NO_ARGUMENTS;
                attribute.argument_count = check_input_attribute_count(parser, &attribute);
                attribute.count_check = count_check;
                attribute.count_bound = bound;
            }
            else if (count_check != BUSTER_CHECK_COUNT_NONE)
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
            else if (rule->kind == CHECK_ATTRIBUTE_CLEANUP && check_input_attribute_count(parser, &attribute) != 1 &&
                     check_input_attribute_parsed(reader, attributes, &attribute) == BUSTER_CHECK_PARSED_ERROR)
            {
                // cleanup's arguments of another count than one: clang's parser's errors of them first (an undeclared
                // name), then its subject's and its count's (check_input_cleanup_declaration).
                valid = false;
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
            attributes->unknown_first = attributes->unknown_first != 0 ? attributes->unknown_first : attribute.first;
            unknown = true;
        }
        else if (valid && known && rule == 0 && check_input_attribute_equal(name, "nocf_check"))
        {
            // clang takes nocf_check as a function type's, whose handler without -fcf-protection (which the driver refuses)
            // warns that it ignores it, wherever it is and whatever its arguments (#147).
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NOCF_CHECK_IGNORED);
            unknown = true;
        }
        else if (valid && known && check_input_attribute_generic(name, generic_fact, subjectless))
        {
            // One clang's generated handler applies with nothing more, which changes nothing here (#147): kept for its
            // subject's check, then its argument count's against clang's.
            const uint32_t count = check_input_attribute_count(parser, &attribute);
            const CheckInputThreadRow* thread = check_input_thread_row(name);
            attribute.kind = thread != NULL ? CHECK_ATTRIBUTE_THREAD_SAFETY : CHECK_ATTRIBUTE_UNKNOWN;
            attribute.argument_count = count;
            attribute.count_check = check_input_attribute_count_check(generic_fact, count, &attribute.count_bound);
            // A thread-safety handler's own count of at least one, after the facts' (checkAtLeastNumArgs), and abi_tag's;
            // preferred_type's type, which its optional argument must give (handlePreferredTypeAttr), exactly one.
            if (attribute.count_check == BUSTER_CHECK_COUNT_NONE && count == 0 &&
                ((thread != NULL && (thread->flags & BUSTER_CHECK_THREAD_LEAST_ONE) != 0) || check_input_attribute_equal(name, "abi_tag")))
            {
                attribute.count_check = BUSTER_CHECK_COUNT_LEAST;
                attribute.count_bound = 1;
            }
            else if (attribute.count_check == BUSTER_CHECK_COUNT_NONE && count == 0 && check_input_attribute_equal(name, "preferred_type"))
            {
                attribute.count_check = BUSTER_CHECK_COUNT_EXACT;
                attribute.count_bound = 1;
            }
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
            attributes->unknown_first = attributes->unknown_first != 0 ? attributes->unknown_first : attribute.first + 1;
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
            uint32_t after = close + 1;
            const bool suffix = valid && !standard && parser->tree.roles[open] == PARSE_ROLE_OPEN_ATTRIBUTE;
            while (suffix && after < end && check_input_attribute_starts(parser, after, end))
            {
                const uint32_t next_open = parser->tokens.kinds[after] == PARSE_KIND_L_SQUARE ? after : after + 1;
                after = next_open < end ? check_input_attribute_after_group(parser, next_open, end) : end + 1;
            }
            const bool definition = suffix && after <= end && after < parser->tokens.count && parser->tree.roles[after] == PARSE_ROLE_OPEN_BODY;
            i = open + 2;
            if (valid && place == BUSTER_CHECK_PLACE_QUALIFIERS)
            {
                // clang's declarator parser rejects a C23 list after a pointer's qualifiers, or at a group's start
                // (err_attributes_not_allowed).
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
                    valid = check_input_attribute_read_item(reader, attributes, &i, list_end, standard, place, definition);
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
            else if (name == C_NAME_NORETURN)
            {
                bool duplicate = false;
                for (uint32_t a = 0; !duplicate && a < attributes->count; a += 1)
                {
                    duplicate = attributes->records[a].kind == CHECK_ATTRIBUTE_NORETURN && attributes->records[a].keyword;
                }
                if (duplicate)
                {
                    check_input_duplicate_specifier(reader, first, C_NAME_NORETURN);
                }
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

// A mode's name without a keyword's underscores, and the count of its vector prefix (V, a power of two that fits in 64 bits,
// and a mode of two letters or more), or 0 for none: *rest the name past it.
static uint64_t check_input_mode_vector(CheckInputReader* reader, const CheckInputAttribute* attribute, String8* rest)
{
    String8 mode = check_input_attribute_name(reader, reader->parser->tokens.names[attribute->argument_first]);
    const bool surrounded = mode.length >= 4 && mode.pointer[0] == '_' && mode.pointer[1] == '_' && mode.pointer[mode.length - 2] == '_' &&
                            mode.pointer[mode.length - 1] == '_';
    mode = surrounded ? (String8){ mode.pointer + 2, mode.length - 4 } : mode;
    uint64_t result = 0;
    *rest = mode;
    if (mode.length >= 4 && mode.pointer[0] == 'V')
    {
        uint64_t position = 1;
        bool overflow = false;
        uint64_t count = 0;
        while (position + 1 <= mode.length - 1 && mode.pointer[position] >= '0' && mode.pointer[position] <= '9')
        {
            const uint32_t digit = (uint32_t)(mode.pointer[position] - '0');
            overflow |= count > (UINT64_MAX - digit) / 10;
            count = overflow ? 0 : count * 10 + digit;
            position += 1;
        }
        const bool vector = position > 1 && !overflow && count != 0 && (count & (count - 1)) == 0;
        result = vector ? count : 0;
        *rest = vector ? (String8){ mode.pointer + position, mode.length - position } : mode;
    }
    return result;
}

// A mode's name past its vector prefix (clang's parseModeAttrArg, #147): its width in bits (0 for none), whether it is an
// integer's, a complex's, and its explicit floating type ('K': __float128, 'T': long double, 'I': __ibm128, else none).
static uint32_t check_input_mode_width(String8 mode, bool* integer, bool* complex, char* explicit_type)
{
    uint32_t width = 0;
    *integer = true;
    *complex = false;
    *explicit_type = 0;
    if (mode.length == 2)
    {
        const char first = mode.pointer[0];
        const char second = mode.pointer[1];
        width = first == 'Q' ? 8u : first == 'H' ? 16u : first == 'S' ? 32u : first == 'D' ? 64u : first == 'X' ? 96u : first == 'T' ? 128u :
                (first == 'K' || first == 'I') && second != 'I' ? 128u : 0u;
        *explicit_type = first == 'K' || first == 'T' || first == 'I' ? first : 0;
        *integer = second != 'F' && second != 'C';
        *complex = second == 'C';
        width = second == 'F' || second == 'C' || second == 'I' ? width : 0u;
    }
    else
    {
        // word, byte, pointer and unwind_word: x86-64's register, char, pointer and unwind word widths.
        width = check_input_attribute_equal(mode, "word") || check_input_attribute_equal(mode, "pointer") || check_input_attribute_equal(mode, "unwind_word") ? 64u :
                check_input_attribute_equal(mode, "byte") ? 8u : 0u;
    }
    return width;
}

// mode's type of `base` (#147), as clang's AddModeAttr makes it: the name without a keyword's underscores; a vector mode
// (V, a power of two, a mode) with -Wdeprecated-attributes' warning (here before an error, else each declarator's); the width's integer type of the base's signedness or
// its floating type (getRealTypeForBitwidth: HF __fp16, XF long double, TF and KF __float128, IF none here), complex for
// a C mode; a vector of it by the mode's count, or the base vector's bytes. The declaration's qualifiers are dropped, as
// clang's setType drops them. clang's errors at the attribute, in its order, else 0: an unknown mode, a vector mode of an
// enumeration, a base of no integer or floating type, a mode of the other kind, an unsupported width, and a complex mode
// of a vector.
static TypeId check_input_attribute_mode(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, TypeId base)
{
    TypeTable* types = reader->types;
    String8 rest = { 0 };
    const uint64_t vector_count = check_input_mode_vector(reader, attribute, &rest);
    bool integer_mode = true;
    bool complex_mode = false;
    char explicit_type = 0;
    const uint32_t width = check_input_mode_width(rest, &integer_mode, &complex_mode, &explicit_type);
    const TypeId old = type_strip_aligned(types, base);
    const uint8_t old_kind = type_kind(types, old);
    const bool vector_base = old_kind == BUSTER_TYPE_KIND_VECTOR || old_kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    const TypeId element = vector_base ? type_strip_aligned(types, type_element(types, old)) : old;
    const uint8_t kind = type_kind(types, element);
    const bool enumeration = kind == BUSTER_TYPE_KIND_ENUM;
    const bool integral = kind == BUSTER_TYPE_KIND_INTEGER || enumeration;
    const bool builtin = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_NULLPTR;
    const bool complex_floating = kind == BUSTER_TYPE_KIND_COMPLEX && type_kind(types, type_strip_aligned(types, type_element(types, element))) == BUSTER_TYPE_KIND_FLOATING;
    const bool floating = kind == BUSTER_TYPE_KIND_FLOATING || complex_floating;
    uint32_t integer_width = 0;
    bool is_signed = false;
    // (An enum with no integer type yet takes int's, signed, as clang's AddModeAttr defaults it.)
    const bool signed_base = (integral && check_action_integer_type(types, element, &integer_width, &is_signed) && is_signed) ||
                             (enumeration && !type_is_complete(types, element));
    TypeId result = 0;
    if (integer_mode)
    {
        result = width == 8 ? (signed_base ? BUSTER_TYPE_SCHAR : BUSTER_TYPE_UCHAR) : width == 16 ? (signed_base ? BUSTER_TYPE_SHORT : BUSTER_TYPE_USHORT) :
                 width == 32 ? (signed_base ? BUSTER_TYPE_INT : BUSTER_TYPE_UINT) : width == 64 ? (signed_base ? BUSTER_TYPE_LONG : BUSTER_TYPE_ULONG) :
                 width == 128 ? (signed_base ? BUSTER_TYPE_INT128 : BUSTER_TYPE_UINT128) : 0;
    }
    else
    {
        result = width == 16 ? BUSTER_TYPE_HALF : width == 32 ? BUSTER_TYPE_FLOAT : width == 64 ? BUSTER_TYPE_DOUBLE : width == 96 ? BUSTER_TYPE_LONG_DOUBLE :
                 width == 128 && explicit_type != 'I' ? BUSTER_TYPE_FLOAT128 : 0;
    }
    CheckAttributeArgumentError error = CHECK_ARGUMENT_COUNT;
    if (width == 0)
    {
        error = CHECK_ARGUMENT_UNKNOWN_MODE;
    }
    else if (enumeration && vector_count != 0)
    {
        error = CHECK_ARGUMENT_MODE_ENUM_VECTOR;
    }
    else if (!builtin && !complex_floating && !integral)
    {
        error = CHECK_ARGUMENT_MODE_PRIMITIVE;
    }
    else if (integer_mode ? !integral : complex_mode ? !complex_floating : !floating)
    {
        error = CHECK_ARGUMENT_MODE_TYPE;
    }
    else if (result == 0)
    {
        error = CHECK_ARGUMENT_UNSUPPORTED_MODE;
    }
    else if (complex_mode && vector_base && vector_count == 0)
    {
        error = CHECK_ARGUMENT_MODE_COMPLEX_VECTOR;
    }
    if (error == CHECK_ARGUMENT_COUNT)
    {
        result = complex_mode ? check_input_type_result(reader, type_complex(types, result)) : result;
        // The base vector's elements' bytes, not its rounded storage's, in as many of the new elements.
        const uint64_t count = vector_count != 0 ? vector_count :
                               vector_base && result != 0 ? type_size(types, element) * types->types.extent[type_index(old)] / type_size(types, result) : 0;
        if (result != 0 && (vector_count != 0 || vector_base))
        {
            result = count != 0 && count <= UINT32_MAX ? check_input_type_result(reader, type_vector(types, result, (uint32_t)count, false)) : 0;
        }
        error = result == 0 && !reader->output->resource ? CHECK_ARGUMENT_MODE_TYPE : error;
    }
    if (error != CHECK_ARGUMENT_COUNT)
    {
        // A vector mode's warning comes first; a declarator's that has its type has it with its subjects
        // (check_input_attribute_subjects), as clang's handler gives it for each.
        if (vector_count != 0)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_VECTOR_MODE);
        }
        result = 0;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, error, attribute->argument_first - attribute->first);
    }
    return result;
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
            result = check_input_attribute_mode(reader, attributes, attribute, result);
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
            if (attribute->resolved && !valid)
            {
                check_input_alignas_error(reader, attributes, NULL, BUSTER_CHECK_NONE, 0, 0);
            }
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
    // clang's CheckAlignasUnderalignment: the greatest of all the alignments (GNU aligned's too) where one is _Alignas's.
    if (valid && attributes->required_alignment_log2 != 0 && !deduced)
    {
        const bool function = type_kind(reader->types, result) == BUSTER_TYPE_KIND_FUNCTION;
        const uint32_t natural = function ? 0 : type_alignment_log2(reader->types, result);
        valid = !function && attributes->alignment_log2 - 1 >= natural;
        if (!valid)
        {
            check_input_alignas_error(reader, attributes, NULL, function ? BUSTER_CHECK_NONE : 1u << natural, function ? 0 : result, 0);
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
        // clang's isCharType: char, signed char or unsigned char.
        const TypeId element = type_unqualified(type_strip_aligned(types, types->types.base[type_index(type)]));
        result = element == BUSTER_TYPE_CHAR || element == BUSTER_TYPE_SCHAR || element == BUSTER_TYPE_UCHAR;
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
        if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_pcs(reader, attributes, attribute))
        {
            const uint32_t call = attribute->value == CHECK_CALL_IGNORED_X86_32 ? CHECK_CALL_SYSV : (uint32_t)attribute->value;
            // (A GPU kernel's convention is a function's declaration's alone, not a pointer's to one.)
            const bool wrapped_kernel = check_input_attribute_kernel(reader, attribute) &&
                                        type_kind(reader->types, type_strip_aligned(reader->types, type)) != BUSTER_TYPE_KIND_FUNCTION;
            if (attribute->value == CHECK_CALL_IGNORED_X86_32 && !wrapped_kernel)
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
                else if (result == 0 && (attribute->value == CHECK_CALL_VECTOR || attribute->value == CHECK_CALL_REG) &&
                         (reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
                {
                    // vectorcall and regcall take no variadic function (#147, clang's supportsVariadicCall): its error.
                    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_VARIADIC_CONVENTION, (uint32_t)attribute->value);
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

// Handlers' checks of generic attributes past their subjects (#147), as clang's make them in C on x86-64: init_priority, a
// C++ one, "'init_priority' attribute ignored"; always_destroy's and no_destroy's variable of no static or thread storage,
// clang's error at its name; nomerge's variable of no function pointer type, "'nomerge' attribute is ignored because 'x'
// is not a function pointer"; xray_log_args's index of the function's parameters (out of bounds, or no constant);
// personality's argument, a function's name or "'personality' argument is not a function"; function_return's,
// zero_call_used_regs's and model's strings (an unsupported one warned of or an error, as clang's, and those whose effect
// the code here hasn't, thunk-extern, any zeroing and the large model, refused by name). Whether the declaration goes on.
static bool check_input_attribute_handled(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration, TypeId type,
                                          uint32_t name)
{
    const Parser* parser = reader->parser;
    const String8 spelled = check_input_attribute_name(reader, attribute->name);
    const uint32_t count = parser->tokens.count;
    const TypeId bare = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    const bool function_pointer = bare != 0 && type_kind(reader->types, bare) == BUSTER_TYPE_KIND_POINTER &&
                                  type_kind(reader->types, type_strip_aligned(reader->types, reader->types->types.base[type_index(bare)])) == BUSTER_TYPE_KIND_FUNCTION;
    const bool destroy = check_input_attribute_equal(spelled, "always_destroy") || check_input_attribute_equal(spelled, "no_destroy");
    const String8 text = attribute->argument_first < attribute->argument_end && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_STRING &&
                         check_input_attribute_string(reader, attribute->argument_first, attribute->argument_end, &attribute->text) ? attribute->text : (String8){ 0 };
    bool valid = true;
    if (attribute->count_check != BUSTER_CHECK_COUNT_NONE)
    {
        // Its count's error comes first (check_input_attribute_subjects).
    }
    else if (check_input_attribute_equal(spelled, "init_priority"))
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
        attribute->ignored = true;
        attribute->subject_ignored = true;
    }
    else if (destroy && (declaration & BUSTER_CHECK_SUBJECT_GLOBAL) == 0 && name < count)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, name, CHECK_ARGUMENT_DESTROY_STORAGE,
                                             check_input_attribute_equal(spelled, "always_destroy") ? 1u : 0u);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "nomerge") && (declaration & BUSTER_CHECK_SUBJECT_VARIABLE) != 0 && !function_pointer && name < count)
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_NOMERGE_POINTER, attribute->first, name);
        attribute->ignored = true;
        attribute->subject_ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "xray_log_args") && attribute->argument_count == 1 && bare != 0 &&
             type_kind(reader->types, bare) == BUSTER_TYPE_KIND_FUNCTION)
    {
        const int64_t index = attribute->value;
        uint32_t parameters = 0;
        type_parameters(reader->types, bare, &parameters);
        if (!attribute->constant_argument)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, 1);
            valid = false;
        }
        else if (index < 1 || index > (int64_t)parameters)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, 1);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "personality") && attribute->argument_count == 1)
    {
        const uint32_t argument = attribute->argument_first;
        const bool lone = argument + 1 == attribute->argument_end && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
        const uint32_t binding = lone ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[argument]) : 0;
        const uint32_t declared = binding != 0 ? reader->bindings.declaration[binding - 1] : BUSTER_CHECK_NONE;
        const TypeId personality = declared < count ? reader->declared_types[declared] : 0;
        if (personality == 0 || type_kind(reader->types, type_strip_aligned(reader->types, personality)) != BUSTER_TYPE_KIND_FUNCTION)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, argument, CHECK_ARGUMENT_PERSONALITY, 0);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "function_return") || check_input_attribute_equal(spelled, "zero_call_used_regs"))
    {
        static const char* const zeroing[] = { "skip", "used-gpr-arg", "used-gpr", "used-arg", "used", "all-gpr-arg", "all-gpr", "all-arg", "all" };
        const bool returns = check_input_attribute_equal(spelled, "function_return");
        bool known = returns && (check_input_attribute_equal(text, "keep") || check_input_attribute_equal(text, "thunk-extern"));
        for (uint32_t i = 0; !returns && !known && i < BUSTER_ARRAY_LENGTH(zeroing); i += 1)
        {
            known = check_input_attribute_equal(text, zeroing[i]);
        }
        const bool unchanged = check_input_attribute_equal(text, returns ? "keep" : "skip");
        if (!known && text.pointer != NULL)
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED, attribute->argument_first,
                                                 attribute->argument_first);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        else if (known && !unchanged)
        {
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
    }
    else if ((check_input_attribute_equal(spelled, "nonblocking") || check_input_attribute_equal(spelled, "nonallocating")) && attribute->argument_count == 1)
    {
        if (!attribute->constant_argument)
        {
            const uint32_t location = check_input_expression_location(parser, attribute->argument_first, attribute->argument_end);
            check_input_attribute_argument_error(reader, attributes, attribute, location, CHECK_ARGUMENT_EFFECT_CONSTANT, 0);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "reqd_work_group_size") || check_input_attribute_equal(spelled, "work_group_size_hint"))
    {
        // clang's handleWorkGroupSize: each a non-negative integer constant of 32 bits ("parameter N", from 0; each error at
        // the argument's expression), and none 0 unless all are.
        uint32_t token = attribute->argument_first;
        uint32_t zeros = 0;
        for (uint32_t i = 0; valid && i < attribute->argument_count && i < 3; i += 1)
        {
            uint32_t stop = token;
            while (stop < attribute->argument_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
            {
                stop = check_input_attribute_after_group(parser, stop, attribute->argument_end);
            }
            const uint32_t location = check_input_expression_location(parser, token, stop);
            const bool constant = ((attribute->constants >> i) & 1u) != 0 && attribute->argument_values != NULL;
            const int64_t number = constant ? attribute->argument_values[i] : 0;
            if (!constant)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, location, CHECK_ARGUMENT_PARAMETER_INTEGER, i);
                valid = false;
            }
            else if (((attribute->wide >> i) & 1u) != 0)
            {
                check_input_attribute_too_large(reader, attributes, attribute, token, stop, number);
                valid = false;
            }
            else if (number < 0)
            {
                // clang's makeAttributeArgExpr goes on past it, which the kernel's check follows.
                const uint32_t before = reader->output->report_count;
                check_input_report_argument(reader, location, CHECK_INPUT_ATTRIBUTE_ARGUMENT, CHECK_ARGUMENT_NON_NEGATIVE);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].attribute = attribute->first;
                }
            }
            zeros += constant && number == 0 ? 1u : 0u;
            token = stop + 1;
        }
        if (valid && zeros != 0 && zeros != 3)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_GREATER_ZERO, 0);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "intel_reqd_sub_group_size") && attribute->argument_count == 1)
    {
        // (checkUInt32Argument's error is at the attribute.)
        if (!attribute->constant_argument)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_AN_INTEGER_CONSTANT, 0);
            valid = false;
        }
        else if (attribute->wide != 0)
        {
            check_input_attribute_too_large(reader, attributes, attribute, attribute->argument_first, attribute->argument_end, attribute->value);
            valid = false;
        }
        else if ((uint32_t)attribute->value == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_GREATER_ZERO, 0);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "amdgpu_flat_work_group_size") || check_input_attribute_equal(spelled, "amdgpu_waves_per_eu"))
    {
        // SemaAMDGPU's checks: each an integer constant (as 32 bits unsigned), then a 0 minimum's maximum 0, and a minimum no
        // greater than the maximum (waves_per_eu's of a maximum given and not 0).
        const bool flat = check_input_attribute_equal(spelled, "amdgpu_flat_work_group_size");
        uint32_t token = attribute->argument_first;
        uint32_t limits[2] = { 0, 0 };
        for (uint32_t i = 0; valid && i < attribute->argument_count && i < 2; i += 1)
        {
            uint32_t stop = token;
            while (stop < attribute->argument_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
            {
                stop = check_input_attribute_after_group(parser, stop, attribute->argument_end);
            }
            const bool constant = ((attribute->constants >> i) & 1u) != 0 && attribute->argument_values != NULL;
            limits[i] = constant ? (uint32_t)attribute->argument_values[i] : 0;
            if (!constant)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER_APPLIED, i);
                valid = false;
            }
            else if (((attribute->wide >> i) & 1u) != 0)
            {
                check_input_attribute_too_large(reader, attributes, attribute, token, stop, attribute->argument_values[i]);
                valid = false;
            }
            token = stop + 1;
        }
        if (valid && limits[0] == 0 && limits[1] != 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_INVALID_MAX_ZERO, 0);
            valid = false;
        }
        else if (valid && (flat || limits[1] != 0) && limits[0] > limits[1])
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_INVALID_MIN_MAX, 0);
            valid = false;
        }
    }
    else if ((check_input_attribute_equal(spelled, "amdgpu_num_sgpr") || check_input_attribute_equal(spelled, "amdgpu_num_vgpr")) && attribute->argument_count == 1)
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_DECLARATIONS, CHECK_WARNING_EVENT_REGISTER_COUNT_DEPRECATED);
        if (!attribute->constant_argument)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_AN_INTEGER_CONSTANT, 0);
            valid = false;
        }
        else if (attribute->wide != 0)
        {
            check_input_attribute_too_large(reader, attributes, attribute, attribute->argument_first, attribute->argument_end, attribute->value);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "model") && text.pointer != NULL)
    {
        if (check_input_attribute_equal(text, "large"))
        {
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
        else if (!check_input_attribute_equal(text, "small"))
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->argument_first, CHECK_ARGUMENT_CODE_MODEL, 0);
            valid = false;
        }
    }
    return valid;
}


// The function an ownership attribute's subject has (#147): a function's type, or a pointer to one's.
static TypeId check_input_ownership_function(const TypeTable* types, TypeId bare)
{
    const bool pointer = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER;
    const TypeId pointee = pointer ? type_strip_aligned(types, types->types.base[type_index(bare)]) : 0;
    return bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_FUNCTION ? bare : pointee != 0 && type_kind(types, pointee) == BUSTER_TYPE_KIND_FUNCTION ? pointee : 0;
}

// Whether ownership_returns' function (`returns`) returns no pointer (#147, clang's err_ownership_takes_return_type).
static bool check_input_ownership_no_pointer(const TypeTable* types, TypeId bare, bool returns)
{
    const TypeId function = check_input_ownership_function(types, bare);
    return returns && function != 0 && type_kind(types, type_strip_aligned(types, type_return(types, function))) != BUSTER_TYPE_KIND_POINTER;
}

static uint32_t check_input_skip_attributes(const Parser* parser, uint32_t token, uint32_t end);;

// The place clang's D->getLocation() is (#147): a declaration's name, a tag's (its keyword's, `begin`, for one of none), or
// the declaration's first token.
static uint32_t check_input_attribute_located(CheckInputReader* reader, uint32_t declaration, uint32_t name, uint32_t begin)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool tag = (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0;
    const uint32_t after = tag && begin + 1 < count ? check_input_skip_attributes(parser, begin + 1, count) : count;
    const uint32_t tag_name = after < count && parser->tokens.kinds[after] == PARSE_KIND_IDENTIFIER ? after : begin;
    return name < count ? name : tag ? tag_name : begin;
}

// A named attribute's warning (check_input_attribute_named_event) with a note at the attribute (`note`, its data `data`).
static void check_input_attribute_noted_event(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningEventKind kind, CheckWarningNoteKind note,
                                              uint32_t data)
{
    const Parser* parser = reader->parser;
    const uint32_t scope_name = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
    const CheckWarningEvent event =
    {
        .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
        .kind = kind,
        .token = attribute->first,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = attribute->first,
        .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                     .data = scope_name | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
    };
    const CheckWarningNote noted =
    {
        .kind = note,
        .token = attribute->first,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = data },
    };
    check_input_warning_event(reader, &event, &noted, 1);
}

// Why a function's result is no span-like structure, as clang's CheckSpanLikeType decides (#147, malloc_span): 0 for one
// that is, else the note's reason (CHECK_WARNING_NOTE_SPAN_REASON's data): 1 incomplete, 2 no struct, 3 not two fields
// (their count from the 8th bit), 4 an expected integer field (its place from the 8th bit), 5 no pointer/integer or
// pointer/pointer pair. A pointer is one to no function; an integer one of no enumeration.
static uint32_t check_input_span_reason(const TypeTable* types, TypeId result)
{
    const TypeId bare = type_strip_aligned(types, result);
    const uint8_t kind = type_kind(types, bare);
    uint32_t reason = 0;
    if (!type_is_complete(types, bare))
    {
        reason = 1;
    }
    else if (kind != BUSTER_TYPE_KIND_STRUCT)
    {
        reason = 2;
    }
    else
    {
        const uint32_t tag = types->types.index[type_index(bare)];
        const uint32_t first = types->tags.member_begin[tag];
        const uint32_t fields = types->tags.member_count[tag];
        bool pointers[2] = { false, false };
        bool integers[2] = { false, false };
        for (uint32_t i = 0; fields == 2 && i < 2; i += 1)
        {
            const TypeId field = type_strip_aligned(types, types->members.type[first + i]);
            const uint8_t field_kind = type_kind(types, field);
            const bool pointer = field_kind == BUSTER_TYPE_KIND_POINTER;
            pointers[i] = pointer && type_kind(types, type_strip_aligned(types, types->types.base[type_index(field)])) != BUSTER_TYPE_KIND_FUNCTION;
            integers[i] = field_kind == BUSTER_TYPE_KIND_INTEGER;
        }
        reason = fields != 2 ? 3u | fields << 8 : pointers[0] && pointers[1] ? 0u : pointers[0] ? (integers[1] ? 0u : 4u | 2u << 8) :
                 pointers[1] ? (integers[0] ? 0u : 4u | 1u << 8) : 5u;
    }
    return reason;
}

// Whether `type` (a parameter's) is clang's isIntegerType: an integer's, a character's, bool's or an enumeration's.
static bool check_input_attribute_integer_type(const TypeTable* types, TypeId type)
{
    const uint8_t kind = type_kind(types, type_strip_aligned(types, type));
    return kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM;
}

// Whether `type` is one vec_type_hint takes (#147, handleVecTypeHint): an extended vector's, a floating one (a complex
// one's among them), or an integer one (a complete enumeration's, a bit-precise one's) but bool's.
static bool check_input_vec_type_hint(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, bare);
    const bool complex = kind == BUSTER_TYPE_KIND_COMPLEX &&
                         type_kind(types, type_strip_aligned(types, types->types.base[type_index(bare)])) == BUSTER_TYPE_KIND_FLOATING;
    const bool integer = (kind == BUSTER_TYPE_KIND_INTEGER && bare != BUSTER_TYPE_BOOL) || kind == BUSTER_TYPE_KIND_BIT_INT ||
                         (kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(types, bare));
    return kind == BUSTER_TYPE_KIND_EXT_VECTOR || kind == BUSTER_TYPE_KIND_FLOATING || complex || integer;
}

// preferred_type's incomplete type (#147, handlePreferredTypeAttr's RequireCompleteType), at the type past its leading
// qualifiers (its TypeLoc's start): the type as written (a typedef's name spells it, qualified or not), and a tag's note
// at the declaration its name has where the type is read, when that declares this tag.
static void check_input_attribute_incomplete(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t end = attribute->argument_end;
    uint32_t first = attribute->argument_first;
    while (first + 1 < end && parser->tokens.kinds[first] == PARSE_KIND_QUALIFIER)
    {
        first += 1;
    }
    bool lone = parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
    for (uint32_t token = first + 1; lone && token < end; token += 1)
    {
        lone = parser->tokens.kinds[token] == PARSE_KIND_QUALIFIER;
    }
    const TypeId type = attribute->argument_type;
    const uint32_t handle = lone ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[first]) : 0;
    const bool typedef_name = handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF;
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, bare);
    const bool tag = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t name = tag ? types->tags.name[types->types.index[type_index(bare)]] : 0;
    const uint32_t binding = name != 0 ? c_bindings_lookup(&reader->bindings, C_BINDING_TAG, name) : 0;
    const uint32_t declaration = binding != 0 ? reader->bindings.declaration[binding - 1] : BUSTER_CHECK_NONE;
    const bool declared = declaration < parser->tokens.count && type_unqualified(reader->declared_types[declaration]) == bare;
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_INCOMPLETE_TYPE, 0);
    if (reader->output->report_count > before)
    {
        CheckInputReport* report = &reader->output->reports[before];
        report->type = type;
        report->type_spelling = typedef_name ? reader->bindings.declaration[handle - 1] : 0;
        report->note_token = declared ? declaration : 0;
        report->note_kind = declared ? CHECK_INPUT_NOTE_FORWARD_DECLARATION : CHECK_INPUT_NOTE_NONE;
    }
}

// Static analyses' and Objective-C's attributes past their subjects (#147), as clang's handlers make them in C, changing
// nothing here: acquire_handle's integer parameter; mig_server_routine's function whose result isn't kern_return_t, a
// typedef of int (the last of its spelling's typedefs), its warning at the declaration's first token (`begin`);
// malloc_span's function of no span-like result, its warning with a note why (check_input_span_reason); iboutlet's and
// iboutletcollection's warning anywhere in C; type_visibility's, of a tag its visibility, of a typedef ignored, of
// anything else an error; param_typestate's and return_typestate's state (unknown, consumed, unconsumed), another's
// warning ("argument not supported: X", return_typestate's quoted); lifetime_capture_by's arguments (the parameters'
// names are checked where its function's declarator ends: check_input_capture_names), with -Wdeprecated-attributes'
// warning of `global`, `unknown` and `this`, and its spellings' (_global, _unknown, _this) of no arguments; the ownerships'
// identifier, counts, result and parameter indices (out of bounds, of another type, or another kind's); NSObject's and
// objc_independent_class's warnings at the declaration's place (check_input_attribute_located), NSObject's typedef of no
// pointer to void or a record an error; objc_precise_lifetime's error; vec_type_hint's type (check_input_vec_type_hint), a
// second one of another type ignored with its warning; preferred_type's incomplete type (check_input_attribute_incomplete).
// Whether the declaration goes on.
static bool check_input_attribute_annotations(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration,
                                              TypeId type, uint32_t name, uint32_t begin)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const String8 spelled = check_input_attribute_name(reader, attribute->name);
    const TypeId bare = type != 0 ? type_strip_aligned(types, type) : 0;
    const bool function = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_FUNCTION;
    const bool parameter = (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0;
    const bool typedef_name = (declaration & BUSTER_CHECK_SUBJECT_TYPEDEF) != 0;
    const bool tag = (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0;
    const uint32_t located = check_input_attribute_located(reader, declaration, name, begin);
    // The first argument's sole identifier, or BUSTER_CHECK_NONE.
    const bool lone = attribute->argument_count >= 1 && attribute->argument_first + 1 <= attribute->argument_end &&
                      parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER &&
                      (attribute->argument_first + 1 == attribute->argument_end || parser->tokens.kinds[attribute->argument_first + 1] == PARSE_KIND_COMMA);
    const String8 word = lone ? check_input_attribute_name(reader, parser->tokens.names[attribute->argument_first]) : (String8){ 0 };
    const bool capture = check_input_attribute_equal(spelled, "lifetime_capture_by");
    const bool capture_this = check_input_attribute_equal(spelled, "lifetime_capture_by_this");
    const bool capture_other = check_input_attribute_equal(spelled, "lifetime_capture_by_global") || check_input_attribute_equal(spelled, "lifetime_capture_by_unknown");
    const bool returns = check_input_attribute_equal(spelled, "ownership_returns");
    const bool ownership = returns || check_input_attribute_equal(spelled, "ownership_holds") || check_input_attribute_equal(spelled, "ownership_takes");
    bool valid = true;
    if (attribute->count_check != BUSTER_CHECK_COUNT_NONE)
    {
        // Its count's error comes first (check_input_attribute_subjects).
    }
    else if (check_input_attribute_equal(spelled, "acquire_handle") && parameter && check_input_attribute_integer_type(types, bare))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUTPUT_PARAMETER, 0);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "mig_server_routine") && function)
    {
        // The last typedef of its result's spelling (a declarator's own may add to it), and the result itself.
        uint32_t spelling = name < count ? check_input_typedef_spelling(reader->output, parser, name) : 0;
        bool kern = false;
        for (uint32_t step = 0; spelling != 0 && step < 64; step += 1)
        {
            kern = check_input_attribute_equal(check_input_attribute_name(reader, parser->tokens.names[spelling]), "kern_return_t");
            spelling = check_input_typedef_spelling(reader->output, parser, spelling);
        }
        if (!kern || type_return(types, bare) != BUSTER_TYPE_INT)
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_MIG_ROUTINE, begin, begin);
            attribute->ignored = true;
        }
    }
    else if (check_input_attribute_equal(spelled, "malloc_span") && function)
    {
        const uint32_t reason = check_input_span_reason(types, type_return(types, bare));
        if (reason != 0)
        {
            check_input_attribute_noted_event(reader, attribute, CHECK_WARNING_EVENT_SPAN_ONLY, CHECK_WARNING_NOTE_SPAN_REASON, reason);
            attribute->ignored = true;
        }
    }
    else if (check_input_attribute_equal(spelled, "forceinline") || check_input_attribute_equal(spelled, "forceinline_calls"))
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "vec_type_hint") && attribute->argument_type != 0 && !check_input_vec_type_hint(types, attribute->argument_type))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_VEC_TYPE_HINT, 0);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "vec_type_hint") && attribute->argument_type != 0)
    {
        // The first one applied wins; another of a different type is ignored.
        const CheckInputAttribute* applied = NULL;
        for (const CheckInputAttribute* other = attributes->records; applied == NULL && other < attribute; other += 1)
        {
            const bool hint = other->kind == CHECK_ATTRIBUTE_UNKNOWN && !other->keyword && !other->ignored && other->argument_type != 0 &&
                              check_input_attribute_equal(check_input_attribute_name(reader, other->name), "vec_type_hint");
            applied = hint ? other : NULL;
        }
        if (applied != NULL && applied->argument_type != attribute->argument_type)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_DIFFERENT_ARGUMENTS);
            attribute->ignored = true;
        }
    }
    else if (check_input_attribute_equal(spelled, "preferred_type") && attribute->argument_type != 0 &&
             type_kind(types, type_strip_aligned(types, attribute->argument_type)) != BUSTER_TYPE_KIND_FUNCTION &&
             !type_is_complete(types, attribute->argument_type))
    {
        check_input_attribute_incomplete(reader, attributes, attribute);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "iboutlet") || check_input_attribute_equal(spelled, "iboutletcollection"))
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_IBOUTLET);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "type_visibility") && tag)
    {
        const bool string = attribute->argument_first < attribute->argument_end && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_STRING &&
                            check_input_attribute_string(reader, attribute->argument_first, attribute->argument_end, &attribute->text);
        if (string && !check_input_attribute_equal(attribute->text, "default") && !check_input_attribute_equal(attribute->text, "hidden") &&
            !check_input_attribute_equal(attribute->text, "protected"))
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED,
                                                 attribute->argument_first, attribute->argument_first);
            attribute->ignored = true;
        }
    }
    else if (check_input_attribute_equal(spelled, "type_visibility") && typedef_name)
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "type_visibility"))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_TYPES_NAMESPACES, 0);
        valid = false;
    }
    else if ((check_input_attribute_equal(spelled, "param_typestate") || check_input_attribute_equal(spelled, "return_typestate")) && attribute->argument_count == 1)
    {
        if (!lone)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_IDENTIFIER, 0);
            valid = false;
        }
        else if (!check_input_attribute_equal(word, "unknown") && !check_input_attribute_equal(word, "consumed") && !check_input_attribute_equal(word, "unconsumed"))
        {
            const bool quoted = check_input_attribute_equal(spelled, "return_typestate");
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                                                 quoted ? CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED : CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED,
                                                 attribute->argument_first, attribute->argument_first);
            attribute->ignored = true;
        }
    }
    else if ((capture_this || capture_other) && attribute->argument_count != 0)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT);
        valid = false;
    }
    else if (capture_this)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_IMPLICIT_OBJECT, 0);
        valid = false;
    }
    else if (capture && attribute->argument_count == 0)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_NO_CAPTURE, 0);
        valid = false;
    }
    else if (capture)
    {
        // Each argument: `global`, `unknown` and `this` deprecated, `this` unavailable here, the parameter's own name
        // itself, and no identifier no parameter's name; another identifier is checked once the parameters are known.
        uint32_t first = attribute->argument_first;
        while (valid && first < attribute->argument_end)
        {
            uint32_t stop = first;
            while (stop < attribute->argument_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
            {
                stop = check_input_attribute_after_group(parser, stop, attribute->argument_end);
            }
            const bool identifier = stop == first + 1 && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
            const String8 argument = identifier ? check_input_attribute_name(reader, parser->tokens.names[first]) : (String8){ 0 };
            const bool global = identifier && (check_input_attribute_equal(argument, "global") || check_input_attribute_equal(argument, "unknown"));
            const bool self = identifier && check_input_attribute_equal(argument, "this");
            if (global || self)
            {
                check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_CAPTURE_DEPRECATED, first, first);
            }
            if (self)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_CAPTURE_THIS, 0);
                valid = false;
            }
            else if (identifier && !global && name < count && parser->tokens.names[first] == parser->tokens.names[name])
            {
                check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_CAPTURE_ITSELF, 0);
                valid = false;
            }
            else if (!identifier)
            {
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_CAPTURE_UNKNOWN, 0);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].value = first;
                }
                valid = false;
            }
            first = stop + 1;
        }
    }
    else if (ownership && !lone)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, 1);
        valid = false;
    }
    else if (ownership && !returns && attribute->argument_count < 2)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_LEAST | 2u << 8);
        valid = false;
    }
    else if (returns && attribute->argument_count > 2)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_MOST | 2u << 8);
        valid = false;
    }
    else if (ownership && check_input_ownership_no_pointer(types, bare, returns))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OWNERSHIP_RETURN, 0);
        valid = false;
    }
    else if (ownership)
    {
        // Its parameter indices (CheckInputAttribute.constants, from the second argument), as clang's
        // checkFunctionOrMethodParameterIndex reads them ("parameter N", the argument's place from 0), each of a pointer
        // (an integer for ownership_returns), and none an index another kind of ownership names.
        const TypeId target = check_input_ownership_function(types, bare);
        uint32_t parameters = 0;
        const TypeId* parameter_types = target != 0 ? type_parameters(types, target, &parameters) : NULL;
        for (uint32_t i = 1; valid && i < attribute->argument_count && i < 8; i += 1)
        {
            const bool constant = ((attribute->constants >> i) & 1u) != 0 && attribute->argument_values != NULL;
            const int64_t index = constant ? attribute->argument_values[i] : 0;
            const bool bounded = constant && index >= 1 && (uint64_t)index <= parameters;
            const TypeId parameter_type = bounded && parameter_types != NULL ? parameter_types[index - 1] : 0;
            const uint8_t parameter_kind = parameter_type != 0 ? type_kind(types, type_strip_aligned(types, parameter_type)) : 0;
            if (!constant)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, i);
                valid = false;
            }
            else if (!bounded)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, i);
                valid = false;
            }
            else if (returns ? !check_input_attribute_integer_type(types, parameter_type) : parameter_kind != BUSTER_TYPE_KIND_POINTER)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OWNERSHIP_TYPE, returns ? 1u : 0u);
                valid = false;
            }
            for (uint32_t j = 0; valid && attributes != NULL && j < attributes->count && attributes->records + j != attribute; j += 1)
            {
                const CheckInputAttribute* earlier = attributes->records + j;
                const String8 earlier_name = earlier->kind == CHECK_ATTRIBUTE_UNKNOWN && !earlier->keyword ? check_input_attribute_name(reader, earlier->name) : (String8){ 0 };
                const bool owner = check_input_attribute_equal(earlier_name, "ownership_holds") || check_input_attribute_equal(earlier_name, "ownership_takes") ||
                                   check_input_attribute_equal(earlier_name, "ownership_returns");
                bool same = false;
                for (uint32_t k = 1; owner && !earlier->ignored && earlier->argument_values != NULL && k < earlier->argument_count && k < 8; k += 1)
                {
                    same |= ((earlier->constants >> k) & 1u) != 0 && earlier->argument_values[k] == index;
                }
                if (same && !check_input_attribute_equal(earlier_name, spelled.pointer))
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OWNERSHIP_CONFLICT, 0);
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].value = earlier->first;
                    }
                    valid = false;
                }
            }
        }
    }
    else if (check_input_attribute_equal(spelled, "NSObject") && typedef_name)
    {
        // A pointer to void or a record (clang's isCARCBridgableType).
        const bool pointer = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER;
        const uint8_t pointee = pointer ? type_kind(types, type_strip_aligned(types, types->types.base[type_index(bare)])) : 0;
        if (!pointer || (pointee != BUSTER_TYPE_KIND_VOID && pointee != BUSTER_TYPE_KIND_STRUCT && pointee != BUSTER_TYPE_KIND_UNION))
        {
            check_input_attribute_argument_error(reader, attributes, attribute, located, CHECK_ARGUMENT_NSOBJECT_POINTER, 0);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "NSObject"))
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_NSOBJECT_ATTRIBUTE, CHECK_WARNING_EVENT_NSOBJECT_TYPEDEF, located, located);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "objc_independent_class"))
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_INDEPENDENT_CLASS,
                                             typedef_name ? CHECK_WARNING_EVENT_INDEPENDENT_POINTER : CHECK_WARNING_EVENT_INDEPENDENT_TYPEDEF, located, located);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "clang_builtin_alias") && attribute->argument_count == 1)
    {
        // (No builtin of x86-64's is one of its targets'.)
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_BUILTIN_ALIAS, 0);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "blocks") && attribute->argument_count == 1 && !lone)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, 1);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "blocks") && attribute->argument_count == 1 && !check_input_attribute_equal(word, "byref"))
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED,
                                             attribute->first, attribute->argument_first);
        attribute->ignored = true;
    }
    else if (check_input_attribute_equal(spelled, "blocks") && attribute->argument_count == 1 && (declaration & BUSTER_CHECK_SUBJECT_GLOBAL) != 0)
    {
        // (A static local's, inside a function, is named so.)
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_BLOCK_NONLOCAL, reader->function_result != 0 ? 1u : 0u);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "objc_precise_lifetime"))
    {
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PRECISE_LIFETIME, 0);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].type = type;
            reader->output->reports[before].type_spelling = name < count ? check_input_typedef_spelling(reader->output, parser, name) : 0;
        }
        valid = false;
    }
    return valid;
}

// A function's GPU kernel attributes where it has no kernel's convention (amdgpu_kernel, device_kernel, nvptx_kernel), as
// clang's ProcessDeclAttributeList ends (#147): the first of reqd_work_group_size, work_group_size_hint, vec_type_hint and
// intel_reqd_sub_group_size, "attribute 'X' can only be applied to an OpenCL kernel function"; then the first of AMDGPU's
// flat_work_group_size, waves_per_eu, num_sgpr and num_vgpr, "'X' attribute only applies to kernel functions"; each at the
// function's name (`name`), in that order of their names, not of the attributes.
static void check_input_attribute_kernels(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name)
{
    static const char* const groups[2][4] = {
        { "reqd_work_group_size", "work_group_size_hint", "vec_type_hint", "intel_reqd_sub_group_size" },
        { "amdgpu_flat_work_group_size", "amdgpu_waves_per_eu", "amdgpu_num_sgpr", "amdgpu_num_vgpr" },
    };
    bool kernel = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        kernel |= check_input_attribute_kernel(reader, attributes->records + i);
    }
    bool reported = false;
    for (uint32_t group = 0; !kernel && attributes != NULL && group < 2; group += 1)
    {
        const CheckInputAttribute* found = NULL;
        for (uint32_t n = 0; found == NULL && n < 4; n += 1)
        {
            for (uint32_t i = 0; found == NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                const bool generic = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && !attribute->ignored;
                found = generic && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), groups[group][n]) ? attribute : NULL;
            }
        }
        const uint32_t before = reader->output->report_count;
        if (found != NULL)
        {
            check_input_report_argument(reader, name, CHECK_INPUT_ATTRIBUTE_ARGUMENT, group == 0 ? CHECK_ARGUMENT_OPENCL_KERNEL : CHECK_ARGUMENT_KERNEL_ONLY);
            attributes->error = attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? CHECK_ATTRIBUTE_ERROR_ARGUMENTS : attributes->error;
        }
        // (Published after the attributes' own diagnostics, as clang gives it once they are applied.)
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].attribute = found->first;
            reader->output->reports[before].repeated = reported;
            reader->output->reports[before].sort = attributes->records[attributes->count - 1].end + 1;
        }
        reported |= found != NULL;
    }
}

// The retain-ownership attributes (#147): consumed (cf_, ns_, os_), the returns' (cf_returns_retained and the others) and
// os_returns_retained_on_zero's and _on_non_zero's, as SemaObjC's handlers take them in C, where no type is an
// Objective-C object or points to a C++ record: CF's a pointer (a parameter's pointee a pointer, for a returns' one), NS's
// and OS's nothing. Each warns, at the declaration's first token (`begin`), and is ignored, where its subject is no type
// it takes: consumed's "X attribute only applies to pointer parameters" (or "Objective-C object"), named as clang's
// string; the returns' of a function "'X' attribute only applies to functions that return a pointer" (or "an Objective-C
// object"), of a parameter "... pointer-to-CF-pointer parameters" (OS's "pointer/reference-to-OSObject-pointer"), of
// another declaration "... functions and methods" (NS's) or "... functions, methods, and parameters"; the out
// parameters' "... pointer/reference-to-OSObject-pointer parameters". ns_returns_retained is a type's too: a function
// type's warning at the attribute, "'ns_returns_retained' attribute only applies to functions that return an
// Objective-C object", and off one "only applies to function types" (check_input_attribute_type_warnings); a tag's
// "functions and methods" at its first token.
static void check_input_attribute_ownership(CheckInputReader* reader, CheckInputAttribute* attribute, uint32_t declaration, TypeId type, uint32_t begin)
{
    const String8 name = check_input_attribute_name(reader, attribute->name);
    const bool cf = check_input_attribute_equal(name, "cf_returns_retained") || check_input_attribute_equal(name, "cf_returns_not_retained");
    const bool os = check_input_attribute_equal(name, "os_returns_retained") || check_input_attribute_equal(name, "os_returns_not_retained");
    const bool ns = check_input_attribute_equal(name, "ns_returns_not_retained") || check_input_attribute_equal(name, "ns_returns_autoreleased");
    const bool retained = check_input_attribute_equal(name, "ns_returns_retained");
    const bool consumed_cf = check_input_attribute_equal(name, "cf_consumed");
    const bool consumed = consumed_cf || check_input_attribute_equal(name, "ns_consumed") || check_input_attribute_equal(name, "os_consumed");
    const bool out = check_input_attribute_equal(name, "os_returns_retained_on_zero") || check_input_attribute_equal(name, "os_returns_retained_on_non_zero");
    // (A tag's declaration has no type here.)
    const TypeId bare = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    const bool function = (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0;
    const bool parameter = (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0;
    const bool tag = (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0;
    const bool pointer = type_kind(reader->types, bare) == BUSTER_TYPE_KIND_POINTER;
    const TypeId pointee = pointer ? type_strip_aligned(reader->types, reader->types->types.base[type_index(bare)]) : 0;
    const TypeId result = function && type_kind(reader->types, bare) == BUSTER_TYPE_KIND_FUNCTION ? type_strip_aligned(reader->types, type_return(reader->types, bare)) : 0;
    CheckWarningEventKind kind = CHECK_WARNING_EVENT_KIND_COUNT;
    uint32_t at = begin;
    if ((cf || os || ns) && function)
    {
        kind = cf && type_kind(reader->types, result) == BUSTER_TYPE_KIND_POINTER ? CHECK_WARNING_EVENT_KIND_COUNT :
               ns ? CHECK_WARNING_EVENT_OWNERSHIP_RETURN_OBJECT : CHECK_WARNING_EVENT_OWNERSHIP_RETURN_POINTER;
    }
    else if ((cf || os || ns) && parameter)
    {
        const bool taken = cf && pointer && type_kind(reader->types, pointee) == BUSTER_TYPE_KIND_POINTER;
        kind = taken ? CHECK_WARNING_EVENT_KIND_COUNT :
               os || (!pointer && !cf) ? CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_OS : CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_CF;
    }
    else if (cf || os || ns)
    {
        kind = ns ? CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS : CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS_PARAMETERS;
    }
    else if (retained && (declaration & BUSTER_CHECK_SUBJECT_FUNCTION_LIKE) != 0)
    {
        kind = CHECK_WARNING_EVENT_OWNERSHIP_RETAINED;
        at = attribute->first;
    }
    else if (retained && tag)
    {
        kind = CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS;
    }
    else if (consumed && !(consumed_cf && pointer))
    {
        kind = check_input_attribute_equal(name, "ns_consumed") ? CHECK_WARNING_EVENT_CONSUMED_OBJECT : CHECK_WARNING_EVENT_CONSUMED_POINTER;
    }
    else if (out)
    {
        kind = CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_OS;
    }
    if (kind != CHECK_WARNING_EVENT_KIND_COUNT && at < reader->parser->tokens.count)
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, kind, at, attribute->first + 1);
        attribute->ignored = true;
        attribute->subject_ignored = true;
    }
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
        // (A GPU kernel's convention, which a type off a function ignored, is a declaration's attribute: its subject's error.)
        const bool kernel = check_input_attribute_kernel(reader, attribute);
        // matrix_type without -fenable-matrix (#147): clang's error, then a typedef's subject's off one; cfi_salt is a
        // function type's, whose warning off one is check_input_attribute_type_warnings', not a subject's.
        const bool generic_name = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword;
        const String8 generic_spelled = generic_name ? check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
        const bool matrix = generic_name && check_input_attribute_equal(generic_spelled, "matrix_type") &&
                            (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) == 0;
        // overflow_behavior without -fexperimental-overflow-behavior-types: clang's warning that it ignores it, but on a tag,
        // where its subject's warning comes.
        const bool tag_declaration = (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0;
        const bool overflow = generic_name && !tag_declaration && check_input_attribute_equal(generic_spelled, "overflow_behavior") && !attribute->ignored;
        // Other targets' vector types are types' attributes: clang's target error first wherever a declarator's type takes
        // them (a tag's declaration has its subject's); and the statements' attributes are a declaration's error.
        const bool riscv = generic_name && check_input_attribute_equal(generic_spelled, "riscv_rvv_vector_bits");
        const bool vector_bits = (riscv || (generic_name && check_input_attribute_equal(generic_spelled, "arm_sve_vector_bits"))) && !tag_declaration;
        const bool statement_only = generic_name && (check_input_attribute_equal(generic_spelled, "atomic") || check_input_attribute_equal(generic_spelled, "code_align") ||
                                                     check_input_attribute_equal(generic_spelled, "opencl_unroll_hint") ||
                                                     check_input_attribute_equal(generic_spelled, "assume"));
        if ((vector_bits || statement_only) && !attribute->ignored)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first,
                                                 vector_bits ? CHECK_ARGUMENT_TARGET_FEATURE : CHECK_ARGUMENT_INVALID_ON_DECLARATION, riscv ? 1u : 0u);
            valid = false;
            continue;
        }
        if (matrix && !attribute->ignored)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_MATRIX_DISABLED, 0);
            const uint32_t before = reader->output->report_count;
            if ((declaration & BUSTER_CHECK_SUBJECT_TYPEDEF) == 0)
            {
                check_input_report(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_SUBJECT);
            }
            else if (attribute->count_check != BUSTER_CHECK_COUNT_NONE)
            {
                check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            }
            // (After the first, at the same token, which the driver keeps.)
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].repeated = true;
                reader->output->reports[before].completion = attribute->first + 1;
            }
            valid = false;
            continue;
        }
        const CheckInputSubjectRow* row = attribute->keyword || overflow || (attribute->ignored && !attribute->subject_ignored && !transparent && !kernel) ? NULL :
                                          check_input_subject_row(scope, check_input_attribute_name(reader, attribute->name), scoped, attribute->standard);
        // A shared record another declarator ignored applies to this one when its subjects fit (checked again below).
        attribute->ignored &= !(attribute->subject_ignored && !transparent);
        attribute->subject_ignored = false;
        if (overflow)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_OVERFLOW_BEHAVIOR, CHECK_WARNING_EVENT_OVERFLOW_BEHAVIOR);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
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
        // clang's retain-ownership attributes (#147, SemaObjC's handleXReturnsXRetainedAttr and AddXConsumedAttr), which change
        // nothing in C: check_input_attribute_ownership; and other handlers' checks (check_input_attribute_handled).
        if (valid && generic && !attribute->ignored && !attribute->subject_ignored)
        {
            check_input_attribute_ownership(reader, attribute, declaration, type, begin);
        }
        if (valid && generic && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_attribute_handled(reader, attributes, attribute, declaration, type, name);
        }
        if (valid && generic && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_attribute_annotations(reader, attributes, attribute, declaration, type, name, begin);
        }
        // A GPU kernel's convention off a function (#147): clang's "'X' attribute only applies to functions" error.
        if (valid && kernel && (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_KERNEL_FUNCTIONS, 0);
            valid = false;
        }
        // A function type's attribute on a tag's declaration (#147, clang's handleCallConvAttr and handleNoReturnAttr of a
        // declaration without a declarator): a convention's arguments (pcs's) first, then x86-64's warning of one it
        // hasn't; then "'X' attribute only applies to functions and methods", which ignores it. regparm has none there.
        const bool tag = (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0;
        const bool convention = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !check_input_attribute_kernel(reader, attribute);
        const bool function_type = convention || (attribute->kind == CHECK_ATTRIBUTE_NORETURN && !attribute->standard && !attribute->keyword) ||
                                   (attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword &&
                                    check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "cfi_unchecked_callee"));
        if (valid && tag && function_type && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_attribute_pcs(reader, attributes, attribute);
            if (valid && convention && attribute->value == CHECK_CALL_IGNORED_X86_32)
            {
                check_input_attribute_unsupported(reader, attribute);
            }
            if (valid)
            {
                check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_FUNCTIONS_METHODS, attribute->first,
                                                     attribute->first + 1);
            }
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        // mode's vector mode, the declarator's warning (#147, check_input_attribute_mode).
        String8 mode_rest = { 0 };
        if (valid && attribute->kind == CHECK_ATTRIBUTE_MODE && !attribute->ignored && attribute->argument_first < attribute->argument_end &&
            check_input_mode_vector(reader, attribute, &mode_rest) != 0)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_VECTOR_MODE);
        }
        // target's handler (#147, check_input_target_warning): a string clang's checkTargetAttr doesn't take, its warning,
        // which ignores the attribute.
        if (valid && attribute->kind == CHECK_ATTRIBUTE_TARGET && !attribute->subject_ignored && check_input_target_warning(reader, attribute))
        {
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        // no_sanitize's (#147): each sanitizer clang doesn't know, its -Wunknown-sanitizers warning at the string; on a
        // variable of static storage, each one but address, hwaddress and memtag, its warning at the variable's name.
        for (uint32_t argument = 0; valid && attribute->kind == CHECK_ATTRIBUTE_NO_SANITIZE && !attribute->subject_ignored && argument < attribute->argument_count;
             argument += 1)
        {
            uint32_t first = attribute->argument_first;
            uint32_t end = attribute->argument_end;
            String8 sanitizer = { 0 };
            const bool string = check_input_attribute_argument(attribute, argument, &first, &end) && check_input_attribute_string(reader, first, end, &sanitizer);
            const PredefinesSanitizers known = string ? predefines_sanitizer_value(sanitizer.pointer, sanitizer.length) : (PredefinesSanitizers){ 0 };
            const bool unknown = string && known.kinds == 0 && known.groups == 0 && !check_input_attribute_equal(sanitizer, "coverage");
            const bool global = (declaration & (BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_GLOBAL)) == (BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_GLOBAL);
            if (unknown)
            {
                check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_UNKNOWN_SANITIZERS, CHECK_WARNING_EVENT_UNKNOWN_SANITIZER, first, first);
            }
            else if (string && global && name < parser->tokens.count && !check_input_attribute_equal(sanitizer, "address") &&
                     !check_input_attribute_equal(sanitizer, "hwaddress") && !check_input_attribute_equal(sanitizer, "memtag"))
            {
                check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_SANITIZER_GLOBAL, name, first);
            }
        }
        // visibility's handler (#147): on a typedef, its "attribute ignored" warning; else a visibility it doesn't know, its
        // "argument not supported" warning at the string (check_input_attribute_validate ignores it).
        const bool visibility = valid && attribute->kind == CHECK_ATTRIBUTE_VISIBILITY && !attribute->subject_ignored;
        if (visibility && (declaration & BUSTER_CHECK_SUBJECT_TYPEDEF) != 0)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED);
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        else if (visibility && !check_input_visibility_known(attribute->text))
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED, attribute->argument_first,
                                                 attribute->argument_first);
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
            if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
            {
                attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
                attributes->error_first = attribute->first;
                attributes->error_end = attribute->end;
            }
        }
        else if (kept && attribute->kind == CHECK_ATTRIBUTE_THREAD_SAFETY)
        {
            valid = check_input_thread_attribute(reader, attribute, declaration, type);
        }
        else if (kept && attribute->deferred != NULL)
        {
            // Its argument's error, its handler's once its subject takes it (check_input_attribute_defer).
            CheckInput* output = reader->output;
            if (output->report_count < output->report_capacity)
            {
                output->reports[output->report_count++] = *attribute->deferred;
            }
            output->resource |= output->report_count >= output->report_capacity && output->reports[output->report_count - 1].token != attribute->deferred->token;
            attributes->error = attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? CHECK_ATTRIBUTE_ERROR_ARGUMENTS : attributes->error;
            valid = false;
        }
    }
    // A kernel's attributes on a function of no GPU kernel's convention (#147, clang's ProcessDeclAttributeList's end).
    if (valid && (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0 && name < parser->tokens.count)
    {
        check_input_attribute_kernels(reader, attributes, name);
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

// Whether declaration `d`, before `token`, is one clang's lookup of `token`'s entity finds in its chain (#147): at file
// scope, or in a block `token` is in (CheckInputReader.declaration_scope_ends).
static bool check_input_declaration_visible(const CheckInputReader* reader, uint32_t d, uint32_t token)
{
    return reader->declaration_scope_ends == NULL || reader->declaration_scope_ends[d] == BUSTER_CHECK_NONE || token <= reader->declaration_scope_ends[d];
}

// Whether `attribute`'s name is one of a mutual exclusion's (check_input_exclusions), which an earlier declaration's
// attribute may exclude.
static bool check_input_attribute_excludable(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const String8 name = check_input_attribute_name(reader, attribute->name);
    bool listed = false;
    for (uint32_t r = 0; !listed && r < BUSTER_ARRAY_LENGTH(check_input_exclusions); r += 1)
    {
        listed = name.length == check_input_exclusions[r].length && memcmp(name.pointer, check_input_exclusions[r].spelling, name.length) == 0;
    }
    return listed;
}

// clang's error for an attribute of a redeclaration that an earlier declaration's of its entity excludes (#147, as its
// mergeDeclAttributes takes them in): at the attribute, with the note at the earlier one. On the serial path, where the
// declarations of `previous`'s entity before `token` are found by their linked object: where `token` has a visible
// previous declaration (`visible`), those its lookup finds alone (check_input_declaration_visible), and otherwise every
// one, as clang finds a block's extern declaration in a closed scope (findLocallyScopedExternCDecl).
static void check_input_attribute_merge_exclusions(CheckInputReader* reader, uint32_t token, uint32_t previous, bool visible)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : 0;
    const uint32_t object = reader->objects != NULL ? reader->objects[previous] : 0;
    bool reported = false;
    for (uint32_t i = 0; !reader->lane && !reported && object != 0 && attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored && !attribute->keyword;
        const String8 name = kept ? check_input_attribute_name(reader, attribute->name) : (String8){ "", 0 };
        const bool listed = kept && check_input_attribute_excludable(reader, attribute);
        const bool scalar = reader->parser->options.scalar;
        const CheckSparseWide all = reader->output->attributes;
        for (uint32_t first = listed ? check_input_attributed_next(all, 0, token, scalar) : token; !reported && first < token;
             first = check_input_attributed_next(all, first + 16, token, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, token, scalar);
            for (uint32_t selected = lanes; !reported && selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const bool seen = reader->objects[d] == object && (!visible || check_input_declaration_visible(reader, d, token));
                const CheckInputAttributes* earlier = seen ? check_input_attributes_at(reader->output, d) : 0;
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
static void check_input_attribute_late(CheckInputReader* reader, uint32_t token, uint32_t previous, uint32_t point, bool visible)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : 0;
    const bool after = attributes != 0 && reader->definition_origins != NULL && reader->definition_origins[previous] &&
                       !check_input_bit(reader->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED);
    bool late = false;
    // A body on a lane: what the earlier declarations' attributes decide, which the lane can't read (other lanes' columns
    // hold them), and what its own decide for later declarations, which it can't publish to them, is source order's
    // (#147): the declaration attributes clang drops after the definition and checks a later one's against
    // (check_input_attribute_after_kind), and those of a mutual exclusion (check_input_attribute_merge_exclusions).
    bool replay = false;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored;
        late |= after && !attribute->ignored && ((reader->late_attributes && check_input_attribute_late_kind(attribute)) ||
                                                 (!reader->lane && !attribute->subject_ignored && check_input_attribute_after_kind(reader, attribute)));
        // (One the declaration inherits, as naked from a file-scope one, check_input_naked_inherit, isn't the body's.)
        const bool written = reader->function_body >= reader->parser->tokens.count || attribute->first > reader->function_body;
        replay |= reader->lane && kept && written && (check_input_attribute_after_kind(reader, attribute) ||
                                                      (!attribute->keyword && check_input_attribute_excludable(reader, attribute)));
    }
    check_input_lane_refuse(reader, replay, "a body's redeclaration has attributes its entity's other declarations decide");
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
                    // (A file-scope declaration's alone: a block's attributes never reach the definition's, #147.)
                    const bool file_scope = reader->declaration_scope_ends == NULL || reader->declaration_scope_ends[d] == BUSTER_CHECK_NONE;
                    if (object != 0 && object <= count && file_scope)
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
    check_input_attribute_merge_exclusions(reader, token, previous, visible);
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
static bool check_input_attribute_function(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type, uint32_t name)
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
            check_input_alignas_error(reader, attributes, NULL, BUSTER_CHECK_NONE, 0, 0);
            valid = false;
        }
        const bool allocation = attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE || attribute->kind == CHECK_ATTRIBUTE_ALLOC_ALIGN;
        const bool nonnull = attribute->kind == CHECK_ATTRIBUTE_NONNULL;
        const bool format = attribute->kind == CHECK_ATTRIBUTE_FORMAT || attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG;
        // format's kind, without a keyword's underscores, as clang's getFormatAttrKind knows it (#147): one it ignores, and
        // one it doesn't know, which its warning ignores; strftime's, whose third argument is 0.
        String8 kind_name = attribute->kind == CHECK_ATTRIBUTE_FORMAT ? check_input_attribute_name(reader, reader->parser->tokens.names[attribute->argument_first]) :
                                                                        (String8){ 0 };
        const bool surrounded = kind_name.length >= 4 && kind_name.pointer[0] == '_' && kind_name.pointer[1] == '_' &&
                                kind_name.pointer[kind_name.length - 2] == '_' && kind_name.pointer[kind_name.length - 1] == '_';
        kind_name = surrounded ? (String8){ kind_name.pointer + 2, kind_name.length - 4 } : kind_name;
        const bool strftime = check_input_attribute_equal(kind_name, "strftime") || check_input_attribute_equal(kind_name, "gnu_strftime");
        const bool ignored_format = check_input_attribute_equal(kind_name, "gcc_diag") || check_input_attribute_equal(kind_name, "gcc_cdiag") ||
                                    check_input_attribute_equal(kind_name, "gcc_cxxdiag") || check_input_attribute_equal(kind_name, "gcc_tdiag");
        static const char* const format_kinds[] = {
            "NSString", "CFString", "scanf", "printf", "printf0", "strfmon", "gnu_scanf", "gnu_printf", "gnu_strfmon", "cmn_err",
            "vcmn_err", "zcmn_err", "kprintf", "syslog", "freebsd_kprintf", "os_trace", "os_log",
        };
        bool recognized_format = attribute->kind != CHECK_ATTRIBUTE_FORMAT || strftime;
        for (uint32_t k = 0; !recognized_format && k < BUSTER_ARRAY_LENGTH(format_kinds); k += 1)
        {
            recognized_format = check_input_attribute_equal(kind_name, format_kinds[k]);
        }
        const bool pointer_result = type_kind(reader->types, type_strip_aligned(reader->types, result)) == BUSTER_TYPE_KIND_POINTER;
        if (!recognized_format && !ignored_format)
        {
            check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED, attribute->first,
                                                 attribute->argument_first);
        }
        // alloc_size's, alloc_align's and assume_aligned's function that returns no pointer: their handlers' warnings, which
        // ignore them (assume_aligned's before its arguments' errors, check_input_attribute_defer).
        const bool assumed = attribute->kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && !attribute->ignored;
        if ((allocation || assumed) && !pointer_result)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
                                              attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE ? CHECK_WARNING_EVENT_RETURN_POINTERS : CHECK_WARNING_EVENT_RETURN_POINTERS_REFERENCES);
        }
        if (!recognized_format || ((allocation || assumed) && !pointer_result))
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
            // clang's error of an argument past its bounds' (#147): a parameter's type, at the argument (PARAMETER_TYPE), or
            // at the attribute.
            CheckAttributeArgumentError failure = CHECK_ARGUMENT_COUNT;
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
                    failure = valid ? failure : CHECK_ARGUMENT_PARAMETER_TYPE;
                }
                if (valid && format)
                {
                    valid = check_input_attribute_string_type(reader->types, parameter);
                    failure = valid ? failure : CHECK_ARGUMENT_FORMAT_STRING;
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
            // format's first argument to check, as clang's handleFormatAttr reads it (its 32 bits unsigned): 0, or strftime's
            // error; a variadic function's first variadic argument; else past the format, with -Wgcc-compat's warning at the
            // declaration's name.
            const uint32_t first_checked = valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT ? (uint32_t)attribute->argument_values[2] : 0;
            if (first_checked != 0 && strftime)
            {
                valid = false;
                failure = CHECK_ARGUMENT_STRFTIME;
            }
            else if (first_checked != 0 && variadic)
            {
                valid = first_checked == count + 1;
                out_of_bounds = !valid;
                failed = 2;
            }
            else if (first_checked != 0)
            {
                if (name < reader->parser->tokens.count)
                {
                    check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_GCC_COMPAT, CHECK_WARNING_EVENT_GCC_VARIADIC, name, BUSTER_CHECK_WARNING_NO_SOURCE);
                }
                valid = first_checked > (uint32_t)attribute->argument_values[1];
                out_of_bounds = !valid;
                failed = 2;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG)
            {
                valid = check_input_attribute_string_type(reader->types, result);
                failure = valid ? failure : CHECK_ARGUMENT_FORMAT_RESULT;
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
            else if (!valid && failure != CHECK_ARGUMENT_COUNT)
            {
                uint32_t first = attribute->argument_first;
                uint32_t after = attribute->end;
                check_input_attribute_argument(attribute, failed, &first, &after);
                check_input_attribute_argument_error(reader, attributes, attribute, failure == CHECK_ARGUMENT_PARAMETER_TYPE ? first : attribute->first, failure, 0);
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
static uint32_t check_input_enum_base_end(const Parser* parser, uint32_t tag, uint32_t colon, uint32_t end);
static uint32_t check_input_enum_base_skip(const Parser* parser, uint32_t colon, uint32_t end);

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

// The deprecated or unavailable record of `attributes` whose attribute is before `bound`, kept, or null: its first
// unavailable one, else its first deprecated one, as clang's Decl::getAvailability takes the worst of a declaration's
// attributes, whatever their order. A tag's records gather its declarations', which a use sees only before it.
static const CheckInputAttribute* check_input_deprecated_record_before(const CheckInputAttributes* attributes, uint32_t bound)
{
    const CheckInputAttribute* deprecated = NULL;
    const CheckInputAttribute* unavailable = NULL;
    for (uint32_t i = 0; attributes != NULL && unavailable == NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored && attribute->first < bound;
        unavailable = kept && attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE ? attribute : NULL;
        deprecated = deprecated == NULL && kept && attribute->kind == CHECK_ATTRIBUTE_DEPRECATED ? attribute : deprecated;
    }
    const CheckInputAttribute* result = unavailable != NULL ? unavailable : deprecated;
    return result;
}

// The deprecated or unavailable record of `attributes`, kept, or null (check_input_deprecated_record_before).
static const CheckInputAttribute* check_input_deprecated_record(const CheckInputAttributes* attributes)
{
    const CheckInputAttribute* result = check_input_deprecated_record_before(attributes, UINT32_MAX);
    return result;
}

// The deprecated or unavailable record that marks what the declaration at `key` declares by its declarations at `limit`
// or before (marks: the first, chain: the next of the same key; check_input_deprecated_uses'), and by attributes before
// `bound`: an unavailable one over a deprecated one, and the latest of its kind, as a redeclaration's attributes are its
// own and then those it inherits, which clang's Decl::getAvailability reads in that order for the worst.
static const CheckInputAttribute* check_input_availability_mark(const CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain,
                                                                uint32_t key, uint32_t limit, uint32_t bound)
{
    const uint32_t count = reader->parser->tokens.count;
    const CheckInputAttribute* deprecated = NULL;
    const CheckInputAttribute* unavailable = NULL;
    for (uint32_t marking = key < count ? marks[key] : 0; marking != 0 && marking - 1 <= limit && marking - 1 < count; marking = chain[marking - 1])
    {
        const CheckInputAttribute* record = check_input_deprecated_record_before(check_input_attributes_at(reader->output, marking - 1), bound);
        unavailable = record != NULL && record->kind == CHECK_ATTRIBUTE_UNAVAILABLE ? record : unavailable;
        deprecated = record != NULL && record->kind == CHECK_ATTRIBUTE_DEPRECATED ? record : deprecated;
    }
    const CheckInputAttribute* result = unavailable != NULL ? unavailable : deprecated;
    return result;
}

// The bit of a context (BUSTER_CHECK_CONTEXT_*) that the declaration at `key` makes, where a deprecated or unavailable
// attribute marks it by a declaration at `limit` or before (check_input_availability_mark), and the tokens from `first`
// to `end` (past the last) that take it. A struct's, a union's or an enum's body among them takes it only with `tags`:
// clang reads a member's or an enumerator's use in its own context, then its tag's parent's, which is a function whose
// body holds it but no declarator whose bound, parameter list or initializer does.
// The context bit (BUSTER_CHECK_CONTEXT_*) of a deprecated or unavailable record, or 0.
static uint8_t check_input_availability_bit(const CheckInputAttribute* attribute)
{
    const uint8_t result = attribute == NULL ? 0 : attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE ? BUSTER_CHECK_CONTEXT_UNAVAILABLE : BUSTER_CHECK_CONTEXT_DEPRECATED;
    return result;
}

// Gives the tokens from `first` to `end` (past the last) the context `bit`, but with `tags` only a struct's, a union's or
// an enum's body among them (check_input_availability_fill).
static void check_input_availability_fill_range(const Parser* parser, uint8_t* contexts, uint8_t bit, uint32_t first, uint32_t end, bool tags)
{
    const uint32_t count = parser->tokens.count;
    for (uint32_t i = first; bit != 0 && i < end && i < count;)
    {
        const uint8_t role = parser->tree.roles[i];
        const uint32_t closer = parser->tree.parents[i];
        const bool own = !tags && (role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM) && closer < count && closer > i;
        contexts[i] |= own ? 0 : bit;
        i = own ? closer + 1 : i + 1;
    }
}

static void check_input_availability_fill(const CheckInputReader* reader, uint8_t* contexts, const uint32_t* marks, const uint32_t* chain, uint32_t key,
                                          uint32_t limit, uint32_t first, uint32_t end, bool tags)
{
    const uint8_t bit = check_input_availability_bit(check_input_availability_mark(reader, marks, chain, key, limit, UINT32_MAX));
    check_input_availability_fill_range(reader->parser, contexts, bit, first, end, tags);
}

// A tag's declaration at its keyword `keyword` (#147): past its name, its attributes and an enum's base (`start`), its
// body's `}` where it is a definition, and the tag's worst mark there: its declarations' up to the body, and an enum's
// attributes after it (clang applies those before its enumerators' uses), but not a struct's, which come after its
// members'. A named tag's marks gather by its first declaration's; an anonymous one's are its keyword's.
typedef struct CheckInputTagMark CheckInputTagMark;
struct CheckInputTagMark
{
    uint32_t start;
    uint32_t closer;
    bool defined;
    const CheckInputAttribute* attribute;
};

static CheckInputTagMark check_input_tag_mark(const CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool is_enum = parser->tokens.kinds[keyword] == PARSE_KIND_ENUM;
    const uint32_t name = check_input_skip_attributes(parser, keyword + 1, count);
    const uint32_t after_name = name < count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER ? name + 1 : name;
    const uint32_t start = is_enum ? check_input_enum_base_end(parser, keyword, check_input_skip_attributes(parser, after_name, count), count) : after_name;
    const uint32_t body = check_input_skip_attributes(parser, start, count);
    const uint8_t body_role = body < count ? parser->tree.roles[body] : PARSE_ROLE_NONE;
    const uint32_t closer = body < count ? parser->tree.parents[body] : count;
    const bool defined = body < count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE && (body_role == PARSE_ROLE_OPEN_STRUCT || body_role == PARSE_ROLE_OPEN_ENUM) &&
                         closer < count && closer > body;
    const uint32_t bound = defined && is_enum ? check_input_skip_attributes(parser, closer + 1, count) : body;
    const uint32_t key = check_input_deprecated_key(reader, keyword);
    const CheckInputTagMark result = {
        .start = start,
        .closer = defined ? closer : count,
        .defined = defined,
        .attribute = key < count ? check_input_availability_mark(reader, marks, chain, key, bound, bound) :
                                   check_input_deprecated_record_before(check_input_attributes_at(reader->output, keyword), bound),
    };
    return result;
}

// A marked tag's own definition, from its keyword at `keyword` through its body (#147): a struct's, a union's or an
// enum's body is its tag's context, which clang reads its members' and enumerators' uses in, and its nested tags' too;
// the mark is check_input_tag_mark's. From past its name: the name of a definition with declarators is a use of the tag,
// which its own mark doesn't drop; and past an enum's base, whose uses check_input_enum_base_context gives a context.
static void check_input_availability_tag(const CheckInputReader* reader, uint8_t* contexts, const uint32_t* marks, const uint32_t* chain, uint32_t keyword)
{
    const CheckInputTagMark tag = check_input_tag_mark(reader, marks, chain, keyword);
    if (tag.defined)
    {
        check_input_availability_fill_range(reader->parser, contexts, check_input_availability_bit(tag.attribute), tag.start, tag.closer + 1, true);
    }
}

// The token after a tag's name at `name`, its attributes and an enum's base (`enum E : int {`): its body's `{`, or what
// follows, as check_input_tag_body finds it from the keyword.
static uint32_t check_input_name_body(const Parser* parser, uint32_t name, uint32_t end)
{
    return check_input_enum_base_skip(parser, check_input_skip_attributes(parser, name + 1, end), end);
}

// Whether the token at `token` is a typedef's declarator's name (#147): C_BINDING_TYPEDEF is also every other token's
// declaration kind, zero, so it is a typedef's where it declares itself and an object (a member doesn't).
static bool check_input_typedef_declared(const CheckInputReader* reader, uint32_t token)
{
    const bool result = token < reader->parser->tokens.count && reader->parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                        reader->declaration_kinds[token] == C_BINDING_TYPEDEF && reader->output->declarations[token] == token && reader->objects != NULL &&
                        reader->objects[token] != 0;
    return result;
}

// Whether the token at `token` is a declarator's name: an object's, a function's, an enumerator's, a tag's or a typedef's.
static bool check_input_declares(const CheckInputReader* reader, uint32_t token)
{
    const bool result = token < reader->parser->tokens.count && (reader->declaration_kinds[token] != 0 || check_input_typedef_declared(reader, token));
    return result;
}

// The mark a declarator's name at `name` has: an object's by its declarations (check_input_availability_mark), or null;
// and a member's, by the record that marks it, as context bits in `*member_bit` (CheckInputReader.member_marks, by its
// name's token: one load, whatever the unit's marked members).
static const CheckInputAttribute* check_input_declarator_mark(const CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain, uint32_t name,
                                                              uint8_t* member_bit)
{
    const uint32_t count = reader->parser->tokens.count;
    const bool object = name < count && check_input_declares(reader, name);
    const CheckInputAttribute* result = object ? check_input_availability_mark(reader, marks, chain, check_input_deprecated_key(reader, name), name, UINT32_MAX) : NULL;
    *member_bit = name < count && !object && reader->member_marks != NULL ? reader->member_marks[name] : 0;
    return result;
}

// Whether a context drops a use whose mark is unavailable or not: a deprecated one drops a deprecated use, an unavailable
// one an unavailable use too.
static bool check_input_availability_dropped(uint8_t context, bool unavailable)
{
    const bool result = (context & BUSTER_CHECK_CONTEXT_UNAVAILABLE) != 0 || (!unavailable && (context & BUSTER_CHECK_CONTEXT_DEPRECATED) != 0);
    return result;
}

// How many times clang diagnoses the use of a tag at `t` in a declaration's specifiers (#147): once for each declarator
// the declaration has, as its delayed diagnostics give each declarator the specifiers' own, each in that declarator's
// context, so a declarator whose mark (an object's, a member's) drops the use by the severity rule takes none. A use in
// a group (a type name's, a parameter's) is one. `context` is the use's own (check_input_availability_contexts), and
// `unavailable` whether its mark is; `*first` takes the first declarator's mark's context bits, and `*bare` whether there
// is none (a group, or a declaration of the tag alone). A
// fixed enum's underlying type (`enum E : T { ... } x`, whose T is no declarator's name), a definition's body after the
// tag's name and attribute groups (whose names aren't declarators') are passed over; the declarators are separated by
// the commas at the declaration's level, and end at its `;` or a function's body.
static uint32_t check_input_tag_use_copies(const CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain, uint32_t t, uint8_t context, bool unavailable,
                                           uint8_t* first, bool* bare)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    int32_t depth = 0;
    int32_t lists = 0;
    bool grouped = false;
    bool scanning = true;
    uint32_t name = count;
    uint32_t copies = 0;
    uint32_t declarators = 0;
    uint32_t first_name = count;
    uint8_t first_bits = 0;
    for (uint32_t at = t + 1; scanning && at <= count;)
    {
        const bool attribute = at < count && check_input_attribute_starts(parser, at, count);
        const uint8_t kind = at < count && !attribute ? parser->tokens.kinds[at] : PARSE_KIND_END;
        const uint8_t role = at < count && !attribute ? parser->tree.roles[at] : PARSE_ROLE_NONE;
        const uint32_t closer = at < count ? parser->tree.parents[at] : count;
        const bool tag_body = kind == PARSE_KIND_L_BRACE && (role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM) && closer < count && closer > at;
        const bool base = depth == 0 && kind == PARSE_KIND_COLON && role == PARSE_ROLE_SPECIFIER;
        const bool opens = !tag_body && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE) && role != PARSE_ROLE_OPEN_BODY;
        const bool closes = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        grouped = depth == 0 && closes;
        const bool last = at == count || grouped || (depth == 0 && (kind == PARSE_KIND_SEMI || role == PARSE_ROLE_OPEN_BODY));
        const bool segment = last || (depth == 0 && kind == PARSE_KIND_COMMA);
        // A declarator's name: its first identifier outside the parameter lists, an object's or a member's.
        const bool names = !segment && name == count && lists == 0 && kind == PARSE_KIND_IDENTIFIER;
        name = names ? at : name;
        // Each segment is a declarator, which drops its copy by its own mark if it has one.
        if (segment && !grouped)
        {
            uint8_t member_bit = 0;
            const CheckInputAttribute* mark = check_input_declarator_mark(reader, marks, chain, name, &member_bit);
            const uint8_t bits = check_input_availability_bit(mark) | member_bit;
            copies += !check_input_availability_dropped(context | bits, unavailable);
            first_bits = declarators == 0 && name < count ? bits : first_bits;
            first_name = declarators == 0 ? name : first_name;
            declarators += 1;
        }
        name = segment ? count : name;
        depth += opens;
        depth -= closes && !grouped;
        lists += (role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_CALL) - (!grouped && (role == PARSE_ROLE_CLOSE_FUNCTION || role == PARSE_ROLE_CLOSE_CALL));
        scanning = !last;
        at = attribute ? check_input_skip_attributes(parser, at, count) : tag_body ? closer + 1 : base ? check_input_enum_base_skip(parser, at, count) : at + 1;
    }
    *first = grouped ? 0 : first_bits;
    *bare = grouped || declarators == 0 || (declarators == 1 && first_name == count);
    const uint32_t result = grouped ? !check_input_availability_dropped(context, unavailable) : copies;
    return result;
}

// The `:` of the fixed enum's underlying type the use at `t` is in, past the parser's specifier `:` (an enum base's,
// which a bit-field's width's isn't) over specifiers alone; the token count where it is in none.
static uint32_t check_input_enum_base_colon(const Parser* parser, uint32_t t)
{
    uint32_t j = t;
    while (j > 0 && parser->tree.roles[j - 1] == PARSE_ROLE_SPECIFIER && parser->tokens.kinds[j - 1] != PARSE_KIND_COLON)
    {
        j -= 1;
    }
    const bool based = j > 0 && parser->tokens.kinds[j - 1] == PARSE_KIND_COLON && parser->tree.roles[j - 1] == PARSE_ROLE_SPECIFIER;
    return based ? j - 1 : parser->tokens.count;
}

// The `enum` keyword of the base whose `:` is at `colon`: back past the enum's name and attributes; the token count
// where none is there.
static uint32_t check_input_enum_base_keyword(const Parser* parser, uint32_t colon)
{
    uint32_t j = colon;
    int32_t depth = 0;
    bool searching = true;
    while (searching && j > 0)
    {
        j -= 1;
        const uint8_t kind = parser->tokens.kinds[j];
        depth += (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE) - (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE);
        searching = !(depth == 0 && (kind == PARSE_KIND_ENUM || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE ||
                                     kind == PARSE_KIND_COMMA));
    }
    return !searching && parser->tokens.kinds[j] == PARSE_KIND_ENUM ? j : parser->tokens.count;
}

// The context clang reads a use in a fixed enum's underlying type at `t` in (#147), whose `:` is at `colon`: its
// declaration's, once (a typedef name's use in the specifiers is once), at any scope. That is its first declarator's
// (check_input_tag_use_copies' scan past the enum's body), an object's or a member's, not the enum's own mark; or, in a
// declaration of the enum alone (`enum E : T { ... };`, `enum E : T;`), the enum's (check_input_tag_mark's: its
// declarations' up to this one, a definition's attributes after its body).
static uint8_t check_input_enum_base_context(const CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain, uint32_t t, uint32_t colon)
{
    uint8_t first = 0;
    bool bare = false;
    check_input_tag_use_copies(reader, marks, chain, t, 0, false, &first, &bare);
    const uint32_t keyword = bare ? check_input_enum_base_keyword(reader->parser, colon) : reader->parser->tokens.count;
    const uint8_t own = keyword < reader->parser->tokens.count ? check_input_availability_bit(check_input_tag_mark(reader, marks, chain, keyword).attribute) : 0;
    return bare ? own : first;
}

// Each token's availability context (#147, CheckInput.availability_contexts), for the uses of the unit's marked members
// (check_input_member_uses): the body of a function that a deprecated or unavailable attribute marks by a declaration up
// to its definition's, a declaration's declarator and initializer where one marks it there or before, and a marked
// declarator's parameter lists, as clang's ShouldDiagnoseAvailabilityInContext reads a use's context (its delayed
// diagnostics drop those in a deprecated declaration's declarator, parameters and initializer). Nested contexts give
// their tokens both bits.
static void check_input_availability_contexts(CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint8_t* contexts = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint8_t, (uint64_t)count + 1);
    reader->output->resource |= contexts == NULL;
    if (contexts != NULL)
    {
        memset(contexts, 0, (uint64_t)count + 1);
        uint32_t function = BUSTER_CHECK_NONE;
        uint32_t body_end = 0;
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            const bool inside = t < body_end;
            function = !inside && kind == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[t] == C_BINDING_FUNCTION ? t : function;
            if (!inside && function != BUSTER_CHECK_NONE && parser->tree.roles[t] == PARSE_ROLE_OPEN_BODY && parser->tree.parents[t] < count)
            {
                body_end = parser->tree.parents[t];
                // Its declarator's tokens before the body, a K&R definition's parameter declarations among them, as a
                // declarator's; then the body, whose tags have the function for their parent.
                const uint32_t key = check_input_deprecated_key(reader, function);
                check_input_availability_fill(reader, contexts, marks, chain, key, function, function + 1, t, false);
                check_input_availability_fill(reader, contexts, marks, chain, key, function, t, body_end + 1, true);
            }
            const uint32_t initialized = kind == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[t] != 0 ? check_sparse_get(reader->output->initializer_ends, t) : BUSTER_CHECK_NONE;
            if (initialized != BUSTER_CHECK_NONE)
            {
                check_input_availability_fill(reader, contexts, marks, chain, check_input_deprecated_key(reader, t), t, t, initialized, false);
            }
            if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM)
            {
                check_input_availability_tag(reader, contexts, marks, chain, t);
            }
            // A marked declarator's parameter lists and array bounds, at any scope, as clang reads their uses in the
            // declaration's context: those after its name, past its groups' `)` (a function pointer's, a function's that
            // returns one), nested lists within them.
            // A marked member's name is such a declarator too, by its own mark (CheckInputReader.member_marks).
            const uint32_t key = kind == PARSE_KIND_IDENTIFIER && check_input_declares(reader, t) ? check_input_deprecated_key(reader, t) : count;
            const uint8_t member_bit = kind == PARSE_KIND_IDENTIFIER && key == count && reader->member_marks != NULL ? reader->member_marks[t] : 0;
            bool suffix = (key < count && marks[key] != 0 && marks[key] - 1 <= t) || member_bit != 0;
            for (uint32_t at = t + 1; suffix && at < count;)
            {
                const uint8_t next = parser->tokens.kinds[at];
                const uint8_t next_role = parser->tree.roles[at];
                const uint32_t closer = parser->tree.parents[at];
                // A function declarator's parentheses and a call's are one role to the two parsers.
                const bool list = next == PARSE_KIND_L_PAREN && (next_role == PARSE_ROLE_OPEN_FUNCTION || next_role == PARSE_ROLE_OPEN_CALL) && closer < count &&
                                  closer > at;
                const bool bound = next == PARSE_KIND_L_SQUARE && closer < count && closer > at;
                if ((list || bound) && member_bit != 0)
                {
                    check_input_availability_fill_range(parser, contexts, member_bit, at, closer + 1, false);
                }
                else if (list || bound)
                {
                    check_input_availability_fill(reader, contexts, marks, chain, key, t, at, closer + 1, false);
                }
                suffix = list || bound || (next == PARSE_KIND_R_PAREN && next_role == PARSE_ROLE_CLOSE_GROUP);
                at = list || bound ? closer + 1 : at + 1;
            }
        }
        reader->output->availability_contexts = contexts;
    }
}

// The tag index of the struct, union or enum `type` is, through qualifiers and alignment, or 0.
static uint32_t check_input_tag_of(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_is_valid(types, type) ? type_unqualified(type_strip_aligned(types, type)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const bool tagged = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM;
    const uint32_t result = tagged ? types->types.index[type_index(bare)] : 0;
    return result;
}

// An anonymous tag's deprecated or unavailable mark (#147): its keyword's attributes before `bound`, those after its
// body among them, as check_input_tag_mark reads them.
static const CheckInputAttribute* check_input_anonymous_mark(const CheckInputReader* reader, uint32_t keyword, uint32_t bound)
{
    const CheckInputAttribute* result = check_input_deprecated_record_before(check_input_attributes_at(reader->output, keyword), bound);
    return result;
}

// The overloadable functions' declarations a deprecated attribute marks (#147, CheckInput.marked_overloads): each
// candidate's by its declarations up to its own, as a redeclaration inherits its attributes, for the calls that select it
// (check_input_overload_uses). An unavailable one is the selection's error already (BUSTER_CHECK_OVERLOAD_UNAVAILABLE).
static void check_input_overload_marks(CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t candidates = input->graph.overload_candidate_count;
    uint32_t marked = 0;
    for (uint32_t pass = 0; pass < 2 && !input->resource; pass += 1)
    {
        // The first pass counts, the second records into the array the count sizes, in the candidates' token order.
        if (pass == 1 && marked != 0)
        {
            input->marked_overloads = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputMarkedMember, marked);
            input->resource |= input->marked_overloads == NULL;
        }
        uint32_t recorded = 0;
        uint32_t last = 0;
        for (uint32_t i = 0; i < candidates && (pass == 0 || input->marked_overloads != NULL) && !input->resource; i += 1)
        {
            const uint32_t declaration = input->graph.overload_candidates[i].declaration;
            const CheckInputAttribute* mark = declaration < count ? check_input_availability_mark(reader, marks, chain, check_input_deprecated_key(reader, declaration),
                                                                                                   declaration, UINT32_MAX) : NULL;
            const bool deprecated = mark != NULL && mark->kind == CHECK_ATTRIBUTE_DEPRECATED && (recorded == 0 || declaration > last);
            const bool message = deprecated && mark->argument_first < mark->argument_end && parser->tokens.kinds[mark->argument_first] == PARSE_KIND_STRING;
            if (deprecated && pass == 1 && recorded < marked)
            {
                input->marked_overloads[recorded] = (CheckInputMarkedMember)
                {
                    .member = declaration,
                    .name = declaration,
                    .attribute = mark->first,
                    .message = message ? mark->argument_first : BUSTER_CHECK_WARNING_NO_SOURCE,
                };
            }
            recorded += deprecated;
            last = deprecated ? declaration : last;
        }
        marked = pass == 0 ? recorded : marked;
        input->marked_overload_count = pass == 1 && input->marked_overloads != NULL ? recorded : 0;
    }
}

// clang's diagnostic of a use of a marked member at its name `name` (#147, DiagnoseUseOfDecl's of a member expression):
// "'x' is deprecated" (-Wdeprecated-declarations), with the attribute's message after a colon and a note at the
// attribute, or the error "'x' is unavailable" (CHECK_INPUT_UNAVAILABLE_USE), with its note at the member's declaration,
// unless the use's context drops it (CheckInput.availability_contexts, and `specified`, a marked declaration's whose
// specifiers it is in).
static void check_input_marked_member_use(CheckInputReader* reader, uint32_t name, const CheckInputMarkedMember* marked, uint8_t specified)
{
    const Parser* parser = reader->parser;
    const uint8_t context = (reader->output->availability_contexts != NULL ? reader->output->availability_contexts[name] : 0) | specified;
    const bool dropped = check_marked_dropped(marked, context);
    if (!dropped && marked->unavailable)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, name, CHECK_INPUT_UNAVAILABLE_USE);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].note_token = marked->name;
            reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_UNAVAILABLE_HERE;
            reader->output->reports[before].detail = marked->message != BUSTER_CHECK_WARNING_NO_SOURCE ? marked->message + 1 : 0;
        }
    }
    else if (!dropped)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
            .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
            .token = name,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = name + 1,
            .payload = { .name = parser->tokens.names[name], .token = marked->message },
        };
        const CheckWarningNote note =
        {
            .kind = CHECK_WARNING_NOTE_DEPRECATED_HERE,
            .token = marked->attribute,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .payload = { .name = parser->tokens.names[marked->name], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
        };
        check_input_warning_event(reader, &event, &note, 1);
    }
}

// The marked members' accesses only the reader's evaluator reads (#147, CheckInputReader.evaluated_members), by their
// names' tokens: each one's member's index plus one, once, where the graph has no access for check_input_member_uses
// (`typeof`'s operand, a file-scope array bound). Null for none; check_input_deprecated_uses diagnoses them in turn.
static uint32_t* check_input_evaluated_members(CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    const uint32_t count = reader->parser->tokens.count;
    uint32_t* result = reader->evaluated_member_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1) : NULL;
    reader->output->resource |= reader->evaluated_member_count != 0 && result == NULL;
    if (result != NULL)
    {
        memset(result, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < reader->evaluated_member_count; i += 1)
        {
            const uint32_t name = reader->evaluated_members[i * 2];
            result[name < count ? name : count] = reader->evaluated_members[i * 2 + 1] + 1;
        }
        for (uint32_t node = 0; node < graph->count; node += 1)
        {
            const uint32_t name = graph->tokens[node] + 1;
            if ((graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_ARROW) && name < count)
            {
                result[name] = 0;
            }
        }
        result[count] = 0;
    }
    return result;
}

// clang's diagnostics of a use of what a deprecated or unavailable attribute marks (#147, DiagnoseUseOfDecl's): at each
// use of a function, a variable, a typedef, an enumerator or a tag after the declaration that marks it, "'X' is
// deprecated" (-Wdeprecated-declarations), or the error "'X' is unavailable", with the attribute's message after a colon
// and a note at the attribute. None in a context that drops it (a deprecated or unavailable function's body or
// declaration's initializer: check_input_availability_contexts, which it makes for check_input_member_uses too), nor in
// a bare declaration of a tag. A member's use (`m.x`) is check_input_member_uses', after checking: its declaration is
// found by the expression's type, which this pass doesn't have.
// The uses are the tokens the bindings resolved to another declaration than their own (CheckInput.declarations); the
// marks are found once, by the declaration a use counts by (check_input_deprecated_key), each key's marked declarations
// in source order. A use reads those up to the declaration its lookup finds, which inherits from the ones before it,
// as clang's redeclarations inherit their attributes: not a block's later one, which it doesn't see. A tag's use, which
// finds its first declaration, reads those before it.
static void check_input_deprecated_uses(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    // marks: a key's first marked declaration plus one; chain: a marked declaration's next of its key plus one; latest:
    // a key's last marked declaration plus one while the marks are found, then a tag key's latest declaration so far.
    uint32_t* marks = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* chain = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    uint32_t* latest = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    // Each marked member's name's mark, once (check_input_declarator_mark's lookup, CheckInputReader.member_marks).
    uint8_t* member_marks = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, (uint64_t)count + 1);
    // The levels and parameter lists open at each body the uses are in (a struct's or union's, a function's, a block's),
    // innermost last, and whether it is a record's: a declaration's own level is its scope's body's.
    int32_t* scope_levels = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, int32_t, (uint64_t)count + 1);
    int32_t* scope_lists = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, int32_t, (uint64_t)count + 1);
    bool* scope_records = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, (uint64_t)count + 1);
    // Each tag's first declaration plus one: its name's, or an anonymous one's keyword (a typedef's use reads its tag's
    // marks by it).
    const uint32_t tag_count = reader->types->tags.count;
    uint32_t* tag_keys = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)tag_count + 1);
    const bool allocated = marks != NULL && chain != NULL && latest != NULL && member_marks != NULL && scope_levels != NULL && scope_lists != NULL && scope_records != NULL &&
                           tag_keys != NULL;
    reader->output->resource |= !allocated;
    if (allocated)
    {
        memset(tag_keys, 0, ((uint64_t)tag_count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t tag = reader->declaration_kinds[i] == C_BINDING_TAG_KIND ? check_input_tag_of(reader->types, reader->declared_types[i]) : 0;
            if (tag != 0 && tag < tag_count && tag_keys[tag] == 0)
            {
                tag_keys[tag] = i + 1;
            }
        }
        memset(marks, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(chain, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(latest, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(member_marks, 0, (uint64_t)count + 1);
        for (uint32_t i = 0; i < reader->output->marked_member_count; i += 1)
        {
            const CheckInputMarkedMember* marked = reader->output->marked_members + i;
            const uint8_t bit = marked->unavailable ? BUSTER_CHECK_CONTEXT_UNAVAILABLE : BUSTER_CHECK_CONTEXT_DEPRECATED;
            member_marks[marked->name < count ? marked->name : count] = bit;
        }
        reader->member_marks = member_marks;
        const bool scalar = parser->options.scalar;
        const CheckSparseWide all = reader->output->attributes;
        // In source order, so that each key's chain runs first to last.
        for (uint32_t first = check_input_attributed_next(all, 0, count, scalar); first < count; first = check_input_attributed_next(all, first + 16, count, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, count, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t key = check_input_deprecated_record(check_input_attributes_at(reader->output, d)) != NULL ? check_input_deprecated_key(reader, d) : count;
                if (key < count)
                {
                    marks[key] = marks[key] == 0 ? d + 1 : marks[key];
                    if (latest[key] != 0)
                    {
                        chain[latest[key] - 1] = d + 1;
                    }
                    latest[key] = d + 1;
                }
            }
        }
        memset(latest, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        check_input_overload_marks(reader, marks, chain);
        // Each token's context (check_input_availability_contexts): a deprecated or unavailable function's body or
        // declaration's initializer, which drops its uses' diagnostics as clang's ShouldDiagnoseAvailabilityInContext does.
        check_input_availability_contexts(reader, marks, chain);
        const uint8_t* contexts = reader->output->availability_contexts;
        const uint32_t* evaluated = check_input_evaluated_members(reader);
        // Where the function body a use is in ends.
        uint32_t body_end = 0;
        // The brackets open before a use (`(`, `[` and `{` less their closers): a file-scope declaration's own level is 0.
        // And of them, the parameter lists (a function declarator's parentheses, a call's to the vector parser).
        int32_t level = 0;
        int32_t lists = 0;
        uint32_t scopes = 0;
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            const uint8_t role = parser->tree.roles[t];
            const bool inside = t < body_end;
            const uint8_t previous = t != 0 ? parser->tokens.kinds[t - 1] : PARSE_KIND_END;
            const uint8_t previous_role = t != 0 ? parser->tree.roles[t - 1] : PARSE_ROLE_NONE;
            level += (previous == PARSE_KIND_L_PAREN || previous == PARSE_KIND_L_SQUARE || previous == PARSE_KIND_L_BRACE) -
                     (previous == PARSE_KIND_R_PAREN || previous == PARSE_KIND_R_SQUARE || previous == PARSE_KIND_R_BRACE);
            lists += (previous_role == PARSE_ROLE_OPEN_FUNCTION || previous_role == PARSE_ROLE_OPEN_CALL) -
                     (previous_role == PARSE_ROLE_CLOSE_FUNCTION || previous_role == PARSE_ROLE_CLOSE_CALL);
            const bool scope_open = previous == PARSE_KIND_L_BRACE &&
                                    (previous_role == PARSE_ROLE_OPEN_STRUCT || previous_role == PARSE_ROLE_OPEN_BODY || previous_role == PARSE_ROLE_OPEN_BLOCK);
            scopes -= previous == PARSE_KIND_R_BRACE && scopes != 0 &&
                      (previous_role == PARSE_ROLE_CLOSE_STRUCT || previous_role == PARSE_ROLE_CLOSE_BODY || previous_role == PARSE_ROLE_CLOSE_BLOCK);
            if (scope_open)
            {
                scope_levels[scopes] = level;
                scope_lists[scopes] = lists;
                scope_records[scopes] = previous_role == PARSE_ROLE_OPEN_STRUCT;
                scopes += 1;
            }
            if (!inside && role == PARSE_ROLE_OPEN_BODY && parser->tree.parents[t] < count)
            {
                body_end = parser->tree.parents[t];
            }
            const uint32_t declared = kind == PARSE_KIND_IDENTIFIER ? reader->output->declarations[t] : BUSTER_CHECK_NONE;
            // A tag's name's body, past its attributes and an enum's base: a definition's (the parser makes the name a
            // leaf before its `{`, or keeps it a tag before an enum's base), whose name is a use of the tag where
            // declarators follow its body (`struct S { ... } x;`), as clang diagnoses the specifiers' type for them, and
            // none alone; or its `;`, a declaration of it (`struct S;`, `enum E : int;`).
            const bool leaf = kind == PARSE_KIND_IDENTIFIER && (role == PARSE_ROLE_TAG || role == PARSE_ROLE_LEAF_RIGHT);
            const uint32_t body_at = leaf ? check_input_name_body(parser, t, count) : count;
            const uint8_t body_role = body_at < count ? parser->tree.roles[body_at] : PARSE_ROLE_NONE;
            const uint32_t body_closer = body_at < count ? parser->tree.parents[body_at] : count;
            const bool defines = body_at < count && parser->tokens.kinds[body_at] == PARSE_KIND_L_BRACE &&
                                 (body_role == PARSE_ROLE_OPEN_STRUCT || body_role == PARSE_ROLE_OPEN_ENUM) && body_closer < count && body_closer > body_at;
            // A leaf is a tag's name only before its body.
            const bool named = leaf && (role == PARSE_ROLE_TAG || defines);
            const bool bare = named && body_at < count && parser->tokens.kinds[body_at] == PARSE_KIND_SEMI;
            const uint32_t defined_end = defines ? check_input_skip_attributes(parser, body_closer + 1, count) : count;
            const bool declares = defined_end < count && parser->tokens.kinds[defined_end] != PARSE_KIND_SEMI;
            const bool tag = role == PARSE_ROLE_TAG || declares;
            const uint32_t tag_declared = declared < count ? declared : t;
            // A tag's declaration or definition, where its unavailable note goes for the uses after it.
            const uint32_t tag_key = named ? tag_declared : count;
            if (tag_key < count && (bare || defines || tag_key == t))
            {
                latest[tag_key] = t + 1;
            }
            // An overloadable function's name is its overload set's, whose resolution clang checks otherwise.
            const bool overloaded = reader->output->overload_heads != NULL && reader->output->overload_heads[t] != 0;
            const bool use = defines ? declares && !overloaded :
                             declared < count && declared != t && role != PARSE_ROLE_MEMBER && !overloaded &&
                             (tag ? !bare : reader->declaration_kinds[t] == 0);
            uint32_t key = use ? (tag ? tag_declared : check_input_deprecated_key(reader, t)) : count;
            // An enumerator of a deprecated enum is deprecated by its enum (the note names the enum).
            const bool by_enum = use && key < count && marks[key] == 0 && declared < count && reader->declaration_kinds[declared] == C_BINDING_ENUMERATOR;
            if (by_enum)
            {
                key = check_input_deprecated_enum(reader, declared);
            }
            // The declarations a use's marks are by: those up to what its lookup finds, or a tag's (and attributes) before it.
            bool by_tag = tag || by_enum;
            uint32_t limit = by_tag ? t - 1 : declared;
            const uint32_t marking = key < count ? marks[key] : 0;
            bool marked = marking != 0 && marking - 1 <= limit && marking - 1 < t;
            // A typedef its own marks don't mark takes the marks of the struct, union or enum it names, as clang's
            // ShouldDiagnoseAvailabilityOfDecl looks through an available typedef (and the typedefs it names) to its tag
            // type: a named tag's by its first declaration, as a tag's use reads them, before the use; an anonymous
            // one's by its keyword's (check_input_anonymous_mark), whose note names its type (`T` for its typedef).
            const uint32_t typedef_tag = use && !tag && !marked && !by_enum && check_input_typedef_declared(reader, declared) ?
                                         check_input_tag_of(reader->types, reader->declared_types[declared]) : 0;
            const uint32_t typedef_first = typedef_tag != 0 && typedef_tag < tag_count && tag_keys[typedef_tag] != 0 ? tag_keys[typedef_tag] - 1 : count;
            const bool typedef_anonymous = typedef_first < count && parser->tokens.kinds[typedef_first] != PARSE_KIND_IDENTIFIER;
            // The tag type's declaration is its definition where one is before the use (which inherits its earlier
            // declarations' marks), else its first declaration, by its own marks alone (clang's TagType::getDecl).
            const uint32_t typedef_completion = typedef_first < count && reader->tag_completions != NULL ? reader->tag_completions[typedef_tag] : BUSTER_CHECK_NONE;
            const bool typedef_defined = typedef_completion != BUSTER_CHECK_NONE && typedef_completion != 0 && typedef_completion <= t;
            // A tag's attributes gather at its first declaration's name, each read before its bound
            // (check_input_deprecated_record_before): the first declaration's own are those before its name's end.
            uint32_t tag_bound = t;
            if (typedef_first < count && !typedef_anonymous && marks[typedef_first] != 0 && marks[typedef_first] - 1 < t)
            {
                key = typedef_first;
                by_tag = true;
                limit = t - 1;
                marked = true;
                tag_bound = typedef_defined ? t : typedef_first + 1;
            }
            // An anonymous struct's, union's or enum's definition with declarators (or in a type name), at its keyword: a
            // use of its type, as a named one's name is, by its keyword's marks, those after its body among them; and an
            // anonymous member's, once (its record is "(anonymous at ...)", check_driver_spelling_build).
            const uint32_t anonymous_body = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ? check_input_skip_attributes(parser, t + 1, count) : count;
            const uint8_t anonymous_role = anonymous_body < count ? parser->tree.roles[anonymous_body] : PARSE_ROLE_NONE;
            const uint32_t anonymous_closer = anonymous_body < count ? parser->tree.parents[anonymous_body] : count;
            const bool anonymous_defines = anonymous_body < count && parser->tokens.kinds[anonymous_body] == PARSE_KIND_L_BRACE &&
                                           (anonymous_role == PARSE_ROLE_OPEN_STRUCT || anonymous_role == PARSE_ROLE_OPEN_ENUM) && anonymous_closer < count &&
                                           anonymous_closer > anonymous_body;
            const uint32_t anonymous_end = anonymous_defines ? check_input_skip_attributes(parser, anonymous_closer + 1, count) : count;
            const bool anonymous_member = anonymous_end < count && parser->tokens.kinds[anonymous_end] == PARSE_KIND_SEMI && scopes != 0 &&
                                          scope_records[scopes - 1] && level == scope_levels[scopes - 1];
            const bool anonymous_use = anonymous_end < count && (parser->tokens.kinds[anonymous_end] != PARSE_KIND_SEMI || anonymous_member);
            const CheckInputAttribute* anonymous = anonymous_use ? check_input_anonymous_mark(reader, t, anonymous_end) :
                                                   typedef_anonymous ? check_input_anonymous_mark(reader, typedef_first, t) : NULL;
            marked |= anonymous != NULL;
            // A use in the specifiers of a declaration itself marked, which clang's delayed diagnostics read in that
            // declaration's context: one before its first declarator, which an attribute marks by its own token. Its worst
            // mark is a context as check_input_availability_contexts' are: a deprecated one drops a deprecated use, an
            // unavailable one an unavailable use too. Only from the specifiers: a `,` or `=` at the declaration's own
            // level, outside every group (an attribute list's, `typeof`'s, a declarator's), ends an initializer or an
            // earlier declarator, whose uses a later declarator's mark doesn't reach. And only the declaration's own
            // declarator, outside every parameter list: a parameter's mark is no context of its own specifiers, and a use
            // in a parameter list takes its function's (check_input_availability_contexts). Looked for only at a marked
            // use, which are few.
            // A use in a fixed enum's underlying type takes its declaration's first declarator's context, at any scope
            // (check_input_enum_base_context), not the specifiers' scan's.
            const uint32_t base_colon = marked && !tag ? check_input_enum_base_colon(parser, t) : count;
            const bool based = base_colon < count;
            // In a body (a record's, a function's, a block's), from its declaration's own level.
            // A marked member's access the evaluator alone reads (check_input_evaluated_members) takes the specifiers' context too.
            const uint32_t evaluated_member = evaluated != NULL ? evaluated[t] : 0;
            uint32_t declarator = (marked || evaluated_member != 0) && !tag && !anonymous_use && !based ? t + 1 : count;
            int32_t depth = scopes != 0 ? level - scope_levels[scopes - 1] : level;
            int32_t open_lists = scopes != 0 ? lists - scope_lists[scopes - 1] : lists;
            // The braces the scan opened: a `}` beyond them closes one open at the use, and a tag's body there (an
            // enumerator's value, a member's bound) isn't the declarator's context; a compound literal's is.
            int32_t braces = 0;
            bool specifiers = true;
            while (specifiers && declarator < count && !check_input_declares(reader, declarator) && member_marks[declarator] == 0 &&
                   parser->tokens.kinds[declarator] != PARSE_KIND_SEMI)
            {
                const uint8_t scanned = parser->tokens.kinds[declarator];
                const uint8_t scanned_role = parser->tree.roles[declarator];
                // A `{` at the declaration's own level is a body or an initializer's; within a group, a compound
                // literal's (`typeof((T){0})`), which the specifiers hold.
                const bool body = scanned == PARSE_KIND_L_BRACE && depth <= 0;
                const bool tag_body = scanned == PARSE_KIND_R_BRACE && braces == 0 && (scanned_role == PARSE_ROLE_CLOSE_STRUCT || scanned_role == PARSE_ROLE_CLOSE_ENUM);
                braces += (scanned == PARSE_KIND_L_BRACE) - (scanned == PARSE_KIND_R_BRACE && braces != 0);
                depth += scanned == PARSE_KIND_L_PAREN || scanned == PARSE_KIND_L_SQUARE || scanned == PARSE_KIND_L_BRACE;
                depth -= scanned == PARSE_KIND_R_PAREN || scanned == PARSE_KIND_R_SQUARE || scanned == PARSE_KIND_R_BRACE;
                open_lists += (scanned_role == PARSE_ROLE_OPEN_FUNCTION || scanned_role == PARSE_ROLE_OPEN_CALL) -
                              (scanned_role == PARSE_ROLE_CLOSE_FUNCTION || scanned_role == PARSE_ROLE_CLOSE_CALL);
                specifiers = !body && !tag_body && (depth > 0 || (scanned != PARSE_KIND_COMMA && scanned != PARSE_KIND_EQUAL));
                declarator += specifiers;
            }
            const uint32_t declarator_key = specifiers && open_lists == 0 && declarator < count && check_input_declares(reader, declarator) ?
                                            check_input_deprecated_key(reader, declarator) : count;
            // In a function's body, only a typedef's mark is its specifiers' context, or a member's: a block's object's
            // isn't, whose specifiers clang diagnoses as it reads them.
            const bool block_object = inside && !(scopes != 0 && scope_records[scopes - 1]) && declarator < count && !check_input_typedef_declared(reader, declarator);
            const CheckInputAttribute* enclosing = declarator_key < count && !block_object ?
                                                   check_input_availability_mark(reader, marks, chain, declarator_key, declarator, UINT32_MAX) : NULL;
            // A marked member's own mark, as an object's (check_input_declarator_mark).
            const uint8_t member_specified = specifiers && open_lists == 0 && declarator < count && declarator_key == count ? member_marks[declarator] : 0;
            const uint8_t specified = based ? check_input_enum_base_context(reader, marks, chain, t, base_colon) : member_specified != 0 ? member_specified :
                                      enclosing == NULL ? 0 :
                                      enclosing->kind == CHECK_ATTRIBUTE_UNAVAILABLE ? BUSTER_CHECK_CONTEXT_UNAVAILABLE : BUSTER_CHECK_CONTEXT_DEPRECATED;
            // A definition's name reads the attributes after its body too, which apply to the type its declarators take.
            const CheckInputAttribute* candidate = anonymous != NULL ? anonymous :
                                                   marked ? check_input_availability_mark(reader, marks, chain, key, limit, declares ? defined_end : by_tag ? tag_bound : UINT32_MAX) :
                                                   NULL;
            // A deprecated context drops a deprecated use's warning, an unavailable one an unavailable use's error too.
            const uint8_t context = (contexts != NULL ? contexts[t] : 0) | specified;
            const CheckInputMarkedMember* evaluated_mark = evaluated_member != 0 ?
                                                           check_marked_member(reader->output->marked_members, reader->output->marked_member_count, evaluated_member - 1) : NULL;
            if (evaluated_mark != NULL)
            {
                check_input_marked_member_use(reader, t, evaluated_mark, specified);
            }
            const bool unavailable = candidate != NULL && candidate->kind == CHECK_ATTRIBUTE_UNAVAILABLE;
            // A tag's once for each declarator (check_input_tag_use_copies), anything else's once.
            uint8_t first_declarator = 0;
            bool no_declarator = false;
            const uint32_t copies = candidate == NULL ? 0 :
                                    anonymous_member ? !check_input_availability_dropped(contexts != NULL ? contexts[t] : 0, unavailable) :
                                    tag || anonymous_use ? check_input_tag_use_copies(reader, marks, chain, t, contexts != NULL ? contexts[t] : 0, unavailable,
                                                                                      &first_declarator, &no_declarator) :
                                    !check_input_availability_dropped(context, unavailable);
            const CheckInputAttribute* attribute = copies != 0 ? candidate : NULL;
            // The attribute's message, its first argument's string.
            const uint32_t message = attribute != NULL && attribute->argument_first < attribute->argument_end &&
                                     parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_STRING ? attribute->argument_first : BUSTER_CHECK_WARNING_NO_SOURCE;
            if (attribute != NULL && attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE)
            {
                // Its note is at the declaration, not the attribute: at the name of the one the use's lookup finds, or a
                // tag's latest declaration, which an enumerator's note names; an anonymous tag's keyword, whose type the
                // report names.
                const uint32_t noted = anonymous != NULL ? (anonymous_use ? t : typedef_first) :
                                       by_tag ? (key < count && latest[key] != 0 ? latest[key] - 1 : key) : declared;
                const uint32_t before = reader->output->report_count;
                check_input_report(reader, t, CHECK_INPUT_UNAVAILABLE_USE);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].note_token = noted;
                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_UNAVAILABLE_HERE;
                    reader->output->reports[before].detail = message != BUSTER_CHECK_WARNING_NO_SOURCE ? message + 1 : 0;
                    // A token's reports print its first alone: the copies after it ride on it.
                    reader->output->reports[before].value = copies - 1;
                }
            }
            else if (attribute != NULL)
            {
                CheckWarningEvent event =
                {
                    .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
                    .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
                    .token = t,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                    .completion = t + 1,
                    .payload = { .name = parser->tokens.names[t], .token = message },
                };
                // An anonymous tag's names are its type's spelling (BUSTER_CHECK_DEPRECATED_TYPE_NAME): its use's at its
                // keyword, and its note's, a typedef's use naming the typedef.
                const TypeId anonymous_type = anonymous != NULL ? reader->declared_types[anonymous_use ? t : typedef_first] : 0;
                if (anonymous_use)
                {
                    event.payload.first_type = anonymous_type;
                    event.payload.data = BUSTER_CHECK_DEPRECATED_TYPE_NAME;
                }
                CheckWarningNote note =
                {
                    .kind = CHECK_WARNING_NOTE_DEPRECATED_HERE,
                    .token = attribute->first,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .name = parser->tokens.names[key < count ? key : t], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                if (anonymous != NULL)
                {
                    note.payload.first_type = anonymous_type;
                    note.payload.data = BUSTER_CHECK_DEPRECATED_TYPE_NAME;
                }
                for (uint32_t copy = 0; copy < copies; copy += 1)
                {
                    check_input_warning_event(reader, &event, &note, 1);
                }
            }
        }
    }
    reader->member_marks = NULL;
    arena_pop_to(reader->scratch, mark);
}

