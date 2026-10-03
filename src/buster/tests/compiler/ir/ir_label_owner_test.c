// Included by ir_test.c. Global label relocations must resolve the first
// function with their symbol without rebuilding or retaining an owner table.
// ir_test_label_owner_index covers the public module validator and preparation
// boundary, malformed relocations, duplicate owners and measured growing work.
typedef struct IrLabelOwnerFixture IrLabelOwnerFixture;
struct IrLabelOwnerFixture
{
    IrProgram program;
    IrSymbolId absent_owner;
};

BUSTER_GLOBAL_LOCAL IrLabelOwnerFixture ir_label_owner_fixture(Arena* arena, u32 function_count, bool repeated)
{
    IrLabelOwnerFixture fixture = {.program = ir_program_initialize(arena, 1, 4, function_count + 3, 0)};
    IrProgram* program = &fixture.program;
    program->data_layout.pointer.size = 8;
    IrTypeId void_type = ir_program_add_type(program, (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}});
    IrTypeId signature = ir_program_add_type(program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = void_type,
        .calling_convention = IR_CALLING_CONVENTION_C, .layout = {.size = 8, .alignment = 8, .resolved = true}});
    IrTypeId pointer = ir_program_add_type(program, (IrType){.kind = IR_TYPE_POINTER, .element_type = void_type,
        .layout = {.size = 8, .alignment = 8, .resolved = true}});
    IrTypeId table = ir_program_add_type(program, (IrType){.kind = IR_TYPE_ARRAY, .element_type = pointer, .element_count = function_count,
        .layout = {.size = 8 * (u64)function_count, .alignment = 8, .resolved = true}});
    for (u32 index = 0; index < function_count; index += 1)
    {
        IrSymbolId symbol = ir_program_add_symbol(program, (IrSymbol){.type = signature, .kind = IR_SYMBOL_FUNCTION,
            .linkage = IR_LINKAGE_INTERNAL, .is_definition = true});
        IrFunction* function = ir_module_add_function(arena, program->modules, (IrFunction){.symbol = symbol, .canonical_type = signature,
            .entry = {.value = 0}, .state = IR_FUNCTION_LOWERED});
        ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
            .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
        IrInstruction row = {.canonical_type = void_type, .symbol = IR_SYMBOL_ID_INVALID, .canonical_local = IR_LOCAL_ID_INVALID,
            .next = IR_INSTRUCTION_ID_INVALID, .result = IR_VALUE_ID_INVALID, .opcode = IR_OPCODE_RETURN,
            .conversion_operation = IR_CONVERSION_COUNT, .unary_operation = IR_UNARY_COUNT, .binary_operation = IR_BINARY_COUNT,
            .memory_order = IR_MEMORY_ORDER_COUNT, .failure_memory_order = IR_MEMORY_ORDER_COUNT, .atomic_operation = IR_ATOMIC_OPERATION_COUNT};
        ir_block_append_instruction(arena, function, (IrBlockId){.value = 0}, row, (IrSourceRange){0}, 0);
    }
    fixture.absent_owner = ir_program_add_symbol(program, (IrSymbol){.type = signature, .kind = IR_SYMBOL_FUNCTION,
        .linkage = IR_LINKAGE_INTERNAL, .is_definition = true});
    for (u32 global_index = 0; global_index < 2; global_index += 1)
    {
        IrSymbolId symbol = ir_program_add_symbol(program, (IrSymbol){.type = table, .kind = IR_SYMBOL_DATA,
            .linkage = IR_LINKAGE_INTERNAL, .is_definition = true});
        u64 size = 8 * (u64)function_count;
        u8* bytes = arena_allocate(arena, u8, size);
        memset(bytes, 0, size);
        IrGlobalRelocation* relocations = arena_allocate(arena, IrGlobalRelocation, function_count);
        for (u32 index = 0; index < function_count; index += 1)
        {
            u32 owner = repeated ? function_count - 1 : (index * 37 + global_index) % function_count;
            relocations[index] = (IrGlobalRelocation){.symbol = program->modules->functions[owner].symbol,
                .label_block = {.value = 0}, .offset = 8 * (u64)index, .is_label_address = true};
        }
        ir_module_add_global(arena, program->modules, (IrGlobal){.symbol = symbol, .type = table,
            .bytes = {.pointer = bytes, .length = size}, .relocations = relocations, .relocation_count = function_count,
            .initializer_kind = IR_GLOBAL_INITIALIZER_BYTES});
    }
    return fixture;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_test_label_owner_index(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    ThreadContext* context = thread_context_selected();
    if (BUSTER_REQUIRE(arguments, context != 0))
    {
        for (u32 repeated = 0; repeated < 2; repeated += 1)
        {
#if BUSTER_BENCH_ALLOCATIONS
            u64 previous_work = 0;
#endif
            for (u32 count = 64; count <= 1024; count *= 4)
            {
                IrLabelOwnerFixture fixture = ir_label_owner_fixture(arena, count, repeated != 0);
                IrModule* module = fixture.program.modules;
                IrModule module_before;
                memcpy(&module_before, module, sizeof(module_before));
                IrFunction* functions_before = arena_allocate(arena, IrFunction, count);
                memcpy(functions_before, module->functions, sizeof(*functions_before) * count);
                IrGlobal globals_before[2];
                memcpy(globals_before, module->globals, sizeof(globals_before));
                IrGlobalRelocation* relocations_before = arena_allocate(arena, IrGlobalRelocation, 2 * count);
                for (u32 global_index = 0; global_index < 2; global_index += 1)
                {
                    memcpy(relocations_before + global_index * count, module->globals[global_index].relocations, sizeof(*relocations_before) * count);
                }
                u64 retained_position = arena->position;
                u64 scratch_positions[SCRATCH_ARENA_COUNT];
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    scratch_positions[index] = context->arenas[index]->position;
                }
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters before = ir_construction_counters();
#endif
                IrValidationResult validation = ir_validate_canonical_module(&fixture.program, module);
                BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters after = ir_construction_counters();
                u64 rows = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_ROWS] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_ROWS];
                u64 probes = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_PROBES] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_PROBES];
                u64 bytes = after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] - before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES];
                BUSTER_TEST(arguments, !before.overflowed && !after.overflowed);
                BUSTER_TEST(arguments, rows == count && probes >= 3 * (u64)count && probes <= 24 * (u64)count);
                BUSTER_TEST(arguments, bytes >= sizeof(u32) * (u64)count && bytes <= 16 * (u64)count);
                u64 work = rows + probes;
                BUSTER_TEST(arguments, !previous_work || work <= 5 * previous_work);
                previous_work = work;
