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
        .type = frame->count == 0 || frame->restart ? BUSTER_TYPE_INT : frame->enumerator_type,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE,
        .constant_kind = CHECK_CONSTANT_INTEGER,
    };
    if (frame->count != 0 && !frame->restart)
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
                    check_input_warning_add_at(reader, frame->cursor, CHECK_WARNING_ENUM_TOO_LARGE, frame->cursor + 1u);
                }
            }
            value->integer = (previous + 1) & check_action_mask(width);
        }
    }
    return valid;
}

// An enumerator of an enum with a fixed underlying type (complete from its base): its value, explicit or the one before
// plus 1, must be representable in that type, and is converted to it; its type is that type in its list, as clang 23
// types it (Sema::CheckEnumConstant), and after the list the enum's in C23 alone (check_input_enum_complete). Explicit
// values are representable as clang's isRepresentableIntegerValue has it: a negative one by its significant bits, the
// others by their active bits, less the sign bit for a signed type. An error is reported at the enumerator's name, as
// clang reports it: "enumerator value is not representable in the underlying type 'T'" (its typedef's spelling from the
// base), the one after the greatest naming its value. An enum a mode completed (BUSTER_CHECK_ENUM_MODED, `moded`) has its
// explicit values checked alone, and kept as a plain enum's.
static bool check_input_enum_fixed_value(CheckInputReader* reader, const CheckTagFrame* frame, uint32_t name, bool explicit_value, CheckValue* value, bool moded)
{
    const TypeTable* types = reader->types;
    const TypeId underlying = types->types.base[type_index(frame->type)];
    unsigned __int128 next = 0;
    uint32_t width = 0;
    bool sign = false;
    bool valid = check_action_integer_type(types, underlying, &width, &sign);
    if (valid && explicit_value)
    {
        uint32_t value_width = 0;
        bool value_sign = false;
        valid = check_action_integer_type(types, value->type, &value_width, &value_sign);
        const unsigned __int128 mask = check_action_mask(value_width);
        const unsigned __int128 bits = value->integer & mask;
        const bool negative = valid && value_sign && (bits >> (value_width - 1)) != 0;
        const uint32_t needed = negative ? check_input_enum_active_bits(~bits & mask) + 1 : check_input_enum_active_bits(bits);
        valid = valid && needed <= (negative || !sign ? width : width - 1);
        if (valid && !moded)
        {
            check_action_convert_plain(types, value, underlying, name);
        }
    }
    else if (valid)
    {
        const unsigned __int128 mask = check_action_mask(width);
        const unsigned __int128 maximum = sign ? mask >> 1 : mask;
        const unsigned __int128 previous = frame->enumerator & mask;
        valid = frame->count == 0 || frame->restart || previous != maximum;
        next = previous + 1;
        *value = (CheckValue)
        {
            .type = underlying,
            .category = CHECK_RVALUE,
            .flags = BUSTER_CHECK_ICE,
            .constant_kind = CHECK_CONSTANT_INTEGER,
            .integer = frame->count == 0 || frame->restart ? 0 : (previous + 1) & mask,
        };
    }
    value->type = moded ? value->type : underlying;
    const uint32_t before = reader->output->report_count;
    if (!valid)
    {
        check_input_report(reader, name, CHECK_INPUT_ENUM_NOT_REPRESENTABLE);
    }
    if (reader->output->report_count > before)
    {
        // (A fixed base's typedef spelled from its `{`, back to its `:`.)
        uint32_t open = frame->token;
        while (open < frame->close && reader->parser->tokens.kinds[open] != PARSE_KIND_L_BRACE)
        {
            open += 1;
        }
        reader->output->reports[before].type = underlying;
        reader->output->reports[before].type_spelling = moded ? 0 : check_input_typedef_spelling(reader->output, reader->parser, open);
        reader->output->reports[before].detail = explicit_value ? 0 : 1;
        reader->output->reports[before].value = (uint64_t)next;
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
    bool missing = false;
    CheckInputAttributes* attributes = frame->enumerator_attributes;
    // An enum with a fixed underlying type is complete from its base on (check_input_tag_identity); one a mode completed
    // isn't fixed (BUSTER_CHECK_ENUM_MODED).
    const bool complete = type_is_complete(reader->types, frame->type);
    const bool moded = complete && reader->enum_fixed[reader->types->types.index[type_index(frame->type)]] == BUSTER_CHECK_ENUM_MODED;
    const bool fixed = complete && !moded;
    *nested_tag = BUSTER_CHECK_NONE;
    const bool resumed = frame->enum_job != 0;
    if (!resumed)
    {
        valid = valid && check_input_read_attributes(reader, &i, frame->close, &attributes);
        // An enumerator's subject as clang's attributes name it (#147).
        reader->attribute_declaration = name + 1;
        valid = valid && check_input_attribute_subjects(reader, attributes, BUSTER_CHECK_SUBJECT_ENUMERATOR, 0, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE);
        reader->attribute_declaration = 0;
        for (uint32_t a = 0; valid && attributes != 0 && a < attributes->count; a += 1)
        {
            CheckInputAttribute* attribute = attributes->records + a;
            // One the subject checks dropped, with their error (#1447) or warning, is no enumerator's.
            const bool dropped = attribute->ignored && attribute->subject_ignored;
            valid = dropped || (attribute->kind != CHECK_ATTRIBUTE_MODE && attribute->kind != CHECK_ATTRIBUTE_FALLTHROUGH &&
                                attribute->kind != CHECK_ATTRIBUTE_ALIGNAS && attribute->kind != CHECK_ATTRIBUTE_OVERLOADABLE);
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
            // Its undeclared names are clang's errors at each one, recovered (#1447, check_constant_name).
            reader->constant_names = CHECK_CONSTANT_NAMES_RECOVER;
            frame->enum_job = valid ? check_input_constant_job(reader, first, i) : 0;
            reader->constant_names = CHECK_CONSTANT_NAMES_NONE;
            frame->enumerator_attributes = attributes;
            failed_token = first;
        }
        CheckConstantInput* job = frame->enum_job;
        valid = job != 0 && check_input_constant_job_step(job, &value, nested_tag);
        // An undeclared name in the value, which no correction took: clang's error, recovered, and the enumerator takes the
        // value it would have had with none, as clang's does (`enum E { Z = 4, A = zzz, B }` gives A 5 and B 6).
        missing = job != 0 && *nested_tag == BUSTER_CHECK_NONE && job->missing != 0;
        if (job != 0)
        {
            i = job->end;
            failed_token = job->failure != BUSTER_CHECK_NONE ? job->failure : job->first;
            if (*nested_tag == BUSTER_CHECK_NONE)
            {
                valid = !missing && valid && check_input_const_rvalue(reader, i, &value) && check_input_integer_constant(reader, &value);
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
        if (missing)
        {
            value = (CheckValue){ 0 };
            valid = fixed ? check_input_enum_fixed_value(reader, frame, name, false, &value, false) : check_input_enum_successor(reader, frame, &value);
        }
        else if (valid && *nested_tag == BUSTER_CHECK_NONE && fixed)
        {
            valid = check_input_enum_fixed_value(reader, frame, name, true, &value, false);
        }
        else if (valid && *nested_tag == BUSTER_CHECK_NONE && moded && !check_input_enum_fixed_value(reader, frame, name, true, &value, true))
        {
            valid = false;
        }
        else if (valid && *nested_tag == BUSTER_CHECK_NONE && check_input_enum_fits_int(reader->types, &value))
        {
            check_action_convert_plain(reader->types, &value, BUSTER_TYPE_INT, name);
        }
    }
    else if (valid && fixed)
    {
        valid = check_input_enum_fixed_value(reader, frame, name, false, &value, false);
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
            // A name of its scope already, an enumeration constant's or (in a parameter list) a parameter's, as clang
            // reports `int f(int K, enum E { K } e)` at the second K.
            const uint32_t identifier = parser->tokens.names[name];
            const uint32_t old = check_input_library_binding(reader, identifier, i);
            const uint32_t depth = check_input_declaration_depth(reader);
            valid = old == 0 || reader->bindings.depth[old - 1] != depth;
            if (!valid)
            {
                const CheckInputMessage message = reader->bindings.kind[old - 1] == C_BINDING_ENUMERATOR ? CHECK_INPUT_ENUMERATOR_REDEFINITION : CHECK_INPUT_REDECLARATION;
                const uint32_t before = reader->output->report_count;
                check_input_report_subject(reader, name, message, reader->bindings.declaration[old - 1]);
                const bool reported = reader->output->report_count == before + 1 && !reader->output->resource;
                if (reported)
                {
                    // clang's note at the earlier declaration (#1447): "previous definition is here", an enumerator's or
                    // any other name's of the scope (an object, a typedef, a parameter), whose error is clang's
                    // "redefinition of 'K'". The enumerator is dropped and the walk goes on, its name still the earlier
                    // one's.
                    reader->output->reports[before].detail = message == CHECK_INPUT_REDECLARATION ? CHECK_REDECLARATION_DEFINITION : 0;
                    reader->output->reports[before].note_token = reader->bindings.declaration[old - 1];
                    reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
                    valid = check_input_recover_reports(reader, before);
                    frame->dropped |= valid;
                    frame->restart |= valid;
                }
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
                check_input_set_attributes_at(reader->output, name, attributes);
                valid = c_bindings_declare(&reader->bindings, C_BINDING_ORDINARY, identifier, C_BINDING_ENUMERATOR, name, i, depth) != 0;
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
                    frame->restart = false;
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

// Whether a fixed enum's alignment after its body is clang's (#1447): clang memoizes an enum's layout at its first query
// (a sizeof, an _Alignof, an array or a record of it) and keeps it, so that a later aligned attribute changes nothing.
// Taken where the enum completed at this body's own `{` and the body doesn't name it (`enum E`), so that nothing could
// have asked; a reference, or a completion at an earlier declaration (`enum E : char;`), stays refused. Its one scan is
// of the body's tokens, of a rare form's only.
static bool check_input_enum_late_alignment(const CheckInputReader* reader, const CheckTagFrame* frame, uint32_t tag)
{
    const Parser* parser = reader->parser;
    const uint32_t open = reader->tag_completions[tag];
    const uint32_t name = reader->types->tags.name[tag];
    bool valid = open < frame->close && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE && parser->tree.parents[open] == frame->close;
    for (uint32_t t = open + 1; valid && name != 0 && t + 1 < frame->close; t += 1)
    {
        valid = parser->tokens.kinds[t] != PARSE_KIND_ENUM || parser->tokens.kinds[t + 1] != PARSE_KIND_IDENTIFIER || parser->tokens.names[t + 1] != name;
    }
    return valid;
}

static bool check_input_enum_complete(CheckInputReader* reader, CheckTagFrame* frame, CheckTagMember* members)
{
    const bool packed = frame->attributes != 0 && frame->attributes->packed;
    const uint8_t alignment = frame->attributes != 0 ? frame->attributes->alignment_log2 : 0;
    const uint32_t tag = reader->types->types.index[type_index(frame->type)];
    bool valid = frame->count != 0 || frame->dropped;
    const bool moded = reader->enum_fixed[tag] == BUSTER_CHECK_ENUM_MODED;
    // A mode after its body (#147): clang's ActOnEnumBody applies the enum's attributes before it takes a type, and takes
    // the mode's (check_input_enum_mode).
    const CheckInputAttribute* late_mode = !moded && reader->enum_fixed[tag] == BUSTER_CHECK_ENUM_NONE ? check_input_enum_mode(frame->attributes, frame->close) : NULL;
    // An error after which clang installs no type, an unknown or unsupported mode's, leaves the enum as one with no mode
    // (#1447); one that still installs a floating or a complex type gives it the integer of its layout
    // (check_input_mode_enum_integer), and XC's or TC's stops the reader.
    const CheckInputAttributeRecovery late_recovery = late_mode != NULL ? check_input_attribute_recovery(reader, frame->attributes, (CheckInputAttribute*)late_mode) :
                                                      (CheckInputAttributeRecovery){ 0 };
    TypeId late_installed = 0;
    const TypeId late_checked = late_mode != NULL ? check_input_attribute_mode(reader, frame->attributes, late_mode, frame->type, &late_installed) : 0;
    uint8_t late_alignment = 0;
    const TypeId late_stand_in = late_mode != NULL && late_checked == 0 && late_installed != frame->type ?
                                 check_input_mode_enum_integer(reader->types, late_installed, &late_alignment) : 0;
    const bool late_recovered = late_mode != NULL && late_checked == 0 && (late_installed == frame->type || late_stand_in != 0) &&
                                check_input_attribute_recover(reader, frame->attributes, &late_recovery, 0, false, false);
    const bool late_dropped = late_recovered && late_installed == frame->type;
    const TypeId late_moded = late_checked != 0 ? late_checked : late_recovered ? late_stand_in : 0;
    late_mode = late_dropped ? NULL : late_mode;
    valid &= late_mode == NULL || late_moded != 0;
    if (reader->enum_fixed[tag] != BUSTER_CHECK_ENUM_NONE && !moded)
    {
        // Complete from its base, with its enumerators of the base's type in its list. An alignment after its body
        // (one before its name was applied there) is clang's too where nothing asked the enum's layout first
        // (check_input_enum_late_alignment). After the list, its enumerators are of its own type in C23 and keep the
        // base's before it, as clang 23 types them (Sema::ActOnEnumBody, C23 6.7.3.3p16).
        if (valid && alignment != 0 && alignment != reader->types->tags.alignment_log2[tag])
        {
            valid = check_input_enum_late_alignment(reader, frame, tag) && type_enum_realign(reader->types, frame->type, alignment);
            if (!valid)
            {
                check_input_report(reader, frame->close, CHECK_INPUT_UNSUPPORTED);
            }
        }
        for (uint32_t member = frame->first; valid && reader->parser->language.standard >= 2023 && member != BUSTER_CHECK_NONE; member = members[member].next)
        {
            reader->declared_types[members[member].member.name] = frame->type;
        }
        reader->enum_fixed[tag] = BUSTER_CHECK_ENUM_LISTED;
    }
    else
    {
        // (One a mode completed has its type; one after its body gives it.)
        const TypeId underlying = moded ? reader->types->types.base[type_index(frame->type)] : late_moded != 0 ? late_moded :
                                  type_enum_underlying(packed, frame->negative_bits, frame->positive_bits);
        // (A complex mode's stand-in takes the complex type's alignment, but where an alignment attribute gives one.)
        const uint8_t complete_alignment = alignment != 0 || late_checked != 0 || !late_recovered ? alignment : late_alignment;
        valid = valid && (moded || type_enum_complete(reader->types, frame->type, underlying, complete_alignment));
        reader->output->resource |= !valid && reader->types->last_error == TYPE_ERROR_FULL;
        reader->enum_fixed[tag] = moded ? BUSTER_CHECK_ENUM_LISTED : reader->enum_fixed[tag];
        if (valid)
        {
            if (frame->negative_bits > 64 || frame->positive_bits > 64 || (frame->negative_bits != 0 && frame->positive_bits == 64))
            {
                check_input_warning_add_at(reader, frame->token, CHECK_WARNING_ENUM_TOO_LARGE, frame->close + 1u);
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
    }
    return valid;
}
