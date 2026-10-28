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
    CHECK_ATTRIBUTE_EXPORT_NAME,
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
    // A keyword's record (_Alignas, asm, a calling convention, _Noreturn, a nullability), not an attribute list's item.
    bool keyword;
};

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
    { "export_name", CHECK_ATTRIBUTE_EXPORT_NAME, CHECK_ATTRIBUTE_STRING, 1, 1 },
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
    { "min_vector_width", CHECK_ATTRIBUTE_MIN_VECTOR_WIDTH, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
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
        const CheckInputAttribute attribute = suffix->records[i];
        if (attribute.kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute.value != CHECK_CALL_IGNORED_X86_32)
        {
            valid = output->calling_convention == CHECK_CALL_DEFAULT || output->calling_convention == attribute.value;
            if (!valid)
            {
                check_input_attribute_error(reader, output, attribute.first, attribute.end, CHECK_ATTRIBUTE_ERROR_CONFLICT);
            }
        }
        valid = valid && check_input_attribute_append(reader, output, attribute);
    }
    if (valid)
    {
        output->packed |= suffix->packed;
        output->has_alignas |= suffix->has_alignas;
        output->alignment_log2 = suffix->alignment_log2 > output->alignment_log2 ? suffix->alignment_log2 : output->alignment_log2;
        output->required_alignment_log2 = suffix->required_alignment_log2 > output->required_alignment_log2 ? suffix->required_alignment_log2 : output->required_alignment_log2;
        output->calling_convention = suffix->calling_convention != CHECK_CALL_DEFAULT ? suffix->calling_convention : output->calling_convention;
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
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
        {
            valid = value > 0 && (uint64_t)value <= BUSTER_TYPE_MAX_VECTOR_BITS;
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
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
            valid = value >= 0 && value <= UINT32_MAX;
            if (valid && (attribute->kind == CHECK_ATTRIBUTE_NONNULL || attribute->kind == CHECK_ATTRIBUTE_ALLOC_SIZE))
            {
                valid = value > 0;
            }
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
    valid &= count >= minimum && (maximum == 255 || count <= maximum);
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
        const bool identifier = attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER || (attribute->schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && argument == 0);
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
    if (valid && attribute->kind == CHECK_ATTRIBUTE_EXPORT_NAME)
    {
        // export_name selects a WebAssembly export. The supported x86-64 output has no such namespace; retain
        // the decoded name and its source range, with the same deliberate no-effect disposition as native Clang.
        attribute->ignored = true;
    }
    if (!valid)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : error);
    }
    return valid;
}

