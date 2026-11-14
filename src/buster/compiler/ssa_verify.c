// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent verification of local promotion, included by ssa.c. Shape, type and dominance checks precede the
// symbolic reaching-definition equations. The latter rebuild their own access inventory from SsaInput and reuse
// one block workspace per local: no constructor summaries or blocks-by-locals tables are trusted or allocated.

static bool ssa_value_valid(const SsaWork* work, const SsaResult* result, uint32_t value, uint32_t block, uint32_t position, uint8_t type)
{
    const SsaInput* input = work->input;
    bool valid = value < (uint64_t)result->phi_base + result->phi_count;
    if (valid && value < input->instruction_count)
    {
        const uint8_t kind = input->kinds[value];
        const uint32_t origin = work->instruction_blocks[value];
        valid = kind != BUSTER_SSA_WRITE && input->types[value] == type;
        valid = valid && !(kind == BUSTER_SSA_READ && result->promoted[input->locals[value]] != 0);
        valid = valid && ssa_dominates(work, origin, block) && (origin != block || value < position);
    }
    else if (valid && value < result->phi_base)
    {
        valid = input->local_types[value - input->instruction_count] == type;
    }
    else if (valid)
    {
        const uint32_t phi = value - result->phi_base;
        valid = result->phi_locals[phi] < input->local_count && result->phi_blocks[phi] < input->block_count;
        valid = valid && input->local_types[result->phi_locals[phi]] == type && ssa_dominates(work, result->phi_blocks[phi], block);
    }
    return valid;
}

static void ssa_check_fail(SsaCheck* check, uint32_t instruction, const char* rule)
{
    if (check->error == SSA_OK)
    {
        *check = (SsaCheck){
            .error = SSA_ERROR_INVALID,
            .instruction = instruction,
            .rule = rule,
        };
    }
}

typedef struct SsaVerifyBlock SsaVerifyBlock;
struct SsaVerifyBlock
{
    uint32_t last;
    uint32_t phi;
    uint32_t incoming;
    uint32_t outgoing;
    bool live;
};

typedef struct SsaVerifyLocal SsaVerifyLocal;
struct SsaVerifyLocal
{
    SsaVerifyBlock* blocks;
    uint32_t* generations;
    uint32_t* touched;
    uint32_t* queue;
    uint32_t touched_count;
};

static void ssa_verify_touch(SsaVerifyLocal* local, uint32_t block, uint32_t generation)
{
    if (local->generations[block] != generation)
    {
        local->generations[block] = generation;
        local->blocks[block] = (SsaVerifyBlock){
            .last = BUSTER_SSA_NONE,
            .phi = BUSTER_SSA_NONE,
            .incoming = BUSTER_SSA_NONE,
            .outgoing = BUSTER_SSA_NONE,
        };
        local->touched[local->touched_count] = block;
        local->touched_count += 1;
    }
}

