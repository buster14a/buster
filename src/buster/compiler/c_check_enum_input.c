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
static bool check_input_enum_successor(CheckInputReader* reader, const CheckTagFrame* frame, CheckValue* value, uint32_t completion)
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
                    check_input_warning_add_at(reader, frame->cursor, CHECK_WARNING_ENUM_TOO_LARGE, completion != 0 ? completion : frame->cursor + 1u);
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
        const bool typed = valid;
        valid = valid && needed <= (negative || !sign ? width : width - 1);
        // (One that isn't representable is converted all the same, as clang's CheckEnumConstant casts it after its error.)
        if (typed && !moded)
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
        // Recovered, as clang goes on with the converted or wrapped value (#1807): the enumerator is declared, or reported
        // as a redefinition, and the walk goes on.
        valid = reader->output->report_count == before + 1 && check_input_recover_reports(reader, before);
    }
    return valid;
}

// C23's redefinition of an enum (#1447, N3037), in any scope (#1826): the second body is an anonymous enum's
// (check_input_tag_identity), whose enumerators of an earlier one's names are no redefinitions but names both declare,
// which their uses find ambiguous; once complete, it is compared with the earlier enum (check_tag_enum_compatible). Only a
// unit that defines an enum twice in one scope takes this, so it has no vector twin.

// CHECK_INPUT_TAG_INCOMPATIBLE's details of an enum's (c_check_tag_compatible.c's CheckTagMismatch has a record's): a pair
// of enumerators whose names or values differ, and one past the other definition's list.
#define BUSTER_CHECK_TAG_ENUM_VALUE 7u
#define BUSTER_CHECK_TAG_ENUM_EXTRA 8u

// Whether `token` is an enumerator of the definition at `keyword`: within its body's braces.
static bool check_tag_enum_member(const Parser* parser, uint32_t keyword, uint32_t token)
{
    uint32_t open = BUSTER_CHECK_NONE;
    const bool named = keyword < parser->tokens.count;
    if (named)
    {
        (void)check_tag_name_token(parser, keyword, 0, &open);
    }
    const bool body = named && open < parser->tokens.count && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE;
    return body && token > open && token < parser->tree.parents[open];
}

// Records that the enumerator at `token` declares the name of the earlier definition's at `earlier`: its uses are
// ambiguous until the comparison decides (check_tag_enum_compatible). Whether there was room. Lanes take no file scope.
static bool check_tag_enum_ambiguous(CheckInputReader* reader, uint32_t token, uint32_t earlier)
{
    const uint32_t count = reader->parser->tokens.count;
    if (reader->ambiguous_index == NULL)
    {
        reader->ambiguous_index = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, count);
        if (reader->ambiguous_index != NULL)
        {
            memset(reader->ambiguous_index, 0, (uint64_t)count * sizeof(uint32_t));
        }
        reader->output->ambiguous_index = reader->ambiguous_index;
    }
    bool room = reader->ambiguous_index != NULL && reader->ambiguous_count < reader->ambiguous_capacity;
    if (reader->ambiguous_index != NULL && !room)
    {
        const uint32_t capacity = reader->ambiguous_capacity == 0 ? 16 : reader->ambiguous_capacity * 2;
        uint32_t* declarations = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        uint32_t* partners = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        room = declarations != NULL && partners != NULL && ends != NULL;
        if (room && reader->ambiguous_count != 0)
        {
            memcpy(declarations, reader->ambiguous_declarations, (uint64_t)reader->ambiguous_count * sizeof(uint32_t));
            memcpy(partners, reader->ambiguous_partners, (uint64_t)reader->ambiguous_count * sizeof(uint32_t));
            memcpy(ends, reader->ambiguous_ends, (uint64_t)reader->ambiguous_count * sizeof(uint32_t));
        }
        reader->ambiguous_declarations = room ? declarations : reader->ambiguous_declarations;
        reader->ambiguous_partners = room ? partners : reader->ambiguous_partners;
        reader->ambiguous_ends = room ? ends : reader->ambiguous_ends;
        reader->ambiguous_capacity = room ? capacity : reader->ambiguous_capacity;
    }
    if (room && token < count)
    {
        reader->ambiguous_declarations[reader->ambiguous_count] = token;
        reader->ambiguous_partners[reader->ambiguous_count] = earlier;
        reader->ambiguous_ends[reader->ambiguous_count] = BUSTER_CHECK_NONE;
        reader->ambiguous_count += 1;
        reader->ambiguous_index[token] = reader->ambiguous_count;
    }
    reader->output->resource |= !room;
    return room;
}

