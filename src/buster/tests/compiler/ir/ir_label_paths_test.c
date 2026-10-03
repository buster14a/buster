// Label-path shape and exact FIELD transfer regressions, included by ir_test.c.
// Raw canonical records exercise the public validators in unity and non-unity
// builds. The pairwise transfer oracle deliberately retains the old search.
typedef struct IrTestLabelPathsFixture IrTestLabelPathsFixture;
struct IrTestLabelPathsFixture
{
    IrProgram program;
    IrFunction function;
    IrType types[2];
    IrValue values[2];
    IrValueId metadata_values[2];
    IrValueLabelMetadata metadata[2];
    IrField field;
    IrInstruction instruction;
    IrValueId operand;
    u64 field_index;
};

BUSTER_GLOBAL_LOCAL void ir_test_label_paths_initialize(IrTestLabelPathsFixture* fixture, Arena* arena, IrValueLabelMetadata source,
                                                        IrValueLabelMetadata result, u64 base_offset, u64 base_size)
{
    *fixture = (IrTestLabelPathsFixture){0};
    fixture->field = (IrField){.type = {.value = 1}, .offset = base_offset};
    fixture->types[0] = (IrType){.kind = IR_TYPE_STRUCT, .fields = &fixture->field, .field_count = 1,
        .layout = {.size = UINT64_MAX, .alignment = 8, .resolved = true}};
    fixture->types[1] = (IrType){.kind = IR_TYPE_STRUCT, .layout = {.size = base_size, .alignment = 8, .resolved = true}};
    fixture->values[0] = (IrValue){.canonical_type = {.value = 0}, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_PLACE};
    fixture->values[1] = (IrValue){.canonical_type = {.value = 1}, .definition = {.value = 0}, .category = IR_VALUE_PLACE};
    fixture->metadata_values[0] = (IrValueId){.value = 0};
    fixture->metadata_values[1] = (IrValueId){.value = 1};
    fixture->metadata[0] = source;
    fixture->metadata[1] = result;
    fixture->operand = (IrValueId){.value = 0};
    fixture->instruction = (IrInstruction){.opcode = IR_OPCODE_FIELD, .canonical_type = {.value = 1}, .result = {.value = 1},
        .operands = &fixture->operand, .operand_count = 1, .immediates = &fixture->field_index, .immediate_count = 1};
    fixture->function = (IrFunction){.values = fixture->values, .value_count = 2, .instructions = &fixture->instruction, .instruction_count = 1,
        .label_metadata_values = fixture->metadata_values, .label_metadata = fixture->metadata, .label_metadata_count = 2, .block_count = 8};
    fixture->program = (IrProgram){.arena = arena, .types = {.types = fixture->types, .count = 2}, .data_layout = {.pointer = {.size = 8}}};
}

