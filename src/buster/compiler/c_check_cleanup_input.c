// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's cleanup attribute (#147), as clang 23 checks it (Sema's handleCleanupAttr): on a block-scope variable of
// automatic storage, its argument names a function, looked up where the argument is (the variable isn't visible in its
// own attribute), which takes one parameter that the variable's address converts to as an assignment would, with no
// diagnostic or with the discarded qualifiers alone (clang's IsAssignConvertCompatible). The last of a declaration's
// cleanup attributes applies; on any other declaration clang warns and ignores it, and so does this, but for its
// argument's lookup, which an undeclared name fails wherever it is. Each applied attribute is a CheckInputCleanup, in
// source order, with its variable's scope: check_program calls the function at each of the scope's exits.
// Include after c_check_vla_input.c, in c_check_input.c. A unit without a cleanup attribute makes no record and pays
// one test per attributed declarator; a body read on a lane that declares such a variable is left to source order.

// clang's rule for the parameter's type `parameter` of the function, which the variable's address (a pointer to
// `variable`) converts to: _Bool, a pointer to void, or a pointer to a type compatible with the variable's, whatever
// either's qualifiers, or an atomic type of one of them, which clang's CheckAssignmentConstraints checks as its value's
// type (#1844: `_Atomic(int *)`, `_Atomic(void *)`, `_Atomic _Bool`). A pointer of another signedness, or to a pointer
// of other qualifiers, which an assignment only warns about, is clang's error here.
static bool check_input_cleanup_parameter(CheckInputReader* reader, TypeId parameter, TypeId variable)
{
    const TypeTable* types = reader->types;
    const TypeId declared = type_unqualified(type_strip_aligned(types, parameter));
    const TypeId target = type_kind(types, declared) == BUSTER_TYPE_KIND_ATOMIC ? type_unqualified(type_strip_aligned(types, type_element(types, declared))) : declared;
    bool accepted = target == BUSTER_TYPE_BOOL;
    if (type_kind(types, target) == BUSTER_TYPE_KIND_POINTER)
    {
        const TypeId element = type_strip_aligned(types, type_element(types, target));
        const TypeAnswer answer = type_kind(types, element) == BUSTER_TYPE_KIND_VOID ? TYPE_ANSWER_YES :
                                  type_compatible(types, type_unqualified(element), type_unqualified(type_strip_aligned(types, variable)));
        reader->output->resource |= answer == TYPE_ANSWER_FULL;
        accepted = answer == TYPE_ANSWER_YES;
    }
    return accepted;
}

// The typedef a cleanup function's parameter spells its type's base with, for its error's text (#1844), or 0: the
// function's declarator `function`'s parameter list, where it has no parentheses of its own, read back from its `)` as a
// declaration's specifiers are (check_input_typedef_use).
static uint32_t check_input_cleanup_parameter_spelling(const CheckInputReader* reader, uint32_t function)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool listed = function != BUSTER_CHECK_NONE && function + 1 < count && parser->tokens.kinds[function + 1] == PARSE_KIND_L_PAREN;
    uint32_t close = listed ? function + 2 : count;
    while (close < count && parser->tokens.kinds[close] != PARSE_KIND_R_PAREN && parser->tokens.kinds[close] != PARSE_KIND_L_PAREN)
    {
        close += 1;
    }
    uint32_t use = 0;
    return close < count && parser->tokens.kinds[close] == PARSE_KIND_R_PAREN ? check_input_typedef_use(reader->output, parser, close, &use) : 0;
}

// The qualifiers a cleanup function's parameter declares itself with (`L *const p`), which its function's type drops and
// clang's error prints (#1844): its name's declaration's, the last token of a parameter list without parentheses of its
// own, where its declared type is a pointer's; none otherwise.
static uint32_t check_input_cleanup_parameter_qualifiers(const CheckInputReader* reader, uint32_t function)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    const bool listed = function != BUSTER_CHECK_NONE && function + 1 < count && parser->tokens.kinds[function + 1] == PARSE_KIND_L_PAREN;
    uint32_t close = listed ? function + 2 : count;
    while (close < count && parser->tokens.kinds[close] != PARSE_KIND_R_PAREN && parser->tokens.kinds[close] != PARSE_KIND_L_PAREN)
    {
        close += 1;
    }
    const uint32_t name = close < count && parser->tokens.kinds[close] == PARSE_KIND_R_PAREN ? close - 1 : BUSTER_CHECK_NONE;
    const bool declared = name < input->token_count && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER && input->declarations[name] == name;
    const TypeId type = declared ? reader->declared_types[name] : 0;
    const bool pointer = type != 0 && type_kind(reader->types, type_unqualified(type_strip_aligned(reader->types, type))) == BUSTER_TYPE_KIND_POINTER;
    return pointer ? type_qualifiers(type) & BUSTER_TYPE_QUALIFIERS : 0;
}

