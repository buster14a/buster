// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's type tags (#1447): argument_with_type_tag(kind, argument, tag) and pointer_with_type_tag(kind, argument, tag) on a
// function or a function pointer, and type_tag_for_datatype(kind, type[, layout_compatible][, must_be_null]) on a variable,
// which -Wtype-safety's warnings at calls read (clang's CheckArgumentWithTypeTag: check_common_type_safety). clang is the
// external oracle of the behavior; no clang code is incorporated.
//
// The reader checks each attribute as clang's parser and handlers do: type_tag_for_datatype's arguments, which clang's parser
// reads its own way (check_input_type_tag_parse), the rules' parameter indices and pointer_with_type_tag's parameter
// (check_input_type_tag_handled). Each function, function pointer or variable declaration that has some of them, its own
// or an earlier redeclaration's, gets an entry (CheckTypeTags.declarations, by its name's token, which a use's lookup finds):
// clang's redeclaration inherits an attribute kind's attributes only where it has none of that kind itself (its
// mergeDeclAttributes' DeclHasAttr), so an entry's rules and tags are its own, else its previous declaration's. A variable
// of an integer type with an initializer registers each of its tags for the initializer's value (Sema's
// RegisterTypeTagForDatatype, as FinalizeDeclaration calls it), which a call's integer literal tag finds, the last
// registration before it of its kind and value. A unit without these attributes pays a flag test where declarations are
// read (CheckInputReader.type_tag_attributes) and a null test where its calls' warnings would be.
//
// Left to clang alone, refused by name: a rule's index past a variadic function's parameters, whose calls' errors clang gives
// where an argument isn't there ("type tag index N is greater than the number of arguments specified"); a rule on a member
// or a tag on a parameter, whose declarations a call through a member or a parameter would read; and a type name the
// constant reader doesn't read.
// Included by c_check_input.c after c_check_attribute_checks_input.c.

#define BUSTER_CHECK_TYPE_TAG_LAYOUT 0x1u
#define BUSTER_CHECK_TYPE_TAG_NULL 0x2u

// A rule: argument_with_type_tag's (or pointer_with_type_tag's, `pointer`) kind, the identifier's name, and its argument's
// and tag's places from 1, as clang's getSourceIndex gives them.
typedef struct CheckTypeTagRule CheckTypeTagRule;
struct CheckTypeTagRule
{
    uint32_t kind;
    uint32_t argument;
    uint32_t tag;
    bool pointer;
};

// A tag: type_tag_for_datatype's kind, its type (`spelling` the typedef declaration that names it, else 0) and its flags
// (BUSTER_CHECK_TYPE_TAG_*).
typedef struct CheckTypeTagDatatype CheckTypeTagDatatype;
struct CheckTypeTagDatatype
{
    uint32_t kind;
    TypeId type;
    uint32_t spelling;
    uint32_t flags;
};

// A declaration's rules and tags: `rule_count` rules from `rule_first` (CheckTypeTags.rules) and `datatype_count` tags from
// `datatype_first` (CheckTypeTags.datatypes).
typedef struct CheckTypeTagDeclaration CheckTypeTagDeclaration;
struct CheckTypeTagDeclaration
{
    uint32_t declaration;
    uint32_t rule_first;
    uint32_t rule_count;
    uint32_t datatype_first;
    uint32_t datatype_count;
};

// A registered value: the variable's name's token, the value (its 64 bits, as clang's getZExtValue reads them) and its tag.
typedef struct CheckTypeTagMagic CheckTypeTagMagic;
struct CheckTypeTagMagic
{
    uint32_t token;
    uint64_t value;
    CheckTypeTagDatatype datatype;
};

// A variable whose tags register its initializer's value once the initializer is complete: its name's token, its own tag's
// attribute's first token (BUSTER_CHECK_NONE for an inherited one's) and its tags.
typedef struct CheckTypeTagPending CheckTypeTagPending;
struct CheckTypeTagPending
{
    uint32_t declaration;
    uint32_t attribute;
    uint32_t datatype_first;
    uint32_t datatype_count;
};

