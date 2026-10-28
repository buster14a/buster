// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source witnesses for -Waddress-of-packed-member. Only direct member chains rooted in a declaration qualify,
// as in Clang 23's RefersToMemberWithReducedAlignment. Retain the nearest packed field, the whole chain's offset
// and the declaration's alignment; a packed attribute alone does not establish misalignment. The sparse records
// survive sizeof/typeof's discarded graph nodes. c_check_packed_diagnostic.c applies consuming conversions later.

typedef struct CheckInputPackedContexts CheckInputPackedContexts;
struct CheckInputPackedContexts
{
    uint8_t* analyzed;
    uint32_t* selections;
};

typedef struct CheckInputPackedNames CheckInputPackedNames;
struct CheckInputPackedNames
{
    uint32_t* tokens;
    uint32_t* parents;
    uint32_t* ends;
    uint32_t* typedefs;
    uint32_t* stack;
};

// Construct the cold tag-to-source handoff once, on the first anonymous diagnostic. Re-scanning every earlier
// token for each anonymous local would make a file of such functions quadratic. The columns have tag_count logical
// entries (NONE for absent source/context, zero for no associated typedef) and live until this pass releases scratch.
static bool check_input_packed_names(CheckInputReader* reader, CheckInputPackedNames* names)
{
    const uint32_t tags = reader->types->tags.count;
    bool valid = names->tokens != NULL && names->parents != NULL && names->ends != NULL && names->typedefs != NULL && names->stack != NULL;
    if (!valid && !reader->output->resource)
    {
        names->tokens = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tags);
        names->parents = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tags);
        names->ends = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tags);
        names->typedefs = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tags);
        names->stack = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, tags);
        valid = names->tokens != NULL && names->parents != NULL && names->ends != NULL && names->typedefs != NULL && names->stack != NULL;
        reader->output->resource |= !valid;
        if (valid)
        {
            memset(names->tokens, 0xff, (uint64_t)tags * sizeof(uint32_t));
            memset(names->parents, 0xff, (uint64_t)tags * sizeof(uint32_t));
            memset(names->ends, 0xff, (uint64_t)tags * sizeof(uint32_t));
            memset(names->typedefs, 0, (uint64_t)tags * sizeof(uint32_t));
            uint32_t* stack = names->stack;
            uint32_t depth = 0;
            const uint32_t count = reader->parser->tokens.count;
            for (uint32_t first = 0; first < count; first += 16)
            {
                const uint32_t width = count - first < 16 ? count - first : 16;
                uint32_t present = 0;
                if (BUSTER_SCALAR_PATH(reader->parser->options.scalar))
                {
                    for (uint32_t i = 0; i < width; i += 1)
                    {
                        present |= (uint32_t)(reader->tag_types[first + i] != 0) << i;
                    }
                }
                else
                {
                    const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, width);
                    const __m512i values = _mm512_maskz_loadu_epi32(active, reader->tag_types + first);
                    present = _mm512_mask_cmpneq_epi32_mask(active, values, _mm512_setzero_si512());
                }
                while (present != 0)
                {
                    const uint32_t token = first + (uint32_t)__builtin_ctz(present);
                    const TypeId type = reader->tag_types[token];
                    const uint8_t kind = type_kind(reader->types, type);
                    if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                    {
                        const uint32_t tag = reader->types->types.index[type_index(type)];
                        const uint32_t body = check_input_tag_body(reader->parser, token, count);
                        if (names->tokens[tag] == BUSTER_CHECK_NONE && body < count && reader->parser->tokens.kinds[body] == PARSE_KIND_L_BRACE)
                        {
                            while (depth != 0 && names->ends[stack[depth - 1]] < token)
                            {
                                depth -= 1;
                            }
                            names->tokens[tag] = token;
                            names->parents[tag] = depth != 0 ? stack[depth - 1] : BUSTER_CHECK_NONE;
                            names->ends[tag] = reader->parser->tree.parents[body];
                            stack[depth++] = tag;
                        }
                    }
                    present &= present - 1;
                }
            }
            // Field declarators are not ordinary binding declarations. Their zero-initialized kind byte is
            // also TYPEDEF, so only the reader's retained DECLARE events certify an associated typedef name.
            const CBindings* bindings = &reader->bindings;
            for (uint32_t first = 0; first < bindings->count; first += 64)
            {
                const uint32_t width = bindings->count - first < 64 ? bindings->count - first : 64;
                uint64_t selected = 0;
                if (BUSTER_SCALAR_PATH(reader->parser->options.scalar))
                {
                    for (uint32_t i = 0; i < width; i += 1)
                    {
                        selected |= (uint64_t)(bindings->operation[first + i] == C_BINDING_DECLARE && bindings->kind[first + i] == C_BINDING_TYPEDEF) << i;
                    }
                }
                else
                {
                    const __mmask64 active = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                    const __m512i operations = _mm512_maskz_loadu_epi8(active, bindings->operation + first);
                    const __m512i kinds = _mm512_maskz_loadu_epi8(active, bindings->kind + first);
                    selected = _mm512_mask_cmpeq_epi8_mask(active, operations, _mm512_set1_epi8(C_BINDING_DECLARE)) &
                               _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(C_BINDING_TYPEDEF));
                }
                while (selected != 0)
                {
                    const uint32_t event = first + (uint32_t)__builtin_ctzll(selected);
                    const uint32_t token = bindings->declaration[event];
                    const TypeId type = token < count ? type_strip_aligned(reader->types, reader->declared_types[token]) : 0;
                    const uint8_t kind = type_kind(reader->types, type);
                    if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
                    {
                        const uint32_t tag = reader->types->types.index[type_index(type)];
                        bool associated = names->ends[tag] < token && names->typedefs[tag] == 0;
                        for (uint32_t at = names->ends[tag] + 1; associated && at < token; at += 1)
                        {
                            associated = reader->parser->tokens.kinds[at] != PARSE_KIND_SEMI;
                        }
                        if (associated)
                        {
                            names->typedefs[tag] = reader->parser->tokens.names[token];
                        }
                    }
                    selected &= selected - 1;
                }
            }
        }
    }
    return valid;
}

