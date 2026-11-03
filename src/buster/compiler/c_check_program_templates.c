// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's node templates, included by c_check_program_lower.c before the tasks' loop. Before a body's statements, three
// steps over its range of nodes (check_lower_template_range), each of the shape a data-parallel pass takes:
//
// 1. The scatter (check_lower_template_scatter), from the last node down: each node's rule, by its op alone
//    (check_lower_template_rules), gives its operands their contexts (a value or an address) and the targets their
//    conversions convert them to, an operand two nodes name being shared; a node the tasks fold
//    (check_lower_template_folds) gives its operands, and theirs, no evaluation instead.
// 2. The classification, each node on its own from the first up: its context with its discard, its template and its
//    entries and its conversion's in its slot of BUSTER_CHECK_LOWER_TEMPLATE_MOST, by the fast twin where it takes the node
//    (check_lower_template_fast: the common shapes, each by its own rules alone) and by the general rules
//    (check_lower_template_classify) otherwise. A template takes its operands only where the scatter gave each the
//    context and the target its rule gives it.
// 3. In the same step, once its operands are done: its entries' offset in node order, which is postorder, and whether
//    its subtree is closed: a template whose evaluated operands are closed and converted, each operand's entries (its
//    subtree's, then its conversion's) following the one before's, and the last one's ending where the node's own start.
//
// When the tasks' loop pops a closed node in the context the pass gave it (check_lower_template_splice), it copies the
// slots of the node's subtree in node order, but the node's own conversion, which the task under it writes, instead of
// expanding the node and its operands task by task. Everything else (the controls, the exceptions, a node out of the
// body's range or in another context) takes the tasks as before, which remain the exact rules: the table's templates
// write what check_lower_expand and the conversions write for the cases they take, which test-lowering-templates checks
// function by function, and the fast twin what the general rules give, which its check compares node by node.
//
// The table (CheckLowerTemplate): a VALUE's read of a local or a global, its address; a folded constant (an integer, a
// float or double, an address); `&x`; a cast, a conversion, a unary plus and a selection; `*p`; a member's access; the
// negation and the complement; the truth tests (`!x`, a condition); the comma; the arithmetic, bitwise and comparison
// operators of scalar classes; pointer plus or minus an integer and a subscript; and an assignment or initialization of
// a scalar to a local, a global or through an address. Each only for classes I8 to F64 (no memory class, half-precision,
// vector or aggregate value but an address), never for an atomic, complex, bit-field or _BitInt value, an overload, a
// compound literal or a variable-length array. A writer takes templates only where its caller asks
// (check_program_lower_templates, the tests), and not with -g's tokens, a tracked stack or a program with a _BitInt type or
// an overloaded address: on the 9700X the scalar steps cost about three times what the splices save (docs/c-lowering.md).
// On the vector host path, the scatter (check_lower_template_scatter_vector) and the classification of the fast twin's
// common shapes with their conversions (check_lower_template_vector) take 16 nodes at a time; the scalar steps are their
// twins, which check_program_lower_templates' check compares node by node, and take the nodes the vector ones leave.
#ifndef BUSTER_C_CHECK_PROGRAM_TEMPLATES_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_TEMPLATES_INCLUDED

// The most nodes a body's range may have, for a writer's columns: a body past them is lowered by the tasks alone.
#define BUSTER_CHECK_LOWER_TEMPLATE_NODES (UINT32_C(1) << 18)
// The most entries one node's template and conversion write, its slot's: an address constant's four with a conversion to
// _Bool's four.
#define BUSTER_CHECK_LOWER_TEMPLATE_MOST 8u

// A node's state in its body's pass.
#define BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED 0x01u
#define BUSTER_CHECK_LOWER_TEMPLATE_SHARED 0x02u
#define BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED 0x04u
#define BUSTER_CHECK_LOWER_TEMPLATE_CLOSED 0x08u
#define BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED 0x10u
#define BUSTER_CHECK_LOWER_TEMPLATE_TAKEN_ADDRESS 0x20u

// A node's context: evaluated for its address, or discarded (check_lower_expression's), as the tasks' flags have them.
#define BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS 0x01u
#define BUSTER_CHECK_LOWER_TEMPLATE_DISCARD 0x04u

typedef enum CheckLowerTemplate
{
    CHECK_LOWER_TEMPLATE_NONE,
    // A folded constant: check_lower_constant's entries, or none where it is discarded.
    CHECK_LOWER_TEMPLATE_CONSTANT,
    // A VALUE of type void: no entry.
    CHECK_LOWER_TEMPLATE_VOID,
    // A VALUE's address (check_lower_address).
    CHECK_LOWER_TEMPLATE_ADDRESS,
    // A VALUE's read: LOCAL or GLOBAL.
    CHECK_LOWER_TEMPLATE_LOCAL,
    CHECK_LOWER_TEMPLATE_GLOBAL,
    // An operand alone: a cast, a conversion, a unary plus, a selection, `&x`, a comma.
    CHECK_LOWER_TEMPLATE_PASS,
    CHECK_LOWER_TEMPLATE_DEREFERENCE,
    CHECK_LOWER_TEMPLATE_MEMBER,
    CHECK_LOWER_TEMPLATE_UNARY,
    CHECK_LOWER_TEMPLATE_TRUTH,
    CHECK_LOWER_TEMPLATE_BINARY,
    CHECK_LOWER_TEMPLATE_POINTER,
    CHECK_LOWER_TEMPLATE_STORE_LOCAL,
    CHECK_LOWER_TEMPLATE_STORE_GLOBAL,
    CHECK_LOWER_TEMPLATE_STORE,
} CheckLowerTemplate;

// A node's classification under its context: its template, and its evaluated operands in their order (at most two),
// each with the context and the conversion target its rule evaluates it with.
typedef struct CheckLowerTemplateUse CheckLowerTemplateUse;
struct CheckLowerTemplateUse
{
    CheckLowerTemplate kind;
    uint32_t children[2];
    uint8_t contexts[2];
    TypeId targets[2];
    uint32_t count;
};

// The parts of a node's value the templates read, from the stream's columns, and from its payload row only where it has
// one (CheckStream.payload_rows: the integer, the floating value, the offset, the overload and the fault), without
// building the whole CheckValue (check_stream_value).
typedef struct CheckLowerTemplateValue CheckLowerTemplateValue;
struct CheckLowerTemplateValue
{
    long double floating;
    uint64_t integer;
    int64_t offset;
    TypeId type;
    uint32_t flags;
    uint32_t object;
    uint32_t overload_head;
    CheckCategory category;
    CheckConstantKind constant_kind;
    CheckFault fault;
};

static CheckLowerTemplateValue check_lower_template_value(const CheckStream* stream, uint32_t node)
{
    const CheckValue* payload = NULL;
    CheckLowerTemplateValue value = { .fault = CHECK_FAULT_NONE };
    if (stream->dense_values != NULL)
    {
        payload = stream->dense_values + node;
    }
    else
    {
        const uint32_t row = stream->payload_rows[node];
        payload = row != 0 && row <= stream->payload_count ? stream->payloads + row - 1 : NULL;
        value.type = stream->value_types[node];
        value.flags = stream->value_flags[node];
        value.object = stream->value_objects[node];
        value.category = (CheckCategory)stream->value_categories[node];
        value.constant_kind = (CheckConstantKind)stream->value_constants[node];
    }
    if (payload != NULL)
    {
        value.floating = payload->floating;
        value.integer = (uint64_t)payload->integer;
        value.offset = payload->offset;
        value.type = payload->type;
        value.flags = payload->flags;
        value.object = payload->object;
        value.overload_head = payload->overload_head;
        value.category = payload->category;
        value.constant_kind = payload->constant_kind;
        value.fault = payload->fault;
    }
    return value;
}

// Whether a class is one a template takes: I8 to F64.
static bool check_lower_template_class(uint8_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
}

// The child's context, as check_lower_expression gives it: `address`, or discarded where the target is void and the child
// is a store of an aggregate, a bit-field's assignment or update, or a comma.
static uint8_t check_lower_template_child_context(const CheckLowerWriter* writer, uint32_t child, bool address, TypeId target)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint8_t context = address ? BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS : 0;
    if (!address && target == BUSTER_TYPE_VOID && child < stream->count)
    {
        const CheckOp op = graph->ops[child];
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const uint32_t destination = graph->operand0[child];
        const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) && destination < stream->count &&
                               (check_stream_flags(stream, destination) & BUSTER_CHECK_BIT_FIELD) != 0;
        context = (store && check_lower_aggregate(writer, check_stream_type(stream, child))) || bit_field || op == CHECK_OP_COMMA ? BUSTER_CHECK_LOWER_TEMPLATE_DISCARD : 0;
    }
    return context;
}

static void check_lower_template_child(const CheckLowerWriter* writer, CheckLowerTemplateUse* use, uint32_t child, bool address, TypeId target)
{
    use->children[use->count] = child;
    use->contexts[use->count] = check_lower_template_child_context(writer, child, address, target);
    use->targets[use->count] = target;
    use->count += 1;
}

// Whether a constant's entries are a template's (check_lower_constant): an address's, an integer's of a class I8 to U64,
// a float's or a double's.
static bool check_lower_template_constant_takes(const CheckLowerWriter* writer, const CheckLowerTemplateValue* value)
{
    const CheckProgram* program = writer->program;
    const uint8_t class = check_lower_class(writer, value->type);
    bool takes = false;
    if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        const uint32_t object = value->object;
        const uint32_t local = object != 0 && object < program->object_count ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
        takes = object == 0 || (object < program->object_count && (local != BUSTER_CHECK_NONE || program->object_symbols[object] != 0));
    }
    else if (!check_lower_complex(writer, value->type) && !check_lower_aggregate(writer, value->type))
    {
        takes = (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64) ||
                (value->constant_kind == CHECK_CONSTANT_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64));
    }
    return takes;
}

// Whether a VALUE's address is a template's (check_lower_address): its object's local's, a global's, or a constant's.
static bool check_lower_template_address_takes(const CheckLowerWriter* writer, uint32_t object)
{
    const CheckProgram* program = writer->program;
    const uint32_t local = object != 0 && object < program->object_count ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
    return object == 0 || (object < program->object_count && (local != BUSTER_CHECK_NONE || program->object_symbols[object] != 0));
}

