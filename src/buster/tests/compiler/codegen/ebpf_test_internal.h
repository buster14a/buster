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
    EbpfArtifact artifact = ebpf_emit_program(arena, &program);
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
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
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
                        bool agreed = codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, first, second, expected);
                        if (!agreed) arguments->show(arguments, S8("eBPF fixture {u32}\n"), fixture);
                        BUSTER_TEST(arguments, agreed);
                    }
                }
            }
        }
        arena_set_position(arguments->arena, mark);
    }
    codegen_test_ebpf_oracle_report(arguments, oracle, S8("codegen_test_ebpf_scalars"));
    return result;
}

// Caller register bits outside a narrow integer's width are not part of its
// value. Check consumers that cannot be repaired by normalizing their result.
BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_argument_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 types[] = {S8("signed char"), S8("unsigned char"), S8("short"), S8("unsigned short"),
                       S8("int"), S8("unsigned int"), S8("long long"), S8("unsigned long long"), S8("_Bool")};
    u32 widths[] = {8, 8, 16, 16, 32, 32, 64, 64, 1};
    String8 expressions[] = {S8("a == b"), S8("a < b"),
        S8("(unsigned long long)a / ((unsigned long long)b | 1ull)"),
        S8("(unsigned long long)a % ((unsigned long long)b | 1ull)"),
        S8("a >> (b & 3)"), S8("a")};
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
    for (u32 type_index = 0; type_index < BUSTER_ARRAY_LENGTH(types); type_index += 1)
    {
        u32 width = widths[type_index];
        bool signed_value = (type_index & 1) == 0 && width != 1;
        u64 mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        u64 sign = UINT64_C(1) << (width - 1);
        u64 values[] = {0, 1, 7, sign - 1, sign, mask,
                        UINT64_C(0xffffffff00000000) & ~mask,
                        (UINT64_C(0x123456789abcdef0) & ~mask) | 7};
        for (u32 operation = 0; operation < BUSTER_ARRAY_LENGTH(expressions); operation += 1)
        {
            for (u32 ssa = 0; ssa < 2; ssa += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
                String8 source = string_format(temporary.arena,
                    S8("unsigned long long probe({S8} a, {S8} b) {{ (void)b; return {S8}; }}"),
                    types[type_index], types[type_index], expressions[operation]);
                CPreprocessResult tokens = c_preprocess(temporary.arena, source,
                    (CPreprocessOptions){.target = target, .data_layout = target_data_layout(target)});
                CParseResult parsed = c_parse(temporary.arena, tokens);
                CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("ebpf-argument-images.c"), tokens, parsed, target,
                    (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
                if (BUSTER_REQUIRE(arguments, !tokens.error_count && !parsed.diagnostic_count && !lowered.diagnostic_count && lowered.program))
                {
                    IrValidationResult validation = ir_validate_canonical_module(lowered.program, lowered.program->modules);
                    if (BUSTER_REQUIRE(arguments, validation.error == IR_VALIDATION_NONE))
                    {
                        EbpfArtifact artifact = ebpf_emit_program(temporary.arena, lowered.program);
                        if (!artifact.success)
                            arguments->show(arguments, S8("eBPF argument type={u32}, operation={u32}, SSA={u32}: {S8}\n"),
                                type_index, operation, ssa, artifact.error.message);
                        if (BUSTER_REQUIRE(arguments, artifact.success))
                        {
                            for (u32 first = 0; first < BUSTER_ARRAY_LENGTH(values); first += 1)
                            {
                                for (u32 second = 0; second < BUSTER_ARRAY_LENGTH(values); second += 1)
                                {
                                    u64 a = values[first] & mask;
                                    u64 b = values[second] & mask;
                                    if (signed_value && (a & sign)) a |= ~mask;
                                    if (signed_value && (b & sign)) b |= ~mask;
                                    u64 expected;
                                    switch (operation)
                                    {
                                    case 0: expected = a == b; break;
                                    case 1: expected = signed_value ? (s64)a < (s64)b : a < b; break;
                                    case 2: expected = a / (b | 1); break;
                                    case 3: expected = a % (b | 1); break;
                                    case 4: expected = signed_value ? (u64)((s64)a >> (b & 3)) : a >> (b & 3); break;
                                    default: expected = a; break;
                                    }
                                    bool agreed = codegen_test_ebpf_check(arguments, &oracle, artifact.bytes,
                                        values[first], values[second], expected);
                                    if (!agreed)
                                        arguments->show(arguments, S8("eBPF argument type={u32}, operation={u32}, SSA={u32}\n"),
                                            type_index, operation, ssa);
                                    BUSTER_TEST(arguments, agreed);
                                }
                            }
                        }
                    }
                }
                scratch_end(temporary);
            }
        }
    }
    codegen_test_ebpf_oracle_report(arguments, oracle, S8("codegen_test_ebpf_argument_images"));
    return result;
}

