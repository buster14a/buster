// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Cold source type annotations. Canonical types, layout, compatibility and CheckValue remain unchanged. The sparse
// token column owns no records: immutable arena shadows can be shared by typedefs and by deferred bodies. A pointer
// is complemented like CheckInput.attributes, so null annotations neither claim nor dirty a sparse block.

static const CheckNoderefType* check_input_noderef_at(const CheckInput* input, uint32_t token)
{
    return check_sparse_wide_present(input->noderef_types) && token < input->token_count ?
           (const CheckNoderefType*)(uintptr_t)~check_sparse_wide_get(input->noderef_types, token) : NULL;
}

static void check_input_noderef_set(CheckInput* input, uint32_t token, const CheckNoderefType* type)
{
    if (check_sparse_wide_present(input->noderef_types) && token < input->token_count)
    {
        check_sparse_wide_set(input->sparse_pool, input->noderef_types, token, ~(uint64_t)(uintptr_t)type);
    }
}

static const CheckNoderefType* check_input_noderef_make(CheckInputReader* reader, const CheckNoderefType* base, const CheckNoderefParameter* parameters, bool marked)
{
    CheckNoderefType* result = NULL;
    if (base != NULL || parameters != NULL || marked)
    {
        result = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckNoderefType, 1);
        reader->output->resource |= result == NULL;
        if (result != NULL)
        {
            *result = (CheckNoderefType){ .base = base, .parameters = parameters, .marked = marked };
        }
    }
    return result;
}

static const CheckNoderefType* check_input_noderef_wrap(CheckInputReader* reader, const CheckNoderefType* base)
{
    return check_input_noderef_make(reader, base, NULL, false);
}

static const CheckNoderefType* check_input_noderef_mark(CheckInputReader* reader, const CheckNoderefType* type)
{
    return type != NULL && type->marked ? type : check_input_noderef_make(reader, type != NULL ? type->base : NULL, type != NULL ? type->parameters : NULL, true);
}

static const CheckNoderefType* check_input_noderef_base(const CheckNoderefType* type)
{
    return type != NULL ? type->base : NULL;
}

static const CheckNoderefType* check_input_noderef_parameter(const CheckNoderefType* type, uint32_t index)
{
    const CheckNoderefParameter* parameter = type != NULL ? type->parameters : NULL;
    while (parameter != NULL && parameter->index != index)
    {
        parameter = parameter->next;
    }
    return parameter != NULL ? parameter->type : NULL;
}

static const CheckNoderefParameter* check_input_noderef_parameter_add(CheckInputReader* reader, const CheckNoderefParameter* parameters,
                                                                   const CheckNoderefType* type, uint32_t index)
{
    const CheckNoderefParameter* result = parameters;
    if (type != NULL)
    {
        CheckNoderefParameter* item = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckNoderefParameter, 1);
        reader->output->resource |= item == NULL;
        if (item != NULL)
        {
            *item = (CheckNoderefParameter){ .type = type, .next = parameters, .index = index };
            result = item;
        }
    }
    return result;
}

// Parameter adjustment and value conversion have the same annotation shape for arrays and pointers. An atomic's
// value loses its atomic wrapper; a function designator acquires a pointer wrapper. Aligned types carry no shadow
// level: alignment is canonical representation, not a source declarator operator.
static const CheckNoderefType* check_input_noderef_value(CheckInputReader* reader, TypeId type, const CheckNoderefType* shadow)
{
    const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, type));
    const CheckNoderefType* result = kind == BUSTER_TYPE_KIND_ATOMIC ? check_input_noderef_base(shadow) : shadow;
    if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        result = check_input_noderef_wrap(reader, result);
    }
    return result;
}

// Attributes on the base specifiers mark that source level. The declarator checks the following pointer/array
// chunk separately; even an invalid placement retains its annotation, as a later typedef use can wrap it.
static const CheckNoderefType* check_input_noderef_attributes(CheckInputReader* reader, const CheckInputAttributes* attributes, const CheckNoderefType* type)
{
    const CheckNoderefType* result = type;
    const uint32_t count = attributes != NULL ? attributes->count : 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (attributes->records[i].kind == CHECK_ATTRIBUTE_NODEREF && !attributes->records[i].ignored)
        {
            result = check_input_noderef_mark(reader, result);
        }
    }
    return result;
}

static bool check_input_noderef_new_attributes(const CheckInputAttributes* attributes, uint32_t first)
{
    const uint32_t count = attributes != NULL ? attributes->count : 0;
    bool found = false;
    for (uint32_t i = first; i < count; i += 1)
    {
        found |= attributes->records[i].kind == CHECK_ATTRIBUTE_NODEREF && !attributes->records[i].ignored;
    }
    return found;
}