// Whether the name at `left` comes before the one at `right` in clang's order of a C23 enum's comparison: their bytes'.
static bool check_tag_enum_before(const CheckInputReader* reader, uint32_t left, uint32_t right)
{
    const InternTable* table = reader->pp->table;
    const uint32_t left_name = reader->parser->tokens.names[left];
    const uint32_t right_name = reader->parser->tokens.names[right];
    const uint32_t left_length = intern_length(table, left_name);
    const uint32_t right_length = intern_length(table, right_name);
    const int order = memcmp(intern_bytes(table, left_name), intern_bytes(table, right_name), left_length < right_length ? left_length : right_length);
    return order < 0 || (order == 0 && left_length < right_length);
}

// Sorts `count` enumerators' tokens by their names (check_tag_enum_before), a bottom-up merge through `spare`, stable.
static void check_tag_enum_sort(const CheckInputReader* reader, uint32_t* tokens, uint32_t* spare, uint32_t count)
{
    uint32_t* from = tokens;
    uint32_t* to = spare;
    for (uint32_t width = 1; width < count; width *= 2)
    {
        for (uint32_t begin = 0; begin < count; begin += 2 * width)
        {
            const uint32_t middle = begin + width < count ? begin + width : count;
            const uint32_t end = begin + 2 * width < count ? begin + 2 * width : count;
            uint32_t left = begin;
            uint32_t right = middle;
            for (uint32_t out = begin; out < end; out += 1)
            {
                const bool take_left = left < middle && (right >= end || !check_tag_enum_before(reader, from[right], from[left]));
                to[out] = take_left ? from[left++] : from[right++];
            }
        }
        uint32_t* swap = from;
        from = to;
        to = swap;
    }
    if (from != tokens)
    {
        memcpy(tokens, from, (uint64_t)count * sizeof(uint32_t));
    }
}

// A recovered CHECK_INPUT_TAG_INCOMPATIBLE of an enum's redefinition at `token`, its name: the notes' enumerators, the
// second definition's at `first` then the earlier's at `second`, or for one past the other's list, it at `first` and
// "no corresponding enumerator here" at the other definition's name (`second`). The values the notes print: the first's
// in `value`, the second's in `type` (its low half) and `type_spelling` (its high half). Whether the reader goes on.
static bool check_tag_enum_report(CheckInputReader* reader, uint32_t token, uint32_t detail, uint32_t first, uint32_t second, uint32_t close)
{
    const uint32_t before = reader->output->report_count;
    const bool placed = token < reader->parser->tokens.count;
    if (placed)
    {
        check_input_report(reader, token, CHECK_INPUT_TAG_INCOMPATIBLE);
    }
    if (reader->output->report_count == before + 1 && !reader->output->resource)
    {
        const uint64_t second_value = detail == BUSTER_CHECK_TAG_ENUM_VALUE ? (uint64_t)reader->enum_values[second] : 0;
        CheckInputReport* report = reader->output->reports + before;
        report->detail = detail;
        report->attribute = first;
        report->note_token = second;
        report->note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
        report->value = (uint64_t)reader->enum_values[first];
        report->type = (TypeId)second_value;
        report->type_spelling = (uint32_t)(second_value >> 32);
        report->repeated = true;
        report->sort = close < reader->parser->tokens.count ? close : 0;
    }
    return !placed || (reader->output->report_count == before + 1 && check_input_recover_reports(reader, before));
}

