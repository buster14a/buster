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
    // One definition's attributes the other hasn't, or the same with other arguments: clang's -Wodr error, its notes at
    // the first attribute where the lists differ, each definition's that has one there.
    CHECK_TAG_MISMATCH_ATTRIBUTE,
    // The same of a member's attributes: clang's -Wodr error of "a member with an attribute", the comparison going on.
    CHECK_TAG_MISMATCH_MEMBER_ATTRIBUTE,
    // (7 and 8 are an enum's, c_check_enum_input.c.) An unnamed member record of the earlier definition and the second's
    // in its place are a struct and a union (#1826): clang's error is the second's, noted at the earlier "is a struct here".
    CHECK_TAG_MISMATCH_KIND = 9,
} CheckTagMismatch;

// Report flags in CheckInputReport.bit_widths beside the widths: the first note's member is unnamed, and the second's.
#define BUSTER_CHECK_TAG_FIRST_UNNAMED 0x8000u
#define BUSTER_CHECK_TAG_SECOND_UNNAMED 0x80000000u

// Whether two attributes are the same, arguments included: clang compares each attribute's kind and arguments.
static bool check_tag_same_attribute(const CheckInputAttribute* left, const CheckInputAttribute* right)
{
    bool result = left->kind == right->kind && left->name == right->name && left->value == right->value &&
                  left->argument_count == right->argument_count && left->text.length == right->text.length &&
                  (left->text.length == 0 || memcmp(left->text.pointer, right->text.pointer, left->text.length) == 0);
    for (uint32_t i = 0; result && left->argument_values != NULL && right->argument_values != NULL && i < left->argument_count; i += 1)
    {
        result = left->argument_values[i] == right->argument_values[i];
    }
    return result;
}

// The first position where two attribute lists differ, or BUSTER_CHECK_NONE where they are the same attributes in order.
static uint32_t check_tag_attributes_differ(const CheckInputAttributes* left, const CheckInputAttributes* right)
{
    const uint32_t left_count = left != NULL ? left->count : 0;
    const uint32_t right_count = right != NULL ? right->count : 0;
    uint32_t i = 0;
    while (i < left_count && i < right_count && check_tag_same_attribute(left->records + i, right->records + i))
    {
        i += 1;
    }
    return i == left_count && i == right_count ? BUSTER_CHECK_NONE : i;
}

// A pair of records compared: the earlier definition's tag and the second's, and each one's keyword.
typedef struct CheckTagPair CheckTagPair;
struct CheckTagPair
{
    uint32_t left_tag;
    uint32_t right_tag;
    uint32_t left_keyword;
    uint32_t right_keyword;
};

// The pairs a comparison has to make, breadth first as clang's structural equivalence queues them, and its scratch.
typedef struct CheckTagQueue CheckTagQueue;
struct CheckTagQueue
{
    CheckTagPair* pairs;
    uint32_t count;
    uint32_t capacity;
    TypeId* pending;
    uint32_t pending_capacity;
    // The two definitions' bodies, `{` to `}`: a record whose own body is inside one is written in it.
    uint32_t bodies[2][2];
    // The last walk met the second definition's record that is an incompatible C23 redefinition itself: clang's member
    // of an invalid declaration's type is an int.
    bool invalid;
    // The last walk met an unnamed record of the earlier definition whose counterpart is of the other kind: their keywords,
    // or BUSTER_CHECK_NONE (CHECK_TAG_MISMATCH_KIND).
    uint32_t kind_left;
    uint32_t kind_right;
    bool kind_union;
};

