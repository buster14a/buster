// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's thread-safety analysis's attributes (#147, c_check_attribute.c's check_input_thread_rows), and the names of a
// generic attribute's arguments (check_input_thread_names): guarded_by,
// requires_capability, acquire_capability and the others, which change nothing a program does. clang's parser reads their
// arguments as expressions where the attribute is, so their names are the scope's there (a declaration's own isn't yet;
// a struct's earlier members are, and a function declarator's parameters), each name that isn't clang's "use of undeclared
// identifier" error before any of the declaration's attributes is applied (check_input_thread_names). Their handlers then
// check what SemaDeclAttr.cpp's do past their subjects and counts (check_input_thread_attribute). Included by
// c_check_input.c, after c_check_counted_by_input.c, whose member lookup it takes.

// Whether the identifier at `token` among an attribute's arguments names something where the attribute is: an ordinary
// name of the scope, one of a function declarator's own parameters (`parameters`, the first's name token, linked by
// CheckInputReader.parameter_next: their prototype's scope, which clang's parser keeps open over attributes after the
// declarator), one of the struct's earlier members (`frames`, `members`: a member's declaration's, or none), or a builtin; a
// member's name after `.` or `->` and a tag's after its keyword are no names of the scope.
static bool check_input_thread_name(CheckInputReader* reader, uint32_t parameters, const CheckTagFrame* frames, uint32_t depth, const CheckTagMember* members,
                                    uint32_t member_count, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint8_t previous = token > 0 ? parser->tokens.kinds[token - 1] : PARSE_KIND_END;
    const uint32_t name = parser->tokens.names[token];
    const String8 spelled = check_input_attribute_name(reader, name);
    const bool member = previous == PARSE_KIND_PERIOD || previous == PARSE_KIND_ARROW;
    const bool tag = previous == PARSE_KIND_STRUCT || previous == PARSE_KIND_ENUM;
    const bool builtin = spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0;
    uint32_t declaration = BUSTER_CHECK_NONE;
    for (uint32_t named = parameters; declaration == BUSTER_CHECK_NONE && named != BUSTER_CHECK_NONE; named = check_sparse_get(reader->parameter_next, named))
    {
        declaration = parser->tokens.names[named] == name ? named : BUSTER_CHECK_NONE;
    }
    const uint32_t field = declaration == BUSTER_CHECK_NONE && frames != NULL ?
                           check_input_counted_member(reader->types, frames, depth, members, member_count, name) : BUSTER_CHECK_NONE;
    declaration = field != BUSTER_CHECK_NONE ? members[field].token : declaration;
    if (!member && !tag && declaration != BUSTER_CHECK_NONE)
    {
        reader->output->declarations[token] = declaration;
        reader->declared_types[token] = field != BUSTER_CHECK_NONE ? members[field].member.type : reader->declared_types[declaration];
    }
    return member || tag || builtin || declaration != BUSTER_CHECK_NONE || c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) != 0;
}

// The end of an attribute's argument that starts at `first`, before `end`: its top level's next comma, or `end`.
static uint32_t check_input_thread_argument_end(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t result = end;
    uint32_t nesting = 0;
    for (uint32_t token = first; result == end && token < end; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        nesting += kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE ? 1u : 0u;
        nesting -= (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE) && nesting != 0 ? 1u : 0u;
        result = nesting == 0 && kind == PARSE_KIND_COMMA ? token : end;
    }
    return result;
}