static bool check_input_attribute_read_item(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t* cursor, uint32_t end, bool standard)
{
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    CheckInputAttribute attribute = { .first = i, .name_token = i, .standard = standard };
    bool valid = i < end && parser->tokens.names[i] != 0;
    bool namespace_known = true;
    bool scoped = false;
    if (valid && standard && i + 2 < end && parser->tokens.kinds[i + 1] == PARSE_KIND_COLON && parser->tokens.kinds[i + 2] == PARSE_KIND_COLON)
    {
        const String8 scope = check_input_attribute_name(reader, parser->tokens.names[i]);
        namespace_known = check_input_attribute_equal(scope, "gnu") || check_input_attribute_equal(scope, "clang");
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
        if (valid && rule != 0)
        {
            attribute.kind = rule->kind;
            attribute.schema = rule->schema;
            reader->cleanup_attributes |= rule->kind == CHECK_ATTRIBUTE_CLEANUP;
            valid = check_input_attribute_validate(reader, attributes, &attribute, rule->minimum, rule->maximum);
        }
        else if (valid)
        {
            check_input_attribute_error(reader, attributes, attribute.name_token, attribute.end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
        if (valid)
        {
            const bool numeric = attribute.schema == CHECK_ATTRIBUTE_INTEGER || attribute.schema == CHECK_ATTRIBUTE_INTEGER_LIST ||
                                 attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
            attribute.resolved = !numeric || attribute.argument_count == 0 || (attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && attribute.argument_count == 1);
            if (attribute.kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
            {
                const CheckInputCallingConvention call = check_input_attribute_call(name);
                valid = call == CHECK_CALL_IGNORED_X86_32 || attributes->calling_convention == CHECK_CALL_DEFAULT || attributes->calling_convention == call;
                if (valid && call != CHECK_CALL_IGNORED_X86_32)
                {
                    attributes->calling_convention = call;
                }
                attribute.value = call;
                if (!valid)
                {
                    check_input_attribute_error(reader, attributes, attribute.first, attribute.end, CHECK_ATTRIBUTE_ERROR_CONFLICT);
                }
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
        if (kind == PARSE_KIND_ATTRIBUTE || standard)
        {
            const uint32_t open = standard ? i : i + 1;
            valid = open + 1 < end && parser->tokens.kinds[open] == (standard ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_PAREN);
            const uint32_t close = valid ? parser->tree.parents[open] : end;
            valid &= close > open + 1 && close < end && parser->tokens.kinds[open + 1] == (standard ? PARSE_KIND_L_SQUARE : PARSE_KIND_L_PAREN);
            const uint32_t list_end = valid ? parser->tree.parents[open + 1] : end;
            valid &= list_end + 1 == close;
            i = open + 2;
            while (valid && i < list_end)
            {
                if (parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                {
                    i += 1;
                }
                else
                {
                    valid = check_input_attribute_read_item(reader, attributes, &i, list_end, standard);
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
                valid = call == CHECK_CALL_IGNORED_X86_32 || attributes->calling_convention == CHECK_CALL_DEFAULT || attributes->calling_convention == call;
                if (valid && call != CHECK_CALL_IGNORED_X86_32)
                {
                    attributes->calling_convention = call;
                }
                if (!valid)
                {
                    check_input_attribute_error(reader, attributes, first, i, CHECK_ATTRIBUTE_ERROR_CONFLICT);
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

// Mode/vector attributes change the scalar element, while a typedef's alignment wraps the resulting complete type.
// An object's/member's alignment is declaration metadata: applying ALIGNED to its type would incorrectly alter
// pointer/array compatibility. The caller feeds the alignment to the layout/storage consumer instead.
static TypeId check_input_attribute_type(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId base, bool is_typedef)
{
    TypeId result = base;
    bool valid = base != 0 && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE;
    for (uint32_t i = 0; valid && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        CheckInputAttributeError error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
        if (attribute->kind == CHECK_ATTRIBUTE_MODE)
        {
            result = check_input_attribute_mode(reader, attribute, result);
            valid = result != 0;
            error = CHECK_ATTRIBUTE_ERROR_MODE;
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
    if (valid && attributes->required_alignment_log2 != 0)
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
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_CONFLICT);
                }
            }
        }
    }
    return result;
}

// Calling conventions attach to the first function inside a completed declarator, through its pointer, array and
// aligned-typedef wrappers. Preserve all those wrappers and their qualifiers; attributes on a scalar subject are
// warning-only ignored metadata. A prefix caller waits for the full declarator before invoking this helper.
static TypeId check_input_attribute_abi(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    TypeId result = type;
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
        for (uint32_t i = 0; i < attributes->count; i += 1)
        {
            CheckInputAttribute* attribute = attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
            {
                attribute->ignored |= !is_function;
                if (result == 0)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end,
                                                reader->output->resource ? CHECK_ATTRIBUTE_ERROR_RESOURCE : CHECK_ATTRIBUTE_ERROR_CONFLICT);
                }
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return attributes == 0 || attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? result : 0;
}

// clang's no_builtin is permitted on a function's definition alone: on another function's declaration it is an error at
// its name, and on anything else clang warns that it only applies to functions and ignores it. Its arguments are kept as
// written, as an argument that names no library function is, which clang warns about and ignores: no code here makes a
// call of a library function that the source doesn't. Whether the declaration's attributes are valid.
static bool check_input_attribute_definition(CheckInputReader* reader, CheckInputAttributes* attributes, bool function, bool definition)
{
    bool valid = true;
    for (uint32_t i = 0; valid && attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        if (attribute->kind == CHECK_ATTRIBUTE_NO_BUILTIN)
        {
            attribute->ignored |= !function;
            valid = !function || definition;
            if (!valid)
            {
                check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_SUBJECT);
            }
        }
    }
    return valid;
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
    for (uint32_t i = 0; valid && is_function && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
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
            uint32_t failed = begin;
            for (uint32_t argument = begin; valid && argument < end; argument += 1)
            {
                const bool scalar = attribute->schema == CHECK_ATTRIBUTE_INTEGER;
                valid = scalar ? attribute->resolved : attribute->argument_values != 0;
                const int64_t index = valid ? (scalar ? attribute->value : attribute->argument_values[argument]) : 0;
                const bool ellipsis_argument = nonnull && variadic && index > count;
                valid &= index > 0 && (index <= count || ellipsis_argument);
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
                any_pointer |= kind == BUSTER_TYPE_KIND_POINTER || ellipsis_argument;
                failed = valid ? failed : argument;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT)
            {
                const int64_t first_checked = attribute->argument_values[2];
                const int64_t format_index = attribute->argument_values[1];
                valid = first_checked == 0 || (first_checked > format_index && first_checked <= (int64_t)count + 1);
                failed = 2;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_FORMAT_ARG)
            {
                valid = check_input_attribute_string_type(reader->types, result);
            }
            if (valid && nonnull && attribute->argument_count != 0 && !any_pointer)
            {
                attribute->ignored = true;
            }
            if (!valid)
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
