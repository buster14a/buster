#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/simd.h>

// Stage 9: pressure-aware scheduling over the machine IR. The selector emits
// rows in typed-IR walk order, so independent computations stretch their
// values across the whole block and the peak live set can exceed the register
// file where a better order would not. This pass reorders rows within
// over-pressured blocks to sink definitions toward their first use: a
// backward walk emits a unit as soon as everything depending on it is placed,
// choosing among ready units the one whose placement grows the live set
// least — its definitions demanded below leave the set, its operands not yet
// demanded enter it — so chains continue before new computations open, and a
// producer with several consumers is not dragged above all of them early. A
// plain depth-first LIFO walk was measured first and worsened the modeled
// pressure on almost every real function (2026-08-10n); the greedy selection
// is what makes the pass earn its name.
//
// The pass gates itself twice on modeled pressure, because its caller's
// acceptance — run placement on both forms, keep the cheaper — pays a whole
// second placement per candidate and placement is the expensive half of the
// machine path. Per block, each value's live window is its first to last
// touching row, the peak is the maximum number of overlapping windows, and
// only the excess above the allocatable file counts: pressure the file
// absorbs produces no traffic under any order. A function with no excess
// anywhere returns before a unit is built; a schedule that fails to lower
// the total excess is discarded before the copy is built. Blocks without
// excess keep their source order even inside a scheduled function, which
// also keeps the diff surface small.
//
// The pass builds a scheduled copy and never edits the input; instructions,
// virtual registers (definition points remapped), and line marks (remapped,
// re-sorted) are fresh arrays and every other side table is shared.
//
// Ordering obligations, all from machine.h/machine.c metadata:
// - Terminators stay last (they are barriers, and the only row a block's
//   scheduling emits first in the backward walk).
// - A FLAGS_USE row glues to the FLAGS_DEFINE row immediately above it into
//   one unit, so nothing can land between a compare and its setcc/branch.
//   The selector always emits the pair adjacent; a block where it did not is
//   left in source order rather than reasoned about.
// - CALL, SIDE_EFFECTS, and terminator rows, and any row naming a physical
//   register operand (argument staging, return-value moves) are barriers:
//   ordered against every other unit in both directions, which freezes call
//   sequences and everything the allocators special-case around them.
// - There is no alias analysis, so memory-touching rows keep their relative
//   order through a chain: loads, stores, frame ops, the aggregate copies,
//   and the incoming-argument reads. Atomics and fences are barriers by
//   their SIDE_EFFECTS attribute.
// - Rows that pass through the target's float state (XMM / vector registers)
//   chain the same way: the ABI bridges stage values that live across
//   neighboring rows, and the float compute rows clobber that state.
// - A virtual register with more than one defining row (x86-64 promoted
//   locals, two-address result chains) keeps every touching row in source
//   order; a single-definition value needs only its def-before-use edges.

