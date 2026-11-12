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
    CHECK_ATTRIBUTE_MS_STRUCT,
    CHECK_ATTRIBUTE_GCC_STRUCT,
    // #1361: conservative object-size compatibility, without hidden ABI arguments.
    CHECK_ATTRIBUTE_PASS_OBJECT_SIZE,
    CHECK_ATTRIBUTE_PASS_DYNAMIC_OBJECT_SIZE,
    // The Swift parameter attributes (#1447), a parameter's ABI in its function's type: its value a TypeParameterABI.
    CHECK_ATTRIBUTE_PARAMETER_ABI,
    CHECK_ATTRIBUTE_DIAGNOSE_IF,
    CHECK_ATTRIBUTE_ENABLE_IF,
    CHECK_ATTRIBUTE_SELECTANY,
    CHECK_ATTRIBUTE_NODEREF,
    // x86's interrupt (#147): an interrupt service routine, clang's handleAnyX86InterruptAttr's checks
    // (check_input_interrupt_handler), with its implicit used; its arguments are expressions it never counts.
    CHECK_ATTRIBUTE_INTERRUPT,
    // x86's force_align_arg_pointer (#1447): a function whose prologue realigns the stack, which an interrupt handler or an
    // old caller may leave misaligned (Tier0Function.realign_stack); clang's handler ignores it, without a word, on a
    // function pointer or a typedef of a function's type or of a pointer to one.
    CHECK_ATTRIBUTE_FORCE_ALIGN_ARG_POINTER,
    // GCC's and clang's patchable_function_entry(N[, M[, "section"]]) (#1447): N bytes of NOPs at a function's entry, M of
    // them before its symbol, and an entry for the sled in a section (__patchable_function_entries by default) that tools
    // patch through. Its count and offset are 32-bit unsigned integer constants (argument_values, constants and wide, read
    // where its attributes are read), its section's string `text`, which clang's handler checks once its subject takes it
    // (check_input_patchable_entry).
    CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY,
    // clang's address-space qualifiers (#1447): GNU's address_space(N) and OpenCL's opencl_global and the rest, which C
    // takes too, a qualifier of the type at the attribute's place (check_input_address_space). CheckInputAttribute.space is
    // an OpenCL one's space (BUSTER_TYPE_SPACE_*), 0 for address_space's, whose value is its argument's.
    CHECK_ATTRIBUTE_ADDRESS_SPACE,
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
    // A deferred condition followed by message/severity strings, without executing the condition.
    CHECK_ATTRIBUTE_CONDITION_STRINGS,
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
    // no_caller_saved_registers (#147): clang's function type bit beside the convention, a System V function's here
    // (TYPE_FUNCTION_ABI_NO_CALLER_SAVED), so that with System V's convention it is this one (check_input_convention_merge).
    CHECK_CALL_NO_CALLER_SAVED,
    // clang's CC_IntelOclBicc, CC_Swift and CC_SwiftAsync (#1447).
    CHECK_CALL_INTEL_OCL,
    CHECK_CALL_SWIFT,
    CHECK_CALL_SWIFT_ASYNC,
    CHECK_CALL_IGNORED_X86_32,
} CheckInputCallingConvention;

// no_caller_saved_registers beside another convention than System V's (#1447): `CHECK_CALL_X | CHECK_CALL_KEEPS_REGISTERS`,
// TYPE_FUNCTION_ABI_KEEPS_REGISTERS' (check_input_attribute_requested_abi). Explicit sysv_abi with the bit is
// `CHECK_CALL_SYSV | CHECK_CALL_KEEPS_REGISTERS` while it is written, and TYPE_FUNCTION_ABI_NO_CALLER_SAVED's type.
#define CHECK_CALL_KEEPS_REGISTERS 0x40u
// clang applies no_caller_saved_registers by rebuilding the function type (adjustFunctionType through
// FunctionTypeUnwrapper), which drops the attribute sugar of every convention written before it, so that no later
// convention conflicts with those (getCallingConvAttributedType finds none) and a later one replaces them (#2014's
// review): CHECK_CALL_HIDDEN marks a list's convention that a later no_caller_saved_registers hid, no explicit one for
// a redeclaration either, and CHECK_CALL_STRIPS a list whose no_caller_saved_registers comes before its first convention,
// which hides the conventions of the lists and the typedef before it.
#define CHECK_CALL_HIDDEN 0x80u
#define CHECK_CALL_STRIPS 0x100u

// A convention's value without the hiding flags.
static uint32_t check_input_convention_unflagged(uint32_t call)
{
    return call & ~(CHECK_CALL_HIDDEN | CHECK_CALL_STRIPS);
}

// A convention's own part (CheckInputCallingConvention): none (CHECK_CALL_DEFAULT) of no_caller_saved_registers alone,
// which is a bit beside whatever convention the function has.
static uint32_t check_input_convention_base(uint32_t call)
{
    const uint32_t unflagged = check_input_convention_unflagged(call);
    return unflagged == CHECK_CALL_NO_CALLER_SAVED ? CHECK_CALL_DEFAULT : unflagged & ~CHECK_CALL_KEEPS_REGISTERS;
}

// Whether a later convention conflicts with this one's own part: it has one, and no later no_caller_saved_registers hid it.
static bool check_input_convention_visible(uint32_t call)
{
    return check_input_convention_base(call) != CHECK_CALL_DEFAULT && (call & CHECK_CALL_HIDDEN) == 0;
}

// Whether `call`, written after another convention, hides that one first (CHECK_CALL_STRIPS): no_caller_saved_registers alone
// does.
static bool check_input_convention_strips(uint32_t call)
{
    return (call & CHECK_CALL_STRIPS) != 0 || check_input_convention_unflagged(call) == CHECK_CALL_NO_CALLER_SAVED;
}

// `base` (a convention's own part) with no_caller_saved_registers' bit where `keeps`: no_caller_saved_registers' own beside
// none, and with an explicit convention, System V's too, that one's with the bit.
static uint32_t check_input_convention_compose(uint32_t base, bool keeps)
{
    return !keeps ? base : base == CHECK_CALL_DEFAULT ? CHECK_CALL_NO_CALLER_SAVED : base | CHECK_CALL_KEEPS_REGISTERS;
}

static bool check_input_convention_keeps(uint32_t call)
{
    const uint32_t unflagged = check_input_convention_unflagged(call);
    return unflagged == CHECK_CALL_NO_CALLER_SAVED || (unflagged & CHECK_CALL_KEEPS_REGISTERS) != 0;
}

// The convention a function asks for with `current` asked for already and `call` next (CheckInputAttributes'
// calling_convention): the first x86-64 has that is still visible, a later one where no_caller_saved_registers hid it
// (CHECK_CALL_HIDDEN), and no_caller_saved_registers' bit beside it, clang's function type bit beside its convention: with
// none, no_caller_saved_registers' own (TYPE_FUNCTION_ABI_NO_CALLER_SAVED), and with another, that one's with
// CHECK_CALL_KEEPS_REGISTERS (#1447).
static CheckInputCallingConvention check_input_convention_merge(CheckInputCallingConvention current, CheckInputCallingConvention call)
{
    const uint32_t current_base = check_input_convention_base(current);
    const uint32_t call_base = check_input_convention_base(call);
    const bool strips = check_input_convention_strips(call);
    const bool takes_call = call_base != CHECK_CALL_DEFAULT && call_base != CHECK_CALL_IGNORED_X86_32 && (!check_input_convention_visible(current) || strips);
    const uint32_t base = takes_call ? call_base : current_base;
    const bool keeps = check_input_convention_keeps(current) || check_input_convention_keeps(call);
    const bool hidden = takes_call ? (call & CHECK_CALL_HIDDEN) != 0 : current_base != CHECK_CALL_DEFAULT && (strips || (current & CHECK_CALL_HIDDEN) != 0);
    const bool stripped = (current & CHECK_CALL_STRIPS) != 0 || (current_base == CHECK_CALL_DEFAULT && strips);
    const uint32_t flags = (hidden ? CHECK_CALL_HIDDEN : 0u) | (stripped ? CHECK_CALL_STRIPS : 0u);
    return (CheckInputCallingConvention)(check_input_convention_compose(base, keeps) | flags);
}

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
    // C23 attributes after declaration specifiers apply to that base type, before declarator chunks (#147).
    bool specifier_type;
    bool specifier_ignored;
    // A C23 calling convention already applied to the specifier's function type, before outer chunks are built.
    bool specifier_applied;
    // Read from the declaration's common specifiers, before the declarator's chunks (#147).
    bool declaration_prefix;
    // Ignored by the subject check (check_input_attribute_subjects) of the declarator that read it last: a record that
    // declaration specifiers' attributes share among their declarators is checked again by each.
    bool subject_ignored;
    // A keyword's record (_Alignas, asm, a calling convention, _Noreturn, a nullability), not an attribute list's item.
    bool keyword;
    // A calling convention's wrong count of arguments (BUSTER_CHECK_COUNT_* and its bound), clang's error where the
    // convention meets a function (check_input_attribute_conventions) or a tag; on another subject, its warning alone.
    uint8_t convention_count;
    uint8_t convention_bound;
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
    // A wrong-count list whose expressions still need the declaration evaluator before the handler's count check.
    bool parse_arguments;
    uint8_t parsed_schema;
    // A generic attribute's integer arguments (xray_log_args's, the function effects', the GPU kernels' sizes), evaluated
    // where its attributes are read (check_input_read_attributes), not where its handler checks them, which a tag's subjects
    // reach from the constant evaluator: whether the first is an integer constant (its value in `value`), and bit N of
    // `constants` the Nth's (its value in `argument_values`). Bit N of `wide` is where the GPU kernels' Nth (bit 0 the
    // first's) needs more than 32 bits (check_input_constant_u32), its value then those bits as clang prints them.
    bool constant_argument;
    uint32_t constants;
    uint32_t wide;
    // An argument's error clang's handler gives once the attribute's subject takes it (#147: tls_model's model, constructor's,
    // destructor's and assume_aligned's integers), which the attributes' reader made and moved here
    // (check_input_attribute_defer), for check_input_attribute_subjects to give; or NULL.
    struct CheckInputReport* deferred;
    // A type argument's type (#147: vec_type_hint's and preferred_type's, check_input_type_argument), read where its
    // attributes are read (check_input_read_attributes), or 0.
    TypeId argument_type;
    // Thread-safety arguments' checked expression types, retained once for the subject's handler.
    TypeId* expression_types;
    // An availability attribute's arguments as clang's parser reads them (#1447, check_input_availability_parse), or NULL.
    struct CheckInputAvailabilityParse* availability;
    // diagnose_if's warning group (#1447), its index in pp_features_warning plus one, or 0.
    uint16_t warning_group;
    // A specifiers' objc_gc or objc_ownership that a declarator moved to its pointer, which its later declarators don't see
    // (#1447, check_input_attribute_objc).
    bool objc_moved;
    // A C23 cfi_salt after the name of a declarator that has a function (#1447): its subjects' error, or where they take it
    // clang's "cannot be applied to a declaration" (check_input_attribute_subjects).
    bool salt_declaration;
    // A C23 objc_gc or objc_ownership where a declaration's attributes are (#1447): before its specifiers or after a
    // declarator's name, which check_input_attribute_objc applies as GNU's after a declarator.
    bool objc_declaration;
    // An EXT_VECTOR_TYPE record's NEON kind (#1447): BUSTER_TYPE_FLAG_NEON for neon_vector_type, BUSTER_TYPE_FLAG_NEON_POLY
    // for neon_polyvector_type, which clang's HandleNeonVectorTypeAttr checks and makes a GNU vector of that kind of;
    // 0 for ext_vector_type. A C23 one after a declarator's name has its -Wdeprecated-attributes warning once its handler
    // took it (check_input_neon_vector), as ProcessDeclAttribute gives it to a valid attribute alone.
    uint8_t neon;
    bool neon_deprecated;
    // A NEON vector's after a typedef's name, which ProcessDeclAttribute refuses there (#1447): its error, once its count's
    // and size's are past, where a variable's is the deprecation warning.
    bool neon_typedef;
    // A tag's earlier declaration's, joined to its definition's list (#1447): availability's handler merges the
    // definition's own with each other, not with these, which the redeclaration's merge meets (check_input_availability_tag_merge).
    bool earlier_declaration;
    // An ADDRESS_SPACE record's OpenCL space (BUSTER_TYPE_SPACE_*, 0 for address_space's), and whether a declarator's chunk
    // applied it to its pointer or its group's type, which the declarator's whole type then doesn't take (#1447).
    uint8_t space;
    bool space_applied;
};

// An availability attribute's arguments as clang's parser reads them (#1447, check_input_availability_parse): its platform's
// token and its index of check_availability_platforms (check_availability_attribute_platform, UINT32_MAX for an unknown
// one); its stages' (introduced, deprecated, obsoleted) keywords and versions' tokens (BUSTER_CHECK_NONE for one not given,
// or cleared by unavailable) and versions; unavailable's keyword (or introduced=NA's) and environment's name, or
// BUSTER_CHECK_NONE.
typedef struct CheckInputAvailabilityParse CheckInputAvailabilityParse;
struct CheckInputAvailabilityParse
{
    uint32_t platform;
    uint32_t platform_index;
    uint32_t keywords[3];
    uint32_t version_tokens[3];
    CheckVersionTuple versions[3];
    uint32_t unavailable;
    uint32_t environment;
};

// An attribute's count of arguments against clang's (CheckInputAttribute.count_check): none wrong, another than exactly
// the bound, fewer than it, or more.
#define BUSTER_CHECK_COUNT_NONE 0
#define BUSTER_CHECK_COUNT_EXACT 1
#define BUSTER_CHECK_COUNT_LEAST 2
#define BUSTER_CHECK_COUNT_MOST 3

typedef struct CheckInputUnknownAttribute CheckInputUnknownAttribute;
struct CheckInputUnknownAttribute
{
    CheckInputUnknownAttribute* next;
    uint32_t first;
    uint32_t name_token;
    uint32_t name;
    uint32_t end;
    bool standard;
    // GNU-scoped C23 unknowns warn once per written attribute, even when a prefix is copied to several declarators.
    bool single_warning;
    bool warned;
    bool* warned_shared;
};

typedef struct CheckInputAttributes CheckInputAttributes;
struct CheckInputAttributes
{
    CheckInputAttribute* records;
    // Diagnostic-only spellings are separate from semantic records. Prefix copies share one-shot C23 GNU warning state.
    CheckInputUnknownAttribute* unknown_begin;
    CheckInputUnknownAttribute* unknown_end;
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
    // A const, pure, warn_unused_result or nodiscard among the records (#1447), which a discarded call's result warns of
    // (c_check_ignored_result.c): the unit's other attribute lists are skipped by it.
    bool results;
    // A nonempty function-type chunk remains after type attributes slide to it. clang rechecks the declaration's
    // already-applied kernel attributes after that chunk's attribute list too (#147).
    bool function_chunk;
    // A function's own parameters: 0 for ordinary, 1..4 for static modes 0..3, 5..8 for dynamic modes.
    // Present only when at least one parameter requests an object size. Immutable output-arena bytes.
    const uint8_t* object_sizes;
    uint32_t object_size_count;
    // Where the first warning of an attribute the list dropped completes, plus one, or 0 for none: one clang gives where it
    // processes the declaration's attributes, after its type's (#147, check_input_attribute_type_warnings): an unknown
    // one's (after its first token), another language's or a type name's declaration attribute's (at it, so one before).
    uint32_t unknown_first;
    // The errors of alignments the list's reader dropped (#1447, CheckInputAttributeRepeat), which its later declarators
    // give again; or NULL. The list's copies share them.
    struct CheckInputAttributeRepeat* repeats;
};