BUSTER_GLOBAL_LOCAL bool ir_test_label_paths_blocks_equal(IrLabelProvenancePath* left, IrLabelProvenancePath* right)
{
    bool equal = left->is_non_label == right->is_non_label;
    for (u32 direction = 0; equal && direction < 2; direction += 1)
    {
        IrLabelProvenancePath* subset = direction ? right : left;
        IrLabelProvenancePath* superset = direction ? left : right;
        for (u32 index = 0; equal && index < subset->label_block_count; index += 1)
        {
            bool found = false;
            for (u32 other = 0; other < superset->label_block_count; other += 1)
            {
                found |= subset->label_blocks[index].value == superset->label_blocks[other].value;
            }
            equal = found;
        }
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL bool ir_test_label_paths_transfer_oracle(IrValueLabelMetadata* result, IrValueLabelMetadata* source, u64 base_offset, u64 base_size)
{
    bool valid = base_offset <= UINT64_MAX - base_size;
    for (u32 direction = 0; valid && direction < 2; direction += 1)
    {
        u32 count = direction ? result->label_path_count : source->label_path_count;
        for (u32 index = 0; valid && index < count; index += 1)
        {
            IrLabelProvenancePath* path = direction ? result->label_paths + index : source->label_paths + index;
            if (!direction && path->offset > UINT64_MAX - path->size)
            {
                valid = false;
            }
            else if (direction || (path->offset >= base_offset && path->offset + path->size <= base_offset + base_size))
            {
                bool found = false;
                u32 other_count = direction ? source->label_path_count : result->label_path_count;
                for (u32 other = 0; other < other_count; other += 1)
                {
                    IrLabelProvenancePath* source_path = direction ? source->label_paths + other : path;
                    IrLabelProvenancePath* result_path = direction ? path : result->label_paths + other;
                    found |= source_path->offset >= base_offset && source_path->offset <= UINT64_MAX - source_path->size &&
                        source_path->offset + source_path->size <= base_offset + base_size &&
                        result_path->offset == source_path->offset - base_offset && result_path->size == source_path->size &&
                        ir_test_label_paths_blocks_equal(source_path, result_path);
                }
                valid = found;
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_label_paths(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrBlockId blocks[] = {{.value = 1}, {.value = 3}};
    IrBlockId reversed_blocks[] = {{.value = 3}, {.value = 1}};
    IrLabelProvenancePath source_template[] = {
        {.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = 1},
        {.offset = 32, .size = 8, .label_blocks = blocks, .label_block_count = 1},
        {.offset = 40, .size = 8, .is_non_label = true},
        {.offset = 48, .size = 8, .label_blocks = blocks + 1, .label_block_count = 1},
        {.offset = 80, .size = 8, .label_blocks = blocks + 1, .label_block_count = 1},
    };
    IrLabelProvenancePath result_template[] = {
        {.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = 1},
        {.offset = 8, .size = 8, .is_non_label = true},
        {.offset = 16, .size = 8, .label_blocks = blocks + 1, .label_block_count = 1},
    };
    // Every cyclic ordering and its reverse, independently for each side.
    for (u32 source_order = 0; source_order < 10; source_order += 1)
    {
        for (u32 result_order = 0; result_order < 6; result_order += 1)
        {
            IrLabelProvenancePath source_paths[5];
            IrLabelProvenancePath result_paths[3];
            for (u32 index = 0; index < 5; index += 1)
            {
                source_paths[index] = source_template[(source_order / 2 + (source_order & 1 ? 4 - index : index)) % 5];
            }
            for (u32 index = 0; index < 3; index += 1)
            {
                result_paths[index] = result_template[(result_order / 2 + (result_order & 1 ? 2 - index : index)) % 3];
            }
            IrLabelProvenancePath source_before[5];
            IrLabelProvenancePath result_before[3];
            memcpy(source_before, source_paths, sizeof(source_paths));
            memcpy(result_before, result_paths, sizeof(result_paths));
            IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 2, .label_paths = source_paths, .label_path_count = 5,
                .has_label_provenance = true, .has_non_label_provenance = true};
            IrValueLabelMetadata destination = {.label_blocks = reversed_blocks, .label_block_count = 2, .label_paths = result_paths, .label_path_count = 3,
                .has_label_provenance = true, .has_non_label_provenance = true};
            IrTestLabelPathsFixture fixture;
            ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, 32, 24);
            BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&fixture.program, &fixture.function, (IrValueId){.value = 0}));
            BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
            BUSTER_TEST(arguments, ir_test_label_paths_transfer_oracle(&destination, &source, 32, 24));
            BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
            BUSTER_TEST(arguments, memcmp(source_before, source_paths, sizeof(source_paths)) == 0);
            BUSTER_TEST(arguments, memcmp(result_before, result_paths, sizeof(result_paths)) == 0);
            BUSTER_TEST(arguments, blocks[0].value == 1 && blocks[1].value == 3 && reversed_blocks[0].value == 3 && reversed_blocks[1].value == 1);
        }
    }

    // Seventeen rows bypass the tiny permutation path. The source offsets
    // carry a nonzero top byte, requiring all eight radix passes; the result
    // remains a separate scrambled low-offset view after translation.
    for (u32 ordering = 0; ordering < 2; ordering += 1)
    {
        for (u32 variant = 0; variant < 3; variant += 1)
        {
            IrLabelProvenancePath source_paths[17];
            IrLabelProvenancePath result_paths[17];
            u64 base_offset = UINT64_C(0x100000000000000);
            for (u32 index = 0; index < 17; index += 1)
            {
                u32 source_index = ordering ? (index * 7) % 17 : 16 - index;
                u32 result_index = (index * 5) % 17;
                source_paths[index] = (IrLabelProvenancePath){.offset = base_offset + (u64)source_index * 8, .size = 8,
                    .is_non_label = (source_index & 1) != 0, .label_blocks = source_index & 1 ? 0 : blocks,
                    .label_block_count = source_index & 1 ? 0 : 1};
                result_paths[index] = (IrLabelProvenancePath){.offset = (u64)result_index * 8, .size = 8,
                    .is_non_label = (result_index & 1) != 0, .label_blocks = result_index & 1 ? 0 : blocks,
                    .label_block_count = result_index & 1 ? 0 : 1};
                if (result_index == 1 && variant == 1)
                {
                    result_paths[index].size = 7;
                }
                if (result_index == 1 && variant == 2)
                {
                    result_paths[index].is_non_label = false;
                    result_paths[index].label_blocks = blocks;
                    result_paths[index].label_block_count = 1;
                }
            }
            IrLabelProvenancePath source_before[17];
            IrLabelProvenancePath result_before[17];
            memcpy(source_before, source_paths, sizeof(source_paths));
            memcpy(result_before, result_paths, sizeof(result_paths));
            IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 1, .label_paths = source_paths, .label_path_count = 17,
                .has_label_provenance = true, .has_non_label_provenance = true};
            IrValueLabelMetadata destination = {.label_blocks = blocks, .label_block_count = 1, .label_paths = result_paths, .label_path_count = 17,
                .has_label_provenance = true, .has_non_label_provenance = true};
            IrTestLabelPathsFixture fixture;
            ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, base_offset, 17 * 8);
            BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&fixture.program, &fixture.function, (IrValueId){.value = 0}));
            BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
            BUSTER_TEST(arguments, ir_test_label_paths_transfer_oracle(&destination, &source, base_offset, 17 * 8) == (variant == 0));
            BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}) == (variant == 0));
            BUSTER_TEST(arguments, memcmp(source_before, source_paths, sizeof(source_paths)) == 0);
            BUSTER_TEST(arguments, memcmp(result_before, result_paths, sizeof(result_paths)) == 0);
        }
    }

    // Each fault starts from the same touching, nonoverlapping valid paths.
    for (u32 variant = 0; variant < 14; variant += 1)
    {
        IrLabelProvenancePath source_paths[5];
        IrLabelProvenancePath result_paths[4];
        memcpy(source_paths, source_template, sizeof(source_paths));
        memcpy(result_paths, result_template, sizeof(result_template));
        result_paths[3] = (IrLabelProvenancePath){.offset = 24, .size = 8, .is_non_label = true};
        IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 2, .label_paths = source_paths, .label_path_count = 5,
            .has_label_provenance = true, .has_non_label_provenance = true};
        IrValueLabelMetadata destination = {.label_blocks = reversed_blocks, .label_block_count = 2, .label_paths = result_paths, .label_path_count = 3,
            .has_label_provenance = true, .has_non_label_provenance = true};
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, 32, 24);
        switch (variant)
        {
        case 0: break;
        case 1: fixture.metadata[1].label_path_count = 2; break;
        case 2: fixture.metadata[1].label_path_count = 4; fixture.types[1].layout.size = 32; break;
        case 3: result_paths[2].offset = 15; break;
        case 4: result_paths[1].size = 7; break;
        case 5: result_paths[1].is_non_label = false; result_paths[1].label_blocks = blocks; result_paths[1].label_block_count = 1; break;
        case 6: result_paths[0].label_blocks = blocks + 1; break;
        case 7: source_paths[4].offset = UINT64_MAX - 3; break;
        case 8: fixture.field.offset = UINT64_MAX - 3; break;
        case 9: result_paths[0].label_blocks = reversed_blocks; result_paths[0].label_block_count = 2; break;
        case 10:
            source_paths[1].label_blocks = blocks; source_paths[1].label_block_count = 2;
            result_paths[0].label_blocks = reversed_blocks; result_paths[0].label_block_count = 2;
            break;
        case 11: result_paths[0].offset = UINT64_MAX - 3; break;
        case 12: fixture.metadata[1].label_paths = 0; break;
        case 13: fixture.metadata[0].label_paths = 0; break;
        default: BUSTER_TODO(); break;
        }
        bool expected = variant == 0 || variant == 10;
        BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}) == expected);
        if (variant < 12)
        {
            BUSTER_TEST(arguments, ir_test_label_paths_transfer_oracle(&fixture.metadata[1], &fixture.metadata[0], fixture.field.offset,
                fixture.types[1].layout.size) == expected);
        }
    }

    // Partial paths at either end are excluded; the contained touching paths
    // remain exact. A window with no contained source has no result paths.
    {
        IrLabelProvenancePath source_paths[] = {
            {.offset = 28, .size = 8, .label_blocks = blocks, .label_block_count = 1},
            {.offset = 40, .size = 8, .label_blocks = blocks, .label_block_count = 1},
            {.offset = 48, .size = 8, .is_non_label = true},
            {.offset = 56, .size = 8, .label_blocks = blocks, .label_block_count = 1},
        };
        IrLabelProvenancePath result_paths[] = {
            {.offset = 16, .size = 8, .is_non_label = true},
            {.offset = 8, .size = 8, .label_blocks = blocks, .label_block_count = 1},
        };
        IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 1, .label_paths = source_paths, .label_path_count = 4,
            .has_label_provenance = true, .has_non_label_provenance = true};
        IrValueLabelMetadata destination = {.label_blocks = blocks, .label_block_count = 1, .label_paths = result_paths, .label_path_count = 2,
            .has_label_provenance = true, .has_non_label_provenance = true};
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, 32, 28);
        BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
        fixture.field.offset = 8;
        fixture.types[1].layout.size = 8;
        fixture.metadata[1] = (IrValueLabelMetadata){0};
        BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
    }

    // Shape rejection is independent of transfer, including nonadjacent input
    // overlaps where an adjacent comparison before sorting misses the fault.
    for (u32 variant = 0; variant < 7; variant += 1)
    {
        IrLabelProvenancePath paths[] = {
            {.offset = 16, .size = 8, .label_blocks = blocks, .label_block_count = 1},
            {.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = 1},
            {.offset = 8, .size = 8, .is_non_label = true},
        };
        switch (variant)
        {
        case 0: break;
        case 1: paths[0].offset = 15; break;
        case 2: paths[2].offset = 0; break;
        case 3: paths[1].size = 24; paths[1].is_non_label = true; paths[1].label_blocks = 0; paths[1].label_block_count = 0; break;
        case 4: paths[2].size = 0; break;
        case 5: paths[0].offset = UINT64_MAX - 3; break;
        case 6: paths[2].offset = 24; break;
        default: BUSTER_TODO(); break;
        }
        IrValueLabelMetadata metadata = {.label_blocks = blocks, .label_block_count = 1, .label_paths = paths, .label_path_count = 3,
            .has_label_provenance = true, .has_non_label_provenance = true};
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, metadata, metadata, 0, 24);
        IrLabelProvenancePath before[3];
        memcpy(before, paths, sizeof(paths));
        BUSTER_TEST(arguments, ir_label_metadata_shape_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}) == (variant == 0));
        BUSTER_TEST(arguments, memcmp(before, paths, sizeof(paths)) == 0);
    }

    // Transfer runs before shape validation. Missing backing must fail before
    // FIELD/LOAD/CAST inspect flags, paths, or path block sets.
    IrOpcode backing_opcodes[] = {IR_OPCODE_FIELD, IR_OPCODE_LOAD, IR_OPCODE_CAST};
    for (u32 opcode_index = 0; opcode_index < BUSTER_ARRAY_LENGTH(backing_opcodes); opcode_index += 1)
    {
        for (u32 variant = 0; variant < 10; variant += 1)
        {
            IrLabelProvenancePath source_path = {.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = 1};
            IrLabelProvenancePath result_path = source_path;
            IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 1, .label_paths = &source_path, .label_path_count = 1,
                .has_label_provenance = true};
            IrValueLabelMetadata destination = {.label_blocks = blocks, .label_block_count = 1, .label_paths = &result_path, .label_path_count = 1,
                .has_label_provenance = true};
            IrTestLabelPathsFixture fixture;
            ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, 0, 8);
            fixture.instruction.opcode = backing_opcodes[opcode_index];
            switch (variant)
            {
            case 0: fixture.metadata[0] = (IrValueLabelMetadata){.label_path_count = 1}; break;
            case 1: fixture.metadata[1] = (IrValueLabelMetadata){.label_path_count = 1}; break;
            case 2: fixture.metadata[0].label_blocks = 0; break;
            case 3: fixture.metadata[1].label_blocks = 0; break;
            case 4: source_path.label_blocks = 0; break;
            case 5: result_path.label_blocks = 0; break;
            case 6: fixture.metadata[0].label_path_count = 0; break;
            case 7: fixture.metadata[1].label_path_count = 0; break;
            case 8: fixture.metadata[0].label_block_count = 0; break;
            case 9: fixture.metadata[1].label_block_count = 0; break;
            default: BUSTER_TODO(); break;
            }
            BUSTER_TEST(arguments, !ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
        }
    }
    {
        // A direct-label LOAD reaches value_has_non_label_path instead of
        // has_label. Its source can advertise storage provenance with no path
        // backing; that must be refused before either scan.
        IrLabelProvenancePath source_path = {.offset = 0, .size = 8, .label_blocks = blocks, .label_block_count = 1};
        IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 1, .label_paths = &source_path, .label_path_count = 1,
            .has_label_provenance = true};
        IrValueLabelMetadata destination = {.label_blocks = blocks, .label_block_count = 1, .is_label_value = true};
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, 0, 8);
        fixture.instruction.opcode = IR_OPCODE_LOAD;
        BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
        fixture.metadata[0].label_paths = 0;
        BUSTER_TEST(arguments, !ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1}));
    }
    {
        // The public result/first guard alone does not inspect operand two.
        // This malformed operand used to reach the aggregate's has_label scan.
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, (IrValueLabelMetadata){0}, (IrValueLabelMetadata){0}, 0, 8);
        IrValue values[3] = {fixture.values[0], fixture.values[0], fixture.values[1]};
        IrValueId metadata_values[] = {{.value = 0}, {.value = 1}, {.value = 2}};
        IrValueId operands[] = {{.value = 0}, {.value = 1}};
        IrValueLabelMetadata metadata[3] = {0};
        fixture.function.values = values;
        fixture.function.value_count = 3;
        fixture.function.label_metadata_values = metadata_values;
        fixture.function.label_metadata = metadata;
        fixture.function.label_metadata_count = 3;
        fixture.instruction.opcode = IR_OPCODE_AGGREGATE;
        fixture.instruction.result.value = 2;
        fixture.instruction.operands = operands;
        fixture.instruction.operand_count = 2;
        BUSTER_TEST(arguments, ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 2}));
        metadata[1].label_path_count = 1;
        BUSTER_TEST(arguments, !ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 2}));
        metadata[1] = (IrValueLabelMetadata){.label_blocks = 0, .label_block_count = 1};
        BUSTER_TEST(arguments, !ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 2}));
        IrLabelProvenancePath malformed_path = {.offset = 0, .size = 8, .label_block_count = 1};
        metadata[1] = (IrValueLabelMetadata){.label_paths = &malformed_path, .label_path_count = 1};
        BUSTER_TEST(arguments, !ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 2}));
    }

    // The public block-parameter helper also precedes shape validation when
    // called standalone. Both destination and incoming backing need guards.
    for (u32 variant = 0; variant < 7; variant += 1)
    {
        IrValueLabelMetadata direct_label = {.label_blocks = blocks, .label_block_count = 1, .is_label_value = true};
        IrTestLabelPathsFixture fixture;
        ir_test_label_paths_initialize(&fixture, arguments->arena, direct_label, direct_label, 0, 8);
        IrIncoming incoming = {.value = {.value = 0}, .predecessor = {.value = 0}};
        IrBlockParameter parameter = {.value = {.value = 1}, .canonical_type = {.value = 1},
            .incoming_count = 1, .first_incoming = &incoming, .last_incoming = &incoming};
        IrLabelProvenancePath malformed_path = {.offset = 0, .size = 8, .label_block_count = 1};
        switch (variant)
        {
        case 0: break;
        case 1: fixture.metadata[0] = (IrValueLabelMetadata){.label_path_count = 1}; break;
        case 2: fixture.metadata[1] = (IrValueLabelMetadata){.label_path_count = 1}; break;
        case 3: fixture.metadata[0].label_blocks = 0; break;
        case 4: fixture.metadata[1].label_blocks = 0; break;
        case 5: fixture.metadata[0] = (IrValueLabelMetadata){.label_paths = &malformed_path, .label_path_count = 1}; break;
        case 6: fixture.metadata[1] = (IrValueLabelMetadata){.label_paths = &malformed_path, .label_path_count = 1}; break;
        default: BUSTER_TODO(); break;
        }
        BUSTER_TEST(arguments, ir_label_block_parameter_provenance_valid(&fixture.function, &parameter) == (variant == 0));
    }