// The cleanup type error (#1844): clang's names the parameter's type, as its declaration spells it, and a pointer to the
// variable's, as the variable's declaration `name` spells its base (CheckInputReport.value: the type, then its spelling).
static void check_input_cleanup_type_error(CheckInputReader* reader, uint32_t argument, uint32_t function, TypeId parameter, uint32_t name, TypeId type)
{
    const TypeId pointer = type_pointer(reader->types, type);
    const uint32_t spelling = name < reader->parser->tokens.count ? check_input_typedef_spelling(reader->output, reader->parser, name) : 0;
    const uint64_t value = (uint64_t)pointer | (uint64_t)spelling << 32;
    const uint32_t qualifiers = check_input_cleanup_parameter_qualifiers(reader, function);
    const TypeId declared = qualifiers != 0 ? type_qualify(reader->types, parameter, qualifiers) : parameter;
    const uint32_t before = reader->output->report_count;
    check_input_report_typed(reader, argument, CHECK_INPUT_CLEANUP_TYPE, declared != 0 ? declared : parameter, BUSTER_CHECK_NONE, value);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].type_spelling = check_input_cleanup_parameter_spelling(reader, function);
    }
}

// clang's -Walign-mismatch of a cleanup's call (#1844, CheckArgAlignment, which handleCleanupAttr runs before it checks the
// parameter's type): a pointer parameter to a complete type more aligned than the variable's type `type`, its typedefs'
// alignment attributes among it, at the attribute's argument, naming the function `function`.
static void check_input_cleanup_alignment(CheckInputReader* reader, uint32_t argument, uint32_t function, TypeId parameter, TypeId type)
{
    const TypeTable* types = reader->types;
    const TypeId pointer = type_unqualified(type_strip_aligned(types, parameter));
    const TypeId pointee = type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER ? type_element(types, pointer) : 0;
    const bool complete = pointee != 0 && type_kind(types, type_strip_aligned(types, pointee)) != BUSTER_TYPE_KIND_VOID && type_is_complete(types, pointee) &&
                          type_is_complete(types, type);
    const uint32_t parameter_log2 = complete ? type_alignment_log2(types, pointee) : 0;
    const uint32_t argument_log2 = complete ? type_alignment_log2(types, type) : 0;
    if (complete && argument_log2 < parameter_log2 && function < reader->parser->tokens.count)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_ALIGN_MISMATCH,
            .kind = CHECK_WARNING_EVENT_ALIGN_MISMATCH,
            .token = argument,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            // Published before the argument's own error, as clang gives it first.
            .completion = argument - 1,
            .payload = { .name = reader->parser->tokens.names[function], .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = argument_log2 | parameter_log2 << 8 },
        };
        check_input_warning_event(reader, &event, NULL, 0);
    }
}

static bool check_input_cleanup_record(CheckInputReader* reader, CheckInputCleanup cleanup)
{
    CheckInput* input = reader->output;
    bool valid = true;
    if (input->cleanup_count == input->cleanup_capacity)
    {
        const uint32_t capacity = input->cleanup_capacity != 0 ? input->cleanup_capacity * 2 : 16;
        CheckInputCleanup* grown = capacity > input->cleanup_capacity ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputCleanup, capacity) : NULL;
        valid = grown != NULL;
        if (valid && input->cleanup_count != 0)
        {
            memcpy(grown, input->cleanups, (uint64_t)input->cleanup_count * sizeof(CheckInputCleanup));
        }
        input->cleanups = valid ? grown : input->cleanups;
        input->cleanup_capacity = valid ? capacity : input->cleanup_capacity;
    }
    if (valid)
    {
        const uint32_t reach = input->cleanup_count != 0 ? input->cleanups[input->cleanup_count - 1].reach : 0;
        cleanup.reach = cleanup.end > reach ? cleanup.end : reach;
        input->cleanups[input->cleanup_count++] = cleanup;
    }
    input->resource |= !valid;
    return valid;
}

