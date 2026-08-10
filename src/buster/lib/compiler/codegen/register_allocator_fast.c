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
    // Pinned registers, owned by the QUALITY pass and invisible to the
    // local scan. `pin_active_masks` holds one register mask per
    // instruction scoping each reservation to its value's live span; null
    // means `pinned_mask` holds everywhere.
    u32 const* pinned_registers;
    u64 pinned_mask;
    u64 const* pin_active_masks;
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

// The registers pinned values own at this instruction, which the local
// scan must stand clear of. Outside every pinned span the register is an
// ordinary member of the pool.
BUSTER_GLOBAL_LOCAL u64 machine_fast_pin_active(MachineFastState* state, u32 instruction_index)
{
    return state->pin_active_masks ? state->pin_active_masks[instruction_index] : state->pinned_mask;
}

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
    // Pass 2: place every contract value not already in its register.
    u64 pending = 0;
    for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
    {
        if (contract_owner[contract_register] != UINT32_MAX && owner[contract_register] != contract_owner[contract_register])
        {
            pending |= 1ull << contract_register;
        }
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
    u32 best = UINT32_MAX;
    u32 best_age = UINT32_MAX;
    u32 dead = UINT32_MAX;
    u32 free_other = UINT32_MAX;
    u64 preferred_class = prefers_callee_saved ? state->description->callee_saved_mask : ~state->description->callee_saved_mask;
    for (u32 physical_register = 0; physical_register < state->active_register_count; physical_register += 1)
    {
        if (!(candidates & (1ull << physical_register)))
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
    prepass.interval_starts = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.interval_ends = arena_allocate(arena, u32, register_count ? register_count : 1);
    prepass.disqualified = arena_allocate(arena, u8, register_count ? register_count : 1);
    prepass.predecessor_offsets = arena_allocate(arena, u32, function->block_count + 1);
    prepass.cold_blocks = arena_allocate(arena, u8, function->block_count ? function->block_count : 1);
    if (!description)
    {
        return prepass;
    }
    u8* definition_seen = arena_allocate(arena, u8, register_count ? register_count : 1);
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
    for (u32 block_index = 0; block_index <= function->block_count; block_index += 1)
    {
        prepass.predecessor_offsets[block_index] = 0;
    }
    u32 backward_edge_count = 0;
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
            bool constrained = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) != 0;
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                MachineRefKind kind = machine_ref_kind(ref);
                if (kind == MACHINE_REF_BLOCK)
                {
                    u32 successor = machine_ref_payload(ref);
                    prepass.predecessor_offsets[successor + 1] += 1;
                    backward_edge_count += successor <= block_index;
                    continue;
                }
                if (kind != MACHINE_REF_VIRTUAL_REGISTER)
                {
                    continue;
                }
                u32 virtual_register = machine_ref_payload(ref);
                prepass.interval_starts[virtual_register] = BUSTER_MIN(prepass.interval_starts[virtual_register], instruction_index);
                prepass.interval_ends[virtual_register] = BUSTER_MAX(prepass.interval_ends[virtual_register], instruction_index);
                prepass.disqualified[virtual_register] |= constrained ? 1u : 0u;
                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
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
                }
            }
        }
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
        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
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
                else if (kind == MACHINE_REF_VIRTUAL_REGISTER)
                {
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    if ((role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) &&
                        prepass.definition_blocks[machine_ref_payload(ref)] != block_index)
                    {
                        prepass.escapes[machine_ref_payload(ref)] = 1;
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
    return prepass;
}

// `pinned_registers` holds a physical register per virtual register that
// owns it, or UINT32_MAX; `pinned_mask` collects them; `pin_active_masks`
// scopes each reservation to its value's span, or reserves `pinned_mask`
// whole-function when null. The local scan never picks, binds, spills, or
// reloads a pinned value: its operands simply name the register, and the
// register rejoins the pool wherever no span covers the instruction. FAST
// passes none of the three.
MachineStackPlacement machine_fast_placement_build_pinned(Arena* arena, MachineFunction* function, u32 const* pinned_registers, u64 pinned_mask,
                                                          u64 const* pin_active_masks)
{
    MachineFastPrepass prepass = machine_fast_prepass_build(arena, function);
    return machine_fast_placement_build_prepassed(arena, function, &prepass, pinned_registers, pinned_mask, pin_active_masks);
}

MachineStackPlacement machine_fast_placement_build_prepassed(Arena* arena, MachineFunction* function, MachineFastPrepass const* prepass,
                                                             u32 const* pinned_registers, u64 pinned_mask, u64 const* pin_active_masks)
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
    if (!prepass->valid)
    {
        return placement;
    }
    MachineBuilderStream edits;
    machine_stream_initialize(&edits, sizeof(MachineEdit));
    // Only the callee-saved pins cost a prologue save; a caller-saved pin
    // is exactly why QUALITY hands them out where a span crosses no call.
    placement.callee_saved_mask |= pinned_mask & description->callee_saved_mask;
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
    for (u64 entry_index = 0; entry_index < (u64)function->block_count * register_count; entry_index += 1)
    {
        contract_owner[entry_index] = UINT32_MAX;
        contract_dirty[entry_index] = false;
        out_owner[entry_index] = UINT32_MAX;
        out_dirty[entry_index] = false;
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
                    entry_owner[contract_register] = value;
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
                        if (edge_owner[contract_register] == value)
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
                machine_fast_conform_edge(&state, &retro_edits, machine_point_make(terminator_index, MACHINE_POINT_BEFORE),
                                          out_owner + (u64)predecessor * register_count, out_dirty + (u64)predecessor * register_count, 0, entry_owner,
                                          entry_dirty, terminator_targets == 1 && predecessor_terminator->opcode != description->switch_opcode);
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
        // free victims through the eviction's dead-owner preference.
        for (u32 contract_register = 0; contract_register < register_count; contract_register += 1)
        {
            u32 value = entry_owner[contract_register];
            if (value != UINT32_MAX)
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
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (!info)
            {
                return placement;
            }
            state.current_point = machine_point_make(instruction_index, MACHINE_POINT_BEFORE);
            state.uses_consumed = false;
            // A register entering a pinned span belongs to its value from
            // here, so any local owner leaves first. At a block head the
            // contract construction already refused these registers, so
            // the eviction only fires where a span opens mid-block — at a
            // pinned value's definition — which is a point that cannot
            // re-execute without passing the block head again.
            u64 pins_opening = machine_fast_pin_active(&state, instruction_index);
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
            bool constrained = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) != 0;
            bool is_call = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_CALL) != 0;
            bool is_terminator = (info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR) != 0;
            // Fixed physical operands and the constrained layout vacate
            // their registers first so uses cannot land on them.
            u64 reserved_mask = info->clobber_mask;
            for (u32 slot = 0; slot < info->operand_count; slot += 1)
            {
                MachineRef ref = instruction->operands[slot];
                if (machine_ref_kind(ref) == MACHINE_REF_PHYSICAL_REGISTER)
                {
                    reserved_mask |= 1ull << machine_ref_payload(ref);
                }
                else if (constrained && machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER)
                {
                    reserved_mask |= 1ull << description->slot_scratch[slot];
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
                else if ((instruction->opcode == description->copy_opcode || instruction->opcode == description->vector_copy_opcode) && slot == 0 &&
                         machine_ref_kind(instruction->operands[1]) == MACHINE_REF_VIRTUAL_REGISTER &&
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
                if (instruction->opcode == description->switch_opcode)
                {
                    // Case targets and the default are all cold, so one
                    // conform to the empty contract serves every edge the
                    // dispatch fans out to.
                    machine_fast_conform_edge(&state, &edits, state.current_point, state.owner, state.dirty, state.virtual_register_locations,
                                              machine_fast_empty_contract_owner, machine_fast_empty_contract_dirty, true);
                }
                else
                {
                    for (u32 slot = 0; slot < info->operand_count; slot += 1)
                    {
                        if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_BLOCK)
                        {
                            continue;
                        }
                        u32 successor = machine_ref_payload(instruction->operands[slot]);
                        if (successor <= block_index)
                        {
                            machine_fast_conform_edge(&state, &edits, state.current_point, state.owner, state.dirty, state.virtual_register_locations,
                                                      contract_owner + (u64)successor * register_count, contract_dirty + (u64)successor * register_count,
                                                      true);
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
    return machine_fast_placement_build_pinned(arena, function, 0, 0, 0);
}
