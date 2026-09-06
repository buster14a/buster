#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS

// A bounded test-only VM for complete scalar functions emitted through the
// public C -> canonical IR -> eBPF interface. Stack addresses are offsets, not
// host pointers; unsupported instructions fail rather than being approximated.
BUSTER_GLOBAL_LOCAL u64 codegen_test_ebpf_read(u8 const* bytes, u32 size)
{
    u64 result = 0;
    for (u32 index = 0; index < size; index += 1) result |= (u64)bytes[index] << (index * 8);
    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_execute(ByteSlice elf, u64 first, u64 second, u64* output)
{
    ByteSlice code = {0};
    bool valid = elf.length >= 64 && memcmp(elf.pointer, "\177ELF\2\1", 6) == 0;
    if (valid)
    {
        u64 sections = codegen_test_ebpf_read(elf.pointer + 40, 8);
        u64 stride = codegen_test_ebpf_read(elf.pointer + 58, 2);
        u64 count = codegen_test_ebpf_read(elf.pointer + 60, 2);
        valid = stride >= 64 && sections <= elf.length && count <= (elf.length - sections) / stride;
        for (u64 index = 0; valid && index < count; index += 1)
        {
            u8 const* section = elf.pointer + sections + index * stride;
            if (codegen_test_ebpf_read(section + 4, 4) == 1 && (codegen_test_ebpf_read(section + 8, 8) & 4))
            {
                u64 offset = codegen_test_ebpf_read(section + 24, 8);
                u64 size = codegen_test_ebpf_read(section + 32, 8);
                valid = !code.length && offset <= elf.length && size <= elf.length - offset;
                if (valid) code = (ByteSlice){elf.pointer + offset, size};
            }
        }
    }
    valid &= code.length && code.length % 8 == 0;
    u8 stack[512] = {0};
    u64 registers[11] = {0};
    registers[1] = first;
    registers[2] = second;
    registers[10] = sizeof(stack);
    u64 pc = 0;
    bool exited = false;
    for (u32 step = 0; valid && !exited && step < 10000; step += 1)
    {
        valid = pc < code.length / 8;
        if (!valid) break;
        u8 const* row = code.pointer + pc * 8;
        u8 opcode = row[0], destination = row[1] & 15, source = row[1] >> 4;
        valid = destination < 11 && source < 11;
        if (!valid) break;
        s16 offset = (s16)codegen_test_ebpf_read(row + 2, 2);
        s32 immediate = (s32)codegen_test_ebpf_read(row + 4, 4);
        u64 right = opcode & 8 ? registers[source] : (u64)(s64)immediate;
        u64 left = registers[destination];
        pc += 1;
        if (opcode == 0x18)
        {
            valid = pc < code.length / 8;
            if (valid)
            {
                registers[destination] = (u32)immediate | (codegen_test_ebpf_read(code.pointer + pc * 8 + 4, 4) << 32);
                pc += 1;
            }
        }
        else if ((opcode & 7) == 7 || (opcode & 7) == 4)
        {
            bool word = (opcode & 7) == 4;
            if (word) { left = (u32)left; right = (u32)right; }
            u32 shift = (u32)right & (word ? 31u : 63u);
            switch (opcode & 0xf0)
            {
            case 0x00: left += right; break;
            case 0x10: left -= right; break;
            case 0x40: left |= right; break;
            case 0x50: left &= right; break;
            case 0x60: left <<= shift; break;
            case 0x70: left >>= shift; break;
            case 0x80: left = 0 - left; break;
            case 0xa0: left ^= right; break;
            case 0xb0: left = right; break;
            case 0xc0: left = word ? (u64)(s64)((s32)left >> shift) : (u64)((s64)left >> shift); break;
            default: valid = false; break;
            }
            registers[destination] = word ? (u32)left : left;
        }
        else if (((opcode & 7) == 1 || (opcode & 7) == 2 || (opcode & 7) == 3) && (opcode & 0xe0) == 0x60)
        {
            u32 width = (opcode & 0x18) == 0x18 ? 8u : (opcode & 0x18) == 0x10 ? 1u : (opcode & 0x18) == 8 ? 2u : 4u;
            bool load = (opcode & 7) == 1;
            u64 address = registers[load ? source : destination] + (u64)(s64)offset;
            valid = address <= sizeof(stack) - width;
            if (valid)
            {
                if (load) registers[destination] = codegen_test_ebpf_read(stack + address, width);
                else
                {
                    u64 value = (opcode & 7) == 3 ? registers[source] : (u64)(s64)immediate;
                    for (u32 index = 0; index < width; index += 1) stack[address + index] = (u8)(value >> (index * 8));
                }
            }
        }
        else if ((opcode & 7) == 5)
        {
            bool taken = false;
            switch (opcode & 0xf0)
            {
            case 0x00: taken = true; break;
            case 0x10: taken = left == right; break;
            case 0x20: taken = left > right; break;
            case 0x30: taken = left >= right; break;
            case 0x40: taken = (left & right) != 0; break;
            case 0x50: taken = left != right; break;
            case 0x60: taken = (s64)left > (s64)right; break;
            case 0x70: taken = (s64)left >= (s64)right; break;
            case 0x90: exited = true; break;
            case 0xa0: taken = left < right; break;
            case 0xb0: taken = left <= right; break;
            case 0xc0: taken = (s64)left < (s64)right; break;
            case 0xd0: taken = (s64)left <= (s64)right; break;
            default: valid = false; break;
            }
            if (taken) pc += (u64)(s64)offset;
        }
        else valid = false;
    }
    *output = registers[0];
    return valid && exited;
}

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
