// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// One ordered typed replay, instantiated by ir_lower.c for count and fill. Counting has no output columns; filling
// receives the non-null function created successfully by ir_create. Keep all operation and CFG logic shared here.
// This file is included twice intentionally; the two macros are private to the including module.

#if !defined(BUSTER_IR_TYPED_EMIT_NAME) || !defined(BUSTER_IR_TYPED_EMIT_COUNT)
#error "include through ir_lower.c with a count/fill instantiation"
#endif

static IrSizes BUSTER_IR_TYPED_EMIT_NAME(const Tier0Function* input, IrType result_type, const IrTypedPlan* plan, const Arena* scratch, const uint8_t* ops, const uint8_t* types, uint32_t* stack, uint8_t* classes, IrFunction* output)
{
#if BUSTER_IR_TYPED_EMIT_COUNT
    output = 0;
#else
    __builtin_assume(output != 0);
#endif
    // A function with indirect jumps has one dispatch block, after the source blocks, which loads the address each jump
    // stored in the dispatch slot, after the depth slots, and is the function's one br_indirect (as clang's indirectgoto
    // block): the CFG's edges are then the jumps' and the labels', not every jump's to every label.
    const bool dispatch = plan->indirect != 0;
    const uint32_t dispatch_slot = input->local_count + plan->maximum_spill_depth;
    const uint32_t dispatch_block = plan->block_count + 1;
    IrTypedSink sink = {
        .output = output,
        .sizes = {
            .block_count = (uint64_t)plan->block_count + 1 + dispatch,
            .slot_count = (uint64_t)dispatch_slot + dispatch,
            .parameter_count = input->parameter_count,
            .label_count = input->label_count,
        },
    };
    const uint32_t slots = dispatch_slot + dispatch;
    // A function that takes or returns an aggregate by value has its own ABI records (IrFunction.parameter_passed):
    // each parameter's location and size (an object's, its bytes'), and its result's. Its result in registers is the
    // address of its bytes, a ptr.
    const bool own = ir_typed_own_records(input);
    const uint32_t own_passed = (uint32_t)sink.sizes.passed_count;
    sink.sizes.passed_count += own ? input->parameter_count : 0;
    // Block zero is a genuine preheader. A source backedge to entry zero goes
    // to block one and never executes parameter initialization again.
    if (output != 0)
    {
        output->block_offsets[0] = 0;
        output->result_type = (uint8_t)(input->result_size != 0 ? IR_PTR : result_type);
        // Label k's block: its entry's source block, after the preheader.
        for (uint32_t k = 0; k < input->label_count; k += 1)
        {
            output->labels[k] = plan->entry_blocks[input->label_entries[k]] + 1;
        }
        if (own)
        {
            output->parameter_passed = own_passed + 1;
            output->result_size = input->result_size;
            output->result_abi = input->result;
            for (uint32_t p = 0; p < input->parameter_count; p += 1)
            {
                output->passed[own_passed + p] = (IrPassed){
                    .location = input->parameters[p],
                    .size = tier0_is_object(input, p) ? input->local_sizes[p] : 0,
                };
            }
        }
        // A variadic definition's incoming frame, which its anchors reach.
        if (input->incoming_variadic != 0)
        {
            output->variadic = true;
            output->incoming = *input->incoming_variadic;
        }
    }
    for (uint32_t s = 0; s < slots; s += 1)
    {
        ir_typed_emit(&sink, IR_STACK_ADDR, IR_PTR, s, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        if (output != 0)
        {
            // The depth slots are expression slots, untyped; the dispatch slot holds an address (i64), which promotion
            // makes a phi of the jumps' addresses.
            const bool local = s < input->local_count;
            const uint8_t class = local ? input->local_classes[s] : BUSTER_TYPED_CLASS_U64;
            // tier0_validate established that an object is an address-taken VOID local aligned to at most 16, and that a
            // role is an address-taken U64 local: a va_list's bytes, or one of the incoming frame's anchors.
            const bool object = local && tier0_is_object(input, s);
            const uint32_t role = local ? input->local_flags[s] & BUSTER_TYPED_LOCAL_ADDRESS_ONLY : 0;
            const bool flags = local && (input->local_flags[s] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
            IrSlot slot = {
                .size = object ? input->local_sizes[s] : local ? tier0_class_bytes(class) : s == dispatch_slot ? 8 : plan->spill_bytes,
                .alignment = object ? 1u << input->local_alignments[s] : local ? 1u << ir_typed_alignment(class) : s == dispatch_slot ? 8 : plan->spill_bytes,
                .type = local && !object ? ir_typed_types[class] : s == dispatch_slot ? IR_I64 : IR_VOID,
                .flags = local ? (flags ? BUSTER_IR_SLOT_ADDRESS_TAKEN : 0) : BUSTER_IR_SLOT_TEMPORARY,
            };
            if (role == BUSTER_TYPED_LOCAL_VA_LIST)
            {
                slot = (IrSlot){ .size = sizeof(AbiVaList), .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
            }
            else if (role != 0)
            {
                const bool save = role == BUSTER_TYPED_LOCAL_REGISTER_SAVE_AREA;
                slot = (IrSlot){
                    .size = save ? BUSTER_ABI_SAVE_AREA_BYTES : 0,
                    .alignment = 16,
                    .type = IR_VOID,
                    .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN | (save ? BUSTER_IR_SLOT_REGISTER_SAVE_AREA : BUSTER_IR_SLOT_OVERFLOW_AREA),
                };
            }
            output->slots[s] = slot;
        }
    }
    for (uint32_t p = 0; p < input->parameter_count; p += 1)
    {
        // An aggregate (an object parameter: a struct, a union, a long double, an __int128) arrives as a ptr to its
        // bytes, which the preheader copies into its object; any other parameter's value is stored in its local's slot.
        const bool object = tier0_is_object(input, p);
        const uint8_t class = input->local_classes[p];
        const IrType type = object ? IR_PTR : (IrType)ir_typed_types[class];
        if (output != 0)
        {
            output->parameter_types[p] = (uint8_t)type;
        }
        const uint32_t parameter = ir_typed_emit(&sink, IR_PARAM, type, p, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        if (object)
        {
            ir_typed_copy_pointers(&sink, p, parameter, input->local_sizes[p]);
        }
        else
        {
            ir_typed_emit(&sink, IR_STORE, IR_VOID, p, parameter, BUSTER_IR_NONE, ir_typed_alignment(class));
        }
    }
    ir_typed_emit(&sink, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);

    // The next jump table, whose jump (Tier0Function.table_jumps, in entry order) is a br_indirect of its own (#785).
    uint32_t table = 0;
    for (uint32_t b = 0; b < plan->block_count; b += 1)
    {
        if (output != 0)
        {
            output->block_offsets[b + 1] = (uint32_t)sink.sizes.instruction_count;
        }
        const uint32_t first = plan->block_firsts[b];
        const uint32_t end = plan->block_firsts[b + 1];
        uint32_t depth = plan->stack_depths[b];
        memcpy(classes, scratch->base + plan->stack_offsets[b], depth);
        // Values at the typed endpoint are discarded, so it needs no reloads.
        if (first < input->count)
        {
            for (uint32_t d = 0; d < depth; d += 1)
            {
                stack[d] = ir_typed_emit(&sink, IR_LOAD, ir_typed_stack_type(classes[d]), input->local_count + d, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
            }
        }
        bool terminated = false;
        for (uint32_t i = first; i < end; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint8_t class = ir_typed_stack_class(input, i);
            const IrType type = (IrType)types[i];
            const uint64_t value = input->values[i];
            switch (kind)
            {
            case BUSTER_TYPED_CONST:
                // An __int128's low 64 bits, sign-extended (I128) or zero-extended (U128); a long double's an index into
                // x87_constants, its 64-bit significand then its sign and exponent; any other's, its bits.
                if (class >= BUSTER_TYPED_CLASS_LIMIT)
                {
                    stack[depth] = ir_typed_vector_constant(&sink, type, input->vector_constants + value * 8);
                }
                else if (class == BUSTER_TYPED_CLASS_F80)
                {
                    stack[depth] = ir_typed_wide_constant(&sink, type, input->x87_constants[value * 2], input->x87_constants[value * 2 + 1]);
                }
                else
                {
                    stack[depth] = ir_typed_wide_constant(&sink, type, value, class == BUSTER_TYPED_CLASS_I128 && (int64_t)value < 0 ? UINT64_MAX : 0);
                }
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_LOCAL:
                stack[depth] = ir_typed_emit(&sink, IR_LOAD, type, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, class >= BUSTER_TYPED_CLASS_LIMIT ? input->local_alignments[value] : ir_typed_alignment(class));
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_LOCAL_ADDRESS:
                stack[depth] = ir_typed_emit(&sink, IR_PTRTOINT, IR_I64, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_GLOBAL:
            case BUSTER_TYPED_GLOBAL_ADDRESS:
            {
                const uint32_t address = ir_typed_emit(&sink, IR_SYMBOL_ADDR, IR_PTR, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                stack[depth] = ir_typed_emit(&sink, kind == BUSTER_TYPED_GLOBAL ? IR_LOAD : IR_PTRTOINT, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth] = class;
                depth += 1;
                break;
            }
            case BUSTER_TYPED_DUP:
                stack[depth] = stack[depth - 1];
                classes[depth] = classes[depth - 1];
                depth += 1;
                break;
            case BUSTER_TYPED_X86:
                depth -= input->operations[value].operand_count;
                stack[depth] = ir_typed_vector_operation(&sink, plan->operations + value, type, stack + depth);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_NEG:
            case BUSTER_TYPED_NOT:
                stack[depth - 1] = ir_typed_emit(&sink, (IrOp)ops[i], type, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                break;
            case BUSTER_TYPED_CONVERT:
                stack[depth - 1] = ir_typed_convert(&sink, stack[depth - 1], classes[depth - 1], class);
                classes[depth - 1] = class;
                break;
            case BUSTER_TYPED_LOAD:
            {
                const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                stack[depth - 1] = ir_typed_emit(&sink, IR_LOAD, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth - 1] = class;
                break;
            }
            case BUSTER_TYPED_PTR_ADD:
                depth -= 1;
                stack[depth - 1] = ir_typed_address(&sink, stack[depth - 1], stack[depth], value);
                classes[depth - 1] = class;
                break;
            case BUSTER_TYPED_STORE:
            case BUSTER_TYPED_SET:
            {
                const uint32_t stored = stack[depth - 1];
                const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth - 2], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_STORE, IR_VOID, address, stored, BUSTER_IR_NONE, 0);
                depth -= 2;
                if (kind == BUSTER_TYPED_STORE)
                {
                    stack[depth] = stored;
                    classes[depth] = class;
                    depth += 1;
                }
                break;
            }
            case BUSTER_TYPED_STORE_LOCAL:
            case BUSTER_TYPED_SET_LOCAL:
            case BUSTER_TYPED_STORE_GLOBAL:
            case BUSTER_TYPED_SET_GLOBAL:
            {
                const bool local = kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
                const uint32_t address = local ? (uint32_t)value : ir_typed_emit(&sink, IR_SYMBOL_ADDR, IR_PTR, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_STORE, IR_VOID, address, stack[depth - 1], BUSTER_IR_NONE, local ? (class >= BUSTER_TYPED_CLASS_LIMIT ? input->local_alignments[value] : ir_typed_alignment(class)) : 0);
                if (kind == BUSTER_TYPED_SET_LOCAL || kind == BUSTER_TYPED_SET_GLOBAL)
                {
                    depth -= 1;
                }
                break;
            }
            case BUSTER_TYPED_CALL:
            {
                depth -= input->call_argument_counts[value];
                // An indirect call's address (U64) was pushed before its arguments, under them.
                const bool indirect = input->call_targets[value] == 0;
                const uint32_t callee = indirect ? ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0) : BUSTER_IR_NONE;
                const uint32_t result = ir_typed_call(&sink, input, (uint32_t)value, type, stack + depth, callee);
                depth -= indirect;
                if (class != BUSTER_TYPED_CLASS_VOID)
                {
                    stack[depth] = result;
                    classes[depth] = class;
                    depth += 1;
                }
                break;
            }
            case BUSTER_TYPED_DISCARD:
                depth -= 1;
                break;
            case BUSTER_TYPED_ALLOCA:
            {
                // Typed validation fixes allocation/save classes to U64, classified as IR_I64 by both paths.
                const uint32_t address = ir_typed_emit(&sink, IR_ALLOCA, IR_PTR, stack[depth - 1], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                stack[depth - 1] = ir_typed_emit(&sink, IR_PTRTOINT, type, address, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth - 1] = class;
                break;
            }
            case BUSTER_TYPED_STACK_SAVE:
                stack[depth] = ir_typed_emit(&sink, IR_STACK_SAVE, type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_STACK_RESTORE:
                depth -= 1;
                ir_typed_emit(&sink, IR_STACK_RESTORE, IR_VOID, stack[depth], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                break;
            case BUSTER_TYPED_COPY:
                ir_typed_copy(&sink, stack[depth - 2], stack[depth - 1], value);
                depth -= 2;
                break;
            case BUSTER_TYPED_BRANCH_FALSE:
            case BUSTER_TYPED_BRANCH_TRUE:
            {
                depth -= 1;
                const uint32_t zero = ir_typed_constant(&sink, type, 0);
                const uint32_t condition = ir_typed_emit(&sink, IR_ICMP, IR_I1, stack[depth], zero, IR_NE, 0);
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                const uint32_t target = plan->entry_blocks[value] + 1;
                const uint32_t next = b + 2;
                ir_typed_emit(&sink, IR_CBR, IR_VOID, condition, kind == BUSTER_TYPED_BRANCH_TRUE ? target : next, kind == BUSTER_TYPED_BRANCH_TRUE ? next : target, 0);
                terminated = true;
                break;
            }
            case BUSTER_TYPED_JUMP:
            case BUSTER_TYPED_JUMP_VALUE:
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                ir_typed_emit(&sink, IR_BR, IR_VOID, plan->entry_blocks[value] + 1, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                terminated = true;
                break;
            case BUSTER_TYPED_RETURN:
            {
                // An aggregate returned in registers returns the address of its bytes (U64), as a ptr.
                uint32_t returned = class == BUSTER_TYPED_CLASS_VOID ? BUSTER_IR_NONE : stack[--depth];
                if (input->result_size != 0)
                {
                    returned = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                }
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                terminated = true;
                break;
            }
            case BUSTER_TYPED_JUMP_INDIRECT:
            {
                // A label's address (U64), at statement level (nothing else on the stack to spill): a jump table's to a
                // br_indirect of its table's labels, a goto *'s to the dispatch block.
                depth -= 1;
                if (table < input->table_count && input->table_jumps[table] == i)
                {
                    const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, stack[depth], BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                    const uint32_t labels = (uint32_t)sink.sizes.extra_count;
                    table += 1;
                    const uint32_t listed = ir_typed_labels(&sink, input, table);
                    ir_typed_emit(&sink, IR_BR_INDIRECT, IR_VOID, address, labels, listed, 0);
                }
                else
                {
                    ir_typed_emit(&sink, IR_STORE, IR_VOID, dispatch_slot, stack[depth], BUSTER_IR_NONE, ir_typed_alignment(BUSTER_TYPED_CLASS_U64));
                    ir_typed_emit(&sink, IR_BR, IR_VOID, dispatch_block, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                }
                terminated = true;
            } break;
            default:
            {
                const uint32_t right = stack[depth - 1];
                const uint32_t left = stack[depth - 2];
                const uint8_t operand_class = classes[depth - 2];
                const uint8_t right_class = classes[depth - 1];
                depth -= 1;
                if (kind >= BUSTER_TYPED_EQ && kind <= BUSTER_TYPED_GE)
                {
                    uint32_t predicate = kind - BUSTER_TYPED_EQ;
                    if (predicate >= IR_LT && !ir_typed_floating(operand_class) && !ir_typed_signed(operand_class))
                    {
                        predicate += IR_ULT - IR_LT;
                    }
                    const uint32_t comparison = ir_typed_emit(&sink, ir_typed_floating(operand_class) ? IR_FCMP : IR_ICMP, IR_I1, left, right, predicate, 0);
                    stack[depth - 1] = ir_typed_emit(&sink, IR_ZEXT, IR_I32, comparison, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                }
                else
                {
                    const bool shift = kind == BUSTER_TYPED_SHL || kind == BUSTER_TYPED_SHR;
                    const uint32_t converted = shift ? ir_typed_convert(&sink, right, right_class, class) : right;
                    stack[depth - 1] = ir_typed_emit(&sink, (IrOp)ops[i], type, left, converted, BUSTER_IR_NONE, 0);
                }
                classes[depth - 1] = class;
                break;
            }
            }
        }
        if (!terminated)
        {
            if (first == input->count)
            {
                // The typed endpoint denotes an epilogue. An explicit semantic
                // result type is required from the producer; nonvoid fallthrough
                // returns undef, as the old stream leaves its result unspecified.
                const IrType returns = input->result_size != 0 ? IR_PTR : result_type;
                const uint32_t returned = returns == IR_VOID ? BUSTER_IR_NONE : ir_typed_emit(&sink, IR_UNDEF, returns, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
            else
            {
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                ir_typed_emit(&sink, IR_BR, IR_VOID, b + 2, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
        }
    }
    if (dispatch)
    {
        if (output != 0)
        {
            output->block_offsets[dispatch_block] = (uint32_t)sink.sizes.instruction_count;
        }
        // The goto *s' labels: those of no jump table, or every label in a function without tables (or whose goto *s have
        // no label of their own to go to, which no jump of C's reaches).
        const uint32_t loaded = ir_typed_emit(&sink, IR_LOAD, IR_I64, dispatch_slot, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(BUSTER_TYPED_CLASS_U64));
        const uint32_t address = ir_typed_emit(&sink, IR_INTTOPTR, IR_PTR, loaded, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        const uint32_t labels = (uint32_t)sink.sizes.extra_count;
        const uint32_t listed = ir_typed_labels(&sink, input, 0);
        ir_typed_emit(&sink, IR_BR_INDIRECT, IR_VOID, address, listed != 0 ? labels : BUSTER_IR_NONE, listed != 0 ? listed : BUSTER_IR_NONE, 0);
    }
    if (output != 0)
    {
        output->block_offsets[plan->block_count + 1 + dispatch] = (uint32_t)sink.sizes.instruction_count;
    }
    return sink.sizes;
}