// The cleanup attributes of declarator `name` of type `type`, in `attributes` (its specifiers' and its own), which
// `applies` says a variable of automatic storage in a block has. Whether the declaration is valid: a reported error isn't.
// The records are its declaration's specifiers' themselves where it has no attributes of its own
// (check_input_merge_attributes), which its other declarators read too: what this declarator makes of them is its own,
// and they stay as they were. clang ignores a function declarator's with a warning, and the variables of the same
// specifiers still have them: their argument count's error and the last of several. An __auto_type variable's
// parameter type waits for the deduced type (check_input_cleanup_deduced); the rest, and its record, are checked here.
static bool check_input_cleanup_declaration(CheckInputReader* reader, const CheckInputAttributes* attributes, uint32_t name, TypeId type, bool applies)
{
    const CheckInputAttribute* applied = NULL;
    bool valid = true;
    for (uint32_t i = 0; valid && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->kind == CHECK_ATTRIBUTE_CLEANUP && !attribute->ignored && attribute->cleanup_count)
        {
            // Another count of arguments than one, an error on each variable it applies to; on any other declaration
            // clang ignores it with a warning.
            if (applies)
            {
                check_input_report(reader, attribute->name_token, CHECK_INPUT_CLEANUP_ARGUMENTS);
                valid = false;
            }
        }
        else if (attribute->kind == CHECK_ATTRIBUTE_CLEANUP && !attribute->ignored)
        {
            // Of several, the last applies, as clang warns.
            applied = attribute;
        }
    }
    if (valid && applied != NULL)
    {
        // The schema bound every argument where parsed, including ignored subjects and duplicate attributes.
        // A shared specifier retains that binding if an earlier declarator shadows the name.
        const uint32_t argument = applied->argument_first;
        const uint32_t function = applied->cleanup_function != 0 ? applied->cleanup_function - 1u : BUSTER_CHECK_NONE;
        const TypeId function_type = function != BUSTER_CHECK_NONE ? type_strip_aligned(reader->types, reader->declared_types[function]) : 0;
        uint32_t parameters = 0;
        const TypeId* parameter_types = function_type != 0 && type_kind(reader->types, function_type) == BUSTER_TYPE_KIND_FUNCTION ?
                                        type_parameters(reader->types, function_type, &parameters) : NULL;
        const bool prototype = function_type != 0 && (reader->types->types.flags[type_index(function_type)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        // The call's alignment warning comes before the parameter's type's error, where the function takes one parameter.
        if (applies && !applied->cleanup_expression && function != BUSTER_CHECK_NONE && parameter_types != NULL && prototype && parameters == 1 &&
            !check_input_auto_type(reader->types, type))
        {
            check_input_cleanup_alignment(reader, argument, function, parameter_types[0], type);
        }
        if (applies && (applied->cleanup_expression || (function != BUSTER_CHECK_NONE && parameter_types == NULL)))
        {
            check_input_report(reader, argument, CHECK_INPUT_CLEANUP_FUNCTION);
            valid = false;
        }
        else if (applies && function != BUSTER_CHECK_NONE && (!prototype || parameters != 1))
        {
            check_input_report(reader, argument, CHECK_INPUT_CLEANUP_PARAMETERS);
            valid = false;
        }
        else if (applies && function != BUSTER_CHECK_NONE && !check_input_auto_type(reader->types, type) &&
                 !check_input_cleanup_parameter(reader, parameter_types[0], type))
        {
            check_input_cleanup_type_error(reader, argument, function, parameter_types[0], name, type);
            valid = false;
        }
        else if (applies && function != BUSTER_CHECK_NONE && reader->scope_count != 0)
        {
            // The variable's scope: its block's, or its for statement's, to the token after its last.
            const uint32_t scope = reader->scope_count - 1;
            const CheckInputCleanup cleanup =
            {
                .declaration = name,
                .function = function,
                .argument = argument,
                .scope = reader->scope_openings[scope],
                .end = reader->scope_ends[scope] + 1,
            };
            valid = check_input_cleanup_record(reader, cleanup);
            // A goto or a case mustn't enter the scope, as a variable-length array's (check_input_vla_finish).
            check_input_rare_claim(reader, &reader->vla_scope_ends[name]);
            reader->vla_scope_ends[name] = valid ? cleanup.end : reader->vla_scope_ends[name];
            check_input_lane_refuse(reader, reader->lane, "a body declares a variable with a cleanup");
        }
    }
    return valid;
}

// An __auto_type variable `declaration`'s cleanup once its initializer gives it type `type`
// (check_input_initializer_finish), as clang checks the attribute against the deduced type: the parameter's type, which
// check_input_cleanup_declaration left to it. Whether the declaration is valid: a reported error isn't. Only a variable
// with a cleanup, whose scope's end the declaration marked, looks for its record: the last one made before those of its
// initializer's own declarations.
static bool check_input_cleanup_deduced(CheckInputReader* reader, uint32_t declaration, TypeId type)
{
    const CheckInput* input = reader->output;
    bool valid = true;
    if (input->cleanup_count != 0 && reader->vla_scope_ends[declaration] != 0)
    {
        uint32_t record = input->cleanup_count;
        while (record != 0 && input->cleanups[record - 1].declaration != declaration)
        {
            record -= 1;
        }
        if (record != 0)
        {
            // The declaration checked that the function has a prototype of one parameter.
            const CheckInputCleanup* cleanup = input->cleanups + record - 1;
            const TypeId function_type = type_strip_aligned(reader->types, reader->declared_types[cleanup->function]);
            uint32_t parameters = 0;
            const TypeId* parameter_types = type_parameters(reader->types, function_type, &parameters);
            // Clang checks alignment when it first handles the attribute, before deduction, while the variable's type
            // is incomplete. The later deduction rechecks assignment compatibility alone (#1844).
            valid = check_input_cleanup_parameter(reader, parameter_types[0], type);
            if (!valid)
            {
                check_input_cleanup_type_error(reader, cleanup->argument, cleanup->function, parameter_types[0], declaration, type);
            }
        }
    }
    return valid;
}
