// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C values in DWARF (#1114), included by c_check_debug.c after CheckDebug. debug.c still owns the line program,
// function address table and CU range list. This opt-in producer enriches its info/abbreviations, appends lexical
// ranges and writes location lists. Only final Tier 0 homes are described; a value without a proved home has no
// location. Nothing here changes instruction selection or forces a register value to memory for debugging.
//
// Types are a bounded worklist, never a recursive walk (records may point back to themselves). First measure all
// DIEs and fix their CU-relative offsets, then write the same layout, so forward references require no relocation.
// NativeDebugExtra retains only section references and global definition addresses for the two native writers.
// DWARF 5 sections 2.6, 3.3/3.5, 4.1, 5 and 7 define the format; no third-party implementation is used.

typedef enum CheckDebugAbbreviation
{
    CHECK_DEBUG_FUNCTION = 3,
    CHECK_DEBUG_VOID = 4,
    CHECK_DEBUG_BASE = 5,
    CHECK_DEBUG_POINTER = 6,
    CHECK_DEBUG_CONST = 7,
    CHECK_DEBUG_VOLATILE = 8,
    CHECK_DEBUG_RESTRICT = 9,
    CHECK_DEBUG_ATOMIC = 10,
    CHECK_DEBUG_TYPEDEF = 11,
    CHECK_DEBUG_ARRAY = 12,
    CHECK_DEBUG_COUNT = 13,
    CHECK_DEBUG_UNKNOWN_COUNT = 14,
    CHECK_DEBUG_STRUCT = 15,
    CHECK_DEBUG_UNION = 16,
    CHECK_DEBUG_MEMBER = 17,
    CHECK_DEBUG_BIT_FIELD = 18,
    CHECK_DEBUG_ENUM = 19,
    CHECK_DEBUG_ENUM_SIGNED = 20,
    CHECK_DEBUG_ENUM_UNSIGNED = 21,
    CHECK_DEBUG_SIGNATURE = 22,
    CHECK_DEBUG_PARAMETER_TYPE = 23,
    CHECK_DEBUG_VARIADIC = 24,
    CHECK_DEBUG_INCOMPLETE_STRUCT = 25,
    CHECK_DEBUG_INCOMPLETE_UNION = 26,
    CHECK_DEBUG_INCOMPLETE_ENUM = 27,
    CHECK_DEBUG_GLOBAL = 28,
    CHECK_DEBUG_LOCAL = 29,
    CHECK_DEBUG_PARAMETER = 30,
    CHECK_DEBUG_UNAVAILABLE_LOCAL = 31,
    CHECK_DEBUG_UNAVAILABLE_PARAMETER = 32,
    CHECK_DEBUG_BLOCK = 33,
    CHECK_DEBUG_VECTOR = 34,
    CHECK_DEBUG_UNAVAILABLE_GLOBAL = 35,
    CHECK_DEBUG_ENUM_WIDE = 36,
    CHECK_DEBUG_INLINED = 37,
    CHECK_DEBUG_ABSTRACT = 38,
    CHECK_DEBUG_INLINED_PARAMETER = 39,
    CHECK_DEBUG_INLINED_VARIABLE = 40,
} CheckDebugAbbreviation;

static const uint8_t check_debug_value_abbreviations[] = {
    0x03, 0x2e, 0x01, 0x03, 0x08, 0x49, 0x13, 0x11, 0x1b, 0x12, 0x06, 0x3a, 0x0f, 0x3b, 0x0f, 0x00, 0x00, // FUNCTION
    0x04, 0x3b, 0x00, 0x03, 0x08, 0x00, 0x00, // VOID
    0x05, 0x24, 0x00, 0x03, 0x08, 0x0b, 0x0f, 0x3e, 0x0b, 0x0d, 0x0f, 0x00, 0x00, // BASE
    0x06, 0x0f, 0x00, 0x0b, 0x0b, 0x49, 0x13, 0x00, 0x00, // POINTER
    0x07, 0x26, 0x00, 0x49, 0x13, 0x00, 0x00, // CONST
    0x08, 0x35, 0x00, 0x49, 0x13, 0x00, 0x00, // VOLATILE
    0x09, 0x37, 0x00, 0x49, 0x13, 0x00, 0x00, // RESTRICT
    0x0a, 0x47, 0x00, 0x49, 0x13, 0x00, 0x00, // ATOMIC
    0x0b, 0x16, 0x00, 0x03, 0x08, 0x49, 0x13, 0x00, 0x00, // TYPEDEF
    0x0c, 0x01, 0x01, 0x49, 0x13, 0x00, 0x00, // ARRAY
    0x0d, 0x21, 0x00, 0x37, 0x0f, 0x00, 0x00, // COUNT
    0x0e, 0x21, 0x00, 0x00, 0x00, // UNKNOWN_COUNT
    0x0f, 0x13, 0x01, 0x03, 0x08, 0x0b, 0x0f, 0x00, 0x00, // STRUCT
    0x10, 0x17, 0x01, 0x03, 0x08, 0x0b, 0x0f, 0x00, 0x00, // UNION
    0x11, 0x0d, 0x00, 0x03, 0x08, 0x49, 0x13, 0x38, 0x0f, 0x00, 0x00, // MEMBER
    0x12, 0x0d, 0x00, 0x03, 0x08, 0x49, 0x13, 0x0d, 0x0f, 0x6b, 0x0f, 0x00, 0x00, // BIT_FIELD
    0x13, 0x04, 0x01, 0x03, 0x08, 0x49, 0x13, 0x0b, 0x0f, 0x00, 0x00, // ENUM
    0x14, 0x28, 0x00, 0x03, 0x08, 0x1c, 0x0d, 0x00, 0x00, // ENUM_SIGNED
    0x15, 0x28, 0x00, 0x03, 0x08, 0x1c, 0x0f, 0x00, 0x00, // ENUM_UNSIGNED
    0x16, 0x15, 0x01, 0x49, 0x13, 0x27, 0x0c, 0x00, 0x00, // SIGNATURE
    0x17, 0x05, 0x00, 0x49, 0x13, 0x00, 0x00, // PARAMETER_TYPE
    0x18, 0x18, 0x00, 0x00, 0x00, // VARIADIC
    0x19, 0x13, 0x00, 0x03, 0x08, 0x3c, 0x19, 0x00, 0x00, // INCOMPLETE_STRUCT
    0x1a, 0x17, 0x00, 0x03, 0x08, 0x3c, 0x19, 0x00, 0x00, // INCOMPLETE_UNION
    0x1b, 0x04, 0x00, 0x03, 0x08, 0x3c, 0x19, 0x00, 0x00, // INCOMPLETE_ENUM
    0x1c, 0x34, 0x00, 0x03, 0x08, 0x49, 0x13, 0x3f, 0x0c, 0x02, 0x18, 0x00, 0x00, // GLOBAL
    0x1d, 0x34, 0x00, 0x03, 0x08, 0x49, 0x13, 0x02, 0x17, 0x00, 0x00, // LOCAL
    0x1e, 0x05, 0x00, 0x03, 0x08, 0x49, 0x13, 0x02, 0x17, 0x00, 0x00, // PARAMETER
    0x1f, 0x34, 0x00, 0x03, 0x08, 0x49, 0x13, 0x00, 0x00, // UNAVAILABLE_LOCAL
    0x20, 0x05, 0x00, 0x03, 0x08, 0x49, 0x13, 0x00, 0x00, // UNAVAILABLE_PARAMETER
    0x21, 0x0b, 0x01, 0x55, 0x17, 0x00, 0x00, // BLOCK
    0x22, 0x01, 0x01, 0x49, 0x13, 0x87, 0x42, 0x19, 0x00, 0x00, // VECTOR
    0x23, 0x34, 0x00, 0x03, 0x08, 0x49, 0x13, 0x3f, 0x0c, 0x00, 0x00, // UNAVAILABLE_GLOBAL
    0x24, 0x28, 0x00, 0x03, 0x08, 0x1c, 0x1e, 0x00, 0x00, // ENUM_WIDE
    0x25, 0x1d, 0x01, 0x31, 0x13, 0x55, 0x17, 0x58, 0x0f, 0x59, 0x0f, 0x00, 0x00, // INLINED
    0x26, 0x2e, 0x01, 0x03, 0x08, 0x49, 0x13, 0x3a, 0x0f, 0x3b, 0x0f, 0x20, 0x0b, 0x00, 0x00, // ABSTRACT
    0x27, 0x05, 0x00, 0x31, 0x13, 0x02, 0x17, 0x00, 0x00, // INLINED_PARAMETER
    0x28, 0x34, 0x00, 0x31, 0x13, 0x02, 0x17, 0x00, 0x00, // INLINED_VARIABLE
    0,
};

// The half-open interval of machine spans that may belong to an inline instance or its descendants.
typedef struct CheckDebugInlineSpans CheckDebugInlineSpans;
struct CheckDebugInlineSpans
{
    uint32_t first;
    uint32_t end;
};