// A record's keyword from its body's `}`, a token before it: the struct or union keyword before the body's `{`.
static uint32_t check_tag_keyword_of(const Parser* parser, uint32_t close)
{
    uint32_t keyword = close < parser->tokens.count && parser->tokens.kinds[close] == PARSE_KIND_R_BRACE ? parser->tree.parents[close] : BUSTER_CHECK_NONE;
    // A record after a specifier that heads its subtree (`_Atomic struct T { … }`, #1826): the body's parent is the
    // specifier, and the keyword the first after it.
    const bool headed = keyword != BUSTER_CHECK_NONE && parser->tokens.kinds[keyword] != PARSE_KIND_L_BRACE;
    uint32_t first = headed ? keyword : close;
    while (first < close && parser->tokens.kinds[first] != PARSE_KIND_STRUCT)
    {
        first += 1;
    }
    keyword = first < close ? first : keyword;
    for (uint32_t steps = 0; keyword != BUSTER_CHECK_NONE && keyword != 0 && steps < 256 && parser->tokens.kinds[keyword] != PARSE_KIND_STRUCT; steps += 1)
    {
        keyword -= 1;
        keyword = parser->tokens.kinds[keyword] == PARSE_KIND_R_PAREN && parser->tree.parents[keyword] < keyword ? parser->tree.parents[keyword] : keyword;
    }
    return keyword != BUSTER_CHECK_NONE && parser->tokens.kinds[keyword] == PARSE_KIND_STRUCT ? keyword : BUSTER_CHECK_NONE;
}

// The keyword of an unnamed record written inside one of the definitions' bodies (`side`), a member's type's own
// definition there; or BUSTER_CHECK_NONE for any other record: a named one, or an unnamed one written elsewhere (a
// typedef's), which clang's C23 comparison takes as compatible with any other such record.
static uint32_t check_tag_written(const CheckInputReader* reader, const CheckTagQueue* queue, uint32_t side, uint32_t tag)
{
    const uint32_t completion = reader->types->tags.name[tag] == 0 ? reader->tag_completions[tag] : BUSTER_CHECK_NONE;
    const bool inside = completion != BUSTER_CHECK_NONE && completion != 0 && completion - 1 > queue->bodies[side][0] && completion - 1 < queue->bodies[side][1];
    return inside ? check_tag_keyword_of(reader->parser, completion - 1) : BUSTER_CHECK_NONE;
}