// A complete C23 redefinition of an enum, compared with the earlier one as clang's structural equivalence compares them:
// each list's enumerators sorted by name, then pairwise their names and values, and past the shorter list the longer one's
// next enumerator; the first difference is clang's "incompatible definitions". A compatible one makes its declarators take
// the earlier type, and its names after its body no longer ambiguous; an incompatible one keeps its own type, and both
// lists' names that both declare ambiguous. Whether the reader goes on.
static bool check_tag_enum_compatible(CheckInputReader* reader, const CheckTagFrame* frame, const CheckTagMember* members)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    uint32_t earlier_keyword = BUSTER_CHECK_NONE;
    const TypeId earlier = check_tag_compatible_earlier(reader, frame->token, &earlier_keyword);
    bool valid = true;
    if (earlier != 0 && earlier_keyword < parser->tokens.count)
    {
        const uint32_t name = types->tags.name[types->types.index[type_index(earlier)]];
        uint32_t new_open = BUSTER_CHECK_NONE;
        uint32_t old_open = BUSTER_CHECK_NONE;
        const uint32_t new_name = check_tag_name_token(parser, frame->token, name, &new_open);
        const uint32_t old_name = check_tag_name_token(parser, earlier_keyword, name, &old_open);
        const bool bodied = old_open < parser->tokens.count && parser->tokens.kinds[old_open] == PARSE_KIND_L_BRACE;
        const uint32_t old_close = bodied ? parser->tree.parents[old_open] : old_open;
        // The earlier list: the identifiers of its body that declare themselves as enumerators. An enum's body holds no
        // other declaration (a tag in an enumerator's value is a type name's, not a declarator's).
        uint32_t old_count = 0;
        for (uint32_t t = bodied ? old_open + 1 : old_close; t < old_close; t += 1)
        {
            old_count += parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && reader->output->declarations[t] == t && reader->declaration_kinds[t] == C_BINDING_ENUMERATOR;
        }
        const uint32_t new_count = frame->count;
        const uint64_t mark = reader->scratch->length;
        const uint64_t room = (uint64_t)old_count + new_count + (old_count > new_count ? old_count : new_count) + 1;
        uint32_t* olds = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, room);
        valid = olds != NULL;
        reader->output->resource |= !valid;
        if (valid)
        {
            uint32_t* news = olds + old_count;
            uint32_t* spare = news + new_count;
            uint32_t at = 0;
            for (uint32_t t = bodied ? old_open + 1 : old_close; t < old_close; t += 1)
            {
                if (parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && reader->output->declarations[t] == t && reader->declaration_kinds[t] == C_BINDING_ENUMERATOR)
                {
                    olds[at++] = t;
                }
            }
            at = 0;
            for (uint32_t member = frame->first; member != BUSTER_CHECK_NONE && at < new_count; member = members[member].next)
            {
                news[at++] = members[member].member.name;
            }
            check_tag_enum_sort(reader, olds, spare, old_count);
            check_tag_enum_sort(reader, news, spare, new_count);
            const uint32_t shorter = old_count < new_count ? old_count : new_count;
            uint32_t differ = 0;
            while (differ < shorter && parser->tokens.names[olds[differ]] == parser->tokens.names[news[differ]] &&
                   reader->enum_values[olds[differ]] == reader->enum_values[news[differ]])
            {
                differ += 1;
            }
            const bool compatible = differ == shorter && old_count == new_count;
            const uint32_t at_name = new_name < parser->tokens.count ? new_name : frame->token;
            if (compatible)
            {
                reader->tag_types[frame->token] = earlier;
            }
            else if (differ < shorter)
            {
                valid = check_tag_enum_report(reader, at_name, BUSTER_CHECK_TAG_ENUM_VALUE, news[differ], olds[differ], frame->close);
            }
            else
            {
                const bool extra_new = new_count > old_count;
                valid = check_tag_enum_report(reader, at_name, BUSTER_CHECK_TAG_ENUM_EXTRA, extra_new ? news[differ] : olds[differ], extra_new ? old_name : new_name,
                                              frame->close);
            }
            // Its enumerators' names that the earlier's declare: no longer ambiguous past its body where they agree.
            for (uint32_t pair = 0; compatible && pair < reader->ambiguous_count; pair += 1)
            {
                const uint32_t token = reader->ambiguous_declarations[pair];
                reader->ambiguous_ends[pair] = token > frame->token && token < frame->close ? frame->close : reader->ambiguous_ends[pair];
            }
            valid = valid && (compatible || check_tag_failed_note(reader, frame->token));
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

// A rejected enumerator's type spelling follows parentheses and selected expressions to the value's own cast or
// declaration. This is diagnostic work only, bounded by the constant job's token range.
static uint32_t check_input_enum_value_spelling(const CheckConstantInput* job)
{
    const CheckInputReader* reader = job->reader;
    const Parser* parser = reader->parser;
    uint32_t root = check_constant_range_root(job, job->first, job->end);
    for (uint32_t steps = 0; root >= job->first && root < job->end && steps < job->end - job->first; steps += 1)
    {
        const uint32_t chosen = reader->selected_tokens != NULL ? reader->selected_tokens[root] : BUSTER_CHECK_NONE;
        const uint32_t selected = chosen & ~CHECK_INPUT_SOURCE_SELECTION;
        if (chosen != BUSTER_CHECK_NONE && selected >= job->first && selected < job->end && selected != root)
        {
            root = selected;
        }
        else if (parser->tree.roles[root] == PARSE_ROLE_CLOSE_GROUP || parser->tokens.kinds[root] == PARSE_KIND_COMMA)
        {
            uint32_t value = BUSTER_CHECK_NONE;
            bool unique = true;
            const bool comma = parser->tokens.kinds[root] == PARSE_KIND_COMMA;
            for (uint32_t child = reader->first_child[root]; child != BUSTER_CHECK_NONE && unique; child = reader->next_child[child])
            {
                if (child >= job->first && child < job->end && job->present[child - job->first])
                {
                    unique = comma || value == BUSTER_CHECK_NONE;
                    value = child;
                }
            }
            root = unique && value != root ? value : BUSTER_CHECK_NONE;
        }
        else
        {
            break;
        }
    }
    return check_input_token_type_spelling(reader->output, parser, root, true);
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
    uint32_t type_report = BUSTER_CHECK_NONE;
    // The value's failure evidence, for its error in clang's words (#1447), and its first token.
    CheckConstantFailure failure = { .token = BUSTER_CHECK_NONE, .callee = BUSTER_CHECK_NONE };
    uint32_t value_first = name;
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
            failure = job->evidence;
            value_first = job->first;
            if (*nested_tag == BUSTER_CHECK_NONE)
            {
                valid = !missing && valid && check_input_const_rvalue(reader, i, &value);
                if (valid)
                {
                    // An unused comma operand is still diagnosed when the final value has a rejected type.
                    check_input_attribute_unused(reader, job->first, i, true);
                }
                if (valid && !check_action_is_integer(reader->types, value.type))
                {
                    const uint32_t before = reader->output->report_count;
                    const uint32_t root = check_constant_range_root(job, job->first, job->end);
                    // Clang names a function or array expression before its required-value conversion decays it.
                    const TypeId original = root != BUSTER_CHECK_NONE ? job->values[root - job->first].type : value.type;
                    const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, original));
                    const TypeId type = type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION ? original : value.type;
                    check_input_report_typed(reader, job->first, CHECK_INPUT_ENUM_VALUE_TYPE, type, BUSTER_CHECK_NONE, 0);
                    if (reader->output->report_count > before)
                    {
                        CheckInputReport* report = reader->output->reports + before;
                        report->type_spelling = check_input_enum_value_spelling(job);
                        report->note_kind = CHECK_INPUT_NOTE_NONE;
                        report->completion = i;
                        type_report = reader->output->recovered_count;
                        missing = check_input_recover_reports(reader, before);
                        type_report = missing ? type_report : BUSTER_CHECK_NONE;
                    }
                    valid = false;
                }
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
        if (missing)
        {
            value = (CheckValue){ 0 };
            valid = fixed ? check_input_enum_fixed_value(reader, frame, name, false, &value, false) :
                            check_input_enum_successor(reader, frame, &value, type_report != BUSTER_CHECK_NONE ? i + 1u : 0);
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
        valid = check_input_enum_successor(reader, frame, &value, 0);
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
            // A C23 redefinition's enumerator of an earlier definition's name (#1447): no redefinition, but a name both
            // declare, which its uses find ambiguous (check_tag_enum_ambiguous).
            // (A third definition's finds the second's, itself ambiguous with the earlier's, whose candidate it names.)
            const uint32_t bound = !valid ? reader->bindings.declaration[old - 1] : BUSTER_CHECK_NONE;
            const uint32_t chained = !valid && bound < parser->tokens.count && reader->ambiguous_index != NULL ? reader->ambiguous_index[bound] : 0;
            const uint32_t old_declaration = chained != 0 ? reader->ambiguous_partners[chained - 1] : bound;
            uint32_t earlier_keyword = BUSTER_CHECK_NONE;
            const bool ambiguous = !valid && reader->c23_count != 0 && reader->bindings.kind[old - 1] == C_BINDING_ENUMERATOR &&
                                   check_tag_compatible_earlier(reader, frame->token, &earlier_keyword) != 0 &&
                                   check_tag_enum_member(parser, earlier_keyword, old_declaration);
            valid |= ambiguous;
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
                    // (After a value error at the same name, recovered, as clang reports both: #1807.)
                    reader->output->reports[before].repeated = true;
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
                    valid = !ambiguous || check_tag_enum_ambiguous(reader, name, old_declaration);
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
        check_input_constant_failure_report(reader, &failure, value_first, failed_token);
    }
    // An invalid value's type is checked before its recovered successor or name. Keep those follow-on errors after
    // it even though their source location is the earlier enumerator name, and keep expression warnings first.
    if (type_report != BUSTER_CHECK_NONE)
    {
        const uint32_t completion = reader->output->recovered_reports[type_report].completion;
        for (uint32_t r = type_report; r < reader->output->recovered_count; r += 1)
        {
            reader->output->recovered_reports[r].completion = completion;
            reader->output->recovered_reports[r].suborder = 1u + r - type_report;
        }
    }
    return valid;
}