// The diagnostic's location is the declaration's beginning, including its specifiers. Parameter frames carry that
// beginning explicitly; an ordinary declarator starts after them, and its preceding parser siblings lead back to it.
static uint32_t check_input_noderef_declaration_first(const Parser* parser, uint32_t token)
{
    uint32_t first = token;
    uint32_t depth = 0;
    while (first != 0)
    {
        const uint8_t kind = parser->tokens.kinds[first - 1];
        const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE;
        const bool close = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE;
        if (depth == 0 && (open || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_R_BRACE))
        {
            break;
        }
        depth = close ? depth + 1 : open && depth != 0 ? depth - 1 : depth;
        first -= 1;
    }
    return first;
}

static void check_input_noderef_placement(CheckInputReader* reader, uint32_t token, uint32_t completion)
{
    const CheckWarningEvent event =
    {
        .id = CHECK_WARNING_ID_IGNORED_ATTRIBUTES,
        .kind = CHECK_WARNING_EVENT_NODEREF_PLACEMENT,
        .token = token,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = completion,
        .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE },
    };
    check_input_warning_once(reader, &event);
}

// A visible function redeclaration's composite keeps the previous return type's sugar, and its prototype's parameter
// sugar when one exists. Parameters of a definition keep their own declaration shadows independently. Objects, whose
// latest declaration supplies their source type, never pass through this function.
static void check_input_noderef_redeclaration(CheckInputReader* reader, uint32_t token, uint32_t previous)
{
    if (check_sparse_wide_present(reader->output->noderef_types))
    {
        const CheckNoderefType* before = check_input_noderef_at(reader->output, previous);
        const CheckNoderefType* own = check_input_noderef_at(reader->output, token);
        const TypeId type = type_strip_aligned(reader->types, reader->declared_types[previous]);
        const bool prototype = (reader->types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
        const CheckNoderefType* parameters = prototype ? before : own;
        check_input_noderef_set(reader->output, token, check_input_noderef_make(reader, check_input_noderef_base(before),
                               parameters != NULL ? parameters->parameters : NULL, before != NULL && before->marked));
    }
}

// Completed records' annotated fields, in member-index order. A deferred body that declares one requests the
// existing source-order replay before publishing any member mapping; lane-local member IDs must never escape.
static void check_input_noderef_member_add(CheckInputReader* reader, uint32_t member, uint32_t token, const CheckNoderefType* type)
{
    CheckInput* input = reader->output;
    if (type != NULL)
    {
        check_input_lane_refuse(reader, reader->lane, "a body declares a noderef-annotated member");
        if (!reader->lane && input->noderef_member_count == input->noderef_member_capacity)
        {
            const uint32_t capacity = input->noderef_member_capacity == 0 ? 16 :
                                      input->noderef_member_capacity <= UINT32_MAX / 2 ? input->noderef_member_capacity * 2 : 0;
            CheckNoderefMember* members = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckNoderefMember, capacity) : NULL;
            input->resource |= members == NULL;
            if (members != NULL)
            {
                if (input->noderef_member_count != 0)
                {
                    memcpy(members, input->noderef_members, input->noderef_member_count * sizeof(CheckNoderefMember));
                }
                input->noderef_members = members;
                input->noderef_member_capacity = capacity;
            }
        }
        if (!reader->lane && input->noderef_member_count < input->noderef_member_capacity)
        {
            input->noderef_members[input->noderef_member_count++] = (CheckNoderefMember){ .type = type, .member = member, .token = token };
        }
    }
}

static const CheckNoderefType* check_input_noderef_member(const CheckInput* input, uint32_t member)
{
    uint32_t first = 0;
    uint32_t end = input->noderef_member_count;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (input->noderef_members[middle].member < member)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < input->noderef_member_count && input->noderef_members[first].member == member ? input->noderef_members[first].type : NULL;
}

typedef struct CheckNoderefPair CheckNoderefPair;
struct CheckNoderefPair
{
    const CheckNoderefType* left;
    const CheckNoderefType* right;
    const CheckNoderefPair* next;
};