// An aligned's or _Alignas's error of its value that the reader recovered and dropped its record for (#1447): clang's
// handleAlignedAttr runs for each declarator, so each declarator after the first one that took the list gives `report`
// again (check_input_attribute_repeats). `declarator` is that first one's key (its name, or its declarator's end), 0
// before it; `next` is the list's next.
typedef struct CheckInputAttributeRepeat CheckInputAttributeRepeat;
struct CheckInputAttributeRepeat
{
    const struct CheckInputReport* report;
    CheckInputAttributeRepeat* next;
    uint32_t declarator;
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
    { "ms_struct", CHECK_ATTRIBUTE_MS_STRUCT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "gcc_struct", CHECK_ATTRIBUTE_GCC_STRUCT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "aligned", CHECK_ATTRIBUTE_ALIGNED, CHECK_ATTRIBUTE_INTEGER, 0, 1 },
    { "mode", CHECK_ATTRIBUTE_MODE, CHECK_ATTRIBUTE_IDENTIFIER, 1, 1 },
    { "vector_size", CHECK_ATTRIBUTE_VECTOR_SIZE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "ext_vector_type", CHECK_ATTRIBUTE_EXT_VECTOR_TYPE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    // clang's NEON vectors (#1447), which x86-64 takes as vectors of their own kind: type attributes in ext_vector_type's
    // places, its record's kind with CheckInputAttribute.neon, checked as HandleNeonVectorTypeAttr does (check_input_neon_vector).
    { "neon_vector_type", CHECK_ATTRIBUTE_EXT_VECTOR_TYPE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "neon_polyvector_type", CHECK_ATTRIBUTE_EXT_VECTOR_TYPE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    // The address-space qualifiers (#1447), type attributes in vector_size's places, applied where clang's processTypeAttrs
    // applies them (check_input_address_space); OpenCL's in the order of their spaces, BUSTER_TYPE_SPACE_GLOBAL's first.
    { "address_space", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "opencl_global", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_local", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_constant", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_private", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_generic", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_global_device", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "opencl_global_host", CHECK_ATTRIBUTE_ADDRESS_SPACE, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "transparent_union", CHECK_ATTRIBUTE_TRANSPARENT_UNION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "may_alias", CHECK_ATTRIBUTE_MAY_ALIAS, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "noderef", CHECK_ATTRIBUTE_NODEREF, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
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
    { "no_caller_saved_registers", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "intel_ocl_bicc", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "swiftcall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "swiftasynccall", CHECK_ATTRIBUTE_CALLING_CONVENTION, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
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
    { "selectany", CHECK_ATTRIBUTE_SELECTANY, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
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
    { "pass_object_size", CHECK_ATTRIBUTE_PASS_OBJECT_SIZE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "pass_dynamic_object_size", CHECK_ATTRIBUTE_PASS_DYNAMIC_OBJECT_SIZE, CHECK_ATTRIBUTE_INTEGER, 1, 1 },
    { "swift_indirect_result", CHECK_ATTRIBUTE_PARAMETER_ABI, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "swift_error_result", CHECK_ATTRIBUTE_PARAMETER_ABI, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "swift_context", CHECK_ATTRIBUTE_PARAMETER_ABI, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "swift_async_context", CHECK_ATTRIBUTE_PARAMETER_ABI, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    { "diagnose_if", CHECK_ATTRIBUTE_DIAGNOSE_IF, CHECK_ATTRIBUTE_CONDITION_STRINGS, 3, 4 },
    { "enable_if", CHECK_ATTRIBUTE_ENABLE_IF, CHECK_ATTRIBUTE_CONDITION_STRINGS, 2, 2 },
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
    { "target_clones", CHECK_ATTRIBUTE_TARGET_CLONES, CHECK_ATTRIBUTE_STRING_LIST, 0, 255 },
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
    // x86's, whose arguments clang's parser reads as expressions and its handler ignores, whatever their count.
    { "interrupt", CHECK_ATTRIBUTE_INTERRUPT, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 255 },
    { "force_align_arg_pointer", CHECK_ATTRIBUTE_FORCE_ALIGN_ARG_POINTER, CHECK_ATTRIBUTE_NO_ARGUMENTS, 0, 0 },
    // Its count, its offset and its section (#1447): two integers and a string, which its own reading takes
    // (check_input_read_attributes) rather than the integer lists' evaluator.
    { "patchable_function_entry", CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY, CHECK_ATTRIBUTE_INTEGER_LIST, 1, 3 },
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

// The rules' row of address_space, before OpenCL's spaces' (#1447).
static uint32_t check_input_address_space_rule(void)
{
    uint32_t result = 0;
    while (result < BUSTER_ARRAY_LENGTH(check_input_attribute_rules) && check_input_attribute_rules[result].kind != CHECK_ATTRIBUTE_ADDRESS_SPACE)
    {
        result += 1;
    }
    return result;
}

static bool check_input_spelling(CheckInputReader* reader, uint32_t token, const uint8_t** bytes, uint32_t* length);

static String8 check_input_attribute_spelling(const InternTable* table, uint32_t name)
{
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

static String8 check_input_attribute_name(CheckInputReader* reader, uint32_t name)
{
    // A reader of no preprocessor, a unit test's, has no names.
    const InternTable* table = reader->pp != NULL ? reader->pp->table : NULL;
    return check_input_attribute_spelling(table, name);
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

// Which Objective-C pointer type attribute a record is (#1447): 1 for objc_gc, 2 for objc_ownership, in either syntax, else 0.
static uint32_t check_input_objc_kind(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const String8 name = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ? check_input_attribute_name(reader, attribute->name) :
                                                                                                                      (String8){ 0 };
    return check_input_attribute_equal(name, "objc_gc") ? 1u : check_input_attribute_equal(name, "objc_ownership") ? 2u : 0u;
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
    CHECK_ARGUMENT_EXPECTED_EXPRESSION,
    CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE,
    CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME,
    CHECK_ARGUMENT_EXPRESSION_SIZEOF,
    CHECK_ARGUMENT_EXPRESSION_CALL_COUNT,
    CHECK_ARGUMENT_EXPRESSION_BINARY,
    // A constant's member access through `->` of no pointer, "member reference type 'int' is not a pointer", at the
    // member's name; and its pointer arithmetic on an incomplete pointee, "arithmetic on a pointer to an incomplete type
    // 'struct T'", at the operator, with the tag's note (#1447, check_constant_failure_error).
    CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER,
    CHECK_ARGUMENT_EXPRESSION_INCOMPLETE_ARITHMETIC,
    CHECK_ARGUMENT_ANNOTATE_CONSTANT,
    CHECK_ARGUMENT_TARGET_CLONES_DEFAULT,
    CHECK_ARGUMENT_SELECTANY_LINKAGE,
    // pcs's identifier argument aapcs, after its "requires a string" (#147): the convention's warning the driver gives, and
    // on a tag then the warning that it takes functions alone.
    CHECK_ARGUMENT_CONVENTION_UNSUPPORTED,
    CHECK_ARGUMENT_FUNCTIONS_METHODS,
    // clang's checks-only attributes' (#1447, check_input_attribute_checks): enforce_tcb's and enforce_tcb_leaf's of one TCB,
    // "attributes 'X("T")' and 'Y("T")' are mutually exclusive" (Y's name token in CheckInputReport.value's low half, the
    // TCB's string token in its high half); callback's "'callback' attribute specifies no callback callee", "'callback'
    // attribute argument 'A' is not a known function parameter" (A's token in the value), "'callback' argument at position N
    // references unavailable implicit 'this'", "... specifies invalid callback callee", "... callee does not have function
    // type", "'X' attribute references function of type 'T' which expects N arguments but attribute specifies M parameter
    // index arguments" (CheckInputReport.type, M in the value), "... callee may not be variadic" and "multiple 'callback'
    // attributes specified"; swift_async's kind, "first argument to 'swift_async' must be either 'none', 'swift_private', or
    // 'not_swift_private'", and its completion handler, "'swift_async' completion handler parameter must have block type
    // returning 'void', type here is 'T'", at the parameter; ns_error_domain's "domain argument 'D' does not refer to global
    // constant" (N 1, D's token in the value; N 0 without it); modular_format's "duplicate aspect 'A' in 'modular_format'
    // attribute" (A's string token in the value), "attribute 'X' is already applied with different arguments" and
    // "'modular_format' attribute requires 'format' attribute", at the function's name.
    CHECK_ARGUMENT_TCB_CONFLICT,
    CHECK_ARGUMENT_CALLBACK_NO_CALLEE,
    CHECK_ARGUMENT_CALLBACK_UNKNOWN,
    CHECK_ARGUMENT_CALLBACK_THIS,
    CHECK_ARGUMENT_CALLBACK_INVALID_CALLEE,
    CHECK_ARGUMENT_CALLBACK_NOT_FUNCTION,
    CHECK_ARGUMENT_CALLBACK_COUNT,
    CHECK_ARGUMENT_CALLBACK_VARIADIC,
    CHECK_ARGUMENT_CALLBACK_MULTIPLE,
    CHECK_ARGUMENT_SWIFT_ASYNC_KIND,
    CHECK_ARGUMENT_SWIFT_ASYNC_BLOCK,
    // The Swift parameter attributes' errors (#1447, clang's SemaSwift::AddParameterABIAttr and checkExtParameterInfos), each
    // with the parameter's ABI (TypeParameterABI) in the number's low bits: its type's, "must have pointer type" (with
    // BUSTER_CHECK_SWIFT_POINTER_POINTER, "pointer to unqualified pointer type"), the type CheckInputReport.type; its
    // function's convention's, swiftcall or swiftasynccall (swiftcall's alone with BUSTER_CHECK_SWIFT_ONLY); an indirect
    // result's after another parameter; an error result's not after a context; and two ABIs on one parameter, with the
    // earlier's ABI in the number's second byte.
    CHECK_ARGUMENT_SWIFT_PARAMETER_TYPE,
    CHECK_ARGUMENT_SWIFT_PARAMETER_CONVENTION,
    CHECK_ARGUMENT_SWIFT_INDIRECT_FIRST,
    CHECK_ARGUMENT_SWIFT_ERROR_AFTER_CONTEXT,
    CHECK_ARGUMENT_SWIFT_PARAMETER_CONFLICT,
    // A function without a prototype of a convention that takes no variadic call (clang's err_cconv_knr).
    CHECK_ARGUMENT_NO_PROTOTYPE_CONVENTION,
    CHECK_ARGUMENT_ERROR_DOMAIN,
    CHECK_ARGUMENT_DUPLICATE_ASPECT,
    CHECK_ARGUMENT_DIFFERENT_ARGUMENTS,
    CHECK_ARGUMENT_REQUIRES_FORMAT,
    // The availability and external_source_symbol attributes' parser's errors (#1447, check_input_availability_parse and
    // check_input_external_source_parse), and availability's Sema's: "expected a platform name, e.g., 'macos'", "expected
    // ','" (after the token, its end), "expected 'introduced', 'deprecated', or 'obsoleted'", "redundant 'K' availability
    // change; only the last specified change will be used", "expected '=' after 'K'", "expected string literal for optional
    // message in 'availability' attribute", "expected an environment name, e.g., 'compute'", "expected a version of the
    // form 'major[.minor[.subminor]]'", "version number must have non-zero major, minor, or sub-minor version", "'K' is not
    // an availability stage; use 'introduced', 'deprecated', or 'obsoleted'" (K's token in the value), "invalid anyAppleOS
    // version 'V' in availability attribute" (at V), "unexpected parameter 'environment' in availability attribute, not
    // permitted in C/C++"; "expected 'language', 'defined_in', 'generated_declaration', or 'USR'", "duplicate 'K' clause
    // in an 'external_source_symbol' attribute" (K's token in the value), "expected '=' after K" (after the token, its
    // end) and "expected string literal for N in 'external_source_symbol' attribute" (N language name, source container
    // name or USR, by the number).
    CHECK_ARGUMENT_AVAILABILITY_PLATFORM,
    CHECK_ARGUMENT_AVAILABILITY_COMMA,
    CHECK_ARGUMENT_AVAILABILITY_CHANGE,
    CHECK_ARGUMENT_AVAILABILITY_REDUNDANT,
    CHECK_ARGUMENT_AVAILABILITY_EQUAL,
    CHECK_ARGUMENT_AVAILABILITY_MESSAGE,
    CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT_NAME,
    CHECK_ARGUMENT_AVAILABILITY_VERSION,
    CHECK_ARGUMENT_AVAILABILITY_ZERO,
    CHECK_ARGUMENT_AVAILABILITY_STAGE,
    CHECK_ARGUMENT_AVAILABILITY_ANY_APPLE,
    CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT,
    CHECK_ARGUMENT_SOURCE_KEYWORD,
    CHECK_ARGUMENT_SOURCE_DUPLICATE,
    CHECK_ARGUMENT_SOURCE_EQUAL,
    CHECK_ARGUMENT_SOURCE_STRING,
    // A `;` just before the `)` of their arguments, "unexpected ';' before ')'"; objc_ownership on a declaration of no
    // declarator, "'X' attribute only applies to variables", at its first token.
    CHECK_ARGUMENT_UNEXPECTED_SEMI,
    CHECK_ARGUMENT_OBJC_VARIABLES,
    // patchable_function_entry's (#1447, check_input_patchable_entry): an offset past its count, "'X' attribute requires
    // integer constant between 0 and N inclusive" (N the count, CheckInputReport.value), at the attribute; an empty section,
    // "section argument to 'patchable_function_entry' attribute is not valid for this target: section must not be empty",
    // at its string.
    CHECK_ARGUMENT_BETWEEN,
    CHECK_ARGUMENT_PATCHABLE_SECTION,
    // neon_vector_type's and neon_polyvector_type's vector of another width than 64 or 128 bits (#1447), at the attribute.
    CHECK_ARGUMENT_NEON_SIZE,
    // diagnose_as_builtin's (#1447, clang's handleDiagnoseAsBuiltinAttr): its first argument no builtin function's name (at
    // it, its place in the number), its builtin's count of parameters (the number) not its count of indices, an index
    // past the function's parameters (its count the number, the index and the function's name in the value), and a
    // parameter's type not its builtin's (the builtin's parameter's place the number, the index and the function's name
    // in the value, the parameter's type the report's).
    CHECK_ARGUMENT_BUILTIN_FUNCTION,
    CHECK_ARGUMENT_BUILTIN_COUNT,
    CHECK_ARGUMENT_BUILTIN_BOUNDS,
    CHECK_ARGUMENT_BUILTIN_TYPES,
    // diagnose_if's and enable_if's (#1447, c_check_condition.c): a third argument other than "error" or "warning", "invalid
    // diagnostic type for 'diagnose_if'; use \"error\" or \"warning\" instead", and a fourth that names no warning group,
    // "unknown warning group 'G'", at the argument; a condition that is no constant without its parameters' values, "'X'
    // attribute expression never produces a constant expression", at the attribute, its note at the subexpression.
    CHECK_ARGUMENT_DIAGNOSTIC_TYPE,
    CHECK_ARGUMENT_WARNING_GROUP,
    CHECK_ARGUMENT_NEVER_CONSTANT,
    // _Alignas's "'aligned' attribute requires integer constant", at the argument (#1447): clang's handler names it so.
    CHECK_ARGUMENT_ALIGNAS_INTEGER_CONSTANT,
    // aligned's and _Alignas's "requested alignment must be 4294967296 bytes or smaller", at the attribute (#1447).
    CHECK_ARGUMENT_ALIGNMENT_TOO_LARGE,
    // The type tags' (#1447, c_check_type_safety_input.c): type_tag_for_datatype's parser's "expected identifier" (its kind's
    // place, or a flag's) and "invalid comparison flag 'F'; use 'layout_compatible' or 'must_be_null'" (F's token in the
    // value), at the token; and its variable's initializer of no integer constant, "'type_tag_for_datatype' attribute
    // requires the initializer to be an integer constant expression", at the attribute.
    CHECK_ARGUMENT_TYPE_TAG_IDENTIFIER,
    CHECK_ARGUMENT_TYPE_TAG_FLAG,
    CHECK_ARGUMENT_TYPE_TAG_INITIALIZER,
    // Objective-C's bridges' (#1447, check_input_attribute_objc_bridge): objc_bridge's, objc_bridge_mutable's and
    // objc_bridge_related's first argument of no identifier, "parameter of 'X' attribute must be a single name of an
    // Objective-C class", at the declaration's first token; objc_bridge's on a typedef, "parameter of 'X' attribute must be
    // 'id' when used on a typedef" and, of a type of no pointer to void, "'objc_bridge(id)' is only allowed on structs and
    // typedefs of void pointers", at the attribute. objc_bridge_related's parser's (check_input_objc_bridge_related_parse):
    // "expected a related Objective-C class name, e.g., 'NSColor'", "expected a class method selector with single argument,
    // e.g., 'colorWithCGColor:'" and "expected ','", at the token.
    CHECK_ARGUMENT_OBJC_CLASS,
    CHECK_ARGUMENT_OBJC_TYPEDEF_ID,
    CHECK_ARGUMENT_OBJC_TYPEDEF_POINTER,
    CHECK_ARGUMENT_OBJC_RELATED_CLASS,
    CHECK_ARGUMENT_OBJC_RELATED_SELECTOR,
    CHECK_ARGUMENT_EXPECTED_COMMA,
    // swift_newtype's and swift_wrapper's parser's empty parentheses (#1447, check_input_swift_newtype_parse), "argument
    // required after attribute", at the `)`.
    CHECK_ARGUMENT_REQUIRED_AFTER,
    // swift_async_error's convention other than `none` with swift_async(none) on the function (#1447,
    // check_input_attribute_swift_async_error), "'swift_async' attribute must be applied to a function annotated with
    // non-'none' attribute 'swift_async'", at the swift_async.
    CHECK_ARGUMENT_SWIFT_ASYNC_NONE,
    // swift_error's (#1447, check_input_attribute_swift_error), at the attribute: "'X' attribute can only be applied to a
    // function with an error parameter", and "'X' attribute with 'C' convention can only be applied to a function returning
    // an integral type" (number 0) or "a pointer" (1), C the convention's identifier (the attribute's first argument).
    CHECK_ARGUMENT_SWIFT_ERROR_PARAMETER,
    CHECK_ARGUMENT_SWIFT_ERROR_RESULT,
    // cfi_salt on a function type without a prototype (#1447, check_input_attribute_salt), "'X' attribute only applies to
    // non-K&R-style functions", at the attribute.
    CHECK_ARGUMENT_NON_KR,
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
// (its specifiers', `common`, then its declarator's, `own`; an underalignment's at the last): on a parameter, a register variable or a bit-field
// (`declaration` 0, 1 or 3, as clang numbers them) "cannot be applied to", on a typedef or a function (BUSTER_CHECK_NONE)
// "only applies to variables and fields"; with `type` (and `declaration` its alignment in bytes) "requested alignment
// is less than minimum alignment" (`spelling` the declaration's typedef's, check_input_typedef_spelling).
static void check_input_alignas_error(CheckInputReader* reader, CheckInputAttributes* common, CheckInputAttributes* own, uint32_t declaration, TypeId type,
                                      uint32_t spelling)
{
    // The underalignment's is at the last _Alignas that stands (clang's CheckAlignasUnderalignment, #1447), the others' at the
    // first.
    const bool last = type != 0;
    CheckInputAttribute* found = NULL;
    CheckInputAttributes* holder = NULL;
    for (uint32_t list = 0; (last || found == NULL) && list < 2; list += 1)
    {
        CheckInputAttributes* attributes = list == 0 ? common : own;
        for (uint32_t i = 0; attributes != NULL && (last || found == NULL) && i < attributes->count; i += 1)
        {
            CheckInputAttribute* record = attributes->records + i;
            const bool match = record->kind == CHECK_ATTRIBUTE_ALIGNAS && (!last || record->deferred == NULL);
            found = match ? record : found;
            holder = match ? attributes : holder;
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
    else if (kind == CHECK_ATTRIBUTE_ALIGNAS)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_ALIGNAS_INTEGER_CONSTANT, 0);
    }
    else if (kind == CHECK_ATTRIBUTE_REGPARM || kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR ||
             (kind == CHECK_ATTRIBUTE_ASSUME_ALIGNED && argument == 0))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_AN_INTEGER_CONSTANT, 0);
    }
    else if (kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
    {
        // (A NEON vector's is verifyValidIntegerConstantExpr's, which names it: #1447.)
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first,
                                             attribute->neon != 0 ? CHECK_ARGUMENT_AN_INTEGER_CONSTANT : CHECK_ARGUMENT_EXT_VECTOR_INTEGER, 0);
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
            feature = predefines_attribute_feature(text.pointer + name, last - name) ? feature : (name << 4 | (last - name) << 18);
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

static bool check_input_attribute_unknown_append(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputUnknownAttribute attribute)
{
    CheckInputUnknownAttribute* record = arena_push_array(reader->arena, 1, sizeof(CheckInputUnknownAttribute), 8);
    const bool valid = record != NULL;
    if (valid)
    {
        *record = attribute;
        record->next = NULL;
        record->warned_shared = attribute.warned_shared != NULL ? attribute.warned_shared : &record->warned;
        if (attributes->unknown_end != NULL)
        {
            attributes->unknown_end->next = record;
        }
        else
        {
            attributes->unknown_begin = record;
        }
        attributes->unknown_end = record;
    }
    else
    {
        check_input_attribute_error(reader, attributes, attribute.first, attribute.end, CHECK_ATTRIBUTE_ERROR_RESOURCE);
    }
    return valid;
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
        attributes->results |= attribute.kind == CHECK_ATTRIBUTE_CONST || attribute.kind == CHECK_ATTRIBUTE_PURE || attribute.kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT;
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
    destination->unknown_begin = NULL;
    destination->unknown_end = NULL;
    bool valid = source->error == CHECK_ATTRIBUTE_ERROR_NONE;
    for (uint32_t i = 0; valid && i < source->count; i += 1)
    {
        valid = check_input_attribute_append(reader, destination, source->records[i]);
    }
    for (const CheckInputUnknownAttribute* unknown = source->unknown_begin; valid && unknown != NULL; unknown = unknown->next)
    {
        valid = check_input_attribute_unknown_append(reader, destination, *unknown);
    }
    return valid;
}

static uint32_t check_input_inherited_contract(const CheckInputAttribute* attribute)
{
    const uint32_t bit = attribute->kind == CHECK_ATTRIBUTE_FORMAT ? 1u :
                         attribute->kind == CHECK_ATTRIBUTE_CONST ? 2u :
                         attribute->kind == CHECK_ATTRIBUTE_PURE ? 4u :
                         attribute->kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT ? 8u : 0u;
    const uint32_t result = !attribute->ignored && !attribute->subject_ignored ? bit : 0u;
    return result;
}

// A compatible visible function redeclaration keeps its predecessor's format and value-discard contracts. Copy only those
// records into a new accumulator: neither the predecessor nor already published declaration metadata is mutable.
static void check_input_contract_inherit(CheckInputReader* reader, uint32_t token, uint32_t previous)
{
    // Binding-only clients can omit the optional attribute column entirely.
    const CheckInputAttributes* prior = check_sparse_wide_present(reader->output->attributes) ?
                                        check_input_attributes_at(reader->output, previous) : NULL;
    uint32_t inherit = 0;
    for (uint32_t i = 0; prior != NULL && i < prior->count; i += 1)
    {
        inherit |= check_input_inherited_contract(prior->records + i);
    }
    const CheckInputAttributes* own = inherit ? check_input_attributes_at(reader->output, token) : NULL;
    uint32_t annotated = 0;
    for (uint32_t i = 0; own != NULL && i < own->count; i += 1)
    {
        annotated |= check_input_inherited_contract(own->records + i);
    }
    const uint32_t missing = inherit & ~annotated;
    if (missing != 0)
    {
        CheckInputAttributes* copy = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes, 1);
        bool valid = copy != NULL;
        if (valid)
        {
            *copy = (CheckInputAttributes){ 0 };
            valid = own == NULL || check_input_attribute_copy(reader, own, copy);
        }
        for (uint32_t i = 0; valid && i < prior->count; i += 1)
        {
            const CheckInputAttribute* attribute = prior->records + i;
            if ((check_input_inherited_contract(attribute) & missing) != 0)
            {
                valid = check_input_attribute_append(reader, copy, *attribute);
            }
        }
        reader->output->resource |= !valid;
        if (valid)
        {
            check_input_set_attributes_at(reader->output, token, copy);
        }
    }
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
    for (const CheckInputUnknownAttribute* unknown = suffix->unknown_begin; valid && unknown != NULL; unknown = unknown->next)
    {
        valid = check_input_attribute_unknown_append(reader, output, *unknown);
    }
    if (valid)
    {
        output->packed |= suffix->packed;
        output->has_alignas |= suffix->has_alignas;
        output->function_chunk |= suffix->function_chunk;
        output->object_sizes = suffix->object_sizes != NULL ? suffix->object_sizes : output->object_sizes;
        output->object_size_count = suffix->object_sizes != NULL ? suffix->object_size_count : output->object_size_count;
        output->alignment_log2 = suffix->alignment_log2 > output->alignment_log2 ? suffix->alignment_log2 : output->alignment_log2;
        output->required_alignment_log2 = suffix->required_alignment_log2 > output->required_alignment_log2 ? suffix->required_alignment_log2 : output->required_alignment_log2;
        // The common one first, as their records are; a conflict is clang's error where they meet a function.
        output->calling_convention = check_input_convention_merge(output->calling_convention, suffix->calling_convention);
        output->unknown_first = output->unknown_first == 0 || (suffix->unknown_first != 0 && suffix->unknown_first < output->unknown_first) ?
                                suffix->unknown_first : output->unknown_first;
        // The common one's repeats, which its other declarators share; a declarator's own list has one declarator, which
        // gives none (check_input_attribute_repeats).
        output->repeats = output->repeats != NULL ? output->repeats : suffix->repeats;
    }
    return valid;
}

// The repeats of `first` then `second` (#1447, CheckInputAttributeRepeat), for a list that joins one's records and then
// the other's before any declarator has taken them: `first`'s copied where both have some. NULL where room ran out.
static CheckInputAttributeRepeat* check_input_attribute_repeats_join(CheckInputReader* reader, CheckInputAttributeRepeat* first, CheckInputAttributeRepeat* second)
{
    CheckInputAttributeRepeat* result = second;
    CheckInputAttributeRepeat** link = &result;
    bool room = true;
    for (const CheckInputAttributeRepeat* repeat = first; room && repeat != NULL; repeat = repeat->next)
    {
        CheckInputAttributeRepeat* copy = arena_push(reader->arena, sizeof(CheckInputAttributeRepeat), 8);
        room = copy != NULL;
        if (room)
        {
            *copy = (CheckInputAttributeRepeat){ .report = repeat->report, .next = second };
            *link = copy;
            link = &copy->next;
        }
    }
    reader->output->resource |= !room;
    return room ? result : NULL;
}

// A recovered error of an aligned's or _Alignas's value (#1447): its report (`report`, as it was made) kept on the list as
// one of its repeats (CheckInputAttributeRepeat), after the ones it has. Whether there was room.
static bool check_input_attribute_repeat_add(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputReport* report)
{
    CheckInputReport* copy = arena_push(reader->arena, sizeof(CheckInputReport), 8);
    CheckInputAttributeRepeat* repeat = copy != NULL ? arena_push(reader->arena, sizeof(CheckInputAttributeRepeat), 8) : NULL;
    reader->output->resource |= repeat == NULL;
    if (repeat != NULL)
    {
        *copy = *report;
        *repeat = (CheckInputAttributeRepeat){ .report = copy };
        CheckInputAttributeRepeat** link = &attributes->repeats;
        while (*link != NULL)
        {
            link = &(*link)->next;
        }
        *link = repeat;
    }
    return repeat != NULL;
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

static CheckStringLiteral check_input_strings_extended(CheckInputReader* reader, uint32_t token, const CheckStringPart* parts, uint32_t count);

// Ordinary string tokens may concatenate. The shared literal decoder retains embedded NULs and UTF-8/escapes;
// consumers must not treat these bounded payloads as C strings. Decoded bytes live in the output arena, while the
// spelling views and the one-part-per-token array are scratch storage and disappear after this operation. An `evaluated`
// one (an asm label's, which clang reads as an evaluated string literal, #1447) takes the escapes the fast decoding
// refuses that clang takes, a delimited one's and an unknown one's with its warning (check_input_strings_extended).
static bool check_input_attribute_string(CheckInputReader* reader, uint32_t first, uint32_t end, bool evaluated, String8* text)
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
        if (valid && !evaluated)
        {
            // An unevaluated string literal's encoding prefix has no effect (#1447): its warning is
            // check_input_unevaluated_refused's.
            check_literal_unevaluated_parts(&parts[i - first], 1);
        }
        if (valid)
        {
            const CheckStringSpan span = check_literal_string_span(&parts[i - first]);
            valid = span.valid && span.encoding == CHECK_STRING_ORDINARY;
        }
    }
    if (valid)
    {
        CheckStringLiteral decoded = check_literal_strings(parts, end - first, reader->pp->options.language.standard >= 2023, reader->arena);
        reader->output->resource |= decoded.resource;
        if (evaluated && !decoded.valid && !decoded.resource)
        {
            decoded = check_input_strings_extended(reader, first, parts, end - first);
        }
        valid = decoded.valid && decoded.byte_count != 0;
        if (valid)
        {
            *text = (String8) { (char*)decoded.bytes, decoded.byte_count - 1 };
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid;
}

// clang's parser reads an attribute's string argument and a static assertion's message as an unevaluated string literal
// (#147, ParseUnevaluatedStringLiteralExpression): of the string literals from `first` to `end` (an attribute's argument's
// parentheses peeled, as ParseUnevaluatedStringInAttribute peels them, into `*end`), the first token that has an escape
// sequence clang refuses there (check_literal_unevaluated_escapes), or BUSTER_CHECK_NONE. Tokens of another kind are left
// to the callers' own errors. A literal it takes has its warnings here (#1447), completed at `completion` (a static
// assertion's keyword, whose failure clang's Sema gives after its parser's warnings), else at their token.
static uint32_t check_input_unevaluated_refused(CheckInputReader* reader, uint32_t first, uint32_t* end, uint32_t completion)
{
    const Parser* parser = reader->parser;
    uint32_t last = *end;
    while (first + 1 < last && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == last - 1)
    {
        first += 1;
        last -= 1;
    }
    bool strings = first < last;
    for (uint32_t token = first; strings && token < last; token += 1)
    {
        strings = parser->tokens.kinds[token] == PARSE_KIND_STRING;
    }
    uint32_t refused = BUSTER_CHECK_NONE;
    // (Whether a token has a prefix or an escape, which alone may have the warnings below: most have neither.)
    bool notable = false;
    for (uint32_t token = first; strings && refused == BUSTER_CHECK_NONE && token < last; token += 1)
    {
        const uint64_t mark = reader->scratch->length;
        const uint8_t* bytes = NULL;
        uint32_t length = 0;
        const bool spelled = check_input_spelling(reader, token, &bytes, &length);
        notable |= spelled && length != 0 && (bytes[0] != '"' || memchr(bytes, '\\', length) != NULL);
        bool had_error = false;
        refused = spelled && check_literal_unevaluated_escapes(bytes, length, reader->pp != NULL && reader->pp->options.language.standard >= 2023, false, NULL, &had_error) != 0 ? token : BUSTER_CHECK_NONE;
        arena_pop_to(reader->scratch, mark);
    }
    // Each encoding prefix's -Winvalid-unevaluated-string warning (#1447), once though a list may be read again; a literal
    // clang refuses has them from the driver, before its escapes' (check_driver_escapes).
    for (uint32_t token = first; notable && refused == BUSTER_CHECK_NONE && token < last; token += 1)
    {
        const uint64_t mark = reader->scratch->length;
        const uint8_t* bytes = NULL;
        uint32_t length = 0;
        const uint32_t prefix = check_input_spelling(reader, token, &bytes, &length) ? check_literal_encoding_prefix(bytes, length) : 0;
        const uint8_t letter = prefix != 0 ? bytes[0] : 0;
        arena_pop_to(reader->scratch, mark);
        if (prefix != 0)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_INVALID_UNEVALUATED_STRING,
                .kind = CHECK_WARNING_EVENT_UNEVALUATED_PREFIX,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = completion != BUSTER_CHECK_NONE ? completion : token + 1,
                .payload = { .name = letter, .data = prefix, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_once(reader, &event);
        }
    }
    // -pedantic's warnings of the delimited and named escapes of a literal clang takes (#1447), once each token though a
    // list may be read again: the driver's of one it refuses (check_driver_escape).
    bool had_error = false;
    for (uint32_t token = first; notable && refused == BUSTER_CHECK_NONE && token < last; token += 1)
    {
        const uint64_t mark = reader->scratch->length;
        const uint8_t* bytes = NULL;
        uint32_t length = 0;
        const bool c23 = reader->pp != NULL && reader->pp->options.language.standard >= 2023;
        const bool spelled = check_input_spelling(reader, token, &bytes, &length) && length != 0;
        const bool before = had_error;
        const uint32_t count = spelled ? check_literal_unevaluated_escapes(bytes, length, c23, true, NULL, &had_error) : 0;
        const uint64_t key = (uint64_t)CHECK_WARNING_EVENT_DELIMITED_ESCAPE << 32 | ((uint64_t)token + 1);
        bool fresh = count != 0 && !check_warning_set_contains(&reader->source_warning_keys, key);
        if (fresh)
        {
            fresh = check_warning_set_add(&reader->source_warning_keys, key);
            reader->output->resource |= !fresh;
        }
        CheckEscapeDiagnostic* diagnostics = fresh ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckEscapeDiagnostic, BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length)) : NULL;
        reader->output->resource |= fresh && diagnostics == NULL;
        bool replayed = before;
        const uint32_t written = diagnostics != NULL ? check_literal_unevaluated_escapes(bytes, length, c23, true, diagnostics, &replayed) : 0;
        for (uint32_t i = 0; i < written; i += 1)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_DELIMITED_ESCAPE,
                .kind = CHECK_WARNING_EVENT_DELIMITED_ESCAPE,
                .token = token,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = completion != BUSTER_CHECK_NONE ? completion : token + 1,
                .payload = { .data = diagnostics[i].kind == CHECK_ESCAPE_DIAGNOSTIC_NAMED, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_event(reader, &event, NULL, 0);
        }
        arena_pop_to(reader->scratch, mark);
    }
    *end = last;
    return refused;
}

// check_input_unevaluated_refused's literal, whose error drops what it belonged to: whether it has one, and then
// CHECK_INPUT_UNEVALUATED_ESCAPE at its first token that has one, once, though a list may be read again.
static bool check_input_unevaluated_strings(CheckInputReader* reader, uint32_t first, uint32_t end, uint32_t completion)
{
    const uint32_t refused = check_input_unevaluated_refused(reader, first, &end, completion);
    bool reported = false;
    for (uint32_t r = 0; refused != BUSTER_CHECK_NONE && r < reader->output->report_count; r += 1)
    {
        reported |= reader->output->reports[r].message == CHECK_INPUT_UNEVALUATED_ESCAPE && reader->output->reports[r].token == refused;
    }
    if (refused != BUSTER_CHECK_NONE && !reported)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, refused, CHECK_INPUT_UNEVALUATED_ESCAPE);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].detail = end;
        }
    }
    return refused != BUSTER_CHECK_NONE;
}

#define BUSTER_CHECK_SWIFT_POINTER_POINTER 0x10u
#define BUSTER_CHECK_SWIFT_ONLY 0x10u

// clang's spelling of a parameter's ABI (getParameterABISpelling).
static const char* check_input_parameter_abi_name(uint32_t abi)
{
    const char* result = abi == TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT ? "swift_indirect_result" : abi == TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT ? "swift_error_result" :
                         abi == TYPE_PARAMETER_ABI_SWIFT_CONTEXT ? "swift_context" : abi == TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT ? "swift_async_context" : "";
    return result;
}

static TypeParameterABI check_input_parameter_abi(String8 name)
{
    const TypeParameterABI result = check_input_attribute_equal(name, "swift_indirect_result") ? TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT :
                                    check_input_attribute_equal(name, "swift_error_result") ? TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT :
                                    check_input_attribute_equal(name, "swift_context") ? TYPE_PARAMETER_ABI_SWIFT_CONTEXT :
                                    check_input_attribute_equal(name, "swift_async_context") ? TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT : TYPE_PARAMETER_ABI_ORDINARY;
    return result;
}

// clang's name of a calling convention (FunctionType::getNameForCallConv): on x86-64 Linux, sysv_abi's and those it
// hasn't (which it takes for the default, CHECK_CALL_IGNORED_X86_32) are cdecl's.
static const char* check_input_convention_name(uint32_t with)
{
    // (A convention's own part, beside no_caller_saved_registers' bit, #1447.)
    const uint32_t unflagged = check_input_convention_unflagged(with);
    const uint32_t call = unflagged == CHECK_CALL_NO_CALLER_SAVED ? unflagged : unflagged & ~CHECK_CALL_KEEPS_REGISTERS;
    const char* result = call == CHECK_CALL_MICROSOFT ? "ms_abi" : call == CHECK_CALL_VECTOR ? "vectorcall" : call == CHECK_CALL_REG ? "regcall" :
                         call == CHECK_CALL_PRESERVE_MOST ? "preserve_most" : call == CHECK_CALL_PRESERVE_ALL ? "preserve_all" :
                         call == CHECK_CALL_PRESERVE_NONE ? "preserve_none" : call == CHECK_CALL_NO_CALLER_SAVED ? "no_caller_saved_registers" :
                         call == CHECK_CALL_INTEL_OCL ? "intel_ocl_bicc" : call == CHECK_CALL_SWIFT ? "swiftcall" :
                         call == CHECK_CALL_SWIFT_ASYNC ? "swiftasynccall" : "cdecl";
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
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_CONVENTIONS, (call & 0xffu) | (previous & 0xffu) << 8);
        if (reader->output->report_count > before)
        {
            CheckInputReport* report = reader->output->reports + before;
            report->completion = report->completion < reader->attribute_completion ? reader->attribute_completion : report->completion;
        }
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
    else if (check_input_attribute_equal(name, "no_caller_saved_registers"))
    {
        result = CHECK_CALL_NO_CALLER_SAVED;
    }
    else if (check_input_attribute_equal(name, "intel_ocl_bicc"))
    {
        result = CHECK_CALL_INTEL_OCL;
    }
    else if (check_input_attribute_equal(name, "swiftcall"))
    {
        result = CHECK_CALL_SWIFT;
    }
    else if (check_input_attribute_equal(name, "swiftasynccall"))
    {
        result = CHECK_CALL_SWIFT_ASYNC;
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
            // A NEON vector's count is checked with its element type (check_input_neon_vector, #1447).
            valid = attribute->neon != 0 || (value > 0 && (uint64_t)value <= BUSTER_TYPE_MAX_VECTOR_BITS);
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || attribute->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE)
        {
            // Checked with its element type (check_input_vector_size, #147), as clang's BuildVectorType checks it; an
            // address space where its type takes it (check_input_address_space, #1447).
            valid = true;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_REGPARM)
        {
            // Clang's x86-64 target accepts all six integer argument registers here; i386's limit is three.
            valid = value >= 0 && value <= 6;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_PASS_OBJECT_SIZE || attribute->kind == CHECK_ATTRIBUTE_PASS_DYNAMIC_OBJECT_SIZE)
        {
            valid = value >= 0 && value <= 3;
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
        else if (kind == CHECK_ATTRIBUTE_ALIGNED || kind == CHECK_ATTRIBUTE_ALIGNAS)
        {
            // A power of 2 past 2^32 (#1447), clang's MaxAlignment's.
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_ALIGNMENT_TOO_LARGE, 0);
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
        const bool string = attribute->schema == CHECK_ATTRIBUTE_STRING || attribute->schema == CHECK_ATTRIBUTE_STRING_LIST ||
                            (attribute->schema == CHECK_ATTRIBUTE_CONDITION_STRINGS && argument != 0);
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
                const CheckInputAttributeError saved = attributes->error;
                const uint32_t saved_first = attributes->error_first;
                const uint32_t saved_end = attributes->error_end;
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, argument + 1);
                // This is handleFormatAttr's check, after the subject, rather than a parser constraint. A variable's
                // format(1, 2, 3) therefore gets only the subject warning, just as format(printf, 2, 3) does.
                valid = check_input_attribute_defer(reader, attributes, attribute, before, saved, saved_first, saved_end);
            }
            else if (!valid && (attribute->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || attribute->kind == CHECK_ATTRIBUTE_CPU_DISPATCH))
            {
                // handleCPUSpecificAttr's check, after the subject, at the attribute (#1447), as format's.
                const CheckInputAttributeError saved = attributes->error;
                const uint32_t saved_first = attributes->error_first;
                const uint32_t saved_end = attributes->error_end;
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_IDENTIFIER, 0);
                valid = check_input_attribute_defer(reader, attributes, attribute, before, saved, saved_first, saved_end);
            }
        }
        else if (valid && string)
        {
            String8 text = { 0 };
            uint32_t string_first = first;
            uint32_t string_end = end;
            // Clang delays these attributes' arguments as expressions: glibc parenthesizes the message. suppress's are
            // expressions too, whose string literals its handler takes through parentheses (checkStringLiteralArgumentAttr's
            // IgnoreParenCasts: `suppress(("a"))`).
            while ((attribute->schema == CHECK_ATTRIBUTE_CONDITION_STRINGS || attribute->kind == CHECK_ATTRIBUTE_SUPPRESS) && string_first + 1 < string_end &&
                   parser->tokens.kinds[string_first] == PARSE_KIND_L_PAREN && parser->tree.parents[string_first] == string_end - 1)
            {
                string_first += 1;
                string_end -= 1;
            }
            const bool decoded = check_input_attribute_string(reader, string_first, string_end, attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL, &text);
            valid = decoded;
            error = CHECK_ATTRIBUTE_ERROR_STRING;
            // diagnose_if's diagnostic type, and its warning group (#1447, clang's handleDiagnoseIfAttr): its error at the
            // string. A group clang knows names the warning's option (CheckInputAttribute.warning_group).
            if (valid && attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF && argument == 2 &&
                !check_input_attribute_equal(text, "warning") && !check_input_attribute_equal(text, "error"))
            {
                check_input_attribute_argument_error(reader, attributes, attribute, string_first, CHECK_ARGUMENT_DIAGNOSTIC_TYPE, 0);
                valid = false;
            }
            if (valid && attribute->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF && argument == 3)
            {
                char group[128];
                const bool fits = text.length != 0 && text.length < sizeof(group) && memchr(text.pointer, 0, text.length) == NULL;
                if (fits)
                {
                    memcpy(group, text.pointer, text.length);
                    group[text.length] = 0;
                }
                const uint32_t index = fits ? pp_warning_group_index(group) : UINT32_MAX;
                if (index == UINT32_MAX)
                {
                    check_input_attribute_argument_error(reader, attributes, attribute, string_first, CHECK_ARGUMENT_WARNING_GROUP, 0);
                }
                attribute->warning_group = index != UINT32_MAX ? (uint16_t)(index + 1) : 0;
                valid = index != UINT32_MAX;
            }
            if (valid && argument == 0)
            {
                attribute->text = text;
            }
            // clang's error of an argument that is no string literal, at it (#147): visibility's own wording, the others'
            // checkStringLiteralArgumentAttr's.
            // visibility's and pcs's arguments are enumerators', which clang's parser reads as identifiers: one is "requires a
            // string". pcs's is its handler's, once a function or a tag takes it (check_input_attribute_pcs): on another type
            // clang's type processing never reads the argument, and the attribute's warning is all.
            // An asm label's string literal that refuses its escapes is the literal's error (#1447, clang's literal parser's
            // escape errors: check_driver_literal_escapes).
            const bool label_literal = !decoded && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !reader->output->resource &&
                                       string_first < string_end && parser->tokens.kinds[string_first] == PARSE_KIND_STRING;
            if (label_literal && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
            {
                // (The attribute's error, which then gives no other.)
                attributes->error = CHECK_ATTRIBUTE_ERROR_STRING;
                attributes->error_first = string_first;
                attributes->error_end = string_end;
                check_input_report(reader, string_first, CHECK_INPUT_LITERAL);
            }
            else if (!decoded)
            {
                const bool named = first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
                const bool pcs = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION &&
                                 check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "pcs");
                const bool enumerated = attribute->kind == CHECK_ATTRIBUTE_VISIBILITY || pcs;
                const CheckInputAttributeError saved = attributes->error;
                const uint32_t saved_first = attributes->error_first;
                const uint32_t saved_end = attributes->error_end;
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, first, enumerated && named ? CHECK_ARGUMENT_STRING : CHECK_ARGUMENT_STRING_LITERAL, 0);
                valid = pcs && named && check_input_attribute_defer(reader, attributes, attribute, before, saved, saved_first, saved_end);
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
// argument whose errors need the expression reader (a type's, a call's, a builtin's or an empty one), which reads it
// before the handler counts it. Linear over the arguments' tokens: only an attribute whose count is
// wrong (or cleanup's) gets here.
#define BUSTER_CHECK_PARSED_CLEAN 0
#define BUSTER_CHECK_PARSED_ERROR 1
#define BUSTER_CHECK_PARSED_UNSURE 2


// Where suppress's argument from `argument` to `stop` (its `,` or the list's end) is no string literal (#1844), or
// BUSTER_CHECK_NONE: clang's parser reads each of its string literals through any number of parentheses
// (`suppress((("a")))`), and its "expected string literal" is at the first token inside them that starts none. What
// follows a whole literal that is no `)` is the parser's error.
static uint32_t check_input_attribute_suppress_literal(const Parser* parser, uint32_t argument, uint32_t stop)
{
    uint32_t token = argument;
    while (token + 1 < stop && parser->tokens.kinds[token] == PARSE_KIND_L_PAREN)
    {
        token += 1;
    }
    return token < stop && parser->tokens.kinds[token] == PARSE_KIND_STRING ? BUSTER_CHECK_NONE : token;
}

// Whether a name is suppress's, as written with its keyword underscores or without.
static bool check_input_attribute_suppress_name(String8 name)
{
    return check_input_attribute_equal(name, "suppress") || check_input_attribute_equal(name, "__suppress__");
}

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
            literal &= kind == PARSE_KIND_STRING || (attribute->kind == CHECK_ATTRIBUTE_SUPPRESS && (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_R_PAREN));
            const String8 spelled = kind == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, parser->tokens.names[token]) : (String8){ 0 };
            const uint8_t previous = token > 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
            const uint8_t next = token + 1 < stop ? parser->tokens.kinds[token + 1] : PARSE_KIND_END;
            unsure |= kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                      kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_ATOMIC ||
                      kind == PARSE_KIND_GENERIC || kind == PARSE_KIND_TYPE_ARGUMENT || (kind == PARSE_KIND_IDENTIFIER && next == PARSE_KIND_L_PAREN) ||
                      (spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0);
            const bool named = previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW ||
                               c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) != 0;
            undeclared = undeclared == BUSTER_CHECK_NONE && kind == PARSE_KIND_IDENTIFIER && !named ? token : undeclared;
        }
        if (identifier)
        {
        }
        else if (string && first < end && (attribute->kind == CHECK_ATTRIBUTE_SUPPRESS ? check_input_attribute_suppress_literal(parser, first, end) != BUSTER_CHECK_NONE :
                                                                                         parser->tokens.kinds[first] != PARSE_KIND_STRING))
        {
            const uint32_t at = attribute->kind == CHECK_ATTRIBUTE_SUPPRESS ? check_input_attribute_suppress_literal(parser, first, end) : first;
            check_input_attribute_argument_error(reader, attributes, attribute, at, CHECK_ARGUMENT_STRING_LITERAL, 0);
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
#define BUSTER_CHECK_LATE_USED (1u << 3)
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
            case CHECK_ATTRIBUTE_USED: bit = BUSTER_CHECK_LATE_USED; break;
            case CHECK_ATTRIBUTE_RETAIN: bit = 1u << 4; break;
            case CHECK_ATTRIBUTE_GNU_INLINE: bit = 1u << 5; break;
            case CHECK_ATTRIBUTE_NORETURN: bit = 1u << 6; break;
            default: break;
        }
    }
    return bit;
}

// The late classes (check_input_attribute_late_bit) a declaration's `attributes` give their entity: an interrupt's implicit
// used too (#147), after which a used that comes after the definition is no new attribute.
static uint8_t check_input_attribute_late_mask(const CheckInputAttributes* attributes, uint8_t function_flags)
{
    uint8_t mask = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool missing_inline = attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE && (function_flags & CHECK_INPUT_FUNCTION_INLINE) == 0;
        const bool interrupt = attribute->kind == CHECK_ATTRIBUTE_INTERRUPT && !attribute->subject_ignored;
        mask |= !attribute->ignored && !missing_inline ? check_input_attribute_late_bit(attribute) | (interrupt ? BUSTER_CHECK_LATE_USED : 0u) : 0u;
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
// An object with external linkage, including a block-scope extern declaration.
#define BUSTER_CHECK_SUBJECT_EXTERNAL_OBJECT 0x2000000u

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
// as rows, in byte order (check_input_subject_row): GCC's, GNU's spelling and gnu::'s, x86's interrupt's (#147, a
// target's attribute of no generated row: handleAnyX86InterruptAttr's, a function's or a function pointer's with a
// prototype), x86's force_align_arg_pointer's (#1447, handleForceAlignArgPointerAttr's, a function's, which ignores a
// function pointer or a typedef of a function without a word: check_input_attribute_subjects), handleSentinelAttr's
// and handleTransparentUnionAttr's; and clang's own, GNU's spelling and clang::'s,
// handleWeakImportAttr's (#147), whose definitions it ignores with its own warning (check_input_attribute_subjects).
static const CheckInputSubjectRow check_input_subject_handlers[] =
{
    { "force_align_arg_pointer", 23, BUSTER_CHECK_SUBJECT_FUNCTION, false, "functions" },
    { "interrupt", 9, BUSTER_CHECK_SUBJECT_PROTOTYPED, false, "non-K&R-style functions" },
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

// Whether a generic attribute's name, without a keyword's underscores, is one of the type tags' rules (#1447,
// c_check_type_safety_input.c): argument_with_type_tag's or pointer_with_type_tag's.
static bool check_input_type_tag_rule(String8 name)
{
    return check_input_attribute_equal(name, "argument_with_type_tag") || check_input_attribute_equal(name, "pointer_with_type_tag");
}

// What clang's parser makes of a custom attribute's arguments (#1447): an error, the attribute kept, or dropped without a
// word.
#define BUSTER_CHECK_CUSTOM_ERROR 0u
#define BUSTER_CHECK_CUSTOM_KEPT 1u
#define BUSTER_CHECK_CUSTOM_DROPPED 2u

static uint32_t check_input_availability_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);
static uint32_t check_input_external_source_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);
static uint32_t check_input_type_tag_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);
static uint32_t check_input_objc_bridge_related_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);
static uint32_t check_input_swift_newtype_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);
static bool check_input_type_tag_handled(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration, TypeId type);
static bool check_input_type_tag_declaration(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t previous,
                                             uint32_t initializer);
static bool check_input_type_tag_finish(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end);
struct CheckTypeTagPending;
static struct CheckTypeTagPending* check_type_tags_pending(const CheckInput* input, uint32_t declaration);
static bool check_input_availability_handle(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute);

static const CheckInputFactRow* check_input_attribute_gnu_fact(CheckInputReader* reader, uint32_t name);
static const CheckInputArgumentsRow* check_input_attribute_gnu_arguments(CheckInputReader* reader, uint32_t name);

// clang's parser's reading of attributes it applies nothing of (#147): a free-standing declaration's specifiers' (clang's
// ParsedFreeStandingDeclSpec), and a bare tag declaration's after its name. Of each GNU list's items and each alignment
// specifier's argument from `first` to `end` (a tag's tokens skipped, CheckInputReader.tag_ends), only what clang's parser
// checks: an argument that must be a string literal (CheckInputArgumentsRow.strings), "expected string literal as argument
// of 'X' attribute", or the escape sequences it refuses in one (check_input_unevaluated_strings), and a name in an argument
// read as an expression that names nothing, "use of undeclared identifier"
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
                const CheckInputArgumentsRow* row = gnu && !comma ? check_input_attribute_gnu_arguments(reader, parser->tokens.names[name]) : NULL;
                const bool known = gnu && !comma && check_input_attribute_gnu_fact(reader, parser->tokens.names[name]) != NULL;
                const uint32_t flags = row != NULL ? row->flags : 0;
                const uint32_t strings = row != NULL ? row->strings : 0;
                const bool typed = (flags & BUSTER_CHECK_ARGUMENTS_TYPE) != 0 || (alignas && arguments_first < arguments_end &&
                                                                                 check_constant_type_start(parser, arguments_first));
                // availability's, external_source_symbol's, type_tag_for_datatype's and objc_bridge_related's arguments, which
                // clang's parser reads its own way (#1447).
                const String8 spelled = known && argued ? check_input_attribute_name(reader, parser->tokens.names[name]) : (String8){ 0 };
                const bool availability = check_input_attribute_equal(spelled, "availability");
                const bool type_tag = check_input_attribute_equal(spelled, "type_tag_for_datatype");
                const bool related = check_input_attribute_equal(spelled, "objc_bridge_related");
                const bool newtype = check_input_attribute_equal(spelled, "swift_newtype") || check_input_attribute_equal(spelled, "swift_wrapper");
                const bool custom = availability || type_tag || related || newtype || check_input_attribute_equal(spelled, "external_source_symbol");
                if (pass == 0 && valid && custom)
                {
                    CheckInputAttributes holder = { 0 };
                    CheckInputAttribute item = {
                        .first = name,
                        .name_token = name,
                        .name = parser->tokens.names[name],
                        .end = items_end,
                        .argument_first = arguments_first,
                        .argument_end = arguments_end,
                    };
                    valid = (availability ? check_input_availability_parse(reader, &holder, &item) :
                             type_tag ? check_input_type_tag_parse(reader, &holder, &item) :
                             related ? check_input_objc_bridge_related_parse(reader, &holder, &item) :
                             newtype ? check_input_swift_newtype_parse(reader, &holder, &item) : check_input_external_source_parse(reader, &holder, &item)) !=
                            BUSTER_CHECK_CUSTOM_ERROR;
                }
                uint32_t index = 0;
                const bool suppress = check_input_attribute_suppress_name(check_input_attribute_name(reader, parser->tokens.names[name]));
                for (uint32_t argument = arguments_first; pass == 0 && valid && !typed && !custom && argument < arguments_end; index += 1)
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
                    if (string && !identifier && check_input_unevaluated_strings(reader, argument, stop, BUSTER_CHECK_NONE))
                    {
                        // An escape sequence clang refuses in it, its error: then clang drops the attribute.
                        valid = false;
                    }
                    else if (string && !identifier && !literals &&
                             (!suppress || check_input_attribute_suppress_literal(parser, argument, stop) != BUSTER_CHECK_NONE))
                    {
                        CheckInputAttributes holder = { 0 };
                        const CheckInputAttribute item = { .first = name, .name_token = name, .name = parser->tokens.names[name], .end = items_end };
                        const uint32_t at = suppress ? check_input_attribute_suppress_literal(parser, argument, stop) : argument;
                        check_input_attribute_argument_error(reader, &holder, &item, at, CHECK_ARGUMENT_STRING_LITERAL, 0);
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
    const CheckInputFactRow* fact = attribute->standard || attribute->keyword ? NULL : check_input_attribute_gnu_fact(reader, attribute->name);
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
// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) uint32_t check_input_type_subjects(const TypeTable* types, TypeId type)
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

_Static_assert(BUSTER_ARRAY_LENGTH(check_input_attribute_rules) < UINT16_MAX && BUSTER_ARRAY_LENGTH(check_input_fact_gnu) < UINT16_MAX &&
                   BUSTER_ARRAY_LENGTH(check_input_arguments_rows) < UINT16_MAX,
               "a GNU name's rows are 16-bit indices");

// The rows of the GNU attribute name `name` (an IdentId, as check_input_attribute_name reads it): its rule's, its facts', its
// subjects' and its arguments' rows and whether clang knows it, each what its lookup gives a GNU spelling, which no scope
// changes. They are kept by the name in the reader's slots (CheckInputReader.gnu_rows), as a unit's attributes repeat a
// few names: glibc's headers give most of sqlite3.c's prototypes __nothrow__, __leaf__ and __nonnull__, and the lookups'
// binary searches, a string comparison a step, took 1.3 ms of its -c on the 9700X (#1489). A slot another name takes
// is looked up again.
static const CheckInputGnuRows* check_input_attribute_gnu_rows(CheckInputReader* reader, uint32_t name)
{
    CheckInputGnuRows* slot = reader->gnu_rows + ((name * UINT32_C(0x9e3779b1)) >> (32 - BUSTER_CHECK_GNU_ROW_BITS));
    if (slot->key != name + 1)
    {
        const String8 text = check_input_attribute_name(reader, name);
        // A calling convention's keyword (`__cdecl`) is spelled with its underscores (check_input_attribute_read_item).
        const bool keyword = name >= C_NAME_CDECL && name <= C_NAME_VECTORCALL && reader->pp != NULL && text.length + 2 == reader->pp->table->spans[name].length;
        const String8 spelled = keyword ? (String8){ text.pointer - 2, text.length + 2 } : text;
        uint16_t rule = 0;
        for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(check_input_attribute_rules) && rule == 0; r += 1)
        {
            rule = check_input_attribute_equal(text, check_input_attribute_rules[r].name) ? (uint16_t)(r + 1) : 0;
        }
        const CheckInputFactRow* fact = check_input_fact_row((String8){ "", 0 }, text, false, false);
        const CheckInputArgumentsRow* arguments = check_input_arguments_row(text);
        *slot = (CheckInputGnuRows){
            .key = name + 1,
            .rule = rule,
            .fact = fact != NULL ? (uint16_t)(fact - check_input_fact_gnu + 1) : 0,
            .arguments = arguments != NULL ? (uint16_t)(arguments - check_input_arguments_rows + 1) : 0,
            .known = check_input_attribute_clang_knows((String8){ "", 0 }, spelled, false, false),
            .subject = check_input_subject_row((String8){ "", 0 }, text, false, false),
        };
    }
    return slot;
}

