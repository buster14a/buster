#include <buster/tests/compiler/ir/interpreter_test.h>
#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL AnalysisEntity* ir_interpreter_test_entity_find(AnalysisResult* analysis, String8 name)
{
    for (u32 entity_index = 0; entity_index < analysis->module.entity_count; entity_index += 1)
    {
        AnalysisEntity* entity = analysis->module.entities + entity_index;
        if (entity->kind == ANALYSIS_ENTITY_CODE && string_equal(entity->name, name))
        {
            return entity;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL bool ir_interpreter_test_static_label_dispatch(Arena* arena, AnalysisProgram* analysis, IrProgram* program, AnalysisEntityId entry)
{
    if (!arena || !analysis || !program || !program->modules)
    {
        return false;
    }
    IrExecutionTarget target = ir_interpreter_function_find(analysis, program, entry, ANALYSIS_INSTANTIATION_ID_INVALID);
    IrFunction* function = target.function;
    if (!function)
    {
        return false;
    }
    AnalysisTypeId table_type = ANALYSIS_TYPE_ID_INVALID;
    AnalysisTypeId pointer_type = ANALYSIS_TYPE_ID_INVALID;
    for (u32 type_index = 0; type_index < target.analysis->types.count; type_index += 1)
    {
        AnalysisType* candidate = target.analysis->types.types + type_index;
        if (candidate->kind != ANALYSIS_TYPE_ARRAY)
        {
            continue;
        }
        AnalysisType* element = analysis_type_from_id(target.analysis, candidate->as.array.element_type);
        if (element && element->kind == ANALYSIS_TYPE_POINTER)
        {
            table_type = candidate->id;
            pointer_type = element->id;
            break;
        }
    }
    IrTypeId canonical_table_type = IR_TYPE_ID_INVALID;
    IrTypeId canonical_pointer_type = IR_TYPE_ID_INVALID;
    IrTypeId canonical_void_type = IR_TYPE_ID_INVALID;
    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* candidate = program->types.types + type_index;
        if (candidate->kind == IR_TYPE_VOID)
        {
            canonical_void_type = candidate->id;
        }
        if (candidate->kind == IR_TYPE_POINTER && canonical_pointer_type.value == IR_ID_UNDERLYING_INVALID)
        {
            canonical_pointer_type = candidate->id;
        }
        if (candidate->kind == IR_TYPE_ARRAY && candidate->element_count == 2 && candidate->layout.size == 16 &&
            canonical_table_type.value == IR_ID_UNDERLYING_INVALID)
        {
            canonical_table_type = candidate->id;
        }
    }
    if (table_type.value == ANALYSIS_ID_UNDERLYING_INVALID || pointer_type.value == ANALYSIS_ID_UNDERLYING_INVALID ||
        canonical_table_type.value == IR_ID_UNDERLYING_INVALID || canonical_pointer_type.value == IR_ID_UNDERLYING_INVALID ||
        canonical_void_type.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    IrGlobal* global = 0;
    IrModule* module = target.module;
    if (!module || !module->globals)
    {
        if (!module)
        {
            return false;
        }
    }
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* candidate = module->globals + global_index;
        if (candidate->type.value == canonical_table_type.value)
        {
            global = candidate;
            break;
        }
    }
    u32 original_global_count = module->global_count;
    IrSymbolId table_symbol = function->symbol;
    bool added_global = false;
    if (!global)
    {
        IrSymbolId added_symbol = ir_program_add_symbol(program, (IrSymbol){
                                                                   .kind = IR_SYMBOL_DATA,
                                                                   .linkage = IR_LINKAGE_INTERNAL,
                                                                   .is_definition = true,
                                                               });
        if (added_symbol.value != IR_ID_UNDERLYING_INVALID)
        {
            table_symbol = added_symbol;
        }
        global = ir_module_add_global(arena, module, (IrGlobal){
                                                        .symbol = table_symbol,
                                                        .type = canonical_table_type,
                                                    });
        added_global = global != 0;
    }
    if (!global || global->symbol.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    IrInstructionId branch_id = IR_INSTRUCTION_ID_INVALID;
    IrBlockId branch_targets[2] = {IR_BLOCK_ID_INVALID, IR_BLOCK_ID_INVALID};
    IrBlock* branch_block = 0;
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_BRANCH_IF && instruction->target_count == 2 && instruction->targets)
        {
            branch_id = (IrInstructionId){.value = instruction_index};
            branch_targets[0] = instruction->targets[0];
            branch_targets[1] = instruction->targets[1];
            break;
        }
    }
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* candidate = function->blocks + block_index;
        for (IrInstructionId instruction_id = candidate->first_instruction; instruction_id.value != IR_ID_UNDERLYING_INVALID;
             instruction_id = function->instructions[instruction_id.value].next)
        {
            if (instruction_id.value == branch_id.value)
            {
                branch_block = candidate;
                break;
            }
        }
        if (branch_block)
        {
            break;
        }
    }
    if (branch_id.value == IR_ID_UNDERLYING_INVALID || !branch_block)
    {
        return false;
    }
    AnalysisType* saved_analysis_pointer = analysis_type_from_id(target.analysis, pointer_type);
    IrType* saved_canonical_pointer = ir_type_from_id(&program->types, canonical_pointer_type);
    IrType* canonical_table = ir_type_from_id(&program->types, canonical_table_type);
    if (!saved_analysis_pointer || !saved_canonical_pointer || !canonical_table || canonical_table->layout.size != 16)
    {
        return false;
    }
    AnalysisType saved_analysis_pointer_value = *saved_analysis_pointer;
    IrType saved_canonical_pointer_value = *saved_canonical_pointer;
    IrGlobal saved_global = *global;
    saved_analysis_pointer->as.element_type = target.analysis->types.builtin.void_type;
    saved_canonical_pointer->element_type = canonical_void_type;
    global->type = canonical_table_type;
    global->initializer_kind = IR_GLOBAL_INITIALIZER_BYTES;
    global->bytes = (ByteSlice){.pointer = arena_allocate(arena, u8, 16), .length = 16};
    memset(global->bytes.pointer, 0, global->bytes.length);
    global->relocations = arena_allocate(arena, IrGlobalRelocation, 2);
    global->relocation_count = 2;
    for (u32 relocation_index = 0; relocation_index < 2; relocation_index += 1)
    {
        global->relocations[relocation_index] = (IrGlobalRelocation){
            .symbol = function->symbol,
            .label_block = branch_targets[relocation_index],
            .offset = relocation_index * 8,
            .is_label_address = true,
        };
    }
    IrBlockId* storage_blocks = arena_allocate(arena, IrBlockId, 2);
    IrBlockId* first_path_blocks = arena_allocate(arena, IrBlockId, 1);
    IrBlockId* second_path_blocks = arena_allocate(arena, IrBlockId, 1);
    IrLabelProvenancePath* storage_paths = arena_allocate(arena, IrLabelProvenancePath, 2);
    if (!storage_blocks || !first_path_blocks || !second_path_blocks || !storage_paths)
    {
        if (added_global)
        {
            module->global_count = original_global_count;
        }
        else
        {
            *global = saved_global;
        }
        *saved_analysis_pointer = saved_analysis_pointer_value;
        *saved_canonical_pointer = saved_canonical_pointer_value;
        return false;
    }
    storage_blocks[0] = branch_targets[0];
    storage_blocks[1] = branch_targets[1];
    first_path_blocks[0] = branch_targets[0];
    second_path_blocks[0] = branch_targets[1];
    storage_paths[0] = (IrLabelProvenancePath){.label_blocks = first_path_blocks, .offset = 0, .size = 8, .label_block_count = 1};
    storage_paths[1] = (IrLabelProvenancePath){.label_blocks = second_path_blocks, .offset = 8, .size = 8, .label_block_count = 1};
    u32 original_value_count = function->value_count;
    u32 original_instruction_count = function->instruction_count;
    IrInstruction original_branch = function->instructions[branch_id.value];
    IrInstructionId original_next = original_branch.next;
    u32 original_label_metadata_count = function->label_metadata_count;
    IrValue global_value = {
        .type = table_type,
        .canonical_type = canonical_table_type,
        .category = IR_VALUE_PLACE,
    };
    IrValueId global_value_id = ir_function_add_value(arena, function, global_value);
    IrValue index_value = {
        .type = pointer_type,
        .canonical_type = canonical_pointer_type,
        .category = IR_VALUE_PLACE,
    };
    IrValueId index_value_id = ir_function_add_value(arena, function, index_value);
    IrValue load_value = {
        .type = pointer_type,
        .canonical_type = canonical_pointer_type,
        .category = IR_VALUE_VALUE,
    };
    IrValueId load_value_id = ir_function_add_value(arena, function, load_value);
    IrBlockId* index_blocks = arena_allocate(arena, IrBlockId, 1);
    IrLabelProvenancePath* index_paths = arena_allocate(arena, IrLabelProvenancePath, 1);
    if (global_value_id.value != IR_ID_UNDERLYING_INVALID && index_value_id.value != IR_ID_UNDERLYING_INVALID &&
        load_value_id.value != IR_ID_UNDERLYING_INVALID)
    {
        *ir_value_label_metadata_ensure(arena, function, global_value_id) = (IrValueLabelMetadata){
            .has_label_provenance = true,
            .label_blocks = storage_blocks,
            .label_block_count = 2,
            .label_paths = storage_paths,
            .label_path_count = 2,
        };
        *ir_value_label_metadata_ensure(arena, function, index_value_id) = (IrValueLabelMetadata){
            .has_label_provenance = true,
            .label_blocks = index_blocks,
            .label_block_count = 1,
            .label_paths = index_paths,
            .label_path_count = 1,
        };
        *ir_value_label_metadata_ensure(arena, function, load_value_id) = (IrValueLabelMetadata){
            .is_label_value = true,
            .label_blocks = index_blocks,
            .label_block_count = 1,
        };
    }
    IrValue constant_value = {
        .type = target.analysis->types.builtin.u32_type,
        .canonical_type = canonical_pointer_type,
        .category = IR_VALUE_VALUE,
    };
    IrValueId constant_value_id = ir_function_add_value(arena, function, constant_value);
    if (global_value_id.value == IR_ID_UNDERLYING_INVALID || index_value_id.value == IR_ID_UNDERLYING_INVALID || load_value_id.value == IR_ID_UNDERLYING_INVALID ||
        constant_value_id.value == IR_ID_UNDERLYING_INVALID)
    {
        function->value_count = original_value_count;
        if (added_global)
        {
            module->global_count = original_global_count;
        }
        else
        {
            *global = saved_global;
        }
        *saved_analysis_pointer = saved_analysis_pointer_value;
        *saved_canonical_pointer = saved_canonical_pointer_value;
        return false;
    }
    IrInstruction global_instruction = {
        .type = table_type,
        .canonical_type = canonical_table_type,
        .symbol = global->symbol,
        .result = global_value_id,
        .opcode = IR_OPCODE_GLOBAL,
    };
    IrInstructionId global_instruction_id = ir_function_add_instruction(arena, function, global_instruction, (IrSourceRange){0});
    IrInstruction constant_instruction = {
        .type = constant_value.type,
        .canonical_type = constant_value.canonical_type,
        .immediates = arena_allocate(arena, u64, 1),
        .immediate_count = 1,
        .result = constant_value_id,
        .opcode = IR_OPCODE_CONSTANT_INTEGER,
    };
    IrInstructionId constant_instruction_id = ir_function_add_instruction(arena, function, constant_instruction, (IrSourceRange){0});
    IrInstruction index_instruction = {
        .operands = arena_allocate(arena, IrValueId, 2),
        .type = pointer_type,
        .canonical_type = canonical_pointer_type,
        .result = index_value_id,
        .opcode = IR_OPCODE_INDEX,
        .operand_count = 2,
    };
    index_instruction.operands[0] = global_value_id;
    index_instruction.operands[1] = constant_value_id;
    IrInstructionId index_instruction_id = ir_function_add_instruction(arena, function, index_instruction, (IrSourceRange){0});
    IrInstruction load_instruction = {
        .operands = arena_allocate(arena, IrValueId, 1),
        .type = pointer_type,
        .canonical_type = canonical_pointer_type,
        .result = load_value_id,
        .opcode = IR_OPCODE_LOAD,
        .operand_count = 1,
    };
    load_instruction.operands[0] = index_value_id;
    IrInstructionId load_instruction_id = ir_function_add_instruction(arena, function, load_instruction, (IrSourceRange){0});
    bool valid = global_instruction_id.value != IR_ID_UNDERLYING_INVALID && constant_instruction_id.value != IR_ID_UNDERLYING_INVALID &&
                 index_instruction_id.value != IR_ID_UNDERLYING_INVALID && load_instruction_id.value != IR_ID_UNDERLYING_INVALID;
    IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
    for (IrInstructionId instruction_id = branch_block->first_instruction; instruction_id.value != IR_ID_UNDERLYING_INVALID;
         instruction_id = function->instructions[instruction_id.value].next)
    {
        if (instruction_id.value == branch_id.value)
        {
            break;
        }
        previous = instruction_id;
    }
    if (valid)
    {
        function->values[global_value_id.value].definition = global_instruction_id;
        function->values[constant_value_id.value].definition = constant_instruction_id;
        function->values[index_value_id.value].definition = index_instruction_id;
        function->values[load_value_id.value].definition = load_instruction_id;
        function->instructions[global_instruction_id.value].next = constant_instruction_id;
        function->instructions[constant_instruction_id.value].next = index_instruction_id;
        function->instructions[index_instruction_id.value].next = load_instruction_id;
        function->instructions[load_instruction_id.value].next = branch_id;
        if (previous.value == IR_ID_UNDERLYING_INVALID)
        {
            branch_block->first_instruction = global_instruction_id;
        }
        else
        {
            function->instructions[previous.value].next = global_instruction_id;
        }
        IrInstruction* branch = function->instructions + branch_id.value;
        branch->opcode = IR_OPCODE_INDIRECT_BRANCH;
        branch->operands = arena_allocate(arena, IrValueId, 1);
        branch->operands[0] = load_value_id;
        branch->operand_count = 1;
        branch->targets = arena_allocate(arena, IrBlockId, 1);
        branch->target_count = 1;
        branch->result = IR_VALUE_ID_INVALID;
        for (u32 selection = 0; selection < 2; selection += 1)
        {
            function->instructions[constant_instruction_id.value].immediates[0] = selection;
            IrValueLabelMetadata* index_metadata = ir_value_label_metadata_find(function, index_value_id);
            IrValueLabelMetadata* load_metadata = ir_value_label_metadata_find(function, load_value_id);
            index_metadata->label_blocks[0] = branch_targets[selection];
            index_metadata->label_paths[0] = (IrLabelProvenancePath){
                .label_blocks = index_metadata->label_blocks,
                .offset = 0,
                .size = 8,
                .label_block_count = 1,
            };
            load_metadata->label_blocks[0] = branch_targets[selection];
            branch->targets[0] = branch_targets[selection];
            IrExecutionArgument arguments[] = {
                {.bits = selection ? 8 : 3},
                {.bits = selection ? 3 : 4},
            };
            IrExecutionResult executed = ir_execute(arena, analysis, program, entry, ANALYSIS_INSTANTIATION_ID_INVALID, arguments, BUSTER_ARRAY_LENGTH(arguments),
                                                    (IrExecutionOptions){0});
            valid &= executed.trap == IR_EXECUTION_TRAP_NONE && executed.has_value && executed.bits == (selection ? 5 : 7);
        }
    }
    function->instructions[branch_id.value] = original_branch;
    function->value_count = original_value_count;
    function->instruction_count = original_instruction_count;
    function->label_metadata_count = original_label_metadata_count;
    if (previous.value == IR_ID_UNDERLYING_INVALID)
    {
        branch_block->first_instruction = branch_id;
    }
    else
    {
        function->instructions[previous.value].next = branch_id;
    }
    function->instructions[branch_id.value].next = original_next;
    if (added_global)
    {
        module->global_count = original_global_count;
    }
    else
    {
        *global = saved_global;
    }
    *saved_analysis_pointer = saved_analysis_pointer_value;
    *saved_canonical_pointer = saved_canonical_pointer_value;
    return valid;
}

