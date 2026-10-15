#define _POSIX_C_SOURCE 200809L
#include <buster/lib/compiler/ir/ir.c>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ProgramState* program_state;
#if PROBE_COVERAGE
void __llvm_profile_reset_counters(void);
#endif

static u64 probe_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ull + t.tv_nsec;
}

int main(int argc, char** argv)
{
    bool ok = argc == 3;
    if (ok)
    {
        ThreadContext* context = thread_context_allocate();
        thread_context_select(context);
        u32 n = (u32)strtoul(argv[2], 0, 10);
        ok = n >= 2 && n <= 32768;
        Arena* arena = arena_create((ArenaCreation){.reserved_size = 256ull * 1024 * 1024});
        IrType types[5] = {
            {.id = {0}, .kind = IR_TYPE_VOID, .layout = {.resolved = true, .alignment = 1}},
            {.id = {1}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .layout = {.resolved = true, .size = 4, .alignment = 4}},
            {.id = {2}, .kind = IR_TYPE_FUNCTION, .return_type = {0}, .layout = {.resolved = true, .alignment = 1}},
            {.id = {3}, .kind = IR_TYPE_STRUCT, .layout = {.resolved = true, .size = (u64)n * 4, .alignment = 4}},
            {.id = {4}, .kind = IR_TYPE_POINTER, .element_type = {0}, .layout = {.resolved = true, .size = 8, .alignment = 8}}
        };
        IrProgram program = {.arena = arena, .types = {.types = types, .count = 5}};
        program.data_layout.pointer.size = 8;
        IrFunction f = {.canonical_type = {2}, .entry = {0}, .state = IR_FUNCTION_LOWERED};
        IrValidationSplice region = {0};
        bool splice = !strcmp(argv[1], "splice");
        bool aggregate = !strncmp(argv[1], "aggregate", 9);
        bool indirect = !strncmp(argv[1], "indirect", 8);
        bool cast = !strncmp(argv[1], "cast", 4);
        bool labelled = indirect || cast;
        bool bad = strstr(argv[1], "bad") != 0;
        bool reversed = strstr(argv[1], "reversed") != 0;
        ok = ok && (splice || aggregate || labelled);
        f.block_count = labelled ? n + 1 : splice ? 2 : 1;
        f.instruction_count = labelled ? n + 3 + cast : splice ? n + 2 : 3;
        f.value_count = splice ? n : cast ? 3 : 2;
        f.blocks = arena_allocate_zeroed(arena, IrBlock, f.block_count);
        f.instructions = arena_allocate_zeroed(arena, IrInstruction, f.instruction_count);
        f.values = arena_allocate_zeroed(arena, IrValue, f.value_count);
        for (u32 b = 0; b < f.block_count; b += 1)
        {
            u32 row = labelled ? (b ? b + 2 + cast : 0) : splice && b ? n + 1 : 0;
            u32 last = labelled ? (b ? row : 2 + cast) : splice && !b ? n : splice ? row : 2;
            f.blocks[b] = (IrBlock){.id = {b}, .first_instruction = {row}, .last_instruction = {last}, .terminated = true, .sealed = true};
        }
        for (u32 i = 0; i < f.instruction_count; i += 1)
        {
            f.instructions[i] = (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = {0}, .result = IR_VALUE_ID_INVALID,
                .next = IR_INSTRUCTION_ID_INVALID};
        }
        u64 immediate = 0;
        if (aggregate || splice)
        {
            for (u32 i = 0; i < (splice ? n : 1); i += 1)
            {
                f.instructions[i] = (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {1}, .result = {i},
                    .immediates = &immediate, .immediate_count = 1, .next = {i + 1}};
                f.values[i] = (IrValue){.canonical_type = {1}, .definition = {i}, .category = IR_VALUE_VALUE};
            }
        }
        if (aggregate)
        {
            types[3].field_count = n;
            types[3].fields = arena_allocate_zeroed(arena, IrField, n);
            IrValueId* operands = arena_allocate_zeroed(arena, IrValueId, n);
            u64* fields = arena_allocate(arena, u64, n);
            for (u32 i = 0; i < n; i += 1)
            {
                types[3].fields[i] = (IrField){.type = {1}, .offset = (u64)i * 4};
                fields[i] = reversed ? n - i - 1 : i;
            }
            if (bad) fields[n - 1] = fields[0];
            f.instructions[1] = (IrInstruction){.opcode = IR_OPCODE_AGGREGATE, .canonical_type = {3}, .result = {1}, .next = {2},
                .operands = operands, .operand_count = n, .immediates = fields, .immediate_count = (u16)n};
            f.values[1] = (IrValue){.canonical_type = {3}, .definition = {1}, .category = IR_VALUE_VALUE};
        }
        if (splice)
        {
            u32 block = 1;
            u32 row = n + 1;
            region = (IrValidationSplice){.blocks = &block, .rows = &row, .block_count = 1, .row_count = 1,
                .base_blocks = f.block_count, .base_instructions = f.instruction_count, .base_values = f.value_count};
            IrValidationResult baseline = ir_validate_function_walk(&program, &f, 0);
            ok = ok && baseline.error == IR_VALIDATION_NONE;
            // Keep these two tiny region arrays alive past the setup scope.
            region.blocks = arena_allocate(arena, u32, 1);
            region.rows = arena_allocate(arena, u32, 1);
            region.blocks[0] = block;
            region.rows[0] = row;
        }
        if (labelled)
        {
            IrBlockId* labels = arena_allocate(arena, IrBlockId, n);
            for (u32 i = 0; i < n; i += 1) labels[i].value = i + 1;
            f.local_count = 1;
            f.values[0] = (IrValue){.canonical_type = {4}, .definition = {0}, .category = IR_VALUE_PLACE};
            f.values[1] = (IrValue){.canonical_type = {4}, .definition = {1}, .category = IR_VALUE_VALUE};
            IrValueId* load_operand = arena_allocate_zeroed(arena, IrValueId, 1);
            IrValueId* branch_operand = arena_allocate(arena, IrValueId, 1);
            branch_operand[0].value = cast ? 2 : 1;
            f.instructions[0] = (IrInstruction){.opcode = IR_OPCODE_LOCAL, .canonical_type = {4}, .canonical_local = {0}, .result = {0}, .next = {1}};
            f.instructions[1] = (IrInstruction){.opcode = IR_OPCODE_LOAD, .canonical_type = {4}, .result = {1}, .next = {2},
                .operands = load_operand, .operand_count = 1};
            IrBlockId* targets = arena_allocate(arena, IrBlockId, n);
            for (u32 i = 0; i < n; i += 1) targets[i] = labels[reversed ? n - i - 1 : i];
            if (bad) targets[n - 1].value = 0;
            f.instructions[2 + cast] = (IrInstruction){.opcode = IR_OPCODE_INDIRECT_BRANCH, .canonical_type = {0}, .result = IR_VALUE_ID_INVALID,
                .next = IR_INSTRUCTION_ID_INVALID, .operands = branch_operand, .operand_count = 1, .targets = targets, .target_count = (u16)n};
            f.label_metadata_count = 2 + cast;
            f.label_metadata_values = arena_allocate(arena, IrValueId, f.label_metadata_count);
            f.label_metadata_values[0].value = 0;
            f.label_metadata_values[1].value = 1;
            f.label_metadata = arena_allocate_zeroed(arena, IrValueLabelMetadata, f.label_metadata_count);
            IrLabelProvenancePath* path = arena_allocate_zeroed(arena, IrLabelProvenancePath, 1);
            *path = (IrLabelProvenancePath){.size = 8, .label_blocks = labels, .label_block_count = n};
            f.label_metadata[0] = (IrValueLabelMetadata){.label_blocks = labels, .label_block_count = n, .has_label_provenance = true,
                .label_paths = path, .label_path_count = 1};
            f.label_metadata[1] = (IrValueLabelMetadata){.label_blocks = labels, .label_block_count = n, .is_label_value = true};
            if (cast)
            {
                IrValueId* cast_operand = arena_allocate(arena, IrValueId, 1);
                cast_operand[0].value = 1;
                f.instructions[2] = (IrInstruction){.opcode = IR_OPCODE_CAST, .canonical_type = {4}, .result = {2}, .next = {3},
                    .operands = cast_operand, .operand_count = 1, .conversion_operation = IR_CONVERSION_IDENTITY};
                f.values[2] = (IrValue){.canonical_type = {4}, .definition = {2}, .category = IR_VALUE_VALUE};
                f.label_metadata_values[2].value = 2;
                f.label_metadata[2] = f.label_metadata[1];
                if (reversed) f.label_metadata[2].label_blocks = targets;
            }
        }
#if PROBE_COVERAGE
        __llvm_profile_reset_counters();
#endif
        IrConstructionCounters before = ir_construction_counters();
        u64 start = probe_now();
        IrValidationResult result = ir_validate_function_walk(&program, &f, splice ? &region : 0);
        u64 ns = probe_now() - start;
        IrValidationError expected = !bad ? IR_VALIDATION_NONE : aggregate ? IR_VALIDATION_OPERATION : IR_VALIDATION_BRANCH_TARGET;
        ok = ok && result.error == expected;
        printf("{\"shape\":\"%s\",\"n\":%u,\"error\":%u,\"ok\":%s,\"ns\":%llu,\"rows\":%u,\"values\":%u}\n",
            argv[1], n, result.error, ok ? "true" : "false", (unsigned long long)ns, f.instruction_count, f.value_count);
        IrConstructionCounters after = ir_construction_counters();
        printf("{\"field_visits\":%llu,\"label_set_work\":%llu,\"marker_bytes\":%llu,\"swept_values\":%llu,\"parameter_blocks\":%llu,\"index_work\":%llu,\"row_checks\":%llu}\n",
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_AGGREGATE_FIELD_VISITS] - before.values[IR_CONSTRUCTION_VALIDATION_AGGREGATE_FIELD_VISITS]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_OWNERSHIP_BYTES_CLEARED] - before.values[IR_CONSTRUCTION_VALIDATION_OWNERSHIP_BYTES_CLEARED]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS] - before.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_SPLICE_PARAMETER_BLOCKS] - before.values[IR_CONSTRUCTION_VALIDATION_SPLICE_PARAMETER_BLOCKS]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_MARKER_INDEX_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_MARKER_INDEX_WORK]),
            (unsigned long long)(after.values[IR_CONSTRUCTION_VALIDATION_INSTRUCTIONS] - before.values[IR_CONSTRUCTION_VALIDATION_INSTRUCTIONS]));
        arena_destroy(arena, 1);
        thread_context_release(context);
    }
    return ok ? 0 : 1;
}
