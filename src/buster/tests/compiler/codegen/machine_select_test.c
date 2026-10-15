#include <buster/tests/compiler/codegen/machine_select_test.h>

#if BUSTER_INCLUDE_TESTS

// Production selector regressions: normalized MIR survives linked-row
// reordering; checked and prevalidated entries agree; malformed shape checks
// remain fail-closed without building unused selection facts.

#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/machine_parameter_edges_internal.h>
#include <buster/lib/compiler/codegen/machine_select_internal.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/frontend/c/c_source_internal.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL IrProgram* machine_selection_test_compile_with_options(Arena* arena, String8 source, Target target, CIRLowerOptions options)
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
    CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("machine-selection.c"), tokens, parse, target, options);
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

BUSTER_GLOBAL_LOCAL IrProgram* machine_selection_test_compile(Arena* arena, String8 source, Target target)
{
    return machine_selection_test_compile_with_options(arena, source, target, (CIRLowerOptions){0});
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
    ir_function_invalidate_cfg(function);
    bool equivalent = false;
    if (arena && program && function && machine_selection_validate_function(arena, program, function) == MACHINE_SELECTION_VALIDATION_NONE)
    {
        MachineSelectionTestOrderPair pair = machine_selection_test_find_order_pair(function);
        if (pair.block)
        {
            MachineSelectResult before = machine_select_canonical_function(arena, program, function, target);
            ir_function_invalidate_cfg(function);
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

            // Publication remaps the reordered instruction IDs. The fixture
            // retains that valid published function; stale builder IDs are no
            // longer a restoration interface.
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
    ir_function_invalidate_cfg(function);
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
    u64 preprocess_mark = c_test_preprocess_unit_mark();
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
            MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, false, 0);
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
    c_test_release_preprocess_arenas_since(preprocess_mark);
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
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        // Release the previous iteration's unit; nothing reads it any more.
        c_test_release_preprocess_arenas_since(preprocess_mark);
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
    c_test_release_preprocess_arenas_since(preprocess_mark);
    return result;
}

// Aggregate loads copy into the result slot from exactly one source: a direct
// local's own frame slot, or the address virtual register the operand helper
// reported for an address-shaped place. Cover both branches feeding that
// source payload plus the special f80 copy width, assert the reference kind
// and payload each branch actually emits, and prove a place carrying no
// address register is rejected instead of copying from an undefined source.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_aggregate_load_sources(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("struct Blob { long a; long b; long c; };\n"
                         "struct Blob blob_global;\n"
                         "struct Blob aggregate_frame(long seed)\n"
                         "{ struct Blob local; local.a = seed; local.b = seed; local.c = seed; struct Blob out = local; return out; }\n"
                         "struct Blob aggregate_pointer(struct Blob *p) { struct Blob out = *p; return out; }\n"
                         "struct Blob aggregate_global(void) { struct Blob out = blob_global; return out; }\n"
                         "long double aggregate_f80(long double *p) { long double out = *p; return out; }\n");
    struct
    {
        String8 name;
        u16 opcode;
        MachineRefKind source_kind;
        u32 copy_width;
    } expected[] = {
        {S8_INITIALIZER("aggregate_frame"), MACHINE_X64_COPY_FRAME_FROM_FRAME, MACHINE_REF_STACK_SLOT, 24},
        {S8_INITIALIZER("aggregate_pointer"), MACHINE_X64_COPY_FRAME_FROM_PTR, MACHINE_REF_VIRTUAL_REGISTER, 24},
        {S8_INITIALIZER("aggregate_global"), MACHINE_X64_COPY_FRAME_FROM_PTR, MACHINE_REF_VIRTUAL_REGISTER, 24},
        {S8_INITIALIZER("aggregate_f80"), MACHINE_X64_COPY_FRAME_FROM_PTR, MACHINE_REF_VIRTUAL_REGISTER, 10},
    };
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
    BUSTER_TEST(arguments, program != 0);
    for (u32 fixture = 0; program && fixture < BUSTER_ARRAY_LENGTH(expected); fixture += 1)
    {
        IrFunction* function = machine_selection_test_find(program, expected[fixture].name);
        BUSTER_TEST(arguments, function != 0);
        if (!function)
        {
            continue;
        }
        MachineSelectResult selection = machine_select_canonical_function(arguments->arena, program, function, target);
        BUSTER_TEST(arguments, selection.supported && machine_verify_function(&selection.function).error == MACHINE_VERIFY_NONE);
        if (!selection.supported)
        {
            continue;
        }
        u32 matching_copies = 0;
        for (u32 row = 0; row < selection.function.instruction_count; row += 1)
        {
            MachineInstruction* instruction = selection.function.instructions + row;
            if (instruction->opcode != MACHINE_X64_COPY_FRAME_FROM_FRAME && instruction->opcode != MACHINE_X64_COPY_FRAME_FROM_PTR)
            {
                continue;
            }
            MachineRef destination = instruction->operands[0];
            MachineRef aggregate_source = instruction->operands[1];
            // The copy always lands in a frame slot; only the source branch
            // is under test here.
            BUSTER_TEST(arguments, machine_ref_kind(destination) == MACHINE_REF_STACK_SLOT &&
                                       machine_ref_payload(destination) < selection.function.stack_slot_count);
            if (instruction->opcode != expected[fixture].opcode)
            {
                continue;
            }
            u32 source_payload = machine_ref_payload(aggregate_source);
            BUSTER_TEST(arguments, machine_ref_kind(aggregate_source) == expected[fixture].source_kind);
            if (expected[fixture].source_kind == MACHINE_REF_STACK_SLOT)
            {
                // A frame-backed aggregate names a real slot and never
                // consults an address register.
                BUSTER_TEST(arguments, source_payload < selection.function.stack_slot_count &&
                                           selection.function.stack_slot_sizes[source_payload] >= expected[fixture].copy_width);
            }
            else
            {
                // The address register must be a register the selector
                // actually produced for the loaded place: defined before the
                // copy reads it, and carrying that place's typed origin.
                BUSTER_TEST(arguments, source_payload < selection.function.virtual_register_count);
                if (source_payload < selection.function.virtual_register_count)
                {
                    MachineVirtualRegister* address = selection.function.virtual_registers + source_payload;
                    BUSTER_TEST(arguments, address->definition_point != MACHINE_POINT_INVALID &&
                                               address->typed_origin != IR_ID_UNDERLYING_INVALID &&
                                               address->register_class == MACHINE_REGISTER_CLASS_GENERAL);
                }
            }
            BUSTER_TEST(arguments, instruction->payload == expected[fixture].copy_width);
            matching_copies += 1;
        }
        BUSTER_TEST(arguments, matching_copies != 0);
    }

    // A place that is neither frame-backed nor address-shaped carries no
    // address register, and the aggregate branch must reject the load rather
    // than copy from an undefined source. Retarget the pointer fixture's
    // aggregate load at its own parameter value, which the selector
    // classifies as a plain register result with no place address.
    IrFunction* pointer = machine_selection_test_find(program, S8("aggregate_pointer"));
    BUSTER_TEST(arguments, pointer != 0);
    if (pointer)
    {
        IrInstruction* load = 0;
        IrInstruction* argument = 0;
        for (u32 row = 0; row < pointer->instruction_count; row += 1)
        {
            IrInstruction* instruction = pointer->instructions + row;
            if (instruction->opcode == IR_OPCODE_ARGUMENT && !argument)
            {
                argument = instruction;
            }
            if (instruction->opcode == IR_OPCODE_LOAD && instruction->operand_count && !load)
            {
                load = instruction;
            }
        }
        BUSTER_TEST(arguments, load != 0 && argument != 0);
        if (load && argument)
        {
            IrValueId saved_place = load->operands[0];
            load->operands[0] = argument->result;
            ir_function_invalidate_cfg(pointer);
            MachineSelectResult rejected = machine_select_canonical_function(arguments->arena, program, pointer, target);
            BUSTER_TEST(arguments, !rejected.supported && rejected.failed_opcode == IR_OPCODE_LOAD);
            load->operands[0] = saved_place;
            ir_function_invalidate_cfg(pointer);
            MachineSelectResult restored = machine_select_canonical_function(arguments->arena, program, pointer, target);
            BUSTER_TEST(arguments, restored.supported);
        }
    }
    c_test_release_preprocess_arenas_since(preprocess_mark);

    return result;
}

