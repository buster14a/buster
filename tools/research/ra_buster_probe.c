// Isolated allocation-validation adapter for real Buster placements.
// ra_probe_function constructs full-width scalar SSA fixtures; ra_probe_lower
// translates only the explicitly admitted MachineInstruction/Edit subset.
// It does not use allocator liveness, owner, pin or rematerialization tables.
// Standalone diagnostic only: no production registration or policy changes.
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/os.h>
#include "ra_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ProgramState* program_state;

BUSTER_GLOBAL_LOCAL u64 ra_probe_now(void)
{
    struct timespec instant;
    if (clock_gettime(CLOCK_MONOTONIC, &instant) != 0)
    {
        abort();
    }
    u64 result = (u64)instant.tv_sec * 1000000000ull + (u64)instant.tv_nsec;
    return result;
}

typedef struct RaProbeLower RaProbeLower;
struct RaProbeLower
{
    RaCheckProgram program;
    RaCheckLocation* locations;
    RaCheckRow* rows;
    RaCheckAllocatedRow* allocated;
    RaCheckEdit* edits;
    RaCheckBlock block;
    RaCheckStatus status;
    char const* reason;
    u32 saved_area;
};

BUSTER_GLOBAL_LOCAL u8 ra_probe_location(RaProbeLower* lower, bool stack, u32 index)
{
    u8 result = RA_CHECK_NONE;
    bool admitted = stack ? index >= lower->saved_area + 8u && index <= lower->program.frame_size
                          : index < 16u && index != MACHINE_X64_RSP && index != MACHINE_X64_RBP;
    if (!admitted)
    {
        lower->status = RA_CHECK_INVALID;
        lower->reason = "invalid register or actual frame range";
    }
    else
    {
        u32 kind = stack ? RA_CHECK_STACK : RA_CHECK_GPR;
        u32 offset = stack ? index - 8u : index;
        for (u32 location = 0; location < lower->program.location_count; location += 1)
        {
            if (lower->locations[location].kind == kind && lower->locations[location].index == offset)
            {
                result = (u8)location;
            }
        }
        if (result == RA_CHECK_NONE)
        {
            if (lower->program.location_count == RA_CHECK_LIMIT)
            {
                lower->status = RA_CHECK_UNCOVERED;
                lower->reason = "prototype location capacity";
            }
            else
            {
                result = (u8)lower->program.location_count;
                lower->locations[lower->program.location_count++] = (RaCheckLocation){.kind = kind, .index = offset, .width_bits = 64};
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ra_probe_virtual(MachineFunction const* function, MachineRef ref)
{
    bool result = machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER &&
                  machine_ref_payload(ref) < function->virtual_register_count;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ra_probe_source_unchanged(MachineFunction const* expected, MachineFunction const* actual)
{
    bool result = expected->instruction_count == actual->instruction_count && expected->virtual_register_count == actual->virtual_register_count &&
                  expected->block_count == actual->block_count && expected->immediate_count == actual->immediate_count &&
                  expected->target == actual->target && expected->edge_count == actual->edge_count &&
                  expected->block_parameter_count == actual->block_parameter_count && expected->stack_slot_count == actual->stack_slot_count &&
                  expected->outgoing_bytes == actual->outgoing_bytes && actual->instructions && actual->virtual_registers && actual->blocks &&
                  (!actual->immediate_count || actual->immediates);
    if (result)
    {
        result = memcmp(expected->instructions, actual->instructions, (u64)expected->instruction_count * sizeof(*expected->instructions)) == 0 &&
                 memcmp(expected->virtual_registers, actual->virtual_registers, (u64)expected->virtual_register_count * sizeof(*expected->virtual_registers)) == 0 &&
                 memcmp(expected->blocks, actual->blocks, (u64)expected->block_count * sizeof(*expected->blocks)) == 0 &&
                 memcmp(expected->immediates, actual->immediates, (u64)expected->immediate_count * sizeof(*expected->immediates)) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL MachineFunction ra_probe_freeze(Arena* arena, MachineFunction const* source)
{
    MachineFunction result = *source;
    result.instructions = arena_allocate(arena, MachineInstruction, source->instruction_count);
    result.virtual_registers = arena_allocate(arena, MachineVirtualRegister, source->virtual_register_count);
    result.blocks = arena_allocate(arena, MachineBlock, source->block_count);
    result.immediates = arena_allocate(arena, u64, source->immediate_count);
    memcpy(result.instructions, source->instructions, (u64)source->instruction_count * sizeof(*source->instructions));
    memcpy(result.virtual_registers, source->virtual_registers, (u64)source->virtual_register_count * sizeof(*source->virtual_registers));
    memcpy(result.blocks, source->blocks, (u64)source->block_count * sizeof(*source->blocks));
    memcpy(result.immediates, source->immediates, (u64)source->immediate_count * sizeof(*source->immediates));
    return result;
}

BUSTER_GLOBAL_LOCAL RaProbeLower ra_probe_lower(Arena* arena, MachineFunction const* function,
                                                 MachineFunction const* actual, MachineStackPlacement const* placement)
{
    RaProbeLower lower = {.status = RA_CHECK_VALID, .reason = "admitted adapter subset"};
    if (!ra_probe_source_unchanged(function, actual))
    {
        lower.status = RA_CHECK_INVALID;
        lower.reason = "allocated source stream differs from frozen preallocation input";
    }
    else if (!placement->valid || !placement->operand_registers || (placement->edit_count && !placement->edits) ||
        !function->instructions || !function->virtual_registers || !function->blocks ||
        (function->immediate_count && !function->immediates))
    {
        lower.status = RA_CHECK_INVALID;
        lower.reason = "missing placement or source storage";
    }
    else if (function->virtual_register_count > RA_CHECK_LIMIT || function->instruction_count > RA_CHECK_MAX_ROWS ||
             placement->edit_count > RA_CHECK_MAX_EDITS || function->block_count != 1 || function->edge_count ||
             function->block_parameter_count || function->stack_slot_count || function->outgoing_bytes ||
             function->target != machine_target_x86_64())
    {
        lower.status = RA_CHECK_UNCOVERED;
        lower.reason = "outside scalar single-block adapter subset or capacity";
    }
    else
    {
        // This adapter admits the SysV body/frame convention only. Save-area
        // and bounds facts are reconstructed here; allocator frame liveness
        // or absence certificates are not used to prove transport.
        u64 allowed_saves = (1ull << MACHINE_X64_RBX) | (1ull << MACHINE_X64_R12) | (1ull << MACHINE_X64_R13) |
                            (1ull << MACHINE_X64_R14) | (1ull << MACHINE_X64_R15);
        if ((placement->callee_saved_mask & ~allowed_saves) || placement->frame_size > INT32_MAX - 40u)
        {
            lower.status = RA_CHECK_INVALID;
            lower.reason = "malformed SysV save mask or frame displacement";
        }
        for (u32 value = 0; value < function->virtual_register_count; value += 1)
        {
            if (function->virtual_registers[value].register_class != MACHINE_REGISTER_CLASS_GENERAL || function->virtual_registers[value].flags)
            {
                lower.status = RA_CHECK_UNCOVERED;
                lower.reason = "non-general or mutable source value";
            }
        }
        u32 saves = 0;
        for (u32 reg = 0; reg < 16; reg += 1)
        {
            saves += (u32)((placement->callee_saved_mask >> reg) & 1u);
        }
        lower.saved_area = saves * 8u;
        lower.locations = arena_allocate(arena, RaCheckLocation, RA_CHECK_LIMIT);
        lower.rows = arena_allocate(arena, RaCheckRow, function->instruction_count);
        lower.allocated = arena_allocate(arena, RaCheckAllocatedRow, function->instruction_count);
        lower.edits = arena_allocate(arena, RaCheckEdit, placement->edit_count ? placement->edit_count : 1);
        lower.block = (RaCheckBlock){.first_row = 0, .row_count = function->instruction_count};
        lower.program = (RaCheckProgram){
            .version = RA_CHECK_VERSION, .symbol_count = function->virtual_register_count,
            .locations = lower.locations, .row_count = function->instruction_count, .rows = lower.rows,
            .allocated_row_count = function->instruction_count, .allocated_rows = lower.allocated,
            .edit_count = placement->edit_count, .edits = lower.edits, .block_count = 1,
            .frame_size = placement->frame_size + lower.saved_area,
        };
        u32 final_value = UINT32_MAX;
        for (u32 row = 0; row < function->instruction_count && lower.status == RA_CHECK_VALID; row += 1)
        {
            MachineInstruction const* source = function->instructions + row;
            u8 const* regs = placement->operand_registers + (u64)row * 4u;
            RaCheckRow* expected = lower.rows + row;
            RaCheckAllocatedRow* allocated = lower.allocated + row;
            *expected = (RaCheckRow){.width_bits = 64, .definition = RA_CHECK_NONE,
                .uses = {RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE}, .fixed_definition = RA_CHECK_NONE,
                .fixed_uses = {RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE}, .tied_use = RA_CHECK_NONE};
            *allocated = (RaCheckAllocatedRow){.width_bits = 64, .definition_location = RA_CHECK_NONE,
                .use_locations = {RA_CHECK_NONE, RA_CHECK_NONE, RA_CHECK_NONE}};
            if (source->flags || source->payload || source->operands[3])
            {
                lower.status = RA_CHECK_UNCOVERED;
                lower.reason = "unmodeled source flags or payload";
            }
            else if (source->opcode == MACHINE_X64_MOV_RI && ra_probe_virtual(function, source->operands[0]) &&
                     machine_ref_kind(source->operands[1]) == MACHINE_REF_IMMEDIATE &&
                     machine_ref_payload(source->operands[1]) < function->immediate_count && !source->operands[2])
            {
                expected->opcode = RA_CHECK_CONST64;
                expected->definition = (u8)machine_ref_payload(source->operands[0]);
                expected->payload = function->immediates[machine_ref_payload(source->operands[1])];
                allocated->definition_location = ra_probe_location(&lower, false, regs[0]);
            }
            else if (source->opcode == MACHINE_X64_ADD64 && ra_probe_virtual(function, source->operands[0]) &&
                     ra_probe_virtual(function, source->operands[1]) && ra_probe_virtual(function, source->operands[2]))
            {
                expected->opcode = RA_CHECK_ADD64;
                expected->definition = (u8)machine_ref_payload(source->operands[0]);
                expected->use_count = 2;
                expected->uses[0] = (u8)machine_ref_payload(source->operands[1]);
                expected->uses[1] = (u8)machine_ref_payload(source->operands[2]);
                expected->tied_use = 0;
                allocated->definition_location = ra_probe_location(&lower, false, regs[0]);
                allocated->use_locations[0] = ra_probe_location(&lower, false, regs[1]);
                allocated->use_locations[1] = ra_probe_location(&lower, false, regs[2]);
            }
            else if (source->opcode == MACHINE_X64_MOV_RR && row + 2u == function->instruction_count &&
                     machine_ref_kind(source->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER &&
                     machine_ref_payload(source->operands[0]) == MACHINE_X64_RAX &&
                     ra_probe_virtual(function, source->operands[1]) && !source->operands[2])
            {
                expected->opcode = RA_CHECK_COPY64;
                expected->use_count = 1;
                expected->uses[0] = (u8)machine_ref_payload(source->operands[1]);
                expected->fixed_definition = MACHINE_X64_RAX;
                allocated->definition_location = ra_probe_location(&lower, false, regs[0]);
                allocated->use_locations[0] = ra_probe_location(&lower, false, regs[1]);
                final_value = machine_ref_payload(source->operands[1]);
            }
            else if (source->opcode == MACHINE_X64_RET && row + 1u == function->instruction_count &&
                     final_value != UINT32_MAX && !source->operands[0] && !source->operands[1] && !source->operands[2])
            {
                // RET's implicit RAX observation comes from the fixture ABI
                // contract, independently of operand_info's zero operands.
                expected->opcode = RA_CHECK_OBSERVE64;
                expected->use_count = 1;
                expected->uses[0] = (u8)final_value;
                expected->fixed_uses[0] = MACHINE_X64_RAX;
                allocated->use_locations[0] = ra_probe_location(&lower, false, MACHINE_X64_RAX);
            }
            else
            {
                lower.status = RA_CHECK_UNCOVERED;
                lower.reason = "unmodeled machine instruction or operand shape";
            }
            allocated->opcode = expected->opcode;
            allocated->payload = expected->payload;
        }
        for (u32 edit = 0; edit < placement->edit_count && lower.status == RA_CHECK_VALID; edit += 1)
        {
            MachineEdit const* source = placement->edits + edit;
            u32 row = machine_point_instruction(source->point);
            MachinePointPhase phase = machine_point_phase(source->point);
            RaCheckEdit* allocated = lower.edits + edit;
            *allocated = (RaCheckEdit){.owner = row, .width_bits = 64, .source_location = RA_CHECK_NONE,
                                      .destination_location = RA_CHECK_NONE};
            if (source->flags || row >= function->instruction_count ||
                (phase != MACHINE_POINT_BEFORE && phase != MACHINE_POINT_AFTER) ||
                (edit && source->point < placement->edits[edit - 1u].point))
            {
                lower.status = RA_CHECK_INVALID;
                lower.reason = "malformed, unsorted, trailing or unemitted-phase edit";
            }
            else
            {
                allocated->phase = phase == MACHINE_POINT_BEFORE ? RA_CHECK_BEFORE : RA_CHECK_AFTER;
                if (source->kind == MACHINE_EDIT_COPY)
                {
                    allocated->kind = RA_CHECK_MOVE64;
                    allocated->source_location = ra_probe_location(&lower, false, source->subject);
                    allocated->destination_location = ra_probe_location(&lower, false, source->location);
                }
                else if (source->kind == MACHINE_EDIT_SPILL || source->kind == MACHINE_EDIT_RELOAD)
                {
                    if (source->subject >= function->virtual_register_count || !placement->virtual_register_offsets)
                    {
                        lower.status = RA_CHECK_INVALID;
                        lower.reason = "malformed spill/reload subject or missing home map";
                    }
                    else
                    {
                        u8 reg = ra_probe_location(&lower, false, source->location);
                        u8 frame = ra_probe_location(&lower, true, placement->virtual_register_offsets[source->subject]);
                        bool spill = source->kind == MACHINE_EDIT_SPILL;
                        allocated->kind = spill ? RA_CHECK_SPILL64 : RA_CHECK_RELOAD64;
                        allocated->source_location = spill ? reg : frame;
                        allocated->destination_location = spill ? frame : reg;
                    }
                }
                else if (source->kind == MACHINE_EDIT_REMATERIALIZE && source->subject < function->immediate_count)
                {
                    u32 recipe = UINT32_MAX;
                    for (u32 candidate = 0; candidate < function->instruction_count; candidate += 1)
                    {
                        MachineInstruction const* original = function->instructions + candidate;
                        if (original->opcode == MACHINE_X64_MOV_RI && machine_ref_kind(original->operands[1]) == MACHINE_REF_IMMEDIATE &&
                            machine_ref_payload(original->operands[1]) == source->subject)
                        {
                            recipe = candidate;
                        }
                    }
                    allocated->kind = RA_CHECK_REMATERIALIZE64;
                    allocated->recipe_row = recipe;
                    allocated->payload = function->immediates[source->subject];
                    allocated->destination_location = ra_probe_location(&lower, false, source->location);
                    if (recipe == UINT32_MAX)
                    {
                        lower.status = RA_CHECK_INVALID;
                        lower.reason = "rematerialization has no source literal row";
                    }
                }
                else
                {
                    lower.status = RA_CHECK_UNCOVERED;
                    lower.reason = "unmodeled edit kind or edit storage";
                }
                RaCheckAllocatedRow* owner = lower.allocated + row;
                u32* first = phase == MACHINE_POINT_BEFORE ? &owner->first_before : &owner->first_after;
                u32* count = phase == MACHINE_POINT_BEFORE ? &owner->before_count : &owner->after_count;
                if (!*count)
                {
                    *first = edit;
                }
                *count += 1u;
            }
        }
    }
    return lower;
}

BUSTER_GLOBAL_LOCAL RaCheckResult ra_probe_check(RaProbeLower* lower, RaCheckWorkspace* workspace)
{
    // The returned aggregate's block pointer must refer to its final storage.
    lower->program.blocks = &lower->block;
    RaCheckResult result = {.status = lower->status, .reason = lower->reason};
    if (lower->status == RA_CHECK_VALID)
    {
        result = ra_check(&lower->program, workspace);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL MachineFunction ra_probe_function(Arena* arena, u32 leaf_count)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32* values = arena_allocate(arena, u32, leaf_count);
    machine_builder_block_begin(&builder);
    for (u32 leaf = 0; leaf < leaf_count; leaf += 1)
    {
        u32 row = builder.instructions.total_count;
        u32 value = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
            .definition_point = machine_point_make(row, MACHINE_POINT_AFTER),
            .register_class = MACHINE_REGISTER_CLASS_GENERAL,
            .typed_origin = IR_ID_UNDERLYING_INVALID,
        });
        machine_builder_instruction(&builder, (MachineInstruction){
            .opcode = MACHINE_X64_MOV_RI,
            .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value), machine_ref_make(MACHINE_REF_IMMEDIATE, leaf)},
        });
        values[leaf] = value;
    }
    for (u32 width = leaf_count; width > 1; width /= 2)
    {
        for (u32 pair = 0; pair < width / 2; pair += 1)
        {
            u32 row = builder.instructions.total_count;
            u32 value = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                .definition_point = machine_point_make(row, MACHINE_POINT_AFTER),
                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                .typed_origin = IR_ID_UNDERLYING_INVALID,
            });
            machine_builder_instruction(&builder, (MachineInstruction){
                .opcode = MACHINE_X64_ADD64,
                .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, value),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[2 * pair]),
                             machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[2 * pair + 1])},
            });
            values[pair] = value;
        }
    }
    machine_builder_instruction(&builder, (MachineInstruction){
        .opcode = MACHINE_X64_MOV_RR,
        .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX),
                     machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, values[0])},
    });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
    function.immediates = arena_allocate(arena, u64, leaf_count);
    function.immediate_count = leaf_count;
    for (u32 leaf = 0; leaf < leaf_count; leaf += 1)
    {
        function.immediates[leaf] = 1u + leaf;
    }
    function.returns_twice_absence_certified = true;
    return function;
}

