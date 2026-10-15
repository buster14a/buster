// Canonical CFG publication, included by ir.c. Terminators own topology;
// ir_function_publish_cfg transposes ordered builder incoming columns once
// into edge argument rows. Native selection and direct emitters consume these
// immutable slices through ir_function_cfg_edge without rebuilding a CFG.
// Instruction spans use one scratch remap and an in-place row permutation;
// source/extras/definitions follow it. Value/block/label IDs and relocation
// ownership remain stable. ir_cfg_separate_operand_slices copies only operand,
// target or immediate slices that may alias another row's.
// Publication is the one termination check every producer passes: each tail
// must be a terminator (ir_instruction_is_terminator) and no terminator may
// have a successor in its chain.

void ir_function_reopen_cfg(IrFunction* function)
{
    if (function && function->published_cfg)
    {
        function->operand_total_rows = UINT32_MAX;
        // Re-entering construction explicitly restores mutable chains. The
        // published representation itself retains no per-row next links.
        IrPublishedCfg const* cfg = function->published_cfg;
        IrBlockParameter* parameters = arena_allocate(cfg->arena, IrBlockParameter, cfg->parameter_count);
        IrIncoming* incoming = arena_allocate(cfg->arena, IrIncoming, cfg->argument_count);
        IrPredecessor* predecessors = arena_allocate(cfg->arena, IrPredecessor, cfg->edge_count);
        u32 incoming_cursor = 0;
        for (u32 block = 0; block < cfg->block_count; block += 1)
        {
            IrCfgBlock const* span = cfg->blocks + block;
            IrBlock* builder = function->blocks + block;
            for (u32 index = 0; index < span->predecessor_count; index += 1)
            {
                IrCfgEdge const* edge = cfg->edges + cfg->predecessors[span->predecessor_offset + index];
                predecessors[span->predecessor_offset + index] = (IrPredecessor){.block = edge->source,
                    .next = index + 1 < span->predecessor_count ? predecessors + span->predecessor_offset + index + 1 : 0};
            }
            builder->predecessor_count = span->predecessor_count;
            builder->first_predecessor = span->predecessor_count ? predecessors + span->predecessor_offset : 0;
            builder->last_predecessor = span->predecessor_count ? predecessors + span->predecessor_offset + span->predecessor_count - 1 : 0;
            for (u32 index = 0; index < span->parameter_count; index += 1)
            {
                IrCfgParameter const* parameter = cfg->parameters + span->parameter_offset + index;
                for (u32 pred = 0; pred < span->predecessor_count; pred += 1)
                {
                    IrCfgEdge const* edge = cfg->edges + cfg->predecessors[span->predecessor_offset + pred];
                    incoming[incoming_cursor + pred] = (IrIncoming){.predecessor = edge->source,
                        .value = cfg->arguments[edge->argument_offset + index],
                        .next = pred + 1 < span->predecessor_count ? incoming + incoming_cursor + pred + 1 : 0};
                }
                parameters[span->parameter_offset + index] = (IrBlockParameter){.canonical_type = parameter->canonical_type,
                    .canonical_local = parameter->canonical_local, .value = parameter->value, .incoming_count = span->predecessor_count,
                    .first_incoming = span->predecessor_count ? incoming + incoming_cursor : 0,
                    .last_incoming = span->predecessor_count ? incoming + incoming_cursor + span->predecessor_count - 1 : 0,
                    .next = index + 1 < span->parameter_count ? parameters + span->parameter_offset + index + 1 : 0};
                incoming_cursor += span->predecessor_count;
            }
            builder->first_parameter = span->parameter_count ? parameters + span->parameter_offset : 0;
            builder->last_parameter = span->parameter_count ? parameters + span->parameter_offset + span->parameter_count - 1 : 0;
            for (u32 offset = 0; offset < span->instruction_count; offset += 1)
            {
                u32 row = span->first_instruction + offset;
                function->instructions[row].next.value = offset + 1 < span->instruction_count ? row + 1 : IR_ID_UNDERLYING_INVALID;
            }
        }
        function->published_cfg = 0;
    }
}