// Whether two members' types are compatible as clang's C23 comparison of two definitions takes them: walked together
// through pointers, arrays, atomics and functions (the same at each level, qualifiers too), two records are compatible
// unless one is unnamed and written in its definition, whose pair is queued to be compared member by member after this
// one; any other pair of types is type_compatible's answer. An explicit stack holds the pairs still to walk.
static bool check_tag_member_types(const CheckInputReader* reader, CheckTagQueue* queue, TypeId left, TypeId right)
{
    const TypeTable* types = reader->types;
    uint32_t count = 1;
    queue->pending[0] = left;
    queue->pending[1] = right;
    queue->invalid = false;
    queue->kind_left = BUSTER_CHECK_NONE;
    queue->kind_right = BUSTER_CHECK_NONE;
    bool result = true;
    while (result && count != 0)
    {
        count -= 1;
        const TypeId a = type_strip_aligned(types, queue->pending[count * 2]);
        const TypeId b = type_strip_aligned(types, queue->pending[count * 2 + 1]);
        const TypeId ua = type_unqualified(a);
        const TypeId ub = type_unqualified(b);
        const uint8_t a_kind = type_kind(types, ua);
        const uint8_t b_kind = type_kind(types, ub);
        const bool records = (a_kind == BUSTER_TYPE_KIND_STRUCT || a_kind == BUSTER_TYPE_KIND_UNION) &&
                             (b_kind == BUSTER_TYPE_KIND_STRUCT || b_kind == BUSTER_TYPE_KIND_UNION);
        const bool wrapper = a_kind == b_kind && (a_kind == BUSTER_TYPE_KIND_POINTER || a_kind == BUSTER_TYPE_KIND_ATOMIC || a_kind == BUSTER_TYPE_KIND_ARRAY ||
                                                  a_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || a_kind == BUSTER_TYPE_KIND_FUNCTION);
        const uint32_t b_record = records ? types->types.index[type_index(ub)] : 0;
        const uint32_t b_completion = records ? reader->tag_completions[b_record] : BUSTER_CHECK_NONE;
        const uint32_t b_record_keyword = b_completion != BUSTER_CHECK_NONE && b_completion != 0 ? check_tag_keyword_of(reader->parser, b_completion - 1) : BUSTER_CHECK_NONE;
        bool failed = false;
        for (uint32_t f = 0; records && b_record_keyword != BUSTER_CHECK_NONE && f < reader->c23_failed_count; f += 1)
        {
            failed |= reader->c23_failed[f] == b_record_keyword;
        }
        queue->invalid |= failed;
        if (failed)
        {
            result = false;
        }
        else if (records)
        {
            const uint32_t a_tag = types->types.index[type_index(ua)];
            const uint32_t b_tag = b_record;
            const uint32_t a_written = check_tag_written(reader, queue, 0, a_tag);
            const uint32_t b_written = check_tag_written(reader, queue, 1, b_tag);
            const bool compared = a_written != BUSTER_CHECK_NONE || b_written != BUSTER_CHECK_NONE;
            result = type_qualifiers(a) == type_qualifiers(b) && (!compared || (a_kind == b_kind && queue->count < queue->capacity));
            // clang's structural comparison of the two records reports their kinds' mismatch itself, at the second's.
            const bool kinds = type_qualifiers(a) == type_qualifiers(b) && a_written != BUSTER_CHECK_NONE && b_written != BUSTER_CHECK_NONE && a_kind != b_kind;
            queue->kind_left = kinds ? a_written : queue->kind_left;
            queue->kind_right = kinds ? b_written : queue->kind_right;
            queue->kind_union = kinds ? a_kind == BUSTER_TYPE_KIND_UNION : queue->kind_union;
            if (result && compared)
            {
                // A named record compared with an unnamed one is where its own body is.
                const uint32_t a_completion = reader->tag_completions[a_tag];
                const uint32_t b_completion = reader->tag_completions[b_tag];
                const uint32_t a_keyword = a_written != BUSTER_CHECK_NONE ? a_written : a_completion != BUSTER_CHECK_NONE && a_completion != 0 ?
                                           check_tag_keyword_of(reader->parser, a_completion - 1) : BUSTER_CHECK_NONE;
                const uint32_t b_keyword = b_written != BUSTER_CHECK_NONE ? b_written : b_completion != BUSTER_CHECK_NONE && b_completion != 0 ?
                                           check_tag_keyword_of(reader->parser, b_completion - 1) : BUSTER_CHECK_NONE;
                queue->pairs[queue->count++] = (CheckTagPair){ .left_tag = a_tag, .right_tag = b_tag, .left_keyword = a_keyword, .right_keyword = b_keyword };
            }
        }
        else if (wrapper)
        {
            const uint32_t a_index = type_index(ua);
            const uint32_t b_index = type_index(ub);
            uint32_t a_parameters = 0;
            uint32_t b_parameters = 0;
            const TypeId* a_list = a_kind == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, ua, &a_parameters) : NULL;
            const TypeId* b_list = a_kind == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(types, ub, &b_parameters) : NULL;
            const bool shaped = type_qualifiers(a) == type_qualifiers(b) && a_parameters == b_parameters &&
                                (a_kind != BUSTER_TYPE_KIND_ARRAY || types->types.extent[a_index] == types->types.extent[b_index]) &&
                                (a_kind != BUSTER_TYPE_KIND_FUNCTION || types->types.flags[a_index] == types->types.flags[b_index]);
            result = shaped && count + 1 + a_parameters <= queue->pending_capacity;
            for (uint32_t p = 0; result && p < a_parameters; p += 1)
            {
                queue->pending[count * 2] = a_list[p];
                queue->pending[count * 2 + 1] = b_list[p];
                count += 1;
            }
            if (result)
            {
                queue->pending[count * 2] = types->types.base[a_index] | (type_kind_is_array(a_kind) ? type_qualifiers(a) : 0);
                queue->pending[count * 2 + 1] = types->types.base[b_index] | (type_kind_is_array(a_kind) ? type_qualifiers(b) : 0);
                count += 1;
            }
        }
        else
        {
            result = type_compatible(types, queue->pending[count * 2], queue->pending[count * 2 + 1]) == TYPE_ANSWER_YES;
        }
    }
    return result;
}

