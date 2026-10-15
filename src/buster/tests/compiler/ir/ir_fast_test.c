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
        c_test_scratch_end(temporary);
    }
    return result;
}

// Lowers `source` through the ordinary frontend SSA path and runs promotion
// with no optional pass. Callers that read `functions` define one function;
// a declared callee is allowed when only module statistics are read.
BUSTER_GLOBAL_LOCAL IrProgram* ir_fast_cse_lower(UnitTestArguments* arguments, String8 source)
{
    IrProgram* program = 0;
    CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
        (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
    CAnalysisResult analysis = c_parse_for_lowering(arguments->arena, preprocess);
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        CIRLowerResult lowered = c_lower_to_ir_with_options(arguments->arena, S8("fast-cse.c"), preprocess, analysis, target_native,
                                                            // The cases count CSE's own constant hits, so each constant keeps its row.
                                                            (CIRLowerOptions){.disable_block_constant_interning = true});
        if (lowered.program && !lowered.diagnostic_count && lowered.program->modules->function_count)
        {
            lowered.program->fast_passes = 0;
            if (ir_prepare_canonical_module(lowered.program, lowered.program->modules, false).error == IR_VALIDATION_NONE) program = lowered.program;
        }
    }
    return program;
}

// Block-local value numbering (#3011): exact hits, near misses, excluded
// operations, block boundaries, root resolution after folding, constants,
// table saturation/collisions, metadata declines and generation rollover.
// Each case also proves the removed rows are exactly the counted hits.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_cse_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 const cse = IR_FAST_PASS_BIT(IR_FAST_CSE);
    struct
    {
        String8 source;
        u32 passes;
        u32 hits;
        u32 commuted;
        u32 constants;
        bool no_candidates;
    } cases[] = {
        // b+a, b&a and b*a commute onto earlier rows; a-b/b-a stay ordered;
        // the repeated duplicate-operand a+a hits without commuting.
        {S8("unsigned test(unsigned a,unsigned b){return ((a+b)*(b+a))^((a-b)^(b-a))^((a&b)|(b&a))^(a*b+b*a)^(a+a)^(a+a);}"), cse, 4, 3, 0, false},
        // Equal expressions in three different blocks never share an entry.
        {S8("unsigned test(unsigned a,unsigned b,int c){unsigned x=a^b;if(c)x+=a^b;return x+(a^b);}"), cse, 0, 0, 0, false},
        // Signed arithmetic is outside the admitted domain, including its constants.
        {S8("int test(int a,int b){return (a+b)^(b+a)^(a*3)^(b*3);}"), cse, 0, 0, 0, true},
        // Loads, division and shifts are excluded even when repeated exactly.
        {S8("unsigned test(unsigned*p){return *p+*p;}"), cse, 0, 0, 0, false},
        {S8("unsigned test(unsigned a,unsigned b){return a/b+a/b;}"), cse, 0, 0, 0, false},
        {S8("unsigned test(unsigned a,unsigned b){return (a<<b)^(a<<b);}"), cse, 0, 0, 0, false},
        // a+c matches a+b only after folding has resolved c to b.
        {S8("unsigned test(unsigned a,unsigned b){unsigned c=b+0u;return (a+b)^(a+c);}"), cse, 0, 0, 0, false},
        {S8("unsigned test(unsigned a,unsigned b){unsigned c=b+0u;return (a+b)^(a+c);}"), cse | IR_FAST_PASS_BIT(IR_FAST_FOLD), 1, 0, 0, false},
        // Equal unsigned constants share one row, which then exposes a*3 twice.
        {S8("unsigned test(unsigned a){return (a*3u)^(a*3u)^(a+7u);}"), cse, 2, 0, 1, false},
    };
    // Every case is short enough that the linear window arm must agree.
    for (u32 run = 0; run < 2 * BUSTER_ARRAY_LENGTH(cases); run += 1)
    {
        u32 index = run / 2;
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrProgram* program = ir_fast_cse_lower(arguments, cases[index].source);
        if (BUSTER_REQUIRE(arguments, program != 0))
        {
            IrModule* module = program->modules;
            IrFunction* function = module->functions;
            u32 binaries = ir_test_opcode_count(function, IR_OPCODE_BINARY);
            u32 constants = ir_test_opcode_count(function, IR_OPCODE_CONSTANT_INTEGER);
            u32 loads = ir_test_opcode_count(function, IR_OPCODE_LOAD);
            program->fast_passes = cases[index].passes;
            program->fast_cse_window = run & 1;
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
            IrFastCseStatistics census = module->fast.cse;
            if (census.hits != cases[index].hits || census.commuted_hits != cases[index].commuted || census.constant_hits != cases[index].constants)
            {
                arguments->show(arguments, S8("CSE case {u32} window={u32}: hits={u64} commuted={u64} constants={u64} candidates={u64}\n"), index, run & 1, census.hits,
                                census.commuted_hits, census.constant_hits, census.candidates);
            }
            BUSTER_TEST(arguments, census.hits == cases[index].hits && module->fast.passes[IR_FAST_CSE].changes == cases[index].hits);
            BUSTER_TEST(arguments, census.commuted_hits == cases[index].commuted && census.constant_hits == cases[index].constants);
            BUSTER_TEST(arguments, (census.candidates == 0) == cases[index].no_candidates);
            BUSTER_TEST(arguments, !census.metadata_declines && !census.saturation_declines);
            BUSTER_TEST(arguments, census.window_hits <= census.hits && census.table_bytes != 0 && census.blocks >= function->block_count);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_LOAD) == loads);
            if (!(cases[index].passes & IR_FAST_PASS_BIT(IR_FAST_FOLD)))
            {
                BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_BINARY) == binaries - (cases[index].hits - cases[index].constants));
                BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_CONSTANT_INTEGER) == constants - cases[index].constants);
            }
        }
        c_test_scratch_end(temporary);
    }
    // One block with more distinct keys than the table admits: insertion is
    // declined, probes collide, and the duplicates keyed before saturation
    // (the constant 1u, then a*1u) still hit. Folding is off so a*1u stays.
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        String8 source = S8("unsigned test(unsigned a){unsigned s=a*1u;");
        for (u32 factor = 2; factor <= IR_FAST_CSE_SLOTS + 44; factor += 1) source = string_format(arguments->arena, S8("{S8}s^=a*{u32}u;"), source, factor);
        source = string_format(arguments->arena, S8("{S8}return s^(a*1u);}"), source);
        IrProgram* program = ir_fast_cse_lower(arguments, source);
        if (BUSTER_REQUIRE(arguments, program != 0))
        {
            IrModule* module = program->modules;
            program->fast_passes = cse;
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
            IrFastCseStatistics census = module->fast.cse;
            BUSTER_TEST(arguments, census.hits == 2 && census.constant_hits == 1 && census.window_hits == 0);
            BUSTER_TEST(arguments, census.saturation_declines != 0 && census.collisions != 0);
            BUSTER_TEST(arguments, census.max_block_candidates > IR_FAST_CSE_SLOTS && census.table_bytes != 0);
        }
        c_test_scratch_end(temporary);
    }
    // The linear arm keeps only the last IR_FAST_CSE_WINDOW keyed rows: a
    // duplicate behind WINDOW-1 other keyed rows still hits, one more pushes
    // its representative out. The table arm catches both.
    for (u32 run = 0; run < 4; run += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        u32 distinct = IR_FAST_CSE_WINDOW - 1 + (run >> 1);
        // Each s*=c is a new keyed row; nothing else between the two a+b is keyed.
        String8 source = S8("unsigned test(unsigned a,unsigned b,unsigned c){unsigned s=a+b;");
        for (u32 index = 1; index <= distinct; index += 1) source = string_format(arguments->arena, S8("{S8}s*=c;"), source);
        source = string_format(arguments->arena, S8("{S8}return s^(a+b);}"), source);
        IrProgram* program = ir_fast_cse_lower(arguments, source);
        if (BUSTER_REQUIRE(arguments, program != 0))
        {
            program->fast_passes = cse;
            program->fast_cse_window = run & 1;
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, program->modules, false).error == IR_VALIDATION_NONE);
            IrFastCseStatistics census = program->modules->fast.cse;
            if (census.hits != (u64)((run & 1) && (run >> 1) ? 0 : 1)) arguments->show(arguments, S8("CSE window run {u32}: hits={u64} candidates={u64}\n"), run, census.hits, census.candidates);
            BUSTER_TEST(arguments, census.hits == (u64)((run & 1) && (run >> 1) ? 0 : 1) && !census.saturation_declines);
        }
        c_test_scratch_end(temporary);
    }
    // Hits age the window too: in A, B x k, A (call arguments, so nothing
    // else between them is keyed) the window keeps A behind seven repeated
    // B hits but not behind eight. The table keeps A either way.
    for (u32 run = 0; run < 4; run += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        u32 repeats = IR_FAST_CSE_WINDOW - 1 + (run >> 1);
        String8 source = S8("unsigned sink(unsigned,...);unsigned test(unsigned a,unsigned b,unsigned c,unsigned d){return sink(a+b");
        for (u32 index = 0; index < repeats; index += 1) source = string_format(arguments->arena, S8("{S8},c*d"), source);
        source = string_format(arguments->arena, S8("{S8},a+b);}"), source);
        IrProgram* program = ir_fast_cse_lower(arguments, source);
        if (BUSTER_REQUIRE(arguments, program != 0))
        {
            program->fast_passes = cse;
            program->fast_cse_window = run & 1;
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, program->modules, false).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, ir_validate_canonical_module(program, program->modules).error == IR_VALIDATION_NONE);
            IrFastCseStatistics census = program->modules->fast.cse;
            u64 expected = repeats - 1 + ((run & 1) && (run >> 1) ? 0 : 1);
            if (census.hits != expected) arguments->show(arguments, S8("CSE repeated-hit run {u32}: hits={u64} candidates={u64}\n"), run, census.hits, census.candidates);
            BUSTER_TEST(arguments, census.hits == expected && census.candidates == repeats + 2);
        }
        c_test_scratch_end(temporary);
    }
    // Hand-edited duplicates: an exact match whose result facts differ is
    // declined, an ordered operation is a different key, a volatile row is
    // not a candidate, and generation rollover clears the table first.
    u64 table_bytes = 0;
    for (u32 variant = 0; variant < 5; variant += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        IrProgram* program = ir_fast_cse_lower(arguments, S8("unsigned test(unsigned a,unsigned b){return (a+b)^(a+b);}"));
        if (BUSTER_REQUIRE(arguments, program != 0))
        {
            IrFunction* function = program->modules->functions;
            IrInstruction* second = 0;
            u32 adds = 0;
            for (u32 index = 0; index < function->instruction_count; index += 1)
            {
                IrInstruction* row = function->instructions + index;
                if (row->opcode == IR_OPCODE_BINARY && row->binary_operation == IR_BINARY_INTEGER_ADD && ++adds == 2) second = row;
            }
            if (BUSTER_REQUIRE(arguments, second != 0))
            {
                if (variant == 1) function->values[second->result.value].is_read_only = !function->values[second->result.value].is_read_only;
                else if (variant == 2) second->binary_operation = IR_BINARY_INTEGER_SUBTRACT;
                else if (variant == 3) second->volatile_access = true;
                program->fast_passes = cse;
                IrFastStatistics statistics = ir_test_fast_cse_function(program, function, variant == 4 ? UINT32_MAX : 0);
                BUSTER_TEST(arguments, statistics.cse.hits == (variant == 0 || variant == 4));
                BUSTER_TEST(arguments, statistics.cse.metadata_declines == (variant == 1));
                BUSTER_TEST(arguments, statistics.cse.candidates == (variant == 3 ? 2u : 3u));
                if (!variant) table_bytes = statistics.cse.table_bytes;
                BUSTER_TEST(arguments, table_bytes != 0 && statistics.cse.table_bytes == (variant == 4 ? 2 * table_bytes : table_bytes));
            }
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// A producer-certified module holding one function the strict validator
// rejects: FAST declines that function alone and still rewrites the other
// (#1602). A failure outside every function still declines them all. Neither
// may become a preparation error, including under the FAST-output check that
// Debug/test/sanitizer builds run, and the decline must not hide the defect.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_decline_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 variant = 0; variant < 5; variant += 1)
    {
        bool module_scope = variant == 1;
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena,
            S8("int storage=1;int broken(int x){return x+0;}int kept(int y){return y+0;}"), target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            IrFunction* broken = 0;
            IrFunction* kept = 0;
            IrGlobal* storage = 0;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* candidate = module->functions + index;
                if (string_equal(candidate->name, S8("broken"))) broken = candidate;
                if (string_equal(candidate->name, S8("kept"))) kept = candidate;
            }
            for (u32 index = 0; index < module->global_count; index += 1)
            {
                IrSymbol* symbol = ir_symbol_from_id(&program->symbols, module->globals[index].symbol);
                if (symbol && string_equal(symbol->name, S8("storage"))) storage = module->globals + index;
            }
            // Publish first so byte comparisons exclude CFG publication's
            // retirement of construction links, and isolate FAST's writes.
            BUSTER_TEST(arguments, ir_function_publish_cfg(program->arena, broken).error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, ir_function_publish_cfg(program->arena, kept).error == IR_VALIDATION_NONE);
            bool corrupted = false;
            if (broken && kept && storage)
            {
                if (module_scope)
                {
                    storage->alignment = 3;
                    corrupted = true;
                }
                for (u32 index = 0; !module_scope && index < broken->instruction_count && !corrupted; index += 1)
                {
                    IrInstruction* row = broken->instructions + index;
                    if (row->opcode == IR_OPCODE_BINARY)
                    {
                        row->binary_operation = IR_BINARY_COUNT;
                        corrupted = true;
                    }
                }
            }
            if (variant == 2)
            {
                for (u32 index = 0; index < kept->instruction_count; index += 1)
                {
                    if (kept->instructions[index].opcode == IR_OPCODE_BINARY)
                    {
                        kept->instructions[index].binary_operation = IR_BINARY_COUNT;
                    }
                }
            }
            BUSTER_TEST(arguments, corrupted);
            if (corrupted)
            {
                u32 broken_count = broken->instruction_count;
                u32 kept_count = kept->instruction_count;
                IrInstruction* original = arena_allocate(arguments->arena, IrInstruction, broken_count);
                memcpy(original, broken->instructions, sizeof(*original) * broken_count);
                u32 value_count = broken->value_count;
                IrValue* original_values = arena_allocate(arguments->arena, IrValue, value_count);
                memcpy(original_values, broken->values, sizeof(*original_values) * value_count);
                program->disable_local_promotion = true;
                program->fast_passes = variant == 4 ? 0 : IR_FAST_ALL;
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, variant != 3);
                BUSTER_TEST(arguments, prepared.error == (variant == 3 ? IR_VALIDATION_OPERATION : IR_VALIDATION_NONE));
                BUSTER_TEST(arguments, broken->instruction_count == broken_count && broken->value_count == value_count);
                BUSTER_TEST(arguments, memcmp(original, broken->instructions, sizeof(*original) * broken_count) == 0);
                BUSTER_TEST(arguments, memcmp(original_values, broken->values, sizeof(*original_values) * value_count) == 0);
                u64 skips = variant >= 3 ? 0 : (variant == 0 ? 1 : 2);
                BUSTER_TEST(arguments, module->fast.validation_skips == skips && module->fast.decline_count == skips);
                if (variant == 0)
                {
                    BUSTER_TEST(arguments, module->fast.functions == 1 && module->fast.passes[IR_FAST_FOLD].changes != 0 &&
                                           kept->instruction_count < kept_count);
                }
                else
                {
                    BUSTER_TEST(arguments, module->fast.functions == 0 && kept->instruction_count == kept_count);
                }
                for (u64 index = 0; index < module->fast.decline_count; index += 1)
                {
                    IrFastDecline decline = module->fast.declines[index];
                    IrValidationResult validation = decline.validation;
                    BUSTER_TEST(arguments, string_equal(decline.function_name, index ? kept->name : broken->name));
                    BUSTER_TEST(arguments, validation.error == (module_scope ? IR_VALIDATION_ALIGNMENT : IR_VALIDATION_OPERATION));
                    BUSTER_TEST(arguments, validation.function.value == (module_scope ? IR_ID_UNDERLYING_INVALID :
                                                                         (index ? kept->id.value : broken->id.value)));
                    BUSTER_TEST(arguments, validation.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
                }
                if (variant < 3)
                {
                    IrFastDecline* records = module->fast.declines;
                    BUSTER_TEST(arguments, module->fast_complete);
                    prepared = ir_prepare_canonical_module(program, module, true);
                    BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE && module->fast.validation_skips == skips &&
                                           module->fast.declines == records);
                }
                IrValidationResult strict = ir_validate_canonical_module(program, module);
                BUSTER_TEST(arguments, strict.error == (module_scope ? IR_VALIDATION_ALIGNMENT : IR_VALIDATION_OPERATION));
                BUSTER_TEST(arguments, strict.function.value == (module_scope ? IR_ID_UNDERLYING_INVALID : broken->id.value));
            }
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// Lowers `source` with construction-time constant interning (#3011) on or
// off, validates it through preparation with no optional pass, and returns
// the lowering counters with the program.
BUSTER_GLOBAL_LOCAL CIRLowerResult ir_fast_intern_lower(UnitTestArguments* arguments, String8 source, bool intern)
{
    CIRLowerResult result = {0};
    CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
        (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
    CAnalysisResult analysis = c_parse_for_lowering(arguments->arena, preprocess);
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        CIRLowerResult lowered = c_lower_to_ir_with_options(arguments->arena, S8("intern.c"), preprocess, analysis, target_native,
                                                            (CIRLowerOptions){.disable_block_constant_interning = !intern});
        if (lowered.program && !lowered.diagnostic_count && lowered.program->modules->function_count)
        {
            lowered.program->fast_passes = 0;
            if (ir_prepare_canonical_module(lowered.program, lowered.program->modules, false).error == IR_VALIDATION_NONE) result = lowered;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 ir_fast_intern_constant_rows(IrProgram* program)
{
    u32 rows = 0;
    IrModule* module = program->modules;
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        rows += ir_test_opcode_count(module->functions + index, IR_OPCODE_CONSTANT_INTEGER);
    }
    return rows;
}

// Construction-time constant interning (#3011): a repeated literal in one
// block reuses the earlier constant and allocates no row, while different
// blocks, types and signs keep their own rows. Off, nothing is looked up.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_intern_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        u32 hits;
    } cases[] = {
        // Three uses of 3u in one block share the first; 7u is distinct.
        {S8("unsigned test(unsigned a){return (a*3u)^(a*3u)^(a+7u)^(a*3u);}"), 2},
        // The same literal in three different blocks is never shared.
        {S8("int test(int c,int a){int x=a*5;if(c){x=x*5;}else{x=x-1;}return x*5;}"), 0},
        // 1, 1u and 1ll have different canonical types; only the repeated int 1 is shared.
        {S8("long long test(int a,unsigned b,long long c){return (a*1)+(b*1u)+(c*1ll)+(a*1);}"), 1},
        // `-9` lowers as a negation of the literal 9, so all three share it.
        {S8("int test(int a){return (a*-9)+(a*9)+(a*-9);}"), 2},
        // A negative enumerator is a negative constant, distinct from its magnitude.
        {S8("enum{N=-9,P=9};int test(int a){return (a*N)+(a*P)+(a*N);}"), 1},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult off = ir_fast_intern_lower(arguments, cases[index].source, false);
        CIRLowerResult on = ir_fast_intern_lower(arguments, cases[index].source, true);
        if (BUSTER_REQUIRE(arguments, off.program != 0 && on.program != 0))
        {
            CIRConstantInternStatistics interned = on.constant_interning;
            if (interned.hits != cases[index].hits)
            {
                arguments->show(arguments, S8("intern case {u32}: hits={u64} lookups={u64} stale={u64}\n"), index, interned.hits, interned.lookups,
                                interned.stale);
            }
            BUSTER_TEST(arguments, interned.hits == cases[index].hits && interned.lookups >= interned.hits && !interned.stale);
            BUSTER_TEST(arguments, !off.constant_interning.lookups && !off.constant_interning.hits && !off.constant_interning.stale);
            // Each hit is a constant row (and its value) that was never built.
            BUSTER_TEST(arguments, ir_fast_intern_constant_rows(on.program) + cases[index].hits == ir_fast_intern_constant_rows(off.program));
            BUSTER_TEST(arguments, on.program->modules->functions->value_count + cases[index].hits ==
                                   off.program->modules->functions->value_count);
            BUSTER_TEST(arguments, ir_validate_canonical_module(on.program, on.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(temporary);
    }
    return result;
}

// Lowers `source` with -fcanonical-construction-fold on or off and prepares
// it with the FAST passes in `passes` (0 keeps the rows as lowered).
BUSTER_GLOBAL_LOCAL CIRLowerResult ir_fast_construction_fold_lower(UnitTestArguments* arguments, String8 source, bool fold, u32 passes)
{
    CIRLowerResult result = {0};
    CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
        (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
    CAnalysisResult analysis = c_parse_for_lowering(arguments->arena, preprocess);
    if (!preprocess.error_count && !analysis.diagnostic_count)
    {
        CIRLowerResult lowered = c_lower_to_ir_with_options(arguments->arena, S8("construction-fold.c"), preprocess, analysis, target_native,
                                                            (CIRLowerOptions){.construction_fold = fold});
        if (lowered.program && !lowered.diagnostic_count && lowered.program->modules->function_count)
        {
            lowered.program->fast_passes = passes;
            if (ir_prepare_canonical_module(lowered.program, lowered.program->modules, false).error == IR_VALIDATION_NONE) result = lowered;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_fast_construction_fold_function(IrProgram* program)
{
    IrFunction* function = 0;
    IrModule* module = program->modules;
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        function = string_equal(module->functions[index].name, S8("test")) ? module->functions + index : function;
    }
    return function;
}

// The immediate `test` returns when its returned value is a CONSTANT_INTEGER row.
BUSTER_GLOBAL_LOCAL bool ir_fast_construction_fold_return(IrFunction const* function, u64* bits)
{
    bool result = false;
    for (u32 index = 0; function && index < function->instruction_count; index += 1)
    {
        IrInstruction const* row = function->instructions + index;
        u32 value = row->opcode == IR_OPCODE_RETURN && row->operand_count == 1 ? row->operands[0].value : UINT32_MAX;
        u32 definition = value < function->value_count ? function->values[value].definition.value : UINT32_MAX;
        IrInstruction const* source = definition < function->instruction_count ? function->instructions + definition : 0;
        if (source && source->opcode == IR_OPCODE_CONSTANT_INTEGER && source->immediate_count == 1)
        {
            *bits = source->immediates[0];
            result = true;
        }
    }
    return result;
}

// -fcanonical-construction-fold (#3010): c_ir_emit_cast and c_ir_truth_value
// build the constant FAST's rule gives a committed integer constant instead
// of its CAST or BINARY row, so the same function returns the same constant
// with those rows never built. Operands that are not single-limb integer
// constants keep their rows; off, nothing changes.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_construction_fold_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        u64 casts;
        u64 truths;
        // The returned constant's low bits, masked to its width; folded marks
        // a case whose return is a constant without FAST.
        u64 value;
        u32 width;
        bool folded;
    } cases[] = {
        // int -> long long sign-extends; unsigned -> unsigned long long zero-extends.
        {S8("long long test(void){return 5;}"), 1, 0, 5, 64, true},
        {S8("unsigned long long test(void){return 0xFFFFFFFFu;}"), 1, 0, 0xFFFFFFFFull, 64, true},
        // A reinterpreted unsigned literal is then sign-extended from the folded int.
        {S8("long long test(void){return (int)0xFFFFFFFFu;}"), 2, 0, UINT64_MAX, 64, true},
        // Truncation wraps, signed or unsigned.
        {S8("unsigned char test(void){return 300;}"), 1, 0, 44, 8, true},
        {S8("signed char test(void){return 200;}"), 1, 0, 200, 8, true},
        {S8("unsigned test(void){return 2147483647;}"), 1, 0, 2147483647, 32, true},
        // _Bool tests the whole value rather than truncating it.
        {S8("_Bool test(void){return 7;}"), 0, 1, 1, 1, true},
        {S8("_Bool test(void){return 0;}"), 0, 1, 0, 1, true},
        {S8("_Bool test(void){return 256LL;}"), 0, 1, 1, 1, true},
        {S8("_Bool test(void){return (unsigned char)256;}"), 1, 1, 0, 1, true},
        {S8("_Bool test(void){_Bool b=0;return b;}"), 0, 1, 0, 1, true},
        // The call still runs; only the literal's conversion folds.
        {S8("int effect(void);_Bool test(void){return (effect(),3);}"), 0, 1, 1, 1, true},
        // Unchanged: non-constant operands, wider than one limb, floating.
        {S8("long long test(int x){return x;}"), 0, 0, 0, 0, false},
        {S8("_Bool test(int x){return x;}"), 0, 0, 0, 0, false},
        {S8("long long test(int x){return -x;}"), 0, 0, 0, 0, false},
        {S8("__int128 test(void){return 5;}"), 0, 0, 0, 0, false},
        {S8("double test(void){return 5;}"), 0, 0, 0, 0, false},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters start = ir_construction_counters();
#endif
        CIRLowerResult off = ir_fast_construction_fold_lower(arguments, cases[index].source, false, 0);
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters middle = ir_construction_counters();
#endif
        CIRLowerResult on = ir_fast_construction_fold_lower(arguments, cases[index].source, true, 0);
#if BUSTER_BENCH_ALLOCATIONS
        IrConstructionCounters end = ir_construction_counters();
#endif
        CIRLowerResult off_fast = ir_fast_construction_fold_lower(arguments, cases[index].source, false, IR_FAST_ALL);
        CIRLowerResult on_fast = ir_fast_construction_fold_lower(arguments, cases[index].source, true, IR_FAST_ALL);
        if (BUSTER_REQUIRE(arguments, off.program && on.program && off_fast.program && on_fast.program))
        {
            CIRConstructionFoldStatistics folds = on.construction_folds;
            if (folds.casts != cases[index].casts || folds.truths != cases[index].truths)
            {
                arguments->show(arguments, S8("construction fold case {u32}: casts={u64} truths={u64}\n"), index, folds.casts, folds.truths);
            }
            BUSTER_TEST(arguments, folds.casts == cases[index].casts && folds.truths == cases[index].truths);
            BUSTER_TEST(arguments, !off.construction_folds.casts && !off.construction_folds.truths);
            IrFunction* function_off = ir_fast_construction_fold_function(off.program);
            IrFunction* function_on = ir_fast_construction_fold_function(on.program);
            if (BUSTER_REQUIRE(arguments, function_off && function_on))
            {
                // Each fold is a CAST or BINARY row that was never built. A
                // truth also never built its zero or the zero's conversion.
                u32 casts_off = ir_test_opcode_count(function_off, IR_OPCODE_CAST);
                u32 casts_on = ir_test_opcode_count(function_on, IR_OPCODE_CAST);
                BUSTER_TEST(arguments, folds.truths ? casts_on + folds.casts <= casts_off : casts_on + folds.casts == casts_off);
                BUSTER_TEST(arguments, ir_test_opcode_count(function_on, IR_OPCODE_BINARY) + folds.truths == ir_test_opcode_count(function_off, IR_OPCODE_BINARY));
                BUSTER_TEST(arguments, ir_test_opcode_count(function_on, IR_OPCODE_CALL) == ir_test_opcode_count(function_off, IR_OPCODE_CALL));
                u64 bits_on = 0;
                u64 bits_late = 0;
                u64 bits_both = 0;
                bool returned_on = ir_fast_construction_fold_return(function_on, &bits_on);
                bool returned_late = ir_fast_construction_fold_return(ir_fast_construction_fold_function(off_fast.program), &bits_late);
                bool returned_both = ir_fast_construction_fold_return(ir_fast_construction_fold_function(on_fast.program), &bits_both);
                BUSTER_TEST(arguments, returned_on == cases[index].folded && returned_late == returned_both);
                if (cases[index].folded)
                {
                    // The construction fold builds exactly what FAST folds to.
                    u64 mask = cases[index].width == 64 ? UINT64_MAX : (1ull << cases[index].width) - 1;
                    BUSTER_TEST(arguments, returned_late && bits_on == bits_late && bits_both == bits_late);
                    BUSTER_TEST(arguments, (bits_on & mask) == cases[index].value);
                }
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(on.program, on.program->modules).error == IR_VALIDATION_NONE);
#if BUSTER_BENCH_ALLOCATIONS
            // The census sees no row it never got: no birth fold or cascade
            // remains for a folded case, and an unchanged one counts alike.
            u64 births_off = 0;
            u64 births_on = 0;
            for (u32 counter = IR_CONSTRUCTION_FOLD_BIRTH_CAST_IDENTITY; counter <= IR_CONSTRUCTION_FOLD_BIRTH_BINARY_LEFT_IDENTITY; counter += 1)
            {
                births_off += middle.values[counter] - start.values[counter];
                births_on += end.values[counter] - middle.values[counter];
            }
            for (u32 counter = IR_CONSTRUCTION_FOLD_BIRTH_CASCADE_CAST_IDENTITY; counter <= IR_CONSTRUCTION_FOLD_BIRTH_CASCADE_BINARY_LEFT_IDENTITY; counter += 1)
            {
                births_off += middle.values[counter] - start.values[counter];
                births_on += end.values[counter] - middle.values[counter];
            }
            BUSTER_TEST(arguments, cases[index].folded ? births_on == 0 && births_off >= folds.casts + folds.truths : births_on == births_off);
#endif
        }
        c_test_scratch_end(temporary);
    }
    {
        // The rule authority's own metadata guard: a volatile result type has
        // no FAST width, so the candidate is left as is.
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_fast_construction_fold_lower(arguments, S8("volatile int g;_Bool h;long long test(void){g=1;h=0;return 5;}"),
                                                                 false, 0);
        IrFunction* function = lowered.program ? ir_fast_construction_fold_function(lowered.program) : 0;
        IrInstruction const* cast = 0;
        for (u32 index = 0; function && index < function->instruction_count; index += 1)
        {
            cast = function->instructions[index].opcode == IR_OPCODE_CAST ? function->instructions + index : cast;
        }
        IrTypeId volatile_type = IR_TYPE_ID_INVALID;
        IrTypeId bool_type = IR_TYPE_ID_INVALID;
        for (u32 index = 0; lowered.program && index < lowered.program->types.count; index += 1)
        {
            IrType* type = ir_type_from_id(&lowered.program->types, (IrTypeId){.value = index});
            volatile_type = type && type->kind == IR_TYPE_INTEGER && type->is_volatile ? (IrTypeId){.value = index} : volatile_type;
            bool_type = type && type->kind == IR_TYPE_BOOLEAN && !type->is_volatile && !type->is_atomic ? (IrTypeId){.value = index} : bool_type;
        }
        if (BUSTER_REQUIRE(arguments, cast && volatile_type.value != IR_ID_UNDERLYING_INVALID && bool_type.value != IR_ID_UNDERLYING_INVALID))
        {
            u64 bits = 0;
            IrInstruction candidate = *cast;
            BUSTER_TEST(arguments, ir_fast_fold_candidate(lowered.program, function, &candidate, 0, &bits) && bits == 5);
            candidate.canonical_type = volatile_type;
            candidate.conversion_operation = (u8)IR_CONVERSION_INTEGER_REINTERPRET;
            bits = 0;
            BUSTER_TEST(arguments, !ir_fast_fold_candidate(lowered.program, function, &candidate, 0, &bits) && bits == 0);
            // A truth candidate: its second operand supplies only a type,
            // and the bits given stand for its value.
            IrValueId operands[] = {cast->operands[0], cast->operands[0]};
            IrInstruction truth = {.opcode = IR_OPCODE_BINARY, .canonical_type = bool_type, .operands = operands, .operand_count = 2,
                                   .binary_operation = (u8)IR_BINARY_INTEGER_NOT_EQUAL};
            u64 right = 0;
            BUSTER_TEST(arguments, ir_fast_fold_candidate(lowered.program, function, &truth, &right, &bits) && bits == 1);
            right = 5;
            BUSTER_TEST(arguments, ir_fast_fold_candidate(lowered.program, function, &truth, &right, &bits) && bits == 0);
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_promotion_proof_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // One invalid body must not discard its neighbors' post-promotion proofs.
    // Reverse source order and disable promotion as the unproven-path control.
    for (u32 variant = 0; variant < 4; variant += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        String8 source = variant & 1u
            ? S8("void inspect(int*);int broken(int c){return c+0;}int kept(int c){inspect(&c);return c*3+0;}"
                 "int promoted(int c){int x=3;if(c)x=7;return x+0;}")
            : S8("void inspect(int*);int promoted(int c){int x=3;if(c)x=7;return x+0;}"
                 "int kept(int c){inspect(&c);return c*3+0;}int broken(int c){return c+0;}");
        CIRLowerResult lowered = ir_promotion_lower(arguments->arena, source, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrProgram* program = lowered.program;
            IrModule* module = program->modules;
            bool promotion = variant < 2;
            program->disable_local_promotion = !promotion;
            program->fast_passes = IR_FAST_ALL;
            IrFunction* kept = 0;
            bool corrupted = false;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (string_equal(function->name, S8("kept"))) kept = function;
                if (string_equal(function->name, S8("broken")))
                {
                    for (u32 row = 0; row < function->instruction_count; row += 1)
                    {
                        if (function->instructions[row].opcode == IR_OPCODE_BINARY)
                        {
                            function->instructions[row].binary_operation = IR_BINARY_COUNT;
                            corrupted = true;
                        }
                    }
                }
            }
            BUSTER_TEST(arguments, corrupted && kept);
            if (corrupted && kept)
            {
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters before = ir_construction_counters();
#endif
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, true);
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters after = ir_construction_counters();
                BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
                BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_INPUT_PROOFS_REUSED] -
                                       before.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_INPUT_PROOFS_REUSED] == (promotion ? 1u : 0u));
                BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_PROOFS_REUSED] -
                                       before.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_PROOFS_REUSED] == (promotion ? 2u : 0u));
#endif
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE && module->fast.functions == 2);
                BUSTER_TEST(arguments, module->fast.validation_skips == 1 && module->fast.decline_count == 1);
                BUSTER_TEST(arguments, (module->local_promotion.promoted_locals != 0) == promotion);
                BUSTER_TEST(arguments, module->fast.passes[IR_FAST_FOLD].changes != 0);
                if (module->fast.decline_count == 1)
                {
                    BUSTER_TEST(arguments, string_equal(module->fast.declines[0].function_name, S8("broken")) &&
                                           module->fast.declines[0].validation.error == IR_VALIDATION_OPERATION);
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_OPERATION);
                // A later preparation cannot inherit the temporary proof: a
                // formerly valid neighbor now fails and must decline as well.
                corrupted = false;
                for (u32 row = 0; row < kept->instruction_count; row += 1)
                {
                    if (kept->instructions[row].opcode == IR_OPCODE_BINARY)
                    {
                        kept->instructions[row].binary_operation = IR_BINARY_COUNT;
                        corrupted = true;
                    }
                }
                BUSTER_TEST(arguments, corrupted);
                module->fast_complete = false;
#if BUSTER_BENCH_ALLOCATIONS
                before = ir_construction_counters();
#endif
                prepared = ir_prepare_canonical_module(program, module, true);
#if BUSTER_BENCH_ALLOCATIONS
                after = ir_construction_counters();
                BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_PROOFS_REUSED] ==
                                       before.values[IR_CONSTRUCTION_PREPARATION_PROMOTION_PROOFS_REUSED]);
