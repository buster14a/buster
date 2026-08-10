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

typedef struct MachineFastState MachineFastState;
struct MachineFastState
{
    Arena* arena;
    MachineFunction* function;
    // The target's register file and special-opcode identities; every
    // machine-specific decision below reads from here.
    MachineTargetDescription const* description;
    MachineStackPlacement* placement;
    MachineBuilderStream* edits;
    // Register file: owning vreg per physical register, age for LRU
    // eviction, and whether the register is newer than the vreg's slot.
    u32 owner[MACHINE_TARGET_REGISTER_LIMIT];
    u32 age[MACHINE_TARGET_REGISTER_LIMIT];
    bool dirty[MACHINE_TARGET_REGISTER_LIMIT];
    // Current register per vreg, or UINT32_MAX when the slot is the home.
    u32* virtual_register_locations;
    // Stage-5 liveness: the instruction index of each vreg's last textual
    // use, and whether any use sits outside the defining block. Escaping
    // values always reach their slots (loop-carried and layout-order
    // hazards live only across blocks); a non-escaping value strictly past
    // its last use is dead and its spill store is dropped.
    u32* last_use;
    u8* escapes;
    // Index of the next call at or after each instruction within its own
    // block, or UINT32_MAX: a value whose last use lies past it crosses
    // the call and is worth a callee-saved binding.
    u32* next_call;
    // Immediate index per vreg whose entire definition is one constant
    // materialization, or UINT32_MAX. Such a value is never stored and
    // never occupies a slot: any reload of it re-materializes instead.
    u32* rematerialize_immediates;
    // Globally pinned registers, owned for the whole function by the
    // QUALITY pass and invisible to the local scan.
    u32 const* pinned_registers;
    u32 pinned_mask;
    u32 clock;
    u32 current_point;
    // Set once the current instruction's uses and defines are placed: from
    // there a value whose last use is this instruction has been read for
    // the final time, so the flush and write-back can drop its store.
    bool uses_consumed;
};

BUSTER_GLOBAL_LOCAL bool machine_fast_owner_is_dead(MachineFastState* state, u32 physical_register)
{
    u32 owner = state->owner[physical_register];
    if (owner == UINT32_MAX)
    {
        return false;
    }
    u32 current_index = state->current_point >> 2;
    u32 last = state->last_use[owner];
    // A value confined to its defining block is redefined before every
    // repeat of that block, so passing its last use retires it. Escaping
    // values always reach their slots.
    return !state->escapes[owner] && (state->uses_consumed ? current_index >= last : current_index > last);
}

