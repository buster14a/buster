#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/string.h>
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
        arguments->arena->position = mark;
    }
    return result;
}

#endif