// A GNU attribute name's facts' row (check_input_fact_row's), or null.
static const CheckInputFactRow* check_input_attribute_gnu_fact(CheckInputReader* reader, uint32_t name)
{
    const CheckInputGnuRows* rows = check_input_attribute_gnu_rows(reader, name);
    return rows->fact != 0 ? check_input_fact_gnu + rows->fact - 1 : NULL;
}

// A GNU attribute name's arguments' row (check_input_arguments_row's), or null.
static const CheckInputArgumentsRow* check_input_attribute_gnu_arguments(CheckInputReader* reader, uint32_t name)
{
    const CheckInputGnuRows* rows = check_input_attribute_gnu_rows(reader, name);
    return rows->arguments != 0 ? check_input_arguments_rows + rows->arguments - 1 : NULL;
}

// An attribute's subjects' row (check_input_subject_row's): a GNU one's by its name's slot.
static const CheckInputSubjectRow* check_input_attribute_subject_of(CheckInputReader* reader, String8 scope, uint32_t name, bool scoped, bool standard)
{
    return standard ? check_input_subject_row(scope, check_input_attribute_name(reader, name), scoped, standard) : check_input_attribute_gnu_rows(reader, name)->subject;
}

// An attribute's facts' row (check_input_fact_row's): a GNU one's by its name's slot.
static const CheckInputFactRow* check_input_attribute_fact_of(CheckInputReader* reader, String8 scope, uint32_t name, bool scoped, bool standard)
{
    return standard ? check_input_fact_row(scope, check_input_attribute_name(reader, name), scoped, standard) : check_input_attribute_gnu_fact(reader, name);
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
            .completion = token,
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
#define BUSTER_CHECK_PLACE_GROUP 5u

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
    return group ? BUSTER_CHECK_PLACE_GROUP : qualifiers ? BUSTER_CHECK_PLACE_QUALIFIERS : role == PARSE_ROLE_CLOSE_FUNCTION ? BUSTER_CHECK_PLACE_FUNCTION : chunk ? BUSTER_CHECK_PLACE_CHUNK :
           specifiers && open > 0 ? BUSTER_CHECK_PLACE_SPECIFIERS : BUSTER_CHECK_PLACE_DECLARATION;
}

// Whether the C23 lists from `open` (the first's outer `[`) of a tag's own list are a tag declaration's or definition's
// (#1447): past them, its name if it has one, then `;` or its body's `{`; not a reference's.
// Whether the declaration whose C23 attribute list opens at `open` (its first `[`) is a typedef's (#1447): a walk back over its tokens, past
// bracketed groups and a record's or an enumeration's body, to its start, for the `typedef` keyword.
static bool check_input_attribute_typedef(const Parser* parser, uint32_t open)
{
    bool found = false;
    bool walking = true;
    uint32_t depth = 0;
    for (uint32_t at = open; walking && !found && at > 0; at -= 1)
    {
        const uint8_t kind = parser->tokens.kinds[at - 1];
        const uint8_t role = parser->tree.roles[at - 1];
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || (kind == PARSE_KIND_R_BRACE && (role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM));
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        walking = depth != 0 || (kind != PARSE_KIND_SEMI && !opener && (kind != PARSE_KIND_R_BRACE || closer));
        depth += closer ? 1u : 0u;
        depth -= opener && depth != 0 ? 1u : 0u;
        found = walking && depth == 0 && parser->tokens.names[at - 1] == C_NAME_TYPEDEF;
    }
    return found;
}

static bool check_input_attribute_tag_declares(const Parser* parser, uint32_t open)
{
    const uint32_t count = parser->tokens.count;
    uint32_t at = open;
    while (at + 1 < count && parser->tokens.kinds[at] == PARSE_KIND_L_SQUARE && parser->tokens.kinds[at + 1] == PARSE_KIND_L_SQUARE)
    {
        uint32_t depth = 0;
        bool scanning = true;
        while (scanning && at < count)
        {
            const uint8_t kind = parser->tokens.kinds[at];
            depth = kind == PARSE_KIND_L_SQUARE ? depth + 1 : kind == PARSE_KIND_R_SQUARE && depth != 0 ? depth - 1 : depth;
            scanning = depth != 0;
            at += 1;
        }
    }
    at += at < count && parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER;
    return at < count && (parser->tokens.kinds[at] == PARSE_KIND_SEMI || parser->tokens.kinds[at] == PARSE_KIND_L_BRACE);
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
static void check_input_attribute_named_once(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningEventKind kind, uint32_t completion)
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
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, kind, token, completion);
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

static bool check_input_attribute_pcs(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, bool tag)
{
    const bool pcs = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "pcs");
    const bool counted = !pcs || attribute->argument_count == 1;
    const bool known = !counted || check_input_attribute_pcs_valid(reader, attribute);
    const CheckInputReport* deferred = pcs ? attribute->deferred : NULL;
    if (deferred != NULL)
    {
        // An identifier for its string (check_input_attribute_validate): "requires a string", given once. clang's
        // checkStringLiteralArgumentAttr goes on with the identifier's text, which the driver words after the error: aapcs's
        // convention x86-64 warns of and ignores, any other's "invalid PCS type".
        CheckInput* output = reader->output;
        if (output->report_count < output->report_capacity)
        {
            // (A tag's, whose aapcs clang then also finds no function's: `value` 1.)
            output->reports[output->report_count] = *deferred;
            output->reports[output->report_count++].value = tag ? 1u : 0u;
        }
        output->resource |= output->report_count >= output->report_capacity && output->reports[output->report_count - 1].token != deferred->token;
        attributes->error = attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? CHECK_ATTRIBUTE_ERROR_ARGUMENTS : attributes->error;
        attribute->deferred = NULL;
    }
    else if (!counted && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
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
    return deferred == NULL && counted && known;
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
// and types are the scope's there (check_input_thread_names). Their warnings (-Wthread-safety-attributes) are off by
// default and retained as sparse events. Linear catalog lookup: an attribute is rare.
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
// handler applies with nothing more (its facts' SIMPLE), no type's or statement's alone, except selectany (handled by its
// explicit rule);
// and these whose own handlers check what is checked here: minsize, optnone (check_input_attribute_optnone), noescape
// (a pointer parameter's, check_input_attribute_subjects), annotate (a string first, check_input_attribute_read_item),
// uninitialized, no_sanitize_memory, analyzer_noreturn and carries_dependency; the
// thread-safety analysis's (check_input_thread_rows); one of no C subject (`subjectless`: Objective-C's, C++'s and the like,
// whose subject's diagnostic is all clang gives in C); and these, whose handlers change nothing in C past their subjects,
// counts and arguments' kinds (#147): btf_decl_tag (BPF's debug information), randomize_layout and no_randomize_layout (without
// -frandomize-layout-seed), Objective-C's boxable and bridge_mutable, Fuchsia's handles (the analyzer's), suppress, Swift's
// bridge, unsafe_buffer_usage and AMDGPU's max_num_work_groups (a kernel's alone); and clang's checks-only attributes
// (#1447: enforce_tcb, enforce_tcb_leaf, callback, swift_async, ns_error_domain, modular_format and diagnose_as_builtin),
// whose handlers check what check_input_attribute_checks does and change nothing in C past TCB's warnings at calls.
static bool check_input_attribute_generic(String8 name, const CheckInputFactRow* fact, bool subjectless)
{
    static const char* const unchanged[] = {
        // Its resolved typedef uses are checked after expression reading, including constant-only type queries.
        "available_only_in_default_eval_method",
        "btf_decl_tag", "randomize_layout", "no_randomize_layout", "objc_boxable", "objc_bridge_mutable",
        "objc_bridge", "objc_bridge_related", "swift_newtype", "swift_wrapper", "swift_async_error",
        "swift_name", "swift_async_name", "swift_error", "cfi_salt", "release_handle", "use_handle", "suppress", "swift_bridge", "unsafe_buffer_usage", "amdgpu_max_num_work_groups",
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
        "arm_sve_vector_bits", "riscv_rvv_vector_bits", "clang_builtin_alias", "builtin_alias", "likely", "unlikely", "atomic", "code_align", "opencl_unroll_hint", "blocks", "assume",
        // A bit-field's preferred type, whose type check_input_attribute_annotations checks; MSVC's forceinline and
        // forceinline_calls, which it ignores off MSVC's ABI.
        "preferred_type", "forceinline", "forceinline_calls",
        // clang's checks-only attributes (#1447), whose handlers' checks are check_input_attribute_checks', their
        // redeclarations' check_input_attribute_merge_checks' and TCB's calls check_input_tcb_calls'.
        "enforce_tcb", "enforce_tcb_leaf", "callback", "swift_async", "ns_error_domain", "modular_format", "availability", "external_source_symbol",
        "diagnose_as_builtin",
        // Objective-C's pointer type attributes, which change no C type but objc_gc's qualifier (check_input_attribute_objc).
        "objc_gc", "objc_ownership",
        // The type tags (#1447), whose handlers' checks are check_input_type_tag_handled's and whose calls' warnings are
        // check_common_type_safety's.
        "argument_with_type_tag", "pointer_with_type_tag", "type_tag_for_datatype",
        // A type's annotation (#1447), sugar that changes no type: its string's and its constants' checks are annotate's
        // (check_input_attribute_annotate).
        "annotate_type",
    };
    const bool simple = fact != NULL && (fact->flags & (BUSTER_CHECK_FACT_SIMPLE | BUSTER_CHECK_FACT_TYPE)) == BUSTER_CHECK_FACT_SIMPLE;
    const bool effect = check_input_attribute_equal(name, "selectany");
    bool handled = check_input_attribute_equal(name, "minsize") || check_input_attribute_equal(name, "optnone") ||
                   check_input_attribute_equal(name, "noescape") || check_input_attribute_equal(name, "annotate") ||
                   check_input_attribute_equal(name, "uninitialized") ||
                   check_input_attribute_equal(name, "no_sanitize_memory") || check_input_attribute_equal(name, "analyzer_noreturn") ||
                   check_input_attribute_equal(name, "carries_dependency") || check_input_thread_row(name) != NULL || subjectless;
    for (uint32_t i = 0; !handled && i < BUSTER_ARRAY_LENGTH(unchanged); i += 1)
    {
        handled = check_input_attribute_equal(name, unchanged[i]);
    }
    return fact != NULL && ((simple && !effect) || handled);
}

// The closer of the type name containing the attribute at `first`, or BUSTER_CHECK_NONE (#147): the group around it,
// past its list's parentheses and a declarator's grouping ones, is a cast's, a sizeof's or alignof's, a compound literal's,
// a specifier's (typeof, _Atomic, _Alignas)
// or a keyword's type argument (va_arg, offsetof, _Generic's association). A parameter's list (a function
// declarator's), a block, a statement, a declaration's end or a tag's own list is none. A scan back over at most 256
// tokens.
static uint32_t check_input_attribute_type_name_end(const Parser* parser, uint32_t first)
{
    uint32_t depth = 0;
    uint32_t i = first;
    uint32_t result = BUSTER_CHECK_NONE;
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
        const bool type_name = enclosing && !tag && (role == PARSE_ROLE_OPEN_CAST || role == PARSE_ROLE_OPEN_SIZEOF || role == PARSE_ROLE_OPEN_COMPOUND ||
                                                    role == PARSE_ROLE_OPEN_SPECIFIER || role == PARSE_ROLE_OPEN_KEYWORD);
        const uint32_t closer = type_name ? parser->tree.parents[i] : BUSTER_CHECK_NONE;
        result = closer > first && closer < parser->tokens.count ? closer : result;
        depth = close ? depth + 1 : open && depth != 0 ? depth - 1 : depth;
        scanning = !stop && !enclosing;
    }
    return result;
}

static bool check_input_attribute_in_type_name(const Parser* parser, uint32_t first)
{
    return check_input_attribute_type_name_end(parser, first) != BUSTER_CHECK_NONE;
}

// clang's parser after a C23 standard attribute's arguments failed (#147, ParseCXX11AttributeArgs, deprecated's or
// nodiscard's escape sequence): it checks the last attribute its specifier sequence parsed before, by that one's
// argument count, as if it had been given the failed one's empty list, "parentheses must be omitted if 'X' attribute's
// argument list is empty" where it takes arguments and "attribute 'X' cannot have an argument list" where it takes none
// (a C23 attribute clang doesn't know takes none), and makes that one invalid. The sequence is the item's list, from
// `list_first` (its first item's token) to `item`, and the lists right before it; an attribute an escape sequence
// dropped (its report) isn't parsed. Returns 0 where there is none, 1 where it takes no arguments, 2 where it takes some,
// with its first token in `*previous`. Cold: only a dropped standard attribute asks.
static uint32_t check_input_attribute_standard_previous(CheckInputReader* reader, uint32_t list_first, uint32_t item, uint32_t* previous)
{
    const Parser* parser = reader->parser;
    const uint8_t* kinds = parser->tokens.kinds;
    // The sequence's first list's first item: back over adjacent `[[ ... ]]`, their brackets balanced.
    uint32_t first = list_first;
    bool earlier = first >= 4;
    while (earlier)
    {
        const uint32_t open = first - 2;
        earlier = open >= 2 && kinds[open - 1] == PARSE_KIND_R_SQUARE && kinds[open - 2] == PARSE_KIND_R_SQUARE;
        uint32_t depth = 0;
        uint32_t t = open;
        do
        {
            t -= 1;
            depth += kinds[t] == PARSE_KIND_R_SQUARE;
            depth -= depth != 0 && kinds[t] == PARSE_KIND_L_SQUARE;
        } while (earlier && t > 0 && depth != 0);
        earlier &= depth == 0 && kinds[t] == PARSE_KIND_L_SQUARE && t + 2 < open && kinds[t + 1] == PARSE_KIND_L_SQUARE;
        first = earlier ? t + 2 : first;
        earlier &= first >= 4;
    }
    uint32_t result = 0;
    uint32_t t = first;
    while (t < item)
    {
        const uint8_t kind = kinds[t];
        const bool separator = kind == PARSE_KIND_COMMA || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_R_SQUARE || parser->tokens.names[t] == 0;
        const bool scoped = !separator && t + 3 < item && kinds[t + 1] == PARSE_KIND_COLON && kinds[t + 2] == PARSE_KIND_COLON;
        const uint32_t name = scoped ? t + 3 : t;
        const bool argued = !separator && name + 1 < item && kinds[name + 1] == PARSE_KIND_L_PAREN;
        const uint32_t close = argued ? parser->tree.parents[name + 1] : name;
        bool dropped = false;
        for (uint32_t r = 0; argued && r < reader->output->report_count; r += 1)
        {
            const CheckInputReport* report = reader->output->reports + r;
            dropped |= report->message == CHECK_INPUT_UNEVALUATED_ESCAPE && report->token > name + 1 && report->token < close;
        }
        if (!separator && !dropped)
        {
            const String8 scope = scoped ? check_input_attribute_name(reader, parser->tokens.names[t]) : (String8){ 0 };
            const String8 spelled = check_input_attribute_name(reader, parser->tokens.names[name]);
            const bool known = check_input_attribute_clang_knows(scope, spelled, scoped, true);
            const CheckInputFactRow* fact = known ? check_input_fact_row(scope, spelled, scoped, true) : NULL;
            result = known && (fact == NULL || fact->maximum != 0) ? 2 : 1;
            *previous = t;
        }
        t = separator ? t + 1 : close > name && close < item ? close + 1 : name + 1;
    }
    return result;
}

// One attribute's handler's errors, which phase 1 of #1447's semantic recovery can carry past (check_input_attribute_recover):
// the attribute, the unit's report count and its declaration's first attribute error before the handler ran.
typedef struct CheckInputAttributeRecovery CheckInputAttributeRecovery;
struct CheckInputAttributeRecovery
{
    CheckInputAttribute* attribute;
    uint32_t reports;
    CheckInputAttributeError error;
    uint32_t error_first;
    uint32_t error_end;
};

// Whether an attribute's error is one clang's handleDeclAttribute gives and then returns from without adding the attribute,
// which leaves the declaration valid: its arguments' (their values, counts, strings and parameter indices), its subject's,
// a mutual exclusion's and x86's interrupt handler's (#1447); and a vector type's element or size, which clang's
// HandleVectorSizeAttr and HandleNeonVectorTypeAttr give and then leave the type as it was (check_input_attribute_type).
// Others stop the reader as before: an undeclared name's, a pragma's, conflicting calling conventions' (a type's, which
// clang makes invalid) and a list's place (its parser's).
static bool check_input_attribute_recoverable(const CheckInputReport* report)
{
    const CheckInputMessage message = report->message;
    const bool attribute = message == CHECK_INPUT_ATTRIBUTE_ARGUMENT || message == CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT ||
                           message == CHECK_INPUT_ATTRIBUTE_SUBJECT || message == CHECK_INPUT_ATTRIBUTE_OUT_OF_BOUNDS ||
                           message == CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS || message == CHECK_INPUT_ATTRIBUTE_EXCLUSION ||
                           message == CHECK_INPUT_ATTRIBUTE_ANNOTATE_STRING || message == CHECK_INPUT_ATTRIBUTE_STRING_ARGUMENT ||
                           message == CHECK_INPUT_ATTRIBUTE_INT_OR_BOOL || message == CHECK_INPUT_ATTRIBUTE_TYPES ||
                           message == CHECK_INPUT_INTERRUPT_HANDLER;
    const bool vector = message == CHECK_INPUT_VECTOR_ELEMENT || message == CHECK_INPUT_VECTOR_BIT_INT || message == CHECK_INPUT_VECTOR_CONSTANT ||
                        message == CHECK_INPUT_VECTOR_NEGATIVE || message == CHECK_INPUT_VECTOR_TOO_LARGE || message == CHECK_INPUT_VECTOR_ZERO ||
                        message == CHECK_INPUT_VECTOR_MULTIPLE;
    return (attribute || vector) && report->pragma == 0;
}

// Whether the errors an attribute's reading or handler gave since `recovery` moved past (#1447): each is one clang gives and
// then drops the attribute for (check_input_attribute_recoverable), and they move to the unit's recovered reports
// (check_input_recover_reports), each kept at its token where another is (CheckInputReport.repeated: each declarator's run of
// a shared record's handler is clang's error again). The declaration's first attribute error is as it was before them, so
// that a later attribute's error is reported too, as clang's is. With `completion`, a subject check's boundary, they take
// it as the subject checks' reports do (check_input_attribute_subjects).
static bool check_input_attribute_errors_recover(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttributeRecovery* recovery,
                                                 uint32_t completion)
{
    CheckInput* input = reader->output;
    bool recoverable = input->report_count > recovery->reports && !input->resource;
    for (uint32_t r = recovery->reports; recoverable && r < input->report_count; r += 1)
    {
        recoverable = check_input_attribute_recoverable(input->reports + r);
    }
    for (uint32_t r = recovery->reports; recoverable && r < input->report_count; r += 1)
    {
        CheckInputReport* report = input->reports + r;
        if (completion != 0)
        {
            report->suborder = (uint64_t)(report->sort != 0 ? report->sort : report->completion != 0 ? report->completion : report->token) + 1;
            report->completion = report->completion < completion ? completion : report->completion;
        }
        report->repeated = true;
        // Without a boundary, it waits for its declarator's (CheckInputReport.pending).
        report->pending = completion == 0;
    }
    const bool recovered = recoverable && check_input_recover_reports(reader, recovery->reports);
    if (recovered)
    {
        attributes->error = recovery->error;
        attributes->error_first = recovery->error_first;
        attributes->error_end = recovery->error_end;
    }
    return recovered;
}

// The state check_input_attribute_errors_recover restores, before an attribute's reading or handler (`attribute`, NULL for
// a reading's).
static CheckInputAttributeRecovery check_input_attribute_recovery(const CheckInputReader* reader, const CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    return (CheckInputAttributeRecovery){
        .attribute = attribute,
        .reports = reader->output->report_count,
        .error = attributes->error,
        .error_first = attributes->error_first,
        .error_end = attributes->error_end,
    };
}

// After an attribute's handler (`recovery->attribute`, none before the first) has run, whether the declaration's attributes
// are still valid (`valid`, as it left them): clang drops an attribute whose handler gives an error and goes on, so the
// declaration's later attributes, the declaration itself and the unit's later ones are checked as without it (#1447). The
// attribute's errors move past (check_input_attribute_errors_recover), with `completion` (the subject checks' boundary, or
// 0), and the attribute is ignored: by the subject checks' handlers (`subject`) as their own ignored ones are, which they
// check again for a shared record's next declarator; by the other passes for good, which leaves it to the calls' checks
// too (a format's) while those passes, which don't read the mark, give its error again for each declarator, as clang does.
static bool check_input_attribute_recover(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttributeRecovery* recovery,
                                          uint32_t completion, bool subject, bool valid)
{
    const bool recovered = !valid && recovery->attribute != NULL && check_input_attribute_errors_recover(reader, attributes, recovery, completion);
    if (recovered)
    {
        recovery->attribute->ignored = true;
        recovery->attribute->subject_ignored |= subject;
    }
    return valid || recovered;
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
    bool recovered = false;
    if (underaligned)
    {
        const uint32_t spelling = name < reader->parser->tokens.count ? check_input_typedef_spelling(reader->output, reader->parser, name) : 0;
        const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, NULL);
        check_input_alignas_error(reader, attributes, NULL, 1u << natural, type, spelling);
        // An error clang gives and goes on from (#1447), each declarator's, completing with its own: after its _Alignas'
        // values' errors (CheckAlignasUnderalignment comes after the attributes), so ordered at the declarator's name.
        if (reader->output->report_count > recovery.reports && name < reader->parser->tokens.count)
        {
            reader->output->reports[recovery.reports].sort = name;
        }
        recovered = check_input_attribute_errors_recover(reader, attributes, &recovery, 0);
    }
    return !underaligned || recovered;
}

// A declarator's _Alignas errors (#1447), in clang's order: for each _Alignas of its specifiers' list (`common`) and then of
// its own (`own`), its place's where the declarator can't take one (`place`: 0, 1 or 3 for a parameter, a register
// variable or a bit-field, BUSTER_CHECK_NONE for a typedef, as check_input_alignas_error takes them), else its value's that
// the reader kept for its declarators (CheckInputAttribute.deferred: no power of 2, no integer constant, past 2^32), once
// for each declarator; the underalignment is the caller's after (check_input_alignas_underaligned). A function's place is
// check_input_attribute_function's; CHECK_ALIGNAS_OBJECT is a variable's or a field's. Each error recovers, as clang goes
// on, waiting for the declarator's end as the reader's errors do (CheckInputReport.pending). Whether the declaration may
// go on: not where room ran out, or where the list had an error before.
#define CHECK_ALIGNAS_OBJECT 2u
static bool check_input_alignas_declarator(CheckInputReader* reader, CheckInputAttributes* common, CheckInputAttributes* own, uint32_t place)
{
    bool valid = true;
    for (uint32_t list = 0; valid && list < 2; list += 1)
    {
        CheckInputAttributes* attributes = list == 0 ? common : own;
        for (uint32_t i = 0; valid && attributes != NULL && attributes->has_alignas && i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            const bool given = attribute->kind == CHECK_ATTRIBUTE_ALIGNAS && (place != CHECK_ALIGNAS_OBJECT || attribute->deferred != NULL);
            CheckInput* output = reader->output;
            const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, NULL);
            if (given && place != CHECK_ALIGNAS_OBJECT)
            {
                CheckInputAttributes one = { 0 };
                const CheckAttributeArgumentError error = place == BUSTER_CHECK_NONE ? CHECK_ARGUMENT_ALIGNAS_SUBJECT : CHECK_ARGUMENT_ALIGNAS_DECLARATION;
                check_input_attribute_argument_error(reader, &one, attribute, attribute->first, error, place == BUSTER_CHECK_NONE ? 0 : place);
            }
            else if (given && output->report_count < output->report_capacity)
            {
                output->reports[output->report_count++] = *attribute->deferred;
            }
            output->resource |= given && output->report_count == recovery.reports;
            valid = !given || check_input_attribute_errors_recover(reader, attributes, &recovery, 0);
        }
    }
    return valid;
}

// The token after a GNU or C23 list's item that starts at `item`: its next comma at the list's level, or the list's end.
static uint32_t check_input_attribute_item_end(const Parser* parser, uint32_t item, uint32_t list_end)
{
    uint32_t t = item;
    while (t < list_end && parser->tokens.kinds[t] != PARSE_KIND_COMMA)
    {
        const uint32_t close = parser->tokens.kinds[t] == PARSE_KIND_L_PAREN ? parser->tree.parents[t] : t;
        t = close > t && close < list_end ? close + 1 : t + 1;
    }
    return t;
}

