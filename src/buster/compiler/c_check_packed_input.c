// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source witnesses for -Waddress-of-packed-member. Only direct member chains rooted in a declaration qualify,
// as in Clang 23's RefersToMemberWithReducedAlignment. Retain the nearest packed field, the whole chain's offset
// and the declaration's alignment; a packed attribute alone does not establish misalignment. The sparse records
// survive sizeof/typeof's discarded graph nodes. c_check_packed_diagnostic.c applies consuming conversions later.

static void check_input_packed_unwrap(const CheckInputReader* reader, uint32_t* first, uint32_t* end)
{
    const Parser* parser = reader->parser;
    bool changed = true;
    while (changed && *first < *end)
    {
        changed = false;
        if (parser->tokens.kinds[*first] == PARSE_KIND_EXTENSION)
        {
            *first += 1;
            changed = true;
        }
        else if (parser->tokens.kinds[*first] == PARSE_KIND_L_PAREN && parser->tree.parents[*first] == *end - 1 &&
                 parser->tree.roles[*end - 1] == PARSE_ROLE_CLOSE_GROUP && reader->output->expression_regions[*end - 1] == 0)
        {
            *first += 1;
            *end -= 1;
            changed = true;
        }
    }
}

// Find the packed culprit along anonymous-member lookup too. Lookup already selected a unique leaf; descend
// only through the anonymous record containing that leaf, overwriting the culprit toward the addressed member.
static uint32_t check_input_packed_field(CheckInputReader* reader, TypeId record, uint32_t name, uint64_t* offset,
                                         uint32_t* packed_member, uint32_t* packed_record)
{
    TypeTable* types = reader->types;
    const uint32_t member = type_find_member(types, record, name, offset);
    uint32_t remaining = types->members.count;
    while (member != 0 && remaining != 0)
    {
        remaining -= 1;
        record = type_strip_aligned(types, record);
        const uint32_t tag = types->types.index[type_index(record)];
        const uint32_t begin = types->tags.member_begin[tag];
        const uint32_t end = begin + types->tags.member_count[tag];
        uint32_t selected = member >= begin && member < end ? member : 0;
        for (uint32_t at = begin; selected == 0 && at < end; at += 1)
        {
            const TypeId inner = type_strip_aligned(types, types->members.type[at]);
            const uint8_t kind = type_kind(types, inner);
            if (types->members.name[at] == 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
            {
                uint64_t ignored = 0;
                if (type_find_member(types, inner, name, &ignored) == member)
                {
                    selected = at;
                }
            }
        }
        if (selected == 0)
        {
            remaining = 0;
        }
        else
        {
            if (types->tags.packed[tag] || (types->members.flags[selected] & BUSTER_TYPE_MEMBER_PACKED) != 0)
            {
                *packed_member = selected;
                *packed_record = tag;
            }
            if (selected == member)
            {
                remaining = 0;
            }
            else
            {
                record = types->members.type[selected];
            }
        }
    }
    return member;
}

static bool check_input_packed_target(const TypeTable* types, TypeId target, uint8_t alignment_log2)
{
    const uint8_t kind = target != 0 ? check_action_kind(types, target) : BUSTER_TYPE_KIND_VOID;
    bool discard = target != 0 && check_action_is_integer(types, target);
    if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId pointee = types->types.base[type_index(type_strip_aligned(types, target))];
        discard = !type_is_complete(types, pointee) || type_alignment_log2(types, pointee) <= alignment_log2;
    }
    return discard;
}

static String8 check_input_packed_identifier(const CheckInputReader* reader, uint32_t name)
{
    const String8 result = {
        .pointer = name != 0 ? (char*)intern_bytes(reader->pp->table, name) : "",
        .length = name != 0 ? intern_length(reader->pp->table, name) : 0,
    };
    return result;
}

static bool check_input_packed_anonymous(const TypeTable* types, uint32_t tag, uint32_t parent)
{
    bool anonymous = false;
    const uint32_t begin = types->tags.member_begin[parent];
    const uint32_t end = begin + types->tags.member_count[parent];
    for (uint32_t member = begin; member < end; member += 1)
    {
        anonymous |= types->members.name[member] == 0 && type_unqualified(types->members.type[member]) == types->tags.type[tag];
    }
    return anonymous;
}

