// Full-validator scaling and rejection controls for aggregate fields, label
// instruction sets and regional splice proofs. Included by ir_test.c.
typedef struct IrValidateScaleFixture IrValidateScaleFixture;
struct IrValidateScaleFixture
{
    IrProgram program;
    IrModule module;
    IrFunction function;
    u64* fields;
    IrBlockId* targets;
};

BUSTER_GLOBAL_LOCAL IrValidateScaleFixture* ir_validate_scale_fixture(Arena* arena, u32 shape, u32 count, u32 order)
{
    IrValidateScaleFixture* fixture = arena_allocate_zeroed(arena, IrValidateScaleFixture, 1);
    IrType* types = arena_allocate_zeroed(arena, IrType, 5);
    types[0] = (IrType){.id = {0}, .kind = IR_TYPE_VOID, .layout = {.resolved = true, .alignment = 1}};
    types[1] = (IrType){.id = {1}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .layout = {.resolved = true, .size = 4, .alignment = 4}};
    types[2] = (IrType){.id = {2}, .kind = IR_TYPE_FUNCTION, .return_type = {0}, .layout = {.resolved = true, .alignment = 1}};
    types[3] = (IrType){.id = {3}, .kind = IR_TYPE_STRUCT, .layout = {.resolved = true, .size = (u64)count * 4, .alignment = 4}};
    types[4] = (IrType){.id = {4}, .kind = IR_TYPE_POINTER, .element_type = {0}, .layout = {.resolved = true, .size = 8, .alignment = 8}};
    fixture->program = (IrProgram){.arena = arena, .types = {.types = types, .count = 5},
        .modules = &fixture->module, .module_count = 1, .data_layout = {.pointer = {.size = 8, .alignment = 8}}};
    fixture->module = (IrModule){.functions = &fixture->function, .function_count = 1};
    bool labelled = shape != 0;
    bool cast = shape == 2;
    IrFunction* function = &fixture->function;
    *function = (IrFunction){.canonical_type = {2}, .entry = {0}, .id = {7}, .state = IR_FUNCTION_LOWERED,
        .block_count = labelled ? count + 1 : 1, .instruction_count = labelled ? count + 3 + cast : 3, .value_count = cast ? 3 : 2};
    function->blocks = arena_allocate_zeroed(arena, IrBlock, function->block_count);
    function->instructions = arena_allocate_zeroed(arena, IrInstruction, function->instruction_count);
    function->values = arena_allocate_zeroed(arena, IrValue, function->value_count);
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        u32 first = labelled && block ? block + 2 + cast : 0;
        u32 last = labelled && block ? first : 2u + cast;
        function->blocks[block] = (IrBlock){.id = {block}, .first_instruction = {first}, .last_instruction = {last}, .terminated = true, .sealed = true};
    }
    for (u32 row = 0; row < function->instruction_count; row += 1)
    {
        function->instructions[row] = (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = {0},
            .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID};
    }
    if (!labelled)
    {
        u64* constant = arena_allocate_zeroed(arena, u64, 1);
        function->instructions[0] = (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {1}, .result = {0},
            .immediates = constant, .immediate_count = 1, .next = {1}};
        function->values[0] = (IrValue){.canonical_type = {1}, .definition = {0}, .category = IR_VALUE_VALUE};
        function->values[1] = (IrValue){.canonical_type = {3}, .definition = {1}, .category = IR_VALUE_VALUE};
        types[3].field_count = count;
        types[3].fields = arena_allocate_zeroed(arena, IrField, count);
        fixture->fields = arena_allocate(arena, u64, count);
        IrValueId* operands = arena_allocate_zeroed(arena, IrValueId, count);
        for (u32 index = 0; index < count; index += 1)
        {
            types[3].fields[index] = (IrField){.type = {1}, .offset = (u64)index * 4};
            fixture->fields[index] = order == 0 ? index : order == 1 ? count - index - 1 : (index * 5 + 3) % count;
        }
        function->instructions[1] = (IrInstruction){.opcode = IR_OPCODE_AGGREGATE, .canonical_type = {3}, .result = {1}, .next = {2},
            .operands = operands, .operand_count = count, .immediates = fixture->fields, .immediate_count = (u16)count};
    }
    else
    {
        IrBlockId* labels = arena_allocate(arena, IrBlockId, count);
        fixture->targets = arena_allocate(arena, IrBlockId, count);
        for (u32 index = 0; index < count; index += 1)
        {
            labels[index].value = index + 1;
            fixture->targets[index].value = (order == 0 ? index : order == 1 ? count - index - 1 : (index * 5 + 3) % count) + 1;
        }
        function->local_count = 1;
        function->values[0] = (IrValue){.canonical_type = {4}, .definition = {0}, .category = IR_VALUE_PLACE};
        function->values[1] = (IrValue){.canonical_type = {4}, .definition = {1}, .category = IR_VALUE_VALUE};
        IrValueId* load = arena_allocate_zeroed(arena, IrValueId, 1);
        IrValueId* branch = arena_allocate(arena, IrValueId, 1);
        branch->value = cast ? 2 : 1;
        function->instructions[0] = (IrInstruction){.opcode = IR_OPCODE_LOCAL, .canonical_type = {4}, .result = {0}, .next = {1}};
        function->instructions[1] = (IrInstruction){.opcode = IR_OPCODE_LOAD, .canonical_type = {4}, .result = {1}, .next = {2},
            .operands = load, .operand_count = 1};
        function->instructions[2 + cast] = (IrInstruction){.opcode = IR_OPCODE_INDIRECT_BRANCH, .canonical_type = {0},
            .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID, .operands = branch, .operand_count = 1,
            .targets = fixture->targets, .target_count = (u16)count};
        function->label_metadata_count = 2 + cast;
        function->label_metadata_values = arena_allocate(arena, IrValueId, function->label_metadata_count);
        function->label_metadata = arena_allocate_zeroed(arena, IrValueLabelMetadata, function->label_metadata_count);
        for (u32 index = 0; index < function->label_metadata_count; index += 1) function->label_metadata_values[index].value = index;
        IrLabelProvenancePath* path = arena_allocate_zeroed(arena, IrLabelProvenancePath, 1);
        *path = (IrLabelProvenancePath){.size = 8, .label_blocks = labels, .label_block_count = count};
        function->label_metadata[0] = (IrValueLabelMetadata){.label_blocks = labels, .label_block_count = count, .has_label_provenance = true,
            .label_paths = path, .label_path_count = 1};
        function->label_metadata[1] = (IrValueLabelMetadata){.label_blocks = labels, .label_block_count = count, .is_label_value = true};
        if (cast)
        {
            IrValueId* operand = arena_allocate(arena, IrValueId, 1);
            operand->value = 1;
            function->instructions[2] = (IrInstruction){.opcode = IR_OPCODE_CAST, .canonical_type = {4}, .result = {2}, .next = {3},
                .operands = operand, .operand_count = 1, .conversion_operation = IR_CONVERSION_IDENTITY};
            function->values[2] = (IrValue){.canonical_type = {4}, .definition = {2}, .category = IR_VALUE_VALUE};
            function->label_metadata[2] = function->label_metadata[1];
            function->label_metadata[2].label_blocks = fixture->targets;
        }
    }
    return fixture;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_validate_instruction_scaling_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 populations[] = {2, 8, 9, 17, 256, 1024, 4096};
    for (u32 shape = 0; shape < 3; shape += 1)
    {
        for (u32 population = 0; population < BUSTER_ARRAY_LENGTH(populations); population += 1)
        {
            u32 count = populations[population];
            for (u32 variant = 0; variant < 7; variant += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arguments->arena);
                IrValidateScaleFixture* fixture = ir_validate_scale_fixture(arguments->arena, shape, count, variant < 3 ? variant : 0);
                IrFunction* function = &fixture->function;
                IrValidationError expected = IR_VALIDATION_NONE;
                if (variant >= 3)
                {
                    expected = shape == 1 ? IR_VALIDATION_BRANCH_TARGET : IR_VALIDATION_OPERATION;
                    if (!shape)
                    {
                        if (variant == 3) fixture->fields[count - 1] = fixture->fields[0];
                        if (variant == 4) fixture->fields[count - 1] = count;
                        if (variant == 5) fixture->program.types.types[3].fields[0].type.value = 0;
                        if (variant == 6)
                        {
                            function->instructions[1].operand_count -= 1;
                            function->instructions[1].immediate_count -= 1;
                        }
                    }
                    else
                    {
                        if (variant == 3) fixture->targets[count - 1] = fixture->targets[0];
                        if (variant == 4) fixture->targets[count - 1].value = 0;
                        if (variant == 5) fixture->targets[count - 1].value = function->block_count;
                        if (variant == 6)
                        {
                            if (shape == 1) function->instructions[2].target_count -= 1;
                            else function->instructions[2].conversion_operation = IR_CONVERSION_POINTER_TO_INTEGER;
                        }
                    }
                }
                u64 bytes = !shape ? sizeof(u64) * count : sizeof(IrBlockId) * count;
                void* data = !shape ? (void*)fixture->fields : (void*)fixture->targets;
                void* snapshot = arena_allocate_bytes(arguments->arena, bytes, 8);
                memcpy(snapshot, data, bytes);
                for (u32 repeat = 0; repeat < 2; repeat += 1)
                {
#if BUSTER_BENCH_ALLOCATIONS
                    IrConstructionCounters before = ir_construction_counters();
#endif
                    IrValidationResult checked = ir_validate_canonical_module(&fixture->program, &fixture->module);
#if BUSTER_BENCH_ALLOCATIONS
                    IrConstructionCounters after = ir_construction_counters();
                    if (expected == IR_VALIDATION_NONE)
                    {
                        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_AGGREGATE_FIELD_VISITS] -
                            before.values[IR_CONSTRUCTION_VALIDATION_AGGREGATE_FIELD_VISITS] <= 2 * (u64)count);
                        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] -
                            before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SET_WORK] <= 150 * (u64)count + 4096);
                        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS] ==
                            before.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS]);
                    }
