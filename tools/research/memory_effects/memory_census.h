// Isolated read-only canonical memory census. Included after driver headers.
// Model A keys by exact PLACE ID and flushes on every store; Model B keys
// by fixed private LOCAL plus constant byte interval and retains disjoint
// entries. Both are block-local, have sixteen slots, and reset on unknown
// memory/effects. Counts are independent structural candidate populations,
// not executed transformations, accepted performance, or additive removals.
#include <stdio.h>

#define RESEARCH_MEMORY_NONE UINT32_MAX
#define RESEARCH_MEMORY_SLOTS 16

typedef struct ResearchMemoryFact ResearchMemoryFact;
struct ResearchMemoryFact
{
    u64 offset;
    u32 root;
    u8 state; // 0 unresolved, 1 known, 2 unknown
    u8 reserved[3];
};

typedef struct ResearchMemoryPath ResearchMemoryPath;
struct ResearchMemoryPath
{
    u64 delta;
    u32 value;
    u32 reserved;
};

typedef struct ResearchMemorySlot ResearchMemorySlot;
struct ResearchMemorySlot
{
    u64 offset;
    u64 size;
    u32 root;
    u32 place;
    u32 type;
    u8 source; // 1 load, 2 store
    bool pending_store;
    u8 reserved[2];
};

typedef struct ResearchMemoryCounts ResearchMemoryCounts;
struct ResearchMemoryCounts
{
    u64 rows;
    u64 operands;
    u64 values;
    u64 address_steps;
    u64 loads;
    u64 stores;
    u64 eligible_loads;
    u64 eligible_stores;
    u64 unknown_accesses;
    u64 hard_barriers;
    u64 comparisons;
    u64 invalidations;
    u64 flush_slots;
    u64 evictions;
    u64 redundant_loads;
    u64 store_forward_loads;
    u64 overwritten_stores;
    u64 cfg_edges;
};