static uint32_t check_input_packed_selection(const CheckInputReader* reader, uint32_t token)
{
    const uint32_t selected = reader->selected_tokens[token];
    return selected != BUSTER_CHECK_NONE ? selected & ~CHECK_INPUT_SOURCE_SELECTION : BUSTER_CHECK_NONE;
}

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
        else if (*first + 1 < *end && parser->tree.roles[*first + 1] == PARSE_ROLE_OPEN_KEYWORD &&
                 parser->tree.parents[*first + 1] == *end - 1 && check_input_packed_selection(reader, *end - 1) != BUSTER_CHECK_NONE)
        {
            const uint32_t selected = check_input_packed_selection(reader, *end - 1);
            *first = check_input_first_token(reader, selected);
            *end = reader->ends[selected] + 1;
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
static String8 check_input_packed_name(CheckInputReader* reader, uint32_t tag, bool field, CheckInputPackedNames* names)
{
    const TypeTable* types = reader->types;
    const Parser* parser = reader->parser;
    String8 result = check_input_packed_identifier(reader, types->tags.name[tag]);
    if (result.length == 0 && check_input_packed_names(reader, names))
    {
        uint32_t* contexts = names->stack;
        const uint32_t token = names->tokens[tag];
        if (!field)
        {
            result = check_input_packed_identifier(reader, names->typedefs[tag]);
        }
        if (result.length == 0 && token < parser->tokens.count)
        {
            uint32_t depth = 0;
            uint64_t capacity = 128;
            for (uint32_t parent = names->parents[tag]; parent != BUSTER_CHECK_NONE && depth < types->tags.count; parent = names->parents[parent])
            {
                contexts[depth++] = parent;
                const uint32_t name = types->tags.name[parent] != 0 ? types->tags.name[parent] : names->typedefs[parent];
                capacity += check_input_packed_identifier(reader, name).length + 32;
            }
            for (uint32_t i = 0; i < depth / 2; i += 1)
            {
                const uint32_t temporary = contexts[i];
                contexts[i] = contexts[depth - 1 - i];
                contexts[depth - 1 - i] = temporary;
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
                    const uint32_t context_name = types->tags.name[context] != 0 ? types->tags.name[context] : names->typedefs[context];
                    const String8 name = check_input_packed_identifier(reader, context_name);
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

static void check_input_packed_address(CheckInputReader* reader, uint32_t address, uint32_t* chain, CheckInputPackedNames* names, const CheckInputPackedContexts* contexts)
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
            const uint32_t index = reader->output->packed_member_count++;
            if (reader->output->packed_members != NULL)
            {
                const uint32_t member_alignment = type_alignment_log2(types, types->members.type[packed_member]);
                const uint32_t record_alignment = type_alignment_log2(types, types->tags.type[packed_record]);
                const uint8_t alignment = (uint8_t)(member_alignment < record_alignment ? member_alignment : record_alignment);
                uint32_t current = address;
                uint32_t parent = parser->tree.parents[current];
                bool transparent = true;
                while (transparent && parent < parser->tokens.count)
                {
                    const bool group = (parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP && reader->output->expression_regions[parent] == 0) ||
                                       (parser->tree.roles[parent] == PARSE_ROLE_PREFIX && parser->tokens.kinds[parent] == PARSE_KIND_EXTENSION);
                    const uint32_t selection = contexts->selections[current];
                    transparent = group || selection != BUSTER_CHECK_NONE;
                    if (transparent)
                    {
                        current = selection != BUSTER_CHECK_NONE ? selection : parent;
                        parent = parser->tree.parents[current];
                    }
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
                    check_input_packed_name(reader, types->types.index[type_index(field_type)], true, names);
                const String8 record_name = check_input_packed_name(reader, packed_record, false, names);
                reader->output->packed_members[index] = (CheckInputPackedMember)
                {
                    .address = address,
                    .token = token,
                    .member = packed_member,
                    .record = packed_record,
                    .member_name = member_name,
                    .record_name = record_name,
                    .alignment_log2 = alignment,
                    .implicit_conversions = contexts->analyzed[address] != 0,
                    .suppressed = check_input_packed_target(types, target, alignment),
                };
            }
        }
    }
}

static bool check_input_packed_any(const uint8_t* bytes, uint32_t first, uint32_t count, uint8_t bits, bool scalar)
{
    (void)scalar;

    bool found = false;
    for (uint32_t i = first; !found && i < count;)
    {
        if (BUSTER_SCALAR_PATH(scalar))
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
    if (BUSTER_SCALAR_PATH(parser->options.scalar))
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

static void check_input_packed_exclude(uint32_t* events, uint32_t count, uint32_t first, uint32_t end)
{
    if (first < end && end <= count)
    {
        events[first] += 1;
        events[end] -= 1;
    }
}

// Inclusive unsigned difference scan, bounded to count entries for both input and output.
static void check_input_packed_analyzed(const uint32_t* events, uint8_t* analyzed, uint32_t count, bool scalar)
{
    (void)scalar;

    uint32_t running = 0;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t width = count - first < 16 ? count - first : 16;
        if (BUSTER_SCALAR_PATH(scalar))
        {
            for (uint32_t i = 0; i < width; i += 1)
            {
                running += events[first + i];
                analyzed[first + i] = running == 0;
            }
        }
        else
        {
            const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, width);
            const __m512i deltas = _mm512_maskz_loadu_epi32(active, events + first);
            const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
            __m512i sums = deltas;
            sums = _mm512_add_epi32(sums, _mm512_maskz_permutexvar_epi32(0xfffe, _mm512_sub_epi32(lanes, _mm512_set1_epi32(1)), sums));
            sums = _mm512_add_epi32(sums, _mm512_maskz_permutexvar_epi32(0xfffc, _mm512_sub_epi32(lanes, _mm512_set1_epi32(2)), sums));
            sums = _mm512_add_epi32(sums, _mm512_maskz_permutexvar_epi32(0xfff0, _mm512_sub_epi32(lanes, _mm512_set1_epi32(4)), sums));
            sums = _mm512_add_epi32(sums, _mm512_maskz_permutexvar_epi32(0xff00, _mm512_sub_epi32(lanes, _mm512_set1_epi32(8)), sums));
            sums = _mm512_add_epi32(sums, _mm512_set1_epi32((int)running));
            const __mmask16 allowed = _mm512_mask_cmpeq_epi32_mask(active, sums, _mm512_setzero_si512());
            _mm_mask_storeu_epi8(analyzed + first, active, _mm_maskz_set1_epi8(allowed, 1));
            running += (uint32_t)_mm512_mask_reduce_add_epi32(active, deltas);
        }
    }
}

// Pending warnings survive unevaluated and unchosen operands, but Clang never performs its implicit-conversion
// analysis there. Build their lexical intervals once and prefix-scan them; a parent walk per address would be
// quadratic in a deep expression. Unsigned difference counts wrap deliberately and have a bounded prefix depth.
static CheckInputPackedContexts check_input_packed_contexts(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t* events = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    uint8_t* analyzed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
    const bool valid = events != NULL && analyzed != NULL;
    reader->output->resource |= !valid;
    if (valid)
    {
        memset(events, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            if (kind == PARSE_KIND_SIZEOF)
            {
                check_input_packed_exclude(events, count, token + 1, reader->ends[token] + 1);
            }
            else if (kind == PARSE_KIND_TYPEOF && token + 1 < count)
            {
                check_input_packed_exclude(events, count, token + 2, parser->tree.parents[token + 1]);
            }
            const uint32_t selected = check_input_packed_selection(reader, token);
            if (selected != BUSTER_CHECK_NONE)
            {
                const uint32_t keyword = reader->first_child[token];
                check_input_packed_exclude(events, count, keyword + 2, check_input_first_token(reader, selected));
                check_input_packed_exclude(events, count, reader->ends[selected] + 1, token);
            }
        }
        check_input_packed_analyzed(events, analyzed, count, BUSTER_SCALAR_PATH(parser->options.scalar));
        // The prefix scan has consumed the difference counts. Reuse them for the inverse selection map:
        // association separators are not expression parents, so a parent walk cannot transparently cross them.
        memset(events, 0xff, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint32_t selected = check_input_packed_selection(reader, token);
            if (selected != BUSTER_CHECK_NONE)
            {
                events[selected] = token;
            }
        }
    }
    return (CheckInputPackedContexts){ .analyzed = analyzed, .selections = events };
}

static bool check_input_packed_storage(CheckInputReader* reader, uint32_t count)
{
    reader->output->packed_members = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputPackedMember, count);
    reader->output->resource |= reader->output->packed_members == NULL;
    return !reader->output->resource;
}

// First determine a chain bound, then count actual layout witnesses without retaining records or formatting names.
// Only matches allocate persistent storage and token-wide conversion context. An unused packed declaration plus
// ordinary &local expressions therefore needs neither records nor chain/context scratch.
static void check_input_packed_members(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count < reader->expression_limit ? parser->tokens.count : reader->expression_limit;
    const TypeTable* types = reader->types;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    const bool packed = check_input_packed_any(types->tags.packed, 1, types->tags.count, UINT8_MAX, scalar) ||
                        check_input_packed_any(types->members.flags, 1, types->members.count, BUSTER_TYPE_MEMBER_PACKED, scalar);
    const uint64_t mark = reader->scratch->length;
    uint32_t capacity = 0;
    for (uint32_t token = 0; packed && token < count; token += 64)
    {
        const uint32_t width = count - token < 64 ? count - token : 64;
        uint64_t addresses = check_input_packed_candidates(parser, token, width);
        while (addresses != 0)
        {
            const uint32_t address = token + (uint32_t)__builtin_ctzll(addresses);
            uint32_t first = address + 1;
            uint32_t end = reader->ends[address] + 1;
            check_input_packed_unwrap(reader, &first, &end);
            if (first + 2 < end && parser->tree.roles[end - 2] == PARSE_ROLE_MEMBER_OPERATOR)
            {
                const uint32_t bound = (end - first) / 2;
                capacity = bound > capacity ? bound : capacity;
            }
            addresses &= addresses - 1;
        }
    }
    if (capacity != 0)
    {
        uint32_t* chain = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, capacity);
        reader->output->resource |= chain == NULL;
        CheckInputPackedContexts contexts = { 0 };
        CheckInputPackedNames names = { 0 };
        for (uint32_t pass = 0; pass < 2 && !reader->output->resource; pass += 1)
        {
            if (pass != 0)
            {
                const uint32_t witnesses = reader->output->packed_member_count;
                reader->output->packed_member_count = 0;
                if (witnesses == 0)
                {
                    break;
                }
                if (check_input_packed_storage(reader, witnesses))
                {
                    contexts = check_input_packed_contexts(reader);
                }
            }
            for (uint32_t token = 0; !reader->output->resource && token < count; token += 64)
            {
                const uint32_t width = count - token < 64 ? count - token : 64;
                uint64_t addresses = check_input_packed_candidates(parser, token, width);
                while (!reader->output->resource && addresses != 0)
                {
                    const uint32_t address = token + (uint32_t)__builtin_ctzll(addresses);
                    check_input_packed_address(reader, address, chain, &names, &contexts);
                    addresses &= addresses - 1;
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}