static bool check_input_attribute_read_item(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t* cursor, uint32_t end, bool standard,
                                            uint32_t place, bool definition)
{
    const Parser* parser = reader->parser;
    uint32_t i = *cursor;
    CheckInputAttribute attribute = { .first = i, .name_token = i, .standard = standard,
                                      .specifier_type = standard && place == BUSTER_CHECK_PLACE_SPECIFIERS };
    bool valid = i < end && parser->tokens.names[i] != 0;
    bool namespace_known = true;
    bool scoped = false;
    bool clang_scope = false;
    bool unknown = false;
    // Whether an argument's escape sequence dropped it (check_input_unevaluated_strings).
    bool escaped = false;
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
        // The name's rows, a GNU spelling's, whose rule is any spelling's of the name.
        const CheckInputGnuRows* rows = check_input_attribute_gnu_rows(reader, attribute.name);
        const CheckInputAttributeRule* rule = namespace_known && rows->rule != 0 ? check_input_attribute_rules + rows->rule - 1 : 0;
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
        const bool known = valid && (standard ? check_input_attribute_clang_knows(scope, spelled, scoped, standard) : rows->known);
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
        attribute.specifier_type = standard && specifiers && typed;
        const bool chunk = place == BUSTER_CHECK_PLACE_CHUNK || place == BUSTER_CHECK_PLACE_FUNCTION;
        const bool chunk_gnu = valid && chunk && gnu_scope;
        const bool ignored_type = known && ((clang_scope && check_input_attribute_equal(name, "noderef")) || (specifiers && check_input_attribute_equal(name, "vector_size")));
        const bool misplaced = known && place != BUSTER_CHECK_PLACE_DECLARATION && fact != NULL && !typed;
        // clang's parser reads an attribute's arguments by their kinds (#147, check_input_arguments_row: ParseAttributeArgsCommon),
        // before its place decides anything: an identifier first argument of one that takes an identifier is one (each
        // identifier argument of one whose identifiers all are); then an argument it reads as an unevaluated string literal
        // that is none is its error at the argument. One whose arguments clang parses its own way (availability's and the
        // like) is its own.
        const CheckInputFactRow* parsed = standard ? fact : known ? check_input_attribute_gnu_fact(reader, attribute.name) : NULL;
        // One of no C subject (#147, check_input_attribute_generic).
        const CheckInputSubjectRow* subject_row = known ? check_input_attribute_subject_of(reader, scope, attribute.name, scoped, standard) : NULL;
        const bool subjectless = subject_row != NULL && subject_row->subjects == 0;
        // (annotate_type's, which clang parses its own way for C++'s packs, are read as any attribute's in C: a string
        // literal, then expressions, #1447.)
        const bool custom = parsed != NULL && (parsed->flags & BUSTER_CHECK_FACT_CUSTOM_PARSING) != 0 && !check_input_attribute_equal(name, "annotate_type");
        // The compatibility conditions are retained, not evaluated here. Their expression-form messages are
        // validated by their own schema below, after peeling parentheses, rather than the eager literal pass.
        const bool condition = rule != NULL && rule->schema == CHECK_ATTRIBUTE_CONDITION_STRINGS;
        const CheckInputArgumentsRow* arguments = valid && known && !custom && !condition ? check_input_attribute_gnu_arguments(reader, attribute.name) : NULL;
        // Its string literals' escape sequences are read in a GNU attribute clang doesn't know too (nodiscard's), by its
        // name: clang's GNU parser reads every one's arguments so (ParseGNUAttributeArgs). A C23 one it doesn't know it
        // skips. The compatibility conditions' messages are strings too, which check_input_unevaluated_strings peels.
        const CheckInputArgumentsRow* read = valid && (known || !standard) && !custom ? check_input_attribute_gnu_arguments(reader, attribute.name) : NULL;
        const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
        const bool suppress = check_input_attribute_suppress_name(name);
        uint32_t literal = BUSTER_CHECK_NONE;
        uint32_t index = 0;
        for (uint32_t argument = attribute.argument_first; read != NULL && literal == BUSTER_CHECK_NONE && !escaped && argument < attribute.argument_end; index += 1)
        {
            uint32_t stop = argument;
            bool strings = true;
            while (stop < attribute.argument_end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
            {
                strings &= parser->tokens.kinds[stop] == PARSE_KIND_STRING;
                stop = check_input_attribute_after_group(parser, stop, attribute.argument_end);
            }
            const bool lone = stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
            const bool identifier = lone && ((index == 0 && (read->flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0) || (read->flags & identifiers) != 0);
            const bool string = index < 32 && ((read->strings >> index) & 1u) != 0 && (read->flags & (identifiers | BUSTER_CHECK_ARGUMENTS_TYPE)) == 0;
            escaped = string && !identifier && check_input_unevaluated_strings(reader, argument, stop, BUSTER_CHECK_NONE);
            // suppress's through parentheses (check_input_attribute_suppress_literal).
            const uint32_t suppressed = suppress && stop != argument ? check_input_attribute_suppress_literal(parser, argument, stop) : argument;
            literal = arguments != NULL && string && !identifier && !escaped && (!strings || stop == argument) ? suppressed : BUSTER_CHECK_NONE;
            argument = stop + 1;
        }
        if (literal != BUSTER_CHECK_NONE)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, literal, CHECK_ARGUMENT_STRING_LITERAL, 0);
            valid = false;
        }
        // The parser recognizes a definition's suffix before Sema processes its attributes: its warning holds even
        // where a type attribute's arguments (regparm's or pcs's) then keep the declarator from completing. An attribute
        // whose argument the parser itself refused (pcs(1)'s, above, or an escape's, below) it dropped first, with no warning.
        else if (definition && !escaped)
        {
            check_input_attribute_gcc_one(reader, &attribute);
        }
        // An argument's escape sequence's error drops the attribute, as clang's parser drops it (its arguments' parse
        // failed): nothing more of it is read or kept, and its list and declaration go on without it.
        valid &= !escaped;
        // A standard one's (deprecated's, any spelling; nodiscard's, as written): clang's parser then checks the attribute
        // parsed before it (check_input_attribute_standard_previous), its error at this one's `(` after the escapes'.
        const InternTable* table = reader->pp->table;
        const bool nodiscard = escaped && table->spans[attribute.name].length == 9 && memcmp(table->records + table->spans[attribute.name].offset, "nodiscard", 9) == 0;
        if (escaped && standard && !scoped && (check_input_attribute_equal(name, "deprecated") || nodiscard))
        {
            uint32_t list_first = attribute.first;
            while (list_first > 0 && parser->tree.depths[list_first - 1] >= parser->tree.depths[attribute.first])
            {
                list_first -= 1;
            }
            uint32_t previous_first = BUSTER_CHECK_NONE;
            const uint32_t previous = check_input_attribute_standard_previous(reader, list_first, attribute.first, &previous_first);
            bool found = false;
            for (uint32_t r = reader->output->report_count; previous != 0 && !found && r-- > 0;)
            {
                CheckInputReport* report = reader->output->reports + r;
                found = report->message == CHECK_INPUT_UNEVALUATED_ESCAPE && report->token >= attribute.argument_first && report->token < attribute.argument_end;
                report->attribute = found ? attribute.name_token : report->attribute;
                report->value = found ? attribute.argument_first | (previous == 1 ? UINT64_C(1) << 32 : 0) : report->value;
            }
            // The invalid one is no more: the last record kept, or the last unknown one, where it is that one.
            if (previous != 0 && attributes->count != 0 && attributes->records[attributes->count - 1].first == previous_first)
            {
                attributes->count -= 1;
            }
            else if (previous != 0 && attributes->unknown_end != NULL && attributes->unknown_end->first == previous_first)
            {
                CheckInputUnknownAttribute* before = NULL;
                for (CheckInputUnknownAttribute* unknown = attributes->unknown_begin; unknown != attributes->unknown_end; unknown = unknown->next)
                {
                    before = unknown;
                }
                attributes->unknown_end = before;
                attributes->unknown_begin = before != NULL ? attributes->unknown_begin : NULL;
                if (before != NULL)
                {
                    before->next = NULL;
                }
            }
        }
        // availability's and external_source_symbol's arguments in parentheses, which clang's parser reads its own way
        // (#1447, check_input_availability_parse and check_input_external_source_parse), with its errors and warnings, before
        // their place decides anything; one it drops without a word (an empty version) is ignored.
        const bool parenthesized = attribute.argument_first == attribute.name_token + 2;
        const bool availability = valid && known && custom && parenthesized && check_input_attribute_equal(name, "availability");
        const bool external_source = valid && known && custom && parenthesized && check_input_attribute_equal(name, "external_source_symbol");
        // type_tag_for_datatype's kind, type name and flags (#1447, check_input_type_tag_parse) too.
        const bool type_tag = valid && known && custom && parenthesized && check_input_attribute_equal(name, "type_tag_for_datatype");
        // objc_bridge_related's class and selectors (#1447, check_input_objc_bridge_related_parse) too.
        const bool related = valid && known && custom && parenthesized && check_input_attribute_equal(name, "objc_bridge_related");
        // swift_newtype's and swift_wrapper's kind (#1447, check_input_swift_newtype_parse) too.
        const bool newtype = valid && known && custom && parenthesized &&
                             (check_input_attribute_equal(name, "swift_newtype") || check_input_attribute_equal(name, "swift_wrapper"));
        const uint32_t custom_parse = availability ? check_input_availability_parse(reader, attributes, &attribute) :
                                      external_source ? check_input_external_source_parse(reader, attributes, &attribute) :
                                      type_tag ? check_input_type_tag_parse(reader, attributes, &attribute) :
                                      related ? check_input_objc_bridge_related_parse(reader, attributes, &attribute) :
                                      newtype ? check_input_swift_newtype_parse(reader, attributes, &attribute) : BUSTER_CHECK_CUSTOM_KEPT;
        valid &= custom_parse != BUSTER_CHECK_CUSTOM_ERROR;
        const bool dropped = custom_parse == BUSTER_CHECK_CUSTOM_DROPPED;
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
        // (cfi_salt is a function type's too, #1447.)
        const bool effect = check_input_attribute_equal(name, "cfi_salt") || check_input_attribute_equal(name, "nonblocking") || check_input_attribute_equal(name, "nonallocating") ||
                            check_input_attribute_equal(name, "blocking") || check_input_attribute_equal(name, "allocating");
        // The NEON vectors and the address spaces (#1447), whose C23 spellings slide from a declaration to its type
        // (clang's slidesFromDeclToDeclSpecLegacyBehavior).
        const bool neon_name = check_input_attribute_equal(name, "neon_vector_type") || check_input_attribute_equal(name, "neon_polyvector_type") ||
                               (rule != 0 && rule->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE) || check_input_attribute_equal(name, "objc_gc");
        // A NEON vector's list before a declaration's specifiers: a parameter's too (#1447).
        const bool neon_leading = leading || before_list == PARSE_KIND_L_PAREN || before_list == PARSE_KIND_COMMA;
        uint32_t tag_bound = 0;
        // A tag's own list, between its keyword and its name or body, of its declaration or definition (a reference's is
        // the parser's error alone: check_input_tag_attributes).
        // (Or a bare declaration's list after the tag's name, as clang's Sema takes it: CheckInputReader.tag_name_list.)
        const bool tag_keyword = declaration_place && reader->tag_attributes &&
                                 (before_list == PARSE_KIND_STRUCT || before_list == PARSE_KIND_ENUM || reader->tag_name_list);
        const bool tag_list = tag_keyword && (reader->tag_name_list || check_input_attribute_tag_declares(parser, list_opener - 2));
        // annotate_type is a type's alone: on a declaration, clang's "cannot be applied to a declaration" (#1447); so is a
        // sliding type attribute on a declaration of no declarator (`[[clang::address_space(1)]];`).
        const bool annotate_type = check_input_attribute_equal(name, "annotate_type");
        const bool bare = declaration_place && leading && neon_name && list_opener > 0 && parser->tree.parents[list_opener - 1] + 2 < parser->tokens.count &&
                          parser->tokens.kinds[parser->tree.parents[list_opener - 1] + 2] == PARSE_KIND_SEMI;
        // C23's objc_ownership on a declaration of no declarator (#1447): clang's "only applies to variables" at its `;`.
        const uint32_t bare_semi = declaration_place && leading && check_input_attribute_equal(name, "objc_ownership") && list_opener > 0 &&
                                   parser->tree.parents[list_opener - 1] + 2 < parser->tokens.count &&
                                   parser->tokens.kinds[parser->tree.parents[list_opener - 1] + 2] == PARSE_KIND_SEMI ? parser->tree.parents[list_opener - 1] + 2 :
                                   BUSTER_CHECK_NONE;
        if (bare_semi != BUSTER_CHECK_NONE && !reader->statement_attributes)
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, bare_semi, CHECK_ARGUMENT_OBJC_VARIABLES, 0);
            valid = false;
        }
        // A function effect or cfi_salt after a declarator's name is that error too where the declarator has a function
        // (#1447); without one it is the function types' warning (check_input_attribute_type_warnings).
        bool declarator_function = false;
        uint32_t scan = declaration_place && effect && !leading ? attribute.end : parser->tokens.count;
        const uint32_t list_depth = list_opener < parser->tokens.count ? parser->tree.depths[list_opener] : 0;
        for (uint32_t steps = 0; !declarator_function && scan < parser->tokens.count && steps < 256; steps += 1)
        {
            const uint8_t kind = parser->tokens.kinds[scan];
            const bool stop = kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_EQUAL ||
                              (kind == PARSE_KIND_COMMA && parser->tree.depths[scan] <= list_depth);
            declarator_function = !stop && kind == PARSE_KIND_L_PAREN && parser->tree.roles[scan] == PARSE_ROLE_OPEN_FUNCTION;
            scan = stop ? parser->tokens.count : scan + 1;
        }
        // (cfi_salt's subjects are checked first there: check_input_attribute_subjects.)
        const bool salt_name = check_input_attribute_equal(name, "cfi_salt");
        attribute.salt_declaration = declaration_place && salt_name && !leading && declarator_function && !reader->statement_attributes;
        if ((declaration_place && effect && (leading || (declarator_function && !salt_name)) && !reader->statement_attributes) ||
            (!reader->statement_attributes && ((declaration_place && annotate_type) || bare)))
        {
            check_input_attribute_argument_error(reader, attributes, &attribute, attribute.first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
            valid = false;
        }
        else if (tag_list && (check_input_attribute_equal(name, "btf_type_tag") || neon_name) &&
                 check_input_attribute_count_check(fact, check_input_attribute_count(parser, &attribute), &tag_bound) == BUSTER_CHECK_COUNT_NONE)
        {
            // A tag's own list (`struct [[clang::btf_type_tag("x")]] S`) declares no declarator, which ProcessDeclAttribute
            // refuses such a type attribute on (#1447), after its count's error.
            check_input_attribute_argument_error(reader, attributes, &attribute, attribute.first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
            valid = false;
        }
        else if (declaration_place && clang_scope && !reader->statement_attributes && !tag_keyword && !leading && !neon_leading &&
                 check_input_attribute_equal(name, "btf_type_tag") && check_input_attribute_typedef(parser, list_opener >= 2 ? list_opener - 2 : 0) &&
                 check_input_attribute_count_check(fact, check_input_attribute_count(parser, &attribute), &tag_bound) == BUSTER_CHECK_COUNT_NONE)
        {
            // After a typedef's name, ProcessDeclAttribute refuses such a type attribute, which a variable's slides to its
            // type with the deprecation warning (#1447).
            check_input_attribute_argument_error(reader, attributes, &attribute, attribute.first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
            valid = false;
        }
        else if (declaration_place && clang_scope && !reader->statement_attributes && !tag_keyword &&
                 (check_input_attribute_equal(name, "btf_type_tag") || (neon_name && neon_leading)))
        {
            // (A statement's is its error alone. The NEON vectors, #1447, slide to the specifiers' type too
            // (slidesFromDeclToDeclSpecLegacyBehavior), GetDeclSpecTypeForDeclarator warning of those before the declaration
            // before it applies them; of those after its name ProcessDeclAttribute warns once its type took them:
            // CheckInputAttribute.neon_deprecated.)
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_ON_DECLARATION);
        }
        attribute.neon_deprecated = valid && declaration_place && clang_scope && !reader->statement_attributes && !tag_keyword && neon_name && !neon_leading;
        attribute.neon_typedef = attribute.neon_deprecated && check_input_attribute_typedef(parser, list_opener >= 2 ? list_opener - 2 : 0);
        attribute.objc_declaration = declaration_place && (check_input_attribute_equal(name, "objc_gc") || check_input_attribute_equal(name, "objc_ownership"));
        if (valid && chunk_gnu && typed)
        {
            // clang's declaration path warns of a calling convention it hasn't before this, on a function's chunk.
            // (Not one of a wrong count, whose count's error is clang's alone: CheckInputAttribute.convention_count.)
            uint32_t ignored_bound = 0;
            const CheckInputFactRow* ignored_fact = standard ? fact : check_input_attribute_gnu_fact(reader, attribute.name);
            if (place == BUSTER_CHECK_PLACE_FUNCTION && rule != 0 && rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_call(name) == CHECK_CALL_IGNORED_X86_32 &&
                check_input_attribute_count_check(ignored_fact, check_input_attribute_count(parser, &attribute), &ignored_bound) == BUSTER_CHECK_COUNT_NONE)
            {
                check_input_attribute_unsupported(reader, &attribute);
            }
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_GCC_COMPAT, CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE);
        }
        // A GNU attribute of a declaration in a type name: clang ignores it with its warning, keeping nothing (#147).
        const CheckInputFactRow* gnu_fact = known && !standard ? check_input_attribute_gnu_fact(reader, attribute.name) : NULL;
        // The facts of one this checker has no rule for (#147), which decide how clang takes it.
        const CheckInputFactRow* generic_fact = known && rule == 0 ? (standard ? fact : gnu_fact) : NULL;
        const uint32_t type_name_end = gnu_fact != NULL && (gnu_fact->flags & BUSTER_CHECK_FACT_TYPE) == 0 ?
                                       check_input_attribute_type_name_end(parser, attribute.first) : BUSTER_CHECK_NONE;
        const bool type_name = type_name_end != BUSTER_CHECK_NONE;
        if (valid && dropped)
        {
            unknown = true;
        }
        else if (valid && chunk_gnu && !typed)
        {
            check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_POSITION);
            unknown = true;
        }
        else if (valid && type_name)
        {
            check_input_attribute_named_once(reader, &attribute, CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE, type_name_end);
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
            attribute.neon = rule->kind != CHECK_ATTRIBUTE_EXT_VECTOR_TYPE ? 0u :
                             check_input_attribute_equal(name, "neon_vector_type") ? BUSTER_TYPE_FLAG_NEON :
                             check_input_attribute_equal(name, "neon_polyvector_type") ? BUSTER_TYPE_FLAG_NEON_POLY : 0u;
            // An OpenCL address space's (#1447), by its row's place after address_space's.
            attribute.space = rule->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE ? (uint8_t)(rule - check_input_attribute_rules - check_input_address_space_rule()) : 0u;
            // OpenCL's device and host parts of its global space, which clang's parser warns are deprecated.
            if (attribute.space == BUSTER_TYPE_SPACE_GLOBAL_DEVICE || attribute.space == BUSTER_TYPE_SPACE_GLOBAL_HOST)
            {
                check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_ADDRESS_SPACE_DEPRECATED);
            }
            reader->cleanup_attributes |= rule->kind == CHECK_ATTRIBUTE_CLEANUP;
            reader->weak_attributes |= rule->kind == CHECK_ATTRIBUTE_WEAK || rule->kind == CHECK_ATTRIBUTE_WEAKREF;
            const bool late_kind = check_input_attribute_late_kind(&attribute);
            reader->late_attributes |= late_kind;
            reader->deprecated_attributes |= rule->kind == CHECK_ATTRIBUTE_DEPRECATED || rule->kind == CHECK_ATTRIBUTE_UNAVAILABLE;
            // diagnose_if's and enable_if's conditions are each function declaration's, which a lane can't publish to the
            // declarations and calls after it (#1447, c_check_condition.c).
            const bool conditional = rule->kind == CHECK_ATTRIBUTE_DIAGNOSE_IF || rule->kind == CHECK_ATTRIBUTE_ENABLE_IF;
            reader->condition_attributes |= conditional;
            check_input_lane_refuse(reader, reader->lane && conditional, "a body declares a diagnose_if or enable_if attribute");
            // A lane cannot publish attribute state to later declarations; source-order replay handles this case.
            check_input_lane_refuse(reader, reader->lane && late_kind, "a body declares an attribute with linked state");
            // x86's interrupt and no_caller_saved_registers (#147): only a unit with either has its calls looked through
            // after checking (CheckInput.interrupt_calls). A body's interrupt routine is its entity's, which source order
            // publishes to the calls after it.
            const bool interrupt = rule->kind == CHECK_ATTRIBUTE_INTERRUPT;
            reader->output->interrupt_calls |= interrupt || (rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_call(name) == CHECK_CALL_NO_CALLER_SAVED);
            check_input_lane_refuse(reader, reader->lane && interrupt, "a body declares an interrupt service routine");
            // An argument count other than clang's is its error, worded as clang's (#147). cleanup's is checked after its
            // subject's already (check_input_cleanup_declaration).
            uint32_t bound = 0;
            const CheckInputFactRow* counted = standard ? fact : check_input_attribute_gnu_fact(reader, attribute.name);
            const uint32_t count = check_input_attribute_count(parser, &attribute);
            // A calling convention's count is clang's error where it meets a function (CheckCallingConvAttr): `preserve_all(1)`
            // on a function is "takes no arguments", and `pcs("aapcs", 1)` "takes one argument", but on a variable each is
            // its subject's warning alone (CheckInputAttribute.convention_count). no_caller_saved_registers' count comes
            // first, as a type's (below).
            const bool convention = rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_call(name) != CHECK_CALL_NO_CALLER_SAVED;
            uint32_t convention_bound = 0;
            const uint8_t convention_count = convention ? check_input_attribute_count_check(counted, count, &convention_bound) : BUSTER_CHECK_COUNT_NONE;
            uint8_t parsed_convention = BUSTER_CHECK_PARSED_CLEAN;
            // (interrupt's arguments x86's handler never counts.)
            uint8_t count_check = rule->kind == CHECK_ATTRIBUTE_MUSTTAIL || rule->kind == CHECK_ATTRIBUTE_CLEANUP || rule->kind == CHECK_ATTRIBUTE_COUNTED_BY || convention ||
                                  interrupt ? BUSTER_CHECK_COUNT_NONE : check_input_attribute_count_check(counted, count, &bound);
            // Attr.td leaves these variadic lists' minimum at zero; the handlers call checkAtLeastNumArgs(1).
            // Keep that constraint after the subject, with the same diagnostic as a generated minimum.
            const bool least_one = rule->kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || rule->kind == CHECK_ATTRIBUTE_CPU_DISPATCH ||
                                   (rule->kind == CHECK_ATTRIBUTE_NO_SANITIZE && rule->schema == CHECK_ATTRIBUTE_STRING_LIST);
            if (count_check == BUSTER_CHECK_COUNT_NONE && count == 0 && least_one)
            {
                count_check = BUSTER_CHECK_COUNT_LEAST;
                bound = 1;
            }
            // The count's error comes after its subject's check (check_input_attribute_subjects), as clang's
            // handleCommonAttributeFeatures has it: `cold(1)` or `format(printf)` on a variable is the subject's warning
            // alone. Its arguments aren't read then, but clang's parser's errors of them come first
            // (check_input_attribute_parsed). Not a type's, whose count clang's type processing checks first, nor
            // sentinel's, whose subject its handler checks after the count. Kept as clang's generated handler's (the
            // generic kind), its own handler's checks and effects aren't made.
            // (no_caller_saved_registers' count before its subject too: clang's type processing checks it first.)
            // (diagnose_if's and enable_if's too: their arguments name the function's parameters, which a generic handler's
            // reading of them wouldn't find; clang's count is its error first, #1447.)
            const bool type_attribute = conditional || rule->kind == CHECK_ATTRIBUTE_NORETURN || rule->kind == CHECK_ATTRIBUTE_PACKED || rule->kind == CHECK_ATTRIBUTE_MAY_ALIAS ||
                                        rule->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE ||
                                        rule->kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION || rule->kind == CHECK_ATTRIBUTE_NULLABILITY ||
                                        rule->kind == CHECK_ATTRIBUTE_SENTINEL ||
                                        (rule->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && check_input_attribute_call(name) == CHECK_CALL_NO_CALLER_SAVED);
            const uint8_t parsed = count_check != BUSTER_CHECK_COUNT_NONE && !type_attribute ? check_input_attribute_parsed(reader, attributes, &attribute) :
                                   BUSTER_CHECK_PARSED_UNSURE;
            if (parsed == BUSTER_CHECK_PARSED_ERROR)
            {
                valid = false;
            }
            else if (parsed == BUSTER_CHECK_PARSED_CLEAN || (count_check != BUSTER_CHECK_COUNT_NONE && !type_attribute))
            {
                attribute.parse_arguments = parsed == BUSTER_CHECK_PARSED_UNSURE;
                attribute.parsed_schema = (uint8_t)attribute.schema;
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
            else if (convention_count != BUSTER_CHECK_COUNT_NONE && !check_input_attribute_equal(name, "pcs") &&
                     (parsed_convention = check_input_attribute_parsed(reader, attributes, &attribute)) == BUSTER_CHECK_PARSED_ERROR)
            {
                // Its arguments are expressions to clang's parser first (#1447): an undeclared name's error drops it. (pcs's
                // are its string's, which its count's error comes before.)
                valid = false;
            }
            else if (convention_count != BUSTER_CHECK_COUNT_NONE)
            {
                // Its count is its error where it meets a function or a tag, and its subject's warning alone elsewhere
                // (check_input_attribute_subjects); arguments whose errors need the expression reader are read as
                // interrupt's are.
                attribute.convention_count = convention_count;
                attribute.convention_bound = (uint8_t)convention_bound;
                attribute.argument_count = count;
                attribute.parse_arguments = parsed_convention == BUSTER_CHECK_PARSED_UNSURE;
                attribute.parsed_schema = (uint8_t)CHECK_ATTRIBUTE_INTEGER_LIST;
            }
            else if (interrupt)
            {
                // interrupt's arguments are expressions to clang's parser, whose errors drop it (an undeclared name, a
                // missing operand: check_input_attribute_expressions), and nothing to x86's handler, whatever they are.
                attribute.parse_arguments = count != 0;
                attribute.parsed_schema = (uint8_t)CHECK_ATTRIBUTE_INTEGER_LIST;
                attribute.argument_count = count;
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
            // TCB's attributes, whose calls' warnings come once the unit's bindings are resolved (check_input_tcb_calls),
            // and those whose redeclarations clang merges (check_input_attribute_merge_checks): their entities'
            // declarations, as a body's lane can't read them, are read in source order.
            const bool tcb = check_input_attribute_equal(name, "enforce_tcb") || check_input_attribute_equal(name, "enforce_tcb_leaf");
            const bool merged = tcb || check_input_attribute_equal(name, "modular_format") || check_input_attribute_equal(name, "availability");
            reader->tcb_attributes |= tcb;
            reader->merged_attributes |= merged;
            // The type tags (#1447), whose declarations a body's lane can't publish in source order for its calls
            // (check_input_type_tag_declaration).
            const bool type_tag = check_input_type_tag_rule(name) || check_input_attribute_equal(name, "type_tag_for_datatype");
            reader->type_tag_attributes |= type_tag;
            check_input_lane_refuse(reader, reader->lane && type_tag, "a body declares a type tag attribute");
            // Objective-C's pointer type attributes are a declarator's or a type name's (check_input_attribute_objc), C23's
            // spellings where they are (#1447).
            const bool objc = check_input_attribute_equal(name, "objc_gc") || check_input_attribute_equal(name, "objc_ownership");
            reader->objc_attributes |= objc;
            check_input_lane_refuse(reader, reader->lane && merged, "a body declares an attribute its entity's redeclarations merge");
            // diagnose_as_builtin's builtin may be a library function the unit declares, whose declaration a body's lane
            // can't read (check_input_checks_library, #1447).
            check_input_lane_refuse(reader, reader->lane && check_input_attribute_equal(name, "diagnose_as_builtin"),
                                    "a body declares diagnose_as_builtin, whose builtin the unit's declarations decide");
            // cfi_salt's salt is a function type's (check_input_attribute_salt, #1447), whose spelling the unit's intern table
            // takes, which a body's lane can't write to.
            reader->salt_attributes |= check_input_attribute_equal(name, "cfi_salt");
            check_input_lane_refuse(reader, reader->lane && check_input_attribute_equal(name, "cfi_salt"), "a body declares cfi_salt, whose salt the unit interns");
            // swift_error's error parameter is the unit's first struct bridged to NSError that a check meets
            // (check_input_swift_cf_error, #1447), which a body's lane can't know.
            check_input_lane_refuse(reader, reader->lane && check_input_attribute_equal(name, "swift_error"),
                                    "a body declares swift_error, whose CFError the unit's earlier checks decide");
            attribute.argument_count = count;
            attribute.count_check = check_input_attribute_count_check(generic_fact, count, &attribute.count_bound);
            // Without parentheses, availability's handler's checkExactlyNumArgs(1) and external_source_symbol's
            // checkAtLeastNumArgs(1), which clang's parser's reading of its arguments always passes.
            // swift_newtype's and swift_wrapper's checkExactlyNumArgs(1) too (#1447).
            if (!parenthesized && (check_input_attribute_equal(name, "availability") || check_input_attribute_equal(name, "external_source_symbol") ||
                                   check_input_attribute_equal(name, "swift_newtype") || check_input_attribute_equal(name, "swift_wrapper")))
            {
                attribute.count_check = check_input_attribute_equal(name, "external_source_symbol") ? BUSTER_CHECK_COUNT_LEAST : BUSTER_CHECK_COUNT_EXACT;
                attribute.count_bound = 1;
            }
            // A thread-safety handler's own count of at least one, after the facts' (checkAtLeastNumArgs), and abi_tag's;
            // preferred_type's type, which its optional argument must give (handlePreferredTypeAttr), exactly one.
            if (attribute.count_check == BUSTER_CHECK_COUNT_NONE && count == 0 &&
                ((thread != NULL && (thread->flags & BUSTER_CHECK_THREAD_LEAST_ONE) != 0) || check_input_attribute_equal(name, "abi_tag")))
            {
                attribute.count_check = BUSTER_CHECK_COUNT_LEAST;
                attribute.count_bound = 1;
            }
            else if (attribute.count_check == BUSTER_CHECK_COUNT_NONE && count == 0 && check_input_attribute_equal(name, "annotate_type"))
            {
                // annotate_type's handler's checkAtLeastNumArgs(1), which its custom parsing leaves to it (HandleAnnotateTypeAttr).
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
            // Declaration attributes are processed after the whole declarator's type, including its parameters. Keep
            // unknown spellings for that point too: a shared specifier list warns once for each declarator. Statements
            // and type names have no declaration subject pass, so publish their warnings here.
            const bool deferred = !reader->statement_attributes && !check_input_attribute_in_type_name(parser, attribute.first);
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
            if (!deferred)
            {
                check_input_warning_event(reader, &event, NULL, 0);
            }
            attributes->unknown_first = attributes->unknown_first != 0 ? attributes->unknown_first : attribute.first + 1;
            if (deferred)
            {
                const CheckInputUnknownAttribute diagnostic = {
                    .first = attribute.first, .name_token = attribute.name_token, .name = attribute.name, .end = attribute.end, .standard = attribute.standard,
                    .single_warning = standard && gnu_scope,
                };
                valid = check_input_attribute_unknown_append(reader, attributes, diagnostic);
            }
            unknown = true;
        }
        if (valid && !unknown)
        {
            const bool numeric = attribute.schema == CHECK_ATTRIBUTE_INTEGER || attribute.schema == CHECK_ATTRIBUTE_INTEGER_LIST ||
                                 attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS;
            // patchable_function_entry's arguments its own reading takes (#1447, check_input_read_attributes): its section
            // is a string, which the integer lists' evaluators would refuse.
            attribute.resolved = !numeric || attribute.argument_count == 0 || (attribute.schema == CHECK_ATTRIBUTE_IDENTIFIER_INTEGERS && attribute.argument_count == 1) ||
                                 attribute.kind == CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY;
            if (attribute.kind == CHECK_ATTRIBUTE_CALLING_CONVENTION)
            {
                // The first convention x86-64 has is the one asked for; one that conflicts with an earlier one is clang's
                // error where they meet a function (check_input_attribute_conventions).
                const CheckInputCallingConvention call = check_input_attribute_call(name);
                attributes->calling_convention = check_input_convention_merge(attributes->calling_convention, call);
                attribute.value = call;
            }
            else if (attribute.kind == CHECK_ATTRIBUTE_PARAMETER_ABI)
            {
                attribute.value = check_input_parameter_abi(name);
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
    if (!valid && !escaped && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        check_input_attribute_error(reader, attributes, attribute.first, i > attribute.first ? i : attribute.first + 1, CHECK_ATTRIBUTE_ERROR_SYNTAX);
    }
    *cursor = i;
    return valid || escaped;
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
        // (A list after a bare tag declaration's name is the tag's, its declaration's: CheckInputReader.tag_name_list.)
        const uint32_t place = standard && !reader->tag_name_list ? check_input_standard_place(parser, i) : BUSTER_CHECK_PLACE_DECLARATION;
        bool recovered = false;
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
            if (valid && (place == BUSTER_CHECK_PLACE_QUALIFIERS || place == BUSTER_CHECK_PLACE_GROUP))
            {
                // clang's declarator parser rejects a C23 list after a pointer's qualifiers, or at a group's start
                // (err_attributes_not_allowed). A group's bounded list is discarded, then the same declarator finishes
                // its type and outer attributes. The retained report still stops the unit before another declaration.
                check_input_report(reader, first, CHECK_INPUT_ATTRIBUTE_LIST_PLACE);
                if (place == BUSTER_CHECK_PLACE_GROUP)
                {
                    i = list_end;
                }
                else
                {
                    valid = false;
                    attributes->error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
                    attributes->error_first = first;
                    attributes->error_end = close + 1;
                }
            }
            while (valid && i < list_end)
            {
                if (parser->tokens.kinds[i] == PARSE_KIND_COMMA)
                {
                    i += 1;
                }
                else
                {
                    // An item whose reading gives an error clang drops it for is left out, and the list goes on (#1447).
                    const uint32_t item = i;
                    const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, NULL);
                    const uint32_t moved = reader->output->recovered_count;
                    valid = check_input_attribute_read_item(reader, attributes, &i, list_end, standard, place, definition);
                    if (!valid && check_input_attribute_errors_recover(reader, attributes, &recovery, 0))
                    {
                        // Its parser's error, at its place: no Sema completion waits for it.
                        for (uint32_t r = moved; r < reader->output->recovered_count; r += 1)
                        {
                            reader->output->recovered_reports[r].pending = false;
                        }
                        i = check_input_attribute_item_end(parser, item, list_end);
                        valid = true;
                    }
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
                const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, NULL);
                const bool read = valid;
                valid = valid && check_input_attribute_validate(reader, attributes, &attribute, 1, 1);
                // An alignment specifier or an asm label whose argument gives an error clang drops it for is left out (#1447).
                recovered = read && !valid && check_input_attribute_errors_recover(reader, attributes, &recovery, 0);
                valid |= recovered;
                i = attribute.end;
            }
            else if (kind == PARSE_KIND_CALLING_CONVENTION)
            {
                const CheckInputCallingConvention call = check_input_attribute_call(check_input_attribute_name(reader, name));
                attribute.value = call;
                attributes->calling_convention = check_input_convention_merge(attributes->calling_convention, call);
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
            valid = valid && (recovered || check_input_attribute_append(reader, attributes, attribute));
        }
        if (valid)
        {
            attributes->has_alignas |= kind == PARSE_KIND_ALIGNAS && !recovered;
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
// of a vector. With an error, `installed` (where not NULL) is the type clang's AddModeAttr installs all the same (#1447): a
// mode of the other kind, or of a base of no integer or floating type, still gives the mode's type, which takes its base's
// signedness where it is an integer and a base vector's bytes; else (another error, or a complex mode of a vector, clang's
// second error then) the base, which clang keeps.
static TypeId check_input_attribute_mode(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, TypeId base,
                                         TypeId* installed)
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
    // clang diagnoses the base and goes on to build the mode's type (AddModeAttr): a complex mode of a vector is its error
    // after, the base's error's, with the base kept.
    const bool mismatch = error == CHECK_ARGUMENT_MODE_PRIMITIVE || error == CHECK_ARGUMENT_MODE_TYPE;
    const bool built = error == CHECK_ARGUMENT_COUNT || (mismatch && result != 0);
    const bool complex_vector = built && complex_mode && vector_base && vector_count == 0;
    error = complex_vector && !mismatch ? CHECK_ARGUMENT_MODE_COMPLEX_VECTOR : error;
    TypeId moded = 0;
    if (built && !complex_vector)
    {
        moded = complex_mode ? check_input_type_result(reader, type_complex(types, result)) : result;
        // The base vector's elements' bytes, not its rounded storage's, in as many of the new elements.
        const uint64_t count = vector_count != 0 ? vector_count :
                               vector_base && moded != 0 ? type_size(types, element) * types->types.extent[type_index(old)] / type_size(types, moded) : 0;
        if (moded != 0 && (vector_count != 0 || vector_base))
        {
            moded = count != 0 && count <= UINT32_MAX ? check_input_type_result(reader, type_vector(types, moded, (uint32_t)count, false)) : 0;
        }
        error = moded == 0 && !reader->output->resource && error == CHECK_ARGUMENT_COUNT ? CHECK_ARGUMENT_MODE_TYPE : error;
    }
    if (error != CHECK_ARGUMENT_COUNT)
    {
        // A vector mode's warning comes first; a declarator's that has its type has it with its subjects
        // (check_input_attribute_subjects), as clang's handler gives it for each.
        if (vector_count != 0)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_VECTOR_MODE);
        }
        const bool unsupported = result == 0 && width != 0 && mismatch;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, error, attribute->argument_first - attribute->first);
        // AddModeAttr diagnoses the base, then still asks the target for the mode. An unavailable target type therefore
        // produces a second error at the same attribute, in that order; so does a complex mode of a vector.
        const bool second = unsupported || (complex_vector && mismatch);
        if (second)
        {
            CheckInputAttributes following = { 0 };
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, &following, attribute, attribute->first,
                                                  unsupported ? CHECK_ARGUMENT_UNSUPPORTED_MODE : CHECK_ARGUMENT_MODE_COMPLEX_VECTOR,
                                                  attribute->argument_first - attribute->first);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].repeated = true;
            }
        }
    }
    if (installed != NULL)
    {
        *installed = error == CHECK_ARGUMENT_COUNT ? moded : mismatch && moded != 0 ? moded : base;
    }
    return error == CHECK_ARGUMENT_COUNT ? moded : 0;
}

// An enum's stand-in for the type a mode's error still installs (#1447, check_input_attribute_mode's `installed`): clang's
// AddModeAttr makes a real floating or a complex type the enum's integer type, which the type table doesn't take, so the
// enum gets the signed integer of its size: of its alignment too for a real floating type (__fp16's, float's, double's,
// long double's and __float128's), and with the complex type's alignment in `alignment_log2` (log2 + 1, as
// type_enum_complete takes it, 0 for none) for a complex one (HC's, SC's and DC's: int, long and __int128 at 2, 4 and 8
// bytes), which gives the enum clang's layout. 0 for another type (XC's and TC's 32 bytes, which no integer has), which
// still stops the reader. (HC's complex __fp16, which the type table can't make, installs nothing: its enum is one with no
// mode, 4 bytes aligned to 4 where clang's are aligned to 2.)
// Whether `installed` is a complex type no integer stands in for (#1447: XC's and TC's 32 bytes): its error recovers all
// the same, the enum one with no mode, as HC's is.
static bool check_input_mode_enum_wide(const TypeTable* types, TypeId installed)
{
    const TypeId bare = installed != 0 ? type_strip_aligned(types, installed) : 0;
    return bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_COMPLEX && type_size(types, bare) > 16;
}

static TypeId check_input_mode_enum_integer(const TypeTable* types, TypeId installed, uint8_t* alignment_log2)
{
    const TypeId bare = installed != 0 ? type_strip_aligned(types, installed) : 0;
    const uint8_t kind = bare != 0 ? type_kind(types, bare) : BUSTER_TYPE_KIND_NONE;
    const bool floating = kind == BUSTER_TYPE_KIND_FLOATING;
    const bool complex = kind == BUSTER_TYPE_KIND_COMPLEX;
    const uint64_t size = floating || complex ? type_size(types, bare) : 0;
    const TypeId integer = size == 2 && floating ? BUSTER_TYPE_SHORT : size == 4 ? BUSTER_TYPE_INT : size == 8 ? BUSTER_TYPE_LONG :
                           size == 16 ? BUSTER_TYPE_INT128 : 0;
    const TypeId result = integer != 0 && (complex || type_alignment(types, integer) == type_alignment(types, bare)) ? integer : 0;
    *alignment_log2 = result != 0 && complex ? (uint8_t)(__builtin_ctzll(type_alignment(types, bare)) + 1) : 0;
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

// An address-space attribute applied to `type` (#1447), as clang's HandleAddressSpaceTypeAttribute applies it: a function
// type's error; address_space's argument's (no integer constant, negative, past BUSTER_TYPE_SPACE_MAX_TARGET); then a
// type that has another space already, "multiple address spaces specified for type", or that space, -Wduplicate-decl-
// specifier's warning. Each error is at the attribute, and gives 0; else the type with the space (type_extend), an
// array's on its element.
static TypeId check_input_address_space(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId type)
{
    TypeTable* types = reader->types;
    const bool function = type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_FUNCTION;
    const int64_t value = attribute->value;
    const uint32_t space = attribute->space != 0 ? attribute->space : BUSTER_TYPE_SPACE_TARGET + (uint32_t)value;
    const CheckInputMessage argument = attribute->space != 0 ? CHECK_INPUT_UNSUPPORTED :
                                       !attribute->resolved ? CHECK_INPUT_ADDRESS_SPACE_CONSTANT :
                                       value < 0 ? CHECK_INPUT_ADDRESS_SPACE_NEGATIVE :
                                       value > (int64_t)BUSTER_TYPE_SPACE_MAX_TARGET ? CHECK_INPUT_ADDRESS_SPACE_TOO_HIGH : CHECK_INPUT_UNSUPPORTED;
    const uint32_t old = function || argument != CHECK_INPUT_UNSUPPORTED ? 0 : type_address_space(type_object_qualifiers(types, type));
    const CheckInputMessage message = function ? CHECK_INPUT_ADDRESS_SPACE_FUNCTION : argument != CHECK_INPUT_UNSUPPORTED ? argument :
                                      old != 0 && old != space ? CHECK_INPUT_ADDRESS_SPACE_MULTIPLE : CHECK_INPUT_UNSUPPORTED;
    TypeId result = 0;
    if (message != CHECK_INPUT_UNSUPPORTED)
    {
        if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
        {
            check_input_report_argument(reader, attribute->first, message, 0);
            attributes->error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
            attributes->error_first = attribute->first;
            attributes->error_end = attribute->end;
        }
    }
    else
    {
        if (old == space)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DUPLICATE_DECLSPEC, CHECK_WARNING_EVENT_ADDRESS_SPACE_IDENTICAL);
        }
        result = check_input_type_result(reader, type_extend(types, type, space));
        // A C23 one after a declarator's name, which ProcessDeclAttribute warns of once its type took it (#1447), as a NEON
        // vector's (CheckInputAttribute.neon_deprecated).
        if (result != 0 && attribute->neon_deprecated)
        {
            check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_ON_DECLARATION);
            attribute->neon_deprecated = false;
        }
    }
    return result;
}

// A declarator chunk's address spaces (#1447): those among `attributes` from `first`, which the chunk applies to its own
// type and the declarator's whole type then doesn't (CheckInputAttribute.space_applied). The list's count where there is
// one, else `first`.
static uint32_t check_input_spaces_claim(CheckInputAttributes* attributes, uint32_t first)
{
    bool found = false;
    for (uint32_t a = first; a < attributes->count; a += 1)
    {
        const bool space = attributes->records[a].kind == CHECK_ATTRIBUTE_ADDRESS_SPACE;
        attributes->records[a].space_applied |= space;
        found |= space;
    }
    return found ? attributes->count : first;
}

// neon_vector_type's and neon_polyvector_type's checks of its element type `element` and its count (#1447), as clang's
// HandleNeonVectorTypeAttr makes them on x86-64, after its count's and its argument's (check_input_attribute_value): an
// element of none of isPermittedNeonBaseType's builtin types (signed char, short, int, long, long long, each unsigned
// too, __fp16, __bf16, float and double; a polynomial vector's signed char, short and long long alone, AArch32's), then a
// vector of another width than 64 or 128 bits, the count truncated to 32 bits and the width computed in 32 bits as
// clang computes them. Each is clang's error at the attribute, false. The count it keeps is the truncated one.
static bool check_input_neon_vector(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId element)
{
    const TypeTable* types = reader->types;
    const TypeId bare = type_unqualified(type_strip_aligned(types, element));
    const uint32_t code = type_is_valid(types, bare) && type_index(bare) < BUSTER_TYPE_BUILTIN_COUNT ? type_index(bare) : BUSTER_TYPE_CODE_NONE;
    const uint64_t plain = (1ull << BUSTER_TYPE_CODE_SCHAR) | (1ull << BUSTER_TYPE_CODE_UCHAR) | (1ull << BUSTER_TYPE_CODE_SHORT) | (1ull << BUSTER_TYPE_CODE_USHORT) |
                           (1ull << BUSTER_TYPE_CODE_INT) | (1ull << BUSTER_TYPE_CODE_UINT) | (1ull << BUSTER_TYPE_CODE_LONG) | (1ull << BUSTER_TYPE_CODE_ULONG) |
                           (1ull << BUSTER_TYPE_CODE_LLONG) | (1ull << BUSTER_TYPE_CODE_ULLONG) | (1ull << BUSTER_TYPE_CODE_HALF) | (1ull << BUSTER_TYPE_CODE_BF16) |
                           (1ull << BUSTER_TYPE_CODE_FLOAT) | (1ull << BUSTER_TYPE_CODE_DOUBLE);
    const uint64_t poly = (1ull << BUSTER_TYPE_CODE_SCHAR) | (1ull << BUSTER_TYPE_CODE_SHORT) | (1ull << BUSTER_TYPE_CODE_LLONG);
    const bool permitted = code != BUSTER_TYPE_CODE_NONE && (((attribute->neon == BUSTER_TYPE_FLAG_NEON_POLY ? poly : plain) >> code) & 1) != 0;
    const uint32_t count = (uint32_t)(uint64_t)attribute->value;
    const uint32_t width = permitted ? (uint32_t)type_size(types, bare) * 8u * count : 0;
    const bool valid = permitted && (width == 64 || width == 128);
    if (!permitted && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_VECTOR_ELEMENT, 0);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].type = element;
            reader->output->reports[before].type_spelling = check_input_typedef_spelling(reader->output, reader->parser, attribute->first);
        }
        attributes->error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    else if (!valid)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_NEON_SIZE, 0);
    }
    else if (attribute->neon_typedef)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
        attribute->neon_deprecated = false;
    }
    else if (attribute->neon_deprecated)
    {
        check_input_attribute_named_event(reader, attribute, CHECK_WARNING_ID_DEPRECATED_ATTRIBUTES, CHECK_WARNING_EVENT_TYPE_ON_DECLARATION);
        attribute->neon_deprecated = false;
    }
    attribute->value = count;
    const bool refused = attribute->neon_typedef;
    attribute->neon_typedef = false;
    return valid && !refused;
}

