// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2524's node machine's C and X as one sweep (c_check_node_machine.c has the machine; docs/node-machine.md, "C and X
// as one sweep"): node_machine_lower walks the records down from the root as C does, and writes each evaluated record's
// row as soon as it has classified it, since the record's context and target, which its consumer gave it, are all its row
// waits for. The records come in reverse postorder, so the tape is written from its end: a record's row lies after its
// operands' entries, which are written after it. Within a record, what is last on the tape is written first: the
// segment after its row (its consumer's test and branch, or an update's first row), then its conversion's entries, then
// its own, each entry of a row from the row's last to its first.
//
// - The classification is C's own (node_machine_classify, _call, node_machine_logical, node_machine_classify_update),
//   and so are the operands' contexts and targets.
// - The rows are X's (node_machine_own, node_machine_convert, node_machine_hook, node_machine_update_first and _last),
//   each written from its last entry.
// - What X numbers by the order it writes in, the sweep numbers after its loop, for the few expressions that have any:
//   the labels (C's node_machine_number_labels ranks the labeled records once C has met them all; the rows hold each
//   label's place among the labeled records in the order the sweep met them until then), the calls' rows (X's rule,
//   node_machine_call_own's, over the calls in the order X would have written them) and the temporaries (X's order is the
//   reverse of this sweep's).
//
// node_machine_contexts and node_machine_expand are its exact twin: the shadow's --fused runs both on every expression
// and compares the two tapes, their labels and their calls (c_check_node_census.c).
#ifndef BUSTER_C_CHECK_NODE_MACHINE_LOWER_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_LOWER_INCLUDED

// The tape's three columns' ends, which the sweep writes back from.
typedef struct NodeMachineBack NodeMachineBack;
struct NodeMachineBack
{
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
};

// One more entry before the `n` written: the count with it.
static inline __attribute__((always_inline)) uint32_t node_machine_back(NodeMachineBack out, uint32_t n, uint8_t kind, uint8_t class, uint64_t value)
{
    const int64_t at = -(int64_t)n - 1;
    out.kinds[at] = kind;
    out.classes[at] = class;
    out.tape[at] = value;
    return n + 1;
}

// node_machine_put_truth's entries, from the last.
static inline __attribute__((always_inline)) uint32_t node_machine_back_truth(NodeMachineBack out, uint32_t n, uint8_t class, bool negate)
{
    const bool narrow = check_lower_narrow(class);
    n = node_machine_back(out, n, negate ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
    n = node_machine_back(out, n, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : class, 0);
    if (narrow)
    {
        n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
    }
    return n;
}

// node_machine_address's entries, from the last.
static uint32_t node_machine_back_address(const NodeMachine* machine, NodeMachineBack out, uint32_t n, uint32_t object, int64_t offset)
{
    if (object == 0)
    {
        n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)offset);
    }
    else
    {
        if (offset != 0)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, (uint64_t)offset);
        }
        if (node_machine_local(machine, object))
        {
            n = node_machine_back(out, n, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, object);
        }
        else
        {
            n = node_machine_back(out, n, BUSTER_TYPED_GLOBAL_ADDRESS, BUSTER_TYPED_CLASS_U64, machine->object_symbols[object]);
        }
    }
    return n;
}

// node_machine_convert's entries, from the last: into `takes` whether the templates take the conversion.
static inline __attribute__((always_inline)) uint32_t node_machine_back_convert(const NodeMachine* machine, NodeMachineBack out, uint32_t n, TypeId from, uint8_t from_facts,
                                                                              uint8_t source, TypeId to, bool* takes)
{
    *takes = true;
    if (to != 0 && from != to)
    {
        const uint8_t to_facts = node_machine_facts(machine, to);
        const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : node_machine_class(machine, to);
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
            *takes = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            if (source != BUSTER_TYPED_CLASS_VOID)
            {
                n = node_machine_back(out, n, BUSTER_TYPED_DISCARD, source, 0);
            }
        }
        else if ((to_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            const bool narrow = check_lower_narrow(source);
            n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
            n = node_machine_back(out, n, BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32, 0);
            n = node_machine_back(out, n, BUSTER_TYPED_CONST, narrow ? BUSTER_TYPED_CLASS_I32 : source, 0);
            if (narrow)
            {
                n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
        }
        else if (source != target)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, target, 0);
        }
    }
    return n;
}