typedef struct CheckDebugValues CheckDebugValues;
struct CheckDebugValues
{
    const CheckProgram* program;
    const CheckProgramRows* rows;
    const Parser* parser;
    const CheckDebug* original;
    uint32_t* type_offsets;
    TypeId* types;
    uint32_t type_count;
    uint32_t* enum_definitions;
    uint32_t* enum_owners;
    uint32_t* local_ids;
    uint32_t* scopes;
    // Beside each open scope of a body's walk, its block's opening token: in check_debug_value_own_blocks, the block its
    // declarations mark; in the write, where it has a DIE to close (BUSTER_CHECK_NONE where it has none, #2543).
    uint32_t* scope_tokens;
    // Whether each body's blocks have their CHECK_DEBUG_OWN marks, which the first of the writer's two passes sets.
    bool blocks_marked;
    // By token: CHECK_DEBUG_ROOT where the token is in one of the unit's roots, CHECK_DEBUG_USED at a file-scope typedef's
    // name that one names (check_debug_value_roots).
    uint8_t* marks;
    // By token, the last token of its statement's subtree. The parser's column where it kept one; otherwise derived for a
    // body's tokens when the body's first lexical block needs them (check_debug_body_ends, #2544), with `ends_ready`
    // marking those bodies for the second pass and `ends_children`, `ends_queue` its scratch.
    const uint32_t* ends;
    uint32_t* derived_ends;
    uint32_t* ends_children;
    uint32_t* ends_queue;
    uint8_t* ends_ready;
    bool ends_failed;
    DebugWriter info;
    DebugWriter ranges;
    DebugWriter locations;
    NativeDebugField* fields;
    NativeDebugAddress* addresses;
    uint32_t field_count;
    uint32_t address_count;
    // The spans' index of the function `span_body` names (#1640, check_debug_value_index): each span's next address break,
    // the starts of its runs of non-decreasing tokens (and the span count after them), and the runs' token tree. Null where
    // the scratch had no room, which leaves every list to check_debug_value_range_scan.
    uint32_t* span_breaks;
    uint32_t* span_segments;
    uint32_t* span_tree;
    uint32_t span_leaves;
    uint32_t span_segment_count;
    uint32_t span_body;
    // Each expanded callee's abstract instance's offset in the unit (a DW_TAG_subprogram with DW_AT_inline and no code,
    // written before its own DW_TAG_subprogram), which the measuring pass sets and the writing pass reads, its instances'
    // abstract origin; which bodies some instance expands; and, for a function's instances, the open ones (a stack) and
    // their order by call token (CheckDebugInline, #2541 step 3), room for the most a function has. An instance's origin
    // can't be the out-of-line DW_TAG_subprogram: a debugger reads an attribute an entry lacks from its origin, and gdb 18
    // (and CI's) takes the inherited DW_AT_low_pc over the instance's DW_AT_ranges, which drops the instance.
    uint32_t* function_offsets;
    uint8_t* expanded;
    // Per parameter or local token of an expanded callee, its abstract instance's DW_TAG_formal_parameter's or
    // DW_TAG_variable's offset, which its instances' refer to (#2541), set as function_offsets are.
    uint32_t* parameter_dies;
    // Each body's index among the unit's functions (CheckDebug.unit, the bodies with code), its .debug_addr entry's, which
    // DW_AT_low_pc and the lists' DW_RLE/DW_LLE_base_addressx name; BUSTER_CHECK_NONE for a dead one, which has no address.
    uint32_t* address_indices;
    uint32_t* inline_open;
    uint64_t* inline_order;
    CheckDebugInlineSpans* inline_spans;
    // The tokens the sweeps stop at (check_debug_value_events), in token order: `events`, each that declares itself
    // (CheckInput.declarations) or opens a lexical block, and `root_ends`, each file-scope declaration's last token.
    // Every other token is neither a declaration nor a scope's opening, so the sweeps read past it.
    uint32_t* events;
    uint32_t event_count;
    uint32_t* root_ends;
    uint32_t root_count;
};

static String8 check_debug_name(const CheckDebugValues* values, uint32_t identifier)
{
    const InternTable* table = values->program->identifiers;
    const String8 result = { .pointer = (char*)intern_bytes(table, identifier), .length = intern_length(table, identifier) };
    return result;
}

static void check_debug_type_add(CheckDebugValues* values, TypeId type)
{
    if (type_is_valid(values->program->types, type) && values->type_offsets[type] == 0)
    {
        values->type_offsets[type] = 1;
        values->types[values->type_count++] = type;
    }
}

static void check_debug_type_reference(CheckDebugValues* values, TypeId type)
{
    // Every reference was reached by the worklist. The measuring pass may see a forward offset of 1, but its
    // fixed-width ref4 is the same size. Writing starts only after the whole measuring pass has assigned offsets.
    debug_integer(&values->info, values->type_offsets[type], 4);
}

static void check_debug_value_field(CheckDebugValues* values, uint32_t section, uint64_t offset)
{
    if (values->fields != NULL)
    {
        values->fields[values->field_count] = (NativeDebugField){ .offset = (uint32_t)values->info.size, .section = section };
    }
    values->field_count += 1;
    debug_integer(&values->info, offset, 4);
}

