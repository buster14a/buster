// The mutable map may be consumed by publication, but the published old-to-new
// map, value definitions and source/extra associations must survive unchanged.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_permutation_ownership_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 counts[] = {1, 2, 5, 17, 4096};
    for (u32 size = 0; size < BUSTER_ARRAY_LENGTH(counts); size += 1)
    {
        for (u32 variant = 0; variant < 8; variant += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            u32 count = counts[size];
            u32 block_count = count < 3 ? 1 : 3;
            u32 extra_count = count < 3 ? count : 3;
            IrInstruction* rows = arena_allocate_zeroed(arguments->arena, IrInstruction, count);
            IrValue* values = arena_allocate_zeroed(arguments->arena, IrValue, count);
            IrSourceRange* sources = variant < 4 ? arena_allocate_zeroed(arguments->arena, IrSourceRange, count) : 0;
            IrBlock* blocks = arena_allocate_zeroed(arguments->arena, IrBlock, block_count);
            u32* order = arena_allocate(arguments->arena, u32, count);
            u32* expected = arena_allocate(arguments->arena, u32, count);
            IrInstructionId extra_ids[3] = {0};
            IrInstructionExtra extras[3] = {0};
            String8 labels[] = {S8("first"), S8("middle"), S8("last")};
            u32 original_extra_ids[3] = {0};
            bool moved = false;
            for (u32 index = 0; index < count; index += 1)
            {
                u32 old = index;
                switch (variant % 4)
                {
                    case 1: old = count - 1 - index; break;
                    case 2: old = (index + 1) % count; break;
                    case 3: old = (index ^ 1) < count ? index ^ 1 : index; break;
                    default: break;
                }
                order[index] = old;
                expected[old] = index;
                moved |= old != index;
                rows[index].result.value = index;
                rows[index].symbol.value = index + 10;
                values[index].definition.value = index;
                if (sources) sources[index].offset = index + 100;
            }
            for (u32 index = 0; index < extra_count; index += 1)
            {
                original_extra_ids[index] = extra_count == 1 ? 0 : index * (count - 1) / (extra_count - 1);
                extra_ids[index].value = original_extra_ids[index];
                extras[index].literal = labels[index];
            }
            for (u32 block = 0; block < block_count; block += 1)
            {
                u32 first = block * count / block_count;
                u32 end = (block + 1) * count / block_count;
                blocks[block].id.value = block;
                blocks[block].first_instruction.value = order[first];
                blocks[block].last_instruction.value = order[end - 1];
                for (u32 index = first; index < end; index += 1)
                {
                    rows[order[index]].next.value = index + 1 < end ? order[index + 1] : IR_ID_UNDERLYING_INVALID;
                }
            }
            IrFunction function = {.blocks = blocks, .block_count = block_count, .instructions = rows, .instruction_count = count,
                .values = values, .value_count = count, .instruction_canonical_sources = sources,
                .extras = extras, .extra_instructions = extra_ids, .extra_count = extra_count,
                .entry = {.value = block_count - 1}, .state = IR_FUNCTION_LOWERED};
            if (BUSTER_REQUIRE(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE && function.published_cfg))
            {
                IrPublishedCfg const* cfg = function.published_cfg;
                BUSTER_TEST(arguments, (cfg->instruction_remap != 0) == moved);
                for (u32 index = 0; index < count; index += 1)
                {
                    BUSTER_TEST(arguments, rows[index].result.value == order[index] && rows[index].symbol.value == order[index] + 10);
                    BUSTER_TEST(arguments, values[index].definition.value == expected[index]);
                    BUSTER_TEST(arguments, rows[index].next.value == IR_ID_UNDERLYING_INVALID);
                    if (sources) BUSTER_TEST(arguments, sources[index].offset == order[index] + 100);
                    if (cfg->instruction_remap) BUSTER_TEST(arguments, cfg->instruction_remap[index].value == expected[index]);
                }
                for (u32 index = 0; index < extra_count; index += 1)
                {
                    BUSTER_TEST(arguments, !index || extra_ids[index - 1].value < extra_ids[index].value);
                    for (u32 old = 0; old < extra_count; old += 1)
                    {
                        if (string_equal(extras[index].literal, labels[old]))
                        {
                            BUSTER_TEST(arguments, extra_ids[index].value == expected[original_extra_ids[old]]);
                        }
                    }
                }
                u64 owner_position = arguments->arena->position;
                BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE && function.published_cfg == cfg);
                BUSTER_TEST(arguments, arguments->arena->position == owner_position);
                // Reopen and reverse each block's instruction chain. Reusing
                // a consumed scratch map or a prior publication cannot work.
                ir_function_invalidate_cfg(&function);
                for (u32 block = 0; block < block_count; block += 1)
                {
                    u32 first = block * count / block_count;
                    u32 end = (block + 1) * count / block_count;
                    blocks[block].first_instruction.value = end - 1;
                    blocks[block].last_instruction.value = first;
                    for (u32 index = first; index < end; index += 1)
                    {
                        rows[index].next.value = index > first ? index - 1 : IR_ID_UNDERLYING_INVALID;
                        expected[order[index]] = first + end - 1 - index;
                    }
                }
                if (BUSTER_REQUIRE(arguments, ir_function_publish_cfg(arguments->arena, &function).error == IR_VALIDATION_NONE && function.published_cfg))
                {
                    for (u32 old = 0; old < count; old += 1)
                    {
                        u32 index = expected[old];
                        BUSTER_TEST(arguments, rows[index].result.value == old && rows[index].symbol.value == old + 10);
                        BUSTER_TEST(arguments, values[old].definition.value == index);
                        if (sources) BUSTER_TEST(arguments, sources[index].offset == old + 100);
                    }
                    if (cfg->instruction_remap)
                    {
                        for (u32 index = 0; index < count; index += 1)
                        {
                            BUSTER_TEST(arguments, cfg->instruction_remap[order[index]].value == index);
                        }
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_cfg_permutation_failure_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 variant = 0; variant < 8; variant += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrInstruction rows[5] = {0};
        IrValue values[5] = {0};
        IrSourceRange sources[5] = {0};
        IrBlock block = {.id = {.value = 0}, .first_instruction = {.value = 1}, .last_instruction = {.value = 0}};
        IrInstructionId extra_id = {.value = 1};
        IrInstructionExtra extra = {.literal = S8_INITIALIZER("preserved")};
        for (u32 index = 0; index < 5; index += 1)
        {
            rows[index].next.value = index == 0 ? IR_ID_UNDERLYING_INVALID : (index + 1) % 5;
            rows[index].result.value = index;
            values[index].definition.value = index;
            sources[index].offset = index + 100;
        }
        IrFunction function = {.blocks = &block, .block_count = 1, .instructions = rows, .instruction_count = 5,
            .values = values, .value_count = 5, .instruction_canonical_sources = sources,
            .extras = &extra, .extra_instructions = &extra_id, .extra_count = 1, .state = IR_FUNCTION_LOWERED};
        switch (variant)
        {
            case 0: rows[0].next.value = 1; break; // cycle after every row was seen
            case 1: rows[4].next.value = IR_ID_UNDERLYING_INVALID; block.last_instruction.value = 4; break; // missing row
            case 2: block.last_instruction.value = 4; break; // wrong tail
            case 3: rows[4].next.value = 5; break; // out-of-range link
            case 4: values[4].definition.value = 5; break;
            case 5: extra_id.value = 5; break;
            case 6: rows[4].operand_count = 1; break; // missing operand storage
            case 7: function.instruction_count = 0; break; // no valid empty instruction stream
            default: break;
        }
        IrInstruction saved_rows[5];
        IrValue saved_values[5];
        IrSourceRange saved_sources[5];
        memcpy(saved_rows, rows, sizeof(rows));
        memcpy(saved_values, values, sizeof(values));
        memcpy(saved_sources, sources, sizeof(sources));
        IrInstructionId saved_extra_id = extra_id;
        BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, &function).error != IR_VALIDATION_NONE && !function.published_cfg);
        BUSTER_TEST(arguments, !memcmp(rows, saved_rows, sizeof(rows)));
        BUSTER_TEST(arguments, !memcmp(values, saved_values, sizeof(values)));
        BUSTER_TEST(arguments, !memcmp(sources, saved_sources, sizeof(sources)));
        BUSTER_TEST(arguments, extra_id.value == saved_extra_id.value && string_equal(extra.literal, S8("preserved")));
        scratch_end(temporary);
    }
    return result;
}

