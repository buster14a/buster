#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/os.h>
#include <buster/lib/simd.h>

#define MACHINE_FAST_OPERAND_PHYSICAL_SHIFT 0u
#define MACHINE_FAST_OPERAND_VIRTUAL_SHIFT 4u
#define MACHINE_FAST_OPERAND_BLOCK_SHIFT 8u
#define MACHINE_FAST_OPERAND_USE_SHIFT 12u
#define MACHINE_FAST_OPERAND_DEFINE_SHIFT 16u
#define MACHINE_FAST_OPERAND_USE_DEFINE_SHIFT 20u
#define MACHINE_FAST_OPERAND_SIMPLE_ROW (1u << 24)
#define MACHINE_FAST_OPERAND_LANE_MASK 0x0fu

BUSTER_GLOBAL_LOCAL u32 machine_fast_operand_mask(u32 operand_masks, u32 shift)
{
    return (operand_masks >> shift) & MACHINE_FAST_OPERAND_LANE_MASK;
}

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
    // Pinned registers, owned by the QUALITY pass and invisible to the
    // local scan. `pin_active_masks` holds one register mask per
    // instruction scoping each reservation to its value's live span; null
    // means `pinned_mask` holds everywhere. The span arrays give each
    // pinned value its own inclusive instruction range, and the split
    // plans carry the boundary work for spans covering only part of a
    // value's life: contract installs at each span's head block and
    // landing-pad write-backs where a value lives past its span.
    u32 const* pinned_registers;
    u64 pinned_mask;
    u64 const* pin_active_masks;
    u32 const* pin_span_starts;
    u32 const* pin_span_ends;
    MachinePinSplitEntry const* split_entries;
    u32 split_entry_count;
    MachinePinSplitStore const* split_stores;
    u32 split_store_count;
    u32 split_store_cursor;
    // The register indices the scan's loops walk. The unified file puts
    // the vector registers past the general ones, and a function with no
    // vector virtual registers can never own one, so its loops stop at
    // the general file's end — the full-width `owner` state stays
    // initialized either way, which keeps the mask-driven clobber spills
    // (the float rows scribble low vector registers) safe no-ops.
    u32 active_register_count;
    u32 clock;
    u32 current_point;
    // Set once the current instruction's uses and defines are placed: from
    // there a value whose last use is this instruction has been read for
    // the final time, so the flush and write-back can drop its store.
    bool uses_consumed;
};

BUSTER_GLOBAL_LOCAL u64 machine_fast_class_mask(MachineFastState* state, u32 virtual_register);
BUSTER_GLOBAL_LOCAL bool machine_fast_source_dies_here(MachineFastState* state, u32 virtual_register);
BUSTER_GLOBAL_LOCAL void machine_fast_spill(MachineFastState* state, u32 physical_register);

// The registers pinned values own at this instruction, which the local
// scan must stand clear of. Outside every pinned span the register is an
// ordinary member of the pool.
BUSTER_GLOBAL_LOCAL u64 machine_fast_pin_active(MachineFastState* state, u32 instruction_index)
{
    return state->pin_active_masks ? state->pin_active_masks[instruction_index] : state->pinned_mask;
}