// Named records use their identifier; anonymous records/fields need their lexical record contexts and presumed
// declaration point. Build that cold text in the source arena rather than reserving a string per type or token.
static String8 check_input_packed_name(CheckInputReader* reader, uint32_t tag, bool field, uint32_t* contexts)
{
    const TypeTable* types = reader->types;
    const Parser* parser = reader->parser;
    String8 result = check_input_packed_identifier(reader, types->tags.name[tag]);
    if (result.length == 0)
    {
        uint32_t token = BUSTER_CHECK_NONE;
        uint32_t closing = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; token == BUSTER_CHECK_NONE && i < parser->tokens.count; i += 1)
        {
            if (reader->tag_types[i] == types->tags.type[tag])
            {
                const uint32_t body = check_input_tag_body(parser, i, parser->tokens.count);
                if (body < parser->tokens.count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE)
                {
                    token = i;
                    closing = parser->tree.parents[body];
                }
            }
        }
        for (uint32_t i = closing < parser->tokens.count ? closing + 1 : parser->tokens.count;
             !field && result.length == 0 && i < parser->tokens.count && parser->tokens.kinds[i] != PARSE_KIND_SEMI; i += 1)
        {
            if (reader->declaration_kinds[i] == C_BINDING_TYPEDEF && type_unqualified(reader->declared_types[i]) == types->tags.type[tag])
            {
                result = check_input_packed_identifier(reader, parser->tokens.names[i]);
            }
        }
        if (result.length == 0 && token < parser->tokens.count)
        {
            uint32_t depth = 0;
            uint64_t capacity = 128;
            for (uint32_t i = 0; i < token; i += 1)
            {
                const TypeId type = reader->tag_types[i];
                const uint8_t kind = type_kind(types, type);
                if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                {
                    const uint32_t body = check_input_tag_body(parser, i, parser->tokens.count);
                    if (body < token && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE && parser->tree.parents[body] > token)
                    {
                        const uint32_t context = types->types.index[type_index(type)];
                        contexts[depth++] = context;
                        capacity += check_input_packed_identifier(reader, types->tags.name[context]).length + 32;
                    }
                }
            }
            const uint32_t source = parser->tokens.sources[token];
            pp_history_ensure(reader->pp);
            const uint32_t location = pp_expansion_location(reader->pp, source, pp_history_at(reader->pp, source));
            const char* file = "";
            uint32_t file_length = 0;
            const uint32_t line = pp_presumed(reader->pp, location, &file, &file_length);
            uint32_t column = 0;
            const uint32_t range = pp_find_range(reader->pp, location);
            if (range != BUSTER_PP_NONE)
            {
                pp_line_of(reader->pp, range, location, &column);
            }
            capacity += file_length;
            char* text = arena_push(reader->arena, capacity, 1);
            reader->output->resource |= text == NULL;
            if (text != NULL)
            {
                const char* kind = type_kind(types, types->tags.type[tag]) == BUSTER_TYPE_KIND_UNION ? "union" : "struct";
                uint64_t length = (uint64_t)snprintf(text, capacity, "%s", field ? kind : "");
                if (field)
                {
                    text[length++] = ' ';
                }
                for (uint32_t i = 0; i < depth; i += 1)
                {
                    const uint32_t context = contexts[i];
                    const String8 name = check_input_packed_identifier(reader, types->tags.name[context]);
                    const bool anonymous = i != 0 && check_input_packed_anonymous(types, context, contexts[i - 1]);
                    const char* context_kind = type_kind(types, types->tags.type[context]) == BUSTER_TYPE_KIND_UNION ? "union" : "struct";
                    length += (uint64_t)(name.length != 0 ? snprintf(text + length, capacity - length, "%.*s::", (int)name.length, name.pointer) :
                        snprintf(text + length, capacity - length, "(%s %s)::", anonymous ? "anonymous" : "unnamed", context_kind));
                }
                const bool anonymous = field || (depth != 0 && check_input_packed_anonymous(types, tag, contexts[depth - 1]));
                length += (uint64_t)snprintf(text + length, capacity - length, "%s%s(%s at %.*s:%u:%u)", field ? "" : kind, field ? "" : " ",
                                             anonymous ? "anonymous" : "unnamed", (int)file_length, file, line, column);
                result = (String8){ .pointer = text, .length = length };
            }
        }
    }
    return result;
}

