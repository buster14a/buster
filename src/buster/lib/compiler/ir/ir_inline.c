// Bounded direct canonical inlining. Included after ir_inline_cfg.c and
// ir_promote.c; the CFG helper owns block/value/local cloning, while this file
// owns candidate policy, recursion bounds, deterministic order and budgets
// (ir_inline_module). It also records what each caller's splices may rewrite
// (ir_inline_splice_region) for the output proof, ir_validate_canonical_splice
// in ir.c, and publishes the proofs it leaves standing (module->inline_proven).
// ir_inline_plant_fault and ir_test_inline_module are the test seam.
#define IR_INLINE_NONE UINT32_MAX
#define IR_INLINE_FUNCTION_SCRATCH_BYTES 64u
#define IR_INLINE_PLAN_ALIGNMENT_BYTES 256u
#define IR_INLINE_GRAPH_FUNCTION_WORK 12u
// Planning scratch per symbol: function_by_symbol plus callee_facts.
#define IR_INLINE_SYMBOL_SCRATCH_BYTES (sizeof(u32) + sizeof(u8))

// Per-symbol callee facts, one byte each, filled once per pass by
// ir_inline_plan_facts so a call row is classified with one byte load instead
// of re-deriving symbol and callee predicates at every site. Directives and
// linkage do not change during the pass. A tiny leaf has no calls, so no
// splice changes it; a spliced caller's tiny bit is recomputed after its
// splices all the same.
#define IR_INLINE_FACT_REQUIRED 0x1u
#define IR_INLINE_FACT_NOINLINE 0x2u
#define IR_INLINE_FACT_LINKAGE 0x4u
#define IR_INLINE_FACT_TINY 0x8u

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
    IrInlineBudgetReason budget_reason;
    u64 budget_demand;
    u64 budget_limit;
    u8* required_callers;
    u8* cyclic;
    u8* callee_facts;
};

