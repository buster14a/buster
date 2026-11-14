// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine's C sweep (c_check_node_machine.c has the machine): node_machine_contexts walks the records
// down from the root and gives each evaluated one its template in its context (node_machine_classify, #1048's
// check_lower_template_classify rules read from the records, and node_machine_logical for `&&`, `||` and `?:`), and its
// evaluated operands their contexts and targets (node_machine_evaluate); a call's by node_machine_classify_call, the
// lowering's plain call; an update's by its plan (node_machine_update_plan, check_lower_update's); then the labels'
// numbers (node_machine_number_labels). It reads the records T typed through the
// record accessors alone, and writes its own columns through them.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_CONTEXTS_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_CONTEXTS_INCLUDED

// Whether record `r`'s constant's entries are a template's (check_lower_template_constant_takes).
static bool node_machine_constant_takes(const NodeMachine* machine, uint32_t r)
{
    const TypeId type = node_machine_record_type(machine, r);
    const uint8_t class = node_machine_class(machine, type);
    const uint8_t facts = node_machine_facts(machine, type);
    const CheckConstantKind constant_kind = node_machine_record_constant_kind(machine, r);
    bool takes = false;
    if (constant_kind == CHECK_CONSTANT_ADDRESS)
    {
        takes = node_machine_address_takes(machine, node_machine_record_object(machine, r));
    }
    else if ((facts & (BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_AGGREGATE)) == 0)
    {
        takes = (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64) ||
                (constant_kind == CHECK_CONSTANT_FLOATING && (class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64));
    }
    return takes;
}

// The child's context as the tasks give it (check_lower_template_child_context): `address`, or discarded where the target
// is void and the child is a store of an aggregate, a bit-field's assignment or update, or a comma.
static uint8_t node_machine_child_context(const NodeMachine* machine, uint32_t child, bool address, TypeId target)
{
    uint8_t context = address ? BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS : 0;
    if (!address && target == BUSTER_TYPE_VOID)
    {
        const CheckOp op = node_machine_record_op(machine, child);
        const bool store = op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE;
        const uint32_t destination = node_machine_record_left(machine, child);
        const bool bit_field = (op == CHECK_OP_ASSIGN || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)) && destination < child &&
                               (node_machine_record_flags(machine, destination) & BUSTER_CHECK_BIT_FIELD) != 0;
        context = (store && (node_machine_record_facts(machine, child) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0) || bit_field || op == CHECK_OP_COMMA ? BUSTER_CHECK_LOWER_TEMPLATE_DISCARD : 0;
    }
    return context;
}

// Record `child` evaluated by its consumer, the record C is classifying, with `address` and `target`: below one more
// call where its consumer is one.
static void node_machine_evaluate(NodeMachine* machine, uint32_t child, bool address, TypeId target)
{
    const uint32_t consumer = machine->classified;
    node_machine_record_set_evaluated(machine, child);
    node_machine_record_set_context(machine, child, node_machine_child_context(machine, child, address, target));
    node_machine_record_set_target(machine, child, target);
    node_machine_record_set_call_depth(machine, child, node_machine_record_call_depth(machine, consumer) + (node_machine_record_op(machine, consumer) == CHECK_OP_CALL));
}

// Record `child` evaluated by its consumer `r` as a value converted to `target`, with `r`'s segment `hook`
// (BUSTER_NODE_MACHINE_AFTER_CONDITION or _AFTER_ARM) after its row.
static void node_machine_evaluate_hooked(NodeMachine* machine, uint32_t child, TypeId target, uint32_t r, uint8_t hook)
{
    node_machine_evaluate(machine, child, false, target);
    node_machine_record_set_hook(machine, child, r, hook);
}

