#include <buster/tests/compiler/codegen/machine_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>

// Size census for the hot records the register-allocator project depends
// on. The machine rows are all-integer and hold everywhere; the typed IR and
// codegen records carry pointers, so their checks apply to 64-bit builds
// only. A designed size change must update these checks in the same change —
// they exist so design documents can never drift from the code the way the
// historical IrInstructionExtra comment did.
BUSTER_CT_CHECK(sizeof(MachineInstruction) == 24);
BUSTER_CT_CHECK(sizeof(MachineVirtualRegister) == 16);
BUSTER_CT_CHECK(sizeof(MachineBlock) == 32);
BUSTER_CT_CHECK(sizeof(MachineEdge) == 16);
BUSTER_CT_CHECK(sizeof(MachineAddress) == 16);
BUSTER_CT_CHECK(sizeof(MachineSegment) == 8);
BUSTER_CT_CHECK(sizeof(MachineUse) == 8);
BUSTER_CT_CHECK(sizeof(MachineEdit) == 16);
BUSTER_CT_CHECK(sizeof(MachineLocationSegment) == 16);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrInstruction) == 112);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrValue) == 24);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlock) == 64);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlockParameter) == 48);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrIncoming) == 16);
BUSTER_CT_CHECK(sizeof(CodegenModuleOptions) == 8);

// Compiles one C source through the C frontend into a canonical IrProgram
// for machine-selection tests. Diagnostics fail the caller's assertions.
BUSTER_GLOBAL_LOCAL IrProgram* machine_test_compile_c(Arena* arena, String8 name, String8 source, Target target)
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
    CIRLowerResult lowered = c_lower_to_ir(arena, name, tokens, parse, target);
    if (lowered.diagnostic_count)
    {
        return 0;
    }
    return lowered.program;
}

BUSTER_GLOBAL_LOCAL IrFunction* machine_test_ir_function_find(IrModule* module, String8 name)
{
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        if (string_equal(module->functions[function_index].name, name))
        {
            return module->functions + function_index;
        }
    }
    return 0;
}

// Selects, verifies, places, and encodes one function; returns encoded
// bytes with valid=false at the first failing pipeline step.
BUSTER_GLOBAL_LOCAL MachineEncodeResult machine_test_encode(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                            MachineSelectResult* select_out)
{
    MachineEncodeResult encoded = {0};
    MachineSelectResult selected = machine_select_canonical_function(arena, program, function, target);
    if (select_out)
    {
        *select_out = selected;
    }
    if (!selected.supported || machine_verify_function(&selected.function).error != MACHINE_VERIFY_NONE)
    {
        return encoded;
    }
    MachineStackPlacement placement = machine_stack_placement_build(arena, &selected.function);
    if (!placement.valid)
    {
        return encoded;
    }
    return machine_encode_x86_64(arena, &selected.function, &placement);
}

BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32 source = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                            });
    u32 destination = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                     .definition_point = machine_point_make(0, MACHINE_POINT_AFTER),
                                                                     .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                     .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                 });
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination),
                                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source)},
                                              .opcode = MACHINE_OPCODE_SKELETON_COPY,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    return machine_function_builder_finish(arena, &builder);
}

