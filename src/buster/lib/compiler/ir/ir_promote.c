// Shared canonical local promotion. Included by ir.c in both build modes.
// Classification decodes operands once into per-local event lists. The common
// store-before-load block-local case needs no CFG analysis. Other candidates
// use one reusable O(blocks + edges) scratch tile: a may-uninitialized walk,
// backward live-in pruning, then block-argument SSA construction. Every walk
// is iterative, including irreducible loops; no C recursion or blocks*locals
// resident matrix is required. ir_promote_compact owns the single final ID
// remap, including instruction sources, extras, labels and builder side data.

#define IR_PROMOTE_NONE UINT32_MAX

typedef struct IrPromoteEvent IrPromoteEvent;
struct IrPromoteEvent
{
    u32 instruction;
    u32 block;
    u32 next;
};

typedef struct IrPromoteLocal IrPromoteLocal;
struct IrPromoteLocal
{
    u32 value;
    u32 first;
    u32 last;
    bool eligible;
};

typedef struct IrPromoteCfg IrPromoteCfg;
struct IrPromoteCfg
{
    u32* offsets;
    u32* predecessors;
    u8* reachable;
    u32* queue;
};

bool ir_local_type_promotable(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    bool result = false;
    if (type && type->layout.resolved && type->layout.size && !type->is_atomic && !type->is_volatile)
    {
        // A sub-int load performs the C frontend's required truncation and
        // signed/unsigned extension. Replacing it directly with the stored
        // SSA value would erase that normalization on targets such as eBPF;
        // keep narrow integer locals in memory until canonical IR represents
        // the conversion explicitly.
        result = type->kind == IR_TYPE_BOOLEAN || type->kind == IR_TYPE_POINTER ||
                 (type->kind == IR_TYPE_INTEGER && type->bit_width >= 32 && type->bit_width <= 128) ||
                 (type->kind == IR_TYPE_FLOAT && (type->bit_width == 32 || type->bit_width == 64));
        if (type->kind == IR_TYPE_VECTOR && type->layout.size <= 64 && !(type->layout.size & (type->layout.size - 1)))
        {
            IrType* element = ir_type_from_id(&program->types, type->element_type);
            result = element && !element->is_atomic && !element->is_volatile && element->layout.resolved &&
                     element->layout.size && type->element_count == type->layout.size / element->layout.size &&
                     type->layout.size % element->layout.size == 0 &&
                     ((element->kind == IR_TYPE_INTEGER && element->bit_width <= 64) ||
                      (element->kind == IR_TYPE_FLOAT && (element->bit_width == 32 || element->bit_width == 64)));
        }
    }
    return result;
}

