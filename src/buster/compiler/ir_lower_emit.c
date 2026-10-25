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
    IrTypedSink sink = {
        .output = output,
        .sizes = {
            .block_count = (uint64_t)plan->block_count + 1,
            .slot_count = (uint64_t)input->local_count + plan->maximum_spill_depth,
            .parameter_count = input->parameter_count,
        },
    };
    const uint32_t slots = input->local_count + plan->maximum_spill_depth;
    // Block zero is a genuine preheader. A source backedge to entry zero goes
    // to block one and never executes parameter initialization again.
    if (output != 0)
    {
        output->block_offsets[0] = 0;
        output->result_type = (uint8_t)result_type;
    }
    for (uint32_t s = 0; s < slots; s += 1)
    {
        ir_typed_emit(&sink, IR_STACK_ADDR, IR_PTR, s, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        if (output != 0)
        {
            const bool local = s < input->local_count;
            const uint8_t class = local ? input->local_classes[s] : BUSTER_TYPED_CLASS_U64;
            // tier0_validate established that an object is an address-taken VOID local aligned to at most 16.
            const bool object = local && tier0_is_object(input, s);
            output->slots[s] = (IrSlot){
                .size = object ? input->local_sizes[s] : tier0_class_bytes(class),
                .alignment = 1u << (object ? input->local_alignments[s] : ir_typed_alignment(class)),
                .type = local ? ir_typed_types[class] : IR_VOID,
                .flags = local ? ((input->local_flags[s] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0 ? BUSTER_IR_SLOT_ADDRESS_TAKEN : 0) : BUSTER_IR_SLOT_TEMPORARY,
            };
        }
    }
    for (uint32_t p = 0; p < input->parameter_count; p += 1)
    {
        const uint8_t class = input->local_classes[p];
        const IrType type = (IrType)ir_typed_types[class];
        if (output != 0)
        {
            output->parameter_types[p] = (uint8_t)type;
        }
        const uint32_t parameter = ir_typed_emit(&sink, IR_PARAM, type, p, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
        ir_typed_emit(&sink, IR_STORE, IR_VOID, p, parameter, BUSTER_IR_NONE, ir_typed_alignment(class));
    }
    ir_typed_emit(&sink, IR_BR, IR_VOID, 1, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);

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
                stack[d] = ir_typed_emit(&sink, IR_LOAD, (IrType)ir_typed_types[classes[d]], input->local_count + d, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(classes[d]));
            }
        }
        bool terminated = false;
        for (uint32_t i = first; i < end; i += 1)
        {
            const uint8_t kind = input->kinds[i];
            const uint8_t class = input->classes[i];
            const IrType type = (IrType)types[i];
            const uint64_t value = input->values[i];
            switch (kind)
            {
            case BUSTER_TYPED_CONST:
                stack[depth] = ir_typed_constant(&sink, type, value);
                classes[depth] = class;
                depth += 1;
                break;
            case BUSTER_TYPED_LOCAL:
                stack[depth] = ir_typed_emit(&sink, IR_LOAD, type, (uint32_t)value, BUSTER_IR_NONE, BUSTER_IR_NONE, ir_typed_alignment(class));
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
                ir_typed_emit(&sink, IR_STORE, IR_VOID, address, stack[depth - 1], BUSTER_IR_NONE, local ? ir_typed_alignment(class) : 0);
                if (kind == BUSTER_TYPED_SET_LOCAL || kind == BUSTER_TYPED_SET_GLOBAL)
                {
                    depth -= 1;
                }
                break;
            }
            case BUSTER_TYPED_CALL:
            {
                depth -= input->call_argument_counts[value];
                const uint32_t result = ir_typed_call(&sink, input, (uint32_t)value, type, stack + depth);
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
                const uint32_t returned = class == BUSTER_TYPED_CLASS_VOID ? BUSTER_IR_NONE : stack[--depth];
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                terminated = true;
                break;
            }
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
                    if (predicate >= IR_LT && !tier0_class_floating(operand_class) && !tier0_class_signed(operand_class))
                    {
                        predicate += IR_ULT - IR_LT;
                    }
                    const uint32_t comparison = ir_typed_emit(&sink, tier0_class_floating(operand_class) ? IR_FCMP : IR_ICMP, IR_I1, left, right, predicate, 0);
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
                const uint32_t returned = result_type == IR_VOID ? BUSTER_IR_NONE : ir_typed_emit(&sink, IR_UNDEF, result_type, BUSTER_IR_NONE, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
                ir_typed_emit(&sink, IR_RET, IR_VOID, returned, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
            else
            {
                ir_typed_spill(&sink, stack, classes, depth, input->local_count);
                ir_typed_emit(&sink, IR_BR, IR_VOID, b + 2, BUSTER_IR_NONE, BUSTER_IR_NONE, 0);
            }
        }
    }
    if (output != 0)
    {
        output->block_offsets[plan->block_count + 1] = (uint32_t)sink.sizes.instruction_count;
    }
    return sink.sizes;
}