typedef struct CheckTypeTags CheckTypeTags;
struct CheckTypeTags
{
    CheckTypeTagRule* rules;
    uint32_t rule_count;
    uint32_t rule_capacity;
    CheckTypeTagDatatype* datatypes;
    uint32_t datatype_count;
    uint32_t datatype_capacity;
    CheckTypeTagDeclaration* declarations;
    uint32_t declaration_count;
    uint32_t declaration_capacity;
    CheckTypeTagMagic* magics;
    uint32_t magic_count;
    uint32_t magic_capacity;
    CheckTypeTagPending* pending;
    uint32_t pending_count;
    uint32_t pending_capacity;
};

// The pending registration of variable `declaration` (by its name's token), or NULL.
static CheckTypeTagPending* check_type_tags_pending(const CheckInput* input, uint32_t declaration)
{
    CheckTypeTags* tags = input->type_tags;
    CheckTypeTagPending* result = NULL;
    for (uint32_t i = 0; tags != NULL && result == NULL && i < tags->pending_count; i += 1)
    {
        result = tags->pending[i].declaration == declaration ? tags->pending + i : NULL;
    }
    return result;
}

// The entry of declaration `declaration` (by binary search), or NULL.
static const CheckTypeTagDeclaration* check_type_tags_declaration(const CheckTypeTags* tags, uint32_t declaration)
{
    uint32_t low = 0;
    uint32_t high = tags != NULL ? tags->declaration_count : 0;
    const CheckTypeTagDeclaration* result = NULL;
    while (result == NULL && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const uint32_t at = tags->declarations[middle].declaration;
        result = at == declaration ? tags->declarations + middle : NULL;
        high = declaration < at ? middle : high;
        low = declaration > at ? middle + 1 : low;
    }
    return result;
}

// The tag registered last before `token` of kind `kind` for `value`, or NULL.
static const CheckTypeTagDatatype* check_type_tags_magic(const CheckTypeTags* tags, uint32_t kind, uint64_t value, uint32_t token)
{
    const CheckTypeTagDatatype* result = NULL;
    for (uint32_t i = 0; tags != NULL && i < tags->magic_count && tags->magics[i].token < token; i += 1)
    {
        const CheckTypeTagMagic* magic = tags->magics + i;
        result = magic->datatype.kind == kind && magic->value == value ? &magic->datatype : result;
    }
    return result;
}

// An array of `size`-byte items grown by doubling to hold one more (`*items`, `*count` used of `*capacity`): whether it does.
static bool check_type_tags_reserve(CheckInputReader* reader, void** items, uint32_t count, uint32_t* capacity, uint64_t size)
{
    bool result = count < *capacity;
    if (!result)
    {
        const uint32_t grown_capacity = *capacity != 0 ? *capacity * 2 : 8;
        void* grown = arena_push_array(reader->arena, grown_capacity, size, 8);
        if (grown != NULL && count != 0)
        {
            memcpy(grown, *items, (uint64_t)count * size);
        }
        reader->output->resource |= grown == NULL;
        *items = grown != NULL ? grown : *items;
        *capacity = grown != NULL ? grown_capacity : *capacity;
        result = grown != NULL;
    }
    return result;
}

