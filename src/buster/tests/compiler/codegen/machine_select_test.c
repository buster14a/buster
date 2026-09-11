#include <buster/tests/compiler/codegen/machine_select_test.h>

#if BUSTER_INCLUDE_TESTS

// Production selector regressions: normalized MIR survives linked-row
// reordering; checked and prevalidated entries agree; malformed shape checks
// remain fail-closed without building unused selection facts.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL IrProgram* machine_selection_test_compile(Arena* arena, String8 source, Target target)
{
    CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
    if (tokens.error_count)
    {
        return 0;
    }
    CParseResult parse = c_parse(arena, tokens);
    if (parse.diagnostic_count)
    {
        return 0;
    }
    CIRLowerResult lowered = c_lower_to_ir(arena, S8("machine-selection.c"), tokens, parse, target);
    IrProgram* result;
    if (lowered.diagnostic_count)
    {
        result = 0;
    }
    else
    {
        result = lowered.program;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* machine_selection_test_find(IrProgram* program, String8 name)
{
    if (program)
    {
        for (u32 module_index = 0; module_index < program->module_count; module_index += 1)
        {
            IrModule* module = program->modules + module_index;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                if (string_equal(module->functions[function_index].name, name))
                {
                    return module->functions + function_index;
                }
            }
        }
    }

    return 0;
}

typedef struct MachineSelectionTestOrderPair MachineSelectionTestOrderPair;
struct MachineSelectionTestOrderPair
{
    IrBlock* block;
    IrInstructionId previous;
    IrInstructionId first;
    IrInstructionId second;
};

BUSTER_GLOBAL_LOCAL MachineSelectionTestOrderPair machine_selection_test_find_order_pair(IrFunction* function)
{
    MachineSelectionTestOrderPair result = {0};
    result.previous = IR_INSTRUCTION_ID_INVALID;
    result.first = IR_INSTRUCTION_ID_INVALID;
    result.second = IR_INSTRUCTION_ID_INVALID;
    if (function)
    {
        for (u32 block_index = 0; block_index < function->block_count && result.second.value == IR_ID_UNDERLYING_INVALID; block_index += 1)
        {
            IrBlock* block = function->blocks + block_index;
            IrInstructionId previous = IR_INSTRUCTION_ID_INVALID;
            for (IrInstructionId first = block->first_instruction; first.value != IR_ID_UNDERLYING_INVALID && !result.block;
                 first = function->instructions[first.value].next)
            {
                IrInstructionId second = function->instructions[first.value].next;
                if (second.value == IR_ID_UNDERLYING_INVALID || second.value == block->last_instruction.value)
                {
                    previous = first;
                    continue;
                }
                IrInstruction* first_instruction = function->instructions + first.value;
                IrInstruction* second_instruction = function->instructions + second.value;
                bool first_reorderable = first_instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || first_instruction->opcode == IR_OPCODE_CONSTANT_FLOAT ||
                                         first_instruction->opcode == IR_OPCODE_CAST || first_instruction->opcode == IR_OPCODE_UNARY ||
                                         first_instruction->opcode == IR_OPCODE_BINARY;
                bool second_reorderable = second_instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || second_instruction->opcode == IR_OPCODE_CONSTANT_FLOAT ||
                                          second_instruction->opcode == IR_OPCODE_CAST || second_instruction->opcode == IR_OPCODE_UNARY ||
                                          second_instruction->opcode == IR_OPCODE_BINARY;
                bool independent = first_reorderable && second_reorderable && first_instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                   second_instruction->result.value != IR_ID_UNDERLYING_INVALID &&
                                   !first_instruction->volatile_access && !second_instruction->volatile_access;
                for (u32 operand_index = 0; independent && operand_index < first_instruction->operand_count; operand_index += 1)
                {
                    independent &= first_instruction->operands[operand_index].value != second_instruction->result.value;
                }
                for (u32 operand_index = 0; independent && operand_index < second_instruction->operand_count; operand_index += 1)
                {
                    independent &= second_instruction->operands[operand_index].value != first_instruction->result.value;
                }
                if (independent)
                {
                    result.block = block;
                    result.previous = previous;
                    result.first = first;
                    result.second = second;
                }
                previous = first;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u32 machine_selection_test_virtual_origin(MachineFunction const* function, u32 virtual_register)
{
    u32 result;
    if (!function || virtual_register >= function->virtual_register_count)
    {
        result = UINT32_MAX;
    }
    else
    {
        result = function->virtual_registers[virtual_register].typed_origin;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_ref_equal(MachineFunction const* before, MachineFunction const* after, MachineRef before_ref, MachineRef after_ref)
{
    MachineRefKind before_kind = machine_ref_kind(before_ref);
    if (before_kind != machine_ref_kind(after_ref))
    {
        return false;
    }
    if (before_kind == MACHINE_REF_VIRTUAL_REGISTER)
    {
        return machine_selection_test_virtual_origin(before, machine_ref_payload(before_ref)) ==
               machine_selection_test_virtual_origin(after, machine_ref_payload(after_ref));
    }
    if (before_kind == MACHINE_REF_IMMEDIATE)
    {
        u32 before_index = machine_ref_payload(before_ref);
        u32 after_index = machine_ref_payload(after_ref);
        return before_index < before->immediate_count && after_index < after->immediate_count && before->immediates[before_index] == after->immediates[after_index];
    }
    return before_ref == after_ref;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_instruction_equal(MachineFunction const* before, MachineFunction const* after,
                                                                   MachineInstruction const* before_instruction, MachineInstruction const* after_instruction)
{
    if (!before_instruction || !after_instruction || before_instruction->opcode != after_instruction->opcode ||
        before_instruction->payload != after_instruction->payload || before_instruction->flags != after_instruction->flags)
    {
        return false;
    }
    MachineOpcodeInfo const* info = machine_opcode_info(before_instruction->opcode);
    u32 operand_count = info ? info->operand_count : BUSTER_ARRAY_LENGTH(before_instruction->operands);
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        if (!machine_selection_test_ref_equal(before, after, before_instruction->operands[operand_index], after_instruction->operands[operand_index]))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u32 machine_selection_test_instruction_origin(MachineFunction const* function, MachineInstruction const* instruction)
{
    if (function && instruction)
    {
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        u32 operand_count = info ? info->operand_count : BUSTER_ARRAY_LENGTH(instruction->operands);
        for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
        {
            MachineOperandRole role = (MachineOperandRole)(info ? (info->operand_info[operand_index] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u))
                                                                : MACHINE_OPERAND_ROLE_NONE);
            if ((role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) &&
                machine_ref_kind(instruction->operands[operand_index]) == MACHINE_REF_VIRTUAL_REGISTER)
            {
                return machine_selection_test_virtual_origin(function, machine_ref_payload(instruction->operands[operand_index]));
            }
        }
    }

    return UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_stream_equal(Arena* arena, MachineSelectResult* before, MachineSelectResult* after)
{
    // Swapping independent IR rows may renumber virtual registers and reorder
    // their immediate side-table entries. Match every complete machine row by
    // its stable defining value, then compare opcode, flags, payload, and all
    // live operands (with virtual/immediate refs normalized).
    if (!arena || !before || !after || !before->supported || !after->supported ||
        machine_verify_function(&before->function).error != MACHINE_VERIFY_NONE || machine_verify_function(&after->function).error != MACHINE_VERIFY_NONE)
    {
        return false;
    }
    if (before->function.virtual_register_count != after->function.virtual_register_count ||
        before->function.instruction_count != after->function.instruction_count)
    {
        return false;
    }
    u8* matched = arena_allocate(arena, u8, after->function.instruction_count ? after->function.instruction_count : 1);
    for (u32 instruction_index = 0; instruction_index < after->function.instruction_count; instruction_index += 1)
    {
        matched[instruction_index] = 0;
    }
    for (u32 before_index = 0; before_index < before->function.instruction_count; before_index += 1)
    {
        MachineInstruction const* before_instruction = before->function.instructions + before_index;
        u32 before_origin = machine_selection_test_instruction_origin(&before->function, before_instruction);
        u32 after_index = 0;
        for (; after_index < after->function.instruction_count; after_index += 1)
        {
            MachineInstruction const* after_instruction = after->function.instructions + after_index;
            if (!matched[after_index] && machine_selection_test_instruction_origin(&after->function, after_instruction) == before_origin &&
                machine_selection_test_instruction_equal(&before->function, &after->function, before_instruction, after_instruction))
            {
                matched[after_index] = 1;
                break;
            }
        }
        if (after_index == after->function.instruction_count)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_order_divergence(Arena* arena, IrProgram* program, IrFunction* function, Target target)
{
    bool equivalent = false;
    if (arena && program && function && machine_selection_validate_function(arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE)
    {
        MachineSelectionTestOrderPair pair = machine_selection_test_find_order_pair(function);
        if (pair.block)
        {
            MachineSelectResult before = machine_select_canonical_function(arena, program, function, target);
            IrInstructionId saved_first = pair.block->first_instruction;
            IrInstructionId saved_previous_next = pair.previous.value == IR_ID_UNDERLYING_INVALID ? IR_INSTRUCTION_ID_INVALID : function->instructions[pair.previous.value].next;
            IrInstructionId saved_first_next = function->instructions[pair.first.value].next;
            IrInstructionId saved_second_next = function->instructions[pair.second.value].next;
            if (pair.previous.value == IR_ID_UNDERLYING_INVALID)
            {
                pair.block->first_instruction = pair.second;
            }
            else
            {
                function->instructions[pair.previous.value].next = pair.second;
            }
            function->instructions[pair.second.value].next = pair.first;
            function->instructions[pair.first.value].next = saved_second_next;

            MachineSelectResult after = machine_select_canonical_function(arena, program, function, target);
            equivalent = before.supported == after.supported && before.failed_opcode == after.failed_opcode &&
                         machine_selection_test_stream_equal(arena, &before, &after);

            pair.block->first_instruction = saved_first;
            if (pair.previous.value != IR_ID_UNDERLYING_INVALID)
            {
                function->instructions[pair.previous.value].next = saved_previous_next;
            }
            function->instructions[pair.first.value].next = saved_first_next;
            function->instructions[pair.second.value].next = saved_second_next;
        }
    }
    return equivalent;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_rejected(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                        MachineSelectionValidationError expected)
{
    MachineSelectionValidationError error = machine_selection_validate_function(arena, program, function);
    MachineSelectResult selected = machine_select_canonical_function(arena, program, function, target);
    return error == expected && !selected.supported;
}

// Coalesce repeated FUNCTION values only in the straight-line fixtures below.
// The first reference dominates every rewritten use; unused definitions remain
// present so the selector's zero-use policy is covered too.
BUSTER_GLOBAL_LOCAL void machine_selection_test_share_callees(IrFunction* function)
{
    for (u32 row = 0; row < function->instruction_count; row += 1)
    {
        IrInstruction* instruction = function->instructions + row;
        for (u32 operand = 0; operand < instruction->operand_count; operand += 1)
        {
            IrValueId value = instruction->operands[operand];
            IrInstructionId definition = function->values[value.value].definition;
            if (definition.value < function->instruction_count && function->instructions[definition.value].opcode == IR_OPCODE_FUNCTION)
            {
                for (u32 earlier = 0; earlier < definition.value; earlier += 1)
                {
                    IrInstruction* reference = function->instructions + earlier;
                    if (reference->opcode == IR_OPCODE_FUNCTION &&
                        reference->symbol.value == function->instructions[definition.value].symbol.value)
                    {
                        instruction->operands[operand] = reference->result;
                        break;
                    }
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void machine_selection_test_reverse_storage(Arena* arena, IrFunction* function)
{
    u32 count = function->instruction_count;
    IrInstruction* rows = arena_allocate(arena, IrInstruction, count);
    IrSourceRange* sources = function->instruction_canonical_sources ? arena_allocate(arena, IrSourceRange, count) : 0;
    for (u32 row = 0; row < count; row += 1)
    {
        rows[count - row - 1] = function->instructions[row];
        IrInstructionId* next = &rows[count - row - 1].next;
        if (next->value != IR_ID_UNDERLYING_INVALID) { next->value = count - next->value - 1; }
        if (sources) { sources[count - row - 1] = function->instruction_canonical_sources[row]; }
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        IrBlock* entry = function->blocks + block;
        if (entry->first_instruction.value != IR_ID_UNDERLYING_INVALID) { entry->first_instruction.value = count - entry->first_instruction.value - 1; }
        if (entry->last_instruction.value != IR_ID_UNDERLYING_INVALID) { entry->last_instruction.value = count - entry->last_instruction.value - 1; }
    }
    for (u32 value = 0; value < function->value_count; value += 1)
    {
        IrInstructionId* definition = &function->values[value].definition;
        if (definition->value != IR_ID_UNDERLYING_INVALID) { definition->value = count - definition->value - 1; }
    }
    for (u32 extra = 0; extra < function->extra_count; extra += 1)
    {
        function->extra_instructions[extra].value = count - function->extra_instructions[extra].value - 1;
    }
    function->instructions = rows;
    function->instruction_canonical_sources = sources;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_ordered_rows_equal(MachineSelectResult* before, MachineSelectResult* after)
{
    MachineFunction* left = &before->function;
    MachineFunction* right = &after->function;
    bool equal = before->supported && after->supported && before->failed_opcode == after->failed_opcode &&
                 left->instruction_count == right->instruction_count && left->virtual_register_count == right->virtual_register_count &&
                 left->immediate_count == right->immediate_count && left->call_target_count == right->call_target_count &&
                 left->stack_slot_count == right->stack_slot_count && left->block_count == right->block_count;
    for (u32 row = 0; equal && row < left->instruction_count; row += 1)
    {
        equal = machine_selection_test_instruction_equal(left, right, left->instructions + row, right->instructions + row);
    }
    for (u32 index = 0; equal && index < left->immediate_count; index += 1) { equal = left->immediates[index] == right->immediates[index]; }
    for (u32 index = 0; equal && index < left->call_target_count; index += 1) { equal = left->call_targets[index].value == right->call_targets[index].value; }
    for (u32 index = 0; equal && index < left->stack_slot_count; index += 1)
    {
        equal = left->stack_slot_sizes[index] == right->stack_slot_sizes[index] && left->stack_slot_alignments[index] == right->stack_slot_alignments[index];
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_direct_call_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("extern int fact_target(int); extern int fact_sink(int (*)(int)); extern int fact_zero(void);\n"
                       "int fact_direct(int x) { return fact_target(x) + fact_target(x + 1); }\n"
                       "int fact_escape_after(int x) { int a = fact_target(x); return a + fact_sink(fact_target); }\n"
                       "int fact_escape_before(int x) { int a = fact_sink(fact_target); return a + fact_target(x); }\n"
                       "int fact_indirect(int (*f)(int), int x) { return f(x); }\n"
                       "int fact_no_calls(void) { return 3; }\n"
                       "int fact_nullary(void) { return fact_zero(); }\n");
    String8 names[] = {S8("fact_direct"), S8("fact_escape_after"), S8("fact_escape_before"), S8("fact_indirect"), S8("fact_no_calls"), S8("fact_nullary")};
    Target target = {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX};
    IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
    BUSTER_TEST(arguments, program != 0);
    u32 direct_values = 0;
    u32 escaped_values = 0;
    u32 unused_values = 0;
    u32 repeated_values = 0;
    for (u32 fixture = 0; program && fixture < BUSTER_ARRAY_LENGTH(names); fixture += 1)
    {
        IrFunction* function = machine_selection_test_find(program, names[fixture]);
        BUSTER_TEST(arguments, function && function->block_count == 1);
        if (!function || function->block_count != 1) { continue; }
        machine_selection_test_share_callees(function);
        MachineSelectResult original = machine_select_canonical_function(arguments->arena, program, function, target);
        for (u32 reversed = 0; reversed < 2; reversed += 1)
        {
            if (reversed) { machine_selection_test_reverse_storage(arguments->arena, function); }
            BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE);
            MachineSelectResult checked = machine_select_canonical_function(arguments->arena, program, function, target);
            MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, 0);
            BUSTER_TEST(arguments, machine_selection_test_ordered_rows_equal(&original, &checked));
            BUSTER_TEST(arguments, machine_selection_test_ordered_rows_equal(&checked, &validated));
            BUSTER_TEST(arguments, checked.supported && machine_verify_function(&checked.function).error == MACHINE_VERIFY_NONE);
            if (!checked.supported) { continue; }
            for (u32 row = 0; row < function->instruction_count; row += 1)
            {
                IrInstruction* reference = function->instructions + row;
                if (reference->opcode != IR_OPCODE_FUNCTION) { continue; }
                // Independent scalar oracle: classify one value from all of
                // its uses, not by replaying the production three-state update.
                u32 uses = 0;
                u32 matching_calls = 0;
                for (u32 use_row = 0; use_row < function->instruction_count; use_row += 1)
                {
                    IrInstruction* use = function->instructions + use_row;
                    for (u32 operand = 0; operand < use->operand_count; operand += 1)
                    {
                        if (use->operands[operand].value == reference->result.value)
                        {
                            uses += 1;
                            matching_calls += use->opcode == IR_OPCODE_CALL && operand == 0 && use->symbol.value == reference->symbol.value;
                        }
                    }
                }
                bool direct = uses != 0 && uses == matching_calls;
                direct_values += direct;
                escaped_values += uses != 0 && !direct;
                unused_values += uses == 0;
                repeated_values += matching_calls > 1;
                u32 definitions = 0;
                for (u32 selected_row = 0; selected_row < checked.function.instruction_count; selected_row += 1)
                {
                    MachineInstruction* selected = checked.function.instructions + selected_row;
                    if (machine_selection_test_instruction_origin(&checked.function, selected) == reference->result.value)
                    {
                        definitions += 1;
                        BUSTER_TEST(arguments, selected->opcode == (direct ? MACHINE_A64_MOV_RI : MACHINE_A64_LEA_SYMBOL));
                        if (direct)
                        {
                            MachineRef immediate = selected->operands[1];
                            BUSTER_TEST(arguments, machine_ref_kind(immediate) == MACHINE_REF_IMMEDIATE &&
                                machine_ref_payload(immediate) < checked.function.immediate_count &&
                                checked.function.immediates[machine_ref_payload(immediate)] == 0);
                        }
                        else
                        {
                            BUSTER_TEST(arguments, selected->payload < checked.function.call_target_count &&
                                checked.function.call_targets[selected->payload].value == reference->symbol.value);
                        }
                    }
                }
                BUSTER_TEST(arguments, definitions == 1);
            }
        }
    }
    BUSTER_TEST(arguments, direct_values && escaped_values && unused_values && repeated_values);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_address_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IrField fields[] = {{.offset = 8}, {.offset = 24}, {.offset = UINT64_MAX - 10}};
    IrType types[] = {
        {.kind = IR_TYPE_STRUCT, .fields = fields, .field_count = 3, .layout = {.resolved = true, .size = 64, .alignment = 32}},
        {.kind = IR_TYPE_POINTER, .layout = {.resolved = true, .size = 8, .alignment = 8}},
        {.kind = IR_TYPE_INTEGER, .is_signed = true, .bit_width = 16, .layout = {.resolved = true, .size = 2, .alignment = 2}},
    };
    IrSymbol symbols[] = {{.is_thread_local = true, .is_definition = true}};
    IrProgram program = {.types = {.types = types, .count = BUSTER_ARRAY_LENGTH(types)},
                         .symbols = {.symbols = symbols, .count = BUSTER_ARRAY_LENGTH(symbols)}};
    IrValue values[144] = {0};
    IrInstruction instructions[144] = {0};
    IrValueId operands[144][2] = {0};
    u64 immediates[144] = {0};
    IrFunction function = {.values = values, .value_count = BUSTER_ARRAY_LENGTH(values),
                           .instructions = instructions, .instruction_count = BUSTER_ARRAY_LENGTH(instructions)};
    for (u32 index = 0; index < function.value_count; index += 1)
    {
        values[index] = (IrValue){.definition = {.value = index}, .canonical_type = {.value = 0}, .category = IR_VALUE_PLACE};
        instructions[index] = (IrInstruction){.opcode = IR_OPCODE_ARGUMENT, .result = {.value = index},
                                              .operands = operands[index], .operand_count = 1,
                                              .immediates = immediates + index, .immediate_count = 1};
    }
    instructions[0].opcode = IR_OPCODE_LOCAL;
    values[0].alignment = 32;
    instructions[1].opcode = IR_OPCODE_FIELD;
    immediates[1] = 1;
    values[1].alignment = 8;
    instructions[2].opcode = IR_OPCODE_ADDRESS_OF;
    operands[2][0].value = 1;
    instructions[3].opcode = IR_OPCODE_DEREFERENCE;
    operands[3][0].value = 2;
    instructions[4].opcode = IR_OPCODE_INDEX;
    instructions[4].operand_count = 2;
    operands[4][0].value = 3;
    operands[4][1].value = 5;
    values[5].canonical_type.value = 2;
    instructions[6].opcode = IR_OPCODE_FIELD;
    operands[6][0].value = 4;
    instructions[7] = instructions[4];
    instructions[7].result.value = 7;
    instructions[7].operands = operands[7];
    operands[7][0].value = 4;
    operands[7][1].value = 5;
    instructions[8].opcode = IR_OPCODE_GLOBAL;
    values[8].is_volatile = true;
    values[8].is_read_only = true;
    instructions[9].opcode = IR_OPCODE_ADDRESS_OF;
    operands[9][0].value = 8;
    instructions[10].opcode = IR_OPCODE_LABEL_ADDRESS;
    instructions[11].opcode = IR_OPCODE_CAST;
    operands[11][0].value = 9;
    instructions[12].opcode = IR_OPCODE_LOAD;
    operands[12][0].value = 9;
    instructions[13].opcode = IR_OPCODE_ATOMIC_LOAD;
    operands[13][0].value = 9;
    values[14].definition = IR_INSTRUCTION_ID_INVALID;
    instructions[15].opcode = IR_OPCODE_FIELD;
    immediates[15] = 3;
    instructions[16].opcode = IR_OPCODE_FIELD;
    immediates[16] = 2;
    instructions[17].opcode = IR_OPCODE_FIELD;
    operands[17][0].value = 16;
    immediates[17] = 1;
    for (u32 index = 20; index < function.value_count; index += 1)
    {
        instructions[index].opcode = IR_OPCODE_ADDRESS_OF;
        operands[index][0].value = index == 20 ? 0 : index - 1;
    }
    MachineSelectionAddressCache cache = {0};
    u64 arena_start = arguments->arena->position;
    MachineSelectionAddress address = machine_selection_address(arguments->arena, &program, &function, &cache, IR_VALUE_ID_INVALID);
    BUSTER_TEST(arguments, !cache.entries && arguments->arena->position == arena_start && address.base_value.value == IR_ID_UNDERLYING_INVALID);
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 3});
    BUSTER_TEST(arguments, address.base_value.value == 0 && address.object_value.value == 0 && address.displacement == 24 &&
                           address.expression_value.value == 3 && address.index_value.value == IR_ID_UNDERLYING_INVALID);
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 1});
    BUSTER_TEST(arguments, address.field_offset == 24 && address.alignment == 8 && (address.flags & MACHINE_SELECTION_ADDRESS_FIELD));
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 6});
    BUSTER_TEST(arguments, address.base_value.value == 0 && address.object_value.value == 0 && address.displacement == 32 &&
                           address.index_value.value == 5 && address.scale == 64 && address.index_bit_width == 16 &&
                           (address.flags & MACHINE_SELECTION_ADDRESS_INDEX_SIGNED) && address.expression_value.value == 6);
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 7});
    BUSTER_TEST(arguments, address.base_value.value == 4 && address.index_value.value == 5 && address.displacement == 0 && address.object_value.value == 0);
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 9});
    u32 symbol_flags = MACHINE_SELECTION_ADDRESS_THREAD_LOCAL | MACHINE_SELECTION_ADDRESS_SYMBOL_DEFINITION |
                       MACHINE_SELECTION_ADDRESS_VOLATILE | MACHINE_SELECTION_ADDRESS_READ_ONLY;
    BUSTER_TEST(arguments, address.symbol.value == 0 && address.base_value.value == 8 && address.object_value.value == 8 &&
                           (address.flags & symbol_flags) == symbol_flags);
    for (u32 index = 10; index <= 15; index += 1)
    {
        address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = index});
        BUSTER_TEST(arguments, address.base_value.value == index && address.expression_value.value == index &&
                               address.object_value.value == IR_ID_UNDERLYING_INVALID && address.symbol.value == IR_ID_UNDERLYING_INVALID);
    }
    address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = 17});
    BUSTER_TEST(arguments, address.base_value.value == 16 && address.displacement == 24 && address.object_value.value == 0);
    u64 allocated = arguments->arena->position;
    // Colliding value ids and chains longer than the cache/path bound must
    // remain conservative, terminate, and allocate no per-value storage.
    for (u32 repeat = 0; repeat < 3; repeat += 1)
    {
        for (u32 index = 20; index < function.value_count; index += 1)
        {
            address = machine_selection_address(arguments->arena, &program, &function, &cache, (IrValueId){.value = index});
            BUSTER_TEST(arguments, address.base_value.value < index && address.displacement == 0 && address.expression_value.value == index);
        }
    }
    BUSTER_TEST(arguments, arguments->arena->position == allocated && allocated - arena_start <= 4096 + 16);
    MachineSelectionAddressCache cold = {0};
    address = machine_selection_address(arguments->arena, &program, &function, &cold, (IrValueId){.value = 143});
    BUSTER_TEST(arguments, address.base_value.value > 0 && address.base_value.value < 143 &&
                           address.object_value.value == IR_ID_UNDERLYING_INVALID && address.expression_value.value == 143);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_address_targets(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("struct T { int x; int y; }; struct S { char pad[32]; struct T t; };\n"
                         "struct S global; extern struct S external; __thread struct S tls;\n"
                         "int address_local(void) { struct S s; int *p = &s.t.y; *p = 17; return s.t.y; }\n"
                         "int *address_global(void) { return &global.t.y; }\n"
                         "int *address_external(void) { return &external.t.y; }\n"
                         "int *address_tls(void) { return &tls.t.y; }\n"
                         "int *address_index(struct S *p, short i) { return &p[i].t.y; }\n"
                         "int address_volatile(volatile struct S *p) { p->t.y = 3; return p->t.y; }\n"
                         "int address_atomic(int **p) { int *v = __atomic_load_n(p, 0); return __atomic_add_fetch(v, 1, 5); }\n"
                         "int address_label(int n) { void *p = n ? &&a : &&b; goto *p; a: n += 2; b: return n; }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        BUSTER_TEST(arguments, program && program->module_count == 1);
        if (program && program->module_count == 1)
        {
            IrModule* module = program->modules;
            BUSTER_TEST(arguments, module->function_count == 8);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                MachineSelectResult selection = machine_select_canonical_function(arguments->arena, program, function, target);
                BUSTER_TEST(arguments, selection.supported && machine_verify_function(&selection.function).error == MACHINE_VERIFY_NONE);
                if (selection.supported && string_equal(function->name, S8("address_local")))
                {
                    u32 normalized_fields = 0;
                    for (u32 row = 0; row < selection.function.instruction_count; row += 1)
                    {
                        MachineInstruction* instruction = selection.function.instructions + row;
                        u32 frame_opcode = target.cpu_arch == CPU_ARCH_X86_64 ? MACHINE_X64_LEA_FRAME : MACHINE_A64_LEA_FRAME;
                        normalized_fields += instruction->opcode == frame_opcode && instruction->payload == 36;
                    }
                    // FIELD/FIELD/ADDRESS_OF shares the original frame base;
                    // this checks the real consumer, not just the fact cache.
                    BUSTER_TEST(arguments, normalized_fields >= 2);
                }
            }
        }
    }
    return result;
}

UnitTestResult machine_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    UnitTestResult direct_call_result = machine_selection_test_direct_call_facts(arguments);
    result.test_count += direct_call_result.test_count;
    result.succeeded_test_count += direct_call_result.succeeded_test_count;
    UnitTestResult address_result = machine_selection_test_address_facts(arguments);
    result.test_count += address_result.test_count;
    result.succeeded_test_count += address_result.succeeded_test_count;
    UnitTestResult target_address_result = machine_selection_test_address_targets(arguments);
    result.test_count += target_address_result.test_count;
    result.succeeded_test_count += target_address_result.succeeded_test_count;
    String8 source = S8("int selection_add(int a, int b) { int local = 7; return a + local + b; }\n"
                         "int selection_memory(int *p) { *p += 1; return *p; }\n"
                         "int selection_order(void) { return 1 + 2; }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        IrFunction* add = machine_selection_test_find(program, S8("selection_add"));
        IrFunction* memory = machine_selection_test_find(program, S8("selection_memory"));
        IrFunction* order = machine_selection_test_find(program, S8("selection_order"));
        BUSTER_TEST(arguments, program != 0 && add != 0 && memory != 0 && order != 0);
        if (add && memory && order)
        {
            IrFunction* functions[] = {add, memory, order};
            for (u32 function_index = 0; function_index < BUSTER_ARRAY_LENGTH(functions); function_index += 1)
            {
                IrFunction* function = functions[function_index];
                u64 before = arguments->arena->position;
                BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE);
                // One byte per row plus one per value replaces the four
                // discarded u32 fact arrays and the old visited-row array.
                BUSTER_TEST(arguments, arguments->arena->position - before == (u64)function->instruction_count + function->value_count);
                MachineSelectResult checked = machine_select_canonical_function(arguments->arena, program, function, target);
                MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, 0);
                BUSTER_TEST(arguments, checked.supported && validated.supported);
                BUSTER_TEST(arguments, checked.failed_opcode == validated.failed_opcode);
                BUSTER_TEST(arguments, machine_selection_test_stream_equal(arguments->arena, &checked, &validated));
            }
            BUSTER_TEST(arguments, machine_selection_test_order_divergence(arguments->arena, program, order, target));

            IrInstruction* operand_probe = 0;
            IrInstruction* definition_probe = 0;
            IrInstruction* second_definition = 0;
            for (u32 instruction_index = 0; instruction_index < add->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = add->instructions + instruction_index;
                if (instruction->operand_count && !operand_probe)
                {
                    operand_probe = instruction;
                }
                if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                {
                    if (!definition_probe)
                    {
                        definition_probe = instruction;
                    }
                    else if (!second_definition)
                    {
                        second_definition = instruction;
                    }
                }
            }
            BUSTER_TEST(arguments, operand_probe && definition_probe && second_definition && add->block_count);
            if (operand_probe && definition_probe && second_definition && add->block_count)
            {
                IrValueId* saved_operands = operand_probe->operands;
                operand_probe->operands = 0;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                operand_probe->operands = saved_operands;

                IrValueId saved_operand = operand_probe->operands[0];
                operand_probe->operands[0] = (IrValueId){.value = add->value_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                operand_probe->operands[0] = saved_operand;

                IrValueId saved_result = definition_probe->result;
                definition_probe->result = (IrValueId){.value = add->value_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_VALUE));
                definition_probe->result = second_definition->result;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_DUPLICATE_DEFINITION));
                definition_probe->result = saved_result;

                u8 saved_opcode = operand_probe->opcode;
                operand_probe->opcode = UINT8_MAX;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_INVALID_OPCODE));
                operand_probe->opcode = saved_opcode;

                IrBlock* block = add->blocks;
                IrInstruction* first = add->instructions + block->first_instruction.value;
                IrInstructionId saved_next = first->next;
                first->next = (IrInstructionId){.value = add->instruction_count};
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                first->next = block->first_instruction;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                first->next = saved_next;

                IrInstructionId saved_last = block->last_instruction;
                block->last_instruction = IR_INSTRUCTION_ID_INVALID;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                block->last_instruction = saved_last;

                IrInstructionId saved_first = block->first_instruction;
                block->first_instruction = first->next;
                BUSTER_TEST(arguments, machine_selection_test_rejected(arguments->arena, program, add, target, MACHINE_SELECTION_VALIDATION_OWNERSHIP));
                block->first_instruction = saved_first;

                BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, add) == MACHINE_SELECTION_VALIDATION_NONE);
            }
            BUSTER_TEST(arguments, machine_selection_validate_function(0, program, add) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
            BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, 0, add) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
            BUSTER_TEST(arguments, machine_selection_validate_function(arguments->arena, program, 0) == MACHINE_SELECTION_VALIDATION_INVALID_ARGUMENT);
        }
    }
    return result;
}

#endif