// The classification pass numbers result vregs in canonical layout order. A
// forward goto stores blocks away from reverse postorder, so a pass (or a
// compact projection of its rows) visited in storage order would number them
// differently. Independent oracle: walk the published rows in layout order
// and require each value's first vreg to grow with it.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_classification_layout_order(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("struct order_wide { long a, b, c; };\n"
                       "long order_layout(long x, struct order_wide w, long* p) {\n"
                       "    long y = x; struct order_wide copy = w; goto second;\n"
                       "first: y += copy.b + *p; goto done;\n"
                       "second: y *= w.c; if (y > 10) goto first; y -= p[1];\n"
                       "done: return y + copy.a; }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        // Release the previous iteration's unit; nothing reads it any more.
        c_test_release_preprocess_arenas_since(preprocess_mark);
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        IrFunction* function = program ? machine_selection_test_find(program, S8("order_layout")) : 0;
        BUSTER_TEST(arguments, function != 0);
        if (!function) { continue; }
        MachineSelectResult checked = machine_select_canonical_function(arguments->arena, program, function, target);
        MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, false, 0);
        BUSTER_TEST(arguments, checked.supported && machine_verify_function(&checked.function).error == MACHINE_VERIFY_NONE);
        BUSTER_TEST(arguments, machine_selection_test_ordered_rows_equal(&checked, &validated));
        if (!checked.supported) { continue; }
        u32* layout = machine_selection_canonical_layout(arguments->arena, function);
        bool reordered = false;
        for (u32 layout_index = 0; layout && layout_index < function->block_count; layout_index += 1)
        {
            reordered |= layout[layout_index] != layout_index;
        }
        BUSTER_TEST(arguments, reordered);
        u32 previous = 0;
        u32 classified = 0;
        bool ordered = true;
        for (u32 layout_index = 0; layout_index < function->block_count; layout_index += 1)
        {
            u32 block_index = layout ? layout[layout_index] : layout_index;
            IrCfgBlock const* block = function->published_cfg->blocks + block_index;
            for (u32 row = block->first_instruction; row < block->first_instruction + block->instruction_count; row += 1)
            {
                u32 value = function->instructions[row].result.value;
                u32 first = UINT32_MAX;
                for (u32 vreg = 0; value < function->value_count && first == UINT32_MAX && vreg < checked.function.virtual_register_count; vreg += 1)
                {
                    first = checked.function.virtual_registers[vreg].typed_origin == value ? vreg : UINT32_MAX;
                }
                if (first != UINT32_MAX)
                {
                    ordered &= !classified || first > previous;
                    previous = first;
                    classified += 1;
                }
            }
        }
        BUSTER_TEST(arguments, ordered && classified > 4);
    }
    c_test_release_preprocess_arenas_since(preprocess_mark);
    return result;
}