UnitTestResult machine_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    MachineRef ref = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER);
    BUSTER_TEST(arguments, machine_ref_payload(ref) == MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(MACHINE_REF_NONE_VALUE) == MACHINE_REF_NONE);
    BUSTER_TEST(arguments, machine_ref_payload(MACHINE_REF_NONE_VALUE) == 0);

    MachinePoint point = machine_point_make(MACHINE_POINT_INSTRUCTION_LIMIT - 1u, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_instruction(point) == MACHINE_POINT_INSTRUCTION_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_point_phase(point) == MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_phase(machine_point_make(7, MACHINE_POINT_BEFORE)) == MACHINE_POINT_BEFORE);
    // Phase order is the overlap contract: BEFORE < EARLY < NORMAL < AFTER
    // within one instruction, and the next instruction's BEFORE follows.
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_BEFORE) < machine_point_make(3, MACHINE_POINT_EARLY));
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_AFTER) < machine_point_make(4, MACHINE_POINT_BEFORE));

    MachineOpcodeInfo const* copy_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, copy_info && copy_info->operand_count == 2);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[0] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_DEFINE);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[1] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_USE);
    MachineOpcodeInfo const* return_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_RETURN);
    BUSTER_TEST(arguments, return_info && (return_info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR));
    BUSTER_TEST(arguments, machine_opcode_info(MACHINE_OPCODE_COUNT) == 0);

    MachineFunction function = machine_test_build_function(arguments->arena);
    BUSTER_TEST(arguments, function.instruction_count == 4);
    BUSTER_TEST(arguments, function.virtual_register_count == 2);
    BUSTER_TEST(arguments, function.block_count == 2);
    BUSTER_TEST(arguments, function.blocks[0].first_instruction == 0 && function.blocks[0].instruction_count == 2);
    BUSTER_TEST(arguments, function.blocks[1].first_instruction == 2 && function.blocks[1].instruction_count == 2);
    BUSTER_TEST(arguments, function.instructions[0].opcode == MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, machine_verify_function(&function).error == MACHINE_VERIFY_NONE);

    // The chunked builder must survive chunk boundaries: enough rows to
    // spill across several 16 KiB chunks, flattened back in exact order.
    u32 large_count = 3000;
    MachineFunctionBuilder large_builder = machine_function_builder_begin(arguments->arena);
    machine_builder_block_begin(&large_builder);
    for (u32 index = 0; index < large_count - 1; index += 1)
    {
        machine_builder_instruction(&large_builder, (MachineInstruction){
                                                        .payload = index,
                                                        .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                                    });
    }
    machine_builder_instruction(&large_builder, (MachineInstruction){
                                                    .payload = large_count - 1,
                                                    .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                                });
    machine_builder_block_end(&large_builder, (MachineBlock){0});
    MachineFunction large = machine_function_builder_finish(arguments->arena, &large_builder);
    BUSTER_TEST(arguments, large.instruction_count == large_count);
    bool payloads_ordered = true;
    for (u32 index = 0; index < large.instruction_count; index += 1)
    {
        payloads_ordered &= large.instructions[index].payload == index;
    }
    BUSTER_TEST(arguments, payloads_ordered);
    BUSTER_TEST(arguments, machine_verify_function(&large).error == MACHINE_VERIFY_NONE);

    // Verifier rejections. Each case mutates a fresh valid function so a
    // single detected defect cannot mask another.
    MachineFunction bad_opcode = machine_test_build_function(arguments->arena);
    bad_opcode.instructions[0].opcode = MACHINE_OPCODE_INVALID;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode).error == MACHINE_VERIFY_OPCODE);
    MachineFunction bad_opcode_range = machine_test_build_function(arguments->arena);
    bad_opcode_range.instructions[0].opcode = MACHINE_OPCODE_COUNT;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode_range).error == MACHINE_VERIFY_OPCODE);

    MachineFunction early_terminator = machine_test_build_function(arguments->arena);
    early_terminator.instructions[0].opcode = MACHINE_OPCODE_SKELETON_RETURN;
    early_terminator.instructions[0].operands[0] = MACHINE_REF_NONE_VALUE;
    early_terminator.instructions[0].operands[1] = MACHINE_REF_NONE_VALUE;
    BUSTER_TEST(arguments, machine_verify_function(&early_terminator).error == MACHINE_VERIFY_TERMINATOR);
    MachineFunction missing_terminator = machine_test_build_function(arguments->arena);
    missing_terminator.instructions[3].opcode = MACHINE_OPCODE_SKELETON_NOP;
    BUSTER_TEST(arguments, machine_verify_function(&missing_terminator).error == MACHINE_VERIFY_TERMINATOR);

    MachineFunction bad_reference = machine_test_build_function(arguments->arena);
    bad_reference.instructions[0].operands[1] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bad_reference.virtual_register_count);
    MachineVerifyResult reference_result = machine_verify_function(&bad_reference);
    BUSTER_TEST(arguments, reference_result.error == MACHINE_VERIFY_OPERAND_REFERENCE && reference_result.operand == 1);

    MachineFunction dirty_slot = machine_test_build_function(arguments->arena);
    dirty_slot.instructions[0].operands[2] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, 0);
    BUSTER_TEST(arguments, machine_verify_function(&dirty_slot).error == MACHINE_VERIFY_OPERAND_SLOT);

    MachineFunction bad_range = machine_test_build_function(arguments->arena);
    bad_range.blocks[1].first_instruction = 3;
    BUSTER_TEST(arguments, machine_verify_function(&bad_range).error == MACHINE_VERIFY_BLOCK_RANGE);
    MachineFunction uncovered = machine_test_build_function(arguments->arena);
    uncovered.blocks[1].instruction_count = 1;
    BUSTER_TEST(arguments, machine_verify_function(&uncovered).error != MACHINE_VERIFY_NONE);

    MachineFunction bad_definition = machine_test_build_function(arguments->arena);
    bad_definition.virtual_registers[1].definition_point = machine_point_make(function.instruction_count, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_verify_function(&bad_definition).error == MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION);

    // Replay round-trip: byte-exact reconstruction, then hard rejection of
    // corrupted headers and truncated payloads.
    ByteSlice replay = machine_replay_serialize(arguments->arena, &function);
    MachineFunction replayed = {0};
    BUSTER_TEST(arguments, machine_replay_deserialize(arguments->arena, replay, &replayed));
    BUSTER_TEST(arguments, replayed.instruction_count == function.instruction_count);
    BUSTER_TEST(arguments, replayed.virtual_register_count == function.virtual_register_count);
    BUSTER_TEST(arguments, replayed.block_count == function.block_count);
    BUSTER_TEST(arguments,
                memcmp(replayed.instructions, function.instructions, function.instruction_count * sizeof(MachineInstruction)) == 0);
    BUSTER_TEST(arguments,
                memcmp(replayed.virtual_registers, function.virtual_registers, function.virtual_register_count * sizeof(MachineVirtualRegister)) == 0);
    BUSTER_TEST(arguments, memcmp(replayed.blocks, function.blocks, function.block_count * sizeof(MachineBlock)) == 0);
    BUSTER_TEST(arguments, machine_verify_function(&replayed).error == MACHINE_VERIFY_NONE);

    MachineFunction rejected = {0};
    ByteSlice truncated = replay;
    truncated.length -= 1;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, truncated, &rejected));
    u8* corrupt_bytes = arena_allocate(arguments->arena, u8, replay.length);
    memcpy(corrupt_bytes, replay.pointer, replay.length);
    corrupt_bytes[0] ^= 0xff;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){.pointer = corrupt_bytes, .length = replay.length}, &rejected));
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){0}, &rejected));

    // Mode plumbing: the driver-facing enum and its report spelling.
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_NONE), S8("none"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_MIR_STACK), S8("mir-stack"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_FAST), S8("fast"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_QUALITY), S8("quality"));

    // Stage 2: x86-64 selection, MIR_STACK placement, and encoding over the
    // scalar subset. Selection and encoding are host-independent; execution
    // requires a non-sanitized x86-64 host and runs the same functions
    // through the canonical NONE path as the differential oracle.
    Target machine_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 machine_c_source = S8("int add(int a, int b) { return a + b; }\n"
                                  "int mul(int a, int b) { return a * b; }\n"
                                  "long widen(int a, unsigned b) { return (long)a + (long)b; }\n"
                                  "int narrow(long v) { return (int)v; }\n"
                                  "int negate(int a) { return -a; }\n"
                                  "long bitnot(long a) { return ~a; }\n"
                                  "int lnot(int a) { return !a; }\n"
                                  "int less(int a, int b) { return a < b; }\n"
                                  "int uless(unsigned a, unsigned b) { return a < b; }\n"
                                  "int six(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; }\n"
                                  "int sum_to(int n) { int s = 0; int i = 1; while (i <= n) { s = s + i; i = i + 1; } return s; }\n"
                                  "long readp(long* p) { return *p; }\n"
                                  "void writep(int* p, int v) { *p = v; }\n"
                                  "int divide(int a, int b) { return a / b; }\n");
    IrProgram* machine_program = machine_test_compile_c(arguments->arena, S8("machine-stage2.c"), machine_c_source, machine_target);
    BUSTER_TEST(arguments, machine_program != 0);
    if (machine_program && machine_program->module_count)
    {
        IrModule* machine_module = machine_program->modules;
        // The whole-module NONE oracle: identical IR through the canonical
        // path, executed at each function's entry offset.
        CodegenModule none_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                      (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, none_module.error == CODEGEN_ERROR_NONE);
        String8 supported_names[] = {
            S8_INITIALIZER("add"), S8_INITIALIZER("mul"), S8_INITIALIZER("widen"), S8_INITIALIZER("narrow"),
            S8_INITIALIZER("negate"), S8_INITIALIZER("bitnot"), S8_INITIALIZER("lnot"), S8_INITIALIZER("less"),
            S8_INITIALIZER("uless"), S8_INITIALIZER("six"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("readp"),
            S8_INITIALIZER("writep"),
        };
        MachineEncodeResult machine_encoded[BUSTER_ARRAY_LENGTH(supported_names)] = {0};
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(supported_names); name_index += 1)
        {
            IrFunction* ir_function = machine_test_ir_function_find(machine_module, supported_names[name_index]);
            BUSTER_TEST(arguments, ir_function != 0);
            if (!ir_function)
            {
                continue;
            }
            MachineSelectResult selected = {0};
            machine_encoded[name_index] = machine_test_encode(arguments->arena, machine_program, ir_function, machine_target, &selected);
            BUSTER_TEST_RAW(arguments, selected.supported,
                            string_format(arguments->arena, S8("select {S8} failed at opcode {u32}"), supported_names[name_index],
                                          (u32)selected.failed_opcode));
            BUSTER_TEST(arguments, selected.machine_instructions >= selected.selected_typed_instructions / 4);
            BUSTER_TEST(arguments, machine_encoded[name_index].valid);
            BUSTER_TEST(arguments, machine_encoded[name_index].byte_count > 8);
            // The prologue shape is fixed: push rbp; mov rbp, rsp.
            BUSTER_TEST(arguments, machine_encoded[name_index].bytes[0] == 0x55 && machine_encoded[name_index].bytes[1] == 0x48 &&
                                       machine_encoded[name_index].bytes[2] == 0x89 && machine_encoded[name_index].bytes[3] == 0xe5);
        }
        // Placement statistics: MIR_STACK round-trips every operand, so a
        // selected function with arithmetic must report both reloads and
        // spills, bounded by four per instruction.
        IrFunction* add_function = machine_test_ir_function_find(machine_module, S8("add"));
        if (add_function)
        {
            MachineSelectResult add_selected = machine_select_canonical_function(arguments->arena, machine_program, add_function, machine_target);
            BUSTER_TEST(arguments, add_selected.supported);
            MachineStackPlacement add_placement = machine_stack_placement_build(arguments->arena, &add_selected.function);
            BUSTER_TEST(arguments, add_placement.valid);
            BUSTER_TEST(arguments, add_placement.reload_count > 0 && add_placement.spill_count > 0);
            BUSTER_TEST(arguments, add_placement.reload_count + add_placement.spill_count <= add_selected.function.instruction_count * 4);
            BUSTER_TEST(arguments, add_placement.frame_size % 16 == 0);
        }
        // Explicit unsupported fallback: divide needs the RAX/RDX pair the
        // stage-2 subset does not model yet.
        IrFunction* divide_function = machine_test_ir_function_find(machine_module, S8("divide"));
        BUSTER_TEST(arguments, divide_function != 0);
        if (divide_function)
        {
            MachineSelectResult divide_selected = machine_select_canonical_function(arguments->arena, machine_program, divide_function, machine_target);
            BUSTER_TEST(arguments, !divide_selected.supported);
            BUSTER_TEST(arguments, divide_selected.failed_opcode == IR_OPCODE_BINARY);
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable none_executable = codegen_make_executable((CodegenFunction){
            .code = none_module.code,
        });
        BUSTER_TEST(arguments, none_executable.error == CODEGEN_ERROR_NONE);
        typedef s64 MachineTestCall2(s64, s64);
        typedef s64 MachineTestCall6(s64, s64, s64, s64, s64, s64);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(supported_names) && none_executable.address; name_index += 1)
        {
            if (!machine_encoded[name_index].valid)
            {
                continue;
            }
            IrFunction* ir_function = machine_test_ir_function_find(machine_module, supported_names[name_index]);
            u32 none_offset = UINT32_MAX;
            for (u32 entry_index = 0; entry_index < none_module.entry_count; entry_index += 1)
            {
                if (ir_function && none_module.entries[entry_index].symbol.value == ir_function->symbol.value)
                {
                    none_offset = none_module.entries[entry_index].offset;
                    break;
                }
            }
            BUSTER_TEST(arguments, none_offset != UINT32_MAX);
            CodegenExecutable machine_executable = codegen_make_executable((CodegenFunction){
                .code = {.pointer = machine_encoded[name_index].bytes, .length = machine_encoded[name_index].byte_count},
            });
            BUSTER_TEST(arguments, machine_executable.error == CODEGEN_ERROR_NONE);
            if (none_offset == UINT32_MAX || !machine_executable.address)
            {
                continue;
            }
            bool is_writep = string_equal(supported_names[name_index], S8("writep"));
            bool is_readp = string_equal(supported_names[name_index], S8("readp"));
            bool is_six = string_equal(supported_names[name_index], S8("six"));
            bool is_loop = string_equal(supported_names[name_index], S8("sum_to"));
            // Functions declared to return `long` promise all sixty-four
            // result bits; `int` returns compare only the low thirty-two,
            // because the canonical and machine paths may legitimately leave
            // different stale upper bits in RAX.
            bool wide_result = string_equal(supported_names[name_index], S8("widen")) ||
                               string_equal(supported_names[name_index], S8("bitnot")) || is_readp;
            s64 probe_arguments[][2] = {
                {0, 0}, {1, 2}, {-1, 5}, {123456789, -987654321}, {-2147483647, 2147483647}, {40, 2}, {7, -7},
            };
            bool all_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(probe_arguments); probe_index += 1)
            {
                s64 left = probe_arguments[probe_index][0];
                s64 right = probe_arguments[probe_index][1];
                if (is_loop)
                {
                    // Keep iteration counts test-sized.
                    left &= 63;
                }
                void* none_address = (u8*)none_executable.address + none_offset;
                void* machine_address = machine_executable.address;
                MachineTestCall2* none_call = 0;
                MachineTestCall2* machine_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&machine_call, &machine_address, sizeof(machine_call));
                if (is_writep)
                {
                    s32 none_cell = 0;
                    s32 machine_cell = 0;
                    none_call((s64)(u64)&none_cell, right);
                    machine_call((s64)(u64)&machine_cell, right);
                    all_equal &= none_cell == machine_cell;
                }
                else if (is_readp)
                {
                    s64 cell = left * 3 + right;
                    all_equal &= none_call((s64)(u64)&cell, 0) == machine_call((s64)(u64)&cell, 0);
                }
                else if (is_six)
                {
                    MachineTestCall6* none_call6 = 0;
                    MachineTestCall6* machine_call6 = 0;
                    memcpy(&none_call6, &none_address, sizeof(none_call6));
                    memcpy(&machine_call6, &machine_address, sizeof(machine_call6));
                    s32 none_result = (s32)none_call6(left, right, left + 1, right + 1, left - 2, right - 2);
                    s32 machine_result = (s32)machine_call6(left, right, left + 1, right + 1, left - 2, right - 2);
                    BUSTER_TEST_RAW(arguments, none_result == machine_result,
                                    string_format(arguments->arena, S8("six none={u64} machine={u64}"), (u64)(u32)none_result, (u64)(u32)machine_result));
                    all_equal &= none_result == machine_result;
                }
                else if (wide_result)
                {
                    all_equal &= none_call(left, right) == machine_call(left, right);
                }
                else
                {
                    all_equal &= (s32)none_call(left, right) == (s32)machine_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, all_equal, supported_names[name_index]);
            codegen_release_executable(machine_executable);
        }
        codegen_release_executable(none_executable);