// Whether record `r`'s conversion folds into its integer constant's one CONST of class `*class` and bits `*bits`
// (node_machine_fold_conversion's rule), which then become the conversion's.
static __attribute__((noinline)) bool node_machine_back_fold(const NodeMachine* machine, uint32_t r, uint8_t* class, uint64_t* bits)
{
    const TypeId from = node_machine_record_type(machine, r);
    const TypeId to = node_machine_record_target(machine, r);
    const uint8_t source = node_machine_record_class(machine, r);
    bool folded = false;
    if (node_machine_record_context(machine, r) == 0 && to != 0 && from != to && *class == source)
    {
        const uint8_t from_facts = node_machine_record_facts(machine, r);
        const uint8_t to_facts = node_machine_facts(machine, to);
        const uint8_t target = (to_facts & BUSTER_CHECK_LOWER_TYPE_AGGREGATE) != 0 ? BUSTER_TYPED_CLASS_U64 : node_machine_class(machine, to);
        const bool empty = source == target && source < BUSTER_TYPED_CLASSES &&
                           ((from_facts ^ to_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0 &&
                           ((from_facts | to_facts) & BUSTER_CHECK_LOWER_TYPE_COMPLEX) == 0 &&
                           (to_facts & (BUSTER_CHECK_LOWER_TYPE_BOOL | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
        uint8_t converted_class = 0;
        uint64_t converted_bits = *bits;
        folded = !empty && check_lower_constant_conversion(source, from_facts, target, to_facts, &converted_class, &converted_bits);
        if (folded)
        {
            *class = converted_class;
            *bits = converted_bits;
        }
    }
    return folded;
}

// node_machine_update_first's entries, from the last: an update's first row, after its destination's entries. Its
// temporaries take their numbers in the order this sweep meets them (node_machine_lower numbers them as X does after
// its loop). Into `takes` whether the templates take its conversion.
static __attribute__((noinline)) uint32_t node_machine_back_update_first(NodeMachine* machine, NodeMachineBack out, uint32_t n, const NodeMachineUpdate* update, bool* takes)
{
    const uint8_t class = update->stored_class;
    const uint64_t access = check_lower_access_value(machine->types, class, update->stored);
    if (update->computing != update->calculated_class && !update->pointer)
    {
        n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, update->computing, 0);
    }
    n = node_machine_back_convert(machine, out, n, update->stored, node_machine_facts(machine, update->stored), class, update->calculated, takes);
    if (update->direct)
    {
        if (update->postfix)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_DUP, class, 0);
        }
        n = node_machine_back(out, n, update->get, class, update->destination);
    }
    else if (!update->postfix)
    {
        n = node_machine_back(out, n, BUSTER_TYPED_LOAD, class, access);
        n = node_machine_back(out, n, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64, 0);
    }
    else
    {
        const uint64_t address = BUSTER_NODE_MACHINE_TEMPORARY | machine->temporary_count;
        const uint64_t old = BUSTER_NODE_MACHINE_TEMPORARY | (machine->temporary_count + 1);
        machine->temporary_count += 2;
        n = node_machine_back(out, n, BUSTER_TYPED_LOCAL, class, old);
        n = node_machine_back(out, n, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        n = node_machine_back(out, n, BUSTER_TYPED_STORE_LOCAL, class, old);
        n = node_machine_back(out, n, BUSTER_TYPED_LOAD, class, access);
        n = node_machine_back(out, n, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, address);
        n = node_machine_back(out, n, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64, address);
    }
    return n;
}

// node_machine_update_last's entries, from the last: an update's own row.
static __attribute__((noinline)) uint32_t node_machine_back_update_last(NodeMachine* machine, NodeMachineBack out, uint32_t n, const NodeMachineUpdate* update)
{
    const uint8_t class = update->stored_class;
    bool takes = true;
    n = node_machine_back(out, n, update->store, class, update->direct ? update->destination : check_lower_access_value(machine->types, class, update->stored));
    n = node_machine_back_convert(machine, out, n, update->calculated, node_machine_facts(machine, update->calculated), update->calculated_class, update->stored, &takes);
    if (update->pointer)
    {
        n = node_machine_back(out, n, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, update->stride);
        if (update->subtract)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
        }
        if (update->increment)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 1);
        }
    }
    else
    {
        n = node_machine_back(out, n, update->kind, update->computing, 0);
        if (update->increment || update->power)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_CONST, update->computing, update->constant);
        }
        else if (update->computing != update->calculated_class && update->kind != BUSTER_TYPED_SHL && update->kind != BUSTER_TYPED_SHR)
        {
            n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, update->computing, 0);
        }
    }
    return n;
}