// A conditional retains shared sugar only when the two operands' annotations agree. Typedef sharing makes the
// ordinary case a pointer comparison. Independently written nested prototypes use an explicit work list, not the
// host call stack; pointer/array/result chains are compared directly without a record per level.
static bool check_input_noderef_equal(CheckInputReader* reader, const CheckNoderefType* left, const CheckNoderefType* right)
{
    const uint64_t mark = reader->scratch->length;
    const CheckNoderefPair* pending = NULL;
    bool equal = true;
    do
    {
        while (equal && left != right)
        {
            equal = left != NULL && right != NULL && left->marked == right->marked;
            const CheckNoderefParameter* a = equal ? left->parameters : NULL;
            const CheckNoderefParameter* b = equal ? right->parameters : NULL;
            while (equal && a != b)
            {
                equal = a != NULL && b != NULL && a->index == b->index;
                if (equal && a->type != b->type)
                {
                    CheckNoderefPair* pair = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckNoderefPair, 1);
                    reader->output->resource |= pair == NULL;
                    equal = pair != NULL;
                    if (equal)
                    {
                        *pair = (CheckNoderefPair){ .left = a->type, .right = b->type, .next = pending };
                        pending = pair;
                    }
                }
                a = equal ? a->next : NULL;
                b = equal ? b->next : NULL;
            }
            left = equal ? left->base : NULL;
            right = equal ? right->base : NULL;
        }
        left = pending != NULL ? pending->left : NULL;
        right = pending != NULL ? pending->right : NULL;
        pending = pending != NULL ? pending->next : NULL;
    } while (equal && (left != NULL || right != NULL));
    arena_pop_to(reader->scratch, mark);
    return equal;
}

static const CheckNoderefType* check_input_noderef_function(CheckInputReader* reader, TypeId type, const CheckNoderefType* shadow)
{
    const TypeTable* types = reader->types;
    type = type_strip_aligned(types, type);
    if (type_kind(types, type) == BUSTER_TYPE_KIND_ATOMIC)
    {
        type = type_strip_aligned(types, type_element(types, type));
        shadow = check_input_noderef_base(shadow);
    }
    return type_kind(types, type) == BUSTER_TYPE_KIND_POINTER ? check_input_noderef_base(shadow) : shadow;
}

// Shared by the source evaluator and the checked graph's warning pass. Only source sugar is transferred; canonical
// result types and value categories have already been checked by the ordinary action. Explicit casts carry their
// type-name shadow in `destination`, as initializers and return conversions carry their destination's shadow.
static const CheckNoderefType* check_input_noderef_action(CheckInputReader* reader, CheckOp op, const TypeId* types,
                                                       const CheckNoderefType* const* shadows, const CheckNoderefType* destination, uint32_t member)
{
    const CheckNoderefType* result = NULL;
    if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE)
    {
        result = destination;
    }
    else if (op == CHECK_OP_SELECT || op == CHECK_OP_STATEMENT_VALUE)
    {
        result = shadows[0];
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        result = check_input_noderef_wrap(reader, shadows[0]);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        result = check_input_noderef_base(check_input_noderef_value(reader, types[0], shadows[0]));
    }
    else if (op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
    {
        const uint8_t kind = type_kind(reader->types, type_strip_aligned(reader->types, type_conversion_operand(reader->types, types[0])));
        const uint32_t operand = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? 0 : 1;
        result = check_input_noderef_value(reader, types[operand], shadows[operand]);
        result = op == CHECK_OP_SUBSCRIPT ? check_input_noderef_base(result) : result;
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_ADD_ASSIGN || op == CHECK_OP_SUBTRACT_ASSIGN ||
             op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT)
    {
        result = check_input_noderef_value(reader, types[0], shadows[0]);
    }
    else if (op == CHECK_OP_COMMA)
    {
        result = check_input_noderef_value(reader, types[1], shadows[1]);
    }
    else if (op == CHECK_OP_CONDITIONAL)
    {
        const CheckNoderefType* a = check_input_noderef_value(reader, types[1], shadows[1]);
        const CheckNoderefType* b = check_input_noderef_value(reader, types[2], shadows[2]);
        const uint8_t ak = type_kind(reader->types, type_strip_aligned(reader->types, types[1]));
        const uint8_t bk = type_kind(reader->types, type_strip_aligned(reader->types, types[2]));
        const bool ap = ak == BUSTER_TYPE_KIND_POINTER || ak == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(ak);
        const bool bp = bk == BUSTER_TYPE_KIND_POINTER || bk == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(bk);
        result = !ap ? b : !bp ? a : check_input_noderef_equal(reader, a, b) ? a : NULL;
    }
    else if (op == CHECK_OP_CALL)
    {
        result = check_input_noderef_base(check_input_noderef_function(reader, types[0], shadows[0]));
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        TypeId record = type_strip_aligned(reader->types, types[0]);
        if (op == CHECK_OP_ARROW)
        {
            record = type_strip_aligned(reader->types, type_element(reader->types, type_conversion_operand(reader->types, record)));
        }
        const uint8_t kind = type_kind(reader->types, record);
        uint64_t offset = 0;
        uint32_t qualifiers = 0;
        const uint32_t index = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ?
                               type_lookup_member(reader->types, record, member, &offset, &qualifiers) : 0;
        result = index != 0 && index != BUSTER_TYPE_MEMBER_UNKNOWN ? check_input_noderef_member(reader->output, index) : NULL;
    }
    return result;
}