#endif
                    IrValidationResult reference = ir_test_validate_canonical_module_reference(&fixture->program, &fixture->module);
                    BUSTER_TEST(arguments, checked.error == expected);
                    BUSTER_TEST(arguments, checked.error == reference.error);
                    BUSTER_TEST(arguments, checked.function.value == reference.function.value);
                    BUSTER_TEST(arguments, checked.block.value == reference.block.value);
                    BUSTER_TEST(arguments, checked.instruction.value == reference.instruction.value);
                    BUSTER_TEST(arguments, memcmp(snapshot, data, bytes) == 0);
                }
                arena_set_position(arguments->arena, temporary.position);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_validate_splice_scaling_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 populations[] = {2, 17, 256, 4096};
    for (u32 population = 0; population < BUSTER_ARRAY_LENGTH(populations); population += 1)
    {
        u32 count = populations[population];
        for (u32 variant = 0; variant < 9; variant += 1)
        {
            if (variant == 8 && count < 17) continue;
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            IrType types[] = {
                {.id = {0}, .kind = IR_TYPE_VOID, .layout = {.resolved = true, .alignment = 1}},
                {.id = {1}, .kind = IR_TYPE_INTEGER, .bit_width = 32, .layout = {.resolved = true, .size = 4, .alignment = 4}},
                {.id = {2}, .kind = IR_TYPE_FUNCTION, .return_type = {0}},
            };
            IrProgram program = {.arena = arguments->arena, .types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)}};
            IrFunction function = {.canonical_type = {2}, .entry = {0}, .state = IR_FUNCTION_LOWERED,
                .block_count = count + 1, .instruction_count = count * 2 + 1, .value_count = count + 1};
            function.blocks = arena_allocate_zeroed(arguments->arena, IrBlock, function.block_count);
            function.instructions = arena_allocate_zeroed(arguments->arena, IrInstruction, function.instruction_count + 1);
            function.values = arena_allocate_zeroed(arguments->arena, IrValue, function.value_count + 1);
            for (u32 row = 0; row <= function.instruction_count; row += 1)
            {
                function.instructions[row] = (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = {0},
                    .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID};
            }
            for (u32 block = 0; block < function.block_count; block += 1)
            {
                u32 first = block ? count + block : 0;
                function.blocks[block] = (IrBlock){.id = {block}, .first_instruction = {first}, .last_instruction = {block ? first : count},
                    .sealed = true, .terminated = true};
            }
            u64 constant = 0;
            for (u32 value = 0; value < count; value += 1)
            {
                function.instructions[value] = (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {1}, .result = {value},
                    .immediates = &constant, .immediate_count = 1, .next = {value + 1}};
                function.values[value] = (IrValue){.canonical_type = {1}, .definition = {value}, .category = IR_VALUE_VALUE};
            }
            function.values[count] = (IrValue){.canonical_type = {1}, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE};
            IrBlockParameter parameter = {.canonical_type = {1}, .value = {count}};
            function.blocks[1].first_parameter = &parameter;
            function.blocks[1].last_parameter = &parameter;
            function.blocks[1].parameter_count = 1;
            IrModule module = {.functions = &function, .function_count = 1};
            BUSTER_TEST(arguments, ir_validate_canonical_module(&program, &module).error == IR_VALIDATION_NONE);
            u32 blocks[] = {count, 4, 2};
            u32 rows[] = {count * 2, count + 4, count + 2};
            u32 parameter_block = 1;
            IrValidationSplice splice = {.blocks = blocks, .rows = rows, .block_count = 1, .row_count = 1,
                .parameter_blocks = &parameter_block, .parameter_block_count = 1, .base_blocks = function.block_count,
                .base_instructions = function.instruction_count, .base_values = function.value_count};
            bool valid = variant == 0 || variant == 1 || variant == 7 || variant == 8;
            if (variant == 1 || variant == 6)
            {
                u32 value = variant == 6 ? 0 : function.value_count++;
                u32 row = rows[0];
                function.instructions[row] = (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = {1}, .result = {value},
                    .immediates = &constant, .immediate_count = 1, .next = {function.instruction_count}};
                function.values[value] = (IrValue){.canonical_type = {1}, .definition = {row}, .category = IR_VALUE_VALUE};
                function.blocks[count].last_instruction.value = function.instruction_count++;
            }
            if (variant == 2) function.blocks[count].first_instruction.value = 0;
            if (variant == 3) function.instruction_count += 1;
            if (variant == 4)
            {
                function.values[function.value_count++] = (IrValue){.canonical_type = {1}, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE};
            }
            if (variant == 5) parameter.incoming_count = 1;
            if (variant == 7)
            {
                blocks[0] = 0;
                splice.row_count = count + 1;
                splice.value_count = count;
                splice.rows = arena_allocate(arguments->arena, u32, splice.row_count);
                splice.values = arena_allocate(arguments->arena, u32, splice.value_count);
                for (u32 row = 0; row <= count; row += 1) splice.rows[row] = row;
                for (u32 value = 0; value < count; value += 1) splice.values[value] = value;
            }
            if (variant == 8) splice.block_count = splice.row_count = 3;
            for (u32 repeat = 0; repeat < 2; repeat += 1)
            {
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters before = ir_construction_counters();
#endif
                IrValidationResult checked = ir_test_validate_canonical_splice(&program, &function, &splice);
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters after = ir_construction_counters();
                if (valid && variant != 7)
                {
                    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_OWNERSHIP_BYTES_CLEARED] -
                        before.values[IR_CONSTRUCTION_VALIDATION_OWNERSHIP_BYTES_CLEARED] <= 4);
                    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS] -
                        before.values[IR_CONSTRUCTION_VALIDATION_VALUE_SWEEP_VISITS] == (variant == 1 ? 1u : 0u));
                    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_SPLICE_PARAMETER_BLOCKS] -
                        before.values[IR_CONSTRUCTION_VALIDATION_SPLICE_PARAMETER_BLOCKS] == 1);
                    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_MARKER_INDEX_WORK] -
                        before.values[IR_CONSTRUCTION_VALIDATION_MARKER_INDEX_WORK] <= 64);
                }
#endif
                BUSTER_TEST(arguments, (checked.error == IR_VALIDATION_NONE) == valid);
                IrValidationResult reference = ir_test_validate_canonical_module_reference(&program, &module);
                BUSTER_TEST(arguments, (reference.error == IR_VALIDATION_NONE) == valid);
            }
            arena_set_position(arguments->arena, temporary.position);
        }
    }
    return result;
}