// type_tag_for_datatype's arguments as clang's ParseTypeTagForDatatypeAttribute reads them (#1447): an identifier, its kind;
// a `,` and a type name; then each `,` and a flag, layout_compatible or must_be_null; then its `)`. Its errors are clang's
// parser's, at the token ("expected identifier", "expected ','" past the kind, "unknown type name 'T'" or "expected a type",
// "invalid comparison flag 'F'; use 'layout_compatible' or 'must_be_null'", "expected ')'" with its note), each of which
// drops the attribute. The flags are `value`'s low half, with BUSTER_CHECK_TYPE_TAG_PENDING: the type name is read after the
// list, by check_input_type_tag_type, which the constant reader's own attribute lists don't reach (the no-recursion rule:
// a type name's constants may hold declarations with attributes).
static uint32_t check_input_type_tag_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t end = attribute->argument_end;
    const uint32_t open = attribute->argument_first - 1;
    const uint32_t kind = attribute->argument_first;
    uint32_t result = BUSTER_CHECK_CUSTOM_KEPT;
    if (kind >= end || parser->tokens.kinds[kind] != PARSE_KIND_IDENTIFIER)
    {
        check_input_custom_error(reader, attributes, attribute, kind, CHECK_ARGUMENT_TYPE_TAG_IDENTIFIER, 0, BUSTER_CHECK_NONE);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    else if (kind + 1 >= end || parser->tokens.kinds[kind + 1] != PARSE_KIND_COMMA)
    {
        // (Past the kind's end, as clang's ExpectAndConsume expects it.)
        check_input_custom_error(reader, attributes, attribute, kind, CHECK_ARGUMENT_AVAILABILITY_COMMA, 0, BUSTER_CHECK_NONE);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    const uint32_t type_first = kind + 2;
    uint32_t type_end = type_first;
    while (result == BUSTER_CHECK_CUSTOM_KEPT && type_end < end && parser->tokens.kinds[type_end] != PARSE_KIND_COMMA)
    {
        type_end = check_input_attribute_after_group(parser, type_end, end);
    }
    const bool starts = result == BUSTER_CHECK_CUSTOM_KEPT && type_first < end && check_input_attribute_type_start(reader, type_first);
    if (result == BUSTER_CHECK_CUSTOM_KEPT && !starts)
    {
        const bool named = type_first < end && parser->tokens.kinds[type_first] == PARSE_KIND_IDENTIFIER;
        check_input_attribute_argument_error(reader, attributes, attribute, type_first, named ? CHECK_ARGUMENT_UNKNOWN_TYPE : CHECK_ARGUMENT_EXPECTED_TYPE,
                                             named ? type_first - attribute->first : 0);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    uint32_t flags = 0;
    uint32_t t = type_end;
    while (result == BUSTER_CHECK_CUSTOM_KEPT && t < end && parser->tokens.kinds[t] == PARSE_KIND_COMMA)
    {
        t += 1;
        const uint32_t word = t < end && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[t] : 0;
        const String8 text = word != 0 ? check_input_attribute_name(reader, word) : (String8){ 0 };
        // (check_input_attribute_name strips a keyword's underscores, which clang's isStr doesn't: the flag is as written.)
        const bool written = word != 0 && intern_length(reader->pp->table, word) == text.length;
        const uint32_t flag = !written ? 0 : check_input_attribute_equal(text, "layout_compatible") ? BUSTER_CHECK_TYPE_TAG_LAYOUT :
                              check_input_attribute_equal(text, "must_be_null") ? BUSTER_CHECK_TYPE_TAG_NULL : 0;
        if (word == 0)
        {
            check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_TYPE_TAG_IDENTIFIER, 0, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (flag == 0)
        {
            check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_TYPE_TAG_FLAG, 0, t);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        flags |= flag;
        t += 1;
    }
    if (result == BUSTER_CHECK_CUSTOM_KEPT && t != end)
    {
        check_input_custom_close(reader, attributes, attribute, t, open);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    attribute->argument_type = 0;
    attribute->value = result == BUSTER_CHECK_CUSTOM_KEPT ? (int64_t)(flags | BUSTER_CHECK_TYPE_TAG_PENDING) : 0;
    return result;
}

// A type_tag_for_datatype's type name, once check_input_read_attributes has its list (#1447, BUSTER_CHECK_TYPE_TAG_PENDING):
// CheckInputAttribute.argument_type (a lone typedef name's by its binding, its declaration in `value`'s high half for the
// type's spelling), or, for a type name the constant reader doesn't read (a tag's definition, a declarator it refuses),
// the attribute refused by name.
static void check_input_type_tag_type(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t end = attribute->argument_end;
    const uint32_t type_first = attribute->argument_first + 2;
    uint32_t type_end = type_first;
    while (type_end < end && parser->tokens.kinds[type_end] != PARSE_KIND_COMMA)
    {
        type_end = check_input_attribute_after_group(parser, type_end, end);
    }
    // A lone typedef name, which the constant reader would take for an expression's operand, by its binding.
    const bool lone = type_first + 1 == type_end && parser->tokens.kinds[type_first] == PARSE_KIND_IDENTIFIER;
    const uint32_t name = parser->tokens.names[type_first];
    const uint32_t handle = lone ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) : 0;
    TypeId type = 0;
    uint32_t spelling = 0;
    if (handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF)
    {
        spelling = reader->bindings.declaration[handle - 1];
        type = reader->declared_types[spelling];
        reader->output->declarations[type_first] = spelling;
        reader->declared_types[type_first] = type;
        reader->output->resource |= c_bindings_use(&reader->bindings, C_BINDING_ORDINARY, name, type_first, check_input_use_point(reader, type_first)) == 0;
    }
    else
    {
        type = check_input_constant_type(reader, type_first, type_end);
    }
    const uint32_t flags = (uint32_t)attribute->value & (BUSTER_CHECK_TYPE_TAG_LAYOUT | BUSTER_CHECK_TYPE_TAG_NULL);
    if (type == 0)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
    }
    attribute->argument_type = type;
    attribute->value = type != 0 ? (int64_t)((uint64_t)spelling << 32 | flags) : 0;
}

// The type tags' handlers' checks past their subjects and counts (#1447, clang's handleArgumentWithTypeTagAttr and
// handleTypeTagForDatatypeAttr): a rule's argument's and tag's indices, each an integer constant ("'X' attribute requires
// parameter N to be an integer constant") that names a parameter, a variadic function's past them too ("'X' attribute
// parameter N is out of bounds"), both at the attribute; pointer_with_type_tag's argument's parameter of a pointer type, else
// "'pointer_with_type_tag' attribute only applies to pointer arguments" there, which clang gives and keeps the attribute
// for; a tag's declaration of no variable, "'type_tag_for_datatype' attribute only applies to variables" there. Whether the
// declaration goes on.
static bool check_input_type_tag_handled(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, uint32_t declaration, TypeId type)
{
    const String8 spelled = check_input_attribute_name(reader, attribute->name);
    const bool rule = check_input_type_tag_rule(spelled);
    const bool datatype = check_input_attribute_equal(spelled, "type_tag_for_datatype");
    const TypeTable* types = reader->types;
    TypeId function = (rule || datatype) && type != 0 ? type_strip_aligned(types, type) : 0;
    function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(types, type_element(types, function)) : function;
    const bool is_function = function != 0 && type_kind(types, function) == BUSTER_TYPE_KIND_FUNCTION;
    uint32_t count = 0;
    const TypeId* parameters = is_function ? type_parameters(types, function, &count) : NULL;
    const bool variadic = is_function && (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_VARIADIC) != 0;
    // The kind's identifier: its handler's first check, of its parser's first argument (none without parentheses).
    const Parser* parser = reader->parser;
    const uint32_t kind = attribute->argument_first;
    const bool identifier = kind < attribute->argument_end && parser->tokens.kinds[kind] == PARSE_KIND_IDENTIFIER &&
                            (kind + 1 == attribute->argument_end || parser->tokens.kinds[kind + 1] == PARSE_KIND_COMMA);
    bool valid = true;
    if ((!rule && !datatype) || attribute->count_check != BUSTER_CHECK_COUNT_NONE)
    {
        // Another attribute's, or its count's error, which comes first (check_input_attribute_subjects).
    }
    else if (!identifier)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_IDENTIFIER, 1);
        valid = false;
    }
    else if (rule && attribute->argument_count == 3 && attribute->argument_values != NULL && is_function)
    {
        uint32_t indices[2] = { 0, 0 };
        for (uint32_t argument = 1; valid && argument < 3; argument += 1)
        {
            const bool constant = ((attribute->constants >> argument) & 1u) != 0;
            const bool wide = ((attribute->wide >> argument) & 1u) != 0;
            // clang's checkFunctionOrMethodParameterIndex: the value as getLimitedValue(UINT_MAX) clamps it.
            const uint32_t index = wide ? UINT32_MAX : (uint32_t)attribute->argument_values[argument];
            if (!constant)
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, argument + 1);
                valid = false;
            }
            else if (index < 1 || (!variadic && index > count))
            {
                check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OUT_OF_BOUNDS, argument + 1);
                valid = false;
            }
            indices[argument - 1] = index;
        }
        // (A variadic argument's place is no parameter's, and of no pointer type.)
        const bool pointer = check_input_attribute_equal(spelled, "pointer_with_type_tag");
        const TypeId parameter = valid && indices[0] <= count ? type_strip_aligned(types, parameters[indices[0] - 1]) : 0;
        if (valid && pointer && (parameter == 0 || type_kind(types, parameter) != BUSTER_TYPE_KIND_POINTER))
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OWNERSHIP_TYPE, 0);
            valid = false;
        }
        if (valid && (indices[0] > count || indices[1] > count))
        {
            // A variadic argument's place, whose calls' errors where it isn't there are clang's alone: refused by name.
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
        if (valid && (declaration & BUSTER_CHECK_SUBJECT_FIELD) != 0)
        {
            // A member's, which a call through the member would read: refused by name.
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
    }
    else if (datatype && (declaration & (BUSTER_CHECK_SUBJECT_VARIABLE | BUSTER_CHECK_SUBJECT_PARAMETER)) == 0)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_OBJC_VARIABLES, 0);
        valid = false;
    }
    else if (datatype && (declaration & BUSTER_CHECK_SUBJECT_PARAMETER) != 0)
    {
        // A parameter's, which a call's tag naming the parameter would read: refused by name.
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
        valid = false;
    }
    return valid;
}

