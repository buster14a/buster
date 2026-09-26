// Included by ir_test.c. These inspect shared canonical rows before any target
// can hide a missing transformation; executable differential uses the fixture
// tests/basic_c_canonical_fast.c with all pass/allocator combinations.
BUSTER_GLOBAL_LOCAL u64 ir_test_operand_total(IrFunction* function)
{
    u64 total = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        total += function->instructions[index].operand_count;
    }
    return total;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_publication_span_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 variant = 0; variant < 4; variant += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena,
            S8("volatile int observed;int test(void){return observed;}"), target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            IrFunction* function = module->functions;
            program->disable_local_promotion = true;
            program->fast_passes = 0;
            u32 count = function->instruction_count;
            BUSTER_TEST(arguments, function->block_count == 1 && count > 1);
            if (function->block_count == 1 && count > 1)
            {
                IrBlock* block = function->blocks;
                IrInstruction* original = arena_allocate(arguments->arena, IrInstruction, count);
                memcpy(original, function->instructions, sizeof(*original) * count);
                if (variant == 2)
                {
                    // Reverse physical row IDs while preserving the linked
                    // program. Publication must build the explicit permutation.
                    for (u32 index = 0; index < count; index += 1)
                    {
                        IrInstruction row = original[index];
                        if (row.next.value < count) row.next.value = count - row.next.value - 1;
                        function->instructions[count - index - 1] = row;
                    }
                    for (u32 index = 0; index < function->value_count; index += 1)
                    {
                        IrInstructionId* definition = &function->values[index].definition;
                        if (definition->value < count) definition->value = count - definition->value - 1;
                    }
                    block->first_instruction.value = count - 1;
                    block->last_instruction.value = 0;
                }
                else if (variant == 3)
                {
                    function->instructions[0].next.value = 0;
                }
                // Publication proves ownership itself even when preparation
                // receives a producer-certified module.
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, variant == 1 || variant == 3);
                BUSTER_TEST(arguments, prepared.error == (variant == 3 ? IR_VALIDATION_INSTRUCTION_OWNERSHIP : IR_VALIDATION_NONE));
                if (variant == 3)
                {
                    BUSTER_TEST(arguments, !function->published_cfg);
                }
                else if (BUSTER_REQUIRE(arguments, function->published_cfg != 0))
                {
                    IrPublishedCfg const* cfg = function->published_cfg;
                    BUSTER_TEST(arguments, cfg->blocks[0].first_instruction == 0 && cfg->blocks[0].instruction_count == count);
                    BUSTER_TEST(arguments, (cfg->instruction_remap != 0) == (variant == 2));
                    for (u32 index = 0; index < count; index += 1)
                    {
                        IrInstruction* row = function->instructions + index;
                        BUSTER_TEST(arguments, row->opcode == original[index].opcode && row->result.value == original[index].result.value);
                        // Dead construction metadata must not influence a
                        // published walk or the strict canonical validator.
                        BUSTER_TEST(arguments, row->next.value == IR_ID_UNDERLYING_INVALID);
                        row->next.value = UINT32_MAX - 1;
                        IrInstructionId published_next = ir_block_next_instruction(function, block, (IrInstructionId){.value = index});
                        BUSTER_TEST(arguments, published_next.value == (index + 1 < count ? index + 1 : UINT32_MAX));
                    }
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                    ir_function_invalidate_cfg(function);
                    BUSTER_TEST(arguments, !function->published_cfg);
                    for (u32 index = 0; index < count; index += 1)
                    {
                        BUSTER_TEST(arguments, function->instructions[index].next.value == original[index].next.value);
                    }
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                }
            }
        }
        scratch_end(temporary);
    }
    return result;
}