// A member's type as clang's note spells it where its record is an incompatible C23 redefinition itself (#1826,
// CheckTagQueue.invalid): that record, an invalid declaration's type, is an int, and the pointers, atomics and arrays
// around it stay (`_Atomic(int) *`). Another shape around it is an int alone, as before.
static TypeId check_tag_invalid_member_type(CheckInputReader* reader, TypeId type)
{
    TypeTable* types = reader->types;
    TypeId levels[16];
    uint32_t depth = 0;
    TypeId at = type_strip_aligned(types, type);
    uint8_t kind = type_kind(types, type_unqualified(at));
    while (depth < BUSTER_ARRAY_LENGTH(levels) && (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_ARRAY))
    {
        levels[depth++] = at;
        at = type_strip_aligned(types, types->types.base[type_index(type_unqualified(at))]);
        kind = type_kind(types, type_unqualified(at));
    }
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    TypeId result = record ? BUSTER_TYPE_INT | type_qualifiers(at) : BUSTER_TYPE_INT;
    for (uint32_t level = depth; record && result != 0 && level-- > 0;)
    {
        const TypeId outer = levels[level];
        const uint32_t index = type_index(type_unqualified(outer));
        const uint8_t outer_kind = type_kind(types, type_unqualified(outer));
        const TypeId built = outer_kind == BUSTER_TYPE_KIND_POINTER ? type_pointer(types, result) :
                             outer_kind == BUSTER_TYPE_KIND_ATOMIC ? type_atomic(types, result) : type_array(types, result, types->types.extent[index]);
        result = built != 0 ? built | type_qualifiers(outer) : 0;
    }
    reader->output->resource |= result == 0;
    return result != 0 ? result : BUSTER_TYPE_INT;
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

// The token of a record body's `index`-th unnamed member from `open`, its `{`: an unnamed bit-field's declaration's first
// token, or an unnamed member record's keyword (`struct { int x; };`), in the body's order; BUSTER_CHECK_NONE where there's
// none.
static uint32_t check_tag_unnamed_token(const Parser* parser, uint32_t open, uint32_t index)
{
    const uint32_t close = open < parser->tokens.count && parser->tokens.kinds[open] == PARSE_KIND_L_BRACE ? parser->tree.parents[open] : open;
    uint32_t result = BUSTER_CHECK_NONE;
    uint32_t seen = 0;
    uint32_t start = open + 1;
    for (uint32_t t = open + 1; result == BUSTER_CHECK_NONE && t < close; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint32_t matched = (kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE) && parser->tree.parents[t] > t &&
                                 parser->tree.parents[t] < close ? parser->tree.parents[t] : t;
        const bool record = kind == PARSE_KIND_L_BRACE && matched != t && matched + 1 < close && parser->tokens.kinds[matched + 1] == PARSE_KIND_SEMI;
        const bool bit_field = kind == PARSE_KIND_COLON && t > start && parser->tokens.kinds[t - 1] != PARSE_KIND_IDENTIFIER;
        const uint32_t found = record ? check_tag_keyword_of(parser, matched) : bit_field ? start : BUSTER_CHECK_NONE;
        result = found != BUSTER_CHECK_NONE && seen == index ? found : BUSTER_CHECK_NONE;
        seen += found != BUSTER_CHECK_NONE ? 1u : 0u;
        start = kind == PARSE_KIND_SEMI ? t + 1 : start;
        t = matched;
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

// A member's token in a record's body from `open`: a named member's name, or an unnamed one's place
// (check_tag_unnamed_token), the `index`-th member of the tag's list from `begin`.
static uint32_t check_tag_member_at(const CheckInputReader* reader, uint32_t open, uint32_t begin, uint32_t index)
{
    const TypeTable* types = reader->types;
    const uint32_t name = types->members.name[begin + index];
    uint32_t unnamed = 0;
    for (uint32_t m = 0; name == 0 && m < index; m += 1)
    {
        unnamed += types->members.name[begin + m] == 0 ? 1u : 0u;
    }
    return open >= reader->parser->tokens.count ? BUSTER_CHECK_NONE : name != 0 ? check_tag_member_token(reader, open, name) :
           check_tag_unnamed_token(reader->parser, open, unnamed);
}

// A recovered CHECK_INPUT_TAG_INCOMPATIBLE at `token` (a record's name, or an unnamed one's keyword), its two notes'
// tokens, the first's member type and the second's, and its widths and flags. Whether the reader goes on.
static bool check_tag_report(CheckInputReader* reader, uint32_t token, CheckTagMismatch mismatch, uint32_t first, uint32_t second, TypeId first_type,
                             TypeId second_type, uint32_t bit_widths, uint32_t close)
{
    const uint32_t before = reader->output->report_count;
    const bool placed = token < reader->parser->tokens.count;
    if (placed)
    {
        check_input_report(reader, token, CHECK_INPUT_TAG_INCOMPATIBLE);
    }
    if (reader->output->report_count == before + 1 && !reader->output->resource)
    {
        CheckInputReport* report = reader->output->reports + before;
        report->detail = mismatch;
        report->attribute = first;
        report->note_token = second;
        report->note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
        report->type = first_type;
        report->value = second_type;
        report->bit_widths = bit_widths;
        // Each of a definition's errors at its name is clang's, the -Wodr ones before its incompatibility's, published where
        // its body ends, after what its members' own declarations report (an inner record's redefinition among them).
        report->repeated = true;
        report->sort = close < reader->parser->tokens.count ? close : 0;
    }
    return !placed || (reader->output->report_count == before + 1 && check_input_recover_reports(reader, before));
}

// clang's -Wodr at `token` (c_check_diagnostic.c), a CheckWarningSeverity: BUSTER_CHECK_ODR_WARNING, at least
// BUSTER_CHECK_ODR_ERROR, or ignored.
#define BUSTER_CHECK_ODR_WARNING 1u
#define BUSTER_CHECK_ODR_ERROR 2u
static uint32_t check_input_odr_severity(const CheckInput* input, uint32_t token);

// The attribute lists' first difference (check_tag_attributes_differ) reported at `token` as clang's -Wodr diagnostic, its
// notes at each list's attribute there: the earlier definition's, then the second's. An error (by default) is reported and
// makes the definitions incompatible (`*odr`); a warning is a warning event, after which clang's comparison goes on and a
// definition compatible otherwise declares the earlier type; an ignored one is nothing. Whether the reader goes on.
static bool check_tag_report_attributes(CheckInputReader* reader, uint32_t token, CheckTagMismatch mismatch, const CheckInputAttributes* left,
                                        const CheckInputAttributes* right, uint32_t at, uint32_t close, bool* odr)
{
    const uint32_t first = left != NULL && at < left->count ? left->records[at].name_token : BUSTER_CHECK_NONE;
    const uint32_t second = right != NULL && at < right->count ? right->records[at].name_token : BUSTER_CHECK_NONE;
    const uint32_t severity = check_input_odr_severity(reader->output, token);
    bool result = true;
    if (severity >= BUSTER_CHECK_ODR_ERROR)
    {
        *odr = true;
        result = check_tag_report(reader, token, mismatch, first, second, 0, 0, 0, close);
    }
    else if (severity == BUSTER_CHECK_ODR_WARNING && token < reader->parser->tokens.count)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_ODR,
            .kind = CHECK_WARNING_EVENT_ODR_ATTRIBUTE,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = close < reader->parser->tokens.count ? close : token,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = mismatch == CHECK_TAG_MISMATCH_MEMBER_ATTRIBUTE },
        };
        CheckWarningNote notes[2];
        uint32_t note_count = 0;
        const uint32_t tokens[2] = { first, second };
        for (uint32_t n = 0; n < 2; n += 1)
        {
            if (tokens[n] < reader->parser->tokens.count)
            {
                notes[note_count++] = (CheckWarningNote){
                    .kind = CHECK_WARNING_NOTE_ATTRIBUTE_HERE,
                    .token = tokens[n],
                    .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                    .payload = { .name = reader->parser->tokens.names[tokens[n]], .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                };
            }
        }
        check_input_warning_event(reader, &event, notes, note_count);
    }
    return result;
}

// Remembers a C23 member's attributes at its name's token, for a redefinition's comparison. Whether there was room.
static bool check_tag_member_attributes_note(CheckInputReader* reader, uint32_t token, CheckInputAttributes* attributes)
{
    bool room = reader->c23_member_count < reader->c23_member_capacity;
    if (!room)
    {
        const uint32_t capacity = reader->c23_member_capacity == 0 ? 16 : reader->c23_member_capacity * 2;
        uint32_t* tokens = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        CheckInputAttributes** lists = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributes*, capacity);
        room = tokens != NULL && lists != NULL;
        if (room && reader->c23_member_count != 0)
        {
            memcpy(tokens, reader->c23_member_tokens, (uint64_t)reader->c23_member_count * sizeof(uint32_t));
            memcpy(lists, reader->c23_member_attributes, (uint64_t)reader->c23_member_count * sizeof(CheckInputAttributes*));
        }
        reader->c23_member_tokens = room ? tokens : reader->c23_member_tokens;
        reader->c23_member_attributes = room ? lists : reader->c23_member_attributes;
        reader->c23_member_capacity = room ? capacity : reader->c23_member_capacity;
    }
    if (room)
    {
        reader->c23_member_tokens[reader->c23_member_count] = token;
        reader->c23_member_attributes[reader->c23_member_count] = attributes;
        reader->c23_member_count += 1;
    }
    reader->output->resource |= !room;
    return room;
}

