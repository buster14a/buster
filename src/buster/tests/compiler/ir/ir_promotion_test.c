// Included by ir_test.c: structural coverage is separate from the executable
// fixture so unchanged runtime behavior cannot conceal missing promotion.
BUSTER_GLOBAL_LOCAL CIRLowerResult ir_promotion_lower(Arena* arena, String8 source, Target target)
{
    CPreprocessResult preprocess = c_preprocess(arena, source, (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
    CAnalysisResult analysis = c_parse(arena, preprocess);
    CIRLowerResult result = {0};
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        // Keep this pass's reference tests independent of direct frontend SSA.
        result = c_lower_to_ir_with_options(arena, S8("local-promotion.c"), preprocess, analysis, target,
                                           (CIRLowerOptions){.disable_direct_ssa = true});
    }
    return result;
}

// A stale input certificate is deliberately supplied after a safely backed
// fault is injected outside the promoted function. These are hook controls,
// not claims that the frontend or promotion produces any of these faults.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_promotion_validation_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("int storage=1;void* address=&storage;"
                        "int test(int c){int x=3;if(c)x=7;return x;}"
                        "double other(void){return 2.0;}");
    for (u32 variant = 0; variant < 5; variant += 1)
    {
        for (u32 certified = 0; certified < 2; certified += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            CIRLowerResult lowered = ir_promotion_lower(arguments->arena, source, target_native);
            BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
            if (lowered.program && !lowered.diagnostic_count)
            {
                IrProgram* program = lowered.program;
                IrModule* module = program->modules;
                IrFunction* function = 0;
                IrFunction* other = 0;
                IrGlobal* storage = 0;
                IrGlobal* address = 0;
                for (u32 index = 0; index < module->function_count; index += 1)
                {
                    IrFunction* candidate = module->functions + index;
                    if (string_equal(candidate->name, S8("test"))) function = candidate;
                    if (string_equal(candidate->name, S8("other"))) other = candidate;
                }
                for (u32 index = 0; index < module->global_count; index += 1)
                {
                    IrGlobal* global = module->globals + index;
                    IrSymbol* symbol = ir_symbol_from_id(&program->symbols, global->symbol);
                    if (symbol && string_equal(symbol->name, S8("storage"))) storage = global;
                    if (symbol && string_equal(symbol->name, S8("address"))) address = global;
                }
                bool fixture_valid = function && other && storage && address && other->entry.value < other->block_count;
                BUSTER_TEST(arguments, fixture_valid);
                if (fixture_valid)
                {
                    IrValidationResult original = ir_validate_canonical_module(program, module);
                    BUSTER_TEST(arguments, original.error == IR_VALIDATION_NONE);
                    BUSTER_TEST(arguments, original.boundary == IR_VALIDATION_BOUNDARY_UNSPECIFIED);
                    u32 old_instructions = function->instruction_count;
                    IrValidationError expected = IR_VALIDATION_OPERATION;
                    IrBlock* other_entry = other->blocks + other->entry.value;
                    if (variant == 0)
                    {
                        storage->alignment = 3;
                        expected = IR_VALIDATION_ALIGNMENT;
                    }
                    else if (variant == 1)
                    {
                        address->relocation_count = 1;
                        address->relocations = 0;
                    }
                    else if (variant == 2)
                    {
                        // The byte range and target symbol are valid, but a data
                        // symbol cannot own a function's label-address relocation.
                        address->initializer_kind = IR_GLOBAL_INITIALIZER_BYTES;
                        address->bytes.length = program->data_layout.pointer.size;
                        address->bytes.pointer = arena_allocate(arguments->arena, u8, address->bytes.length);
                        memset(address->bytes.pointer, 0, address->bytes.length);
                        address->relocation_count = 1;
                        address->relocations = arena_allocate(arguments->arena, IrGlobalRelocation, 1);
                        address->relocations[0] = (IrGlobalRelocation){.symbol = storage->symbol,
                            .label_block = {.value = 0}, .is_label_address = true};
                    }
                    else if (variant == 3)
                    {
                        other_entry->terminated = false;
                        expected = IR_VALIDATION_UNTERMINATED_BLOCK;
                    }
                    else
                    {
                        IrType* signature = ir_type_from_id(&program->types, other->canonical_type);
                        IrType* integer_signature = ir_type_from_id(&program->types, function->canonical_type);
                        BUSTER_TEST(arguments, signature && integer_signature);
                        if (signature && integer_signature)
                        {
                            signature->return_type = integer_signature->return_type;
                        }
                        expected = IR_VALIDATION_RETURN_TYPE;
                    }
                    program->fast_passes = IR_FAST_ALL;
                    IrValidationResult validation = ir_prepare_canonical_module(program, module, certified != 0);
                    BUSTER_TEST(arguments, validation.error == expected);
                    BUSTER_TEST(arguments, validation.boundary == (certified ? IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT
                                                                            : IR_VALIDATION_BOUNDARY_CANONICAL_INPUT));
                    BUSTER_TEST(arguments, !module->local_promotion_complete);
                    BUSTER_TEST(arguments, certified ? module->local_promotion.promoted_locals > 0
                                                    : module->local_promotion.promoted_locals == 0);
                    BUSTER_TEST(arguments, certified ? function->instruction_count < old_instructions
                                                    : function->instruction_count == old_instructions);
                    BUSTER_TEST(arguments, validation.function.value == (variant >= 3 ? other->id.value : IR_ID_UNDERLYING_INVALID));
                    BUSTER_TEST(arguments, validation.block.value == (variant >= 3 ? other_entry->id.value : IR_ID_UNDERLYING_INVALID));
                    BUSTER_TEST(arguments, validation.instruction.value == (variant >= 3 ? other_entry->last_instruction.value : IR_ID_UNDERLYING_INVALID));
                    // A failed scan must also keep the optional transforms off the module.
                    BUSTER_TEST(arguments, module->fast.functions == 0 && !module->fast_complete);
                }
            }
            scratch_end(temporary);
        }
    }
    // A preparation marker is not a certificate after a subsequent mutation.
    // The caller must revoke its input proof; preparation must then validate
    // even when the pass itself has already completed.
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    CIRLowerResult lowered = ir_promotion_lower(arguments->arena, S8("int test(void){int x=3;return x;}"), target_native);
    BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
    if (lowered.program && !lowered.diagnostic_count)
    {
        IrProgram* program = lowered.program;
        IrModule* module = program->modules;
        IrValidationResult prepared = ir_prepare_canonical_module(program, module, true);
        BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE && module->local_promotion_complete);
        BUSTER_TEST(arguments, prepared.boundary == IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT);
        bool function_valid = module->function_count && module->functions[0].entry.value < module->functions[0].block_count;
        BUSTER_TEST(arguments, function_valid);
        if (function_valid)
        {
            IrFunction* function = module->functions;
            IrBlock* block = function->blocks + function->entry.value;
            block->terminated = false;
            IrValidationResult mutated = ir_prepare_canonical_module(program, module, false);
            BUSTER_TEST(arguments, mutated.error == IR_VALIDATION_UNTERMINATED_BLOCK);
            BUSTER_TEST(arguments, mutated.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
            BUSTER_TEST(arguments, mutated.function.value == function->id.value && mutated.block.value == block->id.value);
            BUSTER_TEST(arguments, mutated.instruction.value == block->last_instruction.value);
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_promotion_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_promotion_validation_tests(arguments);
    struct Fixture
    {
        String8 source;
        bool all_locals;
        bool joins;
        bool uninitialized;
        bool barrier;
        bool trivial;
    } fixtures[] = {
        {S8("int test(void){return 7;}"), true, false, false, false, false},
        {S8("int test(void){int x=3;x+=4;return x;}"), true, false, false, false, false},
        {S8("int test(int c){int v;if(c)v=11;else v=29;return v;}"), true, true, false, false, false},
        {S8("int test(int n){int a=1,b=2;while(n-->0){int t=a;a=b;b=t;}return a*10+b;}"), true, true, false, false, false},
        {S8("int test(int c,int x){int v=x;if(c)c+=1;return v;}"), true, false, false, false, true},
        {S8("int test(int c){int v=1;if(c)v=2;else v=3;v=4;return v;}"), true, false, false, false, false},
        {S8("double test(int c,double x){double v=x;if(c)v=3.5;return v;}"), true, true, false, false, false},
        {S8("int* test(int c,int* a,int* b){int* p=a;if(c)p=b;return p;}"), true, true, false, false, false},
        {S8("int test(int c,signed char x){signed char v=x;if(c)v=-7;return v;}"), false, false, false, false, false},
        {S8("typedef int V __attribute__((vector_size(16)));V test(int c,V a,V b){V v=a;if(c)v=b;return v;}"), true, true, false, false, false},
        {S8("int test(int n,int c){int v=1;if(c)goto b;a:v+=2;if(--n>0)goto b;return v;b:v+=3;if(--n>0)goto a;return v;}"), true, true, false, false, false},
        {S8("void side(int*);int test(void){int v=3;side(&v);return v;}"), false, false, false, false, false},
        {S8("int test(void){volatile int v=3;v=4;return v;}"), false, false, false, false, false},
        {S8("int test(void){_Atomic int v=3;v+=4;return v;}"), false, false, false, false, false},
        {S8("int test(int c){int v;if(c)v=1;return v;}"), false, false, true, false, false},
        {S8("int test(void){int v;int x=v;v=2;return x;}"), false, false, true, false, false},
        {S8("int test(int n){int a[n];a[0]=2;return a[0];}"), false, false, false, true, false},
        {S8("int test(int c){int v=c;__asm__ __volatile__(\"\" ::: \"memory\");v++;return v;}"), false, false, false, true, false},
        {S8("int setjmp(void*);int test(void* p){int v=1;if(setjmp(p))v=2;return v;}"), false, false, false, true, false},
        {S8("int test(int (*p)(int)){int v=1;return p(v);}"), false, false, false, true, false},
        {S8("int test(int c){void* p=c?&&a:&&b;goto *p;a:return 3;b:return 4;}"), false, false, false, true, false},
        {S8("__attribute__((noreturn))void stop(int);void test(int n){int x=n;stop(x);}"), true, false, false, false, false},
        // A loop-header parameter becomes trivial only after a later join is
        // removed; the earlier block must be revisited on the next sweep.
        {S8("int test(int n,int x){int v=x;while(n--){if(n&1)v=x;}return v;}"), true, true, false, false, false},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixtures) * 2; index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        struct Fixture fixture = fixtures[index / 2];
        bool input_certified = (index & 1u) != 0;
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena, fixture.source, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            IrFunction* function = 0;
            for (u32 fi = 0; fi < module->function_count; fi += 1)
            {
                if (string_equal(module->functions[fi].name, S8("test"))) function = module->functions + fi;
            }
            BUSTER_TEST(arguments, function != 0);
            if (function)
            {
                u32 old_instructions = function->instruction_count;
                u32 old_values = function->value_count;
                u32 old_locals = ir_test_opcode_count(function, IR_OPCODE_LOCAL);
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                program->disable_local_promotion = true;
                IrValidationResult disabled = ir_prepare_canonical_module(program, module, input_certified);
                BUSTER_TEST(arguments, disabled.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, disabled.boundary == (input_certified ? IR_VALIDATION_BOUNDARY_UNSPECIFIED : IR_VALIDATION_BOUNDARY_CANONICAL_INPUT));
                BUSTER_TEST(arguments, function->instruction_count == old_instructions && function->value_count == old_values);
                BUSTER_TEST(arguments, !module->local_promotion_complete && module->local_promotion.promoted_locals == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_sweeps == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_block_visits == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_visits == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_incoming_visits == 0);
                program->disable_local_promotion = false;
                IrValidationResult valid = ir_prepare_canonical_module(program, module, input_certified);
                BUSTER_TEST(arguments, valid.error == IR_VALIDATION_NONE);
                IrValidationBoundary expected_boundary = module->local_promotion.promoted_locals ? IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT
                                                        : input_certified ? IR_VALIDATION_BOUNDARY_UNSPECIFIED : IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
                BUSTER_TEST(arguments, valid.boundary == expected_boundary);
                BUSTER_TEST(arguments, module->local_promotion_complete);
                u32 locals = ir_test_opcode_count(function, IR_OPCODE_LOCAL);
                u64 joins = 0;
                for (u32 bi = 0; bi < function->block_count; bi += 1)
                {
                    IrBlock* block = function->blocks + bi;
                    joins += block->parameter_count;
                    IrPublishedCfg const* cfg = function->published_cfg;
                    BUSTER_TEST(arguments, cfg && !block->first_parameter && !block->last_parameter && !block->first_predecessor && !block->last_predecessor);
                    IrCfgBlock const* published = cfg->blocks + bi;
                    for (u32 parameter_index = 0; parameter_index < published->parameter_count; parameter_index += 1)
                    {
                        IrCfgParameter const* parameter = cfg->parameters + published->parameter_offset + parameter_index;
                        BUSTER_TEST(arguments, parameter->value.value < function->value_count);
                        if (parameter->value.value < function->value_count)
                        {
                            BUSTER_TEST(arguments, function->values[parameter->value.value].definition.value == IR_ID_UNDERLYING_INVALID);
                        }
                        BUSTER_TEST(arguments, published->predecessor_count > 1 && published->predecessor_count == block->predecessor_count);
                        for (u32 predecessor = 0; predecessor < published->predecessor_count; predecessor += 1)
                        {
                            IrCfgEdge const* edge = cfg->edges + cfg->predecessors[published->predecessor_offset + predecessor];
                            BUSTER_TEST(arguments, edge->destination.value == bi && edge->source.value < function->block_count);
                            BUSTER_TEST(arguments, cfg->arguments[edge->argument_offset + parameter_index].value < function->value_count);
                        }
                    }
                }
                BUSTER_TEST(arguments, (joins != 0) == fixture.joins);
                BUSTER_TEST(arguments, fixture.all_locals ? locals == 0 : locals != 0);
                BUSTER_TEST(arguments, (module->local_promotion.uninitialized_locals != 0) == fixture.uninitialized);
                BUSTER_TEST(arguments, (module->local_promotion.barrier_functions != 0) == fixture.barrier);
                if (fixture.all_locals)
                {
                    BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_LOAD) == 0);
                    BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_STORE) == 0);
                }
                if (fixture.barrier)
                {
                    BUSTER_TEST(arguments, old_locals == locals && old_instructions == function->instruction_count && old_values == function->value_count);
                }
                if (fixture.trivial)
                {
                    BUSTER_TEST(arguments, module->local_promotion.removed_parameters > 0);
                }
                IrLocalPromotionStatistics stats = module->local_promotion;
                BUSTER_TEST(arguments, stats.instructions_before - stats.instructions_after == stats.promoted_locals + stats.removed_loads + stats.removed_stores);
                // Each fixture has one lowered function. The declaration-only
                // callees contribute no sweeps, and promotion preserves blocks.
                BUSTER_TEST(arguments, stats.parameter_block_visits == stats.parameter_sweeps * function->block_count);
                if (stats.promoted_locals)
                {
                    // Only inserted parameters enter the simplifier. A changing
                    // sweep removes at least one, then a final sweep converges.
                    BUSTER_TEST(arguments, (stats.parameter_sweeps != 0) == (stats.inserted_parameters != 0));
                    BUSTER_TEST(arguments, stats.parameter_sweeps <= stats.removed_parameters + 1);
                    BUSTER_TEST(arguments, stats.parameter_visits >= stats.inserted_parameters);
                    if (fixture.trivial)
                    {
                        // Only v is live at this diamond's join; both incoming
                        // definitions are x. Its sole parameter is removed on
                        // the first sweep and absent from the convergence sweep.
                        BUSTER_TEST(arguments, stats.inserted_parameters == 1);
                        BUSTER_TEST(arguments, stats.removed_parameters == 1);
                        BUSTER_TEST(arguments, stats.parameter_sweeps == 2);
                        BUSTER_TEST(arguments, stats.parameter_visits == 1);
                        BUSTER_TEST(arguments, stats.parameter_incoming_visits == 2);
                    }
                    if (index / 2 == BUSTER_ARRAY_LENGTH(fixtures) - 1)
                    {
                        BUSTER_TEST(arguments, stats.removed_parameters >= 2);
                        BUSTER_TEST(arguments, stats.parameter_sweeps == 3);
                    }
                    if (!stats.inserted_parameters && !joins)
                    {
                        // Local-only compaction still remaps rows and values,
                        // but executes no parameter-cleanup sweep.
                        BUSTER_TEST(arguments, stats.parameter_sweeps == 0);
                        BUSTER_TEST(arguments, stats.parameter_visits == 0);
                        BUSTER_TEST(arguments, stats.parameter_incoming_visits == 0);
                    }
                    if (stats.inserted_parameters && !stats.removed_parameters)
                    {
                        BUSTER_TEST(arguments, stats.parameter_sweeps == 1);
                        BUSTER_TEST(arguments, stats.parameter_visits == joins);
                    }
                    if (fixture.joins)
                    {
                        BUSTER_TEST(arguments, stats.parameter_visits > 0);
                        BUSTER_TEST(arguments, stats.parameter_incoming_visits > 0);
                    }
                }
                else
                {
                    BUSTER_TEST(arguments, stats.parameter_sweeps == 0);
                    BUSTER_TEST(arguments, stats.parameter_block_visits == 0);
                    BUSTER_TEST(arguments, stats.parameter_visits == 0);
                    BUSTER_TEST(arguments, stats.parameter_incoming_visits == 0);
                }
                IrValidationResult repeated = ir_prepare_canonical_module(program, module, input_certified);
                BUSTER_TEST(arguments, repeated.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, repeated.boundary == (input_certified ? IR_VALIDATION_BOUNDARY_UNSPECIFIED : IR_VALIDATION_BOUNDARY_CANONICAL_INPUT));
                BUSTER_TEST(arguments, memory_compare(&stats, &module->local_promotion, sizeof(stats)));
                BUSTER_TEST(arguments, (function->opcode_summary & IR_OPCODE_SUMMARY_KNOWN) != 0);
            }
        }
        scratch_end(temporary);
    }
    // Linked-builder input can have parameters before local promotion. The
    // strict validator follows first/next but does not check last_parameter;
    // the old unconditional sweep repaired a stale tail before publication.
    for (u32 stale_tail = 0; stale_tail < 2; stale_tail += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena,
            S8("int test(int c){int v=1;if(c)v=2;else v=3;v=4;return v;}"), target_native);
        if (BUSTER_REQUIRE(arguments, lowered.program && !lowered.diagnostic_count))
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            IrFunction* function = module->functions;
            BUSTER_TEST(arguments, ir_function_publish_cfg(arguments->arena, function).error == IR_VALIDATION_NONE);
            ir_function_invalidate_cfg(function);
            IrBlock* join = 0;
            IrValueId argument = IR_VALUE_ID_INVALID;
            IrInstructionId argument_row = IR_INSTRUCTION_ID_INVALID;
            for (u32 block = 0; block < function->block_count; block += 1)
            {
                if (function->blocks[block].predecessor_count == 2) join = function->blocks + block;
            }
            for (u32 row = 0; row < function->instruction_count; row += 1)
            {
                if (function->instructions[row].opcode == IR_OPCODE_ARGUMENT)
                {
                    argument = function->instructions[row].result;
                    argument_row.value = row;
                }
            }
            if (BUSTER_REQUIRE(arguments, join && argument.value < function->value_count))
            {
                IrTypeId type = function->values[argument.value].canonical_type;
                IrBlockParameter* parameters = arena_allocate(arguments->arena, IrBlockParameter, 2);
                for (u32 index = 0; index < 2; index += 1)
                {
                    IrValueId value = ir_function_add_value(arguments->arena, function, (IrValue){
                        .canonical_type = type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE,
                    });
                    IrIncoming* incoming = arena_allocate(arguments->arena, IrIncoming, join->predecessor_count);
                    IrPredecessor* predecessor = join->first_predecessor;
                    for (u32 pred = 0; pred < join->predecessor_count; pred += 1)
                    {
                        incoming[pred] = (IrIncoming){.predecessor = predecessor->block, .value = argument,
                            .next = pred + 1 < join->predecessor_count ? incoming + pred + 1 : 0};
                        predecessor = predecessor->next;
                    }
                    parameters[index] = (IrBlockParameter){.value = value, .canonical_type = type,
                        .canonical_local = IR_LOCAL_ID_INVALID, .incoming_count = join->predecessor_count,
                        .first_incoming = incoming, .last_incoming = incoming + join->predecessor_count - 1,
                        .next = index ? 0 : parameters + 1};
                }
                join->first_parameter = parameters;
                join->last_parameter = stale_tail ? parameters : parameters + 1;
                join->parameter_count = 2;
                IrInstructionExtra* retained_extra = ir_instruction_extra_ensure(arguments->arena, function, argument_row);
                if (BUSTER_REQUIRE(arguments, retained_extra != 0)) retained_extra->literal = S8("retained-argument");
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, false);
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, module->local_promotion.promoted_locals > 0);
                BUSTER_TEST(arguments, module->local_promotion.inserted_parameters == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_sweeps == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_block_visits == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_visits == 0);
                BUSTER_TEST(arguments, module->local_promotion.parameter_incoming_visits == 0);
                IrPublishedCfg const* cfg = function->published_cfg;
                if (BUSTER_REQUIRE(arguments, cfg != 0))
                {
                    IrCfgBlock const* published = cfg->blocks + join->id.value;
                    BUSTER_TEST(arguments, published->parameter_count == 2);
                    BUSTER_TEST(arguments, !join->first_parameter && !join->last_parameter);
                    BUSTER_TEST(arguments, cfg->parameters[published->parameter_offset].value.value !=
                                           cfg->parameters[published->parameter_offset + 1].value.value);
                    bool extra_retained = false;
                    for (u32 extra = 0; extra < function->extra_count; extra += 1)
                    {
                        IrInstructionId row = function->extra_instructions[extra];
                        extra_retained |= row.value < function->instruction_count &&
                                          function->instructions[row.value].opcode == IR_OPCODE_ARGUMENT &&
                                          string_equal(function->extras[extra].literal, S8("retained-argument"));
                    }
                    BUSTER_TEST(arguments, extra_retained);
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                }
                IrLocalPromotionStatistics stats = module->local_promotion;
                BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, memory_compare(&stats, &module->local_promotion, sizeof(stats)));
            }
        }
        scratch_end(temporary);
    }
    // The two expressions deliberately share their source operand slice.
    // Dense compaction must copy it per retained row before remapping IDs.
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena,
            S8("int test(int a,int b){int x=3;return (a+b)+(a+b)+x;}"), target_native);
        if (BUSTER_REQUIRE(arguments, lowered.program && !lowered.diagnostic_count))
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            IrFunction* function = module->functions;
            IrInstruction* first = 0;
            IrInstruction* second = 0;
            for (u32 row = 0; row < function->instruction_count && !second; row += 1)
            {
                IrInstruction* candidate = function->instructions + row;
                if (candidate->opcode == IR_OPCODE_BINARY && candidate->operand_count == 2)
                {
                    for (u32 previous = 0; previous < row && !second; previous += 1)
                    {
                        IrInstruction* prior = function->instructions + previous;
                        bool same_inputs = prior->opcode == candidate->opcode &&
                                           prior->binary_operation == candidate->binary_operation &&
                                           prior->canonical_type.value == candidate->canonical_type.value &&
                                           prior->operand_count == candidate->operand_count;
                        for (u32 operand = 0; operand < 2 && same_inputs; operand += 1)
                        {
                            IrValueId left = prior->operands[operand];
                            IrValueId right = candidate->operands[operand];
                            if (left.value != right.value)
                            {
                                IrInstructionId left_def = function->values[left.value].definition;
                                IrInstructionId right_def = function->values[right.value].definition;
                                IrInstruction* left_row = left_def.value < function->instruction_count ? function->instructions + left_def.value : 0;
                                IrInstruction* right_row = right_def.value < function->instruction_count ? function->instructions + right_def.value : 0;
                                same_inputs = left_row && right_row &&
                                              left_row->opcode == IR_OPCODE_LOAD && right_row->opcode == IR_OPCODE_LOAD &&
                                              left_row->operand_count == 1 && right_row->operand_count == 1 &&
                                              left_row->operands[0].value == right_row->operands[0].value;
                            }
                        }
                        if (same_inputs)
                        {
                            first = prior;
                            second = candidate;
                        }
                    }
                }
            }
            if (BUSTER_REQUIRE(arguments, first && second))
            {
                second->operands = first->operands;
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, false);
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, module->local_promotion.promoted_locals > 0 &&
                                       module->local_promotion.inserted_parameters == 0 &&
                                       module->local_promotion.parameter_sweeps == 0);
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                u32 matching = 0;
                for (u32 row = 0; row < function->instruction_count; row += 1)
                {
                    IrInstruction* candidate = function->instructions + row;
                    if (candidate->opcode == IR_OPCODE_BINARY && candidate->operand_count == 2)
                    {
                        for (u32 previous = 0; previous < row; previous += 1)
                        {
                            IrInstruction* prior = function->instructions + previous;
                            if (prior->opcode == candidate->opcode && prior->binary_operation == candidate->binary_operation &&
                                prior->operand_count == candidate->operand_count &&
                                prior->operands[0].value == candidate->operands[0].value &&
                                prior->operands[1].value == candidate->operands[1].value)
                            {
                                BUSTER_TEST(arguments, prior->operands != candidate->operands);
                                matching += 1;
                            }
                        }
                    }
                }
                BUSTER_TEST(arguments, matching >= 1);
            }
        }
        scratch_end(temporary);
    }
    return result;
}
