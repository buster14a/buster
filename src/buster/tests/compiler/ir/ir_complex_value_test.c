// Complex construction stays in the existing immutable AGGREGATE vocabulary.
// These publication-boundary checks protect the row/temporary reduction and
// reject constructors that smuggle a memory place in as a captured value.

BUSTER_GLOBAL_LOCAL CIRLowerResult ir_complex_test_lower(Arena* arena, String8 source)
{
    CPreprocessResult pre = c_preprocess(arena, source, (CPreprocessOptions){
                                                          .target = target_native,
                                                          .data_layout = target_data_layout(target_native),
                                                      });
    CAnalysisResult analysis = c_parse(arena, pre);
    CIRLowerResult result = {0};
    if (!pre.error_count && !analysis.diagnostic_count)
    {
        result = c_lower_to_ir(arena, S8("canonical-complex.c"), pre, analysis, target_native);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_complex_value_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CIRLowerResult lowered = ir_complex_test_lower(temporary.arena, S8(
        "double _Complex construct(double r, double i) { return __builtin_complex(r, i); }\n"
        "double project(double r, double i) { return (double)__builtin_complex(r, i); }\n"
        "double _Complex chain(double r, double i) { return -__builtin_complex(r,i) + __builtin_complex(i,r); }\n"
        "struct Pair { double r, i; };\n"
        "struct Pair pair(double r, double i) { return (struct Pair){r, i}; }\n"));
    BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
    if (lowered.program && !lowered.diagnostic_count)
    {
        IrModule* module = lowered.program->modules;
        BUSTER_TEST(arguments, module->function_count == 4);
        BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
        for (u32 function_index = 0; function_index < 3 && function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            u32 constructors = function_index == 2 ? 4u : 1u;
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_AGGREGATE) == constructors);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_LOCAL) == 2);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_STORE) == 2);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_FIELD) == 0);
        }
        // An ordinary struct constructor isolates the verifier rules from the
        // complex frontend fold: this source constructs AGGREGATE on old IR too.
        if (module->function_count == 4)
        {
            IrFunction* function = module->functions + 3;
            IrInstructionId constructor = IR_INSTRUCTION_ID_INVALID;
            IrValueId scalar_place = IR_VALUE_ID_INVALID;
            for (u32 index = 0; index < function->instruction_count; index += 1)
            {
                IrInstruction* instruction = function->instructions + index;
                if (instruction->opcode == IR_OPCODE_AGGREGATE)
                {
                    constructor = (IrInstructionId){.value = index};
                }
                if (instruction->opcode == IR_OPCODE_LOCAL && scalar_place.value == IR_ID_UNDERLYING_INVALID)
                {
                    scalar_place = instruction->result;
                }
            }
            BUSTER_TEST(arguments, constructor.value < function->instruction_count && scalar_place.value < function->value_count);
            if (constructor.value < function->instruction_count && scalar_place.value < function->value_count)
            {
                IrInstruction* instruction = function->instructions + constructor.value;
                IrValue* value = function->values + instruction->result.value;
                u8 category = value->category;
                value->category = IR_VALUE_PLACE;
                IrValidationResult invalid_result = ir_validate_canonical_module(lowered.program, module);
                BUSTER_TEST(arguments, invalid_result.error == IR_VALIDATION_OPERATION);
                BUSTER_TEST(arguments, invalid_result.function.value == function->id.value && invalid_result.block.value == 0 &&
                                           invalid_result.instruction.value == constructor.value);
                value->category = category;
                IrValueId operand = instruction->operands[0];
                instruction->operands[0] = scalar_place;
                IrValidationResult invalid_operand = ir_validate_canonical_module(lowered.program, module);
                BUSTER_TEST(arguments, invalid_operand.error == IR_VALIDATION_OPERATION);
                BUSTER_TEST(arguments, invalid_operand.function.value == function->id.value && invalid_operand.block.value == 0 &&
                                           invalid_operand.instruction.value == constructor.value);
                instruction->operands[0] = operand;
                BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
            }
        }
    }
    // A long expression must not reintroduce one stack temporary per complex
    // constructor, or four FIELD/LOAD rows per arithmetic operand. Build the
    // source with one join rather than repeatedly copying its growing prefix.
    enum { IR_COMPLEX_TEST_TERMS = 128 };
    String8* parts = arena_allocate(temporary.arena, String8, IR_COMPLEX_TEST_TERMS * 2 + 1);
    u32 part_count = 0;
    parts[part_count++] = S8("double _Complex stress(double r, double i) { return ");
    for (u32 term = 0; term < IR_COMPLEX_TEST_TERMS; term += 1)
    {
        parts[part_count++] = S8("__builtin_complex(r,i)");
        parts[part_count++] = term + 1 == IR_COMPLEX_TEST_TERMS ? S8("; }\n") : S8(" + ");
    }
    String8 stress_source = string_join_arena(temporary.arena, (SliceString8){.pointer = parts, .length = part_count}, false);
    CIRLowerResult stress = ir_complex_test_lower(temporary.arena, stress_source);
    BUSTER_TEST(arguments, stress.program && !stress.diagnostic_count);
    if (stress.program && !stress.diagnostic_count)
    {
        IrModule* module = stress.program->modules;
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(stress.program, module).error == IR_VALIDATION_NONE);
        if (module->function_count == 1)
        {
            IrFunction* function = module->functions;
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_AGGREGATE) == 2 * IR_COMPLEX_TEST_TERMS - 1);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_LOCAL) == 2);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_STORE) == 2);
            BUSTER_TEST(arguments, ir_test_opcode_count(function, IR_OPCODE_FIELD) == 0);
            BUSTER_TEST(arguments, function->instruction_count == 6 * IR_COMPLEX_TEST_TERMS + 4);
        }
    }
    scratch_end(temporary);
    return result;
}
