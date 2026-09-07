#pragma once

#include <buster/tests/test.h>
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
            case 0x20: left *= right; break;
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

#endif