IrCfgEdge const* ir_function_cfg_edge(IrFunction const* function, IrBlockId source, IrBlockId destination)
{
    IrCfgEdge const* result = 0;
    IrPublishedCfg const* cfg = function ? function->published_cfg : 0;
    if (cfg && source.value < cfg->block_count && destination.value < cfg->block_count)
    {
        IrCfgBlock const* block = cfg->blocks + destination.value;
        u32 first = block->predecessor_offset;
        u32 end = first + block->predecessor_count;
        // Edges were appended in source ID order. A binary lookup avoids a
        // quadratic switch fan-out while preserving original successor order.
        while (first < end)
        {
            u32 middle = first + (end - first) / 2;
            IrCfgEdge const* edge = cfg->edges + cfg->predecessors[middle];
            if (edge->source.value < source.value)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        if (first < block->predecessor_offset + block->predecessor_count)
        {
            IrCfgEdge const* edge = cfg->edges + cfg->predecessors[first];
            if (edge->source.value == source.value)
            {
                result = edge;
            }
        }
    }
    return result;
}

typedef struct IrCfgExtraOrder IrCfgExtraOrder;
struct IrCfgExtraOrder
{
    IrInstructionExtra extra;
    IrInstructionId instruction;
};

BUSTER_GLOBAL_LOCAL void ir_cfg_remap_extras(Arena* scratch, IrFunction* function, IrInstructionId const* remap)
{
    u32 count = function->extra_count;
    bool ordered = true;
    u32 previous = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        IrInstructionId* instruction = function->extra_instructions + index;
        *instruction = remap[instruction->value];
        ordered &= index == 0 || instruction->value > previous;
        previous = instruction->value;
    }
    // A row permutation need not disturb sparse extras' relative order.
    // Map their keys first; only disordered keys require moving payloads.
    if (!ordered)
    {
        IrCfgExtraOrder* rows = arena_allocate(scratch, IrCfgExtraOrder, count);
        IrCfgExtraOrder* temporary = arena_allocate(scratch, IrCfgExtraOrder, count);
        for (u32 index = 0; index < count; index += 1)
        {
            rows[index] = (IrCfgExtraOrder){.extra = function->extras[index], .instruction = function->extra_instructions[index]};
        }
        // Sparse extras must remain sorted for binary lookup after row permutation.
        // Bottom-up merging preserves linear auxiliary storage and bounded stack.
        for (u64 width = 1; width < count; width *= 2)
        {
            for (u64 begin = 0; begin < count; begin += 2 * width)
            {
                u64 middle = BUSTER_MIN(begin + width, count);
                u64 end = BUSTER_MIN(begin + 2 * width, count);
                u64 left = begin;
                u64 right = middle;
                for (u64 out = begin; out < end; out += 1)
                {
                    bool take_left = left < middle && (right == end || rows[left].instruction.value < rows[right].instruction.value);
                    temporary[out] = rows[take_left ? left++ : right++];
                }
            }
            IrCfgExtraOrder* swap = rows;
            rows = temporary;
            temporary = swap;
        }
        for (u32 index = 0; index < count; index += 1)
        {
            function->extras[index] = rows[index].extra;
            function->extra_instructions[index] = rows[index].instruction;
        }
    }
}

#define IR_CFG_NARROW_POOL_COUNT_BOUND ((u64)UINT32_MAX * (u64)UINT16_MAX)
BUSTER_CT_CHECK(IR_CFG_NARROW_POOL_COUNT_BOUND == UINT64_C(281470681677825));
BUSTER_CT_CHECK(sizeof(((IrFunction*)0)->instruction_count) == sizeof(u32));
BUSTER_CT_CHECK(sizeof(((IrInstruction*)0)->target_count) == sizeof(u16));
BUSTER_CT_CHECK(sizeof(((IrInstruction*)0)->immediate_count) == sizeof(u16));
BUSTER_CT_CHECK(IR_CFG_NARROW_POOL_COUNT_BOUND <= UINT64_MAX / sizeof(IrBlockId));
BUSTER_CT_CHECK(IR_CFG_NARROW_POOL_COUNT_BOUND <= UINT64_MAX / sizeof(u64));

typedef enum IrCfgSliceKind
{
    IR_CFG_SLICE_OPERAND,
    IR_CFG_SLICE_TARGET,
    IR_CFG_SLICE_IMMEDIATE,
} IrCfgSliceKind;

typedef struct IrCfgSliceRange IrCfgSliceRange;
struct IrCfgSliceRange
{
    u64 begin;
    u64 end;
};