UnitTestResult ir_interpreter_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    String8 scalar_source = S8("type Pair = struct\n"
                               "{\n"
                               "    a: s32,\n"
                               "    b: s32,\n"
                               "}\n"
                               "type Number = union\n"
                               "{\n"
                               "    signed_value: s32,\n"
                               "    unsigned_value: u32,\n"
                               "}\n"
                               "code choose : fn (a: s32, b: s32) s32\n"
                               "{\n"
                               "    data value: s32 = a;\n"
                               "    data dispatch_target: u8 = 0;\n"
                               "    data dispatch_table: [2]&u8 = [ &dispatch_target, &dispatch_target ];\n"
                               "    dispatch_table[0] = &dispatch_target;\n"
                               "    if (a < b)\n"
                               "    {\n"
                               "        value += b;\n"
                               "    }\n"
                               "    else\n"
                               "    {\n"
                               "        value -= b;\n"
                               "    }\n"
                               "    return value;\n"
                               "}\n"
                               "code divide : fn (value: s32) s32\n"
                               "{\n"
                               "    return 12 / value;\n"
                               "}\n"
                               "code float_value : fn () f64\n"
                               "{\n"
                               "    return 1.5 * 2.0;\n"
                               "}\n"
                               "code conversion_value : fn () s32\n"
                               "{\n"
                               "    data small: s8 = -2;\n"
                               "    data widened: s32 = @cast(small);\n"
                               "    data floating: f64 = @cast(widened);\n"
                               "    return @cast(floating);\n"
                               "}\n"
                               "code forever : fn () void\n"
                               "{\n"
                               "    loop\n"
                               "    {\n"
                               "    }\n"
                               "}\n"
                               "code memory : fn () s32\n"
                               "{\n"
                               "    data values: [3]s32 = [ 2, 3, 4 ];\n"
                               "    data pair: Pair = { .a = 5, .b = 7 };\n"
                               "    data pointer: &s32 = &values[1];\n"
                               "    data same_pointer: &s32 = &values[1];\n"
                               "    pointer.& += pair.a;\n"
                               "    data selected: []s32 = values[1..];\n"
                               "    selected[1] += 1;\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = @reverse(selected))\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    if (pointer == same_pointer)\n"
                               "    {\n"
                               "        total += 1;\n"
                               "    }\n"
                               "    return total + pair.b;\n"
                               "}\n"
                               "code range_total : fn () s32\n"
                               "{\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = 0 .. 4)\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    for (data value = @reverse(0 .. 4))\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    return total;\n"
                               "}\n"
                               "code vector_value : fn () s32\n"
                               "{\n"
                               "    data left: vector[4]s32 = [ 1, 5, -3, 8 ];\n"
                               "    data right: vector[4]s32 = [ 2, 4, -3, 9 ];\n"
                               "    data sum: vector[4]s32 = left + right;\n"
                               "    data negated: vector[4]s32 = -sum;\n"
                               "    data mask: vector[4]u32 = left < right;\n"
                               "    return negated[1] + @cast(mask[0]);\n"
                               "}\n"
                               "code array_total : fn () s32\n"
                               "{\n"
                               "    data values: [3]s32 = [ 1, 2, 3 ];\n"
                               "    data total: s32 = 0;\n"
                               "    for (data value = values)\n"
                               "    {\n"
                               "        total += value;\n"
                               "    }\n"
                               "    return total;\n"
                               "}\n"
                               "code aggregate_copy : fn () s32\n"
                               "{\n"
                               "    data first: Pair = { .a = 5, .b = 7 };\n"
                               "    data second: Pair = first;\n"
                               "    second.a += 1;\n"
                               "    return first.a * 10 + second.a;\n"
                               "}\n"
                               "code pointer_storage : fn () s32\n"
                               "{\n"
                               "    data value: s32 = 4;\n"
                               "    data pointer: &s32 = &value;\n"
                               "    data pointer_pointer: &&s32 = &pointer;\n"
                               "    pointer_pointer.&.& += 3;\n"
                               "    return value;\n"
                               "}\n"
                               "code union_value : fn () s32\n"
                               "{\n"
                               "    data number: Number = { .signed_value = 9 };\n"
                               "    return number.signed_value;\n"
                               "}\n"
                               "code string_value : fn () s32\n"
                               "{\n"
                               "    data text = \"hello\";\n"
                               "    return @cast(text[1] - 'e');\n"
                               "}\n"
                               "code indexed : fn (index: s32) s32\n"
                               "{\n"
                               "    data values: [1]s32 = [ 9 ];\n"
                               "    return values[index];\n"
                               "}\n"
                               "code variadic_first_two : fn (first: s32, ...) s32\n"
                               "{\n"
                               "    data arguments = @va_start();\n"
                               "    data copy = @va_copy(&arguments);\n"
                               "    data second: s32 = @va_arg(&copy, s32);\n"
                               "    @va_end(&copy);\n"
                               "    @va_end(&arguments);\n"
                               "    return first + second;\n"
                               "}\n"
                               "code variadic_main : fn () s32\n"
                               "{\n"
                               "    return variadic_first_two(20, 22, 99);\n"
                               "}\n"
                               "code main : fn () s32\n"
                               "{\n"
                               "    return choose(3, 4) * 2;\n"
                               "}\n"
                               "code aggregate_return : fn () Pair\n"
                               "{\n"
                               "    data pair: Pair = { .a = 11, .b = 13 };\n"
                               "    return pair;\n"
                               "}\n");
    TokenizerResult scalar_tokens = tokenize(arguments->arena, scalar_source.pointer, scalar_source.length);
    ParserResult scalar_parser = parser_parse(arguments->arena, expression_arena, scalar_source, scalar_tokens);
    BUSTER_TEST(arguments, scalar_tokens.error_count == 0);
    BUSTER_TEST(arguments, scalar_parser.diagnostic_count == 0);
    AnalysisSourceInput scalar_input = {
        .path = S8("interpreter-scalar.bbb"),
        .parser = &scalar_parser,
    };
    AnalysisResult scalar_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 700}, S8("interpreter-scalar"), &scalar_input, 1);
    analysis_resolve_module_interfaces(arguments->arena, &scalar_analysis);
    analysis_analyze_bodies(arguments->arena, &scalar_analysis);
    analysis_compute_layouts(&scalar_analysis, (AnalysisLayoutOptions){
                                                   .pointer_size = 8,
                                                   .pointer_alignment = 8,
                                               });
    AnalysisResult* scalar_modules[] = {&scalar_analysis};
    AnalysisProgram scalar_program_analysis = {
        .module_results = scalar_modules,
        .module_count = BUSTER_ARRAY_LENGTH(scalar_modules),
    };
    IrProgram scalar_program = ir_generate_program(arguments->arena, &scalar_program_analysis);
    BUSTER_TEST(arguments, scalar_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, scalar_program.rejected_function_count == 0);
    IrValidationResult scalar_validation = ir_validate_module(&scalar_analysis, scalar_program.modules);
    BUSTER_TEST(arguments, scalar_validation.error == IR_VALIDATION_NONE);
    BUSTER_TEST(arguments, ir_interpreter_test_static_label_relocations(arguments->arena));

    AnalysisEntity* main_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("main"));
    BUSTER_TEST(arguments, main_entity != 0);
    if (main_entity)
    {
        IrExecutionTarget main_target =
            ir_interpreter_function_find(&scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID);
        IrInstruction* indirect_test_call = 0;
        if (main_target.function)
        {
            for (u32 instruction_index = 0; instruction_index < main_target.function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = main_target.function->instructions + instruction_index;
                if (instruction->opcode == IR_OPCODE_CALL && instruction->operand_count)
                {
                    indirect_test_call = instruction;
                    break;
                }
            }
        }
        BUSTER_TEST(arguments, indirect_test_call != 0);
        if (indirect_test_call)
        {
            indirect_test_call->entity = ANALYSIS_ENTITY_ID_INVALID;
            indirect_test_call->instantiation = ANALYSIS_INSTANTIATION_ID_INVALID;
        }
        IrExecutionResult executed = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                                                0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, executed.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, executed.has_value);
        BUSTER_TEST(arguments, executed.bits == 14);

        IrExecutionResult depth_limited =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                       (IrExecutionOptions){
                           .max_call_depth = 1,
                       });
        BUSTER_TEST(arguments, depth_limited.trap == IR_EXECUTION_TRAP_CALL_DEPTH_LIMIT);
        if (indirect_test_call)
        {
            u32 saved_operand_count = indirect_test_call->operand_count;
            indirect_test_call->operand_count = 0;
            IrExecutionResult malformed =
                ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                           (IrExecutionOptions){0});
            BUSTER_TEST(arguments, malformed.trap == IR_EXECUTION_TRAP_INVALID_PROGRAM);
            indirect_test_call->operand_count = saved_operand_count;

            IrOpcode saved_opcode = indirect_test_call->opcode;
            IrValueId* saved_operands = indirect_test_call->operands;
            IrBlockId* saved_targets = indirect_test_call->targets;
            u32 saved_target_count = indirect_test_call->target_count;
            IrValueId saved_result = indirect_test_call->result;
            indirect_test_call->opcode = IR_OPCODE_INDIRECT_BRANCH;
            indirect_test_call->operands = 0;
            indirect_test_call->operand_count = 0;
            indirect_test_call->targets = 0;
            indirect_test_call->target_count = 1;
            indirect_test_call->result = IR_VALUE_ID_INVALID;
            IrExecutionResult malformed_indirect =
                ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                           (IrExecutionOptions){0});
            BUSTER_TEST(arguments, malformed_indirect.trap == IR_EXECUTION_TRAP_INVALID_PROGRAM);
            indirect_test_call->opcode = saved_opcode;
            indirect_test_call->operands = saved_operands;
            indirect_test_call->operand_count = saved_operand_count;
            indirect_test_call->targets = saved_targets;
            indirect_test_call->target_count = saved_target_count;
            indirect_test_call->result = saved_result;

            IrBlockId* label_targets = arena_allocate(arguments->arena, IrBlockId, 1);
            label_targets[0] = (IrBlockId){.value = 0};
            indirect_test_call->opcode = IR_OPCODE_LABEL_ADDRESS;
            indirect_test_call->operands = 0;
            indirect_test_call->operand_count = 0;
            indirect_test_call->targets = label_targets;
            indirect_test_call->target_count = 1;
            indirect_test_call->result = (IrValueId){.value = main_target.function->value_count + 1};
            IrExecutionResult malformed_label_address =
                ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, main_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                           (IrExecutionOptions){0});
            BUSTER_TEST(arguments, malformed_label_address.trap == IR_EXECUTION_TRAP_INVALID_PROGRAM);
            indirect_test_call->opcode = saved_opcode;
            indirect_test_call->operands = saved_operands;
            indirect_test_call->operand_count = saved_operand_count;
            indirect_test_call->targets = saved_targets;
            indirect_test_call->target_count = saved_target_count;
            indirect_test_call->result = saved_result;
        }
    }
    AnalysisEntity* aggregate_return_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("aggregate_return"));
    BUSTER_TEST(arguments, aggregate_return_entity != 0);
    if (aggregate_return_entity)
    {
        IrExecutionResult aggregate_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, aggregate_return_entity->id,
                                                         ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, aggregate_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, aggregate_result.value.kind == IR_EXECUTION_VALUE_AGGREGATE);
        if (aggregate_result.value.bytes.pointer && aggregate_result.value.bytes.length == 8)
        {
            s32 fields[2] = {0};
            memcpy(fields, aggregate_result.value.bytes.pointer, sizeof(fields));
            BUSTER_TEST(arguments, fields[0] == 11 && fields[1] == 13);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
    }
    AnalysisEntity* variadic_main_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("variadic_main"));
    BUSTER_TEST(arguments, variadic_main_entity != 0);
    if (variadic_main_entity)
    {
        IrExecutionResult executed = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, variadic_main_entity->id,
                                                ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, executed.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, executed.has_value);
        BUSTER_TEST(arguments, executed.bits == 42);
    }

    AnalysisEntity* choose_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("choose"));
    BUSTER_TEST(arguments, choose_entity != 0);
    if (choose_entity)
    {
        IrExecutionArgument choose_less_arguments[] = {
            {.bits = 3},
            {.bits = 4},
        };
        IrExecutionResult chose_less =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, choose_less_arguments,
                       BUSTER_ARRAY_LENGTH(choose_less_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, chose_less.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_less.bits == 7);

        IrExecutionArgument choose_greater_arguments[] = {
            {.bits = 8},
            {.bits = 3},
        };
        IrExecutionResult chose_greater =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID,
                       choose_greater_arguments, BUSTER_ARRAY_LENGTH(choose_greater_arguments), (IrExecutionOptions){0});
        BUSTER_TEST(arguments, chose_greater.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, chose_greater.bits == 5);
    }

    // Exercise the runtime representation directly: a valid LABEL_ADDRESS
    // produces the block id consumed by INDIRECT_BRANCH.  Repointing the
    // label value to each successor keeps the two dispatch cases independent
    // while using the same checked IR shape.
    if (choose_entity)
    {
        bool static_dispatch = ir_interpreter_test_static_label_dispatch(arguments->arena, &scalar_program_analysis, &scalar_program, choose_entity->id);
        BUSTER_TEST(arguments, static_dispatch);
        IrExecutionTarget choose_target =
            ir_interpreter_function_find(&scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID);
        IrFunction* choose_function = choose_target.function;
        IrInstructionId branch_id = IR_INSTRUCTION_ID_INVALID;
        IrBlockId branch_targets[2] = {IR_BLOCK_ID_INVALID, IR_BLOCK_ID_INVALID};
        IrBlock* branch_block = 0;
        for (u32 instruction_index = 0; choose_function && instruction_index < choose_function->instruction_count; instruction_index += 1)
        {
            if (choose_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF)
            {
                IrInstruction* branch = choose_function->instructions + instruction_index;
                if (branch->target_count == 2 && branch->targets)
                {
                    branch_id = (IrInstructionId){.value = instruction_index};
                    branch_targets[0] = branch->targets[0];
                    branch_targets[1] = branch->targets[1];
                    break;
                }
            }
        }
        for (u32 block_index = 0; choose_function && block_index < choose_function->block_count; block_index += 1)
        {
            IrBlock* block = choose_function->blocks + block_index;
            if (block->last_instruction.value == branch_id.value)
            {
                branch_block = block;
                break;
            }
            for (IrInstructionId instruction_id = block->first_instruction; instruction_id.value != IR_ID_UNDERLYING_INVALID;
                 instruction_id = choose_function->instructions[instruction_id.value].next)
            {
                if (instruction_id.value == branch_id.value)
                {
                    branch_block = block;
                    break;
                }
            }
            if (branch_block)
            {
                break;
            }
        }
        AnalysisTypeId analysis_void_pointer = ANALYSIS_TYPE_ID_INVALID;
        AnalysisType* saved_analysis_pointer = 0;
        AnalysisType saved_analysis_pointer_value = {0};
        for (u32 type_index = 0; type_index < scalar_analysis.types.count; type_index += 1)
        {
            AnalysisType* candidate = scalar_analysis.types.types + type_index;
            AnalysisType* element = candidate->kind == ANALYSIS_TYPE_POINTER ? analysis_type_from_id(&scalar_analysis, candidate->as.element_type) : 0;
            if (element && element->kind == ANALYSIS_TYPE_VOID)
            {
                analysis_void_pointer = (AnalysisTypeId){.value = type_index};
                break;
            }
            if (!saved_analysis_pointer && candidate->kind == ANALYSIS_TYPE_POINTER)
            {
                saved_analysis_pointer = candidate;
                saved_analysis_pointer_value = *candidate;
            }
        }
        if (analysis_void_pointer.value == IR_ID_UNDERLYING_INVALID && saved_analysis_pointer)
        {
            saved_analysis_pointer->as.element_type = scalar_analysis.types.builtin.void_type;
            analysis_void_pointer = saved_analysis_pointer->id;
        }
        IrTypeId canonical_void_pointer = IR_TYPE_ID_INVALID;
        IrType* saved_canonical_pointer = 0;
        IrType saved_canonical_pointer_value = {0};
        for (u32 type_index = 0; type_index < scalar_program.types.count; type_index += 1)
        {
            IrType* candidate = scalar_program.types.types + type_index;
            IrType* element = candidate->kind == IR_TYPE_POINTER ? ir_type_from_id(&scalar_program.types, candidate->element_type) : 0;
            if (element && element->kind == IR_TYPE_VOID)
            {
                canonical_void_pointer = (IrTypeId){.value = type_index};
                break;
            }
            if (!saved_canonical_pointer && candidate->kind == IR_TYPE_POINTER)
            {
                saved_canonical_pointer = candidate;
                saved_canonical_pointer_value = *candidate;
            }
        }
        if (canonical_void_pointer.value == IR_ID_UNDERLYING_INVALID && saved_canonical_pointer)
        {
            IrType* void_type = 0;
            for (u32 type_index = 0; type_index < scalar_program.types.count; type_index += 1)
            {
                if (scalar_program.types.types[type_index].kind == IR_TYPE_VOID)
                {
                    void_type = scalar_program.types.types + type_index;
                    break;
                }
            }
            if (void_type)
            {
                saved_canonical_pointer->element_type = void_type->id;
                canonical_void_pointer = saved_canonical_pointer->id;
            }
        }
        BUSTER_TEST(arguments, choose_function && branch_id.value != IR_ID_UNDERLYING_INVALID && branch_block &&
                                  analysis_void_pointer.value != IR_ID_UNDERLYING_INVALID && canonical_void_pointer.value != IR_ID_UNDERLYING_INVALID);
        if (choose_function && branch_id.value != IR_ID_UNDERLYING_INVALID && branch_block && analysis_void_pointer.value != IR_ID_UNDERLYING_INVALID &&
            canonical_void_pointer.value != IR_ID_UNDERLYING_INVALID)
        {
            IrInstruction* branch = choose_function->instructions + branch_id.value;
            IrInstruction original_branch = *branch;
            u32 original_instruction_count = choose_function->instruction_count;
            u32 original_value_count = choose_function->value_count;
            IrInstruction label_instruction = choose_function->instructions[branch_id.value];
            label_instruction.opcode = IR_OPCODE_LABEL_ADDRESS;
            label_instruction.type = analysis_void_pointer;
            label_instruction.canonical_type = canonical_void_pointer;
            label_instruction.operand_count = 0;
            label_instruction.operands = 0;
            label_instruction.immediate_count = 0;
            label_instruction.immediates = 0;
            label_instruction.target_count = 1;
            label_instruction.targets = arena_allocate(arguments->arena, IrBlockId, 1);
            label_instruction.result = IR_VALUE_ID_INVALID;
            label_instruction.next = branch_id;
            u32 original_choose_label_metadata_count = choose_function->label_metadata_count;
            IrValue label_value = {
                .type = analysis_void_pointer,
                .canonical_type = canonical_void_pointer,
                .definition = IR_INSTRUCTION_ID_INVALID,
                .category = IR_VALUE_VALUE,
            };
            IrValueId label_value_id = ir_function_add_value(arguments->arena, choose_function, label_value);
            *ir_value_label_metadata_ensure(arguments->arena, choose_function, label_value_id) = (IrValueLabelMetadata){
                .is_label_value = true,
                .label_block_count = 1,
                .label_blocks = label_instruction.targets,
            };
            IrInstructionId label_instruction_id = ir_function_add_instruction(arguments->arena, choose_function, label_instruction, (IrSourceRange){0});
            choose_function->values[label_value_id.value].definition = label_instruction_id;
            choose_function->instructions[label_instruction_id.value].result = label_value_id;
            IrBlock* linked_block = choose_function->blocks + branch_block->id.value;
            IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
            for (IrInstructionId instruction_id = linked_block->first_instruction; instruction_id.value != IR_ID_UNDERLYING_INVALID;
                 instruction_id = choose_function->instructions[instruction_id.value].next)
            {
                if (instruction_id.value == branch_id.value)
                {
                    break;
                }
                previous = instruction_id;
            }
            if (previous.value == IR_ID_UNDERLYING_INVALID)
            {
                linked_block->first_instruction = label_instruction_id;
            }
            else
            {
                choose_function->instructions[previous.value].next = label_instruction_id;
            }
            branch = choose_function->instructions + branch_id.value;
            branch->opcode = IR_OPCODE_INDIRECT_BRANCH;
            branch->operands = arena_allocate(arguments->arena, IrValueId, 1);
            branch->operands[0] = label_value_id;
            branch->operand_count = 1;
            branch->target_count = 1;
            branch->targets = arena_allocate(arguments->arena, IrBlockId, 1);
            branch->result = IR_VALUE_ID_INVALID;
            for (u32 target_index = 0; target_index < 2; target_index += 1)
            {
                branch->targets[0] = branch_targets[target_index];
                ir_value_label_metadata_find(choose_function, label_value_id)->label_blocks[0] = branch_targets[target_index];
                choose_function->instructions[label_instruction_id.value].targets[0] = branch_targets[target_index];
                IrExecutionArgument dispatch_arguments[] = {
                    {.bits = target_index ? 8 : 3},
                    {.bits = target_index ? 3 : 4},
                };
                IrExecutionResult dispatched = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id,
                                                          ANALYSIS_INSTANTIATION_ID_INVALID, dispatch_arguments, BUSTER_ARRAY_LENGTH(dispatch_arguments),
                                                          (IrExecutionOptions){0});
                BUSTER_TEST(arguments, dispatched.trap == IR_EXECUTION_TRAP_NONE);
                BUSTER_TEST(arguments, dispatched.has_value && dispatched.bits == (target_index ? 5 : 7));
            }
            IrValueLabelMetadata* label_metadata = ir_value_label_metadata_find(choose_function, label_value_id);
            bool saved_non_label_provenance = label_metadata->has_non_label_provenance;
            label_metadata->has_non_label_provenance = true;
            IrExecutionResult non_label_dispatch =
                ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, choose_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                           (IrExecutionOptions){0});
            BUSTER_TEST(arguments, non_label_dispatch.trap == IR_EXECUTION_TRAP_INVALID_PROGRAM);
            label_metadata->has_non_label_provenance = saved_non_label_provenance;
            choose_function->instructions[branch_id.value] = original_branch;
            choose_function->instruction_count = original_instruction_count;
            choose_function->value_count = original_value_count;
            choose_function->label_metadata_count = original_choose_label_metadata_count;
            if (previous.value == IR_ID_UNDERLYING_INVALID)
            {
                linked_block->first_instruction = branch_id;
            }
            else
            {
                choose_function->instructions[previous.value].next = branch_id;
            }
        }
        if (saved_analysis_pointer)
        {
            *saved_analysis_pointer = saved_analysis_pointer_value;
        }
        if (saved_canonical_pointer)
        {
            *saved_canonical_pointer = saved_canonical_pointer_value;
        }
    }

    AnalysisEntity* divide_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("divide"));
    BUSTER_TEST(arguments, divide_entity != 0);
    if (divide_entity)
    {
        IrExecutionArgument zero = {0};
        IrExecutionResult divided_by_zero = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, divide_entity->id,
                                                       ANALYSIS_INSTANTIATION_ID_INVALID, &zero, 1, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, divided_by_zero.trap == IR_EXECUTION_TRAP_DIVISION_BY_ZERO);
    }

    AnalysisEntity* float_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("float_value"));
    BUSTER_TEST(arguments, float_entity != 0);
    if (float_entity)
    {
        IrExecutionResult float_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, float_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, float_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, ir_interpreter_float_read(float_result.bits, 64) == 3.0);
    }

    AnalysisEntity* conversion_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("conversion_value"));
    BUSTER_TEST(arguments, conversion_entity != 0);
    if (conversion_entity)
    {
        IrExecutionResult conversion_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, conversion_entity->id,
                                                         ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, conversion_result.trap == IR_EXECUTION_TRAP_NONE);
        if (conversion_result.bits != (u64)UINT32_MAX - 1)
        {
            BUSTER_TEST_ERROR(S8("conversion_value returned {u64}, expected {u64}"), conversion_result.bits, (u64)UINT32_MAX - 1);
        }
        BUSTER_TEST(arguments, conversion_result.bits == (u64)UINT32_MAX - 1);
    }

    AnalysisEntity* forever_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("forever"));
    BUSTER_TEST(arguments, forever_entity != 0);
    if (forever_entity)
    {
        IrExecutionResult step_limited =
            ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, forever_entity->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0,
                       (IrExecutionOptions){
                           .max_steps = 32,
                       });
        BUSTER_TEST(arguments, step_limited.trap == IR_EXECUTION_TRAP_STEP_LIMIT);
        BUSTER_TEST(arguments, step_limited.step_count == 32);
    }

    AnalysisEntity* memory_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("memory"));
    BUSTER_TEST(arguments, memory_entity != 0);
    if (memory_entity)
    {
        IrExecutionResult memory_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, memory_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, memory_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, memory_result.bits == 21);
    }

    AnalysisEntity* range_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("range_total"));
    BUSTER_TEST(arguments, range_entity != 0);
    if (range_entity)
    {
        IrExecutionResult range_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, range_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, range_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, range_result.bits == 12);
    }

    AnalysisEntity* array_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("array_total"));
    BUSTER_TEST(arguments, array_entity != 0);
    if (array_entity)
    {
        IrExecutionResult array_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, array_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, array_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, array_result.bits == 6);
    }

    AnalysisEntity* vector_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("vector_value"));
    BUSTER_TEST(arguments, vector_entity != 0);
    if (vector_entity)
    {
        IrExecutionResult vector_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, vector_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, vector_result.trap == IR_EXECUTION_TRAP_NONE);
        if (vector_result.bits != (u64)(u32)-10)
        {
            BUSTER_TEST_ERROR(S8("vector_value returned {u64}, expected {u64}"), vector_result.bits, (u64)(u32)-10);
        }
        BUSTER_TEST(arguments, vector_result.bits == (u64)(u32)-10);
    }

    AnalysisEntity* aggregate_copy_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("aggregate_copy"));
    BUSTER_TEST(arguments, aggregate_copy_entity != 0);
    if (aggregate_copy_entity)
    {
        IrExecutionResult aggregate_copy_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, aggregate_copy_entity->id,
                                                             ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, aggregate_copy_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, aggregate_copy_result.bits == 56);
    }

    AnalysisEntity* pointer_storage_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("pointer_storage"));
    BUSTER_TEST(arguments, pointer_storage_entity != 0);
    if (pointer_storage_entity)
    {
        IrExecutionResult pointer_storage_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, pointer_storage_entity->id,
                                                              ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, pointer_storage_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, pointer_storage_result.bits == 7);
    }

    AnalysisEntity* union_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("union_value"));
    BUSTER_TEST(arguments, union_entity != 0);
    if (union_entity)
    {
        IrExecutionResult union_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, union_entity->id,
                                                    ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, union_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, union_result.bits == 9);
    }

    AnalysisEntity* string_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("string_value"));
    BUSTER_TEST(arguments, string_entity != 0);
    if (string_entity)
    {
        IrExecutionResult string_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, string_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, string_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, string_result.bits == 0);
    }

    AnalysisEntity* indexed_entity = ir_interpreter_test_entity_find(&scalar_analysis, S8("indexed"));
    BUSTER_TEST(arguments, indexed_entity != 0);
    if (indexed_entity)
    {
        IrExecutionArgument outside = {.bits = 2};
        IrExecutionResult bounds_result = ir_execute(expression_arena, &scalar_program_analysis, &scalar_program, indexed_entity->id,
                                                     ANALYSIS_INSTANTIATION_ID_INVALID, &outside, 1, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, bounds_result.trap == IR_EXECUTION_TRAP_OUT_OF_BOUNDS);
    }

    String8 math_source = S8("code identity : fn (value: $T) $T\n"
                             "{\n"
                             "    return value;\n"
                             "}\n");
    String8 app_source = S8("import math = \"core/math\";\n"
                            "code main : fn () s32\n"
                            "{\n"
                            "    return math.identity(40) + 2;\n"
                            "}\n");
    TokenizerResult math_tokens = tokenize(arguments->arena, math_source.pointer, math_source.length);
    ParserResult math_parser = parser_parse(arguments->arena, expression_arena, math_source, math_tokens);
    TokenizerResult app_tokens = tokenize(arguments->arena, app_source.pointer, app_source.length);
    ParserResult app_parser = parser_parse(arguments->arena, expression_arena, app_source, app_tokens);
    BUSTER_TEST(arguments, math_tokens.error_count == 0);
    BUSTER_TEST(arguments, math_parser.diagnostic_count == 0);
    BUSTER_TEST(arguments, app_tokens.error_count == 0);
    BUSTER_TEST(arguments, app_parser.diagnostic_count == 0);
    AnalysisSourceInput math_input = {
        .path = S8("math.bbb"),
        .parser = &math_parser,
    };
    AnalysisSourceInput app_input = {
        .path = S8("app.bbb"),
        .parser = &app_parser,
    };
    AnalysisResult math_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 710}, S8("core/math"), &math_input, 1);
    AnalysisResult app_analysis = analysis_index_module(arguments->arena, (AnalysisModuleId){.value = 711}, S8("app"), &app_input, 1);
    AnalysisResult* cross_modules[] = {
        &app_analysis,
        &math_analysis,
    };
    analysis_resolve_program_interfaces(arguments->arena, cross_modules, BUSTER_ARRAY_LENGTH(cross_modules));
    analysis_analyze_bodies(arguments->arena, &app_analysis);
    analysis_analyze_bodies(arguments->arena, &math_analysis);
    AnalysisProgram cross_analysis = {
        .module_results = cross_modules,
        .module_count = BUSTER_ARRAY_LENGTH(cross_modules),
    };
    IrProgram cross_program = ir_generate_program(arguments->arena, &cross_analysis);
    BUSTER_TEST(arguments, app_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_analysis.instantiation_count == 1);
    AnalysisEntity* cross_main = ir_interpreter_test_entity_find(&app_analysis, S8("main"));
    BUSTER_TEST(arguments, cross_main != 0);
    if (cross_main)
    {
        IrExecutionResult cross_result =
            ir_execute(expression_arena, &cross_analysis, &cross_program, cross_main->id, ANALYSIS_INSTANTIATION_ID_INVALID, 0, 0, (IrExecutionOptions){0});
        BUSTER_TEST(arguments, cross_result.trap == IR_EXECUTION_TRAP_NONE);
        BUSTER_TEST(arguments, cross_result.has_value);
        BUSTER_TEST(arguments, cross_result.bits == 42);
    }

    BUSTER_CHECK(arena_destroy(expression_arena, 1));
    arena_set_position(temporary.arena, temporary.position);
    return result;
}
#endif