// The first name a generic attribute's expressions name that names nothing, or BUSTER_CHECK_NONE (#147: one this checker
// has no rule for, as its arguments' kinds say, check_input_arguments_row: its identifier and string literal arguments
// aside, a function declarator's parameters names only where clang parses it in the function's scope or after the
// declaration; check_input_thread_names).
static uint32_t check_input_generic_undeclared(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t parameters, const CheckTagFrame* frames,
                                               uint32_t depth, const CheckTagMember* members, uint32_t member_count)
{
    const Parser* parser = reader->parser;
    const uint32_t scoped = BUSTER_CHECK_ARGUMENTS_FUNCTION_SCOPE | BUSTER_CHECK_ARGUMENTS_LATE | BUSTER_CHECK_ARGUMENTS_LATE_EXPERIMENTAL;
    const uint32_t identifiers = BUSTER_CHECK_ARGUMENTS_VARIADIC_IDENTIFIER | BUSTER_CHECK_ARGUMENTS_STRICT_IDENTIFIER;
    const bool kept = !attribute->ignored && !attribute->subject_ignored;
    uint32_t undeclared = BUSTER_CHECK_NONE;
    // (availability's, external_source_symbol's, type_tag_for_datatype's and objc_bridge_related's arguments are no
    // expressions: clang's parser reads them its own way, check_input_availability_parse, check_input_external_source_parse,
    // check_input_type_tag_parse and check_input_objc_bridge_related_parse.)
    const String8 generic_name = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ? check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
    const bool custom = check_input_attribute_equal(generic_name, "availability") || check_input_attribute_equal(generic_name, "external_source_symbol") ||
                        check_input_attribute_equal(generic_name, "type_tag_for_datatype") ||
                        check_input_attribute_equal(generic_name, "objc_bridge_related") || check_input_attribute_equal(generic_name, "swift_newtype") ||
                        check_input_attribute_equal(generic_name, "swift_wrapper");
    const bool generic = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && kept && !custom;
    const CheckInputArgumentsRow* row = generic ? check_input_arguments_row(generic_name) : NULL;
    const uint32_t flags = row != NULL ? row->flags : 0;
    const uint32_t strings = row != NULL ? row->strings : 0;
    const uint32_t in_scope = (flags & scoped) != 0 ? parameters : BUSTER_CHECK_NONE;
    uint32_t index = 0;
    for (uint32_t argument = attribute->argument_first; generic && (flags & BUSTER_CHECK_ARGUMENTS_TYPE) == 0 && undeclared == BUSTER_CHECK_NONE &&
         argument < attribute->argument_end; index += 1)
    {
        const uint32_t stop = check_input_thread_argument_end(parser, argument, attribute->argument_end);
        const bool lone = stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
        const bool identifier = lone && ((index == 0 && (flags & BUSTER_CHECK_ARGUMENTS_IDENTIFIER) != 0) || (flags & identifiers) != 0);
        const bool string = index < 32 && ((strings >> index) & 1u) != 0;
        // An argument that calls a builtin or a keyword of a type argument (__builtin_offsetof's member, _Generic's)
        // isn't looked into.
        bool builtin = false;
        for (uint32_t token = argument; token < stop; token += 1)
        {
            const String8 spelled = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? check_input_attribute_name(reader, parser->tokens.names[token]) :
                                                                                           (String8){ 0 };
            builtin |= (spelled.length > 10 && memcmp(spelled.pointer, "__builtin_", 10) == 0) || parser->tokens.kinds[token] == PARSE_KIND_TYPE_ARGUMENT ||
                       parser->tokens.kinds[token] == PARSE_KIND_GENERIC;
        }
        for (uint32_t token = argument; !identifier && !string && !builtin && undeclared == BUSTER_CHECK_NONE && token < stop; token += 1)
        {
            const bool named = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
            undeclared = named && !check_input_thread_name(reader, in_scope, frames, depth, members, member_count, token) ? token : BUSTER_CHECK_NONE;
            // An undeclared library function's name: clang's lookup of it declares the function implicitly, with its
            // diagnostic, as an expression's elsewhere does (check_input_implicit_function): diagnose_as_builtin's builtin
            // then takes that declaration (check_input_attribute_diagnose_builtin).
            if (undeclared != BUSTER_CHECK_NONE)
            {
                check_input_implicit_function(reader, token);
                undeclared = c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, parser->tokens.names[token]) != 0 ? BUSTER_CHECK_NONE : undeclared;
            }
        }
        argument = stop + 1;
    }
    return undeclared;
}

// An attribute's argument's name that names nothing (#2251): clang's "use of undeclared identifier" at it, after which its
// parser drops the attribute and the declaration goes on, with its other attributes. The report is recovered and the
// attribute ignored; BUSTER_CHECK_NONE then, or the name where the recovery had no room (the report stops the
// declaration, as it used to).
static uint32_t check_input_names_drop(CheckInputReader* reader, CheckInputAttribute* attribute, uint32_t undeclared)
{
    uint32_t result = undeclared;
    if (undeclared != BUSTER_CHECK_NONE)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, undeclared, CHECK_INPUT_UNDECLARED);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].attribute_name = true;
        }
        const bool recovered = reader->output->report_count > before && check_input_recover_reports(reader, before);
        attribute->ignored |= recovered;
        result = recovered ? BUSTER_CHECK_NONE : undeclared;
    }
    return result;
}