// The template of `node` in `context`, and its evaluated operands (check_lower_expand's rules for what the table takes),
// or CHECK_LOWER_TEMPLATE_NONE: the tasks' rules lower it.
static CheckLowerTemplateUse check_lower_template_classify(const CheckLowerWriter* writer, uint32_t node, uint8_t context, const CheckLowerTemplateValue* node_value)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const CheckLowerTemplateValue* value = node_value;
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const uint32_t a = graph->operand0[node];
    const uint32_t b = graph->operand1[node];
    const TypeId type_a = stream->conversions[node].operands[0].type;
    const TypeId type_b = stream->conversions[node].operands[1].type;
    const uint8_t facts = check_lower_facts(writer, value->type);
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, value->type);
    const bool constant = value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                          (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    const uint32_t object = value->object;
    const bool known = object != 0 && object < program->object_count;
    // Pointer arithmetic's operands: a pointer's, the other's, and the pointer's stride.
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBSCRIPT;
    const bool left_pointer = arithmetic && (check_lower_facts(writer, type_a) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool right_pointer = arithmetic && (check_lower_facts(writer, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool pointers = left_pointer || right_pointer;
    const uint64_t stride = left_pointer ? check_action_pointer_stride(types, type_a) : 0;
    CheckLowerTemplateUse use = { .kind = CHECK_LOWER_TEMPLATE_NONE };
    if (value->overload_head != 0 || (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) != 0)
    {
        // An overload's placeholder, a _BitInt's and a complex value's are the tasks'.
    }
    else if (op == CHECK_OP_VALUE && known && program->input->compound_objects[object - 1] && !program->input->static_objects[object - 1])
    {
        // A compound literal's first read initializes it.
    }
    else if (op == CHECK_OP_STATEMENT_VALUE || (!address && value->category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0))
    {
    }
    else if (constant && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY &&
             ((value->category == CHECK_RVALUE && !address) || ((address || address_value) && value->constant_kind == CHECK_CONSTANT_ADDRESS)))
    {
        use.kind = discard || check_lower_template_constant_takes(writer, value) ? CHECK_LOWER_TEMPLATE_CONSTANT : CHECK_LOWER_TEMPLATE_NONE;
    }
    else if (op == CHECK_OP_VALUE)
    {
        const uint32_t local = known ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
        if (value->type == BUSTER_TYPE_VOID)
        {
            use.kind = CHECK_LOWER_TEMPLATE_VOID;
        }
        else if (address || address_value)
        {
            use.kind = check_lower_template_address_takes(writer, object) ? CHECK_LOWER_TEMPLATE_ADDRESS : CHECK_LOWER_TEMPLATE_NONE;
        }
        else if (value->category == CHECK_LVALUE && known && check_lower_template_class(class) && !aggregate)
        {
            use.kind = local != BUSTER_CHECK_NONE ? CHECK_LOWER_TEMPLATE_LOCAL :
                       program->object_symbols[object] != 0 ? CHECK_LOWER_TEMPLATE_GLOBAL : CHECK_LOWER_TEMPLATE_NONE;
        }
    }
    else if (op == CHECK_OP_SELECT)
    {
        use.kind = CHECK_LOWER_TEMPLATE_PASS;
        check_lower_template_child(writer, &use, a, address, 0);
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        use.kind = CHECK_LOWER_TEMPLATE_PASS;
        check_lower_template_child(writer, &use, a, true, 0);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        use.kind = aggregate || address || address_value || check_lower_template_class(class) ? CHECK_LOWER_TEMPLATE_DEREFERENCE : CHECK_LOWER_TEMPLATE_NONE;
        check_lower_template_child(writer, &use, a, false, type_a);
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        const TypeId record = op == CHECK_OP_ARROW ? type_element(types, type_a) : check_stream_type(stream, a);
        const uint8_t record_kind = check_action_kind(types, record);
        const bool bit_field = (value->flags & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool loads = !address && !address_value;
        if (stream->diagnostics[node] == CHECK_DIAG_NONE && (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && !bit_field &&
            stream->member_offsets[node] % 8 == 0 && (!loads || check_lower_template_class(class)))
        {
            use.kind = CHECK_LOWER_TEMPLATE_MEMBER;
            check_lower_template_child(writer, &use, a, op == CHECK_OP_MEMBER, type_a);
        }
    }
    else if (arithmetic && (value->flags & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && left_pointer && !right_pointer && stride != 0 &&
             (op != CHECK_OP_SUBSCRIPT || address || address_value || check_lower_template_class(class)))
    {
        // A pointer and an integer, the pointer first (check_lower_scale's PTR_ADD); a subscript's load where it reads.
        use.kind = CHECK_LOWER_TEMPLATE_POINTER;
        check_lower_template_child(writer, &use, a, false, type_a);
        check_lower_template_child(writer, &use, b, false, BUSTER_TYPE_LONG);
    }
    else if (op == CHECK_OP_SUBSCRIPT || aggregate || !check_lower_template_class(class))
    {
        // What the rest of the table takes computes in a scalar class: a cast's and a conversion's results among them.
        if ((op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE) && class != UINT8_MAX && class < BUSTER_TYPED_CLASS_VECTOR)
        {
            use.kind = CHECK_LOWER_TEMPLATE_PASS;
            check_lower_template_child(writer, &use, a, false, value->type);
        }
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        use.kind = CHECK_LOWER_TEMPLATE_PASS;
        check_lower_template_child(writer, &use, a, false, value->type);
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        use.kind = CHECK_LOWER_TEMPLATE_UNARY;
        check_lower_template_child(writer, &use, a, false, type_a);
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        const uint8_t operand_class = check_lower_class(writer, type_a);
        if (!check_lower_aggregate(writer, type_a) && operand_class < BUSTER_TYPED_CLASS_VECTOR && operand_class != BUSTER_TYPED_CLASS_VOID)
        {
            use.kind = CHECK_LOWER_TEMPLATE_TRUTH;
            check_lower_template_child(writer, &use, a, false, type_a);
        }
    }
    else if (op == CHECK_OP_COMMA)
    {
        use.kind = CHECK_LOWER_TEMPLATE_PASS;
        check_lower_template_child(writer, &use, a, false, BUSTER_TYPE_VOID);
        check_lower_template_child(writer, &use, b, address, discard ? BUSTER_TYPE_VOID : value->type);
    }
    else if ((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT) && (value->flags & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && !pointers)
    {
        if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)))
        {
            use.kind = CHECK_LOWER_TEMPLATE_BINARY;
            check_lower_template_child(writer, &use, a, false, type_a);
            check_lower_template_child(writer, &use, b, false, type_b);
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE && op < CHECK_OP_PRE_INCREMENT && !arithmetic)
    {
        // The shifts' and compares' counts are of these classes too, so check_lower_count adds nothing.
        if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)) &&
            !check_lower_aggregate(writer, type_a))
        {
            use.kind = CHECK_LOWER_TEMPLATE_BINARY;
            check_lower_template_child(writer, &use, a, false, type_a);
            check_lower_template_child(writer, &use, b, false, type_b);
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        const uint32_t destination_facts = a < stream->count ? check_lower_facts(writer, check_stream_type(stream, a)) : 0;
        const bool bit_field = op == CHECK_OP_ASSIGN && a < stream->count && (check_stream_flags(stream, a) & BUSTER_CHECK_BIT_FIELD) != 0;
        if ((destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 && !bit_field)
        {
            const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
            const uint32_t target = declaration != BUSTER_CHECK_NONE ? program->input->objects[declaration] :
                                    a < stream->count && graph->ops[a] == CHECK_OP_VALUE ? check_stream_object(stream, a) : 0;
            const uint32_t local = target != 0 && target < program->object_count ? check_lower_local_of(writer, target) : BUSTER_CHECK_NONE;
            const SymbolId symbol = target != 0 && target < program->object_count ? program->object_symbols[target] : 0;
            if (local != BUSTER_CHECK_NONE || symbol != 0)
            {
                use.kind = local != BUSTER_CHECK_NONE ? CHECK_LOWER_TEMPLATE_STORE_LOCAL : CHECK_LOWER_TEMPLATE_STORE_GLOBAL;
                check_lower_template_child(writer, &use, op == CHECK_OP_INITIALIZE ? a : b, false, value->type);
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                use.kind = CHECK_LOWER_TEMPLATE_STORE;
                check_lower_template_child(writer, &use, a, true, 0);
                check_lower_template_child(writer, &use, b, false, value->type);
            }
        }
    }
    return use;
}

// A node's slot (BUSTER_CHECK_LOWER_TEMPLATE_MOST entries), which its template's entries and then its conversion's fill.
typedef struct CheckLowerTemplateEntries CheckLowerTemplateEntries;
struct CheckLowerTemplateEntries
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* values;
    uint32_t count;
    bool taken_address;
};

static inline __attribute__((always_inline)) void check_lower_template_put(CheckLowerTemplateEntries* entries, uint8_t kind, uint8_t class, uint64_t value)
{
    entries->kinds[entries->count] = kind;
    entries->classes[entries->count] = class;
    entries->values[entries->count] = value;
    entries->count += 1;
}

// check_lower_address's entries: the object's local's address (its value where it holds a variable-length array's), a
// global's, or the constant, then its offset.
static void check_lower_template_address(const CheckLowerWriter* writer, CheckLowerTemplateEntries* entries, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        check_lower_template_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else
    {
        const uint32_t local = check_lower_local_of(writer, object);
        if (local != BUSTER_CHECK_NONE && writer->indirect[local] != BUSTER_CHECK_LOWER_VLA_NONE)
        {
            check_lower_template_put(entries, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, local);
        }
        else if (local != BUSTER_CHECK_NONE)
        {
            check_lower_template_put(entries, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, local);
            entries->taken_address = true;
        }
        else
        {
            check_lower_template_put(entries, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, writer->program->object_symbols[object]);
        }
        if (offset != 0)
        {
            check_lower_template_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            check_lower_template_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
}

// The entries of `node`'s template `kind` in `context`, after its operands', as the tasks write them.
static void check_lower_template_own(const CheckLowerWriter* writer, uint32_t node, CheckLowerTemplate kind, uint8_t context, const CheckLowerTemplateValue* node_value,
                                     CheckLowerTemplateEntries* entries)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const TypeTable* types = program->types;
    const CheckOp op = graph->ops[node];
    const CheckLowerTemplateValue* value = node_value;
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const uint8_t facts = check_lower_facts(writer, value->type);
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, value->type);
    if (kind == CHECK_LOWER_TEMPLATE_CONSTANT && !discard)
    {
        if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
        {
            check_lower_template_address(writer, entries, value->object, value->offset);
            if (value->category == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
            {
                check_lower_template_put(entries, BUSTER_TYPED_CONVERT, class, 0);
            }
        }
        else if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
        {
            uint64_t bits = value->integer;
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
            check_lower_template_put(entries, BUSTER_TYPED_CONST, class, bits);
        }
        else
        {
            check_lower_template_put(entries, BUSTER_TYPED_CONST, class, check_lower_float_bits((CheckLowerWriter*)writer, class, value->floating));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_ADDRESS)
    {
        check_lower_template_address(writer, entries, value->object, value->offset);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_LOCAL)
    {
        check_lower_template_put(entries, BUSTER_TYPED_LOCAL, class, check_lower_local_of(writer, value->object));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_GLOBAL)
    {
        check_lower_template_put(entries, BUSTER_TYPED_GLOBAL, class, program->object_symbols[value->object]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_DEREFERENCE && !address && !address_value)
    {
        check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_MEMBER)
    {
        const uint64_t offset = stream->member_offsets[node];
        if (offset / 8 != 0)
        {
            check_lower_template_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            check_lower_template_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
        if (!address && !address_value)
        {
            check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_UNARY)
    {
        check_lower_template_put(entries, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_TRUTH)
    {
        const uint8_t operand_class = check_lower_class(writer, stream->conversions[node].operands[0].type);
        const bool narrow = check_lower_narrow(operand_class);
        if (narrow)
        {
            check_lower_template_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
        }
        check_lower_template_put(entries, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : operand_class, 0);
        check_lower_template_put(entries, op == CHECK_OP_NOT ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_BINARY)
    {
        check_lower_template_put(entries, check_lower_binary(op), class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_POINTER)
    {
        if (op == CHECK_OP_SUBTRACT)
        {
            check_lower_template_put(entries, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        check_lower_template_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, check_action_pointer_stride(types, stream->conversions[node].operands[0].type));
        if (op == CHECK_OP_SUBSCRIPT && !address && !address_value)
        {
            check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, value->type));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL || kind == CHECK_LOWER_TEMPLATE_STORE_GLOBAL)
    {
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
        const uint32_t a = graph->operand0[node];
        const uint32_t target = declaration != BUSTER_CHECK_NONE ? program->input->objects[declaration] : check_stream_object(stream, a);
        check_lower_template_put(entries, kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                 kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? check_lower_local_of(writer, target) : program->object_symbols[target]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE)
    {
        check_lower_template_put(entries, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, value->type));
    }
}

// The entries of the conversion from `from` to `to` that check_lower_expression pushes for an operand evaluated with
// `context` (check_lower_scalar_conversion's for what the table takes): whether the table takes it, with none where the
// expression pushes none.
static bool check_lower_template_conversion(const CheckLowerWriter* writer, TypeId from, TypeId to, uint8_t context, CheckLowerTemplateEntries* entries)
{
    bool takes = true;
    if (context == 0 && to != 0 && from != to && !check_lower_conversion_empty(writer, from, to))
    {
        const uint8_t source = check_lower_value_class(writer, from);
        const uint8_t target = check_lower_value_class(writer, to);
        const bool mixed = (check_lower_aggregate(writer, from) || check_lower_aggregate(writer, to)) && target != BUSTER_TYPED_CLASS_VOID;
        const bool complex = (check_lower_complex(writer, from) || check_lower_complex(writer, to)) && target != BUSTER_TYPED_CLASS_VOID;
        if (complex || mixed || source >= BUSTER_TYPED_CLASS_F80 || target >= BUSTER_TYPED_CLASS_F80)
        {
            takes = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            if (source != BUSTER_TYPED_CLASS_VOID)
            {
                check_lower_template_put(entries, BUSTER_TYPED_DISCARD, source, 0);
            }
        }
        else if ((check_lower_facts(writer, to) & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            if (check_lower_narrow(source))
            {
                check_lower_template_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
            check_lower_template_put(entries, BUSTER_TYPED_CONST, check_lower_narrow(source) ? BUSTER_TYPED_CLASS_I32 : source, 0);
            check_lower_template_put(entries, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            check_lower_template_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else if (source != target)
        {
            check_lower_template_put(entries, BUSTER_TYPED_CONVERT, target, 0);
        }
    }
    return takes;
}

// The body's range of nodes: from the first node of its statements' expressions to the last of them, or none (a span
// of 0). The graph is in postorder, its statements' subtrees one after another (a body with a statement expression, whose
// statements' subtrees are within an expression's, takes no templates: it tracks its stack): the first node is the first
// descendant of the smallest root, its first operand's, and so on down.
static void check_lower_template_range(const CheckLowerWriter* writer, const CheckProgramBody* body, uint32_t* first, uint32_t* span)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = program->stream->count;
    uint32_t smallest_root = UINT32_MAX;
    uint32_t high = 0;
    for (uint32_t i = 0; i < body->count; i += 1)
    {
        const uint32_t node = program->controls[body->first + i].node;
        high = node < count && node > high ? node : high;
        smallest_root = node < count && node < smallest_root ? node : smallest_root;
    }
    uint32_t node = smallest_root;
    // The descent's steps are bounded by the node's index, each to a smaller node.
    bool deeper = node < count;
    while (deeper)
    {
        uint32_t smallest = node;
        const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            smallest = operands[k] < smallest ? operands[k] : smallest;
        }
        if (graph->ops[node] != CHECK_OP_VARARGS && graph->argument_counts[node] != 0)
        {
            const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node]];
            smallest = argument < smallest ? argument : smallest;
        }
        deeper = smallest < node;
        node = smallest;
    }
    *first = node;
    *span = node <= high && node < count ? high - node + 1 : 0;
}

// The operands each op's rule evaluates where its node is a template (check_lower_template_classify's), two per op, each
// a byte: whether it is evaluated, its context (a value, an address, or its parent's: a selection's and a comma's right
// operand), and the target its conversion converts it to. A superset where the rules choose (an assignment's destination,
// which a store to a local or a global doesn't evaluate): the pass scatters each node's to its operands before any node
// is classified, and a template takes an operand only where what it was given is what the template's rule gives it.
#define BUSTER_CHECK_LOWER_TEMPLATE_EVALUATED 0x80u
#define BUSTER_CHECK_LOWER_TEMPLATE_VALUE 0x00u
#define BUSTER_CHECK_LOWER_TEMPLATE_PLACE 0x01u
#define BUSTER_CHECK_LOWER_TEMPLATE_PARENT 0x02u
// The target: none, the node's type, its first or second operand's converted type, void, long where the first operand is
// a pointer (an index) and the second's otherwise, and a comma's right operand's: void where the comma is discarded.
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_NONE (0u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_NODE (1u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_A (2u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_B (3u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_VOID (4u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_INDEX (5u << 2)
#define BUSTER_CHECK_LOWER_TEMPLATE_TO_COMMA (6u << 2)

#define BUSTER_CHECK_LOWER_TEMPLATE_RULE(context, target) (BUSTER_CHECK_LOWER_TEMPLATE_EVALUATED | (context) | (target))
#define BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES                                                                                           \
    { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_A),                              \
      BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_B) }
#define BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_A), 0 }
#define BUSTER_CHECK_LOWER_TEMPLATE_WRAPPER_RULES { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_NODE), 0 }
#define BUSTER_CHECK_LOWER_TEMPLATE_POINTER_RULES                                                                                          \
    { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_A),                              \
      BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_INDEX) }

static const uint8_t check_lower_template_rules[CHECK_OP_COUNT][2] = {
    [CHECK_OP_POSITIVE] = BUSTER_CHECK_LOWER_TEMPLATE_WRAPPER_RULES,
    [CHECK_OP_NEGATIVE] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_NOT] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_BIT_NOT] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_ADD] = BUSTER_CHECK_LOWER_TEMPLATE_POINTER_RULES,
    [CHECK_OP_SUBTRACT] = BUSTER_CHECK_LOWER_TEMPLATE_POINTER_RULES,
    [CHECK_OP_MULTIPLY] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_DIVIDE] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_REMAINDER] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_SHIFT_LEFT] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_SHIFT_RIGHT] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_LESS] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_LESS_EQUAL] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_GREATER] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_EQUAL] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_NOT_EQUAL] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_BIT_AND] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_BIT_OR] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_BIT_XOR] = BUSTER_CHECK_LOWER_TEMPLATE_BINARY_RULES,
    [CHECK_OP_CAST] = BUSTER_CHECK_LOWER_TEMPLATE_WRAPPER_RULES,
    [CHECK_OP_ADDRESS] = { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_PLACE, BUSTER_CHECK_LOWER_TEMPLATE_TO_NONE), 0 },
    [CHECK_OP_DEREFERENCE] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_ASSIGN] = { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_PLACE, BUSTER_CHECK_LOWER_TEMPLATE_TO_NONE),
                          BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_NODE) },
    [CHECK_OP_COMMA] = { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_VALUE, BUSTER_CHECK_LOWER_TEMPLATE_TO_VOID),
                         BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_PARENT, BUSTER_CHECK_LOWER_TEMPLATE_TO_COMMA) },
    [CHECK_OP_SUBSCRIPT] = BUSTER_CHECK_LOWER_TEMPLATE_POINTER_RULES,
    [CHECK_OP_MEMBER] = { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_PLACE, BUSTER_CHECK_LOWER_TEMPLATE_TO_A), 0 },
    [CHECK_OP_ARROW] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_INITIALIZE] = BUSTER_CHECK_LOWER_TEMPLATE_WRAPPER_RULES,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_LOWER_TEMPLATE_WRAPPER_RULES,
    [CHECK_OP_CONDITION] = BUSTER_CHECK_LOWER_TEMPLATE_UNARY_RULES,
    [CHECK_OP_SELECT] = { BUSTER_CHECK_LOWER_TEMPLATE_RULE(BUSTER_CHECK_LOWER_TEMPLATE_PARENT, BUSTER_CHECK_LOWER_TEMPLATE_TO_NONE), 0 },
};

