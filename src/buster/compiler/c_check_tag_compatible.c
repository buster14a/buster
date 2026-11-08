// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C23's redefinition of a record in its own scope (#1447, N3037): a compatible one redeclares the earlier type, and an
// incompatible one is clang's "type 'struct S' has incompatible definitions" with two notes, at the first difference
// between the definitions' members in order. The second body is read as an anonymous record's
// (check_input_tag_identity), then compared with the earlier record once complete (check_input_tag_compatible). Only a
// unit that defines a record twice in one scope takes this; the comparison and its notes run then alone.

// The first difference between two complete records' members, in clang's order of the checks of each pair of members: their
// names, whether each is a bit-field and its width, then their types; past the shorter list, the longer one's next member.
typedef enum CheckTagMismatch
{
    CHECK_TAG_MISMATCH_NAME,
    CHECK_TAG_MISMATCH_BIT_FIELD,
    CHECK_TAG_MISMATCH_TYPE,
    // The second definition has a member the earlier doesn't, and the reverse.
    CHECK_TAG_MISMATCH_EXTRA_NEW,
    CHECK_TAG_MISMATCH_EXTRA_OLD,
    // One definition's attributes the other hasn't: clang's -Wodr error, its note at the first such attribute's name.
    CHECK_TAG_MISMATCH_ATTRIBUTE,
} CheckTagMismatch;

// Whether two definitions' attribute lists are the same attributes in order (which clang takes as compatible).
static bool check_tag_same_attributes(const CheckInputAttributes* left, const CheckInputAttributes* right)
{
    const uint32_t left_count = left != NULL ? left->count : 0;
    const uint32_t right_count = right != NULL ? right->count : 0;
    bool result = left_count == right_count;
    for (uint32_t i = 0; result && i < left_count; i += 1)
    {
        result = left->records[i].kind == right->records[i].kind && left->records[i].value == right->records[i].value;
    }
    return result;
}

