// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine's X sweep (c_check_node_machine.c has the machine): node_machine_expand walks the records up the
// postorder and writes each evaluated one's row to the tape, its own entries by its template (node_machine_own,
// check_lower_template_own's, and the logical operators' and the conditional's) and then its conversion's to its target
// in its context (node_machine_conversion, check_lower_template_conversion's), then its consumer's segment after it
// where it has one (node_machine_hook) or an update's first row (node_machine_split, node_machine_update_first), both
// behind one test of its state (node_machine_after_row); an update's second row, its own, by node_machine_update_last;
// a call's CALL by node_machine_call_own, its row numbered as the lowering numbers it. It reads the records T typed and
// C classified through the record accessors alone; the tape (NodeMachine's kinds, classes and tape), its labels'
// entries (label_entries) and the call rows (calls_at, call_records, call_count) are its own.
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

static void node_machine_update_last(NodeMachine* machine, const NodeMachineUpdate* update);

// Record `r`'s own entries by its template (check_lower_template_own's), after its operands', or an update's second row.
// `object` as in C.
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
    else if (kind == BUSTER_NODE_MACHINE_TEMPLATE_UPDATE)
    {
        node_machine_update_last(machine, node_machine_record_update(machine, r));
    }
}

// The entries of a conversion from `from` (its facts `from_facts`, its class on the stack `source`) to `to`
// (check_lower_template_conversion's, which are check_lower_conversion's for the classes it takes): whether the templates
// take it. Inlined into X's loop: the updates' rows, its other callers, would leave it a call of its own at every node.
static inline __attribute__((always_inline)) bool node_machine_convert(NodeMachine* machine, TypeId from, uint8_t from_facts, uint8_t source, TypeId to)
{
    bool takes = true;
    if (to != 0 && from != to)
    {
        const uint8_t to_facts = node_machine_facts(machine, to);
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

// Record `r`'s conversion's entries, from its type to its target in its context: whether the templates take it.
static inline __attribute__((always_inline)) bool node_machine_conversion(NodeMachine* machine, uint32_t r)
{
    return node_machine_record_context(machine, r) != 0 ||
           node_machine_convert(machine, node_machine_record_type(machine, r), node_machine_record_facts(machine, r), node_machine_record_class(machine, r),
                                node_machine_record_target(machine, r));
}


// An update's first row, after its destination's entries (check_lower_update's up to operand 1's): the old value read by
// name, or through the address its destination left (kept for the store, or for a postfix one in a temporary, with the
// old value in another), and converted to its computation's type and class. Whether the templates take the conversion.
static __attribute__((noinline)) bool node_machine_update_first(NodeMachine* machine, const NodeMachineUpdate* update)
{
    const uint8_t class = update->stored_class;
    const uint64_t access = check_lower_access_value(machine->types, class, update->stored);
    if (update->direct)
    {
        node_machine_put(machine, update->get, class, update->destination);
        if (update->postfix)
        {
            node_machine_put(machine, BUSTER_TYPED_DUP, class, 0);
        }
    }
    else if (!update->postfix)
    {
        node_machine_put(machine, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
        node_machine_put(machine, BUSTER_TYPED_LOAD, class, access);
    }
    else
    {
        const uint64_t address = BUSTER_NODE_MACHINE_TEMPORARY | machine->temporary_count;
        const uint64_t old = BUSTER_NODE_MACHINE_TEMPORARY | (machine->temporary_count + 1);
        machine->temporary_count += 2;
        node_machine_put(machine, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        node_machine_put(machine, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        node_machine_put(machine, BUSTER_TYPED_LOAD, class, access);
        node_machine_put(machine, BUSTER_TYPED_STORE_LOCAL, class, old);
        node_machine_put(machine, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        node_machine_put(machine, BUSTER_TYPED_LOCAL, class, old);
    }
    const bool takes = node_machine_convert(machine, update->stored, node_machine_facts(machine, update->stored), class, update->calculated);
    if (update->computing != update->calculated_class && !update->pointer)
    {
        node_machine_put(machine, BUSTER_TYPED_CONVERT, update->computing, 0);
    }
    return takes;
}

// An update's own row, after operand 1's entries (none for an increment or a power of two's shift or mask): the
// operation, the result converted back to the destination's type, and the store, by name or through the kept address.
// The conversion takes, as the plan's classes are I8 to F64 and neither complex nor an aggregate.
static __attribute__((noinline)) void node_machine_update_last(NodeMachine* machine, const NodeMachineUpdate* update)
{
    const uint8_t class = update->stored_class;
    if (update->pointer)
    {
        if (update->increment)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
        }
        if (update->subtract)
        {
            node_machine_put(machine, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        node_machine_put(machine, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, update->stride);
    }
    else
    {
        if (update->increment || update->power)
        {
            node_machine_put(machine, BUSTER_TYPED_CONST, update->computing, update->constant);
        }
        else if (update->computing != update->calculated_class && update->kind != BUSTER_TYPED_SHL && update->kind != BUSTER_TYPED_SHR)
        {
            node_machine_put(machine, BUSTER_TYPED_CONVERT, update->computing, 0);
        }
        node_machine_put(machine, update->kind, update->computing, 0);
    }
    node_machine_convert(machine, update->calculated, node_machine_facts(machine, update->calculated), update->calculated_class, update->stored);
    node_machine_put(machine, update->store, class, update->direct ? update->destination : check_lower_access_value(machine->types, class, update->stored));
}


// X's update's first row after record `r`, its destination's own (none for one read by name): at most
// BUSTER_NODE_MACHINE_ROW_MOST entries.
static NodeMachineRefusal node_machine_split(NodeMachine* machine, uint32_t r)
{
    const uint32_t before = machine->entry_count;
    const bool takes = node_machine_update_first(machine, node_machine_record_update(machine, r));
    return !takes || machine->entry_count - before > BUSTER_NODE_MACHINE_ROW_MOST ? NODE_MACHINE_REFUSED_ROW : NODE_MACHINE_TAKEN;
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

// Call record `r`'s own entry, after its callee's and its arguments': a CALL of its result's class, whose value is its row
// relative to the expression's. The lowering numbers a call's row when its task pops, before its operands' tasks (the
// tasks' preorder), where X writes in postorder: its row is the evaluated calls before its subtree, every one written
// before its least record, and those that contain it, its call depth (C's).
static void node_machine_call_own(NodeMachine* machine, uint32_t r)
{
    const uint32_t row = machine->calls_at[node_machine_record_lowest(machine, r)] + node_machine_record_call_depth(machine, r);
    machine->call_records[row] = r;
    machine->call_count += 1;
    node_machine_put(machine, BUSTER_TYPED_CALL, node_machine_record_class(machine, r), row);
}

// What follows record `r`'s row, where its state `state` has more than BUSTER_NODE_MACHINE_EVALUATED: its consumer's
// segment (node_machine_hook), or else an update's first row (node_machine_split), which is all an update's destination
// read by name writes, since it isn't evaluated.
static NodeMachineRefusal node_machine_after_row(NodeMachine* machine, uint32_t r, uint8_t state)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    if ((state & (BUSTER_NODE_MACHINE_AFTER_CONDITION | BUSTER_NODE_MACHINE_AFTER_ARM)) != 0)
    {
        node_machine_hook(machine, r, state);
    }
    else
    {
        refusal = node_machine_split(machine, r);
    }
    return refusal;
}

// X: each evaluated record's row, its own entries and then its conversion's, up the postorder into the tape, after it its
// consumer's segment where it has one or an update's first row (node_machine_after_row), and each call's row
// (NodeMachine.call_records). `object` as in C. The tape holds every row and segment an expression can keep
// (BUSTER_NODE_MACHINE_TAPE_MOST), so no record tests the room left. A record tests its state twice, as before the
// updates: whether it's evaluated, then whether anything follows (a state past BUSTER_NODE_MACHINE_EVALUATED, which an
// update's destination read by name has too).
static NodeMachineRefusal node_machine_expand(NodeMachine* machine, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    machine->entry_count = 0;
    machine->call_count = 0;
    machine->temporary_count = 0;
    uint32_t r = 0;
    while (refusal == NODE_MACHINE_TAKEN && r < count)
    {
        machine->calls_at[r] = machine->call_count;
        const uint8_t state = node_machine_record_state(machine, r);
        if ((state & BUSTER_NODE_MACHINE_EVALUATED) != 0)
        {
            const uint32_t before = machine->entry_count;
            if (node_machine_record_template(machine, r) == BUSTER_NODE_MACHINE_TEMPLATE_CALL)
            {
                node_machine_call_own(machine, r);
            }
            else
            {
                node_machine_own(machine, r, r == count - 1 ? object : 0);
            }
            const bool takes = node_machine_conversion(machine, r);
            refusal = !takes || machine->entry_count - before > BUSTER_NODE_MACHINE_ROW_MOST ? NODE_MACHINE_REFUSED_ROW : NODE_MACHINE_TAKEN;
        }
        if (refusal == NODE_MACHINE_TAKEN && state > BUSTER_NODE_MACHINE_EVALUATED)
        {
            refusal = node_machine_after_row(machine, r, state);
        }
        r += 1;
    }
    // The record X stopped at: the one whose row refused, or else the last, the root, which is evaluated.
    machine->stopped = r - 1;
    return refusal;
}


#endif