// Each known exit is propagated once. Incoming values at a live block without a phi must all agree; candidate
// phis are symbolic anchors whose operands are checked separately. A WRITE's source can be another local's READ:
// its candidate replacement is used symbolically here and that original READ is checked in its own local's turn.
// Generations reset only touched rows, and the independently derived liveness visits only paths needed by reads.
static SsaCheck ssa_verify_equations(SsaWork* work, const SsaResult* result)
{
    const SsaInput* input = work->input;
    Arena* scratch = work->scratch;
    const uint32_t block_count = input->block_count;
    const uint32_t local_count = input->local_count;
    const uint32_t instruction_count = input->instruction_count;
    const uint32_t phi_count = result->phi_count;
    const uint32_t phi_base = result->phi_base;
    const uint32_t* instruction_blocks = work->instruction_blocks;
    const uint32_t* block_offsets = input->block_offsets;
    const uint32_t* successor_offsets = input->successor_offsets;
    const uint32_t* successors = input->successors;
    const uint8_t* kinds = input->kinds;
    const uint32_t* locals = input->locals;
    const uint32_t* operands = input->operands;
    const uint8_t* promoted = work->result.promoted;
    const uint8_t* reachable = work->result.reachable;
    const uint32_t* predecessor_offsets = work->result.predecessor_offsets;
    const uint32_t* predecessors = work->result.predecessors;
    const uint32_t* phi_blocks = result->phi_blocks;
    const uint32_t* phi_locals = result->phi_locals;
    const uint32_t* operand_offsets = result->phi_offsets;
    const uint32_t* phi_operands = result->phi_operands;
    const uint32_t* replacements = result->replacements;
    uint32_t* access_offsets = ssa_allocate(scratch, (uint64_t)local_count + 1, sizeof(uint32_t));
    uint32_t* phi_offsets = ssa_allocate(scratch, (uint64_t)local_count + 1, sizeof(uint32_t));
    uint32_t* cursors = ssa_allocate(scratch, local_count, sizeof(uint32_t));
    SsaVerifyLocal local = {
        .blocks = ssa_allocate(scratch, block_count, sizeof(SsaVerifyBlock)),
        .generations = ssa_allocate(scratch, block_count, sizeof(uint32_t)),
        .touched = ssa_allocate(scratch, block_count, sizeof(uint32_t)),
        .queue = ssa_allocate(scratch, block_count, sizeof(uint32_t)),
    };
    SsaCheck check = {
        .error = access_offsets != 0 && phi_offsets != 0 && cursors != 0 && local.blocks != 0 && local.generations != 0 && local.touched != 0 && local.queue != 0 ? SSA_OK : SSA_ERROR_MEMORY,
        .instruction = BUSTER_SSA_NONE,
        .rule = "reaching-definition scratch",
    };
    uint32_t* accesses = 0;
    uint32_t* phis = 0;
    if (check.error == SSA_OK)
    {
        memset(access_offsets, 0, ((uint64_t)local_count + 1) * sizeof(uint32_t));
        memset(phi_offsets, 0, ((uint64_t)local_count + 1) * sizeof(uint32_t));
        memset(local.generations, 0, (uint64_t)block_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < instruction_count; i += 1)
        {
            const uint8_t kind = kinds[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && promoted[locals[i]] != 0 && reachable[instruction_blocks[i]] != 0)
            {
                access_offsets[(uint64_t)locals[i] + 1] += 1;
            }
        }
        for (uint32_t phi = 0; phi < phi_count; phi += 1)
        {
            phi_offsets[(uint64_t)phi_locals[phi] + 1] += 1;
        }
        for (uint32_t id = 0; id < local_count; id += 1)
        {
            access_offsets[(uint64_t)id + 1] += access_offsets[id];
            phi_offsets[(uint64_t)id + 1] += phi_offsets[id];
            cursors[id] = access_offsets[id];
        }
        accesses = ssa_allocate(scratch, access_offsets[local_count], sizeof(uint32_t));
        phis = ssa_allocate(scratch, phi_count, sizeof(uint32_t));
        if (accesses == 0 || phis == 0)
        {
            check.error = SSA_ERROR_MEMORY;
        }
    }
    if (check.error == SSA_OK)
    {
        // Stable grouping retains original instruction order, including READs before the block's first WRITE.
        // This scalar inventory is independent of any grouped accesses or def/use summaries used by construction.
        for (uint32_t i = 0; i < instruction_count; i += 1)
        {
            const uint8_t kind = kinds[i];
            if ((kind == BUSTER_SSA_READ || kind == BUSTER_SSA_WRITE) && promoted[locals[i]] != 0 && reachable[instruction_blocks[i]] != 0)
            {
                const uint32_t id = locals[i];
                accesses[cursors[id]] = i;
                cursors[id] += 1;
            }
        }
        memcpy(cursors, phi_offsets, (uint64_t)local_count * sizeof(uint32_t));
        for (uint32_t phi = 0; phi < phi_count; phi += 1)
        {
            const uint32_t id = phi_locals[phi];
            phis[cursors[id]] = phi;
            cursors[id] += 1;
        }
    }
    SsaVerifyBlock* blocks = local.blocks;
    uint32_t* queue = local.queue;
    for (uint32_t id = 0; check.error == SSA_OK && id < local_count; id += 1)
    {
        const uint32_t access_begin = access_offsets[id];
        const uint32_t access_end = access_offsets[(uint64_t)id + 1];
        const uint32_t phi_begin = phi_offsets[id];
        const uint32_t phi_end = phi_offsets[(uint64_t)id + 1];
        if (access_begin == access_end && phi_begin == phi_end)
        {
            continue;
        }
        // ssa_check_input requires instruction_count + local_count < NONE, so id + 1 cannot wrap or be zero.
        const uint32_t generation = id + 1;
        const uint32_t undefined = instruction_count + id;
        local.touched_count = 0;
        uint32_t queued = 0;
        for (uint32_t access = access_begin; access < access_end; access += 1)
        {
            const uint32_t i = accesses[access];
            const uint32_t block = instruction_blocks[i];
            ssa_verify_touch(&local, block, generation);
            if (kinds[i] == BUSTER_SSA_WRITE)
            {
                blocks[block].last = operands[i];
            }
            else if (blocks[block].last == BUSTER_SSA_NONE && !blocks[block].live)
            {
                blocks[block].live = true;
                queue[queued++] = block;
            }
        }
        for (uint32_t index = phi_begin; check.error == SSA_OK && index < phi_end; index += 1)
        {
            const uint32_t phi = phis[index];
            const uint32_t block = phi_blocks[phi];
            ssa_verify_touch(&local, block, generation);
            if (blocks[block].phi != BUSTER_SSA_NONE)
            {
                ssa_check_fail(&check, block_offsets[block], "duplicate phi or predecessor multiplicity");
            }
            blocks[block].phi = phi_base + phi;
            // Even a dead candidate phi must have its operands checked. Its predecessors may lie outside the
            // original local's live region; their equations still yield their final WRITE, phi, or typed undef.
            for (uint32_t edge = predecessor_offsets[block]; edge < predecessor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                ssa_verify_touch(&local, predecessors[edge], generation);
            }
        }
        for (uint32_t head = 0; check.error == SSA_OK && head < queued; head += 1)
        {
            const uint32_t block = queue[head];
            const uint32_t end = predecessor_offsets[(uint64_t)block + 1];
            for (uint32_t edge = predecessor_offsets[block]; edge < end; edge += 1)
            {
                const uint32_t predecessor = predecessors[edge];
                ssa_verify_touch(&local, predecessor, generation);
                if (blocks[predecessor].last == BUSTER_SSA_NONE && !blocks[predecessor].live)
                {
                    blocks[predecessor].live = true;
                    queue[queued++] = predecessor;
                }
            }
        }
        queued = 0;
        const uint32_t touched_count = local.touched_count;
        for (uint32_t index = 0; check.error == SSA_OK && index < touched_count; index += 1)
        {
            const uint32_t block = local.touched[index];
            SsaVerifyBlock* row = &blocks[block];
            uint32_t value = row->phi;
            if (value == BUSTER_SSA_NONE && (block == 0 || !row->live))
            {
                value = undefined;
            }
            row->incoming = value;
            const uint32_t last = row->last;
            const uint32_t outgoing = last == BUSTER_SSA_NONE ? value : last < instruction_count ? replacements[last] : last;
            row->outgoing = outgoing;
            if (outgoing != BUSTER_SSA_NONE)
            {
                queue[queued++] = block;
            }
        }
        for (uint32_t head = 0; check.error == SSA_OK && head < queued; head += 1)
        {
            const uint32_t block = queue[head];
            const uint32_t value = blocks[block].outgoing;
            const uint32_t end = successor_offsets[(uint64_t)block + 1];
            for (uint32_t edge = successor_offsets[block]; check.error == SSA_OK && edge < end; edge += 1)
            {
                const uint32_t successor = successors[edge];
                if (local.generations[successor] == generation && blocks[successor].live && blocks[successor].phi == BUSTER_SSA_NONE)
                {
                    SsaVerifyBlock* row = &blocks[successor];
                    if (row->incoming == BUSTER_SSA_NONE)
                    {
                        row->incoming = value;
                        if (row->last == BUSTER_SSA_NONE)
                        {
                            row->outgoing = value;
                            queue[queued++] = successor;
                        }
                    }
                    else if (row->incoming != value)
                    {
                        ssa_check_fail(&check, block_offsets[successor], "different reaching values without a phi");
                    }
                }
            }
        }
        for (uint32_t index = phi_begin; check.error == SSA_OK && index < phi_end; index += 1)
        {
            const uint32_t phi = phis[index];
            const uint32_t block = phi_blocks[phi];
            const uint32_t begin = predecessor_offsets[block];
            const uint32_t end = predecessor_offsets[(uint64_t)block + 1];
            const uint32_t operand_begin = operand_offsets[phi];
            for (uint32_t edge = begin; check.error == SSA_OK && edge < end; edge += 1)
            {
                const uint32_t expected = blocks[predecessors[edge]].outgoing;
                if (expected == BUSTER_SSA_NONE || expected != phi_operands[operand_begin + (edge - begin)])
                {
                    ssa_check_fail(&check, block_offsets[block], "phi operand is not the predecessor's local value");
                }
            }
        }
        uint32_t previous = BUSTER_SSA_NONE;
        uint32_t current = BUSTER_SSA_NONE;
        for (uint32_t access = access_begin; check.error == SSA_OK && access < access_end; access += 1)
        {
            const uint32_t i = accesses[access];
            const uint32_t block = instruction_blocks[i];
            if (block != previous)
            {
                current = blocks[block].incoming;
                previous = block;
            }
            if (kinds[i] == BUSTER_SSA_WRITE)
            {
                const uint32_t operand = operands[i];
                current = operand < instruction_count ? replacements[operand] : operand;
            }
            else if (current == BUSTER_SSA_NONE || replacements[i] != current)
            {
                ssa_check_fail(&check, i, "read is not the current local value");
            }
        }
    }
    if (check.error == SSA_OK)
    {
        check.rule = 0;
    }
    return check;
}