static bool check_input_auto_type(TypeId type);

// Whether `type` is an __auto_type placeholder's, through pointer levels (#1844): `__auto_type`, `const __auto_type *`.
static bool check_input_auto_type_level(const TypeTable* types, TypeId type)
{
    TypeId level = type != 0 ? type_strip_aligned(types, type) : 0;
    for (uint32_t steps = 0; level != 0 && !check_input_auto_type(level) && steps < 64; steps += 1)
    {
        level = type_kind(types, level) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, level)) : 0;
    }
    return level != 0 && check_input_auto_type(level);
}

// A vector's or a mode's attribute of an __auto_type placeholder's type `type` (#1844), as clang's handlers meet the
// undeduced type: a vector's (vector_size, ext_vector_type, a NEON one) is "invalid vector element type 'T'", T the
// placeholder's type there (check_semantic_auto_type_spelling), at the first one, as clang's type processing runs before
// the declaration's; a mode's "mode attribute only supported for integer and floating-point types". Clang goes on with
// the declaration, the reader stops at it. Whether it gave one.
static bool check_input_attribute_auto_type(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    const CheckInputAttribute* vector = NULL;
    const CheckInputAttribute* mode = NULL;
    for (uint32_t i = 0; i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->specifier_ignored;
        vector = vector == NULL && kept && (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE) ? attribute : vector;
        mode = mode == NULL && kept && attribute->kind == CHECK_ATTRIBUTE_MODE ? attribute : mode;
    }
    if (vector != NULL)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, vector->first, CHECK_INPUT_VECTOR_ELEMENT, 0);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].type = type;
        }
        attributes->error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        attributes->error_first = vector->first;
        attributes->error_end = vector->end;
    }
    else if (mode != NULL)
    {
        check_input_attribute_argument_error(reader, attributes, mode, mode->first, CHECK_ARGUMENT_MODE_PRIMITIVE, mode->argument_first - mode->first);
    }
    return vector != NULL || mode != NULL;
}

// Mode/vector attributes change the scalar element, while a typedef's alignment wraps the resulting complete type.
// An object's/member's alignment is declaration metadata: applying ALIGNED to its type would incorrectly alter
// pointer/array compatibility. The caller feeds the alignment to the layout/storage consumer instead. Of `deduced`
// specifiers, __auto_type's, an _Alignas waits for the deduced type (check_input_auto_type_finish), which `base` isn't.
static TypeId check_input_attribute_type(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId base, bool is_typedef, bool deduced)
{
    TypeId result = base;
    bool valid = base != 0 && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE &&
                 !(check_input_auto_type_level(reader->types, base) && check_input_attribute_auto_type(reader, attributes, base));
    for (uint32_t i = 0; valid && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        CheckInputAttributeError error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
        if (attribute->specifier_type && deduced)
        {
            TypeId function = result;
            uint8_t kind = type_kind(reader->types, function);
            while (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED)
            {
                function = reader->types->types.base[type_index(function)];
                kind = type_kind(reader->types, function);
            }
            attribute->specifier_ignored = kind != BUSTER_TYPE_KIND_FUNCTION;
        }
        if (attribute->kind == CHECK_ATTRIBUTE_MODE && type_kind(reader->types, type_strip_aligned(reader->types, result)) == BUSTER_TYPE_KIND_FUNCTION)
        {
            // A function's, which clang's subject check refuses before the mode is read (check_input_attribute_subjects).
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_MODE)
        {
            const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, attribute);
            TypeId installed = 0;
            const TypeId moded = check_input_attribute_mode(reader, attributes, attribute, result, &installed);
            // An error clang gives and goes on from, with the type it installs all the same (#1447). The specifiers' is each
            // declarator's in clang (AddModeAttr runs for each), which the later ones give again (CheckInputAttributeRepeat).
            const uint32_t made_count = reader->output->report_count - recovery.reports;
            CheckInputReport made[2] = { 0 };
            for (uint32_t r = 0; deduced && r < made_count && r < 2; r += 1)
            {
                made[r] = reader->output->reports[recovery.reports + r];
            }
            valid = moded != 0 || (installed != 0 && check_input_attribute_recover(reader, attributes, &recovery, 0, false, false));
            for (uint32_t r = 0; deduced && moded == 0 && valid && r < made_count && r < 2; r += 1)
            {
                check_input_attribute_repeat_add(reader, attributes, made + r);
            }
            result = moded != 0 ? moded : valid ? installed : 0;
            error = CHECK_ATTRIBUTE_ERROR_MODE;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || (attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE && attribute->neon != 0 && attribute->resolved))
        {
            // A vector's error leaves the type as it was, clang's HandleVectorSizeAttr's and HandleNeonVectorTypeAttr's (#1447).
            const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, attribute);
            const bool checked = attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE ? check_input_vector_size(reader, attributes, attribute, result) :
                                 check_input_neon_vector(reader, attributes, attribute, result);
            const bool recovered = !checked && check_input_attribute_recover(reader, attributes, &recovery, 0, false, false);
            valid = checked || recovered;
            if (checked)
            {
                result = attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE ? type_vector_size(reader->types, result, (uint64_t)attribute->value) :
                         type_vector_kind(reader->types, result, (uint32_t)attribute->value, attribute->neon);
                result = check_input_type_result(reader, result);
                valid = result != 0;
            }
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE)
        {
            valid = attribute->resolved;
            if (valid)
            {
                result = attribute->neon != 0 ? type_vector_kind(reader->types, result, (uint32_t)attribute->value, attribute->neon) :
                         type_vector(reader->types, result, (uint32_t)attribute->value, true);
                result = check_input_type_result(reader, result);
                valid = result != 0;
            }
            error = CHECK_ATTRIBUTE_ERROR_VECTOR;
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_ADDRESS_SPACE)
        {
            // Its place's type: the specifiers', or the whole declarator's after its name; one a chunk applied is its
            // pointer's or its group's (check_input_declarator_step, #1447).
            if (!attribute->space_applied)
            {
                result = check_input_address_space(reader, attributes, attribute, result);
                valid = result != 0;
            }
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS && attribute->deferred != NULL)
        {
            // Its value's error, which waits for its declarators (check_input_alignas_declarator); no alignment.
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
    // A convention ignored on a C23 specifier type must not become the later declarator's ABI. Recompute the compact
    // requested convention from the retained records; an ordinary prefix convention still reaches the full type.
    if (deduced)
    {
        attributes->calling_convention = CHECK_CALL_DEFAULT;
        for (uint32_t i = 0; i < attributes->count; i += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + i;
            if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !attribute->specifier_ignored && !attribute->specifier_applied)
            {
                attributes->calling_convention = check_input_convention_merge(attributes->calling_convention, (CheckInputCallingConvention)attribute->value);
            }
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
static uint32_t check_input_abi_call(TypeFunctionABI with)
{
    // (no_caller_saved_registers' bit beside a convention, #1447, is that one's with CHECK_CALL_KEEPS_REGISTERS.)
    const TypeFunctionABI abi = with == TYPE_FUNCTION_ABI_INVALID ? with : (TypeFunctionABI)(with & ~TYPE_FUNCTION_ABI_KEEPS_REGISTERS);
    const uint32_t bit = with != TYPE_FUNCTION_ABI_INVALID && (with & TYPE_FUNCTION_ABI_KEEPS_REGISTERS) != 0 ? CHECK_CALL_KEEPS_REGISTERS : 0u;
    const uint32_t result = bit | (abi == TYPE_FUNCTION_ABI_MICROSOFT ? CHECK_CALL_MICROSOFT : abi == TYPE_FUNCTION_ABI_VECTOR ? CHECK_CALL_VECTOR :
                            abi == TYPE_FUNCTION_ABI_REG ? CHECK_CALL_REG : abi == TYPE_FUNCTION_ABI_PRESERVE_MOST ? CHECK_CALL_PRESERVE_MOST :
                            abi == TYPE_FUNCTION_ABI_PRESERVE_ALL ? CHECK_CALL_PRESERVE_ALL : abi == TYPE_FUNCTION_ABI_PRESERVE_NONE ? CHECK_CALL_PRESERVE_NONE :
                            abi == TYPE_FUNCTION_ABI_NO_CALLER_SAVED ? CHECK_CALL_NO_CALLER_SAVED : abi == TYPE_FUNCTION_ABI_INTEL_OCL ? CHECK_CALL_INTEL_OCL :
                            abi == TYPE_FUNCTION_ABI_SWIFT ? CHECK_CALL_SWIFT : abi == TYPE_FUNCTION_ABI_SWIFT_ASYNC ? CHECK_CALL_SWIFT_ASYNC : CHECK_CALL_SYSV);
    return result;
}

static uint32_t check_input_no_caller_saved_written(const CheckInput* input, uint32_t token)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(input->attributes) ? check_input_attributes_at(input, token) : NULL;
    uint32_t result = 0;
    for (uint32_t i = 0; attributes != NULL && result == 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool saved = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->value == CHECK_CALL_NO_CALLER_SAVED;
        result = saved ? BUSTER_CHECK_CONVENTION_NO_CALLER_SAVED | (attribute->standard ? BUSTER_CHECK_CONVENTION_STANDARD : 0) : 0;
    }
    return result;
}

// The ABI of a convention (CheckInputAttributes' calling_convention): as the function type has it (`spelled` false), or as
// an origin keeps it (`spelled` true), where explicit sysv_abi beside no_caller_saved_registers stays System V's with
// TYPE_FUNCTION_ABI_KEEPS_REGISTERS, so that a later convention conflicts with it (#1447).
static TypeFunctionABI check_input_convention_abi(uint32_t value, bool spelled)
{
    const CheckInputCallingConvention with = (CheckInputCallingConvention)check_input_convention_unflagged(value);
    // no_caller_saved_registers' bit beside another convention (#1447): that one's with TYPE_FUNCTION_ABI_KEEPS_REGISTERS.
    const bool keeps = with != CHECK_CALL_NO_CALLER_SAVED && check_input_convention_keeps(with);
    const CheckInputCallingConvention call = (CheckInputCallingConvention)(with & ~CHECK_CALL_KEEPS_REGISTERS);
    const TypeFunctionABI result = call == CHECK_CALL_SYSV ? TYPE_FUNCTION_ABI_SYSV : call == CHECK_CALL_MICROSOFT ? TYPE_FUNCTION_ABI_MICROSOFT :
                                   call == CHECK_CALL_VECTOR ? TYPE_FUNCTION_ABI_VECTOR : call == CHECK_CALL_REG ? TYPE_FUNCTION_ABI_REG :
                                   call == CHECK_CALL_PRESERVE_MOST ? TYPE_FUNCTION_ABI_PRESERVE_MOST :
                                   call == CHECK_CALL_PRESERVE_ALL ? TYPE_FUNCTION_ABI_PRESERVE_ALL :
                                   call == CHECK_CALL_PRESERVE_NONE ? TYPE_FUNCTION_ABI_PRESERVE_NONE :
                                   call == CHECK_CALL_NO_CALLER_SAVED ? TYPE_FUNCTION_ABI_NO_CALLER_SAVED :
                                   call == CHECK_CALL_INTEL_OCL ? TYPE_FUNCTION_ABI_INTEL_OCL : call == CHECK_CALL_SWIFT ? TYPE_FUNCTION_ABI_SWIFT :
                                   call == CHECK_CALL_SWIFT_ASYNC ? TYPE_FUNCTION_ABI_SWIFT_ASYNC : TYPE_FUNCTION_ABI_INVALID;
    const TypeFunctionABI kept = keeps && result == TYPE_FUNCTION_ABI_SYSV && !spelled ? TYPE_FUNCTION_ABI_NO_CALLER_SAVED : result;
    return keeps && kept != TYPE_FUNCTION_ABI_INVALID && kept != TYPE_FUNCTION_ABI_NO_CALLER_SAVED ? (TypeFunctionABI)(kept | TYPE_FUNCTION_ABI_KEEPS_REGISTERS) : kept;
}

static TypeFunctionABI check_input_attribute_requested_abi(const CheckInputAttributes* attributes)
{
    return check_input_convention_abi(attributes != 0 ? (uint32_t)attributes->calling_convention : CHECK_CALL_DEFAULT, false);
}

// The canonical default and explicit sysv_abi function types are identical. Keep explicitness on the spelling
// instead: zero means implicit, and an ABI plus one names the first function's explicit convention. New outer
// function declarators start a new origin; pointer, array, aligned, grouping and typedef wrappers preserve it.
static uint8_t check_input_attribute_abi_origin(CheckInputReader* reader, CheckInputAttributes* attributes, uint8_t origin)
{
    uint8_t result = origin;
    const TypeFunctionABI requested = check_input_attribute_requested_abi(attributes);
    // no_caller_saved_registers is a bit beside whatever convention the function has (check_input_convention_merge,
    // #1447): with the earlier origin's, they make one function type, unless their own conventions differ.
    // (The origin is the explicit convention still visible, CHECK_CALL_HIDDEN's none: 0.)
    const uint32_t previous_call = origin != 0 ? check_input_abi_call((TypeFunctionABI)(origin - 1)) : CHECK_CALL_DEFAULT;
    const uint32_t requested_call = requested != TYPE_FUNCTION_ABI_INVALID ? (uint32_t)attributes->calling_convention : CHECK_CALL_DEFAULT;
    const uint32_t previous_base = check_input_convention_base(previous_call);
    const uint32_t requested_base = check_input_convention_base(requested_call);
    const bool compatible = previous_base == CHECK_CALL_DEFAULT || requested_base == CHECK_CALL_DEFAULT || previous_base == requested_base ||
                            check_input_convention_strips(requested_call);
    const uint32_t merged = check_input_convention_merge((CheckInputCallingConvention)previous_call, (CheckInputCallingConvention)requested_call);
    if (requested != TYPE_FUNCTION_ABI_INVALID && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        if (origin == 0 || compatible)
        {
            result = (merged & CHECK_CALL_HIDDEN) != 0 ? 0 : (uint8_t)(check_input_convention_abi(merged, true) + 1);
        }
        else
        {
            bool reported = false;
            // When a C23 specifier attribute and an ordinary declarator attribute meet on the same function,
            // clang diagnoses the specifier attribute against the already-applied declaration convention.
            for (uint32_t i = 0; !reported && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->specifier_applied)
                {
                    check_input_attribute_conflict(reader, attributes, attribute, (uint32_t)attribute->value, check_input_abi_call(requested));
                    reported = true;
                }
            }
            for (uint32_t i = 0; !reported && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !attribute->specifier_ignored && !attribute->specifier_applied && attribute->value != CHECK_CALL_IGNORED_X86_32)
                {
                    check_input_attribute_conflict(reader, attributes, attribute, (uint32_t)attribute->value, check_input_abi_call((TypeFunctionABI)(origin - 1)));
                }
            }
        }
    }
    return result;
}

// A parameter's Swift ABI (#1447, CHECK_ATTRIBUTE_PARAMETER_ABI), from its declaration's attribute lists (its specifiers'
// and its declarator's, in order), as clang's SemaSwift::AddParameterABIAttr gives it to the parameter: an attribute whose
// type its ABI doesn't take is its error, at its name, and gives none ("must have pointer type": swift_context's,
// swift_async_context's and swift_indirect_result's; "pointer to unqualified pointer type": swift_error_result's); one of
// another ABI than an earlier one's is "A and 'B' attributes are not compatible", with its note at the earlier one; one of the
// same, nothing. `adjusted` is the parameter's type as its function takes it, `declared` as it is declared there (with its
// qualifiers and typedef, whose declaration is `spelling`), which the error prints. *token is the attribute's first token
// (BUSTER_CHECK_NONE for none); the result is false after an error.
static bool check_input_swift_parameter(CheckInputReader* reader, CheckInputAttributes* const* lists, uint32_t list_count, TypeId adjusted, TypeId declared,
                                        uint32_t spelling, uint8_t* abi, uint32_t* token)
{
    const TypeTable* types = reader->types;
    bool valid = true;
    const CheckInputAttribute* kept = NULL;
    for (uint32_t list = 0; list < list_count; list += 1)
    {
        CheckInputAttributes* attributes = lists[list];
        for (uint32_t a = 0; valid && attributes != NULL && a < attributes->count; a += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + a;
            if (attribute->kind == CHECK_ATTRIBUTE_PARAMETER_ABI && !attribute->ignored)
            {
                const uint32_t value = (uint32_t)attribute->value;
                const TypeId stripped = type_strip_aligned(types, adjusted);
                const bool pointer = type_kind(types, stripped) == BUSTER_TYPE_KIND_POINTER;
                const TypeId pointee = pointer ? types->types.base[type_index(stripped)] : 0;
                // (swift_error_result's pointee: a pointer without qualifiers of its own, clang's hasLocalQualifiers.)
                const bool pointer_pointer = pointer && type_qualifiers(pointee) == 0 && type_kind(types, type_strip_aligned(types, pointee)) == BUSTER_TYPE_KIND_POINTER;
                const bool error_result = value == TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT;
                if (kept != NULL && kept->value != attribute->value)
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_attribute_argument_error(reader, attributes, attribute, attribute->name_token, CHECK_ARGUMENT_SWIFT_PARAMETER_CONFLICT,
                                                         value | (uint32_t)kept->value << 8);
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].note_token = kept->name_token;
                        reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
                    }
                    valid = false;
                }
                else if (kept == NULL && !(error_result ? pointer_pointer : pointer))
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_attribute_argument_error(reader, attributes, attribute, attribute->name_token, CHECK_ARGUMENT_SWIFT_PARAMETER_TYPE,
                                                         value | (error_result ? BUSTER_CHECK_SWIFT_POINTER_POINTER : 0));
                    if (reader->output->report_count > before)
                    {
                        reader->output->reports[before].type = declared;
                        reader->output->reports[before].type_spelling = spelling;
                    }
                    valid = false;
                }
                else if (kept == NULL)
                {
                    kept = attribute;
                }
            }
        }
    }
    *abi = kept != NULL && valid ? (uint8_t)kept->value : TYPE_PARAMETER_ABI_ORDINARY;
    *token = kept != NULL && valid ? kept->first : BUSTER_CHECK_NONE;
    return valid;
}

// A function type's parameters' Swift ABIs against its convention and their order (#1447, clang's checkExtParameterInfos):
// swift_indirect_result and swift_context of a swiftcall or swiftasynccall function, and swift_error_result of a swiftcall
// one (swift_async_context of any: clang's switch checks no convention of it); an indirect result first or after another, and an error result just after a
// context. Each error is at its parameter (`places`, its name or where an unnamed one's would be); `attributes` are each
// parameter's attribute's first token, the report's. Whether there was none.
static bool check_input_swift_parameters_as(CheckInputReader* reader, TypeId function, TypeFunctionABI convention, const uint32_t* places,
                                            const uint32_t* attributes, uint32_t count)
{
    const TypeTable* types = reader->types;
    const bool swift = convention == TYPE_FUNCTION_ABI_SWIFT;
    const bool either = swift || convention == TYPE_FUNCTION_ABI_SWIFT_ASYNC;
    bool valid = true;
    // The convention's error is the function's first incompatible parameter's alone (clang's hasCheckedSwiftCall).
    bool convention_checked = false;
    for (uint32_t i = 0; type_function_has_parameter_abis(types, function) && i < count; i += 1)
    {
        const TypeParameterABI abi = type_parameter_abi(types, function, i);
        const TypeParameterABI previous = i != 0 ? type_parameter_abi(types, function, i - 1) : TYPE_PARAMETER_ABI_ORDINARY;
        const bool error_result = abi == TYPE_PARAMETER_ABI_SWIFT_ERROR_RESULT;
        uint32_t error = BUSTER_CHECK_NONE;
        uint32_t number = abi;
        if (abi != TYPE_PARAMETER_ABI_ORDINARY && abi != TYPE_PARAMETER_ABI_SWIFT_ASYNC_CONTEXT && !(error_result ? swift : either) && !convention_checked)
        {
            error = CHECK_ARGUMENT_SWIFT_PARAMETER_CONVENTION;
            number |= error_result ? BUSTER_CHECK_SWIFT_ONLY : 0;
            convention_checked = true;
        }
        const uint32_t order = abi == TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT && i != 0 && previous != TYPE_PARAMETER_ABI_SWIFT_INDIRECT_RESULT ?
                               CHECK_ARGUMENT_SWIFT_INDIRECT_FIRST :
                               error_result && previous != TYPE_PARAMETER_ABI_SWIFT_CONTEXT ? CHECK_ARGUMENT_SWIFT_ERROR_AFTER_CONTEXT : BUSTER_CHECK_NONE;
        // (clang gives both, the convention's first: the order's is kept at the same place as a repeated report, which the
        // driver doesn't fold into the first, #1447.)
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            const uint32_t kind = pass == 0 ? error : order;
            if (kind != BUSTER_CHECK_NONE)
            {
                const uint32_t before = reader->output->report_count;
                check_input_report_argument(reader, places[i], CHECK_INPUT_ATTRIBUTE_ARGUMENT, kind | (pass == 0 ? number : abi) << 8);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].attribute = attributes[i];
                    reader->output->reports[before].repeated |= pass == 1 && error != BUSTER_CHECK_NONE;
                }
                valid = false;
            }
        }
    }
    return valid;
}

// check_input_swift_parameters_as with the function type's own convention.
static bool check_input_swift_parameters(CheckInputReader* reader, TypeId function, const uint32_t* places, const uint32_t* attributes, uint32_t count)
{
    return check_input_swift_parameters_as(reader, function, type_function_convention(reader->types, function), places, attributes, count);
}

// Whether `type` is a function or wraps one in pointers, arrays and aligned typedefs, where a calling convention applies.
static bool check_input_attribute_function_wrapped(const TypeTable* types, TypeId type)
{
    TypeId function = type;
    bool wrapped = type != 0;
    while (wrapped)
    {
        const uint8_t kind = type_kind(types, function);
        wrapped = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED;
        function = wrapped ? types->types.base[type_index(function)] : function;
    }
    return type != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
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
        wrapped = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED;
        function = wrapped ? reader->types->types.base[type_index(function)] : function;
    }
    const bool is_function = type_kind(reader->types, function) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t current = CHECK_CALL_DEFAULT;
    for (uint32_t i = 0; is_function && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const bool convention = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !attribute->specifier_ignored && !attribute->specifier_applied;
        // One of a wrong count is its count's error alone, at its subject's check (CheckInputAttribute.convention_count).
        if (convention && attribute->convention_count == BUSTER_CHECK_COUNT_NONE && check_input_attribute_pcs(reader, attributes, attribute, false))
        {
            const uint32_t call = attribute->value == CHECK_CALL_IGNORED_X86_32 ? CHECK_CALL_SYSV : (uint32_t)attribute->value;
            // (A GPU kernel's convention is a function's declaration's alone, not a pointer's to one.)
            const bool wrapped_kernel = check_input_attribute_kernel(reader, attribute) &&
                                        type_kind(reader->types, type_strip_aligned(reader->types, type)) != BUSTER_TYPE_KIND_FUNCTION;
            if (attribute->value == CHECK_CALL_IGNORED_X86_32 && !wrapped_kernel)
            {
                check_input_attribute_unsupported(reader, attribute);
            }
            // no_caller_saved_registers is clang's function type bit beside whatever convention the function has
            // (check_input_convention_merge, #1447); two conventions of their own are clang's conflict.
            // (One a later no_caller_saved_registers hid conflicts with none after it, CHECK_CALL_HIDDEN.)
            const uint32_t base = check_input_convention_base(call);
            const uint32_t current_base = check_input_convention_base(current);
            if (base != CHECK_CALL_DEFAULT && check_input_convention_visible(current) && base != current_base)
            {
                check_input_attribute_conflict(reader, attributes, attribute, base, current_base);
            }
            current = check_input_convention_merge((CheckInputCallingConvention)current, (CheckInputCallingConvention)call);
        }
    }
}

// Calling conventions attach to the first function inside a completed declarator, through its pointer, array and
// aligned-typedef wrappers. Preserve all those wrappers and their qualifiers; attributes on a scalar subject are
// warning-only ignored metadata. A prefix caller waits for the full declarator before invoking this helper.
// Always inlined (#880), as profile-guided builds do; hot callers: check_input_declarator_step and
// check_input_declaration.
static inline __attribute__((always_inline)) TypeId check_input_attribute_abi_with_override(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type,
                                                                                          bool replace_wrapped_abi)
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
            wrapped = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED;
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
            // The function type's convention (a typedef's, System V's where it has none) with the requested one:
            // no_caller_saved_registers' bit beside either (check_input_convention_merge, #1447), unless their own
            // conventions differ.
            const uint32_t previous_call = previous == TYPE_FUNCTION_ABI_SYSV ? CHECK_CALL_DEFAULT : check_input_abi_call(previous);
            // (The list's own, so that explicit sysv_abi with the bit is System V's, and a no_caller_saved_registers before its
            // conventions hides the typedef's, CHECK_CALL_STRIPS.)
            const uint32_t requested_call = (uint32_t)attributes->calling_convention;
            const uint32_t previous_base = check_input_convention_base(previous_call);
            const uint32_t requested_base = check_input_convention_base(requested_call);
            const bool compatible = previous_base == CHECK_CALL_DEFAULT || requested_base == CHECK_CALL_DEFAULT || previous_base == requested_base ||
                                    check_input_convention_strips(requested_call);
            const CheckInputAttributes merged = {
                .calling_convention = check_input_convention_merge((CheckInputCallingConvention)previous_call, (CheckInputCallingConvention)requested_call),
            };
            const TypeFunctionABI kept = compatible ? check_input_attribute_requested_abi(&merged) : abi;
            result = compatible || replace_wrapped_abi ?
                     type_function_like(reader->types, type_return(reader->types, function), parameters, count,
                                        reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_KEY, kept, function) : 0;
            if (compatible || replace_wrapped_abi)
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
            if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !attribute->specifier_applied)
            {
                attribute->ignored |= !is_function;
                if (result == 0 && reader->output->resource)
                {
                    check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_RESOURCE);
                }
                else if (result == 0 && (attribute->value == CHECK_CALL_VECTOR || attribute->value == CHECK_CALL_REG || attribute->value == CHECK_CALL_SWIFT ||
                                         attribute->value == CHECK_CALL_SWIFT_ASYNC) &&
                         (reader->types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
                {
                    // vectorcall and regcall take no variadic function (#147, clang's supportsVariadicCall): its error.
                    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_VARIADIC_CONVENTION, (uint32_t)attribute->value);
                }
                else if (result == 0 && attribute->value != CHECK_CALL_IGNORED_X86_32 && attribute->value != CHECK_CALL_NO_CALLER_SAVED)
                {
                    // The function's type has another convention (a typedef's); no_caller_saved_registers' bit goes beside
                    // any (#1447).
                    check_input_attribute_conflict(reader, attributes, attribute, (uint32_t)attribute->value, check_input_abi_call(previous));
                }
            }
        }
        arena_pop_to(reader->scratch, mark);
    }
    return attributes == 0 || attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? result : 0;
}

static inline __attribute__((always_inline)) TypeId check_input_attribute_abi(CheckInputReader* reader, CheckInputAttributes* attributes, TypeId type)
{
    return check_input_attribute_abi_with_override(reader, attributes, type, false);
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
        case CHECK_ATTRIBUTE_NODEREF:
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
        case CHECK_ATTRIBUTE_MS_STRUCT:
        case CHECK_ATTRIBUTE_GCC_STRUCT:
        case CHECK_ATTRIBUTE_ALIGNAS:
        case CHECK_ATTRIBUTE_MODE:
        case CHECK_ATTRIBUTE_VECTOR_SIZE:
        case CHECK_ATTRIBUTE_EXT_VECTOR_TYPE:
        case CHECK_ATTRIBUTE_ADDRESS_SPACE:
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

static bool check_cpu_dispatch_invalid(const CheckInputReader* reader, uint32_t declaration);

// How multiversioning refused the declaration of binding `handle` (#2135): 0 not at all; 1 for its own attributes or
// rules (a report at it) as its function's first declaration, which clang keeps as the previous declaration but not as
// one that made the function multiversioned; 2 for its own attributes or rules as a redeclaration, or as the declaration
// that made the function multiversioned (the caused-here note at it, with the report at the one before), which clang
// leaves out of the function's lookup.
static uint32_t check_input_multiversion_refused(const CheckInputReader* reader, uint32_t handle)
{
    const uint32_t declaration = reader->bindings.declaration[handle - 1];
    uint32_t refused = 0;
    for (uint32_t r = 0; refused != 2 && r < reader->output->multiversion_report_count; r += 1)
    {
        const CheckInputReport* report = reader->output->multiversion_reports + r;
        const bool caused = report->note_kind == CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED;
        refused = caused && report->note_token == declaration ? 2 : !caused && report->token == declaration ? 1 : refused;
    }
    refused = refused == 0 && reader->cpu_invalid_count != 0 && check_cpu_dispatch_invalid(reader, declaration) ? 1 : refused;
    refused = refused == 1 && reader->bindings.previous[handle - 1] != 0 ? 2 : refused;
    return refused;
}

// clang's Sema checks for GNU's function multiversioning (#839), at -fsyntax-only: a target("default") function, or one
// with target_clones, cpu_specific or cpu_dispatch, refuses every other declaration attribute clang keeps but used (for
// target alone), nonnull and nothrow, at the function's name. A target attribute beside target_clones, cpu_specific or
// cpu_dispatch is incompatible with it: the later attribute's error, with clang's note at the earlier one. A target
// declaration that makes a function multiversioned with an earlier one checks the earlier declaration's attributes
// first, at its name, with clang's note at the new declaration. `previous` is the function's previous declaration's
// attributes, if any.
static void check_input_attribute_multiversion(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name,
                                               const CheckInputAttributes* previous, uint32_t previous_name, uint32_t previous_handle,
                                               bool inline_function, bool previous_inline)
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
        const uint32_t before = reader->output->multiversion_report_count;
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
        for (uint32_t r = before; r < reader->output->multiversion_report_count; r += 1)
        {
            CheckInputReport* report = reader->output->multiversion_reports + r;
            report->completion = reader->attribute_completion;
            report->suborder = (uint64_t)attribute->first + 2;
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
    // The function's declarations before this one, along its name's binding chain (#2135), but those multiversioning
    // refused, which clang leaves out of its lookup (check_input_multiversion_refused): the one before it (clang's OldFD,
    // whose attributes include its earlier declarations'), whether one made the function multiversioned already
    // (target("default"), target_clones, or two target strings), and whether this declaration redeclares one of its
    // versions (an earlier declaration's target string, or target_clones again), which clang takes as that version's
    // redeclaration before any rule (CheckMultiVersionAdditionalDecl): its attributes are merged, not checked.
    (void)previous;
    (void)previous_name;
    const uint32_t current_target = check_input_attribute_target_index(attributes);
    const bool current_clones = first_clones != BUSTER_CHECK_NONE;
    bool multiversioned = false;
    bool redeclaration = false;
    String8 first_text = { 0 };
    uint32_t before = BUSTER_CHECK_NONE;
    uint32_t before_handle = 0;
    for (uint32_t h = previous_handle; h != 0 && h - 1 < reader->bindings.count; h = reader->bindings.previous[h - 1])
    {
        const uint32_t declaration = reader->bindings.declaration[h - 1];
        const uint32_t refused = declaration < reader->parser->tokens.count ? check_input_multiversion_refused(reader, h) : 0;
        const bool function = declaration < reader->parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION && refused != 2;
        // (A first declaration refused for its own attributes is the previous declaration still, but made nothing
        // multiversioned.)
        const CheckInputAttributes* earlier = function && refused == 0 && check_sparse_wide_present(reader->output->attributes) ?
                                              check_input_attributes_at(reader->output, declaration) : NULL;
        const uint32_t earlier_target = check_input_attribute_target_index(earlier);
        const bool earlier_clones = check_input_attribute_other_multiversion(earlier);
        const String8 text = earlier_target != BUSTER_CHECK_NONE ? earlier->records[earlier_target].text : (String8){ 0 };
        multiversioned |= earlier_clones || (earlier_target != BUSTER_CHECK_NONE && check_input_attribute_equal(text, "default")) ||
                          (earlier_target != BUSTER_CHECK_NONE && first_text.pointer != NULL && !check_input_attribute_text_equal(text, first_text));
        first_text = first_text.pointer == NULL && earlier_target != BUSTER_CHECK_NONE ? text : first_text;
        redeclaration |= (current_target != BUSTER_CHECK_NONE && earlier_target != BUSTER_CHECK_NONE &&
                          check_input_attribute_text_equal(attributes->records[current_target].text, text)) ||
                         (current_clones && earlier_clones);
        before_handle = before == BUSTER_CHECK_NONE && function ? h : before_handle;
        before = before == BUSTER_CHECK_NONE && function ? declaration : before;
    }
    const CheckInputAttributes* before_attributes = before != BUSTER_CHECK_NONE && check_sparse_wide_present(reader->output->attributes) ?
                                                    check_input_attributes_at(reader->output, before) : NULL;
    const bool before_inline = before != BUSTER_CHECK_NONE && reader->output->function_flags != NULL &&
                               (reader->output->function_flags[before] & CHECK_INPUT_FUNCTION_INLINE) != 0;
    // A new target declaration that makes the function multiversioned with the one before it checks that one's attributes
    // first (its own and those its earlier declarations gave it), at its name, and its own only if they are clean.
    bool causes = false;
    if (before_attributes != NULL && !check_input_attribute_other_multiversion(before_attributes) && !multiversioned && !redeclaration)
    {
        const uint32_t previous_target = check_input_attribute_target_index(before_attributes);
        causes = current_target != BUSTER_CHECK_NONE &&
                 (check_input_attribute_equal(attributes->records[current_target].text, "default") ||
                  (previous_target != BUSTER_CHECK_NONE &&
                   !check_input_attribute_text_equal(attributes->records[current_target].text, before_attributes->records[previous_target].text)));
    }
    if (causes)
    {
        const CheckInputAttributes* holder = before_attributes;
        uint32_t bad = check_input_attribute_multiversion_disallowed(before_attributes, CHECK_MULTIVERSION_TARGET, NULL, BUSTER_CHECK_NONE, before_inline);
        for (uint32_t h = reader->bindings.previous[before_handle - 1]; bad == BUSTER_CHECK_NONE && h != 0 && h - 1 < reader->bindings.count; h = reader->bindings.previous[h - 1])
        {
            const uint32_t declaration = reader->bindings.declaration[h - 1];
            const bool function = declaration < reader->parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                                  check_input_multiversion_refused(reader, h) != 2;
            holder = function && check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, declaration) : NULL;
            bad = check_input_attribute_multiversion_disallowed(holder, CHECK_MULTIVERSION_TARGET, NULL, BUSTER_CHECK_NONE, before_inline);
        }
        if (bad != BUSTER_CHECK_NONE)
        {
            const uint32_t reports = reader->output->multiversion_report_count;
            check_input_report_multiversion(reader, before, CHECK_INPUT_MULTIVERSION_ATTRIBUTE, CHECK_MULTIVERSION_TARGET,
                                            holder->records[bad].name_token, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, 0);
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
    // A new version of a function multiversioned already is checked as one (CheckMultiVersionAdditionalDecl's rules).
    if (multiversioned && !redeclaration && active == BUSTER_CHECK_NONE && first_target != BUSTER_CHECK_NONE && !incompatible[first_target])
    {
        active = first_target;
        active_kind = CHECK_MULTIVERSION_TARGET;
    }
    if (active != BUSTER_CHECK_NONE && !redeclaration)
    {
        const uint32_t bad = check_input_attribute_multiversion_disallowed(attributes, active_kind, incompatible, active, inline_function);
        if (bad != BUSTER_CHECK_NONE)
        {
            check_input_report_multiversion(reader, name, CHECK_INPUT_MULTIVERSION_ATTRIBUTE, (uint8_t)active_kind,
                                            attributes->records[bad].name_token, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, 0);
        }
    }
    arena_pop_to(reader->scratch, mark);
}

#include "c_check_cpu_dispatch.c"
#include "c_check_target_versions.c"

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
                         check_input_attribute_string(reader, attribute->argument_first, attribute->argument_end, false, &attribute->text) ? attribute->text : (String8){ 0 };
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


// patchable_function_entry where its subject takes it (#1447): clang's handlePatchableFunctionEntryAttr's checks, in its
// order, each an error that drops it: its count's, then its offset's, as checkUInt32Argument reads them strictly unsigned
// (CheckInputAttribute.constants, wide and argument_values: no integer constant, "'X' attribute requires parameter N to be
// an integer constant" at the attribute, N from 0; past 32 bits, err_ice_too_large at the argument's expression; negative,
// "'X' attribute requires a non-negative integral compile time constant expression" at the attribute); an offset past the
// count, "'X' attribute requires integer constant between 0 and N inclusive" at the attribute; and an empty section (its
// string, `text`), at the string. Whether it stands.
static bool check_input_patchable_entry(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute)
{
    const uint32_t count = attribute->argument_count;
    const int64_t* values = attribute->argument_values;
    bool valid = values != NULL;
    for (uint32_t i = 0; valid && i < count && i < 2; i += 1)
    {
        uint32_t first = attribute->argument_first;
        uint32_t end = attribute->argument_end;
        check_input_attribute_argument(attribute, i, &first, &end);
        const int64_t number = values[i];
        if (((attribute->constants >> i) & 1u) == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, i);
            valid = false;
        }
        else if (((attribute->wide >> i) & 1u) != 0)
        {
            check_input_attribute_too_large(reader, attributes, attribute, first, end, number);
            valid = false;
        }
        else if (number < 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_NON_NEGATIVE, 0);
            valid = false;
        }
    }
    if (valid && count >= 2 && values[1] > values[0])
    {
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_BETWEEN, 0);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].value = (uint64_t)values[0];
        }
        valid = false;
    }
    uint32_t section = attribute->argument_end;
    uint32_t section_end = attribute->argument_end;
    if (valid && count == 3 && check_input_attribute_argument(attribute, 2, &section, &section_end) && attribute->text.length == 0)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, section, CHECK_ARGUMENT_PATCHABLE_SECTION, 0);
        valid = false;
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
                            check_input_attribute_string(reader, attribute->argument_first, attribute->argument_end, false, &attribute->text);
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
    else if ((check_input_attribute_equal(spelled, "clang_builtin_alias") || check_input_attribute_equal(spelled, "builtin_alias")) && attribute->argument_count == 1)
    {
        // (No builtin of x86-64's is one of its targets'. C23's clang::builtin_alias is the same attribute, #2291.)
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

// ---------------------------------------------------------------------------------------------------------------------
// clang's checks-only attributes (#1447): enforce_tcb, enforce_tcb_leaf, callback, swift_async, ns_error_domain and
// modular_format, whose handlers in C on x86-64 Linux check their subjects, arguments and redeclarations and change
// nothing a program does, but TCB's warnings at calls. check_input_attribute_checks makes a declaration's own checks;
// c_check_attribute_checks_input.c its redeclarations' (check_input_attribute_merge_checks), modular_format's format
// (check_input_modular_format) and TCB's calls (check_input_tcb_calls).

// The end of an attribute's argument that starts at `first`, before `end`: its next comma at its top level, or `end`.
static uint32_t check_input_checks_argument_end(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t stop = first;
    while (stop < end && parser->tokens.kinds[stop] != PARSE_KIND_COMMA)
    {
        stop = check_input_attribute_after_group(parser, stop, end);
    }
    return stop < end ? stop : end;
}

// Whether the identifier `name` is spelled `text`, as written (no keyword's underscores stripped).
static bool check_input_checks_spelled(const CheckInputReader* reader, uint32_t name, const char* text)
{
    const uint64_t length = strlen(text);
    return reader->pp != NULL && name != 0 && intern_length(reader->pp->table, name) == length && memcmp(intern_bytes(reader->pp->table, name), text, length) == 0;
}

// A string literal argument from `first` to `end`, decoded, or an empty one where it is none.
static String8 check_input_checks_string(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    String8 text = { "", 0 };
    const bool string = first < end && reader->parser->tokens.kinds[first] == PARSE_KIND_STRING;
    if (!(string && check_input_attribute_string(reader, first, end, false, &text)))
    {
        text = (String8){ "", 0 };
    }
    return text;
}

// Whether `attribute` is a kept record of a generic attribute named `name` (its keyword's underscores stripped).
static bool check_input_checks_named(CheckInputReader* reader, const CheckInputAttribute* attribute, const char* name)
{
    const bool kept = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && !attribute->ignored && !attribute->subject_ignored &&
                      attribute->count_check == BUSTER_CHECK_COUNT_NONE;
    return kept && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), name);
}