BUSTER_GLOBAL_LOCAL IrCfgSliceRange ir_cfg_slice_range(IrInstruction const* row, IrCfgSliceKind kind)
{
    u32 count = kind == IR_CFG_SLICE_OPERAND ? row->operand_count : kind == IR_CFG_SLICE_TARGET ? row->target_count : row->immediate_count;
    u64 begin = kind == IR_CFG_SLICE_OPERAND ? (u64)(uintptr_t)row->operands : kind == IR_CFG_SLICE_TARGET ? (u64)(uintptr_t)row->targets : (u64)(uintptr_t)row->immediates;
    return (IrCfgSliceRange){.begin = count ? begin : 0, .end = count ? begin + (u64)count * (kind == IR_CFG_SLICE_IMMEDIATE ? sizeof(u64) : sizeof(IrValueId)) : 0};
}

// A failed ascent may interleave construction allocations with later fold
// allocations. Split at the first descent and prove each address partition
// monotone without overlap. No interval may cross the split. More fragmented
// layouts conservatively keep the copy; no proof survives direct row edits.
BUSTER_GLOBAL_LOCAL bool ir_cfg_slices_disjoint(IrFunction const* function, IrCfgSliceKind kind)
{
    IrCfgSliceRange previous = {0};
    u64 split = 0;
    u64 first = 0;
    for (u32 index = 0; index < function->instruction_count && !split; index += 1)
    {
        IrCfgSliceRange range = ir_cfg_slice_range(function->instructions + index, kind);
        if (range.end)
        {
            if (!first) first = range.begin;
            if (range.begin < previous.end) split = range.end <= first ? first : previous.begin;
            previous = range;
        }
    }
    IrCfgSliceRange last[2] = {{0}};
    u32 seen = 0;
    u32 ascending = 0;
    u32 descending = 0;
    bool disjoint = true;
    for (u32 index = 0; index < function->instruction_count && disjoint; index += 1)
    {
        IrCfgSliceRange range = ir_cfg_slice_range(function->instructions + index, kind);
        if (range.end)
        {
            u32 partition = range.begin >= split;
            u32 bit = 1u << partition;
            disjoint = partition || range.end <= split;
            if (seen & bit)
            {
                if (range.begin >= last[partition].end)
                {
                    disjoint &= !(descending & bit);
                    ascending |= bit;
                }
                else if (range.end <= last[partition].begin)
                {
                    disjoint &= !(ascending & bit);
                    descending |= bit;
                }
                else
                {
                    disjoint = false;
                }
            }
            last[partition] = range;
            seen |= bit;
        }
    }
    return disjoint;
}