// A register-copy cast of an integer constant -- a C null pointer is
// CAST(INTEGER_TO_POINTER) of zero -- defines its own register with MOV_RI,
// so the allocator rematerializes it instead of spilling it
// (`machine_x64_cast_copies_constant`, `machine_a64_cast_copies_constant`).
// A chain of such casts resolves to the constant, and a constant or cast that
// only such casts read selects into nothing. Independent oracle from the IR:
// a constant with any other reader, edge arguments included, keeps its own row.
BUSTER_GLOBAL_LOCAL bool machine_selection_test_copy_cast(IrFunction* function, IrInstruction* row)
{
    return row->opcode == IR_OPCODE_CAST && row->operand_count == 1 && row->operands[0].value < function->value_count &&
           (row->conversion_operation == IR_CONVERSION_INTEGER_TO_POINTER || row->conversion_operation == IR_CONVERSION_POINTER_REINTERPRET ||
            row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET);
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_copies_constant(IrFunction* function, IrInstruction* row)
{
    bool found = false;
    for (u32 step = 0; !found && step < 4 && machine_selection_test_copy_cast(function, row); step += 1)
    {
        IrInstructionId definition = function->values[row->operands[0].value].definition;
        if (definition.value >= function->instruction_count)
        {
            break;
        }
        row = function->instructions + definition.value;
        found = row->opcode == IR_OPCODE_CONSTANT_INTEGER;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_constant_copy_casts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("void* keep(void* a, void* b);\n"
                        "char* copy_cast_only(void* a)\n"
                        "{\n"
                        "    keep(a, (void*)0L);\n"
                        "    keep((void*)(long)64UL, (void*)0L);\n"
                        "    return (char*)(void*)0xffffffffffffffffUL;\n"
                        "}\n"
                        "long copy_cast_shared(long x)\n"
                        "{\n"
                        "    long r = (long)5UL;\n"
                        "    keep((void*)r, (void*)x);\n"
                        "    return r + x;\n"
                        "}\n");
    String8 names[] = {S8("copy_cast_only"), S8("copy_cast_shared")};
    // AArch64 (`machine_a64_cast_copies_constant`) follows the same rule with
    // its own MOV_RI and MOV_RR rows.
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        // Release the previous iteration's unit; nothing reads it any more.
        c_test_release_preprocess_arenas_since(preprocess_mark);
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        bool x86_64 = target.cpu_arch == CPU_ARCH_X86_64;
        u16 mov_ri = x86_64 ? MACHINE_X64_MOV_RI : MACHINE_A64_MOV_RI;
        u16 mov_rr = x86_64 ? MACHINE_X64_MOV_RR : MACHINE_A64_MOV_RR;
        String8 arch_name = x86_64 ? S8("x86-64") : S8("aarch64");
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        u32 materialized_casts = 0;
        u32 dead_casts = 0;
        u32 plain_casts = 0;
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(names); name_index += 1)
        {
            IrFunction* function = program ? machine_selection_test_find(program, names[name_index]) : 0;
            BUSTER_TEST(arguments, function != 0);
            if (!function)
            {
                continue;
            }
            MachineSelectResult selected = machine_select_canonical_function(arguments->arena, program, function, target);
            BUSTER_TEST(arguments, selected.supported && machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
            if (!selected.supported)
            {
                continue;
            }
            // Readers other than a constant-copying cast, counted from the rows
            // and the published edge arguments.
            u8* other_reader = arena_allocate(arguments->arena, u8, function->value_count);
            u8* copy_reader = arena_allocate(arguments->arena, u8, function->value_count);
            memset(other_reader, 0, function->value_count);
            memset(copy_reader, 0, function->value_count);
            for (u32 row_index = 0; row_index < function->instruction_count; row_index += 1)
            {
                IrInstruction* row = function->instructions + row_index;
                bool copying = machine_selection_test_copies_constant(function, row);
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    if (row->operands[operand].value < function->value_count)
                    {
                        u8* reader = copying ? copy_reader : other_reader;
                        reader[row->operands[operand].value] = 1;
                    }
                }
            }
            for (u32 argument = 0; function->published_cfg && argument < function->published_cfg->argument_count; argument += 1)
            {
                u32 value = function->published_cfg->arguments[argument].value;
                if (value < function->value_count)
                {
                    other_reader[value] = 1;
                }
            }
            for (u32 register_index = 0; register_index < selected.function.virtual_register_count; register_index += 1)
            {
                MachineVirtualRegister* virtual_register = selected.function.virtual_registers + register_index;
                u32 origin = virtual_register->typed_origin;
                IrInstructionId definition = origin < function->value_count ? function->values[origin].definition : IR_INSTRUCTION_ID_INVALID;
                if (definition.value >= function->instruction_count)
                {
                    continue;
                }
                IrInstruction* row = function->instructions + definition.value;
                bool defined = virtual_register->definition_point != MACHINE_POINT_INVALID;
                u16 opcode = defined ? selected.function.instructions[machine_point_instruction(virtual_register->definition_point)].opcode : 0;
                if (machine_selection_test_copies_constant(function, row))
                {
                    // Dead exactly when only constant-copying casts read it;
                    // otherwise it materializes the constant itself.
                    bool dead = copy_reader[origin] && !other_reader[origin];
                    BUSTER_TEST(arguments, dead ? !defined : defined && opcode == mov_ri);
                    dead_casts += dead && !defined;
                    materialized_casts += defined && opcode == mov_ri;
                }
                else if (machine_selection_test_copy_cast(function, row))
                {
                    // A copy of a computed value stays a register copy.
                    BUSTER_TEST(arguments, defined && opcode == mov_rr);
                    plain_casts += defined && opcode == mov_rr;
                }
                else if (row->opcode == IR_OPCODE_CONSTANT_INTEGER && copy_reader[origin])
                {
                    BUSTER_TEST(arguments, defined == (other_reader[origin] != 0));
                }
            }
        }
        String8 counts = string_format(arguments->arena, S8("{S8} constant copy casts: materialized {u32}, dead {u32}, plain {u32}"), arch_name,
                                           materialized_casts, dead_casts, plain_casts);
        // Unprepared IR gives every cast here an ordinary reader as well, so none
        // is dead; the per-register checks above still bind the dead case.
        BUSTER_TEST_RAW(arguments, materialized_casts >= 4 && plain_casts >= 1, counts);

    }
    c_test_release_preprocess_arenas_since(preprocess_mark);

    return result;
}