// A TCB attribute's kind: 1 for a kept enforce_tcb, 2 for a kept enforce_tcb_leaf, 0 for anything else.
static uint32_t check_input_tcb_kind(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    return check_input_checks_named(reader, attribute, "enforce_tcb") ? 1u : check_input_checks_named(reader, attribute, "enforce_tcb_leaf") ? 2u : 0u;
}

// A TCB attribute's TCB, its string decoded.
static String8 check_input_tcb_name(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    return check_input_checks_string(reader, attribute->argument_first, attribute->argument_end);
}

// The conflict of TCB attributes (#1447, clang's err_tcb_conflicting_attributes) at `attribute`, which `other` of the other
// kind and the same TCB is: the declaration's first attribute error, `other`'s name in the report's value, the TCB's
// string token above it; and its note at `note` where it has one (a redeclaration's).
static void check_input_tcb_conflict(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, const CheckInputAttribute* other,
                                     uint32_t note)
{
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_TCB_CONFLICT, 0);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].value = (uint64_t)other->name_token | (uint64_t)attribute->argument_first << 32;
        reader->output->reports[before].note_token = note;
        reader->output->reports[before].note_kind = note != BUSTER_CHECK_NONE ? CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE : CHECK_INPUT_NOTE_NONE;
    }
}

// A second error of an attribute's argument, after the declaration's first (check_input_attribute_argument_error), at
// `token`, ordered as at `sort` (0: its own token), which the driver keeps with the first.
static void check_input_checks_second_error(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t token, CheckAttributeArgumentError error,
                                            uint32_t number, uint32_t sort)
{
    const uint32_t before = reader->output->report_count;
    check_input_report_argument(reader, token, CHECK_INPUT_ATTRIBUTE_ARGUMENT, (uint32_t)error | number << 8);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].attribute = attribute->first;
        reader->output->reports[before].repeated = true;
        reader->output->reports[before].sort = sort;
    }
}

// callback's checks (#1447, clang's handleCallbackAttr), on a function of type `function` whose declarator's named
// parameters CheckInputReader.attribute_parameters links: each argument a parameter's name (`__` the unknown callee -1,
// `this` 0, a parameter of either name taking it over) or an index as clang's checkUInt32Argument reads it, from -1 to
// the parameters' count, not 0 (C has no implicit object parameter); the first's (not -1) a parameter of a pointer to a
// function with a prototype that takes the others' count and isn't variadic; and no callback before it on the
// declaration. An argument of no integer constant, or of more than 32 bits, is two errors, the second "out of bounds".
// Whether the declaration goes on.
static bool check_input_attribute_callback(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId function)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const bool prototyped = (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    uint32_t parameter_count = 0;
    const TypeId* parameter_types = type_parameters(types, function, &parameter_count);
    const int64_t bound = prototyped ? (int64_t)parameter_count : 0;
    const uint32_t names = reader->attribute_parameters != 0 ? reader->attribute_parameters - 1 : BUSTER_CHECK_NONE;
    // A definition's identifier list names parameters its type doesn't count, which clang reads past its type's, and more
    // than 32 arguments (CheckInputAttribute.constants' bits): refused by name.
    bool valid = (prototyped || names == BUSTER_CHECK_NONE) && attribute->argument_count <= 32;
    if (!valid)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
    }
    int64_t callee = 0;
    uint32_t first = attribute->argument_first;
    for (uint32_t i = 0; valid && i < attribute->argument_count; i += 1)
    {
        const uint32_t stop = check_input_checks_argument_end(parser, first, attribute->argument_end);
        const bool lone = stop == first + 1 && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
        int64_t index = 0;
        if (lone)
        {
            const uint32_t word = parser->tokens.names[first];
            bool known = check_input_checks_spelled(reader, word, "__") || check_input_checks_spelled(reader, word, "this");
            index = check_input_checks_spelled(reader, word, "__") ? -1 : 0;
            for (uint32_t parameter = names; parameter != BUSTER_CHECK_NONE; parameter = check_sparse_get(reader->parameter_next, parameter))
            {
                const bool named = parser->tokens.names[parameter] == word;
                index = named ? (int64_t)check_sparse_get(reader->parameters, parameter) + 1 : index;
                known |= named;
            }
            if (!known)
            {
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_UNKNOWN, 0);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].value = first;
                }
                valid = false;
            }
        }
        else
        {
            const bool evaluated = i < 32 && ((attribute->constants >> i) & 1u) != 0 && attribute->argument_values != NULL;
            const bool wide = i < 32 && ((attribute->wide >> i) & 1u) != 0;
            const uint32_t location = check_input_expression_location(parser, first, stop);
            if (!evaluated)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, i + 1);
                check_input_checks_second_error(reader, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, i + 1, 0);
                valid = false;
            }
            else if (wide)
            {
                check_input_attribute_too_large(reader, attributes, attribute, first, stop, attribute->argument_values[i]);
                check_input_checks_second_error(reader, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, i + 1, location + 1);
                valid = false;
            }
            index = valid ? (int64_t)(int32_t)(uint32_t)attribute->argument_values[i] : 0;
            if (valid && (index < -1 || index > bound))
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, i + 1);
                valid = false;
            }
        }
        if (valid && index == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_THIS, i + 1);
            valid = false;
        }
        callee = i == 0 ? index : callee;
        first = stop + 1;
    }
    // The callee's parameter: a pointer to a function with a prototype.
    const TypeId parameter = valid && callee >= 1 && callee <= (int64_t)parameter_count ? type_unqualified(type_strip_aligned(types, parameter_types[callee - 1])) : 0;
    const TypeId pointee = parameter != 0 && type_kind(types, parameter) == BUSTER_TYPE_KIND_POINTER ?
                           type_unqualified(type_strip_aligned(types, types->types.base[type_index(parameter)])) : 0;
    const bool callable = pointee != 0 && type_kind(types, pointee) == BUSTER_TYPE_KIND_FUNCTION &&
                          (types->types.flags[type_index(pointee)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    uint32_t callee_count = 0;
    if (callable)
    {
        type_parameters(types, pointee, &callee_count);
    }
    const CheckInputAttribute* earlier = NULL;
    for (const CheckInputAttribute* other = attributes->records; earlier == NULL && other < attribute; other += 1)
    {
        earlier = check_input_checks_named(reader, other, "callback") ? other : NULL;
    }
    if (valid && callee < 1)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_INVALID_CALLEE, 0);
        valid = false;
    }
    else if (valid && !callable)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_NOT_FUNCTION, 0);
        valid = false;
    }
    else if (valid && callee_count != attribute->argument_count - 1)
    {
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_COUNT, callee_count);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].type = pointee;
            reader->output->reports[before].value = attribute->argument_count - 1;
        }
        valid = false;
    }
    else if (valid && (types->types.flags[type_index(pointee)] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_VARIADIC, 0);
        valid = false;
    }
    else if (valid && earlier != NULL)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_MULTIPLE, 0);
        valid = false;
    }
    return valid;
}

// Where clang places a function's parameter (`index`, from 0) whose declarator `name` names (#1447, ParmVarDecl's
// location): its name, or an unnamed one's place for it, after its specifiers, pointers and qualifiers, inside a grouping
// declarator's parentheses (the token there: `,`, `)`, `[` or a function declarator's `(`). BUSTER_CHECK_NONE where
// the function's parameter list isn't found.
static uint32_t check_input_checks_parameter_place(CheckInputReader* reader, uint32_t name, uint32_t index)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t open = name + 1;
    while (open < count && parser->tokens.kinds[open] == PARSE_KIND_R_PAREN)
    {
        open += 1;
    }
    const uint32_t close = open < count && parser->tokens.kinds[open] == PARSE_KIND_L_PAREN ? parser->tree.parents[open] : count;
    uint32_t first = open + 1;
    for (uint32_t i = 0; close < count && i < index && first < close; i += 1)
    {
        first = check_input_checks_argument_end(parser, first, close) + 1;
    }
    const uint32_t end = close < count && first <= close ? check_input_checks_argument_end(parser, first, close) : count;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = first; result == BUSTER_CHECK_NONE && end < count && t < end; t += 1)
    {
        result = check_sparse_get(reader->parameters, t) == index && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER &&
                 reader->declaration_kinds[t] == C_BINDING_OBJECT ? t : BUSTER_CHECK_NONE;
    }
    // Unnamed: past the specifiers (keywords, typedef names, a tag's name and body, attributes, a keyword's group).
    uint32_t t = result == BUSTER_CHECK_NONE && end < count ? first : end;
    bool specifiers = true;
    while (result == BUSTER_CHECK_NONE && end < count && specifiers && t < end)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool keyword = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE || kind == PARSE_KIND_ATOMIC ||
                             kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_EXTENSION;
        const bool grouped = (kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATTRIBUTE) &&
                             t + 1 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN;
        const bool tag = kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM;
        const bool typedef_name = kind == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] == PARSE_ROLE_SPECIFIER;
        if (grouped)
        {
            t = check_input_attribute_after_group(parser, t + 1, end);
        }
        else if (tag)
        {
            t += 1;
            t += t < end && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? 1u : 0u;
            t = t < end && parser->tokens.kinds[t] == PARSE_KIND_L_BRACE ? check_input_attribute_after_group(parser, t, end) : t;
        }
        else
        {
            specifiers = keyword || typedef_name;
            t += specifiers ? 1u : 0u;
        }
    }
    // Then the declarator's pointers and qualifiers, and grouping parentheses' insides.
    bool walking = result == BUSTER_CHECK_NONE && end < count;
    while (walking && t < end)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint8_t next = t + 1 < end ? parser->tokens.kinds[t + 1] : PARSE_KIND_END;
        const bool grouping = kind == PARSE_KIND_L_PAREN && (next == PARSE_KIND_STAR || next == PARSE_KIND_L_PAREN || next == PARSE_KIND_L_SQUARE);
        walking = kind == PARSE_KIND_STAR || kind == PARSE_KIND_QUALIFIER || grouping;
        t += walking ? 1u : 0u;
    }
    result = result == BUSTER_CHECK_NONE && end < count ? t : result;
    return result;
}

// swift_async's checks (#1447, clang's SemaSwift::handleAsyncAttr), on a function of type `function` named at `name`: its
// first argument an identifier of its kinds (none, swift_private, not_swift_private); none alone, any other with a
// completion handler's index, as checkFunctionOrMethodParameterIndex reads it ("parameter 2"), whose parameter must be a
// block pointer, which C has none of: always its error there, at the parameter, naming its type. Whether the declaration
// goes on.
// swift_async_error's convention (#1447, SemaSwift's handleAsyncError): 0 for none of clang's, else 1 none, 2 nonnull_error,
// 3 zero_argument and 4 nonzero_argument, as written; a first argument of no identifier is 5.
static uint32_t check_input_swift_async_error_convention(const CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t argument = attribute->argument_first;
    const bool identifier = attribute->argument_count != 0 && argument < attribute->argument_end && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
    const uint32_t word = identifier ? parser->tokens.names[argument] : 0;
    return !identifier ? 5u : check_input_checks_spelled(reader, word, "none") ? 1u : check_input_checks_spelled(reader, word, "nonnull_error") ? 2u :
           check_input_checks_spelled(reader, word, "zero_argument") ? 3u : check_input_checks_spelled(reader, word, "nonzero_argument") ? 4u : 0u;
}

// Whether a declaration's attributes before `before` hold a swift_async_error its handler applied, of a convention other
// than none (#1447): one a function's swift_async(none) can't have (checkSwiftAsyncErrorBlock).
static bool check_input_swift_async_error_of(const CheckInputReader* reader, const CheckInputAttributes* attributes, const CheckInputAttribute* before)
{
    bool found = false;
    for (const CheckInputAttribute* other = attributes->records; !found && other < before; other += 1)
    {
        const bool kept = other->kind == CHECK_ATTRIBUTE_UNKNOWN && !other->keyword && !other->ignored && !other->subject_ignored &&
                          other->count_check == BUSTER_CHECK_COUNT_NONE;
        const uint32_t convention = kept && check_input_attribute_equal(check_input_attribute_name((CheckInputReader*)reader, other->name), "swift_async_error") ?
                                    check_input_swift_async_error_convention(reader, other) : 0u;
        found = convention >= 2 && convention <= 4;
    }
    return found;
}

static bool check_input_attribute_swift_async(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId function, uint32_t name)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const uint32_t first_end = check_input_checks_argument_end(parser, attribute->argument_first, attribute->argument_end);
    const bool lone = first_end == attribute->argument_first + 1 && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER;
    const uint32_t word = lone ? parser->tokens.names[attribute->argument_first] : 0;
    const bool none = check_input_checks_spelled(reader, word, "none");
    const bool kind = none || check_input_checks_spelled(reader, word, "swift_private") || check_input_checks_spelled(reader, word, "not_swift_private");
    const bool prototyped = (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    uint32_t parameter_count = 0;
    const TypeId* parameter_types = type_parameters(types, function, &parameter_count);
    const int64_t bound = prototyped ? (int64_t)parameter_count : 0;
    const bool evaluated = ((attribute->constants >> 1) & 1u) != 0 && attribute->argument_values != NULL && attribute->argument_count >= 2;
    const int64_t index = evaluated ? attribute->argument_values[1] : 0;
    bool valid = false;
    if (!lone)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, 1);
    }
    else if (!kind)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_SWIFT_ASYNC_KIND, 0);
    }
    else if (none && attribute->argument_count != 1)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT | 1u << 8);
    }
    else if (!none && attribute->argument_count != 2)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT | 2u << 8);
    }
    else if (!none && !evaluated)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, 2);
    }
    else if (!none && (index < 1 || index > bound))
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, 2);
    }
    else if (!none)
    {
        // The completion handler's parameter, at its name (or where an unnamed one's would be), its type as declared there.
        const uint32_t place = name < count ? check_input_checks_parameter_place(reader, name, (uint32_t)index - 1) : BUSTER_CHECK_NONE;
        const TypeId parameter = parameter_types[index - 1];
        const bool named = place < count && parser->tokens.kinds[place] == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[place] == C_BINDING_OBJECT;
        const TypeId declared = named ? reader->declared_types[place] : 0;
        // (A parameter's own typedef names it, where its adjustment didn't change it.)
        const bool spelled = named && declared != 0 && type_unqualified(type_strip_aligned(types, declared)) == type_unqualified(type_strip_aligned(types, parameter));
        if (place >= count)
        {
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
        }
        else
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, place, CHECK_ARGUMENT_SWIFT_ASYNC_BLOCK, 0);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].type = spelled ? declared : parameter;
                reader->output->reports[before].type_spelling = spelled ? check_input_typedef_spelling(reader->output, parser, place) : 0;
            }
        }
    }
    else if (check_input_swift_async_error_of(reader, attributes, attribute))
    {
        // An earlier swift_async_error of a convention other than none (clang's checkSwiftAsyncErrorBlock).
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_SWIFT_ASYNC_NONE, 0);
    }
    else
    {
        valid = true;
    }
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    return valid;
}

// swift_async_error's handler (#1447, SemaSwift's handleAsyncError), past its subjects and its count of one or two: its
// convention (check_input_swift_async_error_convention); one clang doesn't know is its warning "'X' attribute argument not
// supported: 'Y'" at the attribute, which ignores it; zero_argument and nonzero_argument take exactly two arguments, the
// second an integer constant in 32 bits (CheckInputAttribute.constants, read with the attributes), and none and
// nonnull_error one; a convention other than none on a function with swift_async(none) is clang's error at the
// swift_async (check_input_swift_async_error_of for the other order). A first argument of no identifier, on which clang 23
// crashes (handleAsyncError reads it as an identifier), is refused by name. Whether the declaration goes on.
static bool check_input_attribute_swift_async_error(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const uint32_t convention = check_input_swift_async_error_convention(reader, attribute);
    const bool indexed = convention == 3 || convention == 4;
    const uint32_t wanted = indexed ? 2u : 1u;
    // (The second argument's tokens: past the first's top-level comma.)
    const uint32_t first = check_input_checks_argument_end(reader->parser, attribute->argument_first, attribute->argument_end) + 1;
    const uint32_t end = attribute->argument_end;
    const bool located = indexed && first < end;
    const bool constant = ((attribute->constants >> 1) & 1u) != 0 && attribute->argument_values != NULL;
    const bool wide = ((attribute->wide >> 1) & 1u) != 0;
    bool valid = false;
    const CheckInputAttribute* none = NULL;
    for (const CheckInputAttribute* other = attributes->records; convention >= 2 && convention <= 4 && none == NULL && other < attribute; other += 1)
    {
        const bool kept = other->kind == CHECK_ATTRIBUTE_UNKNOWN && !other->keyword && !other->ignored && !other->subject_ignored &&
                          other->count_check == BUSTER_CHECK_COUNT_NONE && other->argument_count == 1 &&
                          check_input_attribute_equal(check_input_attribute_name(reader, other->name), "swift_async");
        none = kept && reader->parser->tokens.kinds[other->argument_first] == PARSE_KIND_IDENTIFIER &&
               check_input_checks_spelled(reader, reader->parser->tokens.names[other->argument_first], "none") ? other : NULL;
    }
    if (convention == 5)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
    }
    else if (convention == 0)
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN, attribute->first,
                                             attribute->argument_first);
        attribute->ignored = true;
        valid = true;
    }
    else if (attribute->argument_count != wanted)
    {
        check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, BUSTER_CHECK_COUNT_EXACT | wanted << 8);
    }
    else if (indexed && !constant)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_AN_INTEGER_CONSTANT, 0);
    }
    else if (indexed && located && wide)
    {
        check_input_attribute_too_large(reader, attributes, attribute, first, end, attribute->argument_values[1]);
    }
    else if (none != NULL)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, none->first, CHECK_ARGUMENT_SWIFT_ASYNC_NONE, 0);
    }
    else
    {
        valid = true;
    }
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    return valid;
}

// Whether `text` is an ASCII identifier as clang's isValidAsciiIdentifier takes it: not empty, a letter or `_` first, then
// letters, digits and `_`.
static bool check_input_swift_identifier(String8 text)
{
    bool valid = text.length != 0;
    for (uint64_t i = 0; valid && i < text.length; i += 1)
    {
        const uint8_t c = (uint8_t)text.pointer[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        valid = letter || (i != 0 && c >= '0' && c <= '9');
    }
    return valid;
}

// Whether `text` is a Swift context name as clang 23 takes it: identifiers (check_input_swift_identifier) joined by `.`.
static bool check_input_swift_context(String8 text)
{
    bool valid = text.length != 0;
    uint64_t start = 0;
    for (uint64_t i = 0; valid && i <= text.length; i += 1)
    {
        if (i == text.length || text.pointer[i] == '.')
        {
            valid = check_input_swift_identifier((String8){ text.pointer + start, i - start });
            start = i + 1;
        }
    }
    return valid;
}

static bool check_input_swift_equal(String8 text, const char* literal)
{
    const uint64_t length = strlen(literal);
    return text.length == length && memcmp(text.pointer, literal, length) == 0;
}

// A Swift function name's check as clang's validateSwiftFunctionName makes it (#1447): an optional "getter:" or "setter:",
// a base name (with a context before a `.`), and its parameters' labels in parentheses, each followed by a `:`. Returns
// BUSTER_CHECK_NONE where it is one, with its count of parameters in `*count` and whether it is a single-parameter init
// in `*init`, else the warning's message (BUSTER_CHECK_SWIFT_NAME_*).
static uint32_t check_input_swift_function_name(String8 name, uint32_t* count, bool* init)
{
    uint32_t result = BUSTER_CHECK_NONE;
    const bool getter = name.length >= 7 && memcmp(name.pointer, "getter:", 7) == 0;
    const bool setter = !getter && name.length >= 7 && memcmp(name.pointer, "setter:", 7) == 0;
    String8 rest = getter || setter ? (String8){ name.pointer + 7, name.length - 7 } : name;
    *count = 0;
    *init = false;
    // (An empty name's last character is clang's StringRef::back() past its data; clang 23 takes it for no `)`.)
    uint64_t open = 0;
    while (open < rest.length && rest.pointer[open] != '(')
    {
        open += 1;
    }
    String8 base = { rest.pointer, open };
    String8 parameters = open < rest.length ? (String8){ rest.pointer + open + 1, rest.length - open - 1 } : (String8){ "", 0 };
    uint64_t dot = base.length;
    for (uint64_t i = 0; i < base.length; i += 1)
    {
        dot = base.pointer[i] == '.' ? i : dot;
    }
    // (clang's rsplit at the last `.`: with nothing after it, the part before it is the base, and there is no context.)
    const bool member = dot + 1 < base.length;
    const String8 context = member ? (String8){ base.pointer, dot } : (String8){ "", 0 };
    base = member ? (String8){ base.pointer + dot + 1, base.length - dot - 1 } : dot < base.length ? (String8){ base.pointer, dot } : base;
    const bool subscript = check_input_swift_equal(base, "subscript");
    if (rest.length == 0 || rest.pointer[rest.length - 1] != ')')
    {
        result = BUSTER_CHECK_SWIFT_NAME_FUNCTION;
    }
    else if (member && !check_input_swift_context(context))
    {
        result = BUSTER_CHECK_SWIFT_NAME_CONTEXT;
    }
    else if (!check_input_swift_identifier(base) || check_input_swift_equal(base, "_"))
    {
        result = BUSTER_CHECK_SWIFT_NAME_BASE;
    }
    else if (subscript && !getter && !setter)
    {
        result = BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_ACCESSOR;
    }
    else if (open >= rest.length)
    {
        result = BUSTER_CHECK_SWIFT_NAME_MISSING_PARAMETERS;
    }
    else
    {
        parameters.length -= 1;
    }
    if (result == BUSTER_CHECK_NONE && parameters.length == 0)
    {
        result = subscript ? BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_PARAMETER : setter ? BUSTER_CHECK_SWIFT_NAME_SETTER : BUSTER_CHECK_NONE;
    }
    else if (result == BUSTER_CHECK_NONE)
    {
        uint32_t self = BUSTER_CHECK_NONE;
        uint32_t new_values = 0;
        String8 current = { "", 0 };
        uint64_t at = 0;
        if (parameters.pointer[parameters.length - 1] != ':')
        {
            result = BUSTER_CHECK_SWIFT_NAME_FUNCTION;
        }
        while (result == BUSTER_CHECK_NONE && at < parameters.length)
        {
            uint64_t colon = at;
            while (colon < parameters.length && parameters.pointer[colon] != ':')
            {
                colon += 1;
            }
            current = (String8){ parameters.pointer + at, colon - at };
            at = colon + 1;
            const bool is_self = member && check_input_swift_equal(current, "self");
            if (!check_input_swift_identifier(current))
            {
                result = BUSTER_CHECK_SWIFT_NAME_PARAMETER;
            }
            else if (is_self && self != BUSTER_CHECK_NONE)
            {
                result = BUSTER_CHECK_SWIFT_NAME_SELVES;
            }
            self = is_self ? *count : self;
            new_values += !is_self && check_input_swift_equal(current, "newValue") ? 1u : 0u;
            *count += result == BUSTER_CHECK_NONE ? 1u : 0u;
        }
        const uint32_t expected = (getter ? 0u : 1u) + (self != BUSTER_CHECK_NONE ? 1u : 0u);
        if (result == BUSTER_CHECK_NONE && subscript && self == BUSTER_CHECK_NONE)
        {
            result = BUSTER_CHECK_SWIFT_NAME_SUBSCRIPT_SELF;
        }
        else if (result == BUSTER_CHECK_NONE && subscript && *count < expected)
        {
            result = getter ? BUSTER_CHECK_SWIFT_NAME_GETTER : BUSTER_CHECK_SWIFT_NAME_SETTER;
        }
        else if (result == BUSTER_CHECK_NONE && subscript && setter && new_values == 0)
        {
            result = BUSTER_CHECK_SWIFT_NAME_NO_NEW_VALUE;
        }
        else if (result == BUSTER_CHECK_NONE && subscript && setter && new_values > 1)
        {
            result = BUSTER_CHECK_SWIFT_NAME_NEW_VALUES;
        }
        else if (result == BUSTER_CHECK_NONE && subscript && getter && new_values != 0)
        {
            result = BUSTER_CHECK_SWIFT_NAME_GETTER_NEW_VALUE;
        }
        else if (result == BUSTER_CHECK_NONE && !subscript && (getter || setter) && *count != expected)
        {
            result = getter ? BUSTER_CHECK_SWIFT_NAME_GETTER : BUSTER_CHECK_SWIFT_NAME_SETTER;
        }
        *init = result == BUSTER_CHECK_NONE && *count == 1 && check_input_swift_equal(base, "init") && !check_input_swift_equal(current, "_");
    }
    return result;
}

// swift_name's and swift_async_name's handlers (#1447, SemaSwift's handleName and handleAsyncName, DiagnoseName), past
// their subjects and counts: the name, a string literal, must fit the declaration, or clang's warning at the string
// (CHECK_WARNING_EVENT_SWIFT_NAME), which ignores the attribute. A function's is a Swift function name
// (check_input_swift_function_name) of a prototype's (else -Wignored-attributes' "only applies to non-K&R-style
// functions"), whose count of parameters must be the function's (an async name's one fewer: it drops the completion
// handler, and a function of none is its own warning): more is right only for a single-parameter init of a function of
// none, fewer only where the function's pointers to unqualified pointees, possible out parameters, make up the rest. An
// enumerator's, a variable's, a typedef's, a tag's or a member's is an identifier, with a context before a `.`; another
// declaration's (a label's) is "cannot be applied to this declaration".
static void check_input_attribute_swift_name(CheckInputReader* reader, CheckInputAttribute* attribute, uint32_t declaration, TypeId type, bool async)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t string = attribute->argument_first;
    const String8 name = check_input_checks_string(reader, string, check_input_checks_argument_end(parser, string, attribute->argument_end));
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
    const bool function = (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0 && bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_FUNCTION;
    const bool prototyped = function && (types->types.flags[type_index(bare)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    const uint32_t named = BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_PARAMETER | BUSTER_CHECK_SUBJECT_FIELD | BUSTER_CHECK_SUBJECT_STRUCT |
                           BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM | BUSTER_CHECK_SUBJECT_TYPEDEF | BUSTER_CHECK_SUBJECT_ENUMERATOR;
    uint32_t code = BUSTER_CHECK_NONE;
    uint32_t expected = 0;
    uint32_t given = 0;
    if (function && !prototyped)
    {
        code = BUSTER_CHECK_SWIFT_NAME_PROTOTYPE;
    }
    else if (function)
    {
        uint32_t parameter_count = 0;
        const TypeId* parameters = type_parameters(types, bare, &parameter_count);
        bool init = false;
        uint32_t swift_count = 0;
        code = async && parameter_count == 0 ? BUSTER_CHECK_SWIFT_NAME_NO_PARAMETERS : check_input_swift_function_name(name, &swift_count, &init);
        const uint32_t count = async && parameter_count != 0 ? parameter_count - 1 : parameter_count;
        uint32_t outs = 0;
        for (uint32_t i = 0; i < parameter_count; i += 1)
        {
            const TypeId parameter = type_unqualified(type_strip_aligned(types, parameters[i]));
            const bool pointer = parameter != 0 && type_kind(types, parameter) == BUSTER_TYPE_KIND_POINTER;
            outs += pointer && (type_qualifiers(type_element(types, parameter)) & BUSTER_TYPE_CONST) == 0 ? 1u : 0u;
        }
        const bool fits = swift_count == count || (swift_count > count ? init && count == 0 : swift_count + outs >= count);
        code = code == BUSTER_CHECK_NONE && !fits ? BUSTER_CHECK_SWIFT_NAME_COUNT : code;
        expected = count;
        given = swift_count;
    }
    else if ((declaration & named) != 0 && !async)
    {
        uint64_t dot = name.length;
        for (uint64_t i = 0; i < name.length; i += 1)
        {
            dot = name.pointer[i] == '.' ? i : dot;
        }
        // (clang's rsplit at the last `.`: with nothing after it, the part before it is the base, and there is no context.)
        const bool member = dot + 1 < name.length;
        const String8 base = member ? (String8){ name.pointer + dot + 1, name.length - dot - 1 } : dot < name.length ? (String8){ name.pointer, dot } : name;
        const String8 context = member ? (String8){ name.pointer, dot } : (String8){ "", 0 };
        code = member && !check_input_swift_context(context) ? BUSTER_CHECK_SWIFT_NAME_CONTEXT :
               !check_input_swift_identifier(base) ? BUSTER_CHECK_SWIFT_NAME_BASE : BUSTER_CHECK_NONE;
    }
    else
    {
        code = BUSTER_CHECK_SWIFT_NAME_DECLARATION;
    }
    if (code != BUSTER_CHECK_NONE)
    {
        const CheckWarningEvent event =
        {
            .id = code == BUSTER_CHECK_SWIFT_NAME_PROTOTYPE ? CHECK_WARNING_ID_IGNORED_ATTRIBUTES : CHECK_WARNING_ID_SWIFT_NAME_ATTRIBUTE,
            .kind = CHECK_WARNING_EVENT_SWIFT_NAME,
            .token = string,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = string + 1,
            .payload = { .name = attribute->name, .token = BUSTER_CHECK_WARNING_NO_SOURCE,
                         .data = code | (expected & 0x1fffu) << 5 | (given & 0x1fffu) << 18 },
        };
        check_input_warning_event(reader, &event, NULL, 0);
        attribute->ignored = true;
    }
}

// Whether `record` is the struct clang's Sema takes for CFError (#1447, isCFError): with none yet (CheckInputReader.cf_error),
// a struct whose declaration so far has objc_bridge or objc_bridge_mutable of NSError, which becomes it; then that one alone.
static bool check_input_swift_cf_error(CheckInputReader* reader, TypeId record)
{
    const Parser* parser = reader->parser;
    const bool structure = record != 0 && type_kind(reader->types, record) == BUSTER_TYPE_KIND_STRUCT;
    bool found = reader->cf_error != 0 && type_unqualified(record) == reader->cf_error;
    const CheckSparseWide all = reader->output->attributes;
    const bool scalar = parser->options.scalar;
    const uint32_t end = parser->tokens.count;
    for (uint32_t first = structure && reader->cf_error == 0 ? check_input_attributed_next(all, 0, end, scalar) : end; !found && first < end;
         first = check_input_attributed_next(all, first + 16, end, scalar))
    {
        for (uint32_t selected = check_input_attributed_group(all, first, end, scalar); !found && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            const CheckInputAttributes* attributes = type_unqualified(reader->tag_types[token]) == type_unqualified(record) ?
                                                     check_input_attributes_at(reader->output, token) : NULL;
            for (uint32_t i = 0; !found && attributes != NULL && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                const String8 spelled = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && !attribute->ignored && !attribute->subject_ignored ?
                                        check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
                const bool bridge = check_input_attribute_equal(spelled, "objc_bridge") || check_input_attribute_equal(spelled, "objc_bridge_mutable");
                found = bridge && attribute->argument_count == 1 && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER &&
                        check_input_checks_spelled(reader, parser->tokens.names[attribute->argument_first], "NSError");
            }
        }
    }
    reader->cf_error = found && reader->cf_error == 0 ? type_unqualified(record) : reader->cf_error;
    return found;
}

// swift_error's handler (#1447, SemaSwift's handleError), past its subjects and count: its convention, as written, none,
// nonnull_error, null_result, zero_result or nonzero_result; another identifier is the warning "'X' attribute argument
// not supported: 'Y'" at the attribute, which ignores it. Every convention but none needs an error parameter, a pointer to
// a pointer to CFError (check_input_swift_cf_error): "'X' attribute can only be applied to a function with an error
// parameter"; null_result a function returning a pointer, and zero_result and nonzero_result one returning an integer (an
// enumeration's or bool too): "'X' attribute with 'C' convention can only be applied to a function returning a pointer"
// or "an integral type", each at the attribute. A first argument of no identifier, on which clang 23 crashes
// (handleError reads it as an identifier), is refused by name. Whether the declaration goes on.
static bool check_input_attribute_swift_error(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId type)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const uint32_t argument = attribute->argument_first;
    const bool identifier = attribute->argument_count == 1 && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
    const uint32_t word = identifier ? parser->tokens.names[argument] : 0;
    const uint32_t convention = check_input_checks_spelled(reader, word, "none") ? 1u : check_input_checks_spelled(reader, word, "nonnull_error") ? 2u :
                                check_input_checks_spelled(reader, word, "null_result") ? 3u : check_input_checks_spelled(reader, word, "zero_result") ? 4u :
                                check_input_checks_spelled(reader, word, "nonzero_result") ? 5u : 0u;
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
    const bool function = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t parameter_count = 0;
    const TypeId* parameters = function ? type_parameters(types, bare, &parameter_count) : NULL;
    bool error_parameter = false;
    for (uint32_t i = 0; convention >= 2 && !error_parameter && i < parameter_count; i += 1)
    {
        const TypeId outer = type_unqualified(type_strip_aligned(types, parameters[i]));
        const TypeId inner = outer != 0 && type_kind(types, outer) == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_strip_aligned(types, type_element(types, outer))) : 0;
        const TypeId record = inner != 0 && type_kind(types, inner) == BUSTER_TYPE_KIND_POINTER ? type_unqualified(type_strip_aligned(types, type_element(types, inner))) : 0;
        error_parameter = record != 0 && check_input_swift_cf_error(reader, record);
    }
    const TypeId result = function ? type_unqualified(type_strip_aligned(types, type_return(types, bare))) : 0;
    const uint8_t result_kind = result != 0 ? type_kind(types, result) : 0;
    uint32_t width = 0;
    bool is_signed = false;
    const bool integral = result_kind == BUSTER_TYPE_KIND_ENUM || (result != 0 && check_action_integer_type(types, result, &width, &is_signed));
    bool valid = false;
    if (!identifier)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
    }
    else if (convention == 0)
    {
        check_input_attribute_named_event_at(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN, attribute->first,
                                             argument);
        attribute->ignored = true;
        valid = true;
    }
    else if (convention >= 2 && !error_parameter)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_SWIFT_ERROR_PARAMETER, 0);
    }
    else if ((convention == 3 && result_kind != BUSTER_TYPE_KIND_POINTER) || (convention >= 4 && !integral))
    {
        // (The convention's token in the report's value, which the message quotes.)
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_SWIFT_ERROR_RESULT, convention == 3 ? 1u : 0u);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].value = argument;
        }
    }
    else
    {
        valid = true;
    }
    if (!valid && attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
    {
        attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
        attributes->error_first = attribute->first;
        attributes->error_end = attribute->end;
    }
    return valid;
}

