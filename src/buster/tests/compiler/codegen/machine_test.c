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

BUSTER_GLOBAL_LOCAL u32 machine_test_module_offset(CodegenModule* module, IrModule* ir_module, String8 name)
{
    IrFunction* ir_function = machine_test_ir_function_find(ir_module, name);
    for (u32 entry_index = 0; ir_function && entry_index < module->entry_count; entry_index += 1)
    {
        if (module->entries[entry_index].symbol.value == ir_function->symbol.value)
        {
            return module->entries[entry_index].offset;
        }
    }
    return UINT32_MAX;
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
    String8 machine_c_source_head = S8("int add(int a, int b) { return a + b; }\n"
                                  "int mul(int a, int b) { return a * b; }\n"
                                  "long widen(int a, unsigned b) { return (long)a + (long)b; }\n"
                                  "int narrow(long v) { return (int)v; }\n"
                                  "int negate(int a) { return -a; }\n"
                                  "long bitnot(long a) { return ~a; }\n"
                                  "int lnot(int a) { return !a; }\n"
                                  "int less(int a, int b) { return a < b; }\n"
                                  "int uless(unsigned a, unsigned b) { return a < b; }\n"
                                  "int six(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; }\n"
                                  "long arr_lit(long a, long b) { long t[4] = {a, b, a + b, 5}; return t[0] * 1000 + t[1] * 100 + t[2] * 10 + t[3]; }\n"
                                  "struct KPair { long low; long high; };\n"
                                  "static long kagg_take(struct KPair pair, long salt) { return pair.low * 3 + pair.high + salt; }\n"
                                  "long kagg(long a, long b) { struct KPair pair = {.low = a + 1, .high = b}; return kagg_take(pair, a) + pair.high; }\n"
                                  "int sum_to(int n) { int s = 0; int i = 1; while (i <= n) { s = s + i; i = i + 1; } return s; }\n"
                                  "long readp(long* p) { return *p; }\n"
                                  "void writep(int* p, int v) { *p = v; }\n"
                                  "int divide(int a, int b) { return a / b; }\n"
                                  "int srem(int a, int b) { return a % b; }\n"
                                  "unsigned long udiv(unsigned long a, unsigned long b) { return a / b; }\n"
                                  "int shl(int a, int b) { return a << b; }\n"
                                  "long sar(long a, int b) { return a >> b; }\n"
                                  "unsigned shr(unsigned a, int b) { return a >> b; }\n"
                                  "int with_call(int a, int b) { return divide(a, 2) + srem(b, 3); }\n"
                                  "int fadd(float a, float b) { return a + b > 1.0f; }\n"
                                  "int seven(int a, int b, int c, int d, int e, int f, int g) { return a + g; }\n"
                                  "long stack_mix(int a, int b, int c, int d, int e, int f, int g, long h) { return a + g * h; }\n"
                                  "long call_stack(long h) { return stack_mix(1, 2, 3, 4, 5, 6, 7, h); }\n"
                                  "double nine(double a, double b, double c, double d, double e, double f, double g, double h, double i) {\n"
                                  "    return a + i * b; }\n"
                                  "int indirect(int (*callee)(int, int), int a) { return callee(a, 2); }\n"
                                  "int call_indirect(int a) { int (*f)(int, int) = divide; return f(a, 3); }\n"
                                  "int aligned_local(int x) { _Alignas(16) long buffer[4]; buffer[0] = x; buffer[3] = x * 2; return (int)(buffer[0] + buffer[3]); }\n"
                                  "_Atomic int atomic_cell;\n"
                                  "int atomic_probe(int v) { return __c11_atomic_fetch_add(&atomic_cell, v, 5); }\n"
                                  "int atomic_ops(int v) { _Atomic int cell; _Atomic long wide; __c11_atomic_store(&cell, v, 5);\n"
                                  "    __c11_atomic_store(&wide, (long)v * 7, 5);\n"
                                  "    int old = __c11_atomic_fetch_add(&cell, 3, 5); old += __c11_atomic_fetch_and(&cell, 6, 5);\n"
                                  "    old += __c11_atomic_exchange(&cell, v * 2, 5); int expected = v * 2;\n"
                                  "    __c11_atomic_compare_exchange_strong(&cell, &expected, 9, 5, 5); __c11_atomic_thread_fence(5);\n"
                                  "    return old + __c11_atomic_load(&cell, 5) + expected + (int)__c11_atomic_fetch_sub(&wide, 2, 5); }\n"
                                  "int goto_probe(int v) { void* t = v ? &&a : &&b; goto *t; a: return 1; b: return 2; }\n"
                                  "typedef struct Big { long a; long b; long c; } Big;\n"
                                  "Big big_make(long a) { Big b; b.a = a; b.b = a * 2; b.c = a ^ 5; return b; }\n"
                                  "long big_sum(Big b) { return b.a + b.b + b.c; }\n"
                                  "long big_round(long a) { Big b = big_make(a); return big_sum(b) + b.c; }\n"
                                  "int counter;\n"
                                  "int bump(int by) { counter = counter + by; return counter; }\n"
                                  "int table[8];\n"
                                  "int table_get(int i) { return table[i]; }\n"
                                  "void table_set(int i, int v) { table[i] = v; }\n"
                                  "typedef struct Pair { int first; long second; } Pair;\n"
                                  "Pair pair;\n"
                                  "long pair_sum(void) { return pair.first + pair.second; }\n"
                                  "int locals_array(int n) { int a[4]; a[0] = n; a[1] = n + 1; a[2] = a[0] * a[1]; a[3] = a[2] - n; return a[3]; }\n"
                                  "int local_pair(int x) { Pair p; p.first = x; p.second = x * 2; return p.first + (int)p.second; }\n"
                                  "int pick(int k) { switch (k) { case 1: return 10; case 3: return 30; case 7: return 70; default: return -k; } }\n");
    String8 machine_c_source_tail = S8(
                                  "int printf(const char* format, ...);\n"
                                  "int call_variadic(int a, long b) { return printf(\"%d %ld\", a, b); }\n"
                                  "typedef struct Span { char* data; unsigned long length; } Span;\n"
                                  "unsigned long span_length(Span s) { return s.length; }\n"
                                  "Span span_make(char* data, unsigned long length) { Span s; s.data = data; s.length = length; return s; }\n"
                                  "long span_round_trip(char* data, unsigned long length) {\n"
                                  "    Span s = span_make(data, length);\n"
                                  "    return (long)span_length(s) + (s.data == data ? 1 : 0);\n"
                                  "}\n"
                                  "typedef struct Single { long only; } Single;\n"
                                  "long single_round_trip(int a) { Single o; o.only = a * 3; Single copy = o; return copy.only; }\n"
                                  "int fmath(int a, int b) { double x = a; double y = b; double z = (x + y) * 0.5 - x / (y + 3.0); return (int)z; }\n"
                                  "int f32math(int a) { float x = (float)a; float y = x * 2.0f + 1.5f; return (int)(y - x); }\n"
                                  "int fcompare(int a, int b) { double x = a; double y = b;\n"
                                  "    return (x < y) + (x <= y) * 2 + (x == y) * 4 + (x != y) * 8 + (x > y) * 16 + (x >= y) * 32; }\n"
                                  "int fnegate(int a) { double x = a; return (int)-x; }\n"
                                  "int fnan(int a) { double n = a * 0.0; n = n / n; return (n == n) ? 1 : 0; }\n"
                                  "unsigned fuconv(unsigned a) { double x = a; return (unsigned)(x + 1.5); }\n"
                                  "double dadd(double a, double b) { return a + b; }\n"
                                  "double dmix(int a, double b, long c, double d) { return (double)a + b * (double)c - d; }\n"
                                  "float fhalf(float a, float b) { return (a - b) * 0.5f; }\n"
                                  "typedef struct DPair { double x; double y; } DPair;\n"
                                  "double dpair_sum(DPair p) { return p.x + p.y; }\n"
                                  "DPair dpair_make(double x, double y) { DPair p; p.x = x; p.y = y; return p; }\n"
                                  "typedef struct Tagged { long tag; double v; } Tagged;\n"
                                  "double tagged_get(Tagged t) { return t.tag ? t.v : -t.v; }\n"
                                  "double dcall(double a, double b) { DPair p = dpair_make(a, b); return dpair_sum(p) * dadd(a, b); }\n");
    String8 machine_c_source = string_format(arguments->arena, S8("{S8}{S8}"), machine_c_source_head, machine_c_source_tail);
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
            S8_INITIALIZER("uless"), S8_INITIALIZER("six"), S8_INITIALIZER("kagg"), S8_INITIALIZER("arr_lit"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("readp"),
            S8_INITIALIZER("writep"), S8_INITIALIZER("divide"), S8_INITIALIZER("srem"), S8_INITIALIZER("udiv"),
            S8_INITIALIZER("shl"), S8_INITIALIZER("sar"), S8_INITIALIZER("shr"), S8_INITIALIZER("bump"),
            S8_INITIALIZER("table_get"), S8_INITIALIZER("table_set"), S8_INITIALIZER("pair_sum"),
            S8_INITIALIZER("locals_array"), S8_INITIALIZER("local_pair"), S8_INITIALIZER("pick"),
            S8_INITIALIZER("aligned_local"), S8_INITIALIZER("span_length"), S8_INITIALIZER("span_make"), S8_INITIALIZER("span_round_trip"),
            S8_INITIALIZER("single_round_trip"), S8_INITIALIZER("fmath"), S8_INITIALIZER("f32math"),
            S8_INITIALIZER("fcompare"), S8_INITIALIZER("fnegate"), S8_INITIALIZER("fnan"), S8_INITIALIZER("fuconv"),
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
        // Direct calls select into fixed-register argument copies plus a
        // relocated call row; float signatures are the current explicit
        // unsupported representative.
        IrFunction* call_function = machine_test_ir_function_find(machine_module, S8("with_call"));
        BUSTER_TEST(arguments, call_function != 0);
        if (call_function)
        {
            MachineSelectResult call_selected = machine_select_canonical_function(arguments->arena, machine_program, call_function, machine_target);
            BUSTER_TEST(arguments, call_selected.supported);
            BUSTER_TEST(arguments, call_selected.function.call_target_count >= 2);
        }
        IrFunction* float_function = machine_test_ir_function_find(machine_module, S8("fadd"));
        BUSTER_TEST(arguments, float_function != 0);
        if (float_function)
        {
            MachineSelectResult float_selected = machine_select_canonical_function(arguments->arena, machine_program, float_function, machine_target);
            BUSTER_TEST(arguments, float_selected.supported);
        }
        // Stack arguments now select; dynamic stack allocation stays the
        // explicit unsupported representative.
        IrFunction* seven_function = machine_test_ir_function_find(machine_module, S8("seven"));
        BUSTER_TEST(arguments, seven_function != 0);
        if (seven_function)
        {
            MachineSelectResult seven_selected = machine_select_canonical_function(arguments->arena, machine_program, seven_function, machine_target);
            BUSTER_TEST(arguments, seven_selected.supported);
        }
        IrFunction* indirect_function = machine_test_ir_function_find(machine_module, S8("indirect"));
        BUSTER_TEST(arguments, indirect_function != 0);
        if (indirect_function)
        {
            MachineSelectResult indirect_selected = machine_select_canonical_function(arguments->arena, machine_program, indirect_function, machine_target);
            BUSTER_TEST(arguments, indirect_selected.supported);
        }
        // Atomics now select; computed goto stays the explicit unsupported
        // representative.
        IrFunction* atomic_function = machine_test_ir_function_find(machine_module, S8("atomic_probe"));
        BUSTER_TEST(arguments, atomic_function != 0);
        if (atomic_function)
        {
            MachineSelectResult atomic_selected = machine_select_canonical_function(arguments->arena, machine_program, atomic_function, machine_target);
            BUSTER_TEST(arguments, atomic_selected.supported);
        }
        IrFunction* goto_function = machine_test_ir_function_find(machine_module, S8("goto_probe"));
        BUSTER_TEST(arguments, goto_function != 0);
        if (goto_function)
        {
            MachineSelectResult goto_selected = machine_select_canonical_function(arguments->arena, machine_program, goto_function, machine_target);
            BUSTER_TEST(arguments, !goto_selected.supported);
        }
        // Variadic direct calls select (AL zero, register-only integer
        // arguments); execution is proven by the linked soak because the
        // extern callee cannot resolve in a raw code copy.
        IrFunction* variadic_function = machine_test_ir_function_find(machine_module, S8("call_variadic"));
        BUSTER_TEST(arguments, variadic_function != 0);
        if (variadic_function)
        {
            MachineSelectResult variadic_selected = machine_select_canonical_function(arguments->arena, machine_program, variadic_function, machine_target);
            BUSTER_TEST(arguments, variadic_selected.supported);
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
            // Functions touching global storage cannot execute from a raw
            // code copy: their data relocations only resolve at link time.
            // The full-unity soak is their execution proof.
            bool touches_globals = string_equal(supported_names[name_index], S8("bump")) ||
                                   string_equal(supported_names[name_index], S8("table_get")) ||
                                   string_equal(supported_names[name_index], S8("table_set")) ||
                                   string_equal(supported_names[name_index], S8("pair_sum"));
            // Call-containing functions execute only through the module
            // differential, where their call relocations resolve.
            bool contains_calls = string_equal(supported_names[name_index], S8("span_round_trip")) ||
                                  string_equal(supported_names[name_index], S8("kagg"));
            if (touches_globals || contains_calls)
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
                               string_equal(supported_names[name_index], S8("bitnot")) ||
                               string_equal(supported_names[name_index], S8("sar")) ||
                               string_equal(supported_names[name_index], S8("udiv")) || is_readp;
            bool is_division = string_equal(supported_names[name_index], S8("divide")) ||
                               string_equal(supported_names[name_index], S8("srem")) ||
                               string_equal(supported_names[name_index], S8("udiv"));
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
                if (is_division)
                {
                    // Never probe a zero divisor; odd divisors also keep the
                    // signed INT_MIN/-1 overflow case out of the grid.
                    right |= 1;
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
            // Frameless prologues stop after the frame-pointer setup; framed
            // ones add a chunked subtract (imm8 or imm32) and a probe touch.
            BUSTER_TEST(arguments, mir_add_descriptor &&
                                       (mir_add_descriptor->prolog_size == 4 || mir_add_descriptor->prolog_size == 12 || mir_add_descriptor->prolog_size == 15));
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_SANITIZE
        // Both modules resolve their internal direct-call relocations the
        // way the linker would, so machine-to-machine calls execute; this
        // must happen before the executable copies are taken.
        for (u32 relocation_index = 0; relocation_index < none_module.relocation_count + mir_module.relocation_count; relocation_index += 1)
        {
            CodegenModule* patched = relocation_index < none_module.relocation_count ? &none_module : &mir_module;
            u32 local_index = relocation_index < none_module.relocation_count ? relocation_index : relocation_index - none_module.relocation_count;
            CodegenModuleRelocation* relocation = patched->relocations + local_index;
            if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
            {
                continue;
            }
            for (u32 entry_index = 0; entry_index < patched->entry_count; entry_index += 1)
            {
                if (patched->entries[entry_index].symbol.value == relocation->symbol.value)
                {
                    u32 displacement = patched->entries[entry_index].offset - (relocation->offset + 4);
                    memcpy(patched->code.pointer + relocation->offset, &displacement, sizeof(displacement));
                    break;
                }
            }
        }
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
            S8_INITIALIZER("uless"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("divide"), S8_INITIALIZER("with_call"),
            S8_INITIALIZER("locals_array"), S8_INITIALIZER("local_pair"), S8_INITIALIZER("pick"),
            S8_INITIALIZER("aligned_local"), S8_INITIALIZER("span_round_trip"), S8_INITIALIZER("single_round_trip"), S8_INITIALIZER("fmath"),
            S8_INITIALIZER("fcompare"), S8_INITIALIZER("fnan"), S8_INITIALIZER("call_stack"), S8_INITIALIZER("big_round"),
            S8_INITIALIZER("call_indirect"), S8_INITIALIZER("atomic_ops"), S8_INITIALIZER("kagg"),
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
        // Float-signature shapes need typed callers: XMM scalars, mixed
        // integer/float argument sequences, all-float and mixed aggregates,
        // aggregate float returns, and machine-to-machine float calls.
        if (none_module_executable.address && mir_module_executable.address)
        {
            typedef double MachineTestCallD2(double, double);
            typedef double MachineTestCallDMix(int, double, long, double);
            typedef float MachineTestCallF2(float, float);
            typedef struct MachineTestDPair
            {
                double x;
                double y;
            } MachineTestDPair;
            typedef double MachineTestCallDPairSum(MachineTestDPair);
            typedef MachineTestDPair MachineTestCallDPairMake(double, double);
            typedef struct MachineTestTagged
            {
                s64 tag;
                double v;
            } MachineTestTagged;
            typedef double MachineTestCallTagged(MachineTestTagged);
            typedef int MachineTestCallI7(int, int, int, int, int, int, int);
            typedef s64 MachineTestCallStackMix(int, int, int, int, int, int, int, s64);
            typedef double MachineTestCallD9(double, double, double, double, double, double, double, double, double);
            String8 float_names[] = {
                S8_INITIALIZER("dadd"), S8_INITIALIZER("dmix"), S8_INITIALIZER("fhalf"), S8_INITIALIZER("dpair_sum"),
                S8_INITIALIZER("dpair_make"), S8_INITIALIZER("tagged_get"), S8_INITIALIZER("dcall"),
                S8_INITIALIZER("seven"), S8_INITIALIZER("stack_mix"), S8_INITIALIZER("nine"),
            };
            void* none_addresses[BUSTER_ARRAY_LENGTH(float_names)];
            void* mir_addresses[BUSTER_ARRAY_LENGTH(float_names)];
            bool float_offsets_found = true;
            for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(float_names); name_index += 1)
            {
                u32 none_offset = machine_test_module_offset(&none_module, machine_module, float_names[name_index]);
                u32 mir_offset = machine_test_module_offset(&mir_module, machine_module, float_names[name_index]);
                float_offsets_found &= none_offset != UINT32_MAX && mir_offset != UINT32_MAX;
                none_addresses[name_index] = none_offset != UINT32_MAX ? (u8*)none_module_executable.address + none_offset : 0;
                mir_addresses[name_index] = mir_offset != UINT32_MAX ? (u8*)mir_module_executable.address + mir_offset : 0;
            }
            BUSTER_TEST(arguments, float_offsets_found);
            if (float_offsets_found)
            {
                MachineTestCallD2* none_dadd;
                MachineTestCallD2* mir_dadd;
                MachineTestCallDMix* none_dmix;
                MachineTestCallDMix* mir_dmix;
                MachineTestCallF2* none_fhalf;
                MachineTestCallF2* mir_fhalf;
                MachineTestCallDPairSum* none_dpair_sum;
                MachineTestCallDPairSum* mir_dpair_sum;
                MachineTestCallDPairMake* none_dpair_make;
                MachineTestCallDPairMake* mir_dpair_make;
                MachineTestCallTagged* none_tagged;
                MachineTestCallTagged* mir_tagged;
                MachineTestCallD2* none_dcall;
                MachineTestCallD2* mir_dcall;
                memcpy(&none_dadd, none_addresses + 0, sizeof(none_dadd));
                memcpy(&mir_dadd, mir_addresses + 0, sizeof(mir_dadd));
                memcpy(&none_dmix, none_addresses + 1, sizeof(none_dmix));
                memcpy(&mir_dmix, mir_addresses + 1, sizeof(mir_dmix));
                memcpy(&none_fhalf, none_addresses + 2, sizeof(none_fhalf));
                memcpy(&mir_fhalf, mir_addresses + 2, sizeof(mir_fhalf));
                memcpy(&none_dpair_sum, none_addresses + 3, sizeof(none_dpair_sum));
                memcpy(&mir_dpair_sum, mir_addresses + 3, sizeof(mir_dpair_sum));
                memcpy(&none_dpair_make, none_addresses + 4, sizeof(none_dpair_make));
                memcpy(&mir_dpair_make, mir_addresses + 4, sizeof(mir_dpair_make));
                memcpy(&none_tagged, none_addresses + 5, sizeof(none_tagged));
                memcpy(&mir_tagged, mir_addresses + 5, sizeof(mir_tagged));
                memcpy(&none_dcall, none_addresses + 6, sizeof(none_dcall));
                memcpy(&mir_dcall, mir_addresses + 6, sizeof(mir_dcall));
                BUSTER_TEST(arguments, none_dadd(1.5, 2.25) == mir_dadd(1.5, 2.25));
                BUSTER_TEST(arguments, none_dadd(-0.125, 1e100) == mir_dadd(-0.125, 1e100));
                BUSTER_TEST(arguments, none_dmix(3, 1.5, -2, 0.25) == mir_dmix(3, 1.5, -2, 0.25));
                BUSTER_TEST(arguments, none_fhalf(7.5f, 2.5f) == mir_fhalf(7.5f, 2.5f));
                MachineTestDPair pair_probe = {3.5, -4.25};
                BUSTER_TEST(arguments, none_dpair_sum(pair_probe) == mir_dpair_sum(pair_probe));
                MachineTestDPair none_made = none_dpair_make(1.25, -8.5);
                MachineTestDPair mir_made = mir_dpair_make(1.25, -8.5);
                BUSTER_TEST(arguments, none_made.x == mir_made.x && none_made.y == mir_made.y);
                MachineTestTagged tagged_probe = {7, 9.5};
                MachineTestTagged tagged_zero = {0, 2.5};
                BUSTER_TEST(arguments, none_tagged(tagged_probe) == mir_tagged(tagged_probe));
                BUSTER_TEST(arguments, none_tagged(tagged_zero) == mir_tagged(tagged_zero));
                BUSTER_TEST(arguments, none_dcall(2.5, 4.0) == mir_dcall(2.5, 4.0));
                MachineTestCallI7* none_seven;
                MachineTestCallI7* mir_seven;
                MachineTestCallStackMix* none_stack_mix;
                MachineTestCallStackMix* mir_stack_mix;
                MachineTestCallD9* none_nine;
                MachineTestCallD9* mir_nine;
                memcpy(&none_seven, none_addresses + 7, sizeof(none_seven));
                memcpy(&mir_seven, mir_addresses + 7, sizeof(mir_seven));
                memcpy(&none_stack_mix, none_addresses + 8, sizeof(none_stack_mix));
                memcpy(&mir_stack_mix, mir_addresses + 8, sizeof(mir_stack_mix));
                memcpy(&none_nine, none_addresses + 9, sizeof(none_nine));
                memcpy(&mir_nine, mir_addresses + 9, sizeof(mir_nine));
                BUSTER_TEST(arguments, none_seven(1, 2, 3, 4, 5, 6, 70) == mir_seven(1, 2, 3, 4, 5, 6, 70));
                BUSTER_TEST(arguments, none_stack_mix(1, 2, 3, 4, 5, 6, 7, -11) == mir_stack_mix(1, 2, 3, 4, 5, 6, 7, -11));
                BUSTER_TEST(arguments,
                            none_nine(1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, -0.25) == mir_nine(1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, -0.25));
                typedef struct MachineTestBig
                {
                    s64 a;
                    s64 b;
                    s64 c;
                } MachineTestBig;
                typedef MachineTestBig MachineTestCallBigMake(s64);
                typedef s64 MachineTestCallBigSum(MachineTestBig);
                u32 none_big_make_offset = machine_test_module_offset(&none_module, machine_module, S8("big_make"));
                u32 mir_big_make_offset = machine_test_module_offset(&mir_module, machine_module, S8("big_make"));
                u32 none_big_sum_offset = machine_test_module_offset(&none_module, machine_module, S8("big_sum"));
                u32 mir_big_sum_offset = machine_test_module_offset(&mir_module, machine_module, S8("big_sum"));
                BUSTER_TEST(arguments, none_big_make_offset != UINT32_MAX && mir_big_make_offset != UINT32_MAX &&
                                           none_big_sum_offset != UINT32_MAX && mir_big_sum_offset != UINT32_MAX);
                if (none_big_make_offset != UINT32_MAX && mir_big_make_offset != UINT32_MAX && none_big_sum_offset != UINT32_MAX &&
                    mir_big_sum_offset != UINT32_MAX)
                {
                    MachineTestCallBigMake* none_big_make;
                    MachineTestCallBigMake* mir_big_make;
                    MachineTestCallBigSum* none_big_sum;
                    MachineTestCallBigSum* mir_big_sum;
                    void* none_big_make_address = (u8*)none_module_executable.address + none_big_make_offset;
                    void* mir_big_make_address = (u8*)mir_module_executable.address + mir_big_make_offset;
                    void* none_big_sum_address = (u8*)none_module_executable.address + none_big_sum_offset;
                    void* mir_big_sum_address = (u8*)mir_module_executable.address + mir_big_sum_offset;
                    memcpy(&none_big_make, &none_big_make_address, sizeof(none_big_make));
                    memcpy(&mir_big_make, &mir_big_make_address, sizeof(mir_big_make));
                    memcpy(&none_big_sum, &none_big_sum_address, sizeof(none_big_sum));
                    memcpy(&mir_big_sum, &mir_big_sum_address, sizeof(mir_big_sum));
                    MachineTestBig none_big = none_big_make(37);
                    MachineTestBig mir_big = mir_big_make(37);
                    BUSTER_TEST(arguments, none_big.a == mir_big.a && none_big.b == mir_big.b && none_big.c == mir_big.c);
                    BUSTER_TEST(arguments, none_big_sum(none_big) == mir_big_sum(mir_big));
                }
            }
        }
        // The fast allocator is a drop-in placement replacement: the same
        // module through FAST must behave identically to the oracle while
        // producing strictly less slot traffic than MIR_STACK.
        CodegenModule fast_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                      (CodegenModuleOptions){
                                                                          .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
                                                                      });
        BUSTER_TEST(arguments, fast_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, fast_module.statistics.fallback_function_count == mir_module.statistics.fallback_function_count);
        for (u32 relocation_index = 0; relocation_index < fast_module.relocation_count; relocation_index += 1)
        {
            CodegenModuleRelocation* relocation = fast_module.relocations + relocation_index;
            if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
            {
                continue;
            }
            for (u32 entry_index = 0; entry_index < fast_module.entry_count; entry_index += 1)
            {
                if (fast_module.entries[entry_index].symbol.value == relocation->symbol.value)
                {
                    u32 displacement = fast_module.entries[entry_index].offset - (relocation->offset + 4);
                    memcpy(fast_module.code.pointer + relocation->offset, &displacement, sizeof(displacement));
                    break;
                }
            }
        }
        CodegenExecutable fast_module_executable = codegen_make_executable((CodegenFunction){
            .code = fast_module.code,
        });
        BUSTER_TEST(arguments, fast_module_executable.error == CODEGEN_ERROR_NONE);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(module_names) && none_module_executable.address && fast_module_executable.address;
             name_index += 1)
        {
            u32 none_offset = machine_test_module_offset(&none_module, machine_module, module_names[name_index]);
            u32 fast_offset = machine_test_module_offset(&fast_module, machine_module, module_names[name_index]);
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && fast_offset != UINT32_MAX);
            if (none_offset == UINT32_MAX || fast_offset == UINT32_MAX)
            {
                continue;
            }
            bool fast_wide = string_equal(module_names[name_index], S8("widen")) || string_equal(module_names[name_index], S8("bitnot"));
            bool fast_loop = string_equal(module_names[name_index], S8("sum_to"));
            s64 fast_probes[][2] = {
                {6, 2}, {-9, 13}, {0, 1},
            };
            bool fast_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(fast_probes); probe_index += 1)
            {
                s64 left = fast_probes[probe_index][0];
                s64 right = fast_probes[probe_index][1];
                if (fast_loop)
                {
                    left &= 63;
                }
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_offset;
                MachineTestModuleCall2* none_call = 0;
                MachineTestModuleCall2* fast_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&fast_call, &fast_address, sizeof(fast_call));
                if (fast_wide)
                {
                    fast_equal &= none_call(left, right) == fast_call(left, right);
                }
                else
                {
                    fast_equal &= (s32)none_call(left, right) == (s32)fast_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, fast_equal, module_names[name_index]);
        }
        // Six register arguments exercise the entry captures: an eager pick
        // must not clobber an incoming argument register before its own
        // capture reads it.
        if (none_module_executable.address && fast_module_executable.address)
        {
            u32 none_offset = machine_test_module_offset(&none_module, machine_module, S8("six"));
            u32 fast_offset = machine_test_module_offset(&fast_module, machine_module, S8("six"));
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && fast_offset != UINT32_MAX);
            if (none_offset != UINT32_MAX && fast_offset != UINT32_MAX)
            {
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_offset;
                MachineTestCall6* none_call6 = 0;
                MachineTestCall6* fast_call6 = 0;
                memcpy(&none_call6, &none_address, sizeof(none_call6));
                memcpy(&fast_call6, &fast_address, sizeof(fast_call6));
                s32 none_result = (s32)none_call6(1, 20, 300, 4000, 50000, 600000);
                s32 fast_result = (s32)fast_call6(1, 20, 300, 4000, 50000, 600000);
                BUSTER_TEST_RAW(arguments, none_result == fast_result,
                                string_format(arguments->arena, S8("fast six none={u64} fast={u64}"), (u64)(u32)none_result, (u64)(u32)fast_result));
            }
        }
        codegen_release_executable(fast_module_executable);
        // The loop-heavy body must see strictly fewer reloads and spills
        // under the fast allocator than under the everything-in-slots mode.
        IrFunction* traffic_function = machine_test_ir_function_find(machine_module, S8("sum_to"));
        if (traffic_function)
        {
            MachineSelectResult traffic_selected = machine_select_canonical_function(arguments->arena, machine_program, traffic_function, machine_target);
            BUSTER_TEST(arguments, traffic_selected.supported);
            if (traffic_selected.supported)
            {
                MachineStackPlacement stack_placement = machine_stack_placement_build(arguments->arena, &traffic_selected.function);
                MachineStackPlacement fast_placement = machine_fast_placement_build(arguments->arena, &traffic_selected.function);
                BUSTER_TEST(arguments, stack_placement.valid && fast_placement.valid);
                BUSTER_TEST_RAW(arguments,
                                fast_placement.reload_count + fast_placement.spill_count <
                                    stack_placement.reload_count + stack_placement.spill_count,
                                string_format(arguments->arena, S8("fast traffic {u32}+{u32} vs stack {u32}+{u32}"), fast_placement.reload_count,
                                              fast_placement.spill_count, stack_placement.reload_count, stack_placement.spill_count));
            }
        }
        codegen_release_executable(none_module_executable);
        codegen_release_executable(mir_module_executable);
#endif
    }

    return result;
}
#endif