BUSTER_GLOBAL_LOCAL void machine_fast_spill(MachineFastState* state, u32 physical_register)
{
    u32 owner = state->owner[physical_register];
    if (owner == UINT32_MAX)
    {
        return;
    }
    if (state->dirty[physical_register] && state->rematerialize_immediates[owner] == UINT32_MAX && !machine_fast_owner_is_dead(state, physical_register))
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

BUSTER_GLOBAL_LOCAL void machine_fast_flush(MachineFastState* state, u32 keep_mask)
{
    for (u32 physical_register = 0; physical_register < state->description->register_count; physical_register += 1)
    {
        if ((keep_mask >> physical_register) & 1u)
        {
            continue;
        }
        machine_fast_spill(state, physical_register);
    }
}

// Block-boundary write-back: dirty live values reach their slots so every
// successor sees consistent memory, but the register mappings survive for
// a successor that inherits the state.
BUSTER_GLOBAL_LOCAL void machine_fast_writeback(MachineFastState* state)
{
    for (u32 physical_register = 0; physical_register < state->description->register_count; physical_register += 1)
    {
        if (state->owner[physical_register] == UINT32_MAX)
        {
            continue;
        }
        if (state->dirty[physical_register] && state->rematerialize_immediates[state->owner[physical_register]] == UINT32_MAX &&
            !machine_fast_owner_is_dead(state, physical_register))
        {
            MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
            *edit = (MachineEdit){
                .point = state->current_point,
                .kind = MACHINE_EDIT_SPILL,
                .subject = state->owner[physical_register],
                .location = physical_register,
            };
            state->placement->spill_count += 1;
            state->placement->boundary_spill_count += 1;
        }
        state->dirty[physical_register] = false;
    }
}

// True when the virtual register has no use after the instruction being
// scanned: its uses are all in the defining block and the last of them is
// this instruction, which the use pass has already consumed.
BUSTER_GLOBAL_LOCAL bool machine_fast_source_dies_here(MachineFastState* state, u32 virtual_register)
{
    return !state->escapes[virtual_register] && state->last_use[virtual_register] == (state->current_point >> 2);
}

// True when the virtual register stays live past the next call in the
// current block, making a callee-saved binding worth its push/pop pair.
BUSTER_GLOBAL_LOCAL bool machine_fast_crosses_call(MachineFastState* state, u32 virtual_register)
{
    u32 upcoming_call = state->next_call[state->current_point >> 2];
    return upcoming_call != UINT32_MAX && state->last_use[virtual_register] > upcoming_call;
}

// Picks a free allocatable register, else evicts the least recently used
// owner — a probe bounded by the register-file size. Call-crossing values
// reach for the callee-saved members; everything else only touches them
// once another binding has already paid their push.
BUSTER_GLOBAL_LOCAL u32 machine_fast_pick(MachineFastState* state, u32 forbidden_mask, bool prefers_callee_saved)
{
    u32 candidates = state->description->allocatable_mask & ~forbidden_mask & ~state->pinned_mask;
    if (!prefers_callee_saved)
    {
        candidates &= ~(state->description->callee_saved_mask & ~state->placement->callee_saved_mask);
    }
    u32 best = UINT32_MAX;
    u32 best_age = UINT32_MAX;
    u32 dead = UINT32_MAX;
    u32 free_other = UINT32_MAX;
    u32 preferred_class = prefers_callee_saved ? state->description->callee_saved_mask : ~state->description->callee_saved_mask;
    for (u32 physical_register = 0; physical_register < state->description->register_count; physical_register += 1)
    {
        if (!(candidates & (1u << physical_register)))
        {
            continue;
        }
        if (state->owner[physical_register] == UINT32_MAX)
        {
            if (!((preferred_class >> physical_register) & 1u))
            {
                if (free_other == UINT32_MAX)
                {
                    free_other = physical_register;
                }
                continue;
            }
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
    if (free_other != UINT32_MAX)
    {
        return free_other;
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
        target = machine_fast_pick(state, forbidden_mask, machine_fast_crosses_call(state, virtual_register));
    }
    if (current == target)
    {
        state->age[target] = ++state->clock;
        return target;
    }
    // Evict the target's stale owner first; then the value either copies
    // register-to-register when it already lives in one, carrying its
    // dirtiness, or reloads from its slot.
    machine_fast_spill(state, target);
    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
    if (current != UINT32_MAX)
    {
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_COPY,
            .subject = current,
            .location = target,
        };
        state->placement->copy_count += 1;
        state->dirty[target] = state->dirty[current];
        state->owner[current] = UINT32_MAX;
        state->dirty[current] = false;
    }
    else if (state->rematerialize_immediates[virtual_register] != UINT32_MAX)
    {
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_REMATERIALIZE,
            .subject = state->rematerialize_immediates[virtual_register],
            .location = target,
        };
        state->placement->rematerialize_count += 1;
        state->dirty[target] = false;
    }
    else
    {
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_RELOAD,
            .subject = virtual_register,
            .location = target,
        };
        state->placement->reload_count += 1;
        state->dirty[target] = false;
    }
    state->owner[target] = virtual_register;
    state->virtual_register_locations[virtual_register] = target;
    state->age[target] = ++state->clock;
    state->placement->callee_saved_mask |= (1u << target) & state->description->callee_saved_mask;
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
    state->placement->callee_saved_mask |= (1u << target) & state->description->callee_saved_mask;
}

