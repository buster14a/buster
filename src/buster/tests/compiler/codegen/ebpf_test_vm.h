#pragma once

#include <buster/tests/test.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS

#if BUSTER_LINUX
#include <sys/syscall.h>
#include <unistd.h>
#endif

// Two execution oracles for complete scalar functions emitted through the
// public C -> canonical IR -> eBPF interface (issue #1305).
//
// codegen_test_ebpf_execute is a bounded test-only VM. Stack addresses are
// offsets, not host pointers. It refuses what it does not model instead of
// approximating it: codegen_test_ebpf_code accepts only an executable section
// holding exactly one function at offset zero with no relocations, so calls,
// globals and strings are refused; codegen_test_ebpf_decode refuses every
// encoding outside the scalar subset and any set reserved field. Before
// running, codegen_test_ebpf_verify_structure applies the verifier's CFG
// rules: in-range jumps, none into an LDDW, no fall off the end, and every
// instruction reachable from the entry. While running, codegen_test_ebpf_run
// requires width-aligned stack accesses and initialized register reads. It is
// still not the verifier.
//
// codegen_test_ebpf_kernel_run loads the same bytes into the Linux verifier
// and JIT when this host permits BPF_PROG_LOAD. The separate kernel_object
// loader selects a named entry in the whole .text section and resolves only
// same-section function calls; the VM retains its single-function contract.
// codegen_test_ebpf_check runs
// every available oracle, and codegen_test_ebpf_oracle_report states whether
// the kernel took part, because VM agreement alone does not prove acceptance.
// Includers that use only the VM leave the oracle helpers unreferenced.

enum
{
    CODEGEN_TEST_EBPF_STACK_BYTES = 512,
    CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS = 1 << 15,
    CODEGEN_TEST_EBPF_MAX_STEPS = 10000,
    CODEGEN_TEST_EBPF_KERNEL_PROLOGUE = 9,
};

BUSTER_GLOBAL_LOCAL u64 codegen_test_ebpf_read(u8 const* bytes, u32 size)
{
    u64 result = 0;
    for (u32 index = 0; index < size; index += 1) result |= (u64)bytes[index] << (index * 8);
    return result;
}

// The one function in the one executable section, or an empty slice for
// anything a loader would have to relocate or choose an entry point for.
BUSTER_GLOBAL_LOCAL ByteSlice codegen_test_ebpf_code(ByteSlice elf)
{
    ByteSlice result = {0};
    ByteSlice code = {0};
    u64 code_index = UINT64_MAX;
    u64 sections = 0;
    u64 stride = 0;
    u64 count = 0;
    u32 functions = 0;
    bool valid = elf.length >= 64 && memcmp(elf.pointer, "\177ELF\2\1", 6) == 0;
    if (valid)
    {
        sections = codegen_test_ebpf_read(elf.pointer + 40, 8);
        stride = codegen_test_ebpf_read(elf.pointer + 58, 2);
        count = codegen_test_ebpf_read(elf.pointer + 60, 2);
        valid = stride >= 64 && sections <= elf.length && count <= (elf.length - sections) / stride;
    }
    for (u64 index = 0; valid && index < count; index += 1)
    {
        u8 const* section = elf.pointer + sections + index * stride;
        if (codegen_test_ebpf_read(section + 4, 4) == 1 && (codegen_test_ebpf_read(section + 8, 8) & 4))
        {
            u64 offset = codegen_test_ebpf_read(section + 24, 8);
            u64 size = codegen_test_ebpf_read(section + 32, 8);
            valid = code_index == UINT64_MAX && offset <= elf.length && size <= elf.length - offset;
            code_index = index;
            if (valid) code = (ByteSlice){elf.pointer + offset, size};
        }
    }
    for (u64 index = 0; valid && index < count; index += 1)
    {
        u8 const* section = elf.pointer + sections + index * stride;
        u64 type = codegen_test_ebpf_read(section + 4, 4);
        u64 offset = codegen_test_ebpf_read(section + 24, 8);
        u64 size = codegen_test_ebpf_read(section + 32, 8);
        // SHT_RELA or SHT_REL applying to the code: a call, global or string.
        valid = !((type == 4 || type == 9) && codegen_test_ebpf_read(section + 44, 4) == code_index);
        if (valid && type == 2)
        {
            valid = codegen_test_ebpf_read(section + 56, 8) == 24 && offset <= elf.length && size <= elf.length - offset;
            for (u64 symbol = 0; valid && symbol + 24 <= size; symbol += 24)
            {
                u8 const* row = elf.pointer + offset + symbol;
                if ((row[4] & 15) == 2 && codegen_test_ebpf_read(row + 6, 2) == code_index)
                {
                    functions += 1;
                    valid = codegen_test_ebpf_read(row + 8, 8) == 0 && codegen_test_ebpf_read(row + 16, 8) == code.length;
                }
            }
        }
    }
    if (valid && functions == 1 && code.length && code.length % 8 == 0 && code.length / 8 <= CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS)
    {
        result = code;
    }
    return result;
}