// Integer images the eBPF writer builds from canonical constants: a signed
// switch compares every label in the condition's normalized image, whatever
// the caller left in the argument register's upper bits, and an integer
// global initializer is its sign and magnitude at the object's width.
BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_integer_images(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    String8 switch_source = S8("long probe(int a, int b) { (void)b; switch (a) { case -1: return 7; case -2147483647 - 1: return 8; "
                               "case 5: return 9; default: return 3; } }");
    CPreprocessResult switch_tokens = c_preprocess(arena, switch_source, (CPreprocessOptions){0});
    CParseResult switch_parse = c_parse(arena, switch_tokens);
    CIRLowerResult switch_lowered = c_lower_to_ir(arena, S8("ebpf-switch.c"), switch_tokens, switch_parse, target);
    BUSTER_TEST(arguments, switch_lowered.program && switch_lowered.diagnostic_count == 0);
    if (switch_lowered.program && switch_lowered.diagnostic_count == 0)
    {
        EbpfArtifact artifact = ebpf_emit_program(arena, switch_lowered.program);
        BUSTER_TEST(arguments, artifact.success);
        struct
        {
            u64 argument;
            u64 expected;
        } cases[] = {
            {UINT64_MAX, 7},
            {UINT64_C(0xffffffff), 7},
            {UINT64_C(0xffffffff80000000), 8},
            {UINT64_C(0x80000000), 8},
            {5, 9},
            {UINT64_C(0x100000005), 9},
            {0, 3},
            {UINT64_C(0x7fffffff), 3},
        };
        for (u32 index = 0; artifact.success && index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            u64 actual = 0;
            bool ran = codegen_test_ebpf_execute(artifact.bytes, cases[index].argument, 0, &actual);
            BUSTER_TEST(arguments, ran && actual == cases[index].expected);
        }
    }
    String8 data_source = S8("int g = -1; long h = -2; short s = -3; unsigned char u = 200; long probe(long a, long b) { return a + b; }");
    CPreprocessResult data_tokens = c_preprocess(arena, data_source, (CPreprocessOptions){0});
    CParseResult data_parse = c_parse(arena, data_tokens);
    CIRLowerResult data_lowered = c_lower_to_ir(arena, S8("ebpf-data.c"), data_tokens, data_parse, target);
    BUSTER_TEST(arguments, data_lowered.program && data_lowered.diagnostic_count == 0);
    if (data_lowered.program && data_lowered.diagnostic_count == 0)
    {
        EbpfArtifact artifact = ebpf_emit_program(arena, data_lowered.program);
        BUSTER_TEST(arguments, artifact.success);
        ByteSlice data = {0};
        u32 section_count = artifact.success ? (u32)codegen_test_ebpf_read(artifact.bytes.pointer + 60, 2) : 0;
        for (u32 section = 1; section < section_count; section += 1)
        {
            ByteSlice header = codegen_test_ebpf_section(artifact.bytes, section);
            u64 flags = header.length ? codegen_test_ebpf_read(header.pointer + 8, 8) : 0;
            if (header.length && codegen_test_ebpf_read(header.pointer + 4, 4) == 1 && (flags & 1) && !(flags & 4))
            {
                data = codegen_test_ebpf_section_data(artifact.bytes, section);
            }
        }
        u8 expected[] = {0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xff, 200};
        BUSTER_TEST(arguments, data.length >= sizeof(expected) && memcmp(data.pointer, expected, sizeof(expected)) == 0);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_local_aggregates(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    ByteSlice fixture = file_read(arguments->arena, S8("tests/basic_c_local_aggregate_copy.c"), (FileReadOptions){0});
    BUSTER_TEST(arguments, fixture.length != 0);
    u64 values[] = {0, 1, 127, 128, UINT64_MAX, UINT64_C(1) << 63, (UINT64_C(1) << 63) - 1};
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
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
                            bool agreed = codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, x, y, expected);
                            if (!agreed)
                            {
                                arguments->show(arguments, S8("eBPF aggregate case {u32}, SSA {u32}\n"), test_case, ssa);
                            }
                            BUSTER_TEST(arguments, agreed);
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
    codegen_test_ebpf_oracle_report(arguments, oracle, S8("codegen_test_ebpf_local_aggregates"));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_stack_liveness(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    u32 lengths[] = {8, 300, 600};
    u64 inputs[] = {0, 1, 127, UINT64_MAX, UINT64_C(1) << 63};
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
    for (u32 ssa = 0; ssa < 2; ssa += 1)
    {
        for (u32 sample = 0; sample < BUSTER_ARRAY_LENGTH(lengths); sample += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            u32 count = lengths[sample];
            String8* parts = arena_allocate(arena, String8, count + 2);
            parts[0] = S8("unsigned long probe(unsigned long x, unsigned long y) {\n");
            for (u32 index = 0; index < count; index += 1) parts[index + 1] = S8("x = x + y;\n");
            parts[count + 1] = S8("return x; }\n");
            String8 source = string_join_arena(arena, (SliceString8){parts, count + 2}, false);
            CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
            CParseResult parse = c_parse(arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("ebpf-chain.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
                if (!artifact.success) arguments->show(arguments, S8("eBPF chain {u32}, SSA {u32}: {S8}\n"), count, ssa, artifact.error.message);
                BUSTER_TEST(arguments, artifact.success);
                if (artifact.success)
                {
                    // Both direct SSA and local-load/store lowering use a fixed
                    // frame independent of the number of sequential operations.
                    BUSTER_TEST(arguments, artifact.stats.max_stack_bytes <= 64);
                    EbpfArtifact repeated = ebpf_emit_program(arena, lowered.program);
                    BUSTER_TEST(arguments, repeated.success && repeated.bytes.length == artifact.bytes.length &&
                                           memory_compare(repeated.bytes.pointer, artifact.bytes.pointer, artifact.bytes.length));
                    for (u32 first = 0; first < BUSTER_ARRAY_LENGTH(inputs); first += 1)
                    {
                        for (u32 second = 0; second < BUSTER_ARRAY_LENGTH(inputs); second += 1)
                        {
                            BUSTER_TEST(arguments, codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, inputs[first], inputs[second],
                                                                           inputs[first] + (u64)count * inputs[second]));
                        }
                    }
                    u32 reloads = 0;
                    for (u32 index = 1; index < artifact.stats.section_count; index += 1)
                    {
                        ByteSlice header = codegen_test_ebpf_section(artifact.bytes, index);
                        if (header.length && (codegen_test_ebpf_read(header.pointer + 8, 8) & 4))
                        {
                            ByteSlice code = codegen_test_ebpf_section_data(artifact.bytes, index);
                            for (u64 offset = 0; offset + 8 <= code.length; offset += 8)
                            {
                                reloads += code.pointer[offset] == 0x79 && (code.pointer[offset + 1] >> 4) == 10;
                            }
                        }
                    }
                    // In SSA form the carried result stays in a register until
                    // the next add; only y and the first x need stack reloads.
                    if (ssa) BUSTER_TEST(arguments, reloads <= count + 1);
                }
            }
            scratch_end(temporary);
        }
    }
    String8 sources[] = {
        // The first multiplication stays live through several younger results.
        S8("unsigned long probe(unsigned long x, unsigned long y) { unsigned long saved=x*3; x=(x+y)^17; x=x*5+y; x=(x^y)+7; return saved+x; }"),
        // The index is consumed by a delayed, nested rematerialized address.
        S8("unsigned long probe(unsigned long x, unsigned long y) { unsigned long a[2]={x,y}; unsigned long *p=&a[(x+1)&1]; unsigned long saved=*p; x=x*3+y; x=x*7+y; return *p+saved+x; }"),
        // Swapped block parameters on a backedge require simultaneous copies.
        S8("unsigned long probe(unsigned long x, unsigned long y) { for(unsigned i=0;i<4;i+=1) { unsigned long saved=x; x=y; y=saved+y; } return x+y; }"),
        // A value used only after a diamond cannot be recycled inside a branch.
        S8("unsigned long probe(unsigned long x, unsigned long y) { unsigned long saved=x*3; if(y&1) x=(x+7)*5; else x=(x+11)*9; return saved+x; }"),
        // Inputs live across rematerialized constants and differing result widths.
        S8("unsigned long probe(unsigned long x, unsigned long y) { unsigned long saved=x+y; unsigned char small=(unsigned char)(x*7); return saved+small+0x123456789abcdef0UL; }"),
    };
    for (u32 ssa = 0; ssa < 2; ssa += 1)
    {
        for (u32 sample = 0; sample < BUSTER_ARRAY_LENGTH(sources); sample += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            CPreprocessResult tokens = c_preprocess(arena, sources[sample], (CPreprocessOptions){0});
            CParseResult parse = c_parse(arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("ebpf-liveness.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
                if (!artifact.success) arguments->show(arguments, S8("eBPF liveness {u32}, SSA {u32}: {S8}\n"), sample, ssa, artifact.error.message);
                BUSTER_TEST(arguments, artifact.success);
                for (u32 first = 0; artifact.success && first < BUSTER_ARRAY_LENGTH(inputs); first += 1)
                {
                    for (u32 second = 0; second < BUSTER_ARRAY_LENGTH(inputs); second += 1)
                    {
                        u64 x = inputs[first], y = inputs[second], expected = 0;
                        switch (sample)
                        {
                        case 0: expected = x*3 + (((((x+y)^17)*5+y)^y)+7); break;
                        case 1: expected = 2 * (((x+1)&1) ? y : x) + (x*3+y)*7+y; break;
                        case 2:
                            for (u32 index = 0; index < 4; index += 1) { u64 saved=x; x=y; y=saved+y; }
                            expected=x+y;
                            break;
                        case 3: expected=x*3 + ((y&1) ? (x+7)*5 : (x+11)*9); break;
                        case 4: expected=x+y+(u8)(x*7)+UINT64_C(0x123456789abcdef0); break;
                        default: break;
                        }
                        bool agreed = codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, inputs[first], inputs[second], expected);
                        if (!agreed) arguments->show(arguments, S8("eBPF liveness {u32}, SSA {u32}\n"), sample, ssa);
                        BUSTER_TEST(arguments, agreed);
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    ByteSlice fixture = file_read(arguments->arena, S8("tests/basic_c_ebpf_stack_liveness.c"), (FileReadOptions){0});
    BUSTER_TEST(arguments, fixture.length != 0);
    for (u32 ssa = 0; fixture.length && ssa < 2; ssa += 1)
    {
        for (u32 dead = 0; dead < 2; dead += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            String8 source = string_format(arena, S8("#define EBPF_DEAD_BRANCH {u32}\n{S8}"), dead,
                                           (String8){(char8*)fixture.pointer, fixture.length});
            CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
            CParseResult parse = c_parse(arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("ebpf-metamorphic-stack.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
                if (!artifact.success) arguments->show(arguments, S8("eBPF dead branch {u32}, SSA {u32}: {S8}\n"), dead, ssa, artifact.error.message);
                BUSTER_TEST(arguments, artifact.success);
                // Issue #1305: the dead branch once left an unreachable block
                // that the kernel verifier rejects.
                for (u32 index = 0; artifact.success && index < BUSTER_ARRAY_LENGTH(inputs); index += 1)
                {
                    BUSTER_TEST(arguments, codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, inputs[index], 0,
                                                                   (u32)((u32)inputs[index] * 7u + 4u)));
                }
            }
            scratch_end(temporary);
        }
    }
    for (u32 sample = 0; sample < 3; sample += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        String8 source;
        if (sample < 2)
        {
            source = string_format(arena, S8("unsigned long probe(void) {{ volatile unsigned char data[{u32}]; return 0UL; }}"), 512 + sample);
        }
        else
        {
            enum { pressure = 65 };
            String8 parts[2 * pressure + 3];
            parts[0] = S8("unsigned long probe(unsigned long x) {\n");
            for (u32 index = 0; index < pressure; index += 1) parts[index + 1] = string_format(arena, S8("unsigned long a{u32}=x+{u32};\n"), index, index);
            parts[pressure + 1] = S8("return 0");
            for (u32 index = 0; index < pressure; index += 1) parts[pressure + 2 + index] = string_format(arena, S8("+a{u32}"), index);
            parts[2 * pressure + 2] = S8("; }");
            source = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), false);
        }
        CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
        CParseResult parse = c_parse(arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arena, S8("ebpf-pressure.c"), tokens, parse, target);
        BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
        if (lowered.program && lowered.diagnostic_count == 0)
        {
            EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
            if (sample == 0) BUSTER_TEST(arguments, artifact.success && artifact.stats.max_stack_bytes == 512);
            else BUSTER_TEST(arguments, !artifact.success && artifact.error.code == EBPF_ERROR_STACK_LIMIT && artifact.bytes.length == 0);
        }
        scratch_end(temporary);
    }
    codegen_test_ebpf_oracle_report(arguments, oracle, S8("codegen_test_ebpf_stack_liveness"));
    return result;
}

// Issue #1305: expected results for the source fixtures of
// codegen_test_ebpf_kernel_regressions, computed by the host.
BUSTER_GLOBAL_LOCAL u64 codegen_test_ebpf_regression_expected(u32 fixture, u64 a, u64 b)
{
    u64 result = 0;
    switch (fixture)
    {
    case 0: result = a > b ? 1 : 2; break;
    case 1: result = (a & 3) == 0 ? b : (a & 3) == 1 ? 7 : a; break;
    case 2: result = (a ^ b) + 3; break;
    case 3: result = (s32)a == -1 ? 1 : (s32)a == INT32_MIN ? 3 : (s32)a == 5 ? 4 : 2; break;
    case 4: result = (s8)a == -1 ? 1 : (s8)a == -128 ? 3 : (s8)a == 127 ? 4 : 2; break;
    case 5: result = (s32)a == -1 ? 1 : (s32)a == 7 ? 3 : 2; break;
    case 6: result = (u32)a == UINT32_MAX ? 1 : (u32)a == 0x80000000u ? 3 : 2; break;
    case 7:
    case 8: result = (b & 1) == 1; break;
    case 9: result = a + (u32)b + (u16)(a >> 16) + 1 + (u64)(s64)(s8)b + (0 - b); break;
    case 10: result = (u16)b + a + (u64)(s64)(s32)(u32)b; break;
    case 11: result = a / (b | 1) + a % (b | 1); break;
    default: break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult codegen_test_ebpf_kernel_regressions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CodegenTestEbpfOracle oracle = codegen_test_ebpf_oracle(arguments->arena);
    Target target = {.cpu_arch = CPU_ARCH_BPFEL, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
    String8 sources[] = {
        // F1: control flow that always returns leaves an unreachable block.
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { if (a > b) return 1; else return 2; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { switch (a & 3) { case 0: return b; case 1: return 7; default: return a; } }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned i = 0; for (;;) { if (i == 3) return a + i; i += 1; a ^= b; } }"),
        // F2: labels are extended from the operand's width by its signedness.
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned long long r; switch ((int)a) { case -1: r = 1; break; "
           "case -2147483647 - 1: r = 3; break; case 5: r = 4; break; default: r = 2; } return r + 0 * b; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned long long r; switch ((signed char)a) { case -1: r = 1; break; "
           "case -128: r = 3; break; case 127: r = 4; break; default: r = 2; } return r + 0 * b; }"),
        // The caller may leave the upper half of an int argument dirty.
        S8("unsigned long long probe(int a, unsigned long long b) { switch (a) { case -1: return 1 + 0 * b; case 7: return 3; default: return 2; } }"),
        S8("unsigned long long probe(unsigned a, unsigned long long b) { switch (a) { case 0xffffffffu: return 1 + 0 * b; case 0x80000000u: return 3; "
           "default: return 2; } }"),
        // F3: normalizing the unsigned index must not clobber the base in R9.
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned char arr[2]; arr[0] = 0; arr[1] = (unsigned char)a; "
           "return &arr[b & 1] == &arr[1u]; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { unsigned char arr[2]; arr[0] = (unsigned char)a; arr[1] = 0; "
           "return &arr[b & 1] == arr + 1u; }"),
        // F4: packed members sit at unaligned frame offsets.
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { struct __attribute__((packed)) P { unsigned char tag; "
           "unsigned long long wide; unsigned int word; unsigned short half; signed char low; long long signed_wide; } p; "
           "p.tag = 1; p.wide = a; p.word = (unsigned int)b; p.half = (unsigned short)(a >> 16); p.low = (signed char)b; "
           "p.signed_wide = (long long)(0 - b); return p.wide + p.word + p.half + p.tag + (unsigned long long)p.low + "
           "(unsigned long long)p.signed_wide; }"),
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { struct __attribute__((packed)) Q { unsigned short half; "
           "unsigned long long wide; int word; }; struct Q q = {(unsigned short)b, a, (int)b}; struct Q copy = q; "
           "return copy.half + copy.wide + (unsigned long long)(long long)copy.word; }"),
        // The VM once refused the unsigned division the emitter produces.
        S8("unsigned long long probe(unsigned long long a, unsigned long long b) { return a / (b | 1) + a % (b | 1); }"),
    };
    u64 inputs[] = {0, 1, 5, 7, 127, 128, 0x7fffffff, 0x80000000, 0xffffffff, UINT64_MAX, UINT64_C(1) << 63, UINT64_C(0xffffffff00000005)};
    for (u32 ssa = 0; ssa < 2; ssa += 1)
    {
        for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(sources); fixture += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            Arena* arena = temporary.arena;
            CPreprocessResult tokens = c_preprocess(arena, sources[fixture], (CPreprocessOptions){0});
            CParseResult parse = c_parse(arena, tokens);
            CIRLowerResult lowered = c_lower_to_ir_with_options(arena, S8("ebpf-kernel-regression.c"), tokens, parse, target,
                                                                (CIRLowerOptions){.disable_direct_ssa = ssa == 0});
            BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
            if (lowered.program && lowered.diagnostic_count == 0)
            {
                EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
                if (!artifact.success) arguments->show(arguments, S8("eBPF regression {u32}, SSA {u32}: {S8}\n"), fixture, ssa, artifact.error.message);
                BUSTER_TEST(arguments, artifact.success);
                for (u32 first = 0; artifact.success && first < BUSTER_ARRAY_LENGTH(inputs); first += 1)
                {
                    for (u32 second = 0; second < BUSTER_ARRAY_LENGTH(inputs); second += 1)
                    {
                        u64 a = inputs[first], b = inputs[second];
                        bool agreed = codegen_test_ebpf_check(arguments, &oracle, artifact.bytes, a, b, codegen_test_ebpf_regression_expected(fixture, a, b));
                        if (!agreed) arguments->show(arguments, S8("eBPF regression {u32}, SSA {u32}\n"), fixture, ssa);
                        BUSTER_TEST(arguments, agreed);
                    }
                }
            }
            scratch_end(temporary);
        }
    }
    // The VM refuses what it does not model. A helper placed before the entry
    // once ran as the entry, and a global's address became stack offset 0.
    String8 refused[] = {
        S8("static unsigned long long helper(unsigned long long x) { return x * 3; } "
           "unsigned long long probe(unsigned long long a, unsigned long long b) { return helper(a) + b; }"),
        S8("unsigned long long G = 5; unsigned long long probe(unsigned long long a, unsigned long long b) { return G + a + 0 * b; }"),
    };
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(refused); fixture += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        CPreprocessResult tokens = c_preprocess(arena, refused[fixture], (CPreprocessOptions){0});
        CParseResult parse = c_parse(arena, tokens);
        CIRLowerResult lowered = c_lower_to_ir(arena, S8("ebpf-vm-refusal.c"), tokens, parse, target);
        BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
        if (lowered.program && lowered.diagnostic_count == 0)
        {
            EbpfArtifact artifact = ebpf_emit_program(arena, lowered.program);
            u64 observed = 0;
            BUSTER_TEST(arguments, artifact.success && !codegen_test_ebpf_execute(artifact.bytes, 7, 2, &observed));
        }
        scratch_end(temporary);
    }
    // Hand-assembled functions pin each VM rule. When the kernel is available
    // it must agree on acceptance and on the result.
    typedef struct CodegenTestEbpfRule CodegenTestEbpfRule;
    struct CodegenTestEbpfRule
    {
        u8 code[64];
        u32 length;
        u64 first;
        u64 second;
        u64 expected;
        bool accepted;
        bool kernel;
    };
    CodegenTestEbpfRule rules[] = {
        // r0 = 1; exit
        {{0xb7, 0x00, 0, 0, 1, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 16, 0, 0, 1, true, true},
        // r0 = 1; exit; r0 = 0; exit: an unreachable instruction.
        {{0xb7, 0x00, 0, 0, 1, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0, 0xb7, 0x00, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 32, 0, 0, 0, false, true},
        // r0 = 1: execution falls off the end.
        {{0xb7, 0x00, 0, 0, 1, 0, 0, 0}, 8, 0, 0, 0, false, true},
        // goto +5; exit: a jump out of range.
        {{0x05, 0x00, 5, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 16, 0, 0, 0, false, true},
        // goto +1; r0 = 0 ll; exit: a jump into the middle of LDDW.
        {{0x05, 0x00, 1, 0, 0, 0, 0, 0, 0x18, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 32, 0, 0, 0, false, true},
        // r0 = 1; *(u64 *)(r10 - 8) = r0; exit
        {{0xb7, 0x00, 0, 0, 1, 0, 0, 0, 0x7b, 0x0a, 0xf8, 0xff, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 0, 0, 1, true, true},
        // r0 = 1; *(u64 *)(r10 - 7) = r0; exit: a misaligned stack store.
        {{0xb7, 0x00, 0, 0, 1, 0, 0, 0, 0x7b, 0x0a, 0xf9, 0xff, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 0, 0, 0, false, true},
        // r0 = r3; exit: R3 is not an argument, so it is uninitialized.
        {{0xbf, 0x30, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 16, 0, 0, 0, false, true},
        // r0 = r1; r0 /= r2; exit, with division by zero defined as zero.
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x3f, 0x20, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 7, 0, 0, true, true},
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x3f, 0x20, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 7, 2, 3, true, true},
        // r0 = r1; r0 %= r2; exit, with remainder by zero keeping the dividend.
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x9f, 0x20, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 7, 0, 7, true, true},
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x9f, 0x20, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 7, 2, 1, true, true},
        // r0 = r1; w0 %= w2; exit keeps only the low half of the dividend.
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x9c, 0x20, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, UINT64_C(0x100000007), 0, 7, true, true},
        // Encodings outside the modeled subset: a helper call, a JMP32 branch,
        // signed division and a map-FD LDDW. The kernel may accept them.
        {{0xb7, 0x00, 0, 0, 0, 0, 0, 0, 0x85, 0, 0, 0, 7, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 0, 0, 0, false, false},
        {{0xb7, 0x00, 0, 0, 0, 0, 0, 0, 0x16, 0x01, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 0, 0, 0, false, false},
        {{0xbf, 0x10, 0, 0, 0, 0, 0, 0, 0x3f, 0x20, 1, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 24, 7, 2, 0, false, false},
        {{0x18, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb7, 0x00, 0, 0, 0, 0, 0, 0, 0x95, 0, 0, 0, 0, 0, 0, 0}, 32, 0, 0, 0, false, false},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rules); index += 1)
    {
        CodegenTestEbpfRule* rule = rules + index;
        ByteSlice code = {rule->code, rule->length};
        u64 observed = 0;
        bool ran = codegen_test_ebpf_run(code, rule->first, rule->second, &observed);
        bool vm_agreed = ran == rule->accepted && (!ran || observed == rule->expected);
        if (!vm_agreed) arguments->show(arguments, S8("eBPF VM rule {u32}: ran {u32}, observed {u64}\n"), index, (u32)ran, observed);
        BUSTER_TEST(arguments, vm_agreed);
        if (oracle.kernel_available && rule->kernel)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            u64 kernel_observed = 0;
            String8 reason = {0};
            CodegenTestEbpfKernel status = codegen_test_ebpf_kernel_run(temporary.arena, code, rule->first, rule->second, &kernel_observed, &reason);
            bool executed = status == CODEGEN_TEST_EBPF_KERNEL_EXECUTED;
            bool kernel_agreed = executed == rule->accepted && (!executed || kernel_observed == rule->expected);
            oracle.kernel_executions += 1;
            if (!kernel_agreed) arguments->show(arguments, S8("eBPF kernel rule {u32}: {S8}, observed {u64}\n"), index, reason, kernel_observed);
            BUSTER_TEST(arguments, kernel_agreed);
            scratch_end(temporary);
        }
    }
    codegen_test_ebpf_oracle_report(arguments, oracle, S8("codegen_test_ebpf_kernel_regressions"));
    return result;
}

#endif