#if BUSTER_BENCH_ALLOCATIONS
    // Geometric rows bound the actual recorded path work, rather than elapsed
    // time. Reverse both arrays to require immutable radix permutations.
    for (u32 rows = 64; rows <= 4096; rows *= 4)
    {
        IrLabelProvenancePath* source_paths = arena_allocate(arguments->arena, IrLabelProvenancePath, rows);
        IrLabelProvenancePath* result_paths = arena_allocate(arguments->arena, IrLabelProvenancePath, rows);
        for (u32 order = 0; order < 2; order += 1)
        {
            u64 base_offset = UINT64_C(0x100000000);
            for (u32 index = 0; index < rows; index += 1)
            {
                u64 offset = (u64)(order ? rows - 1 - index : index) * 8;
                source_paths[index] = (IrLabelProvenancePath){.offset = base_offset + offset, .size = 8, .label_blocks = blocks, .label_block_count = 1};
                result_paths[index] = (IrLabelProvenancePath){.offset = offset, .size = 8, .label_blocks = blocks, .label_block_count = 1};
            }
            IrValueLabelMetadata source = {.label_blocks = blocks, .label_block_count = 1, .label_paths = source_paths, .label_path_count = rows,
                .has_label_provenance = true};
            IrValueLabelMetadata destination = {.label_blocks = blocks, .label_block_count = 1, .label_paths = result_paths, .label_path_count = rows,
                .has_label_provenance = true};
            IrTestLabelPathsFixture fixture;
            ir_test_label_paths_initialize(&fixture, arguments->arena, source, destination, base_offset, (u64)rows * 8);
            IrConstructionCounters before = ir_construction_counters();
            bool valid = ir_label_metadata_transfer_valid(&fixture.program, &fixture.function, (IrValueId){.value = 1});
            IrConstructionCounters after = ir_construction_counters();
            u64 work = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_PATH_WORK] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_PATH_WORK];
            u64 scratch = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES];
            BUSTER_TEST(arguments, valid && !before.overflowed && !after.overflowed);
            BUSTER_TEST(arguments, work >= 4 * (u64)rows && work <= 20 * (u64)rows);
            BUSTER_TEST(arguments, scratch == (order ? 4 * sizeof(IrLabelProvenancePath*) * (u64)rows : 0));
            BUSTER_TEST(arguments, source_paths[0].offset == base_offset + (order ? (u64)(rows - 1) * 8 : 0));
            BUSTER_TEST(arguments, result_paths[0].offset == (order ? (u64)(rows - 1) * 8 : 0));
        }
    }
#endif
    return result;
}