// Whether the tasks lower `node`, evaluated with `address`, as its folded value, without its operands
// (check_lower_expand's constant: its branches before it are the overloads', _BitInts', complex values', compound
// literals', statement expressions' and atomic lvalues', which this takes as no fold, so that their operands stay
// evaluated). From the stream's columns, its payload row read only for a constant.
static bool check_lower_template_folds(const CheckLowerWriter* writer, uint32_t node, bool address)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const CheckOp op = program->input->graph.ops[node];
    bool folds = false;
    if (op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY && op != CHECK_OP_STATEMENT_VALUE && op != CHECK_OP_VALUE &&
        (stream->dense_values != NULL || stream->value_constants[node] != CHECK_CONSTANT_NONE))
    {
        const CheckLowerTemplateValue value = check_lower_template_value(stream, node);
        const uint8_t facts = check_lower_facts(writer, value.type);
        const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
        const bool atomic = !address && value.category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0;
        folds = value.constant_kind != CHECK_CONSTANT_NONE && value.fault == CHECK_FAULT_NONE &&
                (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && value.overload_head == 0 &&
                (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) == 0 && !atomic &&
                ((value.category == CHECK_RVALUE && !address) || ((address || address_value) && value.constant_kind == CHECK_CONSTANT_ADDRESS));
    }
    return folds;
}

// Marks `node`'s operands (in the range) unevaluated: a node the tasks fold, or one under it.
static void check_lower_template_unevaluated(CheckLowerWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const uint32_t first = writer->template_first_node;
    const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    for (uint32_t k = 0; k < 3; k += 1)
    {
        if (operands[k] < node && operands[k] - first < writer->template_span)
        {
            writer->template_states[operands[k] - first] |= BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED;
        }
    }
    const uint32_t arguments = graph->ops[node] != CHECK_OP_VARARGS ? graph->argument_counts[node] : 0;
    for (uint32_t k = 0; k < arguments; k += 1)
    {
        const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + k];
        if (argument < node && argument - first < writer->template_span)
        {
            writer->template_states[argument - first] |= BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED;
        }
    }
}

// Marks every node under `node` (in the range) unevaluated, through their operands (check_lower_template_unevaluated's), an
// operand that is already marked having its own marked: a stack of the marked nodes in `template_children`, whose room
// (two a node of the range) the classification fills only after the scatter.
static void check_lower_template_unevaluated_walk(CheckLowerWriter* writer, uint32_t node)
{
    const CheckGraph* graph = &writer->program->input->graph;
    const uint32_t first = writer->template_first_node;
    uint32_t* stack = writer->template_children;
    uint32_t top = 0;
    stack[top++] = node;
    while (top != 0)
    {
        const uint32_t at = stack[--top];
        const uint32_t operands[3] = { graph->operand0[at], graph->operand1[at], graph->operand2[at] };
        const uint32_t arguments = graph->ops[at] != CHECK_OP_VARARGS ? graph->argument_counts[at] : 0;
        for (uint32_t k = 0; k < 3 + arguments; k += 1)
        {
            const uint32_t operand = k < 3 ? operands[k] : graph->argument_nodes[graph->argument_offsets[at] + k - 3];
            const uint32_t place = operand - first;
            if (operand < at && place < writer->template_span && (writer->template_states[place] & BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED) == 0)
            {
                writer->template_states[place] |= BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED;
                stack[top++] = operand;
            }
        }
    }
}

