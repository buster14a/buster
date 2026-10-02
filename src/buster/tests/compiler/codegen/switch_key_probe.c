#include <stdio.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/file.h>

// Diagnostic only. C supplies a valid function/CFG; the two documented
// canonical edits bypass promotion and select one raw key. No production
// compiler source changes. Every consumer receives a freshly lowered module.
BUSTER_GLOBAL_LOCAL UnitTestResult switch_key_probe(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    char const* names[4][2] = {{"unsigned char", "signed char"}, {"unsigned short", "short"},
                             {"unsigned int", "int"}, {"unsigned long long", "long long"}};
    u32 widths[] = {8, 16, 32, 64};
    for (u32 width_index = 0; width_index < 4; width_index += 1)
    {
        u32 width = widths[width_index];
        for (u32 signed_index = 0; signed_index < 2; signed_index += 1)
        {
            for (u32 variant = 0; variant < 5; variant += 1)
            {
                u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                u64 key = variant == 1 && width < 64 ? (UINT64_C(1) << width) + 7 :
                          variant == 2 ? mask : variant == 3 ? UINT64_MAX : variant == 4 ? 8 : 7;
                for (u32 consumer = 0; consumer < 5; consumer += 1)
                {
                    TemporalArena temporary = scratch_begin(0, 0);
                    char source_buffer[256];
                    int source_length = snprintf(source_buffer, sizeof(source_buffer),
                        "int choose(%s x) { switch (x) { case 7: return 11; default: return 22; } }", names[width_index][signed_index]);
                    String8 source = {.pointer = source_buffer, .length = (u64)source_length};
                    Target target = target_native;
                    CPreprocessResult preprocessed = c_preprocess(temporary.arena, source, (CPreprocessOptions){0});
                    CParseResult parsed = c_parse(temporary.arena, preprocessed);
                    CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("switch-key-probe.c"), preprocessed, parsed, target);
                    bool shape = source_length > 0 && preprocessed.error_count == 0 && parsed.diagnostic_count == 0 &&
                                 lowered.diagnostic_count == 0 && lowered.program && lowered.program->module_count == 1;
                    BUSTER_TEST(arguments, shape);
                    if (shape)
                    {
                        IrProgram* program = lowered.program;
                        IrModule* module = program->modules;
                        IrFunction* function = codegen_test_c_function_find(module, S8("choose"));
                        IrValueId argument_value = IR_VALUE_ID_INVALID;
                        IrInstruction* switched = 0;
                        u32 switch_count = 0;
                        u32 argument_count = 0;
                        for (u32 row = 0; function && row < function->instruction_count; row += 1)
                        {
                            IrInstruction* instruction = function->instructions + row;
                            if (instruction->opcode == IR_OPCODE_ARGUMENT && instruction->immediate_count == 1 && instruction->immediates[0] == 0)
                            {
                                argument_value = instruction->result;
                                argument_count += 1;
                            }
                            if (instruction->opcode == IR_OPCODE_SWITCH)
                            {
                                switched = instruction;
                                switch_count += 1;
                            }
                        }
                        shape = function && switch_count == 1 && argument_count == 1 && argument_value.value < function->value_count &&
                                switched->immediate_count == 1 && switched->operand_count == 1;
                        BUSTER_TEST(arguments, shape);
                        if (shape)
                        {
                            IrType* argument_type = ir_type_from_id(&program->types, function->values[argument_value.value].canonical_type);
                            shape = argument_type && argument_type->kind == IR_TYPE_INTEGER && argument_type->bit_width == width &&
                                    argument_type->is_signed == (signed_index != 0);
                            BUSTER_TEST(arguments, shape);
                            switched->operands[0] = argument_value;
                            switched->immediates[0] = key;
                            IrValidationResult validation = ir_validate_canonical_module(program, module);
                            BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
                            printf("SWITCH_VALIDATION width=%u signed=%u variant=%u consumer=%u key=%llu error=%u\n",
                                   width, signed_index, variant, consumer, (unsigned long long)key, (unsigned)validation.error);
                            if (shape && validation.error == IR_VALIDATION_NONE)
                            {
                                char path_buffer[192];
                                int path_length = snprintf(path_buffer, sizeof(path_buffer), "switch-evidence/w%u-s%u-v%u-c%u.%s",
                                                           width, signed_index, variant, consumer, consumer == 4 ? "bc" : "o");
                                String8 path = {.pointer = path_buffer, .length = (u64)path_length};
                                if (consumer == 4)
                                {
                                    LlvmBitcodeArtifact artifact = llvm_bitcode_emit_program(temporary.arena, program);
                                    printf("SWITCH_EMIT width=%u signed=%u variant=%u consumer=%u error=%u bytes=%llu\n",
                                           width, signed_index, variant, consumer, (unsigned)artifact.error.code, (unsigned long long)artifact.bytes.length);
                                    BUSTER_TEST(arguments, artifact.success);
                                    if (artifact.success)
                                    {
                                        BUSTER_TEST(arguments, file_write(path, artifact.bytes));
                                    }
                                }
                                else
                                {
                                    CodegenModule generated = codegen_generate_canonical_module(temporary.arena, program, module, target,
                                        (CodegenModuleOptions){.register_allocator = (u8)consumer, .verify_invariants = true, .record_fallbacks = true});
                                    printf("SWITCH_EMIT width=%u signed=%u variant=%u consumer=%u error=%u fallback=%u bytes=%llu\n",
                                           width, signed_index, variant, consumer, (unsigned)generated.error,
                                           generated.statistics.fallback_function_count, (unsigned long long)generated.code.length);
                                    BUSTER_TEST(arguments, generated.error == CODEGEN_ERROR_NONE);
                                    if (generated.error == CODEGEN_ERROR_NONE)
                                    {
                                        ObjectFile object = object_from_canonical_codegen_module(temporary.arena, program, &generated, target);
                                        ObjectArtifact artifact = object_write(temporary.arena, &object, object_format_for_target(target));
                                        BUSTER_TEST(arguments, artifact.error == OBJECT_ERROR_NONE);
                                        if (artifact.error == OBJECT_ERROR_NONE)
                                        {
                                            BUSTER_TEST(arguments, file_write(path, artifact.bytes));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    scratch_end(temporary);
                }
            }
        }
    }
    return result;
}