int main(void)
{
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){0});
    machine_opcode_rows_prewarm();
    int status = 0;
    char const* timing = getenv("RA_PROBE_HOSTED_BENCH");
    bool measure = timing && strcmp(timing, "1") == 0;
    u32 leaves[] = {4, 16, 32, 64};
    char const* modes[] = {"mir-stack", "fast", "quality"};
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(leaves); fixture += 1)
    {
        MachineFunction function = ra_probe_function(arena, leaves[fixture]);
        MachineVerifyResult verified = machine_verify_function(&function);
        status |= verified.error != MACHINE_VERIFY_NONE;
        // Freeze before any allocator call, including QUALITY's first attempt.
        // The checker reconstructs semantics from this independent snapshot.
        MachineFunction frozen = ra_probe_freeze(arena, &function);
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            MachineStackPlacement placement;
            switch (mode)
            {
                break; case 0: placement = machine_stack_placement_build(arena, &function);
                break; case 1: placement = machine_fast_placement_build(arena, &function);
                break; default: placement = machine_quality_placement_build(arena, &function);
            }
            status |= !placement.valid;
            bool source_unchanged = ra_probe_source_unchanged(&frozen, &function);
            status |= !source_unchanged;
            RaCheckWorkspace* workspace = arena_allocate(arena, RaCheckWorkspace, 1);
            RaProbeLower lower = ra_probe_lower(arena, &frozen, &function, &placement);
            RaCheckResult checked = ra_probe_check(&lower, workspace);
            bool capacity_case = leaves[fixture] == 64u || (leaves[fixture] == 32u && mode == 0u);
            status |= capacity_case ? checked.status != RA_CHECK_UNCOVERED : checked.status != RA_CHECK_VALID;
            printf("fixture=tree leaves=%u mode=%s rows=%u values=%u edits=%u frame=%u verified=%u placement_valid=%u source_unchanged=%u checker=%u locations=%u transfers=%llu edit_transfers=%llu sweeps=%llu reason=%s\n",
                   leaves[fixture], modes[mode], function.instruction_count, function.virtual_register_count,
                   placement.edit_count, placement.frame_size, (unsigned)verified.error, (unsigned)placement.valid,
                   (unsigned)source_unchanged, (unsigned)checked.status, lower.program.location_count,
                   (unsigned long long)checked.transfer_rows, (unsigned long long)checked.transfer_edits,
                   (unsigned long long)checked.sweeps, checked.reason);
            if (checked.status == RA_CHECK_VALID)
            {
                // Lowering, source verification and allocation are already
                // complete. These diagnostic samples price only checking.
                if (measure)
                {
                    for (u32 sample = 0; sample < 7; sample += 1)
                    {
                        u64 begin = ra_probe_now();
                        RaCheckResult timed = ra_probe_check(&lower, workspace);
                        u64 end = ra_probe_now();
                        status |= timed.status != RA_CHECK_VALID;
                        printf("actual-check-cost leaves=%u mode=%s sample=%u rows=%u edits=%u locations=%u symbols=%u elapsed_ns=%llu transfers=%llu edit_transfers=%llu sweeps=%llu workspace_bytes=%zu\n",
                               leaves[fixture], modes[mode], sample, function.instruction_count, placement.edit_count,
                               lower.program.location_count, lower.program.symbol_count, (unsigned long long)(end - begin),
                               (unsigned long long)timed.transfer_rows, (unsigned long long)timed.transfer_edits,
                               (unsigned long long)timed.sweeps, sizeof(*workspace));
                    }
                }
                RaCheckState entry = workspace->entries[0];
                RaCheckCertificate certificate = {.present = 1, .version = RA_CHECK_VERSION, .state_count = 1, .block_entries = &entry};
                RaCheckResult certified = ra_check_certificate(&lower.program, workspace, &certificate);
                status |= certified.status != RA_CHECK_VALID;
                printf("certificate leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                       (unsigned)certified.status, certified.reason);

                u32 add_row = leaves[fixture];
                u8* allocated_add = placement.operand_registers + (u64)add_row * 4u;
                u8 saved_use = allocated_add[2];
                allocated_add[2] = allocated_add[1];
                RaProbeLower bad_use = ra_probe_lower(arena, &frozen, &function, &placement);
                RaCheckResult use_rejection = ra_probe_check(&bad_use, workspace);
                allocated_add[2] = saved_use;
                status |= use_rejection.status == RA_CHECK_VALID;
                printf("mutation=wrong-allocated-use leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                       (unsigned)use_rejection.status, use_rejection.reason);

                if (placement.edit_count)
                {
                    MachinePoint saved_point = placement.edits[0].point;
                    placement.edits[0].point = machine_point_make(machine_point_instruction(saved_point), MACHINE_POINT_EARLY);
                    RaProbeLower bad_phase = ra_probe_lower(arena, &frozen, &function, &placement);
                    RaCheckResult phase_rejection = ra_probe_check(&bad_phase, workspace);
                    placement.edits[0].point = saved_point;
                    status |= phase_rejection.status != RA_CHECK_INVALID;
                    printf("mutation=unemitted-edit-phase leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                           (unsigned)phase_rejection.status, phase_rejection.reason);
                }
                if (mode == 0)
                {
                    u32 saved_offset = placement.virtual_register_offsets[1];
                    placement.virtual_register_offsets[1] = placement.virtual_register_offsets[0];
                    RaProbeLower bad_alias = ra_probe_lower(arena, &frozen, &function, &placement);
                    RaCheckResult alias_rejection = ra_probe_check(&bad_alias, workspace);
                    placement.virtual_register_offsets[1] = saved_offset;
                    status |= alias_rejection.status == RA_CHECK_VALID;
                    printf("mutation=overlapping-live-homes leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                           (unsigned)alias_rejection.status, alias_rejection.reason);

                    MachineStackPlacement bad_save_placement = placement;
                    bad_save_placement.callee_saved_mask = 1ull << MACHINE_X64_RBX;
                    u32 saved_first_offset = placement.virtual_register_offsets[0];
                    // A push area of eight bytes occupies [0,8) below RBP.
                    // A home displacement of nine denotes [1,9), not a safe
                    // range merely because its endpoint exceeds eight.
                    placement.virtual_register_offsets[0] = 9;
                    RaProbeLower bad_save = ra_probe_lower(arena, &frozen, &function, &bad_save_placement);
                    RaCheckResult save_rejection = ra_probe_check(&bad_save, workspace);
                    placement.virtual_register_offsets[0] = saved_first_offset;
                    status |= save_rejection.status != RA_CHECK_INVALID;
                    printf("mutation=partial-save-area-overlap leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                           (unsigned)save_rejection.status, save_rejection.reason);
                }

                u16 saved_opcode = function.instructions[add_row].opcode;
                function.instructions[add_row].opcode = MACHINE_X64_SUB64;
                RaProbeLower changed_opcode = ra_probe_lower(arena, &frozen, &function, &placement);
                RaCheckResult opcode_rejection = ra_probe_check(&changed_opcode, workspace);
                function.instructions[add_row].opcode = saved_opcode;
                status |= opcode_rejection.status != RA_CHECK_INVALID;
                printf("mutation=source-opcode-after-freeze leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                       (unsigned)opcode_rejection.status, opcode_rejection.reason);

                u32 saved_payload = function.instructions[add_row].payload;
                function.instructions[add_row].payload ^= 1u;
                RaProbeLower changed_payload = ra_probe_lower(arena, &frozen, &function, &placement);
                RaCheckResult payload_rejection = ra_probe_check(&changed_payload, workspace);
                function.instructions[add_row].payload = saved_payload;
                status |= payload_rejection.status != RA_CHECK_INVALID;
                printf("mutation=source-payload-after-freeze leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                       (unsigned)payload_rejection.status, payload_rejection.reason);

                u64 saved_literal = function.immediates[0];
                function.immediates[0] ^= 1u;
                RaProbeLower changed_literal = ra_probe_lower(arena, &frozen, &function, &placement);
                RaCheckResult literal_rejection = ra_probe_check(&changed_literal, workspace);
                function.immediates[0] = saved_literal;
                status |= literal_rejection.status != RA_CHECK_INVALID;
                printf("mutation=source-literal-after-freeze leaves=%u mode=%s checker=%u reason=%s\n", leaves[fixture], modes[mode],
                       (unsigned)literal_rejection.status, literal_rejection.reason);
            }
        }
    }
    arena_destroy(arena, 1);
    thread_context_release(context);
    return status;
}