#endif
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE && module->fast.functions == 1);
                BUSTER_TEST(arguments, module->fast.validation_skips == 2 && module->fast.decline_count == 2);
            }
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_fast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_publication_span_tests(arguments);
    BUSTER_TEST_FIXTURE(arguments, ir_fast_decline_tests);
    BUSTER_TEST_FIXTURE(arguments, ir_fast_promotion_proof_tests);
    BUSTER_TEST_FIXTURE(arguments, ir_fast_cse_tests);
    BUSTER_TEST_FIXTURE(arguments, ir_fast_intern_tests);
    BUSTER_TEST_FIXTURE(arguments, ir_fast_construction_fold_tests);
    String8 source = S8("volatile int observed;int effect(int);"
                       "int test(int input,int* p){int x=input+0;int unused=x*9;"
                       "int a=3,b=4;int* q=&*p;observed=effect(x);return x+(a+b)+*q+observed;}");
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        CPreprocessResult preprocess = c_preprocess(arguments->arena, source,
            (CPreprocessOptions){.target = target_native, .data_layout = target_data_layout(target_native)});
        CAnalysisResult analysis = c_parse_for_lowering(arguments->arena, preprocess);
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
        c_test_scratch_end(temporary);
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
        c_test_scratch_end(temporary);
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
    c_test_scratch_end(temporary);
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
    c_test_scratch_end(invalid_temporary);
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
        c_test_scratch_end(reuse_temporary);
    }
    // Mandatory inlining of a certified module checks only the functions it
    // reads or rewrites. It must add no module-wide strict scan: every scope
    // scan is one preparation already counts (the FAST input/output guards),
    // as a default compile without inlining would pay.
    {
        TemporalArena inline_temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult inline_lowered = ir_promotion_lower(arguments->arena,
            S8("static inline __attribute__((always_inline)) int add_one(int x){return x+1;}"
               "int test(int c){return add_one(c)+add_one(c+2);}"), target_native);
        BUSTER_TEST(arguments, inline_lowered.program && !inline_lowered.diagnostic_count);
        if (inline_lowered.program && !inline_lowered.diagnostic_count)
        {
            IrProgram* inline_program = inline_lowered.program;
            IrModule* inline_module = inline_program->modules;
            inline_program->fast_passes = IR_FAST_ALL;
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters before = ir_construction_counters();
#endif
            IrValidationResult inlined = ir_prepare_canonical_module(inline_program, inline_module, true);
#if BUSTER_BENCH_ALLOCATIONS
            IrConstructionCounters after = ir_construction_counters();
#endif
            BUSTER_TEST(arguments, inlined.error == IR_VALIDATION_NONE);
            BUSTER_TEST(arguments, inline_module->inline_complete && inline_module->inlining.always_inlined == 2);
            BUSTER_TEST(arguments, ir_validate_canonical_module(inline_program, inline_module).error == IR_VALIDATION_NONE);
#if BUSTER_BENCH_ALLOCATIONS
            BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_PREPARATION_DELTA(counter) (after.values[IR_CONSTRUCTION_##counter] - before.values[IR_CONSTRUCTION_##counter])
            BUSTER_TEST(arguments, IR_PREPARATION_DELTA(PREPARATION_INPUT_VALIDATIONS) == 0);
            BUSTER_TEST(arguments, IR_PREPARATION_DELTA(VALIDATION_CALLS) ==
                                       IR_PREPARATION_DELTA(PREPARATION_PROMOTION_OUTPUT_VALIDATIONS) +
                                       IR_PREPARATION_DELTA(PREPARATION_FAST_INPUT_VALIDATIONS) +
                                       IR_PREPARATION_DELTA(PREPARATION_FAST_OUTPUT_VALIDATIONS));
#undef IR_PREPARATION_DELTA
#endif
        }
        c_test_scratch_end(inline_temporary);
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
#if BUSTER_BENCH_ALLOCATIONS
    // #3010 census: `input+0` is committed with its zero already a constant
    // row, so the construction probe sees the same right identity that FAST
    // later applies without a replacement root. Probes record no semantic work.
    {
        TemporalArena census_temporary = arena_begin_temporal(arguments->arena);
        IrConstructionCounters census_before = ir_construction_counters();
        CIRLowerResult census_lowered = ir_promotion_lower(arguments->arena, S8("int test(int input){return input+0;}"), target_native);
        IrConstructionCounters census_lowering = ir_construction_counters();
        if (BUSTER_REQUIRE(arguments, census_lowered.program && !census_lowered.diagnostic_count))
        {
            u64 identities = census_lowering.values[IR_CONSTRUCTION_FOLD_BIRTH_BINARY_RIGHT_IDENTITY] -
                             census_before.values[IR_CONSTRUCTION_FOLD_BIRTH_BINARY_RIGHT_IDENTITY];
            u64 bytes = census_lowering.values[IR_CONSTRUCTION_FOLD_BIRTH_AVOIDABLE_BYTES] - census_before.values[IR_CONSTRUCTION_FOLD_BIRTH_AVOIDABLE_BYTES];
            BUSTER_TEST(arguments, identities == 1 && bytes >= sizeof(IrInstruction) + sizeof(IrValue) + 2 * sizeof(IrValueId));
            BUSTER_TEST(arguments, census_lowering.values[IR_CONSTRUCTION_FOLD_BIRTH_SCALAR_ROWS] > census_before.values[IR_CONSTRUCTION_FOLD_BIRTH_SCALAR_ROWS]);
            IrProgram* census_program = census_lowered.program;
            IrFunction* census_function = census_program->modules->functions;
            IrSemanticCounters semantic_before = ir_semantic_counters();
            ArenaBenchmarkCounters arena_before = arena_benchmark_counters();
            for (u32 index = 0; index < census_function->instruction_count; index += 1)
            {
                ir_construction_fold_census(census_program, census_function, (IrInstructionId){.value = index});
            }
            ArenaBenchmarkCounters arena_after = arena_benchmark_counters();
            IrSemanticCounters semantic_after = ir_semantic_counters();
            BUSTER_TEST(arguments, memcmp(&semantic_before, &semantic_after, sizeof(semantic_before)) == 0);
            // Birth tags live in the row's padding byte and cascade bits in a
            // static cache, so probing allocates nothing.
            BUSTER_TEST(arguments, memcmp(&arena_before, &arena_after, sizeof(arena_before)) == 0);
            IrConstructionCounters probed = ir_construction_counters();
            BUSTER_TEST(arguments, probed.values[IR_CONSTRUCTION_FOLD_BIRTH_BINARY_RIGHT_IDENTITY] -
                                       census_lowering.values[IR_CONSTRUCTION_FOLD_BIRTH_BINARY_RIGHT_IDENTITY] == 1);
            census_program->fast_passes = IR_FAST_PASS_BIT(IR_FAST_FOLD);
            IrFastStatistics folded = ir_test_fast_function(census_program, census_function);
            IrConstructionCounters late = ir_construction_counters();
            BUSTER_TEST(arguments, folded.passes[IR_FAST_FOLD].changes == 1);
            BUSTER_TEST(arguments, late.values[IR_CONSTRUCTION_FOLD_LATE_BINARY_RIGHT_IDENTITY] - probed.values[IR_CONSTRUCTION_FOLD_LATE_BINARY_RIGHT_IDENTITY] == 1);
            BUSTER_TEST(arguments, late.values[IR_CONSTRUCTION_FOLD_LATE_REPLACED_ROOTS] == probed.values[IR_CONSTRUCTION_FOLD_LATE_REPLACED_ROOTS]);
        }
        c_test_scratch_end(census_temporary);
    }
    // #3010 end-of-body census. `input+1` converts the int literal to unsigned
    // for an operator; `return 0` from a _Bool function compares two literals.
    // Each literal is read only by its folded row, so both become dead.
    {
        TemporalArena census_temporary = arena_begin_temporal(arguments->arena);
        IrConstructionCounters before = ir_construction_counters();
        // Each literal must keep its own row: interning would share truth's two equal zeros.
        CIRLowerResult form_lowered = ir_promotion_lower_with_options(arguments->arena,
            S8("unsigned test(unsigned input){return input+1;} _Bool truth(void){return 0;}"), target_native,
            (CIRLowerOptions){.disable_block_constant_interning = true});
        IrConstructionCounters lowering = ir_construction_counters();
#define IR_FOLD_CENSUS_DELTA(later, earlier, counter) ((later).values[IR_CONSTRUCTION_##counter] - (earlier).values[IR_CONSTRUCTION_##counter])
        if (BUSTER_REQUIRE(arguments, form_lowered.program && !form_lowered.diagnostic_count))
        {
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_FUNCTIONS) == 2);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_SIZE_ROWS_LT_64_FUNCTIONS) == 2);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_SIZE_ROWS_LT_64_FOLDS) == 2);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_CONSTANT_ROWS) == 2);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_IDENTITY_ROWS) == 0);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_OPERAND_SLOTS) == 3);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_LITERAL_USES) == 3);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_NET_DEAD_LITERALS) == 3);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_FORM_REINTERPRET_OPERATOR) == 1);
            BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(lowering, before, FOLD_FORM_BOOL_COMPARE_RETURN) == 1);
            // Re-take the census on the published rows. A literal or folded
            // result held outside the rows (a pending SSA store) is live, and
            // the held result is attributed to a store.
            IrProgram* census_program = form_lowered.program;
            IrFunction* census_function = census_program->modules->functions;
            u32 literal = UINT32_MAX;
            u32 folded = UINT32_MAX;
            for (u32 index = 0; index < census_function->instruction_count; index += 1)
            {
                IrInstruction const* row = census_function->instructions + index;
                u32 definition = row->opcode == IR_OPCODE_CAST ? census_function->values[row->operands[0].value].definition.value : UINT32_MAX;
                if (definition < census_function->instruction_count && census_function->instructions[definition].opcode == IR_OPCODE_CONSTANT_INTEGER)
                {
                    literal = row->operands[0].value;
                    folded = row->result.value;
                }
            }
            if (BUSTER_REQUIRE(arguments, literal != UINT32_MAX))
            {
                IrSemanticCounters semantic_before = ir_semantic_counters();
                IrConstructionCounters held_before = ir_construction_counters();
                ArenaBenchmarkCounters arena_before = arena_benchmark_counters();
                IrFoldCensus census = ir_construction_fold_census_begin(census_function);
                ir_construction_fold_census_hold(&census, (IrValueId){.value = literal});
                ir_construction_fold_census_hold(&census, (IrValueId){.value = folded});
                ir_construction_fold_census_end(census_program, census_function, &census);
                ArenaBenchmarkCounters arena_after = arena_benchmark_counters();
                IrConstructionCounters held_after = ir_construction_counters();
                IrSemanticCounters semantic_after = ir_semantic_counters();
                BUSTER_TEST(arguments, memcmp(&semantic_before, &semantic_after, sizeof(semantic_before)) == 0);
                BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(held_after, held_before, FOLD_NET_SSA_USES) == 2);
                BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(held_after, held_before, FOLD_NET_CONSTANT_ROWS) == 1);
                BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(held_after, held_before, FOLD_NET_DEAD_LITERALS) == 0);
                BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(held_after, held_before, FOLD_FORM_REINTERPRET_STORE) == 1);
                BUSTER_TEST(arguments, IR_FOLD_CENSUS_DELTA(held_after, held_before, FOLD_FORM_REINTERPRET_OPERATOR) == 0);
                // Census scratch is carved outside the observed arena requests.
                BUSTER_TEST(arguments, arena_after.calls == arena_before.calls && arena_after.requested_bytes == arena_before.requested_bytes);
            }
        }
