// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's counted_by family of attributes on a record's members (#148): counted_by, sized_by, counted_by_or_null and
// sized_by_or_null, whose argument names the member that holds the count of a flexible array member's elements or of
// a pointer's (or its bytes, for the sized_by ones). clang checks them as Sema::CheckCountedByAttrOnField does
// (SemaBoundsSafety.cpp), which this follows: the member's place and type, its pointee's, then the count's. A valid one
// makes the member's type clang's CountAttributedType, which __builtin_counted_by_ref and __builtin_dynamic_object_size
// read: here, TypeMember.count_kind and count_name. Wherever the attribute is written in the member's declaration (its
// specifiers, a declarator chunk, after the declarator), clang applies it to the member, as this does. A rare
// attribute: scalar. Included by c_check_input.c, after CheckTagFrame and CheckTagMember.

static uint32_t check_input_declaration_begin(const Parser* parser, uint32_t name);

// The attribute's kind (BUSTER_TYPE_COUNTED_BY and the others) by its name, or BUSTER_TYPE_COUNT_NONE.
static uint8_t check_input_counted_kind(String8 name)
{
    const uint8_t result = check_input_attribute_equal(name, "counted_by") ? BUSTER_TYPE_COUNTED_BY :
                           check_input_attribute_equal(name, "sized_by") ? BUSTER_TYPE_SIZED_BY :
                           check_input_attribute_equal(name, "counted_by_or_null") ? BUSTER_TYPE_COUNTED_BY_OR_NULL :
                           check_input_attribute_equal(name, "sized_by_or_null") ? BUSTER_TYPE_SIZED_BY_OR_NULL :
                                                                                   BUSTER_TYPE_COUNT_NONE;
    return result;
}

static const char* const check_input_counted_names[] = { "", "counted_by", "sized_by", "counted_by_or_null", "sized_by_or_null" };

// Whether record `record` is one a member being declared in the innermost of `frames` sees the members of: one of the
// frames' records, or an anonymous struct or union member of one it sees (its members are the enclosing record's).
static bool check_input_counted_visible(const TypeTable* types, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                        uint32_t member_count, TypeId record)
{
    TypeId current = record;
    bool result = false;
    bool climbing = true;
    for (uint32_t step = 0; climbing && !result && step < 64; step += 1)
    {
        for (uint32_t d = 0; !result && d < depth; d += 1)
        {
            result = frames[d].type == current;
        }
        // The anonymous member whose type `current` is, which its own record holds.
        uint32_t holder = BUSTER_CHECK_NONE;
        for (uint32_t m = 0; !result && holder == BUSTER_CHECK_NONE && m < member_count; m += 1)
        {
            const TypeMember* member = &members[m].member;
            holder = member->name == 0 && member->bit_width == BUSTER_TYPE_NOT_BIT_FIELD && type_strip_aligned(types, member->type) == current ? m : BUSTER_CHECK_NONE;
        }
        climbing = holder != BUSTER_CHECK_NONE;
        current = climbing ? members[holder].record : current;
    }
    return result;
}

// The member named `name` that a member being declared in the innermost of `frames` sees, the last declared of those
// (an inner record's shadows an outer's), or BUSTER_CHECK_NONE.
static uint32_t check_input_counted_member(const TypeTable* types, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                           uint32_t member_count, uint32_t name)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t m = member_count; result == BUSTER_CHECK_NONE && m-- > 0;)
    {
        result = name != 0 && members[m].member.name == name && check_input_counted_visible(types, frames, depth, members, member_count, members[m].record) ? m : BUSTER_CHECK_NONE;
    }
    return result;
}