// Barrier / memory / vector chain membership beyond what the attribute bits
// state. Stack-pointer adjustments and outgoing-argument pushes only appear
// inside call sequences and freeze with them.
BUSTER_GLOBAL_LOCAL bool machine_schedule_opcode_is_barrier(u16 opcode)
{
    bool result;
    switch (opcode)
    {
        case MACHINE_X64_PUSH_FRAME:
        case MACHINE_X64_PUSH_REGISTER:
        case MACHINE_X64_SUB_RSP:
        case MACHINE_X64_ADD_RSP:
        case MACHINE_X64_STACK_ALLOCATE:
        case MACHINE_A64_READ_SP:
            result = true;
            break;
        default:
            result = false;
            break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_schedule_info_is_barrier(u16 opcode, MachineOpcodeInfo const* info)
{
    // An opcode with no info is treated as a barrier: nothing may be reordered
    // across something the scheduler cannot describe.
    bool result = true;
    if (info)
    {
        MachineScheduleClass schedule_class = machine_opcode_schedule_class(info);
        MachineMemoryEffect memory_effect = machine_opcode_memory_effect(info);
        result = schedule_class == MACHINE_SCHEDULE_CLASS_BARRIER || schedule_class == MACHINE_SCHEDULE_CLASS_CALL ||
                 schedule_class == MACHINE_SCHEDULE_CLASS_ATOMIC || memory_effect == MACHINE_MEMORY_EFFECT_VOLATILE ||
                 memory_effect == MACHINE_MEMORY_EFFECT_ATOMIC || memory_effect == MACHINE_MEMORY_EFFECT_BARRIER ||
                 (info->attributes & (MACHINE_OPCODE_ATTRIBUTE_CALL | MACHINE_OPCODE_ATTRIBUTE_SIDE_EFFECTS | MACHINE_OPCODE_ATTRIBUTE_TERMINATOR)) != 0 ||
                 machine_schedule_opcode_is_barrier(opcode);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_schedule_opcode_is_memory(u16 opcode)
{
    bool result;
    switch (opcode)
    {
        case MACHINE_X64_LOAD_FRAME:
        case MACHINE_X64_STORE_FRAME8:
        case MACHINE_X64_STORE_FRAME16:
        case MACHINE_X64_STORE_FRAME32:
        case MACHINE_X64_STORE_FRAME64:
        case MACHINE_X64_LOAD_PTR8:
        case MACHINE_X64_LOAD_PTR16:
        case MACHINE_X64_LOAD_PTR32:
        case MACHINE_X64_LOAD_PTR64:
        case MACHINE_X64_STORE_PTR8:
        case MACHINE_X64_STORE_PTR16:
        case MACHINE_X64_STORE_PTR32:
        case MACHINE_X64_STORE_PTR64:
        case MACHINE_X64_COPY_FRAME_FROM_FRAME:
        case MACHINE_X64_COPY_FRAME_FROM_PTR:
        case MACHINE_X64_COPY_PTR_FROM_FRAME:
        case MACHINE_X64_LOAD_INCOMING:
        case MACHINE_A64_LOAD_FRAME:
        case MACHINE_A64_LOAD_FRAME32:
        case MACHINE_A64_STORE_FRAME8:
        case MACHINE_A64_STORE_FRAME16:
        case MACHINE_A64_STORE_FRAME32:
        case MACHINE_A64_STORE_FRAME64:
        case MACHINE_A64_LOAD_PTR8:
        case MACHINE_A64_LOAD_PTR16:
        case MACHINE_A64_LOAD_PTR32:
        case MACHINE_A64_LOAD_PTR64:
        case MACHINE_A64_STORE_PTR8:
        case MACHINE_A64_STORE_PTR16:
        case MACHINE_A64_STORE_PTR32:
        case MACHINE_A64_STORE_PTR64:
        case MACHINE_A64_COPY_FRAME_FROM_FRAME:
        case MACHINE_A64_COPY_FRAME_FROM_PTR:
        case MACHINE_A64_COPY_PTR_FROM_FRAME:
            result = true;
            break;
        default:
            result = false;
            break;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_schedule_info_is_memory(u16 opcode, MachineOpcodeInfo const* info)
{
    return machine_opcode_is_memory(info) || machine_schedule_opcode_is_memory(opcode);
}

BUSTER_GLOBAL_LOCAL bool machine_schedule_opcode_is_vector(u16 opcode)
{
    bool result;
    switch (opcode)
    {
        case MACHINE_X64_FARITH:
        case MACHINE_X64_FCMP_SET:
        case MACHINE_X64_CVT_F32_TO_F64:
        case MACHINE_X64_CVT_F64_TO_F32:
        case MACHINE_X64_CVT_I64_TO_F32:
        case MACHINE_X64_CVT_I64_TO_F64:
        case MACHINE_X64_CVT_F32_TO_I64:
        case MACHINE_X64_CVT_F64_TO_I64:
        case MACHINE_X64_CVT_U64_TO_F32:
        case MACHINE_X64_CVT_U64_TO_F64:
        case MACHINE_X64_CVT_F32_TO_U64:
        case MACHINE_X64_CVT_F64_TO_U64:
        case MACHINE_X64_MOVQ_TO_XMM:
        case MACHINE_X64_MOVQ_FROM_XMM:
        case MACHINE_A64_FMOV_TO_VEC:
        case MACHINE_A64_FMOV_FROM_VEC:
        case MACHINE_A64_FARITH:
        case MACHINE_A64_FCMP_SET:
        case MACHINE_A64_CVT_F32_TO_F64:
        case MACHINE_A64_CVT_F64_TO_F32:
        case MACHINE_A64_CVT_I64_TO_F32:
        case MACHINE_A64_CVT_I64_TO_F64:
        case MACHINE_A64_CVT_F32_TO_I64:
        case MACHINE_A64_CVT_F64_TO_I64:
        case MACHINE_A64_CVT_U64_TO_F32:
        case MACHINE_A64_CVT_U64_TO_F64:
        case MACHINE_A64_CVT_F32_TO_U64:
        case MACHINE_A64_CVT_F64_TO_U64:
        case MACHINE_A64_VLOAD_FRAME:
        case MACHINE_A64_VSTORE_FRAME:
        case MACHINE_A64_VARITH:
            result = true;
            break;
        default:
            result = false;
            break;
    }

    return result;
}
BUSTER_GLOBAL_LOCAL MachineRegisterClass machine_schedule_register_class(MachineFunction* function, u32 virtual_register)
{
    MachineRegisterClass result = MACHINE_REGISTER_CLASS_GENERAL;
    if (virtual_register < function->virtual_register_count)
    {
        MachineRegisterClass register_class = (MachineRegisterClass)function->virtual_registers[virtual_register].register_class;
        if (register_class < MACHINE_REGISTER_CLASS_COUNT && register_class != MACHINE_REGISTER_CLASS_NONE)
        {
            result = register_class;
        }
    }

    return result;
}

#define MACHINE_SCHEDULE_UNIT_BARRIER (1u << 0)
#define MACHINE_SCHEDULE_UNIT_MEMORY (1u << 1)
#define MACHINE_SCHEDULE_UNIT_VECTOR (1u << 2)

// Peak live-window overlap of one block in the walk order given by
// `block_rows_order` (global row indices, block-local length), clamped to
// the excess above the allocatable file. The epoch keys the per-register
// scratch so nothing is cleared between calls.
BUSTER_GLOBAL_LOCAL u32 machine_schedule_block_excess(MachineFunction* function, u32 block_row_count, u32 const* block_rows_order,
                                                      u32 const* class_capacities, u8 const* register_classes, u32 epoch, u32* touch_epochs,
                                                      u32* last_touches, s32* window_deltas)
{
    for (u32 offset = 0; offset < (block_row_count + 1u) * MACHINE_REGISTER_CLASS_COUNT; offset += 1)
    {
        window_deltas[offset] = 0;
    }
    for (u32 offset = 0; offset < block_row_count; offset += 1)
    {
        MachineInstruction* instruction = function->instructions + block_rows_order[offset];
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
            {
                continue;
            }
            u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
            u32 register_class = register_classes[virtual_register];
            s32* class_deltas = window_deltas + register_class * (block_row_count + 1u);
            if (touch_epochs[virtual_register] != epoch)
            {
                touch_epochs[virtual_register] = epoch;
                class_deltas[offset] += 1;
                class_deltas[offset + 1] -= 1;
            }
            else
            {
                // Extend the window: retract the previous close and close
                // after this row instead.
                class_deltas[last_touches[virtual_register] + 1] += 1;
                class_deltas[offset + 1] -= 1;
            }
            last_touches[virtual_register] = offset;
        }
    }
    u32 excess = 0;
    for (u32 register_class = 0; register_class < MACHINE_REGISTER_CLASS_COUNT; register_class += 1)
    {
        s32 live = 0;
        s32 peak = 0;
        s32* class_deltas = window_deltas + register_class * (block_row_count + 1u);
        for (u32 offset = 0; offset < block_row_count; offset += 1)
        {
            live += class_deltas[offset];
            peak = BUSTER_MAX(peak, live);
        }
        if ((u32)peak > class_capacities[register_class])
        {
            excess += (u32)peak - class_capacities[register_class];
        }
    }
    return excess;
}

// The bucket queue the block scheduler drives, gathered so growth evaluation
// and the push that depends on it can be ordinary functions. Everything here
// is per-block scratch owned by machine_schedule_function; the queue only
// borrows it for the length of one block.
typedef struct MachineScheduleQueue MachineScheduleQueue;
struct MachineScheduleQueue
{
    MachineFunction* function;
    u32 block_first_instruction;
    u32 const* unit_first_rows;
    u32 const* unit_row_counts;
    u32 const* demand_epochs;
    u32 demand_epoch;
    u32* unit_seqs;
    u32* entry_units;
    u32* entry_seqs;
    u32* entry_next;
    u32 entry_capacity;
    u32 entry_count;
    // Buckets span growth -16..+16 (a unit reads and writes at most eight
    // operand slots); the minimum bucket is exact because every growth change
    // re-pushes its unit eagerly.
    u32 bucket_heads[33];
    u32 minimum_bucket;
    bool overflow;
};

// How many more values stay live once this unit is placed: every operand it
// defines that something below already demands is retired, and every operand
// it uses that nothing below demands yet is newly born.
BUSTER_GLOBAL_LOCAL s32 machine_schedule_queue_growth(MachineScheduleQueue const* queue, u32 candidate_unit)
{
    s32 growth = 0;
    u32 candidate_first = queue->unit_first_rows[candidate_unit];
    for (u32 row = 0; row < queue->unit_row_counts[candidate_unit]; row += 1)
    {
        MachineInstruction* instruction = queue->function->instructions + queue->block_first_instruction + candidate_first + row;
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        for (u32 slot = 0; slot < info->operand_count; slot += 1)
        {
            if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
            {
                continue;
            }
            u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
            u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
            bool demanded = queue->demand_epochs[virtual_register] == queue->demand_epoch;
            growth -= (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && demanded;
            growth += (role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && !demanded;
        }
    }
    return growth;
}

BUSTER_GLOBAL_LOCAL void machine_schedule_queue_push(MachineScheduleQueue* queue, u32 pushed_unit)
{
    if (queue->entry_count == queue->entry_capacity)
    {
        queue->overflow = true;
        return;
    }
    u32 bucket = (u32)(BUSTER_MAX(BUSTER_MIN(machine_schedule_queue_growth(queue, pushed_unit), 16), -16) + 16);
    queue->unit_seqs[pushed_unit] += 1;
    queue->entry_units[queue->entry_count] = pushed_unit;
    queue->entry_seqs[queue->entry_count] = queue->unit_seqs[pushed_unit];
    queue->entry_next[queue->entry_count] = queue->bucket_heads[bucket];
    queue->bucket_heads[bucket] = queue->entry_count + 1;
    queue->entry_count += 1;
    queue->minimum_bucket = BUSTER_MIN(queue->minimum_bucket, bucket);
}

MachineScheduleResult machine_schedule_function(Arena* arena, MachineFunction* function)
{
    MachineScheduleResult result = {
        .function = *function,
    };
    u32 row_count = function->instruction_count;
    if (row_count && function->block_count && function->target)
    {
        u32 register_count = function->virtual_register_count;
        u32 class_capacities[MACHINE_REGISTER_CLASS_COUNT] = {0};
        // Keep the scheduler portable to MSVC, which has no
        // __builtin_popcountll; simd.h owns the fallback used elsewhere by the
        // allocator and parser.
        class_capacities[MACHINE_REGISTER_CLASS_GENERAL] = mask64_count((Mask64)function->target->allocatable_mask);
        class_capacities[MACHINE_REGISTER_CLASS_VECTOR] = mask64_count((Mask64)function->target->vector_allocatable_mask);
        u32 allocatable_count = BUSTER_MAX(class_capacities[MACHINE_REGISTER_CLASS_GENERAL], class_capacities[MACHINE_REGISTER_CLASS_VECTOR]);
        u32 maximum_block_rows = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            maximum_block_rows = BUSTER_MAX(maximum_block_rows, function->blocks[block_index].instruction_count);
        }
        // Every window opens at a distinct row, so a block's peak overlap never
        // exceeds its row count: a block the size of the register file or
        // smaller cannot have excess, and a function of only such blocks is
        // done before anything is allocated. This is the gate almost every
        // function leaves through.
        if (maximum_block_rows > allocatable_count)
        {
            TemporalArena scratch = scratch_begin(&arena, 1);
            u32* touch_epochs = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
            u32* last_touches = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
            for (u32 register_index = 0; register_index < register_count; register_index += 1)
            {
                touch_epochs[register_index] = 0;
            }
            s32* window_deltas = arena_allocate(scratch.arena, s32, (u64)(maximum_block_rows + 1u) * MACHINE_REGISTER_CLASS_COUNT);
            u32* order_scratch = arena_allocate(scratch.arena, u32, maximum_block_rows ? maximum_block_rows : 1);
            u8* register_classes = arena_allocate(scratch.arena, u8, register_count ? register_count : 1);
            for (u32 register_index = 0; register_index < register_count; register_index += 1)
            {
                register_classes[register_index] = (u8)machine_schedule_register_class(function, register_index);
            }
            // First gate, before any scheduling structure exists: the per-block
            // excess in source order. Blocks the file already covers are excluded
            // from scheduling outright, and a function with no excess anywhere is
            // done — no order can improve it under this model.
            u32* block_excesses = arena_allocate(scratch.arena, u32, function->block_count);
            u32 base_excess = 0;
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                MachineBlock* block = function->blocks + block_index;
                if (block->instruction_count <= allocatable_count)
                {
                    block_excesses[block_index] = 0;
                    continue;
                }
                for (u32 offset = 0; offset < block->instruction_count; offset += 1)
                {
                    order_scratch[offset] = block->first_instruction + offset;
                }
                block_excesses[block_index] = machine_schedule_block_excess(function, block->instruction_count, order_scratch, class_capacities, register_classes,
                                                                            block_index + 1, touch_epochs, last_touches, window_deltas);
                base_excess += block_excesses[block_index];
            }
            if (base_excess)
            {
                u32* definition_totals = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                for (u32 register_index = 0; register_index < register_count; register_index += 1)
                {
                    definition_totals[register_index] = 0;
                }
                // An opcode the table cannot describe leaves the function unscheduled;
                // `described` both ends this pass and skips everything built on it.
                bool described = true;
                for (u32 row = 0; row < row_count && described; row += 1)
                {
                    MachineInstruction* instruction = function->instructions + row;
                    MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                    if (!info)
                    {
                        described = false;
                        continue;
                    }
                    for (u32 slot = 0; slot < info->operand_count; slot += 1)
                    {
                        if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                        {
                            continue;
                        }
                        u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                        definition_totals[machine_ref_payload(instruction->operands[slot])] +=
                            role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;
                    }
                }
                // Per-block scheduling scratch, sized once at the widest block. Every
                // array is indexed by block-local unit index; edge capacity is a hard
                // bound — each row contributes at most four operand edges, and each unit
                // at most a barrier-in edge, one appearance in a barrier's flush list,
                // and one link in each of the memory and vector chains.
                u32 edge_capacity = 8 * maximum_block_rows + 8;
                u32* unit_first_rows = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u32* unit_row_counts = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u8* unit_flags = arena_allocate(scratch.arena, u8, maximum_block_rows);
                u32* edge_sources = arena_allocate(scratch.arena, u32, edge_capacity);
                u32* edge_destinations = arena_allocate(scratch.arena, u32, edge_capacity);
                u32* successor_remaining = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u32* predecessor_offsets = arena_allocate(scratch.arena, u32, maximum_block_rows + 1);
                u32* predecessor_lists = arena_allocate(scratch.arena, u32, edge_capacity);
                u32* flush_list = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u32* newly_ready = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u32* demand_epochs = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                for (u32 register_index = 0; register_index < register_count; register_index += 1)
                {
                    demand_epochs[register_index] = 0;
                }
                // The ready queue. Growth values are small integers, so the queue is a
                // bucket per growth with LIFO entries; entries go stale by sequence
                // number instead of being unlinked, and a demand transition re-pushes
                // every ready unit it touches with a fresh growth. Pushes are one per
                // unit becoming ready plus one per (transition, toucher) pair; a
                // single-definition value transitions at most twice, a multi-definition
                // one up to once per touch, which the per-value toucher cap below
                // bounds, and a block that overflows the capacity anyway falls back to
                // source order rather than scheduling on a partial queue.
                u32 entry_capacity = 10 * maximum_block_rows + 16;
                u32* entry_units = arena_allocate(scratch.arena, u32, entry_capacity);
                u32* entry_seqs = arena_allocate(scratch.arena, u32, entry_capacity);
                u32* entry_next = arena_allocate(scratch.arena, u32, entry_capacity);
                u32* unit_seqs = arena_allocate(scratch.arena, u32, maximum_block_rows);
                u8* unit_states = arena_allocate(scratch.arena, u8, maximum_block_rows);
                // Touch lists: for every value a block touches, the units touching it,
                // for the eager growth updates above. Slots compact the touched values.
                u32* vreg_slots = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                u32* vreg_slot_epochs = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                for (u32 register_index = 0; register_index < register_count; register_index += 1)
                {
                    vreg_slot_epochs[register_index] = 0;
                }
                u32 touch_capacity = 4 * maximum_block_rows + 4;
                u32* slot_touch_offsets = arena_allocate(scratch.arena, u32, touch_capacity + 1);
                u32* touch_units = arena_allocate(scratch.arena, u32, touch_capacity);
                // Per-virtual-register block-local dependence tracking, epoch-stamped:
                // the last defining unit for single-definition values, the last touching
                // unit for multi-definition ones.
                u32* register_units = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                u32* register_epochs = arena_allocate(scratch.arena, u32, register_count ? register_count : 1);
                for (u32 register_index = 0; register_index < register_count; register_index += 1)
                {
                    register_epochs[register_index] = 0;
                }
                u32* new_rows = arena_allocate(scratch.arena, u32, row_count);
                for (u32 row = 0; row < row_count; row += 1)
                {
                    new_rows[row] = row;
                }
                bool moved = false;
                for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
                {
                    MachineBlock* block = function->blocks + block_index;
                    u32 block_rows = block->instruction_count;
                    if (!block_excesses[block_index])
                    {
                        continue;
                    }
                    u32 epoch = block_index + 1;
                    // Unit construction: one unit per row, except that a FLAGS_USE row
                    // joins the unit ending immediately above it when that unit's last
                    // row defines flags. Anything else consuming flags means the
                    // producer is not adjacent; the block keeps its source order.
                    u32 unit_count = 0;
                    bool block_supported = true;
                    for (u32 offset = 0; offset < block_rows && block_supported; offset += 1)
                    {
                        MachineInstruction* instruction = function->instructions + block->first_instruction + offset;
                        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                        u8 flags = 0;
                        flags |= machine_schedule_info_is_barrier(instruction->opcode, info) ? MACHINE_SCHEDULE_UNIT_BARRIER : 0;
                        flags |= machine_schedule_info_is_memory(instruction->opcode, info) ? MACHINE_SCHEDULE_UNIT_MEMORY : 0;
                        flags |= machine_opcode_schedule_class(info) == MACHINE_SCHEDULE_CLASS_VECTOR || machine_schedule_opcode_is_vector(instruction->opcode)
                                     ? MACHINE_SCHEDULE_UNIT_VECTOR
                                     : 0;
                        for (u32 slot = 0; slot < info->operand_count; slot += 1)
                        {
                            flags |= machine_ref_kind(instruction->operands[slot]) == MACHINE_REF_PHYSICAL_REGISTER ? MACHINE_SCHEDULE_UNIT_BARRIER : 0;
                        }
                        if (info->attributes & MACHINE_OPCODE_ATTRIBUTE_FLAGS_USE)
                        {
                            MachineOpcodeInfo const* above = offset ? machine_opcode_info(function->instructions[block->first_instruction + offset - 1].opcode) : 0;
                            if (!above || !(above->attributes & MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE))
                            {
                                block_supported = false;
                                break;
                            }
                            unit_row_counts[unit_count - 1] += 1;
                            unit_flags[unit_count - 1] |= flags;
                            continue;
                        }
                        unit_first_rows[unit_count] = offset;
                        unit_row_counts[unit_count] = 1;
                        unit_flags[unit_count] = flags;
                        unit_count += 1;
                    }
                    if (!block_supported || unit_count <= 2)
                    {
                        continue;
                    }
                    // Edge construction, one forward walk. An edge (source, destination)
                    // means the source unit stays above the destination.
                    u32 edge_count = 0;
                    u32 last_barrier = UINT32_MAX;
                    u32 last_memory = UINT32_MAX;
                    u32 last_vector = UINT32_MAX;
                    u32 flush_count = 0;
                    for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                    {
                        u8 flags = unit_flags[unit_index];
                        if (last_barrier != UINT32_MAX)
                        {
                            edge_sources[edge_count] = last_barrier;
                            edge_destinations[edge_count] = unit_index;
                            edge_count += 1;
                        }
                        if (flags & MACHINE_SCHEDULE_UNIT_BARRIER)
                        {
                            for (u32 flush_index = 0; flush_index < flush_count; flush_index += 1)
                            {
                                edge_sources[edge_count] = flush_list[flush_index];
                                edge_destinations[edge_count] = unit_index;
                                edge_count += 1;
                            }
                            flush_count = 0;
                            last_barrier = unit_index;
                            // The chains restart here: ordering across the barrier is
                            // already transitive through the flush and barrier-in edges.
                            last_memory = UINT32_MAX;
                            last_vector = UINT32_MAX;
                        }
                        else
                        {
                            flush_list[flush_count] = unit_index;
                            flush_count += 1;
                            if (flags & MACHINE_SCHEDULE_UNIT_MEMORY)
                            {
                                if (last_memory != UINT32_MAX)
                                {
                                    edge_sources[edge_count] = last_memory;
                                    edge_destinations[edge_count] = unit_index;
                                    edge_count += 1;
                                }
                                last_memory = unit_index;
                            }
                            if (flags & MACHINE_SCHEDULE_UNIT_VECTOR)
                            {
                                if (last_vector != UINT32_MAX)
                                {
                                    edge_sources[edge_count] = last_vector;
                                    edge_destinations[edge_count] = unit_index;
                                    edge_count += 1;
                                }
                                last_vector = unit_index;
                            }
                        }
                        u32 first_row = unit_first_rows[unit_index];
                        for (u32 unit_row = 0; unit_row < unit_row_counts[unit_index]; unit_row += 1)
                        {
                            MachineInstruction* instruction = function->instructions + block->first_instruction + first_row + unit_row;
                            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                            for (u32 slot = 0; slot < info->operand_count; slot += 1)
                            {
                                if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                                {
                                    continue;
                                }
                                u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
                                u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                                bool defines = role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;
                                bool tracked = register_epochs[virtual_register] == epoch;
                                u32 tracked_unit = tracked ? register_units[virtual_register] : UINT32_MAX;
                                if (definition_totals[virtual_register] > 1)
                                {
                                    // Multi-definition value: every touch stays in
                                    // source order.
                                    if (tracked && tracked_unit != unit_index)
                                    {
                                        edge_sources[edge_count] = tracked_unit;
                                        edge_destinations[edge_count] = unit_index;
                                        edge_count += 1;
                                    }
                                    register_units[virtual_register] = unit_index;
                                    register_epochs[virtual_register] = epoch;
                                }
                                else
                                {
                                    // Single definition: uses order only against it. A
                                    // use with no in-block definition reads a live-in
                                    // value and needs no edge.
                                    if (!defines && tracked && tracked_unit != unit_index)
                                    {
                                        edge_sources[edge_count] = tracked_unit;
                                        edge_destinations[edge_count] = unit_index;
                                        edge_count += 1;
                                    }
                                    if (defines)
                                    {
                                        register_units[virtual_register] = unit_index;
                                        register_epochs[virtual_register] = epoch;
                                    }
                                }
                            }
                        }
                    }
                    // Backward list scheduling. The terminator is the only successor-free
                    // unit — every other unit reaches it through the barrier chain — so
                    // the walk starts there and fills the block bottom-up. Selection is
                    // pressure-greedy: among ready units, take the one whose placement
                    // grows the live set least — a unit's definitions demanded below it
                    // leave the set, its operands not yet demanded enter it — so chains
                    // continue before new computations open, and a producer with several
                    // consumers is not dragged above all of them early. Ties go to the
                    // most recently pushed entry in the winning bucket, which is
                    // depth-first chain-following on tree shapes and recency of demand
                    // everywhere else.
                    for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                    {
                        successor_remaining[unit_index] = 0;
                        predecessor_offsets[unit_index] = 0;
                    }
                    predecessor_offsets[unit_count] = 0;
                    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
                    {
                        successor_remaining[edge_sources[edge_index]] += 1;
                        predecessor_offsets[edge_destinations[edge_index] + 1] += 1;
                    }
                    for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                    {
                        predecessor_offsets[unit_index + 1] += predecessor_offsets[unit_index];
                    }
                    for (u32 edge_index = 0; edge_index < edge_count; edge_index += 1)
                    {
                        predecessor_lists[predecessor_offsets[edge_destinations[edge_index]]] = edge_sources[edge_index];
                        predecessor_offsets[edge_destinations[edge_index]] += 1;
                    }
                    for (u32 unit_index = unit_count; unit_index > 0; unit_index -= 1)
                    {
                        predecessor_offsets[unit_index] = predecessor_offsets[unit_index - 1];
                    }
                    predecessor_offsets[0] = 0;
                    // Touch lists for the eager growth updates: for every value the
                    // block touches, the units touching it, in compact slots.
                    u32 slot_count = 0;
                    slot_touch_offsets[0] = 0;
                    for (u32 pass = 0; pass < 2; pass += 1)
                    {
                        for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                        {
                            u32 first_row = unit_first_rows[unit_index];
                            for (u32 unit_row = 0; unit_row < unit_row_counts[unit_index]; unit_row += 1)
                            {
                                MachineInstruction* instruction = function->instructions + block->first_instruction + first_row + unit_row;
                                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                                {
                                    if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                                    {
                                        continue;
                                    }
                                    u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
                                    if (vreg_slot_epochs[virtual_register] != 2 * epoch + pass)
                                    {
                                        vreg_slot_epochs[virtual_register] = 2 * epoch + pass;
                                        vreg_slots[virtual_register] = pass ? vreg_slots[virtual_register] : slot_count;
                                        slot_count += pass ? 0 : 1;
                                        if (!pass)
                                        {
                                            slot_touch_offsets[slot_count] = 0;
                                        }
                                    }
                                    u32 value_slot = vreg_slots[virtual_register];
                                    if (pass)
                                    {
                                        touch_units[slot_touch_offsets[value_slot]] = unit_index;
                                        slot_touch_offsets[value_slot] += 1;
                                    }
                                    else
                                    {
                                        slot_touch_offsets[value_slot + 1] += 1;
                                    }
                                }
                            }
                        }
                        if (!pass)
                        {
                            for (u32 slot_index = 0; slot_index < slot_count; slot_index += 1)
                            {
                                slot_touch_offsets[slot_index + 1] += slot_touch_offsets[slot_index];
                            }
                        }
                    }
                    for (u32 slot_index = slot_count; slot_index > 0; slot_index -= 1)
                    {
                        slot_touch_offsets[slot_index] = slot_touch_offsets[slot_index - 1];
                    }
                    slot_touch_offsets[0] = 0;
                    for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                    {
                        unit_seqs[unit_index] = 0;
                        unit_states[unit_index] = 0;
                    }
                    u32 demand_epoch = function->block_count * 2 + block_index + 1;
                    MachineScheduleQueue queue = {
                        .function = function,
                        .block_first_instruction = block->first_instruction,
                        .unit_first_rows = unit_first_rows,
                        .unit_row_counts = unit_row_counts,
                        .demand_epochs = demand_epochs,
                        .demand_epoch = demand_epoch,
                        .unit_seqs = unit_seqs,
                        .entry_units = entry_units,
                        .entry_seqs = entry_seqs,
                        .entry_next = entry_next,
                        .entry_capacity = entry_capacity,
                        .minimum_bucket = 33,
                    };
                    for (u32 unit_index = 0; unit_index < unit_count; unit_index += 1)
                    {
                        if (!successor_remaining[unit_index])
                        {
                            unit_states[unit_index] = 1;
                            machine_schedule_queue_push(&queue, unit_index);
                        }
                    }
                    u32 out_row = block_rows;
                    u32 emitted_units = 0;
                    while (queue.minimum_bucket < 33 && !queue.overflow)
                    {
                        u32 entry_slot = queue.bucket_heads[queue.minimum_bucket];
                        if (!entry_slot)
                        {
                            queue.minimum_bucket += 1;
                            continue;
                        }
                        queue.bucket_heads[queue.minimum_bucket] = entry_next[entry_slot - 1];
                        u32 unit_index = entry_units[entry_slot - 1];
                        if (unit_states[unit_index] != 1 || entry_seqs[entry_slot - 1] != unit_seqs[unit_index])
                        {
                            continue;
                        }
                        unit_states[unit_index] = 2;
                        u32 unit_rows = unit_row_counts[unit_index];
                        out_row -= unit_rows;
                        for (u32 unit_row = 0; unit_row < unit_rows; unit_row += 1)
                        {
                            new_rows[block->first_instruction + unit_first_rows[unit_index] + unit_row] = block->first_instruction + out_row + unit_row;
                        }
                        // Update demand: placed definitions are satisfied, operands are
                        // now needed by everything below; a value both defined and used
                        // here stays demanded for its producer above. Every transition
                        // re-pushes the ready units touching the value, so their queue
                        // positions stay exact.
                        u32 emitted_first = unit_first_rows[unit_index];
                        for (u32 direction = 0; direction < 2; direction += 1)
                        {
                            for (u32 unit_row = 0; unit_row < unit_rows; unit_row += 1)
                            {
                                MachineInstruction* instruction = function->instructions + block->first_instruction + emitted_first + unit_row;
                                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                                for (u32 slot = 0; slot < info->operand_count; slot += 1)
                                {
                                    if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                                    {
                                        continue;
                                    }
                                    u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
                                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                                    bool defines = role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;
                                    bool uses = role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE;
                                    bool transition = direction ? uses && demand_epochs[virtual_register] != demand_epoch
                                                                : defines && demand_epochs[virtual_register] == demand_epoch;
                                    if (!transition)
                                    {
                                        continue;
                                    }
                                    demand_epochs[virtual_register] = direction ? demand_epoch : 0;
                                    // A widely-touched value — a hot promoted local —
                                    // transitions once per touch, and refreshing all its
                                    // touchers each time is quadratic; past the cap its
                                    // touchers keep their last pushed growth, a
                                    // heuristic staleness the excess gate still checks.
                                    u32 value_slot = vreg_slots[virtual_register];
                                    if (slot_touch_offsets[value_slot + 1] - slot_touch_offsets[value_slot] > 16)
                                    {
                                        continue;
                                    }
                                    for (u32 touch_index = slot_touch_offsets[value_slot]; touch_index < slot_touch_offsets[value_slot + 1]; touch_index += 1)
                                    {
                                        if (unit_states[touch_units[touch_index]] == 1)
                                        {
                                            machine_schedule_queue_push(&queue, touch_units[touch_index]);
                                        }
                                    }
                                }
                            }
                        }
                        emitted_units += 1;
                        u32 newly_ready_count = 0;
                        for (u32 list_index = predecessor_offsets[unit_index]; list_index < predecessor_offsets[unit_index + 1]; list_index += 1)
                        {
                            u32 predecessor = predecessor_lists[list_index];
                            successor_remaining[predecessor] -= 1;
                            if (!successor_remaining[predecessor])
                            {
                                newly_ready[newly_ready_count] = predecessor;
                                newly_ready_count += 1;
                            }
                        }
                        for (u32 push_index = 0; push_index < newly_ready_count; push_index += 1)
                        {
                            unit_states[newly_ready[push_index]] = 1;
                            machine_schedule_queue_push(&queue, newly_ready[push_index]);
                        }
                    }
                    // A cycle would leave units unemitted; the dependence relation is
                    // acyclic by construction, but an unexpected shape falls back to
                    // source order rather than corrupting the block.
                    if (emitted_units != unit_count || out_row != 0)
                    {
                        for (u32 offset = 0; offset < block_rows; offset += 1)
                        {
                            new_rows[block->first_instruction + offset] = block->first_instruction + offset;
                        }
                        continue;
                    }
                    for (u32 offset = 0; offset < block_rows; offset += 1)
                    {
                        moved |= new_rows[block->first_instruction + offset] != block->first_instruction + offset;
                    }
                }
                if (moved)
                {
                    // Second gate: the scheduled order must lower the total excess, or the
                    // caller's second placement run is not worth paying for. Only scheduled
                    // blocks can differ from their already-computed source-order excess.
                    u32 scheduled_excess = 0;
                    u32 compared_excess = 0;
                    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
                    {
                        MachineBlock* block = function->blocks + block_index;
                        if (!block_excesses[block_index])
                        {
                            continue;
                        }
                        compared_excess += block_excesses[block_index];
                        for (u32 offset = 0; offset < block->instruction_count; offset += 1)
                        {
                            order_scratch[new_rows[block->first_instruction + offset] - block->first_instruction] = block->first_instruction + offset;
                        }
                        scheduled_excess += machine_schedule_block_excess(function, block->instruction_count, order_scratch, class_capacities, register_classes,
                                                                          function->block_count + block_index + 1, touch_epochs, last_touches, window_deltas);
                    }
                    if (scheduled_excess < compared_excess)
                    {
                        MachineInstruction* scheduled_instructions = arena_allocate(arena, MachineInstruction, row_count);
                        for (u32 row = 0; row < row_count; row += 1)
                        {
                            scheduled_instructions[new_rows[row]] = function->instructions[row];
                        }
                        MachineVirtualRegister* scheduled_registers = arena_allocate(arena, MachineVirtualRegister, register_count ? register_count : 1);
                        for (u32 register_index = 0; register_index < register_count; register_index += 1)
                        {
                            scheduled_registers[register_index] = function->virtual_registers[register_index];
                            MachinePoint definition = scheduled_registers[register_index].definition_point;
                            if (definition != MACHINE_POINT_INVALID && machine_point_instruction(definition) < row_count)
                            {
                                scheduled_registers[register_index].definition_point =
                                    machine_point_make(new_rows[machine_point_instruction(definition)], machine_point_phase(definition));
                            }
                        }
                        MachineLineMark* scheduled_marks = arena_allocate(arena, MachineLineMark, function->line_mark_count ? function->line_mark_count : 1);
                        for (u32 mark_index = 0; mark_index < function->line_mark_count; mark_index += 1)
                        {
                            scheduled_marks[mark_index] = function->line_marks[mark_index];
                            if (scheduled_marks[mark_index].row < row_count)
                            {
                                scheduled_marks[mark_index].row = new_rows[scheduled_marks[mark_index].row];
                            }
                        }
                        // The line consumer walks marks in ascending row order; scheduling
                        // shuffles them within each block, so restore the order. Insertion sort:
                        // the sequence is block-locally shuffled but globally almost sorted.
                        for (u32 mark_index = 1; mark_index < function->line_mark_count; mark_index += 1)
                        {
                            MachineLineMark mark = scheduled_marks[mark_index];
                            u32 shift = mark_index;
                            while (shift && scheduled_marks[shift - 1].row > mark.row)
                            {
                                scheduled_marks[shift] = scheduled_marks[shift - 1];
                                shift -= 1;
                            }
                            scheduled_marks[shift] = mark;
                        }
                        result.function.instructions = scheduled_instructions;
                        result.function.virtual_registers = scheduled_registers;
                        result.function.line_marks = scheduled_marks;
                        result.moved = true;
                    }
                }
            }

            // One release for every way out of the gates above; each of them
            // used to carry its own scratch_end beside its own return, and the
            // no-excess gate would now leak without this sitting outside it.
            scratch_end(scratch);
        }
    }

    return result;
}
