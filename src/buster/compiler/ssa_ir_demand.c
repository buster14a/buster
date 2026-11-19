// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Construct the private-value experiment's joins on demand. Ordinary instruction
// IDs stay opaque. The shared simplifier and the IR remapper own final IDs.
typedef struct SsaIrDemand SsaIrDemand;
struct SsaIrDemand
{
    SsaWork* work;
    uint32_t* entry_stamp;
    uint32_t* entry_value;
    uint32_t* exit_stamp;
    uint32_t* exit_value;
    uint32_t* trail;
    uint32_t* blocks;
    uint32_t* locals;
    uint32_t* offsets;
    uint32_t* operands;
    uint32_t count;
    uint32_t capacity;
    uint32_t operand_count;
    uint32_t operand_capacity;
    SsaError error;
};

static uint32_t* ssa_ir_grow_column(Arena* arena, const uint32_t* old, uint32_t used, uint64_t capacity)
{
    uint32_t* next = ssa_allocate(arena, capacity, sizeof(uint32_t));
    if (next != NULL && used != 0)
    {
        memcpy(next, old, (size_t)used * sizeof(*next));
    }
    return next;
}

static bool ssa_ir_reserve_join(SsaIrDemand* plan)
{
    if (plan->count == plan->capacity)
    {
        const uint64_t limit = (uint64_t)BUSTER_SSA_NONE - plan->work->result.phi_base;
        const uint64_t growth = plan->capacity ? (uint64_t)plan->capacity * 2 : 16;
        const uint64_t capacity = growth < limit ? growth : limit;
        if (plan->count >= limit)
        {
            plan->error = SSA_ERROR_LIMIT;
        }
        else
        {
            Arena* arena = plan->work->scratch;
            plan->blocks = ssa_ir_grow_column(arena, plan->blocks, plan->count, capacity);
            plan->locals = ssa_ir_grow_column(arena, plan->locals, plan->count, capacity);
            plan->offsets = ssa_ir_grow_column(arena, plan->offsets, plan->count, capacity + 1);
            if (plan->blocks == NULL || plan->locals == NULL || plan->offsets == NULL)
            {
                plan->error = SSA_ERROR_MEMORY;
            }
            plan->capacity = (uint32_t)capacity;
        }
    }
    return plan->error == SSA_OK;
}

static bool ssa_ir_reserve_operands(SsaIrDemand* plan, uint32_t additional)
{
    const uint64_t need = (uint64_t)plan->operand_count + additional;
    if (need > plan->operand_capacity)
    {
        uint64_t capacity = plan->operand_capacity ? (uint64_t)plan->operand_capacity * 2 : 32;
        capacity = need > capacity ? need : capacity;
        if (need >= BUSTER_SSA_NONE)
        {
            plan->error = SSA_ERROR_LIMIT;
        }
        else
        {
            capacity = capacity < BUSTER_SSA_NONE ? capacity : (uint64_t)BUSTER_SSA_NONE - 1;
            plan->operands = ssa_ir_grow_column(plan->work->scratch, plan->operands, plan->operand_count, capacity);
            plan->operand_capacity = (uint32_t)capacity;
            if (plan->operands == NULL)
            {
                plan->error = SSA_ERROR_MEMORY;
            }
        }
    }
    return plan->error == SSA_OK;
}

// Reachable single-predecessor chains end at entry, a write or a join. A real
// join gets its identity before the predecessor walk, including on a backedge.
static uint32_t ssa_ir_read_entry(SsaIrDemand* plan, uint32_t local, uint32_t b)
{
    const uint32_t stamp = local + 1;
    const SsaResult* view = &plan->work->result;
    uint32_t depth = 0;
    bool from_exit = false;
    uint32_t answer = plan->work->input->instruction_count + local;
    while (plan->entry_stamp[b] != stamp)
    {
        plan->trail[depth++] = b;
        const uint32_t first = view->predecessor_offsets[b];
        const uint32_t past = view->predecessor_offsets[b + 1];
        if (past == first)
        {
            break;
        }
        if (past - first > 1)
        {
            if (ssa_ir_reserve_join(plan))
            {
                const uint32_t j = plan->count++;
                plan->blocks[j] = b;
                plan->locals[j] = local;
                plan->offsets[j] = 0;
                answer = view->phi_base + j;
            }
            break;
        }
        b = view->predecessors[first];
        if (plan->exit_stamp[b] == stamp)
        {
            answer = plan->exit_value[b];
            from_exit = true;
            break;
        }
    }
    if (!from_exit && plan->entry_stamp[b] == stamp)
    {
        answer = plan->entry_value[b];
    }
    while (depth > 0)
    {
        const uint32_t cell = plan->trail[--depth];
        plan->entry_stamp[cell] = stamp;
        plan->entry_value[cell] = answer;
    }
    return answer;
}