// A tag's attributes' names (#1447: a generic attribute's later argument, objc_bridge's), as check_input_thread_names
// checks them but without reading a thread-safety attribute's expressions, which a tag's attributes don't need (its
// capability's is a string) and whose constants may name the tag's type: the first that names nothing is clang's
// parser's error at it, which stops the declaration.
static bool check_input_tag_names(CheckInputReader* reader, const CheckInputAttributes* attributes)
{
    const Parser* parser = reader->parser;
    uint32_t undeclared = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; undeclared == BUSTER_CHECK_NONE && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool thread = attribute->kind == CHECK_ATTRIBUTE_THREAD_SAFETY && !attribute->ignored && !attribute->subject_ignored;
        for (uint32_t token = attribute->argument_first; thread && undeclared == BUSTER_CHECK_NONE && token < attribute->argument_end; token += 1)
        {
            const bool identifier = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
            undeclared = identifier && !check_input_thread_name(reader, BUSTER_CHECK_NONE, NULL, 0, NULL, 0, token) ? token : BUSTER_CHECK_NONE;
        }
        undeclared = undeclared == BUSTER_CHECK_NONE ? check_input_generic_undeclared(reader, attribute, BUSTER_CHECK_NONE, NULL, 0, NULL, 0) : undeclared;
        undeclared = check_input_names_drop(reader, (CheckInputAttribute*)attribute, undeclared);
    }
    return undeclared == BUSTER_CHECK_NONE;
}

// Each thread-safety attribute's argument names, as clang's parser reads them (above), and a generic attribute's
// expressions' (#147: one this checker has no rule for, as its arguments' kinds say, check_input_arguments_row: its
// identifier and string literal arguments aside, a function declarator's parameters names only where clang parses it in
// the function's scope or after the declaration): the first that names nothing is clang's error at it, which stops the
// declaration. `parameters` is a function's declaration's own, or BUSTER_CHECK_NONE; `frames` is the member's struct's, or
// NULL off a member (whose earlier members clang's parser finds in the struct's scope).
static bool check_input_thread_names(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t parameters, const CheckTagFrame* frames,
                                     uint32_t depth, const CheckTagMember* members, uint32_t member_count)
{
    const Parser* parser = reader->parser;
    uint32_t undeclared = BUSTER_CHECK_NONE;
    bool valid = true;
    for (uint32_t i = 0; valid && undeclared == BUSTER_CHECK_NONE && attributes != NULL && i < attributes->count; i += 1)
    {
        CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = !attribute->ignored && !attribute->subject_ignored;
        const bool thread = attribute->kind == CHECK_ATTRIBUTE_THREAD_SAFETY && kept;
        for (uint32_t token = attribute->argument_first; thread && undeclared == BUSTER_CHECK_NONE && token < attribute->argument_end; token += 1)
        {
            const bool identifier = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER;
            undeclared = identifier && !check_input_thread_name(reader, parameters, frames, depth, members, member_count, token) ? token : BUSTER_CHECK_NONE;
        }
        if (thread && undeclared == BUSTER_CHECK_NONE && attribute->argument_count != 0 && attribute->expression_types == NULL)
        {
            attribute->expression_types = BUSTER_ARENA_PUSH_ARRAY(reader->arena, TypeId, attribute->argument_count);
            valid = attribute->expression_types != NULL;
            reader->output->resource |= !valid;
            uint32_t index = 0;
            for (uint32_t argument = attribute->argument_first; valid && argument < attribute->argument_end && index < attribute->argument_count; index += 1)
            {
                const uint32_t stop = check_input_thread_argument_end(parser, argument, attribute->argument_end);
                CheckValue value = { 0 };
                bool call_failure = false;
                valid = check_input_attribute_expression(reader, attributes, attribute, argument, stop, true, &value, &call_failure);
                attribute->expression_types[index] = valid ? value.type : 0;
                argument = stop + 1;
            }
        }
        undeclared = undeclared == BUSTER_CHECK_NONE ? check_input_generic_undeclared(reader, attribute, parameters, frames, depth, members, member_count) :
                                                       undeclared;
        undeclared = check_input_names_drop(reader, attribute, undeclared);
    }
    return valid && undeclared == BUSTER_CHECK_NONE;
}


