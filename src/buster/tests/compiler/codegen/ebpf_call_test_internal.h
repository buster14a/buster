#pragma once

// Private issue #1627 oracle tests. kernel_loader_controls exercises the loader
// without BPF privileges; local_calls lowers both frontend forms and compares
// named-entry execution with independent host arithmetic when available.
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS

enum
{
    CODEGEN_TEST_EBPF_CALL_ELF_BYTES = 704,
    CODEGEN_TEST_EBPF_CALL_SECTIONS = 320,
    CODEGEN_TEST_EBPF_CALL_SYMBOLS = 112,
    CODEGEN_TEST_EBPF_CALL_RELOCATIONS = 256,
};

BUSTER_GLOBAL_LOCAL void codegen_test_ebpf_call_write(u8* bytes, u32 offset, u32 width, u64 value)
{
    for (u32 byte = 0; byte < width; byte += 1) bytes[offset + byte] = (u8)(value >> (byte * 8));
}

// A hand-encoded loader control, independent of the production emitter: helper
// occupies three rows before probe, and probe's first row calls the helper.
// Only the ELF/CFG contract is checked here; the C fixtures below execute ABI-
// valid emitted code through the kernel, not this structural control.
BUSTER_GLOBAL_LOCAL ByteSlice codegen_test_ebpf_call_fixture(Arena* arena)
{
    u8* bytes = arena_allocate_zeroed(arena, u8, CODEGEN_TEST_EBPF_CALL_ELF_BYTES);
    memcpy(bytes, "\177ELF\2\1\1", 7);
    codegen_test_ebpf_call_write(bytes, 16, 2, 1);
    codegen_test_ebpf_call_write(bytes, 18, 2, 247);
    codegen_test_ebpf_call_write(bytes, 20, 4, 1);
    codegen_test_ebpf_call_write(bytes, 40, 8, CODEGEN_TEST_EBPF_CALL_SECTIONS);
    codegen_test_ebpf_call_write(bytes, 52, 2, 64);
    codegen_test_ebpf_call_write(bytes, 58, 2, 64);
    codegen_test_ebpf_call_write(bytes, 60, 2, 6);
    codegen_test_ebpf_call_write(bytes, 62, 2, 5);
    u8 const code[] = {
        0xbf, 0x10, 0, 0, 0, 0, 0, 0,
        0x27, 0, 0, 0, 3, 0, 0, 0,
        0x95, 0, 0, 0, 0, 0, 0, 0,
        0x85, 0x10, 0, 0, 0xff, 0xff, 0xff, 0xff,
        0x0f, 0x20, 0, 0, 0, 0, 0, 0,
        0x95, 0, 0, 0, 0, 0, 0, 0,
    };
    memcpy(bytes + 64, code, sizeof(code));
    memcpy(bytes + 184, "\0helper\0probe\0", 14);
    memcpy(bytes + 200, "\0.text\0.symtab\0.strtab\0.rel.text\0.shstrtab\0", 43);
    for (u32 index = 0; index < 2; index += 1)
    {
        u32 offset = CODEGEN_TEST_EBPF_CALL_SYMBOLS + (index + 1) * 24;
        codegen_test_ebpf_call_write(bytes, offset, 4, index ? 8 : 1);
        codegen_test_ebpf_call_write(bytes, offset + 4, 1, index ? 0x12 : 0x02);
        codegen_test_ebpf_call_write(bytes, offset + 6, 2, 1);
        codegen_test_ebpf_call_write(bytes, offset + 8, 8, index * 24);
        codegen_test_ebpf_call_write(bytes, offset + 16, 8, 24);
    }
    codegen_test_ebpf_call_write(bytes, CODEGEN_TEST_EBPF_CALL_RELOCATIONS, 8, 24);
    codegen_test_ebpf_call_write(bytes, CODEGEN_TEST_EBPF_CALL_RELOCATIONS + 8, 8, (UINT64_C(1) << 32) | 10);
    typedef struct CodegenTestEbpfCallSection CodegenTestEbpfCallSection;
    struct CodegenTestEbpfCallSection
    {
        u32 name, type, offset, size, link, info, entry_size;
        u64 flags;
    };
    CodegenTestEbpfCallSection sections[] = {
        {1, 1, 64, sizeof(code), 0, 0, 0, 6},
        {7, 2, CODEGEN_TEST_EBPF_CALL_SYMBOLS, 72, 3, 2, 24, 0},
        {15, 3, 184, 14, 0, 0, 0, 0},
        {23, 9, CODEGEN_TEST_EBPF_CALL_RELOCATIONS, 16, 2, 1, 16, 0},
        {33, 3, 200, 43, 0, 0, 0, 0},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sections); index += 1)
    {
        CodegenTestEbpfCallSection section = sections[index];
        u32 offset = CODEGEN_TEST_EBPF_CALL_SECTIONS + (index + 1) * 64;
        codegen_test_ebpf_call_write(bytes, offset, 4, section.name);
        codegen_test_ebpf_call_write(bytes, offset + 4, 4, section.type);
        codegen_test_ebpf_call_write(bytes, offset + 8, 8, section.flags);
        codegen_test_ebpf_call_write(bytes, offset + 24, 8, section.offset);
        codegen_test_ebpf_call_write(bytes, offset + 32, 8, section.size);
        codegen_test_ebpf_call_write(bytes, offset + 40, 4, section.link);
        codegen_test_ebpf_call_write(bytes, offset + 44, 4, section.info);
        codegen_test_ebpf_call_write(bytes, offset + 48, 8, 8);
        codegen_test_ebpf_call_write(bytes, offset + 56, 8, section.entry_size);
    }
    return (ByteSlice){bytes, CODEGEN_TEST_EBPF_CALL_ELF_BYTES};
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_kernel_loader_controls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    ByteSlice fixture = codegen_test_ebpf_call_fixture(arena);
    String8 reason = {0};
    CodegenTestEbpfObject loaded = codegen_test_ebpf_kernel_object(arena, fixture, S8("probe"), &reason);
    BUSTER_TEST(arguments, loaded.code.length == 48 && loaded.entry_offset == 24);
    if (loaded.code.length)
    {
        BUSTER_TEST(arguments, (s32)codegen_test_ebpf_read(loaded.code.pointer + 28, 4) == -4);
        BUSTER_TEST(arguments, codegen_test_ebpf_read(fixture.pointer + 92, 4) == UINT32_MAX);
        BUSTER_TEST(arguments, memcmp(loaded.code.pointer, fixture.pointer + 64, 24) == 0);
    }
    BUSTER_TEST(arguments, codegen_test_ebpf_code(fixture).length == 0);
    BUSTER_TEST(arguments, codegen_test_ebpf_kernel_object(arena, fixture, S8("missing"), &reason).code.length == 0);
    BUSTER_TEST(arguments, codegen_test_ebpf_kernel_object(arena, (ByteSlice){fixture.pointer, fixture.length - 1}, S8("probe"), &reason).code.length == 0);
    ByteSlice forward = codegen_test_ebpf_call_fixture(arena);
    memcpy((u8*)forward.pointer + 64, fixture.pointer + 88, 24);
    memcpy((u8*)forward.pointer + 88, fixture.pointer + 64, 24);
    codegen_test_ebpf_call_write((u8*)forward.pointer, 136 + 8, 8, 24);
    codegen_test_ebpf_call_write((u8*)forward.pointer, 160 + 8, 8, 0);
    codegen_test_ebpf_call_write((u8*)forward.pointer, CODEGEN_TEST_EBPF_CALL_RELOCATIONS, 8, 0);
    CodegenTestEbpfObject forward_loaded = codegen_test_ebpf_kernel_object(arena, forward, S8("probe"), &reason);
    BUSTER_TEST(arguments, forward_loaded.code.length == 48 && forward_loaded.entry_offset == 0);
    if (forward_loaded.code.length) BUSTER_TEST(arguments, codegen_test_ebpf_read(forward_loaded.code.pointer + 4, 4) == 2);
    typedef struct CodegenTestEbpfCallMutation CodegenTestEbpfCallMutation;
    struct CodegenTestEbpfCallMutation { u32 offset, width; u64 value; };
    CodegenTestEbpfCallMutation mutations[] = {
        {0, 1, 0}, // Invalid ELF magic/class/data/type/machine/header range.
        {4, 1, 1},
        {5, 1, 2},
        {16, 2, 2},
        {18, 2, 62},
        {40, 8, UINT64_MAX},
        {58, 2, 63},
        {60, 2, UINT16_MAX},
        {62, 2, 6},
        {384 + 24, 8, UINT64_MAX}, // Code offset/size/name and symbol metadata.
        {384 + 32, 8, 47},
        {384, 4, 42},
        {448 + 40, 4, 6},
        {448 + 56, 8, 23},
        {448 + 32, 8, 71},
        {136 + 6, 2, 0}, // Undefined/non-function/wrong-section/bad function target.
        {136 + 4, 1, 1},
        {136 + 6, 2, 3},
        {136 + 8, 8, 8},
        {136 + 16, 8, 0},
        {160 + 8, 8, 16}, // Overlapping function range and invalid entry name.
        {160, 4, 14},
        {136, 4, 8}, // Two definitions with the requested entry name.
        {197, 1, 'x'}, // Unterminated probe symbol name.
        {576 + 40, 4, 3}, // Relocation table link/stride/size/type/symbol/site.
        {576 + 56, 8, 8},
        {576 + 32, 8, 15},
        {264, 8, (UINT64_C(1) << 32) | 1},
        {264, 8, (UINT64_C(99) << 32) | 10},
        {256, 8, 25},
        {256, 8, 48},
        {88, 1, 0x95}, // Wrong instruction/source/destination/reserved offset/addend.
        {89, 1, 0},
        {89, 1, 0x11},
        {90, 2, 1},
        {92, 4, 0},
        {576 + 32, 8, 0}, // A local call without a relocation cannot pass.
        {64, 4, 0x000a0005}, // Jump beyond helper, fall off entry, broken LDDW.
        {104, 1, 0xb7},
        {64, 1, 0x18},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(mutations); index += 1)
    {
        TemporalArena control = arena_begin_temporal(arena);
        ByteSlice changed = codegen_test_ebpf_call_fixture(arena);
        CodegenTestEbpfCallMutation mutation = mutations[index];
        codegen_test_ebpf_call_write((u8*)changed.pointer, mutation.offset, mutation.width, mutation.value);
        CodegenTestEbpfObject rejected = codegen_test_ebpf_kernel_object(arena, changed, S8("probe"), &reason);
        if (rejected.code.length) arguments->show(arguments, S8("eBPF local-call loader accepted control {u32}\n"), index);
        BUSTER_TEST(arguments, rejected.code.length == 0);
        arena_set_position(arena, control.position);
    }
    for (u32 addend = 0; addend < 2; addend += 1)
    {
        TemporalArena control = arena_begin_temporal(arena);
        ByteSlice changed = codegen_test_ebpf_call_fixture(arena);
        u8* bytes = (u8*)changed.pointer;
        codegen_test_ebpf_call_write(bytes, 576 + 4, 4, 4);
        codegen_test_ebpf_call_write(bytes, 576 + 32, 8, 24);
        codegen_test_ebpf_call_write(bytes, 576 + 56, 8, 24);
        codegen_test_ebpf_call_write(bytes, 272, 8, addend);
        BUSTER_TEST(arguments, (codegen_test_ebpf_kernel_object(arena, changed, S8("probe"), &reason).code.length != 0) == (addend == 0));
        arena_set_position(arena, control.position);
    }
    // A duplicate relocation must not overwrite an already resolved call.
    ByteSlice duplicate = codegen_test_ebpf_call_fixture(arena);
    memcpy((u8*)duplicate.pointer + 272, duplicate.pointer + 256, 16);
    codegen_test_ebpf_call_write((u8*)duplicate.pointer, 576 + 32, 8, 32);
    BUSTER_TEST(arguments, codegen_test_ebpf_kernel_object(arena, duplicate, S8("probe"), &reason).code.length == 0);
    ByteSlice continuation = codegen_test_ebpf_call_fixture(arena);
    codegen_test_ebpf_call_write((u8*)continuation.pointer, 64, 2, 0x0018);
    BUSTER_TEST(arguments, codegen_test_ebpf_kernel_object(arena, continuation, S8("probe"), &reason).code.length == 0);
    // A trailing two-row immediate at the instruction limit must be refused
    // without writing beyond the verifier's fixed-size tail bitmap.
    u8* trailing = arena_allocate(arena, u8, CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS * 8);
    for (u32 pc = 0; pc < CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS; pc += 1)
    {
        memcpy(trailing + pc * 8, fixture.pointer + 64, 8);
    }
    trailing[(CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS - 1) * 8] = 0x18;
    trailing[(CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS - 1) * 8 + 1] = 0;
    BUSTER_TEST(arguments, !codegen_test_ebpf_verify_structure((ByteSlice){trailing, CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS * 8}));
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_local_calls(UnitTestArguments* arguments)
{
    UnitTestResult result = codegen_test_ebpf_kernel_loader_controls(arguments);
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    String8 sources[] = {
        S8("static unsigned long long helper(unsigned long long x) { return x * 3; } "
           "unsigned long long probe(unsigned long long a, unsigned long long b) { return helper(a) + b; }"),
        S8("static unsigned long long mix(unsigned long long x, unsigned long long y, unsigned long long z) { return x * 3 + y * 5 + z; } "
           "unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned long long first = mix(a, b, 7); "
           "unsigned long long second = mix(b, a, 11); return mix(first, second, b) + a; }"),
    };
    u64 inputs[] = {0, 2, 7, UINT64_C(0x100000005), UINT64_C(1) << 63, UINT64_MAX};
    for (u32 ssa = 0; ssa < 2; ssa += 1)
    {
        for (u32 sample = 0; sample < BUSTER_ARRAY_LENGTH(sources); sample += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            CPreprocessResult tokens = c_preprocess(arena, sources[sample], (CPreprocessOptions){0});
            CParseResult parse = c_parse(arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("ebpf-local-calls.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
                if (!artifact.success) arguments->show(arguments, S8("eBPF local-call sample {u32}, SSA {u32}: {S8}\n"), sample, ssa, artifact.error.message);
                BUSTER_TEST(arguments, artifact.success);
                if (artifact.success)
                {
                    u8* original = arena_allocate(arena, u8, artifact.bytes.length);
                    memcpy(original, artifact.bytes.pointer, artifact.bytes.length);
                    String8 reason = {0};
                    CodegenTestEbpfObject loaded = codegen_test_ebpf_kernel_object(arena, artifact.bytes, S8("probe"), &reason);
                    if (!loaded.code.length) arguments->show(arguments, S8("eBPF local-call loader sample {u32}, SSA {u32}: {S8}\n"), sample, ssa, reason);
                    BUSTER_TEST(arguments, loaded.code.length && loaded.entry_offset > 0);
                    u32 calls = 0;
                    for (u64 pc = 0; pc < loaded.code.length / 8; pc += 1) calls += loaded.code.pointer[pc * 8] == 0x85;
                    BUSTER_TEST(arguments, calls == (sample ? 3u : 1u));
                    BUSTER_TEST(arguments, memcmp(original, artifact.bytes.pointer, artifact.bytes.length) == 0);
                    u64 vm_observed = 0;
                    BUSTER_TEST(arguments, !codegen_test_ebpf_execute(artifact.bytes, 7, 2, &vm_observed));
                    for (u32 first = 0; loaded.code.length && oracle.kernel_available && first < BUSTER_ARRAY_LENGTH(inputs); first += 1)
                    {
                        for (u32 second = 0; second < BUSTER_ARRAY_LENGTH(inputs); second += 1)
                        {
                            TemporalArena execution = arena_begin_temporal(arena);
                            u64 a = inputs[first], b = inputs[second];
                            u64 expected = sample ? (a * 3 + b * 5 + 7) * 3 + (b * 3 + a * 5 + 11) * 5 + b + a : a * 3 + b;
                            u64 observed = 0;
                            CodegenTestEbpfKernel status = codegen_test_ebpf_kernel_run_entry(arena, loaded.code, loaded.entry_offset, a, b, &observed, &reason);
                            bool agreed = status == CODEGEN_TEST_EBPF_KERNEL_EXECUTED && observed == expected;
                            oracle.kernel_executions += status == CODEGEN_TEST_EBPF_KERNEL_EXECUTED;
                            if (!agreed) arguments->show(arguments, S8("eBPF local-call sample {u32}, SSA {u32}, inputs {u64}/{u64}: {S8}, observed {u64}, expected {u64}\n"),
                                                        sample, ssa, a, b, reason, observed, expected);
                            BUSTER_TEST(arguments, agreed);
                            arena_set_position(arena, execution.position);
                        }
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    if (oracle.kernel_available)
    {
        BUSTER_TEST(arguments, oracle.kernel_executions == 2 * 2 * BUSTER_ARRAY_LENGTH(inputs) * BUSTER_ARRAY_LENGTH(inputs));
        arguments->show(arguments, S8("EBPF_LOCAL_CALL_ORACLE available=1 executions={u64} frontend_forms=2\n"), oracle.kernel_executions);
    }
    else
    {
        arguments->show(arguments, S8("EBPF_LOCAL_CALL_ORACLE available=0 executions=0 reason=kernel-unavailable; loader controls checked\n"));
    }
    return result;
}

#endif