// The record clang's GetEnclosingNamedOrTopAnonRecord gives a member of `record`: past unnamed records to the record
// holding them, as far as the frames go.
static TypeId check_input_counted_named(const TypeTable* types, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                        uint32_t member_count, TypeId record)
{
    TypeId current = record;
    bool climbing = true;
    for (uint32_t step = 0; climbing && step < 64; step += 1)
    {
        const uint32_t tag = types->types.index[type_index(current)];
        climbing = types->tags.name[tag] == 0;
        // The record that holds `current`: the frame below its own, or an anonymous member's record.
        TypeId holder = 0;
        for (uint32_t d = 1; climbing && holder == 0 && d < depth; d += 1)
        {
            holder = frames[d].type == current ? frames[d - 1].type : 0;
        }
        for (uint32_t m = 0; climbing && holder == 0 && m < member_count; m += 1)
        {
            const TypeMember* member = &members[m].member;
            holder = member->bit_width == BUSTER_TYPE_NOT_BIT_FIELD && type_strip_aligned(types, member->type) == current ? members[m].record : 0;
        }
        climbing = climbing && holder != 0;
        current = climbing ? holder : current;
    }
    return current;
}

// A non-boolean integer type, as clang's isIntegerType and isBooleanType see a count's.
static bool check_input_counted_integer(const TypeTable* types, TypeId type)
{
    const TypeId stripped = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, stripped);
    return (kind == BUSTER_TYPE_KIND_INTEGER && stripped != BUSTER_TYPE_BOOL) || kind == BUSTER_TYPE_KIND_BIT_INT ||
           (kind == BUSTER_TYPE_KIND_ENUM && type_is_complete(types, stripped));
}

static void check_input_counted_report(CheckInputReader* reader, uint32_t token, CheckInputMessage message, uint32_t detail, TypeId type,
                                       uint32_t note_token)
{
    const uint32_t before = reader->output->report_count;
    check_input_report(reader, token, message);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].detail = detail;
        reader->output->reports[before].type = type;
        reader->output->reports[before].note_token = note_token;
        reader->output->reports[before].note_kind = note_token != BUSTER_CHECK_NONE ? CHECK_INPUT_NOTE_COUNTED_FIELD : CHECK_INPUT_NOTE_NONE;
    }
}

static bool check_input_thread_name(CheckInputReader* reader, uint32_t parameters, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                    uint32_t member_count, uint32_t token);

static bool check_condition_unevaluated_keyword(const Parser* parser, uint32_t token);

// The token past a group that opens at `open` (a `(` or `[`), before `end`.
static uint32_t check_input_counted_group_end(const Parser* parser, uint32_t open, uint32_t end)
{
    uint32_t nesting = 0;
    uint32_t t = open;
    do
    {
        const uint8_t kind = parser->tokens.kinds[t];
        nesting += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE ? 1u : 0u;
        nesting -= (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE) && nesting != 0 ? 1u : 0u;
        t += 1;
    } while (nesting != 0 && t < end);
    return t;
}

// The token past the operand of sizeof's, alignof's or typeof's keyword at `keyword`, before `end`: a parenthesized
// one's group, or a unary expression's prefix operators, primary and postfix operators.
static uint32_t check_input_counted_unevaluated_end(const Parser* parser, uint32_t keyword, uint32_t end)
{
    uint32_t t = keyword + 1;
    while (t < end && parser->tokens.kinds[t] != PARSE_KIND_L_PAREN && parser->tokens.kinds[t] != PARSE_KIND_IDENTIFIER &&
           parser->tokens.kinds[t] != PARSE_KIND_NUMBER && parser->tokens.kinds[t] != PARSE_KIND_STRING && parser->tokens.kinds[t] != PARSE_KIND_CHARACTER)
    {
        t += 1;
    }
    const bool grouped = t == keyword + 1 && t < end && parser->tokens.kinds[t] == PARSE_KIND_L_PAREN;
    t = t < end && parser->tokens.kinds[t] == PARSE_KIND_L_PAREN ? check_input_counted_group_end(parser, t, end) : t < end ? t + 1 : t;
    bool postfix = !grouped;
    while (postfix && t < end)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool group = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE;
        const bool member = (kind == PARSE_KIND_PERIOD || kind == PARSE_KIND_ARROW) && t + 1 < end;
        postfix = group || member || kind == PARSE_KIND_PLUSPLUS || kind == PARSE_KIND_MINUSMINUS;
        t = group ? check_input_counted_group_end(parser, t, end) : member ? t + 2 : postfix ? t + 1 : t;
    }
    return t;
}