static SsaError ssa_ir_construct_demanded(SsaWork* work)
{
    const SsaInput* input = work->input;
    SsaBounded tape = { 0 };
    SsaIrDemand plan = { .work = work, .error = ssa_bounded_accesses(work, &tape) };
    const uint32_t blocks = input->block_count;
    plan.entry_stamp = ssa_allocate(work->scratch, blocks, 4);
    plan.entry_value = ssa_allocate(work->scratch, blocks, 4);
    plan.exit_stamp = ssa_allocate(work->scratch, blocks, 4);
    plan.exit_value = ssa_allocate(work->scratch, blocks, 4);
    plan.trail = ssa_allocate(work->scratch, blocks, 4);
    if (!plan.entry_stamp || !plan.entry_value || !plan.exit_stamp || !plan.exit_value || !plan.trail)
    {
        plan.error = SSA_ERROR_MEMORY;
    }
    if (plan.error == SSA_OK)
    {
        memset(plan.entry_stamp, 0, (size_t)blocks * 4);
        memset(plan.exit_stamp, 0, (size_t)blocks * 4);
    }
    for (uint32_t local = 0; plan.error == SSA_OK && local < input->local_count; ++local)
    {
        const uint32_t begin = tape.offsets[local];
        const uint32_t end = tape.offsets[local + 1];
        const uint32_t first_join = plan.count;
        // A predecessor's final write can be later in layout than its consumer.
        for (uint32_t n = begin; n < end; ++n)
        {
            const uint32_t i = tape.accesses[n];
            if (input->kinds[i] == BUSTER_SSA_WRITE)
            {
                const uint32_t b = work->instruction_blocks[i];
                plan.exit_stamp[b] = local + 1;
                plan.exit_value[b] = input->operands[i];
            }
        }
        uint32_t last_block = BUSTER_SSA_NONE;
        uint32_t last = BUSTER_SSA_NONE;
        for (uint32_t n = begin; plan.error == SSA_OK && n < end; ++n)
        {
            const uint32_t i = tape.accesses[n];
            const uint32_t b = work->instruction_blocks[i];
            if (input->kinds[i] == BUSTER_SSA_WRITE)
            {
                last_block = b;
                last = input->operands[i];
            }
            else
            {
                tape.replacements[i] = last_block == b ? last : ssa_ir_read_entry(&plan, local, b);
            }
        }
        // Only joins reachable from an exposed read enter this worklist.
        for (uint32_t j = first_join; plan.error == SSA_OK && j < plan.count; ++j)
        {
            const uint32_t b = plan.blocks[j];
            const uint32_t p0 = work->result.predecessor_offsets[b];
            const uint32_t p1 = work->result.predecessor_offsets[b + 1];
            plan.offsets[j] = plan.operand_count;
            if (ssa_ir_reserve_operands(&plan, p1 - p0))
            {
                for (uint32_t p = p0; plan.error == SSA_OK && p < p1; ++p)
                {
                    const uint32_t pred = work->result.predecessors[p];
                    const uint32_t value = plan.exit_stamp[pred] == local + 1 ? plan.exit_value[pred] : ssa_ir_read_entry(&plan, local, pred);
                    plan.operands[plan.operand_count++] = value;
                }
            }
        }
    }
    if (plan.error == SSA_OK)
    {
        work->phi_count = plan.count;
        work->operand_count = plan.operand_count;
        work->phi_capacity = plan.capacity;
        work->operand_capacity = plan.operand_capacity;
        work->phi_locals = plan.locals;
        work->phi_blocks = plan.blocks;
        work->phi_offsets = plan.offsets;
        work->phi_operands = plan.operands;
        if (plan.count != 0)
        {
            work->phi_offsets[plan.count] = plan.operand_count;
        }
        const uint64_t size = (uint64_t)work->result.phi_base + plan.count;
        work->aliases = ssa_allocate(work->scratch, size, sizeof(uint32_t));
        if (work->aliases == NULL)
        {
            plan.error = SSA_ERROR_MEMORY;
        }
        else
        {
            memcpy(work->aliases, tape.replacements, (size_t)input->instruction_count * 4);
            for (uint64_t i = input->instruction_count; i < size; ++i)
            {
                work->aliases[i] = (uint32_t)i;
            }
        }
    }
    return plan.error;
}
