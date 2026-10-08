// Bounded direct canonical inlining. Included after ir_inline_cfg.c and
// ir_promote.c; the CFG helper owns block/value/local cloning, while this file
// owns candidate policy, recursion bounds, deterministic order and budgets.
#define IR_INLINE_NONE UINT32_MAX
#define IR_INLINE_FUNCTION_SCRATCH_BYTES 64u
#define IR_INLINE_PLAN_ALIGNMENT_BYTES 256u
#define IR_INLINE_GRAPH_FUNCTION_WORK 12u

typedef struct IrInlinePlan IrInlinePlan;
struct IrInlinePlan
{
    u32* function_by_symbol;
    u32* order;
    u32* reverse_offsets;
    u32* reverse_callers;
    u32* reverse_blocks;
    u32* reverse_rows;
    u32* remaining;
    u32* queue;
    u32* first_required_blocks;
    u32* first_required_rows;
    u32* cycle_callers;
    u32* cycle_blocks;
    u32* cycle_rows;
    u64 scratch_bytes;
    u64 work_units;
    u32 first_cycle;
    u32 first_required_caller;
    u8* required_callers;
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
    bool result = callee && callee->instruction_count <= instruction_limit && callee->block_count == 1 &&
                  callee->entry.value == 0 && callee->label_metadata_count == 0 && callee->blocks[0].parameter_count == 0;
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

BUSTER_GLOBAL_LOCAL IrValidationResult ir_inline_first_required_error(IrProgram* program, IrModule* module)
{
    IrValidationResult result = (IrValidationResult){.error = IR_VALIDATION_NONE, .boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT};
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
    return result;
}

BUSTER_GLOBAL_LOCAL void ir_inline_plan_optional(IrProgram* program, IrModule* module, Arena* arena,
                                                   IrInlinePlan* plan, u32* order_count_out)
{
    u32 count = module->function_count;
    plan->function_by_symbol = arena_allocate(arena, u32, program->symbols.count ? program->symbols.count : 1u);
    plan->order = arena_allocate(arena, u32, count ? count : 1u);
    plan->required_callers = arena_allocate(arena, u8, count ? count : 1u);
    plan->scratch_bytes = ir_inline_storage_add(ir_inline_storage_multiply(program->symbols.count, sizeof(u32)),
                                                 ir_inline_storage_add(ir_inline_storage_multiply(count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
    memset(plan->required_callers, 0, count ? count : 1u);
    for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1) plan->function_by_symbol[symbol] = IR_INLINE_NONE;
    for (u32 function = 0; function < count; function += 1)
    {
        IrFunction* row = module->functions + function;
        if (row->symbol.value < program->symbols.count) plan->function_by_symbol[row->symbol.value] = function;
        plan->order[function] = function;
    }
    plan->work_units = ir_inline_storage_add(ir_inline_storage_multiply(count, 2u), program->symbols.count);
    plan->first_cycle = IR_INLINE_NONE;
    plan->first_required_caller = IR_INLINE_NONE;
    *order_count_out = count;
}

#define IR_INLINE_EDGE_CHUNK_CAPACITY 128u

typedef struct IrInlineEdge IrInlineEdge;
struct IrInlineEdge
{
    u32 caller;
    u32 block;
    u32 row;
};

typedef struct IrInlineEdgeChunk IrInlineEdgeChunk;
struct IrInlineEdgeChunk
{
    IrInlineEdgeChunk* next;
    u32 count;
    IrInlineEdge edges[IR_INLINE_EDGE_CHUNK_CAPACITY];
};

BUSTER_GLOBAL_LOCAL bool ir_inline_plan_graph(IrProgram* program, IrModule* module, Arena* arena, IrInlinePlan* plan,
                                                        u64 scratch_limit, u64 work_limit, u64 base_work, u32* order_count_out)
{
    u32 count = module->function_count;
    u64 base_scratch = ir_inline_storage_multiply(program->symbols.count, sizeof(u32));
    base_scratch = ir_inline_storage_add(base_scratch, ir_inline_storage_add(ir_inline_storage_multiply(count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
    u64 graph_work = ir_inline_storage_add(base_work, ir_inline_storage_multiply(count, IR_INLINE_GRAPH_FUNCTION_WORK));
    graph_work = ir_inline_storage_add(graph_work, program->symbols.count);
    plan->work_units = graph_work;
    bool result = base_scratch <= scratch_limit && graph_work <= work_limit;
    u64 edge_count = 0;
    u64 chunk_count = 0;
    IrInlineEdgeChunk* first_chunk = 0;
    IrInlineEdgeChunk* last_chunk = 0;
    if (result)
    {
        plan->function_by_symbol = arena_allocate(arena, u32, program->symbols.count ? program->symbols.count : 1u);
        plan->order = arena_allocate(arena, u32, count ? count : 1u);
        plan->reverse_offsets = arena_allocate(arena, u32, (u64)count + 1);
        plan->remaining = arena_allocate(arena, u32, count ? count : 1u);
        plan->queue = arena_allocate(arena, u32, count ? count : 1u);
        plan->cyclic = arena_allocate(arena, u8, count ? count : 1u);
        plan->first_cycle = IR_INLINE_NONE;
        plan->first_required_caller = IR_INLINE_NONE;
        plan->required_callers = arena_allocate(arena, u8, count ? count : 1u);
        plan->first_required_blocks = arena_allocate(arena, u32, count ? count : 1u);
        plan->first_required_rows = arena_allocate(arena, u32, count ? count : 1u);
        plan->cycle_callers = arena_allocate(arena, u32, count ? count : 1u);
        plan->cycle_blocks = arena_allocate(arena, u32, count ? count : 1u);
        plan->cycle_rows = arena_allocate(arena, u32, count ? count : 1u);
        memset(plan->reverse_offsets, 0, sizeof(u32) * ((u64)count + 1));
        memset(plan->remaining, 0, sizeof(u32) * (count ? count : 1u));
        memset(plan->cyclic, 0, count ? count : 1u);
        memset(plan->required_callers, 0, count ? count : 1u);
        for (u32 function = 0; function < count; function += 1)
        {
            plan->first_required_blocks[function] = IR_INLINE_NONE;
            plan->first_required_rows[function] = IR_INLINE_NONE;
            plan->cycle_callers[function] = IR_INLINE_NONE;
            plan->cycle_blocks[function] = IR_INLINE_NONE;
            plan->cycle_rows[function] = IR_INLINE_NONE;
        }
        for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1)
            plan->function_by_symbol[symbol] = IR_INLINE_NONE;
        for (u32 function = 0; function < count; function += 1)
        {
            IrFunction* row = module->functions + function;
            if (row->symbol.value < program->symbols.count)
                plan->function_by_symbol[row->symbol.value] = function;
        }

        for (u32 function = 0; function < count && result; function += 1)
        {
            IrFunction* caller = module->functions + function;
            if (caller->state != IR_FUNCTION_LOWERED) continue;
            for (u32 block = 0; block < caller->block_count && result; block += 1)
            {
                for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result;
                     row = caller->instructions[row].next.value)
                {
                    IrInstruction* call = caller->instructions + row;
                    IrSymbol* symbol = call->opcode == IR_OPCODE_CALL
                                           ? ir_symbol_from_id(&program->symbols, call->symbol)
                                           : 0;
                    if (!symbol || !symbol->always_inline ||
                        call->symbol.value >= program->symbols.count) continue;
                    plan->required_callers[function] = 1;
                    if (plan->first_required_caller == IR_INLINE_NONE)
                        plan->first_required_caller = function;
                    if (plan->first_required_rows[function] == IR_INLINE_NONE)
                    {
                        plan->first_required_blocks[function] = block;
                        plan->first_required_rows[function] = row;
                    }
                    if (!ir_inline_direct_call(caller, call)) continue;
                    u32 callee = plan->function_by_symbol[call->symbol.value];
                    if (callee >= count) continue;
                    if (edge_count == UINT32_MAX || plan->remaining[function] == UINT32_MAX ||
                        plan->reverse_offsets[callee + 1] == UINT32_MAX)
                    {
                        result = false;
                        break;
                    }
                    if (!last_chunk || last_chunk->count == IR_INLINE_EDGE_CHUNK_CAPACITY)
                    {
                        u64 next_chunk_count = chunk_count + 1u;
                        u64 chunk_bytes = ir_inline_storage_multiply(next_chunk_count, sizeof(IrInlineEdgeChunk));
                        u64 retained_edges = ir_inline_storage_multiply(edge_count + 1u, 3u * sizeof(u32));
                        u64 next_scratch = ir_inline_storage_add(base_scratch,
                                                                  ir_inline_storage_add(chunk_bytes, retained_edges));
                        u64 next_work = ir_inline_storage_add(base_work, ir_inline_storage_multiply(count, IR_INLINE_GRAPH_FUNCTION_WORK));
                        next_work = ir_inline_storage_add(next_work, program->symbols.count);
                        next_work = ir_inline_storage_add(next_work, ir_inline_storage_multiply(edge_count + 1u, 3u));
                        next_work = ir_inline_storage_add(next_work, ir_inline_storage_multiply(next_chunk_count, 2u));
                        if (next_scratch > scratch_limit || next_work > work_limit)
                        {
                            result = false;
                            break;
                        }
                        IrInlineEdgeChunk* chunk = arena_allocate(arena, IrInlineEdgeChunk, 1);
                        chunk->next = 0;
                        chunk->count = 0;
                        if (last_chunk) last_chunk->next = chunk;
                        else first_chunk = chunk;
                        last_chunk = chunk;
                        chunk_count = next_chunk_count;
                    }
                    u64 retained_edges = ir_inline_storage_multiply(edge_count + 1u, 3u * sizeof(u32));
                    u64 retained_chunks = ir_inline_storage_multiply(chunk_count, sizeof(IrInlineEdgeChunk));
                    u64 next_scratch = ir_inline_storage_add(base_scratch,
                                                              ir_inline_storage_add(retained_chunks, retained_edges));
                    u64 next_work = ir_inline_storage_add(base_work, ir_inline_storage_multiply(count, IR_INLINE_GRAPH_FUNCTION_WORK));
                    next_work = ir_inline_storage_add(next_work, program->symbols.count);
                    next_work = ir_inline_storage_add(next_work, ir_inline_storage_multiply(edge_count + 1u, 3u));
                    next_work = ir_inline_storage_add(next_work, ir_inline_storage_multiply(chunk_count, 2u));
                    if (next_scratch > scratch_limit || next_work > work_limit)
                    {
                        result = false;
                        break;
                    }
                    IrInlineEdge* edge = last_chunk->edges + last_chunk->count++;
                    edge->caller = function;
                    edge->block = block;
                    edge->row = row;
                    plan->remaining[function] += 1;
                    plan->reverse_offsets[callee + 1] += 1;
                    edge_count += 1;
                }
            }
        }

        u64 total_scratch = ir_inline_storage_add(base_scratch,
                                                   ir_inline_storage_add(
                                                       ir_inline_storage_multiply(chunk_count, sizeof(IrInlineEdgeChunk)),
                                                       ir_inline_storage_multiply(edge_count, 3u * sizeof(u32))));
        plan->work_units = ir_inline_storage_add(base_work, ir_inline_storage_multiply(count, IR_INLINE_GRAPH_FUNCTION_WORK));
        plan->work_units = ir_inline_storage_add(plan->work_units, program->symbols.count);
        plan->work_units = ir_inline_storage_add(plan->work_units, ir_inline_storage_multiply(edge_count, 3u));
        plan->work_units = ir_inline_storage_add(plan->work_units, ir_inline_storage_multiply(chunk_count, 2u));
        if (total_scratch > scratch_limit || plan->work_units > work_limit) result = false;
        if (result)
        {
            plan->scratch_bytes = total_scratch;
            for (u32 index = 0; index < count; index += 1)
                plan->reverse_offsets[index + 1] += plan->reverse_offsets[index];
            plan->reverse_callers = arena_allocate(arena, u32, edge_count ? edge_count : 1u);
            plan->reverse_blocks = arena_allocate(arena, u32, edge_count ? edge_count : 1u);
            plan->reverse_rows = arena_allocate(arena, u32, edge_count ? edge_count : 1u);
            u32* cursor = arena_allocate(arena, u32, count ? count : 1u);
            if (count) memcpy(cursor, plan->reverse_offsets, sizeof(u32) * count);
            for (IrInlineEdgeChunk* chunk = first_chunk; chunk; chunk = chunk->next)
            {
                for (u32 index = 0; index < chunk->count; index += 1)
                {
                    IrInlineEdge const* captured = chunk->edges + index;
                    IrFunction* caller = module->functions + captured->caller;
                    IrInstruction* call = caller->instructions + captured->row;
                    u32 callee = plan->function_by_symbol[call->symbol.value];
                    u32 edge = cursor[callee]++;
                    plan->reverse_callers[edge] = captured->caller;
                    plan->reverse_blocks[edge] = captured->block;
                    plan->reverse_rows[edge] = captured->row;
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
            for (u32 function = 0; function < count; function += 1)
            {
                plan->cyclic[function] = plan->remaining[function] != 0;
                if (!plan->cyclic[function]) continue;
                for (u32 edge = plan->reverse_offsets[function]; edge < plan->reverse_offsets[function + 1]; edge += 1)
                {
                    plan->cycle_callers[function] = plan->reverse_callers[edge];
                    plan->cycle_blocks[function] = plan->reverse_blocks[edge];
                    plan->cycle_rows[function] = plan->reverse_rows[edge];
                    if (plan->first_cycle == IR_INLINE_NONE) plan->first_cycle = function;
                    break;
                }
            }
            *order_count_out = order_count;
        }
    }
    return result;
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
        for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1)
        {
            has_required |= program->symbols.symbols[symbol].always_inline;
        }
        u64 planning_rows = 0;
        for (u32 function = 0; function < module->function_count; function += 1)
        {
            IrFunction* candidate = module->functions + function;
            if (candidate->state != IR_FUNCTION_LOWERED) continue;
            planning_rows = ir_inline_storage_add(planning_rows, candidate->block_count);
            planning_rows = ir_inline_storage_add(planning_rows, candidate->instruction_count);
        }
        u64 planning_units = ir_inline_storage_add(module->function_count, program->symbols.count);
        if (has_required)
        {
            planning_units = ir_inline_storage_add(planning_units, planning_rows);
        }
        if (!has_required && !options.tiny)
        {
            module->inline_complete = true;
        }
        else
        {
            u64 planning_scratch = ir_inline_storage_multiply(program->symbols.count, sizeof(u32));
            planning_scratch = ir_inline_storage_add(planning_scratch,
                                                      ir_inline_storage_add(ir_inline_storage_multiply(module->function_count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
            u64 optional_metadata_work = ir_inline_storage_add(ir_inline_storage_multiply(module->function_count, 2u), program->symbols.count);
            u64 initial_work = has_required ? planning_units : ir_inline_storage_add(planning_units, optional_metadata_work);
            if (initial_work > IR_FAST_WORK_BUDGET || planning_scratch > IR_FAST_SCRATCH_BUDGET)
            {
                module->inlining.visits = initial_work;
                if (has_required)
                {
                    result = ir_inline_first_required_error(program, module);
                }
                if (result.error == IR_VALIDATION_NONE)
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
                bool plan_ready = has_required
                                     ? ir_inline_plan_graph(program, module, arena, &plan, IR_FAST_SCRATCH_BUDGET, IR_FAST_WORK_BUDGET, planning_units, &order_count)
                                     : true;
                if (plan_ready && !has_required) ir_inline_plan_optional(program, module, arena, &plan, &order_count);
                module->inlining.visits = has_required ? plan.work_units : ir_inline_storage_add(planning_units, plan.work_units);
                if (!plan_ready)
                {
                    if (has_required)
                    {
                        u32 required_caller = plan.first_required_caller;
                        if (required_caller < module->function_count && plan.first_required_rows &&
                            plan.first_required_rows[required_caller] != IR_INLINE_NONE)
                        {
                            module->inlining.budget_skips += 1;
                            result = ir_inline_callsite_error(module->functions + required_caller,
                                                              plan.first_required_blocks[required_caller],
                                                              plan.first_required_rows[required_caller]);
                        }
                        else
                        {
                            result = ir_inline_first_required_error(program, module);
                        }
                    }
                    if (result.error == IR_VALIDATION_NONE)
                    {
                        module->inlining.budget_skips += 1;
                        module->inline_complete = true;
                    }
                }
                else
                {
                u32 count = module->function_count;
                if (plan.first_cycle < count)
                {
                    u32 function = plan.first_cycle;
                    u32 caller_index = plan.cycle_callers[function];
                    u32 block = plan.cycle_blocks[function];
                    u32 row = plan.cycle_rows[function];
                    if (caller_index < count && block != IR_INLINE_NONE && row != IR_INLINE_NONE)
                    {
                        module->inlining.recursion_skips += 1;
                        result = ir_inline_callsite_error(module->functions + caller_index, block, row);
                    }
                    else
                    {
                        result = ir_inline_first_required_error(program, module);
                    }
                }
                u64 module_growth = 0;
                u64 module_storage = 0;
                u64 total_work = module->inlining.visits;
                u64 total_copies = 0;
                u32* function_growth_used = arena_allocate(arena, u32, count ? count : 1u);
                u64* function_storage_used = arena_allocate(arena, u64, count ? count : 1u);
                u32* function_sites_used = arena_allocate(arena, u32, count ? count : 1u);
                memset(function_growth_used, 0, sizeof(u32) * (count ? count : 1u));
                memset(function_storage_used, 0, sizeof(u64) * (count ? count : 1u));
                memset(function_sites_used, 0, sizeof(u32) * (count ? count : 1u));
                u32 phase_count = options.tiny ? 2u : 1u;
                for (u32 phase = 0; phase < phase_count && result.error == IR_VALIDATION_NONE; phase += 1)
                {
                    for (u32 order_index = 0; order_index < order_count && result.error == IR_VALIDATION_NONE; order_index += 1)
                    {
                    u32 caller_index = plan.order[order_index];
                    IrFunction* caller = module->functions + caller_index;
                    if (caller->state != IR_FUNCTION_LOWERED) continue;
                    if (phase == 0 && (!plan.required_callers || !plan.required_callers[caller_index])) continue;
                    if (phase == 1)
                    {
                        total_work = ir_inline_storage_add(total_work, caller->block_count);
                        module->inlining.visits = ir_inline_storage_add(module->inlining.visits, caller->block_count);
                        bool possible_tiny_call = false;
                        u64 recursive_sites = 0;
                        bool caller_scan_exhausted = total_work > IR_FAST_WORK_BUDGET;
                        for (u32 block = 0; block < caller->block_count && !possible_tiny_call && !caller_scan_exhausted; block += 1)
                        {
                            for (u32 row = caller->blocks[block].first_instruction.value;
                                 row != IR_INLINE_NONE && !possible_tiny_call && !caller_scan_exhausted;
                                 row = caller->instructions[row].next.value)
                            {
                                total_work = ir_inline_storage_add(total_work, 1u);
                                module->inlining.visits += 1;
                                if (total_work > IR_FAST_WORK_BUDGET)
                                {
                                    caller_scan_exhausted = true;
                                    break;
                                }
                                IrInstruction* call = caller->instructions + row;
                                IrSymbol* symbol = call->opcode == IR_OPCODE_CALL
                                                       ? ir_symbol_from_id(&program->symbols, call->symbol)
                                                       : 0;
                                if (symbol && !symbol->always_inline && !symbol->noinline &&
                                    ir_inline_direct_call(caller, call))
                                {
                                    u32 callee_index = call->symbol.value < program->symbols.count
                                                           ? plan.function_by_symbol[call->symbol.value]
                                                           : IR_INLINE_NONE;
                                    IrFunction* callee = callee_index < count ? module->functions + callee_index : 0;
                                    if (ir_inline_linkage_allowed(program, symbol, callee))
                                    {
                                        if (callee_index == caller_index)
                                        {
                                            recursive_sites += 1;
                                            continue;
                                        }
                                        u64 callee_scan_work = 1u;
                                        if (callee->block_count == 1 &&
                                            callee->instruction_count <= options.max_callee_instructions)
                                        {
                                            callee_scan_work =
                                                ir_inline_storage_add(callee_scan_work, callee->instruction_count);
                                        }
                                        total_work = ir_inline_storage_add(total_work, callee_scan_work);
                                        module->inlining.visits =
                                            ir_inline_storage_add(module->inlining.visits, callee_scan_work);
                                        if (total_work > IR_FAST_WORK_BUDGET)
                                        {
                                            caller_scan_exhausted = true;
                                            break;
                                        }
                                        possible_tiny_call =
                                            ir_inline_tiny_leaf(callee, options.max_callee_instructions);
                                    }
                                }
                            }
                        }
                        if (caller_scan_exhausted)
                        {
                            module->inlining.budget_skips += 1;
                            continue;
                        }
                        if (!possible_tiny_call)
                        {
                            module->inlining.candidates += recursive_sites;
                            module->inlining.recursion_skips += recursive_sites;
                            continue;
                        }
                    }
                    u64 caller_plan_work = (u64)caller->instruction_count + caller->block_count;
                    if (caller_plan_work > IR_FAST_WORK_BUDGET - BUSTER_MIN(total_work, (u64)IR_FAST_WORK_BUDGET))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0 && plan.first_required_rows[caller_index] != IR_INLINE_NONE)
                        {
                            result = ir_inline_callsite_error(caller, plan.first_required_blocks[caller_index],
                                                              plan.first_required_rows[caller_index]);
                        }
                        continue;
                    }
                    u64 caller_scratch = (u64)caller->instruction_count * 10u +
                                         (u64)caller->value_count * 4u + (u64)caller->block_count * 4u;
                    if (caller_scratch > IR_FAST_SCRATCH_BUDGET - BUSTER_MIN(plan.scratch_bytes, (u64)IR_FAST_SCRATCH_BUDGET))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0 && plan.first_required_rows[caller_index] != IR_INLINE_NONE)
                        {
                            result = ir_inline_callsite_error(caller, plan.first_required_blocks[caller_index],
                                                              plan.first_required_rows[caller_index]);
                        }
                        continue;
                    }
                    total_work = ir_inline_storage_add(total_work, caller->block_count);
                    module->inlining.visits = ir_inline_storage_add(module->inlining.visits, caller->block_count);
                    TemporalArena function_scratch = scratch_begin(&program->arena, 1);
                    Arena* function_arena = function_scratch.arena;
                    u32 original_instructions = caller->instruction_count;
                    u32 original_blocks = caller->block_count;
                    u32* planned_next = arena_allocate(function_arena, u32, original_instructions ? original_instructions : 1u);
                    u32* planned_heads = arena_allocate(function_arena, u32, original_blocks ? original_blocks : 1u);
                    for (u32 block = 0; block < original_blocks; block += 1) planned_heads[block] = IR_INLINE_NONE;

                    u64 caller_edge_units = 0;
                    u64 caller_phi_units = 0;
                    bool caller_preflight_exhausted = false;
                    for (u32 block_index = 0; block_index < original_blocks && !caller_preflight_exhausted; block_index += 1)
                    {
                        IrBlock const* block = caller->blocks + block_index;
                        u64 block_work = 1u;
                        if (block->last_instruction.value < caller->instruction_count)
                        {
                            IrInstruction const* terminator = caller->instructions + block->last_instruction.value;
                            caller_edge_units = ir_inline_storage_add(caller_edge_units, terminator->target_count);
                            block_work = ir_inline_storage_add(block_work, ir_inline_storage_add(1u, terminator->target_count));
                        }
                        total_work = ir_inline_storage_add(total_work, block_work);
                        module->inlining.visits = ir_inline_storage_add(module->inlining.visits, block_work);
                        if (total_work > IR_FAST_WORK_BUDGET)
                        {
                            caller_preflight_exhausted = true;
                            break;
                        }
                        for (IrBlockParameter const* parameter = block->first_parameter;
                             parameter && !caller_preflight_exhausted; parameter = parameter->next)
                        {
                            u64 parameter_work = ir_inline_storage_add(1u, parameter->incoming_count);
                            caller_phi_units = ir_inline_storage_add(caller_phi_units, parameter_work);
                            total_work = ir_inline_storage_add(total_work, parameter_work);
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, parameter_work);
                            if (total_work > IR_FAST_WORK_BUDGET) caller_preflight_exhausted = true;
                        }
                    }
                    if (caller_preflight_exhausted)
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0 && plan.first_required_rows[caller_index] != IR_INLINE_NONE)
                        {
                            result = ir_inline_callsite_error(caller, plan.first_required_blocks[caller_index],
                                                              plan.first_required_rows[caller_index]);
                        }
                        scratch_end(function_scratch);
                        continue;
                    }
                    u32 function_growth = function_growth_used[caller_index];
                    u64 function_storage = function_storage_used[caller_index];
                    u64 function_projected_units = 0;
                    u64 function_projected_blocks = 0;
                    u64 function_projected_locals = 0;
                    u32 function_sites = function_sites_used[caller_index];
                    u64 caller_main_scan = ir_inline_storage_add(original_instructions, original_blocks);
                    if (caller_main_scan > IR_FAST_WORK_BUDGET -
                                               BUSTER_MIN(total_work, (u64)IR_FAST_WORK_BUDGET))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0 && plan.first_required_rows[caller_index] != IR_INLINE_NONE)
                        {
                            result = ir_inline_callsite_error(caller, plan.first_required_blocks[caller_index],
                                                              plan.first_required_rows[caller_index]);
                        }
                        scratch_end(function_scratch);
                        continue;
                    }
                    total_work = ir_inline_storage_add(total_work, original_blocks);
                    module->inlining.visits = ir_inline_storage_add(module->inlining.visits, original_blocks);
                    u32 caller_rows_scanned = 0;
                    for (u32 block = 0; block < original_blocks && result.error == IR_VALIDATION_NONE; block += 1)
                    {
                        u32 position = 0;
                        for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE;
                             row = caller->instructions[row].next.value)
                        {
                            position += 1;
                            planned_next[row] = position;
                            total_work = ir_inline_storage_add(total_work, 1u);
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, 1u);
                            caller_rows_scanned += 1;
                            IrInstruction* call = caller->instructions + row;
                            if (call->opcode != IR_OPCODE_CALL) continue;
                            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, call->symbol);
                            bool required = symbol && symbol->always_inline;
                            bool tiny = options.tiny && symbol && !symbol->noinline && ir_inline_direct_call(caller, call);
                            if ((phase == 0 && !required) || (phase == 1 && (required || !tiny))) continue;
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
                            if (callee_index == caller_index || (plan.cyclic && callee_index < count && plan.cyclic[callee_index]))
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
                            u64 caller_rows_remaining = original_instructions - caller_rows_scanned;
                            if (!required)
                            {
                                u64 tiny_scan_work = 1u;
                                if (callee->block_count == 1 &&
                                    callee->instruction_count <= options.max_callee_instructions)
                                {
                                    tiny_scan_work =
                                        ir_inline_storage_add(tiny_scan_work, callee->instruction_count);
                                }
                                u64 tiny_work_with_caller_rows =
                                    ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (tiny_scan_work >
                                    IR_FAST_WORK_BUDGET -
                                        BUSTER_MIN(tiny_work_with_caller_rows, (u64)IR_FAST_WORK_BUDGET))
                                {
                                    module->inlining.budget_skips += 1;
                                    continue;
                                }
                                total_work = ir_inline_storage_add(total_work, tiny_scan_work);
                                module->inlining.visits =
                                    ir_inline_storage_add(module->inlining.visits, tiny_scan_work);
                            }
                            bool tiny_eligible = required || ir_inline_tiny_leaf(callee, options.max_callee_instructions);
                            if (!tiny_eligible)
                            {
                                module->inlining.shape_skips += 1;
                                continue;
                            }
                            u64 callee_payload_units = callee->extra_count;
                            bool callee_scan_exhausted = false;
                            for (u32 row_id = 0; row_id < callee->instruction_count && !callee_scan_exhausted; row_id += 1)
                            {
                                u64 reserved_work = ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (reserved_work >= IR_FAST_WORK_BUDGET)
                                {
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                IrInstruction const* source = callee->instructions + row_id;
                                u64 payload = source->operand_count;
                                payload = ir_inline_storage_add(payload, source->target_count);
                                payload = ir_inline_storage_add(payload, source->immediate_count);
                                u64 scan_work = ir_inline_storage_add(payload, 1u);
                                if (scan_work > IR_FAST_WORK_BUDGET -
                                                    BUSTER_MIN(reserved_work, (u64)IR_FAST_WORK_BUDGET))
                                {
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                total_work = ir_inline_storage_add(total_work, scan_work);
                                module->inlining.visits = ir_inline_storage_add(module->inlining.visits, scan_work);
                                callee_payload_units = ir_inline_storage_add(callee_payload_units, payload);
                                if (total_work > IR_FAST_WORK_BUDGET) callee_scan_exhausted = true;
                            }
                            for (u32 block_id = 0; block_id < callee->block_count && !callee_scan_exhausted; block_id += 1)
                            {
                                u64 reserved_work = ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (reserved_work >= IR_FAST_WORK_BUDGET - 1u)
                                {
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                total_work = ir_inline_storage_add(total_work, 1u);
                                module->inlining.visits = ir_inline_storage_add(module->inlining.visits, 1u);
                                for (IrBlockParameter const* parameter = callee->blocks[block_id].first_parameter;
                                     parameter && !callee_scan_exhausted; parameter = parameter->next)
                                {
                                    u64 reserved_parameter_work =
                                        ir_inline_storage_add(total_work, caller_rows_remaining);
                                    if (reserved_parameter_work >= IR_FAST_WORK_BUDGET)
                                    {
                                        callee_scan_exhausted = true;
                                        break;
                                    }
                                    u64 parameter_work = ir_inline_storage_add(1u, parameter->incoming_count);
                                    if (parameter_work > IR_FAST_WORK_BUDGET -
                                                             BUSTER_MIN(reserved_parameter_work,
                                                                        (u64)IR_FAST_WORK_BUDGET))
                                    {
                                        callee_scan_exhausted = true;
                                        break;
                                    }
                                    total_work = ir_inline_storage_add(total_work, parameter_work);
                                    module->inlining.visits =
                                        ir_inline_storage_add(module->inlining.visits, parameter_work);
                                    callee_payload_units = ir_inline_storage_add(callee_payload_units, parameter_work);
                                    if (total_work > IR_FAST_WORK_BUDGET) callee_scan_exhausted = true;
                                }
                            }
                            if (callee_scan_exhausted)
                            {
                                module->inlining.budget_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            IrType* callee_signature = ir_type_from_id(&program->types, callee->canonical_type);
                            IrType* callee_return_type = callee_signature
                                                             ? ir_type_from_id(&program->types, callee_signature->return_type)
                                                             : 0;
                            bool aggregate_result = call->result.value != IR_ID_UNDERLYING_INVALID &&
                                                    callee_return_type &&
                                                    (callee_return_type->kind == IR_TYPE_ARRAY ||
                                                     callee_return_type->kind == IR_TYPE_STRUCT ||
                                                     callee_return_type->kind == IR_TYPE_UNION ||
                                                     callee_return_type->kind == IR_TYPE_SLICE);
                            u64 projected_blocks_before = ir_inline_storage_add(caller->block_count, function_projected_blocks);
                            u64 projected_locals_before = ir_inline_storage_add(caller->local_count, function_projected_locals);
                            u64 projected_new_locals = ir_inline_storage_add(callee->local_count, aggregate_result ? 1u : 0u);
                            u64 projected_locals_after = ir_inline_storage_add(projected_locals_before, projected_new_locals);
                            u64 projected_blocks_after = ir_inline_storage_add(projected_blocks_before,
                                                                                (u64)callee->block_count + 1u);
                            u64 prefix_rows = planned_next[row];
                            u64 caller_work = ir_inline_storage_multiply(projected_blocks_before, 7u);
                            caller_work = ir_inline_storage_add(caller_work, ir_inline_storage_multiply(prefix_rows, 3u));
                            caller_work = ir_inline_storage_add(caller_work,
                                                                ir_inline_storage_multiply(caller->debug_local_count, 3u));
                            caller_work = ir_inline_storage_add(caller_work, caller_phi_units);
                            caller_work = ir_inline_storage_add(caller_work, function_projected_units);
                            u64 projected_edges = ir_inline_storage_add(caller_edge_units, function_projected_units);
                            projected_edges = ir_inline_storage_add(projected_edges, callee_payload_units);
                            caller_work = ir_inline_storage_add(caller_work,
                                                                ir_inline_storage_multiply(projected_edges, 5u));
                            caller_work = ir_inline_storage_add(caller_work,
                                                                ir_inline_storage_multiply(projected_blocks_after,
                                                                                           projected_locals_after));
                            caller_work = ir_inline_storage_add(caller_work, projected_blocks_after);
                            u64 callee_work = ir_inline_storage_multiply(callee->instruction_count, 8u);
                            callee_work = ir_inline_storage_add(callee_work,
                                                               ir_inline_storage_multiply(callee->value_count, 3u));
                            callee_work = ir_inline_storage_add(callee_work,
                                                               ir_inline_storage_multiply(callee->block_count, 8u));
                            callee_work = ir_inline_storage_add(callee_work,
                                                               ir_inline_storage_multiply(callee_payload_units, 6u));
                            callee_work = ir_inline_storage_add(callee_work,
                                                               ir_inline_storage_multiply(callee->local_count, 3u));
                            callee_work = ir_inline_storage_add(callee_work,
                                                               ir_inline_storage_multiply(callee->debug_local_count, 3u));
                            callee_work = ir_inline_storage_add(callee_work,
                                                               callee_signature ? ir_inline_storage_multiply(callee_signature->parameter_count, 2u) : 0u);
                            u64 support_scratch = callee_signature
                                                      ? ir_inline_storage_multiply(callee_signature->parameter_count, sizeof(bool))
                                                      : 0u;
                            u64 active_scratch = ir_inline_storage_add(plan.scratch_bytes, caller_scratch);
                            if (support_scratch > IR_FAST_SCRATCH_BUDGET -
                                                      BUSTER_MIN(active_scratch, (u64)IR_FAST_SCRATCH_BUDGET))
                            {
                                module->inlining.budget_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            u64 candidate_work = ir_inline_storage_add(caller_work, callee_work);
                            candidate_work = ir_inline_storage_add(candidate_work, call->operand_count);
                            u64 work_with_caller_rows = ir_inline_storage_add(total_work, caller_rows_remaining);
                            if (candidate_work >
                                IR_FAST_WORK_BUDGET - BUSTER_MIN(work_with_caller_rows, (u64)IR_FAST_WORK_BUDGET))
                            {
                                module->inlining.budget_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            total_work += candidate_work;
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, candidate_work);
                            u32 copied = 0;
                            u32 growth = 0;
                            bool shape = ir_inline_cfg_supported(program, caller, (IrBlockId){.value = block},
                                                                    (IrInstructionId){.value = row}, callee, &copied, &growth);
                            if (!shape)
                            {
                                module->inlining.shape_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
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
                            // Reserve transient argument and predecessor maps
                            // alongside the retained graph and caller-planning arrays.
                            u64 projected_scratch = ir_inline_storage_add(function_projected_units, projected_units);
                            projected_scratch = ir_inline_storage_multiply(projected_scratch, 5u);
                            projected_scratch = ir_inline_storage_add(projected_scratch,
                                                                     ir_inline_storage_multiply(projected_blocks_after, sizeof(u32)));
                            projected_scratch = ir_inline_storage_add(projected_scratch, support_scratch);
                            bool scratch_within = projected_scratch <= IR_FAST_SCRATCH_BUDGET -
                                                                        BUSTER_MIN(active_scratch, (u64)IR_FAST_SCRATCH_BUDGET);
                            bool within = scratch_within && total_work <= IR_FAST_WORK_BUDGET &&
                                          function_sites < options.max_call_sites && storage != UINT64_MAX &&
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
                        // The splice appends rows and values, preserves all existing IDs, rebuilds affected block chains and
                        // predecessor lists, and assigns every cloned or synthetic result definition. The stale operand
                        // population is ignored while operand_total_rows differs from instruction_count; opcode_summary only
                        // over-approximates, so no full-function remap or compaction is needed here.
                        ir_function_invalidate_cfg(caller);
                        module->local_promotion_complete = false;
                        module->local_promotion = (IrLocalPromotionStatistics){0};
                        module->fast_complete = false;
                        module->fast = (IrFastStatistics){0};
                    }
                    function_growth_used[caller_index] = function_growth;
                    function_storage_used[caller_index] = function_storage;
                    function_sites_used[caller_index] = function_sites;
                    scratch_end(function_scratch);
                    }
                }
                if (result.error == IR_VALIDATION_NONE && module->inlining.inlined)
                {
                    result = ir_validate_canonical_module(program, module);
                    result.boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT;
                }
                if (result.error == IR_VALIDATION_NONE) module->inline_complete = true;
                }
                scratch_end(planning);
            }
        }
    }
    return result;
}