// A declaration's rules and tags (#1447, CheckTypeTags.declarations), its own or else its previous declaration's of each
// kind, and its tags' registered values where it is a variable of an integer type whose initializer runs from `initializer`
// (its `=`, else BUSTER_CHECK_NONE): that initializer of no integer constant is clang's error at each tag. `name` is the
// declaration's name's token. Whether the declaration goes on.
static bool check_input_type_tag_declaration(CheckInputReader* reader, CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t previous,
                                             uint32_t initializer)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    CheckTypeTags* tags = input->type_tags;
    if (tags == NULL)
    {
        tags = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckTypeTags, 1);
        input->resource |= tags == NULL;
        if (tags != NULL)
        {
            memset(tags, 0, sizeof(*tags));
        }
        input->type_tags = tags;
    }
    bool valid = tags != NULL;
    const CheckTypeTagDeclaration* inherited = valid && previous != BUSTER_CHECK_NONE ? check_type_tags_declaration(tags, previous) : NULL;
    CheckTypeTagDeclaration entry = { .declaration = name };
    if (inherited != NULL)
    {
        entry = *inherited;
        entry.declaration = name;
    }
    uint32_t own_rules = 0;
    uint32_t own_datatypes = 0;
    for (uint32_t i = 0; valid && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool kept = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && !attribute->ignored && !attribute->subject_ignored;
        const String8 spelled = kept ? check_input_attribute_name(reader, attribute->name) : (String8){ 0 };
        if (kept && check_input_type_tag_rule(spelled) && attribute->argument_count == 3 && attribute->argument_values != NULL)
        {
            valid = check_type_tags_reserve(reader, (void**)&tags->rules, tags->rule_count, &tags->rule_capacity, sizeof(CheckTypeTagRule));
            if (valid)
            {
                entry.rule_first = own_rules == 0 ? tags->rule_count : entry.rule_first;
                own_rules += 1;
                tags->rules[tags->rule_count++] = (CheckTypeTagRule){
                    .kind = parser->tokens.names[attribute->argument_first],
                    .argument = (uint32_t)attribute->argument_values[1],
                    .tag = (uint32_t)attribute->argument_values[2],
                    .pointer = check_input_attribute_equal(spelled, "pointer_with_type_tag"),
                };
            }
        }
        else if (kept && check_input_attribute_equal(spelled, "type_tag_for_datatype") && attribute->argument_type != 0)
        {
            valid = check_type_tags_reserve(reader, (void**)&tags->datatypes, tags->datatype_count, &tags->datatype_capacity, sizeof(CheckTypeTagDatatype));
            if (valid)
            {
                entry.datatype_first = own_datatypes == 0 ? tags->datatype_count : entry.datatype_first;
                own_datatypes += 1;
                tags->datatypes[tags->datatype_count++] = (CheckTypeTagDatatype){
                    .kind = parser->tokens.names[attribute->argument_first],
                    .type = attribute->argument_type,
                    .spelling = (uint32_t)((uint64_t)attribute->value >> 32),
                    .flags = (uint32_t)attribute->value & (BUSTER_CHECK_TYPE_TAG_LAYOUT | BUSTER_CHECK_TYPE_TAG_NULL),
                };
            }
        }
    }
    entry.rule_count = own_rules != 0 ? own_rules : entry.rule_count;
    entry.datatype_count = own_datatypes != 0 ? own_datatypes : entry.datatype_count;
    // The registered values: a variable of an integer or enumeration type with an initializer, each of its tags, once its
    // initializer is complete (check_input_type_tag_finish). Its own tags' attribute is where an initializer of no integer
    // constant is an error; an inherited one's is another declaration's.
    const TypeId bare = type != 0 ? type_unqualified(type_strip_aligned(reader->types, type)) : 0;
    const uint8_t kind = bare != 0 ? type_kind(reader->types, bare) : BUSTER_TYPE_KIND_NONE;
    const bool integral = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_ENUM;
    if (valid && integral && initializer != BUSTER_CHECK_NONE && entry.datatype_count != 0)
    {
        uint32_t attribute_token = BUSTER_CHECK_NONE;
        for (uint32_t r = 0; own_datatypes != 0 && attribute_token == BUSTER_CHECK_NONE && attributes != NULL && r < attributes->count; r += 1)
        {
            const CheckInputAttribute* attribute = attributes->records + r;
            attribute_token = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword && attribute->argument_type != 0 && !attribute->ignored &&
                              check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "type_tag_for_datatype") ? attribute->first :
                              BUSTER_CHECK_NONE;
        }
        valid = check_type_tags_reserve(reader, (void**)&tags->pending, tags->pending_count, &tags->pending_capacity, sizeof(CheckTypeTagPending));
        if (valid)
        {
            tags->pending[tags->pending_count++] = (CheckTypeTagPending){
                .declaration = name,
                .attribute = attribute_token,
                .datatype_first = entry.datatype_first,
                .datatype_count = entry.datatype_count,
            };
        }
    }
    if (valid && (entry.rule_count != 0 || entry.datatype_count != 0))
    {
        valid = check_type_tags_reserve(reader, (void**)&tags->declarations, tags->declaration_count, &tags->declaration_capacity, sizeof(CheckTypeTagDeclaration));
        // In ascending order of the declarations' tokens: source order appends, and one read out of it is moved into place.
        uint32_t at = valid ? tags->declaration_count : 0;
        while (valid && at > 0 && tags->declarations[at - 1].declaration > name)
        {
            tags->declarations[at] = tags->declarations[at - 1];
            at -= 1;
        }
        if (valid && at > 0 && tags->declarations[at - 1].declaration == name)
        {
            // The same declaration read again: its entry is replaced.
            memmove(tags->declarations + at, tags->declarations + at + 1, (uint64_t)(tags->declaration_count - at) * sizeof(CheckTypeTagDeclaration));
            at -= 1;
            tags->declaration_count -= 1;
        }
        if (valid)
        {
            tags->declarations[at] = entry;
            tags->declaration_count += 1;
        }
    }
    return valid;
}