// `pinned_registers` holds a physical register per virtual register that
// owns it for the whole function, or UINT32_MAX; `pinned_mask` collects
// them. The local scan never picks, binds, spills, or reloads a pinned
// value: its operands simply name the register. FAST passes neither.
MachineStackPlacement machine_fast_placement_build_pinned(Arena* arena, MachineFunction* function, u32 const* pinned_registers, u32 pinned_mask)
{
    // Frame layout is shared with the stack placement for now; slot
    // elimination is the stage-6 optimization.
    MachineStackPlacement placement = {
        .virtual_register_offsets = arena_allocate(arena, u32, function->virtual_register_count),
        .stack_slot_offsets = arena_allocate(arena, u32, function->stack_slot_count),
        .operand_registers = arena_allocate(arena, u8, (u64)function->instruction_count * 4),
    };
    MachineTargetDescription const* description = function->target;
    if (!description)
    {
        return placement;
    }
    MachineBuilderStream edits;
    machine_stream_initialize(&edits, sizeof(MachineEdit));
    placement.callee_saved_mask |= pinned_mask;
    MachineFastState state = {
        .arena = arena,
        .function = function,
        .description = description,
        .placement = &placement,
        .edits = &edits,
        .virtual_register_locations = arena_allocate(arena, u32, function->virtual_register_count),
        .last_use = arena_allocate(arena, u32, function->virtual_register_count),
        .escapes = arena_allocate(arena, u8, function->virtual_register_count),
        .next_call = arena_allocate(arena, u32, function->instruction_count),
        .rematerialize_immediates = arena_allocate(arena, u32, function->virtual_register_count),
        .pinned_registers = pinned_registers,
        .pinned_mask = pinned_mask,
    };
    // Constant definitions are recreatable anywhere, so they never pay for
    // a store or a slot. A second definition of the same value disables
    // the recipe: which constant is current would then depend on the path.
    u8* definition_seen = arena_allocate(arena, u8, function->virtual_register_count);
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        state.rematerialize_immediates[register_index] = UINT32_MAX;
        definition_seen[register_index] = 0;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
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
            if (role != MACHINE_OPERAND_ROLE_DEFINE && role != MACHINE_OPERAND_ROLE_USE_DEFINE)
            {
                continue;
            }
            u32 defined = machine_ref_payload(ref);
            bool constant_definition = instruction->opcode == description->constant_opcode && slot == 0 &&
                                       machine_ref_kind(instruction->operands[1]) == MACHINE_REF_IMMEDIATE;
            state.rematerialize_immediates[defined] =
                constant_definition && !definition_seen[defined] ? machine_ref_payload(instruction->operands[1]) : UINT32_MAX;
            definition_seen[defined] = 1;
        }
    }
    // Edge contract, straight-line form: a block whose only predecessor is
    // its layout neighbor sees exactly the scan state the neighbor's
    // write-back left behind, so it inherits the register file; every
    // other block starts cold. Switch targets over-count conservatively.
    u32* predecessor_counts = arena_allocate(arena, u32, function->block_count);
    u32* single_predecessors = arena_allocate(arena, u32, function->block_count);
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        predecessor_counts[block_index] = 0;
        single_predecessors[block_index] = UINT32_MAX;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
        {
            MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                if (machine_ref_kind(instruction->operands[slot]) == MACHINE_REF_BLOCK)
                {
                    u32 successor = machine_ref_payload(instruction->operands[slot]);
                    predecessor_counts[successor] += 1;
                    single_predecessors[successor] = block_index;
                }
            }
        }
    }
    for (u32 case_index = 0; case_index < function->switch_case_count; case_index += 1)
    {
        predecessor_counts[function->switch_cases[case_index].target_block] += 2;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        u32 upcoming_call = UINT32_MAX;
        for (u32 offset = block->instruction_count; offset > 0; offset -= 1)
        {
            u32 instruction_index = block->first_instruction + offset - 1;
            MachineOpcodeInfo const* call_info = machine_opcode_info(function->instructions[instruction_index].opcode);
            if (call_info && (call_info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL))
            {
                upcoming_call = instruction_index;
            }
            state.next_call[instruction_index] = upcoming_call;
        }
    }
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
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        state.owner[physical_register] = UINT32_MAX;
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        MachineBlock* block = function->blocks + block_index;
        bool inherits = block_index > 0 && predecessor_counts[block_index] == 1 && single_predecessors[block_index] == block_index - 1;
        if (!inherits)
        {
            for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
            {
                u32 owner = state.owner[physical_register];
                if (owner != UINT32_MAX)
                {
                    state.virtual_register_locations[owner] = UINT32_MAX;
                    state.owner[physical_register] = UINT32_MAX;
                    state.dirty[physical_register] = false;
                }
            }
        }
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
            state.uses_consumed = false;
            u8* operand_registers = placement.operand_registers + (u64)instruction_index * 4;
            bool constrained = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) != 0;
            bool is_call = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL) != 0;
            bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
            // Fixed physical operands and the constrained layout vacate
            // their registers first so uses cannot land on them.
            u32 reserved_mask = info->clobber_mask;
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    reserved_mask |= 1u << machine_ref_payload(ref);
                }
                else if (constrained && machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER)
                {
                    reserved_mask |= 1u << description->slot_scratch[slot];
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
                if (state.pinned_registers && state.pinned_registers[machine_ref_payload(ref)] != UINT32_MAX)
                {
                    operand_registers[slot] = (u8)state.pinned_registers[machine_ref_payload(ref)];
                    continue;
                }
                u32 target = constrained                                          ? description->slot_scratch[slot]
                             : instruction->opcode == description->indirect_call_opcode ? (u32)description->indirect_call_register
                                                                                        : UINT32_MAX;
                // A copy into a fixed physical register stages its source in
                // that same register, so an argument sequence can never
                // clobber an already-placed argument through a free pick;
                // the float-argument bridge stages through RAX, which is
                // never an argument register.
                if (instruction->opcode == description->copy_opcode && slot == 1 &&
                    machine_ref_kind(instruction->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    target = machine_ref_payload(instruction->operands[0]);
                }
                if (instruction->opcode == description->float_bridge_opcode)
                {
                    target = description->float_bridge_register;
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
            u32 clobber_mask = info->clobber_mask;
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
                if (state.pinned_registers && state.pinned_registers[machine_ref_payload(ref)] != UINT32_MAX)
                {
                    operand_registers[slot] = (u8)state.pinned_registers[machine_ref_payload(ref)];
                    continue;
                }
                u32 target;
                if (constrained)
                {
                    target = description->slot_scratch[slot];
                }
                else if (instruction->opcode == description->copy_opcode && slot == 0 &&
                         machine_ref_kind(instruction->operands[1]) == MACHINE_REF_PHYSICAL_REGISTER &&
                         (description->allocatable_mask >> machine_ref_payload(instruction->operands[1])) & 1u)
                {
                    // A capture of a fixed physical register (incoming
                    // argument, call result) binds in place: a free pick here
                    // could land on an argument register whose own capture
                    // has not executed yet and destroy it.
                    target = machine_ref_payload(instruction->operands[1]);
                }
                else if (instruction->opcode == description->copy_opcode && slot == 0 &&
                         machine_ref_kind(instruction->operands[1]) == MACHINE_REF_VIRTUAL_REGISTER &&
                         machine_ref_payload(instruction->operands[1]) != machine_ref_payload(ref) &&
                         operand_registers[1] != UINT8_MAX && !((state.pinned_mask >> operand_registers[1]) & 1u) &&
                         machine_fast_source_dies_here(&state, machine_ref_payload(instruction->operands[1])))
                {
                    // Coalesce: the source's last use is this copy, so the
                    // destination takes its register and the row encodes to
                    // nothing. Ownership transfers directly — routing it
                    // through an eviction would write the dying source back
                    // to a slot nobody reads.
                    target = operand_registers[1];
                    u32 dying = machine_ref_payload(instruction->operands[1]);
                    state.owner[target] = UINT32_MAX;
                    state.dirty[target] = false;
                    state.virtual_register_locations[dying] = UINT32_MAX;
                }
                else
                {
                    target = machine_fast_pick(&state, reserved_mask, machine_fast_crosses_call(&state, machine_ref_payload(ref)));
                }
                machine_fast_bind(&state, machine_ref_payload(ref), target);
                operand_registers[slot] = (u8)target;
            }
            state.uses_consumed = true;
            if (is_call)
            {
                // Callee-saved members survive the call by definition;
                // everything else is caller-saved and flushes.
                machine_fast_flush(&state, description->callee_saved_mask);
            }
            if (is_terminator)
            {
                // Dirty values return to their slots at the boundary so
                // every successor sees consistent memory; the mappings
                // stay for a straight-line successor to inherit. The
                // stores cannot disturb the flags a conditional
                // terminator just consumed, and the edits stay at this
                // terminator's BEFORE point, which the encoder emits
                // ahead of it.
                machine_fast_writeback(&state);
            }
        }
    }
    placement.edits = arena_allocate(arena, MachineEdit, edits.total_count);
    placement.edit_count = edits.total_count;
    machine_stream_flatten(&edits, placement.edits);
    // Frame layout runs after the scan: every touch of a vreg slot flows
    // through the edit stream, so only edit subjects get backing slots.
    // Values that never left their registers cost no frame bytes.
    u8* slot_needed = arena_allocate(arena, u8, function->virtual_register_count);
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        slot_needed[register_index] = 0;
    }
    for (u32 edit_index = 0; edit_index < placement.edit_count; edit_index += 1)
    {
        // Only the memory edits name a virtual register: a copy's subject
        // is a physical register and a rematerialization's is an immediate
        // index, and neither indexes this array.
        MachineEdit* edit = placement.edits + edit_index;
        if (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD)
        {
            slot_needed[edit->subject] = 1;
        }
    }
    // Slot reuse by defining block: a non-escaping value's every edit sits
    // inside the block that defines it, so two such values from different
    // blocks never hold their slots at the same time and draw from one
    // shared pool. Escaping values keep dedicated slots — proving their
    // ranges disjoint needs the cross-block liveness the global stage
    // brings. The pool is as wide as the busiest single block.
    u32* pool_indices = arena_allocate(arena, u32, function->virtual_register_count);
    u32* block_pool_cursors = arena_allocate(arena, u32, function->block_count);
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        block_pool_cursors[block_index] = 0;
    }
    u32 pool_size = 0;
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        pool_indices[register_index] = UINT32_MAX;
        if (!slot_needed[register_index] || state.escapes[register_index] || definition_blocks[register_index] == UINT32_MAX)
        {
            continue;
        }
        u32 definition_block = definition_blocks[register_index];
        pool_indices[register_index] = block_pool_cursors[definition_block];
        block_pool_cursors[definition_block] += 1;
        pool_size = BUSTER_MAX(pool_size, block_pool_cursors[definition_block]);
    }
    // The pushed callee-saved registers sit between the frame base and the
    // slots, so every offset starts past them, and the stack allocation
    // keeps sixteen-alignment across an odd push count.
    u32 push_count = 0;
    for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
    {
        push_count += (placement.callee_saved_mask >> physical_register) & 1u;
    }
    u32 pool_base = 8 * push_count;
    u32 running = pool_base + 8 * pool_size;
    for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
    {
        placement.virtual_register_offsets[register_index] = 0;
        if (!slot_needed[register_index])
        {
            continue;
        }
        if (pool_indices[register_index] != UINT32_MAX)
        {
            placement.virtual_register_offsets[register_index] = pool_base + 8 * (pool_indices[register_index] + 1);
            continue;
        }
        running += 8;
        placement.virtual_register_offsets[register_index] = running;
    }
    for (u32 slot_index = 0; slot_index < function->stack_slot_count; slot_index += 1)
    {
        u32 slot_alignment = function->stack_slot_alignments ? function->stack_slot_alignments[slot_index] : 8;
        running = (running + function->stack_slot_sizes[slot_index] + slot_alignment - 1) & ~(slot_alignment - 1);
        placement.stack_slot_offsets[slot_index] = running;
    }
    placement.frame_size = ((running - 8 * push_count + 15u) & ~15u) + ((push_count & 1u) ? 8u : 0u);
    placement.valid = true;
    return placement;
}

MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function)
{
    return machine_fast_placement_build_pinned(arena, function, 0, 0);
}