// What follows record `r`'s row (node_machine_after_row's, `state` its state past BUSTER_NODE_MACHINE_EVALUATED), from
// the last entry: its consumer's segment (node_machine_hook's), a tested value's test and branch
// (node_machine_branch_hook's) or an update's first row (node_machine_split's). A label's
// entry is kept as the entries written when the sweep reached it (`ends`, by the label's number in the sweep's order),
// which is its place from the tape's end. Into `takes` whether an update's first row is a row the machine takes.
static __attribute__((noinline)) uint32_t node_machine_back_after_row(NodeMachine* machine, NodeMachineBack out, uint32_t n, uint32_t r, uint8_t state, uint32_t* ends, bool* takes)
{
    *takes = true;
    if ((state & (BUSTER_NODE_MACHINE_AFTER_CONDITION | BUSTER_NODE_MACHINE_AFTER_ARM)) != 0)
    {
        const uint32_t consumer = node_machine_record_hook(machine, r);
        const uint32_t alternate = node_machine_record_label_base(machine, consumer);
        if ((state & BUSTER_NODE_MACHINE_AFTER_CONDITION) != 0)
        {
            n = node_machine_back(out, n, node_machine_record_op(machine, consumer) == CHECK_OP_LOGICAL_OR ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE,
                                  BUSTER_TYPED_CLASS_I32, alternate);
            n = node_machine_back_truth(out, n, node_machine_class(machine, node_machine_record_converted(machine, consumer, 0)), false);
        }
        else
        {
            const uint8_t class = node_machine_record_class(machine, consumer);
            ends[alternate] = n;
            n = node_machine_back(out, n, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
        }
    }
    else if ((state & BUSTER_NODE_MACHINE_AFTER_BRANCH) != 0)
    {
        // node_machine_branch_hook's: the branch, to its hook's label as the sweep knows it until the labels are numbered
        // (node_machine_lower_number), then the test.
        n = node_machine_back(out, n, (state & BUSTER_NODE_MACHINE_BRANCH_TRUE) != 0 ? BUSTER_TYPED_BRANCH_TRUE : BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32,
                              node_machine_branch_label(machine, node_machine_record_hook(machine, r)));
        n = node_machine_back_truth(out, n, node_machine_class(machine, node_machine_record_target(machine, r)), false);
    }
    else if ((state & BUSTER_NODE_MACHINE_SPLIT) != 0)
    {
        const uint32_t before = n;
        n = node_machine_back_update_first(machine, out, n, node_machine_record_update(machine, r), takes);
        *takes = *takes && n - before <= BUSTER_NODE_MACHINE_ROW_MOST;
    }
    return n;
}

// Record `r`'s own entries by its template `kind` (node_machine_own's), from the last. `object` as in C. A labeled
// record's labels' entries as node_machine_back_after_row keeps them; a call's row is its number among the calls in the
// sweep's order until node_machine_lower numbers it, and its entry's place from the tape's end is kept in `call_ends`.
static inline __attribute__((always_inline)) uint32_t node_machine_back_own(NodeMachine* machine, NodeMachineBack out, uint32_t n, uint32_t r, CheckLowerTemplate kind,
                                                                          uint32_t object, uint32_t* ends)
{
    const uint8_t class = node_machine_record_class(machine, r);
    switch ((uint32_t)kind)
    {
        case CHECK_LOWER_TEMPLATE_LOCAL:
        {
            n = node_machine_back(out, n, BUSTER_TYPED_LOCAL, class, node_machine_record_object(machine, r));
        } break;
        case CHECK_LOWER_TEMPLATE_CONSTANT:
        {
            // An integer constant's CONST is node_machine_lower's, with its conversion.
            if ((node_machine_record_context(machine, r) & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) == 0)
            {
                if (node_machine_record_constant_kind(machine, r) == CHECK_CONSTANT_ADDRESS)
                {
                    if (node_machine_record_category(machine, r) == CHECK_RVALUE && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_I64)
                    {
                        n = node_machine_back(out, n, BUSTER_TYPED_CONVERT, class, 0);
                    }
                    n = node_machine_back_address(machine, out, n, node_machine_record_object(machine, r), node_machine_record_offset(machine, r));
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
                    n = node_machine_back(out, n, BUSTER_TYPED_CONST, class, bits);
                }
                else
                {
                    n = node_machine_back(out, n, BUSTER_TYPED_CONST, class, check_lower_float_bits(NULL, class, node_machine_record_floating(machine, r)));
                }
            }
        } break;
        case CHECK_LOWER_TEMPLATE_MEMBER:
        {
            const uint64_t offset = node_machine_record_member_offset(machine, r);
            if (node_machine_loads(machine, r))
            {
                n = node_machine_back(out, n, BUSTER_TYPED_LOAD, class, check_lower_access_value(machine->types, class, node_machine_record_type(machine, r)));
            }
            if (offset / 8 != 0)
            {
                n = node_machine_back(out, n, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
                n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, offset / 8);
            }
        } break;
        case CHECK_LOWER_TEMPLATE_STORE_LOCAL: case CHECK_LOWER_TEMPLATE_STORE_GLOBAL:
        {
            const uint32_t target = node_machine_record_op(machine, r) == CHECK_OP_INITIALIZE ? object : node_machine_record_object(machine, node_machine_record_left(machine, r));
            n = node_machine_back(out, n, kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL, class,
                                  kind == CHECK_LOWER_TEMPLATE_STORE_LOCAL ? target : machine->object_symbols[target]);
        } break;
        case CHECK_LOWER_TEMPLATE_STORE:
        {
            n = node_machine_back(out, n, BUSTER_TYPED_STORE, class, check_lower_access_value(machine->types, class, node_machine_record_type(machine, r)));
        } break;
        case CHECK_LOWER_TEMPLATE_TRUTH:
        {
            n = node_machine_back_truth(out, n, node_machine_class(machine, node_machine_record_converted(machine, r, 0)), node_machine_record_op(machine, r) == CHECK_OP_NOT);
        } break;
        case CHECK_LOWER_TEMPLATE_BINARY:
        {
            n = node_machine_back(out, n, check_lower_binary(node_machine_record_op(machine, r)), class, 0);
        } break;
        case CHECK_LOWER_TEMPLATE_POINTER:
        {
            const CheckOp op = node_machine_record_op(machine, r);
            if (op == CHECK_OP_SUBSCRIPT && node_machine_loads(machine, r))
            {
                n = node_machine_back(out, n, BUSTER_TYPED_LOAD, class, check_lower_access_value(machine->types, class, node_machine_record_type(machine, r)));
            }
            n = node_machine_back(out, n, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, check_action_pointer_stride(machine->types, node_machine_record_converted(machine, r, 0)));
            if (op == CHECK_OP_SUBTRACT)
            {
                n = node_machine_back(out, n, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64, 0);
            }
        } break;
        case CHECK_LOWER_TEMPLATE_GLOBAL:
        {
            n = node_machine_back(out, n, BUSTER_TYPED_GLOBAL, class, machine->object_symbols[node_machine_record_object(machine, r)]);
        } break;
        case CHECK_LOWER_TEMPLATE_ADDRESS:
        {
            n = node_machine_back_address(machine, out, n, node_machine_record_object(machine, r), node_machine_record_offset(machine, r));
        } break;
        case CHECK_LOWER_TEMPLATE_DEREFERENCE:
        {
            if (node_machine_loads(machine, r))
            {
                n = node_machine_back(out, n, BUSTER_TYPED_LOAD, class, check_lower_access_value(machine->types, class, node_machine_record_type(machine, r)));
            }
        } break;
        case CHECK_LOWER_TEMPLATE_UNARY:
        {
            n = node_machine_back(out, n, node_machine_record_op(machine, r) == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT, class, 0);
        } break;
        case NODE_MACHINE_TEMPLATE_BRANCH:
        {
            // From the last: its end; a logical operator's value at its alternate, the alternate, its jump to its end and
            // its second operand's test.
            const CheckOp op = node_machine_record_op(machine, r);
            const uint32_t alternate = node_machine_record_label_base(machine, r);
            ends[alternate + 1] = n;
            if (op != CHECK_OP_CONDITIONAL)
            {
                n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, op == CHECK_OP_LOGICAL_OR);
                ends[alternate] = n;
                n = node_machine_back(out, n, class == BUSTER_TYPED_CLASS_VOID ? BUSTER_TYPED_JUMP : BUSTER_TYPED_JUMP_VALUE, class, alternate + 1);
                n = node_machine_back_truth(out, n, node_machine_class(machine, node_machine_record_converted(machine, r, 1)), false);
            }
        } break;
        case NODE_MACHINE_TEMPLATE_DECIDED:
        {
            n = node_machine_back(out, n, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, node_machine_record_op(machine, r) == CHECK_OP_LOGICAL_OR);
        } break;
        case NODE_MACHINE_TEMPLATE_SECOND:
        {
            n = node_machine_back_truth(out, n, node_machine_class(machine, node_machine_record_converted(machine, r, 1)), false);
        } break;
        case NODE_MACHINE_TEMPLATE_UPDATE:
        {
            n = node_machine_back_update_last(machine, out, n, node_machine_record_update(machine, r));
        } break;
        case NODE_MACHINE_TEMPLATE_BRANCHES:
        {
            // node_machine_own's: an `&&` where true's or an `||` where false's own label, past its second operand.
            if (node_machine_record_past(machine, r))
            {
                ends[node_machine_record_label_base(machine, r)] = n;
            }
        } break;
        case NODE_MACHINE_TEMPLATE_CALL:
        {
            // Its row waits for the calls before its subtree, which the sweep meets after it.
            const uint32_t met = machine->call_count;
            machine->calls_written[met] = r;
            machine->call_count = met + 1;
            n = node_machine_back(out, n, BUSTER_TYPED_CALL, class, met);
            machine->call_ends[met] = n;
        } break;
        default:
        {
        } break;
    }
    return n;
}