static void check_debug_value_type(CheckDebugValues* values, TypeId type)
{
    const TypeTable* table = values->program->types;
    const uint32_t index = type_index(type);
    const uint32_t qualifiers = type_qualifiers(type);
    const uint32_t kind = table->types.kind[index];
    const TypeId base = table->types.base[index];
    DebugWriter* info = &values->info;
    values->type_offsets[type] = (uint32_t)info->size;
    if (qualifiers != 0)
    {
        const uint32_t qualifier = qualifiers & (0u - qualifiers);
        debug_uleb(info, qualifier == BUSTER_TYPE_CONST ? CHECK_DEBUG_CONST : qualifier == BUSTER_TYPE_VOLATILE ? CHECK_DEBUG_VOLATILE : CHECK_DEBUG_RESTRICT);
        check_debug_type_reference(values, type & ~qualifier);
    }
    else if (kind == BUSTER_TYPE_KIND_VOID)
    {
        debug_uleb(info, CHECK_DEBUG_VOID);
        debug_string(info, BUSTER_S8("void"));
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_ALIGNED ||
             kind == BUSTER_TYPE_KIND_QUALIFIED)
    {
        debug_uleb(info, kind == BUSTER_TYPE_KIND_POINTER ? CHECK_DEBUG_POINTER : kind == BUSTER_TYPE_KIND_ATOMIC ? CHECK_DEBUG_ATOMIC : CHECK_DEBUG_TYPEDEF);
        if (kind == BUSTER_TYPE_KIND_POINTER)
        {
            debug_integer(info, 8, 1);
        }
        else if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_QUALIFIED)
        {
            debug_string(info, BUSTER_S8(""));
        }
        check_debug_type_reference(values, base);
    }
    else if (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const bool vector = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
        debug_uleb(info, vector ? CHECK_DEBUG_VECTOR : CHECK_DEBUG_ARRAY);
        check_debug_type_reference(values, base);
        const bool known = vector || kind == BUSTER_TYPE_KIND_ARRAY;
        debug_uleb(info, known ? CHECK_DEBUG_COUNT : CHECK_DEBUG_UNKNOWN_COUNT);
        if (known)
        {
            debug_uleb(info, table->types.extent[index]);
        }
        debug_integer(info, 0, 1);
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        debug_uleb(info, CHECK_DEBUG_SIGNATURE);
        check_debug_type_reference(values, base);
        debug_integer(info, (table->types.flags[index] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0, 1);
        const uint32_t first = table->types.index[index];
        const uint64_t count = table->types.extent[index];
        for (uint64_t p = 0; p < count; p += 1)
        {
            debug_uleb(info, CHECK_DEBUG_PARAMETER_TYPE);
            check_debug_type_reference(values, table->parameters.type[first + p]);
        }
        if ((table->types.flags[index] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
        {
            debug_uleb(info, CHECK_DEBUG_VARIADIC);
        }
        debug_integer(info, 0, 1);
    }
    else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
    {
        const uint32_t tag = table->types.index[index];
        const bool complete = type_is_complete(table, type);
        const uint32_t code = kind == BUSTER_TYPE_KIND_STRUCT ? (complete ? CHECK_DEBUG_STRUCT : CHECK_DEBUG_INCOMPLETE_STRUCT) :
                              kind == BUSTER_TYPE_KIND_UNION ? (complete ? CHECK_DEBUG_UNION : CHECK_DEBUG_INCOMPLETE_UNION) :
                                                              (complete ? CHECK_DEBUG_ENUM : CHECK_DEBUG_INCOMPLETE_ENUM);
        debug_uleb(info, code);
        debug_string(info, check_debug_name(values, table->tags.name[tag]));
        if (complete && kind == BUSTER_TYPE_KIND_ENUM)
        {
            check_debug_type_reference(values, base);
        }
        if (complete)
        {
            debug_uleb(info, type_size(table, type));
        }
        if (complete && kind != BUSTER_TYPE_KIND_ENUM)
        {
            const uint32_t first = table->tags.member_begin[tag];
            const uint32_t count = table->tags.member_count[tag];
            for (uint32_t m = first; m < first + count; m += 1)
            {
                const bool bits = (table->members.flags[m] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                // Unnamed bit fields reserve padding, not an object a debugger can name.
                if (!bits || table->members.name[m] != 0)
                {
                    debug_uleb(info, bits ? CHECK_DEBUG_BIT_FIELD : CHECK_DEBUG_MEMBER);
                    debug_string(info, check_debug_name(values, table->members.name[m]));
                    check_debug_type_reference(values, table->members.type[m]);
                    if (bits)
                    {
                        debug_uleb(info, table->members.bit_width[m]);
                    }
                    debug_uleb(info, bits ? table->members.offset[m] : table->members.offset[m] / 8);
                }
            }
        }
        if (complete && kind == BUSTER_TYPE_KIND_ENUM && values->enum_definitions[index] != 0)
        {
            const CheckInput* input = values->program->input;
            const CheckInputEnumDefinition definition = input->enum_definitions[values->enum_definitions[index] - 1];
            uint64_t rank = 0;
            uint64_t width = 0;
            bool signed_value = false;
            type_integer_properties(table, base, &rank, &width, &signed_value);
            uint32_t low = 0;
            uint32_t high = input->enum_value_count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                if (input->enum_values[middle].token <= definition.open)
                {
                    low = middle + 1;
                }
                else
                {
                    high = middle;
                }
            }
            for (uint32_t e = low; e < input->enum_value_count && input->enum_values[e].token < definition.close; e += 1)
            {
                if (values->enum_owners[e] != values->enum_definitions[index])
                {
                    continue;
                }
                const CheckInputEnumValue enumerator = input->enum_values[e];
                // The checker keeps narrow signed constants as masked bits, not sign-extended host integers.
                // Reuse its conversion rules before choosing SLEB128 or a wide constant's 16-byte representation.
                CheckValue constant = { .type = input->declared_types[enumerator.token], .integer = enumerator.value, .constant_kind = CHECK_CONSTANT_INTEGER };
                check_action_convert_plain(table, &constant, BUSTER_TYPE_INT128, enumerator.token);
                const unsigned __int128 value = constant.integer;
                const bool wide = signed_value ? (__int128)value < INT64_MIN || (__int128)value > INT64_MAX : value > UINT64_MAX;
                debug_uleb(info, wide ? CHECK_DEBUG_ENUM_WIDE : signed_value ? CHECK_DEBUG_ENUM_SIGNED : CHECK_DEBUG_ENUM_UNSIGNED);
                debug_string(info, check_debug_name(values, values->parser->tokens.names[enumerator.token]));
                if (wide)
                {
                    debug_bytes(info, &value, 16);
                }
                else if (signed_value)
                {
                    debug_sleb(info, (int64_t)value);
                }
                else
                {
                    debug_uleb(info, (uint64_t)value);
                }
            }
        }
        if (complete)
        {
            debug_integer(info, 0, 1);
        }
    }
    else
    {
        char spelling[96];
        const char* name = index < BUSTER_TYPE_CODE_COMPLEX ? type_builtins[index].name : NULL;
        uint32_t encoding = kind == BUSTER_TYPE_KIND_FLOATING ? 4 : kind == BUSTER_TYPE_KIND_NULLPTR ? 1 : 7;
        uint64_t bits = type_size(table, type) * 8;
        if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            uint64_t rank = 0;
            bool signed_value = false;
            type_integer_properties(table, type, &rank, &bits, &signed_value);
            encoding = index == BUSTER_TYPE_CODE_BOOL ? 2 : signed_value ? 5 : 7;
            if (index == BUSTER_TYPE_CODE_CHAR || index == BUSTER_TYPE_CODE_SCHAR || index == BUSTER_TYPE_CODE_UCHAR)
            {
                encoding = signed_value ? 6 : 8;
            }
            if (kind == BUSTER_TYPE_KIND_BIT_INT)
            {
                snprintf(spelling, sizeof(spelling), "%s_BitInt(%llu)", signed_value ? "" : "unsigned ", (unsigned long long)bits);
                name = spelling;
            }
        }
        else if (kind == BUSTER_TYPE_KIND_COMPLEX)
        {
            const uint32_t element = type_index(base);
            const char* element_name = element < BUSTER_TYPE_CODE_COMPLEX ? type_builtins[element].name : NULL;
            snprintf(spelling, sizeof(spelling), "_Complex %s", element_name != NULL ? element_name : "_BitInt");
            name = spelling;
            encoding = type_kind(table, base) == BUSTER_TYPE_KIND_FLOATING ? 3 : 0x80; // DW_ATE_complex_float / GNU complex integer
        }
        bits = index == BUSTER_TYPE_CODE_LONG_DOUBLE ? 80 : bits;
        name = name != NULL ? name : "<unsupported C type>";
        debug_uleb(info, CHECK_DEBUG_BASE);
        debug_string(info, (String8){ .pointer = (char*)name, .length = strlen(name) });
        debug_uleb(info, type_size(table, type));
        debug_integer(info, encoding, 1);
        debug_uleb(info, bits);
    }
}

// Each list describes precisely the sized entry spans belonging to a lexical interval. Coalesce adjacent spans,
// including zero-length source entries, but never span a hole such as a tail call's frame teardown. This is shared
// by lexical ranges and locations; their DWARF 5 base_addressx/offset_pair codes are equal.
static void check_debug_value_pair(DebugWriter* out, uint32_t begin, uint32_t end, const uint8_t* expression, uint32_t bytes)
{
    debug_integer(out, 4, 1); // DW_RLE/DW_LLE_offset_pair
    debug_uleb(out, begin);
    debug_uleb(out, end);
    if (expression != NULL)
    {
        debug_uleb(out, bytes);
        debug_bytes(out, expression, bytes);
    }
}

typedef struct CheckDebugRangeClip CheckDebugRangeClip;
struct CheckDebugRangeClip
{
    uint32_t begin;
    uint32_t end;
    bool list;
};

static void check_debug_value_pair_clipped(DebugWriter* out, uint32_t begin, uint32_t end, const uint8_t* expression, uint32_t bytes, CheckDebugRangeClip clip)
{
    begin = begin < clip.begin ? clip.begin : begin;
    end = end > clip.end ? clip.end : end;
    if (begin < end)
    {
        check_debug_value_pair(out, begin, end, expression, bytes);
    }
}

// The list by every span of the function, keeping those whose token lies in [first, last]: check_debug_value_range's
// twin and test oracle, and its path where the spans have no index.
static void check_debug_value_range_scan_clipped(const CheckDebugBody* source, DebugWriter* out, uint32_t body, uint32_t first, uint32_t last,
                                                const uint8_t* expression, uint32_t bytes, CheckDebugRangeClip clip)
{
    if (clip.list)
    {
        debug_integer(out, 1, 1); // DW_RLE/DW_LLE_base_addressx
        debug_uleb(out, body);
    }
    uint32_t begin = 0;
    uint32_t end = 0;
    const uint32_t count = source->span_count;
    for (uint32_t s = 0; s <= count; s += 1)
    {
        const CheckDebugValueSpan span = s < count ? source->spans[s] : (CheckDebugValueSpan){ 0 };
        const bool keep = s < count && span.token >= first && span.token <= last;
        if (begin < end && (!keep || span.begin != end))
        {
            check_debug_value_pair_clipped(out, begin, end, expression, bytes, clip);
            begin = end;
        }
        if (keep)
        {
            begin = begin == end ? span.begin : begin;
            end = span.end;
        }
    }
    if (clip.list)
    {
        debug_integer(out, 0, 1);
    }
}

static void check_debug_value_range_scan(const CheckDebugBody* source, DebugWriter* out, uint32_t body, uint32_t first, uint32_t last,
                                         const uint8_t* expression, uint32_t bytes)
{
    const CheckDebugRangeClip clip = { .end = UINT32_MAX, .list = true };
    check_debug_value_range_scan_clipped(source, out, body, first, last, expression, bytes, clip);
}

// The leaves of the token tree over `segments` runs: the next power of two at or past it.
static uint32_t check_debug_value_leaves(uint32_t segments)
{
    uint32_t leaves = 1;
    while (leaves < segments)
    {
        leaves *= 2;
    }
    return leaves;
}

// A function's spans' index (#1640): `breaks[i]`, the first span past i that doesn't begin where the one before it ends
// (or the count); `segments`, the starts of the maximal runs of non-decreasing tokens, then the count; and `tree`, the
// runs' smallest and largest tokens in an implicit binary tree of `leaves` leaves (its nodes' minima, then their maxima),
// so that a scope reaches only the runs its tokens overlap. Code runs in source order but around loops and branches placed
// apart, each a run of its own. Returns the leaves, and the runs in `segment_count`.
static uint32_t check_debug_value_index(const CheckDebugValueSpan* spans, uint32_t count, uint32_t* breaks, uint32_t* segments, uint32_t* tree,
                                        uint32_t* runs)
{
    uint32_t segment_count = 0;
    for (uint32_t i = count; i-- > 0;)
    {
        breaks[i] = i + 1 == count || spans[i + 1].begin != spans[i].end ? i + 1 : breaks[i + 1];
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (i == 0 || spans[i].token < spans[i - 1].token)
        {
            segments[segment_count++] = i;
        }
    }
    segments[segment_count] = count;
    *runs = segment_count;
    const uint32_t leaves = check_debug_value_leaves(segment_count);
    uint32_t* minima = tree;
    uint32_t* maxima = tree + 2 * (uint64_t)leaves;
    for (uint32_t leaf = 0; leaf < leaves; leaf += 1)
    {
        const bool run = leaf < segment_count;
        minima[leaves + leaf] = run ? spans[segments[leaf]].token : UINT32_MAX;
        maxima[leaves + leaf] = run ? spans[segments[leaf + 1] - 1].token : 0;
    }
    for (uint32_t node = leaves; node-- > 1;)
    {
        minima[node] = minima[2 * node] < minima[2 * node + 1] ? minima[2 * node] : minima[2 * node + 1];
        maxima[node] = maxima[2 * node] > maxima[2 * node + 1] ? maxima[2 * node] : maxima[2 * node + 1];
    }
    return leaves;
}

// A list's pair in progress (check_debug_value_range_scan's begin and end) and the index after its last kept span.
typedef struct CheckDebugValueWalk CheckDebugValueWalk;
struct CheckDebugValueWalk
{
    DebugWriter* out;
    const uint8_t* expression;
    uint32_t bytes;
    uint32_t begin;
    uint32_t end;
    uint32_t after;
    CheckDebugRangeClip clip;
};

// The kept spans [start, stop), in the scan's state machine: spans outside the scope before them end the pair, and so does
// each address break among them, which `breaks` reaches without visiting the spans between.
static void check_debug_value_walk(CheckDebugValueWalk* walk, const CheckDebugValueSpan* spans, const uint32_t* breaks, uint32_t start, uint32_t stop)
{
    // The token index can select one long monotonic run. Restrict that run by PC before walking its address gaps;
    // otherwise many small optimized-location ranges would each revisit every gap in the function.
    if (walk->clip.begin != 0)
    {
        uint32_t low = start;
        uint32_t high = stop;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = spans[middle].end <= walk->clip.begin;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        start = low;
    }
    if (walk->clip.end != UINT32_MAX)
    {
        uint32_t low = start;
        uint32_t high = stop;
        while (low < high)
        {
            const uint32_t middle = low + (high - low) / 2;
            const bool before = spans[middle].begin < walk->clip.end;
            low = before ? middle + 1 : low;
            high = before ? high : middle;
        }
        stop = low;
    }
    if (start != walk->after && walk->begin < walk->end)
    {
        check_debug_value_pair_clipped(walk->out, walk->begin, walk->end, walk->expression, walk->bytes, walk->clip);
        walk->begin = walk->end;
    }
    for (uint32_t i = start; i < stop;)
    {
        if (walk->begin < walk->end && spans[i].begin != walk->end)
        {
            check_debug_value_pair_clipped(walk->out, walk->begin, walk->end, walk->expression, walk->bytes, walk->clip);
            walk->begin = walk->end;
        }
        walk->begin = walk->begin == walk->end ? spans[i].begin : walk->begin;
        const uint32_t run = breaks[i] < stop ? breaks[i] : stop;
        walk->end = spans[run - 1].end;
        i = run;
    }
    walk->after = stop;
}

// check_debug_value_range_scan's list from the spans' index (#1640): the token tree gives the runs the scope's tokens
// overlap, in span order. A node whose runs' tokens all lie in the scope keeps all its spans, one range of indices taken
// whole; a leaf's run holds the scope's spans as one range, found by two binary searches. Each range is walked from address
// break to address break, in the scan's state machine (check_debug_value_walk), so the bytes are the scan's, in time for
// the tree's nodes the scope's bounds cut through and its pairs rather than for every span.
static void check_debug_value_range_indexed_clipped(const CheckDebugBody* source, const uint32_t* breaks, const uint32_t* segments, uint32_t segment_count,
                                            const uint32_t* tree, uint32_t leaves, DebugWriter* out, uint32_t body, uint32_t first, uint32_t last,
                                            const uint8_t* expression, uint32_t bytes, CheckDebugRangeClip clip)
{
    const CheckDebugValueSpan* spans = source->spans;
    const uint32_t* minima = tree;
    const uint32_t* maxima = tree + 2 * (uint64_t)leaves;
    if (clip.list)
    {
        debug_integer(out, 1, 1); // DW_RLE/DW_LLE_base_addressx
        debug_uleb(out, body);
    }
    CheckDebugValueWalk walk = { .out = out, .expression = expression, .bytes = bytes, .clip = clip };
    // The tree's nodes left to visit, left first: a node and its right sibling's at most at each of its 32 levels.
    uint32_t stack[66];
    uint32_t depth = source->span_count != 0 ? 1 : 0;
    stack[0] = 1;
    while (depth != 0)
    {
        const uint32_t node = stack[--depth];
        if (minima[node] > last || maxima[node] < first)
        {
            continue;
        }
        // The node's leaves: its level's share of them, from its place in the level.
        const uint32_t level = 31 - (uint32_t)__builtin_clz(node);
        const uint32_t width = leaves >> level;
        const uint32_t leaf = node * width - leaves;
        const uint32_t end_leaf = leaf + width < segment_count ? leaf + width : segment_count;
        if (leaf >= end_leaf || spans[segments[leaf]].begin >= clip.end || spans[segments[end_leaf] - 1].end <= clip.begin)
        {
            continue;
        }
        if (minima[node] >= first && maxima[node] <= last)
        {
            check_debug_value_walk(&walk, spans, breaks, segments[leaf], segments[end_leaf]);
        }
        else if (node < leaves)
        {
            stack[depth++] = 2 * node + 1;
            stack[depth++] = 2 * node;
        }
        else
        {
            uint32_t low = segments[leaf];
            uint32_t high = segments[leaf + 1];
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = spans[middle].token < first ? middle + 1 : low;
                high = spans[middle].token < first ? high : middle;
            }
            const uint32_t start = low;
            high = segments[leaf + 1];
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = spans[middle].token <= last ? middle + 1 : low;
                high = spans[middle].token <= last ? high : middle;
            }
            if (start < low)
            {
                check_debug_value_walk(&walk, spans, breaks, start, low);
            }
        }
    }
    if (walk.begin < walk.end)
    {
        check_debug_value_pair_clipped(out, walk.begin, walk.end, expression, bytes, clip);
    }
    if (clip.list)
    {
        debug_integer(out, 0, 1);
    }
}

