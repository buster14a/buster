#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>

#if BUSTER_INCLUDE_TESTS

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_scalars(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // Exercise complete emitted functions, including ABI argument capture,
    // stack spills/reloads, predicate branches, signed comparison and widening.
    String8 ebpf_sources[] = {
        S8("long probe(long a, long b) { return a == b; }"),
        S8("long probe(long a, long b) { return a != b; }"),
        S8("long probe(long a, long b) { return a < b; }"),
        S8("long probe(long a, long b) { return a <= b; }"),
        S8("long probe(long a, long b) { return a > b; }"),
        S8("long probe(long a, long b) { return a >= b; }"),
        S8("long probe(unsigned long a, unsigned long b) { return a < b; }"),
        S8("long probe(unsigned long a, unsigned long b) { return a <= b; }"),
        S8("long probe(unsigned long a, unsigned long b) { return a > b; }"),
        S8("long probe(unsigned long a, unsigned long b) { return a >= b; }"),
        S8("long probe(void* a, void* b) { return a == b; }"),
        S8("long probe(void* a, void* b) { return a != b; }"),
        S8("long probe(long a, long b) { (void)b; return !a; }"),
        S8("long probe(int a, int b) { (void)b; return ~a; }"),
        S8("long probe(signed char a, signed char b) { (void)b; return ~a; }"),
        S8("unsigned long probe(unsigned int a, unsigned int b) { (void)b; return ~a; }"),
        S8("unsigned long probe(unsigned long a, unsigned long b) { return a * b; }"),
        S8("unsigned long probe(unsigned int a, unsigned int b) { return a * b; }"),
    };
    u64 ebpf_values[] = {0, 1, 127, 128, 0x7fffffff, 0x80000000, UINT64_MAX, (u64)1 << 63};
    Target ebpf_target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(ebpf_sources); fixture += 1)
    {
        u64 mark = arguments->arena->position;
        CPreprocessResult tokens = c_preprocess(arguments->arena, ebpf_sources[fixture], (CPreprocessOptions){0});
        CParseResult parse = c_parse(arguments->arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arguments->arena, S8("ebpf-scalar.c"), tokens, parse, ebpf_target);
        BUSTER_TEST(arguments, lowered.program != 0 && lowered.diagnostic_count == 0);
        if (lowered.program && lowered.diagnostic_count == 0)
        {
            EbpfArtifact artifact = ebpf_emit_program(arguments->arena, lowered.program);
            BUSTER_TEST(arguments, artifact.success);
            if (artifact.success)
            {
                for (u32 left_index = 0; left_index < BUSTER_ARRAY_LENGTH(ebpf_values); left_index += 1)
                {
                    for (u32 right_index = 0; right_index < BUSTER_ARRAY_LENGTH(ebpf_values); right_index += 1)
                    {
                        u64 first = ebpf_values[left_index], second = ebpf_values[right_index];
                        u64 expected = 0;
                        switch (fixture)
                        {
                        case 0: expected = first == second; break;
                        case 1: expected = first != second; break;
                        case 2: expected = (s64)first < (s64)second; break;
                        case 3: expected = (s64)first <= (s64)second; break;
                        case 4: expected = (s64)first > (s64)second; break;
                        case 5: expected = (s64)first >= (s64)second; break;
                        case 6: expected = first < second; break;
                        case 7: expected = first <= second; break;
                        case 8: expected = first > second; break;
                        case 9: expected = first >= second; break;
                        case 10: expected = first == second; break;
                        case 11: expected = first != second; break;
                        case 12: expected = first == 0; break;
                        case 13: expected = (u64)(s64)~(s32)first; break;
                        case 14: expected = (u64)(s64)~(s32)(s8)first; break;
                        case 15: expected = (u64)~(u32)first; break;
                        case 16: expected = first * second; break;
                        case 17: expected = (u64)((u32)first * (u32)second); break;
                        default: break;
                        }
                        u64 actual = 0;
                        bool ran = codegen_test_ebpf_execute(artifact.bytes, first, second, &actual);
                        if (!ran || actual != expected) arguments->show(arguments, S8("eBPF fixture {u32}, operands {u64}/{u64}, actual {u64}, expected {u64}\n"), fixture, first, second, actual, expected);
                        BUSTER_TEST(arguments, ran && actual == expected);
                    }
                }
            }
        }
        arena_set_position(arguments->arena, mark);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_local_aggregates(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    ByteSlice fixture = file_read(arguments->arena, S8("tests/basic_c_local_aggregate_copy.c"), (FileReadOptions){0});
    BUSTER_TEST(arguments, fixture.length != 0);
    u64 values[] = {0, 1, 127, 128, UINT64_MAX, UINT64_C(1) << 63, (UINT64_C(1) << 63) - 1};
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    for (u32 test_case = 0; fixture.length && test_case < 5; test_case += 1)
    {
        for (u32 ssa = 0; ssa < 2; ssa += 1)
        {
            u64 mark = arguments->arena->position;
            String8 source = string_format(arguments->arena, S8("#define LOCAL_AGGREGATE_CASE {u32}\n{S8}"), test_case,
                                           (String8){.pointer = (char8*)fixture.pointer, .length = fixture.length});
            CPreprocessResult tokens = c_preprocess(arguments->arena, source, (CPreprocessOptions){0});
            CParseResult parse = c_parse(arguments->arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arguments->arena, S8("local-aggregate.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program != 0 && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, lowered.program->modules).error == IR_VALIDATION_NONE);
                EbpfArtifact artifact = ebpf_emit_program(arguments->arena, lowered.program);
                if (!artifact.success)
                {
                    arguments->show(arguments, S8("eBPF aggregate case {u32}, SSA {u32}: {S8}\n"), test_case, ssa, artifact.error.message);
                }
                BUSTER_TEST(arguments, artifact.success);
                if (artifact.success)
                {
                    BUSTER_TEST(arguments, artifact.stats.max_stack_bytes > 0 && artifact.stats.max_stack_bytes <= 512);
                    EbpfArtifact repeated = ebpf_emit_program(arguments->arena, lowered.program);
                    BUSTER_TEST(arguments, repeated.success && repeated.bytes.length == artifact.bytes.length &&
                                           memory_compare(repeated.bytes.pointer, artifact.bytes.pointer, artifact.bytes.length));
                    for (u32 left = 0; left < BUSTER_ARRAY_LENGTH(values); left += 1)
                    {
                        for (u32 right = 0; right < BUSTER_ARRAY_LENGTH(values); right += 1)
                        {
                            u64 x = values[left], y = values[right];
                            u64 expected;
                            switch (test_case)
                            {
                            case 0: expected = x + y; break;
                            case 1: expected = x + 2 * y + 1; break;
                            case 2: expected = x + y + 12; break;
                            case 3: expected = x + y + 16; break;
                            default: expected = x; break;
                            }
                            u64 observed = 0;
                            bool ran = codegen_test_ebpf_execute(artifact.bytes, x, y, &observed);
                            if (!ran || observed != expected)
                            {
                                arguments->show(arguments, S8("eBPF aggregate case {u32}, SSA {u32}, inputs {u64}/{u64}, observed {u64}, expected {u64}\n"),
                                                test_case, ssa, x, y, observed, expected);
                            }
                            BUSTER_TEST(arguments, ran && observed == expected);
                        }
                    }
                }
            }
            arena_set_position(arguments->arena, mark);
        }
    }
    String8 refused[] = {
        S8("struct R { unsigned long long x[2]; }; unsigned long long probe(struct R r) { return r.x[0]; }"),
        S8("struct R { unsigned long long x[2]; }; struct R probe(unsigned long long x) { struct R r = {{x, 1}}; return r; }"),
        S8("unsigned long long probe(unsigned long long x) { struct R { unsigned char x[513]; }; struct R a = {{0}}; struct R b = a; return b.x[x & 255]; }"),
        S8("unsigned long long probe(unsigned long long x) { struct __attribute__((aligned(16))) R { unsigned long long x[2]; }; struct R a = {{x, 1}}; struct R b = a; return b.x[0]; }"),
    };
    for (u32 test_case = 0; test_case < BUSTER_ARRAY_LENGTH(refused); test_case += 1)
    {
        u64 mark = arguments->arena->position;
        CPreprocessResult tokens = c_preprocess(arguments->arena, refused[test_case], (CPreprocessOptions){0});
        CParseResult parse = c_parse(arguments->arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arguments->arena, S8("local-aggregate-refusal.c"), tokens, parse, target);
        BUSTER_TEST(arguments, lowered.program != 0 && lowered.diagnostic_count == 0);
        if (lowered.program && lowered.diagnostic_count == 0)
        {
            EbpfArtifact artifact = ebpf_emit_program(arguments->arena, lowered.program);
            BUSTER_TEST(arguments, !artifact.success && artifact.error.message.length != 0 && artifact.bytes.length == 0);
        }
        arena_set_position(arguments->arena, mark);
    }
    return result;
}

#endif