static void check_input_packed_address(CheckInputReader* reader, uint32_t address, uint32_t* chain)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    uint32_t first = address + 1;
    uint32_t end = reader->ends[address] + 1;
    check_input_packed_unwrap(reader, &first, &end);
    const uint32_t token = first;
    uint32_t depth = 0;
    bool valid = true;
    while (valid && first + 2 < end && parser->tree.roles[end - 2] == PARSE_ROLE_MEMBER_OPERATOR)
    {
        chain[depth++] = end - 2;
        end -= 2;
        check_input_packed_unwrap(reader, &first, &end);
        // An arrow's member-valued base undergoes lvalue conversion; Clang's chain walk stops at that cast.
        valid = depth == 1 || parser->tokens.kinds[chain[depth - 2]] != PARSE_KIND_ARROW;
    }
    valid &= depth != 0 && first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
    const uint32_t declaration = valid ? reader->output->declarations[first] : BUSTER_CHECK_NONE;
    valid &= declaration < parser->tokens.count && !reader->output->compound_objects[declaration];
    TypeId type = valid ? reader->declared_types[first] : 0;
    uint32_t packed_member = 0;
    uint32_t packed_record = 0;
    uint64_t offset = 0;
    uint32_t complete_alignment = 0;
    for (uint32_t i = depth; valid && i-- > 0;)
    {
        const uint32_t access = chain[i];
        type = type_strip_aligned(types, type);
        const bool arrow = parser->tokens.kinds[access] == PARSE_KIND_ARROW;
        if (arrow && type_kind(types, type) == BUSTER_TYPE_KIND_POINTER)
        {
            type = type_strip_aligned(types, types->types.base[type_index(type)]);
        }
        const uint8_t kind = type_kind(types, type);
        valid = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_is_complete(types, type);
        if (valid)
        {
            if (i == depth - 1)
            {
                complete_alignment = type_alignment_log2(types, type);
                if (!arrow)
                {
                    const uint32_t natural = type_alignment_log2(types, reader->declared_types[first]);
                    const CheckInputAttributes* attributes = reader->output->attributes[declaration];
                    const uint32_t declared = attributes != 0 && attributes->alignment_log2 != 0 ? attributes->alignment_log2 - 1 : 0;
                    complete_alignment = natural > complete_alignment ? natural : complete_alignment;
                    complete_alignment = declared > complete_alignment ? declared : complete_alignment;
                }
            }
            uint64_t field_offset = 0;
            const uint32_t member = check_input_packed_field(reader, type, parser->tokens.names[access + 1], &field_offset, &packed_member, &packed_record);
            valid = member != 0 && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0;
            offset += field_offset;
            type = member != 0 ? types->members.type[member] : 0;
        }
    }
    if (valid && packed_member != 0)
    {
        const uint32_t expected = type_alignment_log2(types, type);
        const uint64_t mask = (UINT64_C(1) << expected) - 1;
        if (((offset / 8) & mask) != 0 || complete_alignment < expected)
        {
            const uint32_t member_alignment = type_alignment_log2(types, types->members.type[packed_member]);
            const uint32_t record_alignment = type_alignment_log2(types, types->tags.type[packed_record]);
            const uint8_t alignment = (uint8_t)(member_alignment < record_alignment ? member_alignment : record_alignment);
            uint32_t parent = parser->tree.parents[address];
            while (parent < parser->tokens.count &&
                   ((parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP && reader->output->expression_regions[parent] == 0) ||
                    (parser->tree.roles[parent] == PARSE_ROLE_PREFIX && parser->tokens.kinds[parent] == PARSE_KIND_EXTENSION)))
            {
                parent = parser->tree.parents[parent];
            }
            TypeId target = parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CAST ? reader->query_types[parent] : 0;
            if (parent < parser->tokens.count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_CAST && target == 0)
            {
                const uint32_t opening = reader->first_child[parent];
                if (opening < parent && parser->tokens.kinds[opening] == PARSE_KIND_L_PAREN)
                {
                    target = check_input_constant_type(reader, opening + 1, parent);
                }
            }
            const uint32_t name = types->members.name[packed_member];
            const TypeId field_type = type_strip_aligned(types, types->members.type[packed_member]);
            const String8 member_name = name != 0 ? check_input_packed_identifier(reader, name) :
                check_input_packed_name(reader, types->types.index[type_index(field_type)], true, chain);
            const String8 record_name = check_input_packed_name(reader, packed_record, false, chain);
            reader->output->packed_members[reader->output->packed_member_count++] = (CheckInputPackedMember)
            {
                .address = address,
                .token = token,
                .member = packed_member,
                .record = packed_record,
                .member_name = member_name,
                .record_name = record_name,
                .alignment_log2 = alignment,
                .suppressed = check_input_packed_target(types, target, alignment),
            };
        }
    }
}