static void check_debug_value_range_indexed(const CheckDebugBody* source, const uint32_t* breaks, const uint32_t* segments, uint32_t segment_count,
                                           const uint32_t* tree, uint32_t leaves, DebugWriter* out, uint32_t body, uint32_t first, uint32_t last,
                                           const uint8_t* expression, uint32_t bytes)
{
    const CheckDebugRangeClip clip = { .end = UINT32_MAX, .list = true };
    check_debug_value_range_indexed_clipped(source, breaks, segments, segment_count, tree, leaves, out, body, first, last, expression, bytes, clip);
}

static void check_debug_value_range_clipped(CheckDebugValues* values, uint32_t body, uint32_t first, uint32_t last, const uint8_t* expression,
                                           uint32_t bytes, CheckDebugRangeClip clip)
{
    const CheckDebugBody* source = values->rows->values + body;
    DebugWriter* out = expression != NULL ? &values->locations : &values->ranges;
    if (values->span_breaks != NULL && values->span_body != body)
    {
        values->span_body = body;
        values->span_leaves = check_debug_value_index(source->spans, source->span_count, values->span_breaks, values->span_segments, values->span_tree,
                                                      &values->span_segment_count);
    }
    if (values->span_breaks != NULL)
    {
        check_debug_value_range_indexed_clipped(source, values->span_breaks, values->span_segments, values->span_segment_count, values->span_tree, values->span_leaves,
                                                out, values->address_indices[body], first, last, expression, bytes, clip);
    }
    else
    {
        check_debug_value_range_scan_clipped(source, out, values->address_indices[body], first, last, expression, bytes, clip);
    }
}

static void check_debug_value_range(CheckDebugValues* values, uint32_t body, uint32_t first, uint32_t last, const uint8_t* expression, uint32_t bytes)
{
    const CheckDebugRangeClip clip = { .end = UINT32_MAX, .list = true };
    check_debug_value_range_clipped(values, body, first, last, expression, bytes, clip);
}

static uint32_t check_debug_home_expression(Tier0Home home, uint8_t class, bool indirect, uint8_t* bytes)
{
    static const uint8_t registers[] = { 0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };
    DebugWriter expression = { .bytes = bytes };
    if ((home.is_register || home.mode == BUSTER_X86_MEMORY_BASE) && home.reg < 16)
    {
        const uint32_t reg = home.is_register && tier0_class_floating(class) ? 17u + home.reg : registers[home.reg];
        const bool value = home.is_register && !indirect;
        debug_integer(&expression, value ? 0x90 : 0x92, 1); // DW_OP_regx / DW_OP_bregx
        debug_uleb(&expression, reg);
        if (!value)
        {
            debug_sleb(&expression, home.is_register ? 0 : home.displacement);
        }
        if (!home.is_register && indirect)
        {
            debug_integer(&expression, 0x06, 1); // DW_OP_deref: the home holds a VLA's address, not its bytes
        }
    }
    return (uint32_t)expression.size;
}