// True when the virtual register is pinned and its span covers the
// instruction. A stage-8 span covers every occurrence of its value, so
// each one resolves to the pinned register; a split span opens later or
// closes earlier, and the value is an ordinary scan citizen at the
// occurrences outside.
BUSTER_GLOBAL_LOCAL bool machine_fast_pin_covers(MachineFastState* state, u32 virtual_register, u32 instruction_index)
{
    if (!state->pinned_registers || state->pinned_registers[virtual_register] == UINT32_MAX)
    {
        return false;
    }
    return !state->pin_span_starts ||
           (state->pin_span_starts[virtual_register] <= instruction_index && instruction_index <= state->pin_span_ends[virtual_register]);
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_operand_class(MachineOpcodeInfo const* info, u32 slot)
{
    return (info->operand_info[slot] >> MACHINE_OPERAND_CLASS_SHIFT) & 0x7u;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_tied_destination(MachineOpcodeInfo const* info)
{
    u32 encoded = info ? (u32)(info->tied_pair & 0x0fu) : 0;
    return encoded ? encoded - 1u : UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_tied_source(MachineOpcodeInfo const* info)
{
    u32 encoded = info ? (u32)((info->tied_pair >> 4) & 0x0fu) : 0;
    return encoded ? encoded - 1u : UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_slot_scratch(MachineFastState* state, MachineOpcodeInfo const* info, u32 slot)
{
    return machine_fast_operand_class(info, slot) == MACHINE_REGISTER_CLASS_VECTOR ? state->description->vector_slot_scratch[slot]
                                                                                    : state->description->slot_scratch[slot];
}

// Materialize a tied source in the destination's register without stealing a
// live source binding.  A normal ensure is deliberately not used here: it
// moves the source's ownership, which is only sound when the source dies at
// this row.  If the source remains live, the old register stays owned by it
// and the destination receives a transient copy.  A source with no register
// is already current in its home slot, so a reload/rematerialization supplies
// the transient copy directly.
BUSTER_GLOBAL_LOCAL void machine_fast_materialize_tied(MachineFastState* state, u32 source, u32 target, bool source_dies)
{
    u32 current = state->virtual_register_locations[source];
    if (current == target && !source_dies)
    {
        // The fixed target is also the source's only register. Preserve SSA
        // by writing the source home. The register contents remain valid for
        // this row; the destination transfer below takes ownership after it.
        machine_fast_spill(state, target);
        MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
        if (state->rematerialize_immediates[source] != UINT32_MAX)
        {
            *edit = (MachineEdit){
                .point = state->current_point,
                .kind = MACHINE_EDIT_REMATERIALIZE,
                .subject = state->rematerialize_immediates[source],
                .location = target,
            };
            state->placement->rematerialize_count += 1;
        }
        else
        {
            *edit = (MachineEdit){
                .point = state->current_point,
                .kind = MACHINE_EDIT_RELOAD,
                .subject = source,
                .location = target,
            };
            state->placement->reload_count += 1;
        }
        state->dirty[target] = false;
        return;
    }
    if (current != UINT32_MAX)
    {
        machine_fast_spill(state, target);
        MachineEdit* copy = (MachineEdit*)machine_stream_append(state->arena, state->edits);
        *copy = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_COPY,
            .subject = current,
            .location = target,
        };
        state->placement->copy_count += 1;
        if (source_dies)
        {
            state->owner[current] = UINT32_MAX;
            state->dirty[current] = false;
            state->virtual_register_locations[source] = UINT32_MAX;
        }
        state->owner[target] = UINT32_MAX;
        state->dirty[target] = false;
        return;
    }
    machine_fast_spill(state, target);
    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, state->edits);
    if (state->rematerialize_immediates[source] != UINT32_MAX)
    {
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_REMATERIALIZE,
            .subject = state->rematerialize_immediates[source],
            .location = target,
        };
        state->placement->rematerialize_count += 1;
    }
    else
    {
        *edit = (MachineEdit){
            .point = state->current_point,
            .kind = MACHINE_EDIT_RELOAD,
            .subject = source,
            .location = target,
        };
        state->placement->reload_count += 1;
    }
    state->owner[target] = UINT32_MAX;
    state->dirty[target] = false;
}

BUSTER_GLOBAL_LOCAL bool machine_fast_owner_is_dead(MachineFastState* state, u32 physical_register)
{
    u32 owner = state->owner[physical_register];
    bool result;
    if (owner == UINT32_MAX)
    {
        result = false;
    }
    else
    {
        u32 current_index = state->current_point >> 2;
        u32 last = state->last_use[owner];
        // A value confined to its defining block is redefined before every
        // repeat of that block, so passing its last use retires it. Escaping
        // values always reach their slots.
        result = !state->escapes[owner] && (state->uses_consumed ? current_index >= last : current_index > last);
    }

    return result;
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

BUSTER_GLOBAL_LOCAL void machine_fast_flush(MachineFastState* state, u64 keep_mask)
{
    for (u32 physical_register = 0; physical_register < state->active_register_count; physical_register += 1)
    {
        if ((keep_mask >> physical_register) & 1u)
        {
            continue;
        }
        machine_fast_spill(state, physical_register);
    }
}

// Stage-5 edge contracts. A block's contract is the register file it may
// assume at entry: an owning virtual register and a dirtiness per physical
// register, agreed once when the block is scanned and then satisfied by
// every edge into it. An edge whose delivered state differs conforms — the
// difference becomes edits at the source terminator's BEFORE point, so a
// value stays in its register across the boundary instead of round-tripping
// through its slot the way the old unconditional write-back forced.
BUSTER_GLOBAL_LOCAL u32 const machine_fast_empty_contract_owner[MACHINE_TARGET_REGISTER_LIMIT] = {
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
};
BUSTER_GLOBAL_LOCAL bool const machine_fast_empty_contract_dirty[MACHINE_TARGET_REGISTER_LIMIT] = {0};

BUSTER_GLOBAL_LOCAL void machine_fast_conform_append(MachineFastState* state, MachineBuilderStream* stream, MachinePoint point, u16 kind, u32 subject,
                                                     u32 location)
{
    MachineEdit* edit = (MachineEdit*)machine_stream_append(state->arena, stream);
    *edit = (MachineEdit){
        .point = point,
        .kind = kind,
        .subject = subject,
        .location = location,
    };
}

// Rewrites one edge's delivered register file into the shape a successor's
// contract promises. Values the contract leaves in memory write back (the
// boundary spills the old write-back emitted for every boundary), and the
// values it keeps in registers resolve as a parallel move set: a copy when
// the value is already in a register, a rematerialization or reload when it
// is not, and a spill through memory as the cycle breaker when every
// remaining contract register still holds another pending value's only
// dirty copy. `owner`/`dirty` are either the live scan state at a
// terminator whose successor contract is already fixed (`locations`
// alongside) or a recorded edge snapshot (locations null, conforming
// retroactively while a later block chooses its contract). `allow_moves`
// is false for a snapshot edge whose source has other successors: a copy
// or reload there would also execute on paths that already consumed the
// snapshot state, so such an edge is only ever asked for the universally
// safe spills — the contract construction drops any entry the edge does
// not already satisfy.
BUSTER_GLOBAL_LOCAL void machine_fast_conform_edge(MachineFastState* state, MachineBuilderStream* stream, MachinePoint point, u32* owner, bool* dirty,
                                                   u32* locations, u32 const* contract_owner, bool const* contract_dirty, bool allow_moves)
{
    u32 register_count = state->active_register_count;
    // Pass 1: write back what the contract sends home. Mappings survive —
    // the successor ignores them, a layout successor that designates this
    // edge still reuses the clean copies, and any register a contract value
    // needs is reclaimed by the placement pass below. Values that never
    // escape their block are dead at a boundary and rematerializable
    // constants never store, exactly as the eviction path treats them.
    for (u32 physical_register = 0; physical_register < register_count; physical_register += 1)
    {
        u32 resident = owner[physical_register];
        if (resident == UINT32_MAX || !dirty[physical_register])
        {
            continue;
        }
        bool kept = false;
        for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
        {
            kept |= contract_owner[contract_register] == resident;
        }
        if (kept)
        {
            continue;
        }
        if (state->escapes[resident] && state->rematerialize_immediates[resident] == UINT32_MAX)
        {
            machine_fast_conform_append(state, stream, point, MACHINE_EDIT_SPILL, resident, physical_register);
            state->placement->spill_count += 1;
            state->placement->boundary_spill_count += 1;
        }
        dirty[physical_register] = false;
    }
    // Pass 2: place every contract value not already in its register. An
    // entry whose value's own pinned span covers this edge's source is
    // already satisfied by the span invariant — the register carries the
    // value there even though the scan's file does not track it — and
    // installing it again would run once per iteration on a backward
    // edge, which is exactly the traffic a split span exists to remove.
    u32 source_instruction = machine_point_instruction(point);
    u64 pending = 0;
    for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
    {
        u32 contract_value = contract_owner[contract_register];
        if (contract_value == UINT32_MAX || owner[contract_register] == contract_value)
        {
            continue;
        }
        if (state->pinned_registers && state->pinned_registers[contract_value] == contract_register &&
            machine_fast_pin_covers(state, contract_value, source_instruction))
        {
            continue;
        }
        pending |= 1ull << contract_register;
    }
    while (pending)
    {
        BUSTER_CHECK(allow_moves);
        bool progressed = false;
        for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
        {
            if (!((pending >> contract_register) & 1u))
            {
                continue;
            }
            u32 value = contract_owner[contract_register];
            u32 resident = owner[contract_register];
            if (resident != UINT32_MAX && resident != value && dirty[contract_register])
            {
                // The occupant blocks the claim only while it is another
                // pending value's single dirty copy; a clean occupant
                // reloads at its own turn, its slot current by the
                // clean-implies-stored invariant.
                bool resident_pending = false;
                for (u32 other = 0; other < register_count; other += 1)
                {
                    resident_pending |= contract_owner[other] == resident && ((pending >> other) & 1u);
                }
                if (resident_pending)
                {
                    continue;
                }
            }
            if (resident != UINT32_MAX && resident != value)
            {
                owner[contract_register] = UINT32_MAX;
                if (locations)
                {
                    locations[resident] = UINT32_MAX;
                }
                dirty[contract_register] = false;
            }
            u32 source = UINT32_MAX;
            if (locations)
            {
                source = locations[value];
            }
            else
            {
                for (u32 other = 0; other < register_count; other += 1)
                {
                    source = owner[other] == value ? other : source;
                }
            }
            if (source != UINT32_MAX)
            {
                machine_fast_conform_append(state, stream, point, MACHINE_EDIT_COPY, source, contract_register);
                state->placement->copy_count += 1;
                state->placement->boundary_copy_count += 1;
                dirty[contract_register] = dirty[source];
                owner[source] = UINT32_MAX;
                dirty[source] = false;
            }
            else if (state->rematerialize_immediates[value] != UINT32_MAX)
            {
                machine_fast_conform_append(state, stream, point, MACHINE_EDIT_REMATERIALIZE, state->rematerialize_immediates[value], contract_register);
                state->placement->rematerialize_count += 1;
                dirty[contract_register] = false;
            }
            else
            {
                machine_fast_conform_append(state, stream, point, MACHINE_EDIT_RELOAD, value, contract_register);
                state->placement->reload_count += 1;
                state->placement->boundary_reload_count += 1;
                dirty[contract_register] = false;
            }
            owner[contract_register] = value;
            if (locations)
            {
                locations[value] = contract_register;
            }
            pending &= ~(1ull << contract_register);
            progressed = true;
        }
        if (!progressed)
        {
            // Every remaining target holds another pending value's only
            // dirty copy: a cycle. Break it through memory — write one
            // occupant back and release it, and its own claim reloads it.
            u32 broken = 0;
            while (!((pending >> broken) & 1u))
            {
                broken += 1;
            }
            u32 resident = owner[broken];
            machine_fast_conform_append(state, stream, point, MACHINE_EDIT_SPILL, resident, broken);
            state->placement->spill_count += 1;
            state->placement->boundary_spill_count += 1;
            dirty[broken] = false;
            owner[broken] = UINT32_MAX;
            if (locations)
            {
                locations[resident] = UINT32_MAX;
            }
        }
    }
    // Pass 3: a delivery dirtier than the contract admits stores now — the
    // successor believes the slot is current and would evict silently. A
    // cleaner delivery than promised is free.
    for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
    {
        u32 value = contract_owner[contract_register];
        if (value != UINT32_MAX && owner[contract_register] == value && dirty[contract_register] && !contract_dirty[contract_register])
        {
            machine_fast_conform_append(state, stream, point, MACHINE_EDIT_SPILL, value, contract_register);
            state->placement->spill_count += 1;
            state->placement->boundary_spill_count += 1;
            dirty[contract_register] = false;
        }
    }
}

// Edge copies are SSA block-parameter assignments.  The regular contract
// arrays name the value at the source block, while the successor contract
// must name the parameter value.  Keep that rename edge-local: a conditional
// source can deliver different parameter values to each successor, so the
// scan state and its out snapshot must continue to describe the source block.
// Apply parameter renames to a private edge snapshot, then feed it through
// the existing parallel-copy resolver.  Cloning is intentional: the source
// block may have another successor with a different parameter mapping, and
// mutating its live register file would make the second edge observe the
// first edge's SSA names.
BUSTER_GLOBAL_LOCAL bool machine_fast_edge_can_move(MachineFunction* function, MachineEdge const* edge)
{
    if (!function || !edge || edge->source_block >= function->block_count || !function->blocks[edge->source_block].instruction_count)
    {
        return false;
    }
    MachineBlock const* source_block = function->blocks + edge->source_block;
    MachineInstruction const* terminator = function->instructions + source_block->first_instruction + source_block->instruction_count - 1u;
    MachineOpcodeInfo const* info = machine_opcode_info(terminator->opcode);
    if (!info || (function->target && (terminator->opcode == function->target->switch_opcode || terminator->opcode == MACHINE_X64_INDIRECT_BRANCH ||
                                       terminator->opcode == MACHINE_A64_INDIRECT_BRANCH)))
    {
        return false;
    }
    u32 target_count = 0;
    for (u32 slot = 0; slot < info->operand_count; slot += 1)
    {
        target_count += machine_ref_kind(terminator->operands[slot]) == MACHINE_REF_BLOCK;
    }
    return target_count == 1;
}

BUSTER_GLOBAL_LOCAL void machine_fast_conform_edge_parameters(MachineFastState* state, MachineBuilderStream* stream, MachinePoint point,
                                                              MachineEdge const* edge, u32* owner, bool* dirty, u32* locations,
                                                              u32 const* contract_owner, bool const* contract_dirty, bool allow_moves)
{
    if (!edge || !edge->copy_count || !state->function->edge_copy_sources || !state->function->block_parameters ||
        !machine_fast_edge_can_move(state->function, edge))
    {
        machine_fast_conform_edge(state, stream, point, owner, dirty, locations, contract_owner, contract_dirty, allow_moves);
        return;
    }
    u32 register_count = state->active_register_count;
    u32* mapped_owner = arena_allocate(state->arena, u32, register_count);
    bool* mapped_dirty = arena_allocate(state->arena, bool, register_count);
    memcpy(mapped_owner, owner, sizeof(u32) * register_count);
    memcpy(mapped_dirty, dirty, sizeof(bool) * register_count);
    u32* mapped_locations = 0;
    if (locations)
    {
        mapped_locations = arena_allocate(state->arena, u32, state->function->virtual_register_count);
        memcpy(mapped_locations, locations, sizeof(u32) * state->function->virtual_register_count);
    }
    MachineBlock const* destination = state->function->blocks + edge->destination_block;
    u32 copy_count = BUSTER_MIN(edge->copy_count, destination->parameter_count);
    for (u32 copy_index = 0; copy_index < copy_count; copy_index += 1)
    {
        u32 source_index = edge->copy_offset + copy_index;
        if (source_index >= state->function->edge_copy_source_count)
        {
            break;
        }
        MachineRef source = state->function->edge_copy_sources[source_index];
        u32 destination_value = state->function->block_parameters[destination->parameter_offset + copy_index].virtual_register;
        for (u32 physical_register = 0; physical_register < register_count; physical_register += 1)
        {
            if (mapped_owner[physical_register] == destination_value)
            {
                mapped_owner[physical_register] = UINT32_MAX;
                mapped_dirty[physical_register] = false;
            }
        }
        u32 source_value = UINT32_MAX;
        u32 source_register = UINT32_MAX;
        if (machine_ref_kind(source) == MACHINE_REF_VIRTUAL_REGISTER)
        {
            source_value = machine_ref_payload(source);
            source_register = mapped_locations ? mapped_locations[source_value] : UINT32_MAX;
            if (!mapped_locations)
            {
                for (u32 physical_register = 0; physical_register < register_count; physical_register += 1)
                {
                    if (mapped_owner[physical_register] == source_value)
                    {
                        source_register = physical_register;
                        break;
                    }
                }
            }
        }
        else if (machine_ref_kind(source) == MACHINE_REF_PHYSICAL_REGISTER)
        {
            source_register = machine_ref_payload(source);
            source_register = source_register < register_count ? source_register : UINT32_MAX;
        }
        if (source_register != UINT32_MAX)
        {
            mapped_owner[source_register] = destination_value;
            if (mapped_locations)
            {
                mapped_locations[destination_value] = source_register;
                if (source_value != UINT32_MAX)
                {
                    mapped_locations[source_value] = UINT32_MAX;
                }
            }
        }
    }
    machine_fast_conform_edge(state, stream, point, mapped_owner, mapped_dirty, mapped_locations, contract_owner, contract_dirty, allow_moves);
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_edge_mapped_owner(MachineFunction* function, MachineEdge const* edge, u32 value, u32 const* owner,
                                                       u32 register_count)
{
    BUSTER_UNUSED(owner);
    BUSTER_UNUSED(register_count);
    if (function && edge && value != UINT32_MAX && function->edge_copy_sources && function->block_parameters)
    {
        MachineBlock const* destination = function->blocks + edge->destination_block;
        u32 copy_count = BUSTER_MIN(edge->copy_count, destination->parameter_count);
        for (u32 copy_index = 0; copy_index < copy_count; copy_index += 1)
        {
            u32 source_index = edge->copy_offset + copy_index;
            if (source_index >= function->edge_copy_source_count)
            {
                break;
            }
            MachineRef source = function->edge_copy_sources[source_index];
            if (machine_ref_kind(source) != MACHINE_REF_VIRTUAL_REGISTER || machine_ref_payload(source) != value)
            {
                continue;
            }
            // Name the parameter even when the source is currently spilled. The
            // conform resolver then emits a reload into the parameter's contract
            // register; retaining the source name would lose the SSA assignment.
            return function->block_parameters[destination->parameter_offset + copy_index].virtual_register;
        }
    }

    return value;
}

// Retroactive edits target points the main stream has already passed, in
// whatever order later contract constructions reached them; the encoder
// walks one sorted cursor. Bottom-up stable merge sort — a single-successor
// edge's repair sequence is a little program whose internal order is
// meaning.
BUSTER_GLOBAL_LOCAL void machine_fast_sort_edits(MachineEdit* edits, MachineEdit* scratch, u32 count)
{
    for (u32 width = 1; width < count; width *= 2)
    {
        for (u32 start = 0; start < count; start += 2 * width)
        {
            u32 middle = BUSTER_MIN(start + width, count);
            u32 limit = BUSTER_MIN(start + 2 * width, count);
            u32 left = start;
            u32 right = middle;
            u32 out = start;
            while (left < middle && right < limit)
            {
                scratch[out++] = edits[right].point < edits[left].point ? edits[right++] : edits[left++];
            }
            while (left < middle)
            {
                scratch[out++] = edits[left++];
            }
            while (right < limit)
            {
                scratch[out++] = edits[right++];
            }
        }
        for (u32 index = 0; index < count; index += 1)
        {
            edits[index] = scratch[index];
        }
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

// The register file a virtual register may draw from: its class's
// allocatable mask. The vector file shares the scan, the contracts, and
// the edit stream with the general one — only the candidate set differs.
BUSTER_GLOBAL_LOCAL u64 machine_fast_class_mask(MachineFastState* state, u32 virtual_register)
{
    return state->function->virtual_registers[virtual_register].register_class == MACHINE_REGISTER_CLASS_VECTOR
               ? state->description->vector_allocatable_mask
               : state->description->allocatable_mask;
}

BUSTER_GLOBAL_LOCAL u32 machine_fast_first_set(u64 mask)
{
#if BUSTER_COMPILER_CLANG || BUSTER_COMPILER_GCC
    return (u32)__builtin_ctzll(mask);
#else
    u32 result = 0;
    while (!(mask & 1u))
    {
        mask >>= 1;
        result += 1;
    }
    return result;
#endif
}

// The allocator's common scalar file is exactly sixteen u32 owner lanes.  A
// free-register query is therefore one contiguous 512-bit dword equality
// whose mask arrives one bit per lane — no byte-mask collapse.  The all-ones
// free sentinel is the same bit pattern at any lane width, so the byte splat
// builds the comparand.  Wider vector files consume the same kernel in
// sixteen-register chunks, while non-AVX-512 targets retain the bounded
// scalar walk.
//
// The chunk count is the register-file limit and not `active_register_count`,
// so the whole query is three straight-line compares with no loop and no
// data-dependent branch at all.  Two facts license reading past the active
// count.  `owner` is declared MACHINE_TARGET_REGISTER_LIMIT lanes wide, so
// every chunk is a whole in-bounds vector.  And `candidates` never carries a
// bit at or above `active_register_count`: that count is either the target's
// full file, or — for a function with no vector virtual register — one past
// the highest bit of `allocatable_mask | callee_saved_mask`, which is a
// superset of the only class mask such a function can ask about.  The lanes
// above it therefore contribute nothing whatever they hold, which matters
// because the reduced-count case leaves them at the free sentinel.
//
// The loop was previously bounded by `active_register_count` with an
// `if (!active) continue;` skipping empty chunks.  That saved a
// memory-folded VPCMPEQD and bought two data-dependent branches; a stage-1
// profile put the skip alone at ~1% of all branch misses.  Removing only the
// skip is not the fix — the misses relocate onto the loop's exit branch,
// unchanged in weight — so the loop goes too.
BUSTER_GLOBAL_LOCAL u64 machine_fast_free_candidates(MachineFastState* state, u64 candidates)
{
#if BUSTER_SIMD_512
    BUSTER_CT_CHECK(MACHINE_TARGET_REGISTER_LIMIT % 16u == 0u);
    Simd512 sentinel = simd512_splat(UINT8_MAX);
    u64 free = 0;
    for (u32 base = 0; base < MACHINE_TARGET_REGISTER_LIMIT; base += 16)
    {
        u32 active = (u32)((candidates >> base) & UINT64_C(0xffff));
        Mask64 free_words = simd512_equal_word(simd512_load(state->owner + base), sentinel);
        free |= (u64)((u32)free_words & active) << base;
    }
    return free;
#else
    u64 free = 0;
    for (u64 remaining = candidates; remaining; remaining &= remaining - 1)
    {
        u32 physical_register = machine_fast_first_set(remaining);
        u64 bit = 1ull << physical_register;
        u64 is_free = (u64)(state->owner[physical_register] == UINT32_MAX);
        free |= bit & (0u - is_free);
    }
    return free;
#endif
}

// Picks a free allocatable register, else evicts the least recently used
// owner — a probe bounded by the register-file size. Call-crossing values
// reach for the callee-saved members; everything else only touches them
// once another binding has already paid their push.
BUSTER_GLOBAL_LOCAL u32 machine_fast_pick(MachineFastState* state, u64 class_mask, u64 forbidden_mask, bool prefers_callee_saved)
{
    u64 candidates = class_mask & ~forbidden_mask & ~machine_fast_pin_active(state, state->current_point >> 2);
    if (!prefers_callee_saved)
    {
        // Avoid paying a new callee-saved push for a value that does not
        // cross a call — unless the unpaid members are all that remain,
        // which caller-saved span pins can arrange.
        u64 without_unpaid = candidates & ~(state->description->callee_saved_mask & ~state->placement->callee_saved_mask);
        candidates = without_unpaid ? without_unpaid : candidates;
    }
    u64 preferred_class = prefers_callee_saved ? state->description->callee_saved_mask : ~state->description->callee_saved_mask;
    u64 free = machine_fast_free_candidates(state, candidates);
    u64 preferred_free = free & preferred_class;
    if (preferred_free)
    {
        return machine_fast_first_set(preferred_free);
    }
    if (free)
    {
        return machine_fast_first_set(free);
    }

    u32 best = UINT32_MAX;
    u32 best_age = UINT32_MAX;
    u32 dead = UINT32_MAX;
    for (u32 physical_register = 0; physical_register < state->active_register_count; physical_register += 1)
    {
        if (!(candidates & (1ull << physical_register)))
        {
            continue;
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
BUSTER_GLOBAL_LOCAL u32 machine_fast_ensure(MachineFastState* state, u32 virtual_register, u32 target, u64 forbidden_mask)
{
    u32 current = state->virtual_register_locations[virtual_register];
    if (target == UINT32_MAX)
    {
        if (current != UINT32_MAX)
        {
            state->age[current] = ++state->clock;
            return current;
        }
        target = machine_fast_pick(state, machine_fast_class_mask(state, virtual_register), forbidden_mask,
                                   machine_fast_crosses_call(state, virtual_register));
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
    state->placement->callee_saved_mask |= (1ull << target) & state->description->callee_saved_mask;
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
    state->placement->callee_saved_mask |= (1ull << target) & state->description->callee_saved_mask;
}

// The pin-independent half of the scan, computed once per function and read
// by every scan of it. Two merged walks replace the former one-walk-per-fact
// shape: the first collects everything a single forward pass can —
// rematerialization recipes, predecessor counts, defining blocks, last
// textual uses, whole-function touch intervals with their constrained
// disqualifications, and the backward-edge tally — and the second, once
// defining blocks and predecessor offsets are complete, fills the adjacency
// list, marks cold entries, decides escapes, and records the backward-edge
// spans. QUALITY reads the intervals and spans for its global layer and
// hands the same prepass to both of its scan runs.
MachineFastPrepass machine_fast_prepass_build(Arena* arena, MachineFunction* function)
{
    MachineFastPrepass prepass = {0};
    MachineTargetDescription const* description = function->target;
    u32 register_count = function->virtual_register_count;
    prepass.rematerialize_immediates = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.definition_blocks = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.last_use = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.escapes = arena_allocate(arena, u8, register_count ? register_count : 1);
    prepass.next_call = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
    prepass.operand_masks = arena_allocate(arena, u32, function->instruction_count ? function->instruction_count : 1);
    prepass.interval_starts = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.interval_ends = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.disqualified = arena_allocate(arena, u8, register_count ? register_count : 1);
    prepass.predecessor_offsets = arena_allocate(arena, u32, function->block_count + 1);
    prepass.cold_blocks = arena_allocate(arena, u8, function->block_count ? function->block_count : 1);
    if (description)
    {
        u8* definition_seen = arena_allocate(arena, u8, register_count ? register_count : 1);
        // Most functions fit a defining/use block identity in sixteen bits. Keep
        // that transient classification dense; pathological block counts retain
        // the original all-row escape scan instead of widening every common row.
        u16* use_blocks = function->block_count < UINT16_MAX - 1u ? arena_allocate(arena, u16, register_count ? register_count : 1) : 0;
        if (use_blocks && register_count)
        {
            memset(use_blocks, 0xff, (u64)register_count * sizeof(*use_blocks));
        }
        for (u32 register_index = 0; register_index < register_count; register_index += 1)
        {
            prepass.rematerialize_immediates[register_index] = UINT32_MAX;
            prepass.definition_blocks[register_index] = UINT32_MAX;
            prepass.last_use[register_index] = 0;
            prepass.escapes[register_index] = 0;
            prepass.interval_starts[register_index] = UINT32_MAX;
            prepass.interval_ends[register_index] = 0;
            prepass.disqualified[register_index] = 0;
            definition_seen[register_index] = 0;
        }
        // Block parameters are SSA definitions at block entry. Edge sources are
        // SSA uses on their corresponding incoming edge; account for both before
        // scanning instruction rows so liveness and escape decisions include
        // loop-carried values even when the edge has no terminator operand.
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            MachineBlock const* block = function->blocks + block_index;
            for (u32 parameter_index = 0; parameter_index < block->parameter_count; parameter_index += 1)
            {
                u32 virtual_register = function->block_parameters[block->parameter_offset + parameter_index].virtual_register;
                if (virtual_register >= register_count)
                {
                    continue;
                }
                u32 definition = block->first_instruction;
                prepass.definition_blocks[virtual_register] = block_index;
                prepass.interval_starts[virtual_register] = BUSTER_MIN(prepass.interval_starts[virtual_register], definition);
                prepass.interval_ends[virtual_register] = BUSTER_MAX(prepass.interval_ends[virtual_register], definition);
            }
        }
        for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
        {
            MachineEdge const* edge = function->edges + edge_index;
            for (u32 copy_index = 0; copy_index < edge->copy_count; copy_index += 1)
            {
                u32 source_index = edge->copy_offset + copy_index;
                if (source_index >= function->edge_copy_source_count)
                {
                    continue;
                }
                MachineRef source = function->edge_copy_sources[source_index];
                if (machine_ref_kind(source) != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 virtual_register = machine_ref_payload(source);
                if (virtual_register >= register_count)
                {
                    continue;
                }
                prepass.last_use[virtual_register] = BUSTER_MAX(prepass.last_use[virtual_register], function->blocks[edge->source_block].first_instruction + function->blocks[edge->source_block].instruction_count - 1u);
                prepass.escapes[virtual_register] = 1;
                prepass.interval_starts[virtual_register] = BUSTER_MIN(prepass.interval_starts[virtual_register], function->blocks[edge->source_block].first_instruction);
                prepass.interval_ends[virtual_register] = BUSTER_MAX(prepass.interval_ends[virtual_register], function->blocks[edge->destination_block].first_instruction);
            }
        }
        for (u32 block_index = 0; block_index <= function->block_count; block_index += 1)
        {
            prepass.predecessor_offsets[block_index] = 0;
        }
        u32 backward_edge_count = 0;
        bool block_references_only_in_terminators = true;
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
                    return prepass;
                }
                prepass.callee_saved_clobber_mask |= info->clobber_mask & description->callee_saved_mask;
                bool constrained = machine_opcode_has_constraints(info);
                u32 operand_masks = 0;
                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                {
                    MachineRef ref = instruction->operands[slot];
                    MachineRefKind kind = machine_ref_kind(ref);
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    operand_masks |= (u32)(role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                                     << (MACHINE_FAST_OPERAND_USE_SHIFT + slot);
                    operand_masks |= (u32)(role == MACHINE_OPERAND_ROLE_DEFINE) << (MACHINE_FAST_OPERAND_DEFINE_SHIFT + slot);
                    operand_masks |= (u32)(role == MACHINE_OPERAND_ROLE_USE_DEFINE) << (MACHINE_FAST_OPERAND_USE_DEFINE_SHIFT + slot);
                    if (kind == MACHINE_REF_BLOCK)
                    {
                        operand_masks |= 1u << (MACHINE_FAST_OPERAND_BLOCK_SHIFT + slot);
                        block_references_only_in_terminators &= (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
                        u32 successor = machine_ref_payload(ref);
                        prepass.predecessor_offsets[successor + 1] += 1;
                        backward_edge_count += successor <= block_index;
                        continue;
                    }
                    if (kind != MACHINE_REF_VIRTUAL_REGISTER)
                    {
                        operand_masks |= (u32)(kind == MACHINE_REF_PHYSICAL_REGISTER) << (MACHINE_FAST_OPERAND_PHYSICAL_SHIFT + slot);
                        continue;
                    }
                    operand_masks |= 1u << (MACHINE_FAST_OPERAND_VIRTUAL_SHIFT + slot);
                    u32 virtual_register = machine_ref_payload(ref);
                    prepass.interval_starts[virtual_register] = BUSTER_MIN(prepass.interval_starts[virtual_register], instruction_index);
                    prepass.interval_ends[virtual_register] = BUSTER_MAX(prepass.interval_ends[virtual_register], instruction_index);
                    // QUALITY reserves whole-life pins for unconstrained values.
                    // A tied operand still has a legal coalescing path in FAST,
                    // but pinning it globally would hide the source/destination
                    // register relation from QUALITY's split probes.
                    prepass.disqualified[virtual_register] |= constrained ? 1u : 0u;
                    if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                    {
                        if (prepass.definition_blocks[virtual_register] == UINT32_MAX)
                        {
                            prepass.definition_blocks[virtual_register] = block_index;
                        }
                        // Constant definitions are recreatable anywhere, so they
                        // never pay for a store or a slot. A second definition of
                        // the same value disables the recipe: which constant is
                        // current would then depend on the path.
                        bool constant_definition = instruction->opcode == description->constant_opcode && slot == 0 &&
                                                   machine_ref_kind(instruction->operands[1]) == MACHINE_REF_IMMEDIATE;
                        prepass.rematerialize_immediates[virtual_register] =
                            constant_definition && !definition_seen[virtual_register] ? machine_ref_payload(instruction->operands[1]) : UINT32_MAX;
                        definition_seen[virtual_register] = 1;
                    }
                    if (role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                    {
                        prepass.last_use[virtual_register] = BUSTER_MAX(prepass.last_use[virtual_register], instruction_index);
                        if (use_blocks)
                        {
                            u16 use_block = use_blocks[virtual_register];
                            use_blocks[virtual_register] = use_block == UINT16_MAX || use_block == block_index ? (u16)block_index : UINT16_MAX - 1u;
                        }
                    }
                }
                if (instruction->opcode == MACHINE_X64_INDIRECT_BRANCH || instruction->opcode == MACHINE_A64_INDIRECT_BRANCH)
                {
                    if (instruction->payload > function->switch_case_count ||
                        instruction->flags > function->switch_case_count - instruction->payload)
                    {
                        return prepass;
                    }
                    for (u32 target_index = 0; target_index < instruction->flags; target_index += 1)
                    {
                        u32 successor = function->switch_cases[instruction->payload + target_index].target_block;
                        if (successor >= function->block_count)
                        {
                            return prepass;
                        }
                        prepass.predecessor_offsets[successor + 1] += 1;
                        backward_edge_count += successor <= block_index;
                    }
                }
                if (!constrained && !(info->attributes & (MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_TERMINATOR)) && !info->clobber_mask &&
                    !(operand_masks & ((MACHINE_FAST_OPERAND_LANE_MASK << MACHINE_FAST_OPERAND_PHYSICAL_SHIFT) |
                                       (MACHINE_FAST_OPERAND_LANE_MASK << MACHINE_FAST_OPERAND_BLOCK_SHIFT))))
                {
                    operand_masks |= MACHINE_FAST_OPERAND_SIMPLE_ROW;
                }
                prepass.operand_masks[instruction_index] = operand_masks;
            }
        }
        for (u32 register_index = 0; use_blocks && register_index < register_count; register_index += 1)
        {
            u16 use_block = use_blocks[register_index];
            prepass.escapes[register_index] |= use_block != UINT16_MAX && use_block != prepass.definition_blocks[register_index];
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            prepass.predecessor_offsets[block_index + 1] += prepass.predecessor_offsets[block_index];
        }
        u32 predecessor_total = prepass.predecessor_offsets[function->block_count];
        prepass.predecessor_list = arena_allocate(arena, u32, predecessor_total ? predecessor_total : 1);
        u32* predecessor_cursors = arena_allocate(arena, u32, function->block_count ? function->block_count : 1);
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            predecessor_cursors[block_index] = prepass.predecessor_offsets[block_index];
            prepass.cold_blocks[block_index] = 0;
        }
        for (u32 case_index = 0; case_index < function->switch_case_count; case_index += 1)
        {
            prepass.cold_blocks[function->switch_cases[case_index].target_block] = 1;
        }
        prepass.loop_spans = arena_allocate(arena, u64, backward_edge_count ? backward_edge_count : 1);
        // A block must start cold — the empty contract — when some future edge
        // into it could not conform: a switch dispatch cannot host per-target
        // repairs, and a two-target conditional whose successors both precede
        // it has no later successor left to absorb repairs made for the other.
        // Terminator shapes classify structurally by block-ref operand count,
        // so the rules hold on any target; only the table dispatch needs its
        // identity from the description. Escapes decide here too, now that the
        // defining blocks are complete: layout order does not prove a def
        // precedes its uses, so the first walk alone could not tell.
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            MachineBlock* block = function->blocks + block_index;
            u32 first_offset = use_blocks && block_references_only_in_terminators && block->instruction_count ? block->instruction_count - 1u : 0;
            for (u32 offset = first_offset; offset < block->instruction_count; offset += 1)
            {
                MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                u32 target_references[BUSTER_ARRAY_LENGTH(instruction->operands)];
                u32 target_reference_count = 0;
                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                {
                    MachineRef ref = instruction->operands[slot];
                    MachineRefKind kind = machine_ref_kind(ref);
                    if (kind == MACHINE_REF_BLOCK)
                    {
                        u32 successor = machine_ref_payload(ref);
                        prepass.predecessor_list[predecessor_cursors[successor]++] = block_index;
                        target_references[target_reference_count++] = successor;
                        if (instruction->opcode == description->switch_opcode)
                        {
                            prepass.cold_blocks[successor] = 1;
                        }
                            if (successor <= block_index)
                        {
                            u32 loop_start = function->blocks[successor].first_instruction;
                            u32 loop_end = block->first_instruction + block->instruction_count - 1;
                            prepass.loop_spans[prepass.loop_span_count] = ((u64)loop_start << 32) | loop_end;
                            prepass.loop_span_count += 1;
                        }
                    }
                    else if (!use_blocks && kind == MACHINE_REF_VIRTUAL_REGISTER)
                    {
                        u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                        if ((role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) &&
                            prepass.definition_blocks[machine_ref_payload(ref)] != block_index)
                        {
                            prepass.escapes[machine_ref_payload(ref)] = 1;
                        }
                    }
                }
                if (instruction->opcode == MACHINE_X64_INDIRECT_BRANCH || instruction->opcode == MACHINE_A64_INDIRECT_BRANCH)
                {
                    for (u32 target_index = 0; target_index < instruction->flags; target_index += 1)
                    {
                        u32 successor = function->switch_cases[instruction->payload + target_index].target_block;
                        prepass.predecessor_list[predecessor_cursors[successor]++] = block_index;
                        prepass.cold_blocks[successor] = 1;
                        if (successor <= block_index)
                        {
                            u32 loop_start = function->blocks[successor].first_instruction;
                            u32 loop_end = block->first_instruction + block->instruction_count - 1;
                            prepass.loop_spans[prepass.loop_span_count] = ((u64)loop_start << 32) | loop_end;
                            prepass.loop_span_count += 1;
                        }
                    }
                }
                if (target_reference_count == 2 && target_references[0] <= block_index && target_references[1] <= block_index)
                {
                    prepass.cold_blocks[target_references[0]] = 1;
                    prepass.cold_blocks[target_references[1]] = 1;
                }
            }
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
                prepass.next_call[instruction_index] = upcoming_call;
            }
        }
        // A function with no vector virtual registers can never own a vector
        // register, so its loops stop at the general file's end; the widened
        // unified file would otherwise double every scan pass for the common
        // all-scalar function.
        prepass.active_register_count = description->register_count;
        if (description->vector_allocatable_mask)
        {
            bool scan_has_vector = false;
            for (u32 register_index = 0; register_index < register_count; register_index += 1)
            {
                scan_has_vector |= function->virtual_registers[register_index].register_class == MACHINE_REGISTER_CLASS_VECTOR;
            }
            if (!scan_has_vector)
            {
                u64 general_mask = description->allocatable_mask | description->callee_saved_mask;
                u32 general_top = 0;
                for (u32 bit_index = 0; bit_index < MACHINE_TARGET_REGISTER_LIMIT; bit_index += 1)
                {
                    general_top = ((general_mask >> bit_index) & 1u) ? bit_index + 1 : general_top;
                }
                prepass.active_register_count = general_top;
            }
        }
        prepass.valid = true;
    }

    return prepass;
}

// `pinned_registers` holds a physical register per virtual register that
// owns it, or UINT32_MAX; `pinned_mask` collects them; `pin_active_masks`
// scopes each reservation to its value's span, or reserves `pinned_mask`
// whole-function when null. Within its span the local scan never picks,
// binds, spills, or reloads a pinned value: its operands simply name the
// register, and the register rejoins the pool wherever no span covers the
// instruction. The span arrays make that boundary per-value; a split
// span's handoff arrives pre-validated in `split_entries` (the head-block
// contract installs the value, so every entering edge conforms it into
// the register while the backward edges the span covers are satisfied by
// the span invariant) and `split_stores` (row-sorted landing-pad
// write-backs for values living past their span). FAST passes none of
// them.
MachineStackPlacement machine_fast_placement_build_pinned(Arena* arena, MachineFunction* function, u32 const* pinned_registers, u64 pinned_mask,
                                                          u64 const* pin_active_masks, u32 const* pin_span_starts, u32 const* pin_span_ends,
                                                          MachinePinSplitEntry const* split_entries, u32 split_entry_count,
                                                          MachinePinSplitStore const* split_stores, u32 split_store_count)
{
    MachineFastPrepass prepass = machine_fast_prepass_build(arena, function);
    return machine_fast_placement_build_prepassed(arena, function, &prepass, pinned_registers, pinned_mask, pin_active_masks, pin_span_starts,
                                                  pin_span_ends, split_entries, split_entry_count, split_stores, split_store_count);
}

MachineStackPlacement machine_fast_placement_build_prepassed(Arena* arena, MachineFunction* function, MachineFastPrepass const* prepass,
                                                             u32 const* pinned_registers, u64 pinned_mask, u64 const* pin_active_masks,
                                                             u32 const* pin_span_starts, u32 const* pin_span_ends,
                                                             MachinePinSplitEntry const* split_entries, u32 split_entry_count,
                                                             MachinePinSplitStore const* split_stores, u32 split_store_count)
{
    // Frame layout is shared with the stack placement for now; slot
    // elimination is the stage-6 optimization.
    MachineStackPlacement placement = {
        .virtual_register_offsets = arena_allocate(arena, u32, function->virtual_register_count),
        .stack_slot_offsets = arena_allocate(arena, u32, function->stack_slot_count),
        .operand_registers = arena_allocate(arena, u8, (u64)function->instruction_count * 4),
    };
    MachineTargetDescription const* description = function->target;
    if (description)
    {
        if (!prepass->valid)
        {
            return placement;
        }
        MachineBuilderStream edits;
        machine_stream_initialize(&edits, sizeof(MachineEdit));
        // Only the callee-saved pins cost a prologue save; a caller-saved pin
        // is exactly why QUALITY hands them out where a span crosses no call.
        placement.callee_saved_mask |= pinned_mask & description->callee_saved_mask;
        // Metadata-only clobbers (for example CMPXCHG16B's implicit RBX write)
        // still require an ABI save even when no virtual value owns that
        // physical register during the scan.
        placement.callee_saved_mask |= prepass->callee_saved_clobber_mask;
        MachineFastState state = {
            .arena = arena,
            .function = function,
            .description = description,
            .placement = &placement,
            .edits = &edits,
            .virtual_register_locations = arena_allocate(arena, u32, function->virtual_register_count),
            .last_use = prepass->last_use,
            .escapes = prepass->escapes,
            .next_call = prepass->next_call,
            .rematerialize_immediates = prepass->rematerialize_immediates,
            .pinned_registers = pinned_registers,
            .pinned_mask = pinned_mask,
            .pin_active_masks = pin_active_masks,
            .pin_span_starts = pin_span_starts,
            .pin_span_ends = pin_span_ends,
            .split_entries = split_entries,
            .split_entry_count = split_entry_count,
            .split_stores = split_stores,
            .split_store_count = split_store_count,
            .active_register_count = prepass->active_register_count,
        };
        u32 const* predecessor_offsets = prepass->predecessor_offsets;
        u32 const* predecessor_list = prepass->predecessor_list;
        u8 const* cold_blocks = prepass->cold_blocks;
        u32 const* definition_blocks = prepass->definition_blocks;
        // Contracts and per-edge snapshots, one register file per block. A
        // block's out state is recorded at its terminator after any inline
        // conforms, which is exactly what every one of its edges delivers; a
        // later contract construction conforms the snapshot retroactively and
        // its mutations persist, so two successors of one conditional never
        // write the same value back twice.
        u32 register_count = state.active_register_count;
        u32* contract_owner = arena_allocate(arena, u32, (u64)function->block_count * register_count);
        bool* contract_dirty = arena_allocate(arena, bool, (u64)function->block_count * register_count);
        u32* out_owner = arena_allocate(arena, u32, (u64)function->block_count * register_count);
        bool* out_dirty = arena_allocate(arena, bool, (u64)function->block_count * register_count);
        u64 entry_count = (u64)function->block_count * register_count;
        if (entry_count)
        {
            memset(contract_owner, 0xff, entry_count * sizeof(*contract_owner));
            memset(contract_dirty, 0, entry_count * sizeof(*contract_dirty));
            memset(out_owner, 0xff, entry_count * sizeof(*out_owner));
            memset(out_dirty, 0, entry_count * sizeof(*out_dirty));
        }
        MachineBuilderStream retro_edits;
        machine_stream_initialize(&retro_edits, sizeof(MachineEdit));
        for (u32 register_index = 0; register_index < function->virtual_register_count; register_index += 1)
        {
            state.virtual_register_locations[register_index] = UINT32_MAX;
        }
        for (u32 physical_register = 0; physical_register < description->register_count; physical_register += 1)
        {
            state.owner[physical_register] = UINT32_MAX;
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            MachineBlock* block = function->blocks + block_index;
            u32* entry_owner = contract_owner + (u64)block_index * register_count;
            bool* entry_dirty = contract_dirty + (u64)block_index * register_count;
            // Contract construction. The designated predecessor — the layout
            // neighbor when it is a real edge, else the first already-scanned
            // one — donates its out state, filtered to values worth carrying: a
            // value must escape its block (anything else is dead at a
            // boundary), still be live here, and not be a rematerializable
            // constant, which recreates anywhere for less than a register.
            if (block_index > 0 && !cold_blocks[block_index])
            {
                u32 first_predecessor = predecessor_offsets[block_index];
                u32 predecessor_limit = predecessor_offsets[block_index + 1];
                u32 designated = UINT32_MAX;
                bool has_unscanned_predecessor = false;
                for (u32 predecessor_index = first_predecessor; predecessor_index < predecessor_limit; predecessor_index += 1)
                {
                    u32 predecessor = predecessor_list[predecessor_index];
                    if (predecessor >= block_index)
                    {
                        has_unscanned_predecessor = true;
                    }
                    else if (predecessor == block_index - 1 || designated == UINT32_MAX)
                    {
                        designated = predecessor;
                    }
                }
                if (designated != UINT32_MAX)
                {
                    u32 const* donor_owner = out_owner + (u64)designated * register_count;
                    bool const* donor_dirty = out_dirty + (u64)designated * register_count;
                    MachineEdge const* designated_edge = 0;
                    for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
                    {
                        MachineEdge const* candidate = function->edges + edge_index;
                        if (candidate->source_block == designated && candidate->destination_block == block_index)
                        {
                            designated_edge = candidate;
                            break;
                        }
                    }
                    // A register a pinned span holds at this block's entry
                    // belongs to the pinned value here, whatever any edge
                    // delivers, so the contract cannot promise it.
                    u64 entry_pin_active = block->instruction_count ? machine_fast_pin_active(&state, block->first_instruction) : 0;
                    for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
                    {
                        u32 value = donor_owner[contract_register];
                        if (value == UINT32_MAX || ((entry_pin_active >> contract_register) & 1u) || !state.escapes[value] ||
                            state.rematerialize_immediates[value] != UINT32_MAX || state.last_use[value] < block->first_instruction)
                        {
                            continue;
                        }
                        // A clean value is carried through single-predecessor
                        // and join blocks, where retention costs nothing on
                        // any edge that still holds it — but not into a block
                        // a back edge reaches, where the loop would reload it
                        // every iteration whether or not the body wants it.
                        // Measured on the pair's final shape: -20 M against
                        // carrying clean values everywhere. Dirty values are
                        // always worth carrying: the entry is the write-back
                        // its edge would otherwise pay.
                        if (has_unscanned_predecessor && !donor_dirty[contract_register] && predecessor_limit - first_predecessor > 1)
                        {
                            continue;
                        }
                        entry_owner[contract_register] = machine_fast_edge_mapped_owner(function, designated_edge, value, donor_owner, register_count);
                        entry_dirty[contract_register] = donor_dirty[contract_register];
                    }
                    // Intersection with the other scanned predecessors. An
                    // edge from a single-successor jump repairs retroactively,
                    // so a mismatch there keeps the entry and the conform
                    // below moves or reloads the value; any other source has
                    // successors that already consumed its state, so only its
                    // exact matches survive. Dirtiness is the OR over what the
                    // keeping edges actually deliver.
                    for (u32 predecessor_index = first_predecessor; predecessor_index < predecessor_limit; predecessor_index += 1)
                    {
                        u32 predecessor = predecessor_list[predecessor_index];
                        if (predecessor >= block_index || predecessor == designated)
                        {
                            continue;
                        }
                        u32 const* edge_owner = out_owner + (u64)predecessor * register_count;
                        bool const* edge_dirty = out_dirty + (u64)predecessor * register_count;
                        MachineEdge const* predecessor_edge = 0;
                        for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
                        {
                            MachineEdge const* candidate = function->edges + edge_index;
                            if (candidate->source_block == predecessor && candidate->destination_block == block_index)
                            {
                                predecessor_edge = candidate;
                                break;
                            }
                        }
                        MachineBlock* predecessor_block = function->blocks + predecessor;
                        bool repairs_fully = false;
                        u64 repair_pin_active = 0;
                        if (predecessor_block->instruction_count)
                        {
                            u32 terminator_index = predecessor_block->first_instruction + predecessor_block->instruction_count - 1;
                            MachineInstruction* predecessor_terminator = function->instructions + terminator_index;
                            MachineOpcodeInfo const* terminator_info = machine_opcode_info(predecessor_terminator->opcode);
                            u32 terminator_targets = 0;
                            for (u32 slot = 0; slot < terminator_info->operand_count; slot += 1)
                            {
                                terminator_targets += machine_ref_kind(predecessor_terminator->operands[slot]) == MACHINE_REF_BLOCK;
                            }
                            repairs_fully = terminator_targets == 1 && predecessor_terminator->opcode != description->switch_opcode;
                            // A repair on this edge writes at the terminator,
                            // so it may not target a register a pinned span
                            // holds there; a matching delivery is proof the
                            // register was free, since a span's register is
                            // never in the scan's own file while it is held.
                            repair_pin_active = machine_fast_pin_active(&state, terminator_index);
                        }
                        for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
                        {
                            u32 value = entry_owner[contract_register];
                            if (value == UINT32_MAX)
                            {
                                continue;
                            }
                            u32 delivered_value = machine_fast_edge_mapped_owner(function, predecessor_edge, edge_owner[contract_register], edge_owner,
                                                                                 register_count);
                            if (delivered_value == value)
                            {
                                entry_dirty[contract_register] |= edge_dirty[contract_register];
                            }
                            else if (repairs_fully && !((repair_pin_active >> contract_register) & 1u))
                            {
                                for (u32 other = 0; other < register_count; other += 1)
                                {
                                    entry_dirty[contract_register] |= edge_owner[other] == value && edge_dirty[other];
                                }
                            }
                            else
                            {
                                entry_owner[contract_register] = UINT32_MAX;
                                entry_dirty[contract_register] = false;
                            }
                        }
                    }
                }
            }
            // Split spans opening at this block force their value into the
            // contract: every entering edge then installs it into the pinned
            // register through the ordinary conform below, and the backward
            // edges the span covers satisfy the entry through the conform's
            // span-invariant skip. Any other contract entry carrying the
            // value goes — a second register holding a copy the span's writes
            // would silently stale is exactly the hazard the pin verifier
            // exists for. The entry promises dirty: the value's slot is
            // refreshed by its landing-pad stores when it lives past the
            // span, and nothing reads it when it does not, so a dirty
            // delivery pays no entry store.
            for (u32 split_index = 0; split_index < state.split_entry_count; split_index += 1)
            {
                if (state.split_entries[split_index].block != block_index)
                {
                    continue;
                }
                u32 split_value = state.split_entries[split_index].virtual_register;
                u32 split_register = state.pinned_registers[split_value];
                for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
                {
                    if (entry_owner[contract_register] == split_value)
                    {
                        entry_owner[contract_register] = UINT32_MAX;
                        entry_dirty[contract_register] = false;
                    }
                }
                entry_owner[split_register] = split_value;
                entry_dirty[split_register] = true;
            }
            // Conform every scanned predecessor edge to the contract just
            // fixed (the empty one for cold blocks and for blocks no scanned
            // edge reaches). The edits land retroactively at each source
            // terminator's point — the merge below re-sorts the stream — and
            // the snapshot mutations persist so a second successor of the
            // same source sees what its edge now actually delivers.
            if (block_index > 0)
            {
                for (u32 predecessor_index = predecessor_offsets[block_index]; predecessor_index < predecessor_offsets[block_index + 1]; predecessor_index += 1)
                {
                    u32 predecessor = predecessor_list[predecessor_index];
                    MachineBlock* predecessor_block = function->blocks + predecessor;
                    if (predecessor >= block_index || !predecessor_block->instruction_count)
                    {
                        continue;
                    }
                    u32 terminator_index = predecessor_block->first_instruction + predecessor_block->instruction_count - 1;
                    MachineInstruction* predecessor_terminator = function->instructions + terminator_index;
                    MachineOpcodeInfo const* terminator_info = machine_opcode_info(predecessor_terminator->opcode);
                    u32 terminator_targets = 0;
                    for (u32 slot = 0; slot < terminator_info->operand_count; slot += 1)
                    {
                        terminator_targets += machine_ref_kind(predecessor_terminator->operands[slot]) == MACHINE_REF_BLOCK;
                    }
                    MachineEdge const* predecessor_edge = 0;
                    for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
                    {
                        MachineEdge const* candidate = function->edges + edge_index;
                        if (candidate->source_block == predecessor && candidate->destination_block == block_index)
                        {
                            predecessor_edge = candidate;
                            break;
                        }
                    }
                    machine_fast_conform_edge_parameters(&state, &retro_edits, machine_point_make(terminator_index, MACHINE_POINT_BEFORE), predecessor_edge,
                                                         out_owner + (u64)predecessor * register_count, out_dirty + (u64)predecessor * register_count, 0,
                                                         entry_owner, entry_dirty, terminator_targets == 1 && predecessor_terminator->opcode != description->switch_opcode);
                }
            }
            // The scan state becomes exactly the contract: the register file of
            // the block ahead is an interface every edge satisfies, not
            // whatever the layout neighbor happened to leave behind.
            for (u32 physical_register = 0; physical_register < register_count; physical_register += 1)
            {
                u32 owner = state.owner[physical_register];
                if (owner != UINT32_MAX)
                {
                    state.virtual_register_locations[owner] = UINT32_MAX;
                    state.owner[physical_register] = UINT32_MAX;
                    state.dirty[physical_register] = false;
                }
            }
            // Carried values enter fresh: in a loop the contract is the
            // working set, and making it the preferred eviction victim would
            // have the body displace exactly what the back edge must then
            // restore, every iteration. Dead expression temporaries are still
            // free victims through the eviction's dead-owner preference. An
            // entry naming a register a pinned span holds at this block's
            // head is the span's own installed value: it lives outside the
            // scan's file — the span invariant owns it — and loading it as a
            // local owner would have the opening eviction store it back
            // spuriously at a point every iteration passes.
            u64 head_pin_active = state.pinned_registers && block->instruction_count ? machine_fast_pin_active(&state, block->first_instruction) : 0;
            for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
            {
                u32 value = entry_owner[contract_register];
                if (value != UINT32_MAX && !((head_pin_active >> contract_register) & 1u))
                {
                    state.owner[contract_register] = value;
                    state.dirty[contract_register] = entry_dirty[contract_register];
                    state.virtual_register_locations[value] = contract_register;
                    state.age[contract_register] = ++state.clock;
                }
            }
            for (u32 offset = 0; offset < block->instruction_count; offset += 1)
            {
                u32 instruction_index = block->first_instruction + offset;
                MachineInstruction* instruction = function->instructions + instruction_index;
                u32 operand_masks = prepass->operand_masks[instruction_index];
                u32 virtual_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_VIRTUAL_SHIFT);
                u32 use_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_USE_SHIFT);
                u32 define_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_DEFINE_SHIFT);
                u32 use_define_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_USE_DEFINE_SHIFT);
                state.current_point = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
                state.uses_consumed = false;
                // Split write-backs land at this row's head: the landing pad
                // every path out of the split span passes, where the value's
                // register still holds it. Emitted ahead of the row's own
                // edits, so nothing at this point can have touched the
                // register first, and the slot is current before any reload
                // of the value the row itself places.
                while (state.split_store_cursor < state.split_store_count && state.split_stores[state.split_store_cursor].row == instruction_index)
                {
                    u32 stored_value = state.split_stores[state.split_store_cursor].virtual_register;
                    MachineEdit* store_edit = (MachineEdit*)machine_stream_append(state.arena, state.edits);
                    *store_edit = (MachineEdit){
                        .point = state.current_point,
                        .kind = MACHINE_EDIT_SPILL,
                        .subject = stored_value,
                        .location = state.pinned_registers[stored_value],
                    };
                    placement.spill_count += 1;
                    placement.boundary_spill_count += 1;
                    state.split_store_cursor += 1;
                }
                // A register entering a pinned span belongs to its value from
                // here, so any local owner leaves first. At a block head the
                // contract construction already refused these registers, so
                // the eviction only fires where a span opens mid-block — at a
                // pinned value's definition — which is a point that cannot
                // re-execute without passing the block head again.
                u64 pins_opening = state.pinned_registers ? machine_fast_pin_active(&state, instruction_index) : 0;
                for (u32 physical_register = 0; pins_opening; physical_register += 1)
                {
                    if (pins_opening & (1ull << physical_register))
                    {
                        pins_opening &= ~(1ull << physical_register);
                        if (state.owner[physical_register] != UINT32_MAX)
                        {
                            machine_fast_spill(&state, physical_register);
                        }
                    }
                }
                u8* operand_registers = placement.operand_registers + (u64)instruction_index * 4;
                memset(operand_registers, 0xff, 4);
                // Most selected rows are virtual-only, unconstrained dataflow.
                // Keep that homogeneous population on a compact mask kernel;
                // fixed registers, clobbers and control flow stay in the general
                // path below instead of taxing every common row with their state.
                if (operand_masks & MACHINE_FAST_OPERAND_SIMPLE_ROW)
                {
                    for (u32 remaining = virtual_slots & use_slots; remaining; remaining &= remaining - 1u)
                    {
                        u32 slot = machine_fast_first_set(remaining);
                        MachineRef ref = instruction->operands[slot];
                        u32 virtual_register = machine_ref_payload(ref);
                        if (state.pinned_registers && machine_fast_pin_covers(&state, virtual_register, instruction_index))
                        {
                            operand_registers[slot] = (u8)state.pinned_registers[virtual_register];
                            continue;
                        }
                        u32 used = machine_fast_ensure(&state, virtual_register, UINT32_MAX, 0);
                        operand_registers[slot] = (u8)used;
                        if (use_define_slots & (1u << slot))
                        {
                            state.dirty[used] = true;
                        }
                    }
                    for (u32 remaining = virtual_slots & define_slots; remaining; remaining &= remaining - 1u)
                    {
                        u32 slot = machine_fast_first_set(remaining);
                        MachineRef ref = instruction->operands[slot];
                        u32 virtual_register = machine_ref_payload(ref);
                        if (state.pinned_registers && machine_fast_pin_covers(&state, virtual_register, instruction_index))
                        {
                            operand_registers[slot] = (u8)state.pinned_registers[virtual_register];
                            continue;
                        }
                        u32 target;
                        if ((instruction->opcode == description->copy_opcode || instruction->opcode == description->vector_copy_opcode) && slot == 0 &&
                            (virtual_slots & (1u << 1)) && machine_ref_payload(instruction->operands[1]) != virtual_register &&
                            operand_registers[1] != UINT8_MAX &&
                            (!state.pinned_registers || !((machine_fast_pin_active(&state, instruction_index) >> operand_registers[1]) & 1u)) &&
                            machine_fast_source_dies_here(&state, machine_ref_payload(instruction->operands[1])))
                        {
                            target = operand_registers[1];
                            u32 dying = machine_ref_payload(instruction->operands[1]);
                            state.owner[target] = UINT32_MAX;
                            state.dirty[target] = false;
                            state.virtual_register_locations[dying] = UINT32_MAX;
                        }
                        else
                        {
                            target = machine_fast_pick(&state, machine_fast_class_mask(&state, virtual_register), 0,
                                                       machine_fast_crosses_call(&state, virtual_register));
                        }
                        machine_fast_bind(&state, virtual_register, target);
                        operand_registers[slot] = (u8)target;
                    }
                    state.uses_consumed = true;
                    continue;
                }
                // The prepass validated every opcode before publishing a simple
                // row. Only the irregular population needs its full descriptor
                // again during placement.
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                if (!info)
                {
                    return placement;
                }
                u32 physical_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_PHYSICAL_SHIFT);
                u32 block_slots = machine_fast_operand_mask(operand_masks, MACHINE_FAST_OPERAND_BLOCK_SHIFT);
                bool constrained = machine_opcode_has_constraints(info);
                bool is_call = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL) != 0;
                bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
                u32 tied_destination = machine_fast_tied_destination(info);
                u32 tied_source = machine_fast_tied_source(info);
                u32 tied_target = UINT32_MAX;
                bool tied_source_dies = false;
                // Fixed physical operands and the constrained layout vacate
                // their registers first so uses cannot land on them.
                u64 reserved_mask = info->clobber_mask;
                u32 reservation_slots =
                    (physical_slots | info->fixed_register_mask | (constrained ? virtual_slots : 0u)) & MACHINE_FAST_OPERAND_LANE_MASK;
                for (u32 remaining = reservation_slots; remaining; remaining &= remaining - 1u)
                {
                    u32 slot = machine_fast_first_set(remaining);
                    MachineRef ref = instruction->operands[slot];
                    u32 lane = 1u << slot;
                    if (physical_slots & lane)
                    {
                        reserved_mask |= 1ull << machine_ref_payload(ref);
                        operand_registers[slot] = (u8)machine_ref_payload(ref);
                    }
                    u32 fixed = machine_opcode_fixed_register(info, slot);
                    if (fixed != UINT32_MAX && fixed < MACHINE_TARGET_REGISTER_LIMIT)
                    {
                        reserved_mask |= 1ull << fixed;
                    }
                    else if (constrained && (virtual_slots & lane))
                    {
                        reserved_mask |= 1ull << machine_fast_slot_scratch(&state, info, slot);
                    }
                }
                // Establish the tied register before placing uses.  A dying
                // source may transfer its existing register; a live source must
                // be copied to another register so true SSA remains intact.
                if (tied_destination < info->operand_count && tied_source < info->operand_count && tied_destination != tied_source)
                {
                    MachineRef destination_ref = instruction->operands[tied_destination];
                    MachineRef source_ref = instruction->operands[tied_source];
                    u32 fixed_destination = machine_opcode_fixed_register(info, tied_destination);
                    if (physical_slots & (1u << tied_destination))
                    {
                        tied_target = machine_ref_payload(destination_ref);
                    }
                    else if (fixed_destination != UINT32_MAX)
                    {
                        tied_target = fixed_destination;
                    }
                    else if (constrained)
                    {
                        tied_target = machine_fast_slot_scratch(&state, info, tied_destination);
                    }
                    if (virtual_slots & (1u << tied_source))
                    {
                        u32 source_virtual = machine_ref_payload(source_ref);
                        tied_source_dies = machine_fast_source_dies_here(&state, source_virtual);
                        u32 source_location = state.virtual_register_locations[source_virtual];
                        if (tied_target == UINT32_MAX && source_location != UINT32_MAX && tied_source_dies)
                        {
                            tied_target = source_location;
                        }
                        if (tied_target == UINT32_MAX)
                        {
                            u64 forbidden = reserved_mask;
                            if (source_location != UINT32_MAX && !tied_source_dies)
                            {
                                forbidden |= 1ull << source_location;
                            }
                            u32 destination_virtual = (virtual_slots & (1u << tied_destination))
                                                           ? machine_ref_payload(destination_ref)
                                                           : source_virtual;
                            tied_target = machine_fast_pick(&state, machine_fast_class_mask(&state, destination_virtual), forbidden,
                                                            machine_fast_crosses_call(&state, destination_virtual));
                        }
                    }
                    else if ((physical_slots & (1u << tied_source)) && tied_target == UINT32_MAX)
                    {
                        tied_target = machine_ref_payload(source_ref);
                    }
                }
                // Uses first: constrained slots force their scratch register,
                // free slots keep or pick any register.
                for (u32 remaining = virtual_slots & use_slots; remaining; remaining &= remaining - 1u)
                {
                    u32 slot = machine_fast_first_set(remaining);
                    MachineRef ref = instruction->operands[slot];
                    u32 fixed = machine_opcode_fixed_register(info, slot);
                    if (slot == tied_source && tied_target != UINT32_MAX)
                    {
                        machine_fast_materialize_tied(&state, machine_ref_payload(ref), tied_target, tied_source_dies);
                        operand_registers[slot] = (u8)tied_target;
                        continue;
                    }
                    if (fixed == UINT32_MAX && machine_fast_pin_covers(&state, machine_ref_payload(ref), instruction_index))
                    {
                        operand_registers[slot] = (u8)state.pinned_registers[machine_ref_payload(ref)];
                        continue;
                    }
                    u32 target = fixed != UINT32_MAX                  ? fixed
                                 : constrained                         ? machine_fast_slot_scratch(&state, info, slot)
                                 : instruction->opcode == description->indirect_call_opcode ? (u32)description->indirect_call_register
                                                                                            : UINT32_MAX;
                    // A copy into a fixed physical register stages its source in
                    // that same register, so an argument sequence can never
                    // clobber an already-placed argument through a free pick;
                    // the float-argument bridge stages through RAX, which is
                    // never an argument register. The vector copy stages the
                    // same way: a reload landing on an already-staged ZMM
                    // argument register would destroy it.
                    if ((instruction->opcode == description->copy_opcode || instruction->opcode == description->vector_copy_opcode) && slot == 1 &&
                        (physical_slots & 1u))
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
                    if (use_define_slots & (1u << slot))
                    {
                        state.dirty[used] = true;
                    }
                }
                // Fixed physical destinations and encoder-internal clobbers
                // evict their owners before the instruction writes them.
                for (u32 remaining = physical_slots & define_slots; remaining; remaining &= remaining - 1u)
                {
                    u32 slot = machine_fast_first_set(remaining);
                    MachineRef ref = instruction->operands[slot];
                    u32 physical_register = machine_ref_payload(ref);
                    if (state.owner[physical_register] != UINT32_MAX)
                    {
                        // Overwriting a live owner: the spill store lands before
                        // this instruction writes the register, and any
                        // same-instruction use was already consumed.
                        machine_fast_spill(&state, physical_register);
                    }
                }
                // Early-clobber definitions cannot share a register with any
                // live input, even when the input's textual use was placed first.
                // Rebind the definition to a free register (or spill the input)
                // before its write executes.
                u32 early_clobber_slots = info->early_clobber_mask & (~use_slots | use_define_slots) & MACHINE_FAST_OPERAND_LANE_MASK;
                for (u32 remaining = early_clobber_slots; remaining; remaining &= remaining - 1u)
                {
                    u32 slot = machine_fast_first_set(remaining);
                    u32 destination = operand_registers[slot];
                    if (destination == UINT8_MAX)
                    {
                        continue;
                    }
                    for (u32 other = 0; other < info->operand_count; other += 1)
                    {
                        if (other == slot || operand_registers[other] == UINT8_MAX || operand_registers[other] != destination)
                        {
                            continue;
                        }
                        // A tied use is allowed to share the destination by
                        // definition; early-clobber only disqualifies the other
                        // inputs.  For an ordinary input, move the virtual
                        // destination instead of rebinding the input: the input
                        // register still contains the value for the instruction,
                        // while the destination's write lands in a disjoint file.
                        if (other == tied_source)
                        {
                            continue;
                        }
                        MachineRef destination_ref = instruction->operands[slot];
                        if (virtual_slots & (1u << slot))
                        {
                            machine_fast_spill(&state, destination);
                            u32 destination_virtual = machine_ref_payload(destination_ref);
                            u64 forbidden = reserved_mask | (1ull << destination);
                            for (u32 input_slot = 0; input_slot < info->operand_count; input_slot += 1)
                            {
                                if (input_slot != slot && operand_registers[input_slot] != UINT8_MAX)
                                {
                                    forbidden |= 1ull << operand_registers[input_slot];
                                }
                            }
                            u32 replacement = machine_fast_pick(&state, machine_fast_class_mask(&state, destination_virtual), forbidden,
                                                                machine_fast_crosses_call(&state, destination_virtual));
                            operand_registers[slot] = (u8)replacement;
                            break;
                        }
                    }
                }
                u64 clobber_mask = info->clobber_mask;
                for (u32 physical_register = 0; clobber_mask; physical_register += 1)
                {
                    if (clobber_mask & (1ull << physical_register))
                    {
                        clobber_mask &= ~(1ull << physical_register);
                        machine_fast_spill(&state, physical_register);
                    }
                }
                // Vreg definitions bind their register lazily dirty.
                for (u32 remaining = virtual_slots & define_slots; remaining; remaining &= remaining - 1u)
                {
                    u32 slot = machine_fast_first_set(remaining);
                    MachineRef ref = instruction->operands[slot];
                    if (machine_fast_pin_covers(&state, machine_ref_payload(ref), instruction_index))
                    {
                        operand_registers[slot] = (u8)state.pinned_registers[machine_ref_payload(ref)];
                        continue;
                    }
                    u32 target;
                    u32 fixed = machine_opcode_fixed_register(info, slot);
                    if (fixed != UINT32_MAX)
                    {
                        target = fixed;
                    }
                    else if (slot == tied_destination && tied_target != UINT32_MAX)
                    {
                        target = tied_target;
                    }
                    else if (constrained)
                    {
                        target = machine_fast_slot_scratch(&state, info, slot);
                    }
                    else if ((instruction->opcode == description->copy_opcode || instruction->opcode == description->vector_copy_opcode) && slot == 0 &&
                             (physical_slots & (1u << 1)) &&
                             ((description->allocatable_mask | description->vector_allocatable_mask) >> machine_ref_payload(instruction->operands[1])) & 1u)
                    {
                        // A capture of a fixed physical register (incoming
                        // argument, call result) binds in place: a free pick here
                        // could land on an argument register whose own capture
                        // has not executed yet and destroy it. Vector captures —
                        // incoming ZMM arguments, the ZMM0 call result — bind the
                        // same way in the vector file.
                        target = machine_ref_payload(instruction->operands[1]);
                    }
                    else if ((instruction->opcode == description->copy_opcode || instruction->opcode == description->vector_copy_opcode) && slot == 0 &&
                             (virtual_slots & (1u << 1)) &&
                             machine_ref_payload(instruction->operands[1]) != machine_ref_payload(ref) &&
                             operand_registers[1] != UINT8_MAX && !((machine_fast_pin_active(&state, instruction_index) >> operand_registers[1]) & 1u) &&
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
                        target = machine_fast_pick(&state, machine_fast_class_mask(&state, machine_ref_payload(ref)), reserved_mask,
                                                   machine_fast_crosses_call(&state, machine_ref_payload(ref)));
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
                    // Successors whose contract is already fixed — every
                    // backward edge, the self loop, and any statically cold
                    // target — get the state rewritten to their contract here.
                    // The edits stay at this terminator's BEFORE point, which
                    // the encoder emits ahead of the branch, and none of them
                    // can disturb the flags a conditional terminator just
                    // consumed. A forward, not-yet-cold successor takes the
                    // state as it stands: the snapshot recorded below is what
                    // its own contract construction conforms retroactively.
                    if (instruction->opcode == description->switch_opcode || instruction->opcode == MACHINE_X64_INDIRECT_BRANCH ||
                        instruction->opcode == MACHINE_A64_INDIRECT_BRANCH)
                    {
                        // Case targets and the default are all cold, so one
                        // conform to the empty contract serves every edge the
                        // dispatch fans out to.
                        machine_fast_conform_edge(&state, &edits, state.current_point, state.owner, state.dirty, state.virtual_register_locations,
                                                  machine_fast_empty_contract_owner, machine_fast_empty_contract_dirty, true);
                    }
                    else
                    {
                        for (u32 remaining = block_slots; remaining; remaining &= remaining - 1u)
                        {
                            u32 slot = machine_fast_first_set(remaining);
                            u32 successor = machine_ref_payload(instruction->operands[slot]);
                            MachineEdge const* successor_edge = 0;
                            for (u32 edge_index = 0; edge_index < function->edge_count; edge_index += 1)
                            {
                                MachineEdge const* candidate = function->edges + edge_index;
                                if (candidate->source_block == block_index && candidate->destination_block == successor)
                                {
                                    successor_edge = candidate;
                                    break;
                                }
                            }
                            if (successor <= block_index)
                            {
                                machine_fast_conform_edge_parameters(&state, &edits, state.current_point, successor_edge, state.owner, state.dirty,
                                                                     state.virtual_register_locations, contract_owner + (u64)successor * register_count,
                                                                     contract_dirty + (u64)successor * register_count, true);
                            }
                            else if (cold_blocks[successor])
                            {
                                machine_fast_conform_edge(&state, &edits, state.current_point, state.owner, state.dirty, state.virtual_register_locations,
                                                          machine_fast_empty_contract_owner, machine_fast_empty_contract_dirty, true);
                            }
                        }
                    }
                    // A return keeps its dirty values: the frame dies with it
                    // and there is no successor to see the slots.
                    u32* exit_owner = out_owner + (u64)block_index * register_count;
                    bool* exit_dirty = out_dirty + (u64)block_index * register_count;
                    for (u32 physical_register = 0; physical_register < register_count; physical_register += 1)
                    {
                        exit_owner[physical_register] = state.owner[physical_register];
                        exit_dirty[physical_register] = state.dirty[physical_register];
                    }
                }
            }
        }
        // The scan's own stream is point-sorted by construction; retroactive
        // conforms are not, so they sort and merge behind it — after the
        // terminator's own edits at the same point, which is the state every
        // retroactive repair was computed against.
        placement.edit_count = edits.total_count + retro_edits.total_count;
        placement.edits = arena_allocate(arena, MachineEdit, placement.edit_count ? placement.edit_count : 1);
        if (!retro_edits.total_count)
        {
            machine_stream_flatten(&edits, placement.edits);
        }
        else
        {
            MachineEdit* main_edits = arena_allocate(arena, MachineEdit, edits.total_count ? edits.total_count : 1);
            MachineEdit* retro_flat = arena_allocate(arena, MachineEdit, retro_edits.total_count);
            MachineEdit* retro_scratch = arena_allocate(arena, MachineEdit, retro_edits.total_count);
            machine_stream_flatten(&edits, main_edits);
            machine_stream_flatten(&retro_edits, retro_flat);
            machine_fast_sort_edits(retro_flat, retro_scratch, retro_edits.total_count);
            u32 main_cursor = 0;
            u32 retro_cursor = 0;
            for (u32 edit_index = 0; edit_index < placement.edit_count; edit_index += 1)
            {
                bool take_main = main_cursor < edits.total_count &&
                                 (retro_cursor >= retro_edits.total_count || main_edits[main_cursor].point <= retro_flat[retro_cursor].point);
                placement.edits[edit_index] = take_main ? main_edits[main_cursor++] : retro_flat[retro_cursor++];
            }
        }
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
            // Vector values keep dedicated slots below: the shared pool is
            // eight bytes per entry and a sixty-four-byte member would widen
            // every slot for a class that rarely spills.
            if (!slot_needed[register_index] || state.escapes[register_index] || definition_blocks[register_index] == UINT32_MAX ||
                function->virtual_registers[register_index].register_class == MACHINE_REGISTER_CLASS_VECTOR)
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
        // Zero where the callee-saved pushes precede the frame pointer (Win64):
        // the saves then live at the frame pointer's positive offsets, so no
        // frame byte below it is reserved for them. See the frame-size note
        // below — subtracting a save area that is not there sizes the allocation
        // short and buries the deepest slots under the stack pointer.
        u32 pool_base = description->saves_precede_frame_pointer ? 0u : 8 * push_count;
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
            if (function->virtual_registers[register_index].register_class == MACHINE_REGISTER_CLASS_VECTOR)
            {
                // Sixty-four-byte home at a sixteen-byte offset boundary,
                // mirroring the canonical frame layout's vector clamp; every
                // access is the unaligned vmovdqu8 either way.
                running = ((running + 15u) & ~15u) + 64u;
            }
            else
            {
                running += 8;
            }
            placement.virtual_register_offsets[register_index] = running;
        }
        for (u32 slot_index = 0; slot_index < function->stack_slot_count; slot_index += 1)
        {
            // The outgoing argument area is placed at the bottom of the frame
            // below, where a call's stack pointer lands on its base.
            if (function->outgoing_bytes && slot_index == function->outgoing_slot)
            {
                continue;
            }
            u32 slot_alignment = function->stack_slot_alignments ? function->stack_slot_alignments[slot_index] : 8;
            running = (running + function->stack_slot_sizes[slot_index] + slot_alignment - 1) & ~(slot_alignment - 1);
            placement.stack_slot_offsets[slot_index] = running;
        }
        placement.frame_size = ((running - pool_base + 15u) & ~15u) + ((push_count & 1u) ? 8u : 0u) + function->outgoing_bytes;
        if (function->outgoing_bytes)
        {
            placement.stack_slot_offsets[function->outgoing_slot] = placement.frame_size;
        }
        // The deepest offset handed out must sit inside the allocation plus the
        // save area that really lies below the frame pointer; see the same guard
        // in machine_stack_placement_build. An invalid placement falls back to the
        // canonical emitter, which is always sound.
        if (running <= placement.frame_size + pool_base)
        {
            // See machine_stack_placement_build: the saves the Win64 prologue pushes
            // before the frame pointer lie between it and the incoming arguments.
            placement.incoming_base = description->saves_precede_frame_pointer ? 8u * push_count : 0u;
            placement.valid = true;
        }
    }

    return placement;
}

MachineStackPlacement machine_fast_placement_build(Arena* arena, MachineFunction* function)
{
    return machine_fast_placement_build_pinned(arena, function, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}
