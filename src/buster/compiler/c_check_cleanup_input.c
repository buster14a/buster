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
// either's qualifiers. A pointer of another signedness, or to a pointer of other qualifiers, which an assignment only
// warns about, is clang's error here.
static bool check_input_cleanup_parameter(CheckInputReader* reader, TypeId parameter, TypeId variable)
{
    const TypeTable* types = reader->types;
    const TypeId target = type_unqualified(type_strip_aligned(types, parameter));
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
// specifiers still have them: their argument count's error and the last of several.
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
        else if (applies && function != BUSTER_CHECK_NONE && !check_input_cleanup_parameter(reader, parameter_types[0], type))
        {
            check_input_report(reader, argument, CHECK_INPUT_CLEANUP_TYPE);
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
            reader->vla_scope_ends[name] = valid ? cleanup.end : reader->vla_scope_ends[name];
            check_input_lane_refuse(reader, reader->lane, "a body declares a variable with a cleanup");
        }
    }
    return valid;
}