// Whether object declaration `token`'s object is defined in the unit: its symbol has an atom, its storage.
static bool check_debug_value_defined(const CheckDebugValues* values, uint32_t token)
{
    const CheckInput* input = values->program->input;
    const uint32_t object = input->objects[token];
    const SymbolId symbol = object < values->program->object_count ? values->program->object_symbols[object] : 0;
    const Link* link = values->rows->link;
    return link != NULL && symbol != 0 && symbol < link->symbols.count && link->symbols.atom[symbol] != 0 &&
           (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_OBJECT;
}

static void check_debug_value_variable(CheckDebugValues* values, uint32_t token, uint32_t body, uint32_t scope_end, bool parameter)
{
    const CheckInput* input = values->program->input;
    const TypeId type = input->declared_types[token];
    const uint32_t object = input->objects[token];
    const SymbolId symbol = object < values->program->object_count ? values->program->object_symbols[object] : 0;
    const bool defined = check_debug_value_defined(values, token);
    const bool local = body < values->program->body_count;
    const CheckDebugBody* source = local ? values->rows->values + body : NULL;
    const uint32_t id = local ? values->local_ids[token] : BUSTER_CHECK_NONE;
    uint8_t expression[16];
    const uint32_t bytes = source != NULL && id < source->local_count ?
                           check_debug_home_expression(source->homes[id], source->classes[id], source->bindings[id].indirect, expression) : 0;
    const bool ranged = source != NULL && id < source->local_count && source->location_first != NULL &&
                        source->location_first[id] < source->location_first[id + 1];
    const uint32_t abbreviation = defined ? CHECK_DEBUG_GLOBAL : bytes != 0 || ranged ? (parameter ? CHECK_DEBUG_PARAMETER : CHECK_DEBUG_LOCAL) :
                                  local ? (parameter ? CHECK_DEBUG_UNAVAILABLE_PARAMETER : CHECK_DEBUG_UNAVAILABLE_LOCAL) : CHECK_DEBUG_UNAVAILABLE_GLOBAL;
    debug_uleb(&values->info, abbreviation);
    debug_string(&values->info, check_debug_name(values, values->parser->tokens.names[token]));
    check_debug_type_reference(values, type);
    if (defined || !local)
    {
        debug_integer(&values->info, check_input_linkage(input->declaration_bits, token) == 2, 1);
    }
    if (defined)
    {
        debug_uleb(&values->info, 9);
        debug_integer(&values->info, 0x03, 1); // DW_OP_addr, relocated against this definition's section
        if (values->addresses != NULL)
        {
            values->addresses[values->address_count] = (NativeDebugAddress){ .offset = (uint32_t)values->info.size, .symbol = symbol };
        }
        values->address_count += 1;
        debug_integer(&values->info, 0, 8);
    }
    else if (ranged)
    {
        check_debug_value_field(values, BUSTER_NATIVE_DEBUG_LOCLISTS, values->locations.size);
        debug_integer(&values->locations, 1, 1); // DW_LLE_base_addressx
        debug_uleb(&values->locations, values->address_indices[body]);
        const uint32_t begin = source->location_first[id];
        const uint32_t end = source->location_first[id + 1];
        const CheckDebugLocation* locations = source->locations;
        const uint8_t class = source->classes[id];
        const bool indirect = source->bindings[id].indirect;
        for (uint32_t r = begin; r < end; r += 1)
        {
            const CheckDebugLocation location = locations[r];
            uint32_t size = 0;
            if (location.value_bytes != 0)
            {
                DebugWriter constant = { .bytes = expression };
                debug_integer(&constant, 0x9e, 1); // DW_OP_implicit_value: literal bytes, never an address
                debug_uleb(&constant, location.value_bytes);
                debug_integer(&constant, location.value, location.value_bytes);
                size = (uint32_t)constant.size;
            }
            else
            {
                size = check_debug_home_expression(location.home, class, indirect, expression);
            }
            if (size != 0)
            {
                if (parameter)
                {
                    // Incoming parameter provenance can be proved before the first source statement. Its lexical
                    // scope is the subprogram; the producer's machine ranges already exclude every unproven PC.
                    check_debug_value_pair(&values->locations, location.begin, location.end, expression, size);
                }
                else
                {
                    const CheckDebugRangeClip clip = { .begin = location.begin, .end = location.end };
                    check_debug_value_range_clipped(values, body, token, scope_end, expression, size, clip);
                }
            }
        }
        debug_integer(&values->locations, 0, 1);
    }
    else if (bytes != 0)
    {
        check_debug_value_field(values, BUSTER_NATIVE_DEBUG_LOCLISTS, values->locations.size);
        check_debug_value_range(values, body, parameter ? 0 : token, scope_end, expression, bytes);
    }
}

static void check_debug_value_typedef(CheckDebugValues* values, uint32_t token)
{
    debug_uleb(&values->info, CHECK_DEBUG_TYPEDEF);
    debug_string(&values->info, check_debug_name(values, values->parser->tokens.names[token]));
    check_debug_type_reference(values, values->program->input->declared_types[token]);
}

// Named parameters keep checker records even when they belong only to a function type. Only a definition's
// parameters acquire a body owner. The others neither name a variable nor root their source qualifiers in DWARF;
// their adjusted types remain reachable through the function signature.
static bool check_debug_value_declaration(const CheckInput* input, uint32_t token)
{
    return input->declarations[token] == token && (check_sparse_get(input->parameters, token) == BUSTER_CHECK_NONE ||
                                                  check_sparse_get(input->declaration_bodies, token) != BUSTER_CHECK_NONE);
}

// The end of instance `k`'s subtree among a function's instances (CheckDebugInline), which lists each instance's own after
// it (the inliner appends an expansion's callee's instances after its own): the first after it with no ancestor at k.
static uint32_t check_debug_inline_end(const CheckDebugBody* source, uint32_t k)
{
    uint32_t end = k + 1;
    bool inside = true;
    while (inside && end < source->inline_count)
    {
        uint32_t parent = source->inlines[end].parent;
        while (parent != BUSTER_CHECK_NONE && parent > k && parent < end)
        {
            parent = source->inlines[parent].parent;
        }
        inside = parent == k;
        end += inside ? 1 : 0;
    }
    return end;
}

// A span names its innermost instance. Extend the first and last span of each parent in reverse instance
// order: children follow their parent. A function span or another instance can still break the range inside
// these bounds; the list writer checks each span there. An instance can have code only in its children.
static void check_debug_inline_span_bounds(const CheckDebugBody* source, CheckDebugInlineSpans* bounds)
{
    for (uint32_t i = 0; i < source->inline_count; i += 1)
    {
        bounds[i] = (CheckDebugInlineSpans){ .first = source->span_count };
    }
    for (uint32_t s = 0; s < source->span_count; s += 1)
    {
        const uint32_t id = source->spans[s].inline_instance;
        if (id != 0 && id <= source->inline_count)
        {
            CheckDebugInlineSpans* instance = &bounds[id - 1];
            instance->first = s < instance->first ? s : instance->first;
            instance->end = s + 1;
        }
    }
    for (uint32_t i = source->inline_count; i != 0; i -= 1)
    {
        const uint32_t parent = source->inlines[i - 1].parent;
        if (parent < i - 1)
        {
            const CheckDebugInlineSpans child = bounds[i - 1];
            bounds[parent].first = child.first < bounds[parent].first ? child.first : bounds[parent].first;
            bounds[parent].end = child.end > bounds[parent].end ? child.end : bounds[parent].end;
        }
    }
}

// The range list of instance subtree [first, end) in body `source`, whose .debug_addr entry is `address`: its spans
// (CheckDebugValueSpan.inline_instance) with adjacent ones coalesced. A subtree without a pair writes no bytes.
static uint32_t check_debug_inline_ranges(const CheckDebugBody* source, DebugWriter* out, uint32_t address, uint32_t first, uint32_t end, CheckDebugInlineSpans spans)
{
    uint32_t pairs = 0;
    uint32_t begin = 0;
    uint32_t stop = 0;
    for (uint32_t s = spans.first; s <= spans.end; s += 1)
    {
        const CheckDebugValueSpan span = s < spans.end ? source->spans[s] : (CheckDebugValueSpan){ 0 };
        const bool keep = s < spans.end && span.inline_instance > first && span.inline_instance <= end;
        if (begin < stop && (!keep || span.begin != stop))
        {
            if (pairs == 0)
            {
                debug_integer(out, 1, 1); // DW_RLE_base_addressx
                debug_uleb(out, address);
            }
            check_debug_value_pair(out, begin, stop, NULL, 0);
            pairs += 1;
            begin = stop;
        }
        if (keep)
        {
            begin = begin == stop ? span.begin : begin;
            stop = span.end;
        }
    }
    if (pairs != 0)
    {
        debug_integer(out, 0, 1);
    }
    return pairs;
}

// Whether `token` is an automatic local an abstract instance lists (#2541): an object declared in a body, named, of a
// valid type, not a parameter (those its parameters list) and without a definition of its own (a static's).
static bool check_debug_abstract_local(const CheckDebugValues* values, uint32_t token)
{
    const CheckInput* input = values->program->input;
    return check_debug_value_declaration(input, token) && type_is_valid(values->program->types, input->declared_types[token]) && values->parser->tokens.names[token] != 0 &&
           input->declaration_kinds[token] == C_BINDING_OBJECT && check_sparse_get(input->parameters, token) == BUSTER_CHECK_NONE && !check_debug_value_defined(values, token);
}

// Instance `instance`'s callee's locals with proven locations in body `body`'s code (#2541): a DW_TAG_formal_parameter
// for a parameter, a DW_TAG_variable for an automatic local, whose abstract origin is its callee's abstract instance's
// and whose location list is its local's (CheckDebugInline.local: the callee's local j is local + j, its declaration
// the callee's own binding's token). Its callee's other parameters and locals its abstract instance gives, without a
// location.
static void check_debug_inline_parameters(CheckDebugValues* values, uint32_t body, CheckDebugInline instance)
{
    const CheckInput* input = values->program->input;
    const CheckDebugBody* source = values->rows->values + body;
    const bool expanded = instance.body < values->program->body_count && values->expanded[instance.body] != 0;
    const CheckDebugBody* callee = expanded ? values->rows->values + instance.body : NULL;
    const uint32_t opening = expanded ? values->program->bodies[instance.body].opening : BUSTER_CHECK_NONE;
    const uint32_t closing = expanded ? values->parser->tree.parents[opening] : 0;
    // Its parameters are its first locals, in their order; a local's token is one its abstract instance lists, in its body.
    uint32_t parameter = expanded ? check_sparse_get(input->parameters, opening) : BUSTER_CHECK_NONE;
    for (uint32_t j = 0; callee != NULL && j < instance.local_count && j < callee->local_count; j += 1)
    {
        const bool owned = parameter != BUSTER_CHECK_NONE;
        const uint32_t token = owned ? parameter : callee->bindings[j].token;
        const bool local = !owned && token > opening && token <= closing && token < input->token_count && check_debug_abstract_local(values, token);
        parameter = owned ? check_sparse_get(input->parameter_next, parameter) : BUSTER_CHECK_NONE;
        const uint32_t id = instance.local + j;
        const bool ranged = (owned || local) && id < source->local_count && source->location_first != NULL && source->location_first[id] < source->location_first[id + 1];
        if (ranged)
        {
            debug_uleb(&values->info, owned ? CHECK_DEBUG_INLINED_PARAMETER : CHECK_DEBUG_INLINED_VARIABLE);
            debug_integer(&values->info, values->parameter_dies[token], 4);
            check_debug_value_field(values, BUSTER_NATIVE_DEBUG_LOCLISTS, values->locations.size);
            debug_integer(&values->locations, 1, 1); // DW_LLE_base_addressx
            debug_uleb(&values->locations, values->address_indices[body]);
            for (uint32_t r = source->location_first[id]; r < source->location_first[id + 1]; r += 1)
            {
                const CheckDebugLocation location = source->locations[r];
                uint8_t expression[16];
                uint32_t size = 0;
                if (location.value_bytes != 0)
                {
                    DebugWriter constant = { .bytes = expression };
                    debug_integer(&constant, 0x9e, 1); // DW_OP_implicit_value: literal bytes, never an address
                    debug_uleb(&constant, location.value_bytes);
                    debug_integer(&constant, location.value, location.value_bytes);
                    size = (uint32_t)constant.size;
                }
                else
                {
                    size = check_debug_home_expression(location.home, source->classes[id], false, expression);
                }
                // The instance's own ranges scope it: the producer's machine ranges already exclude every unproven PC.
                if (size != 0)
                {
                    check_debug_value_pair(&values->locations, location.begin, location.end, expression, size);
                }
            }
            debug_integer(&values->locations, 0, 1);
        }
    }
}

// Body `body`'s described instance `k` and its subtree, nested as their expansions were (#2541 step 3): a
// DW_TAG_inlined_subroutine each, whose abstract origin is its callee's abstract instance, at its call's file and line,
// over its code and its nested instances' code. An instance without code, or undescribed, is left out with
// its subtree. Explicit stack, no recursion: the instances list each subtree in order.
static void check_debug_inline_write(CheckDebugValues* values, uint32_t body, uint32_t k)
{
    const CheckDebugBody* source = values->rows->values + body;
    const uint32_t end = check_debug_inline_end(source, k);
    uint32_t depth = 0;
    for (uint32_t j = k; j < end; j += 1)
    {
        const CheckDebugInline instance = source->inlines[j];
        while (depth != 0 && values->inline_open[depth - 1] != instance.parent)
        {
            debug_integer(&values->info, 0, 1);
            depth -= 1;
        }
        const bool placed = j == k || depth != 0;
        const uint32_t subtree = check_debug_inline_end(source, j);
        // A list without spans writes nothing. Save its start so the DIE can refer to the list after writing it.
        const uint64_t range_offset = values->ranges.size;
        const CheckDebugInlineSpans spans = values->inline_spans != NULL && source->inline_count > 1 ? values->inline_spans[j] : (CheckDebugInlineSpans){ .end = source->span_count };
        const bool spanned = placed && instance.described && check_debug_inline_ranges(source, &values->ranges, values->address_indices[body], j, subtree, spans) != 0;
        if (spanned)
        {
            debug_uleb(&values->info, CHECK_DEBUG_INLINED);
            debug_integer(&values->info, values->function_offsets[instance.body], 4);
            check_debug_value_field(values, DEBUG_SECTION_RNGLISTS, range_offset);
            debug_uleb(&values->info, instance.call_file);
            debug_uleb(&values->info, instance.call_line);
            check_debug_inline_parameters(values, body, instance);
            values->inline_open[depth++] = j;
        }
        else
        {
            j = subtree - 1;
        }
    }
    while (depth != 0)
    {
        debug_integer(&values->info, 0, 1);
        depth -= 1;
    }
}

static int check_debug_inline_compare(const void* x, const void* y)
{
    const uint64_t a = *(const uint64_t*)x;
    const uint64_t b = *(const uint64_t*)y;
    return a < b ? -1 : a > b ? 1 : 0;
}

#define CHECK_DEBUG_ROOT 1u
#define CHECK_DEBUG_USED 2u
#define CHECK_DEBUG_BODY 4u
#define CHECK_DEBUG_SCANNED 8u
// At a block's opening token, where its scope declares something of its own (check_debug_value_own_blocks, #2543).
#define CHECK_DEBUG_OWN 16u

// The unit's tokens the sweeps stop at (CheckDebugValues.events and root_ends), 16 tokens at a time: a token that
// declares itself or opens a block, a block, a for's clause or a condition's (the roles the body walk opens a scope at),
// and a token whose parent is the tree's root. Each list has room for every token and 16 more: each block's whole
// compressed register is stored and the next block's overwrites what lies past its count.
static void check_debug_value_events(CheckDebugValues* values)
{
    const uint32_t tokens = values->program->input->token_count;
    const uint32_t* declarations = values->program->input->declarations;
    const uint8_t* roles = values->parser->tree.roles;
    const uint32_t* parents = values->parser->tree.parents;
    const __m128i open_block = _mm_set1_epi8((char)PARSE_ROLE_OPEN_BLOCK);
    const __m128i open_for = _mm_set1_epi8((char)PARSE_ROLE_OPEN_FOR);
    const __m128i open_condition = _mm_set1_epi8((char)PARSE_ROLE_OPEN_CONDITION);
    const __m512i root = _mm512_set1_epi32((int)BUSTER_PARSE_ROOT);
    __m512i index = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t events = 0;
    uint32_t roots = 0;
    for (uint32_t t = 0; t < tokens; t += 16)
    {
        const uint32_t take = tokens - t < 16 ? tokens - t : 16;
        const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
        const __m128i role = _mm_maskz_loadu_epi8(valid, roles + t);
        const __mmask16 block = _mm_mask_cmpeq_epi8_mask(valid, role, open_block) | _mm_mask_cmpeq_epi8_mask(valid, role, open_for) |
                                _mm_mask_cmpeq_epi8_mask(valid, role, open_condition);
        const __mmask16 self = _mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, declarations + t), index);
        const __mmask16 last = _mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, parents + t), root);
        _mm512_storeu_si512(values->events + events, _mm512_maskz_compress_epi32(self | block, index));
        _mm512_storeu_si512(values->root_ends + roots, _mm512_maskz_compress_epi32(last, index));
        events += (uint32_t)__builtin_popcount(self | block);
        roots += (uint32_t)__builtin_popcount(last);
        index = _mm512_add_epi32(index, _mm512_set1_epi32(16));
    }
    values->event_count = events;
    values->root_count = roots;
}