#endif
                bool rewound = arena->position == retained_position;
                for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
                {
                    rewound &= context->arenas[index]->position == scratch_positions[index];
                    TemporalArena poison = arena_begin_temporal(context->arenas[index]);
                    u8* bytes = arena_allocate(poison.arena, u8, 16 * (u64)count);
                    memset(bytes, 0xa5, 16 * (u64)count);
                    scratch_end(poison);
                }
                BUSTER_TEST(arguments, rewound);
                BUSTER_TEST(arguments, !memcmp(&module_before, module, sizeof(*module)) &&
                    !memcmp(functions_before, module->functions, sizeof(*functions_before) * count) &&
                    !memcmp(globals_before, module->globals, sizeof(globals_before)));
                for (u32 global_index = 0; global_index < 2; global_index += 1)
                {
                    BUSTER_TEST(arguments, !memcmp(relocations_before + global_index * count, module->globals[global_index].relocations,
                        sizeof(*relocations_before) * count));
                }
                BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_NONE);

                // Ordinary relocations need no label-owner rows or allocation,
                // even when the module owns a large function collection.
                for (u32 global_index = 0; global_index < 2; global_index += 1)
                {
                    for (u32 index = 0; index < count; index += 1)
                    {
                        module->globals[global_index].relocations[index].is_label_address = false;
                    }
                }
#if BUSTER_BENCH_ALLOCATIONS
                before = ir_construction_counters();
#endif
                BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_NONE);
#if BUSTER_BENCH_ALLOCATIONS
                after = ir_construction_counters();
                BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_ROWS] == before.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_ROWS] &&
                    after.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_PROBES] == before.values[IR_CONSTRUCTION_VALIDATION_LABEL_OWNER_PROBES] &&
                    after.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES] == before.values[IR_CONSTRUCTION_VALIDATION_LABEL_SCRATCH_BYTES]);