#endif
        // Stage 3 wiring: the same module generated under MIR_STACK routes
        // every eligible function through the machine path and counts the
        // rest as explicit fallbacks; the canonical NONE module is the
        // execution oracle for both kinds.
        CodegenModule mir_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                     (CodegenModuleOptions){
                                                                         .register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                                                                     });
        BUSTER_TEST(arguments, mir_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, none_module.statistics.fallback_function_count == 0);
        BUSTER_TEST_RAW(arguments, mir_module.statistics.fallback_function_count == 1,
                        string_format(arguments->arena, S8("mir fallbacks {u32}"), mir_module.statistics.fallback_function_count));
        IrFunction* mir_add_function = machine_test_ir_function_find(machine_module, S8("add"));
        if (mir_add_function)
        {
            CodegenFunctionDescriptor* mir_add_descriptor = 0;
            for (u32 descriptor_index = 0; descriptor_index < mir_module.function_count; descriptor_index += 1)
            {
                if (mir_module.functions[descriptor_index].symbol.value == mir_add_function->symbol.value)
                {
                    mir_add_descriptor = mir_module.functions + descriptor_index;
                    break;
                }
            }
            BUSTER_TEST(arguments, mir_add_descriptor != 0);
            BUSTER_TEST(arguments, mir_add_descriptor && mir_add_descriptor->code_size > 8);
            BUSTER_TEST(arguments, mir_add_descriptor && mir_add_descriptor->unwind_action_count >= 2 &&
                                       mir_add_descriptor->unwind_actions[0].kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER);
            BUSTER_TEST(arguments, mir_add_descriptor && (mir_add_descriptor->prolog_size == 4 || mir_add_descriptor->prolog_size == 11));
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        CodegenExecutable none_module_executable = codegen_make_executable((CodegenFunction){
            .code = none_module.code,
        });
        CodegenExecutable mir_module_executable = codegen_make_executable((CodegenFunction){
            .code = mir_module.code,
        });
        BUSTER_TEST(arguments, none_module_executable.error == CODEGEN_ERROR_NONE && mir_module_executable.error == CODEGEN_ERROR_NONE);
        String8 module_names[] = {
            S8_INITIALIZER("add"), S8_INITIALIZER("mul"), S8_INITIALIZER("widen"), S8_INITIALIZER("narrow"),
            S8_INITIALIZER("negate"), S8_INITIALIZER("bitnot"), S8_INITIALIZER("lnot"), S8_INITIALIZER("less"),
            S8_INITIALIZER("uless"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("divide"),
        };
        typedef s64 MachineTestModuleCall2(s64, s64);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(module_names) && none_module_executable.address && mir_module_executable.address;
             name_index += 1)
        {
            IrFunction* module_function = machine_test_ir_function_find(machine_module, module_names[name_index]);
            u32 none_offset = UINT32_MAX;
            u32 mir_offset = UINT32_MAX;
            for (u32 entry_index = 0; module_function && entry_index < none_module.entry_count; entry_index += 1)
            {
                if (none_module.entries[entry_index].symbol.value == module_function->symbol.value)
                {
                    none_offset = none_module.entries[entry_index].offset;
                }
            }
            for (u32 entry_index = 0; module_function && entry_index < mir_module.entry_count; entry_index += 1)
            {
                if (mir_module.entries[entry_index].symbol.value == module_function->symbol.value)
                {
                    mir_offset = mir_module.entries[entry_index].offset;
                }
            }
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && mir_offset != UINT32_MAX);
            if (none_offset == UINT32_MAX || mir_offset == UINT32_MAX)
            {
                continue;
            }
            bool module_wide = string_equal(module_names[name_index], S8("widen")) || string_equal(module_names[name_index], S8("bitnot"));
            bool module_loop = string_equal(module_names[name_index], S8("sum_to"));
            s64 module_probes[][2] = {
                {5, 3}, {-7, 9}, {0, 1}, {2147483646, -2},
            };
            bool module_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(module_probes); probe_index += 1)
            {
                s64 left = module_probes[probe_index][0];
                s64 right = module_probes[probe_index][1];
                if (module_loop)
                {
                    left &= 63;
                }
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* mir_address = (u8*)mir_module_executable.address + mir_offset;
                MachineTestModuleCall2* none_call = 0;
                MachineTestModuleCall2* mir_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&mir_call, &mir_address, sizeof(mir_call));
                if (module_wide)
                {
                    module_equal &= none_call(left, right) == mir_call(left, right);
                }
                else
                {
                    module_equal &= (s32)none_call(left, right) == (s32)mir_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, module_equal, module_names[name_index]);
        }
        codegen_release_executable(none_module_executable);
        codegen_release_executable(mir_module_executable);
#endif
    }

    return result;
}
#endif
