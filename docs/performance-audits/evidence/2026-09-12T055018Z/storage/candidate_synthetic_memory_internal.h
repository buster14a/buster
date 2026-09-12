#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/ebpf/ebpf_internal.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/tests/compiler/codegen/ebpf_test_vm.h>

#if BUSTER_INCLUDE_TESTS

// These readers inspect serialized ELF fields independently of the emitter's
// context and key map. All table slices are bounded before decoding entries.
BUSTER_GLOBAL_LOCAL ByteSlice codegen_test_ebpf_section(ByteSlice elf, u32 index)
{
    ByteSlice result = {0};
    if (elf.length >= 64 && memcmp(elf.pointer, "\177ELF\2\1", 6) == 0)
    {
        u64 offset = codegen_test_ebpf_read(elf.pointer + 40, 8);
        u64 stride = codegen_test_ebpf_read(elf.pointer + 58, 2);
        u64 count = codegen_test_ebpf_read(elf.pointer + 60, 2);
        if (stride >= 64 && index < count && offset <= elf.length && count <= (elf.length - offset) / stride)
            result = (ByteSlice){elf.pointer + offset + index * stride, 64};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ByteSlice codegen_test_ebpf_section_data(ByteSlice elf, u32 index)
{
    ByteSlice result = {0};
    ByteSlice header = codegen_test_ebpf_section(elf, index);
    if (header.length)
    {
        u64 offset = codegen_test_ebpf_read(header.pointer + 24, 8);
        u64 size = codegen_test_ebpf_read(header.pointer + 32, 8);
        if (offset <= elf.length && size <= elf.length - offset) result = (ByteSlice){elf.pointer + offset, size};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 codegen_test_ebpf_name(ByteSlice strings, u32 offset)
{
    String8 result = {0};
    u64 end = offset;
    while (end < strings.length && strings.pointer[end]) end += 1;
    if (end < strings.length) result = (String8){(char8*)strings.pointer + offset, end - offset};
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_string_symbols(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    enum { string_count = 65, local_function_count = string_count / 2 };
    IrProgram program = ir_program_initialize(arena, 1, 4, string_count + 2, 0);
    IrTypeId byte = ir_program_add_type(&program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 8,
        .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    IrTypeId boolean = ir_program_add_type(&program, (IrType){.kind = IR_TYPE_BOOLEAN, .bit_width = 1,
        .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    IrTypeId pointer = ir_program_add_type(&program, (IrType){.kind = IR_TYPE_POINTER, .element_type = byte,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrTypeId signature = ir_program_add_type(&program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = boolean,
        .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    // Empty and entirely filtered tables exercise initialization at counts 0/1.
    EbpfArtifact empty = ebpf_emit_program(arena, &program);
    BUSTER_TEST(arguments, empty.success);
    ir_program_add_symbol(&program, (IrSymbol){.name = S8("filtered_type"), .type = byte, .kind = IR_SYMBOL_TYPE});
    EbpfArtifact filtered = ebpf_emit_program(arena, &program);
    BUSTER_TEST(arguments, filtered.success && filtered.bytes.length == empty.bytes.length &&
        memory_compare(filtered.bytes.pointer, empty.bytes.pointer, empty.bytes.length));
    ir_program_add_symbol(&program, (IrSymbol){.name = S8("bpf_helper#7"), .type = signature,
        .kind = IR_SYMBOL_FUNCTION, .linkage = IR_LINKAGE_IMPORT});
    // C currently lowers string literals as globals. Construct canonical string
    // instructions explicitly so this test proves the synthetic-key path runs.
    for (u32 index = 0; index < string_count; index += 1)
    {
        String8 name = string_format(arena, S8("string_{u32}"), index);
        IrSymbolId symbol = ir_program_add_symbol(&program, (IrSymbol){.name = name, .type = signature,
            .kind = IR_SYMBOL_FUNCTION, .linkage = index & 1 ? IR_LINKAGE_INTERNAL : IR_LINKAGE_EXTERNAL, .is_definition = true});
        IrFunction* function = ir_module_add_function(arena, program.modules, (IrFunction){.name = name, .symbol = symbol,
            .canonical_type = signature, .entry = {.value = 0}, .state = IR_FUNCTION_LOWERED});
        IrBlock* block = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
            .last_instruction = IR_INSTRUCTION_ID_INVALID, .terminated = true, .sealed = true});
        IrValueId value = ir_function_add_value(arena, function, (IrValue){.canonical_type = pointer,
            .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
        IrInstructionId constant = ir_function_add_instruction(arena, function, (IrInstruction){.opcode = IR_OPCODE_CONSTANT_STRING,
            .canonical_type = pointer, .result = value, .next = IR_INSTRUCTION_ID_INVALID}, (IrSourceRange){0});
        String8 literal = S8("abcd");
        literal.length = index % 4 + 1;
        ir_instruction_extra_ensure(arena, function, constant)->literal = literal;
        IrValueId comparison = ir_function_add_value(arena, function, (IrValue){.canonical_type = boolean,
            .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
        IrValueId* compared = arena_allocate(arena, IrValueId, 2);
        compared[0] = value;
        compared[1] = value;
        IrInstructionId compare = ir_function_add_instruction(arena, function, (IrInstruction){.opcode = IR_OPCODE_BINARY,
            .binary_operation = IR_BINARY_POINTER_EQUAL, .canonical_type = boolean, .result = comparison,
            .operands = compared, .operand_count = 2, .next = IR_INSTRUCTION_ID_INVALID}, (IrSourceRange){0});
        IrValueId* returned = arena_allocate(arena, IrValueId, 1);
        returned[0] = comparison;
        IrInstructionId exit = ir_function_add_instruction(arena, function, (IrInstruction){.opcode = IR_OPCODE_RETURN,
            .canonical_type = boolean, .result = IR_VALUE_ID_INVALID, .operands = returned, .operand_count = 1,
            .next = IR_INSTRUCTION_ID_INVALID}, (IrSourceRange){0});
        function->values[value.value].definition = constant;
        function->values[comparison.value].definition = compare;
        function->instructions[constant.value].next = compare;
        function->instructions[compare.value].next = exit;
        block->first_instruction = constant;
        block->last_instruction = exit;
    }
    BUSTER_TEST(arguments, ir_validate_canonical_module(&program, program.modules).error == IR_VALIDATION_NONE);
    u64 emit_before = arena->position;
    EbpfArtifact artifact = ebpf_emit_program(arena, &program);
    printf("synthetic_emit_arena_bytes=%llu object_bytes=%llu\n", (unsigned long long)(arena->position - emit_before), (unsigned long long)artifact.bytes.length);
    if (!artifact.success) arguments->show(arguments, S8("eBPF string symbols: {S8}\n"), artifact.error.message);
    BUSTER_TEST(arguments, artifact.success);
    if (artifact.success)
    {
        EbpfArtifact repeated = ebpf_emit_program(arena, &program);
        BUSTER_TEST(arguments, repeated.success && repeated.bytes.length == artifact.bytes.length &&
            memory_compare(repeated.bytes.pointer, artifact.bytes.pointer, artifact.bytes.length));
        ByteSlice elf = artifact.bytes;
        u32 section_count = (u32)codegen_test_ebpf_read(elf.pointer + 60, 2);
        ByteSlice symbols = {0}, strings = {0}, relocations = {0};
        u32 first_global = 0;
        for (u32 index = 1; index < section_count; index += 1)
        {
            ByteSlice header = codegen_test_ebpf_section(elf, index);
            BUSTER_TEST(arguments, header.length != 0);
            if (!header.length) continue;
            u32 type = (u32)codegen_test_ebpf_read(header.pointer + 4, 4);
            if (type == 2)
            {
                BUSTER_TEST(arguments, symbols.length == 0 && codegen_test_ebpf_read(header.pointer + 56, 8) == 24);
                symbols = codegen_test_ebpf_section_data(elf, index);
                strings = codegen_test_ebpf_section_data(elf, (u32)codegen_test_ebpf_read(header.pointer + 40, 4));
                first_global = (u32)codegen_test_ebpf_read(header.pointer + 44, 4);
            }
            if (type == 9)
            {
                BUSTER_TEST(arguments, relocations.length == 0 && codegen_test_ebpf_read(header.pointer + 56, 8) == 16);
                relocations = codegen_test_ebpf_section_data(elf, index);
            }
        }
        // Null + one local SECTION symbol for every non-null section precede
        // named records. Local functions, strings, global functions follow.
        BUSTER_TEST(arguments, symbols.length == (section_count + 2 * string_count) * 24);
        BUSTER_TEST(arguments, first_global == section_count + local_function_count + string_count);
        BUSTER_TEST(arguments, relocations.length == string_count * 2 * 16);
        u64 rodata_offset = 0;
        for (u32 index = 0; index < string_count && symbols.length == (section_count + 2 * string_count) * 24; index += 1)
        {
            u32 function_index = index & 1 ? section_count + index / 2 : first_global + index / 2;
            u8 const* function = symbols.pointer + function_index * 24;
            String8 name = codegen_test_ebpf_name(strings, (u32)codegen_test_ebpf_read(function, 4));
            BUSTER_TEST(arguments, string_equal(name, string_format(arena, S8("string_{u32}"), index)));
            BUSTER_TEST(arguments, function[4] == (index & 1 ? 2 : 18));
            u32 string_index = section_count + local_function_count + index;
            u8 const* string = symbols.pointer + string_index * 24;
            BUSTER_TEST(arguments, string_equal(codegen_test_ebpf_name(strings, (u32)codegen_test_ebpf_read(string, 4)), S8(".L.str")));
            BUSTER_TEST(arguments, string[4] == 1 && codegen_test_ebpf_read(string + 8, 8) == rodata_offset &&
                codegen_test_ebpf_read(string + 16, 8) == index % 4 + 2);
            ByteSlice rodata = codegen_test_ebpf_section_data(elf, (u32)codegen_test_ebpf_read(string + 6, 2));
            u32 length = index % 4 + 1;
            bool bytes_match = rodata_offset <= rodata.length && length + 1 <= rodata.length - rodata_offset;
            if (bytes_match) bytes_match = memory_compare(rodata.pointer + rodata_offset, "abcd", length) && rodata.pointer[rodata_offset + length] == 0;
            BUSTER_TEST(arguments, bytes_match);
            rodata_offset += length + 1;
            for (u32 reference = 0; reference < 2 && relocations.length == string_count * 2 * 16; reference += 1)
            {
                u8 const* relocation = relocations.pointer + (index * 2 + reference) * 16;
                BUSTER_TEST(arguments, codegen_test_ebpf_read(relocation + 8, 8) == ((u64)string_index << 32 | 1));
                u64 offset = codegen_test_ebpf_read(relocation, 8);
                u64 begin = codegen_test_ebpf_read(function + 8, 8), size = codegen_test_ebpf_read(function + 16, 8);
                BUSTER_TEST(arguments, offset >= begin && offset - begin < size && offset % 8 == 0);
            }
        }
        for (u64 index = 0; index + 24 <= symbols.length; index += 24)
        {
            String8 name = codegen_test_ebpf_name(strings, (u32)codegen_test_ebpf_read(symbols.pointer + index, 4));
            BUSTER_TEST(arguments, !string_equal(name, S8("filtered_type")) && !string_equal(name, S8("bpf_helper#7")));
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_global_symbols(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = S8("long helper(void) __asm__(\"bpf_helper#7\"); "
        "long exported = 5; static long hidden = 3; "
        "long *exported_address = &exported; static long *hidden_address = &hidden; "
        "static long sub(void) { return hidden + exported; } "
        "long entry(void) { return sub() + helper() + *hidden_address + *exported_address; }");
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
    CParseResult parsed = c_parse(arena, tokens);
    CIRLowerResult lowered = c_lower_to_ir(arena, S8("ebpf-symbols.c"), tokens, parsed, target);
    BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
    if (lowered.program && lowered.diagnostic_count == 0)
    {
        EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
        BUSTER_TEST(arguments, artifact.success);
        if (artifact.success)
        {
            ByteSlice elf = artifact.bytes;
            u32 section_count = (u32)codegen_test_ebpf_read(elf.pointer + 60, 2);
            ByteSlice symbols = {0}, strings = {0};
            for (u32 section = 1; section < section_count; section += 1)
            {
                ByteSlice header = codegen_test_ebpf_section(elf, section);
                if (header.length && codegen_test_ebpf_read(header.pointer + 4, 4) == 2)
                {
                    symbols = codegen_test_ebpf_section_data(elf, section);
                    strings = codegen_test_ebpf_section_data(elf, (u32)codegen_test_ebpf_read(header.pointer + 40, 4));
                }
            }
            String8 expected[] = {S8("hidden"), S8("hidden_address"), S8("sub"), S8("exported"), S8("exported_address"), S8("entry")};
            BUSTER_TEST(arguments, symbols.length == (section_count + BUSTER_ARRAY_LENGTH(expected)) * 24);
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected) && symbols.length == (section_count + BUSTER_ARRAY_LENGTH(expected)) * 24; index += 1)
            {
                u8 const* symbol = symbols.pointer + (section_count + index) * 24;
                BUSTER_TEST(arguments, string_equal(codegen_test_ebpf_name(strings, (u32)codegen_test_ebpf_read(symbol, 4)), expected[index]));
            }
            u32 data_references[2] = {0}, address_references[4] = {0}, calls = 0, helpers = 0;
            for (u32 section = 1; section < section_count; section += 1)
            {
                ByteSlice header = codegen_test_ebpf_section(elf, section);
                if (!header.length) continue;
                ByteSlice data = codegen_test_ebpf_section_data(elf, section);
                u32 type = (u32)codegen_test_ebpf_read(header.pointer + 4, 4);
                if (type == 9)
                {
                    BUSTER_TEST(arguments, data.length % 16 == 0);
                    for (u64 offset = 0; offset + 16 <= data.length; offset += 16)
                    {
                        u64 info = codegen_test_ebpf_read(data.pointer + offset + 8, 8);
                        u32 symbol = (u32)(info >> 32), kind = (u32)info;
                        bool valid = symbol < symbols.length / 24;
                        String8 name = valid ? codegen_test_ebpf_name(strings, (u32)codegen_test_ebpf_read(symbols.pointer + symbol * 24, 4)) : (String8){0};
                        bool hidden = string_equal(name, S8("hidden")), exported = string_equal(name, S8("exported"));
                        if (kind == 2 && (hidden || exported))
                        {
                            data_references[exported] += 1;
                            u32 owner_index = section_count + (exported ? 4 : 1);
                            valid &= owner_index < symbols.length / 24;
                            if (valid)
                            {
                                u8 const* owner = symbols.pointer + owner_index * 24;
                                valid &= codegen_test_ebpf_read(owner + 6, 2) == codegen_test_ebpf_read(header.pointer + 44, 4) &&
                                    codegen_test_ebpf_read(owner + 8, 8) == codegen_test_ebpf_read(data.pointer + offset, 8);
                            }
                        }
                        else if (kind == 10 && string_equal(name, S8("sub"))) calls += 1;
                        else if (kind == 1)
                        {
                            if (hidden || exported) address_references[exported] += 1;
                            else if (string_equal(name, S8("hidden_address"))) address_references[2] += 1;
                            else if (string_equal(name, S8("exported_address"))) address_references[3] += 1;
                            else valid = false;
                            u32 owner_index = section_count + (hidden || exported ? 2 : 5);
                            valid &= owner_index < symbols.length / 24;
                            if (valid)
                            {
                                u8 const* owner = symbols.pointer + owner_index * 24;
                                u64 begin = codegen_test_ebpf_read(owner + 8, 8), size = codegen_test_ebpf_read(owner + 16, 8);
                                u64 relocation_offset = codegen_test_ebpf_read(data.pointer + offset, 8);
                                valid &= codegen_test_ebpf_read(owner + 6, 2) == codegen_test_ebpf_read(header.pointer + 44, 4) &&
                                    relocation_offset >= begin && relocation_offset - begin < size;
                            }
                        }
                        else valid = false;
                        BUSTER_TEST(arguments, valid);
                    }
                }
                else if (type == 1 && (codegen_test_ebpf_read(header.pointer + 8, 8) & 4))
                {
                    for (u64 offset = 0; offset + 8 <= data.length; offset += 8)
                        helpers += data.pointer[offset] == 0x85 && data.pointer[offset + 1] == 0 && codegen_test_ebpf_read(data.pointer + offset + 4, 4) == 7;
                }
            }
            BUSTER_TEST(arguments, data_references[0] == 1 && data_references[1] == 1 && calls == 1 && helpers == 1);
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(address_references); index += 1) BUSTER_TEST(arguments, address_references[index] != 0);
        }
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_symbols(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 counts[] = {0, 1, 8, 9, 16, 17, 32, 33, 64, 65, 1024};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(counts); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        EbpfSymbolIndexProbe probe = ebpf_test_symbol_index(temporary.arena, counts[index]);
        BUSTER_TEST(arguments, probe.valid && probe.symbol_count == counts[index]);
        BUSTER_TEST(arguments, probe.invalid_key_rejected && probe.full_count_rejected);
#if BUSTER_BENCH_ALLOCATIONS
        // One direct table read per insertion, missing-gap lookup, duplicate
        // insertion and reverse lookup. This fails quadratic scanning without
        // depending on elapsed time, machine load or a generated-code oracle.
        BUSTER_TEST(arguments, probe.lookup_steps <= (u64)counts[index] * 4);
#endif
        scratch_end(temporary);
    }
    UnitTestResult strings = codegen_test_ebpf_string_symbols(arguments);
    result.succeeded_test_count += strings.succeeded_test_count;
    result.test_count += strings.test_count;
    UnitTestResult globals = codegen_test_ebpf_global_symbols(arguments);
    result.succeeded_test_count += globals.succeeded_test_count;
    result.test_count += globals.test_count;
    return result;
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