#endif
            }
        }

        IrLabelOwnerFixture fixture = ir_label_owner_fixture(arena, 2, false);
        IrModule* module = fixture.program.modules;
        IrGlobal* global = module->globals;
        IrGlobalRelocation saved_relocation = global->relocations[1];
        IrFunction saved_owner = module->functions[1];
        IrSymbol* symbol = ir_symbol_from_id(&fixture.program.symbols, saved_owner.symbol);
        IrSymbol saved_symbol = *symbol;
        // The first row is valid, so every refusal also exercises cleanup of
        // an already-built owner index instead of only the cold refusal path.
        for (u32 variant = 0; variant < 12; variant += 1)
        {
            global->relocations[1] = saved_relocation;
            module->functions[1] = saved_owner;
            *symbol = saved_symbol;
            switch (variant)
            {
            case 0: global->relocations[1].symbol = IR_SYMBOL_ID_INVALID; break;
            case 1: global->relocations[1].symbol.value = fixture.program.symbols.count; break;
            case 2: global->relocations[1].symbol = global->symbol; break;
            case 3: symbol->is_definition = false; break;
            case 4: global->relocations[1].symbol = fixture.absent_owner; break;
            case 5: module->functions[1].state = IR_FUNCTION_NOT_LOWERED; break;
            case 6: module->functions[1].state = IR_FUNCTION_REJECTED; break;
            case 7: module->functions[1].state = IR_FUNCTION_DECLARATION; break;
            case 8: global->relocations[1].label_block.value = saved_owner.block_count; break;
            case 9: global->relocations[1].label_block = IR_BLOCK_ID_INVALID; break;
            case 10: global->relocations[1].addend = 1; break;
            case 11: module->functions[1].symbol = IR_SYMBOL_ID_INVALID; break;
            }
            u64 scratch_positions[SCRATCH_ARENA_COUNT];
            for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
            {
                scratch_positions[index] = context->arenas[index]->position;
            }
            IrValidationResult validation = ir_validate_canonical_module(&fixture.program, module);
            BUSTER_TEST(arguments, validation.error == IR_VALIDATION_OPERATION && validation.function.value == IR_ID_UNDERLYING_INVALID &&
                validation.block.value == IR_ID_UNDERLYING_INVALID && validation.instruction.value == IR_ID_UNDERLYING_INVALID);
            bool rewound = true;
            for (u32 index = 0; index < (u32)SCRATCH_ARENA_COUNT; index += 1)
            {
                rewound &= context->arenas[index]->position == scratch_positions[index];
            }
            BUSTER_TEST(arguments, rewound);
            // Uncertified preparation reaches the same public input boundary.
            validation = ir_prepare_canonical_module(&fixture.program, module, false);
            BUSTER_TEST(arguments, validation.error == IR_VALIDATION_OPERATION && validation.boundary == IR_VALIDATION_BOUNDARY_CANONICAL_INPUT);
        }
        global->relocations[1] = saved_relocation;
        module->functions[1] = saved_owner;
        *symbol = saved_symbol;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_NONE);

        // All relocations name a shared symbol. The later lowered owner cannot
        // rescue the first declaration, but a later declaration cannot shadow
        // the first lowered owner either.
        module->functions[1].symbol = module->functions[0].symbol;
        for (u32 global_index = 0; global_index < 2; global_index += 1)
        {
            for (u32 index = 0; index < 2; index += 1)
            {
                module->globals[global_index].relocations[index].symbol = module->functions[0].symbol;
            }
        }
        IrFunctionState first_states[] = {IR_FUNCTION_NOT_LOWERED, IR_FUNCTION_REJECTED, IR_FUNCTION_DECLARATION};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(first_states); index += 1)
        {
            module->functions[0].state = first_states[index];
            BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_OPERATION);
        }
        module->functions[0].state = IR_FUNCTION_LOWERED;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_NONE);
        module->functions[1].state = IR_FUNCTION_DECLARATION;
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, module).error == IR_VALIDATION_NONE);
        fixture.program.disable_local_promotion = true;
        BUSTER_TEST(arguments, ir_prepare_canonical_module(&fixture.program, module, false).error == IR_VALIDATION_NONE);
    }
    return result;
}