BUSTER_GLOBAL_LOCAL u64 ir_inline_storage_add(u64 left, u64 right)
{
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

BUSTER_GLOBAL_LOCAL u64 ir_inline_storage_multiply(u64 left, u64 right)
{
    return left && right > UINT64_MAX / left ? UINT64_MAX : left * right;
}

// Blocks that carry a local-value snapshot (IrBlock.local_values). Canonical
// construction attaches none, and a splice gives a cloned block one only when
// its callee block or call block has one (ir_inline_cfg_local_values), so the
// reservation below charges snapshots for these blocks rather than every block.
BUSTER_GLOBAL_LOCAL u64 ir_inline_snapshot_blocks(IrFunction const* function)
{
    u64 result = 0;
    for (u32 block = 0; block < function->block_count; block += 1) result += function->blocks[block].local_values != 0;
    return result;
}

// Retained bytes of a caller's splice reservation (IrInlineCfgReserve) once
// its planned sites add `added_blocks` blocks and the other counts: the
// `snapshot_blocks` original blocks carrying snapshots, widened once to the
// final local count when sites add locals, the `added_snapshots` new blocks
// that carry one, the place/memory arrays, debug rows when sites add any, and
// the single predecessor rebuild over `edges`. A planned site is charged the
// increase over the previous planned total, so a caller pays for widening
// once instead of once per site.
BUSTER_GLOBAL_LOCAL u64 ir_inline_reserve_bytes(IrFunction const* caller, u64 snapshot_blocks, u64 added_blocks,
                                                u64 added_snapshots, u64 added_locals, u64 added_debug_locals,
                                                u64 added_debug_scopes, u64 edges)
{
    u64 result = 0;
    if (added_blocks)
    {
        u64 locals = ir_inline_storage_add(caller->local_count, added_locals);
        u64 snapshots = added_locals ? ir_inline_storage_add(snapshot_blocks, added_snapshots) : added_snapshots;
        u64 snapshot_bytes = ir_inline_storage_add(ir_inline_storage_multiply(locals, sizeof(IrValueId)),
                                                   BUSTER_ALIGN_OF(IrValueId) - 1u);
        result = ir_inline_storage_multiply(snapshots, snapshot_bytes);
        if (added_locals || !caller->local_places || !caller->local_uses_memory)
        {
            result = ir_inline_storage_add(result, ir_inline_storage_multiply(locals, sizeof(IrValueId) + sizeof(bool)));
            result = ir_inline_storage_add(result, (BUSTER_ALIGN_OF(IrValueId) - 1u) + (BUSTER_ALIGN_OF(bool) - 1u));
        }
        if (added_debug_locals)
        {
            u64 rows = ir_inline_storage_add(caller->debug_local_count, added_debug_locals);
            result = ir_inline_storage_add(result, ir_inline_storage_multiply(rows, sizeof(IrDebugLocal)));
            result = ir_inline_storage_add(result, BUSTER_ALIGN_OF(IrDebugLocal) - 1u);
        }
        if (added_debug_scopes)
        {
            u64 rows = ir_inline_storage_add(caller->debug_scope_count, added_debug_scopes);
            result = ir_inline_storage_add(result, ir_inline_storage_multiply(rows, sizeof(IrDebugScope)));
            result = ir_inline_storage_add(result, BUSTER_ALIGN_OF(IrDebugScope) - 1u);
        }
        result = ir_inline_storage_add(result, ir_inline_storage_multiply(edges, sizeof(IrPredecessor)));
        result = ir_inline_storage_add(result, BUSTER_ALIGN_OF(IrPredecessor) - 1u);
    }
    return result;
}

// Work of the same reservation: filling every snapshot it widens or a new
// block carries, the widening walk's visit to each other original block,
// copying the place/debug arrays and one predecessor rebuild.
BUSTER_GLOBAL_LOCAL u64 ir_inline_reserve_work(IrFunction const* caller, u64 snapshot_blocks, u64 added_blocks,
                                               u64 added_snapshots, u64 added_locals, u64 added_debug_locals,
                                               u64 added_debug_scopes, u64 edges)
{
    u64 result = 0;
    if (added_blocks)
    {
        u64 blocks = ir_inline_storage_add(caller->block_count, added_blocks);
        u64 locals = ir_inline_storage_add(caller->local_count, added_locals);
        u64 snapshots = added_locals ? ir_inline_storage_add(snapshot_blocks, added_snapshots) : added_snapshots;
        result = ir_inline_storage_multiply(snapshots, ir_inline_storage_add(locals, 1u));
        if (added_locals && snapshot_blocks < caller->block_count)
        {
            result = ir_inline_storage_add(result, caller->block_count - snapshot_blocks);
        }
        result = ir_inline_storage_add(result, ir_inline_storage_multiply(locals, 2u));
        if (added_debug_locals) result = ir_inline_storage_add(result, ir_inline_storage_add(caller->debug_local_count, added_debug_locals));
        if (added_debug_scopes) result = ir_inline_storage_add(result, ir_inline_storage_add(caller->debug_scope_count, added_debug_scopes));
        result = ir_inline_storage_add(result, ir_inline_storage_add(blocks, ir_inline_storage_multiply(edges, 5u)));
    }
    return result;
}

// Expansion reads and writes only the strict canonical form. Rather than scan
// the whole module, check each function the pass reads (a candidate's caller
// and callee) or rewrites (a spliced caller), once per state change: 0 is
// unknown, then IR_INLINE_STRICT_VALID or IR_INLINE_STRICT_INVALID.
#define IR_INLINE_STRICT_VALID 1u
#define IR_INLINE_STRICT_INVALID 2u

BUSTER_GLOBAL_LOCAL bool ir_inline_function_strict(IrProgram* program, IrModule* module, u8* strict, u32 index)
{
    if (!strict[index])
    {
        strict[index] = ir_validate_canonical_function(program, module->functions + index).error == IR_VALIDATION_NONE
                            ? IR_INLINE_STRICT_VALID : IR_INLINE_STRICT_INVALID;
    }
    bool result = strict[index] == IR_INLINE_STRICT_VALID;
    return result;
}

// What one batch of splices into `caller` may rewrite, recorded before the
// first (IrValidationSplice): the original blocks holding planned sites, the
// rows they hold and the values those rows and the blocks' parameters define.
BUSTER_GLOBAL_LOCAL IrValidationSplice ir_inline_splice_region(Arena* arena, IrFunction const* caller, u32 const* planned_heads, u32 original_blocks)
{
    IrValidationSplice result = {.base_blocks = caller->block_count, .base_instructions = caller->instruction_count,
                                 .base_values = caller->value_count};
    u32 parameters = 0;
    for (u32 block = 0; block < original_blocks; block += 1)
    {
        result.parameter_block_count += caller->blocks[block].first_parameter != 0;
        if (planned_heads[block] != IR_INLINE_NONE)
        {
            result.block_count += 1;
            for (u32 row = caller->blocks[block].first_instruction.value; row < caller->instruction_count; row = caller->instructions[row].next.value)
            {
                result.row_count += 1;
            }
            parameters += caller->blocks[block].parameter_count;
        }
    }
    result.blocks = arena_allocate(arena, u32, result.block_count ? result.block_count : 1u);
    result.rows = arena_allocate(arena, u32, result.row_count ? result.row_count : 1u);
    result.values = arena_allocate(arena, u32, result.row_count + parameters ? result.row_count + parameters : 1u);
    result.parameter_blocks = arena_allocate(arena, u32, result.parameter_block_count);
    u32 parameter_block_cursor = 0;
    u32 block_cursor = 0;
    u32 row_cursor = 0;
    for (u32 block = 0; block < original_blocks; block += 1)
    {
        if (caller->blocks[block].first_parameter) result.parameter_blocks[parameter_block_cursor++] = block;
        if (planned_heads[block] != IR_INLINE_NONE)
        {
            result.blocks[block_cursor] = block;
            block_cursor += 1;
            for (IrBlockParameter const* parameter = caller->blocks[block].first_parameter;
                 parameter && result.value_count < result.row_count + parameters; parameter = parameter->next)
            {
                result.values[result.value_count] = parameter->value.value;
                result.value_count += parameter->value.value < caller->value_count;
            }
            for (u32 row = caller->blocks[block].first_instruction.value; row < caller->instruction_count; row = caller->instructions[row].next.value)
            {
                result.rows[row_cursor] = row;
                row_cursor += 1;
                u32 value = caller->instructions[row].result.value;
                if (value < caller->value_count && caller->values[value].definition.value == row)
                {
                    result.values[result.value_count] = value;
                    result.value_count += 1;
                }
            }
        }
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
// Plants one IrInlineSpliceFault in a spliced caller (ir_test_inline_module).
BUSTER_GLOBAL_LOCAL void ir_inline_plant_fault(IrFunction* caller, IrValidationSplice const* region, IrInlineSpliceFault fault)
{
    bool planted = fault == IR_INLINE_SPLICE_FAULT_NONE;
    for (u32 row = region->base_instructions; !planted && row < caller->instruction_count; row += 1)
    {
        IrInstruction* instruction = caller->instructions + row;
        if (fault == IR_INLINE_SPLICE_FAULT_OPERAND && instruction->operand_count)
        {
            instruction->operands[0].value = caller->value_count + 7u;
            planted = true;
        }
        else if (fault == IR_INLINE_SPLICE_FAULT_SECOND_DEFINITION && instruction->result.value < caller->value_count)
        {
            for (u32 outside = 0; !planted && outside < region->base_instructions; outside += 1)
            {
                u32 value = caller->instructions[outside].result.value;
                bool in_region = false;
                for (u32 index = 0; index < region->row_count; index += 1) in_region |= region->rows[index] == outside;
                if (!in_region && value < region->base_values && caller->values[value].definition.value == outside)
                {
                    instruction->result.value = value;
                    caller->values[value].definition.value = row;
                    planted = true;
                }
            }
        }
    }
    for (u32 block = region->base_blocks; !planted && block < caller->block_count; block += 1)
    {
        IrBlock* target = caller->blocks + block;
        if (fault == IR_INLINE_SPLICE_FAULT_ORPHAN && target->first_instruction.value != target->last_instruction.value)
        {
            target->first_instruction = caller->instructions[target->first_instruction.value].next;
            planted = true;
        }
        else if (fault == IR_INLINE_SPLICE_FAULT_INCOMING && target->first_parameter)
        {
            target->first_parameter->incoming_count += 1;
            planted = true;
        }
    }
    for (u32 row = 0; !planted && fault == IR_INLINE_SPLICE_FAULT_OUTSIDE && row < region->base_instructions; row += 1)
    {
        bool in_region = false;
        for (u32 index = 0; index < region->row_count; index += 1) in_region |= region->rows[index] == row;
        if (!in_region && caller->instructions[row].operand_count)
        {
            caller->instructions[row].operands[0].value = caller->value_count + 7u;
            planted = true;
        }
    }
    BUSTER_CHECK(planted);
}
#endif

BUSTER_GLOBAL_LOCAL void ir_inline_record_budget_refusal(IrModule* module, bool required,
                                                                  IrInlineBudgetReason reason, u64 demand, u64 limit)
{
    if (required && module && module->inlining.required_budget_reason == IR_INLINE_BUDGET_NONE)
    {
        module->inlining.required_budget_reason = reason;
        module->inlining.required_budget_demand = demand;
        module->inlining.required_budget_limit = limit;
    }
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

// The tiny bit of a linkage-allowed callee; zero unless tiny inlining is on.
BUSTER_GLOBAL_LOCAL u8 ir_inline_tiny_fact(IrFunction* callee, IrInlineOptions const* options)
{
    u8 result = options->tiny && ir_inline_tiny_leaf(callee, options->max_callee_instructions) ? IR_INLINE_FACT_TINY : 0u;
    return result;
}

// Fill plan->callee_facts once function_by_symbol is known; the symbol
// loop is already charged as one planning unit per symbol.
BUSTER_GLOBAL_LOCAL void ir_inline_plan_facts(IrProgram* program, IrModule* module, Arena* arena, IrInlinePlan* plan,
                                              IrInlineOptions const* options)
{
    u32 symbol_count = program->symbols.count;
    plan->callee_facts = arena_allocate(arena, u8, symbol_count ? symbol_count : 1u);
    for (u32 symbol = 0; symbol < symbol_count; symbol += 1)
    {
        IrSymbol* row = program->symbols.symbols + symbol;
        u32 function = plan->function_by_symbol[symbol];
        IrFunction* callee = function < module->function_count ? module->functions + function : 0;
        u8 facts = (row->always_inline ? IR_INLINE_FACT_REQUIRED : 0u) | (row->noinline ? IR_INLINE_FACT_NOINLINE : 0u);
        if (callee && ir_inline_linkage_allowed(program, row, callee))
        {
            facts |= IR_INLINE_FACT_LINKAGE | ir_inline_tiny_fact(callee, options);
        }
        plan->callee_facts[symbol] = facts;
    }
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
                                                   IrInlinePlan* plan, IrInlineOptions const* options, u32* order_count_out)
{
    u32 count = module->function_count;
    plan->function_by_symbol = arena_allocate(arena, u32, program->symbols.count ? program->symbols.count : 1u);
    plan->order = arena_allocate(arena, u32, count ? count : 1u);
    plan->required_callers = arena_allocate(arena, u8, count ? count : 1u);
    plan->scratch_bytes = ir_inline_storage_add(ir_inline_storage_multiply(program->symbols.count, IR_INLINE_SYMBOL_SCRATCH_BYTES),
                                                 ir_inline_storage_add(ir_inline_storage_multiply(count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
    memset(plan->required_callers, 0, count ? count : 1u);
    for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1) plan->function_by_symbol[symbol] = IR_INLINE_NONE;
    for (u32 function = 0; function < count; function += 1)
    {
        IrFunction* row = module->functions + function;
        if (row->symbol.value < program->symbols.count) plan->function_by_symbol[row->symbol.value] = function;
        plan->order[function] = function;
    }
    ir_inline_plan_facts(program, module, arena, plan, options);
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
                                                        IrInlineOptions const* options, u64 scratch_limit, u64 work_limit,
                                                        u64 base_work, u32* order_count_out)
{
    u32 count = module->function_count;
    u64 base_scratch = ir_inline_storage_multiply(program->symbols.count, IR_INLINE_SYMBOL_SCRATCH_BYTES);
    base_scratch = ir_inline_storage_add(base_scratch, ir_inline_storage_add(ir_inline_storage_multiply(count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
    u64 graph_work = ir_inline_storage_add(base_work, ir_inline_storage_multiply(count, IR_INLINE_GRAPH_FUNCTION_WORK));
    graph_work = ir_inline_storage_add(graph_work, program->symbols.count);
    plan->work_units = graph_work;
    bool result = base_scratch <= scratch_limit && graph_work <= work_limit;
    if (graph_work > work_limit)
    {
        plan->budget_reason = IR_INLINE_BUDGET_WORK;
        plan->budget_demand = graph_work;
        plan->budget_limit = work_limit;
    }
    else if (base_scratch > scratch_limit)
    {
        plan->budget_reason = IR_INLINE_BUDGET_SCRATCH;
        plan->budget_demand = base_scratch;
        plan->budget_limit = scratch_limit;
    }
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
        ir_inline_plan_facts(program, module, arena, plan, options);
        u8 const* facts = plan->callee_facts;
        u32 symbol_count = program->symbols.count;

        for (u32 function = 0; function < count && result; function += 1)
        {
            IrFunction* caller = module->functions + function;
            if (caller->state != IR_FUNCTION_LOWERED) continue;
            // Few callers name a required callee. A function whose opcode
            // summary records no call is skipped outright; the rest take a
            // streaming pass over the dense row array that rejects them
            // without chasing block lists. Every listed row is in that array,
            // so no site is missed.
            bool calls_required = false;
            if (ir_function_may_contain_opcodes(caller, IR_OPCODE_BIT(IR_OPCODE_CALL)))
            {
                IrInstruction const* rows = caller->instructions;
                u32 row_count = caller->instruction_count;
                for (u32 row = 0; row < row_count; row += 1)
                {
                    if (rows[row].opcode == IR_OPCODE_CALL && rows[row].symbol.value < symbol_count &&
                        (facts[rows[row].symbol.value] & IR_INLINE_FACT_REQUIRED))
                    {
                        calls_required = true;
                        break;
                    }
                }
            }
            for (u32 block = 0; block < caller->block_count && result && calls_required; block += 1)
            {
                for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result;
                     row = caller->instructions[row].next.value)
                {
                    IrInstruction* call = caller->instructions + row;
                    if (call->opcode != IR_OPCODE_CALL || call->symbol.value >= symbol_count ||
                        !(facts[call->symbol.value] & IR_INLINE_FACT_REQUIRED)) continue;
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
                        plan->budget_reason = IR_INLINE_BUDGET_WORK;
                        plan->budget_demand = UINT64_MAX;
                        plan->budget_limit = work_limit;
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
                            plan->budget_reason = next_work > work_limit ? IR_INLINE_BUDGET_WORK : IR_INLINE_BUDGET_SCRATCH;
                            plan->budget_demand = next_work > work_limit ? next_work : next_scratch;
                            plan->budget_limit = next_work > work_limit ? work_limit : scratch_limit;
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
                        plan->budget_reason = next_work > work_limit ? IR_INLINE_BUDGET_WORK : IR_INLINE_BUDGET_SCRATCH;
                        plan->budget_demand = next_work > work_limit ? next_work : next_scratch;
                        plan->budget_limit = next_work > work_limit ? work_limit : scratch_limit;
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
        if (total_scratch > scratch_limit || plan->work_units > work_limit)
        {
            result = false;
            plan->budget_reason = plan->work_units > work_limit ? IR_INLINE_BUDGET_WORK : IR_INLINE_BUDGET_SCRATCH;
            plan->budget_demand = plan->work_units > work_limit ? plan->work_units : total_scratch;
            plan->budget_limit = plan->work_units > work_limit ? work_limit : scratch_limit;
        }
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
u32 ir_inline_module_growth_limit(u32 max_module_growth, u64 module_rows)
{
    u32 result = max_module_growth;
    if (!result)
    {
        u64 relative = module_rows >> IR_INLINE_MODULE_GROWTH_SHIFT;
        result = relative > IR_INLINE_MODULE_GROWTH ? (u32)BUSTER_MIN(relative, (u64)UINT32_MAX) : IR_INLINE_MODULE_GROWTH;
    }
    return result;
}

// Every function the pass proves strictly valid and leaves in that state is
// published in module->inline_proven when FAST will run, so the FAST input
// guard reuses the proof (ir_prepare_canonical_module). `fault` is
// IR_INLINE_SPLICE_FAULT_NONE outside ir_test_inline_module.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_inline_module(IrProgram* program, IrModule* module, IrInlineSpliceFault fault)
{
    IrValidationResult result = (IrValidationResult){.error = IR_VALIDATION_NONE, .boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT};
    if (!program || !program->arena || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    else if (!module->inline_complete)
    {
        module->inlining = (IrInlineStatistics){0};
        module->inline_proven = 0;
        IrInlineOptions options = program->inline_options;
        if (!options.max_callee_instructions) options.max_callee_instructions = IR_INLINE_TINY_INSTRUCTIONS;
        if (!options.caller_growth_shift) options.caller_growth_shift = IR_INLINE_CALLER_GROWTH_SHIFT;
        if (options.caller_growth_shift > 32u) options.caller_growth_shift = 32u;
        program->inline_options = options;
        // Zero row/site limits keep required expansion under the independent work
        // and storage guards, rather than the optional profitability caps.
        // Preserve zero in the program so later modules use the same policy.
        u32 required_function_growth = options.max_function_growth ? options.max_function_growth : UINT32_MAX;
        u32 required_module_growth = options.max_module_growth ? options.max_module_growth : UINT32_MAX;
        u32 required_call_sites = options.max_call_sites ? options.max_call_sites : UINT32_MAX;
        if (!options.max_call_sites) options.max_call_sites = IR_INLINE_CALL_SITES;
        if (!options.max_function_growth) options.max_function_growth = IR_INLINE_FUNCTION_GROWTH;
        bool has_required = false;
        for (u32 symbol = 0; symbol < program->symbols.count; symbol += 1)
        {
            has_required |= program->symbols.symbols[symbol].always_inline;
        }
        u64 planning_rows = 0;
        u64 module_rows = 0;
        for (u32 function = 0; function < module->function_count; function += 1)
        {
            IrFunction* candidate = module->functions + function;
            if (candidate->state != IR_FUNCTION_LOWERED) continue;
            planning_rows = ir_inline_storage_add(planning_rows, candidate->block_count);
            planning_rows = ir_inline_storage_add(planning_rows, candidate->instruction_count);
            module_rows = ir_inline_storage_add(module_rows, candidate->instruction_count);
        }
        // A zero module limit stays relative in the program's options, so
        // each module derives its own; only this pass uses the result.
        options.max_module_growth = ir_inline_module_growth_limit(options.max_module_growth, module_rows);
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
            u64 planning_scratch = ir_inline_storage_multiply(program->symbols.count, IR_INLINE_SYMBOL_SCRATCH_BYTES);
            planning_scratch = ir_inline_storage_add(planning_scratch,
                                                      ir_inline_storage_add(ir_inline_storage_multiply(module->function_count, IR_INLINE_FUNCTION_SCRATCH_BYTES), IR_INLINE_PLAN_ALIGNMENT_BYTES));
            u64 optional_metadata_work = ir_inline_storage_add(ir_inline_storage_multiply(module->function_count, 2u), program->symbols.count);
            u64 initial_work = has_required ? planning_units : ir_inline_storage_add(planning_units, optional_metadata_work);
            if (initial_work > IR_FAST_WORK_BUDGET || planning_scratch > IR_FAST_SCRATCH_BUDGET)
            {
                module->inlining.visits = initial_work;
                if (has_required)
                {
                    ir_inline_record_budget_refusal(module, true,
                        initial_work > IR_FAST_WORK_BUDGET ? IR_INLINE_BUDGET_WORK : IR_INLINE_BUDGET_SCRATCH,
                        initial_work > IR_FAST_WORK_BUDGET ? initial_work : planning_scratch,
                        initial_work > IR_FAST_WORK_BUDGET ? IR_FAST_WORK_BUDGET : IR_FAST_SCRATCH_BUDGET);
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
                                     ? ir_inline_plan_graph(program, module, arena, &plan, &options, IR_FAST_SCRATCH_BUDGET, IR_FAST_WORK_BUDGET, planning_units, &order_count)
                                     : true;
                if (plan_ready && !has_required) ir_inline_plan_optional(program, module, arena, &plan, &options, &order_count);
                module->inlining.visits = has_required ? plan.work_units : ir_inline_storage_add(planning_units, plan.work_units);
                if (!plan_ready)
                {
                    if (has_required)
                    {
                        ir_inline_record_budget_refusal(module, true,
                            plan.budget_reason != IR_INLINE_BUDGET_NONE ? plan.budget_reason : IR_INLINE_BUDGET_WORK,
                            plan.budget_reason != IR_INLINE_BUDGET_NONE ? plan.budget_demand : plan.work_units,
                            plan.budget_reason != IR_INLINE_BUDGET_NONE ? plan.budget_limit : IR_FAST_WORK_BUDGET);
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
                u64 total_work = module->inlining.visits;
                u64 total_copies = 0;
                u32* function_growth_used = arena_allocate(arena, u32, count ? count : 1u);
                u64* function_storage_used = arena_allocate(arena, u64, count ? count : 1u);
                u32* function_sites_used = arena_allocate(arena, u32, count ? count : 1u);
                u8* strict = arena_allocate_zeroed(arena, u8, count ? count : 1u);
                memset(function_growth_used, 0, sizeof(u32) * (count ? count : 1u));
                memset(function_storage_used, 0, sizeof(u64) * (count ? count : 1u));
                memset(function_sites_used, 0, sizeof(u32) * (count ? count : 1u));
                u32 phase_count = options.tiny ? 2u : 1u;
                IrTypeId void_type = ir_inline_cfg_void_type(program);
                IrInlineDiscard discard = {0};
                for (u32 phase = 0; phase < phase_count && result.error == IR_VALIDATION_NONE; phase += 1)
                {
                    for (u32 order_index = 0; order_index < order_count && result.error == IR_VALIDATION_NONE; order_index += 1)
                    {
                    u32 caller_index = plan.order[order_index];
                    IrFunction* caller = module->functions + caller_index;
                    if (caller->state != IR_FUNCTION_LOWERED) continue;
                    if (phase == 0 && (!plan.required_callers || !plan.required_callers[caller_index])) continue;
                    if (phase == 1 && module_growth >= options.max_module_growth)
                    {
                        // No optional site can fit once module growth is spent.
                        module->inlining.budget_skips += 1;
                        continue;
                    }
                    u64 caller_work_limit = IR_FAST_WORK_BUDGET;
                    u64 caller_storage_limit = IR_FAST_SCRATCH_BUDGET;
                    // The optional tiny-inline heuristic keeps its existing
                    // charge, which prices a snapshot for every block.
                    u64 caller_snapshot_blocks = caller->block_count;
                    if (phase == 0)
                    {
                        caller_snapshot_blocks = ir_inline_snapshot_blocks(caller);
                        // Required expansion must relocate the caller's existing
                        // tables/snapshots. Allow that input-proportional cost
                        // once, plus the fixed allowance for expansion itself.
                        u64 relocation_snapshots = caller_snapshot_blocks ? 1u : 0u;
                        u64 relocation_work = ir_inline_reserve_work(caller, caller_snapshot_blocks, 1u, relocation_snapshots,
                                                                     1u, 1u, 1u, 0);
                        u64 relocation_bytes = ir_inline_reserve_bytes(caller, caller_snapshot_blocks, 1u, relocation_snapshots,
                                                                       1u, 1u, 1u, 0);
                        relocation_work = ir_inline_storage_add(relocation_work,
                            ir_inline_cfg_table_growth_work(caller,
                                caller->block_count, (u64)caller->block_count + 1u,
                                caller->instruction_count, (u64)caller->instruction_count + 1u,
                                caller->value_count, (u64)caller->value_count + 1u,
                                caller->extra_count, (u64)caller->extra_count + 1u));
                        relocation_bytes = ir_inline_storage_add(relocation_bytes,
                            ir_inline_cfg_table_growth_bytes(caller,
                                caller->block_count, (u64)caller->block_count + 1u,
                                caller->instruction_count, (u64)caller->instruction_count + 1u,
                                caller->value_count, (u64)caller->value_count + 1u,
                                caller->extra_count, (u64)caller->extra_count + 1u));
                        caller_work_limit = ir_inline_storage_add(caller_work_limit, relocation_work);
                        caller_storage_limit = ir_inline_storage_add(caller_storage_limit, relocation_bytes);
                    }
                    if (phase == 0)
                    {
                        // Graph planning stays module-wide. Expansion bounds
                        // each caller, as FAST does, so an earlier intrinsic
                        // consumer cannot exhaust a later caller's allowance.
                        total_work = 0;
                    }
                    if (phase == 1)
                    {
                        // Optional selection bounds work per caller, as FAST
                        // does; mandatory planning above stays module-wide.
                        total_work = caller->block_count;
                        module->inlining.visits = ir_inline_storage_add(module->inlining.visits, caller->block_count);
                        bool possible_tiny_call = false;
                        u64 recursive_sites = 0;
                        bool caller_scan_exhausted = total_work > caller_work_limit;
                        // Rows and callee scans share one running count, added
                        // to total_work and visits once after the scan.
                        u64 scan_budget = caller_work_limit - BUSTER_MIN(total_work, (u64)caller_work_limit);
                        u64 scan_used = 0;
                        IrInstruction const* scan_rows = caller->instructions;
                        for (u32 block = 0; block < caller->block_count && !possible_tiny_call && !caller_scan_exhausted; block += 1)
                        {
                            for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE;
                                 row = scan_rows[row].next.value)
                            {
                                scan_used += 1;
                                if (scan_used > scan_budget)
                                {
                                    caller_scan_exhausted = true;
                                    break;
                                }
                                if (scan_rows[row].opcode != IR_OPCODE_CALL) continue;
                                IrInstruction* call = caller->instructions + row;
                                u8 facts = call->symbol.value < program->symbols.count ? plan.callee_facts[call->symbol.value] : 0u;
                                if ((facts & (IR_INLINE_FACT_REQUIRED | IR_INLINE_FACT_NOINLINE | IR_INLINE_FACT_LINKAGE)) ==
                                        IR_INLINE_FACT_LINKAGE &&
                                    ir_inline_direct_call(caller, call))
                                {
                                    u32 callee_index = plan.function_by_symbol[call->symbol.value];
                                    IrFunction* callee = module->functions + callee_index;
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
                                    scan_used = ir_inline_storage_add(scan_used, callee_scan_work);
                                    if (scan_used > scan_budget)
                                    {
                                        caller_scan_exhausted = true;
                                        break;
                                    }
                                    possible_tiny_call = (facts & IR_INLINE_FACT_TINY) != 0;
                                    if (possible_tiny_call) break;
                                }
                            }
                        }
                        total_work = ir_inline_storage_add(total_work, scan_used);
                        module->inlining.visits = ir_inline_storage_add(module->inlining.visits, scan_used);
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
                    if (caller_plan_work > caller_work_limit - BUSTER_MIN(total_work, (u64)caller_work_limit))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0)
                        {
                            ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                ir_inline_storage_add(total_work, caller_plan_work), caller_work_limit);
                        }
                        if (phase == 0 && plan.first_required_rows[caller_index] != IR_INLINE_NONE)
                        {
                            result = ir_inline_callsite_error(caller, plan.first_required_blocks[caller_index],
                                                              plan.first_required_rows[caller_index]);
                        }
                        continue;
                    }
                    u64 caller_scratch = ir_inline_storage_multiply(caller->instruction_count, 10u);
                    caller_scratch = ir_inline_storage_add(caller_scratch,
                                                            ir_inline_storage_multiply(caller->value_count, 4u));
                    caller_scratch = ir_inline_storage_add(caller_scratch,
                                                            ir_inline_storage_multiply(caller->block_count, 4u));
                    if (phase == 0)
                    {
                        caller_scratch = ir_inline_storage_add(caller_scratch,
                            ir_inline_storage_multiply(BUSTER_MAX(caller->block_count, 1u), sizeof(u64)));
                    }
                    if (caller_scratch > IR_FAST_SCRATCH_BUDGET - BUSTER_MIN(plan.scratch_bytes, (u64)IR_FAST_SCRATCH_BUDGET))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0)
                        {
                            ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_SCRATCH,
                                ir_inline_storage_add(plan.scratch_bytes, caller_scratch), IR_FAST_SCRATCH_BUDGET);
                        }
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
                    u64* block_phi_units = phase == 0
                                               ? arena_allocate_zeroed(function_arena, u64, original_blocks ? original_blocks : 1u)
                                               : 0;

                    u64 caller_edge_units = 0;
                    u64 caller_phi_units = 0;
                    // Every block tail and the debug rows are checked once
                    // here instead of once per call site; each splice keeps
                    // tails terminated and debug scopes ordered.
                    bool caller_tails_valid = ir_inline_cfg_debug_rows_valid(caller);
                    bool caller_preflight_exhausted = false;
                    for (u32 block_index = 0; block_index < original_blocks && !caller_preflight_exhausted; block_index += 1)
                    {
                        IrBlock const* block = caller->blocks + block_index;
                        u64 block_work = 1u;
                        if (block->last_instruction.value < caller->instruction_count)
                        {
                            IrInstruction const* terminator = caller->instructions + block->last_instruction.value;
                            caller_tails_valid &= ir_instruction_is_terminator(terminator) &&
                                                  terminator->next.value == IR_ID_UNDERLYING_INVALID;
                            caller_edge_units = ir_inline_storage_add(caller_edge_units, terminator->target_count);
                            block_work = ir_inline_storage_add(block_work, 1u);
                            if (phase != 0)
                            {
                                block_work = ir_inline_storage_add(block_work, terminator->target_count);
                            }
                        }
                        else
                        {
                            caller_tails_valid = false;
                        }
                        total_work = ir_inline_storage_add(total_work, block_work);
                        module->inlining.visits = ir_inline_storage_add(module->inlining.visits, block_work);
                        if (total_work > caller_work_limit)
                        {
                            caller_preflight_exhausted = true;
                            break;
                        }
                        for (IrBlockParameter const* parameter = block->first_parameter;
                             parameter && !caller_preflight_exhausted; parameter = parameter->next)
                        {
                            u64 edge_work = ir_inline_storage_add(1u, parameter->incoming_count);
                            caller_phi_units = ir_inline_storage_add(caller_phi_units, edge_work);
                            if (block_phi_units)
                            {
                                block_phi_units[block_index] =
                                    ir_inline_storage_add(block_phi_units[block_index], edge_work);
                            }
                            // The required splice visits each caller parameter
                            // once here. Incoming edges are charged only for
                            // successors whose original tail moves below.
                            u64 parameter_work = phase == 0 ? 1u : edge_work;
                            total_work = ir_inline_storage_add(total_work, parameter_work);
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, parameter_work);
                            if (total_work > caller_work_limit) caller_preflight_exhausted = true;
                        }
                    }
                    if (caller_preflight_exhausted)
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0)
                        {
                            ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                total_work, caller_work_limit);
                        }
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
                    u64 module_growth_before = module_growth;
                    u64 total_copies_before = total_copies;
                    u64 function_projected_units = 0;
                    u64 function_projected_blocks = 0;
                    u64 function_projected_locals = 0;
                    u64 function_projected_instructions = 0;
                    u64 function_projected_values = 0;
                    u64 function_projected_extras = 0;
                    u64 function_projected_debug = 0;
                    u64 function_projected_edges = 0;
                    u64 function_projected_scopes = 0;
                    u64 function_projected_snapshots = 0;
                    u32 function_sites = function_sites_used[caller_index];
                    u64 caller_main_scan = ir_inline_storage_add(original_instructions, original_blocks);
                    if (caller_main_scan > caller_work_limit -
                                               BUSTER_MIN(total_work, (u64)caller_work_limit))
                    {
                        module->inlining.budget_skips += 1;
                        if (phase == 0)
                        {
                            ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                ir_inline_storage_add(total_work, caller_main_scan), caller_work_limit);
                        }
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
                    // caller_main_scan above reserved these rows. Each row is
                    // charged one unit of total_work and visits; the charge is
                    // settled at every call row, before any budget reads it,
                    // and after the walk, instead of once per row.
                    u32 caller_rows_scanned = 0;
                    u32 caller_rows_charged = 0;
                    bool caller_validation_work_charged = false;
                    IrInstruction const* caller_rows = caller->instructions;
                    for (u32 block = 0; block < original_blocks && result.error == IR_VALIDATION_NONE; block += 1)
                    {
                        u32 block_rows_before = caller_rows_scanned;
                        bool moved_tail_work_charged = false;
                        for (u32 row = caller->blocks[block].first_instruction.value; row != IR_INLINE_NONE && result.error == IR_VALIDATION_NONE;
                             row = caller_rows[row].next.value)
                        {
                            caller_rows_scanned += 1;
                            if (caller_rows[row].opcode != IR_OPCODE_CALL) continue;
                            total_work += caller_rows_scanned - caller_rows_charged;
                            module->inlining.visits += caller_rows_scanned - caller_rows_charged;
                            caller_rows_charged = caller_rows_scanned;
                            IrInstruction* call = caller->instructions + row;
                            bool symbol_known = call->symbol.value < program->symbols.count;
                            u8 facts = symbol_known ? plan.callee_facts[call->symbol.value] : 0u;
                            bool required = (facts & IR_INLINE_FACT_REQUIRED) != 0;
                            if (phase == 0 ? !required
                                           : required || !symbol_known || !options.tiny || (facts & IR_INLINE_FACT_NOINLINE))
                                continue;
                            bool direct = ir_inline_direct_call(caller, call);
                            if (phase == 1 && !direct) continue;
                            module->inlining.candidates += 1;
                            u32 callee_index = symbol_known ? plan.function_by_symbol[call->symbol.value] : IR_INLINE_NONE;
                            IrFunction* callee = callee_index < count ? module->functions + callee_index : 0;
                            if (!direct || !(facts & IR_INLINE_FACT_LINKAGE) || (required && (facts & IR_INLINE_FACT_NOINLINE)))
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
                            // Existing caller label values and destination IDs
                            // survive the splice. Only callee-owned computed
                            // labels need a cloning contract and remain refused.
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
                                    caller_work_limit -
                                        BUSTER_MIN(tiny_work_with_caller_rows, (u64)caller_work_limit))
                                {
                                    module->inlining.budget_skips += 1;
                                    continue;
                                }
                                total_work = ir_inline_storage_add(total_work, tiny_scan_work);
                                module->inlining.visits =
                                    ir_inline_storage_add(module->inlining.visits, tiny_scan_work);
                            }
                            bool tiny_eligible = required || (facts & IR_INLINE_FACT_TINY);
                            if (!tiny_eligible)
                            {
                                module->inlining.shape_skips += 1;
                                continue;
                            }
                            // A legacy certified shape stays a call when optional;
                            // a source directive reports the call instead. An
                            // optional site's caller is checked below, only once
                            // the relocation bound keeps its planned sites.
                            if ((required && !ir_inline_function_strict(program, module, strict, caller_index)) ||
                                !ir_inline_function_strict(program, module, strict, callee_index))
                            {
                                module->inlining.shape_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            u64 callee_payload_units = callee->extra_count;
                            u64 callee_target_units = 0;
                            u64 callee_return_units = 0;
                            u64 callee_snapshot_blocks = 0;
                            bool callee_scan_exhausted = false;
                            u64 callee_scan_demand = 0;
                            for (u32 row_id = 0; row_id < callee->instruction_count && !callee_scan_exhausted; row_id += 1)
                            {
                                u64 reserved_work = ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (reserved_work >= caller_work_limit)
                                {
                                    callee_scan_demand = ir_inline_storage_add(reserved_work, 1u);
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                IrInstruction const* source = callee->instructions + row_id;
                                u64 payload = source->operand_count;
                                payload = ir_inline_storage_add(payload, source->target_count);
                                payload = ir_inline_storage_add(payload, source->immediate_count);
                                u64 scan_work = ir_inline_storage_add(payload, 1u);
                                if (scan_work > caller_work_limit -
                                                    BUSTER_MIN(reserved_work, (u64)caller_work_limit))
                                {
                                    callee_scan_demand = ir_inline_storage_add(reserved_work, scan_work);
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                total_work = ir_inline_storage_add(total_work, scan_work);
                                module->inlining.visits = ir_inline_storage_add(module->inlining.visits, scan_work);
                                callee_payload_units = ir_inline_storage_add(callee_payload_units, payload);
                                callee_target_units = ir_inline_storage_add(callee_target_units, source->target_count);
                                callee_return_units =
                                    ir_inline_storage_add(callee_return_units, source->opcode == IR_OPCODE_RETURN ? 1u : 0u);
                                if (total_work > caller_work_limit)
                                {
                                    callee_scan_demand = total_work;
                                    callee_scan_exhausted = true;
                                }
                            }
                            for (u32 block_id = 0; block_id < callee->block_count && !callee_scan_exhausted; block_id += 1)
                            {
                                u64 reserved_work = ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (reserved_work >= caller_work_limit - 1u)
                                {
                                    callee_scan_demand = ir_inline_storage_add(reserved_work, 2u);
                                    callee_scan_exhausted = true;
                                    break;
                                }
                                total_work = ir_inline_storage_add(total_work, 1u);
                                module->inlining.visits = ir_inline_storage_add(module->inlining.visits, 1u);
                                callee_snapshot_blocks += callee->blocks[block_id].local_values != 0;
                                for (IrBlockParameter const* parameter = callee->blocks[block_id].first_parameter;
                                     parameter && !callee_scan_exhausted; parameter = parameter->next)
                                {
                                    u64 reserved_parameter_work =
                                        ir_inline_storage_add(total_work, caller_rows_remaining);
                                    if (reserved_parameter_work >= caller_work_limit)
                                    {
                                        callee_scan_demand = ir_inline_storage_add(reserved_parameter_work, 1u);
                                        callee_scan_exhausted = true;
                                        break;
                                    }
                                    u64 parameter_work = ir_inline_storage_add(1u, parameter->incoming_count);
                                    if (parameter_work > caller_work_limit -
                                                             BUSTER_MIN(reserved_parameter_work,
                                                                        (u64)caller_work_limit))
                                    {
                                        callee_scan_demand = ir_inline_storage_add(reserved_parameter_work, parameter_work);
                                        callee_scan_exhausted = true;
                                        break;
                                    }
                                    total_work = ir_inline_storage_add(total_work, parameter_work);
                                    module->inlining.visits =
                                        ir_inline_storage_add(module->inlining.visits, parameter_work);
                                    callee_payload_units = ir_inline_storage_add(callee_payload_units, parameter_work);
                                    if (total_work > caller_work_limit)
                                    {
                                        callee_scan_demand = total_work;
                                        callee_scan_exhausted = true;
                                    }
                                }
                            }
                            if (callee_scan_exhausted)
                            {
                                module->inlining.budget_skips += 1;
                                if (required)
                                {
                                    ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                        callee_scan_demand ? callee_scan_demand : total_work, caller_work_limit);
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
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
                            u64 prefix_rows = caller_rows_scanned - block_rows_before;
                            u64 caller_work = 0;
                            if (phase == 0)
                            {
                                // The splice rechecks each call-block prefix and repairs
                                // only moved-tail successor phis. Charge the output proof's
                                // caller phis once per caller and its second scan of each
                                // spliced original block. The preflight's caller debug-row
                                // check also runs once per caller.
                                u64 caller_proof_and_debug_work = 0;
                                if (!caller_validation_work_charged)
                                {
                                    caller_validation_work_charged = true;
                                    caller_proof_and_debug_work = caller_phi_units;
                                    caller_proof_and_debug_work =
                                        ir_inline_storage_add(caller_proof_and_debug_work, caller->debug_local_count);
                                    caller_proof_and_debug_work =
                                        ir_inline_storage_add(caller_proof_and_debug_work, caller->debug_scope_count);
                                }
                                u64 moved_tail_work = 0;
                                if (!moved_tail_work_charged)
                                {
                                    moved_tail_work_charged = true;
                                    // The output proof revisits every old caller phi once,
                                    // then checks this spliced block's phis a second time.
                                    if (block_phi_units)
                                    {
                                        moved_tail_work =
                                            ir_inline_storage_add(moved_tail_work, block_phi_units[block]);
                                    }
                                    u32 tail_id = caller->blocks[block].last_instruction.value;
                                    IrInstruction const* moved_tail =
                                        tail_id < original_instructions ? caller->instructions + tail_id : 0;
                                    if (moved_tail && moved_tail->targets)
                                    {
                                        for (u32 target = 0; target < moved_tail->target_count; target += 1)
                                        {
                                            // Planning reads the edge once and the splice
                                            // walks it again; repeated targets are repeated work.
                                            moved_tail_work = ir_inline_storage_add(moved_tail_work, 2u);
                                            u32 successor = moved_tail->targets[target].value;
                                            if (successor < original_blocks && block_phi_units)
                                            {
                                                moved_tail_work = ir_inline_storage_add(moved_tail_work,
                                                                                      block_phi_units[successor]);
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    // Earlier source-order calls splice after the first call's
                                    // one-edge branch into a parameterless callee entry.
                                    moved_tail_work = 1u;
                                }
                                caller_work = ir_inline_storage_multiply(prefix_rows, aggregate_result ? 3u : 2u);
                                caller_work = ir_inline_storage_add(caller_work, moved_tail_work);
                                caller_work = ir_inline_storage_add(caller_work, caller_proof_and_debug_work);
                            }
                            else
                            {
                                // Preserve the optional tiny-inline heuristic's
                                // existing per-caller work charge.
                                caller_work = ir_inline_storage_multiply(projected_blocks_before, 7u);
                                caller_work = ir_inline_storage_add(caller_work,
                                                                   ir_inline_storage_multiply(prefix_rows, 3u));
                                caller_work = ir_inline_storage_add(caller_work,
                                    ir_inline_storage_multiply(caller->debug_local_count, 3u));
                                caller_work = ir_inline_storage_add(caller_work, caller_phi_units);
                                caller_work = ir_inline_storage_add(caller_work, function_projected_units);
                            }
                            // Predecessor rebuilding walks targets, not all cloned payload rows.
                            u64 projected_edges = ir_inline_storage_add(caller_edge_units, function_projected_edges);
                            projected_edges = ir_inline_storage_add(projected_edges, callee_target_units);
                            projected_edges = ir_inline_storage_add(projected_edges,
                                                                   ir_inline_storage_add(callee_return_units, 1u));
                            bool callee_has_debug = callee->debug_local_count != 0 || callee->debug_scope_count != 0;
                            u64 projected_scopes_after = ir_inline_storage_add(function_projected_scopes,
                                                                                callee_has_debug ? (u64)callee->debug_scope_count + 1u : 0u);
                            u64 projected_debug_after = ir_inline_storage_add(function_projected_debug, callee->debug_local_count);
                            u64 reserve_edges_before = ir_inline_storage_add(caller_edge_units, function_projected_edges);
                            // A cloned block carries a snapshot when its callee block or
                            // the call block does; the continuation, when the call block does.
                            u64 site_snapshots = phase != 0 || caller->blocks[block].local_values
                                                     ? (u64)callee->block_count + 1u : callee_snapshot_blocks;
                            u64 projected_snapshots_after = ir_inline_storage_add(function_projected_snapshots, site_snapshots);
                            u64 reserve_work_before = ir_inline_reserve_work(caller, caller_snapshot_blocks, function_projected_blocks,
                                                                             function_projected_snapshots, function_projected_locals,
                                                                             function_projected_debug, function_projected_scopes,
                                                                             reserve_edges_before);
                            u64 reserve_work_after = ir_inline_reserve_work(caller, caller_snapshot_blocks,
                                                                            projected_blocks_after - caller->block_count,
                                                                            projected_snapshots_after,
                                                                            projected_locals_after - caller->local_count,
                                                                            projected_debug_after, projected_scopes_after, projected_edges);
                            caller_work = ir_inline_storage_add(caller_work,
                                                                reserve_work_after == UINT64_MAX ? UINT64_MAX
                                                                                                 : reserve_work_after - BUSTER_MIN(reserve_work_before, reserve_work_after));
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
                            if (phase == 0)
                            {
                                // Required callee debug scopes are validated during planning and again at splice.
                                callee_work = ir_inline_storage_add(callee_work,
                                                                   ir_inline_storage_multiply(callee->debug_scope_count, 2u));
                            }
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
                                if (required)
                                {
                                    ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_SCRATCH,
                                        ir_inline_storage_add(active_scratch, support_scratch), IR_FAST_SCRATCH_BUDGET);
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
                                continue;
                            }
                            u64 candidate_work = ir_inline_storage_add(caller_work, callee_work);
                            candidate_work = ir_inline_storage_add(candidate_work, call->operand_count);
                            u64 work_with_caller_rows = ir_inline_storage_add(total_work, caller_rows_remaining);
                            if (candidate_work >
                                caller_work_limit - BUSTER_MIN(work_with_caller_rows, (u64)caller_work_limit))
                            {
                                module->inlining.budget_skips += 1;
                                if (required)
                                {
                                    ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                        ir_inline_storage_add(work_with_caller_rows, candidate_work), caller_work_limit);
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
                                continue;
                            }
                            total_work += candidate_work;
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, candidate_work);
                            u32 copied = 0;
                            u32 growth = 0;
                            bool shape = caller_tails_valid && projected_edges <= UINT32_MAX &&
                                         ir_inline_cfg_supported(program, caller, (IrBlockId){.value = block},
                                                                 (IrInstructionId){.value = row}, callee, true, &copied, &growth);
                            if (!shape)
                            {
                                module->inlining.shape_skips += 1;
                                if (required) result = ir_inline_callsite_error(caller, block, row);
                                continue;
                            }
                            u64 storage = ir_inline_cfg_storage_site_bytes(caller, callee);
                            u64 table_blocks_before = ir_inline_storage_add(caller->block_count, function_projected_blocks);
                            u64 table_blocks_after = ir_inline_storage_add(table_blocks_before, (u64)callee->block_count + 1u);
                            u64 table_instructions_before =
                                ir_inline_storage_add(caller->instruction_count, function_projected_instructions);
                            u64 table_instructions_after = ir_inline_storage_add(table_instructions_before, growth);
                            u64 table_values_before = ir_inline_storage_add(caller->value_count, function_projected_values);
                            u64 table_values_after =
                                ir_inline_storage_add(table_values_before, (u64)callee->value_count + (aggregate_result ? 1u : 0u));
                            u64 table_extras_before = ir_inline_storage_add(caller->extra_count, function_projected_extras);
                            u64 table_extras_after = ir_inline_storage_add(table_extras_before, callee->extra_count);
                            u64 table_work = ir_inline_cfg_table_growth_work(caller,
                                                                                 table_blocks_before, table_blocks_after,
                                                                                 table_instructions_before, table_instructions_after,
                                                                                 table_values_before, table_values_after,
                                                                                 table_extras_before, table_extras_after);
                            u64 table_work_with_caller_rows = ir_inline_storage_add(total_work, caller_rows_remaining);
                            if (table_work > caller_work_limit -
                                                 BUSTER_MIN(table_work_with_caller_rows, (u64)caller_work_limit))
                            {
                                module->inlining.budget_skips += 1;
                                if (required)
                                {
                                    ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                        ir_inline_storage_add(table_work_with_caller_rows, table_work), caller_work_limit);
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
                                continue;
                            }
                            total_work = ir_inline_storage_add(total_work, table_work);
                            module->inlining.visits = ir_inline_storage_add(module->inlining.visits, table_work);
                            u64 table_growth = ir_inline_cfg_table_growth_bytes(caller,
                                                                                table_blocks_before, table_blocks_after,
                                                                                table_instructions_before, table_instructions_after,
                                                                                table_values_before, table_values_after,
                                                                                table_extras_before, table_extras_after);
                            storage = ir_inline_storage_add(storage, table_growth);
                            u64 reserve_bytes_before = ir_inline_reserve_bytes(caller, caller_snapshot_blocks, function_projected_blocks,
                                                                               function_projected_snapshots, function_projected_locals,
                                                                               function_projected_debug, function_projected_scopes,
                                                                               reserve_edges_before);
                            u64 reserve_bytes_after = ir_inline_reserve_bytes(caller, caller_snapshot_blocks,
                                                                              projected_blocks_after - caller->block_count,
                                                                              projected_snapshots_after,
                                                                              projected_locals_after - caller->local_count,
                                                                              projected_debug_after, projected_scopes_after, projected_edges);
                            u64 reserve_growth = reserve_bytes_after == UINT64_MAX
                                                     ? UINT64_MAX
                                                     : reserve_bytes_after - BUSTER_MIN(reserve_bytes_before, reserve_bytes_after);
                            u64 clone_maps = ir_inline_storage_multiply(callee->value_count, sizeof(IrValueId));
                            clone_maps = ir_inline_storage_add(clone_maps,
                                                               ir_inline_storage_multiply(callee->block_count, sizeof(IrBlockId)));
                            clone_maps = ir_inline_storage_add(clone_maps,
                                                               ir_inline_storage_multiply(callee->instruction_count,
                                                                                          sizeof(IrInstructionId)));
                            u64 allocation_alignment = BUSTER_ALIGN_OF(IrBlock);
                            if (BUSTER_ALIGN_OF(IrInstruction) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrInstruction);
                            if (BUSTER_ALIGN_OF(IrSourceRange) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrSourceRange);
                            if (BUSTER_ALIGN_OF(IrValue) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrValue);
                            if (BUSTER_ALIGN_OF(IrValueId) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrValueId);
                            if (BUSTER_ALIGN_OF(IrBlockId) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrBlockId);
                            if (BUSTER_ALIGN_OF(IrInstructionId) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrInstructionId);
                            if (BUSTER_ALIGN_OF(IrBlockParameter) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrBlockParameter);
                            if (BUSTER_ALIGN_OF(IrIncoming) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrIncoming);
                            if (BUSTER_ALIGN_OF(IrDebugLocal) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrDebugLocal);
                            if (BUSTER_ALIGN_OF(IrPredecessor) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrPredecessor);
                            if (BUSTER_ALIGN_OF(IrInstructionExtra) > allocation_alignment)
                                allocation_alignment = BUSTER_ALIGN_OF(IrInstructionExtra);
                            u64 allocation_count = ir_inline_storage_add(16u,
                                ir_inline_storage_add(ir_inline_storage_multiply(callee->instruction_count, 3u),
                                    ir_inline_storage_add((u64)callee->block_count + 1u, callee_payload_units)));
                            u64 alignment_bytes = ir_inline_storage_multiply(allocation_count, allocation_alignment - 1u);
                            storage = ir_inline_storage_add(storage, reserve_growth);
                            storage = ir_inline_storage_add(storage, clone_maps);
                            storage = ir_inline_storage_add(storage, alignment_bytes);
                            u64 projected_units = copied;
                            projected_units = ir_inline_storage_add(projected_units, callee->value_count + (aggregate_result ? 1u : 0u));
                            projected_units = ir_inline_storage_add(projected_units, (u64)callee->block_count + 1u);
                            projected_units = ir_inline_storage_add(projected_units, callee->local_count + (aggregate_result ? 1u : 0u));
                            projected_units = ir_inline_storage_add(projected_units, callee->debug_local_count);
                            projected_units = ir_inline_storage_add(projected_units, callee->extra_count);
                            projected_units = ir_inline_storage_add(projected_units, callee_payload_units);
                            if (phase == 0)
                            {
                                // Keep the final caller validation and predecessor
                                // rebuild within the same finite per-caller work
                                // allowance as the splice planning that precedes it.
                                u64 projected_work_with_caller_rows =
                                    ir_inline_storage_add(total_work, caller_rows_remaining);
                                if (projected_units > caller_work_limit -
                                                          BUSTER_MIN(projected_work_with_caller_rows,
                                                                     (u64)caller_work_limit))
                                {
                                    module->inlining.budget_skips += 1;
                                    ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                        ir_inline_storage_add(projected_work_with_caller_rows, projected_units),
                                        caller_work_limit);
                                    result = ir_inline_callsite_error(caller, block, row);
                                    continue;
                                }
                                // Charge each projected clone once, after it fits.
                                total_work = ir_inline_storage_add(total_work, projected_units);
                                module->inlining.visits =
                                    ir_inline_storage_add(module->inlining.visits, projected_units);
                            }
                            // Reserve transient argument and predecessor maps
                            // alongside the retained graph and caller-planning arrays.
                            u64 projected_scratch = ir_inline_storage_add(function_projected_units, projected_units);
                            projected_scratch = ir_inline_storage_multiply(projected_scratch, 5u);
                            projected_scratch = ir_inline_storage_add(projected_scratch,
                                                                     ir_inline_storage_multiply(projected_blocks_after, sizeof(u32)));
                            projected_scratch = ir_inline_storage_add(projected_scratch, support_scratch);
                            // Optional sites bound retained storage per caller, as
                            // FAST does; module-wide expansion stays bounded by
                            // max_module_growth rows. Splicing into a caller copies
                            // its exactly sized tables, so a module-wide byte cap
                            // would let a few large callers starve the rest.
                            bool scratch_within = projected_scratch <= IR_FAST_SCRATCH_BUDGET -
                                                                        BUSTER_MIN(active_scratch, (u64)IR_FAST_SCRATCH_BUDGET);
                            u32 function_growth_limit = required ? required_function_growth : options.max_function_growth;
                            u32 module_growth_limit = required ? required_module_growth : options.max_module_growth;
                            u32 call_sites_limit = required ? required_call_sites : options.max_call_sites;
                            bool within = scratch_within && total_work <= caller_work_limit &&
                                          function_sites < call_sites_limit && storage != UINT64_MAX &&
                                          storage <= caller_storage_limit - BUSTER_MIN(function_storage, caller_storage_limit) &&
                                          growth <= function_growth_limit -
                                                        BUSTER_MIN(function_growth, function_growth_limit) &&
                                          growth <= module_growth_limit -
                                                        BUSTER_MIN(module_growth, module_growth_limit) &&
                                          copied <= IR_FAST_WORK_BUDGET - BUSTER_MIN(total_copies, (u64)IR_FAST_WORK_BUDGET);
                            if (!within)
                            {
                                module->inlining.budget_skips += 1;
                                if (required)
                                {
                                    if (!scratch_within)
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_SCRATCH,
                                            ir_inline_storage_add(active_scratch, projected_scratch),
                                            IR_FAST_SCRATCH_BUDGET);
                                    }
                                    else if (total_work > caller_work_limit)
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_WORK,
                                            total_work, caller_work_limit);
                                    }
                                    else if (function_sites >= call_sites_limit)
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_CALL_SITES,
                                            (u64)function_sites + 1u, call_sites_limit);
                                    }
                                    else if (storage == UINT64_MAX ||
                                             storage > caller_storage_limit -
                                                           BUSTER_MIN(function_storage, caller_storage_limit))
                                    {
                                        u64 storage_base = function_storage;
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_STORAGE,
                                            storage == UINT64_MAX ? UINT64_MAX :
                                                ir_inline_storage_add(storage_base, storage),
                                            caller_storage_limit);
                                    }
                                    else if (growth > function_growth_limit -
                                                          BUSTER_MIN(function_growth, function_growth_limit))
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_FUNCTION_GROWTH,
                                            ir_inline_storage_add(function_growth, growth),
                                            function_growth_limit);
                                    }
                                    else if (growth > module_growth_limit -
                                                          BUSTER_MIN(module_growth, module_growth_limit))
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_MODULE_GROWTH,
                                            ir_inline_storage_add(module_growth, growth),
                                            module_growth_limit);
                                    }
                                    else
                                    {
                                        ir_inline_record_budget_refusal(module, true, IR_INLINE_BUDGET_COPY_ROWS,
                                            ir_inline_storage_add(total_copies, copied), IR_FAST_WORK_BUDGET);
                                    }
                                    result = ir_inline_callsite_error(caller, block, row);
                                }
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
                            function_projected_instructions = ir_inline_storage_add(function_projected_instructions, growth);
                            function_projected_values =
                                ir_inline_storage_add(function_projected_values,
                                                      (u64)callee->value_count + (aggregate_result ? 1u : 0u));
                            function_projected_extras =
                                ir_inline_storage_add(function_projected_extras, callee->extra_count);
                            function_projected_debug =
                                ir_inline_storage_add(function_projected_debug, callee->debug_local_count);
                            function_projected_scopes = projected_scopes_after;
                            function_projected_snapshots = projected_snapshots_after;
                            u64 cloned_edge_delta = ir_inline_storage_add(callee_target_units,
                                                                                 ir_inline_storage_add(callee_return_units, 1u));
                            function_projected_edges =
                                ir_inline_storage_add(function_projected_edges, cloned_edge_delta);
                            module_growth += growth;
                            total_copies += copied;
                        }
                    }
                    total_work += caller_rows_scanned - caller_rows_charged;
                    module->inlining.visits += caller_rows_scanned - caller_rows_charged;
                    // The first splice relocates the caller's exactly sized
                    // tables, and the arena keeps the old copy. Optional sites
                    // stay calls unless their planned growth is worth that
                    // relocation (IrInlineOptions.caller_growth_shift); mandatory
                    // sites are never dropped here. Only a caller that keeps
                    // optional sites pays for its strict check, which a dropped
                    // large caller would otherwise spend for nothing (#3854).
                    u32 planned_sites = function_sites - function_sites_used[caller_index];
                    bool relocation_bounded = phase == 1 && planned_sites &&
                        ((u64)(function_growth - function_growth_used[caller_index]) << options.caller_growth_shift) <
                            original_instructions;
                    bool caller_invalid = phase == 1 && planned_sites && !relocation_bounded &&
                                          !ir_inline_function_strict(program, module, strict, caller_index);
                    if (relocation_bounded || caller_invalid)
                    {
                        if (relocation_bounded) module->inlining.budget_skips += planned_sites;
                        else module->inlining.shape_skips += planned_sites;
                        for (u32 block = 0; block < original_blocks; block += 1) planned_heads[block] = IR_INLINE_NONE;
                        function_sites = function_sites_used[caller_index];
                        function_growth = function_growth_used[caller_index];
                        function_storage = function_storage_used[caller_index];
                        module_growth = module_growth_before;
                        total_copies = total_copies_before;
                    }
                    bool changed = false;
                    // Every caller with planned sites passed the strict check
                    // as it stands now (as a mandatory site's caller, after the
                    // relocation bound, or as an earlier batch's output), so
                    // the batch's output check needs only what it rewrites.
                    bool proof_base = strict[caller_index] == IR_INLINE_STRICT_VALID && !caller->published_cfg;
                    IrValidationSplice region = {0};
                    if (function_sites != function_sites_used[caller_index])
                    {
                        if (proof_base) region = ir_inline_splice_region(function_arena, caller, planned_heads, original_blocks);
                        // One growth per table, to exactly the counts admission
                        // charged (as geometric growth from the current capacity),
                        // so no table is reallocated when its final count still
                        // fits. A splice adds exactly its planned rows.
                        ir_inline_cfg_reserve_tables(program->arena, caller,
                            ir_inline_storage_add(caller->block_count, function_projected_blocks),
                            ir_inline_storage_add(caller->instruction_count, function_projected_instructions),
                            ir_inline_storage_add(caller->value_count, function_projected_values),
                            ir_inline_storage_add(caller->extra_count, function_projected_extras), &discard);
                    }
                    IrInlineCfgReserve reserve = ir_inline_cfg_reserve(caller, function_projected_locals, function_projected_debug,
                                                                       function_projected_scopes);
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
                                                           (IrInstructionId){.value = row}, callee, &reserve, void_type, &copied))
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
                        // assigns every cloned or synthetic result definition; predecessor lists are rebuilt once per caller
                        // here, after its last splice. The stale operand
                        // population is ignored while operand_total_rows differs from instruction_count; opcode_summary only
                        // over-approximates, so no full-function remap or compaction is needed here.
                        ir_inline_cfg_rebuild_predecessors(program->arena, caller);
                        ir_function_invalidate_cfg(caller);
                        u32 caller_symbol = caller->symbol.value;
                        if (caller_symbol < program->symbols.count && plan.function_by_symbol[caller_symbol] == caller_index &&
                            (plan.callee_facts[caller_symbol] & IR_INLINE_FACT_LINKAGE))
                        {
                            plan.callee_facts[caller_symbol] = (u8)((plan.callee_facts[caller_symbol] & ~IR_INLINE_FACT_TINY) |
                                                                    ir_inline_tiny_fact(caller, &options));
                        }
#if BUSTER_INCLUDE_TESTS
                        ir_inline_plant_fault(caller, &region, fault);
#endif
                        // The splice proof decides alone in a production build.
                        // When it refuses, the full check gives the verdict and
                        // the location; a checked build runs the full check
                        // anyway, so a write outside the region is still caught.
                        bool proven = proof_base && ir_validate_canonical_splice(program, caller, &region).error == IR_VALIDATION_NONE;
                        module->inlining.splice_proofs += proven;
                        module->inlining.splice_fallbacks += !proven;
                        IrValidationResult output = !proven || BUSTER_IR_TRANSFORM_CHECKS ? ir_validate_canonical_function(program, caller)
                                                                                          : ir_validation_ok();
                        strict[caller_index] = output.error == IR_VALIDATION_NONE ? IR_INLINE_STRICT_VALID : IR_INLINE_STRICT_INVALID;
                        if (output.error != IR_VALIDATION_NONE)
                        {
                            result = output;
                            result.boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT;
                        }
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
                // No pointer reads a relocated caller's old tables; hand the
                // remaining open runs back before codegen peaks.
                module->inlining.released_bytes += ir_inline_cfg_discard_release(&discard);
                if (result.error == IR_VALIDATION_NONE) module->inline_complete = true;
                // Only the FAST input guard reads the proofs, and it runs next.
                bool any_proven = false;
                for (u32 index = 0; index < count && !any_proven; index += 1) any_proven = strict[index] == IR_INLINE_STRICT_VALID;
                if (result.error == IR_VALIDATION_NONE && program->fast_passes && any_proven)
                {
                    module->inline_proven = arena_allocate(program->arena, u8, count);
                    for (u32 index = 0; index < count; index += 1)
                    {
                        module->inline_proven[index] = strict[index] == IR_INLINE_STRICT_VALID && !module->functions[index].published_cfg;
                    }
                }
                }
                scratch_end(planning);
            }
        }
    }
#if !BUSTER_INCLUDE_TESTS
    (void)fault;
#endif
    return result;
}

#if BUSTER_INCLUDE_TESTS
IrValidationResult ir_test_inline_module(IrProgram* program, IrModule* module, IrInlineSpliceFault fault)
{
    IrValidationResult result = ir_inline_module(program, module, fault);
    return result;
}
#endif