// Preparation validates and publishes each function straight after
// transforming it. What it decides must not move: FAST's input guard is
// answered for the whole module before any function is rewritten, the
// promotion-output check still rejects the module before FAST or publication
// starts, the output check covers every function, and statistics and
// publication cover the whole module exactly once. Direct frontend SSA leaves
// promotion nothing to change, so its planted fault reaches the guard; shared
// promotion changes every function, so test builds reject the same fault at
// the promotion-output boundary.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_preparation_order_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("int first(int x){return x+0;}int second(int x){return (x*1)+(2*3);}int last(int x){return x+(4-4);}");
    for (u32 variant = 0; variant < 4; variant += 1)
    {
        bool shared_promotion = variant >= 2;
        bool guarded = variant & 1;
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = {0};
        if (shared_promotion)
        {
            lowered = ir_promotion_lower(arguments->arena, source, target_native);
        }
        else
        {
            CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
                (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
            CAnalysisResult analysis = c_parse(arguments->arena, preprocess);
            lowered = c_lower_to_ir_with_options(arguments->arena, S8("preparation-order.c"), preprocess, analysis, target_native,
                                                 (CIRLowerOptions){0});
        }
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count && BUSTER_REQUIRE(arguments, lowered.program->modules->function_count == 3))
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            if (guarded)
            {
                // Only the last function is a shape the strict validator
                // rejects; the two before it are visited and pass first.
                IrFunction* last = module->functions + 2;
                bool corrupted = false;
                for (u32 index = 0; index < last->instruction_count && !corrupted; index += 1)
                {
                    if (last->instructions[index].opcode == IR_OPCODE_BINARY)
                    {
                        last->instructions[index].binary_operation = IR_BINARY_COUNT;
                        corrupted = true;
                    }
                }
                BUSTER_TEST(arguments, corrupted);
            }
            program->fast_passes = IR_FAST_ALL;
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters before = ir_construction_counters();
#endif
            u32 counts[3];
            for (u32 index = 0; index < 3; index += 1)
            {
                counts[index] = module->functions[index].instruction_count;
            }
            IrValidationResult prepared = ir_prepare_canonical_module(program, module, true);
            BUSTER_TEST(arguments, (module->local_promotion.promoted_locals != 0) == shared_promotion);
            bool rejected = guarded && shared_promotion && BUSTER_IR_TRANSFORM_CHECKS;
            if (rejected)
            {
                BUSTER_TEST(arguments, prepared.error != IR_VALIDATION_NONE && prepared.function.value == 2);
                BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT);
                BUSTER_TEST(arguments, !module->local_promotion_complete && !module->fast_complete);
                BUSTER_TEST(arguments, module->fast.functions == 0);
            }
            else
            {
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, module->local_promotion_complete && module->fast_complete);
                if (guarded)
                {
                    BUSTER_TEST(arguments, module->fast.validation_skips == 1 && module->fast.functions == 0);
                }
                else
                {
                    BUSTER_TEST(arguments, module->fast.validation_skips == 0 && module->fast.functions == 3);
                    BUSTER_TEST(arguments, module->fast.passes[IR_FAST_FOLD].changes != 0);
                    BUSTER_TEST(arguments, prepared.boundary == (BUSTER_IR_TRANSFORM_CHECKS ? IR_VALIDATION_BOUNDARY_FAST_OUTPUT
                                                                                             : IR_VALIDATION_BOUNDARY_UNSPECIFIED));
                }
            }
            IrPublishedCfg const* published[3];
            for (u32 index = 0; index < 3; index += 1)
            {
                IrFunction* function = module->functions + index;
                published[index] = function->published_cfg;
                BUSTER_TEST(arguments, (published[index] != 0) == !rejected);
                if (guarded && !shared_promotion)
                {
                    BUSTER_TEST(arguments, function->instruction_count == counts[index]);
                }
                else if (!guarded)
                {
                    BUSTER_TEST(arguments, function->instruction_count < counts[index]);
                }
            }
            if (!guarded)
            {
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
            }