static SsaCheck ssa_verify(const SsaInput* input, const SsaResult* result, Arena* scratch)
{
    SsaCheck check = {
        .error = result != 0 && scratch != 0 ? ssa_check_input(input) : SSA_ERROR_INVALID,
        .instruction = BUSTER_SSA_NONE,
        .rule = "input",
    };
    const uint64_t saved = scratch == 0 ? 0 : scratch->length;
    SsaWork work = { 0 };
    if (check.error == SSA_OK)
    {
        check.error = ssa_prepare(&work, input, scratch, false);
    }
    if (check.error == SSA_OK)
    {
        // These are shape bounds only. No allocation or iteration uses their Cartesian extents.
        const uint64_t maximum_phis = (uint64_t)input->block_count * work.promoted_count;
        const uint64_t maximum_operands = (uint64_t)work.result.reachable_edge_count * work.promoted_count;
        const bool pointers = result->predecessor_offsets != 0 && result->phi_offsets != 0 && (input->local_count == 0 || result->promoted != 0) && (input->block_count == 0 || result->reachable != 0) && (input->instruction_count == 0 || result->replacements != 0) && (result->phi_count == 0 || (result->phi_blocks != 0 && result->phi_locals != 0)) && (result->operand_count == 0 || result->phi_operands != 0) && (result->reachable_edge_count == 0 || (result->predecessors != 0 && result->predecessor_edges != 0));
        if (!pointers || result->phi_base != work.result.phi_base || result->phi_count > maximum_phis || result->operand_count > maximum_operands || (uint64_t)result->phi_base + result->phi_count >= BUSTER_SSA_NONE || result->reachable_edge_count != work.result.reachable_edge_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "output columns or counts");
        }
    }
    if (check.error == SSA_OK)
    {
        const bool locals = input->local_count == 0 || memcmp(result->promoted, work.result.promoted, input->local_count) == 0;
        const bool blocks = input->block_count == 0 || memcmp(result->reachable, work.result.reachable, input->block_count) == 0;
        const bool offsets = memcmp(result->predecessor_offsets, work.result.predecessor_offsets, ((uint64_t)input->block_count + 1) * sizeof(uint32_t)) == 0;
        const uint64_t bytes = (uint64_t)result->reachable_edge_count * sizeof(uint32_t);
        const bool predecessors = bytes == 0 || (memcmp(result->predecessors, work.result.predecessors, bytes) == 0 && memcmp(result->predecessor_edges, work.result.predecessor_edges, bytes) == 0);
        if (!locals || !blocks || !offsets || !predecessors || result->phi_offsets[0] != 0 || result->phi_offsets[result->phi_count] != result->operand_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "promotion, reachability or predecessors");
        }
    }
    for (uint32_t phi = 0; check.error == SSA_OK && phi < result->phi_count; phi += 1)
    {
        const uint32_t block = result->phi_blocks[phi];
        const uint32_t id = result->phi_locals[phi];
        if (block == 0 || block >= input->block_count || id >= input->local_count || result->reachable[block] == 0 || result->promoted[id] == 0 || result->phi_offsets[phi] > result->phi_offsets[(uint64_t)phi + 1] || result->phi_offsets[(uint64_t)phi + 1] > result->operand_count)
        {
            ssa_check_fail(&check, BUSTER_SSA_NONE, "phi block, local or operand range");
        }
        else if (result->phi_offsets[(uint64_t)phi + 1] - result->phi_offsets[phi] != result->predecessor_offsets[(uint64_t)block + 1] - result->predecessor_offsets[block])
        {
            ssa_check_fail(&check, input->block_offsets[block], "duplicate phi or predecessor multiplicity");
        }
    }
    for (uint32_t i = 0; check.error == SSA_OK && i < input->instruction_count; i += 1)
    {
        const uint8_t kind = input->kinds[i];
        if (kind == BUSTER_SSA_WRITE)
        {
            if (result->replacements[i] != BUSTER_SSA_NONE)
            {
                ssa_check_fail(&check, i, "write must not define a value");
            }
        }
        else if (kind == BUSTER_SSA_READ && result->promoted[input->locals[i]] != 0)
        {
            const uint32_t block = work.instruction_blocks[i];
            const uint32_t value = result->replacements[i];
            const bool valid = result->reachable[block] != 0 ? ssa_value_valid(&work, result, value, block, i, input->types[i]) : value == input->instruction_count + input->locals[i];
            if (!valid)
            {
                ssa_check_fail(&check, i, "read replacement type, dominance or canonical value");
            }
        }
        else if (result->replacements[i] != i)
        {
            ssa_check_fail(&check, i, "unpromoted instruction changed");
        }
    }
    for (uint32_t phi = 0; check.error == SSA_OK && phi < result->phi_count; phi += 1)
    {
        const uint32_t block = result->phi_blocks[phi];
        const uint8_t type = input->local_types[result->phi_locals[phi]];
        uint32_t same = BUSTER_SSA_NONE;
        bool distinct = false;
        for (uint32_t operand = result->phi_offsets[phi]; operand < result->phi_offsets[(uint64_t)phi + 1]; operand += 1)
        {
            const uint32_t edge = result->predecessor_offsets[block] + operand - result->phi_offsets[phi];
            const uint32_t predecessor = result->predecessors[edge];
            const uint32_t value = result->phi_operands[operand];
            if (!ssa_value_valid(&work, result, value, predecessor, input->block_offsets[(uint64_t)predecessor + 1], type))
            {
                ssa_check_fail(&check, input->block_offsets[block], "phi operand type or edge dominance");
            }
            if (value != result->phi_base + phi)
            {
                distinct |= same != BUSTER_SSA_NONE && same != value;
                same = value;
            }
        }
        if (!distinct)
        {
            ssa_check_fail(&check, input->block_offsets[block], "trivial phi");
        }
    }
    if (check.error == SSA_OK)
    {
        check = ssa_verify_equations(&work, result);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, saved);
    }
    return check;
}
