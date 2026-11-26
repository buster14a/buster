// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Measurement-only conditional control costs. Keep copy-only transport gains separate from total gains.
// This follows tier1_emit_cfg_branch's orientation and counts records on each semantic edge, including its
// one Jcc. A split change can alter the sibling's JMP count even when that sibling's placement is unchanged.

static void tier1_emit_region_branch_records(uint32_t block, uint32_t true_block, uint32_t false_block,
                                              bool split_true, bool split_false, uint32_t records[2])
{
    const bool reverse = (!split_true && !split_false && true_block == block + 1) ||
                         (split_true && (!split_false || false_block == block + 1));
    const uint32_t taken = reverse ? 1 : 0;
    const uint32_t taken_block = reverse ? false_block : true_block;
    const uint32_t inline_block = reverse ? true_block : false_block;
    const bool taken_split = reverse ? split_false : split_true;
    records[taken] = 1u + (taken_split && taken_block != block + 1);
    records[taken ^ 1u] = 1u + (inline_block != block + 1 || taken_split);
}

// The dispatch pass can copy an indirect jump through an earlier single-successor relay. Conservatively
// treat every single-successor chain reaching IR_BR_INDIRECT as dispatch-sensitive, even when a body record
// would in fact block fusion. A chain with B visited blocks and no indirect terminator has entered a cycle;
// it cannot acquire an indirect first record. The bounded walk charges before every structural visit.
static Tier1EmitError tier1_emit_region_branch_dispatch(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                        uint32_t block, uint64_t* work, bool* result)
{
    const IrFunction* function = plan->function;
    const uint32_t added = input->block_count - function->block_count;
    bool dispatch = false;
    Tier1EmitError error = TIER1_EMIT_OK;
    for (uint32_t visited = 0; error == TIER1_EMIT_OK && visited < input->block_count; visited += 1)
    {
        error = tier1_emit_recolor_charge(6, work) ? error : TIER1_EMIT_LIMIT;
        error = error != TIER1_EMIT_OK || block < input->block_count ? error : TIER1_EMIT_INVALID;
        if (error != TIER1_EMIT_OK)
        {
            break;
        }
        if (block >= added)
        {
            const uint32_t original = block - added;
            const uint32_t first = function->block_offsets[original];
            const uint32_t past = function->block_offsets[original + 1];
            dispatch = first < past && function->op[past - 1] == IR_BR_INDIRECT;
        }
        const uint32_t first = input->successor_offsets[block];
        if (dispatch || input->successor_offsets[block + 1] - first != 1)
        {
            break;
        }
        block = input->successors[first];
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = dispatch;
    }
    return error;
}

// Restrict the new control freedom to a direct two-successor conditional, with neither successor a
// dispatch-sensitive chain. Both successors matter: orientation can turn either semantic edge into a JMP.
static Tier1EmitError tier1_emit_region_branch_pair(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                    uint32_t block, uint64_t* work, bool* result)
{
    const IrFunction* function = plan->function;
    const uint32_t added = input->block_count - function->block_count;
    bool valid = block >= added && block < input->block_count &&
                 input->successor_offsets[block + 1] - input->successor_offsets[block] == 2;
    if (valid)
    {
        const uint32_t original = block - added;
        const uint32_t first = function->block_offsets[original];
        const uint32_t past = function->block_offsets[original + 1];
        valid = first < past && function->op[past - 1] == IR_CBR;
    }
    Tier1EmitError error = TIER1_EMIT_OK;
    const uint32_t first = valid ? input->successor_offsets[block] : 0;
    for (uint32_t k = 0; error == TIER1_EMIT_OK && valid && k < 2; k += 1)
    {
        bool dispatch = false;
        error = tier1_emit_region_branch_dispatch(plan, input, input->successors[first + k], work, &dispatch);
        valid = error == TIER1_EMIT_OK && !dispatch;
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = valid;
    }
    return error;
}

// Replace the legacy fixed-SPLIT offset with actual conditional branch records only for the opt-in batch
// scorer's eligible pairs. All other control terms remain frozen constants because their split changes fail.
static Tier1EmitError tier1_emit_region_branch_initial(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                       const Tier1CfgAllocation* cfg, const Tier1EmitRecolorModel* model,
                                                       uint64_t* work, Tier1EmitRecolorCost* result)
{
    Tier1EmitRecolorCost cost = *result;
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)input->block_count * 8 + input->edge_count * UINT64_C(3), work) ?
                          TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < input->block_count; b += 1)
    {
        bool supported = false;
        error = tier1_emit_region_branch_pair(plan, input, b, work, &supported);
        if (error == TIER1_EMIT_OK && supported)
        {
            const uint32_t first = input->successor_offsets[b];
            uint32_t records[2];
            tier1_emit_region_branch_records(b, input->successors[first], input->successors[first + 1],
                                             cfg->edge_placements[first] == TIER1_CFG_EDGE_SPLIT,
                                             cfg->edge_placements[first + 1] == TIER1_CFG_EDGE_SPLIT, records);
            for (uint32_t k = 0; k < 2; k += 1)
            {
                const uint32_t e = first + k;
                const int64_t adjustment = (int64_t)records[k] - (cfg->edge_placements[e] == TIER1_CFG_EDGE_SPLIT);
                cost.edges += (__int128)model->edge_weights[e] * adjustment;
            }
        }
    }
    if (error == TIER1_EMIT_OK)
    {
        *result = cost;
    }
    return error;
}

