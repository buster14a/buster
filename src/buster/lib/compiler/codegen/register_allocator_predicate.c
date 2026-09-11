// Separate x86 predicate placement. Included by machine.c after FAST/QUALITY.
// machine_predicate_placement_build partitions only functions containing MASK
// values; the existing 48-register allocator sees their other operands unchanged.
// Predicate values use k1-k7, eight-byte homes, caller-clobbered block-local
// residency and parallel edge captures. No predicate ID enters a GPR/ZMM mask.

typedef struct MachinePredicateState MachinePredicateState;
struct MachinePredicateState
{
    Arena* arena;
    MachineFunction* function;
    MachineStackPlacement* placement;
    MachineBuilderStream edits;
    u32 owner[MACHINE_PREDICATE_REGISTER_COUNT];
    u32 age[MACHINE_PREDICATE_REGISTER_COUNT];
    u32* locations;
    u32* last_use;
    u32* rematerialize_immediates;
    u8* escapes;
    u8 held;
    u8 dirty;
    u32 clock;
};

BUSTER_GLOBAL_LOCAL bool machine_predicate_ref(MachineFunction* function, MachineRef ref)
{
    bool result = false;
    if (machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER)
    {
        result = function->virtual_registers[machine_ref_payload(ref)].register_class == MACHINE_REGISTER_CLASS_MASK;
    }
    else if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
    {
        result = machine_ref_payload(ref) >= MACHINE_PREDICATE_REGISTER_BASE;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void machine_predicate_edit(MachinePredicateState* state, MachinePoint point, u32 kind, u32 subject, u32 reg)
{
    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, &state->edits);
    *edit = (MachineEdit){.point = point, .kind = (u16)kind, .subject = subject, .location = (u16)(MACHINE_PREDICATE_REGISTER_BASE + reg)};
    state->placement->reload_count += kind == MACHINE_EDIT_RELOAD;
    state->placement->spill_count += kind == MACHINE_EDIT_SPILL;
    state->placement->copy_count += kind == MACHINE_EDIT_COPY;
    state->placement->rematerialize_count += kind == MACHINE_EDIT_REMATERIALIZE;
}

BUSTER_GLOBAL_LOCAL void machine_predicate_release(MachinePredicateState* state, MachinePoint point, u32 reg)
{
    u8 lane = (u8)(1u << reg);
    if (state->held & lane)
    {
        u32 value = state->owner[reg];
        if ((state->dirty & lane) && state->rematerialize_immediates[value] == UINT32_MAX &&
            (state->escapes[value] || state->last_use[value] >= (point >> 2)))
        {
            machine_predicate_edit(state, point, MACHINE_EDIT_SPILL, value, reg);
        }
        state->locations[value] = UINT32_MAX;
        state->held &= (u8)~lane;
        state->dirty &= (u8)~lane;
    }
}

BUSTER_GLOBAL_LOCAL u32 machine_predicate_pick(MachinePredicateState* state, MachinePoint point, u8 reserved)
{
    u8 candidates = (u8)(MACHINE_PREDICATE_ALLOCATABLE_MASK & ~(u32)reserved);
    u8 free_mask = (u8)(candidates & ~state->held);
    u32 selected = UINT32_MAX;
    for (u32 reg = 1; reg < MACHINE_PREDICATE_REGISTER_COUNT; reg += 1)
    {
        if ((free_mask ? free_mask : candidates) & (1u << reg))
        {
            if (selected == UINT32_MAX || (!free_mask && state->age[reg] < state->age[selected])) selected = reg;
        }
    }
    if (selected != UINT32_MAX) machine_predicate_release(state, point, selected);
    return selected;
}

BUSTER_GLOBAL_LOCAL MachineStackPlacement machine_predicate_placement_build(Arena* arena, MachineFunction* function, u32 mode)
{
    bool inspect = function->target && function->target->predicate_allocatable_mask && !function->predicate_absence_certified;
    u32 predicate_count = 0;
    for (u32 value = 0; inspect && value < function->virtual_register_count; value += 1)
    {
        predicate_count += function->virtual_registers[value].register_class == MACHINE_REGISTER_CLASS_MASK;
    }
    bool has_predicates = predicate_count != 0;
    for (u32 row = 0; inspect && !has_predicates && row < function->instruction_count; row += 1)
    {
        for (u32 slot = 0; slot < MACHINE_INSTRUCTION_OPERAND_COUNT; slot += 1)
            has_predicates |= machine_predicate_ref(function, function->instructions[row].operands[slot]);
    }
    MachineFunction projected = *function;
    u32* homes = 0;
    u32 temporary_slot = UINT32_MAX;
    if (has_predicates)
    {
        homes = arena_allocate(arena, u32, function->virtual_register_count);
        projected.instructions = arena_allocate(arena, MachineInstruction, function->instruction_count);
        memcpy(projected.instructions, function->instructions, (u64)function->instruction_count * sizeof(MachineInstruction));
        for (u32 row = 0; row < function->instruction_count; row += 1)
        {
            for (u32 slot = 0; slot < MACHINE_INSTRUCTION_OPERAND_COUNT; slot += 1)
            {
                if (machine_predicate_ref(function, projected.instructions[row].operands[slot])) projected.instructions[row].operands[slot] = 0;
            }
        }
        projected.blocks = arena_allocate(arena, MachineBlock, function->block_count);
        memcpy(projected.blocks, function->blocks, (u64)function->block_count * sizeof(MachineBlock));
        projected.block_parameters = arena_allocate(arena, MachineBlockParameter, function->block_parameter_count);
        projected.block_parameter_count = 0;
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            MachineBlock const* source = function->blocks + block;
            MachineBlock* destination = projected.blocks + block;
            destination->parameter_offset = projected.block_parameter_count;
            destination->parameter_count = 0;
            for (u32 index = 0; index < source->parameter_count; index += 1)
            {
                MachineBlockParameter parameter = function->block_parameters[source->parameter_offset + index];
                if (function->virtual_registers[parameter.virtual_register].register_class != MACHINE_REGISTER_CLASS_MASK)
                {
                    projected.block_parameters[projected.block_parameter_count++] = parameter;
                    destination->parameter_count += 1;
                }
            }
        }
        projected.edges = arena_allocate(arena, MachineEdge, function->edge_count);
        projected.edge_copy_sources = arena_allocate(arena, MachineRef, function->edge_copy_source_count);
        projected.edge_copy_source_count = 0;
        u32 maximum_copies = 0;
        u32* block_copies = arena_allocate_zeroed(arena, u32, function->block_count);
        for (u32 index = 0; index < function->edge_count; index += 1)
        {
            MachineEdge edge = function->edges[index];
            MachineBlock const* block = function->blocks + edge.destination_block;
            u32 predicate_copies = 0;
            projected.edges[index] = edge;
            projected.edges[index].copy_offset = projected.edge_copy_source_count;
            projected.edges[index].copy_count = 0;
            for (u32 copy = 0; copy < edge.copy_count; copy += 1)
            {
                u32 destination = function->block_parameters[block->parameter_offset + copy].virtual_register;
                if (function->virtual_registers[destination].register_class == MACHINE_REGISTER_CLASS_MASK) predicate_copies += 1;
                else
                {
                    projected.edge_copy_sources[projected.edge_copy_source_count++] = function->edge_copy_sources[edge.copy_offset + copy];
                    projected.edges[index].copy_count += 1;
                }
            }
            block_copies[edge.source_block] += predicate_copies;
            maximum_copies = BUSTER_MAX(maximum_copies, block_copies[edge.source_block]);
        }
        projected.stack_slot_count += predicate_count + (maximum_copies != 0);
        projected.stack_slot_sizes = arena_allocate(arena, u32, projected.stack_slot_count);
        projected.stack_slot_alignments = arena_allocate(arena, u32, projected.stack_slot_count);
        for (u32 slot = 0; slot < function->stack_slot_count; slot += 1)
        {
            projected.stack_slot_sizes[slot] = function->stack_slot_sizes[slot];
            projected.stack_slot_alignments[slot] = function->stack_slot_alignments ? function->stack_slot_alignments[slot] : 8;
        }
        u32 next_slot = function->stack_slot_count;
        for (u32 value = 0; value < function->virtual_register_count; value += 1)
        {
            if (function->virtual_registers[value].register_class == MACHINE_REGISTER_CLASS_MASK)
            {
                homes[value] = next_slot;
                projected.stack_slot_sizes[next_slot] = 8;
                projected.stack_slot_alignments[next_slot++] = 8;
            }
        }
        if (maximum_copies)
        {
            temporary_slot = next_slot;
            projected.stack_slot_sizes[next_slot] = maximum_copies * 8u;
            projected.stack_slot_alignments[next_slot] = 8;
        }
    }
    MachineStackPlacement placement;
    switch (mode)
    {
        case 1: placement = machine_fast_placement_build_core(arena, &projected); break;
        case 2: placement = machine_quality_placement_build_core(arena, &projected); break;
        default: placement = machine_stack_placement_build_core(arena, &projected); break;
    }
    if (has_predicates && placement.valid)
    {
        MachinePredicateState state = {.arena = arena, .function = function, .placement = &placement,
            .locations = arena_allocate(arena, u32, function->virtual_register_count),
            .last_use = arena_allocate_zeroed(arena, u32, function->virtual_register_count),
            .rematerialize_immediates = arena_allocate(arena, u32, function->virtual_register_count),
            .escapes = arena_allocate_zeroed(arena, u8, function->virtual_register_count)};
        memset(state.rematerialize_immediates, 0xff, (u64)function->virtual_register_count * sizeof(u32));
        for (u32 row = 0; row < function->instruction_count; row += 1)
        {
            MachineInstruction instruction = function->instructions[row];
            if (instruction.opcode == MACHINE_X64_KMOV_FROM_GENERAL &&
                machine_ref_kind(instruction.operands[0]) == MACHINE_REF_VIRTUAL_REGISTER &&
                machine_ref_kind(instruction.operands[1]) == MACHINE_REF_VIRTUAL_REGISTER)
            {
                u32 value = machine_ref_payload(instruction.operands[0]);
                u32 source = machine_ref_payload(instruction.operands[1]);
                MachineVirtualRegister source_value = function->virtual_registers[source];
                if (!((source_value.flags | function->virtual_registers[value].flags) & MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE) && source_value.definition_point != MACHINE_POINT_INVALID)
                {
                    MachineInstruction definition = function->instructions[machine_point_instruction(source_value.definition_point)];
                    if (definition.opcode == MACHINE_X64_MOV_RI && machine_ref_kind(definition.operands[1]) == MACHINE_REF_IMMEDIATE)
                    {
                        u32 immediate = machine_ref_payload(definition.operands[1]);
                        u64 bits = function->immediates[immediate];
                        if (bits == 0 || (bits == UINT64_MAX && instruction.payload == 64)) state.rematerialize_immediates[value] = immediate;
                    }
                }
            }
        }
        machine_stream_initialize(&state.edits, sizeof(MachineEdit));
        memset(state.locations, 0xff, (u64)function->virtual_register_count * sizeof(u32));
        u32* use_blocks = arena_allocate(arena, u32, function->virtual_register_count);
        memset(use_blocks, 0xff, (u64)function->virtual_register_count * sizeof(u32));
        for (u32 value = 0; value < function->virtual_register_count; value += 1)
        {
            if (function->virtual_registers[value].register_class == MACHINE_REGISTER_CLASS_MASK)
            {
                placement.virtual_register_offsets[value] = placement.stack_slot_offsets[homes[value]];
            }
        }
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            MachineBlock const* info = function->blocks + block;
            for (u32 row = info->first_instruction; row < info->first_instruction + info->instruction_count; row += 1)
            {
                for (u32 slot = 0; slot < MACHINE_INSTRUCTION_OPERAND_COUNT; slot += 1)
                {
                    MachineRef ref = function->instructions[row].operands[slot];
                    if (machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && machine_predicate_ref(function, ref))
                    {
                        u32 value = machine_ref_payload(ref);
                        state.escapes[value] |= use_blocks[value] != UINT32_MAX && use_blocks[value] != block;
                        use_blocks[value] = block;
                        state.last_use[value] = row;
                    }
                }
            }
        }
        for (u32 copy = 0; copy < function->edge_copy_source_count; copy += 1)
        {
            MachineRef ref = function->edge_copy_sources[copy];
            if (machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && machine_predicate_ref(function, ref)) state.escapes[machine_ref_payload(ref)] = 1;
        }
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            MachineBlock const* block_info = function->blocks + block;
            for (u32 row = block_info->first_instruction; row < block_info->first_instruction + block_info->instruction_count; row += 1)
            {
                MachineInstruction const* instruction = function->instructions + row;
                MachineOpcodeInfo const* opcode = machine_opcode_info(instruction->opcode);
                MachinePoint before = machine_point_make(row, MACHINE_POINT_BEFORE);
                bool legacy_mask_scratch = instruction->opcode == MACHINE_X64_VPCMP_MASK || instruction->opcode == MACHINE_X64_VPMOVB2M ||
                    instruction->opcode == MACHINE_X64_VLOAD_PTR_MASKED || instruction->opcode == MACHINE_X64_VSTORE_PTR_MASKED ||
                    instruction->opcode == MACHINE_X64_VCOMPRESS_STORE_PTR || instruction->opcode == MACHINE_X64_VPERMT2B || instruction->opcode == MACHINE_X64_VCOMPRESSB;
                bool boundary = legacy_mask_scratch || (opcode->attributes & (MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_TERMINATOR)) != 0;
                if (boundary)
                {
                    for (u32 reg = 1; reg < MACHINE_PREDICATE_REGISTER_COUNT; reg += 1) machine_predicate_release(&state, before, reg);
                }
                u8 reserved = 0;
                for (u32 slot = 0; slot < opcode->operand_count; slot += 1)
                {
                    MachineRef ref = instruction->operands[slot];
                    if (machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER && machine_predicate_ref(function, ref))
                    {
                        u32 reg = state.locations[machine_ref_payload(ref)];
                        if (reg != UINT32_MAX) reserved |= (u8)(1u << reg);
                    }
                    else if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER && machine_predicate_ref(function, ref))
                    {
                        reserved |= (u8)(1u << (machine_ref_payload(ref) - MACHINE_PREDICATE_REGISTER_BASE));
                    }
                }
                for (u32 pass = 0; pass < 2; pass += 1)
                {
                    for (u32 slot = 0; slot < opcode->operand_count; slot += 1)
                    {
                        MachineRef ref = instruction->operands[slot];
                        u32 role = opcode->operand_info[slot] & 3u;
                        bool use = (role & MACHINE_OPERAND_ROLE_USE) != 0;
                        if (!machine_predicate_ref(function, ref) || use != (pass == 0)) continue;
                        u32 reg;
                        if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
                        {
                            reg = machine_ref_payload(ref) - MACHINE_PREDICATE_REGISTER_BASE;
                            machine_predicate_release(&state, before, reg);
                        }
                        else
                        {
                            u32 value = machine_ref_payload(ref);
                            reg = state.locations[value];
                            if (reg == UINT32_MAX)
                            {
                                reg = machine_predicate_pick(&state, before, reserved);
                                BUSTER_CHECK(reg != UINT32_MAX);
                                state.owner[reg] = value;
                                state.locations[value] = reg;
                                state.held |= (u8)(1u << reg);
                                if (use)
                                {
                                    u32 immediate = state.rematerialize_immediates[value];
                                    machine_predicate_edit(&state, before, immediate == UINT32_MAX ? MACHINE_EDIT_RELOAD : MACHINE_EDIT_REMATERIALIZE,
                                        immediate == UINT32_MAX ? value : immediate, reg);
                                }
                            }
                            state.age[reg] = ++state.clock;
                            if (role & MACHINE_OPERAND_ROLE_DEFINE) state.dirty |= (u8)(1u << reg);
                        }
                        reserved |= (u8)(1u << reg);
                        placement.operand_registers[(u64)row * 4 + slot] = (u8)(MACHINE_PREDICATE_REGISTER_BASE + reg);
                    }
                }
                if (mode == 0 && !boundary)
                {
                    MachinePoint after = machine_point_make(row, MACHINE_POINT_AFTER);
                    for (u32 reg = 1; reg < MACHINE_PREDICATE_REGISTER_COUNT; reg += 1) machine_predicate_release(&state, after, reg);
                }
                if (row + 1 == block_info->first_instruction + block_info->instruction_count)
                {
                    MachinePoint end = (opcode->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) ? before : machine_point_make(row, MACHINE_POINT_AFTER);
                    for (u32 reg = 1; reg < MACHINE_PREDICATE_REGISTER_COUNT; reg += 1) machine_predicate_release(&state, end, reg);
                    // Capture all successor sources before publishing any
                    // destination: one outgoing edge can overwrite another's
                    // source home. Fixed inputs precede reload scratch writes.
                    for (u32 pass = 0; pass < 3; pass += 1)
                    {
                        u32 captured = 0;
                        for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
                        {
                            MachineEdge const* edge = function->edges + edge_index;
                            if (edge->source_block != block || !edge->copy_count) continue;
                            MachineBlock const* destination = function->blocks + edge->destination_block;
                            for (u32 copy = 0; copy < edge->copy_count; copy += 1)
                            {
                                u32 value = function->block_parameters[destination->parameter_offset + copy].virtual_register;
                                if (function->virtual_registers[value].register_class != MACHINE_REGISTER_CLASS_MASK) continue;
                                u32 frame = placement.stack_slot_offsets[temporary_slot] - captured++ * 8u;
                                MachineRef source = function->edge_copy_sources[edge->copy_offset + copy];
                                u32 reg = machine_ref_kind(source) == MACHINE_REF_PHYSICAL_REGISTER ? machine_ref_payload(source) - MACHINE_PREDICATE_REGISTER_BASE : 1;
                                bool physical = machine_ref_kind(source) == MACHINE_REF_PHYSICAL_REGISTER;
                                if (pass < 2 && physical != (pass == 0)) continue;
                                if (pass < 2)
                                {
                                    if (machine_ref_kind(source) == MACHINE_REF_VIRTUAL_REGISTER)
                                    {
                                        u32 source_value = machine_ref_payload(source);
                                        u32 immediate = state.rematerialize_immediates[source_value];
                                        machine_predicate_edit(&state, before, immediate == UINT32_MAX ? MACHINE_EDIT_RELOAD : MACHINE_EDIT_REMATERIALIZE,
                                            immediate == UINT32_MAX ? source_value : immediate, reg);
                                    }
                                    machine_predicate_edit(&state, before, MACHINE_EDIT_FRAME_SPILL, frame, reg);
                                }
                                else
                                {
                                    machine_predicate_edit(&state, before, MACHINE_EDIT_FRAME_RELOAD, frame, 1);
                                    machine_predicate_edit(&state, before, MACHINE_EDIT_SPILL, value, 1);
                                }
                            }
                        }
                    }
                }
            }
        }
        MachineEdit* predicates = arena_allocate(arena, MachineEdit, state.edits.total_count);
        machine_stream_flatten(&state.edits, predicates);
        // Reserve homes conservatively before GPR placement, then retain
        // only homes actually referenced by a predicate edit. The predicate
        // slots are one contiguous suffix of the selector's stack slots.
        u8* needed = arena_allocate_zeroed(arena, u8, function->virtual_register_count);
        for (u32 index = 0; index < state.edits.total_count; index += 1)
        {
            MachineEdit edit = predicates[index];
            if (edit.kind == MACHINE_EDIT_RELOAD || edit.kind == MACHINE_EDIT_SPILL) needed[edit.subject] = 1;
        }
        u32 removed = 0;
        for (u32 value = 0; value < function->virtual_register_count; value += 1)
        {
            if (function->virtual_registers[value].register_class == MACHINE_REGISTER_CLASS_MASK)
            {
                if (needed[value]) placement.virtual_register_offsets[value] -= removed;
                else { removed += 8; placement.virtual_register_offsets[value] = UINT32_MAX; }
            }
        }
        if (temporary_slot != UINT32_MAX) placement.stack_slot_offsets[temporary_slot] -= removed;
        placement.edge_copy_temporary_offset -= removed;
        placement.frame_size -= removed & ~15u;
        if (function->outgoing_bytes) placement.stack_slot_offsets[function->outgoing_slot] = placement.frame_size;
        for (u32 index = 0; index < state.edits.total_count; index += 1)
        {
            MachineEdit* edit = predicates + index;
            if (edit->kind == MACHINE_EDIT_FRAME_SPILL || edit->kind == MACHINE_EDIT_FRAME_RELOAD) edit->subject -= removed;
        }
        MachineEdit* merged = arena_allocate(arena, MachineEdit, placement.edit_count + state.edits.total_count);
        u32 generic_cursor = 0;
        u32 predicate_cursor = 0;
        u32 count = 0;
        while (generic_cursor < placement.edit_count || predicate_cursor < state.edits.total_count)
        {
            bool generic = generic_cursor < placement.edit_count && (predicate_cursor == state.edits.total_count || placement.edits[generic_cursor].point <= predicates[predicate_cursor].point);
            merged[count++] = generic ? placement.edits[generic_cursor++] : predicates[predicate_cursor++];
        }
        placement.edits = merged;
        placement.edit_count = count;
    }
    return placement;
}

MachineStackPlacement machine_stack_placement_build(Arena* arena, MachineFunction* function)
{
    return machine_predicate_placement_build(arena, function, 0);
}

MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function)
{
    return machine_predicate_placement_build(arena, function, 1);
}

MachineStackPlacement machine_quality_placement_build(Arena* arena, MachineFunction* function)
{
    return machine_predicate_placement_build(arena, function, 2);
}
