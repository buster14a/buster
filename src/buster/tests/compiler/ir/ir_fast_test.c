// Included by ir_test.c. These inspect shared canonical rows before any target
// can hide a missing transformation; executable differential uses the fixture
// tests/basic_c_canonical_fast.c with all pass/allocator combinations.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("volatile int observed;int effect(int);"
                       "int test(int input,int* p){int x=input+0;int unused=x*9;"
                       "int a=3,b=4;int* q=&*p;observed=effect(x);return x+(a+b)+*q+observed;}");
    for (u32 mask = 0; mask <= IR_FAST_ALL; mask += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena, source, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
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
    // A safely backed budget-control input: no instruction/block traversal and
    // no value storage. The guard must decline before allocating or touching
    // the advertised value population. This is not a valid-IR certification test.
    IrFunction oversized = {.state = IR_FUNCTION_LOWERED, .value_count = UINT32_MAX};
    IrProgram oversized_program = {.arena = arguments->arena, .disable_local_promotion = true, .fast_passes = IR_FAST_ALL};
    IrFastStatistics oversized_statistics = ir_test_fast_function(&oversized_program, &oversized);
    BUSTER_TEST(arguments, oversized_statistics.budget_skips == 1 && oversized_statistics.scratch_peak_bytes == 0);
    BUSTER_TEST(arguments, oversized.value_count == UINT32_MAX && oversized.values == 0);
    for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) BUSTER_TEST(arguments, oversized_statistics.passes[pass].changes == 0);
    return result;
}
