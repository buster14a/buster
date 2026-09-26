// Included by ir_test.c. The synthetic builder is deliberately independent of
// publication: incoming lists run in reverse source order; expected edge rows
// are derived from the fixture's integer formula, not a second publisher.
BUSTER_GLOBAL_LOCAL IrFunction ir_cfg_test_function(Arena* arena, u32 degree, u32 parameter_count)
{
    IrFunction function = {.block_count = degree + 1, .instruction_count = degree + 1,
                           .value_count = parameter_count * (degree + 1), .state = IR_FUNCTION_LOWERED};
    function.blocks = arena_allocate(arena, IrBlock, function.block_count);
    function.instructions = arena_allocate(arena, IrInstruction, function.instruction_count);
    function.values = arena_allocate(arena, IrValue, function.value_count);
    memset(function.values, 0, sizeof(*function.values) * function.value_count);
    IrBlockId* targets = arena_allocate(arena, IrBlockId, 2);
    targets[0].value = degree;
    targets[1].value = degree;
    for (u32 index = 0; index <= degree; index += 1)
    {
        function.blocks[index] = (IrBlock){.id = {.value = index}, .first_instruction = {.value = index},
                                         .last_instruction = {.value = index}, .terminated = true, .sealed = true};
        function.instructions[index] = (IrInstruction){.next = IR_INSTRUCTION_ID_INVALID, .result = IR_VALUE_ID_INVALID,
            .opcode = index == degree ? IR_OPCODE_RETURN : IR_OPCODE_BRANCH, .targets = index == degree ? 0 : targets,
            .target_count = index == degree ? 0 : 2};
    }
    IrBlock* join = function.blocks + degree;
    IrPredecessor* predecessors = arena_allocate(arena, IrPredecessor, degree);
    for (u32 index = 0; index < degree; index += 1)
    {
        predecessors[index] = (IrPredecessor){.block = {.value = degree - index - 1},
            .next = index + 1 < degree ? predecessors + index + 1 : 0};
    }
    join->predecessor_count = degree;
    join->first_predecessor = degree ? predecessors : 0;
    join->last_predecessor = degree ? predecessors + degree - 1 : 0;
    IrBlockParameter* parameters = arena_allocate(arena, IrBlockParameter, parameter_count);
    IrIncoming* incoming = arena_allocate(arena, IrIncoming, (u64)degree * parameter_count);
    for (u32 index = 0; index < parameter_count; index += 1)
    {
        IrIncoming* column = incoming + (u64)index * degree;
        parameters[index] = (IrBlockParameter){.value = {.value = index}, .incoming_count = degree,
            .first_incoming = degree ? column : 0, .last_incoming = degree ? column + degree - 1 : 0,
            .next = index + 1 < parameter_count ? parameters + index + 1 : 0};
        for (u32 pred = 0; pred < degree; pred += 1)
        {
            u32 source = degree - pred - 1;
            column[pred] = (IrIncoming){.predecessor = {.value = source},
                .value = {.value = parameter_count + source * parameter_count + index},
                .next = pred + 1 < degree ? column + pred + 1 : 0};
        }
    }
    join->parameter_count = parameter_count;
    join->first_parameter = parameter_count ? parameters : 0;
    join->last_parameter = parameter_count ? parameters + parameter_count - 1 : 0;
    return function;
}