// The labels', the calls' and the temporaries' numbers, after node_machine_lower's loop wrote `n` entries: what X numbers
// by the order it writes in. Only an expression with one of them runs it.
static __attribute__((noinline)) void node_machine_lower_number(NodeMachine* machine, NodeMachineBack out, uint32_t n, uint32_t* ends)
{
    const uint32_t labeled = machine->labeled_count;
    const uint32_t calls = machine->call_count;
    const uint32_t temporaries = machine->temporary_count;
    if (labeled != 0)
    {
        // The rows hold each label by its record's place among the labeled records as the sweep met them (2k and 2k + 1
        // for the kth, a branched operator's one label 2k): C's numbering gives its number, and its entry is its place
        // from the tape's start. A branch to the control's own label keeps it.
        uint32_t* met = machine->label_met;
        for (uint32_t k = 0; k < labeled; k += 1)
        {
            met[k] = machine->stack[k];
        }
        node_machine_number_labels(machine);
        for (uint32_t k = 0; k < labeled; k += 1)
        {
            const uint32_t base = node_machine_record_label_base(machine, met[k]);
            machine->label_entries[base] = n - ends[2 * k];
            if (node_machine_record_labels(machine, met[k]) == 2)
            {
                machine->label_entries[base + 1] = n - ends[2 * k + 1];
            }
        }
        for (uint32_t i = 1; i <= n; i += 1)
        {
            const uint8_t kind = out.kinds[-(int64_t)i];
            const uint64_t label = out.tape[-(int64_t)i];
            if ((kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE || kind == BUSTER_TYPED_JUMP || kind == BUSTER_TYPED_JUMP_VALUE) &&
                label != BUSTER_NODE_MACHINE_CONTROL_LABEL)
            {
                out.tape[-(int64_t)i] = node_machine_record_label_base(machine, met[label / 2]) + (label & 1);
            }
        }
    }
    if (calls != 0)
    {
        // The sweep met the calls from the last record to the first: X writes them in the other order, and a call's row is
        // the calls X wrote before its subtree's least record, plus those that contain it (node_machine_call_own).
        uint32_t* written = machine->calls_written;
        for (uint32_t k = 0; k < calls / 2; k += 1)
        {
            const uint32_t record = written[k];
            const uint32_t end = machine->call_ends[k];
            written[k] = written[calls - 1 - k];
            machine->call_ends[k] = machine->call_ends[calls - 1 - k];
            written[calls - 1 - k] = record;
            machine->call_ends[calls - 1 - k] = end;
        }
        for (uint32_t k = 0; k < calls; k += 1)
        {
            const uint32_t r = written[k];
            const uint32_t lowest = node_machine_record_lowest(machine, r);
            uint32_t before = k;
            while (before != 0 && written[before - 1] >= lowest)
            {
                before -= 1;
            }
            const uint32_t row = before + node_machine_record_call_depth(machine, r);
            machine->call_records[row] = r;
            out.tape[-(int64_t)machine->call_ends[k]] = row;
        }
    }
    if (temporaries != 0)
    {
        // An update's two temporaries are numbered together: X's first pair is the sweep's last.
        for (uint32_t i = 1; i <= n; i += 1)
        {
            const uint8_t kind = out.kinds[-(int64_t)i];
            const uint64_t value = out.tape[-(int64_t)i];
            if ((kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL) && (value & BUSTER_NODE_MACHINE_TEMPORARY) != 0)
            {
                const uint64_t number = value & ~BUSTER_NODE_MACHINE_TEMPORARY;
                out.tape[-(int64_t)i] = BUSTER_NODE_MACHINE_TEMPORARY | (temporaries - 2 - (number & ~UINT64_C(1)) + (number & 1));
            }
        }
    }
}