#undef IR_FOLD_CENSUS_DELTA
        scratch_end(census_temporary);
    }
    // #3010 matched association: each fold ir_fast_fold applies after full
    // preparation (inlining, promotion, FAST and their compactions) is
    // classified by the birth tag its row carried from commit. Every late fold
    // lands in exactly one of DIRECT, CASCADE and POST.
    {
        typedef struct IrFoldMatchCase IrFoldMatchCase;
        struct IrFoldMatchCase
        {
            String8 source;
            u32 passes;
            IrConstructionCounter counter;
            u64 counted;
            u64 direct;
            u64 cascade;
            u64 post;
            u64 unfoldable;
            // fold_match_clone_* of whichever class, then fold_survival_*.
            u64 clones;
            u64 reached;
            u64 skipped;
            u64 unfolded;
        };
        u32 const fold = IR_FAST_PASS_BIT(IR_FAST_FOLD);
        IrFoldMatchCase const cases[] = {
            // Direct: the zero is a committed constant when `+0` commits; the
            // tag survives promotion of `input` and both compactions.
            {S8("int test(int input){return input+0;}"), fold, IR_CONSTRUCTION_FOLD_MATCH_DIRECT_BINARY_RIGHT_IDENTITY, 1, 1, 0, 0, 0, 0, 1, 0, 0},
            // The same birth fold reaches FAST but is not folded when the
            // fold pass is not selected.
            {S8("int test(int input){return input+0;}"), IR_FAST_PASS_BIT(IR_FAST_DCE), IR_CONSTRUCTION_FOLD_MATCH_DIRECT_BINARY_RIGHT_IDENTITY, 0, 0, 0, 0, 0, 0, 1, 0, 1},
            // Cascade: the int zero is converted to unsigned by a birth
            // cast_constant, and only that fold makes `+0` an identity.
            {S8("unsigned test(unsigned input){return input+0;}"), fold, IR_CONSTRUCTION_FOLD_MATCH_CASCADE_BINARY_RIGHT_IDENTITY, 1, 1, 1, 0, 0, 0, 2, 0, 0},
            // Post (promotion): both operands are loads until promotion
            // forwards the stored constants.
            {S8("int test(void){int x=2;int y=3;return x+y;}"), fold, IR_CONSTRUCTION_FOLD_MATCH_POST_BINARY_CONSTANT, 1, 0, 0, 1, 1, 0, 0, 0, 0},
            // Post (inlining): the callee's `x+y` is not foldable at its own
            // commit; the inlined clone folds only with the caller's arguments.
            {S8("static inline __attribute__((always_inline)) int add(int x,int y){return x+y;}"
                "int test(void){return add(2,3);}"), fold, IR_CONSTRUCTION_FOLD_MATCH_POST_BINARY_CONSTANT, 1, 0, 0, 1, 1, 1, 0, 0, 0},
            // Direct clone: the callee's `x+0` was foldable at its own
            // commit; the callee and its clone both fold, and only the
            // callee's own row counts toward survival.
            {S8("static inline __attribute__((always_inline)) int f(int x){return x+0;}"
                "int test(int y){return f(y);}"), fold, IR_CONSTRUCTION_FOLD_MATCH_DIRECT_BINARY_RIGHT_IDENTITY, 2, 2, 0, 0, 0, 1, 1, 0, 0},
            // Skipped: FAST declines a computed-goto function on provenance.
            {S8("int test(int i){static void* t[]={&&a};goto *t[i];a: return i+0;}"), fold,
             IR_CONSTRUCTION_FOLD_MATCH_DIRECT_BINARY_RIGHT_IDENTITY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            IrFoldMatchCase const* test_case = cases + index;
            TemporalArena match_temporary = arena_begin_temporal(arguments->arena);
            IrConstructionCounters before = ir_construction_counters();
            CIRLowerResult match_lowered = ir_promotion_lower(arguments->arena, test_case->source, target_native);
            if (BUSTER_REQUIRE(arguments, match_lowered.program && !match_lowered.diagnostic_count))
            {
                IrProgram* match_program = match_lowered.program;
                match_program->fast_passes = test_case->passes;
                IrValidationResult prepared = ir_prepare_canonical_module(match_program, match_program->modules, true);
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                IrConstructionCounters after = ir_construction_counters();
                BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
#define IR_FOLD_MATCH_DELTA(counter) (after.values[counter] - before.values[counter])
                u64 late = 0, direct = 0, cascade = 0, post = 0;
                for (u32 rule = 0; rule <= (u32)(IR_CONSTRUCTION_FOLD_LATE_BINARY_LEFT_IDENTITY - IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY); rule += 1)
                {
                    late += IR_FOLD_MATCH_DELTA((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY + rule));
                    direct += IR_FOLD_MATCH_DELTA((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_MATCH_DIRECT_CAST_IDENTITY + rule));
                    cascade += IR_FOLD_MATCH_DELTA((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_MATCH_CASCADE_CAST_IDENTITY + rule));
                    post += IR_FOLD_MATCH_DELTA((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_MATCH_POST_CAST_IDENTITY + rule));
                }
                BUSTER_TEST(arguments, late == direct + cascade + post);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(test_case->counter) == test_case->counted);
                BUSTER_TEST(arguments, direct == test_case->direct && cascade == test_case->cascade && post == test_case->post);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_POST_UNPROBED) == 0);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_CLONE_DIRECT) +
                                           IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_CLONE_CASCADE) +
                                           IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_CLONE_POST) == test_case->clones);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_SURVIVAL_REACHED) == test_case->reached);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_SURVIVAL_SKIPPED) == test_case->skipped);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_SURVIVAL_UNFOLDED) == test_case->unfolded);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_POST_UNFOLDABLE_AT_BIRTH) == test_case->unfoldable);
                BUSTER_TEST(arguments, IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_POST_UNPROBED) +
                                           IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_POST_UNFOLDABLE_AT_BIRTH) +
                                           IR_FOLD_MATCH_DELTA(IR_CONSTRUCTION_FOLD_MATCH_POST_RULE_CHANGED) == post);
#undef IR_FOLD_MATCH_DELTA
            }
            scratch_end(match_temporary);
        }
    }
#endif
    return result;
}