// Whether two modular_format attributes have the same arguments (#1447): their implementation's name, its string, and
// their aspects as sets (clang keeps them sorted; each attribute's are distinct).
static bool check_input_modular_same(CheckInputReader* reader, const CheckInputAttribute* a, const CheckInputAttribute* b)
{
    const Parser* parser = reader->parser;
    bool same = a->argument_count == b->argument_count && parser->tokens.names[a->argument_first] == parser->tokens.names[b->argument_first];
    uint32_t a_first = check_input_checks_argument_end(parser, a->argument_first, a->argument_end) + 1;
    uint32_t b_first = check_input_checks_argument_end(parser, b->argument_first, b->argument_end) + 1;
    const uint32_t a_stop = check_input_checks_argument_end(parser, a_first, a->argument_end);
    const uint32_t b_stop = check_input_checks_argument_end(parser, b_first, b->argument_end);
    same = same && check_input_attribute_text_equal(check_input_checks_string(reader, a_first, a_stop), check_input_checks_string(reader, b_first, b_stop));
    a_first = a_stop + 1;
    b_first = b_stop + 1;
    for (uint32_t i = 2; same && i < a->argument_count && a_first < a->argument_end; i += 1)
    {
        const uint32_t stop = check_input_checks_argument_end(parser, a_first, a->argument_end);
        const String8 aspect = check_input_checks_string(reader, a_first, stop);
        bool found = false;
        for (uint32_t other = b_first; !found && other < b->argument_end;)
        {
            const uint32_t other_stop = check_input_checks_argument_end(parser, other, b->argument_end);
            found = check_input_attribute_text_equal(aspect, check_input_checks_string(reader, other, other_stop));
            other = other_stop + 1;
        }
        same = found;
        a_first = stop + 1;
    }
    return same;
}

static bool check_input_attribute_diagnose_builtin(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId function,
                                                   uint32_t name);

// Objective-C's bridges' handlers (#1447, SemaObjC's handleBridgeAttr, handleBridgeMutableAttr and handleBridgeRelatedAttr),
// past their subjects and counts, which change nothing in C: a first argument of no identifier (an expression, or none of
// objc_bridge_related without parentheses, whose custom parsing skips the count) is clang's error at the declaration's
// first token, as clang's Decl::getBeginLoc gives it (a tag's keyword, a typedef's first token); on a typedef objc_bridge's
// identifier must be `id`, as written, and its type a pointer to void, qualified or not, each clang's error at the
// attribute. Whether the declaration goes on.
static bool check_input_attribute_objc_bridge(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration,
                                              TypeId type, uint32_t begin)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t argument = attribute->argument_first;
    const bool identifier = attribute->argument_count != 0 && argument < attribute->argument_end && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
    const bool bridge = check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "objc_bridge");
    const bool on_typedef = bridge && identifier && (declaration & BUSTER_CHECK_SUBJECT_TYPEDEF) != 0;
    // (As written: clang's isStr("id"), which a keyword's underscores don't pass.)
    const uint32_t word = on_typedef ? parser->tokens.names[argument] : 0;
    const bool id = word != 0 && intern_length(reader->pp->table, word) == 2 && memcmp(intern_bytes(reader->pp->table, word), "id", 2) == 0;
    const TypeId bare = on_typedef && type != 0 ? type_unqualified(type_strip_aligned(types, type)) : 0;
    const bool pointer = bare != 0 && type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER;
    const TypeId pointee = pointer ? type_unqualified(type_strip_aligned(types, type_element(types, bare))) : 0;
    const bool void_pointer = pointee != 0 && type_kind(types, pointee) == BUSTER_TYPE_KIND_VOID;
    bool valid = true;
    if (!identifier)
    {
        const uint32_t first = reader->attribute_declaration != 0 ? reader->attribute_declaration - 1 : begin;
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_OBJC_CLASS, 0);
        valid = false;
    }
    else if (on_typedef && !id)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OBJC_TYPEDEF_ID, 0);
        valid = false;
    }
    else if (on_typedef && !void_pointer)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OBJC_TYPEDEF_POINTER, 0);
        valid = false;
    }
    return valid;
}

// clang's checks-only attributes' handlers (#1447), past their subjects and counts: enforce_tcb's and enforce_tcb_leaf's
// TCB, which no earlier attribute of the declaration has of the other kind (err_tcb_conflicting_attributes, at the later);
// callback's (check_input_attribute_callback), swift_async's (check_input_attribute_swift_async) and diagnose_as_builtin's
// (check_input_attribute_diagnose_builtin, c_check_attribute_checks_input.c); ns_error_domain's
// argument, a variable's name where the attribute is (SemaObjC's handleNSErrorDomain: an ordinary lookup, which a
// function, a typedef or an enumerator fails, at the name, as any other argument does at it); modular_format's
// implementation's name, its aspects (no two the same, at the second), and its arguments, those of an earlier one of
// the declaration (err_attribute_already_applied... at it, with its note at the earlier). Whether the declaration goes on.
static bool check_input_attribute_checks(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration, TypeId type,
                                         uint32_t name, uint32_t begin)
{
    const Parser* parser = reader->parser;
    const String8 spelled = check_input_attribute_name(reader, attribute->name);
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(reader->types, type)) : 0;
    const bool function = bare != 0 && type_kind(reader->types, bare) == BUSTER_TYPE_KIND_FUNCTION;
    const uint32_t tcb = check_input_tcb_kind(reader, attribute);
    bool valid = true;
    if (attribute->count_check != BUSTER_CHECK_COUNT_NONE || attribute->ignored || attribute->subject_ignored)
    {
        // Its count's error comes first (check_input_attribute_subjects), and an ignored one has no handler.
    }
    else if (tcb != 0)
    {
        const String8 own = check_input_tcb_name(reader, attribute);
        const CheckInputAttribute* conflict = NULL;
        for (const CheckInputAttribute* other = attributes->records; conflict == NULL && other < attribute; other += 1)
        {
            const uint32_t kind = check_input_tcb_kind(reader, other);
            conflict = kind != 0 && kind != tcb && check_input_attribute_text_equal(own, check_input_tcb_name(reader, other)) ? other : NULL;
        }
        if (conflict != NULL)
        {
            check_input_tcb_conflict(reader, attributes, attribute, conflict, BUSTER_CHECK_NONE);
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "availability") && attribute->availability != NULL)
    {
        valid = check_input_availability_handle(reader, attributes, attribute);
    }
    else if (check_input_attribute_equal(spelled, "objc_ownership") && attribute->count_check == BUSTER_CHECK_COUNT_NONE &&
             (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM | BUSTER_CHECK_SUBJECT_ENUMERATOR |
                             BUSTER_CHECK_SUBJECT_LABEL)) != 0)
    {
        // A declaration of no declarator (handleObjCOwnershipAttr, after the common count check): clang's error at its
        // first token, a tag's keyword, a label's or an enumerator's name.
        const uint32_t first = reader->attribute_declaration != 0 ? reader->attribute_declaration - 1 : begin;
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_OBJC_VARIABLES, 0);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "objc_bridge") || check_input_attribute_equal(spelled, "objc_bridge_mutable") ||
             check_input_attribute_equal(spelled, "objc_bridge_related"))
    {
        valid = check_input_attribute_objc_bridge(reader, attributes, attribute, declaration, type, begin);
    }
    else if (check_input_attribute_equal(spelled, "callback") && attribute->argument_count == 0)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_CALLBACK_NO_CALLEE, 0);
        valid = false;
    }
    else if (check_input_attribute_equal(spelled, "callback") && function)
    {
        valid = check_input_attribute_callback(reader, attributes, attribute, bare);
    }
    else if (check_input_attribute_equal(spelled, "swift_async") && function)
    {
        valid = check_input_attribute_swift_async(reader, attributes, attribute, bare, name);
    }
    else if (check_input_attribute_equal(spelled, "swift_async_error"))
    {
        valid = check_input_attribute_swift_async_error(reader, attributes, attribute);
    }
    else if (check_input_attribute_equal(spelled, "swift_name") || check_input_attribute_equal(spelled, "swift_async_name"))
    {
        check_input_attribute_swift_name(reader, attribute, declaration, type, check_input_attribute_equal(spelled, "swift_async_name"));
    }
    else if (check_input_attribute_equal(spelled, "swift_error"))
    {
        valid = check_input_attribute_swift_error(reader, attributes, attribute, type);
    }
    else if (check_input_attribute_equal(spelled, "diagnose_as_builtin") && function)
    {
        valid = check_input_attribute_diagnose_builtin(reader, attributes, attribute, bare, name);
    }
    else if (check_input_attribute_equal(spelled, "ns_error_domain") && attribute->argument_count == 1)
    {
        const uint32_t argument = attribute->argument_first;
        const bool lone = argument + 1 == attribute->argument_end && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
        const uint32_t handle = lone ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[argument]) : 0;
        if (handle == 0 || reader->bindings.kind[handle - 1] != C_BINDING_OBJECT)
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, argument, CHECK_ARGUMENT_ERROR_DOMAIN, lone ? 1u : 0u);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].value = argument;
            }
            valid = false;
        }
    }
    else if (check_input_attribute_equal(spelled, "modular_format") && attribute->argument_count >= 2)
    {
        const uint32_t first_end = check_input_checks_argument_end(parser, attribute->argument_first, attribute->argument_end);
        const bool lone = first_end == attribute->argument_first + 1 && parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_IDENTIFIER;
        uint32_t duplicate = BUSTER_CHECK_NONE;
        uint32_t aspect = check_input_checks_argument_end(parser, first_end + 1, attribute->argument_end) + 1;
        const uint32_t aspects = aspect;
        while (lone && duplicate == BUSTER_CHECK_NONE && aspect < attribute->argument_end)
        {
            const uint32_t stop = check_input_checks_argument_end(parser, aspect, attribute->argument_end);
            const String8 text = check_input_checks_string(reader, aspect, stop);
            for (uint32_t earlier = aspects; duplicate == BUSTER_CHECK_NONE && earlier < aspect;)
            {
                const uint32_t earlier_stop = check_input_checks_argument_end(parser, earlier, attribute->argument_end);
                duplicate = check_input_attribute_text_equal(text, check_input_checks_string(reader, earlier, earlier_stop)) ? aspect : BUSTER_CHECK_NONE;
                earlier = earlier_stop + 1;
            }
            aspect = stop + 1;
        }
        const CheckInputAttribute* applied = NULL;
        for (const CheckInputAttribute* other = attributes->records; applied == NULL && other < attribute; other += 1)
        {
            applied = check_input_checks_named(reader, other, "modular_format") ? other : NULL;
        }
        if (!lone)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, 1);
            valid = false;
        }
        else if (duplicate != BUSTER_CHECK_NONE)
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, duplicate, CHECK_ARGUMENT_DUPLICATE_ASPECT, 0);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].value = duplicate;
            }
            valid = false;
        }
        else if (applied != NULL && !check_input_modular_same(reader, applied, attribute))
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_DIFFERENT_ARGUMENTS, 0);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].note_token = applied->first;
                reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
            }
            valid = false;
        }
    }
    return valid;
}

// A function's GPU kernel attributes where it has no kernel's convention (amdgpu_kernel, device_kernel, nvptx_kernel), as
// clang's ProcessDeclAttributeList ends (#147): the first of reqd_work_group_size, work_group_size_hint, vec_type_hint and
// intel_reqd_sub_group_size, "attribute 'X' can only be applied to an OpenCL kernel function"; then the first of AMDGPU's
// flat_work_group_size, waves_per_eu, num_sgpr and num_vgpr, "'X' attribute only applies to kernel functions"; each at the
// function's name (`name`), in that order of their names, not of the attributes. A nonempty function-type chunk is
// processed after the specifiers too: prefix attributes are checked before it, then checked again with the later list.
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
    for (uint32_t pass = 0; !kernel && attributes != NULL && pass < (attributes->function_chunk ? 2u : 1u); pass += 1)
    {
        const bool prefix = attributes->function_chunk && pass == 0;
        for (uint32_t group = 0; group < 2; group += 1)
        {
            const CheckInputAttribute* found = NULL;
            for (uint32_t n = 0; found == NULL && n < 4; n += 1)
            {
                for (uint32_t i = 0; found == NULL && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    const bool generic = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && !attribute->ignored &&
                                         (!prefix || attribute->declaration_prefix);
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

// Whether `type` is the second parameter's that x86's interrupt takes on x86-64 (#147, handleAnyX86InterruptAttr): an
// unsigned integer type of 64 bits (clang's isUnsignedIntegerType: an unsigned builtin integer, an unsigned _BitInt, or a
// complete enumeration of an unsigned underlying type), qualified or not, but not an atomic one.
static bool check_input_interrupt_code(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    const bool enumeration = type_kind(types, bare) == BUSTER_TYPE_KIND_ENUM && type_is_complete(types, bare);
    const TypeId integer = enumeration ? type_unqualified(type_strip_aligned(types, types->types.base[type_index(bare)])) : bare;
    const uint8_t kind = type_kind(types, integer);
    const bool bit_int = kind == BUSTER_TYPE_KIND_BIT_INT && (types->types.flags[type_index(integer)] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 &&
                         types->types.extent[type_index(integer)] == 64;
    return (kind == BUSTER_TYPE_KIND_INTEGER && (integer == BUSTER_TYPE_ULONG || integer == BUSTER_TYPE_ULLONG)) || bit_int;
}

// The first token of parameter `index` of the parameter list opening at `open`, as clang's ParmVarDecl's range begins: its
// declaration's first token past leading C23 attribute specifiers and __extension__ (check_input_attribute_begin); or
// BUSTER_CHECK_NONE past the list's parameters. One walk over the list's tokens, its own commas counted at its depth.
static uint32_t check_input_interrupt_parameter(const Parser* parser, uint32_t open, uint32_t index)
{
    const uint32_t close = parser->tree.parents[open];
    uint32_t start = open + 1;
    uint32_t seen = 0;
    uint32_t depth = 0;
    for (uint32_t t = open + 1; seen < index && t < close; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1u : 0u;
        depth -= (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) && depth != 0 ? 1u : 0u;
        if (depth == 0 && kind == PARSE_KIND_COMMA)
        {
            seen += 1;
            start = t + 1;
        }
    }
    return seen == index && start < close ? check_input_attribute_begin(parser, start, close) : BUSTER_CHECK_NONE;
}

// The first token of a K&R definition's declaration of its identifier list's name `listed`, among its declarations after
// the list's `)` up to its body's `{`, as clang's ParmVarDecl's range begins (that declaration's first token past leading
// C23 attribute specifiers and __extension__); BUSTER_CHECK_NONE where none declares it (an implicit int's).
static uint32_t check_input_interrupt_kr_parameter(const Parser* parser, uint32_t open, uint32_t listed)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t first = parser->tree.parents[open] + 1;
    uint32_t start = first;
    uint32_t depth = 0;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = first; result == BUSTER_CHECK_NONE && t < count && parser->tree.roles[t] != PARSE_ROLE_OPEN_BODY; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        depth += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1u : 0u;
        depth -= (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) && depth != 0 ? 1u : 0u;
        start = depth == 0 && kind == PARSE_KIND_SEMI ? t + 1 : start;
        const bool named = kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == parser->tokens.names[listed] &&
                           parser->tree.roles[t] == PARSE_ROLE_OPERAND && parser->tokens.kinds[t - 1] != PARSE_KIND_PERIOD &&
                           parser->tokens.kinds[t - 1] != PARSE_KIND_ARROW;
        result = named ? check_input_attribute_begin(parser, start, t + 1) : BUSTER_CHECK_NONE;
    }
    return result;
}

// Where a function declaration's result type begins, as clang's FunctionDecl::getReturnTypeSourceRange has it for
// interrupt's first error: the first type specifier among its specifiers from `first` (past storage classes, function
// specifiers, qualifiers, calling conventions and attributes; `_Complex` isn't its type's start, and `_Atomic(T)`'s is T's)
// when its own parameter list `list` follows its name `name` (past grouping parentheses) with no array or function
// declarator after it. BUSTER_CHECK_NONE otherwise, where clang's range is empty: a function declared by a typedef's name,
// or one that returns a pointer to an array or a function, whose result's type is written around the name.
static uint32_t check_input_interrupt_result(const Parser* parser, uint32_t first, uint32_t name, uint32_t list)
{
    uint32_t after_name = name + 1;
    while (after_name < list && parser->tokens.kinds[after_name] == PARSE_KIND_R_PAREN)
    {
        after_name += 1;
    }
    uint32_t after_list = list < parser->tokens.count ? parser->tree.parents[list] + 1 : parser->tokens.count;
    while (after_list < parser->tokens.count && parser->tokens.kinds[after_list] == PARSE_KIND_R_PAREN)
    {
        after_list += 1;
    }
    const uint8_t next = after_list < parser->tokens.count ? parser->tokens.kinds[after_list] : PARSE_KIND_END;
    const bool placed = list < parser->tokens.count && after_name == list && next != PARSE_KIND_L_SQUARE && next != PARSE_KIND_L_PAREN;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = first; placed && result == BUSTER_CHECK_NONE && t < name;)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint32_t word = parser->tokens.names[t];
        const bool complex = word == C_NAME_COMPLEX_C99 || word == C_NAME_GNU_COMPLEX || word == C_NAME_GNU_COMPLEX2;
        const bool opened = t + 1 < name && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN;
        if ((kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS) && opened)
        {
            t = parser->tree.parents[t + 1] + 1;
        }
        else if (kind == PARSE_KIND_L_SQUARE && t + 1 < name && parser->tokens.kinds[t + 1] == PARSE_KIND_L_SQUARE)
        {
            t = parser->tree.parents[t] + 1;
        }
        else if (kind == PARSE_KIND_ATOMIC && opened)
        {
            t += 2;
        }
        else if ((kind == PARSE_KIND_TYPE && !complex) || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                 kind == PARSE_KIND_IDENTIFIER)
        {
            result = t;
        }
        else
        {
            t += 1;
        }
    }
    return result;
}

// x86's interrupt on a declaration its subjects take (#147, clang's handleAnyX86InterruptAttr on x86-64): a function's, or a
// variable's, a member's, a parameter's or a typedef's of a pointer to a function, with a prototype. Its function type must
// return void, at its result type's start (check_input_interrupt_result); have one or two parameters (a variadic one's
// named ones), at the declaration's start (`begin`); a pointer first, at that parameter's start; and an unsigned 64-bit
// integer second (check_input_interrupt_code), at its start. A function declared by a typedef's name has its parameters at
// its name; a declaration of no function (a pointer's) has neither those places nor its result's, where clang's errors
// have none (BUSTER_CHECK_INTERRUPT_UNLOCATED). The first rule broken is the declaration's error. Whether none is.
static bool check_input_interrupt_handler(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t declaration,
                                          TypeId type, uint32_t begin, uint32_t name)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    const uint32_t count = parser->tokens.count;
    const TypeId bare = type_strip_aligned(types, type);
    const TypeId function = type_kind(types, bare) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, bare)) : bare;
    // A function's declaration: its own parameter list (an identifier list's, of a K&R definition), where it has one.
    const bool declared = (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0;
    const uint32_t list = declared && reader->subject_function_list < count ? reader->subject_function_list : BUSTER_CHECK_NONE;
    const bool typed = type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t parameter_count = 0;
    const TypeId* parameters = typed ? type_parameters(types, function, &parameter_count) : NULL;
    const TypeId result = typed ? type_return(types, function) : BUSTER_TYPE_VOID;
    uint32_t rule = UINT32_MAX;
    uint32_t token = BUSTER_CHECK_NONE;
    if (type_kind(types, type_strip_aligned(types, result)) != BUSTER_TYPE_KIND_VOID)
    {
        rule = BUSTER_CHECK_INTERRUPT_RESULT;
        token = list != BUSTER_CHECK_NONE && name < count ? check_input_interrupt_result(parser, begin, name, list) : BUSTER_CHECK_NONE;
    }
    else if (parameter_count < 1 || parameter_count > 2)
    {
        rule = BUSTER_CHECK_INTERRUPT_COUNT;
        token = begin;
    }
    else
    {
        const bool pointer = type_kind(types, type_strip_aligned(types, parameters[0])) == BUSTER_TYPE_KIND_POINTER;
        const uint32_t index = !pointer ? 0 : parameter_count == 2 && !check_input_interrupt_code(types, parameters[1]) ? 1 : 2;
        rule = index == 0 ? BUSTER_CHECK_INTERRUPT_POINTER : index == 1 ? BUSTER_CHECK_INTERRUPT_CODE : UINT32_MAX;
        // A K&R definition's parameters' types are its declarations', whose places are theirs (its identifier list's names,
        // a comma between each two); another prototype's are its list's, and a typedef's function's its name's.
        const uint32_t named = list + 1 + 2 * index;
        if (index < 2 && list != BUSTER_CHECK_NONE && reader->subject_identifier_list)
        {
            token = named < count && parser->tokens.kinds[named] == PARSE_KIND_IDENTIFIER ? check_input_interrupt_kr_parameter(parser, list, named) : BUSTER_CHECK_NONE;
        }
        else if (index < 2 && list != BUSTER_CHECK_NONE)
        {
            token = check_input_interrupt_parameter(parser, list, index);
        }
        else if (index < 2 && declared)
        {
            token = name;
        }
    }
    if (rule != UINT32_MAX)
    {
        const bool located = token < count;
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, located ? token : attribute->name_token, CHECK_INPUT_INTERRUPT_HANDLER);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].detail = rule | (located ? 0u : BUSTER_CHECK_INTERRUPT_UNLOCATED);
            reader->output->reports[before].attribute = attribute->first;
            // In the order of the declaration's attributes, as clang's handlers run, wherever its place (#1447).
            reader->output->reports[before].sort = attribute->first;
        }
        if (attributes->error == CHECK_ATTRIBUTE_ERROR_NONE)
        {
            attributes->error = CHECK_ATTRIBUTE_ERROR_SUBJECT;
            attributes->error_first = attribute->first;
            attributes->error_end = attribute->end;
        }
    }
    return rule == UINT32_MAX;
}

// A declarator's run of its list's repeats (#1447, CheckInputAttributeRepeat), `declarator` its key: the first declarator
// that takes them has their reports already; each other one gives them again, as clang's handler does for each
// declarator, completing where the reader's waiting errors of its own list do (before `completion`, the subject checks').
static void check_input_attribute_repeats(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t declarator, uint32_t completion)
{
    CheckInput* output = reader->output;
    for (CheckInputAttributeRepeat* repeat = attributes != NULL && declarator != 0 ? attributes->repeats : NULL; repeat != NULL; repeat = repeat->next)
    {
        const bool again = repeat->declarator != 0 && repeat->declarator != declarator;
        repeat->declarator = repeat->declarator != 0 ? repeat->declarator : declarator;
        if (again && output->report_count < output->report_capacity && !output->resource)
        {
            const CheckInputAttributeRecovery recovery = check_input_attribute_recovery(reader, attributes, NULL);
            output->reports[output->report_count++] = *repeat->report;
            check_input_attribute_errors_recover(reader, attributes, &recovery, completion > 1 ? completion - 1 : completion);
        }
        output->resource |= again && output->report_count >= output->report_capacity;
    }
}