// The kernel's opcode and reserved-field checks, restricted to the subset the
// VM executes: 64-bit LDDW without a pseudo source, memory accesses, unsigned
// ALU and ALU32 operations, JA, EXIT and 64-bit conditional jumps. Calls,
// JMP32, byte swaps, sign-extending and atomic forms are refused.
BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_decode(u8 const* row)
{
    u8 opcode = row[0];
    u8 destination = row[1] & 15;
    u8 source = row[1] >> 4;
    u8 operation = opcode & 0xf0;
    bool register_source = (opcode & 8) != 0;
    s16 offset = (s16)codegen_test_ebpf_read(row + 2, 2);
    s32 immediate = (s32)codegen_test_ebpf_read(row + 4, 4);
    bool result = destination <= 10 && source <= 10;
    switch (opcode & 7)
    {
    case 0: result = result && opcode == 0x18 && source == 0 && offset == 0 && destination != 10; break;
    case 1: result = result && (opcode & 0xe0) == 0x60 && immediate == 0 && destination != 10; break;
    case 2: result = result && (opcode & 0xe0) == 0x60 && source == 0; break;
    case 3: result = result && (opcode & 0xe0) == 0x60 && immediate == 0; break;
    case 4:
    case 7:
    {
        u32 width = (opcode & 7) == 4 ? 32 : 64;
        // A nonzero offset selects signed division or a sign-extending move.
        result = result && offset == 0 && destination != 10 && operation <= 0xc0;
        if (operation == 0x80)
        {
            result = result && !register_source && source == 0 && immediate == 0;
        }
        else if (register_source)
        {
            result = result && immediate == 0;
        }
        else if (operation == 0x60 || operation == 0x70 || operation == 0xc0)
        {
            result = result && source == 0 && immediate >= 0 && (u32)immediate < width;
        }
        else
        {
            result = result && source == 0 && ((operation != 0x30 && operation != 0x90) || immediate != 0);
        }
    }
    break;
    case 5:
        if (operation == 0x00)
        {
            result = result && !register_source && source == 0 && destination == 0 && immediate == 0;
        }
        else if (operation == 0x90)
        {
            result = result && !register_source && source == 0 && destination == 0 && offset == 0 && immediate == 0;
        }
        else
        {
            result = result && operation != 0x80 && operation <= 0xd0 && (register_source ? immediate == 0 : source == 0);
        }
        break;
    default: result = false; break;
    }
    return result;
}

