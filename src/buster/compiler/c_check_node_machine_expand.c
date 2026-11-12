// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine's X sweep (c_check_node_machine.c has the machine): node_machine_expand walks the records up
// the postorder and writes each evaluated one's row to the tape, its own entries by its template (node_machine_own,
// check_lower_template_own's, and the logical operators' and the conditional's) and then its conversion's to its target
// in its context (node_machine_conversion, check_lower_template_conversion's), then its consumer's segment after it where
// it has one (node_machine_hook). It reads the records T typed and C classified through the record accessors alone; the
// tape (NodeMachine's kinds, classes and tape) and its labels' entries (label_entries) are its own.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_EXPAND_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_EXPAND_INCLUDED

static inline __attribute__((always_inline)) void node_machine_put(NodeMachine* machine, uint8_t kind, uint8_t class, uint64_t value)
{
    const uint32_t at = machine->entry_count;
    machine->kinds[at] = kind;
    machine->classes[at] = class;
    machine->tape[at] = value;
    machine->entry_count = at + 1;
}

// A test of a value of class `class` (check_lower_truth's entries): a narrow class's value converted to I32, then compared
// with 0 by NE, or by EQ where `negate`.
static void node_machine_put_truth(NodeMachine* machine, uint8_t class, bool negate)
{
    const bool narrow = check_lower_narrow(class);
    if (narrow)
    {
        node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
    }
    node_machine_put(machine, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
    node_machine_put(machine, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
}

// An object's address's entries (check_lower_template_address): a local's (its object, which the splice numbers) or a
// global's symbol, then its offset; a constant's alone.
static void node_machine_address(NodeMachine* machine, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else
    {
        if (node_machine_local(machine, object))
        {
            node_machine_put(machine, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        else
        {
            node_machine_put(machine, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, machine->program->object_symbols[object]);
        }
        if (offset != 0)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
            node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
    }
}

// Record `r`'s own entries by its template (check_lower_template_own's), after its operands'. `object` as in C.
static void node_machine_own(NodeMachine* machine, uint32_t r, uint32_t object)
{
    const CheckProgram* program = machine->program;
    const TypeTable* types = machine->types;
    const CheckOp op = node_machine_record_op(machine, r);
    const CheckLowerTemplate kind = node_machine_record_template(machine, r);
    const uint8_t context = node_machine_record_context(machine, r);
    const bool address = (context & BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS) != 0;
    const bool discard = (context & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) != 0;
    const bool address_value = (node_machine_record_facts(machine, r) & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
    const uint8_t class = node_machine_record_class(machine, r);
    if (kind == CHECK_LOWER_TEMPLATE_CONSTANT && !discard)
    {
        if (node_machine_record_constant_kind(machine, r) == CHECK_CONSTANT_ADDRESS)
        {
            node_machine_address(machine, node_machine_record_object(machine, r), node_machine_record_offset(machine, r));
            if (node_machine_record_category(machine, r) == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
            {
                node_machine_put(machine, BUSTER_TYPED_CONVERT, class, 0);
            }
        }
        else if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
        {
            uint64_t bits = node_machine_record_integer(machine, r);
            const uint32_t width = 8u << ((class - 1) / 2);
            if (width < 64)
            {
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
            node_machine_put(machine, BUSTER_TYPED_CONST, class, bits);
        }
        else
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, class, check_lower_float_bits(NULL, class, node_machine_record_floating(machine, r)));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_ADDRESS)
    {
        node_machine_address(machine, node_machine_record_object(machine, r), node_machine_record_offset(machine, r));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_LOCAL)
    {
        node_machine_put(machine, BUSTER_TYPED_LOCAL, class, node_machine_record_object(machine, r));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_GLOBAL)
    {
        node_machine_put(machine, BUSTER_TYPED_GLOBAL, class, program->object_symbols[node_machine_record_object(machine, r)]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_DEREFERENCE && !address && !address_value)
    {
        node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, node_machine_record_type(machine, r)));
    }
    else if (kind == CHECK_LOWER_TEMPLATE_MEMBER)
    {
        const uint64_t offset = node_machine_record_member_offset(machine, r);
        if (offset / 8 != 0)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
        }
        if (!address && !address_value)
        {
            node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, node_machine_record_type(machine, r)));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_UNARY)
    {
        node_machine_put(machine, op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_TRUTH)
    {
        node_machine_put_truth(machine, node_machine_class(machine, node_machine_record_converted(machine, r, 0)), op == CHECK_OP_NOT);
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_BRANCH)
    {
        // After its second operand's or second arm's row: a logical operator's test of it, its jump to its end, its
        // alternate and its value there, 0 for `&&` and 1 for `||`; then its end, which a conditional's first arm jumps to.
        const uint32_t alternate = node_machine_record_label_base(machine, r);
        if (op != CHECK_OP_CONDITIONAL)
        {
            node_machine_put_truth(machine, node_machine_class(machine, node_machine_record_converted(machine, r, 1)), false);
            node_machine_put(machine, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
            machine->label_entries[alternate] = machine->entry_count;
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
        }
        machine->label_entries[alternate + 1] = machine->entry_count;
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_DECIDED)
    {
        node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
    }
    else if (kind == (CheckLowerTemplate)NODE_MACHINE_TEMPLATE_SECOND)
    {
        node_machine_put_truth(machine, node_machine_class(machine, node_machine_record_converted(machine, r, 1)), false);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_BINARY)
    {
        node_machine_put(machine, check_lower_binary(op), class, 0);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_POINTER)
    {
        if (op == CHECK_OP_SUBTRACT)
        {
            node_machine_put(machine, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, check_action_pointer_stride(types, node_machine_record_converted(machine, r, 0)));
        if (op == CHECK_OP_SUBSCRIPT && !address && !address_value)
        {
            node_machine_put(machine, BUSTER_TYPED_LOAD, class, check_lower_access_value(types, class, node_machine_record_type(machine, r)));
        }
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL || kind == CHECK_LOWER_TEMPLATE_STORE_GLOBAL)
    {
        const uint32_t a = node_machine_record_left(machine, r);
        const uint32_t target = op == CHECK_OP_INITIALIZE ? object : node_machine_record_object(machine, a);
        node_machine_put(machine, kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                         kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? target : program->object_symbols[target]);
    }
    else if (kind == CHECK_LOWER_TEMPLATE_STORE)
    {
        node_machine_put(machine, BUSTER_TYPED_STORE, class, check_lower_access_value(types, class, node_machine_record_type(machine, r)));
    }
}

// Record `r`'s conversion's entries, from its type to its target in its context (check_lower_template_conversion's):
// whether the templates take it.
static bool node_machine_conversion(NodeMachine* machine, uint32_t r)
{
    const TypeId from = node_machine_record_type(machine, r);
    const TypeId to = node_machine_record_target(machine, r);
    bool takes = true;
    if (node_machine_record_context(machine, r) == 0 && to != 0 && from != to)
    {
        const uint8_t from_facts = node_machine_record_facts(machine, r);
        const uint8_t to_facts = node_machine_facts(machine, to);
        const uint8_t source = node_machine_record_class(machine, r);
        const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : node_machine_class(machine, to);
        // check_lower_conversion_empty: the same class, neither a _BitInt nor complex, nor a conversion to bool.
        const bool empty = source == target && source < BUSTER_TYPED_CLASSES &&
                           ((from_facts ^ to_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0 &&
                           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 &&
                           (to_facts & (BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
        const bool mixed = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        const bool complex = ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) != 0 && target != BUSTER_TYPED_CLASS_VOID;
        if (empty)
        {
        }
        else if (complex || mixed || source >= BUSTER_TYPED_CLASS_F80 || target >= BUSTER_TYPED_CLASS_F80)
        {
            takes = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            if (source != BUSTER_TYPED_CLASS_VOID)
            {
                node_machine_put(machine, BUSTER_TYPED_DISCARD, source, 0);
            }
        }
        else if ((to_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            const bool narrow = check_lower_narrow(source);
            if (narrow)
            {
                node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
            node_machine_put(machine, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : source, 0);
            node_machine_put(machine, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            node_machine_put(machine, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
        }
        else if (source != target)
        {
            node_machine_put(machine, BUSTER_TYPED_CONVERT, target, 0);
        }
    }
    return takes;
}

// The segment of record `r`'s consumer after `r`'s row (`state`, its state, says which): a logical operator's or a
// conditional's test of its condition and branch to its alternate, past its second operand or first arm (a `||`'s where
// the condition holds, the others' where it doesn't); or a conditional's jump from its first arm to its end, then its
// alternate, where its second arm starts.
static void node_machine_hook(NodeMachine* machine, uint32_t r, uint8_t state)
{
    const uint32_t consumer = node_machine_record_hook(machine, r);
    const uint32_t alternate = node_machine_record_label_base(machine, consumer);
    if ((state & BUSTER_NODE_MACHINE_AFTER_CONDITION) != 0)
    {
        node_machine_put_truth(machine, node_machine_class(machine, node_machine_record_converted(machine, consumer, 0)), false);
        node_machine_put(machine, node_machine_record_op(machine, consumer) == CHECK_OP_LOGICAL_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE,
                         BUSTER_TYPED_CLASS_I32, alternate);
    }
    else
    {
        const uint8_t class = node_machine_record_class(machine, consumer);
        node_machine_put(machine, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
        machine->label_entries[alternate] = machine->entry_count;
    }
}

// X: each evaluated record's row, its own entries and then its conversion's, up the postorder into the tape, and after it
// its consumer's segment where it has one (node_machine_hook). `object` as in C.
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    machine->entry_count = 0;
    uint32_t r = 0;
    while (refusal == NODE_MACHINE_TAKEN && r < count)
    {
        const uint8_t state = node_machine_record_state(machine, r);
        if ((state & BUSTER_NODE_MACHINE_EVALUATED) != 0)
        {
            const uint32_t before = machine->entry_count;
            node_machine_own(machine, r, r == count - 1 ? object : 0);
            const bool takes = node_machine_conversion(machine, r);
            refusal = !takes || machine->entry_count - before > BUSTER_NODE_MACHINE_ROW_MOST ? NODE_MACHINE_REFUSED_ROW : NODE_MACHINE_TAKEN;
            if (refusal == NODE_MACHINE_TAKEN && (state & (BUSTER_NODE_MACHINE_AFTER_CONDITION | BUSTER_NODE_MACHINE_AFTER_ARM)) != 0)
            {
                node_machine_hook(machine, r, state);
            }
            machine->stopped = r;
        }
        r += refusal == NODE_MACHINE_TAKEN;
    }
    return refusal;
}


#endif