// Whether the first checked expression has no integer or boolean type. Parentheses, casts, arithmetic and calls have
// already gone through the same actions as a declaration's other expressions.
static bool check_input_thread_not_int(CheckInputReader* reader, const CheckInputAttribute* attribute)
{
    const TypeId type = attribute->expression_types != NULL ? attribute->expression_types[0] : 0;
    uint32_t width = 0;
    bool is_signed = false;
    const TypeId plain = type != 0 ? type_unqualified(type_strip_aligned(reader->types, type)) : 0;
    const bool enumerated = plain != 0 && type_kind(reader->types, plain) == BUSTER_TYPE_KIND_ENUM;
    return plain != 0 && !enumerated && !check_action_integer_type(reader->types, plain, &width, &is_signed);
}

static bool check_input_thread_marked(CheckInputReader* reader, const CheckInputAttributes* attributes)
{
    bool marked = false;
    for (uint32_t i = 0; !marked && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const String8 name = check_input_attribute_name(reader, attribute->name);
        marked = !attribute->ignored && !attribute->subject_ignored && attribute->kind == CHECK_ATTRIBUTE_THREAD_SAFETY &&
                 (check_input_attribute_equal(name, "capability") || check_input_attribute_equal(name, "shared_capability") || check_input_attribute_equal(name, "lockable"));
    }
    return marked;
}

// Capability is attached to a declaration, not to a structural type ID: a typedef of int must not mark every int.
// Named typedefs use their exact spelling declaration. Records use the sparse attributed-token index, so an anonymous
// record and an earlier tag hidden in the current scope are found without a second name lookup or dense token scan.
static bool check_input_thread_capability(CheckInputReader* reader, TypeId type, uint32_t spelling, uint32_t before)
{
    const Parser* parser = reader->parser;
    bool marked = spelling != 0 && spelling < parser->tokens.count &&
                  check_input_thread_marked(reader, check_input_attributes_at(reader->output, spelling));
    TypeId record = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    record = record != 0 && type_kind(reader->types, record) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(reader->types, type_element(reader->types, record)) : record;
    const uint8_t kind = record != 0 ? type_kind(reader->types, record) : 0;
    const bool tagged = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    marked |= tagged && !type_is_complete(reader->types, record);
    const CheckSparseWide attributes = reader->output->attributes;
    const bool scalar = parser->options.scalar;
    const uint32_t end = before < parser->tokens.count ? before : parser->tokens.count;
    for (uint32_t first = tagged && !marked ? check_input_attributed_next(attributes, 0, end, scalar) : end;
         !marked && first < end; first = check_input_attributed_next(attributes, first + 16, end, scalar))
    {
        for (uint32_t selected = check_input_attributed_group(attributes, first, end, scalar); !marked && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
            marked = type_unqualified(reader->tag_types[token]) == type_unqualified(record) &&
                     check_input_thread_marked(reader, check_input_attributes_at(reader->output, token));
        }
    }
    return marked;
}

static uint32_t check_input_thread_spelling(const CheckInputReader* reader, uint32_t first)
{
    const uint32_t declaration = first < reader->parser->tokens.count ? reader->output->declarations[first] : BUSTER_CHECK_NONE;
    return declaration < reader->parser->tokens.count ? check_input_typedef_spelling(reader->output, reader->parser, declaration) : 0;
}

// The annotation language also accepts boolean combinations and address/dereference operators over capability
// expressions. The C expression has already been checked; walk these original tokens without recursive AST visits.
static bool check_input_thread_capability_expression(CheckInputReader* reader, uint32_t first, uint32_t end)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    bool operand = false;
    for (uint32_t token = first; valid && token < end; token += 1)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (kind == PARSE_KIND_L_PAREN && token + 1 < end && check_input_attribute_type_start(reader, token + 1))
        {
            const uint32_t close = parser->tree.parents[token];
            valid = close > token && close < end;
            token = valid ? close : token;
        }
        else if (kind == PARSE_KIND_IDENTIFIER)
        {
            const TypeId type = reader->declared_types[token];
            valid = type != 0 && check_input_thread_capability(reader, type, check_input_thread_spelling(reader, token), first);
            operand = true;
        }
        else
        {
            valid = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_EXCLAIM || kind == PARSE_KIND_AMP ||
                    kind == PARSE_KIND_STAR || kind == PARSE_KIND_AMPAMP || kind == PARSE_KIND_PIPEPIPE;
        }
    }
    return valid && operand;
}