static bool check_input_packed_any(const uint8_t* bytes, uint32_t first, uint32_t count, uint8_t bits, bool scalar)
{
    bool found = false;
    for (uint32_t i = first; !found && i < count;)
    {
        if (scalar)
        {
            found = (bytes[i] & bits) != 0;
            i += 1;
        }
        else
        {
            const uint32_t width = count - i < 64 ? count - i : 64;
            const __mmask64 valid = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            const __m512i values = _mm512_maskz_loadu_epi8(valid, bytes + i);
            found = (_mm512_test_epi8_mask(values, _mm512_set1_epi8((char)bits)) & valid) != 0;
            i += width;
        }
    }
    return found;
}

static uint64_t check_input_packed_candidates(const Parser* parser, uint32_t first, uint32_t width)
{
    uint64_t result = 0;
    if (parser->options.scalar)
    {
        for (uint32_t i = 0; i < width; i += 1)
        {
            const bool address = parser->tokens.kinds[first + i] == PARSE_KIND_AMP && parser->tree.roles[first + i] == PARSE_ROLE_PREFIX;
            result |= (uint64_t)address << i;
        }
    }
    else
    {
        const __mmask64 valid = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, parser->tokens.kinds + first);
        const __m512i roles = _mm512_maskz_loadu_epi8(valid, parser->tree.roles + first);
        result = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(PARSE_KIND_AMP)) &
                 _mm512_cmpeq_epi8_mask(roles, _mm512_set1_epi8(PARSE_ROLE_PREFIX));
    }
    return result;
}

static void check_input_packed_members(CheckInputReader* reader)
{
    const uint32_t count = reader->parser->tokens.count;
    const TypeTable* types = reader->types;
    const bool scalar = reader->parser->options.scalar;
    const bool packed = check_input_packed_any(types->tags.packed, 1, types->tags.count, UINT8_MAX, scalar) ||
                        check_input_packed_any(types->members.flags, 1, types->members.count, BUSTER_TYPE_MEMBER_PACKED, scalar);
    const uint64_t mark = reader->scratch->length;
    uint32_t candidates = 0;
    for (uint32_t token = 0; packed && token < count; token += 64)
    {
        const uint32_t width = count - token < 64 ? count - token : 64;
        candidates += (uint32_t)__builtin_popcountll(check_input_packed_candidates(reader->parser, token, width));
    }
    if (candidates != 0)
    {
        reader->output->packed_members = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputPackedMember, candidates);
        uint32_t* chain = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, count);
        reader->output->resource |= reader->output->packed_members == 0 || chain == 0;
        const uint32_t end = count < reader->expression_limit ? count : reader->expression_limit;
        for (uint32_t token = 0; !reader->output->resource && token < end; token += 64)
        {
            const uint32_t width = end - token < 64 ? end - token : 64;
            uint64_t addresses = check_input_packed_candidates(reader->parser, token, width);
            while (addresses != 0)
            {
                const uint32_t address = token + (uint32_t)__builtin_ctzll(addresses);
                check_input_packed_address(reader, address, chain);
                addresses &= addresses - 1;
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}