BUSTER_GLOBAL_LOCAL IrFunction ir_cfg_pool_test_function(IrInstruction* rows, u32 row_count, IrBlock* block)
{
    for (u32 index = 0; index < row_count; index += 1)
    {
        rows[index].next.value = index + 1 < row_count ? index + 1 : IR_ID_UNDERLYING_INVALID;
        rows[index].result = IR_VALUE_ID_INVALID;
    }
    rows[row_count - 1].opcode = IR_OPCODE_RETURN;
    *block = (IrBlock){.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = row_count - 1},
                       .terminated = true, .sealed = true};
    return (IrFunction){.blocks = block, .block_count = 1, .instructions = rows, .instruction_count = row_count,
                        .state = IR_FUNCTION_LOWERED};
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_pool_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 base_allocated_bytes = sizeof(IrPublishedCfg) + sizeof(IrCfgBlock);
    u64 narrow_pool_count_bound = (u64)UINT32_MAX * (u64)UINT16_MAX;
    BUSTER_TEST(arguments, narrow_pool_count_bound == UINT64_C(281470681677825));
    BUSTER_TEST(arguments, narrow_pool_count_bound <= UINT64_MAX / sizeof(IrBlockId));
    BUSTER_TEST(arguments, narrow_pool_count_bound <= UINT64_MAX / sizeof(u64));

    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrInstruction rows[1] = {0};
        IrBlock block = {0};
        IrFunction function = ir_cfg_pool_test_function(rows, BUSTER_ARRAY_LENGTH(rows), &block);
        BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
        IrPublishedCfg const* cfg = function.published_cfg;
        if (BUSTER_REQUIRE(arguments, cfg != 0))
        {
            BUSTER_TEST(arguments, !cfg->operand_pool && !cfg->target_pool && !cfg->immediate_pool);
            BUSTER_TEST(arguments, !cfg->operand_count && !cfg->target_count && !cfg->immediate_count);
            BUSTER_TEST(arguments, cfg->allocated_bytes == base_allocated_bytes);
        }
        scratch_end(temporary);
    }

    for (u32 scattered_pool = 0; scattered_pool < 4; scattered_pool += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrValueId operands[] = {{.value = 10}, {.value = 11}, {.value = 12}, {.value = 13}, {.value = 13}};
        IrBlockId targets[] = {{.value = 20}, {.value = 21}, {.value = 22}, {.value = 23}, {.value = 23}};
        u64 immediates[] = {30, 31, 32, 33, 33};
        IrInstruction rows[3] = {0};
        IrBlock block = {0};
        IrFunction function = ir_cfg_pool_test_function(rows, BUSTER_ARRAY_LENGTH(rows), &block);
        if (scattered_pool == 0)
        {
            operands[2].value = 99;
            operands[3].value = 12;
        }
        else if (scattered_pool == 1)
        {
            targets[2].value = 99;
            targets[3].value = 22;
        }
        else if (scattered_pool == 2)
        {
            immediates[2] = 99;
            immediates[3] = 32;
        }
        rows[0].operands = operands;
        rows[0].operand_count = 2;
        rows[0].targets = targets;
        rows[0].target_count = 2;
        rows[0].immediates = immediates;
        rows[0].immediate_count = 2;
        rows[1].operands = operands + (scattered_pool == 0 ? 3 : 2);
        rows[1].operand_count = 2;
        rows[1].targets = targets + (scattered_pool == 1 ? 3 : 2);
        rows[1].target_count = 2;
        rows[1].immediates = immediates + (scattered_pool == 2 ? 3 : 2);
        rows[1].immediate_count = 2;
        BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
        IrPublishedCfg const* cfg = function.published_cfg;
        if (BUSTER_REQUIRE(arguments, cfg != 0))
        {
            u64 expected_allocated_bytes = base_allocated_bytes;
            expected_allocated_bytes += scattered_pool == 0 ? 4 * sizeof(IrValueId) : 0;
            expected_allocated_bytes += scattered_pool == 1 ? 4 * sizeof(IrBlockId) : 0;
            expected_allocated_bytes += scattered_pool == 2 ? 4 * sizeof(u64) : 0;
            BUSTER_TEST(arguments, cfg->operand_count == 4 && cfg->target_count == 4 && cfg->immediate_count == 4);
            BUSTER_TEST(arguments, cfg->allocated_bytes == expected_allocated_bytes);
            BUSTER_TEST(arguments, (cfg->operand_pool == operands) == (scattered_pool != 0));
            BUSTER_TEST(arguments, (cfg->target_pool == targets) == (scattered_pool != 1));
            BUSTER_TEST(arguments, (cfg->immediate_pool == immediates) == (scattered_pool != 2));
            BUSTER_TEST(arguments, rows[0].operands == cfg->operand_pool);
            BUSTER_TEST(arguments, rows[0].targets == cfg->target_pool);
            BUSTER_TEST(arguments, rows[0].immediates == cfg->immediate_pool);
            BUSTER_TEST(arguments, rows[1].operands == cfg->operand_pool + 2);
            BUSTER_TEST(arguments, rows[1].targets == cfg->target_pool + 2);
            BUSTER_TEST(arguments, rows[1].immediates == cfg->immediate_pool + 2);
            for (u32 index = 0; index < 4; index += 1)
            {
                BUSTER_TEST(arguments, cfg->operand_pool[index].value == 10 + index);
                BUSTER_TEST(arguments, cfg->target_pool[index].value == 20 + index);
                BUSTER_TEST(arguments, cfg->immediate_pool[index] == 30 + index);
            }
        }
        scratch_end(temporary);
    }

    for (u32 missing_pool = 0; missing_pool < 3; missing_pool += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrInstruction rows[2] = {0};
        IrBlock block = {0};
        IrFunction function = ir_cfg_pool_test_function(rows, BUSTER_ARRAY_LENGTH(rows), &block);
        rows[0].operand_count = missing_pool == 0;
        rows[0].target_count = (u16)(missing_pool == 1);
        rows[0].immediate_count = (u16)(missing_pool == 2);
        IrValidationResult publication = ir_function_publish_cfg(arguments->arena, &function);
        BUSTER_TEST(arguments, publication.error == IR_VALIDATION_OPERATION);
        BUSTER_TEST(arguments, publication.boundary == IR_VALIDATION_BOUNDARY_CFG_PUBLICATION);
        BUSTER_TEST(arguments, !function.published_cfg);
        scratch_end(temporary);
    }

    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrBlockId* targets = arena_allocate(arguments->arena, IrBlockId, UINT16_MAX);
        u64* immediates = arena_allocate(arguments->arena, u64, UINT16_MAX);
        IrInstruction rows[2] = {0};
        IrBlock block = {0};
        IrFunction function = ir_cfg_pool_test_function(rows, BUSTER_ARRAY_LENGTH(rows), &block);
        targets[0].value = 41;
        targets[UINT16_MAX - 1].value = 42;
        immediates[0] = 51;
        immediates[UINT16_MAX - 1] = 52;
        rows[0].targets = targets;
        rows[0].target_count = UINT16_MAX;
        rows[0].immediates = immediates;
        rows[0].immediate_count = UINT16_MAX;
        BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
        IrPublishedCfg const* cfg = function.published_cfg;
        if (BUSTER_REQUIRE(arguments, cfg != 0))
        {
            BUSTER_TEST(arguments, cfg->target_count == UINT16_MAX && cfg->immediate_count == UINT16_MAX);
            BUSTER_TEST(arguments, cfg->target_pool == targets && cfg->immediate_pool == immediates);
            BUSTER_TEST(arguments, cfg->target_pool[0].value == 41 && cfg->target_pool[UINT16_MAX - 1].value == 42);
            BUSTER_TEST(arguments, cfg->immediate_pool[0] == 51 && cfg->immediate_pool[UINT16_MAX - 1] == 52);
            BUSTER_TEST(arguments, cfg->allocated_bytes == base_allocated_bytes);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_instruction_span_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    IrFunction function = {.block_count = 2, .instruction_count = 5, .value_count = 5, .extra_count = 2,
                           .entry = {.value = 1}, .state = IR_FUNCTION_LOWERED};
    IrBlock blocks[] = {
        {.id = {.value = 0}, .first_instruction = {.value = 0}, .last_instruction = {.value = 3}},
        {.id = {.value = 1}, .first_instruction = {.value = 1}, .last_instruction = {.value = 4}},
    };
    IrInstruction rows[5] = {0};
    IrValue values[5] = {0};
    IrSourceRange sources[5] = {0};
    IrValueId shared_operands[] = {{.value = 0}, {.value = 1}, {.value = 2}, {.value = 3}, {.value = 4}};
    u32 next[] = {2, 4, 3, IR_ID_UNDERLYING_INVALID, IR_ID_UNDERLYING_INVALID};
    IrInstructionId extra_ids[] = {{.value = 1}, {.value = 2}};
    IrInstructionExtra extras[] = {{.literal = S8_INITIALIZER("old-one")}, {.literal = S8_INITIALIZER("old-two")}};
    for (u32 index = 0; index < 5; index += 1)
    {
        rows[index].next.value = next[index];
        rows[index].result.value = index;
        rows[index].symbol.value = index + 10;
        rows[index].operands = shared_operands + index;
        rows[index].operand_count = index < 4 ? 2 : 1;
        values[index].definition.value = index;
        sources[index].offset = index + 100;
    }
    // Publication requires each chain to end in its one terminator.
    rows[3].opcode = IR_OPCODE_UNREACHABLE;
    rows[4].opcode = IR_OPCODE_UNREACHABLE;
    function.blocks = blocks;
    function.instructions = rows;
    function.values = values;
    function.instruction_canonical_sources = sources;
    function.extra_instructions = extra_ids;
    function.extras = extras;
    BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
    IrPublishedCfg const* cfg = function.published_cfg;
    BUSTER_TEST(arguments, cfg && cfg->instruction_remap && cfg->instruction_count == 5);
    if (cfg && cfg->instruction_remap)
    {
        // The 1->3->2->1 cycle distinguishes the old-to-new map from its
        // inverse. Operand slices deliberately overlap before publication.
        u32 expected_remap[] = {0, 3, 1, 2, 4};
        u32 expected_rows[] = {0, 2, 3, 1, 4};
        BUSTER_TEST(arguments, cfg->operand_count == 9 && cfg->operand_pool != shared_operands);
        for (u32 index = 0; index < 5; index += 1)
        {
            u32 old = expected_rows[index];
            BUSTER_TEST(arguments, cfg->instruction_remap[index].value == expected_remap[index]);
            BUSTER_TEST(arguments, values[index].definition.value == expected_remap[index]);
            BUSTER_TEST(arguments, rows[index].result.value == old);
            BUSTER_TEST(arguments, rows[index].symbol.value == old + 10);
            BUSTER_TEST(arguments, sources[index].offset == old + 100);
            BUSTER_TEST(arguments, rows[index].operand_count == (old < 4 ? 2u : 1u));
            for (u32 operand = 0; operand < rows[index].operand_count; operand += 1)
            {
                BUSTER_TEST(arguments, rows[index].operands[operand].value == old + operand);
            }
            BUSTER_TEST(arguments, rows[index].next.value == IR_ID_UNDERLYING_INVALID);
            // Spans stay authoritative even if inactive construction links are poisoned.
            rows[index].next.value = UINT32_MAX - 1;
            IrInstructionId published_next = ir_block_next_instruction(&function, blocks + (index < 3 ? 0 : 1), (IrInstructionId){.value = index});
            BUSTER_TEST(arguments, published_next.value == (index == 2 || index == 4 ? UINT32_MAX : index + 1));
        }
        BUSTER_TEST(arguments, cfg->blocks[0].first_instruction == 0 && cfg->blocks[0].instruction_count == 3);
        BUSTER_TEST(arguments, cfg->blocks[1].first_instruction == 3 && cfg->blocks[1].instruction_count == 2);
        BUSTER_TEST(arguments, blocks[0].last_instruction.value == 2 && blocks[1].first_instruction.value == 3);
        BUSTER_TEST(arguments, extra_ids[0].value == 1 && extra_ids[1].value == 3);
        BUSTER_TEST(arguments, string_equal(extras[0].literal, S8("old-two")) && string_equal(extras[1].literal, S8("old-one")));
        BUSTER_TEST(arguments, function.entry.value == 1);
        IrBlockId owners[5];
        BUSTER_TEST(arguments, ir_function_instruction_owners(&function, owners).error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, owners[0].value == 0 && owners[1].value == 0 && owners[2].value == 0 && owners[3].value == 1 && owners[4].value == 1);
        ir_function_invalidate_cfg(&function);
        BUSTER_TEST(arguments, !function.published_cfg && rows[0].next.value == 1 && rows[1].next.value == 2 && rows[3].next.value == 4);
        // Mutating one reopened operand must not alter the earlier row whose
        // construction slice shared that element, or the original slice.
        rows[3].operands[0].value = 4;
        BUSTER_TEST(arguments, rows[0].operands[1].value == 1 && shared_operands[1].value == 1);
        BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, function.published_cfg && !function.published_cfg->instruction_remap);
        BUSTER_TEST(arguments, rows[3].operands[0].value == 4 && rows[0].operands[1].value == 1);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_publication_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_cfg_pool_tests(arguments);
    UnitTestResult spans = ir_cfg_instruction_span_tests(arguments);
    result.test_count += spans.test_count;
    result.succeeded_test_count += spans.succeeded_test_count;
    u32 degrees[] = {0, 1, 2, 17, 4096};
    u32 widths[] = {0, 1, 2, 32, 33};
    for (u32 di = 0; di < BUSTER_ARRAY_LENGTH(degrees); di += 1)
    {
        for (u32 wi = 0; wi < BUSTER_ARRAY_LENGTH(widths); wi += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            u32 degree = degrees[di];
            u32 width = widths[wi];
            IrFunction function = ir_cfg_test_function(arguments->arena, degree, width);
            IrFunction before = function;
            BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
            IrPublishedCfg const* cfg = function.published_cfg;
            BUSTER_TEST(arguments, cfg != 0);
            if (cfg)
            {
                BUSTER_TEST(arguments, cfg->block_count == degree + 1 && cfg->edge_count == degree);
                BUSTER_TEST(arguments, cfg->parameter_count == width && cfg->argument_count == degree * width);
                BUSTER_TEST(arguments, function.blocks == before.blocks && function.instructions == before.instructions && function.values == before.values);
                BUSTER_TEST(arguments, function.entry.value == before.entry.value && function.instruction_count == before.instruction_count);
                for (u32 source = 0; source < degree; source += 1)
                {
                    IrCfgEdge const* edge = ir_function_cfg_edge(&function, (IrBlockId){.value = source}, (IrBlockId){.value = degree});
                    BUSTER_TEST(arguments, edge == cfg->edges + source);
                    BUSTER_TEST(arguments, cfg->blocks[source].successor_count == 1 && cfg->blocks[source].successor_offset == source);
                    BUSTER_TEST(arguments, cfg->predecessors[source] == source);
                    if (edge)
                    {
                        for (u32 parameter = 0; parameter < width; parameter += 1)
                        {
                            BUSTER_TEST(arguments, cfg->arguments[edge->argument_offset + parameter].value == width + source * width + parameter);
                        }
                    }
                }
                for (u32 parameter = 0; parameter < width; parameter += 1)
                {
                    BUSTER_TEST(arguments, cfg->parameters[parameter].value.value == parameter);
                }
                BUSTER_TEST(arguments, cfg->blocks[degree].predecessor_count == degree && cfg->blocks[degree].parameter_count == width);
                BUSTER_TEST(arguments, ir_function_cfg_edge(&function, (IrBlockId){.value = degree}, (IrBlockId){.value = degree}) == 0);
                BUSTER_TEST(arguments, ir_function_cfg_edge(&function, (IrBlockId){.value = degree + 1}, (IrBlockId){.value = degree}) == 0);
                BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE && function.published_cfg == cfg);
                ir_function_invalidate_cfg(&function);
                BUSTER_TEST(arguments, !function.published_cfg);
            }
            scratch_end(temporary);
        }
    }
    for (u32 variant = 0; variant < 13; variant += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrFunction function = ir_cfg_test_function(arguments->arena, 2, 2);
        IrBlock* join = function.blocks + 2;
        switch (variant)
        {
            case 0: join->first_parameter->first_incoming = 0; break;
            case 1: join->first_parameter->last_incoming->next = join->first_parameter->first_incoming; break;
            case 2: join->first_parameter->last_incoming = join->first_parameter->first_incoming; break;
            case 3: join->first_parameter->incoming_count += 1; break;
            case 4: join->parameter_count = 1; break;
            case 5: join->parameter_count = 3; break;
            case 6: join->last_parameter = join->first_parameter; break;
            case 7: join->last_predecessor->next = join->first_predecessor; break;
            case 8: join->last_predecessor->block = join->first_predecessor->block; break;
            case 9: join->predecessor_count = 1; break;
            case 10: join->first_predecessor->block.value = 2; break;
            case 11: function.instructions[0].targets[0].value = 3; break;
            case 12: function.instructions[0].targets = 0; break;
        }
        IrValidationResult publication = ir_function_publish_cfg(arguments->arena, &function);
        BUSTER_TEST(arguments, publication.error != IR_VALIDATION_NONE && publication.boundary == IR_VALIDATION_BOUNDARY_CFG_PUBLICATION);
        BUSTER_TEST(arguments, !function.published_cfg);
        scratch_end(temporary);
    }
    // No-parameter destinations may omit predecessor construction entirely;
    // their CFG still includes all terminator edges.
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    IrFunction function = ir_cfg_test_function(arguments->arena, 2, 0);
    function.blocks[2].first_predecessor = 0;
    function.blocks[2].last_predecessor = 0;
    function.blocks[2].predecessor_count = 0;
    BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE);
    BUSTER_TEST(arguments, function.published_cfg && function.published_cfg->edge_count == 2);
    scratch_end(temporary);
    return result;
}