// A pending variable's registrations, once its initializer from `begin` to `end` is complete (#1447, from
// check_input_initializer_finish): its value as an integer constant (its 64 bits) for each of its tags, each the last
// registration of its kind and value from `end` on; an initializer of no integer constant is clang's error at its own tag's
// attribute (FinalizeDeclaration's, after the initializer's own errors: a recovered report at the initializer's end), and
// registers nothing. Whether the declaration goes on.
static bool check_input_type_tag_finish(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    CheckTypeTags* tags = input->type_tags;
    CheckTypeTagPending* pending = check_type_tags_pending(input, declaration);
    bool valid = true;
    if (pending != NULL)
    {
        // A scalar's braced initializer, `{ 1 }`, is its element's value (an initializer list clang folds as an integer
        // constant).
        while (begin + 1 < end && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE && parser->tree.parents[begin] == end - 1)
        {
            begin += 1;
            end -= 1;
        }
        CheckValue value = { 0 };
        int64_t number = 0;
        const bool constant = check_input_evaluate(reader, begin, end, &value) && check_input_const_rvalue(reader, end, &value) &&
                              check_input_integer_attribute(reader, &value) && value.constant_kind == CHECK_CONSTANT_INTEGER && value.fault == CHECK_FAULT_NONE &&
                              check_action_i64(reader->types, &value, &number);
        if (!constant && pending->attribute != BUSTER_CHECK_NONE)
        {
            const uint32_t before = input->report_count;
            const CheckInputAttribute at = { .first = pending->attribute, .name_token = pending->attribute, .end = pending->attribute + 1 };
            CheckInputAttributes one = { 0 };
            check_input_attribute_argument_error(reader, &one, &at, pending->attribute, CHECK_ARGUMENT_TYPE_TAG_INITIALIZER, 0);
            for (uint32_t r = before; r < input->report_count; r += 1)
            {
                input->reports[r].completion = end;
            }
            valid = input->report_count > before && check_input_recover_reports(reader, before);
        }
        else if (!constant)
        {
            // An inherited tag, whose attribute is another declaration's: refused by name.
            check_input_report(reader, declaration, CHECK_INPUT_UNSUPPORTED);
            valid = false;
        }
        for (uint32_t i = 0; constant && valid && i < pending->datatype_count; i += 1)
        {
            const CheckTypeTagDatatype datatype = tags->datatypes[pending->datatype_first + i];
            valid = check_type_tags_reserve(reader, (void**)&tags->magics, tags->magic_count, &tags->magic_capacity, sizeof(CheckTypeTagMagic));
            // In ascending order of their initializers' ends: source order appends.
            uint32_t at = valid ? tags->magic_count : 0;
            while (valid && at > 0 && tags->magics[at - 1].token > end)
            {
                tags->magics[at] = tags->magics[at - 1];
                at -= 1;
            }
            if (valid)
            {
                tags->magics[at] = (CheckTypeTagMagic){ .token = end, .value = (uint64_t)number, .datatype = datatype };
                tags->magic_count += 1;
            }
        }
        pending->declaration = BUSTER_CHECK_NONE;
    }
    return valid;
}