// A non-negative constant INDEX index folds into its base address offset
// (`machine_x64_index_constant_offset`, `machine_a64_index_constant_offset`): a frame array element's address is
// one LEA_FRAME, which the allocators rematerialize, and a pointer element's
// is one LEA_OFFSET, or a copy at offset zero. A negative or variable index
// keeps the computed form, a mutable copy-then-add value. Independent oracle
// from the IR rows; a constant only folded INDEX rows read selects into
// nothing.
BUSTER_GLOBAL_LOCAL IrInstruction* machine_selection_test_definition(IrFunction* function, IrValueId value)
{
    IrInstructionId definition = value.value < function->value_count ? function->values[value.value].definition : IR_INSTRUCTION_ID_INVALID;
    return definition.value < function->instruction_count ? function->instructions + definition.value : 0;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_folded_index(IrFunction* function, IrInstruction* row)
{
    IrInstruction* index = row->opcode == IR_OPCODE_INDEX && row->operand_count >= 2 ? machine_selection_test_definition(function, row->operands[1]) : 0;
    return index && index->opcode == IR_OPCODE_CONSTANT_INTEGER && index->immediate_count == 1 && !index->immediate_is_negative &&
           index->immediates[0] < (1u << 20);
}

// CAST and INDEX can consume the same constant. An ordinary or edge reader
// keeps it live; otherwise both folded readers disappear together. The
// oracle examines canonical readers independently of the selector's scratch.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_folded_read_lifetimes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("void sink(void*);\n"
                       "void folded_only(long* p) { sink((void*)3L); sink(&p[3L]); }\n"
                       "long folded_shared(long* p, long x) { sink((void*)3L); sink(&p[3L]); return x + 3L; }\n"
                       "long folded_edge(long* p, int flag) { long n; if (flag) { sink((void*)3L); sink(&p[3L]); n = 3L; } else n = 4L; return n; }\n"
                       "void folded_limits(long* p, long i) { sink((char*)(unsigned char*)(void*)3L); sink(&p[-1L]); sink(&p[0x100000000L]); sink(&p[i]); }\n");
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
    BUSTER_TEST(arguments, program && program->module_count == 1);
    u32 mixed_dead = 0;
    u32 mixed_kept = 0;
    u32 edge_kept = 0;
    u32 limit_kept = 0;
    u32 unfolded_indices = 0;
    if (program && program->module_count == 1)
    {
        IrModule* module = program->modules;
        IrValidationResult prepared = ir_prepare_canonical_module(program, module, false);
        BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
        for (u32 function_index = 0; prepared.error == IR_VALIDATION_NONE && function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            if (function->state != IR_FUNCTION_LOWERED)
            {
                continue;
            }
            MachineSelectResult selected = machine_select_canonical_function(arguments->arena, program, function, target);
            MachineVerifyResult verified = selected.supported ? machine_verify_function(&selected.function) : (MachineVerifyResult){0};
            BUSTER_TEST_RAW(arguments, selected.supported && verified.error == MACHINE_VERIFY_NONE,
                            string_format(arguments->arena, S8("{S8} supported={u32} verify={u32} row={u32}"),
                                          function->name, (u32)selected.supported, (u32)verified.error, verified.instruction));
            if (!selected.supported)
            {
                continue;
            }
            u8* readers = arena_allocate(arguments->arena, u8, function->value_count);
            u8* defined = arena_allocate(arguments->arena, u8, function->value_count);
            memset(readers, 0, function->value_count);
            memset(defined, 0, function->value_count);
            for (u32 row_index = 0; row_index < function->instruction_count; row_index += 1)
            {
                IrInstruction* row = function->instructions + row_index;
                bool copy = machine_selection_test_copies_constant(function, row);
                bool index = machine_selection_test_folded_index(function, row);
                unfolded_indices += row->opcode == IR_OPCODE_INDEX && !index;
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    u32 value = row->operands[operand].value;
                    if (value < function->value_count)
                    {
                        readers[value] |= (u8)(copy ? 1u : index && operand == 1 ? 2u : 4u);
                    }
                }
            }
            for (u32 edge = 0; edge < function->published_cfg->argument_count; edge += 1)
            {
                readers[function->published_cfg->arguments[edge].value] |= 8u;
            }
            for (u32 reg = 0; reg < selected.function.virtual_register_count; reg += 1)
            {
                MachineVirtualRegister* value = selected.function.virtual_registers + reg;
                if (value->typed_origin < function->value_count && value->definition_point != MACHINE_POINT_INVALID)
                {
                    defined[value->typed_origin] = 1;
                }
            }
            for (u32 value = 0; value < function->value_count; value += 1)
            {
                IrInstruction* row = machine_selection_test_definition(function, (IrValueId){.value = value});
                if (row && row->opcode == IR_OPCODE_CONSTANT_INTEGER && (readers[value] & 3u))
                {
                    bool kept = (readers[value] & 12u) != 0;
                    BUSTER_TEST(arguments, defined[value] == kept);
                    mixed_dead += (readers[value] & 3u) == 3u && !kept;
                    mixed_kept += (readers[value] & 3u) == 3u && kept;
                    edge_kept += (readers[value] & 8u) != 0 && defined[value];
                }
                if (row && row->opcode == IR_OPCODE_CONSTANT_INTEGER && row->immediate_count &&
                    (row->immediate_is_negative || row->immediates[0] == 0x100000000ull))
                {
                    BUSTER_TEST(arguments, defined[value]);
                    limit_kept += defined[value];
                }
            }
        }
    }
    BUSTER_TEST_RAW(arguments, mixed_dead && mixed_kept && edge_kept && limit_kept && unfolded_indices >= 3,
                    string_format(arguments->arena, S8("dead={u32} shared={u32} edge={u32} limits={u32} unfolded={u32}"),
                                  mixed_dead, mixed_kept, edge_kept, limit_kept, unfolded_indices));
    c_test_release_preprocess_arenas_since(preprocess_mark);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_constant_index_fold(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("long f(void);\n"
                        "void g(long* p);\n"
                        "struct S { long x; long arr[4]; };\n"
                        "long frame_constant(void)\n"
                        "{\n"
                        "    long a[8];\n"
                        "    a[3L] = f();\n"
                        "    a[0L] = f();\n"
                        "    g(a);\n"
                        "    return a[3L] + a[0L];\n"
                        "}\n"
                        "long field_constant(void)\n"
                        "{\n"
                        "    struct S s;\n"
                        "    s.arr[2L] = f();\n"
                        "    g(&s.x);\n"
                        "    return s.arr[2L];\n"
                        "}\n"
                        "long pointer_constant(long* p, unsigned char* q, long i)\n"
                        "{\n"
                        "    p[2L] = f();\n"
                        "    p[0L] = f();\n"
                        "    p[-1L] = f();\n"
                        "    p[i] = f();\n"
                        "    q[5UL] = (unsigned char)f();\n"
                        "    return p[2L];\n"
                        "}\n");
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    String8 names[] = {S8("frame_constant"), S8("field_constant"), S8("pointer_constant")};
    // AArch64 (`machine_a64_index_constant_offset`) follows the same rule with
    // its own LEA_FRAME, LEA_OFFSET and MOV_RR rows.
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        Target target = {.cpu_arch = arches[arch_index], .os = OPERATING_SYSTEM_LINUX};
        c_test_release_preprocess_arenas_since(preprocess_mark);
        bool x86_64 = target.cpu_arch == CPU_ARCH_X86_64;
        u16 lea_frame = x86_64 ? MACHINE_X64_LEA_FRAME : MACHINE_A64_LEA_FRAME;
        u16 lea_offset = x86_64 ? MACHINE_X64_LEA_OFFSET : MACHINE_A64_LEA_OFFSET;
        u16 mov_rr = x86_64 ? MACHINE_X64_MOV_RR : MACHINE_A64_MOV_RR;
        String8 arch_name = x86_64 ? S8("x86-64") : S8("aarch64");
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        u32 frame_folds = 0;
        u32 pointer_folds = 0;
        u32 zero_copies = 0;
        u32 computed = 0;
        u32 dead_constants = 0;
        u32 frame_spills = 0;
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(names); name_index += 1)
        {
            IrFunction* function = program ? machine_selection_test_find(program, names[name_index]) : 0;
            BUSTER_TEST(arguments, function != 0);
            if (!function)
            {
                continue;
            }
            MachineSelectResult selected = machine_select_canonical_function(arguments->arena, program, function, target);
            BUSTER_TEST(arguments, selected.supported && machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
            if (!selected.supported)
            {
                continue;
            }
            // Readers other than a folded INDEX, counted from the rows and the
            // published edge arguments.
            u8* other_reader = arena_allocate(arguments->arena, u8, function->value_count);
            u8* fold_reader = arena_allocate(arguments->arena, u8, function->value_count);
            memset(other_reader, 0, function->value_count);
            memset(fold_reader, 0, function->value_count);
            for (u32 row_index = 0; row_index < function->instruction_count; row_index += 1)
            {
                IrInstruction* row = function->instructions + row_index;
                bool folded = machine_selection_test_folded_index(function, row);
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    if (row->operands[operand].value < function->value_count)
                    {
                        u8* reader = folded && operand == 1 ? fold_reader : other_reader;
                        reader[row->operands[operand].value] = 1;
                    }
                }
            }
            for (u32 argument = 0; function->published_cfg && argument < function->published_cfg->argument_count; argument += 1)
            {
                u32 value = function->published_cfg->arguments[argument].value;
                if (value < function->value_count)
                {
                    other_reader[value] = 1;
                }
            }
            u8* defined_origin = arena_allocate(arguments->arena, u8, function->value_count);
            memset(defined_origin, 0, function->value_count);
            u8* frame_folded = arena_allocate(arguments->arena, u8, selected.function.virtual_register_count + 1);
            memset(frame_folded, 0, selected.function.virtual_register_count + 1);
            for (u32 register_index = 0; register_index < selected.function.virtual_register_count; register_index += 1)
            {
                MachineVirtualRegister* virtual_register = selected.function.virtual_registers + register_index;
                u32 origin = virtual_register->typed_origin;
                IrInstruction* row = origin < function->value_count ? machine_selection_test_definition(function, (IrValueId){.value = origin}) : 0;
                if (!row)
                {
                    continue;
                }
                bool defined = virtual_register->definition_point != MACHINE_POINT_INVALID;
                // A definition point implies selected rows; the explicit test
                // lets the analyzer see that (#3952).
                MachineInstruction* definition = defined && selected.function.instructions ? selected.function.instructions + machine_point_instruction(virtual_register->definition_point) : 0;
                u16 opcode = definition ? definition->opcode : 0;
                if (machine_selection_test_folded_index(function, row))
                {
                    IrInstruction* base = machine_selection_test_definition(function, row->operands[0]);
                    IrInstruction* index = machine_selection_test_definition(function, row->operands[1]);
                    IrType* element = ir_type_from_id(&program->types, row->canonical_type);
                    u64 offset = element ? index->immediates[0] * element->layout.size : UINT64_MAX;
                    bool frame_base = base && (base->opcode == IR_OPCODE_LOCAL || base->opcode == IR_OPCODE_FIELD);
                    if (frame_base)
                    {
                        // The rematerialization recipe: an immutable value whose
                        // recorded definition takes a stack slot's address.
                        bool folded = opcode == lea_frame && machine_ref_kind(definition->operands[1]) == MACHINE_REF_STACK_SLOT &&
                                      !(virtual_register->flags & MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE) &&
                                      (base->opcode != IR_OPCODE_LOCAL || definition->payload == offset);
                        BUSTER_TEST(arguments, folded);
                        frame_folds += folded;
                        frame_folded[register_index] = folded;
                    }
                    else if (offset == 0)
                    {
                        BUSTER_TEST(arguments, opcode == mov_rr);
                        zero_copies += opcode == mov_rr;
                    }
                    else
                    {
                        bool folded = opcode == lea_offset && definition->payload == offset;
                        BUSTER_TEST(arguments, folded);
                        pointer_folds += folded;
                    }
                }
                else if (row->opcode == IR_OPCODE_INDEX)
                {
                    // The computed form copies the base and adds the scaled
                    // index into the same register, so the value is mutable
                    // and has no recipe.
                    bool kept = opcode == mov_rr && (virtual_register->flags & MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE);
                    BUSTER_TEST(arguments, kept);
                    computed += kept;
                }
                if (defined)
                {
                    defined_origin[origin] = 1;
                }
            }
            for (u32 value = 0; value < function->value_count; value += 1)
            {
                IrInstruction* row = machine_selection_test_definition(function, (IrValueId){.value = value});
                if (row && row->opcode == IR_OPCODE_CONSTANT_INTEGER && fold_reader[value])
                {
                    BUSTER_TEST(arguments, defined_origin[value] == other_reader[value]);
                    dead_constants += !defined_origin[value];
                }
            }
            // Every folded frame address is live across a call; the allocator
            // recreates it rather than storing it.
            MachineStackPlacement placement = machine_fast_placement_build(arguments->arena, &selected.function);
            BUSTER_TEST(arguments, placement.valid);
            for (u32 edit_index = 0; edit_index < placement.edit_count; edit_index += 1)
            {
                MachineEdit* edit = placement.edits + edit_index;
                frame_spills += edit->kind == MACHINE_EDIT_SPILL && edit->subject < selected.function.virtual_register_count && frame_folded[edit->subject];
            }
        }
        String8 counts = string_format(arguments->arena, S8("{S8} constant index folds: frame {u32}, pointer {u32}, zero copies {u32}, computed {u32}, dead constants {u32}, frame spills {u32}"),
                                       arch_name, frame_folds, pointer_folds, zero_copies, computed, dead_constants, frame_spills);
        BUSTER_TEST_RAW(arguments, frame_folds >= 3 && pointer_folds >= 2 && zero_copies >= 1 && computed >= 2 && dead_constants >= 1 && frame_spills == 0,
                        counts);
    }
    c_test_release_preprocess_arenas_since(preprocess_mark);

    return result;
}