// C and X as one sweep: every evaluated record's template and its operands' contexts and targets, down from the root,
// which its consumer evaluates with `context` and converts to `target` (`object`: what a root INITIALIZE initializes),
// and its row at once, the tape written from its end (the file's comment). The machine's tape is then its columns'
// last `entry_count` entries, from `entry_first`, with its labels' entries (counted from that first entry) and its
// calls' rows as X leaves them. A refusal is C's or X's for the expression: where a row refuses, C's own sweep says
// whether a template refuses before it.
static NodeMachineRefusal node_machine_lower(NodeMachine* machine, uint8_t context, TypeId target, uint32_t object)
{
    NodeMachineRefusal refusal = NODE_MACHINE_TAKEN;
    const uint32_t count = machine->count;
    const NodeMachineBack out = {
        .kinds = machine->kinds + BUSTER_NODE_MACHINE_TAPE_MOST,
        .classes = machine->classes + BUSTER_NODE_MACHINE_TAPE_MOST,
        .tape = machine->tape + BUSTER_NODE_MACHINE_TAPE_MOST,
    };
    uint32_t* ends = machine->label_ends;
    node_machine_records_set_unevaluated(machine, count);
    machine->label_count = 0;
    machine->labeled_count = 0;
    machine->update_count = 0;
    machine->call_count = 0;
    machine->temporary_count = 0;
    const uint32_t root = count - 1;
    node_machine_root(machine, root, context, target);
    uint32_t r = count;
    uint32_t stopped = root;
    uint32_t n = 0;
    bool rows = true;
    while (refusal == NODE_MACHINE_TAKEN && r != 0)
    {
        r -= 1;
        const uint8_t state = node_machine_record_state(machine, r);
        if (__builtin_expect(state > BUSTER_NODE_MACHINE_EVALUATED, 0))
        {
            bool takes = true;
            n = node_machine_back_after_row(machine, out, n, r, state, ends, &takes);
            rows = rows && takes;
        }
        if ((state & BUSTER_NODE_MACHINE_EVALUATED) != 0)
        {
            NodeMachineUse use = {
                .count = 0,
            };
            const uint32_t labeled = machine->labeled_count;
            const CheckLowerTemplate kind = __builtin_expect((state & BUSTER_NODE_MACHINE_BRANCHED) != 0, 0) ? node_machine_classify_branches(machine, r) :
                                            node_machine_record_op(machine, r) == CHECK_OP_CALL ? node_machine_classify_call(machine, r) :
                                                                                                   node_machine_classify(machine, r, r == root ? object : 0, &use);
            refusal = kind == CHECK_LOWER_TEMPLATE_NONE ? NODE_MACHINE_REFUSED_TEMPLATE : NODE_MACHINE_TAKEN;
            stopped = r;
            if (use.count != 0)
            {
                const uint32_t depth = node_machine_record_call_depth(machine, r);
                node_machine_evaluate_at(machine, use.children[0], use.addresses[0], use.targets[0], depth);
                if (use.count == 2)
                {
                    node_machine_evaluate_at(machine, use.children[1], use.addresses[1], use.targets[1], depth);
                }
            }
            if (__builtin_expect(machine->labeled_count != labeled, 0))
            {
                // A labeled record: its labels by its place among those the sweep has met, until they are numbered.
                node_machine_record_set_label_base(machine, r, 2 * labeled);
            }
            // Its row, from the last entry: its conversion's entries, then its own. An integer constant's one CONST takes
            // its conversion where it folds, as the tasks' does (#2678).
            const uint32_t before = n;
            const uint8_t class = node_machine_record_class(machine, r);
            bool takes = true;
            if (kind == CHECK_LOWER_TEMPLATE_CONSTANT && node_machine_record_constant_kind(machine, r) == CHECK_CONSTANT_INTEGER &&
                (node_machine_record_context(machine, r) & BUSTER_CHECK_LOWER_TEMPLATE_DISCARD) == 0)
            {
                uint8_t constant_class = class;
                uint64_t bits = 0;
                if (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64)
                {
                    bits = node_machine_record_integer(machine, r);
                    const uint32_t width = 8u << ((class - 1) / 2);
                    if (width < 64)
                    {
                        const uint64_t mask = (UINT64_C(1) << width) - 1;
                        bits &= mask;
                        bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
                    }
                }
                else
                {
                    bits = check_lower_float_bits(NULL, class, node_machine_record_floating(machine, r));
                }
                if (!node_machine_back_fold(machine, r, &constant_class, &bits) && node_machine_record_context(machine, r) == 0)
                {
                    n = node_machine_back_convert(machine, out, n, node_machine_record_type(machine, r), node_machine_record_facts(machine, r), class,
                                                  node_machine_record_target(machine, r), &takes);
                }
                n = node_machine_back(out, n, BUSTER_TYPED_CONST, constant_class, bits);
            }
            else
            {
                if (node_machine_record_context(machine, r) == 0)
                {
                    n = node_machine_back_convert(machine, out, n, node_machine_record_type(machine, r), node_machine_record_facts(machine, r), class,
                                                  node_machine_record_target(machine, r), &takes);
                }
                n = node_machine_back_own(machine, out, n, r, kind, r == root ? object : 0, ends);
            }
            rows = rows && takes && n - before <= BUSTER_NODE_MACHINE_ROW_MOST;
        }
    }
    machine->stopped = stopped;
    machine->entry_first = BUSTER_NODE_MACHINE_TAPE_MOST - n;
    machine->entry_count = n;
    if (__builtin_expect(refusal == NODE_MACHINE_TAKEN && !rows, 0))
    {
        // A row X refuses: C's sweep says whether a template the sweep hadn't reached refuses first, as the two sweeps do.
        refusal = node_machine_contexts(machine, context, target, object);
        refusal = refusal == NODE_MACHINE_TAKEN ? NODE_MACHINE_REFUSED_ROW : refusal;
    }
    else if (__builtin_expect(refusal == NODE_MACHINE_TAKEN && (machine->label_count | machine->call_count | machine->temporary_count) != 0, 0))
    {
        node_machine_lower_number(machine, out, n, ends);
    }
    return refusal;
}

#endif