#if BUSTER_BENCH_ALLOCATIONS && BUSTER_IR_TRANSFORM_CHECKS
            IrConstructionCounters after = ir_construction_counters();
            BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_PREPARATION_EXPECT(counter, expected) \
    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter] == (expected))
            IR_PREPARATION_EXPECT(PREPARATION_PROMOTION_OUTPUT_VALIDATIONS, shared_promotion ? 1 : 0);
            IR_PREPARATION_EXPECT(PREPARATION_FAST_INPUT_VALIDATIONS, shared_promotion ? 0 : 1);
            IR_PREPARATION_EXPECT(PREPARATION_FAST_OUTPUT_VALIDATIONS, guarded ? 0 : 1);
            IR_PREPARATION_EXPECT(PREPARATION_PROMOTION_FUNCTIONS, 3);
            IR_PREPARATION_EXPECT(PREPARATION_FAST_FUNCTIONS, guarded ? 0 : 3);
            IR_PREPARATION_EXPECT(PREPARATION_PUBLICATION_FUNCTIONS, rejected ? 0 : 3);
            IR_PREPARATION_EXPECT(VALIDATION_CALLS, guarded ? 1 : 3);
            IR_PREPARATION_EXPECT(VALIDATION_OWNERSHIP_FUNCTIONS, guarded ? 3 : 9);