// From the last node down, each node's rule gives its operands their contexts and targets (`template_contexts`'
// BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS and `template_targets`), before an operand's own turn: a selection's and a comma's
// right operand take their parent's, which its own parent gave it already. An operand two nodes give theirs is shared, the
// tasks'. A node whose op isn't the table's gives none: its operands keep a value's context and no target, which the
// splice checks against the tasks' flags (a template takes an operand only where its rule gives the same, and a closed root
// copies none of its own conversion).
static void check_lower_template_scatter(CheckLowerWriter* writer, uint32_t first, uint32_t span)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    for (uint32_t r = span; r-- > 0;)
    {
        const uint32_t node = first + r;
        const CheckOp op = graph->ops[node];
        const bool silent = (writer->template_states[r] & BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED) != 0 ||
                            check_lower_template_folds(writer, node, (writer->template_contexts[r] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0);
        if (silent)
        {
            check_lower_template_unevaluated(writer, node);
        }
        const uint8_t* rules = op < CHECK_OP_COUNT && !silent ? check_lower_template_rules[op] : check_lower_template_rules[CHECK_OP_VALUE];
        const uint32_t operands[2] = { graph->operand0[node], graph->operand1[node] };
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const uint8_t rule = rules[k];
            const uint32_t c = operands[k] - first;
            if ((rule & BUSTER_CHECK_LOWER_TEMPLATE_EVALUATED) != 0 && operands[k] < node && c < span)
            {
                const uint8_t mode = rule & 3u;
                const uint8_t parent = writer->template_contexts[r];
                const uint8_t to = rule & (7u << 2);
                TypeId target = 0;
                if (to == BUSTER_CHECK_LOWER_TEMPLATE_TO_NODE)
                {
                    target = check_stream_type(stream, node);
                }
                else if (to == BUSTER_CHECK_LOWER_TEMPLATE_TO_A || to == BUSTER_CHECK_LOWER_TEMPLATE_TO_B)
                {
                    target = stream->conversions[node].operands[to == BUSTER_CHECK_LOWER_TEMPLATE_TO_B].type;
                }
                else if (to == BUSTER_CHECK_LOWER_TEMPLATE_TO_VOID)
                {
                    target = BUSTER_TYPE_VOID;
                }
                else if (to == BUSTER_CHECK_LOWER_TEMPLATE_TO_INDEX)
                {
                    const bool pointer = (check_lower_facts(writer, stream->conversions[node].operands[0].type) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                    target = pointer ? BUSTER_TYPE_LONG : stream->conversions[node].operands[1].type;
                }
                else if (to == BUSTER_CHECK_LOWER_TEMPLATE_TO_COMMA)
                {
                    // The comma's own context: discarded where its parent gave it void and no address.
                    const uint8_t own = check_lower_template_child_context(writer, node, (parent & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, writer->template_targets[r]);
                    target = (own & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0 ? BUSTER_TYPE_VOID : check_stream_type(stream, node);
                }
                const uint8_t context = mode == BUSTER_CHECK_LOWER_TEMPLATE_PLACE ? BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS :
                                        mode == BUSTER_CHECK_LOWER_TEMPLATE_PARENT ? (uint8_t)(parent & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) : 0;
                // A shared operand keeps what its highest operator gave it, as the vector scatter's links do.
                uint8_t* state = writer->template_states + c;
                const bool assigned = (*state & BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED) != 0;
                *state |= assigned ? BUSTER_CHECK_LOWER_TEMPLATE_SHARED : BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED;
                writer->template_contexts[c] = assigned ? writer->template_contexts[c] : context;
                writer->template_targets[c] = assigned ? writer->template_targets[c] : target;
            }
        }
    }
}

_Static_assert(CHECK_OP_SELECT < 64 && CHECK_OP_COMMA < 64 && CHECK_OP_ASSIGN < 64 && CHECK_OP_MEMBER < 64,
               "the rules' table fits the vector scatter's 64 bytes");

// A link's parts (CheckLowerWriter.template_links): the operator's place in the range, the operand's rule (its first or
// second), whether its target is a comma's, and its context's mode (BUSTER_CHECK_LOWER_TEMPLATE_VALUE, _PLACE or _PARENT).
#define BUSTER_CHECK_LOWER_TEMPLATE_LINK_SHIFT 4u
#define BUSTER_CHECK_LOWER_TEMPLATE_LINK_SECOND 0x8u
#define BUSTER_CHECK_LOWER_TEMPLATE_LINK_COMMA 0x4u

// check_lower_template_scatter's results, data-parallel. Each operator links each operand its rule evaluates (in the range
// and before it) and gives it its target, 16 operators at a time, by scatters in node order, so that an operand two
// operators link keeps the highest's link and target. The links read back mark the operands another operator linked
// shared. Each operand's context is its link's mode: an address's for a place, a value's otherwise, and its operator's
// own for a parent's (a selection's and a comma's right operand), which, with a comma's right operand's target, its scalar
// step takes from the last operand down. The operands of an operator the tasks fold are not evaluated, nor theirs: a
// walk from each such operator, in the range's order from the last, which the folds' own operands are rare enough for.
// It gives the evaluated, unshared nodes the scalar scatter's contexts and targets, which test-lowering-templates compares.
static __attribute__((noinline)) void check_lower_template_scatter_vector(CheckLowerWriter* writer, uint32_t first, uint32_t span)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    uint8_t rule_bytes[2][64] __attribute__((aligned(64)));
    for (uint32_t op = 0; op < 64; op += 1)
    {
        rule_bytes[0][op] = op < CHECK_OP_COUNT ? check_lower_template_rules[op][0] : 0;
        rule_bytes[1][op] = op < CHECK_OP_COUNT ? check_lower_template_rules[op][1] : 0;
    }
    const __m512i rules0 = _mm512_load_si512(rule_bytes[0]);
    const __m512i rules1 = _mm512_load_si512(rule_bytes[1]);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i conversion_offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32((int)sizeof(CheckNodeConversions)));
    const __m512i bound = _mm512_set1_epi32((int)span);
    const __m512i first_node = _mm512_set1_epi32((int)first);
    memset(writer->template_links, 0xff, (uint64_t)span * sizeof(uint32_t));
    // The links and the targets, in node order.
    for (uint32_t base = 0; base < span; base += 16)
    {
        const uint32_t count = span - base < 16 ? span - base : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const uint32_t node = first + base;
        const __m512i nodes = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)node));
        const __m512i places = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)base));
        const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + node);
        const __mmask16 small = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
        const __m512i keys = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
        const __m512i types = _mm512_maskz_loadu_epi32(active, stream->value_types + node);
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const __m512i rule = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, k == 0 ? rules0 : rules1))));
            const __m512i operands = _mm512_maskz_loadu_epi32(active, (k == 0 ? graph->operand0 : graph->operand1) + node);
            const __m512i places_c = _mm512_sub_epi32(operands, first_node);
            const __mmask16 linked = small & _mm512_test_epi32_mask(rule, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_EVALUATED)) &
                                     _mm512_cmplt_epu32_mask(operands, nodes) & _mm512_cmplt_epu32_mask(places_c, bound);
            if (linked != 0)
            {
                const __m512i mode = _mm512_and_si512(rule, _mm512_set1_epi32(3));
                const __m512i to = _mm512_and_si512(rule, _mm512_set1_epi32(7u << 2));
                const __mmask16 to_node = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_NODE));
                const __mmask16 to_a = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_A));
                const __mmask16 to_b = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_B));
                const __mmask16 to_void = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_VOID));
                const __mmask16 to_index = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_INDEX));
                const __mmask16 to_comma = _mm512_cmpeq_epi32_mask(to, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_TO_COMMA));
                const __m512i types_a = _mm512_mask_i32gather_epi32(zero, linked & (to_a | to_index), conversion_offsets, &stream->conversions[node].operands[0].type, 1);
                const __m512i types_b = _mm512_mask_i32gather_epi32(zero, linked & (to_b | to_index), conversion_offsets, &stream->conversions[node].operands[1].type, 1);
                const __m512i indices_a = _mm512_srli_epi32(types_a, BUSTER_TYPE_INDEX_SHIFT);
                const __m512i info_a = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), linked & to_index & _mm512_cmplt_epu32_mask(indices_a, _mm512_set1_epi32((int)writer->type_count)),
                                                                   indices_a, writer->type_templates, 4);
                const __mmask16 pointer = to_index & _mm512_test_epi32_mask(_mm512_srli_epi32(info_a, 8), _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_POINTER));
                __m512i targets = _mm512_maskz_mov_epi32(to_node, types);
                targets = _mm512_mask_mov_epi32(targets, to_a, types_a);
                targets = _mm512_mask_mov_epi32(targets, to_b | (to_index & (__mmask16)~pointer), types_b);
                targets = _mm512_mask_mov_epi32(targets, to_void, _mm512_set1_epi32(BUSTER_TYPE_VOID));
                targets = _mm512_mask_mov_epi32(targets, pointer, _mm512_set1_epi32(BUSTER_TYPE_LONG));
                __m512i links = _mm512_or_si512(_mm512_slli_epi32(places, BUSTER_CHECK_LOWER_TEMPLATE_LINK_SHIFT), mode);
                links = _mm512_or_si512(links, _mm512_set1_epi32(k == 0 ? 0 : BUSTER_CHECK_LOWER_TEMPLATE_LINK_SECOND));
                links = _mm512_mask_or_epi32(links, to_comma, links, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_LINK_COMMA));
                _mm512_mask_i32scatter_epi32(writer->template_links, linked, places_c, links, 4);
                _mm512_mask_i32scatter_epi32(writer->template_targets, linked, places_c, targets, 4);
            }
        }
    }
    // The links read back: an operand whose link is another operator's is shared.
    for (uint32_t base = 0; base < span; base += 16)
    {
        const uint32_t count = span - base < 16 ? span - base : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const uint32_t node = first + base;
        const __m512i nodes = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)node));
        const __m512i places = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)base));
        const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + node);
        const __mmask16 small = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
        const __m512i keys = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const __m512i rule = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, k == 0 ? rules0 : rules1))));
            const __m512i operands = _mm512_maskz_loadu_epi32(active, (k == 0 ? graph->operand0 : graph->operand1) + node);
            const __m512i places_c = _mm512_sub_epi32(operands, first_node);
            const __mmask16 linked = small & _mm512_test_epi32_mask(rule, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_EVALUATED)) &
                                     _mm512_cmplt_epu32_mask(operands, nodes) & _mm512_cmplt_epu32_mask(places_c, bound);
            const __m512i links = _mm512_mask_i32gather_epi32(none, linked, places_c, writer->template_links, 4);
            const __m512i owner = _mm512_srli_epi32(links, BUSTER_CHECK_LOWER_TEMPLATE_LINK_SHIFT);
            const __mmask16 second = _mm512_test_epi32_mask(links, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_LINK_SECOND));
            const __mmask16 own = _mm512_cmpeq_epi32_mask(owner, places) & (k == 0 ? (__mmask16)~second : second);
            uint32_t lost = (uint32_t)(linked & (__mmask16)~own);
            uint32_t lost_places[16];
            _mm512_storeu_si512(lost_places, places_c);
            for (; lost != 0; lost &= lost - 1)
            {
                writer->template_states[lost_places[__builtin_ctz(lost)]] |= BUSTER_CHECK_LOWER_TEMPLATE_SHARED;
            }
        }
    }
    // Each operand's state and context from its link; the parents' modes and the commas' targets left for the steps after.
    uint32_t work = 0;
    for (uint32_t base = 0; base < span; base += 16)
    {
        const uint32_t count = span - base < 16 ? span - base : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const __m512i links = _mm512_mask_loadu_epi32(none, active, writer->template_links + base);
        const __mmask16 linked = active & _mm512_cmpneq_epi32_mask(links, none);
        const __m512i mode = _mm512_and_si512(links, _mm512_set1_epi32(3));
        const __mmask16 place = linked & _mm512_cmpeq_epi32_mask(mode, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_PLACE));
        const __mmask16 later = linked & (_mm512_cmpeq_epi32_mask(mode, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_PARENT)) |
                                          _mm512_test_epi32_mask(links, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_LINK_COMMA)));
        const __m512i states = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, writer->template_states + base));
        _mm_mask_storeu_epi8(writer->template_states + base, active,
                             _mm512_cvtepi32_epi8(_mm512_mask_or_epi32(states, linked, states, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED))));
        _mm_mask_storeu_epi8(writer->template_contexts + base, active, _mm512_cvtepi32_epi8(_mm512_maskz_mov_epi32(place, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS))));
        _mm512_storeu_si512(writer->template_work + work, _mm512_maskz_compress_epi32(later, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)base))));
        work += (uint32_t)__builtin_popcount(later);
    }
    for (uint32_t w = work; w-- > 0;)
    {
        const uint32_t c = writer->template_work[w];
        const uint32_t link = writer->template_links[c];
        const uint32_t p = link >> BUSTER_CHECK_LOWER_TEMPLATE_LINK_SHIFT;
        const uint8_t parent = (uint8_t)(writer->template_contexts[p] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS);
        if ((link & 3u) == BUSTER_CHECK_LOWER_TEMPLATE_PARENT)
        {
            writer->template_contexts[c] = parent;
        }
        if ((link & BUSTER_CHECK_LOWER_TEMPLATE_LINK_COMMA) != 0)
        {
            const uint8_t own = check_lower_template_child_context(writer, first + p, parent != 0, writer->template_targets[p]);
            writer->template_targets[c] = (own & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0 ? BUSTER_TYPE_VOID : check_stream_type(stream, first + p);
        }
    }
    // The folds' operands: each fold with an operand in the range, from the last, unless an earlier one's walk marked it.
    work = 0;
    for (uint32_t base = 0; base < span; base += 16)
    {
        const uint32_t count = span - base < 16 ? span - base : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const uint32_t node = first + base;
        const __m512i nodes = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)node));
        const __m512i constants = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, stream->value_constants + node));
        __mmask16 operand = 0;
        const uint32_t* const columns[3] = { graph->operand0, graph->operand1, graph->operand2 };
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const __m512i operands = _mm512_maskz_loadu_epi32(active, columns[k] + node);
            operand |= _mm512_cmplt_epu32_mask(operands, nodes) & _mm512_cmplt_epu32_mask(_mm512_sub_epi32(operands, first_node), bound);
        }
        const __m512i arguments = _mm512_maskz_loadu_epi32(active, graph->argument_counts + node);
        operand |= _mm512_cmpneq_epi32_mask(arguments, zero);
        const __mmask16 candidates = active & operand & _mm512_cmpneq_epi32_mask(constants, _mm512_set1_epi32(CHECK_CONSTANT_NONE));
        _mm512_storeu_si512(writer->template_work + work, _mm512_maskz_compress_epi32(candidates, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)base))));
        work += (uint32_t)__builtin_popcount(candidates);
    }
    for (uint32_t w = work; w-- > 0;)
    {
        const uint32_t r = writer->template_work[w];
        if ((writer->template_states[r] & BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED) == 0 &&
            check_lower_template_folds(writer, first + r, (writer->template_contexts[r] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0))
        {
            check_lower_template_unevaluated_walk(writer, first + r);
        }
    }
}

// The common nodes' templates without check_lower_template_classify's walk through every rule: a VALUE's read and address,
// an integer constant, `&x`, a cast, a conversion and a unary plus, `*p`, pointer plus or minus an integer and a
// subscript, the negation and the complement, the truth tests and the scalar binary operators, each where the rules
// before its own in check_lower_template_classify can't apply (no payload row but an integer constant's, so no overload,
// fault or offset; no _BitInt, complex or atomic lvalue; no compound literal; not discarded). It writes the node's own
// entries and gives its evaluated operands, which take the contexts and targets the scatter's table gives them, or
// returns unhandled for the general rules. Its results are the general rules', which check_program_lower_templates'
// check (CheckLowerWriter.template_check) compares node by node; it is the twin of a vector pass to come.
typedef struct CheckLowerTemplateFast CheckLowerTemplateFast;
struct CheckLowerTemplateFast
{
    uint32_t children[2];
    uint32_t count;
    bool handled;
};

