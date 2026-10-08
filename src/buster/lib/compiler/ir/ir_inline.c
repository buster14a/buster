// Bounded direct canonical inlining. Included after ir_inline_cfg.c and
// ir_promote.c; the CFG helper owns block/value/local cloning, while this file
// owns candidate policy, recursion bounds, deterministic order and budgets.
#define IR_INLINE_NONE UINT32_MAX

typedef struct IrInlinePlan IrInlinePlan;
struct IrInlinePlan
{
    u32* function_by_symbol;
    u32* order;
    u32* reverse_offsets;
    u32* reverse_callers;
    u32* remaining;
    u32* queue;
    u8* cyclic;
};

BUSTER_GLOBAL_LOCAL u64 ir_inline_storage_add(u64 left, u64 right)
{
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

BUSTER_GLOBAL_LOCAL u64 ir_inline_storage_multiply(u64 left, u64 right)
{
    return left && right > UINT64_MAX / left ? UINT64_MAX : left * right;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_inline_required(IrFunction* function, IrBlockId block, IrInstructionId instruction)
{
    IrValidationResult result = ir_validation_error(IR_VALIDATION_INLINE_REQUIRED, function, block, instruction);
    result.boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_direct_call(IrFunction* caller, IrInstruction const* call)
{
    bool result = call->opcode == IR_OPCODE_CALL && call->symbol.value != IR_INLINE_NONE && call->operand_count && call->operands &&
                  call->operands[0].value < caller->value_count;
    if (result)
    {
        IrInstructionId definition = caller->values[call->operands[0].value].definition;
        IrInstruction* reference = definition.value < caller->instruction_count ? caller->instructions + definition.value : 0;
        result = reference && reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == call->symbol.value;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_linkage_allowed(IrProgram* program, IrSymbol* symbol, IrFunction* callee)
{
    bool external_interposable = program && program->external_function_definitions_interposable && ir_symbol_is_interposable(symbol);
    bool result = program && symbol && callee && symbol->kind == IR_SYMBOL_FUNCTION && symbol->is_definition && !symbol->is_weak && !external_interposable &&
                  callee->state == IR_FUNCTION_LOWERED && callee->canonical_type.value == symbol->type.value &&
                  !symbol->is_returns_twice;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_inline_tiny_leaf(IrFunction* callee, u32 instruction_limit)
{
    bool result = callee && callee->block_count == 1 && callee->entry.value == 0 && callee->label_metadata_count == 0 &&
                  callee->blocks[0].parameter_count == 0;
    u32 count = 0;
    bool returned = false;
    if (result)
    {
        for (u32 row_id = callee->blocks[0].first_instruction.value; row_id != IR_INLINE_NONE; row_id = callee->instructions[row_id].next.value)
        {
            if (row_id >= callee->instruction_count)
            {
                result = false;
                break;
            }
            IrInstruction* row = callee->instructions + row_id;
            count += 1;
            if (row->opcode == IR_OPCODE_RETURN)
            {
                returned = row_id == callee->blocks[0].last_instruction.value && row->next.value == IR_INLINE_NONE;
            }
            else if (row->opcode == IR_OPCODE_CALL || row->opcode == IR_OPCODE_STACK_ALLOCATE ||
                     row->opcode == IR_OPCODE_STACK_SAVE || row->opcode == IR_OPCODE_STACK_RESTORE || row->opcode == IR_OPCODE_RETURN_ADDRESS ||
                     row->opcode == IR_OPCODE_VA_START || row->opcode == IR_OPCODE_VA_COPY || row->opcode == IR_OPCODE_VA_END ||
                     row->opcode == IR_OPCODE_VA_ARG || row->opcode == IR_OPCODE_INLINE_ASSEMBLY || row->opcode == IR_OPCODE_LABEL_ADDRESS ||
                     row->opcode == IR_OPCODE_INDIRECT_BRANCH || row->opcode == IR_OPCODE_BRANCH || row->opcode == IR_OPCODE_BRANCH_IF ||
                     row->opcode == IR_OPCODE_SWITCH || row->opcode == IR_OPCODE_UNREACHABLE)
            {
                result = false;
            }
        }
    }
    result &= returned && count <= instruction_limit;
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_inline_callsite_error(IrFunction* caller, u32 block, u32 row)
{
    return ir_inline_required(caller, (IrBlockId){.value = block}, (IrInstructionId){.value = row});
}

BUSTER_GLOBAL_LOCAL void ir_inline_plan_graph(IrProgram* program, IrModule* module, Arena* arena, IrInlinePlan* plan, u32* order_count_out)
{
    u32 count = module->function_count;
    plan->function_by_symbol = arena_allocate(arena, u32, program->symbols.count ? program->symbols.count : 1u);
    plan->order = arena_allocate(arena, u32, count ? count : 1u);
    plan->reverse_offsets = arena_allocate(arena, u32, (u64)count + 1);
    plan->remaining = arena_allocate(arena, u32, count ? count : 1u);
    plan->queue = arena_allocate(arena, u32, count ? count : 1u);
    plan->cyclic = arena_allocate(arena, u8, count ? count : 1u);
    memset(plan->reverse_offsets, 0, sizeof(u32) * ((u64)count + 1));
    memset(plan->remaining, 0, sizeof(u32) * (count ? count : 1u));
    memset(plan->cyclic, 0, count ? count : 1u);
    for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1) plan->function_by_symbol[symbol] = IR_INLINE_NONE;
    for (u32 function = 0; function < count; function += 1)
    {
        IrFunction* row = module->functions + function;
        if (row->symbol.value < program->symbols.count) plan->function_by_symbol[row->symbol.value] = function;
    }
    u64 edge_count = 0;
    for (u32 function = 0; function < count; function += 1)
    {
        IrFunction* caller = module->functions + function;
        if (caller->state != IR_FUNCTION_LOWERED) continue;
        for (u32 block = 0; block < caller->block_count; block += 1)
        {
            for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE; row = caller->instructions[row].next.value)
            {
                IrInstruction* call = caller->instructions + row;
                IrSymbol* symbol = call->opcode == IR_OPCODE_CALL ? ir_symbol_from_id(&program->symbols, call->symbol) : 0;
                if (symbol && symbol->always_inline && call->symbol.value < program->symbols.count)
                {
                    u32 callee = plan->function_by_symbol[call->symbol.value];
                    if (callee < count)
                    {
                        plan->remaining[function] += 1;
                        plan->reverse_offsets[callee + 1] += 1;
                        edge_count += 1;
                    }
                }
            }
        }
    }
    for (u32 index = 0; index < count; index += 1) plan->reverse_offsets[index + 1] += plan->reverse_offsets[index];
    plan->reverse_callers = arena_allocate(arena, u32, edge_count ? edge_count : 1u);
    u32* cursor = arena_allocate(arena, u32, count ? count : 1u);
    if (count) memcpy(cursor, plan->reverse_offsets, sizeof(u32) * count);
    for (u32 function = 0; function < count; function += 1)
    {
        IrFunction* caller = module->functions + function;
        if (caller->state != IR_FUNCTION_LOWERED) continue;
        for (u32 block = 0; block < caller->block_count; block += 1)
        {
            for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE; row = caller->instructions[row].next.value)
            {
                IrInstruction* call = caller->instructions + row;
                IrSymbol* symbol = call->opcode == IR_OPCODE_CALL ? ir_symbol_from_id(&program->symbols, call->symbol) : 0;
                if (symbol && symbol->always_inline && call->symbol.value < program->symbols.count)
                {
                    u32 callee = plan->function_by_symbol[call->symbol.value];
                    if (callee < count) plan->reverse_callers[cursor[callee]++] = function;
                }
            }
        }
    }
    u32 head = 0;
    u32 tail = 0;
    for (u32 function = 0; function < count; function += 1)
    {
        if (!plan->remaining[function]) plan->queue[tail++] = function;
    }
    u32 order_count = 0;
    while (head < tail)
    {
        u32 callee = plan->queue[head++];
        plan->order[order_count++] = callee;
        for (u32 edge = plan->reverse_offsets[callee]; edge < plan->reverse_offsets[callee + 1]; edge += 1)
        {
            u32 caller = plan->reverse_callers[edge];
            if (plan->remaining[caller]) plan->remaining[caller] -= 1;
            if (!plan->remaining[caller]) plan->queue[tail++] = caller;
        }
    }
    for (u32 function = 0; function < count; function += 1) plan->cyclic[function] = plan->remaining[function] != 0;
    *order_count_out = order_count;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_inline_module(IrProgram* program, IrModule* module)
{
    IrValidationResult result = (IrValidationResult){.error = IR_VALIDATION_NONE, .boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT};
    if (!program || !program->arena || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    else if (!module->inline_complete)
    {
        module->inlining = (IrInlineStatistics){0};
        IrInlineOptions options = program->inline_options;
        if (!options.max_callee_instructions) options.max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS;
        if (!options.max_function_growth) options.max_function_growth = IR_INLINE_FUNCTION_GROWTH;
        if (!options.max_module_growth) options.max_module_growth = IR_INLINE_MODULE_GROWTH;
        if (!options.max_call_sites) options.max_call_sites = IR_INLINE_CALL_SITES;
        program->inline_options = options;
        bool has_required = false;
        u64 planning_units = ir_inline_storage_add(module->function_count, program->symbols.count);
        u64 instruction_units = 0;
        u64 max_function_scratch = 0;
        for (u32 function = 0; function < module->function_count; function += 1)
        {
            IrFunction* candidate = module->functions + function;
            planning_units = ir_inline_storage_add(planning_units, (u64)candidate->instruction_count + candidate->value_count + candidate->block_count);
            for (u32 row_id = 0; row_id < candidate->instruction_count; row_id += 1)
            {
                IrInstruction const* row = candidate->instructions + row_id;
                planning_units = ir_inline_storage_add(planning_units, (u64)row->operand_count + row->target_count + row->immediate_count);
            }
            instruction_units = ir_inline_storage_add(instruction_units, candidate->instruction_count);
            u64 function_scratch = (u64)candidate->instruction_count * 10u +
                                   (u64)candidate->value_count * 4u + (u64)candidate->block_count * 4u;
            max_function_scratch = BUSTER_MAX(max_function_scratch, function_scratch);
            if (candidate->state != IR_FUNCTION_LOWERED) continue;
            for (u32 block = 0; block < candidate->block_count; block += 1)
            {
                for (u32 row = candidate->blocks[block].first_instruction.value; row != IR_INLINE_NONE;
                     row = candidate->instructions[row].next.value)
                {
                    IrInstruction* call = candidate->instructions + row;
                    IrSymbol* symbol = call->opcode == IR_OPCODE_CALL ? ir_symbol_from_id(&program->symbols, call->symbol) : 0;
                    has_required |= symbol && symbol->always_inline;
                }
            }
        }
        if (!has_required && !options.tiny)
        {
            module->inline_complete = true;
        }
        else
        {
            u64 planning_scratch = ir_inline_storage_multiply(program->symbols.count, sizeof(u32));
            planning_scratch = ir_inline_storage_add(planning_scratch,
                                                      ir_inline_storage_multiply(module->function_count, 40u));
            planning_scratch = ir_inline_storage_add(planning_scratch,
                                                      ir_inline_storage_multiply(instruction_units, sizeof(u32)));
            planning_scratch = ir_inline_storage_add(planning_scratch, max_function_scratch);
            if (planning_units > IR_FAST_WORK_BUDGET || planning_scratch > IR_FAST_SCRATCH_BUDGET)
            {
                module->inlining.visits = planning_units;
                if (has_required)
                {
                    for (u32 function = 0; function < module->function_count && result.error == IR_VALIDATION_NONE; function += 1)
                    {
                        IrFunction* caller = module->functions + function;
                        if (caller->state != IR_FUNCTION_LOWERED) continue;
                        for (u32 block = 0; block < caller->block_count && result.error == IR_VALIDATION_NONE; block += 1)
                        {
                            for (u32 row = caller->blocks[block].first_instruction.value;
                                 row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE; row = caller->instructions[row].next.value)
                            {
                                IrInstruction* call = caller->instructions + row;
                                IrSymbol* symbol = call->opcode == IR_OPCODE_CALL ? ir_symbol_from_id(&program->symbols, call->symbol) : 0;
                                if (symbol && symbol->always_inline)
                                {
                                    module->inlining.budget_skips += 1;
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
                            }
                        }
                    }
                }
                else
                {
                    module->inlining.budget_skips += 1;
                    module->inline_complete = true;
                }
            }
            else
            {
                TemporalArena planning = scratch_begin(&program->arena, 1);
                Arena* arena = planning.arena;
                IrInlinePlan plan = {0};
                u32 order_count = 0;
                ir_inline_plan_graph(program, module, arena, &plan, &order_count);
                u32 count = module->function_count;
                for (u32 function = 0; function < count && result.error == IR_VALIDATION_NONE; function += 1)
                {
                    if (!plan.cyclic[function]) continue;
                    IrFunction* caller = module->functions + function;
                    for (u32 block = 0; block < caller->block_count && result.error == IR_VALIDATION_NONE; block += 1)
                    {
                        for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE;
                             row = caller->instructions[row].next.value)
                        {
                            IrInstruction* call = caller->instructions + row;
                            IrSymbol* symbol = call->opcode == IR_OPCODE_CALL ? ir_symbol_from_id(&program->symbols, call->symbol) : 0;
                            u32 callee = call->symbol.value < program->symbols.count ? plan.function_by_symbol[call->symbol.value] : IR_INLINE_NONE;
                            if (symbol && symbol->always_inline && callee < count && plan.cyclic[callee])
                            {
                                module->inlining.recursion_skips += 1;
                                result = ir_inline_callsite_error(caller, block, row);
                            }
                        }
                    }
                }
                u64 module_growth = 0;
                u64 module_storage = 0;
                u64 total_work = 0;
                u64 total_copies = 0;
                for (u32 order_index = 0; order_index < order_count && result.error == IR_VALIDATION_NONE; order_index += 1)
                {
                    u32 caller_index = plan.order[order_index];
                    IrFunction* caller = module->functions + caller_index;
                    if (caller->state != IR_FUNCTION_LOWERED) continue;
                    TemporalArena function_scratch = scratch_begin(&program->arena, 1);
                    Arena* function_arena = function_scratch.arena;
                    u32 original_instructions = caller->instruction_count;
                    u32 original_blocks = caller->block_count;
                    u8* planned = arena_allocate(function_arena, u8, original_instructions ? original_instructions : 1u);
                    u32* planned_next = arena_allocate(function_arena, u32, original_instructions ? original_instructions : 1u);
                    u32* planned_heads = arena_allocate(function_arena, u32, original_blocks ? original_blocks : 1u);
                    memset(planned, 0, original_instructions ? original_instructions : 1u);
                    for (u32 row = 0; row < original_instructions; row += 1) planned_next[row] = IR_INLINE_NONE;
                    for (u32 block = 0; block < original_blocks; block += 1) planned_heads[block] = IR_INLINE_NONE;
                    u32 function_growth = 0;
                    u64 function_storage = 0;
                    u64 function_projected_units = 0;
                    u64 function_projected_blocks = 0;
                    u64 function_projected_locals = 0;
                    u32 function_sites = 0;
                    for (u32 block = 0; block < original_blocks && result.error == IR_VALIDATION_NONE; block += 1)
                    {
                        for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE;
                             row = caller->instructions[row].next.value)
                        {
                            total_work += 1;
                            module->inlining.visits += 1;
                            IrInstruction* call = caller->instructions + row;
                            if (call->opcode != IR_OPCODE_CALL) continue;
                            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, call->symbol);
                            bool required = symbol && symbol->always_inline;
                            bool tiny = options.tiny && symbol && !symbol->noinline && ir_inline_direct_call(caller, call);
                            if (!required && !tiny) continue;
                            module->inlining.candidates += 1;
                            u32 callee_index = call->symbol.value < program->symbols.count ? plan.function_by_symbol[call->symbol.value] : IR_INLINE_NONE;
                            IrFunction* callee = callee_index < count ? module->functions + callee_index : 0;
                            if (!ir_inline_direct_call(caller, call) || !ir_inline_linkage_allowed(program, symbol, callee) ||
                                (required && symbol->noinline))
                            {
                                module->inlining.linkage_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            if (callee_index == caller_index || (callee_index < count && plan.cyclic[callee_index]))
                            {
                                module->inlining.recursion_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            if (caller->label_metadata_count)
                            {
                                module->inlining.shape_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            u64 callee_payload_units = 0;
                            for (u32 row_id = 0; row_id < callee->instruction_count; row_id += 1)
                            {
                                IrInstruction const* source = callee->instructions + row_id;
                                callee_payload_units = ir_inline_storage_add(callee_payload_units, source->operand_count);
                                callee_payload_units = ir_inline_storage_add(callee_payload_units, source->target_count);
                                callee_payload_units = ir_inline_storage_add(callee_payload_units, source->immediate_count);
                            }
                            for (u32 block_id = 0; block_id < callee->block_count; block_id += 1)
                            {
                                for (IrBlockParameter const* parameter = callee->blocks[block_id].first_parameter; parameter;
                                     parameter = parameter->next)
                                {
                                    callee_payload_units = ir_inline_storage_add(callee_payload_units, 1u);
                                    callee_payload_units = ir_inline_storage_add(callee_payload_units, parameter->incoming_count);
                                }
                            }
                            u64 candidate_work = (u64)callee->instruction_count * 5u + callee->value_count +
                                                 (u64)callee->block_count * 3u + call->operand_count + callee_payload_units;
                            u64 caller_scan_work = (u64)caller->instruction_count * 2u + (u64)caller->block_count * 5u +
                                                   (u64)caller->debug_local_count * 2u;
                            caller_scan_work = ir_inline_storage_add(caller_scan_work, function_projected_units);
                            candidate_work = ir_inline_storage_add(candidate_work, caller_scan_work);
                            if (candidate_work > IR_FAST_WORK_BUDGET - BUSTER_MIN(total_work, (u64)IR_FAST_WORK_BUDGET))
                            {
                                module->inlining.budget_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            total_work += candidate_work;
                            u32 copied = 0;
                            u32 growth = 0;
                            bool shape = required ? ir_inline_cfg_supported(program, caller, (IrBlockId){.value = block},
                                                                            (IrInstructionId){.value = row}, callee, &copied, &growth)
                                                  : ir_inline_tiny_leaf(callee, options.max_callee_instructions) &&
                                                        ir_inline_cfg_supported(program, caller, (IrBlockId){.value = block},
                                                                                (IrInstructionId){.value = row}, callee, &copied, &growth);
                            if (!shape)
                            {
                                module->inlining.shape_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            bool aggregate_result = call->result.value != IR_ID_UNDERLYING_INVALID &&
                                                    (ir_type_from_id(&program->types, ir_type_from_id(&program->types, callee->canonical_type)->return_type)->kind == IR_TYPE_ARRAY ||
                                                     ir_type_from_id(&program->types, ir_type_from_id(&program->types, callee->canonical_type)->return_type)->kind == IR_TYPE_STRUCT ||
                                                     ir_type_from_id(&program->types, ir_type_from_id(&program->types, callee->canonical_type)->return_type)->kind == IR_TYPE_UNION ||
                                                     ir_type_from_id(&program->types, ir_type_from_id(&program->types, callee->canonical_type)->return_type)->kind == IR_TYPE_SLICE);
                            u64 storage = ir_inline_cfg_storage_bytes(caller, callee);
                            u64 projected_extra = ir_inline_storage_multiply(function_projected_units, sizeof(IrFunction));
                            u64 projected_local_slots = (u64)caller->local_count + function_projected_locals + callee->local_count +
                                                        (aggregate_result ? 1u : 0u);
                            u64 projected_snapshot_cells = ir_inline_storage_multiply(function_projected_blocks, projected_local_slots);
                            u64 projected_base_blocks = (u64)caller->block_count + callee->block_count + 1u;
                            u64 projected_old_local_cells =
                                ir_inline_storage_multiply(projected_base_blocks, function_projected_locals);
                            projected_snapshot_cells = ir_inline_storage_add(projected_snapshot_cells, projected_old_local_cells);
                            u64 projected_snapshots = ir_inline_storage_multiply(projected_snapshot_cells, sizeof(IrValueId));
                            storage = ir_inline_storage_add(storage, ir_inline_storage_add(projected_extra, projected_snapshots));
                            u64 projected_units = copied;
                            projected_units = ir_inline_storage_add(projected_units, callee->value_count + (aggregate_result ? 1u : 0u));
                            projected_units = ir_inline_storage_add(projected_units, (u64)callee->block_count + 1u);
                            projected_units = ir_inline_storage_add(projected_units, callee->local_count + (aggregate_result ? 1u : 0u));
                            projected_units = ir_inline_storage_add(projected_units, callee->debug_local_count);
                            projected_units = ir_inline_storage_add(projected_units, callee->extra_count);
                            projected_units = ir_inline_storage_add(projected_units, callee_payload_units);
                            bool within = total_work <= IR_FAST_WORK_BUDGET && function_sites < options.max_call_sites &&
                                          storage != UINT64_MAX &&
                                          storage <= IR_FAST_SCRATCH_BUDGET - BUSTER_MIN(function_storage, (u64)IR_FAST_SCRATCH_BUDGET) &&
                                          storage <= IR_FAST_SCRATCH_BUDGET - BUSTER_MIN(module_storage, (u64)IR_FAST_SCRATCH_BUDGET) &&
                                          growth <= options.max_function_growth -
                                                        BUSTER_MIN(function_growth, options.max_function_growth) &&
                                          growth <= options.max_module_growth -
                                                        BUSTER_MIN(module_growth, options.max_module_growth) &&
                                          copied <= IR_FAST_WORK_BUDGET - BUSTER_MIN(total_copies, (u64)IR_FAST_WORK_BUDGET);
                            if (!within)
                            {
                                module->inlining.budget_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            planned[row] = 1;
                            planned_next[row] = planned_heads[block];
                            planned_heads[block] = row;
                            function_sites += 1;
                            function_growth += growth;
                            function_storage += storage;
                            function_projected_units = ir_inline_storage_add(function_projected_units, projected_units);
                            function_projected_blocks = ir_inline_storage_add(function_projected_blocks, (u64)callee->block_count + 1u);
                            function_projected_locals = ir_inline_storage_add(function_projected_locals,
                                                                             callee->local_count + (aggregate_result ? 1u : 0u));
                            module_growth += growth;
                            module_storage += storage;
                            total_copies += copied;
                        }
                    }
                    bool changed = false;
                    for (u32 block = 0; block < original_blocks && result.error == IR_VALIDATION_NONE; block += 1)
                    {
                        for (u32 row = planned_heads[block]; row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE; row = planned_next[row])
                        {
                        if (!planned[row]) continue;
                        IrInstruction* call = caller->instructions + row;
                        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, call->symbol);
                        u32 callee_index = call->symbol.value < program->symbols.count ? plan.function_by_symbol[call->symbol.value] : IR_INLINE_NONE;
                        IrFunction* callee = callee_index < count ? module->functions + callee_index : 0;
                        u32 copied = 0;
                        u32 rows_before = caller->instruction_count;
                        if (!callee || !ir_inline_cfg_call(program->arena, program, caller, (IrBlockId){.value = block},
                                                           (IrInstructionId){.value = row}, callee, &copied))
                        {
                            result = ir_inline_callsite_error(caller, block, row);
                        }
                        else
                        {
                            changed = true;
                            module->inlining.inlined += 1;
                            module->inlining.always_inlined += symbol && symbol->always_inline;
                            module->inlining.copied_instructions += copied;
                            module->inlining.growth += caller->instruction_count - rows_before;
                        }
                        }
                    }
                    if (changed && result.error == IR_VALIDATION_NONE)
                    {
                        u32* replacements = arena_allocate(function_arena, u32, caller->value_count ? caller->value_count : 1u);
                        u8* removed = arena_allocate(function_arena, u8, caller->instruction_count ? caller->instruction_count : 1u);
                        for (u32 value = 0; value < caller->value_count; value += 1) replacements[value] = value;
                        memset(removed, 0, caller->instruction_count);
                        ir_rewrite_compact(function_arena, program, caller, replacements, removed);
                        ir_function_invalidate_cfg(caller);
                        module->local_promotion_complete = false;
                        module->local_promotion = (IrLocalPromotionStatistics){0};
                        module->fast_complete = false;
                        module->fast = (IrFastStatistics){0};
                    }
                    scratch_end(function_scratch);
                }
                if (result.error == IR_VALIDATION_NONE && module->inlining.inlined)
                {
                    result = ir_validate_canonical_module(program, module);
                    result.boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT;
                }
                if (result.error == IR_VALIDATION_NONE) module->inline_complete = true;
                scratch_end(planning);
            }
        }
    }
    return result;
}