bool ir_local_promotion_call_barrier(IrProgram* program, IrInstruction const* row)
{
    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, row->symbol);
    // The canonical call contract does not yet carry returns_twice. Indirect
    // calls cannot establish its absence; the known non-local-jump spellings
    // likewise keep the entire function in memory until that effect is modeled.
    bool result = !symbol;
    if (symbol)
    {
        String8 names[] = {S8("setjmp"), S8("_setjmp"), S8("sigsetjmp"), S8("__sigsetjmp"), S8("__builtin_setjmp"),
                           S8("longjmp"), S8("_longjmp"), S8("siglongjmp"), S8("__longjmp_chk"), S8("__builtin_longjmp")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names) && !result; index += 1)
        {
            result = string_equal(symbol->name, names[index]);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_promote_cfg(Arena* arena, IrProgram* program, IrFunction* function, IrPromoteCfg* cfg)
{
    u32 count = function->block_count;
    cfg->offsets = arena_allocate(arena, u32, (u64)count + 1);
    cfg->reachable = arena_allocate(arena, u8, count);
    cfg->queue = arena_allocate(arena, u32, count);
    u32* stamps = arena_allocate(arena, u32, count);
    memset(cfg->offsets, 0, sizeof(u32) * ((u64)count + 1));
    memset(cfg->reachable, 0, count);
    memset(stamps, 0, sizeof(u32) * count);
    bool valid = function->entry.value < count;
    u64 edge_count = 0;
    for (u32 block = 0; block < count && valid; block += 1)
    {
        IrBlock* source = function->blocks + block;
        valid = source->id.value == block && source->sealed && source->terminated && source->last_instruction.value < function->instruction_count;
        if (valid)
        {
            IrInstruction* terminator = function->instructions + source->last_instruction.value;
            for (u32 index = 0; index < terminator->target_count && valid; index += 1)
            {
                u32 target = terminator->targets[index].value;
                valid = target < count;
                if (valid && stamps[target] != block + 1)
                {
                    stamps[target] = block + 1;
                    cfg->offsets[target + 1] += 1;
                    edge_count += 1;
                }
            }
        }
    }
    valid &= edge_count <= UINT32_MAX;
    if (valid)
    {
        for (u32 block = 0; block < count; block += 1)
        {
            cfg->offsets[block + 1] += cfg->offsets[block];
        }
        cfg->predecessors = arena_allocate(arena, u32, edge_count);
        u32* cursor = arena_allocate(arena, u32, count);
        memcpy(cursor, cfg->offsets, sizeof(u32) * count);
        memset(stamps, 0, sizeof(u32) * count);
        for (u32 block = 0; block < count; block += 1)
        {
            IrInstruction* row = function->instructions + function->blocks[block].last_instruction.value;
            for (u32 index = 0; index < row->target_count; index += 1)
            {
                u32 target = row->targets[index].value;
                if (stamps[target] != block + 1)
                {
                    stamps[target] = block + 1;
                    cfg->predecessors[cursor[target]++] = block;
                }
            }
        }
        // Prove that the frontend's predecessor lists describe exactly the
        // terminator edges, then retain their order for every incoming list.
        memset(stamps, 0, sizeof(u32) * count);
        for (u32 block = 0; block < count && valid; block += 1)
        {
            u32 begin = cfg->offsets[block];
            u32 end = cfg->offsets[block + 1];
            for (u32 index = begin; index < end; index += 1)
            {
                stamps[cfg->predecessors[index]] = block + 1;
            }
            IrBlock* destination = function->blocks + block;
            // The memory-form C builder may publish terminators without
            // predecessor lists; direct SSA publishes the complete graph.
            // Empty lists without parameters are unmaterialized, not a claim
            // that the block has no incoming CFG edges.
            if (!destination->predecessor_count && !destination->first_predecessor && !destination->parameter_count)
            {
                continue;
            }
            valid = end - begin == destination->predecessor_count;
            u32 index = begin;
            for (IrPredecessor* pred = function->blocks[block].first_predecessor; pred && valid; pred = pred->next)
            {
                valid = index < end && pred->block.value < count && stamps[pred->block.value] == block + 1;
                if (valid)
                {
                    stamps[pred->block.value] = 0;
                    cfg->predecessors[index++] = pred->block.value;
                }
            }
            valid &= index == end;
        }
        if (valid)
        {
            for (u32 block = 0; block < count; block += 1)
            {
                IrBlock* destination = function->blocks + block;
                if (!destination->predecessor_count && !destination->first_predecessor)
                {
                    for (u32 index = cfg->offsets[block]; index < cfg->offsets[block + 1]; index += 1)
                    {
                        IrPredecessor* pred = arena_allocate(program->arena, IrPredecessor, 1);
                        *pred = (IrPredecessor){.block = {.value = cfg->predecessors[index]}};
                        if (destination->last_predecessor)
                        {
                            destination->last_predecessor->next = pred;
                        }
                        else
                        {
                            destination->first_predecessor = pred;
                        }
                        destination->last_predecessor = pred;
                        destination->predecessor_count += 1;
                    }
                }
            }
            u32 read = 0;
            u32 write = 0;
            cfg->queue[write++] = function->entry.value;
            cfg->reachable[function->entry.value] = 1;
            while (read < write)
            {
                u32 block = cfg->queue[read++];
                IrInstruction* row = function->instructions + function->blocks[block].last_instruction.value;
                for (u32 index = 0; index < row->target_count; index += 1)
                {
                    u32 target = row->targets[index].value;
                    if (!cfg->reachable[target])
                    {
                        cfg->reachable[target] = 1;
                        cfg->queue[write++] = target;
                    }
                }
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 ir_promote_root(u32* replacements, u32 value)
{
    u32 root = value;
    while (replacements[root] != root)
    {
        root = replacements[root];
    }
    while (value != root)
    {
        u32 next = replacements[value];
        replacements[value] = root;
        value = next;
    }
    return root;
}

BUSTER_GLOBAL_LOCAL void ir_promote_remove_events(IrFunction* function, IrPromoteLocal* local, IrPromoteEvent* events, u8* removed,
                                                  u32* replacements, u32 const* entries, IrLocalPromotionStatistics* statistics)
{
    u32 current = IR_PROMOTE_NONE;
    u32 block = IR_PROMOTE_NONE;
    for (u32 event = local->first; event != IR_PROMOTE_NONE; event = events[event].next)
    {
        IrPromoteEvent access = events[event];
        IrInstruction* row = function->instructions + access.instruction;
        if (block != access.block)
        {
            block = access.block;
            current = entries ? entries[block] : IR_PROMOTE_NONE;
        }
        if (row->opcode == IR_OPCODE_LOCAL)
        {
            current = IR_PROMOTE_NONE;
            if (function->local_places && row->canonical_local.value < function->local_count &&
                function->local_places[row->canonical_local.value].value == local->value)
            {
                function->local_places[row->canonical_local.value] = IR_VALUE_ID_INVALID;
                if (function->local_uses_memory)
                {
                    function->local_uses_memory[row->canonical_local.value] = false;
                }
            }
        }
        else if (row->opcode == IR_OPCODE_STORE)
        {
            current = row->operands[1].value;
            statistics->removed_stores += 1;
        }
        else
        {
            BUSTER_CHECK(current != IR_PROMOTE_NONE);
            replacements[row->result.value] = current;
            statistics->removed_loads += 1;
        }
        removed[access.instruction] = 1;
    }
    statistics->promoted_locals += 1;
}

// All arrays below are reused for the next local. Effects are 0=forward the
// incoming value, 1=store a new definition, 2=LOCAL lifetime reset.
BUSTER_GLOBAL_LOCAL bool ir_promote_global(Arena* arena, IrProgram* program, IrFunction* function, IrPromoteLocal* local,
                                           IrPromoteEvent* events, IrPromoteCfg* cfg, u8* effects, u8* live, u8* bad_in, u8* bad_out,
                                           u32* entries, u32* exits, u32* replacements, u8* removed, IrLocalPromotionStatistics* statistics)
{
    u32 count = function->block_count;
    memset(effects, 0, count);
    memset(live, 0, count);
    memset(bad_in, 0, count);
    memset(bad_out, 0, count);
    memset(entries, 0xff, sizeof(u32) * count);
    memset(exits, 0xff, sizeof(u32) * count);
    bool safe = true;
    for (u32 event = local->first; event != IR_PROMOTE_NONE; event = events[event].next)
    {
        IrPromoteEvent access = events[event];
        IrInstruction* row = function->instructions + access.instruction;
        u32 block = access.block;
        safe &= cfg->reachable[block] != 0;
        if (row->opcode == IR_OPCODE_LOAD)
        {
            safe &= effects[block] != 2;
            live[block] |= effects[block] == 0;
        }
        else
        {
            effects[block] = row->opcode == IR_OPCODE_STORE ? 1 : 2;
            exits[block] = row->opcode == IR_OPCODE_STORE ? row->operands[1].value : IR_PROMOTE_NONE;
        }
    }
    u32 read = 0;
    u32 write = 0;
    for (u32 block = 0; block < count; block += 1)
    {
        bad_in[block] = block == function->entry.value || !cfg->reachable[block] || cfg->offsets[block] == cfg->offsets[block + 1];
        bad_out[block] = effects[block] == 2 || (effects[block] == 0 && bad_in[block]);
        if (bad_out[block])
        {
            cfg->queue[write++] = block;
        }
    }
    while (read < write)
    {
        u32 block = cfg->queue[read++];
        IrInstruction* terminator = function->instructions + function->blocks[block].last_instruction.value;
        for (u32 index = 0; index < terminator->target_count; index += 1)
        {
            u32 target = terminator->targets[index].value;
            bad_in[target] = 1;
            if (effects[target] == 0 && !bad_out[target])
            {
                bad_out[target] = 1;
                cfg->queue[write++] = target;
            }
        }
    }
    for (u32 block = 0; block < count; block += 1)
    {
        safe &= !(live[block] && bad_in[block]);
    }
    if (safe)
    {
        read = 0;
        write = 0;
        for (u32 block = 0; block < count; block += 1)
        {
            if (live[block])
            {
                cfg->queue[write++] = block;
            }
        }
        while (read < write)
        {
            u32 block = cfg->queue[read++];
            for (u32 index = cfg->offsets[block]; index < cfg->offsets[block + 1]; index += 1)
            {
                u32 pred = cfg->predecessors[index];
                if (!effects[pred] && !live[pred])
                {
                    live[pred] = 1;
                    cfg->queue[write++] = pred;
                }
            }
        }
        u32 parameter_count = 0;
        u32 first_parameter = function->value_count;
        for (u32 block = 0; block < count; block += 1)
        {
            if (live[block] && cfg->offsets[block + 1] - cfg->offsets[block] > 1)
            {
                safe &= function->blocks[block].parameter_count < UINT16_MAX && (u64)first_parameter + parameter_count < UINT32_MAX;
                entries[block] = first_parameter + parameter_count++;
                if (!effects[block])
                {
                    exits[block] = entries[block];
                }
            }
        }
        // Known store/parameter definitions flow along single-predecessor
        // chains. A cyclic live chain necessarily reaches a join parameter.
        read = 0;
        write = 0;
        for (u32 block = 0; block < count; block += 1)
        {
            if (exits[block] != IR_PROMOTE_NONE)
            {
                cfg->queue[write++] = block;
            }
        }
        while (read < write)
        {
            u32 block = cfg->queue[read++];
            IrInstruction* row = function->instructions + function->blocks[block].last_instruction.value;
            for (u32 index = 0; index < row->target_count; index += 1)
            {
                u32 target = row->targets[index].value;
                if (live[target] && entries[target] == IR_PROMOTE_NONE && cfg->offsets[target + 1] - cfg->offsets[target] == 1)
                {
                    entries[target] = exits[block];
                    if (!effects[target])
                    {
                        exits[target] = exits[block];
                        cfg->queue[write++] = target;
                    }
                }
            }
        }
        for (u32 block = 0; block < count; block += 1)
        {
            safe &= !live[block] || entries[block] != IR_PROMOTE_NONE;
            if (live[block])
            {
                for (u32 index = cfg->offsets[block]; index < cfg->offsets[block + 1]; index += 1)
                {
                    safe &= exits[cfg->predecessors[index]] != IR_PROMOTE_NONE;
                }
            }
        }
        if (safe)
        {
            IrTypeId type = function->values[local->value].canonical_type;
            IrLocalId canonical_local = function->instructions[function->values[local->value].definition.value].canonical_local;
            for (u32 block = 0; block < count; block += 1)
            {
                if (live[block] && cfg->offsets[block + 1] - cfg->offsets[block] > 1)
                {
                    IrBlock* destination = function->blocks + block;
                    IrValueId value = ir_function_add_value(program->arena, function, (IrValue){
                        .canonical_type = type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE,
                    });
                    BUSTER_CHECK(value.value == entries[block]);
                    IrBlockParameter* parameter = arena_allocate(program->arena, IrBlockParameter, 1);
                    *parameter = (IrBlockParameter){.value = value, .canonical_type = type, .canonical_local = canonical_local};
                    for (u32 index = cfg->offsets[block]; index < cfg->offsets[block + 1]; index += 1)
                    {
                        u32 predecessor = cfg->predecessors[index];
                        IrIncoming* incoming = arena_allocate(program->arena, IrIncoming, 1);
                        *incoming = (IrIncoming){.predecessor = {.value = predecessor}, .value = {.value = exits[predecessor]}};
                        if (parameter->last_incoming)
                        {
                            parameter->last_incoming->next = incoming;
                        }
                        else
                        {
                            parameter->first_incoming = incoming;
                        }
                        parameter->last_incoming = incoming;
                        parameter->incoming_count += 1;
                    }
                    if (destination->last_parameter)
                    {
                        destination->last_parameter->next = parameter;
                    }
                    else
                    {
                        destination->first_parameter = parameter;
                    }
                    destination->last_parameter = parameter;
                    destination->parameter_count += 1;
                    statistics->inserted_parameters += 1;
                }
            }
            ir_promote_remove_events(function, local, events, removed, replacements, entries, statistics);
        }
    }
    if (!safe)
    {
        statistics->uninitialized_locals += 1;
    }
    (void)arena;
    return safe;
}

BUSTER_GLOBAL_LOCAL void ir_promote_compact(Arena* arena, IrProgram* program, IrFunction* function, u32 old_value_count, u32* load_replacements,
                                            u8* removed, IrLocalPromotionStatistics* statistics)
{
    u32 count = function->value_count;
    u32* replacements = arena_allocate(arena, u32, count);
    for (u32 value = 0; value < count; value += 1)
    {
        replacements[value] = value < old_value_count ? load_replacements[value] : value;
    }
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            IrBlock* destination = function->blocks + block;
            IrBlockParameter** link = &destination->first_parameter;
            destination->last_parameter = 0;
            while (*link)
            {
                IrBlockParameter* parameter = *link;
                u32 same = IR_PROMOTE_NONE;
                bool trivial = parameter->value.value >= old_value_count;
                for (IrIncoming* incoming = parameter->first_incoming; incoming && trivial; incoming = incoming->next)
                {
                    u32 value = ir_promote_root(replacements, incoming->value.value);
                    if (value != parameter->value.value)
                    {
                        trivial = same == IR_PROMOTE_NONE || value == same;
                        same = value;
                    }
                }
                if (trivial && same != IR_PROMOTE_NONE)
                {
                    replacements[parameter->value.value] = same;
                    *link = parameter->next;
                    destination->parameter_count -= 1;
                    statistics->removed_parameters += 1;
                    changed = true;
                }
                else
                {
                    destination->last_parameter = parameter;
                    link = &parameter->next;
                }
            }
        }
    }
    u32* instruction_map = arena_allocate(arena, u32, function->instruction_count);
    u32* next_map = arena_allocate(arena, u32, function->instruction_count);
    u32* value_map = arena_allocate(arena, u32, count);
    memset(value_map, 0xff, sizeof(u32) * count);
    memset(next_map, 0xff, sizeof(u32) * function->instruction_count);
    u32 instruction_count = 0;
    u64 operand_count = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        instruction_map[index] = removed[index] ? IR_PROMOTE_NONE : instruction_count++;
        if (!removed[index])
        {
            IrInstruction* row = function->instructions + index;
            operand_count += row->operand_count;
            if (row->result.value < count)
            {
                value_map[row->result.value] = 0;
            }
        }
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        IrBlock* destination = function->blocks + block;
        for (IrBlockParameter* parameter = destination->first_parameter; parameter; parameter = parameter->next)
        {
            value_map[parameter->value.value] = 0;
        }
        u32 previous = IR_PROMOTE_NONE;
        u32 first = IR_PROMOTE_NONE;
        for (u32 row = destination->first_instruction.value; row != IR_PROMOTE_NONE; row = function->instructions[row].next.value)
        {
            if (!removed[row])
            {
                if (previous != IR_PROMOTE_NONE)
                {
                    next_map[previous] = instruction_map[row];
                }
                else
                {
                    first = instruction_map[row];
                }
                previous = row;
            }
        }
        destination->first_instruction.value = first;
        destination->last_instruction.value = previous == IR_PROMOTE_NONE ? IR_PROMOTE_NONE : instruction_map[previous];
    }
    u32 value_count = 0;
    for (u32 value = 0; value < count; value += 1)
    {
        if (value_map[value] != IR_PROMOTE_NONE)
        {
            value_map[value] = value_count++;
        }
    }
    // Flatten before overwriting value rows. Only eliminated place IDs have no
    // replacement; classification proved no retained semantic operand uses one.
    for (u32 value = 0; value < count; value += 1)
    {
        replacements[value] = ir_promote_root(replacements, value);
    }
    for (u32 value = 0; value < count; value += 1)
    {
        u32 destination = value_map[value];
        if (destination != IR_PROMOTE_NONE)
        {
            IrValue copy = function->values[value];
            if (copy.definition.value != IR_PROMOTE_NONE)
            {
                copy.definition.value = instruction_map[copy.definition.value];
            }
            function->values[destination] = copy;
        }
    }
    // Shared operand slices are legal in hand-built IR. Copy into one pool,
    // rather than remapping a shared slice in place more than once.
    IrValueId* operands = arena_allocate(program->arena, IrValueId, operand_count);
    for (u32 value = 0; value < count; value += 1)
    {
        replacements[value] = value_map[replacements[value]];
    }
    u64 operand_cursor = 0;
    function->opcode_summary = IR_OPCODE_SUMMARY_KNOWN;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        if (!removed[index])
        {
            IrInstruction copy = function->instructions[index];
            IrValueId* old_operands = copy.operands;
            copy.operands = copy.operand_count ? operands + operand_cursor : 0;
            for (u32 operand = 0; operand < copy.operand_count; operand += 1)
            {
                u32 value = replacements[old_operands[operand].value];
                BUSTER_CHECK(value != IR_PROMOTE_NONE);
                copy.operands[operand].value = value;
            }
            operand_cursor += copy.operand_count;
            copy.result.value = copy.result.value < count ? value_map[copy.result.value] : IR_PROMOTE_NONE;
            copy.next.value = next_map[index];
            function->instructions[instruction_map[index]] = copy;
            if (function->instruction_canonical_sources)
            {
                function->instruction_canonical_sources[instruction_map[index]] = function->instruction_canonical_sources[index];
            }
            function->opcode_summary |= (u64)1 << copy.opcode;
        }
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        IrBlock* destination = function->blocks + block;
        for (IrBlockParameter* parameter = destination->first_parameter; parameter; parameter = parameter->next)
        {
            parameter->value.value = value_map[parameter->value.value];
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                incoming->value.value = replacements[incoming->value.value];
                BUSTER_CHECK(incoming->value.value != IR_PROMOTE_NONE);
            }
        }
        if (destination->local_values)
        {
            for (u32 local = 0; local < function->local_count; local += 1)
            {
                u32 value = destination->local_values[local].value;
                destination->local_values[local].value = value < count ? replacements[value] : IR_PROMOTE_NONE;
            }
        }
    }
    if (function->local_places)
    {
        for (u32 local = 0; local < function->local_count; local += 1)
        {
            u32 value = function->local_places[local].value;
            function->local_places[local].value = value < count ? replacements[value] : IR_PROMOTE_NONE;
        }
    }
    u32 extra_count = 0;
    for (u32 index = 0; index < function->extra_count; index += 1)
    {
        u32 old = function->extra_instructions[index].value;
        if (!removed[old])
        {
            function->extra_instructions[extra_count].value = instruction_map[old];
            function->extras[extra_count++] = function->extras[index];
        }
    }
    function->extra_count = extra_count;
    // Label-bearing functions are excluded before classification. Keeping their
    // identity/provenance graph untouched is intentional, not a lossy remap.
    BUSTER_CHECK(function->label_metadata_count == 0);
    function->instruction_count = instruction_count;
    function->value_count = value_count;
}