#undef IR_PREPARATION_EXPECT
#endif
            if (!rejected)
            {
                // The backend's own preparation call finds nothing left to do.
                BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, true).error == IR_VALIDATION_NONE);
                for (u32 index = 0; index < 3; index += 1)
                {
                    BUSTER_TEST(arguments, module->functions[index].published_cfg == published[index]);
                }
            }
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_publication_span_tests(arguments);
    UnitTestResult preparation_order = ir_fast_preparation_order_tests(arguments);
    result.test_count += preparation_order.test_count;
    result.succeeded_test_count += preparation_order.succeeded_test_count;
    String8 source = S8("volatile int observed;int effect(int);"
                       "int test(int input,int* p){int x=input+0;int unused=x*9;"
                       "int a=3,b=4;int* q=&*p;observed=effect(x);return x+(a+b)+*q+observed;}");
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult analysis = c_parse(arguments->arena, preprocess);
        CIRLowerResult lowered = c_lower_to_ir_with_options(arguments->arena, S8("fast-operand-total.c"),
            preprocess, analysis, target_native, (CIRLowerOptions){0});
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrModule* module = lowered.program->modules;
            IrFunction* function = 0;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                if (string_equal(module->functions[index].name, S8("test"))) function = module->functions + index;
            }
            if (BUSTER_REQUIRE(arguments, function && function->instruction_count))
            {
                BUSTER_TEST(arguments, function->operand_total_rows == function->instruction_count);
                BUSTER_TEST(arguments, function->operand_total == ir_test_operand_total(function));
                lowered.program->fast_passes = 0;
                BUSTER_TEST(arguments, ir_prepare_canonical_module(lowered.program, module, false).error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, function->operand_total_rows == function->instruction_count);
                BUSTER_TEST(arguments, function->published_cfg != 0);
                ir_function_invalidate_cfg(function);
                BUSTER_TEST(arguments, function->operand_total_rows != function->instruction_count);
            }
        }
        scratch_end(temporary);
    }
    for (u32 mask = 0; mask <= IR_FAST_ALL; mask += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena, source, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                BUSTER_TEST(arguments, function->operand_total_rows != function->instruction_count || function->operand_total == ir_test_operand_total(function));
            }
            IrValidationResult before = ir_prepare_canonical_module(program, module, false);
            BUSTER_TEST(arguments, before.error == IR_VALIDATION_NONE);
            u32 calls = 0, stores = 0, loads = 0;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                calls += ir_test_opcode_count(module->functions + index, IR_OPCODE_CALL);
                stores += ir_test_opcode_count(module->functions + index, IR_OPCODE_STORE);
                loads += ir_test_opcode_count(module->functions + index, IR_OPCODE_LOAD);
            }
            program->fast_passes = mask;
            IrValidationResult prepared = ir_prepare_canonical_module(program, module, true);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, module->fast.instructions_after <= module->fast.instructions_before);
            BUSTER_TEST(arguments, module->fast.scratch_peak_bytes <= IR_FAST_SCRATCH_BUDGET);
            BUSTER_TEST(arguments, !module->fast.budget_skips && !module->fast.provenance_skips);
            u32 after_calls = 0, after_stores = 0, after_loads = 0;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                BUSTER_TEST(arguments, function->operand_total_rows != function->instruction_count || function->operand_total == ir_test_operand_total(function));
                after_calls += ir_test_opcode_count(function, IR_OPCODE_CALL);
                after_stores += ir_test_opcode_count(function, IR_OPCODE_STORE);
                after_loads += ir_test_opcode_count(function, IR_OPCODE_LOAD);
                for (u32 row_index = 0; row_index < function->instruction_count; row_index += 1)
                {
                    IrInstruction* row = function->instructions + row_index;
                    if (row->opcode == IR_OPCODE_CALL || row->opcode == IR_OPCODE_STORE || row->opcode == IR_OPCODE_LOAD)
                    {
                        BUSTER_TEST(arguments, !ir_instruction_is_pure(program, function, row));
                    }
                }
            }
            BUSTER_TEST(arguments, calls == after_calls && stores == after_stores && loads == after_loads);
            for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
            {
                bool enabled = (mask & IR_FAST_PASS_BIT(pass)) != 0;
                IrFastPassStatistics statistics = module->fast.passes[pass];
                BUSTER_TEST(arguments, enabled || (!statistics.changes && !statistics.visits));
                BUSTER_TEST(arguments, statistics.nanoseconds == 0);
                if (enabled && (pass == IR_FAST_FOLD || pass == IR_FAST_DCE)) BUSTER_TEST(arguments, statistics.changes != 0);
            }
            IrFastStatistics snapshot = module->fast;
            IrValidationResult repeated = ir_prepare_canonical_module(program, module, false);
            BUSTER_TEST(arguments, repeated.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, memcmp(&snapshot, &module->fast, sizeof(snapshot)) == 0);
        }
        scratch_end(temporary);
    }
    // Build a nontrivial join first, then make every edge carry the same
    // dominating argument. This specifically exercises existing parameters;
    // the promotion oracle only simplifies the parameters it introduced.
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    CIRLowerResult lowered = ir_promotion_lower(arguments->arena,
        S8("int test(int condition,int input){int x;if(condition)x=input+1;else x=input+2;return x;}"), target_native);
    BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
    if (lowered.program && !lowered.diagnostic_count)
    {
        IrProgram* program = lowered.program;
        IrModule* module = program->modules;
        BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
        IrFunction* function = module->functions;
        BUSTER_TEST(arguments, function->published_cfg != 0);
        u64 before_reopen = arguments->arena->position;
        u32 saved_passes = program->fast_passes;
        program->fast_passes = 0;
        IrFastStatistics reopened = ir_test_fast_function(program, function);
        program->fast_passes = saved_passes;
        BUSTER_TEST(arguments, !reopened.budget_skips && !function->published_cfg);
        BUSTER_TEST(arguments, reopened.retained_bytes != 0 && reopened.retained_bytes >= arguments->arena->position - before_reopen);
        for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) BUSTER_TEST(arguments, reopened.passes[pass].changes == 0);
        u32 argument = IR_ID_UNDERLYING_INVALID;
        u32 parameters = 0;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            if (row->opcode == IR_OPCODE_ARGUMENT) argument = row->result.value;
        }
        BUSTER_TEST(arguments, argument < function->value_count);
        if (argument < function->value_count)
        {
            for (u32 block = 0; block < function->block_count; block += 1)
            {
                for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
                {
                    BUSTER_TEST(arguments, parameter->canonical_type.value == function->values[argument].canonical_type.value);
                    for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next) incoming->value.value = argument;
                    parameters += 1;
                }
            }
            BUSTER_TEST(arguments, parameters != 0);
            program->fast_passes = IR_FAST_PASS_BIT(IR_FAST_PARAMETERS);
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, module->fast.passes[IR_FAST_PARAMETERS].changes == parameters);
            BUSTER_TEST(arguments, !module->fast.parameter_budget_hits);
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
        }
        // The classifier must not erase operations whose exceptions or effects
        // are not described by a pure scalar-integer contract.
        IrInstruction probe = function->instructions[0];
        probe.result.value = 0;
        probe.canonical_type = function->values[0].canonical_type;
        u8 effects[] = {IR_OPCODE_LOAD, IR_OPCODE_CALL, IR_OPCODE_ATOMIC_LOAD, IR_OPCODE_ATOMIC_STORE,
                        IR_OPCODE_ATOMIC_FENCE, IR_OPCODE_INLINE_ASSEMBLY, IR_OPCODE_DEBUG_TRAP,
                        IR_OPCODE_STACK_ALLOCATE, IR_OPCODE_STACK_SAVE, IR_OPCODE_STACK_RESTORE, IR_OPCODE_SIMD};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(effects); index += 1)
        {
            probe.opcode = effects[index];
            BUSTER_TEST(arguments, !ir_instruction_is_pure(program, function, &probe));
        }
        probe.opcode = IR_OPCODE_BINARY;
        u8 exceptional[] = {IR_BINARY_SIGNED_DIVIDE, IR_BINARY_UNSIGNED_DIVIDE, IR_BINARY_SIGNED_REMAINDER,
                            IR_BINARY_UNSIGNED_REMAINDER, IR_BINARY_FLOAT_ADD, IR_BINARY_FLOAT_DIVIDE};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(exceptional); index += 1)
        {
            probe.binary_operation = exceptional[index];
            BUSTER_TEST(arguments, !ir_instruction_is_pure(program, function, &probe));
        }
        probe.binary_operation = IR_BINARY_INTEGER_ADD;
        probe.volatile_access = true;
        BUSTER_TEST(arguments, !ir_instruction_is_pure(program, function, &probe));
    }
    scratch_end(temporary);
    // Optional transforms must decline a producer-certified legacy shape that
    // the stricter canonical validator cannot prove. Such source is still
    // accepted by its existing consumer, so FAST must not create a new error.
    TemporalArena invalid_temporary = arena_begin_temporal(arguments->arena);
    CIRLowerResult invalid_lowered = ir_promotion_lower(arguments->arena, S8("int test(int x){return x+0;}"), target_native);
    BUSTER_TEST(arguments, invalid_lowered.program && !invalid_lowered.diagnostic_count);
    if (invalid_lowered.program && !invalid_lowered.diagnostic_count)
    {
        IrProgram* invalid_program = invalid_lowered.program;
        IrModule* invalid_module = invalid_program->modules;
        IrFunction* invalid_function = invalid_module->functions;
        bool corrupted = false;
        for (u32 index = 0; index < invalid_function->instruction_count && !corrupted; index += 1)
        {
            IrInstruction* row = invalid_function->instructions + index;
            if (row->opcode == IR_OPCODE_BINARY)
            {
                row->binary_operation = IR_BINARY_COUNT;
                corrupted = true;
            }
        }
        BUSTER_TEST(arguments, corrupted);
        u32 instruction_count = invalid_function->instruction_count;
        invalid_program->disable_local_promotion = true;
        invalid_program->fast_passes = IR_FAST_ALL;
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters before = ir_construction_counters();
#endif
        IrValidationResult skipped = ir_prepare_canonical_module(invalid_program, invalid_module, true);
        BUSTER_TEST(arguments, skipped.error == IR_VALIDATION_NONE && invalid_module->fast_complete);
        BUSTER_TEST(arguments, invalid_module->fast.validation_skips == 1 && invalid_module->fast.functions == 0);
        BUSTER_TEST(arguments, invalid_function->instruction_count == instruction_count);
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters after = ir_construction_counters();
        BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_PREPARATION_FAST_INPUT_VALIDATIONS]
                             - before.values[IR_CONSTRUCTION_PREPARATION_FAST_INPUT_VALIDATIONS] == 1);
        BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_OUTPUT_VALIDATIONS]
                             - before.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_OUTPUT_VALIDATIONS] == 0);