// clang's -Wdivision-by-zero in a counted_by family's argument [first, end) (#1842), which its parser reads as an
// expression: each `/` or `%` whose right operand is a constant zero, at the operator. Rare: scalar.
static void check_input_counted_division(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    for (uint32_t t = first; t < end && !reader->output->resource; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const bool divides = (kind == PARSE_KIND_SLASH || kind == PARSE_KIND_PERCENT) && parser->tree.roles[t] == PARSE_ROLE_MULTIPLICATIVE;
        // The right operand: the run past the operator that its node holds.
        uint32_t stop = t + 1;
        bool under = divides;
        while (under && stop < end)
        {
            uint32_t at = stop;
            for (uint32_t steps = 0; at != t && at >= first && at < end && parser->tree.parents[at] != at && steps < end - first; steps += 1)
            {
                at = parser->tree.parents[at];
            }
            under = at == t;
            stop += under ? 1u : 0u;
        }
        CheckValue value = { 0 };
        TypeId unused = 0;
        const uint32_t reports = reader->output->report_count;
        const bool zero = divides && stop > t + 1 && check_input_constant_read(reader, t + 1, stop, false, false, true, &value, &unused, NULL, NULL) &&
                          value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE && value.integer == 0;
        reader->output->report_count = reports;
        if (zero)
        {
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_DIVISION_BY_ZERO,
                .kind = CHECK_WARNING_EVENT_DIVISION_BY_ZERO,
                .token = t,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = t + 1,
                .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = kind == PARSE_KIND_PERCENT ? 1u : 0u },
            };
            check_input_warning_once(reader, &event);
        }
    }
}

// The type of a counted_by family's argument that is more than a name (#1842), as clang's parser reads it where the
// attribute is: the record's earlier members and the scope's names (check_input_thread_name; in sizeof's, alignof's and
// typeof's unevaluated operands, the scope's alone, as clang's member lookup there finds none), each name that names
// nothing clang's "use of undeclared identifier" error, at the first (*undeclared, which ends the attribute); 0 where its
// type isn't worked out.
static TypeId check_input_counted_argument_type(CheckInputReader* reader, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                                uint32_t member_count, const CheckInputAttribute* attribute, uint32_t first, uint32_t end, uint32_t* undeclared,
                                                bool* expression_error)
{
    *expression_error = false;
    const Parser* parser = reader->parser;
    *undeclared = BUSTER_CHECK_NONE;
    uint32_t unevaluated = first;
    for (uint32_t t = first; t < end; t += 1)
    {
        unevaluated = t >= unevaluated && check_condition_unevaluated_keyword(parser, t) ? check_input_counted_unevaluated_end(parser, t, end) : unevaluated;
        const bool fields = t >= unevaluated;
        const bool named = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] != PARSE_ROLE_SPECIFIER &&
                           check_input_thread_name(reader, BUSTER_CHECK_NONE, fields ? frames : NULL, depth, members, member_count, t);
        *undeclared = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER && parser->tree.roles[t] != PARSE_ROLE_SPECIFIER && !named && *undeclared == BUSTER_CHECK_NONE ?
                      t : *undeclared;
    }
    TypeId type = 0;
    if (*undeclared == BUSTER_CHECK_NONE)
    {
        // Its type alone: a report the reading makes is none of clang's here, which reads the argument as an expression.
        const uint32_t reports = reader->output->report_count;
        CheckValue value = { 0 };
        TypeId unused = 0;
        CheckConstantFailure failure = { .token = BUSTER_CHECK_NONE, .callee = BUSTER_CHECK_NONE };
        uint32_t failed = BUSTER_CHECK_NONE;
        const bool read = check_input_constant_read(reader, first, end, false, false, true, &value, &unused, &failed, &failure);
        type = read || value.type != 0 ? value.type : 0;
        reader->output->report_count = reports;
        check_input_counted_division(reader, first, end);
        // A member access clang's Sema refuses in the expression (#1842): its error, as an attribute expression's
        // (check_constant_failure_error); the caller then gives clang's integer error.
        const CheckConstantError mapped = !read && failure.diagnostic != CHECK_DIAG_NONE ?
                                          check_constant_failure_error(reader, &failure, failure.token < end ? failure.token : first) :
                                          (CheckConstantError){ .error = CHECK_ARGUMENT_COUNT };
        if (mapped.error == CHECK_ARGUMENT_EXPRESSION_MEMBER_BASE || mapped.error == CHECK_ARGUMENT_EXPRESSION_MEMBER_NAME ||
            mapped.error == CHECK_ARGUMENT_EXPRESSION_ARROW_POINTER)
        {
            const uint32_t before = reader->output->report_count;
            check_input_report_argument(reader, mapped.token, CHECK_INPUT_ATTRIBUTE_ARGUMENT, (uint32_t)mapped.error | mapped.number << 8);
            if (reader->output->report_count > before)
            {
                CheckInputReport* report = reader->output->reports + before;
                report->attribute = attribute->first;
                report->type = mapped.type;
                report->value = mapped.value;
                report->type_spelling = check_input_typedef_spelling(reader->output, reader->parser, mapped.spelling != 0 ? mapped.spelling : first);
                report->note_token = mapped.note_token;
                report->note_kind = mapped.note_kind;
                report->completion = mapped.token + 1;
            }
            *expression_error = true;
            type = 0;
        }
    }
    return type;
}