static bool check_input_attribute_subjects(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t declaration, TypeId type, uint32_t begin,
                                           uint32_t name)
{
    const Parser* parser = reader->parser;
    const uint32_t warning_begin = reader->warning_events.count;
    const uint32_t report_begin = reader->output->report_count;
    const uint32_t completion = reader->attribute_completion;
    const uint32_t pointer_variable = BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_FUNCTION_POINTER;
    const uint32_t subjects = declaration | ((declaration & pointer_variable) == pointer_variable ? BUSTER_CHECK_SUBJECT_FUNCTION_POINTER_VARIABLE : 0);
    uint32_t microsoft_layout = UINT32_MAX;
    uint32_t ordinary_layout = UINT32_MAX;
    bool valid = true;
    const CheckInputUnknownAttribute* unknown = attributes != NULL ? attributes->unknown_begin : NULL;
    // The waiting recovered errors of this declarator's attributes, which Sema gives where it processes them (#1447,
    // CheckInputReport.pending), complete at its last token: after the parser's errors in it, before the next token's (a
    // missing `;`'s, check_prefix_resume). Only those within the declaration's tokens: a parameter's checks, which come
    // first, leave its function's to the function's. Those of a declaration with no subject check keep waiting, and so
    // their places; the cursor passes the ones no longer waiting.
    CheckInput* output = reader->output;
    const uint32_t last = completion != 0 ? completion - 1 : 0;
    for (uint32_t r = reader->recovered_pending; r < output->recovered_count; r += 1)
    {
        CheckInputReport* report = output->recovered_reports + r;
        if (report->pending && report->token >= begin && report->token < last)
        {
            report->suborder = (uint64_t)(report->sort != 0 ? report->sort : report->completion != 0 ? report->completion : report->token) + 1;
            report->completion = report->completion < last ? last : report->completion;
            report->pending = false;
        }
    }
    while (reader->recovered_pending < output->recovered_count && !output->recovered_reports[reader->recovered_pending].pending)
    {
        reader->recovered_pending += 1;
    }
    check_input_attribute_repeats(reader, attributes, name != BUSTER_CHECK_NONE ? name : completion, completion);
    CheckInputAttributeRecovery recovery = { 0 };
    // The condition first takes the last attribute's errors past, where it can (#1447): the body leaves an attribute at
    // several `continue`s.
    for (uint32_t i = 0; (valid = check_input_attribute_recover(reader, attributes, &recovery, completion, true, valid)) && attributes != NULL &&
                         (i < attributes->count || unknown != NULL);)
    {
        if (unknown != NULL && (i == attributes->count || unknown->first < attributes->records[i].first))
        {
            const CheckInputAttribute attribute = {
                .first = unknown->first, .name_token = unknown->name_token, .name = unknown->name, .end = unknown->end, .standard = unknown->standard,
            };
            if (!unknown->single_warning || !*unknown->warned_shared)
            {
                check_input_attribute_named_event(reader, &attribute, CHECK_WARNING_ID_UNKNOWN_ATTRIBUTE, CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE);
                *unknown->warned_shared = true;
            }
            unknown = unknown->next;
            continue;
        }
        CheckInputAttribute* attribute = attributes->records + i++;
        recovery = check_input_attribute_recovery(reader, attributes, attribute);
        if (attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && attribute->convention_count != BUSTER_CHECK_COUNT_NONE && !attribute->subject_ignored &&
            (check_input_attribute_function_wrapped(reader->types, type) ||
             (declaration & (BUSTER_CHECK_SUBJECT_STRUCT | BUSTER_CHECK_SUBJECT_UNION | BUSTER_CHECK_SUBJECT_ENUM)) != 0))
        {
            // A calling convention's wrong count on a function, or a pointer or an array of one, is clang's error alone,
            // after its parser's warnings (a definition's -Wgcc-compat); on a tag too, whose declaration clang's
            // handleCallConvAttr checks it on (#1447); on another subject its warning (#147,
            // CheckInputAttribute.convention_count).
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT,
                                        attribute->convention_count | (uint32_t)attribute->convention_bound << 8);
            attributes->error = CHECK_ATTRIBUTE_ERROR_ARGUMENTS;
            attributes->error_first = attribute->first;
            attributes->error_end = attribute->end;
            valid = false;
            continue;
        }
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
                                                     check_input_attribute_equal(generic_spelled, "assume") ||
                                                     check_input_attribute_equal(generic_spelled, "likely") ||
                                                     check_input_attribute_equal(generic_spelled, "unlikely"));
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
        // cfi_salt after a declarator's name, where no function of its type takes it (#1447): clang distributes it from the
        // declarator and drops it with its warning, so its subjects aren't checked.
        const bool salt_trailing = reader->salt_attributes && !attribute->keyword && attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && name < parser->tokens.count &&
                                   attribute->first > name && check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "cfi_salt");
        TypeId salted = salt_trailing ? type : 0;
        while (salted != 0 && (type_kind(reader->types, salted) == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(type_kind(reader->types, salted)) ||
                               type_kind(reader->types, salted) == BUSTER_TYPE_KIND_ALIGNED || type_kind(reader->types, salted) == BUSTER_TYPE_KIND_QUALIFIED))
        {
            salted = reader->types->types.base[type_index(salted)];
        }
        // (A C23 one is a type's alone wherever it is, no subjects either, but after the name of a declarator with a
        // function.)
        const bool salt_type = (salt_trailing && (salted == 0 || type_kind(reader->types, salted) != BUSTER_TYPE_KIND_FUNCTION)) ||
                               (reader->salt_attributes && attribute->standard && !attribute->salt_declaration && attribute->kind == CHECK_ATTRIBUTE_UNKNOWN &&
                                check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "cfi_salt"));
        const CheckInputSubjectRow* row = attribute->keyword || overflow || salt_type || (attribute->ignored && !attribute->subject_ignored && !transparent && !kernel) ?
                                          NULL : check_input_attribute_subject_of(reader, scope, attribute->name, scoped, attribute->standard);
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
        // force_align_arg_pointer's handler ignores a function pointer (a variable's, a parameter's or a member's) or a
        // typedef of a function's type or a pointer to one, without a word (#1447).
        const bool quiet = attribute->kind == CHECK_ATTRIBUTE_FORCE_ALIGN_ARG_POINTER && (subjects & BUSTER_CHECK_SUBJECT_FUNCTION) == 0 &&
                           ((subjects & BUSTER_CHECK_SUBJECT_FUNCTION_POINTER) != 0 ||
                            (subjects & (BUSTER_CHECK_SUBJECT_TYPEDEF | BUSTER_CHECK_SUBJECT_FUNCTION_LIKE)) == (BUSTER_CHECK_SUBJECT_TYPEDEF | BUSTER_CHECK_SUBJECT_FUNCTION_LIKE));
        if (quiet)
        {
            attribute->ignored = true;
            attribute->subject_ignored = true;
        }
        else if (row != NULL && (row->subjects & subjects) == 0 && row->error && undeclared)
        {
            check_input_report(reader, counted, CHECK_INPUT_UNDECLARED);
            valid = false;
        }
        else if (row != NULL && (row->subjects & subjects) == 0 && row->error)
        {
            check_input_report(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_SUBJECT);
            valid = false;
        }
        else if (attribute->salt_declaration)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_INVALID_ON_DECLARATION, 0);
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
        if (valid && generic && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_attribute_checks(reader, attributes, attribute, declaration, type, name, begin);
        }
        if (valid && generic && !attribute->ignored && !attribute->subject_ignored && reader->type_tag_attributes)
        {
            valid = check_input_type_tag_handled(reader, attributes, attribute, declaration, type);
        }
        // x86's interrupt where its subjects take it (#147): its handler's checks of the function's type. One that fails
        // them is no handler, as clang's handleAnyX86InterruptAttr returns before adding the attribute: no direct call's
        // error, no implicit used, no -Wexcessive-regsave of its body's calls.
        if (valid && attribute->kind == CHECK_ATTRIBUTE_INTERRUPT && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_interrupt_handler(reader, attributes, attribute, declaration, type, begin, name);
            attribute->ignored = !valid;
            attribute->subject_ignored = !valid;
            // Its error leaves the declaration valid, as clang's handler returns (#1447): the declaration's later
            // attributes' errors follow it, published with the declarator's other waiting errors, in their order.
            valid = valid || check_input_attribute_errors_recover(reader, attributes, &recovery, completion > 1 ? completion - 1 : completion);
        }
        // patchable_function_entry where its subjects take it (#1447): its handler's checks of its arguments.
        if (valid && attribute->kind == CHECK_ATTRIBUTE_PATCHABLE_FUNCTION_ENTRY && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_patchable_entry(reader, attributes, attribute);
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
        // (no_caller_saved_registers on a tag is nothing to clang: its type processing never meets it.)
        const bool convention = attribute->kind == CHECK_ATTRIBUTE_CALLING_CONVENTION && !check_input_attribute_kernel(reader, attribute) &&
                                attribute->value != CHECK_CALL_NO_CALLER_SAVED;
        const bool function_type = convention || (attribute->kind == CHECK_ATTRIBUTE_NORETURN && !attribute->standard && !attribute->keyword) ||
                                   (attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword &&
                                    check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "cfi_unchecked_callee"));
        if (valid && tag && function_type && !attribute->ignored && !attribute->subject_ignored)
        {
            valid = check_input_attribute_pcs(reader, attributes, attribute, true);
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
            const bool string = check_input_attribute_argument(attribute, argument, &first, &end) && check_input_attribute_string(reader, first, end, false, &sanitizer);
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
        // Layout handlers reject the opposite kind on this declaration, but permit both across redeclarations.
        // Remember each first local attribute once: repeated identical attributes must not rescan the whole prefix.
        const bool microsoft = kept && attribute->kind == CHECK_ATTRIBUTE_MS_STRUCT && attribute->first >= begin;
        const bool ordinary = kept && attribute->kind == CHECK_ATTRIBUTE_GCC_STRUCT && attribute->first >= begin;
        const uint32_t excluding = microsoft ? ordinary_layout : ordinary ? microsoft_layout :
                                   kept ? check_input_attribute_excluding(reader, attributes, i - 1) : UINT32_MAX;
        microsoft_layout = microsoft && microsoft_layout == UINT32_MAX ? i - 1 : microsoft_layout;
        ordinary_layout = ordinary && ordinary_layout == UINT32_MAX ? i - 1 : ordinary_layout;
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
            const uint32_t before = reader->output->report_count;
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT_COUNT, attribute->count_check | attribute->count_bound << 8);
            if (reader->output->report_count > before)
            {
                // All original argument syntax precedes the handler, including a type-name error retained by the
                // diagnostic-only parser view. Keep the attribute's name as its location, and its end as publication.
                reader->output->reports[before].completion = attribute->end;
            }
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
        else if (kept && attribute->kind == CHECK_ATTRIBUTE_TARGET_CLONES)
        {
            // Its strings' versions (#1838, c_check_target_versions.c).
            valid = check_input_target_clones(reader, attributes, attribute);
        }
        else if (kept && attribute->kind == CHECK_ATTRIBUTE_SELECTANY && (declaration & (BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_FUNCTION)) != 0 &&
                 (declaration & BUSTER_CHECK_SUBJECT_EXTERNAL_OBJECT) == 0)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_SELECTANY_LINKAGE, 0);
            valid = false;
        }
        else if (kept && attribute->deferred != NULL && attribute->kind != CHECK_ATTRIBUTE_CALLING_CONVENTION)
        {
            // Its argument's error, its handler's once its subject takes it (check_input_attribute_defer); a convention's
            // where it meets a function or a tag (check_input_attribute_pcs).
            CheckInput* output = reader->output;
            if (output->report_count < output->report_capacity)
            {
                output->reports[output->report_count++] = *attribute->deferred;
            }
            output->resource |= output->report_count >= output->report_capacity && output->reports[output->report_count - 1].token != attribute->deferred->token;
            attributes->error = attributes->error == CHECK_ATTRIBUTE_ERROR_NONE ? CHECK_ATTRIBUTE_ERROR_ARGUMENTS : attributes->error;
            valid = false;
        }
        const bool required_typedef = valid && kept && attribute->kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT &&
                                      (declaration & BUSTER_CHECK_SUBJECT_TYPEDEF) != 0;
        reader->output->typedef_result_contracts |= required_typedef;
        // A lane's input is private; source-order replay publishes a body's unit-wide result-contract guard.
        check_input_lane_refuse(reader, reader->lane && required_typedef, "a body declares a result-contract typedef");
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
    // Subject checks run after the declarator's type is complete. Give their warnings and errors that same boundary,
    // retaining each handler's earlier ordering key separately, so its introductory warning still precedes its error
    // and the final kernel checks follow both. Keep parameters before the containing declarator's checks. Parser macro
    // warnings retain their own lexical boundaries, including any read by an attribute's nested type expression.
    for (uint32_t w = warning_begin; w < reader->warning_events.count; w += 1)
    {
        CheckWarningEvent* event = reader->warning_events.events + w;
        if (event->kind != CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE && event->completion_kind == CHECK_WARNING_COMPLETE_AT_TOKEN &&
            event->completion < completion)
        {
            event->suborder = (uint64_t)event->completion + 1;
            event->completion = completion;
        }
    }
    for (uint32_t r = report_begin; r < reader->output->report_count; r += 1)
    {
        CheckInputReport* report = reader->output->reports + r;
        report->suborder = (uint64_t)(report->sort != 0 ? report->sort : report->completion != 0 ? report->completion : report->token) + 1;
        report->completion = report->completion < completion ? completion : report->completion;
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
    CheckInputAttributeRecovery recovery = { 0 };
    // The condition first takes the last attribute's errors past, where it can (#1447).
    for (uint32_t i = 0; (valid = check_input_attribute_recover(reader, attributes, &recovery, 0, false, valid)) && attributes != 0 && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        recovery = check_input_attribute_recovery(reader, attributes, attribute);
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
        // A function's target or target_clones attribute asks its rules and binding about multiversioning (#1838).
        reader->target_versions |= (kind == CHECK_ATTRIBUTE_TARGET || kind == CHECK_ATTRIBUTE_TARGET_CLONES) && subject.function;
        if ((kind == CHECK_ATTRIBUTE_CPU_SPECIFIC || kind == CHECK_ATTRIBUTE_CPU_DISPATCH) && subject.function)
        {
            // clang's handler checks each CPU's name (#1447, c_check_cpu_dispatch.c); an error drops the attribute, and
            // clang goes on with the declaration.
            reader->cpu_dispatch = true;
            reader->output->cpu_dispatch = true;
            check_input_cpu_dispatch_options(reader, attribute);
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_NO_BUILTIN)
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
                          kind == CHECK_ATTRIBUTE_MS_STRUCT || kind == CHECK_ATTRIBUTE_GCC_STRUCT ||
                          kind == CHECK_ATTRIBUTE_ALIGNAS || kind == CHECK_ATTRIBUTE_MODE || kind == CHECK_ATTRIBUTE_VECTOR_SIZE ||
                          kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE || kind == CHECK_ATTRIBUTE_ADDRESS_SPACE || kind == CHECK_ATTRIBUTE_TRANSPARENT_UNION ||
                          kind == CHECK_ATTRIBUTE_ALIAS ||
                          kind == CHECK_ATTRIBUTE_IFUNC || kind == CHECK_ATTRIBUTE_WEAKREF || kind == CHECK_ATTRIBUTE_ASM_LABEL ||
                          kind == CHECK_ATTRIBUTE_CALLING_CONVENTION || kind == CHECK_ATTRIBUTE_NORETURN || kind == CHECK_ATTRIBUTE_OVERLOADABLE ||
                          kind == CHECK_ATTRIBUTE_CLEANUP || kind == CHECK_ATTRIBUTE_FALLTHROUGH ||
                          kind == CHECK_ATTRIBUTE_UNSEQUENCED || kind == CHECK_ATTRIBUTE_REPRODUCIBLE || kind == CHECK_ATTRIBUTE_NULLABILITY ||
                          kind == CHECK_ATTRIBUTE_REGPARM || kind == CHECK_ATTRIBUTE_MUSTTAIL || kind == CHECK_ATTRIBUTE_NOMERGE ||
                          kind == CHECK_ATTRIBUTE_SUPPRESS ||
                          (attribute->standard && (kind == CHECK_ATTRIBUTE_DEPRECATED || kind == CHECK_ATTRIBUTE_UNUSED || kind == CHECK_ATTRIBUTE_WARN_UNUSED_RESULT));
    const Parser* parser = reader->parser;
    const bool scoped = attribute->standard && attribute->first != attribute->name_token;
    const String8 scope = scoped ? check_input_attribute_name(reader, parser->tokens.names[attribute->first]) : (String8){ "", 0 };
    const CheckInputFactRow* fact = excluded ? NULL : check_input_attribute_fact_of(reader, scope, attribute->name, scoped, attribute->standard);
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
// each declarator's: one declarator's is copied before its records are marked. A `redefinition` (a second definition of
// the entity, of a compatible type) is checked too, before its error (#1920): clang merges its attributes when it
// declares it (mergeDeclAttributes) and reports the redefinition at its body or initializer.
static void check_input_attribute_late(CheckInputReader* reader, uint32_t token, uint32_t previous, uint32_t point, bool visible, bool redefinition)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : 0;
    const bool after = attributes != 0 && reader->definition_origins != NULL && reader->definition_origins[previous] &&
                       (redefinition || !check_input_bit(reader->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED));
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
                    // (A redefinition's comes before its error, at the declaration's name: at the attribute.)
                    .completion = redefinition ? attribute->name_token : point,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                const CheckWarningNote note = {
                    .kind = CHECK_WARNING_NOTE_PREVIOUS_DEFINITION,
                    .token = reader->definition_origins[previous] - 1,
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
                check_input_warning_event(reader, &event, missing_inline ? NULL : &note, missing_inline ? 0 : 1);
                // x86's interrupt's implicit used (#147), which clang drops after it with the same warning at no location,
                // unless the definition has a used already.
                const bool implicit_used = attribute->kind == CHECK_ATTRIBUTE_INTERRUPT && !attribute->subject_ignored &&
                                           (reader->late_attribute_masks[reader->objects[previous]] & BUSTER_CHECK_LATE_USED) == 0;
                if (implicit_used)
                {
                    CheckWarningEvent unlocated = event;
                    unlocated.flags = BUSTER_CHECK_WARNING_UNLOCATED;
                    check_input_warning_event(reader, &unlocated, &note, 1);
                }
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
// Always inlined, as it was before #1447 grew the type readers it inlines past clang's threshold.
static inline __attribute__((always_inline)) bool check_input_nonnull_pointer(const TypeTable* types, TypeId type)
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
    CheckInputAttributeRecovery recovery = { 0 };
    // The condition first takes the last attribute's errors past, where it can (#1447).
    for (uint32_t i = 0; (valid = check_input_attribute_recover(reader, attributes, &recovery, 0, false, valid)) && is_function && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = &attributes->records[i];
        recovery = check_input_attribute_recovery(reader, attributes, attribute);
        // One whose subjects are prototyped functions alone (format and the like) on a K&R function's type, which clang
        // ignores with its subject's warning before reading its arguments (check_input_attribute_subjects).
        const bool scoped = attribute->standard && attribute->first != attribute->name_token;
        const CheckInputSubjectRow* row = attribute->keyword || prototyped ? NULL :
            check_input_attribute_subject_of(reader, scoped ? check_input_attribute_name(reader, reader->parser->tokens.names[attribute->first]) : (String8){ "", 0 },
                                             attribute->name, scoped, attribute->standard);
        if (row != NULL && (row->subjects & (BUSTER_CHECK_SUBJECT_PROTOTYPED | BUSTER_CHECK_SUBJECT_FUNCTION | BUSTER_CHECK_SUBJECT_FUNCTION_LIKE)) == BUSTER_CHECK_SUBJECT_PROTOTYPED)
        {
            continue;
        }
        if (attribute->kind == CHECK_ATTRIBUTE_ALIGNAS)
        {
            // Each _Alignas's, at it (#1447).
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_ALIGNAS_SUBJECT, 0);
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
        // A malformed format kind is its handler's deferred error, once its subject is known. It must neither become
        // an unknown format warning here nor make the parameter-index reader report a replacement error.
        const bool identifier_error = attribute->kind == CHECK_ATTRIBUTE_FORMAT && attribute->deferred != NULL &&
                                      reader->parser->tokens.kinds[attribute->argument_first] != PARSE_KIND_IDENTIFIER;
        bool recognized_format = attribute->kind != CHECK_ATTRIBUTE_FORMAT || strftime || identifier_error;
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
        else if ((allocation || nonnull || format) && attribute->deferred == NULL)
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

// The bracket depth a backward walk has after stepping over a token of `kind` at `depth`.
static uint32_t check_input_deprecated_enum_depth(uint8_t kind, uint32_t depth)
{
    return kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE ? depth + 1 :
           (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE) && depth != 0 ? depth - 1 : depth;
}

// The tag key (check_input_deprecated_key) of the enum whose body declares the enumerator at `enumerator`: its `{`'s, back
// past the earlier enumerators, and its keyword's before that; `count` for none. `enums` keeps each answer plus one by
// enumerator token (#1354): a walk stops at the nearest earlier enumerator of its body already answered and answers
// every one it passed. Repeated uses of a flat enum share that walk; nested enum bodies can be traversed again by an
// enclosing enum's walk.
static uint32_t check_input_deprecated_enum(const CheckInputReader* reader, uint32_t enumerator, uint32_t* enums)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t known = enums[enumerator];
    uint32_t i = enumerator;
    uint32_t depth = 0;
    bool scanning = known == 0;
    while (scanning && i > 0)
    {
        i -= 1;
        const uint8_t kind = parser->tokens.kinds[i];
        known = depth == 0 && reader->declaration_kinds[i] == C_BINDING_ENUMERATOR ? enums[i] : 0;
        scanning = known == 0 && !(kind == PARSE_KIND_L_BRACE && depth == 0);
        depth = check_input_deprecated_enum_depth(kind, depth);
    }
    uint32_t keyword = i;
    while (!scanning && known == 0 && keyword > 0 && parser->tokens.kinds[keyword] != PARSE_KIND_ENUM)
    {
        keyword -= 1;
    }
    const uint32_t result = known != 0                                                   ? known - 1 :
                            !scanning && parser->tokens.kinds[keyword] == PARSE_KIND_ENUM ? check_input_deprecated_key(reader, keyword) :
                                                                                            count;
    // The enumerators the walk passed, at its own depth, are the same body's.
    if (enums[enumerator] == 0)
    {
        enums[enumerator] = result + 1;
        depth = 0;
        for (uint32_t j = enumerator; j > i + 1;)
        {
            j -= 1;
            const uint8_t kind = parser->tokens.kinds[j];
            if (depth == 0 && reader->declaration_kinds[j] == C_BINDING_ENUMERATOR)
            {
                enums[j] = result + 1;
            }
            depth = check_input_deprecated_enum_depth(kind, depth);
        }
    }
    return result;
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
// One token's part of check_input_availability_contexts, which `*function` (the last function's name outside a body) and
// `*body_end` (where the body it is in ends) carry from the tokens before it.
static void check_input_availability_token(CheckInputReader* reader, uint8_t* contexts, const uint32_t* marks, const uint32_t* chain, uint32_t t, uint32_t* function,
                                           uint32_t* body_end)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t kind = parser->tokens.kinds[t];
    const bool inside = t < *body_end;
    *function = !inside && kind == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[t] == C_BINDING_FUNCTION ? t : *function;
    if (!inside && *function != BUSTER_CHECK_NONE && parser->tree.roles[t] == PARSE_ROLE_OPEN_BODY && parser->tree.parents[t] < count)
    {
        *body_end = parser->tree.parents[t];
        // Its declarator's tokens before the body, a K&R definition's parameter declarations among them, as a
        // declarator's; then the body, whose tags have the function for their parent.
        const uint32_t key = check_input_deprecated_key(reader, *function);
        check_input_availability_fill(reader, contexts, marks, chain, key, *function, *function + 1, t, false);
        check_input_availability_fill(reader, contexts, marks, chain, key, *function, t, *body_end + 1, true);
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

// The 64 tokens from `first` (`length` of them) whose part of check_input_availability_contexts can do anything: a body's
// `{`, a function's name, which a body after it reads, a struct's, a union's or an enum's keyword, a marked member's name,
// and an identifier whose key is marked (`marked`, check_input_deprecated_marked), which every fill reads.
static uint64_t check_input_availability_events(const CheckInputReader* reader, uint32_t first, uint32_t length, uint64_t marked)
{
    const Parser* parser = reader->parser;
    const uint64_t valid = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
    const __m512i kinds = _mm512_maskz_loadu_epi8(valid, parser->tokens.kinds + first);
    const __m512i roles = _mm512_maskz_loadu_epi8(valid, parser->tree.roles + first);
    const __m512i declared = _mm512_maskz_loadu_epi8(valid, reader->declaration_kinds + first);
    const __m512i members = reader->member_marks != NULL ? _mm512_maskz_loadu_epi8(valid, reader->member_marks + first) : _mm512_setzero_si512();
    const uint64_t functions = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_IDENTIFIER)) &
                               _mm512_cmpeq_epi8_mask(declared, _mm512_set1_epi8(C_BINDING_FUNCTION));
    const uint64_t events = _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_STRUCT)) |
                            _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_ENUM)) | functions | _mm512_test_epi8_mask(members, members) | marked;
    return events & valid;
}

static void check_input_availability_contexts(CheckInputReader* reader, const uint32_t* marks, const uint32_t* chain, const uint64_t* marked)
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
        if (marked != NULL)
        {
            // The vector path visits only the tokens whose part can do anything (check_input_availability_events): the
            // others' set no context and change neither the function nor the body.
            uint32_t first = 0;
            while (first < count)
            {
                const uint32_t length = count - first < 64 ? count - first : 64;
                for (uint64_t events = check_input_availability_events(reader, first, length, marked[first / 64]); events != 0; events &= events - 1)
                {
                    check_input_availability_token(reader, contexts, marks, chain, first + (uint32_t)__builtin_ctzll(events), &function, &body_end);
                }
                // length <= count - first: first reaches count exactly, even at the u32 limit.
                first += length;
            }
        }
        else
        {
            for (uint32_t t = 0; t < count; t += 1)
            {
                check_input_availability_token(reader, contexts, marks, chain, t, &function, &body_end);
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

// The next declarator separator after `first`, with complete groups skipped. Delayed availability diagnostics for
// file-scope type specifiers complete here, after type construction and declaration attributes. The caller keeps
// this cursor across uses in the same declaration, so many marked parameter types do not rescan the declaration.
static uint32_t check_input_availability_declarator_end(const Parser* parser, uint32_t first)
{
    uint32_t token = first;
    while (token < parser->tokens.count)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint32_t close = parser->tree.parents[token];
        if ((kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && close > token && close < parser->tokens.count)
        {
            token = close + 1;
        }
        else if (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_EQUAL || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE)
        {
            break;
        }
        else
        {
            token += 1;
        }
    }
    return token;
}

// The tables check_input_deprecated_uses' uses read (check_input_deprecated_use): the marks, each tag key's latest
// declaration so far (written as the walk passes it), the marked members' names, the scopes open at a use, each tag's
// first declaration, the uses' contexts and the members only the evaluator reads.
typedef struct CheckInputDeprecatedPass CheckInputDeprecatedPass;
struct CheckInputDeprecatedPass
{
    const uint32_t* marks;
    const uint32_t* chain;
    uint32_t* latest;
    const uint8_t* member_marks;
    const int32_t* scope_levels;
    const int32_t* scope_lists;
    const bool* scope_records;
    const uint32_t* tag_keys;
    uint32_t* enums;
    uint32_t tag_count;
    // Cursor for the declaration whose availability diagnostics are delayed (#1455).
    uint32_t delayed_first;
    uint32_t delayed_end;
    const uint8_t* contexts;
    const uint32_t* evaluated;
    // Whether an unavailable use's error is given: a complete unit's alone. A rejected unit's view (#2202) gives its
    // deprecated uses' warnings, which publish as its other warnings do, but not errors the view's reports don't select.
    bool errors;
};

// The use at `t`, if it is one, of what a deprecated or unavailable attribute marks (check_input_deprecated_uses): its
// diagnostic, and a tag's declaration's place for the notes of the uses after it. `level` and `lists` are the brackets
// and parameter lists open before it, `scopes` the scopes (`pass->scope_levels` and the rest), and `inside` whether it is
// in a function's body.
static void check_input_deprecated_use(CheckInputReader* reader, CheckInputDeprecatedPass* pass, uint32_t t, int32_t level, int32_t lists, uint32_t scopes,
                                       bool inside)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t* marks = pass->marks;
    const uint32_t* chain = pass->chain;
    uint32_t* latest = pass->latest;
    const uint8_t* member_marks = pass->member_marks;
    const int32_t* scope_levels = pass->scope_levels;
    const int32_t* scope_lists = pass->scope_lists;
    const bool* scope_records = pass->scope_records;
    const uint32_t* tag_keys = pass->tag_keys;
    const uint32_t tag_count = pass->tag_count;
    const uint8_t* contexts = pass->contexts;
    const uint32_t* evaluated = pass->evaluated;
    const uint8_t kind = parser->tokens.kinds[t];
    const uint8_t role = parser->tree.roles[t];
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
        key = check_input_deprecated_enum(reader, declared, pass->enums);
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
    uint32_t completion = t + 1;
    // File-scope typedef uses in a declaration's specifiers or parameter types are delayed until that
    // declarator is complete. Initializers and type names in expressions retain their own use boundaries;
    // record members and block declarations have separate availability contexts above.
    const bool delayed = attribute != NULL && !inside && scopes == 0 && !tag && !anonymous_use &&
                         role == PARSE_ROLE_SPECIFIER && check_input_typedef_declared(reader, declared) && reader->declaration_starts != NULL;
    if (delayed)
    {
        const uint32_t first = reader->declaration_starts[t];
        if (pass->delayed_first != first)
        {
            pass->delayed_first = first;
            pass->delayed_end = check_input_availability_declarator_end(parser, first);
        }
        while (pass->delayed_end < t && parser->tokens.kinds[pass->delayed_end] == PARSE_KIND_COMMA)
        {
            pass->delayed_end = check_input_availability_declarator_end(parser, pass->delayed_end + 1);
        }
        completion = pass->delayed_end > t ? pass->delayed_end : completion;
    }
    // The attribute's message, its first argument's string.
    const uint32_t message = attribute != NULL && attribute->argument_first < attribute->argument_end &&
                             parser->tokens.kinds[attribute->argument_first] == PARSE_KIND_STRING ? attribute->argument_first : BUSTER_CHECK_WARNING_NO_SOURCE;
    if (attribute != NULL && attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE && pass->errors)
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
            reader->output->reports[before].completion = completion;
            // A token's reports print its first alone: the copies after it ride on it.
            reader->output->reports[before].value = copies - 1;
        }
    }
    else if (attribute != NULL && attribute->kind != CHECK_ATTRIBUTE_UNAVAILABLE)
    {
        CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_DEPRECATED_DECLARATIONS,
            .kind = CHECK_WARNING_EVENT_DEPRECATED_USE,
            .token = t,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = completion,
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

// The keys `marks` marks, of `count` (its entries that aren't 0), as a filter's halves (CheckInputFilter), which also
// take every token whose object's first declaration (`objects`, where not null) is such a key: the declarations whose
// key is marked, as check_input_deprecated_marked gives a declaration its key. A unit marks few keys. Two scans, 16
// entries at a time: the marks, then the objects, whose first declarations the marked keys' own filter tests.
static CheckInputFilter check_input_deprecated_mark_filter(const uint32_t* marks, const uint32_t* objects, uint32_t count)
{
    uint32_t low[16] = { 0 };
    uint32_t high[16] = { 0 };
    uint32_t marked = 0;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - first < 16 ? count - first : 16);
        const __m512i values = _mm512_maskz_loadu_epi32(lanes, marks + first);
        for (uint32_t keys = _mm512_mask_test_epi32_mask(lanes, values, values); keys != 0; keys &= keys - 1)
        {
            const uint32_t key = first + (uint32_t)__builtin_ctz(keys);
            low[(key / 32) % 16] |= UINT32_C(1) << (key % 32);
            high[(key / 16384) % 16] |= UINT32_C(1) << ((key / 512) % 32);
            marked += 1;
        }
    }
    const CheckInputFilter keys = { .low = _mm512_loadu_si512(low), .high = _mm512_loadu_si512(high) };
    const __m512i limit = _mm512_set1_epi32((int32_t)count);
    // Without a marked key, no object's first declaration is one, and the scan is left out.
    for (uint32_t first = 0; objects != NULL && marked != 0 && first < count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, count - first < 16 ? count - first : 16);
        const __m512i object_first = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(lanes, objects + first), _mm512_set1_epi32(1));
        const __mmask16 owned = _mm512_mask_cmplt_epu32_mask(lanes, object_first, limit);
        for (uint32_t selected = check_input_filter_test(&keys, object_first, owned); selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            if (marks[objects[token] - 1] != 0)
            {
                low[(token / 32) % 16] |= UINT32_C(1) << (token % 32);
                high[(token / 16384) % 16] |= UINT32_C(1) << ((token / 512) % 32);
            }
        }
    }
    return (CheckInputFilter){ .low = _mm512_loadu_si512(low), .high = _mm512_loadu_si512(high) };
}

// Bit t of `words`, a word for each 64 tokens: whether the identifier at t is a use or a declaration whose key a
// deprecated or unavailable attribute marks (`marks`, check_input_deprecated_key's): its declaration's object's first
// declaration where it has one, else its declaration. The vector path's walks (check_input_availability_events,
// check_input_deprecated_block) visit these and few other tokens. 16 identifiers' declarations are tested against a
// filter of the declarations whose key is marked (check_input_deprecated_mark_filter), and only those 16 that pass
// gather their objects and their keys' marks (#529): the two gathers at every 16 identifiers took 0.7% of
// -c sqlite3.c's cycles, for the few keys glibc's headers mark.
// Keep this whole-column leaf out of the large reader's frame: LLVM 23.1.4 MSan can allocate wide callback
// shadows at a check inside the loop after inlining (#1610). Its own small instrumentation context retains every
// check without inheriting the reader's callback threshold. The loop and its vector-only admission stay unchanged.
static __attribute__((noinline)) void check_input_deprecated_marked(const CheckInputReader* reader, const uint32_t* marks, uint64_t* words)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t* declarations = reader->output->declarations;
    const uint32_t* objects = reader->objects;
    const __m512i limit = _mm512_set1_epi32((int32_t)count);
    const __m512i one = _mm512_set1_epi32(1);
    const CheckInputFilter filter = check_input_deprecated_mark_filter(marks, objects, count);
    uint32_t first = 0;
    while (first < count)
    {
        const uint32_t length = count - first < 64 ? count - first : 64;
        const uint64_t valid = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, parser->tokens.kinds + first);
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(PARSE_KIND_IDENTIFIER));
        uint64_t word = 0;
        for (uint32_t lane = 0; lane < 64; lane += 16)
        {
            const __mmask16 lanes = (__mmask16)(identifiers >> lane);
            if (lanes != 0)
            {
                const __m512i declared = _mm512_maskz_loadu_epi32(lanes, declarations + first + lane);
                const __mmask16 known = _mm512_mask_cmplt_epu32_mask(lanes, declared, limit);
                const __mmask16 candidates = check_input_filter_test(&filter, declared, known);
                if (candidates != 0)
                {
                    // Its declaration, then that declaration's object's first declaration where it has one.
                    const __m512i object = objects != NULL ? _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, declared, objects, 4) : _mm512_setzero_si512();
                    const __m512i object_first = _mm512_sub_epi32(object, one);
                    const __m512i key = _mm512_mask_mov_epi32(declared, _mm512_cmplt_epu32_mask(object_first, limit), object_first);
                    const __m512i mark = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, key, marks, 4);
                    word |= (uint64_t)_mm512_mask_test_epi32_mask(candidates, mark, mark) << lane;
                }
            }
        }
        words[first / 64] = word;
        // length <= count - first: first reaches count exactly, even at the u32 limit.
        first += length;
    }
}

// The 64 tokens' columns the vector path's walk of check_input_deprecated_uses reads, from `first` (`length` of them): the
// brackets that open and close a level, the parameter lists' and calls' parentheses, the scopes' braces and the bodies'
// `{`, as the scalar walk reads each token's, and the tokens check_input_deprecated_use may do anything at: a struct's,
// a union's or an enum's keyword; a tag's name, or a leaf one that may be; an identifier whose key is marked (`marked`,
// check_input_deprecated_marked), every identifier where a tag is marked (`tags`: an enumerator's enum's, a typedef's
// struct's), and a member only the evaluator reads (`evaluated`). At any other token it does nothing.
typedef struct CheckInputDeprecatedBlock CheckInputDeprecatedBlock;
struct CheckInputDeprecatedBlock
{
    uint64_t opens;
    uint64_t closes;
    uint64_t list_opens;
    uint64_t list_closes;
    uint64_t pushes;
    uint64_t pops;
    uint64_t bodies;
    uint64_t uses;
};

static CheckInputDeprecatedBlock check_input_deprecated_block(const Parser* parser, uint32_t first, uint32_t length, uint64_t marked, bool tags, const uint32_t* evaluated)
{
    const uint64_t valid = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
    const __m512i kinds = _mm512_maskz_loadu_epi8(valid, parser->tokens.kinds + first);
    const __m512i roles = _mm512_maskz_loadu_epi8(valid, parser->tree.roles + first);
    const uint64_t identifiers = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_IDENTIFIER));
    const uint64_t left_braces = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_L_BRACE));
    const uint64_t right_braces = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_R_BRACE));
    const uint64_t leaves = _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_TAG)) | _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_LEAF_RIGHT));
    uint64_t members = 0;
    for (uint32_t lane = 0; evaluated != NULL && lane < 64; lane += 16)
    {
        const __m512i names = _mm512_maskz_loadu_epi32((__mmask16)(valid >> lane), evaluated + first + lane);
        members |= (uint64_t)_mm512_test_epi32_mask(names, names) << lane;
    }
    const CheckInputDeprecatedBlock result = {
        .opens = (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_L_PAREN)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_L_SQUARE)) | left_braces) &
                 valid,
        .closes = (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_R_PAREN)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_R_SQUARE)) | right_braces) &
                  valid,
        .list_opens = (_mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_FUNCTION)) | _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_CALL))) & valid,
        .list_closes = (_mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_FUNCTION)) | _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_CALL))) &
                       valid,
        .pushes = left_braces &
                  (_mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_STRUCT)) | _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY)) |
                   _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_BLOCK))) & valid,
        .pops = right_braces &
                (_mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_STRUCT)) | _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_BODY)) |
                 _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_BLOCK))) & valid,
        .bodies = _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY)) & valid,
        .uses = (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_STRUCT)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_ENUM)) |
                 (identifiers & (leaves | (tags ? UINT64_MAX : 0))) | marked | members) & valid,
    };
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
// The scalar path walks every token (check_input_deprecated_use). A unit with such an attribute marks a few declarations
// (glibc's headers mark a handful of functions), so the vector path visits only the tokens a use may be at and the
// braces that open and close the scopes they read, with the brackets between them counted 64 tokens a mask (#1489):
// sqlite3.c's 716,158 tokens took 25 M cycles of the 9700X's for the scalar walk.
static void check_input_deprecated_uses(CheckInputReader* reader, bool errors)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    // The vector path's gathers index by signed 32-bit lanes.
    const bool vector = BUSTER_VECTOR_PATH(!parser->options.scalar) && count <= INT32_MAX;
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
    // Each enumerator's enum key plus one, memoized by its token (#1354).
    uint32_t* enums = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    // The vector path's identifiers whose key is marked (check_input_deprecated_marked), a bit a token.
    uint64_t* marked = vector ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)count / 64 + 1) : NULL;
    const bool allocated = marks != NULL && chain != NULL && latest != NULL && member_marks != NULL && scope_levels != NULL && scope_lists != NULL && scope_records != NULL &&
                           tag_keys != NULL && enums != NULL && (!vector || marked != NULL);
    reader->output->resource |= !allocated;
    if (allocated)
    {
        memset(tag_keys, 0, ((uint64_t)tag_count + 1) * sizeof(uint32_t));
        memset(enums, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        if (vector)
        {
            // The tags' declarations, 64 tokens a mask, in order.
            uint32_t first = 0;
            while (first < count)
            {
                const uint32_t length = count - first < 64 ? count - first : 64;
                const uint64_t valid = length == 64 ? UINT64_MAX : (UINT64_C(1) << length) - 1;
                const __m512i kinds = _mm512_maskz_loadu_epi8(valid, reader->declaration_kinds + first);
                for (uint64_t tags = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_BINDING_TAG_KIND)); tags != 0; tags &= tags - 1)
                {
                    const uint32_t i = first + (uint32_t)__builtin_ctzll(tags);
                    const uint32_t tag = check_input_tag_of(reader->types, reader->declared_types[i]);
                    if (tag != 0 && tag < tag_count && tag_keys[tag] == 0)
                    {
                        tag_keys[tag] = i + 1;
                    }
                }
                first += length;
            }
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t tag = reader->declaration_kinds[i] == C_BINDING_TAG_KIND ? check_input_tag_of(reader->types, reader->declared_types[i]) : 0;
                if (tag != 0 && tag < tag_count && tag_keys[tag] == 0)
                {
                    tag_keys[tag] = i + 1;
                }
            }
        }
        memset(marks, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(chain, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(latest, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        memset(member_marks, 0, (uint64_t)count + 1);
        for (uint32_t i = 0; i < reader->output->marked_member_count; i += 1)
        {
            const CheckInputMarkedMember* marked_member = reader->output->marked_members + i;
            const uint8_t bit = marked_member->unavailable ? BUSTER_CHECK_CONTEXT_UNAVAILABLE : BUSTER_CHECK_CONTEXT_DEPRECATED;
            member_marks[marked_member->name < count ? marked_member->name : count] = bit;
        }
        reader->member_marks = member_marks;
        const bool scalar = parser->options.scalar;
        const CheckSparseWide all = reader->output->attributes;
        // Whether an attribute marks what may be a tag (an enumerator's use reads its enum's marks, and a typedef's use
        // its struct's, union's or enum's): any mark but on a function, an object or an enumerator, or on a keyword.
        bool tags = false;
        // In source order, so that each key's chain runs first to last.
        for (uint32_t first = check_input_attributed_next(all, 0, count, scalar); first < count; first = check_input_attributed_next(all, first + 16, count, scalar))
        {
            const uint32_t lanes = check_input_attributed_group(all, first, count, scalar);
            for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const bool recorded = check_input_deprecated_record(check_input_attributes_at(reader->output, d)) != NULL;
                const uint32_t key = recorded ? check_input_deprecated_key(reader, d) : count;
                const uint8_t key_kind = key < count ? reader->declaration_kinds[key] : C_BINDING_TYPEDEF;
                const bool named = key < count && parser->tokens.kinds[key] == PARSE_KIND_IDENTIFIER &&
                                   (key_kind == C_BINDING_OBJECT || key_kind == C_BINDING_FUNCTION || key_kind == C_BINDING_ENUMERATOR);
                tags |= recorded && (parser->tokens.kinds[d] != PARSE_KIND_IDENTIFIER || (key < count && !named));
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
        // `latest` again all zeros, for the walk: the loop above wrote it only at its keys, which the same tokens give
        // again (#529), where a fill wrote every token's entry.
        for (uint32_t first = check_input_attributed_next(all, 0, count, scalar); first < count; first = check_input_attributed_next(all, first + 16, count, scalar))
        {
            for (uint32_t selected = check_input_attributed_group(all, first, count, scalar); selected != 0; selected &= selected - 1)
            {
                const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t key = check_input_deprecated_record(check_input_attributes_at(reader->output, d)) != NULL ? check_input_deprecated_key(reader, d) : count;
                latest[key < count ? key : count] = 0;
            }
        }
        check_input_overload_marks(reader, marks, chain);
        if (vector)
        {
            check_input_deprecated_marked(reader, marks, marked);
        }
        // Each token's context (check_input_availability_contexts): a deprecated or unavailable function's body or
        // declaration's initializer, which drops its uses' diagnostics as clang's ShouldDiagnoseAvailabilityInContext does.
        check_input_availability_contexts(reader, marks, chain, marked);
        CheckInputDeprecatedPass pass = {
            .marks = marks,
            .chain = chain,
            .latest = latest,
            .member_marks = member_marks,
            .scope_levels = scope_levels,
            .scope_lists = scope_lists,
            .scope_records = scope_records,
            .tag_keys = tag_keys,
            .enums = enums,
            .tag_count = tag_count,
            .delayed_first = count,
            .contexts = reader->output->availability_contexts,
            .evaluated = check_input_evaluated_members(reader),
            .errors = errors,
        };
        // Where the function body a use is in ends.
        uint32_t body_end = 0;
        // The brackets open before a use (`(`, `[` and `{` less their closers): a file-scope declaration's own level is 0.
        // And of them, the parameter lists (a function declarator's parentheses, a call's to the vector parser).
        int32_t level = 0;
        int32_t lists = 0;
        uint32_t scopes = 0;
        if (vector)
        {
            // Each event in order: a use with the levels the tokens before it open, by their masks' bits below it; a
            // body's `{` (inside: before its update, as the scalar walk reads it); a scope's brace, which the tokens
            // after it read, as the scalar walk's `previous`.
            uint32_t first = 0;
            while (first < count)
            {
                const uint32_t length = count - first < 64 ? count - first : 64;
                const CheckInputDeprecatedBlock block = check_input_deprecated_block(parser, first, length, marked[first / 64], tags, pass.evaluated);
                for (uint64_t events = block.uses | block.bodies | block.pushes | block.pops; events != 0; events &= events - 1)
                {
                    const uint32_t bit = (uint32_t)__builtin_ctzll(events);
                    const uint64_t below = (UINT64_C(1) << bit) - 1;
                    const uint64_t self = UINT64_C(1) << bit;
                    const uint32_t t = first + bit;
                    const int32_t at_level = level + __builtin_popcountll(block.opens & below) - __builtin_popcountll(block.closes & below);
                    const int32_t at_lists = lists + __builtin_popcountll(block.list_opens & below) - __builtin_popcountll(block.list_closes & below);
                    const bool inside = t < body_end;
                    if ((block.uses & self) != 0)
                    {
                        check_input_deprecated_use(reader, &pass, t, at_level, at_lists, scopes, inside);
                    }
                    if ((block.bodies & self) != 0 && !inside && parser->tree.parents[t] < count)
                    {
                        body_end = parser->tree.parents[t];
                    }
                    if ((block.pushes & self) != 0)
                    {
                        scope_levels[scopes] = at_level + ((block.opens & self) != 0) - ((block.closes & self) != 0);
                        scope_lists[scopes] = at_lists + ((block.list_opens & self) != 0) - ((block.list_closes & self) != 0);
                        scope_records[scopes] = parser->tree.roles[t] == PARSE_ROLE_OPEN_STRUCT;
                        scopes += 1;
                    }
                    scopes -= (block.pops & self) != 0 && scopes != 0;
                }
                level += __builtin_popcountll(block.opens) - __builtin_popcountll(block.closes);
                lists += __builtin_popcountll(block.list_opens) - __builtin_popcountll(block.list_closes);
                // length <= count - first: first reaches count exactly, even at the u32 limit.
                first += length;
            }
        }
        for (uint32_t t = 0; !vector && t < count; t += 1)
        {
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
            check_input_deprecated_use(reader, &pass, t, level, lists, scopes, inside);
        }
    }
    reader->member_marks = NULL;
    arena_pop_to(reader->scratch, mark);
}