static CheckLowerTemplateFast check_lower_template_fast(const CheckLowerWriter* writer, uint32_t node, bool address, CheckLowerTemplateEntries* entries)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    const TypeId type = stream->value_types[node];
    const uint32_t flags = stream->value_flags[node];
    const uint32_t object = stream->value_objects[node];
    const uint8_t category = stream->value_categories[node];
    const uint8_t constant = stream->value_constants[node];
    const uint32_t row = stream->payload_rows[node];
    const uint8_t facts = check_lower_facts(writer, type);
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = aggregate ? BUSTER_TYPED_CLASS_U64 : check_lower_class(writer, type);
    const bool known = object != 0 && object < program->object_count;
    const bool ordinary = (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) == 0 &&
                          (address || category != CHECK_LVALUE || (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0);
    const bool reads = !address && !address_value;
    const bool compound = op == CHECK_OP_VALUE && known && program->input->compound_objects[object - 1] && !program->input->static_objects[object - 1];
    // The payload row's parts the rules read: an overload (which no template takes), a fault, an integer and an offset.
    const CheckValue* payload = row != 0 && row <= stream->payload_count ? stream->payloads + row - 1 : NULL;
    const bool payloaded = row == 0 || payload != NULL;
    const bool overloaded = payload != NULL && payload->overload_head != 0;
    const CheckFault fault = payload != NULL ? payload->fault : CHECK_FAULT_NONE;
    const int64_t offset = payload != NULL ? payload->offset : 0;
    // check_lower_expand's constant: its value folded, its operands not evaluated.
    const bool folds = constant != CHECK_CONSTANT_NONE && fault == CHECK_FAULT_NONE && (flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                       op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY && op != CHECK_OP_STATEMENT_VALUE &&
                       ((category == CHECK_RVALUE && !address) || (!reads && constant == CHECK_CONSTANT_ADDRESS));
    const bool usable = ordinary && payloaded && !overloaded && !compound;
    CheckLowerTemplateFast fast = { .handled = false };
    if (!usable)
    {
    }
    else if (op == CHECK_OP_VALUE && category == CHECK_LVALUE && type != BUSTER_TYPE_VOID)
    {
        // A read, whatever its constant (the rules fold only an rvalue's or an address's), or an address: the constant's
        // and the VALUE's rules give an lvalue's address the same entries (check_lower_address, no conversion).
        const uint32_t local = known ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
        const SymbolId symbol = known ? program->object_symbols[object] : 0;
        if (!reads && (object == 0 || (known && (local != BUSTER_CHECK_NONE || symbol != 0))))
        {
            check_lower_template_address(writer, entries, object, offset);
            fast.handled = true;
        }
        else if (reads && known && !aggregate && check_lower_template_class(class) && (local != BUSTER_CHECK_NONE || symbol != 0))
        {
            check_lower_template_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL, class, local != BUSTER_CHECK_NONE ? local : symbol);
            fast.handled = true;
        }
    }
    else if (folds)
    {
        // An integer constant of a class I8 to U64, of an op that is never discarded (its context has no discard).
        const bool discardable = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE || op == CHECK_OP_COMMA || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
        if (constant == CHECK_CONSTANT_INTEGER && category == CHECK_RVALUE && !address && !discardable && !aggregate && class >= BUSTER_TYPED_CLASS_I8 &&
            class <= BUSTER_TYPED_CLASS_U64)
        {
            uint64_t bits = payload != NULL ? (uint64_t)payload->integer : 0;
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
            check_lower_template_put(entries, BUSTER_TYPED_CONST, class, bits);
            fast.handled = true;
        }
        else if (constant == CHECK_CONSTANT_ADDRESS)
        {
            // An address constant (check_lower_constant's): its object's address and offset, converted where it is a signed
            // integer's rvalue.
            const uint32_t local = known ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
            if (object == 0 || (known && (local != BUSTER_CHECK_NONE || program->object_symbols[object] != 0)))
            {
                check_lower_template_address(writer, entries, object, offset);
                if (category == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
                {
                    check_lower_template_put(entries, BUSTER_TYPED_CONVERT, class, 0);
                }
                fast.handled = true;
            }
        }
    }
    else if (op == CHECK_OP_VALUE)
    {
        // Not an lvalue's, not folded: a void value's (none), or an address at offset 0 (an offset has a payload row).
        const uint32_t local = known ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
        const SymbolId symbol = known ? program->object_symbols[object] : 0;
        if (type == BUSTER_TYPE_VOID)
        {
            fast.handled = true;
        }
        else if (!reads && (object == 0 || (known && (local != BUSTER_CHECK_NONE || symbol != 0))))
        {
            check_lower_template_address(writer, entries, object, offset);
            fast.handled = true;
        }
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        // A member's read, or its address: past the base's, its offset's addition where it has one.
        const TypeId type_a = stream->conversions[node].operands[0].type;
        const TypeId record = op == CHECK_OP_ARROW ? type_element(program->types, type_a) : check_stream_type(stream, graph->operand0[node]);
        const uint8_t record_kind = check_action_kind(program->types, record);
        const uint64_t bits = stream->member_offsets[node];
        if (stream->diagnostics[node] == CHECK_DIAG_NONE && (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) &&
            (flags & BUSTER_CHECK_BIT_FIELD) == 0 && bits % 8 == 0 && (!reads || check_lower_template_class(class)))
        {
            if (bits / 8 != 0)
            {
                check_lower_template_put(entries, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, bits / 8);
                check_lower_template_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            }
            if (reads)
            {
                check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, 0);
            }
            fast.children[0] = graph->operand0[node];
            fast.count = 1;
            fast.handled = true;
        }
    }
    else if ((op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE) && !aggregate && check_lower_template_class(class))
    {
        // A scalar store: to a local or a global it names, or through its destination's address.
        const uint32_t a = graph->operand0[node];
        const uint32_t destination_facts = a < stream->count ? check_lower_facts(writer, check_stream_type(stream, a)) : 0;
        const bool bit_field = op == CHECK_OP_ASSIGN && a < stream->count && (check_stream_flags(stream, a) & BUSTER_CHECK_BIT_FIELD) != 0;
        if ((destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 && !bit_field)
        {
            const uint32_t declaration = op == CHECK_OP_INITIALIZE ? writer->initializer_declarations[node] : BUSTER_CHECK_NONE;
            const uint32_t target = declaration != BUSTER_CHECK_NONE ? program->input->objects[declaration] :
                                    a < stream->count && graph->ops[a] == CHECK_OP_VALUE ? check_stream_object(stream, a) : 0;
            const bool named = target != 0 && target < program->object_count;
            const uint32_t local = named ? check_lower_local_of(writer, target) : BUSTER_CHECK_NONE;
            const SymbolId symbol = named ? program->object_symbols[target] : 0;
            if (local != BUSTER_CHECK_NONE || symbol != 0)
            {
                check_lower_template_put(entries, local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                         local != BUSTER_CHECK_NONE ? local : symbol);
                fast.children[0] = op == CHECK_OP_INITIALIZE ? a : graph->operand1[node];
                fast.count = 1;
                fast.handled = true;
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                check_lower_template_put(entries, BUSTER_TYPED_STORE, class, 0);
                fast.children[0] = a;
                fast.children[1] = graph->operand1[node];
                fast.count = 2;
                fast.handled = true;
            }
        }
    }
    else if (constant == CHECK_CONSTANT_NONE)
    {
        const uint32_t a = graph->operand0[node];
        const uint32_t b = graph->operand1[node];
        const TypeId type_a = stream->conversions[node].operands[0].type;
        const TypeId type_b = stream->conversions[node].operands[1].type;
        const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBSCRIPT;
        const bool left_pointer = arithmetic && (check_lower_facts(writer, type_a) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        const bool right_pointer = arithmetic && (check_lower_facts(writer, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
        const bool element = (flags & BUSTER_CHECK_VECTOR_ELEMENT) != 0;
        const bool computes = !aggregate && check_lower_template_class(class);
        const uint8_t binary = op < CHECK_OP_PRE_INCREMENT && !arithmetic ? check_lower_binary(op) : BUSTER_TYPED_NONE;
        fast.children[0] = a;
        fast.children[1] = b;
        if (op == CHECK_OP_ADDRESS)
        {
            fast.count = 1;
        }
        else if (op == CHECK_OP_DEREFERENCE && (aggregate || !reads || check_lower_template_class(class)))
        {
            if (reads)
            {
                check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, 0);
            }
            fast.count = 1;
        }
        else if (arithmetic && !element && left_pointer && !right_pointer && (op != CHECK_OP_SUBSCRIPT || !reads || check_lower_template_class(class)))
        {
            const uint64_t stride = check_action_pointer_stride(program->types, type_a);
            if (stride != 0)
            {
                if (op == CHECK_OP_SUBTRACT)
                {
                    check_lower_template_put(entries, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
                }
                check_lower_template_put(entries, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, stride);
                if (op == CHECK_OP_SUBSCRIPT && reads)
                {
                    check_lower_template_put(entries, BUSTER_TYPED_LOAD, class, 0);
                }
                fast.count = 2;
            }
        }
        else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
        {
            fast.count = class < BUSTER_TYPED_CLASS_VECTOR ? 1 : 0;
        }
        else if (!computes)
        {
        }
        else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
        {
            check_lower_template_put(entries, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
            fast.count = 1;
        }
        else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
        {
            const uint8_t operand_class = check_lower_class(writer, type_a);
            if (!check_lower_aggregate(writer, type_a) && operand_class < BUSTER_TYPED_CLASS_VECTOR && operand_class != BUSTER_TYPED_CLASS_VOID)
            {
                const bool narrow = check_lower_narrow(operand_class);
                if (narrow)
                {
                    check_lower_template_put(entries, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
                }
                check_lower_template_put(entries, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : operand_class, 0);
                check_lower_template_put(entries, op == CHECK_OP_NOT ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
                fast.count = 1;
            }
        }
        else if ((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT) && !element && !left_pointer && !right_pointer)
        {
            if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)))
            {
                check_lower_template_put(entries, check_lower_binary(op), class, 0);
                fast.count = 2;
            }
        }
        else if (binary != BUSTER_TYPED_NONE)
        {
            if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)) &&
                !check_lower_aggregate(writer, type_a))
            {
                check_lower_template_put(entries, binary, class, 0);
                fast.count = 2;
            }
        }
        fast.handled = fast.count != 0;
    }
    return fast;
}

// A node's state where the vector classification took it (check_lower_template_vector): its template is the fast twin's,
// its own entries in its slot, its evaluated operands in `template_children` and their count in `template_counts`.
#define BUSTER_CHECK_LOWER_TEMPLATE_FAST 0x40u

_Static_assert(CHECK_OP_PRE_INCREMENT < 64 && sizeof(SymbolId) == sizeof(uint32_t) && sizeof(CheckNodeConversions) == 32,
               "the vector classification's tables and gathers");

// The vector classification's tables for a writer: each op's binary operator below CHECK_OP_PRE_INCREMENT
// (check_lower_binary's), and a bit for each object a compound literal of automatic storage takes
// (check_lower_template_fast's `compound`), bit `o - 1` for object `o`, from the input's columns 64 objects at a time.
static void check_lower_template_vector_begin(CheckLowerWriter* writer)
{
    const CheckProgram* program = writer->program;
    const uint64_t object_words = ((uint64_t)program->object_count + 32) / 32;
    for (uint32_t op = 0; op < 64; op += 1)
    {
        writer->template_binary[op] = op < CHECK_OP_PRE_INCREMENT ? check_lower_binary((CheckOp)op) : BUSTER_TYPED_NONE;
    }
    memset(writer->template_compound_bits, 0, object_words * sizeof(uint32_t));
    const uint32_t bytes = program->object_count != 0 ? program->object_count - 1 : 0;
    const uint8_t* compound = program->input->compound_objects;
    const uint8_t* statics = program->input->static_objects;
    for (uint32_t i = 0; compound != NULL && statics != NULL && i < bytes; i += 64)
    {
        const uint32_t count = bytes - i < 64 ? bytes - i : 64;
        const __mmask64 active = count == 64 ? ~(__mmask64)0 : (((__mmask64)1 << count) - 1);
        const __m512i literal = _mm512_maskz_loadu_epi8(active, compound + i);
        const __m512i fixed = _mm512_maskz_loadu_epi8(active, statics + i);
        const uint64_t bits = (uint64_t)(_mm512_test_epi8_mask(literal, literal) & _mm512_testn_epi8_mask(fixed, fixed));
        memcpy(writer->template_compound_bits + i / 32, &bits, count > 32 ? 8 : 4);
    }
}

// The fast twin's common shapes 16 nodes at a time (check_lower_template_fast's), for the nodes of the body's range the
// scatter left evaluated and unshared and whose payload rows are none (so no overload, fault or offset), in a value's or
// an address's context (none of these shapes is discarded): a VALUE's read of a local or a global; `&x`; `*p`; a cast,
// a conversion or a unary plus; the negation and the complement; the truth tests; and the scalar binary operators but
// pointer arithmetic. Each lane it takes gets the fast twin's result, BUSTER_CHECK_LOWER_TEMPLATE_FAST in its state, its
// own entries in its slot and their count in `template_own`, and its evaluated operands. The body's loop takes the fast
// twin and the general rules for the others, and with `template_check` compares the twin with each lane taken here.
static __attribute__((noinline)) void check_lower_template_vector(CheckLowerWriter* writer, uint32_t first, uint32_t span)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i zero = _mm512_setzero_si512();
    const __m512i none = _mm512_set1_epi32((int)BUSTER_CHECK_NONE);
    const __m512i conversion_offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32((int)sizeof(CheckNodeConversions)));
    const __m512i interleave_low = _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    const __m512i interleave_high = _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    const __m512i binary_table = _mm512_loadu_si512(writer->template_binary);
    const __m512i type_count = _mm512_set1_epi32((int)writer->type_count);
    for (uint32_t base = 0; base < span; base += 16)
    {
        const uint32_t count = span - base < 16 ? span - base : 16;
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const uint32_t node = first + base;
        const __m512i states = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, writer->template_states + base));
        const __m512i contexts = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, writer->template_contexts + base));
        const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + node);
        const __m512i types = _mm512_maskz_loadu_epi32(active, stream->value_types + node);
        const __m512i flags = _mm512_maskz_loadu_epi32(active, stream->value_flags + node);
        const __m512i objects = _mm512_maskz_loadu_epi32(active, stream->value_objects + node);
        const __m512i categories = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, stream->value_categories + node));
        const __m512i constants = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, stream->value_constants + node));
        const __m512i rows = _mm512_maskz_loadu_epi32(active, stream->payload_rows + node);
        const __mmask16 eligible = active & _mm512_testn_epi32_mask(states, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_SHARED | BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED)) &
                                   _mm512_cmpeq_epi32_mask(rows, zero);
        // The node's type's class and facts (check_lower_class's and check_lower_facts'), its class as a value's.
        const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
        const __m512i info = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), eligible & _mm512_cmplt_epu32_mask(indices, type_count), indices,
                                                         writer->type_templates, 4);
        const __m512i raw_classes = _mm512_and_si512(info, _mm512_set1_epi32(0xff));
        const __m512i facts = _mm512_and_si512(_mm512_srli_epi32(info, 8), _mm512_set1_epi32(0xff));
        const __mmask16 aggregate = _mm512_test_epi32_mask(facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __m512i classes = _mm512_mask_mov_epi32(raw_classes, aggregate, _mm512_set1_epi32(BUSTER_TYPED_CLASS_U64));
        const __mmask16 address = _mm512_test_epi32_mask(contexts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS));
        const __mmask16 reads = (__mmask16)(~address & ~_mm512_test_epi32_mask(facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_ADDRESS)));
        const __mmask16 lvalue = _mm512_cmpeq_epi32_mask(categories, _mm512_set1_epi32(CHECK_LVALUE));
        const __mmask16 ordinary = _mm512_testn_epi32_mask(facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) &
                                   (address | (__mmask16)~lvalue | _mm512_testn_epi32_mask(facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_ATOMIC)));
        const __mmask16 known = _mm512_cmpneq_epi32_mask(objects, zero) & _mm512_cmplt_epu32_mask(objects, _mm512_set1_epi32((int)program->object_count));
        const __mmask16 value_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_VALUE));
        const __m512i object_bits = _mm512_sub_epi32(objects, one);
        const __mmask16 lookup = eligible & value_op & known;
        const __m512i compound_words = _mm512_mask_i32gather_epi32(zero, lookup, _mm512_srli_epi32(object_bits, 5), writer->template_compound_bits, 4);
        const __mmask16 compound = lookup & _mm512_test_epi32_mask(compound_words, _mm512_sllv_epi32(one, _mm512_and_si512(object_bits, _mm512_set1_epi32(31))));
        const __mmask16 usable = eligible & ordinary & (__mmask16)~compound;
        const __mmask16 template_class = _mm512_cmple_epu32_mask(_mm512_sub_epi32(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)),
                                                                 _mm512_set1_epi32(BUSTER_TYPED_CLASS_F64 - BUSTER_TYPED_CLASS_I8));
        // A VALUE's read: its local's (check_lower_local_of) or its global's.
        const __mmask16 reading = usable & value_op & lvalue & _mm512_cmpneq_epi32_mask(types, _mm512_set1_epi32(BUSTER_TYPE_VOID)) & reads & known & (__mmask16)~aggregate &
                                  template_class;
        __mmask16 mapped = reading;
        if (writer->locals != NULL)
        {
            const __m512i local_words = _mm512_mask_i32gather_epi32(zero, reading, _mm512_srli_epi32(objects, 5), (const uint32_t*)writer->locals, 4);
            mapped &= _mm512_test_epi32_mask(local_words, _mm512_sllv_epi32(one, _mm512_and_si512(objects, _mm512_set1_epi32(31))));
        }
        const __m512i locals = _mm512_mask_i32gather_epi32(none, mapped, objects, writer->local_map, 4);
        const __m512i symbols = _mm512_mask_i32gather_epi32(zero, reading, objects, program->object_symbols, 4);
        const __mmask16 local = _mm512_cmpneq_epi32_mask(locals, none);
        const __mmask16 read = reading & (local | _mm512_cmpneq_epi32_mask(symbols, zero));
        // The ops of no constant but the members', the stores' and the VALUEs' (check_lower_template_fast's last branch).
        const __mmask16 member_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_MEMBER)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_ARROW));
        const __mmask16 store_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_ASSIGN)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_INITIALIZE));
        const __mmask16 plain = usable & _mm512_cmpeq_epi32_mask(constants, _mm512_set1_epi32(CHECK_CONSTANT_NONE)) & (__mmask16)~value_op & (__mmask16)~member_op &
                                (__mmask16)~store_op;
        const __m512i types_a = _mm512_mask_i32gather_epi32(zero, plain, conversion_offsets, &stream->conversions[node].operands[0].type, 1);
        const __m512i types_b = _mm512_mask_i32gather_epi32(zero, plain, conversion_offsets, &stream->conversions[node].operands[1].type, 1);
        const __m512i indices_a = _mm512_srli_epi32(types_a, BUSTER_TYPE_INDEX_SHIFT);
        const __m512i indices_b = _mm512_srli_epi32(types_b, BUSTER_TYPE_INDEX_SHIFT);
        const __m512i info_a = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), plain & _mm512_cmplt_epu32_mask(indices_a, type_count), indices_a,
                                                           writer->type_templates, 4);
        const __m512i info_b = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), plain & _mm512_cmplt_epu32_mask(indices_b, type_count), indices_b,
                                                           writer->type_templates, 4);
        const __m512i classes_a = _mm512_and_si512(info_a, _mm512_set1_epi32(0xff));
        const __m512i classes_b = _mm512_and_si512(info_b, _mm512_set1_epi32(0xff));
        const __m512i facts_a = _mm512_srli_epi32(info_a, 8);
        const __m512i facts_b = _mm512_srli_epi32(info_b, 8);
        const __mmask16 arithmetic = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_ADD)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_SUBTRACT)) |
                                     _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_SUBSCRIPT));
        const __mmask16 left_pointer = arithmetic & _mm512_test_epi32_mask(facts_a, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_POINTER));
        const __mmask16 right_pointer = arithmetic & _mm512_test_epi32_mask(facts_b, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_POINTER));
        const __mmask16 element = _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_CHECK_VECTOR_ELEMENT));
        const __mmask16 aggregate_a = _mm512_test_epi32_mask(facts_a, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __m512i limit = _mm512_set1_epi32(BUSTER_TYPED_CLASS_F64 - BUSTER_TYPED_CLASS_I8);
        const __mmask16 template_a = _mm512_cmple_epu32_mask(_mm512_sub_epi32(classes_a, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)), limit);
        const __mmask16 template_b = _mm512_cmple_epu32_mask(_mm512_sub_epi32(classes_b, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)), limit);
        const __mmask16 small = _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
        const __m512i binaries = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutexvar_epi8(_mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops)), binary_table))));
        // The last branch's chain: `&x`, `*p`, pointer arithmetic (the twin's), the wrappers, then the computing shapes.
        const __mmask16 taking_address = plain & _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_ADDRESS));
        const __mmask16 dereference_op = plain & _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_DEREFERENCE));
        const __mmask16 dereference = dereference_op & (aggregate | (__mmask16)~reads | template_class);
        const __mmask16 subscript = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_SUBSCRIPT));
        const __mmask16 pointer = plain & arithmetic & (__mmask16)~element & left_pointer & (__mmask16)~right_pointer &
                                  ((__mmask16)~subscript | (__mmask16)~reads | template_class);
        const __mmask16 wrapper_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CAST)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CONVERT)) |
                                     _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_POSITIVE));
        const __mmask16 wrapper = plain & wrapper_op & _mm512_cmplt_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_VECTOR));
        const __mmask16 computes = plain & (__mmask16)~aggregate & template_class & (__mmask16)~(taking_address | dereference_op | pointer | wrapper_op);
        const __mmask16 unary_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_NEGATIVE)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_BIT_NOT));
        const __mmask16 truth_op = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_NOT)) | _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CONDITION));
        const __mmask16 unary = computes & unary_op;
        const __mmask16 truth = computes & truth_op & (__mmask16)~aggregate_a & _mm512_cmplt_epu32_mask(classes_a, _mm512_set1_epi32(BUSTER_TYPED_CLASS_VECTOR)) &
                                _mm512_cmpneq_epi32_mask(classes_a, _mm512_set1_epi32(BUSTER_TYPED_CLASS_VOID));
        const __mmask16 sum = computes & arithmetic & (__mmask16)~subscript & (__mmask16)~element & (__mmask16)~left_pointer & (__mmask16)~right_pointer & template_a &
                              template_b;
        const __mmask16 binary = computes & (__mmask16)~arithmetic & (__mmask16)~unary_op & (__mmask16)~truth_op & _mm512_cmpneq_epi32_mask(binaries, zero) &
                                 template_a & template_b & (__mmask16)~aggregate_a;
        const __mmask16 taken = read | taking_address | dereference | wrapper | unary | truth | sum | binary;
        // Their entries: at most three, a value only in a read's first.
        const __mmask16 narrow = truth & _mm512_cmple_epu32_mask(_mm512_sub_epi32(classes_a, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)),
                                                                 _mm512_set1_epi32(BUSTER_TYPED_CLASS_U16 - BUSTER_TYPED_CLASS_I8));
        const __m512i test = _mm512_mask_blend_epi32(_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_NOT)), _mm512_set1_epi32(BUSTER_TYPED_NE), _mm512_set1_epi32(BUSTER_TYPED_EQ));
        const __m512i unary_kind = _mm512_mask_blend_epi32(_mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_NEGATIVE)), _mm512_set1_epi32(BUSTER_TYPED_NOT), _mm512_set1_epi32(BUSTER_TYPED_NEG));
        __m512i kind0 = _mm512_mask_blend_epi32(local, _mm512_set1_epi32(BUSTER_TYPED_GLOBAL), _mm512_set1_epi32(BUSTER_TYPED_LOCAL));
        kind0 = _mm512_mask_mov_epi32(kind0, dereference, _mm512_set1_epi32(BUSTER_TYPED_LOAD));
        kind0 = _mm512_mask_mov_epi32(kind0, unary, unary_kind);
        kind0 = _mm512_mask_mov_epi32(kind0, sum | binary, binaries);
        kind0 = _mm512_mask_mov_epi32(kind0, truth, _mm512_mask_blend_epi32(narrow, _mm512_set1_epi32(BUSTER_TYPED_CONST), _mm512_set1_epi32(BUSTER_TYPED_CONVERT)));
        const __m512i kind1 = _mm512_mask_blend_epi32(narrow, test, _mm512_set1_epi32(BUSTER_TYPED_CONST));
        const __m512i class0 = _mm512_mask_mov_epi32(classes, truth, _mm512_mask_blend_epi32(narrow, classes_a, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I32)));
        const __m512i class12 = _mm512_set1_epi32(BUSTER_TYPED_CLASS_I32);
        const __m512i kinds = _mm512_or_si512(_mm512_or_si512(kind0, _mm512_slli_epi32(kind1, 8)), _mm512_slli_epi32(test, 16));
        const __m512i entry_classes = _mm512_or_si512(_mm512_or_si512(class0, _mm512_slli_epi32(class12, 8)), _mm512_slli_epi32(class12, 16));
        __m512i own = _mm512_maskz_mov_epi32(read | unary | sum | binary | (dereference & reads), one);
        own = _mm512_mask_mov_epi32(own, truth, _mm512_mask_blend_epi32(narrow, _mm512_set1_epi32(2), _mm512_set1_epi32(3)));
        __m512i operand_counts = _mm512_maskz_mov_epi32(taking_address | dereference | wrapper | unary | truth, one);
        operand_counts = _mm512_mask_mov_epi32(operand_counts, sum | binary, _mm512_set1_epi32(2));
        const __m512i values = _mm512_maskz_mov_epi32(read, _mm512_mask_blend_epi32(local, symbols, locals));
        // The conversion to the target the scatter gave (check_lower_template_conversion's): in a value's context (these
        // shapes are never discarded), none where the types' classes and facts make it empty, the lane's state lacking
        // BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED where the table takes none of it, otherwise a discard, the test and
        // narrowing of a conversion to _Bool, or one conversion.
        const __m512i targets = _mm512_maskz_loadu_epi32(active, writer->template_targets + base);
        const __m512i target_indices = _mm512_srli_epi32(targets, BUSTER_TYPE_INDEX_SHIFT);
        const __mmask16 targeted = taken & _mm512_cmpneq_epi32_mask(targets, zero);
        const __m512i target_info = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), targeted & _mm512_cmplt_epu32_mask(target_indices, type_count), target_indices,
                                                                writer->type_templates, 4);
        const __m512i target_facts = _mm512_and_si512(_mm512_srli_epi32(target_info, 8), _mm512_set1_epi32(0xff));
        const __mmask16 target_aggregate = _mm512_test_epi32_mask(target_facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __m512i target_classes = _mm512_mask_mov_epi32(_mm512_and_si512(target_info, _mm512_set1_epi32(0xff)), target_aggregate, _mm512_set1_epi32(BUSTER_TYPED_CLASS_U64));
        const __m512i mixed_facts = _mm512_or_si512(facts, target_facts);
        const __mmask16 empty = _mm512_cmpeq_epi32_mask(classes, target_classes) & _mm512_cmplt_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASSES)) &
                                _mm512_testn_epi32_mask(_mm512_xor_si512(facts, target_facts), _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) &
                                _mm512_testn_epi32_mask(mixed_facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_COMPLEX)) &
                                _mm512_testn_epi32_mask(target_facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT));
        const __mmask16 needed = targeted & (__mmask16)~address & _mm512_cmpneq_epi32_mask(types, targets) & (__mmask16)~empty;
        const __mmask16 target_void = _mm512_cmpeq_epi32_mask(target_classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_VOID));
        const __m512i f80 = _mm512_set1_epi32(BUSTER_TYPED_CLASS_F80);
        const __mmask16 refused = needed & (((__mmask16)~target_void & _mm512_test_epi32_mask(mixed_facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX))) |
                                            _mm512_cmpge_epu32_mask(classes, f80) | _mm512_cmpge_epu32_mask(target_classes, f80));
        const __mmask16 converting = needed & (__mmask16)~refused;
        const __mmask16 discard = converting & target_void & _mm512_cmpneq_epi32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_VOID));
        const __mmask16 to_bool = converting & (__mmask16)~target_void & _mm512_test_epi32_mask(target_facts, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TYPE_BOOL));
        const __mmask16 narrow_source = to_bool & _mm512_cmple_epu32_mask(_mm512_sub_epi32(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)),
                                                                          _mm512_set1_epi32(BUSTER_TYPED_CLASS_U16 - BUSTER_TYPED_CLASS_I8));
        const __mmask16 convert = converting & (__mmask16)~target_void & (__mmask16)~to_bool & _mm512_cmpneq_epi32_mask(classes, target_classes);
        const __m512i bool_kinds = _mm512_mask_blend_epi32(narrow_source,
                                                           _mm512_set1_epi32(BUSTER_TYPED_CONST | BUSTER_TYPED_NE << 8 | BUSTER_TYPED_CONVERT << 16),
                                                           _mm512_set1_epi32(BUSTER_TYPED_CONVERT | BUSTER_TYPED_CONST << 8 | BUSTER_TYPED_NE << 16 | BUSTER_TYPED_CONVERT << 24));
        const __m512i bool_classes = _mm512_mask_blend_epi32(narrow_source,
                                                             _mm512_or_si512(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I32 << 8 | BUSTER_TYPED_CLASS_U8 << 16)),
                                                             _mm512_set1_epi32(BUSTER_TYPED_CLASS_I32 | BUSTER_TYPED_CLASS_I32 << 8 | BUSTER_TYPED_CLASS_I32 << 16 | BUSTER_TYPED_CLASS_U8 << 24));
        __m512i conversion_kinds = _mm512_maskz_mov_epi32(discard, _mm512_set1_epi32(BUSTER_TYPED_DISCARD));
        conversion_kinds = _mm512_mask_mov_epi32(conversion_kinds, to_bool, bool_kinds);
        conversion_kinds = _mm512_mask_mov_epi32(conversion_kinds, convert, _mm512_set1_epi32(BUSTER_TYPED_CONVERT));
        __m512i conversion_classes = _mm512_maskz_mov_epi32(discard, classes);
        conversion_classes = _mm512_mask_mov_epi32(conversion_classes, to_bool, bool_classes);
        conversion_classes = _mm512_mask_mov_epi32(conversion_classes, convert, target_classes);
        __m512i conversions = _mm512_maskz_mov_epi32(discard | convert, one);
        conversions = _mm512_mask_mov_epi32(conversions, to_bool, _mm512_mask_blend_epi32(narrow_source, _mm512_set1_epi32(3), _mm512_set1_epi32(4)));
        // The slots: each lane's kinds and classes eight bytes, its own then its conversion's, and its values a row of
        // eight words, the first a read's and the rest none.
        const uint64_t slot = (uint64_t)base * BUSTER_CHECK_LOWER_TEMPLATE_MOST;
        const __mmask8 low = (__mmask8)taken;
        const __mmask8 high = (__mmask8)(taken >> 8);
        const __m512i shifts_low = _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(own)), 3);
        const __m512i shifts_high = _mm512_slli_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(own, 1)), 3);
        const __m512i kinds_low = _mm512_sllv_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(conversion_kinds)), shifts_low);
        const __m512i kinds_high = _mm512_sllv_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(conversion_kinds, 1)), shifts_high);
        const __m512i classes_low = _mm512_sllv_epi64(_mm512_cvtepu32_epi64(_mm512_castsi512_si256(conversion_classes)), shifts_low);
        const __m512i classes_high = _mm512_sllv_epi64(_mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(conversion_classes, 1)), shifts_high);
        // A lane's own entries' bytes (`own`, at most three, so that with a conversion's four they fit the slot's eight),
        // then the conversion's: (own & mask) | conversion, ternary logic 0xea.
        const __m512i own_mask_low = _mm512_sub_epi64(_mm512_sllv_epi64(_mm512_set1_epi64(1), shifts_low), _mm512_set1_epi64(1));
        const __m512i own_mask_high = _mm512_sub_epi64(_mm512_sllv_epi64(_mm512_set1_epi64(1), shifts_high), _mm512_set1_epi64(1));
        _mm512_mask_storeu_epi64(writer->template_slot_kinds + slot, low,
                                 _mm512_ternarylogic_epi64(own_mask_low, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(kinds)), kinds_low, 0xea));
        _mm512_mask_storeu_epi64(writer->template_slot_kinds + slot + 64, high,
                                 _mm512_ternarylogic_epi64(own_mask_high, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(kinds, 1)), kinds_high, 0xea));
        _mm512_mask_storeu_epi64(writer->template_slot_classes + slot, low,
                                 _mm512_ternarylogic_epi64(own_mask_low, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(entry_classes)), classes_low, 0xea));
        _mm512_mask_storeu_epi64(writer->template_slot_classes + slot + 64, high,
                                 _mm512_ternarylogic_epi64(own_mask_high, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(entry_classes, 1)), classes_high, 0xea));
        uint64_t* slot_values = writer->template_slot_values + slot;
        const __m512i values_low = _mm512_cvtepu32_epi64(_mm512_castsi512_si256(values));
        const __m512i values_high = _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(values, 1));
        for (uint32_t i = 0; i < 8; i += 1)
        {
            const __m512i pick = _mm512_setr_epi64((int64_t)i, 8, 8, 8, 8, 8, 8, 8);
            _mm512_mask_storeu_epi64(slot_values + (uint64_t)i * BUSTER_CHECK_LOWER_TEMPLATE_MOST, (__mmask8)(0u - ((low >> i) & 1u)), _mm512_permutex2var_epi64(values_low, pick, zero));
            _mm512_mask_storeu_epi64(slot_values + (uint64_t)(i + 8) * BUSTER_CHECK_LOWER_TEMPLATE_MOST, (__mmask8)(0u - ((high >> i) & 1u)),
                                     _mm512_permutex2var_epi64(values_high, pick, zero));
        }
        // The operands, two a node, their count and the own entries' count; the state's mark.
        const __m512i operands_a = _mm512_maskz_loadu_epi32(active, graph->operand0 + node);
        const __m512i operands_b = _mm512_maskz_loadu_epi32(active, graph->operand1 + node);
        const __mmask16 pairs_low = (__mmask16)_pdep_u32(taken & 0xffu, 0x5555u) | (__mmask16)(_pdep_u32(taken & 0xffu, 0x5555u) << 1);
        const __mmask16 pairs_high = (__mmask16)_pdep_u32(taken >> 8, 0x5555u) | (__mmask16)(_pdep_u32(taken >> 8, 0x5555u) << 1);
        _mm512_mask_storeu_epi32(writer->template_children + 2 * (uint64_t)base, pairs_low, _mm512_permutex2var_epi32(operands_a, interleave_low, operands_b));
        _mm512_mask_storeu_epi32(writer->template_children + 2 * (uint64_t)base + 16, pairs_high, _mm512_permutex2var_epi32(operands_a, interleave_high, operands_b));
        _mm_mask_storeu_epi8(writer->template_counts + base, taken, _mm512_cvtepi32_epi8(operand_counts));
        _mm_mask_storeu_epi8(writer->template_own + base, taken, _mm512_cvtepi32_epi8(own));
        _mm_mask_storeu_epi8(writer->template_total + base, taken, _mm512_cvtepi32_epi8(_mm512_add_epi32(own, conversions)));
        const __m512i marks = _mm512_mask_or_epi32(_mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_FAST), (__mmask16)~refused, _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_FAST),
                                                   _mm512_set1_epi32(BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED));
        _mm_mask_storeu_epi8(writer->template_states + base, taken, _mm512_cvtepi32_epi8(_mm512_or_si512(states, marks)));
    }
}