// The place of the first of `count` ascending tokens `list` at or past `token` (`count` where none is).
static uint32_t check_debug_value_at_or_past(const uint32_t* list, uint32_t count, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (list[middle] < token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The unit's roots, as clang's DWARF has them (#2531): its file-scope declarations that define what the unit emits, a
// function's body or an object's storage (any declaration of a defined object), with everything inside them; and the
// file-scope typedefs whose names they bind (CheckInput.declarations), and those that such a typedef's declaration binds
// in turn. Only these give the unit's DWARF its types, typedefs and variables: a prototype, an extern declaration, a
// typedef or a tag that nothing defined in the unit names gives it nothing, as in clang's, which leaves out unused
// types. So a file-scope declaration that the unit's definitions don't reach changes no -g object, and attested
// declarations skip with -g (docs/attested-declarations.md): what the reach doesn't reach, no root binds.
// `starts` gives each declaration's last token its first; `pending` is room for a declaration a token.
static void check_debug_value_roots(CheckDebugValues* values, uint32_t* starts, uint32_t* pending)
{
    const CheckProgram* program = values->program;
    const CheckInput* input = program->input;
    const Parser* parser = values->parser;
    const uint32_t tokens = input->token_count;
    uint8_t* marks = values->marks;
    memset(marks, 0, (uint64_t)tokens + 1);
    // A root's last token is marked first where one of its tokens declares a body or a defined object: the first root
    // end at or past that token closes its declaration. (Only a declaration's own token, an event, defines an object.)
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const uint32_t declaration = program->bodies[b].declaration;
        const uint32_t root = declaration < tokens ? check_debug_value_at_or_past(values->root_ends, values->root_count, declaration) : values->root_count;
        if (root < values->root_count)
        {
            marks[values->root_ends[root]] |= CHECK_DEBUG_ROOT;
        }
    }
    uint32_t root = 0;
    for (uint32_t e = 0; e < values->event_count; e += 1)
    {
        const uint32_t t = values->events[e];
        if (check_debug_value_declaration(input, t) && input->declaration_kinds[t] == C_BINDING_OBJECT && check_debug_value_defined(values, t))
        {
            while (root < values->root_count && values->root_ends[root] < t)
            {
                root += 1;
            }
            if (root < values->root_count)
            {
                marks[values->root_ends[root]] |= CHECK_DEBUG_ROOT;
            }
        }
    }
    // Each file-scope declaration, its last token the tree's root (a declaration of a valid parse is a tree of its own):
    // all its tokens a root's where its last is marked. The roots then queue to be scanned for the typedefs they bind.
    uint32_t first = 0;
    uint32_t count = 0;
    for (uint32_t r = 0; r < values->root_count; r += 1)
    {
        const uint32_t last = values->root_ends[r];
        starts[last] = first;
        if ((marks[last] & CHECK_DEBUG_ROOT) != 0)
        {
            memset(marks + first, CHECK_DEBUG_ROOT, (uint64_t)last - first + 1);
            marks[last] |= CHECK_DEBUG_SCANNED;
            pending[count] = last;
            count += 1;
        }
        first = last + 1;
    }
    // The typedefs the roots bind, then those their declarations bind: each declaration scanned once, its identifiers
    // that name another token's declaration 16 at a time.
    const __m128i identifier = _mm_set1_epi8((char)PARSE_KIND_IDENTIFIER);
    while (count != 0)
    {
        count -= 1;
        const uint32_t last = pending[count];
        for (uint32_t at = starts[last]; at <= last; at += 16)
        {
            const uint32_t take = last - at + 1 < 16 ? last - at + 1 : 16;
            const __mmask16 valid = (__mmask16)_bzhi_u32(0xffff, take);
            const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int)at), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
            const __mmask16 named = _mm_mask_cmpeq_epi8_mask(valid, _mm_maskz_loadu_epi8(valid, parser->tokens.kinds + at), identifier);
            const __m512i declared = _mm512_maskz_loadu_epi32(named, input->declarations + at);
            const __mmask16 other = _mm512_mask_cmplt_epu32_mask(named, declared, _mm512_set1_epi32((int)tokens)) & _mm512_cmpneq_epi32_mask(declared, index);
            for (uint32_t rest = other; rest != 0; rest &= rest - 1)
            {
                const uint32_t declaration = input->declarations[at + (uint32_t)__builtin_ctz(rest)];
                if (input->declaration_kinds[declaration] == C_BINDING_TYPEDEF && (marks[declaration] & (CHECK_DEBUG_USED | CHECK_DEBUG_ROOT)) == 0)
                {
                    marks[declaration] |= CHECK_DEBUG_USED;
                    // The typedef's declaration, scanned in turn: its own last token is the first root end at or past it.
                    const uint32_t end_root = check_debug_value_at_or_past(values->root_ends, values->root_count, declaration);
                    const uint32_t end = end_root < values->root_count ? values->root_ends[end_root] : tokens;
                    if (end < tokens && (marks[end] & CHECK_DEBUG_SCANNED) == 0)
                    {
                        marks[end] |= CHECK_DEBUG_SCANNED;
                        pending[count] = end;
                        count += 1;
                    }
                }
            }
        }
    }
}

// The statement ends of one body's tokens, `opening` to `closing`, its closing brace their root (#2544). The whole
// unit's derivation (check_program_statement_ends) walked every file-scope token as well, which -g's blocks never read.
// The same bounded leaf queue, over the body alone: a body's statements are its own tokens' subtrees.
static bool check_debug_body_ends(CheckDebugValues* values, uint32_t opening, uint32_t closing)
{
    const uint32_t* parents = values->parser->tree.parents;
    uint32_t* ends = values->derived_ends;
    uint32_t* children = values->ends_children;
    uint32_t* queue = values->ends_queue;
    memset(&children[opening], 0, ((uint64_t)closing - opening + 1) * sizeof(uint32_t));
    for (uint32_t token = opening; token <= closing; token += 1)
    {
        ends[token] = token;
        const uint32_t parent = parents[token];
        const bool inner = parent >= opening && parent <= closing && parent != token;
        children[inner ? parent : token] += inner;
    }
    uint32_t length = 0;
    for (uint32_t token = opening; token <= closing; token += 1)
    {
        queue[length] = token;
        length += children[token] == 0;
    }
    for (uint32_t at = 0; at < length; at += 1)
    {
        const uint32_t token = queue[at];
        const uint32_t parent = parents[token];
        if (parent >= opening && parent <= closing && parent != token)
        {
            ends[parent] = ends[token] > ends[parent] ? ends[token] : ends[parent];
            children[parent] -= 1;
            queue[length] = parent;
            length += children[parent] == 0;
        }
    }
    const bool result = length == closing - opening + 1;
    return result;
}

// Whether `token` declares a local the walk writes a DIE of (an object's or a typedef's) in the innermost open scope.
static bool check_debug_value_scoped(const CheckDebugValues* values, uint32_t token)
{
    const CheckInput* input = values->program->input;
    const uint8_t kind = input->declaration_kinds[token];
    return check_debug_value_declaration(input, token) && type_is_valid(values->program->types, input->declared_types[token]) &&
           values->parser->tokens.names[token] != 0 && (kind == C_BINDING_OBJECT || kind == C_BINDING_TYPEDEF);
}

// CHECK_DEBUG_OWN at the opening token of each block of body `b` (`opening` to `closing`) whose scope declares an object
// or a typedef of its own (#2543): only such a block gets a DW_TAG_lexical_block, as clang's, whose LLVM drops a scope
// without children and gives the children of one that holds only scopes to its parent (DwarfCompileUnit). A block that
// declares nothing told a debugger nothing, and cost a DIE, a range list and, in an object, a relocation: sqlite3.c's
// 24,839 blocks are 2,139 (clang's 2,120). The walk check_debug_values_write takes, with its stack of the scopes' ends,
// once per body before the first of its two passes, so that both write the same blocks.
static void check_debug_value_own_blocks(CheckDebugValues* values, uint32_t b, uint32_t opening, uint32_t closing)
{
    const CheckInput* input = values->program->input;
    const Parser* parser = values->parser;
    uint32_t depth = 0;
    // The body's events alone: a scope that ended before one closes first, as it would have at its end's token.
    for (uint32_t e = check_debug_value_at_or_past(values->events, values->event_count, opening + 1); e < values->event_count && values->events[e] <= closing; e += 1)
    {
        const uint32_t token = values->events[e];
        while (depth != 0 && values->scopes[depth - 1] < token)
        {
            depth -= 1;
        }
        const uint32_t role = parser->tree.roles[token];
        const bool block = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION;
        if (block && values->ends == NULL)
        {
            values->ends_ready[b] = values->ends_ready[b] != 0 || check_debug_body_ends(values, opening, closing);
            values->ends_failed = values->ends_failed || values->ends_ready[b] == 0;
        }
        if (block)
        {
            const uint32_t parent = parser->tree.parents[token];
            const uint32_t* ends = values->ends != NULL ? values->ends : values->derived_ends;
            values->scopes[depth] = parent < input->token_count ? ends[parent] : input->token_count;
            values->scope_tokens[depth] = token;
            values->marks[token] &= (uint8_t)~CHECK_DEBUG_OWN;
            depth += 1;
        }
        if (depth != 0 && check_debug_value_scoped(values, token))
        {
            values->marks[values->scope_tokens[depth - 1]] |= CHECK_DEBUG_OWN;
        }
        while (depth != 0 && values->scopes[depth - 1] <= token)
        {
            depth -= 1;
        }
    }
}

