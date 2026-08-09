#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/os.h>

// FRA stage 4: local fast register allocation over the compact machine IR.
// Included by machine.c in the backend-implementation-file pattern. A
// forward scan keeps values in registers between their uses inside one
// block, spills lazily on eviction, calls, and block boundaries, and forces
// the fixed-register operand layout for the constrained opcodes whose
// encoder sequences pin specific registers. The output is the same
// placement contract the MIR_STACK builder produces, so the encoder is
// untouched: per-slot operand registers plus a point-sorted reload/spill
// edit stream.

// Registers the local allocator may own between instructions. RSP and RBP
// are reserved, RBX and R12-R15 wait for callee-save activation in the
// global stage, and every member here is caller-saved, so calls simply
// flush the file.
#define MACHINE_FAST_ALLOCATABLE_MASK                                                                                                                          \
    ((1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RCX) | (1u << MACHINE_X64_RDX) | (1u << MACHINE_X64_RSI) | (1u << MACHINE_X64_RDI) |                         \
     (1u << MACHINE_X64_R8) | (1u << MACHINE_X64_R9) | (1u << MACHINE_X64_R10) | (1u << MACHINE_X64_R11))

typedef struct MachineFastState MachineFastState;
struct MachineFastState
{
    Arena* arena;
    MachineFunction* function;
    MachineStackPlacement* placement;
    MachineBuilderStream* edits;
    // Register file: owning vreg per physical register, age for LRU
    // eviction, and whether the register is newer than the vreg's slot.
    u32 owner[MACHINE_X64_REGISTER_COUNT];
    u32 age[MACHINE_X64_REGISTER_COUNT];
    bool dirty[MACHINE_X64_REGISTER_COUNT];
    // Current register per vreg, or UINT32_MAX when the slot is the home.
    u32* virtual_register_locations;
    // Stage-5 liveness: the instruction index of each vreg's last textual
    // use, and whether any use sits outside the defining block. Escaping
    // values always reach their slots (loop-carried and layout-order
    // hazards live only across blocks); a non-escaping value strictly past
    // its last use is dead and its spill store is dropped.
    u32* last_use;
    u8* escapes;
    u32 clock;
    u32 current_point;
};

BUSTER_GLOBAL_LOCAL bool machine_fast_owner_is_dead(MachineFastState* state, u32 physical_register)
{
    u32 owner = state->owner[physical_register];
    if (owner == UINT32_MAX)
    {
        return false;
    }
    return !state->escapes[owner] && (state->current_point >> 2) > state->last_use[owner];
}

// The encoder sequences of these opcodes pin operand registers (divides,
// shift counts in CL, setcc through AL/DL, switch chains, atomic layouts,
// copy scratches), so their operands take the historical per-slot scratch
// assignment and everything else in the file must stand clear.
BUSTER_GLOBAL_LOCAL bool machine_fast_opcode_is_constrained(u16 opcode)
{
    switch (opcode)
    {
        break;
    case MACHINE_X64_SETCC:
    case MACHINE_X64_FCMP_SET:
    case MACHINE_X64_SHL32:
    case MACHINE_X64_SHL64:
    case MACHINE_X64_SAR32:
    case MACHINE_X64_SAR64:
    case MACHINE_X64_SHR32:
    case MACHINE_X64_SHR64:
    case MACHINE_X64_SDIV32:
    case MACHINE_X64_SDIV64:
    case MACHINE_X64_UDIV32:
    case MACHINE_X64_UDIV64:
    case MACHINE_X64_SREM32:
    case MACHINE_X64_SREM64:
    case MACHINE_X64_UREM32:
    case MACHINE_X64_UREM64:
    case MACHINE_X64_SWITCH:
    case MACHINE_X64_ATOMIC_STORE_XCHG:
    case MACHINE_X64_ATOMIC_RMW:
    case MACHINE_X64_ATOMIC_CMPXCHG:
    case MACHINE_X64_COPY_FRAME_FROM_PTR:
    case MACHINE_X64_COPY_PTR_FROM_FRAME:
        return true;
    default:
        return false;
    }
    return false;
}

