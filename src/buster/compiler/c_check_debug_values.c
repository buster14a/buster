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
    0,
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
    uint32_t* local_ids;
    uint32_t* scopes;
    DebugWriter info;
    DebugWriter ranges;
    DebugWriter locations;
    NativeDebugField* fields;
    NativeDebugAddress* addresses;
    uint32_t field_count;
    uint32_t address_count;
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
    else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_ALIGNED)
    {
        debug_uleb(info, kind == BUSTER_TYPE_KIND_POINTER ? CHECK_DEBUG_POINTER : kind == BUSTER_TYPE_KIND_ATOMIC ? CHECK_DEBUG_ATOMIC : CHECK_DEBUG_TYPEDEF);
        if (kind == BUSTER_TYPE_KIND_POINTER)
        {
            debug_integer(info, 8, 1);
        }
        else if (kind == BUSTER_TYPE_KIND_ALIGNED)
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
static void check_debug_value_range(CheckDebugValues* values, uint32_t body, uint32_t first, uint32_t last, const uint8_t* expression, uint32_t bytes)
{
    const CheckDebugBody* source = values->rows->values + body;
    DebugWriter* out = expression != NULL ? &values->locations : &values->ranges;
    debug_integer(out, 1, 1); // DW_RLE/DW_LLE_base_addressx
    debug_uleb(out, body);
    uint32_t begin = 0;
    uint32_t end = 0;
    const uint32_t count = source->span_count;
    for (uint32_t s = 0; s <= count; s += 1)
    {
        const CheckDebugValueSpan span = s < count ? source->spans[s] : (CheckDebugValueSpan){ 0 };
        const bool keep = s < count && span.token >= first && span.token <= last;
        if (begin < end && (!keep || span.begin != end))
        {
            debug_integer(out, 4, 1); // DW_RLE/DW_LLE_offset_pair
            debug_uleb(out, begin);
            debug_uleb(out, end);
            if (expression != NULL)
            {
                debug_uleb(out, bytes);
                debug_bytes(out, expression, bytes);
            }
            begin = end;
        }
        if (keep)
        {
            begin = begin == end ? span.begin : begin;
            end = span.end;
        }
    }
    debug_integer(out, 0, 1);
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

static void check_debug_value_variable(CheckDebugValues* values, uint32_t token, uint32_t body, uint32_t scope_end, bool parameter)
{
    const CheckInput* input = values->program->input;
    const TypeId type = input->declared_types[token];
    const uint32_t object = input->objects[token];
    const SymbolId symbol = object < values->program->object_count ? values->program->object_symbols[object] : 0;
    const Link* link = values->rows->link;
    const bool defined = link != NULL && symbol != 0 && symbol < link->symbols.count && link->symbols.atom[symbol] != 0 &&
                         (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_OBJECT;
    const bool local = body < values->program->body_count;
    const CheckDebugBody* source = local ? values->rows->values + body : NULL;
    const uint32_t id = local ? values->local_ids[token] : BUSTER_CHECK_NONE;
    uint8_t expression[16];
    const uint32_t bytes = source != NULL && id < source->local_count ?
                           check_debug_home_expression(source->homes[id], source->classes[id], source->bindings[id].indirect, expression) : 0;
    const uint32_t abbreviation = defined ? CHECK_DEBUG_GLOBAL : bytes != 0 ? (parameter ? CHECK_DEBUG_PARAMETER : CHECK_DEBUG_LOCAL) :
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
    for (uint32_t token = 0; token < input->token_count; token += 1)
    {
        const bool declaration = input->declarations[token] == token && type_is_valid(program->types, input->declared_types[token]);
        const bool file = declaration && check_sparse_get(input->declaration_bodies, token) == BUSTER_CHECK_NONE;
        if (file && input->declaration_kinds[token] == C_BINDING_TYPEDEF)
        {
            check_debug_value_typedef(values, token);
        }
        else if (file && input->declaration_kinds[token] == C_BINDING_OBJECT && input->objects[token] < program->object_count &&
                 program->object_declarations[input->objects[token]] == token && parser->tokens.names[token] != 0)
        {
            check_debug_value_variable(values, token, program->body_count, input->token_count, false);
        }
    }
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const DebugFunction function = original->unit.functions[b];
        const uint32_t closing = parser->tree.parents[body.opening];
        const TypeId type = input->declared_types[body.declaration];
        debug_uleb(&values->info, CHECK_DEBUG_FUNCTION);
        debug_string(&values->info, function.name);
        check_debug_type_reference(values, type_return(program->types, type));
        debug_uleb(&values->info, b);
        debug_integer(&values->info, function.size, 4);
        debug_uleb(&values->info, function.file);
        debug_uleb(&values->info, function.line);
        for (uint32_t p = check_sparse_get(input->parameters, body.opening); p != BUSTER_CHECK_NONE; p = check_sparse_get(input->parameter_next, p))
        {
            check_debug_value_variable(values, p, b, closing, true);
        }
        uint32_t depth = 0;
        for (uint32_t token = body.opening + 1; token <= closing; token += 1)
        {
            const uint32_t role = parser->tree.roles[token];
            const bool block = role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_OPEN_FOR || role == PARSE_ROLE_OPEN_CONDITION;
            if (block)
            {
                const uint32_t end = parser->ends[parser->tree.parents[token]];
                debug_uleb(&values->info, CHECK_DEBUG_BLOCK);
                check_debug_value_field(values, DEBUG_SECTION_RNGLISTS, values->ranges.size);
                check_debug_value_range(values, b, token, end, NULL, 0);
                values->scopes[depth++] = end;
            }
            if (input->declarations[token] == token && type_is_valid(program->types, input->declared_types[token]) && parser->tokens.names[token] != 0)
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
            while (depth != 0 && values->scopes[depth - 1] <= token)
            {
                debug_integer(&values->info, 0, 1);
                depth -= 1;
            }
        }
        debug_integer(&values->info, 0, 1); // subprogram children
    }
    debug_integer(&values->info, 0, 1); // compilation unit children
    debug_length(&values->info, 0, values->info.size - 4);
    debug_length(&values->ranges, 0, values->ranges.size - 4);
    debug_length(&values->locations, 0, values->locations.size - 4);
}

static bool check_debug_values(CheckDebug* debug, const CheckProgram* program, const CheckProgramRows* rows, const Parser* parser, Arena* arena, Arena* scratch)
{
    const TypeTable* table = program->types;
    const uint64_t room = (uint64_t)table->types.count << BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t tokens = program->input->token_count;
    CheckDebugValues values = { .program = program, .rows = rows, .parser = parser, .original = debug };
    values.type_offsets = room <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, room) : NULL;
    values.types = room <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(scratch, TypeId, room) : NULL;
    values.enum_definitions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, table->types.count);
    values.local_ids = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    values.scopes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)tokens + 1);
    bool valid = values.type_offsets != NULL && values.types != NULL && values.enum_definitions != NULL && values.local_ids != NULL && values.scopes != NULL;
    if (valid)
    {
        memset(values.type_offsets, 0, room * sizeof(uint32_t));
        memset(values.enum_definitions, 0, (uint64_t)table->types.count * sizeof(uint32_t));
        memset(values.local_ids, 0xff, ((uint64_t)tokens + 1) * sizeof(uint32_t));
        for (uint32_t t = 0; t < tokens; t += 1)
        {
            if (program->input->declarations[t] == t)
            {
                check_debug_type_add(&values, program->input->declared_types[t]);
            }
        }
        for (uint32_t e = 0; e < program->input->enum_definition_count; e += 1)
        {
            values.enum_definitions[type_index(program->input->enum_definitions[e].type)] = e + 1;
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
        valid = values.info.size < BUSTER_DEBUG_DWARF32_LIMIT && values.ranges.size < BUSTER_DEBUG_DWARF32_LIMIT && values.locations.size < BUSTER_DEBUG_DWARF32_LIMIT;
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