static void check_debug_values_write(CheckDebugValues* values)
{
    const CheckProgram* program = values->program;
    const CheckInput* input = program->input;
    const Parser* parser = values->parser;
    const CheckDebug* original = values->original;
    const uint32_t prefix = original->output.fields[DEBUG_SECTION_RNGLISTS] + 4;
    debug_bytes(&values->info, original->output.sections[DEBUG_SECTION_INFO].bytes, prefix);
    debug_bytes(&values->ranges, original->output.sections[DEBUG_SECTION_RNGLISTS].bytes, original->output.sections[DEBUG_SECTION_RNGLISTS].size);
    debug_integer(&values->locations, 0, 4);
    debug_integer(&values->locations, 5, 2);
    debug_integer(&values->locations, 8, 1);
    debug_integer(&values->locations, 0, 1);
    debug_integer(&values->locations, 0, 4); // no offset table: DW_FORM_sec_offset refers directly to each list
    for (uint32_t t = 0; t < values->type_count; t += 1)
    {
        check_debug_value_type(values, values->types[t]);
    }
    for (uint32_t e = 0; e < values->event_count; e += 1)
    {
        const uint32_t token = values->events[e];
        const bool declaration = check_debug_value_declaration(input, token) && type_is_valid(program->types, input->declared_types[token]);
        const bool file = declaration && check_sparse_get(input->declaration_bodies, token) == BUSTER_CHECK_NONE;
        // (A file-scope typedef that a root names, and an object the unit defines: check_debug_value_roots.)
        if (file && input->declaration_kinds[token] == C_BINDING_TYPEDEF && (values->marks[token] & (CHECK_DEBUG_USED | CHECK_DEBUG_ROOT)) != 0)
        {
            check_debug_value_typedef(values, token);
        }
        else if (file && input->declaration_kinds[token] == C_BINDING_OBJECT && input->objects[token] < program->object_count &&
                 program->object_declarations[input->objects[token]] == token && parser->tokens.names[token] != 0 &&
                 check_debug_value_defined(values, token))
        {
            check_debug_value_variable(values, token, program->body_count, input->token_count, false);
        }
    }
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const DebugFunction function = original->bodies[b];
        const uint32_t closing = parser->tree.parents[body.opening];
        const TypeId type = input->declared_types[body.declaration];
        // The abstract instance its instances refer to (#2541 step 3): its name, type, line and parameters, no code.
        if (values->expanded[b] != 0)
        {
            values->function_offsets[b] = (uint32_t)values->info.size;
            debug_uleb(&values->info, CHECK_DEBUG_ABSTRACT);
            debug_string(&values->info, function.name);
            check_debug_type_reference(values, type_return(program->types, type));
            debug_uleb(&values->info, function.file);
            debug_uleb(&values->info, function.line);
            debug_integer(&values->info, 1, 1); // DW_INL_inlined
            for (uint32_t p = check_sparse_get(input->parameters, body.opening); p != BUSTER_CHECK_NONE; p = check_sparse_get(input->parameter_next, p))
            {
                values->parameter_dies[p] = (uint32_t)values->info.size;
                debug_uleb(&values->info, CHECK_DEBUG_UNAVAILABLE_PARAMETER);
                debug_string(&values->info, check_debug_name(values, parser->tokens.names[p]));
                check_debug_type_reference(values, input->declared_types[p]);
            }
            // Its automatic locals, without a location (each instance's has its own, #2541), so that a debugger in an
            // instance's frame names them; flat, as gdb takes a lexical block without addresses.
            const uint32_t end = parser->tree.parents[body.opening];
            for (uint32_t e = check_debug_value_at_or_past(values->events, values->event_count, body.opening + 1); e < values->event_count && values->events[e] <= end; e += 1)
            {
                const uint32_t token = values->events[e];
                if (check_debug_abstract_local(values, token))
                {
                    values->parameter_dies[token] = (uint32_t)values->info.size;
                    debug_uleb(&values->info, CHECK_DEBUG_UNAVAILABLE_LOCAL);
                    debug_string(&values->info, check_debug_name(values, parser->tokens.names[token]));
                    check_debug_type_reference(values, input->declared_types[token]);
                }
            }
            debug_integer(&values->info, 0, 1); // abstract instance children
        }
        // Its own DW_TAG_subprogram and what it holds, but for a dead body (CheckProgramRows.dead), which has no code.
        if (values->address_indices[b] != BUSTER_CHECK_NONE)
        {
            // Its top-level described instances (#2541 step 3) by call token, written where the walk reaches their call,
            // in the scope that holds it; those whose call is outside the body's tokens after them.
            const CheckDebugBody* source = values->rows->values + b;
            uint32_t ordered = 0;
            for (uint32_t k = 0; k < source->inline_count; k += 1)
            {
                if (source->inlines[k].parent == BUSTER_CHECK_NONE && source->inlines[k].described)
                {
                    values->inline_order[ordered++] = (uint64_t)source->inlines[k].call_token << 32 | k;
                }
            }
            if (ordered != 0 && source->inline_count > 1 && values->inline_spans != NULL)
            {
                check_debug_inline_span_bounds(source, values->inline_spans);
            }
            qsort(values->inline_order, ordered, sizeof(uint64_t), check_debug_inline_compare);
            uint32_t next_inline = 0;
            while (next_inline < ordered && (uint32_t)(values->inline_order[next_inline] >> 32) <= body.opening)
            {
                next_inline += 1;
            }
            const uint32_t first_inline = next_inline;
            debug_uleb(&values->info, CHECK_DEBUG_FUNCTION);
            debug_string(&values->info, function.name);
            check_debug_type_reference(values, type_return(program->types, type));
            debug_uleb(&values->info, values->address_indices[b]);
            debug_integer(&values->info, function.size, 4);
            debug_uleb(&values->info, function.file);
            debug_uleb(&values->info, function.line);
            for (uint32_t p = check_sparse_get(input->parameters, body.opening); p != BUSTER_CHECK_NONE; p = check_sparse_get(input->parameter_next, p))
            {
                check_debug_value_variable(values, p, b, closing, true);
            }
            if (!values->blocks_marked)
            {
                check_debug_value_own_blocks(values, b, body.opening, closing);
            }
            uint32_t depth = 0;
            // The body's events and its instances' calls, in token order; nothing else in it opens, declares or calls.
            // A scope that ended before the next of them closes first, as it would have at its end's token.
            uint32_t e = check_debug_value_at_or_past(values->events, values->event_count, body.opening + 1);
            while ((e < values->event_count && values->events[e] <= closing) ||
                   (next_inline < ordered && (uint32_t)(values->inline_order[next_inline] >> 32) <= closing))
            {
                const uint32_t event = e < values->event_count && values->events[e] <= closing ? values->events[e] : UINT32_MAX;
                const uint32_t call = next_inline < ordered ? (uint32_t)(values->inline_order[next_inline] >> 32) : UINT32_MAX;
                const uint32_t token = event < call ? event : call;
                e += event == token ? 1 : 0;
                while (depth != 0 && values->scopes[depth - 1] < token)
                {
                    if (values->scope_tokens[depth - 1] != BUSTER_CHECK_NONE)
                    {
                        debug_integer(&values->info, 0, 1);
                    }
                    depth -= 1;
                }
                const uint32_t role = parser->tree.roles[token];
                const bool block = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION;
                if (block && values->ends == NULL)
                {
                    values->ends_ready[b] = values->ends_ready[b] != 0 || check_debug_body_ends(values, body.opening, closing);
                    values->ends_failed = values->ends_failed || values->ends_ready[b] == 0;
                }
                if (block)
                {
                    const uint32_t parent = parser->tree.parents[token];
                    const uint32_t* ends = values->ends != NULL ? values->ends : values->derived_ends;
                    const uint32_t end = parent < input->token_count ? ends[parent] : input->token_count;
                    // (Its own declarations' block alone, #2543: a block's without puts its children in its parent.)
                    const bool own = (values->marks[token] & CHECK_DEBUG_OWN) != 0;
                    if (own)
                    {
                        debug_uleb(&values->info, CHECK_DEBUG_BLOCK);
                        check_debug_value_field(values, DEBUG_SECTION_RNGLISTS, values->ranges.size);
                        check_debug_value_range(values, b, token, end, NULL, 0);
                    }
                    values->scopes[depth] = end;
                    values->scope_tokens[depth] = own ? token : BUSTER_CHECK_NONE;
                    depth += 1;
                }
                // (check_debug_value_scoped's test, which marks the blocks.)
                if (check_debug_value_declaration(input, token) && type_is_valid(program->types, input->declared_types[token]) && parser->tokens.names[token] != 0)
                {
                    if (input->declaration_kinds[token] == C_BINDING_OBJECT)
                    {
                        check_debug_value_variable(values, token, b, depth != 0 ? values->scopes[depth - 1] : closing, false);
                    }
                    else if (input->declaration_kinds[token] == C_BINDING_TYPEDEF)
                    {
                        check_debug_value_typedef(values, token);
                    }
                }
                while (next_inline < ordered && (uint32_t)(values->inline_order[next_inline] >> 32) == token)
                {
                    check_debug_inline_write(values, b, (uint32_t)values->inline_order[next_inline]);
                    next_inline += 1;
                }
                while (depth != 0 && values->scopes[depth - 1] <= token)
                {
                    if (values->scope_tokens[depth - 1] != BUSTER_CHECK_NONE)
                    {
                        debug_integer(&values->info, 0, 1);
                    }
                    depth -= 1;
                }
            }
            // The scopes that end at the closing brace, as its token closed them.
            while (depth != 0 && values->scopes[depth - 1] <= closing)
            {
                if (values->scope_tokens[depth - 1] != BUSTER_CHECK_NONE)
                {
                    debug_integer(&values->info, 0, 1);
                }
                depth -= 1;
            }
            // The instances whose call is outside the body's tokens (before its opening brace, or past its closing one).
            for (uint32_t k = 0; k < ordered; k += 1)
            {
                if (k < first_inline || k >= next_inline)
                {
                    check_debug_inline_write(values, b, (uint32_t)values->inline_order[k]);
                }
            }
            debug_integer(&values->info, 0, 1); // subprogram children
        }
    }
    debug_integer(&values->info, 0, 1); // compilation unit children
    values->blocks_marked = true;
    debug_length(&values->info, 0, values->info.size - 4);
    debug_length(&values->ranges, 0, values->ranges.size - 4);
    debug_length(&values->locations, 0, values->locations.size - 4);
}