// Requires a full materialized edge view. Transport gains are read-only; total gains start as their copy.
// Inspect each source's complete successor pair, so a changed split cannot hide a sibling's control delta.
static Tier1EmitError tier1_emit_region_branch_gains(const Tier1SelectPlan* plan, const Tier1CfgInput* input,
                                                     const Tier1CfgAllocation* before, const Tier1CfgAllocation* after,
                                                     const Tier1EmitRecolorModel* model, uint64_t* work, Tier1EmitRegionScore* score)
{
    Tier1EmitError error = tier1_emit_recolor_charge((uint64_t)input->block_count * 8 + input->edge_count * UINT64_C(4), work) ?
                          TIER1_EMIT_OK : TIER1_EMIT_LIMIT;
    for (uint32_t e = 0; error == TIER1_EMIT_OK && e < input->edge_count; e += 1)
    {
        score->total_edge_gains[e] = score->edge_gains[e];
    }
    for (uint32_t b = 0; error == TIER1_EMIT_OK && b < input->block_count; b += 1)
    {
        const uint32_t first = input->successor_offsets[b];
        const uint32_t past = input->successor_offsets[b + 1];
        bool changed = false;
        for (uint32_t e = first; e < past; e += 1)
        {
            changed |= (before->edge_placements[e] == TIER1_CFG_EDGE_SPLIT) != (after->edge_placements[e] == TIER1_CFG_EDGE_SPLIT);
        }
        if (!changed)
        {
            for (uint32_t e = first; error == TIER1_EMIT_OK && e < past; e += 1)
            {
                error = tier1_emit_region_score_control(plan, input, e, before->edge_placements[e], after->edge_placements[e]) ?
                        error : TIER1_EMIT_INVALID;
                // A predecessor move prefix can suppress this source's first-record indirect copy, changing
                // whether later incoming jumps fuse through it. Guard source prefixes as well as target entries.
                if (error == TIER1_EMIT_OK && (before->edge_placements[e] == TIER1_CFG_EDGE_PREDECESSOR) !=
                                             (after->edge_placements[e] == TIER1_CFG_EDGE_PREDECESSOR))
                {
                    bool dispatch = false;
                    error = tier1_emit_region_branch_dispatch(plan, input, b, work, &dispatch);
                    error = error != TIER1_EMIT_OK || !dispatch ? error : TIER1_EMIT_INVALID;
                }
                if (error == TIER1_EMIT_OK && (before->edge_placements[e] == TIER1_CFG_EDGE_SUCCESSOR) !=
                                             (after->edge_placements[e] == TIER1_CFG_EDGE_SUCCESSOR))
                {
                    bool dispatch = false;
                    error = tier1_emit_region_branch_dispatch(plan, input, input->successors[e], work, &dispatch);
                    error = error != TIER1_EMIT_OK || !dispatch ? error : TIER1_EMIT_INVALID;
                }
            }
        }
        else
        {
            bool supported = false;
            error = tier1_emit_region_branch_pair(plan, input, b, work, &supported);
            error = error != TIER1_EMIT_OK || supported ? error : TIER1_EMIT_INVALID;
            if (error != TIER1_EMIT_OK)
            {
                break;
            }
            uint32_t old_records[2];
            uint32_t new_records[2];
            tier1_emit_region_branch_records(b, input->successors[first], input->successors[first + 1],
                                             before->edge_placements[first] == TIER1_CFG_EDGE_SPLIT,
                                             before->edge_placements[first + 1] == TIER1_CFG_EDGE_SPLIT, old_records);
            tier1_emit_region_branch_records(b, input->successors[first], input->successors[first + 1],
                                             after->edge_placements[first] == TIER1_CFG_EDGE_SPLIT,
                                             after->edge_placements[first + 1] == TIER1_CFG_EDGE_SPLIT, new_records);
            for (uint32_t k = 0; k < 2; k += 1)
            {
                const uint32_t e = first + k;
                const int64_t gain = (int64_t)old_records[k] - new_records[k];
                score->total_edge_gains[e] += gain;
                score->cost.edges -= (__int128)model->edge_weights[e] * gain;
            }
        }
    }
    return error;
}