#endif
    }
    scratch_end(invalid_temporary);
    // A certified module that promotion actually changed is scanned once at
    // the promotion-output boundary in test builds. FAST's input guard asks
    // the same predicate of the same unmutated rows, so it must start from
    // that scan rather than repeat it; the guard still runs when nothing has
    // proven the current representation (promotion changed nothing).
    for (u32 promotes = 0; promotes < 2; promotes += 1)
    {
        TemporalArena reuse_temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult reuse_lowered = ir_promotion_lower(arguments->arena,
            promotes ? S8("int test(int c){int x=3;if(c)x=7;return x+0;}") : S8("volatile int g;int test(void){return g+0;}"), target_native);
        BUSTER_TEST(arguments, reuse_lowered.program && !reuse_lowered.diagnostic_count);
        if (reuse_lowered.program && !reuse_lowered.diagnostic_count)
        {
            IrProgram* reuse_program = reuse_lowered.program;
            IrModule* reuse_module = reuse_program->modules;
            reuse_program->fast_passes = IR_FAST_ALL;
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters before = ir_construction_counters();
#endif
            IrValidationResult reused = ir_prepare_canonical_module(reuse_program, reuse_module, true);
            BUSTER_TEST(arguments, reused.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, (reuse_module->local_promotion.promoted_locals != 0) == (promotes != 0));
            BUSTER_TEST(arguments, reuse_module->local_promotion_complete && reuse_module->fast_complete);
            BUSTER_TEST(arguments, reuse_module->fast.validation_skips == 0 && reuse_module->fast.functions == 1);
            BUSTER_TEST(arguments, reuse_module->fast.passes[IR_FAST_FOLD].changes != 0);
            BUSTER_TEST(arguments, reused.boundary == IR_VALIDATION_BOUNDARY_FAST_OUTPUT);
            BUSTER_TEST(arguments, ir_validate_canonical_module(reuse_program, reuse_module).error == IR_VALIDATION_NONE);
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters after = ir_construction_counters();
            BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_PREPARATION_EXPECT(counter, expected) \
    BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter] == (expected))
            IR_PREPARATION_EXPECT(PREPARATION_INPUT_VALIDATIONS, 0);
            IR_PREPARATION_EXPECT(PREPARATION_PROMOTION_OUTPUT_VALIDATIONS, promotes ? 1 : 0);
            IR_PREPARATION_EXPECT(PREPARATION_FAST_INPUT_VALIDATIONS, promotes ? 0 : 1);
            IR_PREPARATION_EXPECT(PREPARATION_FAST_OUTPUT_VALIDATIONS, 1);
            IR_PREPARATION_EXPECT(VALIDATION_CALLS, 3);
