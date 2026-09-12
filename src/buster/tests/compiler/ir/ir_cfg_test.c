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
    UnitTestResult result = ir_cfg_instruction_span_tests(arguments);
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
