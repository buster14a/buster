// Included by ir_test.c: structural coverage is separate from the executable
// fixture so unchanged runtime behavior cannot conceal missing promotion.
BUSTER_GLOBAL_LOCAL CIRLowerResult ir_promotion_lower(Arena* arena, String8 source, Target target)
{
    CPreprocessResult preprocess = c_preprocess(arena, source, (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
    CAnalysisResult analysis = c_parse(arena, preprocess);
    CIRLowerResult result = {0};
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        result = c_lower_to_ir(arena, S8("local-promotion.c"), preprocess, analysis, target);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_promotion_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct Fixture
    {
        String8 source;
        bool all_locals;
        bool joins;
        bool uninitialized;
        bool barrier;
        bool trivial;
    } fixtures[] = {
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
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(fixtures); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        struct Fixture fixture = fixtures[index];
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
                BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, function->instruction_count == old_instructions && function->value_count == old_values);
                BUSTER_TEST(arguments, !module->local_promotion_complete && module->local_promotion.promoted_locals == 0);
                program->disable_local_promotion = false;
                IrValidationResult valid = ir_prepare_canonical_module(program, module, false);
                BUSTER_TEST(arguments, valid.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, module->local_promotion_complete);
                u32 locals = ir_test_opcode_count(function, IR_OPCODE_LOCAL);
                u64 joins = 0;
                for (u32 bi = 0; bi < function->block_count; bi += 1)
                {
                    IrBlock* block = function->blocks + bi;
                    joins += block->parameter_count;
                    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
                    {
                        BUSTER_TEST(arguments, parameter->value.value < function->value_count);
                        if (parameter->value.value < function->value_count)
                        {
                            BUSTER_TEST(arguments, function->values[parameter->value.value].definition.value == IR_ID_UNDERLYING_INVALID);
                        }
                        BUSTER_TEST(arguments, block->predecessor_count > 1 && parameter->incoming_count == block->predecessor_count);
                        IrPredecessor* pred = block->first_predecessor;
                        for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
                        {
                            BUSTER_TEST(arguments, pred && incoming->predecessor.value == pred->block.value);
                            BUSTER_TEST(arguments, incoming->value.value < function->value_count);
                            if (pred) pred = pred->next;
                        }
                        BUSTER_TEST(arguments, pred == 0);
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
                BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, memory_compare(&stats, &module->local_promotion, sizeof(stats)));
                BUSTER_TEST(arguments, (function->opcode_summary & IR_OPCODE_SUMMARY_KNOWN) != 0);
            }
        }
        scratch_end(temporary);
    }
    return result;
}