BUSTER_GLOBAL_LOCAL void ir_promote_function(IrProgram* program, IrFunction* function, IrLocalPromotionStatistics* statistics)
{
    statistics->instructions_before += function->instruction_count;
    statistics->values_before += function->value_count;
    u32 local_count = 0;
    bool barrier = function->label_metadata_count != 0;
    // Direct frontend SSA emits no LOCAL rows for promoted owners. Use the
    // producer's conservative opcode summary to avoid a redundant FAST-path
    // discovery scan. Hand-built/uncertified functions still scan normally.
    bool may_have_locals = ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_LOCAL));
    for (u32 index = 0; may_have_locals && index < function->instruction_count && !barrier; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        local_count += row->opcode == IR_OPCODE_LOCAL;
        barrier = row->opcode == IR_OPCODE_INLINE_ASSEMBLY || row->opcode == IR_OPCODE_INDIRECT_BRANCH ||
                  row->opcode == IR_OPCODE_LABEL_ADDRESS || row->opcode == IR_OPCODE_STACK_ALLOCATE ||
                  row->opcode == IR_OPCODE_STACK_SAVE || row->opcode == IR_OPCODE_STACK_RESTORE ||
                  (row->opcode == IR_OPCODE_CALL && ir_local_promotion_call_barrier(program, row));
    }
    if (barrier)
    {
        statistics->barrier_functions += 1;
    }
    if (local_count && !barrier)
    {
        TemporalArena scratch = scratch_begin(&program->arena, 1);
        Arena* arena = scratch.arena;
        u32 old_value_count = function->value_count;
        u32* local_by_value = arena_allocate(arena, u32, old_value_count);
        u32* replacements = arena_allocate(arena, u32, old_value_count);
        u8* removed = arena_allocate(arena, u8, function->instruction_count);
        IrPromoteLocal* locals = arena_allocate(arena, IrPromoteLocal, local_count);
        IrPromoteEvent* events = arena_allocate(arena, IrPromoteEvent, function->instruction_count);
        memset(local_by_value, 0xff, sizeof(u32) * old_value_count);
        memset(removed, 0, function->instruction_count);
        for (u32 value = 0; value < old_value_count; value += 1)
        {
            replacements[value] = value;
        }
        u32 local_index = 0;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            if (row->opcode == IR_OPCODE_LOCAL)
            {
                IrValue* value = function->values + row->result.value;
                locals[local_index] = (IrPromoteLocal){
                    .value = row->result.value, .first = IR_PROMOTE_NONE, .last = IR_PROMOTE_NONE,
                    .eligible = value->category == IR_VALUE_PLACE && !value->is_volatile && !row->volatile_access &&
                                row->operand_count == 0 && ir_local_type_promotable(program, row->canonical_type),
                };
                local_by_value[row->result.value] = local_index++;
            }
        }
        statistics->candidate_locals += local_count;
        u32 event_count = 0;
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            IrBlock* source = function->blocks + block;
            for (u32 index = source->first_instruction.value; index != IR_PROMOTE_NONE; index = function->instructions[index].next.value)
            {
                IrInstruction* row = function->instructions + index;
                u32 owner = row->opcode == IR_OPCODE_LOCAL ? local_by_value[row->result.value] : IR_PROMOTE_NONE;
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    u32 local = local_by_value[row->operands[operand].value];
                    if (local != IR_PROMOTE_NONE)
                    {
                        IrPromoteLocal* candidate = locals + local;
                        IrTypeId type = function->values[candidate->value].canonical_type;
                        bool load = row->opcode == IR_OPCODE_LOAD && row->operand_count == 1 && operand == 0 &&
                                    row->canonical_type.value == type.value;
                        bool store = row->opcode == IR_OPCODE_STORE && row->operand_count == 2 && operand == 0 &&
                                     function->values[row->operands[1].value].canonical_type.value == type.value &&
                                     function->values[row->operands[1].value].category == IR_VALUE_VALUE;
                        candidate->eligible &= (load || store) && !row->volatile_access;
                        if (load || store)
                        {
                            owner = local;
                        }
                    }
                }
                if (owner != IR_PROMOTE_NONE)
                {
                    IrPromoteLocal* local = locals + owner;
                    events[event_count] = (IrPromoteEvent){.instruction = index, .block = block, .next = IR_PROMOTE_NONE};
                    if (local->last != IR_PROMOTE_NONE)
                    {
                        events[local->last].next = event_count;
                    }
                    else
                    {
                        local->first = event_count;
                    }
                    local->last = event_count++;
                }
            }
            for (IrBlockParameter* parameter = source->first_parameter; parameter; parameter = parameter->next)
            {
                for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
                {
                    u32 local = local_by_value[incoming->value.value];
                    if (local != IR_PROMOTE_NONE)
                    {
                        locals[local].eligible = false;
                    }
                }
            }
        }
        IrPromoteCfg cfg = {0};
        bool cfg_built = false;
        bool cfg_valid = false;
        u8* effects = 0;
        u8* live = 0;
        u8* bad_in = 0;
        u8* bad_out = 0;
        u32* entries = 0;
        u32* exits = 0;
        u64 promoted_before = statistics->promoted_locals;
        for (u32 index = 0; index < local_count; index += 1)
        {
            IrPromoteLocal* local = locals + index;
            if (local->eligible)
            {
                // Each block may independently define the local before reading
                // it. Those cases (including write-only locals) need no CFG.
                bool block_local = true;
                bool defined = false;
                u32 block = IR_PROMOTE_NONE;
                for (u32 event = local->first; event != IR_PROMOTE_NONE && block_local; event = events[event].next)
                {
                    IrPromoteEvent access = events[event];
                    IrInstruction* row = function->instructions + access.instruction;
                    if (access.block != block)
                    {
                        defined = false;
                        block = access.block;
                    }
                    if (row->opcode == IR_OPCODE_LOAD)
                    {
                        block_local &= defined;
                    }
                    else
                    {
                        defined = row->opcode == IR_OPCODE_STORE;
                    }
                }
                if (block_local)
                {
                    ir_promote_remove_events(function, local, events, removed, replacements, 0, statistics);
                }
                else
                {
                    if (!cfg_built)
                    {
                        cfg_built = true;
                        cfg_valid = ir_promote_cfg(arena, program, function, &cfg);
                        effects = arena_allocate(arena, u8, function->block_count);
                        live = arena_allocate(arena, u8, function->block_count);
                        bad_in = arena_allocate(arena, u8, function->block_count);
                        bad_out = arena_allocate(arena, u8, function->block_count);
                        entries = arena_allocate(arena, u32, function->block_count);
                        exits = arena_allocate(arena, u32, function->block_count);
                    }
                    if (cfg_valid)
                    {
                        ir_promote_global(arena, program, function, local, events, &cfg, effects, live, bad_in, bad_out,
                                          entries, exits, replacements, removed, statistics);
                    }
                }
            }
        }
        if (statistics->promoted_locals != promoted_before)
        {
            ir_promote_compact(arena, program, function, old_value_count, replacements, removed, statistics);
        }
        scratch_end(scratch);
    }
    statistics->instructions_after += function->instruction_count;
    statistics->values_after += function->value_count;
}

IrValidationResult ir_prepare_canonical_module(IrProgram* program, IrModule* module, bool assume_validated)
{
    IrValidationResult result = ir_validation_ok();
    if (!program || !program->arena || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
    }
    else
    {
        if (!assume_validated)
        {
            result = ir_validate_canonical_module(program, module);
        }
        if (result.error == IR_VALIDATION_NONE && !program->disable_local_promotion && !module->local_promotion_complete)
        {
            module->local_promotion = (IrLocalPromotionStatistics){0};
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    ir_promote_function(program, function, &module->local_promotion);
                }
            }
            if (module->local_promotion.promoted_locals)
            {
                // A certified producer has already established its canonical
                // contract. Its fast path intentionally skips the general
                // validator both before and after preparation; the structural
                // tests and every non-certified producer still validate the
                // transformed module here.
                if (!assume_validated)
                {
                    result = ir_validate_canonical_module(program, module);
                }
            }
            module->local_promotion_complete = result.error == IR_VALIDATION_NONE;
        }
    }
    return result;
}