// Whether two record members' types are compatible as C23's tag compatibility takes them: type_compatible's answer, or,
// for two anonymous records of one kind, the same members in order (names, bit-fields' widths and types), checked with an
// explicit stack of the pairs still to compare, bounded by the records' members.
static bool check_tag_member_types(const TypeTable* types, TypeId left, TypeId right, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t capacity = types->members.count + 1;
    TypeId* pending = arena_push_array(scratch, (uint64_t)capacity * 2, sizeof(TypeId), _Alignof(TypeId));
    uint32_t count = 0;
    bool result = pending != NULL;
    if (result)
    {
        pending[0] = left;
        pending[1] = right;
        count = 1;
    }
    while (result && count != 0)
    {
        count -= 1;
        const TypeId a = type_unqualified(type_strip_aligned(types, pending[count * 2]));
        const TypeId b = type_unqualified(type_strip_aligned(types, pending[count * 2 + 1]));
        const uint8_t kind = type_kind(types, a);
        const bool record = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_kind(types, b) == kind;
        const uint32_t a_tag = record ? types->types.index[type_index(a)] : 0;
        const uint32_t b_tag = record ? types->types.index[type_index(b)] : 0;
        const bool anonymous = record && a != b && types->tags.name[a_tag] == 0 && types->tags.name[b_tag] == 0 &&
                               type_is_complete(types, a) && type_is_complete(types, b) &&
                               (type_qualifiers(pending[count * 2]) == type_qualifiers(pending[count * 2 + 1]));
        if (!anonymous)
        {
            result = type_compatible(types, pending[count * 2], pending[count * 2 + 1]) == TYPE_ANSWER_YES;
        }
        else
        {
            const uint32_t a_begin = types->tags.member_begin[a_tag];
            const uint32_t b_begin = types->tags.member_begin[b_tag];
            const uint32_t members = types->tags.member_count[a_tag];
            result = members == types->tags.member_count[b_tag];
            for (uint32_t m = 0; result && m < members; m += 1)
            {
                result = types->members.name[a_begin + m] == types->members.name[b_begin + m] &&
                         types->members.bit_width[a_begin + m] == types->members.bit_width[b_begin + m] && count < capacity;
                if (result)
                {
                    pending[count * 2] = types->members.type[a_begin + m];
                    pending[count * 2 + 1] = types->members.type[b_begin + m];
                    count += 1;
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

// The name token of a record's member called `name` in its body from `open`, its `{`: its first such identifier among the
// body's own declarations (outside nested braces, brackets and parentheses), which isn't a typedef's use. BUSTER_CHECK_NONE
// where there's none.
static uint32_t check_tag_member_token(const CheckInputReader* reader, uint32_t open, uint32_t name)
{
    const Parser* parser = reader->parser;
    const uint32_t close = open < parser->tokens.count && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE ? parser->tree.parents[open] : open;
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t t = open + 1; result == BUSTER_CHECK_NONE && t < close; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool nested = (kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && parser->tree.parents[t] > t &&
                            parser->tree.parents[t] < close;
        const uint32_t declared = kind == PARSE_KIND_IDENTIFIER ? reader->output->declarations[t] : BUSTER_CHECK_NONE;
        const bool typedef_use = declared < reader->output->token_count && declared != t && reader->declaration_kinds[declared] == C_BINDING_TYPEDEF;
        result = kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == name && !typedef_use ? t : BUSTER_CHECK_NONE;
        t = nested ? parser->tree.parents[t] : t;
    }
    return result;
}

// A tag's name token in its definition from `keyword`, its struct or union keyword: the identifier before its body's `{`
// whose name is `name`, past attributes. BUSTER_CHECK_NONE where there's none.
static uint32_t check_tag_name_token(const Parser* parser, uint32_t keyword, uint32_t name, uint32_t* open)
{
    uint32_t result = BUSTER_CHECK_NONE;
    *open = BUSTER_CHECK_NONE;
    for (uint32_t t = keyword + 1; *open == BUSTER_CHECK_NONE && t < parser->tokens.count; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        result = kind == PARSE_KIND_IDENTIFIER && parser->tokens.names[t] == name && result == BUSTER_CHECK_NONE ? t : result;
        *open = kind == PARSE_KIND_L_BRACE ? t : *open;
        t = kind == PARSE_KIND_L_PAREN && parser->tree.parents[t] > t ? parser->tree.parents[t] : t;
        *open = kind == PARSE_KIND_SEMI ? t : *open;
    }
    return result;
}

// The earlier record a C23 redefinition at `keyword` redeclares (check_input_tag_identity), or 0.
static TypeId check_tag_compatible_earlier(const CheckInputReader* reader, uint32_t keyword, uint32_t* earlier_keyword)
{
    TypeId result = 0;
    *earlier_keyword = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; result == 0 && i < reader->c23_count; i += 1)
    {
        result = reader->c23_tags[i] == keyword ? reader->c23_earlier[i] : 0;
        *earlier_keyword = result != 0 ? reader->c23_earlier_keywords[i] : BUSTER_CHECK_NONE;
    }
    return result;
}

// Remembers that the record whose definition starts at `keyword` redefines `earlier`, defined at `earlier_keyword`
// (C23), for its completion. Whether there was room.
static bool check_tag_compatible_note(CheckInputReader* reader, uint32_t keyword, TypeId earlier, uint32_t earlier_keyword)
{
    bool room = reader->c23_count < reader->c23_capacity;
    if (!room)
    {
        const uint32_t capacity = reader->c23_capacity == 0 ? 8 : reader->c23_capacity * 2;
        uint32_t* tags = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        TypeId* types = BUSTER_ARENA_PUSH_ARRAY(reader->arena, TypeId, capacity);
        uint32_t* keywords = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        room = tags != NULL && types != NULL && keywords != NULL;
        if (room && reader->c23_count != 0)
        {
            memcpy(tags, reader->c23_tags, (uint64_t)reader->c23_count * sizeof(uint32_t));
            memcpy(types, reader->c23_earlier, (uint64_t)reader->c23_count * sizeof(TypeId));
            memcpy(keywords, reader->c23_earlier_keywords, (uint64_t)reader->c23_count * sizeof(uint32_t));
        }
        reader->c23_tags = room ? tags : reader->c23_tags;
        reader->c23_earlier = room ? types : reader->c23_earlier;
        reader->c23_earlier_keywords = room ? keywords : reader->c23_earlier_keywords;
        reader->c23_capacity = room ? capacity : reader->c23_capacity;
    }
    if (room)
    {
        reader->c23_tags[reader->c23_count] = keyword;
        reader->c23_earlier[reader->c23_count] = earlier;
        reader->c23_earlier_keywords[reader->c23_count] = earlier_keyword;
        reader->c23_count += 1;
    }
    reader->output->resource |= !room;
    return room;
}

// The completed record `type` of a definition at `keyword`, compared with the earlier record it redefines in C23: a
// compatible one makes the tag's own type the earlier one, which its declarators take; an incompatible one is reported,
// recovered, at its name, the definition keeping its own type. Whether the reader goes on.
static bool check_input_tag_compatible(CheckInputReader* reader, uint32_t keyword, TypeId type, const CheckInputAttributes* attributes)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    uint32_t earlier_keyword = BUSTER_CHECK_NONE;
    const TypeId earlier = check_tag_compatible_earlier(reader, keyword, &earlier_keyword);
    bool valid = true;
    if (earlier != 0)
    {
        const uint32_t old_tag = types->types.index[type_index(earlier)];
        const uint32_t new_tag = types->types.index[type_index(type)];
        const uint32_t old_begin = types->tags.member_begin[old_tag];
        const uint32_t new_begin = types->tags.member_begin[new_tag];
        const uint32_t old_count = types->tags.member_count[old_tag];
        const uint32_t new_count = types->tags.member_count[new_tag];
        const uint32_t shorter = old_count < new_count ? old_count : new_count;
        bool found = false;
        CheckTagMismatch mismatch = CHECK_TAG_MISMATCH_TYPE;
        uint32_t member = 0;
        for (uint32_t m = 0; !found && m < shorter; m += 1)
        {
            const bool named = types->members.name[old_begin + m] == types->members.name[new_begin + m];
            const bool widths = types->members.bit_width[old_begin + m] == types->members.bit_width[new_begin + m];
            const bool typed = named && widths && check_tag_member_types(types, types->members.type[new_begin + m], types->members.type[old_begin + m], reader->scratch);
            found = !named || !widths || !typed;
            mismatch = !named ? CHECK_TAG_MISMATCH_NAME : !widths ? CHECK_TAG_MISMATCH_BIT_FIELD : CHECK_TAG_MISMATCH_TYPE;
            member = m;
        }
        if (!found && old_count != new_count)
        {
            found = true;
            mismatch = new_count > old_count ? CHECK_TAG_MISMATCH_EXTRA_NEW : CHECK_TAG_MISMATCH_EXTRA_OLD;
            member = shorter;
        }
        // Attributes one definition has and the other hasn't come first: clang's -Wodr error, at the first of them.
        const CheckInputAttributes* earlier_attributes = earlier_keyword < parser->tokens.count ? check_input_attributes_at(reader->output, earlier_keyword) : NULL;
        if (!check_tag_same_attributes(attributes, earlier_attributes))
        {
            found = true;
            mismatch = CHECK_TAG_MISMATCH_ATTRIBUTE;
        }
        if (!found)
        {
            reader->tag_types[keyword] = earlier;
        }
        else
        {
            const uint32_t name = types->tags.name[old_tag];
            uint32_t new_open = BUSTER_CHECK_NONE;
            uint32_t old_open = BUSTER_CHECK_NONE;
            const uint32_t new_name = check_tag_name_token(parser, keyword, name, &new_open);
            const uint32_t old_name = earlier_keyword < parser->tokens.count ? check_tag_name_token(parser, earlier_keyword, name, &old_open) : BUSTER_CHECK_NONE;
            const bool from_new = mismatch != CHECK_TAG_MISMATCH_EXTRA_OLD;
            const bool members = mismatch != CHECK_TAG_MISMATCH_ATTRIBUTE;
            const uint32_t new_member = !members || mismatch == CHECK_TAG_MISMATCH_EXTRA_OLD ? BUSTER_CHECK_NONE :
                                        check_tag_member_token(reader, new_open, types->members.name[new_begin + member]);
            const uint32_t old_member = !members || mismatch == CHECK_TAG_MISMATCH_EXTRA_NEW ? BUSTER_CHECK_NONE :
                                        check_tag_member_token(reader, old_open, types->members.name[old_begin + member]);
            const CheckInputAttributes* attributed = attributes != NULL && attributes->count != 0 ? attributes : earlier_attributes;
            const uint32_t attribute = attributed != NULL && attributed->count != 0 ? attributed->records[0].name_token : BUSTER_CHECK_NONE;
            const uint32_t before = reader->output->report_count;
            check_input_report(reader, new_name < parser->tokens.count ? new_name : keyword, CHECK_INPUT_TAG_INCOMPATIBLE);
            if (reader->output->report_count == before + 1 && !reader->output->resource)
            {
                CheckInputReport* report = reader->output->reports + before;
                report->detail = mismatch;
                // The first note's token and type, then the second's: the second definition's member then the earlier's,
                // or a member then "no corresponding field" at the other definition's name.
                report->attribute = !members ? attribute : from_new ? new_member : old_member;
                report->note_token = !members ? BUSTER_CHECK_NONE : mismatch == CHECK_TAG_MISMATCH_EXTRA_NEW ? old_name :
                                     mismatch == CHECK_TAG_MISMATCH_EXTRA_OLD ? new_name : old_member;
                report->note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
                report->type = !members ? 0 : from_new ? types->members.type[new_begin + member] : types->members.type[old_begin + member];
                report->value = mismatch == CHECK_TAG_MISMATCH_NAME || mismatch == CHECK_TAG_MISMATCH_TYPE || mismatch == CHECK_TAG_MISMATCH_BIT_FIELD ?
                                types->members.type[old_begin + member] : 0;
                // Each width plus one, 0 for a member that isn't a bit-field: the second definition's in the high half.
                const uint32_t new_width = types->members.bit_width[new_begin + member];
                const uint32_t old_width = types->members.bit_width[old_begin + member];
                const bool new_field = new_width == BUSTER_TYPE_NOT_BIT_FIELD || new_width == 0;
                const bool old_field = old_width == BUSTER_TYPE_NOT_BIT_FIELD || old_width == 0;
                report->bit_widths = mismatch != CHECK_TAG_MISMATCH_BIT_FIELD ? 0 : (new_field ? 0 : new_width + 1) << 16 | (old_field ? 0 : old_width + 1);
            }
            valid = reader->output->report_count == before + 1 && check_input_recover_reports(reader, before);
        }
    }
    return valid;
}