// A fixed enum's alignment after its body, as clang takes it (#1447): clang memoizes an enum's layout at its first query
// and keeps it, so that a later aligned attribute changes nothing after one. Where the enum completed at this body's own
// `{`, each `enum E` the body writes is read: as a sizeof's or an _Alignof's operand, or its array's element, it asked the layout (#1447 C3: the
// alignment is ignored, BUSTER_CHECK_LATE_ALIGNMENT_KEEP); as a cast's type, or a pointer to it in either, it didn't;
// anywhere else, or a completion at
// an earlier declaration (`enum E : char;`), the attribute stays refused (BUSTER_CHECK_LATE_ALIGNMENT_REFUSE). Its one
// scan is of the body's tokens, of a rare form's only.
#define BUSTER_CHECK_LATE_ALIGNMENT_REFUSE 0u
#define BUSTER_CHECK_LATE_ALIGNMENT_APPLY 1u
#define BUSTER_CHECK_LATE_ALIGNMENT_KEEP 2u

static uint32_t check_input_enum_late_alignment(const CheckInputReader* reader, const CheckTagFrame* frame, uint32_t tag)
{
    const Parser* parser = reader->parser;
    const uint32_t open = reader->tag_completions[tag];
    const uint32_t name = reader->types->tags.name[tag];
    bool valid = open < frame->close && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE && parser->tree.parents[open] == frame->close;
    bool queried = false;
    for (uint32_t t = open + 1; valid && name != 0 && t + 1 < frame->close; t += 1)
    {
        const bool named = parser->tokens.kinds[t] == PARSE_KIND_ENUM && parser->tokens.kinds[t + 1] == PARSE_KIND_IDENTIFIER && parser->tokens.names[t + 1] == name;
        // The construct whose group holds the name: the nearest sizeof's, _Alignof's or cast's closing token.
        uint8_t context = PARSE_ROLE_CLOSE_BODY;
        for (uint32_t at = t, steps = 0; named && at < frame->close && steps < frame->close - open && context == PARSE_ROLE_CLOSE_BODY; steps += 1)
        {
            const uint8_t role = parser->tree.roles[at];
            context = role == PARSE_ROLE_CLOSE_SIZEOF || role == PARSE_ROLE_CLOSE_CAST ? role : context;
            at = parser->tree.parents[at] != at ? parser->tree.parents[at] : frame->close;
        }
        // The type itself (`sizeof(enum E)`, or an array of it) asks its layout; a pointer to it (`enum E *`) doesn't; anything else stays refused.
        const uint8_t next = named && t + 2 < frame->close ? parser->tokens.kinds[t + 2] : PARSE_KIND_END;
        const bool itself = next == PARSE_KIND_R_PAREN || next == PARSE_KIND_L_SQUARE;
        const bool pointer = next == PARSE_KIND_STAR;
        queried |= named && context == PARSE_ROLE_CLOSE_SIZEOF && itself;
        valid = !named || ((context == PARSE_ROLE_CLOSE_SIZEOF || context == PARSE_ROLE_CLOSE_CAST) && (itself || pointer));
    }
    return !valid ? BUSTER_CHECK_LATE_ALIGNMENT_REFUSE : queried ? BUSTER_CHECK_LATE_ALIGNMENT_KEEP : BUSTER_CHECK_LATE_ALIGNMENT_APPLY;
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
    // (check_input_mode_enum_integer), and XC's or TC's leaves it one with no mode (check_input_mode_enum_wide).
    const CheckInputAttributeRecovery late_recovery = late_mode != NULL ? check_input_attribute_recovery(reader, frame->attributes, (CheckInputAttribute*)late_mode) :
                                                      (CheckInputAttributeRecovery){ 0 };
    TypeId late_installed = 0;
    const TypeId late_checked = late_mode != NULL ? check_input_attribute_mode(reader, frame->attributes, late_mode, frame->type, &late_installed) : 0;
    uint8_t late_alignment = 0;
    const TypeId late_stand_in = late_mode != NULL && late_checked == 0 && late_installed != frame->type ?
                                 check_input_mode_enum_integer(reader->types, late_installed, &late_alignment) : 0;
    const bool late_wide = late_mode != NULL && late_checked == 0 && late_stand_in == 0 && check_input_mode_enum_wide(reader->types, late_installed);
    const bool late_recovered = late_mode != NULL && late_checked == 0 && (late_installed == frame->type || late_stand_in != 0 || late_wide) &&
                                check_input_attribute_recover(reader, frame->attributes, &late_recovery, 0, false, false);
    const bool late_dropped = late_recovered && (late_installed == frame->type || late_wide);
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
            const uint32_t late = check_input_enum_late_alignment(reader, frame, tag);
            valid = late == BUSTER_CHECK_LATE_ALIGNMENT_KEEP || (late == BUSTER_CHECK_LATE_ALIGNMENT_APPLY && type_enum_realign(reader->types, frame->type, alignment));
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