// The counted_by family's attributes of a member being declared in the innermost of `frames` (its declaration's first
// token `begin`, its type `type`), checked as clang's CheckCountedByAttrOnField checks them, in its order, with its
// errors; a valid one's kind and count into `member`, the last one's. False where one is an error (reported).
static bool check_input_counted_by_member(CheckInputReader* reader, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                          uint32_t member_count, const CheckInputAttributes* attributes, uint32_t begin, TypeMember* member)
{
    const Parser* parser = reader->parser;
    const TypeTable* types = reader->types;
    bool valid = true;
    for (uint32_t a = 0; valid && attributes != 0 && a < attributes->count; a += 1)
    {
        const CheckInputAttribute* attribute = &attributes->records[a];
        const uint32_t reported = reader->output->report_count;
        const uint8_t kind = attribute->kind == CHECK_ATTRIBUTE_COUNTED_BY ? check_input_counted_kind(check_input_attribute_name(reader, attribute->name)) :
                                                                             BUSTER_TYPE_COUNT_NONE;
        const uint32_t index = kind != BUSTER_TYPE_COUNT_NONE ? (uint32_t)kind - 1 : 0;
        const bool bytes = kind == BUSTER_TYPE_SIZED_BY || kind == BUSTER_TYPE_SIZED_BY_OR_NULL;
        const bool or_null = kind == BUSTER_TYPE_COUNTED_BY_OR_NULL || kind == BUSTER_TYPE_SIZED_BY_OR_NULL;
        const TypeId field = type_strip_aligned(types, member->type);
        const uint8_t field_kind = type_kind(types, field);
        const bool array = field_kind == BUSTER_TYPE_KIND_ARRAY || field_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || field_kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        const bool pointer = field_kind == BUSTER_TYPE_KIND_POINTER;
        // The count: one identifier, which the record's members name first, then the ordinary scopes; a member declared
        // later is no name yet.
        const uint32_t first = attribute->argument_first;
        const bool single = kind != BUSTER_TYPE_COUNT_NONE && first + 1 == attribute->argument_end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
        const bool grouped = kind != BUSTER_TYPE_COUNT_NONE && first + 3 == attribute->argument_end && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN &&
                             parser->tokens.kinds[first + 1] == PARSE_KIND_IDENTIFIER && parser->tokens.kinds[first + 2] == PARSE_KIND_R_PAREN;
        const uint32_t named = single ? first : grouped ? first + 1 : BUSTER_CHECK_NONE;
        const uint32_t name = named != BUSTER_CHECK_NONE ? parser->tokens.names[named] : 0;
        const uint32_t count = named != BUSTER_CHECK_NONE ? check_input_counted_member(types, frames, depth, members, member_count, name) : BUSTER_CHECK_NONE;
        const uint32_t handle = named != BUSTER_CHECK_NONE && count == BUSTER_CHECK_NONE ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) : 0;
        const uint32_t declaration = handle != 0 ? reader->bindings.declaration[handle - 1] : BUSTER_CHECK_NONE;
        const uint8_t binding = handle != 0 ? reader->bindings.kind[handle - 1] : C_BINDING_KINDS;
        const TypeId count_type = count != BUSTER_CHECK_NONE ? members[count].member.type :
                                  binding == C_BINDING_ENUMERATOR ? BUSTER_TYPE_INT :
                                  declaration < parser->tokens.count && binding != C_BINDING_TYPEDEF ? reader->declared_types[declaration] : 0;
        // The pointee's or the element's type, and what clang finds wrong with it (CountedByInvalidPointeeTypeKind).
        const TypeId pointee = pointer || array ? type_element(types, field) : 0;
        const TypeId pointee_stripped = type_strip_aligned(types, pointee);
        const uint8_t pointee_kind = type_kind(types, pointee_stripped);
        const bool always_incomplete = pointee != 0 && (pointee_kind == BUSTER_TYPE_KIND_VOID || pointee_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY);
        const bool flexible_pointee = pointee_kind == BUSTER_TYPE_KIND_STRUCT && (types->types.flags[type_index(pointee_stripped)] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0;
        const uint32_t invalid = always_incomplete && !bytes && pointee_kind != BUSTER_TYPE_KIND_VOID ? 1u :
                                 pointee_kind == BUSTER_TYPE_KIND_FUNCTION ? 3u : flexible_pointee ? 4u : 0u;
        // The count's record against the member's, and the records clang compares, past unnamed ones.
        const TypeId record = frames[depth - 1].type;
        const TypeId count_record = count != BUSTER_CHECK_NONE ? members[count].record : 0;
        const bool count_union = count != BUSTER_CHECK_NONE && count_record != record && type_kind(types, count_record) == BUSTER_TYPE_KIND_UNION;
        const bool separate = count != BUSTER_CHECK_NONE && count_record != record && !count_union &&
                              check_input_counted_named(types, frames, depth, members, member_count, record) !=
                              check_input_counted_named(types, frames, depth, members, member_count, count_record);
        const uint32_t count_begin = count != BUSTER_CHECK_NONE ? members[count].begin :
                                     declaration < parser->tokens.count ? (binding == C_BINDING_ENUMERATOR ? declaration : check_input_declaration_begin(parser, declaration)) :
                                     BUSTER_CHECK_NONE;
        // A literal's type, for clang's integer check of an expression that is no name.
        bool literal_integer = true;
        if (kind != BUSTER_TYPE_COUNT_NONE && named == BUSTER_CHECK_NONE && first + 1 == attribute->argument_end &&
            (parser->tokens.kinds[first] == PARSE_KIND_NUMBER || parser->tokens.kinds[first] == PARSE_KIND_CHARACTER || parser->tokens.kinds[first] == PARSE_KIND_STRING))
        {
            CheckLiteral literal;
            check_input_literal(reader, first, &literal);
            const CheckValue value = literal.valid ? check_input_literal_value(reader, first, literal) : (CheckValue){ 0 };
            literal_integer = literal.valid && check_input_counted_integer(types, value.type);
        }
        // Any other expression's type, with the members' names (#1842); a literal's where it isn't worked out.
        uint32_t undeclared = BUSTER_CHECK_NONE;
        bool expression_error = false;
        const TypeId argument_type = kind != BUSTER_TYPE_COUNT_NONE && named == BUSTER_CHECK_NONE && first < attribute->argument_end ?
                                     check_input_counted_argument_type(reader, frames, depth, members, member_count, attribute, first, attribute->argument_end,
                                                                       &undeclared, &expression_error) : 0;
        const bool integer_argument = expression_error ? false : argument_type != 0 ? check_input_counted_integer(types, argument_type) : literal_integer;
        if (kind == BUSTER_TYPE_COUNT_NONE)
        {
        }
        else if (named != BUSTER_CHECK_NONE && count == BUSTER_CHECK_NONE && handle == 0)
        {
            // clang's parser reads the argument first: a name nothing declares is its error, and the attribute's end.
            check_input_report(reader, named, CHECK_INPUT_UNDECLARED);
            valid = false;
        }
        else if (undeclared != BUSTER_CHECK_NONE)
        {
            check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
            valid = false;
        }
        else if (type_kind(types, record) == BUSTER_TYPE_KIND_UNION)
        {
            check_input_counted_report(reader, begin, CHECK_INPUT_COUNTED_UNION, index, 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if ((array && (bytes || or_null)) || (!array && !pointer))
        {
            check_input_counted_report(reader, begin, CHECK_INPUT_COUNTED_SUBJECT, index | (array ? 0x100u : 0u), 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (array && field_kind != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
        {
            check_input_counted_report(reader, begin, CHECK_INPUT_COUNTED_ARRAY, index, 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (invalid != 0 && array && invalid == 4)
        {
            // A flexible array member of structs with one: clang's warning, which drops the attribute.
            const CheckWarningEvent event =
            {
                .id = CHECK_WARNING_ID_COUNTED_BY_ELEMENT,
                .kind = CHECK_WARNING_EVENT_COUNTED_BY_ELEMENT,
                .token = begin,
                .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                .completion = attribute->end < parser->tokens.count ? attribute->end : begin,
                .payload = { .first_type = pointee, .name = index, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
            };
            check_input_warning_once(reader, &event);
        }
        else if (invalid != 0)
        {
            check_input_counted_report(reader, begin, CHECK_INPUT_COUNTED_POINTEE, index | (array ? 0x100u : 0u) | invalid << 16, pointee, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (named != BUSTER_CHECK_NONE && !check_input_counted_integer(types, count_type))
        {
            check_input_counted_report(reader, first, CHECK_INPUT_COUNTED_INTEGER, index, 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (named == BUSTER_CHECK_NONE || grouped)
        {
            // Another expression: one of no integer type, clang's integer error (its type with the members' names,
            // check_input_counted_argument_type); anything else, its declaration reference's.
            check_input_counted_report(reader, first, integer_argument ? CHECK_INPUT_COUNTED_REFERENCE : CHECK_INPUT_COUNTED_INTEGER, index, 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (count == BUSTER_CHECK_NONE)
        {
            check_input_counted_report(reader, first, CHECK_INPUT_COUNTED_OUTSIDE, index, 0, count_begin);
            valid = false;
        }
        else if (count_union)
        {
            check_input_counted_report(reader, count_begin, CHECK_INPUT_COUNTED_UNION_FIELD, index, 0, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (separate)
        {
            check_input_counted_report(reader, first, CHECK_INPUT_COUNTED_STRUCT, index | (array ? 0x100u : 0u), 0, count_begin);
            valid = false;
        }
        else
        {
            member->count_kind = kind;
            member->count_name = name;
        }
        // Clang's attribute errors come once its argument is parsed, after the warnings and errors its expression gave
        // (#1842), which keep their places.
        for (uint32_t r = reported; r < reader->output->report_count; r += 1)
        {
            CheckInputReport* report = reader->output->reports + r;
            const bool parsed = report->message == CHECK_INPUT_ATTRIBUTE_ARGUMENT || report->message == CHECK_INPUT_UNDECLARED;
            report->completion = !parsed && report->completion < attribute->argument_end ? attribute->argument_end : report->completion;
        }
    }
    return valid;
}

static uint32_t check_input_first_token(const CheckInputReader* reader, uint32_t token);

static uint32_t check_input_first_token(const CheckInputReader* reader, uint32_t token);

// clang's CheckInvalidBuiltinCountedByRef (#148): __builtin_counted_by_ref's value, which may not leak the count's
// address, by where the call (its `)`, `close`) stands as written, past parentheses: an assignment's operand or a
// declaration's initializer ("assigned to a variable"; a designator's in an initializer list is none), a call's argument
// ("passed into a function"), a return's value ("returned from a function"), a subscript's base ("used in an array
// subscript expression") and a binary operator's operand ("used in a binary expression"). At the call's first token.
static void check_input_counted_ref_context(CheckInputReader* reader, uint32_t close)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    uint32_t child = close;
    uint32_t parent = parser->tree.parents[close];
    for (uint32_t depth = 0; depth < 4096 && parent < count && parser->tree.roles[parent] == PARSE_ROLE_CLOSE_GROUP; depth += 1)
    {
        child = parent;
        parent = parser->tree.parents[parent];
    }
    // A call's arguments hang on its separators.
    uint32_t call = parent;
    for (uint32_t depth = 0; depth < 4096 && call < count && parser->tree.roles[call] == PARSE_ROLE_SEPARATOR; depth += 1)
    {
        call = parser->tree.parents[call];
    }
    // A designator's `=` in an initializer list, whose element clang's check doesn't visit.
    uint32_t list = parent < count && parser->tree.roles[parent] == PARSE_ROLE_ASSIGN ? parser->tree.parents[parent] : count;
    for (uint32_t depth = 0; depth < 4096 && list < count && parser->tree.roles[list] == PARSE_ROLE_SEPARATOR; depth += 1)
    {
        list = parser->tree.parents[list];
    }
    const uint8_t role = parent < count ? parser->tree.roles[parent] : PARSE_ROLE_NONE;
    const uint8_t next = child + 1 < count ? parser->tokens.kinds[child + 1] : PARSE_KIND_END;
    // A separator's `,` is a comma operator but in a call's arguments, a prototype's, a keyword's group or an initializer
    // list (as the constant evaluator reads it).
    const uint8_t listing = call < count ? parser->tree.roles[call] : PARSE_ROLE_NONE;
    const bool comma = role == PARSE_ROLE_COMMA ||
                       (role == PARSE_ROLE_SEPARATOR && listing != PARSE_ROLE_CLOSE_CALL && listing != PARSE_ROLE_CLOSE_FUNCTION &&
                        listing != PARSE_ROLE_CLOSE_KEYWORD && listing != PARSE_ROLE_CLOSE_INITIALIZER);
    const bool binary = role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL ||
                        role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR ||
                        role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || comma;
    const uint32_t first = check_input_first_token(reader, close);
    if (role == PARSE_ROLE_ASSIGN && !(list < count && parser->tree.roles[list] == PARSE_ROLE_CLOSE_INITIALIZER))
    {
        check_input_report_argument(reader, first, CHECK_INPUT_COUNTED_REF_LEAK, 0);
    }
    else if (binary)
    {
        check_input_report_argument(reader, first, CHECK_INPUT_COUNTED_REF_USE, 1);
    }
    else if (role == PARSE_ROLE_CLOSE_SUBSCRIPT && next == PARSE_KIND_L_SQUARE)
    {
        check_input_report_argument(reader, first, CHECK_INPUT_COUNTED_REF_USE, 0);
    }
    else if (role == PARSE_ROLE_RETURN)
    {
        check_input_report_argument(reader, first, CHECK_INPUT_COUNTED_REF_LEAK, 2);
    }
    else if (listing == PARSE_ROLE_CLOSE_CALL && next != PARSE_KIND_L_PAREN)
    {
        check_input_report_argument(reader, first, CHECK_INPUT_COUNTED_REF_LEAK, 1);
    }
}
