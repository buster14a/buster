// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source adapter for enumeration values. The provisional and completed types follow Clang's C rules;
// LLVM 23 SemaDecl.cpp is a behavioral reference only. Integer operations below use unsigned target bits.

static uint32_t check_input_enum_active_bits(unsigned __int128 value)
{
    const uint64_t high = (uint64_t)(value >> 64);
    const uint64_t low = (uint64_t)value;
    return high != 0 ? 128 - (uint32_t)__builtin_clzll(high) : low != 0 ? 64 - (uint32_t)__builtin_clzll(low) : 0;
}

static bool check_input_enum_fits_int(const TypeTable* types, const CheckValue* value)
{
    int64_t number = 0;
    return check_action_i64(types, value, &number) && number >= INT32_MIN && number <= INT32_MAX;
}

// An implicit successor retains the preceding enumerator's provisional type. Overflow widens to the next
// standard integer type with the same signedness; long long has the same width as long on this target. At the
// largest available width Clang warns and retains the wrapped value, rather than introducing __int128.
static bool check_input_enum_successor(CheckInputReader* reader, const CheckTagFrame* frame, CheckValue* value)
{
    uint32_t width = 0;
    bool sign = false;
    bool valid = true;
    *value = (CheckValue)
    {
        .type = frame->count == 0 ? BUSTER_TYPE_INT : frame->enumerator_type,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE,
        .constant_kind = CHECK_CONSTANT_INTEGER,
    };
    if (frame->count != 0)
    {
        valid = check_action_integer_type(reader->types, value->type, &width, &sign);
        if (valid)
        {
            const unsigned __int128 mask = check_action_mask(width);
            const unsigned __int128 previous = frame->enumerator & mask;
            const unsigned __int128 maximum = sign ? mask >> 1 : mask;
            if (previous == maximum)
            {
                const uint32_t previous_width = width;
                const TypeId signed_types[] = { BUSTER_TYPE_SHORT, BUSTER_TYPE_INT, BUSTER_TYPE_LONG, BUSTER_TYPE_LLONG };
                const TypeId unsigned_types[] = { BUSTER_TYPE_USHORT, BUSTER_TYPE_UINT, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULLONG };
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(signed_types); i += 1)
                {
                    const TypeId candidate = sign ? signed_types[i] : unsigned_types[i];
                    uint32_t candidate_width = 0;
                    bool candidate_sign = false;
                    check_action_integer_type(reader->types, candidate, &candidate_width, &candidate_sign);
                    if (candidate_width > width)
                    {
                        value->type = candidate;
                        width = candidate_width;
                        break;
                    }
                }
                if (width == previous_width)
                {
                    reader->output->constant_warnings[frame->cursor] |= CHECK_WARNING_ENUM_TOO_LARGE;
                }
            }
            value->integer = (previous + 1) & check_action_mask(width);
        }
    }
    return valid;
}