// Extra registers an opcode's encoder sequence scribbles on beyond its
// declared operands; owners must vacate before the instruction runs.
BUSTER_GLOBAL_LOCAL u32 machine_fast_opcode_clobber_mask(u16 opcode)
{
    switch (opcode)
    {
        break;
    case MACHINE_X64_SDIV32:
    case MACHINE_X64_SDIV64:
    case MACHINE_X64_UDIV32:
    case MACHINE_X64_UDIV64:
    case MACHINE_X64_SREM32:
    case MACHINE_X64_SREM64:
    case MACHINE_X64_UREM32:
    case MACHINE_X64_UREM64:
        return 1u << MACHINE_X64_RDX;
    case MACHINE_X64_FCMP_SET:
        return 1u << MACHINE_X64_RDX;
    case MACHINE_X64_SWITCH:
        return 1u << MACHINE_X64_RCX;
    case MACHINE_X64_ATOMIC_RMW:
        return 1u << MACHINE_X64_R8;
    case MACHINE_X64_COPY_FRAME_FROM_FRAME:
        return 1u << MACHINE_X64_RAX;
    case MACHINE_X64_COPY_FRAME_FROM_PTR:
        return 1u << MACHINE_X64_RAX;
    case MACHINE_X64_COPY_PTR_FROM_FRAME:
        return (1u << MACHINE_X64_RAX) | (1u << MACHINE_X64_RDX);
    default:
        return 0;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void machine_fast_spill(MachineFastState* state, u32 physical_register)
{
    u32 owner = state->owner[physical_register];
    if (owner == UINT32_MAX)
    {
        return;
    }
    if (state->dirty[physical_register] && !machine_fast_owner_is_dead(state, physical_register))
    {
        MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_SPILL,
            .subject = owner,
            .location = physical_register,
        };
        state->placement->spill_count += 1;
    }
    state->owner[physical_register] = UINT32_MAX;
    state->dirty[physical_register] = false;
    state->virtual_register_locations[owner] = UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL void machine_fast_flush(MachineFastState* state)
{
    for (u32 physical_register = 0; physical_register < MACHINE_X64_REGISTER_COUNT; physical_register += 1)
    {
        machine_fast_spill(state, physical_register);
    }
}

// Picks a free allocatable register, else evicts the least recently used
// owner — a probe bounded by the register-file size.
BUSTER_GLOBAL_LOCAL u32 machine_fast_pick(MachineFastState* state, u32 forbidden_mask)
{
    u32 candidates = MACHINE_FAST_ALLOCATABLE_MASK & ~forbidden_mask;
    u32 best = UINT32_MAX;
    u32 best_age = UINT32_MAX;
    u32 dead = UINT32_MAX;
    for (u32 physical_register = 0; physical_register < MACHINE_X64_REGISTER_COUNT; physical_register += 1)
    {
        if (!(candidates & (1u << physical_register)))
        {
            continue;
        }
        if (state->owner[physical_register] == UINT32_MAX)
        {
            return physical_register;
        }
        // A dead owner costs nothing to displace: its spill is dropped.
        if (dead == UINT32_MAX && machine_fast_owner_is_dead(state, physical_register))
        {
            dead = physical_register;
        }
        if (state->age[physical_register] < best_age)
        {
            best_age = state->age[physical_register];
            best = physical_register;
        }
    }
    if (dead != UINT32_MAX)
    {
        best = dead;
    }
    machine_fast_spill(state, best);
    return best;
}

// Materializes `virtual_register` in `target` (UINT32_MAX picks freely) and
// returns the register used.
BUSTER_GLOBAL_LOCAL u32 machine_fast_ensure(MachineFastState* state, u32 virtual_register, u32 target, u32 forbidden_mask)
{
    u32 current = state->virtual_register_locations[virtual_register];
    if (target == UINT32_MAX)
    {
        if (current != UINT32_MAX)
        {
            state->age[current] = ++state->clock;
            return current;
        }
        target = machine_fast_pick(state, forbidden_mask);
    }
    if (current == target)
    {
        state->age[target] = ++state->clock;
        return target;
    }
    // The value moves through its slot: park it if it lives elsewhere,
    // spill the stale owner of the target, then reload.
    if (current != UINT32_MAX)
    {
        machine_fast_spill(state, current);
    }
    machine_fast_spill(state, target);
    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
    *edit = (MachineEdit){
        .point = state->current_point,
        .kind = MACHINE_EDIT_RELOAD,
        .subject = virtual_register,
        .location = target,
    };
    state->placement->reload_count += 1;
    state->owner[target] = virtual_register;
    state->dirty[target] = false;
    state->virtual_register_locations[virtual_register] = target;
    state->age[target] = ++state->clock;
    return target;
}

BUSTER_GLOBAL_LOCAL void machine_fast_bind(MachineFastState* state, u32 virtual_register, u32 target)
{
    machine_fast_spill(state, target);
    u32 current = state->virtual_register_locations[virtual_register];
    if (current != UINT32_MAX && current != target)
    {
        state->owner[current] = UINT32_MAX;
        state->dirty[current] = false;
    }
    state->owner[target] = virtual_register;
    state->dirty[target] = true;
    state->virtual_register_locations[virtual_register] = target;
    state->age[target] = ++state->clock;
}

MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function)
{
    // Frame layout is shared with the stack placement for now; slot
    // elimination is the stage-6 optimization.
    MachineStackPlacement placement = {
        .virtual_register_offsets = arena_allocate(arena, u32, function->virtual_register_count),
        .stack_slot_offsets = arena_allocate(arena, u32, function->stack_slot_count),
        .operand_registers = arena_allocate(arena, u8, (u64)function->instruction_count * 4),
    };
    MachineBuilderStream edits;
    machine_stream_initialize(&edits, sizeof(MachineEdit));
    MachineFastState state = {
        .arena = arena,
        .function = function,
        .placement = &placement,
        .edits = &edits,
        .virtual_register_locations = arena_allocate(arena, u32, function->virtual_register_count),
        .last_use = arena_allocate(arena, u32, function->virtual_register_count),
        .escapes = arena_allocate(arena, u8, function->virtual_register_count),
    };
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        state.virtual_register_locations[register_index] = UINT32_MAX;
        state.last_use[register_index] = 0;
        state.escapes[register_index] = 0;
    }
    // Liveness pre-passes: defining blocks first (a def dominates its uses,
    // but layout order does not prove it precedes them), then last textual
    // uses and block escapes.
    u32* definition_blocks = arena_allocate(arena, u32, function->virtual_register_count);
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        definition_blocks[register_index] = UINT32_MAX;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            MachineInstruction* instruction = function->instructions + instruction_index;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (!info)
            {
                return placement;
            }
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                u32 virtual_register = machine_ref_payload(ref);
                if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                {
                    if (definition_blocks[virtual_register] == UINT32_MAX)
                    {
                        definition_blocks[virtual_register] = block_index;
                    }
                }
                if (role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                {
                    state.last_use[virtual_register] = BUSTER_MAX(state.last_use[virtual_register], instruction_index);
                }
            }
        }
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            MachineInstruction* instruction = function->instructions + instruction_index;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                u32 virtual_register = machine_ref_payload(ref);
                if ((role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) &&
                    definition_blocks[virtual_register] != block_index)
                {
                    state.escapes[virtual_register] = 1;
                }
            }
        }
    }
    for (u32 physical_register = 0; physical_register < MACHINE_X64_REGISTER_COUNT; physical_register += 1)
    {
        state.owner[physical_register] = UINT32_MAX;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            u32 instruction_index = block->first_instruction + offset;
            MachineInstruction* instruction = function->instructions + instruction_index;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (!info)
            {
                return placement;
            }
            state.current_point = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
            u8* operand_registers = placement.operand_registers + (u64)instruction_index * 4;
            bool constrained = machine_fast_opcode_is_constrained(instruction->opcode);
            bool is_call = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL) != 0;
            bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
            // Fixed physical operands and the constrained layout vacate
            // their registers first so uses cannot land on them.
            u32 reserved_mask = machine_fast_opcode_clobber_mask(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    reserved_mask |= 1u << machine_ref_payload(ref);
                }
                else if (constrained && machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER)
                {
                    reserved_mask |= 1u << machine_x64_slot_scratch[slot];
                }
            }
            // Uses first: constrained slots force their scratch register,
            // free slots keep or pick any register.
            for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(instruction->operands); slot += 1)
            {
                operand_registers[slot] = UINT8_MAX;
                if (slot >= info->operand_count)
                {
                    continue;
                }
                MachineRef ref = instruction->operands[slot];
                MachineRefKind kind = machine_ref_kind(ref);
                if (kind == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    operand_registers[slot] = (u8)machine_ref_payload(ref);
                    continue;
                }
                if (kind != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                if (role != MACHINE_OPERAND_ROLE_USE && role != MACHINE_OPERAND_ROLE_USE_DEFINE)
                {
                    continue;
                }
                u32 target = constrained ? machine_x64_slot_scratch[slot]
                             : instruction->opcode == MACHINE_X64_CALL_INDIRECT ? (u32)MACHINE_X64_R10
                                                                                : UINT32_MAX;
                // A copy into a fixed physical register stages its source in
                // that same register, so an argument sequence can never
                // clobber an already-placed argument through a free pick;
                // the float-argument bridge stages through RAX, which is
                // never an argument register.
                if (instruction->opcode == MACHINE_X64_MOV_RR && slot == 1 &&
                    machine_ref_kind(instruction->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    target = machine_ref_payload(instruction->operands[0]);
                }
                if (instruction->opcode == MACHINE_X64_MOVQ_TO_XMM)
                {
                    target = MACHINE_X64_RAX;
                }
                u32 used = machine_fast_ensure(&state, machine_ref_payload(ref), target,
                                               target == UINT32_MAX ? reserved_mask : 0);
                operand_registers[slot] = (u8)used;
                if (role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                {
                    state.dirty[used] = true;
                }
            }
            // Fixed physical destinations and encoder-internal clobbers
            // evict their owners before the instruction writes them.
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    u32 physical_register = machine_ref_payload(ref);
                    if (role == MACHINE_OPERAND_ROLE_DEFINE && state.owner[physical_register] != UINT32_MAX)
                    {
                        // Overwriting a live owner: the spill store lands
                        // before this instruction writes the register, and
                        // any same-instruction use was already consumed.
                        machine_fast_spill(&state, physical_register);
                    }
                }
            }
            u32 clobber_mask = machine_fast_opcode_clobber_mask(instruction->opcode);
            for (u32 physical_register = 0; clobber_mask; physical_register += 1)
            {
                if (clobber_mask & (1u << physical_register))
                {
                    clobber_mask &= ~(1u << physical_register);
                    machine_fast_spill(&state, physical_register);
                }
            }
            // Vreg definitions bind their register lazily dirty.
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                if (role != MACHINE_OPERAND_ROLE_DEFINE)
                {
                    continue;
                }
                u32 target;
                if (constrained)
                {
                    target = machine_x64_slot_scratch[slot];
                }
                else if (instruction->opcode == MACHINE_X64_MOV_RR && slot == 0 &&
                         machine_ref_kind(instruction->operands[1]) == MACHINE_REF_PHYSICAL_REGISTER &&
                         (MACHINE_FAST_ALLOCATABLE_MASK >> machine_ref_payload(instruction->operands[1])) & 1u)
                {
                    // A capture of a fixed physical register (incoming
                    // argument, call result) binds in place: a free pick here
                    // could land on an argument register whose own capture
                    // has not executed yet and destroy it.
                    target = machine_ref_payload(instruction->operands[1]);
                }
                else
                {
                    target = machine_fast_pick(&state, reserved_mask);
                }
                machine_fast_bind(&state, machine_ref_payload(ref), target);
                operand_registers[slot] = (u8)target;
            }
            if (is_call)
            {
                // Every allocatable register is caller-saved.
                machine_fast_flush(&state);
            }
            if (is_terminator)
            {
                // Block-local allocation: everything returns to its slot at
                // the boundary. The spill moves cannot disturb the flags a
                // conditional terminator just consumed because they are
                // plain stores, and the edits stay at this terminator's
                // BEFORE point, which the encoder emits ahead of it.
                machine_fast_flush(&state);
            }
        }
    }
    placement.edits = arena_allocate(arena, MachineEdit, edits.total_count);
    placement.edit_count = edits.total_count;
    machine_stream_flatten(&edits, placement.edits);
    // Frame layout runs after the scan: every touch of a vreg slot flows
    // through the edit stream, so only edit subjects get backing slots.
    // Values that never left their registers cost no frame bytes, and the
    // guard-page-probe bail applies to the compacted frame.
    u8* slot_needed = state.escapes;
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        slot_needed[register_index] = 0;
    }
    for (u32 edit_index = 0; edit_index < placement.edit_count; edit_index += 1)
    {
        slot_needed[placement.edits[edit_index].subject] = 1;
    }
    u32 running = 0;
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        placement.virtual_register_offsets[register_index] = 0;
        if (slot_needed[register_index])
        {
            running += 8;
            placement.virtual_register_offsets[register_index] = running;
        }
    }
    for (u32 slot_index = 0; slot_index < function->stack_slot_count; slot_index += 1)
    {
        u32 slot_alignment = function->stack_slot_alignments ? function->stack_slot_alignments[slot_index] : 8;
        running = (running + function->stack_slot_sizes[slot_index] + slot_alignment - 1) & ~(slot_alignment - 1);
        placement.stack_slot_offsets[slot_index] = running;
    }
    placement.frame_size = (running + 15u) & ~15u;
    if (placement.frame_size >= 4080)
    {
        return placement;
    }
    placement.valid = true;
    return placement;
}