static void check_input_thread_warning(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckWarningEventKind kind, TypeId type, uint32_t spelling)
{
    const Parser* parser = reader->parser;
    const uint32_t scope = attribute->standard && attribute->first != attribute->name_token ? parser->tokens.names[attribute->first] : 0;
    const CheckWarningEvent event = {
        .id = CHECK_WARNING_ID_THREAD_SAFETY_ATTRIBUTES,
        .kind = kind,
        .token = attribute->first,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = attribute->first,
        .payload = { .name = attribute->name, .token = spelling, .first_type = type,
                     .data = scope | (attribute->standard ? BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE : 0) },
    };
    check_input_warning_event(reader, &event, NULL, 0);
}

static void check_input_thread_warnings(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t declaration, TypeId type, uint8_t flags)
{
    const Parser* parser = reader->parser;
    const String8 name = check_input_attribute_name(reader, attribute->name);
    const bool order = check_input_attribute_equal(name, "acquired_after") || check_input_attribute_equal(name, "acquired_before");
    const bool pointer = check_input_attribute_equal(name, "pt_guarded_by") || check_input_attribute_equal(name, "pt_guarded_var");
    const bool locks = (flags & (BUSTER_CHECK_THREAD_INDEXED | BUSTER_CHECK_THREAD_TRYLOCK)) != 0 ||
                       check_input_attribute_equal(name, "requires_capability") || check_input_attribute_equal(name, "requires_shared_capability") ||
                       check_input_attribute_equal(name, "exclusive_locks_required") || check_input_attribute_equal(name, "shared_locks_required") ||
                       check_input_attribute_equal(name, "locks_excluded");
    const bool guarded = check_input_attribute_equal(name, "guarded_by") || check_input_attribute_equal(name, "pt_guarded_by") ||
                         check_input_attribute_equal(name, "lock_returned") || order;
    const bool subject = (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0;
    const TypeId plain = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    const uint8_t kind = plain != 0 ? type_kind(reader->types, plain) : 0;
    const bool function_pointer = kind == BUSTER_TYPE_KIND_POINTER && type_kind(reader->types, type_element(reader->types, plain)) == BUSTER_TYPE_KIND_FUNCTION;
    const bool requires = check_input_attribute_equal(name, "requires_capability") || check_input_attribute_equal(name, "requires_shared_capability") ||
                          check_input_attribute_equal(name, "exclusive_locks_required") || check_input_attribute_equal(name, "shared_locks_required") ||
                          check_input_attribute_equal(name, "locks_excluded");
    const bool invalid_subject = locks && !subject && !function_pointer;
    const bool invalid_context = order && !check_input_thread_capability(reader, type, 0, attribute->first);
    if (invalid_subject)
    {
        const bool parameter = (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0 && requires;
        check_input_thread_warning(reader, attribute, parameter ? CHECK_WARNING_EVENT_THREAD_PARAMETER : CHECK_WARNING_EVENT_THREAD_FUNCTION_POINTER,
                                   0, (declaration & BUSTER_CHECK_SUBJECT_FIELD) != 0);
    }
    if (invalid_context)
    {
        check_input_thread_warning(reader, attribute, CHECK_WARNING_EVENT_THREAD_CONTEXT, 0, 0);
    }
    const uint32_t skip = (flags & BUSTER_CHECK_THREAD_TRYLOCK) != 0 ? 1u : 0u;
    uint32_t parameters = 0;
    const TypeId* parameter_types = type != 0 && type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION ? type_parameters(reader->types, type, &parameters) : NULL;
    if (!invalid_subject && locks && attribute->argument_count == skip)
    {
        check_input_thread_warning(reader, attribute, CHECK_WARNING_EVENT_THREAD_METHOD, 0, 0);
    }
    for (uint32_t argument = attribute->argument_first, index = 0; !invalid_subject && !invalid_context && (locks || guarded) && argument < attribute->argument_end; index += 1)
    {
        const uint32_t stop = check_input_thread_argument_end(parser, argument, attribute->argument_end);
        if (index >= skip && attribute->expression_types != NULL)
        {
            TypeId argument_type = attribute->expression_types[index];
            uint32_t spelling = check_input_thread_spelling(reader, argument);
            String8 string = { 0 };
            const bool literal = parser->tokens.kinds[argument] == PARSE_KIND_STRING && check_input_attribute_string(reader, argument, stop, false, &string);
            if (literal && string.length != 0 && !(string.length == 1 && string.pointer[0] == '*'))
            {
                check_input_thread_warning(reader, attribute, CHECK_WARNING_EVENT_THREAD_ARGUMENT, 0, 0);
            }
            if (!literal && stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_NUMBER &&
                (flags & BUSTER_CHECK_THREAD_INDEXED) != 0 && parameter_types != NULL)
            {
                CheckLiteral number;
                check_input_literal(reader, argument, &number);
                const CheckValue value = number.valid ? check_input_literal_value(reader, argument, number) : (CheckValue){ 0 };
                if (value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer != 0 && value.integer <= parameters)
                {
                    argument_type = parameter_types[value.integer - 1];
                    spelling = 0;
                }
            }
            if (!literal && !check_input_thread_capability(reader, argument_type, spelling, attribute->first) &&
                !check_input_thread_capability_expression(reader, argument, stop))
            {
                check_input_thread_warning(reader, attribute, CHECK_WARNING_EVENT_THREAD_CAPABILITY, argument_type, spelling);
            }
        }
        argument = stop + 1;
    }
    const bool incomplete = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && !type_is_complete(reader->types, plain);
    if (pointer && kind != BUSTER_TYPE_KIND_POINTER && !incomplete)
    {
        check_input_thread_warning(reader, attribute, CHECK_WARNING_EVENT_THREAD_POINTER, type, 0);
    }
}

// A thread-safety attribute's handler's checks past its subject and count (#147), each an error that stops the
// declaration: capability's and shared_capability's name a string literal, at the argument (checkStringLiteralArgumentAttr);
// a trylock's first argument an int or a bool, at the attribute (checkTryLockFunAttrCommon); and a lock function's
// argument that is an integer literal, on a function's declaration, an index of its parameters from 1, at the attribute
// (checkAttrArgsAreCapabilityObjs). `declaration` and `type` are the declaration's subjects and type.
static bool check_input_thread_attribute(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t declaration, TypeId type)
{
    const Parser* parser = reader->parser;
    const CheckInputThreadRow* row = check_input_thread_row(check_input_attribute_name(reader, attribute->name));
    const uint8_t flags = row != NULL ? row->flags : 0;
    const uint32_t first = attribute->argument_first;
    const uint32_t end = attribute->argument_end;
    const bool arguments = attribute->argument_count != 0 && first < end;
    bool valid = true;
    if ((flags & BUSTER_CHECK_THREAD_STRING) != 0 && arguments && parser->tokens.kinds[first] != PARSE_KIND_STRING)
    {
        const uint32_t before = reader->output->report_count;
        check_input_report(reader, first, CHECK_INPUT_ATTRIBUTE_STRING_ARGUMENT);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].attribute = attribute->first;
        }
        valid = false;
    }
    else if ((flags & BUSTER_CHECK_THREAD_TRYLOCK) != 0 && arguments && check_input_thread_not_int(reader, attribute))
    {
        check_input_report(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_INT_OR_BOOL);
        valid = false;
    }
    else if ((flags & BUSTER_CHECK_THREAD_INDEXED) != 0 && arguments && (declaration & BUSTER_CHECK_SUBJECT_FUNCTION) != 0 && type != 0 &&
             type_kind(reader->types, type) == BUSTER_TYPE_KIND_FUNCTION)
    {
        uint32_t parameters = 0;
        type_parameters(reader->types, type, &parameters);
        uint32_t index = 0;
        uint32_t failed = 0;
        for (uint32_t argument = first; failed == 0 && argument < end;)
        {
            const uint32_t stop = check_input_thread_argument_end(parser, argument, end);
            index += 1;
            if (stop == argument + 1 && parser->tokens.kinds[argument] == PARSE_KIND_NUMBER)
            {
                CheckLiteral literal;
                check_input_literal(reader, argument, &literal);
                const CheckValue value = literal.valid ? check_input_literal_value(reader, argument, literal) : (CheckValue){ 0 };
                uint32_t width = 0;
                bool is_signed = false;
                const bool integer = literal.valid && check_action_integer_type(reader->types, value.type, &width, &is_signed);
                failed = integer && (value.integer == 0 || value.integer > parameters) ? index : 0;
            }
            argument = stop + 1;
        }
        if (failed != 0)
        {
            check_input_report_argument(reader, attribute->first, CHECK_INPUT_ATTRIBUTE_INDEX_BOUNDS, failed | parameters << 16);
            valid = false;
        }
    }
    if (valid)
    {
        check_input_thread_warnings(reader, attribute, declaration, type, flags);
    }
    return valid;
}