#undef IR_PREPARATION_EXPECT
#endif
        }
        scratch_end(reuse_temporary);
    }
    // A safely backed budget-control input: no instruction/block traversal and
    // no value storage. The guard must decline before allocating or touching
    // the advertised value population. This is not a valid-IR certification test.
    // Known summaries must admit from the producer's facts, before touching
    // row storage. Unknown summaries and stale row populations scan the row.
    for (u32 known = 0; known < 3; known += 1)
    {
        IrInstruction row = {.opcode = IR_OPCODE_LABEL_ADDRESS, .operand_count = 1};
        IrFunction probe = {.state = IR_FUNCTION_LOWERED, .instructions = known == 1 ? 0 : &row,
            .instruction_count = 1, .operand_total_rows = known == 2 ? 0 : 1, .operand_total = IR_FAST_WORK_BUDGET + 1,
            .opcode_summary = known ? IR_OPCODE_SUMMARY_KNOWN : 0};
        IrProgram probe_program = {.arena = arguments->arena, .fast_passes = IR_FAST_ALL};
        IrFastStatistics probe_statistics = ir_test_fast_function(&probe_program, &probe);
        BUSTER_TEST(arguments, probe_statistics.budget_skips == (known == 1));
        BUSTER_TEST(arguments, probe_statistics.provenance_skips == (known != 1));
        BUSTER_TEST(arguments, probe_statistics.scratch_peak_bytes == 0);
        if (known == 1)
        {
            probe.opcode_summary |= IR_OPCODE_BIT(IR_OPCODE_INDIRECT_BRANCH);
            probe.operand_total = 0;
            probe_statistics = ir_test_fast_function(&probe_program, &probe);
            BUSTER_TEST(arguments, probe_statistics.provenance_skips == 1 && probe_statistics.budget_skips == 0);
            BUSTER_TEST(arguments, probe_statistics.scratch_peak_bytes == 0);
        }
    }
    IrFunction oversized = {.state = IR_FUNCTION_LOWERED, .value_count = UINT32_MAX};
    IrProgram oversized_program = {.arena = arguments->arena, .disable_local_promotion = true, .fast_passes = IR_FAST_ALL};
    IrFastStatistics oversized_statistics = ir_test_fast_function(&oversized_program, &oversized);
    BUSTER_TEST(arguments, oversized_statistics.budget_skips == 1 && oversized_statistics.scratch_peak_bytes == 0);
    BUSTER_TEST(arguments, oversized.value_count == UINT32_MAX && oversized.values == 0);
    for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) BUSTER_TEST(arguments, oversized_statistics.passes[pass].changes == 0);
    // Published counts must be admitted before restoring their builder arrays.
    // Only the flat row is backed: value/CFG storage is deliberately absent,
    // so this private budget probe must decline before a pass can inspect it.
    // Each case isolates one retained allocation below the work/scratch caps.
    for (u32 kind = 0; kind < 3; kind += 1)
    {
        IrPublishedCfg published = {.arena = arguments->arena};
        if (kind == 0) published.parameter_count = (u32)(IR_FAST_RETAINED_BUDGET / sizeof(IrBlockParameter)) + 1u;
        else if (kind == 1) published.argument_count = (u32)(IR_FAST_RETAINED_BUDGET / sizeof(IrIncoming)) + 1u;
        else published.edge_count = (u32)(IR_FAST_RETAINED_BUDGET / sizeof(IrPredecessor)) + 1u;
        BUSTER_TEST(arguments, (u64)published.parameter_count + published.argument_count + 2u <= IR_FAST_WORK_BUDGET);
        IrInstruction row = {.opcode = IR_OPCODE_FIELD, .result = {.value = 0}};
        IrFunction published_oversized = {.state = IR_FUNCTION_LOWERED, .published_cfg = &published,
            .instructions = &row, .instruction_count = 1, .value_count = 1};
        u64 before = arguments->arena->position;
        IrFastStatistics skipped = ir_test_fast_function(&oversized_program, &published_oversized);
        BUSTER_TEST(arguments, skipped.budget_skips == 1 && !skipped.provenance_skips);
        BUSTER_TEST(arguments, skipped.scratch_peak_bytes == 0 && skipped.retained_bytes == 0);
        BUSTER_TEST(arguments, arguments->arena->position == before && published_oversized.published_cfg == &published);
        BUSTER_TEST(arguments, published_oversized.instructions == &row && published_oversized.values == 0);
        for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
            BUSTER_TEST(arguments, skipped.passes[pass].changes == 0 && skipped.passes[pass].visits == 0);
    }
    return result;
}