static bool check_input_enum_read(CheckInputReader* reader, CheckTagFrame* frame, CheckTagMember* members, uint32_t* member_count, uint32_t* nested_tag)
{
    const Parser* parser = reader->parser;
    const uint32_t name = frame->cursor;
    const uint32_t report_count = reader->output->report_count;
    uint32_t failed_token = name;
    uint32_t i = name + 1;
    bool valid = name < frame->close && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER;
    CheckValue value = { 0 };
    CheckInputAttributes* attributes = frame->enumerator_attributes;
    *nested_tag = BUSTER_CHECK_NONE;
    const bool resumed = frame->enum_job != 0;
    if (!resumed)
    {
        valid = valid && check_input_read_attributes(reader, &i, frame->close, &attributes);
        for (uint32_t a = 0; valid && attributes != 0 && a < attributes->count; a += 1)
        {
            CheckInputAttribute* attribute = attributes->records + a;
            valid = attribute->kind != CHECK_ATTRIBUTE_MODE && attribute->kind != CHECK_ATTRIBUTE_FALLTHROUGH &&
                    attribute->kind != CHECK_ATTRIBUTE_ALIGNAS && attribute->kind != CHECK_ATTRIBUTE_OVERLOADABLE;
            if (!valid)
            {
                check_input_attribute_error(reader, attributes, attribute->first, attribute->end, CHECK_ATTRIBUTE_ERROR_SUBJECT);
            }
            attribute->ignored |= attribute->kind == CHECK_ATTRIBUTE_PACKED || attribute->kind == CHECK_ATTRIBUTE_NORETURN ||
                                  attribute->kind == CHECK_ATTRIBUTE_VECTOR_SIZE || attribute->kind == CHECK_ATTRIBUTE_EXT_VECTOR_TYPE;
        }
    }
    if (valid && (resumed || (i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_EQUAL)))
    {
        if (!resumed)
        {
            const uint32_t first = ++i;
            while (i < frame->close && parser->tokens.kinds[i] != PARSE_KIND_COMMA)
            {
                const uint8_t kind = parser->tokens.kinds[i];
                if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
                {
                    const uint32_t close = parser->tree.parents[i];
                    if (close <= i || close >= frame->close)
                    {
                        valid = false;
                        break;
                    }
                    i = close;
                }
                i += 1;
            }
            frame->enum_job = valid ? check_input_constant_job(reader, first, i) : 0;
            frame->enumerator_attributes = attributes;
            failed_token = first;
        }
        CheckConstantInput* job = frame->enum_job;
        valid = job != 0 && check_input_constant_job_step(job, &value, nested_tag);
        if (job != 0)
        {
            i = job->end;
            failed_token = job->failure != BUSTER_CHECK_NONE ? job->failure : job->first;
            if (*nested_tag == BUSTER_CHECK_NONE)
            {
                valid = valid && check_input_integer_constant(reader, &value);
                if (valid)
                {
                    check_input_constant_warning(reader, job->first, i, &value, true);
                }
                const uint64_t mark = job->mark;
                frame->enum_job = 0;
                frame->enumerator_attributes = 0;
                arena_pop_to(reader->scratch, mark);
            }
        }
        if (valid && *nested_tag == BUSTER_CHECK_NONE && check_input_enum_fits_int(reader->types, &value))
        {
            check_action_convert_plain(reader->types, &value, BUSTER_TYPE_INT, name);
        }
    }
    else if (valid)
    {
        valid = check_input_enum_successor(reader, frame, &value);
    }
    if (*nested_tag == BUSTER_CHECK_NONE)
    {
        uint32_t width = 0;
        bool sign = false;
        valid = valid && check_action_integer_type(reader->types, value.type, &width, &sign);
        if (valid)
        {
            const uint32_t identifier = parser->tokens.names[name];
            const uint32_t old = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, identifier);
            valid = old == 0 || reader->bindings.depth[old - 1] != reader->scope_count;
            if (!valid)
            {
                check_input_report(reader, name, CHECK_INPUT_REDECLARATION);
            }
            else
            {
                const unsigned __int128 mask = check_action_mask(width);
                const unsigned __int128 bits = value.integer & mask;
                const bool negative = sign && (bits >> (width - 1)) != 0;
                const uint32_t active = check_input_enum_active_bits(negative ? ~bits & mask : bits);
                const uint32_t required = negative ? active + 1 : active != 0 ? active : 1;
                frame->negative_bits = negative && required > frame->negative_bits ? required : frame->negative_bits;
                frame->positive_bits = !negative && required > frame->positive_bits ? required : frame->positive_bits;
                frame->wide_enumerators |= !check_input_enum_fits_int(reader->types, &value);
                reader->declared_types[name] = value.type;
                reader->declaration_kinds[name] = C_BINDING_ENUMERATOR;
                reader->enum_values[name] = bits;
                reader->output->declarations[name] = name;
                reader->output->attributes[name] = attributes;
                valid = c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, identifier, C_BINDING_ENUMERATOR, name, i, reader->scope_count) != 0;
                reader->output->resource |= !valid;
                if (valid)
                {
                    const uint32_t member = (*member_count)++;
                    members[member] = (CheckTagMember)
                    {
                        .member = { .name = name, .type = value.type },
                        .next = BUSTER_CHECK_NONE,
                    };
                    if (frame->last != BUSTER_CHECK_NONE)
                    {
                        members[frame->last].next = member;
                    }
                    else
                    {
                        frame->first = member;
                    }
                    frame->last = member;
                    frame->enumerator = bits;
                    frame->enumerator_type = value.type;
                    frame->count += 1;
                }
            }
        }
        for (uint32_t ignored = name; ignored < i; ignored += 1)
        {
            reader->ignored[ignored] = true;
        }
        if (i < frame->close && parser->tokens.kinds[i] == PARSE_KIND_COMMA)
        {
            reader->ignored[i++] = true;
        }
        else
        {
            valid &= i == frame->close;
        }
        frame->cursor = i;
    }
    if (!valid && reader->output->report_count == report_count)
    {
        check_input_report(reader, failed_token, CHECK_INPUT_DECLARATION);
    }
    return valid;
}

static bool check_input_enum_complete(CheckInputReader* reader, CheckTagFrame* frame, CheckTagMember* members)
{
    const bool packed = frame->attributes != 0 && frame->attributes->packed;
    const uint8_t alignment = frame->attributes != 0 ? frame->attributes->alignment_log2 : 0;
    const TypeId underlying = type_enum_underlying(packed, frame->negative_bits, frame->positive_bits);
    bool valid = frame->count != 0 && type_enum_complete(reader->types, frame->type, underlying, alignment);
    reader->output->resource |= !valid && reader->types->last_error == TYPE_ERROR_FULL;
    if (valid)
    {
        if (frame->negative_bits > 64 || frame->positive_bits > 64 || (frame->negative_bits != 0 && frame->positive_bits == 64))
        {
            reader->output->constant_warnings[frame->token] |= CHECK_WARNING_ENUM_TOO_LARGE;
        }
        const TypeId type = !frame->wide_enumerators ? BUSTER_TYPE_INT : reader->parser->language.standard >= 2023 ? frame->type : underlying;
        for (uint32_t member = frame->first; member != BUSTER_CHECK_NONE; member = members[member].next)
        {
            const uint32_t token = members[member].member.name;
            CheckValue value =
            {
                .type = members[member].member.type,
                .integer = reader->enum_values[token],
                .constant_kind = CHECK_CONSTANT_INTEGER,
            };
            check_action_convert_plain(reader->types, &value, type, token);
            reader->declared_types[token] = type;
            reader->enum_values[token] = value.integer;
        }
    }
    return valid;
}