// Reuse ascending slices immediately. Only a kind whose ascent fails needs
// the bounded address-partition proof; ambiguous or overlapping layouts are
// copied. Reopened rows may be edited directly, so every publication proves
// its current slices again rather than retaining a mutable certificate.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_cfg_separate_operand_slices(Arena* arena, IrFunction* function, IrPublishedCfg* cfg)
{
    IrValidationResult result = ir_validation_ok();
    u64 operand_end = 0;
    u64 target_end = 0;
    u64 immediate_end = 0;
    u64 operand_count = 0;
    u64 target_count = 0;
    u64 immediate_count = 0;
    bool operands_disjoint = true;
    bool targets_disjoint = true;
    bool immediates_disjoint = true;
    for (u32 index = 0; index < function->instruction_count && result.error == IR_VALIDATION_NONE; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        if ((row->operand_count && !row->operands) || (row->target_count && !row->targets) || (row->immediate_count && !row->immediates))
        {
            result = ir_validation_error(IR_VALIDATION_OPERATION, function, IR_BLOCK_ID_INVALID, (IrInstructionId){.value = index});
        }
        else
        {
            if (row->operand_count)
            {
                operands_disjoint &= (u64)(uintptr_t)row->operands >= operand_end;
                operand_end = (u64)(uintptr_t)(row->operands + row->operand_count);
                operand_count += row->operand_count;
            }
            if (row->target_count)
            {
                targets_disjoint &= (u64)(uintptr_t)row->targets >= target_end;
                target_end = (u64)(uintptr_t)(row->targets + row->target_count);
                target_count += row->target_count;
            }
            if (row->immediate_count)
            {
                immediates_disjoint &= (u64)(uintptr_t)row->immediates >= immediate_end;
                immediate_end = (u64)(uintptr_t)(row->immediates + row->immediate_count);
                immediate_count += row->immediate_count;
            }
            // Operand counts are u32, so their u32-by-u32 total can exceed
            // the byte-size limit. The two u16 counts are bounded at compile
            // time above by instruction_count's u32 representation.
            if (operand_count > UINT64_MAX / sizeof(IrValueId))
            {
                result.error = IR_VALIDATION_INVALID_ID;
            }
        }
    }
    if (result.error == IR_VALIDATION_NONE)
    {
        if (!operands_disjoint) operands_disjoint = ir_cfg_slices_disjoint(function, IR_CFG_SLICE_OPERAND);
        if (!targets_disjoint) targets_disjoint = ir_cfg_slices_disjoint(function, IR_CFG_SLICE_TARGET);
        if (!immediates_disjoint) immediates_disjoint = ir_cfg_slices_disjoint(function, IR_CFG_SLICE_IMMEDIATE);
    }
    if (result.error == IR_VALIDATION_NONE && (!operands_disjoint || !targets_disjoint || !immediates_disjoint))
    {
        IrValueId* operands = operands_disjoint ? 0 : arena_allocate(arena, IrValueId, operand_count);
        IrBlockId* targets = targets_disjoint ? 0 : arena_allocate(arena, IrBlockId, target_count);
        u64* immediates = immediates_disjoint ? 0 : arena_allocate(arena, u64, immediate_count);
        cfg->allocated_bytes += operands_disjoint ? 0 : operand_count * sizeof(*operands);
        cfg->allocated_bytes += targets_disjoint ? 0 : target_count * sizeof(*targets);
        cfg->allocated_bytes += immediates_disjoint ? 0 : immediate_count * sizeof(*immediates);
        u64 operand_cursor = 0;
        u64 target_cursor = 0;
        u64 immediate_cursor = 0;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            if (operands && row->operand_count)
            {
                memcpy(operands + operand_cursor, row->operands, sizeof(*operands) * row->operand_count);
                row->operands = operands + operand_cursor;
                operand_cursor += row->operand_count;
            }
            if (targets && row->target_count)
            {
                memcpy(targets + target_cursor, row->targets, sizeof(*targets) * row->target_count);
                row->targets = targets + target_cursor;
                target_cursor += row->target_count;
            }
            if (immediates && row->immediate_count)
            {
                memcpy(immediates + immediate_cursor, row->immediates, sizeof(*immediates) * row->immediate_count);
                row->immediates = immediates + immediate_cursor;
                immediate_cursor += row->immediate_count;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_cfg_publish_instruction_rows(Arena* arena, Arena* scratch, IrFunction* function,
                                                                      IrPublishedCfg* cfg, IrCfgBlock* blocks)
{
    IrValidationResult result = ir_validation_ok();
    u32 count = function->instruction_count;
    IrInstructionId* remap = arena_allocate(scratch, IrInstructionId, count);
    memset(remap, 0xff, sizeof(*remap) * count);
    u32 cursor = 0;
    bool moved = false;
    for (u32 index = 0; index < function->block_count && result.error == IR_VALIDATION_NONE; index += 1)
    {
        IrBlock* block = function->blocks + index;
        IrInstructionId id = block->first_instruction;
        IrInstructionId last = IR_INSTRUCTION_ID_INVALID;
        blocks[index].first_instruction = cursor;
        while (id.value != IR_ID_UNDERLYING_INVALID && result.error == IR_VALIDATION_NONE)
        {
            if (id.value >= count || remap[id.value].value != IR_ID_UNDERLYING_INVALID)
            {
                result = ir_validation_error(IR_VALIDATION_INSTRUCTION_OWNERSHIP, function, block->id, id);
            }
            else
            {
                moved |= id.value != cursor;
                remap[id.value].value = cursor;
                cursor += 1;
                last = id;
                // The walk already holds the row: a terminator with a successor
                // in its chain is the one termination fault the tail check
                // above cannot see.
                IrInstruction* row = function->instructions + id.value;
                IR_CONSTRUCTION_RECORD(CFG_TERMINATOR_CHECKS, 1);
                id = row->next;
                if (id.value != IR_ID_UNDERLYING_INVALID && ir_instruction_is_terminator(row))
                {
                    result = ir_validation_error(IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR, function, block->id, id);
                }
            }
        }
        blocks[index].instruction_count = cursor - blocks[index].first_instruction;
        if (result.error == IR_VALIDATION_NONE && last.value != block->last_instruction.value)
        {
            result = ir_validation_error(IR_VALIDATION_INSTRUCTION_OWNERSHIP, function, block->id, block->last_instruction);
        }
    }
    if (result.error == IR_VALIDATION_NONE && cursor != count)
    {
        result.error = IR_VALIDATION_INSTRUCTION_OWNERSHIP;
    }
    for (u32 index = 0; index < function->value_count && result.error == IR_VALIDATION_NONE; index += 1)
    {
        u32 definition = function->values[index].definition.value;
        if (definition != IR_ID_UNDERLYING_INVALID && definition >= count)
        {
            result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, function->values[index].definition);
        }
    }
    if (function->extra_count && (!function->extras || !function->extra_instructions))
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    for (u32 index = 0; index < function->extra_count && result.error == IR_VALIDATION_NONE; index += 1)
    {
        if (function->extra_instructions[index].value >= count)
        {
            result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, IR_BLOCK_ID_INVALID, function->extra_instructions[index]);
        }
    }
    if (result.error == IR_VALIDATION_NONE)
    {
        result = ir_cfg_separate_operand_slices(arena, function, cfg);
    }
    if (result.error == IR_VALIDATION_NONE)
    {
        // Definitions and extras consume the old-to-new map before its
        // entries become the visited marks of the in-place permutation.
        if (moved)
        {
            for (u32 value = 0; value < function->value_count; value += 1)
            {
                IrInstructionId* definition = &function->values[value].definition;
                if (definition->value != IR_ID_UNDERLYING_INVALID)
                {
                    *definition = remap[definition->value];
                }
            }
            ir_cfg_remap_extras(scratch, function, remap);
        }
        // Each cycle carries its original row forward to the mapped slot.
        // A completed entry is identity. Cycles start at their lowest row,
        // so the outer loop can also discard each final construction link.
        for (u32 index = 0; index < count; index += 1)
        {
            if (remap[index].value != index)
            {
                IrInstruction saved = function->instructions[index];
                IrSourceRange source = function->instruction_canonical_sources ? function->instruction_canonical_sources[index] : (IrSourceRange){0};
                u32 from = index;
                u32 destination = remap[from].value;
                while (destination != index)
                {
                    IrInstruction next = function->instructions[destination];
                    function->instructions[destination] = saved;
                    saved = next;
                    if (function->instruction_canonical_sources)
                    {
                        IrSourceRange next_source = function->instruction_canonical_sources[destination];
                        function->instruction_canonical_sources[destination] = source;
                        source = next_source;
                    }
                    remap[from].value = from;
                    from = destination;
                    destination = remap[from].value;
                }
                function->instructions[index] = saved;
                if (function->instruction_canonical_sources)
                {
                    function->instruction_canonical_sources[index] = source;
                }
                remap[from].value = from;
            }
            function->instructions[index].next = IR_INSTRUCTION_ID_INVALID;
        }
        for (u32 index = 0; index < function->block_count; index += 1)
        {
            IrBlock* block = function->blocks + index;
            IrCfgBlock* published = blocks + index;
            block->first_instruction.value = published->instruction_count ? published->first_instruction : IR_ID_UNDERLYING_INVALID;
            block->last_instruction.value = published->instruction_count ? published->first_instruction + published->instruction_count - 1 : IR_ID_UNDERLYING_INVALID;
        }
        cfg->instruction_count = count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_published_cfg(IrFunction* function)
{
    IrValidationResult result = ir_validation_ok();
    IrPublishedCfg const* cfg = function->published_cfg;
    bool valid = cfg && cfg->block_count == function->block_count && cfg->instruction_count == function->instruction_count && cfg->blocks &&
                 (!cfg->edge_count || (cfg->edges && cfg->predecessors)) && (!cfg->parameter_count || cfg->parameters) &&
                 (!cfg->argument_count || cfg->arguments);
    u32 rows = 0;
    u32 successors = 0;
    u32 predecessors = 0;
    u32 parameters = 0;
    for (u32 index = 0; valid && index < cfg->block_count; index += 1)
    {
        IrCfgBlock const* block = cfg->blocks + index;
        IrBlock* original = function->blocks + index;
        valid = block->first_instruction == rows && block->instruction_count && block->instruction_count <= cfg->instruction_count - rows &&
                block->successor_offset == successors && block->successor_count <= cfg->edge_count - successors &&
                block->predecessor_offset == predecessors && block->predecessor_count <= cfg->edge_count - predecessors &&
                block->parameter_offset == parameters && block->parameter_count <= cfg->parameter_count - parameters &&
                original->id.value == index && original->first_instruction.value == rows && original->parameter_count == block->parameter_count &&
                original->predecessor_count == block->predecessor_count && !original->first_parameter && !original->last_parameter &&
                !original->first_predecessor && !original->last_predecessor;
        if (valid)
        {
            rows += block->instruction_count;
            successors += block->successor_count;
            predecessors += block->predecessor_count;
            parameters += block->parameter_count;
            valid = original->last_instruction.value == rows - 1;
        }
    }
    valid = valid && rows == cfg->instruction_count && successors == cfg->edge_count && predecessors == cfg->edge_count && parameters == cfg->parameter_count;
    if (valid)
    {
        Arena* owner = cfg->arena;
        TemporalArena scratch = scratch_begin(&owner, 1);
        u32* stamps = arena_allocate(scratch.arena, u32, cfg->block_count);
        memset(stamps, 0xff, sizeof(*stamps) * cfg->block_count);
        u32 arguments = 0;
        for (u32 source = 0; valid && source < cfg->block_count; source += 1)
        {
            IrCfgBlock const* block = cfg->blocks + source;
            IrInstruction* terminator = function->instructions + block->first_instruction + block->instruction_count - 1;
            valid = !terminator->target_count || terminator->targets;
            u32 successor = 0;
            for (u32 index = 0; valid && index < terminator->target_count; index += 1)
            {
                u32 target = terminator->targets[index].value;
                valid = target < cfg->block_count;
                if (valid && stamps[target] != source)
                {
                    stamps[target] = source;
                    valid = successor < block->successor_count;
                    if (valid)
                    {
                        IrCfgEdge const* edge = cfg->edges + block->successor_offset + successor++;
                        valid = edge->source.value == source && edge->destination.value == target && edge->argument_offset == arguments &&
                                cfg->blocks[target].parameter_count <= cfg->argument_count - arguments;
                        if (valid) arguments += cfg->blocks[target].parameter_count;
                    }
                }
            }
            valid = valid && successor == block->successor_count;
            u32 previous_source = 0;
            for (u32 index = 0; valid && index < block->predecessor_count; index += 1)
            {
                u32 edge_index = cfg->predecessors[block->predecessor_offset + index];
                valid = edge_index < cfg->edge_count;
                if (valid)
                {
                    IrCfgEdge const* edge = cfg->edges + edge_index;
                    valid = edge->destination.value == source && edge->source.value < cfg->block_count &&
                            (!index || previous_source < edge->source.value);
                    previous_source = edge->source.value;
                }
            }
        }
        valid = valid && arguments == cfg->argument_count;
        scratch_end(scratch);
    }
    if (!valid)
    {
        result = ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, IR_BLOCK_ID_INVALID, IR_INSTRUCTION_ID_INVALID);
    }
    return result;
}

IrValidationResult ir_function_publish_cfg(Arena* arena, IrFunction* function)
{
    IrValidationResult result = ir_validation_ok();
    if (!arena || !function || !function->block_count || !function->blocks || !function->instructions ||
        (function->value_count && !function->values) || function->entry.value >= function->block_count)
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    else if (!function->published_cfg)
    {
        IR_CONSTRUCTION_RECORD(CFG_BUILDS, 1);
        TemporalArena temporary = scratch_begin(&arena, 1);
        u32 count = function->block_count;
        IR_CONSTRUCTION_RECORD(CFG_SCRATCH_SLOTS, (u64)count * 3);
        u32* stamps = arena_allocate(temporary.arena, u32, count);
        u32* cursors = arena_allocate(temporary.arena, u32, count);
        u32* source_edges = arena_allocate(temporary.arena, u32, count);
        memset(stamps, 0xff, sizeof(*stamps) * count);
        IrCfgBlock* blocks = arena_allocate(arena, IrCfgBlock, count);
        memset(blocks, 0, sizeof(*blocks) * count);
        u64 edge_count = 0;
        u64 parameter_count = 0;
        for (u32 source = 0; source < count && result.error == IR_VALIDATION_NONE; source += 1)
        {
            IrBlock* block = function->blocks + source;
            u32 tail = block->last_instruction.value;
            IR_CONSTRUCTION_RECORD(CFG_TERMINATOR_CHECKS, 1);
            if (block->id.value != source || (tail >= function->instruction_count && tail != IR_ID_UNDERLYING_INVALID))
            {
                result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, block->last_instruction);
            }
            else if (tail == IR_ID_UNDERLYING_INVALID || !ir_instruction_is_terminator(function->instructions + tail))
            {
                // Edges come from the tail, so an open block would publish as
                // one that falls off its end. Every producer passes here,
                // including those that never ran the canonical validator.
                result = ir_validation_error(IR_VALIDATION_UNTERMINATED_BLOCK, function, block->id, block->last_instruction);
            }
            else
            {
                IrInstruction* terminator = function->instructions + tail;
                if (terminator->target_count && !terminator->targets)
                {
                    result = ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, block->last_instruction);
                }
                for (u32 index = 0; index < terminator->target_count && result.error == IR_VALIDATION_NONE; index += 1)
                {
                    IR_CONSTRUCTION_RECORD(CFG_TARGET_VISITS, 1);
                    u32 target = terminator->targets[index].value;
                    if (target >= count)
                    {
                        result = ir_validation_error(IR_VALIDATION_BRANCH_TARGET, function, block->id, block->last_instruction);
                    }
                    else if (stamps[target] != source)
                    {
                        stamps[target] = source;
                        blocks[source].successor_count += 1;
                        blocks[target].predecessor_count += 1;
                        edge_count += 1;
                    }
                }
                blocks[source].parameter_count = block->parameter_count;
                parameter_count += block->parameter_count;
                if (edge_count > UINT32_MAX || parameter_count > UINT32_MAX)
                {
                    result = ir_validation_error(IR_VALIDATION_INVALID_ID, function, block->id, block->last_instruction);
                }
            }
        }
        u64 argument_count = 0;
        if (result.error == IR_VALIDATION_NONE)
        {
            u32 successor_offset = 0;
            u32 predecessor_offset = 0;
            u32 parameter_offset = 0;
            for (u32 index = 0; index < count; index += 1)
            {
                IrCfgBlock* block = blocks + index;
                block->successor_offset = successor_offset;
                block->predecessor_offset = predecessor_offset;
                block->parameter_offset = parameter_offset;
                successor_offset += block->successor_count;
                predecessor_offset += block->predecessor_count;
                parameter_offset += block->parameter_count;
                argument_count += (u64)block->parameter_count * block->predecessor_count;
                cursors[index] = block->predecessor_offset;
            }
            if (argument_count > UINT32_MAX)
            {
                result.error = IR_VALIDATION_BLOCK_PARAMETER;
            }
        }
        if (result.error == IR_VALIDATION_NONE)
        {
            IrCfgEdge* edges = arena_allocate(arena, IrCfgEdge, edge_count);
            u32* predecessors = arena_allocate(arena, u32, edge_count);
            IrCfgParameter* parameters = arena_allocate(arena, IrCfgParameter, parameter_count);
            IrValueId* arguments = arena_allocate(arena, IrValueId, argument_count);
            // The largest predecessor list cannot exceed the number of blocks:
            // parallel edges to one destination have already been suppressed.
            u32* predecessor_order = arena_allocate(temporary.arena, u32, count);
            IR_CONSTRUCTION_RECORD(CFG_SCRATCH_SLOTS, count);
            memset(stamps, 0xff, sizeof(*stamps) * count);
            u32 edge_cursor = 0;
            u32 argument_cursor = 0;
            for (u32 source = 0; source < count; source += 1)
            {
                IrInstruction* terminator = function->instructions + function->blocks[source].last_instruction.value;
                for (u32 index = 0; index < terminator->target_count; index += 1)
                {
                    u32 target = terminator->targets[index].value;
                    if (stamps[target] != source)
                    {
                        stamps[target] = source;
                        edges[edge_cursor] = (IrCfgEdge){.source = {.value = source}, .destination = {.value = target},
                                                       .argument_offset = argument_cursor};
                        predecessors[cursors[target]++] = edge_cursor++;
                        argument_cursor += blocks[target].parameter_count;
                    }
                }
            }
            IR_CONSTRUCTION_RECORD(CFG_UNIQUE_EDGES, edge_count);
            memset(stamps, 0xff, sizeof(*stamps) * count);
            for (u32 target = 0; target < count && result.error == IR_VALIDATION_NONE; target += 1)
            {
                IrBlock* builder = function->blocks + target;
                IrCfgBlock* block = blocks + target;
                bool valid = true;
                for (u32 index = 0; index < block->predecessor_count; index += 1)
                {
                    u32 edge = predecessors[block->predecessor_offset + index];
                    u32 source = edges[edge].source.value;
                    source_edges[source] = edge;
                    stamps[source] = target;
                    cursors[source] = UINT32_MAX;
                }
                // Parameter-free blocks may omit their builder predecessor
                // list. If present, require its exact extent and topology too.
                bool listed = builder->predecessor_count || builder->first_predecessor || builder->last_predecessor || builder->parameter_count;
                if (listed)
                {
                    valid = builder->predecessor_count == block->predecessor_count;
                    IrPredecessor* predecessor = builder->first_predecessor;
                    IrPredecessor* last = 0;
                    for (u32 index = 0; index < block->predecessor_count && valid; index += 1)
                    {
                        valid = predecessor && predecessor->block.value < count;
                        if (valid)
                        {
                            u32 source = predecessor->block.value;
                            valid = stamps[source] == target && cursors[source] != target;
                            if (valid)
                            {
                                cursors[source] = target;
                                predecessor_order[index] = source_edges[source];
                                last = predecessor;
                                predecessor = predecessor->next;
                            }
                        }
                    }
                    valid = valid && !predecessor && last == builder->last_predecessor;
                }
                IrBlockParameter* parameter = builder->first_parameter;
                IrBlockParameter* last_parameter = 0;
                for (u32 index = 0; index < block->parameter_count && valid; index += 1)
                {
                    IR_CONSTRUCTION_RECORD(CFG_PARAMETER_VISITS, 1);
                    valid = parameter && parameter->value.value < function->value_count &&
                            parameter->incoming_count == block->predecessor_count &&
                            function->values[parameter->value.value].canonical_type.value == parameter->canonical_type.value;
                    if (valid)
                    {
                        parameters[block->parameter_offset + index] = (IrCfgParameter){.canonical_type = parameter->canonical_type,
                            .canonical_local = parameter->canonical_local, .value = parameter->value};
                        IrIncoming* incoming = parameter->first_incoming;
                        IrIncoming* last = 0;
                        for (u32 predecessor = 0; predecessor < block->predecessor_count && valid; predecessor += 1)
                        {
                            IR_CONSTRUCTION_RECORD(CFG_INCOMING_VISITS, 1);
                            IrCfgEdge* edge = edges + predecessor_order[predecessor];
                            valid = incoming && incoming->predecessor.value == edge->source.value && incoming->value.value < function->value_count &&
                                    function->values[incoming->value.value].canonical_type.value == parameter->canonical_type.value;
                            if (valid)
                            {
                                arguments[edge->argument_offset + index] = incoming->value;
                                last = incoming;
                                incoming = incoming->next;
                            }
                        }
                        valid = valid && !incoming && last == parameter->last_incoming;
                        last_parameter = parameter;
                        parameter = parameter->next;
                    }
                }
                valid = valid && !parameter && last_parameter == builder->last_parameter;
                if (!valid)
                {
                    result = ir_validation_error(IR_VALIDATION_BLOCK_PARAMETER, function, builder->id, IR_INSTRUCTION_ID_INVALID);
                }
            }
            if (result.error == IR_VALIDATION_NONE)
            {
                IrPublishedCfg* cfg = arena_allocate(arena, IrPublishedCfg, 1);
                *cfg = (IrPublishedCfg){.arena = arena, .blocks = blocks, .edges = edges, .predecessors = predecessors, .parameters = parameters,
                    .arguments = arguments, .block_count = count, .edge_count = (u32)edge_count, .parameter_count = (u32)parameter_count,
                    .argument_count = (u32)argument_count, .allocated_bytes = sizeof(*cfg) + sizeof(*blocks) * (u64)count +
                        (sizeof(*edges) + sizeof(*predecessors)) * edge_count + sizeof(*parameters) * parameter_count +
                        sizeof(*arguments) * argument_count};
                result = ir_cfg_publish_instruction_rows(arena, temporary.arena, function, cfg, blocks);
                if (result.error == IR_VALIDATION_NONE)
                {
                    for (u32 block = 0; block < count; block += 1)
                    {
                        IrBlock* builder = function->blocks + block;
                        builder->first_parameter = 0;
                        builder->last_parameter = 0;
                        builder->first_predecessor = 0;
                        builder->last_predecessor = 0;
                        builder->predecessor_count = cfg->blocks[block].predecessor_count;
                    }
                    function->published_cfg = cfg;
                }
            }
        }
        scratch_end(temporary);
    }
    if (result.error != IR_VALIDATION_NONE)
    {
        result.boundary = IR_VALIDATION_BOUNDARY_CFG_PUBLICATION;
        IR_CONSTRUCTION_RECORD(CFG_FAILURES, 1);
    }
    return result;
}