// Compare the complete publication, not allocated addresses or only branch
// opcodes. The legacy route is scoped to this thread and restored by the seam.
BUSTER_GLOBAL_LOCAL bool machine_selection_test_layout_bytes(void const* a, void const* b, u64 bytes)
{
    bool result = !bytes || (a && b && memcmp(a, b, bytes) == 0);
    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_layout_equal(Arena* arena, MachineSelectResult* a, MachineSelectResult* b, u32 canonical_blocks)
{
    MachineFunction* x = &a->function;
    MachineFunction* y = &b->function;
    ByteSlice first = machine_replay_serialize(arena, x);
    ByteSlice second = machine_replay_serialize(arena, y);
    bool result = a->supported && b->supported && a->returns_value == b->returns_value &&
                  a->selector_certified == b->selector_certified && a->failed_opcode == b->failed_opcode &&
                  a->selected_typed_instructions == b->selected_typed_instructions &&
                  a->machine_instructions == b->machine_instructions && a->simd_operation_count == b->simd_operation_count &&
                  a->mutable_virtual_register_count == b->mutable_virtual_register_count &&
                  first.length == second.length && machine_selection_test_layout_bytes(first.pointer, second.pointer, first.length);
#define MACHINE_LAYOUT_TABLE(table, count) \
    result = result && x->count == y->count && machine_selection_test_layout_bytes(x->table, y->table, (u64)x->count * sizeof(*x->table))
    MACHINE_LAYOUT_TABLE(immediates, immediate_count);
    MACHINE_LAYOUT_TABLE(stack_slot_sizes, stack_slot_count);
    MACHINE_LAYOUT_TABLE(stack_slot_alignments, stack_slot_count);
    MACHINE_LAYOUT_TABLE(call_targets, call_target_count);
    MACHINE_LAYOUT_TABLE(call_target_references, call_target_count);
    MACHINE_LAYOUT_TABLE(switch_cases, switch_case_count);
    MACHINE_LAYOUT_TABLE(line_marks, line_mark_count);
    MACHINE_LAYOUT_TABLE(debug_values, debug_value_count);
    MACHINE_LAYOUT_TABLE(va_args, va_arg_count);
    MACHINE_LAYOUT_TABLE(inline_assembly_operands, inline_assembly_operand_count);
    MACHINE_LAYOUT_TABLE(inline_assembly_relocations, inline_assembly_relocation_count);
#undef MACHINE_LAYOUT_TABLE
    result = result && x->inline_assembly_count == y->inline_assembly_count && x->target == y->target &&
             x->outgoing_bytes == y->outgoing_bytes && x->outgoing_slot == y->outgoing_slot &&
             x->nonvolatile_memory_certified == y->nonvolatile_memory_certified &&
             x->windows_aarch64_frame == y->windows_aarch64_frame && x->windows_aarch64_variadic == y->windows_aarch64_variadic &&
             x->predicate_absence_certified == y->predicate_absence_certified &&
             x->returns_twice_absence_certified == y->returns_twice_absence_certified && x->distinct_frame_objects == y->distinct_frame_objects;
    for (u32 i = 0; result && i < x->inline_assembly_count; i += 1)
    {
        MachineInlineAssembly left = x->inline_assemblies[i];
        MachineInlineAssembly right = y->inline_assemblies[i];
        result = string_equal(left.source, right.source) && left.bytes.length == right.bytes.length &&
                 machine_selection_test_layout_bytes(left.bytes.pointer, right.bytes.pointer, left.bytes.length);
        left.source = right.source = (String8){0};
        left.bytes = right.bytes = (ByteSlice){0};
        result = result && memcmp(&left, &right, sizeof(left)) == 0;
    }
    for (u32 i = 0; result && i < x->stack_slot_count; i += 1)
    {
        result = (x->stack_slot_memory_flags ? x->stack_slot_memory_flags[i] : 0) ==
                 (y->stack_slot_memory_flags ? y->stack_slot_memory_flags[i] : 0);
    }
    for (u32 i = 0; result && i < canonical_blocks; i += 1)
    {
        result = (a->canonical_block_entries ? a->canonical_block_entries[i] : i) ==
                 (b->canonical_block_entries ? b->canonical_block_entries[i] : i);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_parameter_edge_layout(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8(
        "extern int callee(int);\n"
        "int simple(int x) { return x + 3; }\n"
        "int joins(int x, int y) { int r = y; if (x) r += x; return r; }\n"
        "int calling(int x) { return callee(x) + 1; }\n"
        "int loop(int x) { int y = 0; while (x--) { y += x ? x : y; if (y > 40) break; } return y; }\n"
        "int switches(int x) { switch(x) { case 0: case 2: return x ? 7 : 8; case 3: return 9; default: return 10; } }\n"
        "int labels(int x) { void *p = x ? &&a : &&b; goto *p; a: return 1; b: return 2; }\n"
        "__int128 wide(__int128 x, __int128 y) { return (x / y) + (x ? y : 3); }\n"
        "int assembly(int x) { asm volatile(\"\" ::: \"memory\"); return x ? 4 : 3; }\n"
        "int debugged(volatile int *p, int x) { int y = *p; return x && (y ? x + y : y); }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    u64 mark = c_test_preprocess_unit_mark();
    for (u32 arch = 0; arch < BUSTER_ARRAY_LENGTH(arches); arch += 1)
    {
        c_test_release_preprocess_arenas_since(mark);
        Target target = {.cpu_arch = arches[arch], .os = OPERATING_SYSTEM_LINUX};
        IrProgram* program = machine_selection_test_compile(arguments->arena, source, target);
        BUSTER_TEST(arguments, program != 0);
        u32 splitting_functions = 0;
        for (u32 module = 0; program && module < program->module_count; module += 1)
        {
            IrModule* unit = program->modules + module;
            BUSTER_TEST(arguments, ir_prepare_canonical_module(program, unit, false).error == IR_VALIDATION_NONE);
            for (u32 i = 0; i < unit->function_count; i += 1)
            {
                IrFunction* function = unit->functions + i;
                if (!function->block_count)
                {
                    continue;
                }
                for (u32 debug = 0; debug < 2; debug += 1)
                {
                    MachineSelectResult legacy = machine_test_select_parameter_edge_layout(arguments->arena, program, function, target, false, debug != 0);
                    MachineSelectResult direct = machine_test_select_parameter_edge_layout(arguments->arena, program, function, target, true, debug != 0);
                    BUSTER_TEST_RAW(arguments, machine_selection_test_layout_equal(arguments->arena, &legacy, &direct, function->block_count), function->name);
                    splitting_functions += direct.function.block_count > function->block_count && string_equal(function->name, S8("joins"));
                }
            }
        }
        BUSTER_TEST(arguments, splitting_functions != 0);
    }
    // Geometric publication sizes cross both one- and multiple-chunk builders.
    // Fill one flat source buffer; neither construction nor the oracle scans
    // an accumulated prefix or searches for matching instructions.
    String8 prefix = S8("int growing(int x) { int r = 0;\n");
    String8 statement = S8("if (x & 1) r += x;\n");
    String8 suffix = S8("return r; }\n");
    for (u32 count = 8; count <= 512; count *= 8)
    {
        u64 size = prefix.length + count * statement.length + suffix.length;
        char8* bytes = arena_allocate(arguments->arena, char8, size);
        memcpy(bytes, prefix.pointer, prefix.length);
        for (u32 i = 0; i < count; i += 1)
        {
            memcpy(bytes + prefix.length + i * statement.length, statement.pointer, statement.length);
        }
        memcpy(bytes + size - suffix.length, suffix.pointer, suffix.length);
        for (u32 arch = 0; arch < BUSTER_ARRAY_LENGTH(arches); arch += 1)
        {
            c_test_release_preprocess_arenas_since(mark);
            Target target = {.cpu_arch = arches[arch], .os = OPERATING_SYSTEM_LINUX};
            IrProgram* program = machine_selection_test_compile(arguments->arena, (String8){.pointer = bytes, .length = size}, target);
            if (program)
            {
                BUSTER_TEST(arguments, ir_prepare_canonical_module(program, program->modules, false).error == IR_VALIDATION_NONE);
            }
            IrFunction* function = machine_selection_test_find(program, S8("growing"));
            BUSTER_TEST(arguments, function != 0);
            if (function)
            {
                u64 start = arguments->arena->position;
                MachineSelectResult legacy = machine_test_select_parameter_edge_layout(arguments->arena, program, function, target, false, true);
                u64 legacy_bytes = arguments->arena->position - start;
                start = arguments->arena->position;
                MachineSelectResult direct = machine_test_select_parameter_edge_layout(arguments->arena, program, function, target, true, true);
                u64 direct_bytes = arguments->arena->position - start;
                BUSTER_TEST(arguments, machine_selection_test_layout_equal(arguments->arena, &legacy, &direct, function->block_count));
                BUSTER_TEST(arguments, direct.function.block_count > function->block_count);
                BUSTER_TEST(arguments, direct_bytes < legacy_bytes);
            }
        }
    }
    c_test_release_preprocess_arenas_since(mark);
    MachineFunctionBuilder builder = {0};
    MachinePoint last = machine_test_builder_definition_point(&builder, MACHINE_POINT_INSTRUCTION_LIMIT - 1);
    BUSTER_TEST(arguments, machine_point_instruction(last) == MACHINE_POINT_INSTRUCTION_LIMIT - 1 && !builder.point_capacity_exceeded);
    BUSTER_TEST(arguments, machine_test_builder_definition_point(&builder, MACHINE_POINT_INSTRUCTION_LIMIT) == MACHINE_POINT_INVALID && builder.point_capacity_exceeded);
    builder.point_capacity_exceeded = false;
    BUSTER_TEST(arguments, machine_test_builder_definition_point(&builder, UINT32_MAX) == MACHINE_POINT_INVALID && builder.point_capacity_exceeded);
    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_selection_test_debug_equal(MachineFunction const* left, MachineFunction const* right)
{
    bool equal = left->line_mark_count == right->line_mark_count && left->debug_value_count == right->debug_value_count;
    if (equal && left->line_mark_count)
    {
        equal = memory_compare(left->line_marks, right->line_marks, sizeof(MachineLineMark) * left->line_mark_count);
    }
    if (equal && left->debug_value_count)
    {
        equal = memory_compare(left->debug_values, right->debug_values, sizeof(MachineDebugValue) * left->debug_value_count);
    }
    return equal;
}

// The forced reference still gathers alias lifetime facts for every use.
// Compare complete selected rows and debug homes, including edge readers and
// late-disqualified, store-free and genuinely promotable locals.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_alias_fact_guard(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 sources[] = {
        S8("int test(int x) { return x * 3 + 1; }"),
        S8("extern int input; int test(int x) { int local=x+input, old=local; local=input+7; return old+local; }"),
        S8("void escape(int*); int test(int x) { int local=x; escape(&local); if(local<x)return local; return x; }"),
        S8("int test(int x) { int y; if(x)y=3; else y=4; return y; }"),
        S8("int test(int x) { int local; if(x)goto use; return x; use:return local; }"),
        S8("extern int input; int test(void) { int x=input,c=x<3; x=input+7; if(c)return x; return input; }")
    };
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    u64 mark = c_test_preprocess_unit_mark();
    u32 empty = 0;
    u32 qualified = 0;
    u32 edge_readers = 0;
    for (u32 source = 0; source < BUSTER_ARRAY_LENGTH(sources); source += 1)
    {
        for (u32 direct = 0; direct < 2; direct += 1)
        {
            for (u32 system = 0; system < BUSTER_ARRAY_LENGTH(systems); system += 1)
            {
                c_test_release_preprocess_arenas_since(mark);
                Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[system]};
                IrProgram* program = machine_selection_test_compile_with_options(arguments->arena, sources[source], target,
                                                                                 (CIRLowerOptions){.disable_direct_ssa = direct == 0});
                BUSTER_TEST(arguments, program && program->module_count == 1);
                if (!program || program->module_count != 1) { continue; }
                program->disable_local_promotion = direct == 0;
                program->fast_passes = 0;
                IrValidationResult prepared = ir_prepare_canonical_module(program, program->modules, false);
                IrFunction* function = machine_selection_test_find(program, S8("test"));
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE && function != 0);
                if (prepared.error != IR_VALIDATION_NONE || !function) { continue; }
                MachineSelectionModule* context = machine_select_module_prepare(arguments->arena, program, target);
                for (u32 debug = 0; debug < 2; debug += 1)
                {
                    MachineSelectResult selected = machine_test_select_work(arguments->arena, program, function, target, context, 0, debug != 0);
                    u64 updates = context->test_alias_use_updates;
                    MachineSelectResult reference = machine_test_select_work(arguments->arena, program, function, target, context,
                                                                              MACHINE_TEST_SELECT_LEGACY_ALIAS_FACTS, debug != 0);
                    BUSTER_TEST(arguments, selected.supported && reference.supported);
                    BUSTER_TEST(arguments, machine_selection_test_ordered_rows_equal(&selected, &reference));
                    BUSTER_TEST(arguments, machine_selection_test_debug_equal(&selected.function, &reference.function));
                    BUSTER_TEST(arguments, machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
                    BUSTER_TEST(arguments, updates == 0 || updates == context->test_alias_use_updates);
                    empty += updates == 0 && context->test_alias_use_updates != 0;
                    qualified += updates != 0;
                    edge_readers += function->published_cfg && function->published_cfg->argument_count != 0;
                }
            }
        }
    }
    BUSTER_TEST(arguments, empty != 0 && qualified != 0 && edge_readers != 0);
    c_test_release_preprocess_arenas_since(mark);
    return result;
}

// The reference allocates before fusion. A prospective register query must
// preserve that exact decision across scalar, frame, address and wide values;
// complete final rows, register definitions and debug homes remain identical.
BUSTER_GLOBAL_LOCAL UnitTestResult machine_selection_test_deferred_registers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8(
        "void sink(void*);\n"
        "long long branch(long long x,long long y) { if(!(x<y))return x+3; return y+7; }\n"
        "void folds(long long* p) { sink((void*)3LL); sink(&p[3LL]); }\n"
        "long long shared(long long* p,int flag) { long long n; if(flag) { sink((void*)3LL); sink(&p[3LL]); n=3; } else n=4; return n; }\n"
        "int floating(long double x) { return x>0 ? 3 : 1; }\n"
        "long long aligned(long long x) { _Alignas(32) long long y=x; sink(&y); if(y<x)return y; return x; }\n"
        "__int128 wide(__int128 x,__int128 y) { return x<y ? x/y : y; }\n"
        "typedef unsigned long long V __attribute__((vector_size(64)));\n"
        "V vector(V x,V y) { return x+y; }\n");
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS, OPERATING_SYSTEM_ANDROID};
    u64 mark = c_test_preprocess_unit_mark();
    u64 omitted = 0;
    for (u32 direct = 0; direct < 2; direct += 1)
    {
        for (u32 system = 0; system < BUSTER_ARRAY_LENGTH(systems); system += 1)
        {
            c_test_release_preprocess_arenas_since(mark);
            Target target = {.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_AMD_ZEN_5, .os = systems[system]};
            IrProgram* program = machine_selection_test_compile_with_options(arguments->arena, source, target,
                                                                             (CIRLowerOptions){.disable_direct_ssa = direct == 0});
            BUSTER_TEST(arguments, program && program->module_count == 1);
            if (!program || program->module_count != 1) { continue; }
            program->disable_local_promotion = direct == 0;
            IrValidationResult prepared = ir_prepare_canonical_module(program, program->modules, false);
            BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
            if (prepared.error != IR_VALIDATION_NONE) { continue; }
            MachineSelectionModule* context = machine_select_module_prepare(arguments->arena, program, target);
            for (u32 index = 0; index < program->modules->function_count; index += 1)
            {
                IrFunction* function = program->modules->functions + index;
                if (!function->block_count) { continue; }
                for (u32 debug = 0; debug < 2; debug += 1)
                {
                    MachineSelectResult selected = machine_test_select_work(arguments->arena, program, function, target, context, 0, debug != 0);
                    omitted += context->test_dead_registers_omitted;
                    MachineSelectResult reference = machine_test_select_work(arguments->arena, program, function, target, context,
                                                                              MACHINE_TEST_SELECT_LEGACY_REGISTERS, debug != 0);
                    BUSTER_TEST_RAW(arguments, selected.supported && reference.supported,
                                   string_format(arguments->arena, S8("{S8} system={u32} direct={u32} selected={u32}/{u32} rejected={u32}/{u32}"),
                                                 function->name, system, direct, (u32)selected.supported, (u32)reference.supported,
                                                 selected.failed_opcode, reference.failed_opcode));
                    bool equal = machine_selection_test_ordered_rows_equal(&selected, &reference);
                    BUSTER_TEST(arguments, equal);
                    BUSTER_TEST(arguments, machine_selection_test_debug_equal(&selected.function, &reference.function));
                    BUSTER_TEST(arguments, selected.supported && machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
                    BUSTER_TEST(arguments, context->test_dead_registers_omitted == 0);
                    if (equal)
                    {
                        BUSTER_TEST(arguments, memory_compare(selected.function.instructions, reference.function.instructions,
                                                             sizeof(MachineInstruction) * selected.function.instruction_count));
                        BUSTER_TEST(arguments, memory_compare(selected.function.virtual_registers, reference.function.virtual_registers,
                                                             sizeof(MachineVirtualRegister) * selected.function.virtual_register_count));
                    }
                }
            }
        }
    }
    BUSTER_TEST(arguments, omitted != 0);
    c_test_release_preprocess_arenas_since(mark);
    return result;
}

UnitTestResult machine_selection_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Each fixture returns every unit it preprocessed; the registry's own
    // arena is reserved once, before the first fixture's reading.
    c_test_unit_registry_prewarm();
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_deferred_registers);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_alias_fact_guard);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_parameter_edge_layout);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_constant_index_fold);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_constant_copy_casts);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_folded_read_lifetimes);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_direct_call_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_address_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_address_targets);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_aggregate_load_sources);
    C_TEST_BOUNDED_FIXTURE(arguments, machine_selection_test_classification_layout_order);
    String8 source = S8("int selection_add(int a, int b) { int local = 7; return a + local + b; }\n"
                         "int selection_memory(int *p) { *p += 1; return *p; }\n"
                         "int selection_order(void) { return 1 + 2; }\n");
    CpuArch arches[] = {CPU_ARCH_X86_64, CPU_ARCH_AARCH64};
    u64 preprocess_mark = c_test_preprocess_unit_mark();
    for (u32 arch_index = 0; arch_index < BUSTER_ARRAY_LENGTH(arches); arch_index += 1)
    {
        // Release the previous iteration's unit; nothing reads it any more.
        c_test_release_preprocess_arenas_since(preprocess_mark);
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
                MachineSelectResult validated = machine_select_validated_canonical_function(arguments->arena, program, function, target, false, false, 0);
                BUSTER_TEST(arguments, checked.supported && validated.supported);
                BUSTER_TEST(arguments, checked.failed_opcode == validated.failed_opcode);
                BUSTER_TEST(arguments, machine_selection_test_stream_equal(arguments->arena, &checked, &validated));
            }
            BUSTER_TEST(arguments, machine_selection_test_order_divergence(arguments->arena, program, order, target));

            ir_function_invalidate_cfg(add);
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
    c_test_release_preprocess_arenas_since(preprocess_mark);
    return result;
}

#endif
