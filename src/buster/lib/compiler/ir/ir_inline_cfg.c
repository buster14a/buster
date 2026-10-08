// Multi-block direct-call splice. Included by ir_inline.c.
BUSTER_GLOBAL_LOCAL IrLocalId ir_inline_cfg_local(IrLocalId id, u32 base)
{
    return id.value == IR_ID_UNDERLYING_INVALID ? id : (IrLocalId){.value = base + id.value};
}

BUSTER_GLOBAL_LOCAL IrTypeId ir_inline_cfg_void_type(IrProgram* program)
{
    IrTypeId result = IR_TYPE_ID_INVALID;
    if (program)
    {
        for (u32 i = 0; i < program->types.count; i += 1)
        {
            if (program->types.types[i].kind == IR_TYPE_VOID)
            {
                result = program->types.types[i].id;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ir_inline_cfg_storage_bytes(IrFunction const* caller, IrFunction const* callee)
{
    bool valid = caller && callee && (!caller->block_count || (caller->blocks && caller->instructions)) &&
                 (!callee->block_count || (callee->blocks && callee->instructions));
    u64 cost = 0;
    u64 local_slots = valid ? (u64)caller->local_count + callee->local_count + 1 : 0;
    u64 block_rows = valid ? (u64)caller->block_count + callee->block_count + 1 : 0;
    u64 instruction_rows = valid ? (u64)caller->instruction_count + (u64)callee->instruction_count * 2 + 4 : 0;
    u64 value_rows = valid ? (u64)caller->value_count + callee->value_count + 1 : 0;
    u64 debug_rows = valid ? (u64)caller->debug_local_count + callee->debug_local_count : 0;
    u64 edge_rows = 0;
    u64 parameter_rows = 0;
    u64 incoming_rows = 0;
    u64 operand_rows = 0;
    u64 target_rows = 0;
    u64 immediate_rows = 0;
    u64 extra_rows = valid ? (u64)caller->extra_count + callee->extra_count : 0;
    if (valid)
    {
        for (u32 i = 0; i < caller->block_count; i += 1)
        {
            IrBlock const* block = caller->blocks + i;
            if (block->last_instruction.value < caller->instruction_count)
            {
                u64 count = caller->instructions[block->last_instruction.value].target_count;
                if (count > UINT64_MAX - edge_rows) valid = false;
                else edge_rows += count;
            }
        }
        for (u32 i = 0; i < callee->block_count; i += 1)
        {
            IrBlock const* block = callee->blocks + i;
            if (block->last_instruction.value < callee->instruction_count)
            {
                u64 count = callee->instructions[block->last_instruction.value].target_count;
                if (count > UINT64_MAX - edge_rows) valid = false;
                else edge_rows += count;
            }
            for (IrBlockParameter const* parameter = block->first_parameter; parameter; parameter = parameter->next)
            {
                parameter_rows += 1;
                if (parameter->incoming_count > UINT64_MAX - incoming_rows) valid = false;
                else incoming_rows += parameter->incoming_count;
            }
        }
        for (u32 i = 0; i < callee->instruction_count; i += 1)
        {
            IrInstruction const* row = callee->instructions + i;
            if (row->operand_count > UINT64_MAX - operand_rows ||
                row->target_count > UINT64_MAX - target_rows ||
                row->immediate_count > UINT64_MAX - immediate_rows) valid = false;
            else
            {
                operand_rows += row->operand_count;
                target_rows += row->target_count;
                immediate_rows += row->immediate_count;
            }
        }
    }
    // Overestimate each growing parallel table by its final population. The
    // arena retains superseded arrays until the compile arena is retired.
#define IR_INLINE_CFG_COST(count, element_size) \
    do \
    { \
        u64 count_ = (u64)(count); \
        u64 size_ = (u64)(element_size); \
        if (count_ && size_ > (UINT64_MAX - cost) / count_) valid = false; \
        else if (valid) cost += count_ * size_; \
    } while (0)
    IR_INLINE_CFG_COST(block_rows * 4, sizeof(IrBlock));
    IR_INLINE_CFG_COST(instruction_rows * 4, sizeof(IrInstruction) + sizeof(IrSourceRange));
    IR_INLINE_CFG_COST(value_rows * 4, sizeof(IrValue));
    IR_INLINE_CFG_COST(local_slots * 2, sizeof(IrValueId) + sizeof(bool));
    IR_INLINE_CFG_COST(debug_rows, sizeof(IrDebugLocal));
    IR_INLINE_CFG_COST(valid ? edge_rows + callee->block_count + 1 : 0, sizeof(IrPredecessor));
    IR_INLINE_CFG_COST(parameter_rows + 1, sizeof(IrBlockParameter));
    IR_INLINE_CFG_COST(valid ? incoming_rows + callee->instruction_count : 0, sizeof(IrIncoming));
    IR_INLINE_CFG_COST(valid ? operand_rows + (u64)callee->block_count * 2 + 2 : 0, sizeof(IrValueId));
    IR_INLINE_CFG_COST(valid ? target_rows + callee->instruction_count + 1 : 0, sizeof(IrBlockId));
    IR_INLINE_CFG_COST(immediate_rows, sizeof(u64));
    IR_INLINE_CFG_COST(extra_rows * 4, sizeof(IrInstructionId) + sizeof(IrInstructionExtra));
#define IR_INLINE_CFG_PRODUCT(left, right, element_size) \
    do \
    { \
        u64 left_ = (u64)(left); \
        u64 right_ = (u64)(right); \
        if (left_ && right_ > UINT64_MAX / left_) valid = false; \
        else IR_INLINE_CFG_COST(left_ * right_, element_size); \
    } while (0)
    IR_INLINE_CFG_PRODUCT(block_rows, local_slots, sizeof(IrValueId));
#undef IR_INLINE_CFG_PRODUCT
#undef IR_INLINE_CFG_COST
    if (!valid) cost = UINT64_MAX;
    return cost;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_cfg_supported(IrProgram* program, IrFunction* caller, IrBlockId call_block,
                                    IrInstructionId call_id, IrFunction const* callee,
                                    u32* copied_rows_out, u32* growth_out)
{
    u32 copied = 0;
    TemporalArena scratch = {0};
    if (program && program->arena)
    {
        Arena* conflicts[] = {program->arena};
        scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    }
    bool valid = program && program->arena && caller && callee && caller != callee &&
                 caller->state == IR_FUNCTION_LOWERED && callee->state == IR_FUNCTION_LOWERED &&
                 !caller->published_cfg && !callee->published_cfg &&
                 call_block.value < caller->block_count && call_id.value < caller->instruction_count &&
                 callee->block_count && callee->entry.value < callee->block_count &&
                 !callee->label_metadata_count && caller->symbol.value != callee->symbol.value &&
                 callee->local_count <= UINT32_MAX - caller->local_count &&
                 callee->debug_local_count <= UINT32_MAX - caller->debug_local_count;
    IrInstruction const* call = valid ? caller->instructions + call_id.value : 0;
    IrType* signature = valid ? ir_type_from_id(&program->types, callee->canonical_type) : 0;
    IrType* return_type = signature ? ir_type_from_id(&program->types, signature->return_type) : 0;
    valid = valid && call && call->opcode == IR_OPCODE_CALL && signature && signature->kind == IR_TYPE_FUNCTION &&
            !signature->is_variadic && !signature->is_unprototyped && return_type &&
            signature->parameter_count < UINT32_MAX &&
            call->operand_count == signature->parameter_count + 1 &&
            call->symbol.value == callee->symbol.value &&
            call->operands && call->operands[0].value < caller->value_count &&
            caller->values[call->operands[0].value].definition.value < caller->instruction_count;
    if (valid)
    {
        for (u32 i = 1; i < call->operand_count; i += 1)
        {
            if (call->operands[i].value >= caller->value_count) valid = false;
        }
        if (return_type->kind == IR_TYPE_VOID)
            valid = valid && call->result.value == IR_ID_UNDERLYING_INVALID;
    }
    if (valid)
    {
        IrInstruction const* reference = caller->instructions + caller->values[call->operands[0].value].definition.value;
        valid = reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == callee->symbol.value &&
                (call->result.value == IR_ID_UNDERLYING_INVALID ||
                 (call->result.value < caller->value_count &&
                  caller->values[call->result.value].canonical_type.value == signature->return_type.value));
    }
    if (valid)
    {
        IrBlock const* block = caller->blocks + call_block.value;
        bool found = false;
        IrInstructionId row = block->first_instruction;
        for (u32 visits = 0; row.value < caller->instruction_count && visits <= caller->instruction_count; visits += 1)
        {
            if (row.value == call_id.value) found = true;
            row = caller->instructions[row.value].next;
        }
        valid = found;
    }

    // The entry block is entered without edge arguments. Formal parameters are
    // IR_OPCODE_ARGUMENT rows and are replaced with the already-evaluated call
    // operands below.
    if (valid)
    {
        for (u32 b = 0; b < caller->block_count; b += 1)
        {
            IrBlock const* block = caller->blocks + b;
            IrInstruction const* tail = block->last_instruction.value < caller->instruction_count
                                            ? caller->instructions + block->last_instruction.value : 0;
            if (!tail || !ir_instruction_is_terminator(tail) ||
                tail->next.value != IR_ID_UNDERLYING_INVALID) valid = false;
        }
        for (u32 b = 0; b < callee->block_count; b += 1)
        {
            IrBlock const* block = callee->blocks + b;
            IrInstruction const* tail = block->last_instruction.value < callee->instruction_count
                                            ? callee->instructions + block->last_instruction.value : 0;
            if (!tail || !ir_instruction_is_terminator(tail) ||
                tail->next.value != IR_ID_UNDERLYING_INVALID) valid = false;
        }
    }
    if (valid) valid = !callee->blocks[callee->entry.value].first_parameter;
    u32 argument_count = signature ? signature->parameter_count : 0;
    bool* seen_arguments = valid ? arena_allocate_zeroed(scratch.arena, bool, argument_count) : 0;
    u32 return_count = 0;
    for (u32 i = 0; valid && i < callee->instruction_count; i += 1)
    {
        IrInstruction const* row = callee->instructions + i;
        if (row->opcode == IR_OPCODE_ARGUMENT)
        {
            valid = row->result.value < callee->value_count && row->immediate_count == 1 && row->immediates &&
                    row->immediates[0] < argument_count;
            if (valid)
            {
                u32 argument = (u32)row->immediates[0];
                IrValue const* actual = caller->values + call->operands[argument + 1].value;
                IrValue const* formal = callee->values + row->result.value;
                valid = !seen_arguments[argument] &&
                        actual->canonical_type.value == formal->canonical_type.value &&
                        actual->category == formal->category;
                seen_arguments[argument] = true;
            }
            continue;
        }
        if (row->opcode == IR_OPCODE_RETURN)
        {
            return_count += 1;
            valid = (return_type->kind == IR_TYPE_VOID ? row->operand_count == 0 :
                     row->operand_count == 1 && row->operands != 0);
        }
        valid = valid && (row->opcode != IR_OPCODE_CALL || !ir_call_returns_twice(program, row)) &&
                row->opcode != IR_OPCODE_STACK_SAVE && row->opcode != IR_OPCODE_STACK_RESTORE &&
                row->opcode != IR_OPCODE_RETURN_ADDRESS && row->opcode != IR_OPCODE_LABEL_ADDRESS &&
                row->opcode != IR_OPCODE_INDIRECT_BRANCH && row->opcode != IR_OPCODE_STACK_ALLOCATE &&
                row->opcode != IR_OPCODE_VA_START &&
                row->opcode != IR_OPCODE_VA_COPY && row->opcode != IR_OPCODE_VA_END && row->opcode != IR_OPCODE_VA_ARG &&
                !(row->opcode == IR_OPCODE_INLINE_ASSEMBLY && row->target_count != 0);
        if (row->operand_count && !row->operands) valid = false;
        if (row->target_count && !row->targets) valid = false;
        if (row->immediate_count && !row->immediates) valid = false;
        for (u32 target = 0; valid && target < row->target_count; target += 1)
        {
            if (row->targets[target].value >= callee->block_count) valid = false;
        }
        if (valid && copied != UINT32_MAX) copied += 1;
        else if (valid) valid = false;
    }
    for (u32 i = 0; valid && i < argument_count; i += 1) valid = seen_arguments[i];
    bool nonreturning_no_result = return_count == 0 && signature && signature->is_noreturn &&
                                  call && call->result.value == IR_ID_UNDERLYING_INVALID;
    valid = valid && (return_count != 0 || nonreturning_no_result);
    if (valid && (callee->block_count == UINT32_MAX ||
                  caller->block_count > UINT32_MAX - callee->block_count - 1)) valid = false;
    if (valid)
    {
        u64 edges = (u64)return_count + 1;
        for (u32 b = 0; b < caller->block_count; b += 1)
            edges += caller->instructions[caller->blocks[b].last_instruction.value].target_count;
        for (u32 b = 0; b < callee->block_count; b += 1)
            edges += callee->instructions[callee->blocks[b].last_instruction.value].target_count;
        if (edges > UINT32_MAX) valid = false;
    }
    if (valid && caller->instruction_count > UINT32_MAX - copied) valid = false;
    bool has_result = valid && call->result.value != IR_ID_UNDERLYING_INVALID;
    u32 aggregate_slot = has_result && (return_type->kind == IR_TYPE_ARRAY || return_type->kind == IR_TYPE_STRUCT ||
                                        return_type->kind == IR_TYPE_UNION || return_type->kind == IR_TYPE_SLICE);
    if (valid && (callee->value_count > UINT32_MAX - caller->value_count ||
                  (aggregate_slot && callee->value_count == UINT32_MAX - caller->value_count))) valid = false;
    if (valid && aggregate_slot &&
        (caller->local_count > UINT32_MAX - callee->local_count ||
         caller->local_count + callee->local_count == UINT32_MAX)) valid = false;
    u32 scope = 0;
    for (u32 i = 0; valid && i < caller->debug_local_count; i += 1)
    {
        if (caller->debug_locals[i].scope_depth > scope) scope = caller->debug_locals[i].scope_depth;
    }
    if (valid && scope == UINT32_MAX) valid = false;
    for (u32 i = 0; valid && i < callee->debug_local_count; i += 1)
    {
        if (callee->debug_locals[i].scope_depth > UINT32_MAX - scope - 1) valid = false;
    }
    if (valid && aggregate_slot)
    {
        if (return_count > UINT32_MAX - 2 || return_count + 2 > UINT32_MAX - copied ||
            copied + return_count + 2 > UINT32_MAX - caller->instruction_count) valid = false;
        else copied += return_count + 2;
    }
    if (valid)
    {
        if (copied_rows_out) *copied_rows_out = copied;
        if (growth_out) *growth_out = copied;
    }
    if (scratch.arena) scratch_end(scratch);
    return valid;
}

BUSTER_GLOBAL_LOCAL void ir_inline_cfg_rebuild_predecessors(Arena* arena, IrFunction* function)
{
    u64 maximum = 0;
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        function->blocks[block].first_predecessor = 0;
        function->blocks[block].last_predecessor = 0;
        function->blocks[block].predecessor_count = 0;
        IrBlock* source = function->blocks + block;
        IrInstruction const* tail = source->last_instruction.value < function->instruction_count
                                        ? function->instructions + source->last_instruction.value : 0;
        BUSTER_CHECK(tail && ir_instruction_is_terminator(tail));
        if (tail) maximum += tail->target_count;
    }
    BUSTER_CHECK(maximum <= UINT32_MAX);
    IrPredecessor* rows = arena_allocate_zeroed(arena, IrPredecessor, (u32)maximum);
    Arena* conflicts[] = {arena};
    TemporalArena scratch = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
    u32* seen = arena_allocate_zeroed(scratch.arena, u32, function->block_count);
    u32 cursor = 0;
    for (u32 source_id = 0; source_id < function->block_count; source_id += 1)
    {
        IrBlock* source = function->blocks + source_id;
        IrInstruction const* tail = function->instructions + source->last_instruction.value;
        for (u32 i = 0; i < tail->target_count; i += 1)
        {
            u32 target = tail->targets[i].value;
            BUSTER_CHECK(target < function->block_count);
            if (target >= function->block_count || seen[target] == source_id + 1) continue;
            seen[target] = source_id + 1;
            IrBlock* destination = function->blocks + target;
            IrPredecessor* predecessor = rows + cursor++;
            predecessor->block = source->id;
            if (destination->last_predecessor) destination->last_predecessor->next = predecessor;
            else destination->first_predecessor = predecessor;
            destination->last_predecessor = predecessor;
            destination->predecessor_count += 1;
        }
    }
    scratch_end(scratch);
}

BUSTER_GLOBAL_LOCAL void ir_inline_cfg_copy_parameters(Arena* arena, IrFunction* caller, IrFunction const* callee,
                                          IrBlockId source_id, IrBlockId destination_id,
                                          IrBlockId const* block_map, IrValueId const* value_map, u32 local_base)
{
    IrBlock const* source = callee->blocks + source_id.value;
    IrBlock* destination = caller->blocks + destination_id.value;
    IrBlockParameter* previous = 0;
    for (IrBlockParameter const* parameter = source->first_parameter; parameter; parameter = parameter->next)
    {
        IrBlockParameter* copy = arena_allocate_zeroed(arena, IrBlockParameter, 1);
        *copy = *parameter;
        copy->next = 0;
        copy->first_incoming = 0;
        copy->last_incoming = 0;
        copy->incoming_count = 0;
        copy->value = value_map[parameter->value.value];
        copy->canonical_local = ir_inline_cfg_local(parameter->canonical_local, local_base);
        for (IrIncoming const* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
        {
            IrIncoming* edge = arena_allocate(arena, IrIncoming, 1);
            edge->predecessor = block_map[incoming->predecessor.value];
            edge->value = value_map[incoming->value.value];
            edge->next = 0;
            if (copy->last_incoming) copy->last_incoming->next = edge;
            else copy->first_incoming = edge;
            copy->last_incoming = edge;
            copy->incoming_count += 1;
        }
        if (previous) previous->next = copy;
        else destination->first_parameter = copy;
        previous = copy;
        destination->last_parameter = copy;
        destination->parameter_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL IrValueId* ir_inline_cfg_local_values(Arena* arena, IrFunction const* callee,
                                             IrBlock const* source, IrBlock const* call_site,
                                             IrValueId const* value_map, u32 old_locals, u32 new_locals)
{
    IrValueId* result = 0;
    if (source->local_values || call_site->local_values)
    {
        result = arena_allocate(arena, IrValueId, new_locals);
        for (u32 i = 0; i < new_locals; i += 1) result[i] = IR_VALUE_ID_INVALID;
        if (call_site->local_values)
        {
            for (u32 i = 0; i < old_locals; i += 1) result[i] = call_site->local_values[i];
        }
        if (source->local_values)
        {
            for (u32 i = 0; i < callee->local_count; i += 1)
            {
                IrValueId value = source->local_values[i];
                if (value.value != IR_ID_UNDERLYING_INVALID) result[old_locals + i] = value_map[value.value];
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_cfg_call(Arena* arena, IrProgram* program, IrFunction* caller, IrBlockId call_block,
                               IrInstructionId call_id, IrFunction const* callee, u32* copied_rows_out)
{
    u32 copied = 0;
    u32 growth = 0;
    bool valid = arena && ir_inline_cfg_supported(program, caller, call_block, call_id, callee, &copied, &growth);
    if (valid)
    {
    IrInstruction call = caller->instructions[call_id.value];
    IrType* signature = ir_type_from_id(&program->types, callee->canonical_type);
    IrType* return_type = ir_type_from_id(&program->types, signature->return_type);
    IrTypeId void_type = ir_inline_cfg_void_type(program);
    u32 old_block_count = caller->block_count;
    u32 old_local_count = caller->local_count;
    u32 old_instruction_count = caller->instruction_count;
    u32 old_debug_count = caller->debug_local_count;
    IrBlock original = caller->blocks[call_block.value];
    IrInstructionId suffix = caller->instructions[call_id.value].next;
    IrInstructionId original_last = original.last_instruction;
    bool has_result = call.result.value != IR_ID_UNDERLYING_INVALID;
    bool aggregate_slot = has_result && (return_type->kind == IR_TYPE_ARRAY || return_type->kind == IR_TYPE_STRUCT ||
                          return_type->kind == IR_TYPE_UNION || return_type->kind == IR_TYPE_SLICE);
    u32 new_local_count = old_local_count + callee->local_count + (aggregate_slot ? 1u : 0u);
    u32 new_debug_count = old_debug_count + callee->debug_local_count;
    u32 scope_base = 0;
    for (u32 i = 0; i < old_debug_count; i += 1)
        if (caller->debug_locals[i].scope_depth > scope_base) scope_base = caller->debug_locals[i].scope_depth;
    scope_base += 1;

    // Widen local snapshots before publishing the larger local-id universe.
    for (u32 b = 0; b < old_block_count; b += 1)
    {
        IrBlock* block = caller->blocks + b;
        if (block->local_values)
        {
            IrValueId* values = arena_allocate(arena, IrValueId, new_local_count);
            memcpy(values, block->local_values, sizeof(*values) * old_local_count);
            for (u32 i = old_local_count; i < new_local_count; i += 1) values[i] = IR_VALUE_ID_INVALID;
            block->local_values = values;
        }
    }
    IrValueId* places = arena_allocate(arena, IrValueId, new_local_count);
    bool* memory = arena_allocate_zeroed(arena, bool, new_local_count);
    for (u32 i = 0; i < old_local_count; i += 1)
    {
        places[i] = caller->local_places ? caller->local_places[i] : IR_VALUE_ID_INVALID;
        memory[i] = caller->local_uses_memory ? caller->local_uses_memory[i] : false;
    }
    caller->local_places = places;
    caller->local_uses_memory = memory;

    if (new_debug_count)
    {
        IrDebugLocal* debug = arena_allocate(arena, IrDebugLocal, new_debug_count);
        if (old_debug_count) memcpy(debug, caller->debug_locals, sizeof(*debug) * old_debug_count);
        caller->debug_locals = debug;
    }
    caller->local_count = new_local_count;

    IrValueId* value_map = arena_allocate(arena, IrValueId, callee->value_count);
    for (u32 i = 0; i < callee->value_count; i += 1) value_map[i] = IR_VALUE_ID_INVALID;
    for (u32 i = 0; i < callee->instruction_count; i += 1)
    {
        IrInstruction const* row = callee->instructions + i;
        if (row->opcode == IR_OPCODE_ARGUMENT)
        {
            u32 argument = (u32)row->immediates[0];
            value_map[row->result.value] = call.operands[argument + 1];
        }
    }
    for (u32 i = 0; i < callee->value_count; i += 1)
    {
        if (value_map[i].value == IR_ID_UNDERLYING_INVALID)
            value_map[i] = ir_function_add_value(arena, caller, callee->values[i]);
    }
    for (u32 i = 0; i < callee->local_count; i += 1)
    {
        caller->local_places[old_local_count + i] =
            callee->local_places && callee->local_places[i].value < callee->value_count
                ? value_map[callee->local_places[i].value] : IR_VALUE_ID_INVALID;
        caller->local_uses_memory[old_local_count + i] =
            callee->local_uses_memory ? callee->local_uses_memory[i] : false;
    }
    IrValueId aggregate_place = IR_VALUE_ID_INVALID;
    u32 aggregate_local = old_local_count + callee->local_count;
    if (aggregate_slot)
    {
        IrValue place = {
            .canonical_type = signature->return_type,
            .definition = IR_INSTRUCTION_ID_INVALID,
            .category = IR_VALUE_PLACE,
        };
        aggregate_place = ir_function_add_value(arena, caller, place);
        caller->local_places[aggregate_local] = aggregate_place;
        caller->local_uses_memory[aggregate_local] = true;
    }

    IrBlockId* block_map = arena_allocate(arena, IrBlockId, callee->block_count);
    for (u32 b = 0; b < callee->block_count; b += 1)
    {
        block_map[b] = (IrBlockId){.value = old_block_count + b};
        IrBlock block = callee->blocks[b];
        block.id = block_map[b];
        block.first_parameter = 0;
        block.last_parameter = 0;
        block.parameter_count = 0;
        block.first_predecessor = 0;
        block.last_predecessor = 0;
        block.predecessor_count = 0;
        block.first_instruction = IR_INSTRUCTION_ID_INVALID;
        block.last_instruction = IR_INSTRUCTION_ID_INVALID;
        block.local_values = ir_inline_cfg_local_values(arena, callee, callee->blocks + b,
                                                        caller->blocks + call_block.value, value_map,
                                                        old_local_count, new_local_count);
        ir_function_add_block(arena, caller, block);
    }
    IrBlockId continuation_id = {.value = caller->block_count};
    IrBlock continuation = original;
    continuation.id = continuation_id;
    continuation.first_parameter = 0;
    continuation.last_parameter = 0;
    continuation.parameter_count = 0;
    continuation.first_predecessor = 0;
    continuation.last_predecessor = 0;
    continuation.predecessor_count = 0;
    continuation.first_instruction = suffix;
    continuation.last_instruction = original_last;
    continuation.local_values = ir_inline_cfg_local_values(arena, callee,
                                                           &(IrBlock){0}, caller->blocks + call_block.value,
                                                           value_map, old_local_count, new_local_count);
    ir_function_add_block(arena, caller, continuation);

    // Clone every non-formal row, then rewire row and block chains by ID.
    IrInstructionId* instruction_map = arena_allocate(arena, IrInstructionId, callee->instruction_count);
    for (u32 i = 0; i < callee->instruction_count; i += 1) instruction_map[i] = IR_INSTRUCTION_ID_INVALID;
    for (u32 i = 0; i < callee->instruction_count; i += 1)
    {
        IrInstruction const* source = callee->instructions + i;
        if (source->opcode == IR_OPCODE_ARGUMENT) continue;
        IrInstruction row = *source;
        row.next = IR_INSTRUCTION_ID_INVALID;
        row.result = source->result.value == IR_ID_UNDERLYING_INVALID
                         ? IR_VALUE_ID_INVALID : value_map[source->result.value];
        row.canonical_local = ir_inline_cfg_local(source->canonical_local, old_local_count);
        if (source->operand_count)
        {
            row.operands = arena_allocate(arena, IrValueId, source->operand_count);
            for (u32 operand = 0; operand < source->operand_count; operand += 1)
                row.operands[operand] = value_map[source->operands[operand].value];
        }
        if (source->target_count)
        {
            row.targets = arena_allocate(arena, IrBlockId, source->target_count);
            for (u32 target = 0; target < source->target_count; target += 1)
                row.targets[target] = block_map[source->targets[target].value];
        }
        if (source->immediate_count)
        {
            row.immediates = arena_allocate(arena, u64, source->immediate_count);
            memcpy(row.immediates, source->immediates, sizeof(u64) * source->immediate_count);
        }
        if (source->opcode == IR_OPCODE_RETURN)
        {
            if (aggregate_slot)
            {
                row = (IrInstruction){
                    .operands = arena_allocate(arena, IrValueId, 2),
                    .canonical_type = void_type,
                    .symbol = IR_SYMBOL_ID_INVALID,
                    .canonical_local = IR_LOCAL_ID_INVALID,
                    .next = IR_INSTRUCTION_ID_INVALID,
                    .result = IR_VALUE_ID_INVALID,
                    .operand_count = 2,
                    .opcode = IR_OPCODE_STORE,
                };
                row.operands[0] = aggregate_place;
                row.operands[1] = value_map[source->operands[0].value];
            }
            else
            {
                row = (IrInstruction){
                    .targets = arena_allocate(arena, IrBlockId, 1),
                    .canonical_type = void_type,
                .symbol = IR_SYMBOL_ID_INVALID,
                .canonical_local = IR_LOCAL_ID_INVALID,
                .next = IR_INSTRUCTION_ID_INVALID,
                .result = IR_VALUE_ID_INVALID,
                .operand_count = 0,
                .target_count = 1,
                    .opcode = IR_OPCODE_BRANCH,
                };
                row.targets[0] = continuation_id;
            }
        }
        IrSourceRange source_range = ir_instruction_canonical_source((IrFunction*)callee, (IrInstructionId){.value = i});
        IrInstructionId added = ir_function_add_instruction(arena, caller, row, source_range);
        instruction_map[i] = added;
        if (row.result.value != IR_ID_UNDERLYING_INVALID) caller->values[row.result.value].definition = added;
        if (source->opcode == IR_OPCODE_RETURN && aggregate_slot)
        {
            IrInstruction branch_row = {
                .targets = arena_allocate(arena, IrBlockId, 1),
                .canonical_type = void_type,
                .symbol = IR_SYMBOL_ID_INVALID,
                .canonical_local = IR_LOCAL_ID_INVALID,
                .next = IR_INSTRUCTION_ID_INVALID,
                .result = IR_VALUE_ID_INVALID,
                .target_count = 1,
                .opcode = IR_OPCODE_BRANCH,
            };
            branch_row.targets[0] = continuation_id;
            IrInstructionId branch_id = ir_function_add_instruction(arena, caller, branch_row, source_range);
            caller->instructions[added.value].next = branch_id;
        }
        IrInstructionExtra extra = ir_instruction_extra((IrFunction*)callee, (IrInstructionId){.value = i});
        if (extra.label_name_count || extra.operand_name_count || extra.clobber_count || extra.literal.length)
            *ir_instruction_extra_ensure(arena, caller, added) = extra;
    }

    // Attach cloned block parameters and instruction chains.
    for (u32 b = 0; b < callee->block_count; b += 1)
    {
        IrBlock const* source = callee->blocks + b;
        IrBlock* destination = caller->blocks + block_map[b].value;
        ir_inline_cfg_copy_parameters(arena, caller, callee, (IrBlockId){.value = b},
                                      block_map[b], block_map, value_map, old_local_count);
        IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
        for (IrInstructionId row = source->first_instruction;
             row.value < callee->instruction_count; row = callee->instructions[row.value].next)
        {
            IrInstructionId mapped = instruction_map[row.value];
            if (mapped.value != IR_ID_UNDERLYING_INVALID)
            {
                if (previous.value == IR_ID_UNDERLYING_INVALID) destination->first_instruction = mapped;
                else caller->instructions[previous.value].next = mapped;
                previous = mapped;
                if (callee->instructions[row.value].opcode == IR_OPCODE_RETURN && aggregate_slot)
                    previous = caller->instructions[mapped.value].next;
            }
            if (row.value == source->last_instruction.value) break;
        }
        destination->last_instruction = previous;
        destination->terminated = source->terminated;
    }

    // Turn the old call row into the entry edge and move its suffix to the
    // continuation. The original call result becomes a continuation parameter.
    IrInstruction branch = {
        .targets = arena_allocate(arena, IrBlockId, 1),
        .canonical_type = void_type,
        .symbol = IR_SYMBOL_ID_INVALID,
        .canonical_local = IR_LOCAL_ID_INVALID,
        .next = IR_INSTRUCTION_ID_INVALID,
        .result = IR_VALUE_ID_INVALID,
        .target_count = 1,
        .opcode = IR_OPCODE_BRANCH,
    };
    branch.targets[0] = block_map[callee->entry.value];
    caller->instructions[call_id.value] = branch;
    caller->blocks[call_block.value].last_instruction = call_id;
    caller->blocks[call_block.value].terminated = true;
    IrInstructionExtra* old_extra = ir_instruction_extra_find(caller, call_id);
    if (old_extra) memset(old_extra, 0, sizeof(*old_extra));

    if (aggregate_slot)
    {
        IrSourceRange call_source = ir_instruction_canonical_source(caller, call_id);
        IrInstruction local_row = {
            .canonical_type = signature->return_type,
            .canonical_local = {.value = aggregate_local},
            .next = IR_INSTRUCTION_ID_INVALID,
            .result = aggregate_place,
            .opcode = IR_OPCODE_LOCAL,
        };
        IrInstructionId local_id = ir_function_add_instruction(arena, caller, local_row, call_source);
        IrBlock* prefix = caller->blocks + call_block.value;
        IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
        for (IrInstructionId row = prefix->first_instruction;
             row.value < caller->instruction_count && row.value != call_id.value;
             row = caller->instructions[row.value].next) previous = row;
        caller->instructions[local_id.value].next = call_id;
        if (previous.value == IR_ID_UNDERLYING_INVALID) prefix->first_instruction = local_id;
        else caller->instructions[previous.value].next = local_id;
        caller->values[aggregate_place.value].definition = local_id;

        IrInstruction load_row = {
            .operands = arena_allocate(arena, IrValueId, 1),
            .canonical_type = signature->return_type,
            .canonical_local = IR_LOCAL_ID_INVALID,
            .next = suffix,
            .result = call.result,
            .operand_count = 1,
            .opcode = IR_OPCODE_LOAD,
        };
        load_row.operands[0] = aggregate_place;
        IrInstructionId load_id = ir_function_add_instruction(arena, caller, load_row, call_source);
        caller->instructions[load_id.value].next = suffix;
        caller->blocks[continuation_id.value].first_instruction = load_id;
        caller->values[call.result.value].definition = load_id;
    }

    if (has_result && return_type->kind != IR_TYPE_VOID && !aggregate_slot)
    {
        IrValueId result = call.result;
        caller->values[result.value].definition = IR_INSTRUCTION_ID_INVALID;
        IrBlockParameter* parameter = arena_allocate_zeroed(arena, IrBlockParameter, 1);
        parameter->canonical_type = signature->return_type;
        parameter->canonical_local = IR_LOCAL_ID_INVALID;
        parameter->value = result;
        IrIncoming* last = 0;
        for (u32 b = 0; b < callee->block_count; b += 1)
        {
            IrBlock const* source = callee->blocks + b;
            for (IrInstructionId row = source->first_instruction;
                 row.value < callee->instruction_count; row = callee->instructions[row.value].next)
            {
                if (callee->instructions[row.value].opcode == IR_OPCODE_RETURN)
                {
                    IrIncoming* incoming = arena_allocate_zeroed(arena, IrIncoming, 1);
                    incoming->predecessor = block_map[b];
                    incoming->value = value_map[callee->instructions[row.value].operands[0].value];
                    if (last) last->next = incoming;
                    else parameter->first_incoming = incoming;
                    last = incoming;
                    parameter->incoming_count += 1;
                }
                if (row.value == source->last_instruction.value) break;
            }
        }
        parameter->last_incoming = last;
        caller->blocks[continuation_id.value].first_parameter = parameter;
        caller->blocks[continuation_id.value].last_parameter = parameter;
        caller->blocks[continuation_id.value].parameter_count = 1;
    }

    // Existing outgoing edge arguments now originate at the continuation.
    for (u32 b = 0; b < old_block_count; b += 1)
    {
        for (IrBlockParameter* parameter = caller->blocks[b].first_parameter; parameter; parameter = parameter->next)
        {
            IrIncoming* new_first = 0;
            IrIncoming* new_last = 0;
            IrIncoming* moved_first = 0;
            IrIncoming* moved_last = 0;
            IrIncoming* current = parameter->first_incoming;
            while (current)
            {
                IrIncoming* next = current->next;
                current->next = 0;
                if (current->predecessor.value == call_block.value)
                {
                    current->predecessor = continuation_id;
                    if (moved_last) moved_last->next = current;
                    else moved_first = current;
                    moved_last = current;
                }
                else
                {
                    if (new_last) new_last->next = current;
                    else new_first = current;
                    new_last = current;
                }
                current = next;
            }
            if (moved_first)
            {
                if (new_last) new_last->next = moved_first;
                else new_first = moved_first;
                new_last = moved_last;
            }
            parameter->first_incoming = new_first;
            parameter->last_incoming = new_last;
        }
    }

    caller->debug_local_count = new_debug_count;
    for (u32 i = 0; i < callee->debug_local_count; i += 1)
    {
        IrDebugLocal local = callee->debug_locals[i];
        local.id.value += old_local_count;
        local.scope_depth += scope_base;
        local.is_parameter = false;
        caller->debug_locals[old_debug_count + i] = local;
    }
    ir_inline_cfg_rebuild_predecessors(arena, caller);
    if (copied_rows_out) *copied_rows_out = copied;
    (void)old_instruction_count;
    (void)growth;
    valid = true;
    }
    return valid;
}
