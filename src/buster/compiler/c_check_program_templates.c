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
// an overloaded address: on the 9700X these scalar steps cost about three times what the splices save (docs/c-lowering.md),
// so no compile takes them until they are data-parallel.
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
    if (value->overload_head != 0 || (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) != 0 ||
        (op == CHECK_OP_CAST && (stream->conversions[node].operands[0].flags & BUSTER_CHECK_CONVERT_TO_UNION) != 0))
    {
        // An overload's placeholder, a _BitInt's, a complex value's and a cast to a union (#2301) are the tasks'.
    }
    else if (op == CHECK_OP_VALUE && known && check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND) && !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC))
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
        uint8_t binary = check_lower_binary(op);
        uint64_t right = 0;
        const bool power = check_lower_power(writer, node, class, &binary, &right) != BUSTER_CHECK_LOWER_POWER_NONE;
        // A rewritten right operand no longer has its child's original entries. Leave this root to the task loop;
        // its children may still be closed templates. The fast twin makes the same choice below.
        // The shifts' and compares' counts are of these classes too, so check_lower_count adds nothing.
        if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)) &&
            !check_lower_aggregate(writer, type_a) && !power)
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
            const uint32_t declaration = op == CHECK_OP_INITIALIZE ? check_lower_initializer_of(writer, node) : BUSTER_CHECK_NONE;
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
        const uint32_t declaration = op == CHECK_OP_INITIALIZE ? check_lower_initializer_of(writer, node) : BUSTER_CHECK_NONE;
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
                uint8_t* state = writer->template_states + c;
                *state |= (*state & BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED) != 0 ? BUSTER_CHECK_LOWER_TEMPLATE_SHARED : BUSTER_CHECK_LOWER_TEMPLATE_ASSIGNED;
                writer->template_contexts[c] = context;
                writer->template_targets[c] = target;
            }
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
    const bool compound = op == CHECK_OP_VALUE && known && check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND) && !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC);
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
            const uint32_t declaration = op == CHECK_OP_INITIALIZE ? check_lower_initializer_of(writer, node) : BUSTER_CHECK_NONE;
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
            uint8_t selected = binary;
            uint64_t right = 0;
            const bool power = check_lower_power(writer, node, class, &selected, &right) != BUSTER_CHECK_LOWER_POWER_NONE;
            if (check_lower_template_class(check_lower_class(writer, type_a)) && check_lower_template_class(check_lower_class(writer, type_b)) &&
                !check_lower_aggregate(writer, type_a) && !power)
            {
                check_lower_template_put(entries, binary, class, 0);
                fast.count = 2;
            }
        }
        fast.handled = fast.count != 0;
    }
    return fast;
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
        check_lower_template_scatter(writer, first, span);
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
            const uint8_t context = check_lower_template_child_context(writer, node, (writer->template_contexts[r] & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, target);
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
                // The fast twin where it takes the node (its operands' contexts and targets the scatter's table's, which
                // need no comparison), the general rules otherwise.
                const bool columns = stream->dense_values == NULL && (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) == 0;
                const CheckLowerTemplateFast fast = columns ? check_lower_template_fast(writer, node, (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0, &entries) :
                                                              (CheckLowerTemplateFast){ .handled = false };
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
                    const bool converted = check_lower_template_conversion(writer, type, target, context, &entries);
                    total = entries.count;
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