// Remembers an incompatible C23 redefinition's keyword (CheckInputReader.c23_failed). Whether there was room.
static bool check_tag_failed_note(CheckInputReader* reader, uint32_t keyword)
{
    bool room = reader->c23_failed_count < reader->c23_failed_capacity;
    if (!room)
    {
        const uint32_t capacity = reader->c23_failed_capacity == 0 ? 8 : reader->c23_failed_capacity * 2;
        uint32_t* keywords = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, capacity);
        room = keywords != NULL;
        if (room && reader->c23_failed_count != 0)
        {
            memcpy(keywords, reader->c23_failed, (uint64_t)reader->c23_failed_count * sizeof(uint32_t));
        }
        reader->c23_failed = room ? keywords : reader->c23_failed;
        reader->c23_failed_capacity = room ? capacity : reader->c23_failed_capacity;
    }
    if (room)
    {
        reader->c23_failed[reader->c23_failed_count++] = keyword;
    }
    reader->output->resource |= !room;
    return room;
}

// A C23 member's attributes, by its name's token (check_tag_member_attributes_note), or NULL.
static const CheckInputAttributes* check_tag_member_attributes(const CheckInputReader* reader, uint32_t token)
{
    const CheckInputAttributes* result = NULL;
    for (uint32_t i = 0; result == NULL && i < reader->c23_member_count; i += 1)
    {
        result = reader->c23_member_tokens[i] == token ? reader->c23_member_attributes[i] : NULL;
    }
    return result;
}