// Whether a value of `type` takes check_lower_truth's test of it by a constant and NE (as the truth template does): a type of
// a class short of a vector's and not void, and no aggregate (a complex value's or a __float128's test is a task of its
// own) or vector (a vector condition's conditional is one too, check_lower_vector_logical).
static bool node_machine_testable(const NodeMachine* machine, TypeId type)
{
    const uint32_t word = node_machine_word(machine, type);
    const uint8_t class = (uint8_t)word;
    const uint8_t kind = (uint8_t)(word >> 16);
    return ((word >> 8) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0 && class < BUSTER_TYPED_CLASS_VECTOR && class != BUSTER_TYPED_CLASS_VOID &&
           kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR;
}

// C's logical operator or conditional at record `r` (`op`), of value class `class` (check_lower_logical's rules, which
// check_lower_expand reaches for one that isn't folded, of a scalar class, or a conditional's of void, whose condition is
// no vector): its template, with its evaluated operands given their targets and its segments after them, or
// CHECK_LOWER_TEMPLATE_NONE. A condition the program knows (check_program_runtime_truth's, from its record) leaves its
// untaken operand unevaluated and numbers no label; a conditional of one operand twice (`a ?: b`), whose condition the
// tasks keep in a temporary, is the tasks'.
static CheckLowerTemplate node_machine_logical(NodeMachine* machine, uint32_t r, CheckOp op, uint8_t class)
{
    const uint32_t a = node_machine_record_left(machine, r);
    const uint32_t b = node_machine_record_right(machine, r);
    const bool conditional = op == CHECK_OP_CONDITIONAL;
    const uint32_t c = conditional ? node_machine_record_third(machine, r) : UINT32_MAX;
    const TypeId type = node_machine_record_type(machine, r);
    const TypeId type_a = node_machine_record_converted(machine, r, 0);
    const TypeId type_b = node_machine_record_converted(machine, r, 1);
    const bool scalar = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 && (node_machine_record_facts(machine, r) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) == 0;
    const bool shape = a < r && b < r && (!conditional || (c < r && a != b));
    CheckLowerTemplate kind = CHECK_LOWER_TEMPLATE_NONE;
    if (shape && (scalar || (conditional && class == BUSTER_TYPED_CLASS_VOID)))
    {
        const uint32_t flags = node_machine_record_flags(machine, a);
        bool truth = false;
        const bool known = node_machine_record_constant_kind(machine, a) != CHECK_CONSTANT_NONE && (flags & BUSTER_CHECK_FP_ENVIRONMENT) == 0 &&
                           node_machine_record_fault(machine, a) == CHECK_FAULT_NONE && node_machine_record_truth(machine, a, &truth);
        const bool effects = (flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0;
        if (known)
        {
            // The condition, for its effects alone, then the arm or the second operand it leaves the value to, if any.
            const bool decided = !conditional && (op == CHECK_OP_LOGICAL_OR) == truth;
            const bool second = !conditional && !decided;
            kind = conditional ? CHECK_LOWER_TEMPLATE_PASS : decided ? (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_DECIDED :
                   node_machine_testable(machine, type_b) ? (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_SECOND : CHECK_LOWER_TEMPLATE_NONE;
            if (kind != CHECK_LOWER_TEMPLATE_NONE && effects)
            {
                node_machine_evaluate(machine, a, false, BUSTER_TYPE_VOID);
            }
            if (kind != CHECK_LOWER_TEMPLATE_NONE && (conditional || second))
            {
                node_machine_evaluate(machine, conditional ? (truth ? b : c) : b, false, conditional ? type : type_b);
            }
        }
        else if (node_machine_testable(machine, type_a) && (conditional || node_machine_testable(machine, type_b)))
        {
            kind = (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_BRANCH;
            machine->stack[machine->label_count / 2] = r;
            machine->label_count += 2;
            node_machine_evaluate_hooked(machine, a, type_a, r, BUSTER_NODE_MACHINE_AFTER_CONDITION);
            if (conditional)
            {
                node_machine_evaluate_hooked(machine, b, type, r, BUSTER_NODE_MACHINE_AFTER_ARM);
                node_machine_evaluate(machine, c, false, type);
            }
            else
            {
                node_machine_evaluate(machine, b, false, type_b);
            }
        }
    }
    return kind;
}

// How the tasks write the division or remainder at record `r` (or its update's) in `class` (check_lower_power's answer,
// BUSTER_CHECK_LOWER_POWER_*), read from the records: by a constant power of two, a shift or a mask, its count or mask
// into `right`, or a signed one's bias sequence, its k into `right`. The templates leave such a node to the tasks; an
// update writes a shift or a mask as they do, and leaves a signed one's sequence to them.
static uint8_t node_machine_power(const NodeMachine* machine, uint32_t r, uint8_t class, uint8_t binary, uint64_t* right)
{
    uint8_t answer = BUSTER_CHECK_LOWER_POWER_NONE;
    const uint32_t b = node_machine_record_right(machine, r);
    const TypeId converted = node_machine_record_converted(machine, r, 1);
    *right = 0;
    if ((binary == BUSTER_TYPED_DIV || binary == BUSTER_TYPED_MOD) && class >= BUSTER_TYPED_CLASS_I32 && class <= BUSTER_TYPED_CLASS_U64 && b < r &&
        (node_machine_facts(machine, converted) & BUSTER_CHECK_LOWER_TYPE_BIT_INT) == 0)
    {
        const TypeId type = node_machine_record_type(machine, b);
        const CheckOp op = node_machine_record_op(machine, b);
        const uint8_t source_class = node_machine_class(machine, type);
        const bool plain_integer = source_class >= BUSTER_TYPED_CLASS_I8 && source_class <= BUSTER_TYPED_CLASS_U64 &&
                                   (node_machine_facts(machine, type) & BUSTER_CHECK_LOWER_TYPE_BIT_INT) == 0;
        const bool proven = plain_integer && node_machine_record_constant_kind(machine, b) == CHECK_CONSTANT_INTEGER && node_machine_record_fault(machine, b) == CHECK_FAULT_NONE &&
                            node_machine_record_category(machine, b) == CHECK_RVALUE && !node_machine_record_overloaded(machine, b) &&
                            (node_machine_record_flags(machine, b) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && op != CHECK_OP_STATEMENT_VALUE &&
                            op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY;
        uint8_t kind = binary;
        answer = proven ? check_lower_power_divisor(source_class, node_machine_record_integer(machine, b), class, &kind, right) : BUSTER_CHECK_LOWER_POWER_NONE;
    }
    return answer;
}

static bool node_machine_scalar_class(uint8_t class)
{
    return class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
}

// The plan of the update at record `r`, into `update`: whether the machine takes it, as check_lower_update writes it. It
// takes a destination of a class I8 to F64 that is neither an aggregate, atomic, a _BitInt nor complex, nor a bit-field,
// a vector's element, a register variable's (a global one is its register's, check_lower_global_register_update; the
// census refuses every one) or in a segment's space, and a computation of such a class: a local's or a global's by name
// (`direct`), any other lvalue through its address, a pointer's by its pointee's size (not a variable-length array's).
static __attribute__((noinline)) bool node_machine_update_plan(const NodeMachine* machine, uint32_t r, NodeMachineUpdate* update)
{
    const CheckProgram* program = machine->program;
    const TypeTable* types = machine->types;
    const CheckOp op = node_machine_record_op(machine, r);
    const uint32_t a = node_machine_record_left(machine, r);
    const uint32_t b = node_machine_record_right(machine, r);
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool subtract = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT || op == CHECK_OP_SUBTRACT_ASSIGN;
    const bool shaped = a < r && (increment ? b == UINT32_MAX : b < r);
    const uint32_t destination = shaped ? a : r;
    const TypeId stored = node_machine_record_type(machine, destination);
    const uint32_t destination_flags = node_machine_record_flags(machine, destination);
    const TypeId calculated = node_machine_record_converted(machine, r, 0);
    const TypeId right = node_machine_record_converted(machine, r, 1);
    const uint32_t stored_word = node_machine_word(machine, stored);
    const uint32_t calculated_word = node_machine_word(machine, calculated);
    const uint32_t right_word = node_machine_word(machine, right);
    const uint8_t stored_class = (uint8_t)stored_word;
    const uint8_t calculated_class = (uint8_t)calculated_word;
    const uint32_t excluded = BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX;
    const bool pointer = ((stored_word >> 8) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool named = shaped && node_machine_record_op(machine, a) == CHECK_OP_VALUE;
    const uint32_t object = named ? node_machine_record_object(machine, a) : 0;
    const bool known = object != 0 && object < program->object_count;
    const bool local = known && node_machine_local(machine, object);
    bool takes = shaped && node_machine_record_category(machine, destination) == CHECK_LVALUE && node_machine_scalar_class(stored_class) &&
                 node_machine_scalar_class(calculated_class) && (((stored_word | calculated_word) >> 8) & excluded) == 0 &&
                 (destination_flags & (BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_VECTOR_ELEMENT)) == 0 &&
                 (!named || (known && !check_input_bit(program->input->declaration_bits, object - 1, BUSTER_CHECK_BITS_REGISTER)));
    // Operand 1's type, which check_lower_count reads for a shift's count, is of such a class too.
    takes = takes && (increment || pointer || (node_machine_scalar_class((uint8_t)right_word) && ((right_word >> 8) & excluded) == 0));
    takes = takes && check_lower_segment_override(types, stored) == 0;
    const uint64_t stride = takes && pointer ? check_action_pointer_stride(types, stored) : 0;
    takes = takes && (!pointer || stride != 0);
    const uint8_t computing = check_lower_computed(calculated_class);
    uint8_t kind = increment ? (subtract ? BUSTER_TYPED_SUB : BUSTER_TYPED_ADD) : check_lower_binary(op);
    uint64_t constant = 0;
    const uint8_t answer = takes && !increment && !pointer ? node_machine_power(machine, r, computing, kind, &constant) : BUSTER_CHECK_LOWER_POWER_NONE;
    // A signed power of two's sequence (check_lower_signed_power_entries) is past an update's row
    // (BUSTER_NODE_MACHINE_ROW_MOST): the machine leaves the update to the tasks.
    takes = takes && answer != BUSTER_CHECK_LOWER_POWER_SIGNED;
    const bool power = answer == BUSTER_CHECK_LOWER_POWER_MASK;
    kind = power ? (kind == BUSTER_TYPED_DIV ? BUSTER_TYPED_SHR : BUSTER_TYPED_AND) : kind;
    constant = !increment                                    ? constant :
               calculated_class == BUSTER_TYPED_CLASS_F32 ? UINT32_C(0x3f800000) :
               calculated_class == BUSTER_TYPED_CLASS_F64 ? UINT64_C(0x3ff0000000000000) :
                                                            1;
    const uint8_t direct_store = postfix ? (local ? BUSTER_TYPED_SET_LOCAL : BUSTER_TYPED_SET_GLOBAL) : (local ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL);
    *update = (NodeMachineUpdate){
        .destination = local ? object : known ? program->object_symbols[object] : 0,
        .constant = constant,
        .stride = stride,
        .stored = stored,
        .calculated = calculated,
        .right = pointer ? BUSTER_TYPE_LONG : right,
        .stored_class = stored_class,
        .calculated_class = calculated_class,
        .computing = computing,
        .kind = kind,
        .get = local ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL,
        .store = named ? direct_store : postfix ? BUSTER_TYPED_SET : BUSTER_TYPED_STORE,
        .increment = increment,
        .postfix = postfix,
        .subtract = subtract,
        .pointer = pointer,
        .power = power,
        .direct = named,
    };
    return takes;
}

// C's classification of record `r` in its context: its template, with its evaluated operands given their contexts and
// targets (check_lower_template_classify's rules, from the records), or CHECK_LOWER_TEMPLATE_NONE. `object` is the
// object a root INITIALIZE initializes.
static CheckLowerTemplate node_machine_classify(NodeMachine* machine, uint32_t r, uint32_t object)
{
    const CheckProgram* program = machine->program;
    const TypeTable* types = machine->types;
    const CheckOp op = node_machine_record_op(machine, r);
    const uint8_t context = node_machine_record_context(machine, r);
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const uint32_t a = node_machine_record_left(machine, r);
    const uint32_t b = node_machine_record_right(machine, r);
    const TypeId type_a = node_machine_record_converted(machine, r, 0);
    const TypeId type_b = node_machine_record_converted(machine, r, 1);
    const uint8_t facts = node_machine_record_facts(machine, r);
    const bool aggregate = (facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0;
    const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = node_machine_record_class(machine, r);
    const bool constant = node_machine_record_constant_kind(machine, r) != CHECK_CONSTANT_NONE && node_machine_record_fault(machine, r) == CHECK_FAULT_NONE &&
                          (node_machine_record_flags(machine, r) & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    const uint32_t known_object = node_machine_record_object(machine, r);
    const bool known = known_object != 0 && known_object < program->object_count;
    const bool arithmetic = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT || op == CHECK_OP_SUBSCRIPT;
    const bool left_pointer = arithmetic && (node_machine_facts(machine, type_a) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool right_pointer = arithmetic && (node_machine_facts(machine, type_b) & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
    const bool pointers = left_pointer || right_pointer;
    const bool scalar_class = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64;
    CheckLowerTemplate kind = CHECK_LOWER_TEMPLATE_NONE;
    if (node_machine_record_overloaded(machine, r) || (facts & (BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)) != 0 ||
        (op == CHECK_OP_CAST && (node_machine_record_conversion_flags(machine, r, 0) & BUSTER_CHECK_CONVERT_TO_UNION) != 0))
    {
    }
    else if (op == CHECK_OP_VALUE && known && check_input_bit(program->input->declaration_bits, known_object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
             !check_input_bit(program->input->declaration_bits, known_object - 1, BUSTER_CHECK_BITS_STATIC))
    {
    }
    else if (!address && node_machine_record_category(machine, r) == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0)
    {
    }
    else if (constant && op != CHECK_OP_INITIALIZE &&
             ((node_machine_record_category(machine, r) == CHECK_RVALUE && !address) ||
              ((address || address_value) && node_machine_record_constant_kind(machine, r) == CHECK_CONSTANT_ADDRESS)))
    {
        kind = discard || node_machine_constant_takes(machine, r) ? CHECK_LOWER_TEMPLATE_CONSTANT : CHECK_LOWER_TEMPLATE_NONE;
    }
    else if (op == CHECK_OP_VALUE)
    {
        if (node_machine_record_type(machine, r) == BUSTER_TYPE_VOID)
        {
            kind = CHECK_LOWER_TEMPLATE_VOID;
        }
        else if (address || address_value)
        {
            kind = node_machine_address_takes(machine, known_object) ? CHECK_LOWER_TEMPLATE_ADDRESS : CHECK_LOWER_TEMPLATE_NONE;
        }
        else if (node_machine_record_category(machine, r) == CHECK_LVALUE && known && scalar_class && !aggregate)
        {
            kind = node_machine_local(machine, known_object) ? CHECK_LOWER_TEMPLATE_LOCAL : CHECK_LOWER_TEMPLATE_GLOBAL;
        }
    }
    else if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
    {
        kind = address ? CHECK_LOWER_TEMPLATE_NONE : node_machine_logical(machine, r, op, class);
    }
    else if (op == CHECK_OP_SELECT)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, address, 0);
    }
    else if (op == CHECK_OP_ADDRESS)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, true, 0);
    }
    else if (op == CHECK_OP_DEREFERENCE)
    {
        kind = aggregate || address || address_value || scalar_class ? CHECK_LOWER_TEMPLATE_DEREFERENCE : CHECK_LOWER_TEMPLATE_NONE;
        node_machine_evaluate(machine, a, false, type_a);
    }
    else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
    {
        TypeId record = type_strip_aligned(types, op == CHECK_OP_ARROW ? type_element(types, type_a) : node_machine_record_type(machine, a));
        uint8_t record_kind = type_kind(types, record);
        if (__builtin_expect(record_kind == BUSTER_TYPE_KIND_ATOMIC, 0))
        {
            record = type_strip_aligned(types, type_element(types, record));
            record_kind = type_kind(types, record);
        }
        const bool bit_field = (node_machine_record_flags(machine, r) & BUSTER_CHECK_BIT_FIELD) != 0;
        const bool loads = !address && !address_value;
        if ((record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && !bit_field && node_machine_record_member_offset(machine, r) % 8 == 0 &&
            (!loads || scalar_class))
        {
            kind = CHECK_LOWER_TEMPLATE_MEMBER;
            node_machine_evaluate(machine, a, op == CHECK_OP_MEMBER, type_a);
        }
    }
    else if (arithmetic && (node_machine_record_flags(machine, r) & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && left_pointer && !right_pointer &&
             check_action_pointer_stride(types, type_a) != 0 && (op != CHECK_OP_SUBSCRIPT || address || address_value || scalar_class))
    {
        kind = CHECK_LOWER_TEMPLATE_POINTER;
        node_machine_evaluate(machine, a, false, type_a);
        node_machine_evaluate(machine, b, false, BUSTER_TYPE_LONG);
    }
    else if (op == CHECK_OP_SUBSCRIPT || aggregate || !scalar_class)
    {
        if ((op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE) && class != UINT8_MAX && class < BUSTER_TYPED_CLASS_VECTOR)
        {
            kind = CHECK_LOWER_TEMPLATE_PASS;
            node_machine_evaluate(machine, a, false, node_machine_record_type(machine, r));
        }
    }
    else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
    {
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, false, node_machine_record_type(machine, r));
    }
    else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
    {
        kind = CHECK_LOWER_TEMPLATE_UNARY;
        node_machine_evaluate(machine, a, false, type_a);
    }
    else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
    {
        const uint8_t operand_class = node_machine_class(machine, type_a);
        if (!node_machine_aggregate(machine, type_a) && operand_class < BUSTER_TYPED_CLASS_VECTOR && operand_class != BUSTER_TYPED_CLASS_VOID)
        {
            kind = CHECK_LOWER_TEMPLATE_TRUTH;
            node_machine_evaluate(machine, a, false, type_a);
        }
    }
    else if (op == CHECK_OP_COMMA)
    {
        // The record's type, read before its operands are evaluated: after node_machine_evaluate, a call, its column's
        // pointer would be loaded again.
        const TypeId type = node_machine_record_type(machine, r);
        kind = CHECK_LOWER_TEMPLATE_PASS;
        node_machine_evaluate(machine, a, false, BUSTER_TYPE_VOID);
        node_machine_evaluate(machine, b, address, discard ? BUSTER_TYPE_VOID : type);
    }
    else if ((op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT) && (node_machine_record_flags(machine, r) & BUSTER_CHECK_VECTOR_ELEMENT) == 0 && !pointers)
    {
        const uint8_t class_a = node_machine_class(machine, type_a);
        const uint8_t class_b = node_machine_class(machine, type_b);
        if (class_a >= BUSTER_TYPED_CLASS_I8 && class_a <= BUSTER_TYPED_CLASS_F64 && class_b >= BUSTER_TYPED_CLASS_I8 && class_b <= BUSTER_TYPED_CLASS_F64)
        {
            kind = CHECK_LOWER_TEMPLATE_BINARY;
            node_machine_evaluate(machine, a, false, type_a);
            node_machine_evaluate(machine, b, false, type_b);
        }
    }
    else if (check_lower_binary(op) != BUSTER_TYPED_NONE && op < CHECK_OP_PRE_INCREMENT && !arithmetic)
    {
        const uint8_t class_a = node_machine_class(machine, type_a);
        const uint8_t class_b = node_machine_class(machine, type_b);
        uint64_t power_right = 0;
        const bool power = node_machine_power(machine, r, class, check_lower_binary(op), &power_right) != BUSTER_CHECK_LOWER_POWER_NONE;
        if (class_a >= BUSTER_TYPED_CLASS_I8 && class_a <= BUSTER_TYPED_CLASS_F64 && class_b >= BUSTER_TYPED_CLASS_I8 && class_b <= BUSTER_TYPED_CLASS_F64 &&
            !node_machine_aggregate(machine, type_a) && !power)
        {
            kind = CHECK_LOWER_TEMPLATE_BINARY;
            node_machine_evaluate(machine, a, false, type_a);
            node_machine_evaluate(machine, b, false, type_b);
        }
    }
    else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_INITIALIZE)
    {
        const uint32_t destination_facts = a < r ? node_machine_facts(machine, node_machine_record_type(machine, a)) : 0;
        const bool bit_field = op == CHECK_OP_ASSIGN && a < r && (node_machine_record_flags(machine, a) & BUSTER_CHECK_BIT_FIELD) != 0;
        if ((destination_facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 && !bit_field)
        {
            const uint32_t target = op == CHECK_OP_INITIALIZE ? object :
                                    a < r && node_machine_record_op(machine, a) == CHECK_OP_VALUE ? node_machine_record_object(machine, a) : 0;
            const bool named = target != 0 && target < program->object_count;
            if (named)
            {
                kind = node_machine_local(machine, target) ? CHECK_LOWER_TEMPLATE_STORE_LOCAL : CHECK_LOWER_TEMPLATE_STORE_GLOBAL;
                node_machine_evaluate(machine, op == CHECK_OP_INITIALIZE ? a : b, false, node_machine_record_type(machine, r));
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                // As the comma's: the record's type before its operands are evaluated.
                const TypeId type = node_machine_record_type(machine, r);
                kind = CHECK_LOWER_TEMPLATE_STORE;
                node_machine_evaluate(machine, a, true, 0);
                node_machine_evaluate(machine, b, false, type);
            }
        }
    }
    else if (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN)
    {
        // An update's first row follows its destination's entries: a direct one's destination isn't evaluated, and its
        // first row reads it by name.
        const uint32_t k = machine->update_count;
        NodeMachineUpdate* update = machine->updates + k;
        if (!address && k < BUSTER_NODE_MACHINE_MOST / 2 && node_machine_update_plan(machine, r, update))
        {
            kind = BUSTER_NODE_MACHINE_TEMPLATE_UPDATE;
            machine->update_count = k + 1;
            if (!update->direct)
            {
                node_machine_evaluate(machine, a, true, 0);
            }
            if (!update->increment && !update->power)
            {
                node_machine_evaluate(machine, b, false, update->right);
            }
            node_machine_record_set_update(machine, r, a, k);
        }
    }
    return kind;
}

// C's classification of call record `r`: the lowering's plain call (check_lower_call_body's `plain`: a result of a plain
// scalar or void, parameters and arguments of plain scalars, none a transparent union's), evaluated for its value. Its
// callee is then evaluated for its value, without a conversion, where the call is through a pointer, and each argument to
// its conversion's type, as the call's tasks push them. BUSTER_NODE_MACHINE_TEMPLATE_CALL, or CHECK_LOWER_TEMPLATE_NONE
// for any other call: a struct's, a union's, a long double's, a 128-bit integer's, a half-precision or an atomic value's
// argument, parameter or result, which the lowering passes otherwise.
static CheckLowerTemplate node_machine_classify_call(NodeMachine* machine, uint32_t r)
{
    const TypeTable* types = machine->types;
    const uint32_t a = node_machine_record_left(machine, r);
    const TypeId function = node_machine_callee_function(machine, a);
    const uint32_t count = node_machine_record_argument_count(machine, r);
    bool plain = (node_machine_record_context(machine, r) & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) == 0 && function != 0 &&
                 check_lower_plain_scalar(node_machine_word(machine, node_machine_record_type(machine, r)), true);
    uint32_t parameters = 0;
    const TypeId* parameter_types = plain ? type_parameters(types, function, &parameters) : NULL;
    for (uint32_t i = 0; plain && i < parameters; i += 1)
    {
        plain = check_lower_plain_scalar(node_machine_word(machine, parameter_types[i]), false);
    }
    for (uint32_t i = 0; plain && i < count; i += 1)
    {
        plain = check_lower_plain_scalar(node_machine_word(machine, node_machine_record_argument_converted(machine, r, i)), false) &&
                (node_machine_record_argument_conversion_flags(machine, r, i) & BUSTER_CHECK_CONVERT_TRANSPARENT_UNION) == 0;
    }
    if (plain && node_machine_direct_callee(machine, a) == 0)
    {
        node_machine_evaluate(machine, a, false, 0);
    }
    for (uint32_t i = 0; plain && i < count; i += 1)
    {
        node_machine_evaluate(machine, node_machine_record_argument(machine, r, i), false, node_machine_record_argument_converted(machine, r, i));
    }
    return plain ? BUSTER_NODE_MACHINE_TEMPLATE_CALL : CHECK_LOWER_TEMPLATE_NONE;
}

// The labels' numbers (NodeMachine.label_bases), each labeled record's first, in the order the tasks number them: the
// order they pop the records, a preorder from the root with each record's operands in the order of their entries, which
// is the records' (X's postorder). A labeled record's rank in it among the labeled records is the number of those that are
// its ancestors (whose subtree, from its first record to it, holds it) and of those before its subtree's first record (to
// its left). A subtree's first record is its least (node_machine_record_lowest, T's, a call's arguments included). Only
// an expression with labels runs it, and it reads the labeled records alone (C listed them in `stack`, their subtrees'
// first records after them): a walk of every record cost C 125 cycles a new taken node on sqlite3.c on the 9700X, three
// times its average, by its branches. Scalar: an expression has a few, and their order is the tree's.
static __attribute__((noinline)) void node_machine_number_labels(NodeMachine* machine)
{
    const uint32_t labeled = machine->label_count / 2;
    uint32_t* records = machine->stack;
    uint32_t* firsts = machine->stack + labeled;
    for (uint32_t i = 0; i < labeled; i += 1)
    {
        firsts[i] = node_machine_record_lowest(machine, records[i]);
    }
    for (uint32_t i = 0; i < labeled; i += 1)
    {
        const uint32_t r = records[i];
        uint32_t rank = 0;
        for (uint32_t j = 0; j < labeled; j += 1)
        {
            const uint32_t other = records[j];
            rank += (other > r && firsts[j] <= r) || other < firsts[i];
        }
        node_machine_record_set_label_base(machine, r, 2 * rank);
    }
}

// C: every evaluated record's template and its operands' contexts and targets, down from the root, which its consumer
// evaluates with `context` and converts to `target`; `object` is what a root INITIALIZE initializes. Then the labels'
// numbers, where a record numbers any.
static NodeMachineRefusal node_machine_contexts(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    node_machine_records_set_unevaluated(machine, count);
    machine->label_count = 0;
    machine->update_count = 0;
    const uint32_t root = count - 1;
    node_machine_record_set_evaluated(machine, root);
    node_machine_record_set_context(machine, root, context);
    node_machine_record_set_target(machine, root, target);
    node_machine_record_set_call_depth(machine, root, 0);
    uint32_t r = count;
    while (refusal == NODE_MACHINE_TAKEN && r != 0)
    {
        r -= 1;
        if (node_machine_record_evaluated(machine, r))
        {
            machine->classified = r;
            const CheckLowerTemplate kind = node_machine_record_op(machine, r) == CHECK_OP_CALL ? node_machine_classify_call(machine, r) :
                                                                                                   node_machine_classify(machine, r, r == root ? object : 0);
            node_machine_record_set_template(machine, r, kind);
            refusal = kind == CHECK_LOWER_TEMPLATE_NONE ? NODE_MACHINE_REFUSED_TEMPLATE : NODE_MACHINE_TAKEN;
            machine->stopped = r;
        }
    }
    if (__builtin_expect(refusal == NODE_MACHINE_TAKEN && machine->label_count != 0, 0))
    {
        node_machine_number_labels(machine);
    }
    return refusal;
}

#endif