BUSTER_GLOBAL_LOCAL ResearchMemoryFact research_memory_resolve(IrProgram* program, IrFunction* function,
    ResearchMemoryFact* facts, ResearchMemoryPath* path, u32 value, ResearchMemoryCounts* counts)
{
    u32 depth = 0;
    u32 current = value;
    ResearchMemoryFact result = {.root = RESEARCH_MEMORY_NONE, .state = 2};
    bool walking = true;
    while (walking && current < function->value_count && depth < function->value_count)
    {
        counts->address_steps += 1;
        if (facts[current].state)
        {
            result = facts[current];
            walking = false;
        }
        else
        {
            IrValue* candidate = function->values + current;
            IrInstruction* row = candidate->definition.value < function->instruction_count
                ? function->instructions + candidate->definition.value : 0;
            u32 parent = RESEARCH_MEMORY_NONE;
            u64 delta = 0;
            bool valid = false;
            if (row && row->opcode == IR_OPCODE_LOCAL && candidate->category == IR_VALUE_PLACE && !row->operand_count)
            {
                IrType* type = ir_type_from_id(&program->types, candidate->canonical_type);
                if (type && type->layout.resolved && type->layout.size && !type->is_volatile && !type->is_atomic &&
                    !candidate->is_volatile && !row->volatile_access)
                {
                    result = (ResearchMemoryFact){.root = current, .state = 1};
                    facts[current] = result;
                }
                else
                {
                    facts[current] = result;
                }
                walking = false;
            }
            else
            {
                if (row && row->operands && row->operand_count == 1 &&
                    (row->opcode == IR_OPCODE_ADDRESS_OF || row->opcode == IR_OPCODE_DEREFERENCE))
                {
                    parent = row->operands[0].value;
                    valid = true;
                }
                else if (row && row->opcode == IR_OPCODE_FIELD && candidate->category == IR_VALUE_PLACE &&
                    row->operands && row->operand_count == 1 && row->immediates && row->immediate_count == 1)
                {
                    parent = row->operands[0].value;
                    IrType* owner = parent < function->value_count
                        ? ir_type_from_id(&program->types, function->values[parent].canonical_type) : 0;
                    u64 field = row->immediates[0];
                    valid = owner && field < owner->field_count && !owner->fields[field].is_bit_field;
                    if (valid) delta = owner->fields[field].offset;
                }
                else if (row && row->opcode == IR_OPCODE_INDEX && candidate->category == IR_VALUE_PLACE &&
                    row->operands && row->operand_count == 2)
                {
                    parent = row->operands[0].value;
                    u32 index = row->operands[1].value;
                    IrType* owner = parent < function->value_count
                        ? ir_type_from_id(&program->types, function->values[parent].canonical_type) : 0;
                    IrType* element = owner ? ir_type_from_id(&program->types, owner->element_type) : 0;
                    IrInstruction* constant = index < function->value_count &&
                        function->values[index].definition.value < function->instruction_count
                        ? function->instructions + function->values[index].definition.value : 0;
                    valid = owner && element && element->layout.resolved && element->layout.size && constant &&
                        constant->opcode == IR_OPCODE_CONSTANT_INTEGER && !constant->immediate_is_negative &&
                        constant->immediate_count == 1 && constant->immediates &&
                        constant->immediates[0] <= UINT64_MAX / element->layout.size;
                    if (valid) delta = constant->immediates[0] * element->layout.size;
                }
                if (valid && parent < function->value_count)
                {
                    path[depth++] = (ResearchMemoryPath){.value = current, .delta = delta};
                    current = parent;
                }
                else
                {
                    facts[current] = result;
                    walking = false;
                }
            }
        }
    }
    if (walking) result = (ResearchMemoryFact){.root = RESEARCH_MEMORY_NONE, .state = 2};
    while (depth)
    {
        ResearchMemoryPath step = path[--depth];
        if (result.state == 1 && result.offset <= UINT64_MAX - step.delta)
        {
            result.offset += step.delta;
        }
        else
        {
            result = (ResearchMemoryFact){.root = RESEARCH_MEMORY_NONE, .state = 2};
        }
        facts[step.value] = result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool research_memory_access(IrProgram* program, IrFunction* function,
    ResearchMemoryFact* facts, u8* escaped, IrInstruction const* row, ResearchMemorySlot* access)
{
    bool valid = row->operands && ((row->opcode == IR_OPCODE_LOAD && row->operand_count == 1) ||
        (row->opcode == IR_OPCODE_STORE && row->operand_count == 2));
    u32 place = valid ? row->operands[0].value : RESEARCH_MEMORY_NONE;
    valid = valid && place < function->value_count && (row->opcode == IR_OPCODE_LOAD ||
        (row->operands[1].value < function->value_count &&
         function->values[row->operands[1].value].category == IR_VALUE_VALUE));
    if (valid)
    {
        IrValue* value = function->values + place;
        ResearchMemoryFact fact = facts[place];
        IrTypeId type_id = row->opcode == IR_OPCODE_LOAD ? row->canonical_type :
            function->values[row->operands[1].value].canonical_type;
        IrType* type = ir_type_from_id(&program->types, type_id);
        valid = value->category == IR_VALUE_PLACE && fact.state == 1 && fact.root < function->value_count &&
            !escaped[fact.root] && type && type->kind == IR_TYPE_INTEGER &&
            (type->bit_width == 32 || type->bit_width == 64) && type->layout.resolved &&
            type->layout.size == type->bit_width / 8 && !type->is_volatile && !type->is_atomic &&
            !value->is_volatile && !row->volatile_access && value->canonical_type.value == type_id.value;
        if (valid)
        {
            IrValue* root = function->values + fact.root;
            IrType* allocation = ir_type_from_id(&program->types, root->canonical_type);
            u32 alignment = root->alignment ? root->alignment : allocation->layout.alignment;
            valid = type->layout.alignment && alignment >= type->layout.alignment &&
                fact.offset % type->layout.alignment == 0 && fact.offset <= allocation->layout.size &&
                type->layout.size <= allocation->layout.size - fact.offset;
            if (valid)
            {
                *access = (ResearchMemorySlot){.root = fact.root, .offset = fact.offset,
                    .size = type->layout.size, .place = place, .type = type_id.value,
                    .source = row->opcode == IR_OPCODE_LOAD ? 1 : 2,
                    .pending_store = row->opcode == IR_OPCODE_STORE};
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL void research_memory_flush(ResearchMemorySlot* slots, u32* count, ResearchMemoryCounts* counts)
{
    counts->flush_slots += *count;
    counts->invalidations += *count;
    *count = 0;
    (void)slots;
}

BUSTER_GLOBAL_LOCAL void research_memory_model(IrProgram* program, IrFunction* function,
    ResearchMemoryFact* facts, u8* escaped, u8 const* reachable, bool precise, ResearchMemoryCounts* counts)
{
    ResearchMemorySlot slots[RESEARCH_MEMORY_SLOTS];
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        if (!reachable[block]) continue;
        u32 count = 0;
        IrBlock* owner = function->blocks + block;
        for (u32 index = owner->first_instruction.value; index < function->instruction_count;
            index = ir_block_next_instruction(function, owner, (IrInstructionId){.value = index}).value)
        {
            IrInstruction* row = function->instructions + index;
            counts->rows += 1;
            counts->operands += row->operand_count;
            bool load = row->opcode == IR_OPCODE_LOAD;
            bool store = row->opcode == IR_OPCODE_STORE;
            if (load || store)
            {
                counts->loads += load;
                counts->stores += store;
                ResearchMemorySlot access = {0};
                bool eligible = research_memory_access(program, function, facts, escaped, row, &access);
                counts->eligible_loads += load && eligible;
                counts->eligible_stores += store && eligible;
                if (eligible)
                {
                    u32 exact = RESEARCH_MEMORY_NONE;
                    for (u32 slot = 0; slot < count; slot += 1)
                    {
                        counts->comparisons += 1;
                        bool same = precise ? slots[slot].root == access.root && slots[slot].offset == access.offset &&
                            slots[slot].size == access.size : slots[slot].place == access.place;
                        if (same && slots[slot].type == access.type) exact = slot;
                    }
                    if (exact != RESEARCH_MEMORY_NONE)
                    {
                        counts->redundant_loads += load && slots[exact].source == 1;
                        counts->store_forward_loads += load && slots[exact].source == 2;
                        counts->overwritten_stores += store && slots[exact].pending_store;
                    }
                    if (store && !precise)
                    {
                        research_memory_flush(slots, &count, counts);
                        exact = RESEARCH_MEMORY_NONE;
                    }
                    else
                    {
                        u32 write = 0;
                        for (u32 slot = 0; slot < count; slot += 1)
                        {
                            counts->comparisons += 1;
                            bool overlaps = precise ? slots[slot].root == access.root &&
                                slots[slot].offset < access.offset + access.size &&
                                access.offset < slots[slot].offset + slots[slot].size : slots[slot].place == access.place;
                            if (store && overlaps)
                            {
                                counts->invalidations += 1;
                            }
                            else
                            {
                                // Model A has no proof that different place
                                // IDs are disjoint. Every read observes any
                                // potentially aliasing pending store.
                                if (load && (!precise || overlaps)) slots[slot].pending_store = false;
                                if (slot == exact) exact = write;
                                slots[write++] = slots[slot];
                            }
                        }
                        count = write;
                        if (store) exact = RESEARCH_MEMORY_NONE;
                    }
                    if (exact != RESEARCH_MEMORY_NONE)
                    {
                        slots[exact] = access;
                    }
                    else
                    {
                        if (count == RESEARCH_MEMORY_SLOTS)
                        {
                            counts->evictions += 1;
                            for (u32 slot = 1; slot < count; slot += 1) slots[slot - 1] = slots[slot];
                            count -= 1;
                        }
                        slots[count++] = access;
                    }
                }
                else
                {
                    counts->unknown_accesses += 1;
                    counts->hard_barriers += 1;
                    research_memory_flush(slots, &count, counts);
                }
            }
            else
            {
                bool harmless = row->opcode == IR_OPCODE_ARGUMENT || row->opcode == IR_OPCODE_LOCAL ||
                    row->opcode == IR_OPCODE_CONSTANT_FLOAT || ir_instruction_is_pure(program, function, row);
                if (!harmless)
                {
                    counts->hard_barriers += 1;
                    research_memory_flush(slots, &count, counts);
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void buster_research_memory_census(IrProgram* program, IrModule* module)
{
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        if (function->state == IR_FUNCTION_LOWERED)
        {
            TemporalArena temporary = scratch_begin(&program->arena, 1);
            Arena* arena = temporary.arena;
            ResearchMemoryFact* facts = arena_allocate_zeroed(arena, ResearchMemoryFact, function->value_count);
            ResearchMemoryPath* path = arena_allocate(arena, ResearchMemoryPath, function->value_count);
            u8* escaped = arena_allocate_zeroed(arena, u8, function->value_count);
            u8* reachable = arena_allocate_zeroed(arena, u8, function->block_count);
            u32* queue = arena_allocate(arena, u32, function->block_count);
            ResearchMemoryCounts base = {.values = function->value_count, .rows = function->instruction_count};
            for (u32 value = 0; value < function->value_count; value += 1)
            {
                (void)research_memory_resolve(program, function, facts, path, value, &base);
            }
            for (u32 index = 0; index < function->instruction_count; index += 1)
            {
                IrInstruction* row = function->instructions + index;
                base.operands += row->operand_count;
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    ResearchMemoryFact fact = facts[row->operands[operand].value];
                    if (fact.state == 1)
                    {
                        bool allowed = operand == 0 && (row->opcode == IR_OPCODE_LOAD || row->opcode == IR_OPCODE_STORE ||
                            ((row->opcode == IR_OPCODE_FIELD || row->opcode == IR_OPCODE_INDEX ||
                              row->opcode == IR_OPCODE_ADDRESS_OF || row->opcode == IR_OPCODE_DEREFERENCE) &&
                              row->result.value < function->value_count && facts[row->result.value].state == 1));
                        if (!allowed || row->volatile_access) escaped[fact.root] = 1;
                    }
                }
            }
            if (function->published_cfg)
            {
                IrPublishedCfg const* cfg = function->published_cfg;
                for (u32 index = 0; index < cfg->argument_count; index += 1)
                {
                    ResearchMemoryFact fact = facts[cfg->arguments[index].value];
                    if (fact.state == 1) escaped[fact.root] = 1;
                    base.operands += 1;
                }
            }
            else
            {
                for (u32 block = 0; block < function->block_count; block += 1)
                {
                    for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
                    {
                        for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
                        {
                            ResearchMemoryFact fact = facts[incoming->value.value];
                            if (fact.state == 1) escaped[fact.root] = 1;
                            base.operands += 1;
                        }
                    }
                }
            }
            u32 read = 0;
            u32 write = 0;
            if (function->entry.value < function->block_count)
            {
                queue[write++] = function->entry.value;
                reachable[function->entry.value] = 1;
            }
            while (read < write)
            {
                u32 block = queue[read++];
                IrInstruction* terminator = function->instructions + function->blocks[block].last_instruction.value;
                for (u32 target = 0; target < terminator->target_count; target += 1)
                {
                    u32 destination = terminator->targets[target].value;
                    base.cfg_edges += 1;
                    if (destination < function->block_count && !reachable[destination])
                    {
                        reachable[destination] = 1;
                        queue[write++] = destination;
                    }
                }
            }
            for (u32 model = 0; model < 2; model += 1)
            {
                ResearchMemoryCounts counts = base;
                research_memory_model(program, function, facts, escaped, reachable, model != 0, &counts);
                fprintf(stderr, "MEMORY_CENSUS,%.*s,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                    (int)function->name.length, (char const*)function->name.pointer, model,
                    (unsigned long long)counts.rows, (unsigned long long)counts.operands,
                    (unsigned long long)counts.values, (unsigned long long)counts.address_steps,
                    (unsigned long long)counts.loads, (unsigned long long)counts.stores,
                    (unsigned long long)counts.eligible_loads, (unsigned long long)counts.eligible_stores,
                    (unsigned long long)counts.unknown_accesses, (unsigned long long)counts.hard_barriers,
                    (unsigned long long)counts.comparisons, (unsigned long long)counts.invalidations,
                    (unsigned long long)counts.flush_slots, (unsigned long long)counts.evictions,
                    (unsigned long long)counts.redundant_loads, (unsigned long long)counts.store_forward_loads,
                    (unsigned long long)counts.overwritten_stores,
                    (unsigned long long)(33 * (u64)function->value_count + 5 * (u64)function->block_count + sizeof(ResearchMemorySlot) * RESEARCH_MEMORY_SLOTS),
                    (unsigned long long)counts.cfg_edges);
            }
            scratch_end(temporary);
        }
    }
}