// The completed record `type` of a definition at `keyword`, compared with the earlier record it redefines in C23, as
// clang's structural equivalence compares them: the records' attributes, then each pair of members in order, their
// attributes (each difference clang's -Wodr error, the comparison going on), names, bit-fields' widths and types
// (check_tag_member_types), and past the shorter list the longer one's next member; the first difference is clang's
// "incompatible definitions", and ends the comparison. The unnamed records written in members' types are compared the
// same way after, breadth first, each pair's difference reported at the second definition's unnamed record. A compatible
// definition makes the tag's own type the earlier one, which its declarators take; an incompatible one keeps its own.
// Whether the reader goes on.
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
        const uint32_t name = types->tags.name[old_tag];
        uint32_t new_open = BUSTER_CHECK_NONE;
        uint32_t old_open = BUSTER_CHECK_NONE;
        const uint32_t new_name = check_tag_name_token(parser, keyword, name, &new_open);
        const uint32_t old_name = earlier_keyword < parser->tokens.count ? check_tag_name_token(parser, earlier_keyword, name, &old_open) : BUSTER_CHECK_NONE;
        // Room for every record's pair once, and for every member's walk.
        const uint64_t mark = reader->scratch->length;
        CheckTagQueue queue = { .capacity = types->tags.count + 1, .pending_capacity = types->members.count + types->parameters.count + 2,
                               .kind_left = BUSTER_CHECK_NONE, .kind_right = BUSTER_CHECK_NONE };
        queue.pairs = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckTagPair, queue.capacity);
        queue.pending = queue.pairs != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, TypeId, (uint64_t)queue.pending_capacity * 2) : NULL;
        valid = queue.pending != NULL;
        reader->output->resource |= !valid;
        const uint32_t opens[2] = { old_open, new_open };
        for (uint32_t side = 0; side < 2; side += 1)
        {
            const bool body = opens[side] < parser->tokens.count && parser->tokens.kinds[opens[side]] == PARSE_KIND_L_BRACE;
            queue.bodies[side][0] = body ? opens[side] : 0;
            queue.bodies[side][1] = body ? parser->tree.parents[opens[side]] : 0;
        }
        if (valid)
        {
            queue.pairs[0] = (CheckTagPair){ .left_tag = old_tag, .right_tag = new_tag, .left_keyword = earlier_keyword, .right_keyword = keyword };
            queue.count = 1;
        }
        bool compatible = valid;
        bool odr = false;
        for (uint32_t q = 0; valid && compatible && q < queue.count; q += 1)
        {
            const CheckTagPair pair = queue.pairs[q];
            const bool top = q == 0;
            // The second definition's record: its name for the redefinition's own pair, else its keyword.
            const uint32_t at = top ? (new_name < parser->tokens.count ? new_name : keyword) : pair.right_keyword;
            uint32_t left_open = top ? old_open : BUSTER_CHECK_NONE;
            uint32_t right_open = top ? new_open : BUSTER_CHECK_NONE;
            if (!top)
            {
                (void)check_tag_name_token(parser, pair.left_keyword, 0, &left_open);
                (void)check_tag_name_token(parser, pair.right_keyword, 0, &right_open);
            }
            const CheckInputAttributes* left_attributes = pair.left_keyword < parser->tokens.count ? check_input_attributes_at(reader->output, pair.left_keyword) : NULL;
            const CheckInputAttributes* right_attributes = top ? attributes : pair.right_keyword < parser->tokens.count ?
                                                           check_input_attributes_at(reader->output, pair.right_keyword) : NULL;
            const uint32_t differ = check_tag_attributes_differ(left_attributes, right_attributes);
            if (differ != BUSTER_CHECK_NONE && at < parser->tokens.count)
            {
                valid = check_tag_report_attributes(reader, at, CHECK_TAG_MISMATCH_ATTRIBUTE, left_attributes, right_attributes, differ, queue.bodies[1][1], &odr);
            }
            const uint32_t old_begin = types->tags.member_begin[pair.left_tag];
            const uint32_t new_begin = types->tags.member_begin[pair.right_tag];
            const uint32_t old_count = types->tags.member_count[pair.left_tag];
            const uint32_t new_count = types->tags.member_count[pair.right_tag];
            const uint32_t shorter = old_count < new_count ? old_count : new_count;
            for (uint32_t m = 0; valid && compatible && m < shorter; m += 1)
            {
                const uint32_t old_member = check_tag_member_at(reader, left_open, old_begin, m);
                const uint32_t new_member = check_tag_member_at(reader, right_open, new_begin, m);
                const CheckInputAttributes* old_list = old_member < parser->tokens.count ? check_tag_member_attributes(reader, old_member) : NULL;
                const CheckInputAttributes* new_list = new_member < parser->tokens.count ? check_tag_member_attributes(reader, new_member) : NULL;
                const uint32_t member_differ = check_tag_attributes_differ(old_list, new_list);
                if (member_differ != BUSTER_CHECK_NONE && at < parser->tokens.count)
                {
                    valid = check_tag_report_attributes(reader, at, CHECK_TAG_MISMATCH_MEMBER_ATTRIBUTE, old_list, new_list, member_differ, queue.bodies[1][1], &odr);
                }
                const uint32_t old_width = types->members.bit_width[old_begin + m];
                const uint32_t new_width = types->members.bit_width[new_begin + m];
                const bool named = types->members.name[old_begin + m] == types->members.name[new_begin + m];
                const bool widths = old_width == new_width;
                compatible = named && widths &&
                             check_tag_member_types(reader, &queue, types->members.type[old_begin + m], types->members.type[new_begin + m]);
                if (valid && !compatible && named && widths && queue.kind_right != BUSTER_CHECK_NONE)
                {
                    valid = check_tag_report(reader, queue.kind_right, CHECK_TAG_MISMATCH_KIND, queue.kind_left, BUSTER_CHECK_NONE, 0, queue.kind_union, BUSTER_CHECK_TAG_FIRST_UNNAMED,
                                             queue.bodies[1][1]);
                }
                else if (valid && !compatible)
                {
                    const CheckTagMismatch mismatch = !named ? CHECK_TAG_MISMATCH_NAME : !widths ? CHECK_TAG_MISMATCH_BIT_FIELD : CHECK_TAG_MISMATCH_TYPE;
                    // Each width plus one, 0 for a member that isn't a bit-field: the second definition's in the high half.
                    const bool new_field = new_width == BUSTER_TYPE_NOT_BIT_FIELD || (new_width == 0 && types->members.name[new_begin + m] != 0);
                    const bool old_field = old_width == BUSTER_TYPE_NOT_BIT_FIELD || (old_width == 0 && types->members.name[old_begin + m] != 0);
                    const uint32_t widths_pair = mismatch != CHECK_TAG_MISMATCH_BIT_FIELD ? 0 : (new_field ? 0 : new_width + 1) << 16 | (old_field ? 0 : old_width + 1);
                    const uint32_t flags = (types->members.name[new_begin + m] == 0 ? BUSTER_CHECK_TAG_FIRST_UNNAMED : 0) |
                                           (types->members.name[old_begin + m] == 0 ? BUSTER_CHECK_TAG_SECOND_UNNAMED : 0);
                    const TypeId new_type = queue.invalid ? check_tag_invalid_member_type(reader, types->members.type[new_begin + m]) : types->members.type[new_begin + m];
                    valid = check_tag_report(reader, at, mismatch, new_member, old_member, new_type, types->members.type[old_begin + m], widths_pair | flags,
                                             queue.bodies[1][1]);
                }
            }
            if (valid && compatible && old_count != new_count)
            {
                compatible = false;
                const bool extra_new = new_count > old_count;
                const uint32_t member = extra_new ? check_tag_member_at(reader, right_open, new_begin, shorter) : check_tag_member_at(reader, left_open, old_begin, shorter);
                const TypeId member_type = extra_new ? types->members.type[new_begin + shorter] : types->members.type[old_begin + shorter];
                const uint32_t other = top ? (extra_new ? old_name : new_name) : (extra_new ? pair.left_keyword : pair.right_keyword);
                const bool unnamed = types->members.name[(extra_new ? new_begin : old_begin) + shorter] == 0;
                valid = check_tag_report(reader, at, extra_new ? CHECK_TAG_MISMATCH_EXTRA_NEW : CHECK_TAG_MISMATCH_EXTRA_OLD, member, other, member_type, 0,
                                         unnamed ? BUSTER_CHECK_TAG_FIRST_UNNAMED : 0, queue.bodies[1][1]);
            }
        }
        if (valid && compatible && !odr)
        {
            reader->tag_types[keyword] = earlier;
        }
        else if (valid)
        {
            valid = check_tag_failed_note(reader, keyword);
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}