// The vector classification's check (CheckLowerWriter.template_check): the lane at `r` must have the context
// check_lower_template_child_context gives it, the fast twin must take it with the same evaluated operands and own entries,
// and check_lower_template_conversion must give it the same entries, total and whether the table takes the conversion.
static bool check_lower_template_vector_twin(const CheckLowerWriter* writer, uint32_t node, uint32_t r, const CheckLowerTemplateFast* vector)
{
    const uint64_t slot = (uint64_t)r * BUSTER_CHECK_LOWER_TEMPLATE_MOST;
    const TypeId target = writer->template_targets[r];
    const uint8_t context = check_lower_template_child_context(writer, node, (writer->template_contexts[r] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, target);
    uint8_t kinds[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    uint8_t classes[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    uint64_t values[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    CheckLowerTemplateEntries twin = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
    };
    const bool columns = writer->program->stream->dense_values == NULL && (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) == 0;
    const CheckLowerTemplateFast fast = columns ? check_lower_template_fast(writer, node, (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, &twin) :
                                                  (CheckLowerTemplateFast){ .handled = false };
    const uint32_t own = twin.count;
    const bool converted = fast.handled && check_lower_template_conversion(writer, writer->program->stream->value_types[node], target, context, &twin);
    bool same = fast.handled && context == writer->template_contexts[r] && fast.count == vector->count && own == writer->template_own[r] &&
                twin.count == writer->template_total[r] && converted == ((writer->template_states[r] & BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED) != 0) && !twin.taken_address;
    for (uint32_t k = 0; same && k < fast.count; k += 1)
    {
        same = fast.children[k] == vector->children[k];
    }
    for (uint32_t e = 0; same && e < twin.count; e += 1)
    {
        same = kinds[e] == writer->template_slot_kinds[slot + e] && classes[e] == writer->template_slot_classes[slot + e] && values[e] == writer->template_slot_values[slot + e];
    }
    return same;
}

// The fast twin's check (CheckLowerWriter.template_check): the general rules must give `node` a template, its own entries
// and its evaluated operands as `fast` and `entries` do, and give each operand the context and the target the scatter
// gave it, where the operand is one a template could take.
static bool check_lower_template_twin(const CheckLowerWriter* writer, uint32_t node, uint32_t r, uint8_t context, const CheckLowerTemplateFast* fast,
                                      const CheckLowerTemplateEntries* entries)
{
    uint8_t kinds[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    uint8_t classes[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    uint64_t values[BUSTER_CHECK_LOWER_TEMPLATE_MOST];
    CheckLowerTemplateEntries general = { .kinds = kinds, .classes = classes, .values = values };
    const CheckLowerTemplateValue value = check_lower_template_value(writer->program->stream, node);
    const CheckLowerTemplateUse use = check_lower_template_classify(writer, node, context, &value);
    bool same = use.kind != CHECK_LOWER_TEMPLATE_NONE && use.count == fast->count;
    if (same)
    {
        check_lower_template_own(writer, node, use.kind, context, &value, &general);
        same = general.count == entries->count && general.taken_address == entries->taken_address && memcmp(kinds, entries->kinds, general.count) == 0 &&
               memcmp(classes, entries->classes, general.count) == 0 && memcmp(values, entries->values, general.count * sizeof(uint64_t)) == 0;
    }
    for (uint32_t k = 0; same && k < use.count; k += 1)
    {
        const uint32_t c = use.children[k] - writer->template_first_node;
        same = use.children[k] == fast->children[k];
        if (same && use.children[k] < node && c < r && (writer->template_states[c] & (BUSTER_CHECK_LOWER_TEMPLATE_SHARED | BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED)) == 0)
        {
            same = writer->template_contexts[c] == use.contexts[k] && writer->template_targets[c] == use.targets[k];
        }
    }
    return same;
}

// Body `body`'s templates (CheckLowerWriter.template_ready): its nodes' contexts, templates, entries and closed subtrees,
// before its statements are lowered. A body the columns can't take has none.
static __attribute__((noinline)) void check_lower_template_body(CheckLowerWriter* writer, const CheckProgramBody* body)
{
    uint32_t first = 0;
    uint32_t span = 0;
    writer->template_ready = false;
    check_lower_template_range(writer, body, &first, &span);
    if (span != 0 && span <= writer->template_capacity && !writer->track)
    {
        const CheckStream* stream = writer->program->stream;
        writer->template_first_node = first;
        writer->template_span = span;
        memset(writer->template_states, 0, span);
        memset(writer->template_contexts, 0, span);
        memset(writer->template_targets, 0, (uint64_t)span * sizeof(TypeId));
        const bool vector_path = !BUSTER_SCALAR_PATH(writer->scalar) && stream->dense_values == NULL && writer->type_templates != NULL;
        if (!vector_path || writer->template_check)
        {
            check_lower_template_scatter(writer, first, span);
        }
        if (vector_path)
        {
            if (!writer->template_vector_ready)
            {
                check_lower_template_vector_begin(writer);
                writer->template_vector_ready = true;
            }
            if (writer->template_check)
            {
                // The scalar scatter's results, which the vector one's must equal for the nodes both leave evaluated and
                // unshared.
                memcpy(writer->template_check_states, writer->template_states, span);
                memcpy(writer->template_check_contexts, writer->template_contexts, span);
                memcpy(writer->template_check_targets, writer->template_targets, (uint64_t)span * sizeof(TypeId));
                memset(writer->template_states, 0, span);
                memset(writer->template_contexts, 0, span);
                memset(writer->template_targets, 0, (uint64_t)span * sizeof(TypeId));
            }
            check_lower_template_scatter_vector(writer, first, span);
            const uint8_t excluded = BUSTER_CHECK_LOWER_TEMPLATE_SHARED | BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED;
            for (uint32_t r = 0; writer->template_check && r < span; r += 1)
            {
                const bool scalar_taken = (writer->template_check_states[r] & excluded) == 0;
                const bool vector_taken = (writer->template_states[r] & excluded) == 0;
                const bool same = scalar_taken == vector_taken &&
                                  (!scalar_taken || ((writer->template_check_contexts[r] == writer->template_contexts[r]) &&
                                                     writer->template_check_targets[r] == writer->template_targets[r]));
                writer->template_mismatches += same ? 0 : 1;
            }
            check_lower_template_vector(writer, first, span);
        }
        // Each node on its own, from the first up: its operands' contexts are final before its own turn, and their
        // classifications and closures before its own. Its context with its discard, its template, its entries and its
        // conversion's in its slot, then its entries' offset in node order, which is postorder, and whether its subtree is
        // closed: a template whose evaluated operands the scatter gave the context and the target its rule gives them, each
        // taken alone, closed and converted, the first one's entries (its subtree's, then its conversion's) followed by the
        // second's, and the last ending where the node's own start. The splice copies the slots of a closed subtree's nodes
        // in node order, which then give those entries.
        const uint8_t mask = BUSTER_CHECK_LOWER_TEMPLATE_CLOSED | BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED;
        uint32_t cursor = 0;
        for (uint32_t r = 0; r < span; r += 1)
        {
            const uint32_t node = first + r;
            uint8_t state = writer->template_states[r];
            const TypeId target = writer->template_targets[r];
            // The vector classification's lanes have their contexts, never discarded, and their conversions.
            const bool vector = (state & BUSTER_CHECK_LOWER_TEMPLATE_FAST) != 0;
            const uint8_t context = vector ? writer->template_contexts[r] :
                                             check_lower_template_child_context(writer, node, (writer->template_contexts[r] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, target);
            uint32_t own_count = 0;
            uint32_t total = 0;
            bool closed = false;
            uint32_t start = r;
            uint32_t nodes = 1;
            uint32_t last = node;
            writer->template_contexts[r] = context;
            if ((state & (BUSTER_CHECK_LOWER_TEMPLATE_SHARED | BUSTER_CHECK_LOWER_TEMPLATE_UNEVALUATED)) == 0)
            {
                const uint64_t slot = (uint64_t)r * BUSTER_CHECK_LOWER_TEMPLATE_MOST;
                CheckLowerTemplateEntries entries = {
                    .kinds = writer->template_slot_kinds + slot,
                    .classes = writer->template_slot_classes + slot,
                    .values = writer->template_slot_values + slot,
                };
                // The vector classification's result where it took the node, else the fast twin where it takes it (its
                // operands' contexts and targets the scatter's table's, which need no comparison), the general rules
                // otherwise.
                const bool columns = stream->dense_values == NULL && (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) == 0;
                CheckLowerTemplateFast fast = { .handled = false };
                if (vector)
                {
                    fast.children[0] = writer->template_children[2 * (uint64_t)r];
                    fast.children[1] = writer->template_children[2 * (uint64_t)r + 1];
                    fast.count = writer->template_counts[r];
                    fast.handled = true;
                    entries.count = writer->template_own[r];
                    writer->template_vector_nodes += 1;
                    if (writer->template_check)
                    {
                        writer->template_mismatches += check_lower_template_vector_twin(writer, node, r, &fast) ? 0 : 1;
                    }
                }
                else if (columns)
                {
                    fast = check_lower_template_fast(writer, node, (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, &entries);
                }
                CheckLowerTemplateUse use = { .kind = CHECK_LOWER_TEMPLATE_NONE };
                TypeId type = 0;
                if (fast.handled)
                {
                    use.kind = CHECK_LOWER_TEMPLATE_PASS;
                    use.count = fast.count;
                    use.children[0] = fast.children[0];
                    use.children[1] = fast.children[1];
                    type = stream->value_types[node];
                    writer->template_fast_nodes += 1;
                    if (writer->template_check)
                    {
                        writer->template_mismatches += check_lower_template_twin(writer, node, r, context, &fast, &entries) ? 0 : 1;
                    }
                }
                else
                {
                    const CheckLowerTemplateValue value = check_lower_template_value(stream, node);
                    entries.count = 0;
                    entries.taken_address = false;
                    use = check_lower_template_classify(writer, node, context, &value);
                    type = value.type;
                    if (use.kind != CHECK_LOWER_TEMPLATE_NONE)
                    {
                        check_lower_template_own(writer, node, use.kind, context, &value, &entries);
                    }
                }
                if (use.kind != CHECK_LOWER_TEMPLATE_NONE)
                {
                    own_count = entries.count;
                    const bool converted = vector ? (state & BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED) != 0 : check_lower_template_conversion(writer, type, target, context, &entries);
                    total = vector ? writer->template_total[r] : entries.count;
                    state |= converted ? BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED : 0;
                    closed = true;
                    bool taken_address = entries.taken_address;
                    // The offset the next operand's subtree must start at: the node's own entries' for the last.
                    for (uint32_t k = use.count; k-- > 0;)
                    {
                        const uint32_t c = use.children[k] - first;
                        const bool given = closed && use.children[k] < node && c < span && (writer->template_states[c] & mask) == mask &&
                                           (fast.handled || (writer->template_contexts[c] == use.contexts[k] && writer->template_targets[c] == use.targets[k])) &&
                                           writer->template_offsets[c] + writer->template_total[c] == (k + 1 == use.count ? cursor : writer->template_offsets[start]);
                        closed = given;
                        start = given ? writer->template_first[c] - first : start;
                        nodes += given ? writer->template_nodes[c] : 0;
                        last = given && k + 1 == use.count ? writer->template_last[c] : last;
                        taken_address = taken_address || (given && (writer->template_states[c] & BUSTER_CHECK_LOWER_TEMPLATE_TAKEN_ADDRESS) != 0);
                    }
                    state |= closed ? BUSTER_CHECK_LOWER_TEMPLATE_CLOSED : 0;
                    state |= closed && taken_address ? BUSTER_CHECK_LOWER_TEMPLATE_TAKEN_ADDRESS : 0;
                    state = closed ? state : (uint8_t)(state & ~BUSTER_CHECK_LOWER_TEMPLATE_CONVERTED);
                }
            }
            start = closed ? start : r;
            nodes = closed ? nodes : 1;
            last = closed ? last : node;
            writer->template_states[r] = state;
            writer->template_own[r] = (uint8_t)own_count;
            writer->template_total[r] = (uint8_t)total;
            writer->template_offsets[r] = cursor;
            writer->template_first[r] = first + start;
            writer->template_nodes[r] = nodes;
            writer->template_last[r] = last;
            cursor += total;
        }
        writer->template_ready = true;
    }
}

// Splices closed `node`'s entries, popped with the tasks' `flags`, in place of its expansion: whether it did. Its context
// must be the one the pass gave it (a node no template evaluates takes a value's), the entries' room there, and the
// writer's state what its tasks would leave: the token its last node's, its locals' addresses taken. The entries are its
// subtree's nodes' slots in node order, each node's with its conversion, but its own, whose conversion the task under it
// writes.
static inline __attribute__((always_inline)) bool check_lower_template_splice(CheckLowerWriter* writer, uint32_t node, uint8_t flags)
{
    const uint32_t r = node - writer->template_first_node;
    bool spliced = false;
    if (writer->template_ready && r < writer->template_span && (writer->template_states[r] & BUSTER_CHECK_LOWER_TEMPLATE_CLOSED) != 0 &&
        flags == writer->template_contexts[r])
    {
        const uint32_t start = writer->template_first[r] - writer->template_first_node;
        const uint32_t length = writer->template_offsets[r] - writer->template_offsets[start] + writer->template_own[r];
        // Room past the entries for a slot's whole copy.
        const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
        if (writer->count <= room && length + BUSTER_CHECK_LOWER_TEMPLATE_MOST <= room - writer->count)
        {
            uint32_t count = writer->count;
            for (uint32_t m = start; m <= r; m += 1)
            {
                const uint64_t slot = (uint64_t)m * BUSTER_CHECK_LOWER_TEMPLATE_MOST;
                memcpy(writer->kinds + count, writer->template_slot_kinds + slot, BUSTER_CHECK_LOWER_TEMPLATE_MOST);
                memcpy(writer->classes + count, writer->template_slot_classes + slot, BUSTER_CHECK_LOWER_TEMPLATE_MOST);
                memcpy(writer->values + count, writer->template_slot_values + slot, BUSTER_CHECK_LOWER_TEMPLATE_MOST * sizeof(uint64_t));
                count += m == r ? writer->template_own[m] : writer->template_total[m];
            }
            if ((writer->template_states[r] & BUSTER_CHECK_LOWER_TEMPLATE_TAKEN_ADDRESS) != 0)
            {
                for (uint32_t e = writer->count; e < count; e += 1)
                {
                    if (writer->kinds[e] == BUSTER_TYPED_LOCAL_ADDRESS)
                    {
                        writer->local_flags[writer->values[e]] |= BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                    }
                }
            }
            writer->count = count;
            writer->token = writer->program->input->graph.tokens[writer->template_last[r]];
            writer->expression_nodes += writer->template_nodes[r];
            writer->template_spliced_nodes += writer->template_nodes[r];
            writer->template_spliced_entries += length;
            spliced = true;
        }
    }
    return spliced;
}

#endif