// The verifier's check_cfg over one function. A fixed point over the
// instruction order replaces a worklist; each pass is linear.
BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_verify_function(ByteSlice code, bool local_calls)
{
    u64 tails[CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS / 64] = {0};
    u64 reached[CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS / 64] = {0};
    s64 count = (s64)(code.length / 8);
    bool valid = count != 0 && count <= CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS && code.length % 8 == 0;
    for (s64 pc = 0; valid && pc < count; pc += 1)
    {
        u8 const* row = code.pointer + pc * 8;
        if (!((tails[pc / 64] >> (pc % 64)) & 1))
        {
            bool call = local_calls && row[0] == 0x85 && row[1] == 0x10 && codegen_test_ebpf_read(row + 2, 2) == 0;
            valid = call || codegen_test_ebpf_decode(row);
            if (valid && row[0] == 0x18)
            {
                valid = pc + 1 < count && row[8] == 0 && row[9] == 0 && codegen_test_ebpf_read(row + 10, 2) == 0;
                if (valid) tails[(pc + 1) / 64] |= UINT64_C(1) << ((pc + 1) % 64);
            }
        }
    }
    reached[0] = 1;
    bool changed = valid;
    while (changed)
    {
        changed = false;
        for (s64 pc = 0; valid && pc < count; pc += 1)
        {
            if (((reached[pc / 64] >> (pc % 64)) & 1) && !((tails[pc / 64] >> (pc % 64)) & 1))
            {
                u8 opcode = code.pointer[pc * 8];
                s64 target = pc + 1 + (s16)codegen_test_ebpf_read(code.pointer + pc * 8 + 2, 2);
                // JA only jumps, EXIT has no successor and LDDW is two rows.
                s64 successors[2] = {pc + (opcode == 0x18 ? 2 : 1), target};
                u32 begin = opcode == 0x05 ? 1 : 0;
                u32 end = opcode == 0x95 ? 0 : (opcode & 7) == 5 && opcode != 0x85 ? 2 : 1;
                for (u32 index = begin; valid && index < end; index += 1)
                {
                    s64 next = successors[index];
                    valid = next >= 0 && next < count && !((tails[next / 64] >> (next % 64)) & 1);
                    if (valid && !((reached[next / 64] >> (next % 64)) & 1))
                    {
                        reached[next / 64] |= UINT64_C(1) << (next % 64);
                        changed = true;
                    }
                }
            }
        }
    }
    for (s64 pc = 0; valid && pc < count; pc += 1)
    {
        valid = ((reached[pc / 64] | tails[pc / 64]) >> (pc % 64)) & 1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_verify_structure(ByteSlice code)
{
    return codegen_test_ebpf_verify_function(code, false);
}

typedef struct CodegenTestEbpfObject CodegenTestEbpfObject;
struct CodegenTestEbpfObject
{
    ByteSlice code;
    u64 entry_offset;
};

// Test-only ELF64/BPF loader. Require a partition of .text by STT_FUNC rows,
// choose one unambiguous named entry and refuse every code relocation except
// R_BPF_64_32 to a defined function in that section. The emitted REL form uses
// immediate -1 (zero addend); RELA additionally requires an explicit zero.
// Structural checks run without BPF privileges; register and stack verification
// remains the kernel's responsibility for this multi-function execution path.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL CodegenTestEbpfObject codegen_test_ebpf_kernel_object(Arena* arena, ByteSlice elf, String8 entry,
                                                                                           String8* reason)
{
    CodegenTestEbpfObject result = {0};
    ByteSlice code = {0}, symbols = {0}, strings = {0};
    u64 sections = 0, stride = 0, count = 0;
    u64 code_index = UINT64_MAX, symbol_index = UINT64_MAX, entry_offset = UINT64_MAX;
    bool valid = elf.length >= 64 && memcmp(elf.pointer, "\177ELF\2\1\1", 7) == 0 &&
                 codegen_test_ebpf_read(elf.pointer + 16, 2) == 1 && codegen_test_ebpf_read(elf.pointer + 18, 2) == 247 &&
                 codegen_test_ebpf_read(elf.pointer + 20, 4) == 1 && codegen_test_ebpf_read(elf.pointer + 52, 2) == 64 && entry.length;
    if (valid)
    {
        sections = codegen_test_ebpf_read(elf.pointer + 40, 8);
        stride = codegen_test_ebpf_read(elf.pointer + 58, 2);
        count = codegen_test_ebpf_read(elf.pointer + 60, 2);
        valid = stride >= 64 && count && sections <= elf.length && count <= (elf.length - sections) / stride;
    }
    u64 names_index = valid ? codegen_test_ebpf_read(elf.pointer + 62, 2) : UINT64_MAX;
    ByteSlice names = {0};
    if (valid)
    {
        valid = names_index < count;
        if (valid)
        {
            u8 const* section = elf.pointer + sections + names_index * stride;
            u64 offset = codegen_test_ebpf_read(section + 24, 8), size = codegen_test_ebpf_read(section + 32, 8);
            valid = codegen_test_ebpf_read(section + 4, 4) == 3 && offset <= elf.length && size <= elf.length - offset;
            if (valid) names = (ByteSlice){elf.pointer + offset, size};
        }
    }
    for (u64 index = 0; valid && index < count; index += 1)
    {
        u8 const* section = elf.pointer + sections + index * stride;
        u64 type = codegen_test_ebpf_read(section + 4, 4), flags = codegen_test_ebpf_read(section + 8, 8);
        u64 offset = codegen_test_ebpf_read(section + 24, 8), size = codegen_test_ebpf_read(section + 32, 8);
        // SHT_NOBITS has no payload; all other referenced byte ranges exist.
        valid = type == 8 || (offset <= elf.length && size <= elf.length - offset);
        if (valid && (flags & 4))
        {
            u64 name = codegen_test_ebpf_read(section, 4);
            valid = type == 1 && code_index == UINT64_MAX && name < names.length && names.length - name >= 6 &&
                    memcmp(names.pointer + name, ".text\0", 6) == 0 && size && size % 8 == 0 &&
                    size / 8 <= CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS;
            if (valid)
            {
                code_index = index;
                code = (ByteSlice){elf.pointer + offset, size};
            }
        }
        if (valid && type == 2)
        {
            valid = symbol_index == UINT64_MAX && codegen_test_ebpf_read(section + 56, 8) == 24 && size && size % 24 == 0;
            u64 link = codegen_test_ebpf_read(section + 40, 4);
            valid = valid && link < count;
            if (valid)
            {
                u8 const* table = elf.pointer + sections + link * stride;
                u64 table_offset = codegen_test_ebpf_read(table + 24, 8), table_size = codegen_test_ebpf_read(table + 32, 8);
                valid = codegen_test_ebpf_read(table + 4, 4) == 3 && table_offset <= elf.length && table_size <= elf.length - table_offset;
                if (valid)
                {
                    symbol_index = index;
                    symbols = (ByteSlice){elf.pointer + offset, size};
                    strings = (ByteSlice){elf.pointer + table_offset, table_size};
                }
            }
        }
    }
    valid = valid && code_index != UINT64_MAX && symbol_index != UINT64_MAX;
    u64* starts = valid ? arena_allocate_zeroed(arena, u64, code.length / 8) : 0;
    u8* owners = valid ? arena_allocate_zeroed(arena, u8, code.length / 8) : 0;
    for (u64 index = 0; valid && index < symbols.length / 24; index += 1)
    {
        u8 const* symbol = symbols.pointer + index * 24;
        u64 name = codegen_test_ebpf_read(symbol, 4), name_end = name;
        valid = name < strings.length;
        while (valid && name_end < strings.length && strings.pointer[name_end]) name_end += 1;
        valid = valid && name_end < strings.length;
        if (valid && (symbol[4] & 15) == 2 && codegen_test_ebpf_read(symbol + 6, 2) == code_index)
        {
            u64 offset = codegen_test_ebpf_read(symbol + 8, 8), size = codegen_test_ebpf_read(symbol + 16, 8);
            valid = size && offset < code.length && size <= code.length - offset && offset % 8 == 0 && size % 8 == 0;
            if (valid)
            {
                starts[offset / 8] = size;
                for (u64 pc = offset / 8; valid && pc < (offset + size) / 8; pc += 1)
                {
                    valid = owners[pc] == 0;
                    if (valid) owners[pc] = 1;
                }
                if (valid && entry.length == name_end - name && memcmp(strings.pointer + name, entry.pointer, entry.length) == 0)
                {
                    valid = entry_offset == UINT64_MAX;
                    entry_offset = offset;
                }
            }
        }
    }
    valid = valid && entry_offset != UINT64_MAX;
    for (u64 pc = 0; valid && pc < code.length / 8; pc += 1) valid = owners[pc] != 0;
    u8* relocated = valid ? arena_allocate(arena, u8, code.length) : 0;
    u8* calls = valid ? arena_allocate_zeroed(arena, u8, code.length / 8) : 0;
    if (valid) memcpy(relocated, code.pointer, code.length);
    for (u64 index = 0; valid && index < count; index += 1)
    {
        u8 const* section = elf.pointer + sections + index * stride;
        u64 type = codegen_test_ebpf_read(section + 4, 4);
        if ((type == 4 || type == 9) && codegen_test_ebpf_read(section + 44, 4) == code_index)
        {
            u64 offset = codegen_test_ebpf_read(section + 24, 8), size = codegen_test_ebpf_read(section + 32, 8);
            u64 width = type == 4 ? 24 : 16;
            valid = codegen_test_ebpf_read(section + 40, 4) == symbol_index && codegen_test_ebpf_read(section + 56, 8) == width &&
                    size % width == 0;
            for (u64 row = 0; valid && row < size; row += width)
            {
                u8 const* relocation = elf.pointer + offset + row;
                u64 site = codegen_test_ebpf_read(relocation, 8), info = codegen_test_ebpf_read(relocation + 8, 8);
                u64 target_index = info >> 32;
                valid = (u32)info == 10 && target_index < symbols.length / 24 && site < code.length && site % 8 == 0 &&
                        (type != 4 || codegen_test_ebpf_read(relocation + 16, 8) == 0);
                if (valid)
                {
                    u8 const* target = symbols.pointer + target_index * 24;
                    u64 target_offset = codegen_test_ebpf_read(target + 8, 8);
                    u8* instruction = relocated + site;
                    valid = (target[4] & 15) == 2 && codegen_test_ebpf_read(target + 6, 2) == code_index &&
                            target_offset < code.length && target_offset % 8 == 0 && starts[target_offset / 8] &&
                            instruction[0] == 0x85 && instruction[1] == 0x10 && codegen_test_ebpf_read(instruction + 2, 2) == 0 &&
                            codegen_test_ebpf_read(instruction + 4, 4) == UINT32_MAX && calls[site / 8] == 0;
                    if (valid)
                    {
                        s64 immediate = (s64)(target_offset / 8) - (s64)(site / 8) - 1;
                        for (u32 byte = 0; byte < 4; byte += 1) instruction[4 + byte] = (u8)((u64)immediate >> (byte * 8));
                        calls[site / 8] = 1;
                    }
                }
            }
        }
    }
    for (u64 pc = 0; valid && pc < code.length / 8; pc += 1)
    {
        if (starts[pc]) valid = codegen_test_ebpf_verify_function((ByteSlice){relocated + pc * 8, starts[pc]}, true);
        if (relocated[pc * 8] == 0x85) valid = valid && calls[pc] != 0;
    }
    *reason = valid ? S8("prepared named entry and local calls") : S8("unsupported or malformed eBPF object, entry or call relocation");
    if (valid) result = (CodegenTestEbpfObject){.code = {relocated, code.length}, .entry_offset = entry_offset};
    return result;
}

// Execute one structurally verified function with R1 = first, R2 = second.
BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_run(ByteSlice code, u64 first, u64 second, u64* output)
{
    u8 stack[CODEGEN_TEST_EBPF_STACK_BYTES] = {0};
    u64 registers[11] = {0};
    // A two-argument call leaves R0, R3-R5 and the callee-saved R6-R9 unset.
    u32 initialized = 1u << 1 | 1u << 2 | 1u << 10;
    registers[1] = first;
    registers[2] = second;
    registers[10] = sizeof(stack);
    bool valid = codegen_test_ebpf_verify_structure(code);
    bool exited = false;
    u64 pc = 0;
    for (u32 step = 0; valid && !exited && step < CODEGEN_TEST_EBPF_MAX_STEPS; step += 1)
    {
        // Structure verification proved that pc starts an in-range instruction.
        u8 const* row = code.pointer + pc * 8;
        u8 opcode = row[0], destination = row[1] & 15, source = row[1] >> 4;
        u8 operation = opcode & 0xf0;
        u32 instruction_class = opcode & 7;
        bool register_source = (opcode & 8) != 0;
        s16 offset = (s16)codegen_test_ebpf_read(row + 2, 2);
        s32 immediate = (s32)codegen_test_ebpf_read(row + 4, 4);
        u64 right = register_source ? registers[source] : (u64)(s64)immediate;
        u64 left = registers[destination];
        bool memory = instruction_class >= 1 && instruction_class <= 3;
        u32 reads = 0;
        if (instruction_class == 4 || instruction_class == 7)
        {
            reads = (register_source ? 1u << source : 0) | (operation == 0xb0 ? 0 : 1u << destination);
        }
        else if (memory)
        {
            reads = 1u << (instruction_class == 1 ? source : destination) | (instruction_class == 3 ? 1u << source : 0);
        }
        else if (instruction_class == 5 && operation == 0x90)
        {
            reads = 1;
        }
        else if (instruction_class == 5 && operation != 0x00)
        {
            reads = 1u << destination | (register_source ? 1u << source : 0);
        }
        valid = (reads & ~initialized) == 0;
        pc += 1;
        if (valid && opcode == 0x18)
        {
            registers[destination] = (u32)immediate | (codegen_test_ebpf_read(code.pointer + pc * 8 + 4, 4) << 32);
            initialized |= 1u << destination;
            pc += 1;
        }
        else if (valid && (instruction_class == 4 || instruction_class == 7))
        {
            bool word = instruction_class == 4;
            if (word)
            {
                left = (u32)left;
                right = (u32)right;
            }
            u32 shift = (u32)right & (word ? 31u : 63u);
            switch (operation)
            {
            case 0x00: left += right; break;
            case 0x10: left -= right; break;
            case 0x20: left *= right; break;
            // The kernel defines division by zero as 0 and remainder as the dividend.
            case 0x30: left = right ? left / right : 0; break;
            case 0x40: left |= right; break;
            case 0x50: left &= right; break;
            case 0x60: left <<= shift; break;
            case 0x70: left >>= shift; break;
            case 0x80: left = 0 - left; break;
            case 0x90: left = right ? left % right : left; break;
            case 0xa0: left ^= right; break;
            case 0xb0: left = right; break;
            case 0xc0: left = word ? (u64)(s64)((s32)left >> shift) : (u64)((s64)left >> shift); break;
            default: valid = false; break;
            }
            registers[destination] = word ? (u32)left : left;
            initialized |= 1u << destination;
        }
        else if (valid && memory)
        {
            u32 width = (opcode & 0x18) == 0x18 ? 8u : (opcode & 0x18) == 0x10 ? 1u : (opcode & 0x18) == 8 ? 2u : 4u;
            bool load = instruction_class == 1;
            u64 address = registers[load ? source : destination] + (u64)(s64)offset;
            // The verifier requires every stack access to be width-aligned.
            valid = address <= sizeof(stack) - width && address % width == 0;
            if (valid && load)
            {
                registers[destination] = codegen_test_ebpf_read(stack + address, width);
                initialized |= 1u << destination;
            }
            else if (valid)
            {
                u64 value = instruction_class == 3 ? registers[source] : (u64)(s64)immediate;
                for (u32 index = 0; index < width; index += 1) stack[address + index] = (u8)(value >> (index * 8));
            }
        }
        else if (valid)
        {
            bool taken = false;
            switch (operation)
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
    }
    *output = registers[0];
    return valid && exited;
}

BUSTER_GLOBAL_LOCAL bool codegen_test_ebpf_execute(ByteSlice elf, u64 first, u64 second, u64* output)
{
    ByteSlice code = codegen_test_ebpf_code(elf);
    bool result = code.length != 0 && codegen_test_ebpf_run(code, first, second, output);
    return result;
}

typedef enum CodegenTestEbpfKernel
{
    CODEGEN_TEST_EBPF_KERNEL_UNAVAILABLE,
    CODEGEN_TEST_EBPF_KERNEL_REJECTED,
    CODEGEN_TEST_EBPF_KERNEL_EXECUTED,
} CodegenTestEbpfKernel;

#if BUSTER_LINUX && defined(SYS_bpf)
// Prefixes of union bpf_attr for BPF_PROG_LOAD and BPF_PROG_TEST_RUN. The
// kernel zero-fills the members after a shorter attribute.
typedef struct CodegenTestEbpfLoadAttribute CodegenTestEbpfLoadAttribute;
struct CodegenTestEbpfLoadAttribute
{
    u32 program_type;
    u32 instruction_count;
    u64 instructions;
    u64 license;
    u32 log_level;
    u32 log_size;
    u64 log_buffer;
    u32 kernel_version;
    u32 flags;
};

typedef struct CodegenTestEbpfRunAttribute CodegenTestEbpfRunAttribute;
struct CodegenTestEbpfRunAttribute
{
    u32 program;
    u32 return_value;
    u32 data_size_in;
    u32 data_size_out;
    u64 data_in;
    u64 data_out;
    u32 repeat;
    u32 duration;
    u32 context_size_in;
    u32 context_size_out;
    u64 context_in;
    u64 context_out;
};

BUSTER_CT_CHECK(sizeof(CodegenTestEbpfLoadAttribute) == 48);
BUSTER_CT_CHECK(sizeof(CodegenTestEbpfRunAttribute) == 64);

enum
{
    CODEGEN_TEST_EBPF_COMMAND_PROGRAM_LOAD = 5,
    CODEGEN_TEST_EBPF_COMMAND_PROGRAM_TEST_RUN = 10,
    CODEGEN_TEST_EBPF_PROGRAM_TYPE_SYSCALL = 31,
    CODEGEN_TEST_EBPF_PROGRAM_SLEEPABLE = 1 << 4,
    CODEGEN_TEST_EBPF_KERNEL_LOG_BYTES = 1 << 18,
};

// The verifier's last diagnostic line; the trailing statistics are skipped.
BUSTER_GLOBAL_LOCAL String8 codegen_test_ebpf_kernel_reason(u8 const* log, u64 capacity)
{
    u64 end = 0;
    while (end < capacity && log[end]) end += 1;
    String8 result = S8("rejected without a verifier log");
    bool searching = true;
    while (searching && end)
    {
        while (end && (log[end - 1] == '\n' || log[end - 1] == ' ')) end -= 1;
        u64 start = end;
        while (start && log[start - 1] != '\n') start -= 1;
        String8 line = {(char8*)log + start, end - start};
        searching = line.length >= 9 && memcmp(line.pointer, "processed", 9) == 0;
        if (!searching && line.length) result = line;
        end = start;
    }
    return result;
}
#endif

// Load `code` as the bpf-to-bpf callee of a sleepable BPF_PROG_TYPE_SYSCALL
// program (Linux 5.14+) that passes R1 = first and R2 = second, and read the
// full 64-bit result back through the context; a socket filter would return
// only its low 32 bits. Otherwise this is the issue #1305 loader.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL CodegenTestEbpfKernel codegen_test_ebpf_kernel_run_entry(Arena* arena, ByteSlice code, u64 entry_offset,
                                                                                              u64 first, u64 second, u64* output, String8* reason)
{
    CodegenTestEbpfKernel result = CODEGEN_TEST_EBPF_KERNEL_UNAVAILABLE;
    *reason = S8("BPF_PROG_LOAD is not available on this platform");
#if BUSTER_LINUX && defined(SYS_bpf)
    if (!code.length || code.length % 8 || code.length / 8 > CODEGEN_TEST_EBPF_MAX_INSTRUCTIONS ||
        entry_offset >= code.length || entry_offset % 8)
    {
        result = CODEGEN_TEST_EBPF_KERNEL_REJECTED;
        *reason = S8("invalid code section or entry offset");
    }
    else
    {
        // r6 = r1; r1 = first ll; r2 = second ll; call +3;
        // *(u64 *)(r6 + 0) = r0; r0 = 0; exit
        u8 prologue[CODEGEN_TEST_EBPF_KERNEL_PROLOGUE * 8] = {
            0xbf, 0x16, 0, 0, 0, 0, 0, 0,
            0x18, 0x01, 0, 0, 0, 0, 0, 0,
            0x00, 0x00, 0, 0, 0, 0, 0, 0,
            0x18, 0x02, 0, 0, 0, 0, 0, 0,
            0x00, 0x00, 0, 0, 0, 0, 0, 0,
            0x85, 0x10, 0, 0, 3, 0, 0, 0,
            0x7b, 0x06, 0, 0, 0, 0, 0, 0,
            0xb7, 0x00, 0, 0, 0, 0, 0, 0,
            0x95, 0x00, 0, 0, 0, 0, 0, 0,
        };
        u64 inputs[2] = {first, second};
        u32 entry_call = (u32)(entry_offset / 8) + CODEGEN_TEST_EBPF_KERNEL_PROLOGUE - 6;
        for (u32 byte = 0; byte < 4; byte += 1) prologue[5 * 8 + 4 + byte] = (u8)(entry_call >> (byte * 8));
        for (u32 index = 0; index < 2; index += 1)
        {
            for (u32 byte = 0; byte < 4; byte += 1)
            {
                prologue[8 + index * 16 + 4 + byte] = (u8)(inputs[index] >> (byte * 8));
                prologue[16 + index * 16 + 4 + byte] = (u8)(inputs[index] >> (32 + byte * 8));
            }
        }
        u64 count = CODEGEN_TEST_EBPF_KERNEL_PROLOGUE + code.length / 8;
        u8* program = arena_allocate(arena, u8, count * 8);
        memcpy(program, prologue, sizeof(prologue));
        memcpy(program + sizeof(prologue), code.pointer, code.length);
        static char8 const license[] = "GPL";
        CodegenTestEbpfLoadAttribute load = {
            .program_type = CODEGEN_TEST_EBPF_PROGRAM_TYPE_SYSCALL,
            .instruction_count = (u32)count,
            .instructions = (u64)(uintptr_t)program,
            .license = (u64)(uintptr_t)license,
            .flags = CODEGEN_TEST_EBPF_PROGRAM_SLEEPABLE,
        };
        long descriptor = syscall(SYS_bpf, CODEGEN_TEST_EBPF_COMMAND_PROGRAM_LOAD, &load, sizeof(load));
        if (descriptor < 0)
        {
            // Load again only to explain the rejection.
            u8* log = arena_allocate_zeroed(arena, u8, CODEGEN_TEST_EBPF_KERNEL_LOG_BYTES);
            load.log_level = 1;
            load.log_size = CODEGEN_TEST_EBPF_KERNEL_LOG_BYTES;
            load.log_buffer = (u64)(uintptr_t)log;
            descriptor = syscall(SYS_bpf, CODEGEN_TEST_EBPF_COMMAND_PROGRAM_LOAD, &load, sizeof(load));
            *reason = codegen_test_ebpf_kernel_reason(log, CODEGEN_TEST_EBPF_KERNEL_LOG_BYTES);
            result = CODEGEN_TEST_EBPF_KERNEL_REJECTED;
        }
        if (descriptor >= 0)
        {
            u64 context = 0;
            CodegenTestEbpfRunAttribute run = {
                .program = (u32)descriptor,
                .context_size_in = sizeof(context),
                .context_in = (u64)(uintptr_t)&context,
            };
            long status = syscall(SYS_bpf, CODEGEN_TEST_EBPF_COMMAND_PROGRAM_TEST_RUN, &run, sizeof(run));
            close((int)descriptor);
            result = status == 0 && result != CODEGEN_TEST_EBPF_KERNEL_REJECTED ? CODEGEN_TEST_EBPF_KERNEL_EXECUTED
                                                                                 : CODEGEN_TEST_EBPF_KERNEL_REJECTED;
            *reason = result == CODEGEN_TEST_EBPF_KERNEL_EXECUTED ? S8("executed") : *reason;
            *output = context;
        }
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(code);
    BUSTER_UNUSED(entry_offset);
    BUSTER_UNUSED(first);
    BUSTER_UNUSED(second);
    BUSTER_UNUSED(output);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL CodegenTestEbpfKernel codegen_test_ebpf_kernel_run(Arena* arena, ByteSlice code, u64 first, u64 second, u64* output,
                                                                     String8* reason)
{
    return codegen_test_ebpf_kernel_run_entry(arena, code, 0, first, second, output, reason);
}

typedef struct CodegenTestEbpfOracle CodegenTestEbpfOracle;
struct CodegenTestEbpfOracle
{
    u64 kernel_executions;
    bool kernel_available;
};

// The kernel oracle is available when a known program loads and returns 7.
// Unprivileged users, seccomp filters and pre-5.14 kernels all fail here.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL CodegenTestEbpfOracle codegen_test_ebpf_oracle(Arena* arena)
{
    u8 probe[] = {0xb7, 0x00, 0, 0, 7, 0, 0, 0, 0x95, 0x00, 0, 0, 0, 0, 0, 0};
    u64 observed = 0;
    String8 reason = {0};
    TemporalArena temporary = scratch_begin(&arena, 1);
    CodegenTestEbpfKernel status = codegen_test_ebpf_kernel_run(temporary.arena, (ByteSlice){probe, sizeof(probe)}, 0, 0, &observed, &reason);
    scratch_end(temporary);
    CodegenTestEbpfOracle result = {.kernel_available = status == CODEGEN_TEST_EBPF_KERNEL_EXECUTED && observed == 7};
    return result;
}

// True when every available oracle executed `elf` and returned `expected`.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL bool codegen_test_ebpf_check(UnitTestArguments* arguments, CodegenTestEbpfOracle* oracle, ByteSlice elf, u64 first, u64 second,
                                                 u64 expected)
{
    u64 observed = 0;
    bool ran = codegen_test_ebpf_execute(elf, first, second, &observed);
    bool result = ran && observed == expected;
    if (!result)
    {
        arguments->show(arguments, S8("eBPF VM: inputs {u64}/{u64}, ran {u32}, observed {u64}, expected {u64}\n"), first, second, (u32)ran,
                        observed, expected);
    }
    if (oracle->kernel_available)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        u64 kernel_observed = 0;
        String8 reason = {0};
        CodegenTestEbpfKernel status =
            codegen_test_ebpf_kernel_run(temporary.arena, codegen_test_ebpf_code(elf), first, second, &kernel_observed, &reason);
        bool agreed = status == CODEGEN_TEST_EBPF_KERNEL_EXECUTED && kernel_observed == expected;
        oracle->kernel_executions += 1;
        if (!agreed)
        {
            arguments->show(arguments, S8("eBPF kernel: inputs {u64}/{u64}, {S8}, observed {u64}, expected {u64}\n"), first, second, reason,
                            kernel_observed, expected);
        }
        result = result && agreed;
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL void codegen_test_ebpf_oracle_report(UnitTestArguments* arguments, CodegenTestEbpfOracle oracle, String8 check)
{
    if (oracle.kernel_available)
    {
        arguments->show(arguments, S8("eBPF kernel verifier/JIT for {S8}: available, {u64} executions compared\n"), check,
                        oracle.kernel_executions);
    }
    else
    {
        arguments->show(arguments, S8("eBPF kernel verifier/JIT for {S8}: unavailable, VM only\n"), check);
    }
}

#endif