static bool check_debug_values(CheckDebug* debug, const CheckProgram* program, const CheckProgramRows* rows, const Parser* parser, Arena* arena, Arena* scratch)
{
    const CheckInput* input = program->input;
    const TypeTable* table = program->types;
    const uint64_t room = (uint64_t)table->types.count << BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t tokens = input->token_count;
    CheckDebugValues values = { .program = program, .rows = rows, .parser = parser, .original = debug };
    values.type_offsets = room <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, room) : NULL;
    values.types = room <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, TypeId, room) : NULL;
    values.enum_definitions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, table->types.count);
    values.enum_owners = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->input->enum_value_count + 1);
    values.local_ids = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    values.scopes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    values.scope_tokens = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    values.marks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)tokens + 1);
    values.events = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 16);
    values.root_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 16);
    // Each enum definition's opening token and index, in token order (check_debug_values' sweep of the enumerators).
    uint64_t* enum_opens = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)program->input->enum_definition_count + 1);
    // The spans' index (#1640), room for the largest function's; without it, each list scans its function's spans.
    uint32_t most_spans = 0;
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        most_spans = rows->values[b].span_count > most_spans ? rows->values[b].span_count : most_spans;
    }
    values.span_breaks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)most_spans + 1);
    values.span_segments = values.span_breaks != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)most_spans + 1) : NULL;
    values.span_tree = values.span_segments != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 4 * (uint64_t)check_debug_value_leaves(most_spans + 1)) : NULL;
    values.span_breaks = values.span_tree != NULL ? values.span_breaks : NULL;
    values.span_body = BUSTER_CHECK_NONE;
    uint32_t most_inlines = 0;
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        most_inlines = rows->values[b].inline_count > most_inlines ? rows->values[b].inline_count : most_inlines;
    }
    values.function_offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->body_count + 1);
    values.expanded = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->body_count + 1);
    values.parameter_dies = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    values.address_indices = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->body_count + 1);
    values.inline_open = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)most_inlines + 1);
    values.inline_order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)most_inlines + 1);
    // The driver uses tree_only parsing: the checker's derived traversal was scratch-owned and is gone now. The blocks'
    // statement ends are derived by body where a block needs them (check_debug_body_ends, #2544), not for the whole unit.
    values.ends = parser->ends;
    const bool derive = values.ends == NULL;
    values.derived_ends = derive ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1) : NULL;
    values.ends_children = derive ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1) : NULL;
    values.ends_queue = derive ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1) : NULL;
    values.ends_ready = derive ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->body_count + 1) : NULL;
    const bool ends_valid = !derive || (values.derived_ends != NULL && values.ends_children != NULL && values.ends_queue != NULL && values.ends_ready != NULL);
    bool valid = values.type_offsets != NULL && values.types != NULL && values.enum_definitions != NULL && values.enum_owners != NULL && values.local_ids != NULL &&
                 values.scopes != NULL && values.scope_tokens != NULL && values.marks != NULL && ends_valid && values.function_offsets != NULL && values.expanded != NULL && values.address_indices != NULL &&
                 values.parameter_dies != NULL && values.inline_open != NULL && values.inline_order != NULL && values.events != NULL && values.root_ends != NULL &&
                 enum_opens != NULL;
    if (valid)
    {
        // A lone instance visits the spans only once. Without room for the optional index, the same writer
        // starts at the first span of the function and ends at the last.
        values.inline_spans = most_inlines > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDebugInlineSpans, most_inlines) : NULL;
        memset(values.type_offsets, 0, room * sizeof(uint32_t));
        memset(values.enum_definitions, 0, (uint64_t)table->types.count * sizeof(uint32_t));
        check_debug_value_events(&values);
        // (The roots' scan takes the local ids and the scopes for its starts and its worklist, before either is used.)
        check_debug_value_roots(&values, values.local_ids, values.scopes);
        memset(values.local_ids, 0xff, ((uint64_t)tokens + 1) * sizeof(uint32_t));
        memset(values.expanded, 0, (uint64_t)program->body_count + 1);
        if (derive)
        {
            memset(values.ends_ready, 0, (uint64_t)program->body_count + 1);
        }
        uint32_t address = 0;
        for (uint32_t b = 0; b < program->body_count; b += 1)
        {
            const bool code = rows->dead == NULL || rows->dead[b] == 0;
            values.address_indices[b] = code ? address : BUSTER_CHECK_NONE;
            address += code ? 1 : 0;
        }
        for (uint32_t b = 0; b < program->body_count; b += 1)
        {
            const uint32_t instances = rows->dead == NULL || rows->dead[b] == 0 ? rows->values[b].inline_count : 0;
            for (uint32_t k = 0; k < instances; k += 1)
            {
                const CheckDebugInline instance = rows->values[b].inlines[k];
                values.expanded[instance.body < program->body_count ? instance.body : program->body_count] |= instance.described;
            }
        }
        for (uint32_t event = 0; event < values.event_count; event += 1)
        {
            const uint32_t t = values.events[event];
            if (check_debug_value_declaration(input, t) && (values.marks[t] & (CHECK_DEBUG_ROOT | CHECK_DEBUG_USED)) != 0)
            {
                check_debug_type_add(&values, input->declared_types[t]);
            }
        }
        const uint32_t definitions = program->input->enum_definition_count;
        bool ordered = true;
        for (uint32_t e = 0; e < definitions; e += 1)
        {
            const CheckInputEnumDefinition definition = program->input->enum_definitions[e];
            values.enum_definitions[type_index(definition.type)] = e + 1;
            enum_opens[e] = (uint64_t)definition.open << 32 | e;
            ordered = ordered && (e == 0 || enum_opens[e - 1] < enum_opens[e]);
        }
        if (!ordered)
        {
            qsort(enum_opens, definitions, sizeof(uint64_t), check_debug_inline_compare);
        }
        // Sweep the definitions' opening braces and the enumerators in source order: the innermost open definition owns
        // each enumerator. As a sweep of every token, an enumerator whose token isn't past the one before it (or isn't
        // a token) is never reached, and neither is any after it.
        uint32_t depth = 0;
        uint32_t e = 0;
        uint32_t o = 0;
        uint32_t assigned = 0;
        bool enumerators = true;
        bool more = true;
        while (more)
        {
            const uint32_t value_token = e < program->input->enum_value_count ? program->input->enum_values[e].token : UINT32_MAX;
            enumerators = enumerators && e < program->input->enum_value_count && (e == 0 || value_token > assigned) && value_token < tokens;
            const uint32_t value = enumerators ? value_token : UINT32_MAX;
            const uint32_t open = o < definitions ? (uint32_t)(enum_opens[o] >> 32) : UINT32_MAX;
            const uint32_t token = open < value ? open : value;
            more = token < tokens;
            while (more && depth != 0 && program->input->enum_definitions[values.scopes[depth - 1]].close <= token)
            {
                depth -= 1;
            }
            if (more && open == token)
            {
                values.scopes[depth++] = (uint32_t)enum_opens[o];
                o += 1;
            }
            if (more && value == token)
            {
                values.enum_owners[e++] = depth != 0 ? values.scopes[depth - 1] + 1 : 0;
                assigned = token;
            }
        }
        // This bound grows: visiting a type can append its pointee, parameters, members and qualifier base.
        for (uint32_t t = 0; t < values.type_count; t += 1)
        {
            const TypeId type = values.types[t];
            const uint32_t index = type_index(type);
            const uint32_t qualifiers = type_qualifiers(type);
            const uint32_t kind = table->types.kind[index];
            if (qualifiers != 0)
            {
                check_debug_type_add(&values, type & ~(qualifiers & (0u - qualifiers)));
            }
            else
            {
                check_debug_type_add(&values, table->types.base[index]);
                if (kind == BUSTER_TYPE_KIND_FUNCTION)
                {
                    const uint32_t first = table->types.index[index];
                    const uint64_t count = table->types.extent[index];
                    for (uint64_t p = 0; p < count; p += 1)
                    {
                        check_debug_type_add(&values, table->parameters.type[first + p]);
                    }
                }
                else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                {
                    const uint32_t tag = table->types.index[index];
                    const uint32_t first = table->tags.member_begin[tag];
                    const uint32_t count = table->tags.member_count[tag];
                    for (uint32_t m = first; m < first + count; m += 1)
                    {
                        check_debug_type_add(&values, table->members.type[m]);
                    }
                }
            }
        }
        for (uint32_t b = 0; b < program->body_count; b += 1)
        {
            const CheckDebugBody* source = rows->values + b;
            for (uint32_t l = 0; l < source->local_count; l += 1)
            {
                const uint32_t token = source->bindings[l].token;
                if (token < tokens)
                {
                    values.local_ids[token] = l;
                }
            }
        }
        check_debug_values_write(&values);
        valid = !values.ends_failed && values.info.size < BUSTER_DEBUG_DWARF32_LIMIT && values.ranges.size < BUSTER_DEBUG_DWARF32_LIMIT && values.locations.size < BUSTER_DEBUG_DWARF32_LIMIT;
    }
    const uint64_t info_size = values.info.size;
    const uint64_t range_size = values.ranges.size;
    const uint64_t location_size = values.locations.size;
    const uint32_t fields = values.field_count;
    const uint32_t addresses = values.address_count;
    uint8_t* info = valid ? arena_push(arena, info_size, 1) : NULL;
    uint8_t* ranges = valid ? arena_push(arena, range_size, 1) : NULL;
    uint8_t* locations = valid ? arena_push(arena, location_size, 1) : NULL;
    const DebugSection old_abbrev = debug->output.sections[DEBUG_SECTION_ABBREV];
    const uint64_t abbrev_size = old_abbrev.size - 1 + sizeof(check_debug_value_abbreviations);
    uint8_t* abbreviations = valid ? arena_push(arena, abbrev_size, 1) : NULL;
    values.fields = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, NativeDebugField, (uint64_t)fields + 1) : NULL;
    values.addresses = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, NativeDebugAddress, (uint64_t)addresses + 1) : NULL;
    valid = valid && info != NULL && ranges != NULL && locations != NULL && abbreviations != NULL && values.fields != NULL && values.addresses != NULL;
    if (valid)
    {
        memcpy(abbreviations, old_abbrev.bytes, old_abbrev.size - 1);
        memcpy(abbreviations + old_abbrev.size - 1, check_debug_value_abbreviations, sizeof(check_debug_value_abbreviations));
        values.info = (DebugWriter){ .bytes = info };
        values.ranges = (DebugWriter){ .bytes = ranges };
        values.locations = (DebugWriter){ .bytes = locations };
        values.field_count = 0;
        values.address_count = 0;
        check_debug_values_write(&values);
        valid = values.info.size == info_size && values.ranges.size == range_size && values.locations.size == location_size && values.field_count == fields && values.address_count == addresses;
    }
    if (valid)
    {
        debug->output.sections[DEBUG_SECTION_INFO] = (DebugSection){ .bytes = info, .capacity = info_size, .size = info_size };
        debug->output.sections[DEBUG_SECTION_ABBREV] = (DebugSection){ .bytes = abbreviations, .capacity = abbrev_size, .size = abbrev_size };
        debug->output.sections[DEBUG_SECTION_RNGLISTS] = (DebugSection){ .bytes = ranges, .capacity = range_size, .size = range_size };
        for (uint32_t s = 0; s < BUSTER_DEBUG_SECTIONS; s += 1)
        {
            const DebugSection section = debug->output.sections[s];
            debug->object.sections[s] = (String8){ .pointer = (char*)section.bytes, .length = section.size };
            debug->attach.sections[s] = debug->object.sections[s];
        }
        debug->object.sections[BUSTER_NATIVE_DEBUG_LOCLISTS] = (String8){ .pointer = (char*)locations, .length = location_size };
        debug->attach.sections[BUSTER_NATIVE_DEBUG_LOCLISTS] = debug->object.sections[BUSTER_NATIVE_DEBUG_LOCLISTS];
        debug->object.extra = (NativeDebugExtra){ .fields = values.fields, .addresses = values.addresses, .field_count = fields, .address_count = addresses };
        debug->attach.extra = debug->object.extra;
    }
    return valid;
}
