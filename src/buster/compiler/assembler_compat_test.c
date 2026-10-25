// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original focused fixtures for #233. Every accepted GNU fixture has an independent object oracle.

static void asm_test_local_symbols(AsmTest* test)
{
    static const char source[] = ".text\n.globl visible\nvisible:\n.Ldiscarded: nop\n1: jmp 1b\n.data\nkept: .quad .Ldiscarded,.\n";
    AsmResult result = asm_test_pair(test, (String8){.pointer = (char*)source, .length = sizeof(source) - 1}, 1, X86_ARCH_BASELINE);
    const Object object = asm_object(&result, BUSTER_S8("locals.s"), &test->vector);
    ObjectView view;
    char error[256] = {0};
    bool okay = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
    bool visible = false;
    bool kept = false;
    for (uint64_t i = 1; okay && i < view.symbol_count; i += 1)
    {
        const char* name = object_view_string(view.symbol_names, view.symbol_names_size, view.symbols[i].st_name);
        okay = name != 0 && !(name[0] == '.' && name[1] == 'L');
        visible |= name != 0 && strcmp(name, "visible") == 0;
        kept |= name != 0 && strcmp(name, "kept") == 0;
    }
    asm_test_check(test, okay && visible && kept, "temporary labels elided, ordinary local/global names retained", error);
    ObjectUnits units;
    ObjectSymbols symbols;
    if (result.error == 0 && object_prepare(&units, &result.link, 1, &test->vector) && object_map_symbols(&symbols, &result.link, &test->vector))
    {
        const uint64_t saved = test->vector.length;
        ObjectOptions options = result.object_options;
        options.atom_section_count -= 1;
        const Object short_sections = object_build(&result.link, &units, &symbols, 0, options, BUSTER_S8("locals.s"), &test->vector);
        options = result.object_options;
        options.temporary_symbol_count -= 1;
        const Object short_symbols = object_build(&result.link, &units, &symbols, 0, options, BUSTER_S8("locals.s"), &test->vector);
        asm_test_check(test, short_sections.error == OBJECT_ERROR_RANGE && short_symbols.error == OBJECT_ERROR_RANGE && test->vector.length == saved,
                       "short optional writer metadata rejected before output allocation", 0);
    }
    else
    {
        asm_test_check(test, false, "optional writer metadata fixture allocation", 0);
    }
}

static void asm_test_high_address(AsmTest* test, const char* directory)
{
    static const char source[] = ".text\n.globl _start\n_start: leal external,%edi\nxorl $0x80000000,%edi\nmovl $60,%eax\nsyscall\n";
    AsmResult result = asm_test_pair(test, (String8){.pointer = (char*)source, .length = sizeof(source) - 1}, 1, X86_ARCH_BASELINE);
    const Object object = asm_object(&result, BUSTER_S8("high.s"), &test->vector);
    char object_path[PATH_MAX];
    char executable[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(object_path, sizeof(object_path), "%s/high.o", directory);
    snprintf(executable, sizeof(executable), "%s/high", directory);
    snprintf(output, sizeof(output), "%s/high-output", directory);
    snprintf(errors, sizeof(errors), "%s/high-errors", directory);
    char* link[] = {"ld", "--defsym=external=0x80000000", "-o", executable, object_path, 0};
    char* run[] = {executable, 0};
    const bool okay = object.error == OBJECT_ERROR_NONE && object_write_file(object_path, object.bytes, object.size) == OBJECT_ERROR_NONE &&
                      object_test_run(link, output, errors) == 0 && object_test_run(run, output, errors) == 0;
    asm_test_check(test, okay, "32-bit LEA links and executes an address above INT32_MAX", 0);
    unlink(object_path);
    unlink(executable);
    unlink(output);
    unlink(errors);
}

static void asm_test_expression_operators(AsmTest* test, const char* directory, bool cross_check)
{
    static const char* const operators[] = {"*", "+", "-", "<<", ">>", "&", "|", "^", "!", "==", "!=", "<>", "<", ">", "<=", ">=", "&&", "||"};
    static const char* const operands[][3] =
    {
        {"2", "3", "4"},
        {"-1", "7", "1"},
        {"0x8000000000000000", "1", "0"},
    };
    char source[65536];
    size_t length = snprintf(source, sizeof(source), ".data\noperator_pairs:\n");
    bool valid = true;
    for (uint32_t first = 0; valid && first < BUSTER_ARRAY_LENGTH(operators); first += 1)
    {
        for (uint32_t second = 0; valid && second < BUSTER_ARRAY_LENGTH(operators); second += 1)
        {
            for (uint32_t values = 0; valid && values < BUSTER_ARRAY_LENGTH(operands); values += 1)
            {
                const int count = snprintf(source + length, sizeof(source) - length, ".quad %s %s %s %s %s\n", operands[values][0], operators[first], operands[values][1], operators[second], operands[values][2]);
                valid = count >= 0 && (size_t)count < sizeof(source) - length;
                if (valid)
                {
                    length += count;
                }
            }
        }
    }
    asm_test_check(test, valid, "operator-pair fixture capacity", 0);
    if (valid && cross_check)
    {
        asm_test_oracle(test, source, directory, X86_ARCH_BASELINE);
    }
    else if (valid)
    {
        asm_test_pair(test, (String8){.pointer = source, .length = length}, 1, X86_ARCH_BASELINE);
    }
}

static void asm_test_cfi_sections(AsmTest* test, const char* directory, bool cross_check)
{
    char source[32768];
    size_t length = 0;
    bool valid = true;
    for (uint32_t i = 0; valid && i < 128; i += 1)
    {
        const int count = snprintf(source + length, sizeof(source) - length,
                                   ".section .text.cfi_%u,\"ax\",@progbits\ncfi_%u:\n.cfi_startproc simple\n.cfi_def_cfa 7,%u\nret\n.cfi_endproc\n", i, i, (i % 17 + 1) * 8);
        valid = count >= 0 && (size_t)count < sizeof(source) - length;
        if (valid)
        {
            length += count;
        }
    }
    asm_test_check(test, valid, "many-section CFI fixture capacity", 0);
    if (valid && cross_check)
    {
        asm_test_oracle(test, source, directory, X86_ARCH_BASELINE);
    }
    else if (valid)
    {
        asm_test_pair(test, (String8){.pointer = source, .length = length}, 1, X86_ARCH_BASELINE);
    }
}

static void asm_test_sparse_cfi(AsmTest* test)
{
    static const char body[] = "\n.text\nsparse: .cfi_startproc\nret\n.cfi_endproc\n";
    static const uint32_t padding[] = {16, 1u << 20};
    Arena files = arena_reserve(1ull << 24);
    uint64_t first_size = 0;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(padding); i += 1)
    {
        files.length = 0;
        const uint32_t size = padding[i] + sizeof(body) - 1;
        char* source = arena_push(&files, size, 1);
        asm_test_check(test, source != 0, "sparse CFI source allocation", 0);
        if (source != 0)
        {
            source[0] = '#';
            memset(source + 1, 'x', padding[i] - 1);
            memcpy(source + padding[i], body, sizeof(body) - 1);
            AsmResult result = asm_test_pair(test, (String8){.pointer = source, .length = size}, 1, X86_ARCH_BASELINE);
            uint64_t frame_bytes = 0;
            for (AtomId atom = 1; result.error == 0 && atom < result.link.atoms.count; atom += 1)
            {
                if (result.link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
                {
                    frame_bytes += result.link.atoms.size[atom];
                }
            }
            const uint64_t retained = test->vector.length;
            asm_test_check(test, result.error == 0 && frame_bytes == 48 && (i == 0 || retained == first_size), "CFI arena reservation independent of unrelated source bytes", result.error);
            printf("assembler sparse CFI: source %u bytes, retained arena %llu bytes, .eh_frame %llu bytes\n", size, (unsigned long long)retained, (unsigned long long)frame_bytes);
            first_size = retained;
        }
    }
    arena_release(&files);
}

static void asm_test_compatibility(AsmTest* test, const char* directory, bool cross_check)
{
    static const char* const accepted[] =
    {
        ".data\nexpr: .long 1<<4,64>>3,1+2<<3,(1+2)<<3,3*2<<2,32>>1*2\n"
        ".long 1==1,1!=1,1<>2,1<2,2>1,2<=2,3>=4,0xffffffffffffffff<0\n"
        ".long 1||0&&0,0||2&&3,!0,!9,1!2,4+1==5,1|2+4,1==2+3\n"
        ".quad -1>>1,1<<63,1<<64,1>>64,1<<-1\n",
        ".text\nconvert:\nmovzbw %al,%cx\nmovzbl %al,%ecx\nmovzbq %al,%rcx\n"
        "movzwl %ax,%ecx\nmovzwq %ax,%rcx\nmovsbw %al,%cx\nmovsbl %al,%ecx\n"
        "movsbq %al,%rcx\nmovswl %ax,%ecx\nmovswq %ax,%rcx\nmovslq %eax,%rcx\n"
        "movzbl (%rdi),%eax\nmovzwq 2(%rdi),%rcx\nmovswl (%rdi),%eax\n"
        "cbtw\ncwtl\ncltq\ncwtd\ncltd\ncqto\nret\n",
        ".data\nconstants:\n.equ .Lamount,3\n.byte .Lamount\n.equ .Lamount,4\n.byte .Lamount\n"
        ".set .Lamount,5\n.byte .Lamount\n.equiv .Lunique,6\n.byte .Lunique\n",
        ".file \"source.c\"\n.ident \"original test emitter\"\n"
        ".data\nmetadata: .short 1\n.value 2\n.int 3\n.align 8\n.quad 4\n"
        ".section .note.GNU-stack,\"\",@progbits\n",
        ".file \"clang-note.c\"\n.text\nquoted_note: ret\n"
        ".section \".note.GNU-stack\",\"\",@progbits\n",
        ".intel_syntax noprefix\n.text\nuppercase:\nmov rax,QWORD PTR [rdi]\n"
        "mov eax,DWORD PTR [rdi+4]\nmov rax,OFFSET FLAT:external\n"
        "mov rax,QWORD PTR external[rip]\nmov eax,DWORD PTR FS:[8]\nret\n",
        ".intel_syntax noprefix\n.text\nparentheses:\nmov eax,[rax+(rsi)]\n"
        "mov eax,[rax+2*(rsi)]\nmov eax,[(rax+rsi*4)+8]\nmov eax,[((rsi))+rax]\n"
        "mov eax,[rax+(rsp)]\nmov eax,[rax+(2+2)*(rsi)]\nret\n",
        ".intel_syntax noprefix\n.text\nexplicit_index:\nmov eax,[rsi*1]\n"
        "mov eax,[1*rsi]\nmov eax,[(rsi)*1]\nmov eax,[rax+(rsi*1)]\n"
        "mov eax,[(rsi*1)+rax]\nret\n",
        ".text\n.globl entry,target\nentry: jmp target\nje target\ncall target\n"
        "target: ret\njmp .Lelsewhere\n.data\n.Lelsewhere: .byte 0\nreference: .quad .Lelsewhere\n",
        ".text\n.weak weak_target\nweak_entry: jmp weak_target\nje weak_target\ncall weak_target\n"
        "weak_target: ret\n.globl far_target\nfar_entry: jne far_target\n.zero 140\n"
        "far_target: ret\n",
        ".text\n.globl entry,target\nentry: jmp target\nje target\ncall target\n"
        "target: ret\n.section .text.other,\"ax\",@progbits\nother: jmp .Llocal\n"
        ".text\n.Llocal: ret\n.data\nreference: .quad .Llocal\n",
        ".section .text.one,\"ax\",@progbits\n.globl one\none: ret\n"
        ".section .text.two,\"ax\",@progbits\ntwo: ret\n"
        ".section .rodata.str1.1,\"aMS\",@progbits,1\n.Lmessage: .string \"original\"\n"
        ".section .rodata.table,\"a\",@progbits\n.balign 8\ntable: .quad one,.Lmessage\n"
        ".section .data.value,\"aw\",@progbits\nvalue: .long 17\n"
        ".section .bss.zero,\"aw\",@nobits\nzero: .zero 32\n",
        ".file \"quoted.c\"\n.text\n.globl \"quoted_function\"\n.type \"quoted_function\",@function\n"
        "\"quoted_function\": ret\n.size \"quoted_function\",.-\"quoted_function\"\n",
        ".text\n\":numeric.1.1\": nop\n1: jmp 1b\n.data\nquoted_reference: .quad \":numeric.1.1\"\n",
        ".text\n\"quote\\\"name\": ret\n\"slash\\\\name\": ret\n"
        ".data\nescaped_reference: .quad \"quote\\\"name\",\"slash\\\\name\"\n",
        ".text\n.globl framed\n.type framed,@function\nframed:\n.cfi_startproc\n"
        "pushq %rbp\n.cfi_def_cfa_offset 16\n.cfi_offset 6,-16\n"
        "movq %rsp,%rbp\n.cfi_def_cfa_register 6\n"
        "pushq %rbx\n.cfi_offset 3,-24\n.cfi_remember_state\n"
        "popq %rbx\n.cfi_restore 3\n.cfi_restore_state\n"
        "popq %rbp\n.cfi_def_cfa 7,8\nret\n.cfi_endproc\n.size framed,.-framed\n",
        ".text\nwidth_bits:\nandw $65535,%si\nimul $4294967295,127(%r12),%eax\n"
        "movq $0xffffffffffffffff,%rdi\nret\n",
        ".intel_syntax noprefix\n.text\nwidth_bits:\nand si,65535\nimul eax,[r12+127],4294967295\n"
        "mov rdi,0xffffffffffffffff\nret\n",
        ".text\ntruncated_address:\nleal external,%ecx\nleaw external,%cx\nleaq external,%rcx\nret\n",
        ".cfi_sections .eh_frame\n.text\nstateful:\n.cfi_startproc\n"
        "pushq %rbp\n.cfi_adjust_cfa_offset 8\n.cfi_rel_offset %rbp,0\n"
        ".cfi_def_cfa_register %rbp\n.cfi_register %r12,%r13\n"
        ".cfi_undefined %r14\n.cfi_same_value %r15\n.cfi_remember_state\n"
        "popq %rbp\n.cfi_def_cfa %rsp,8\n.cfi_restore %rbp\n.cfi_restore_state\n"
        "ret\n.cfi_endproc\n",
        ".text\ninitial_state: .cfi_startproc simple\n.cfi_def_cfa 7,8\n"
        ".cfi_remember_state\n.cfi_def_cfa_offset 16\n.cfi_restore_state\n"
        "nop\nret\n.cfi_endproc\n",
        ".text\nshared_cie_a: .cfi_startproc\nnop\n.cfi_def_cfa_offset 16\nret\n.cfi_endproc\n"
        "shared_cie_b: .cfi_startproc\nret\n.cfi_endproc\n"
        "simple_cie: .cfi_startproc simple\n.cfi_def_cfa 7,8\nret\n.cfi_endproc\n",
        ".text\nrelaxed_frame: .cfi_startproc\njmp .Lfar\n.zero 160\n.p2align 5\n"
        ".Lfar: .cfi_def_cfa_offset 16\nret\n.cfi_endproc\n",
        ".text\nextended_cfi: .cfi_startproc simple\n.cfi_return_column 17\n.cfi_signal_frame\n"
        ".cfi_def_cfa %rsp,8\n.cfi_offset 80,-24\n.cfi_val_offset %r12,16\n"
        ".cfi_register 80,81\n.cfi_escape 0,7,6\nnop\n.cfi_restore 80\nret\n.cfi_endproc\n",
        ".text\nsigned_cfi: .cfi_startproc\n.cfi_def_cfa 7,-16\n"
        ".cfi_offset 6,16\n.cfi_val_offset 12,-24\nnop\n.cfi_def_cfa_offset -24\n"
        "ret\n.cfi_endproc\n",
        ".text\ninterleaved_frame: .cfi_startproc\npushq %rbp\n.cfi_def_cfa_offset 16\n"
        ".section .data.interleaved,\"aw\",@progbits\npointer: .quad .Lfinish\n.text\n"
        "movq %rsp,%rbp\n.cfi_def_cfa_register 6\n"
        ".section .rodata.interleaved,\"a\",@progbits\nvalue: .long 7\n.text\n"
        "movl $1,%eax\njmp .Lfinish\n.zero 140\n.p2align 4\n.Lfinish:\n"
        "popq %rbp\n.cfi_def_cfa 7,8\nret\n.cfi_endproc\n",
        ".section .rodata.cst8,\"aM\",@progbits,8\n.p2align 3\n.Lconstant: .quad 0x12345678\n"
        ".text\nconstant_reader: movq .Lconstant(%rip),%rax\nret\n.data\nconstant_address: .quad .Lconstant+1\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accepted); i += 1)
    {
        if (cross_check)
        {
            if (!asm_test_oracle(test, accepted[i], directory, X86_ARCH_BASELINE))
            {
                fprintf(stderr, "assembler compatibility fixture %u:\n%s", i, accepted[i]);
            }
        }
        else
        {
            asm_test_pair(test, (String8){.pointer = (char*)accepted[i], .length = strlen(accepted[i])}, 1, X86_ARCH_BASELINE);
        }
    }
    static const char* const rejected[] =
    {
        ".equiv unique,1\n.equiv unique,2\n",
        ".long 1<<\n",
        ".long 1&&\n",
        ".long 1<=>2\n",
        ".long (1||0\n",
        ".data\n\"bad\\nname\": .quad 0\n",
        ".text\n\"1\": ret\n",
        ".addrsig unexpected\n",
        ".addrsig_sym\n",
        ".section .note.GNU-stack,\"x\",@progbits\n",
        ".section .note.GNU-stack,\"\",@progbits\n.byte 1\n",
        ".section .note.GNU-stack,\"\",@nobits\n",
        ".section \".note.GNU-stack\",\"x\",@progbits\n",
        ".section \".note.GNU-stack\",\"\",@progbits\n.byte 1\n",
        ".text\nmovzbl %ax,%eax\n",
        ".text\nmovsbq %al,%ecx\n",
        ".intel_syntax noprefix\nmov eax,[rax+(-rsi)]\n",
        ".intel_syntax noprefix\nmov eax,[rax+(rsi*rdi)]\n",
        ".intel_syntax noprefix\nmov eax,[rax+(rsi*16)]\n",
        ".intel_syntax noprefix\nmov eax,[rax+rsi+rdi]\n",
        ".intel_syntax noprefix\nmov eax,[rip*1]\n",
        ".intel_syntax noprefix\nmov eax,[rsp*1]\n",
        ".text\n.cfi_endproc\n",
        ".text\nf: .cfi_startproc\n.cfi_startproc\n",
        ".text\nf: .cfi_startproc\nret\n",
        ".text\nf: .cfi_startproc\n.cfi_restore_state\n.cfi_endproc\n",
        ".section .text.bad,\"ax\",@nobits\n",
        ".section .rodata.bad,\"aMS\",@progbits,0\n",
        ".text\nf: .cfi_startproc\n.cfi_escape 256\n.cfi_endproc\n",
        ".text\nf: .cfi_startproc\n.cfi_offset 6,-7\n.cfi_endproc\n",
        ".text\nf: .cfi_startproc\n.cfi_def_cfa_offset 0x100000000\n.cfi_endproc\n",
        ".text\nf: .cfi_startproc\n.section .text.other,\"ax\",@progbits\n.cfi_endproc\n",
        ".section .data.changed,\"aw\",@progbits\n.section .data.changed,\"a\",@progbits\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        asm_test_pair(test, (String8){.pointer = (char*)rejected[i], .length = strlen(rejected[i])}, 0, X86_ARCH_BASELINE);
    }
    // Address-significance annotations are LLVM-only. Omitting the table conservatively preserves all addresses.
    static const char annotations[] = ".text\nf: ret\n.addrsig\n.addrsig_sym f\n";
    asm_test_pair(test, (String8){.pointer = (char*)annotations, .length = sizeof(annotations) - 1}, 1, X86_ARCH_BASELINE);
    asm_test_local_symbols(test);
    if (cross_check)
    {
        asm_test_high_address(test, directory);
    }
    asm_test_expression_operators(test, directory, cross_check);
    asm_test_cfi_sections(test, directory, cross_check);
    asm_test_sparse_cfi(test);
    if (cross_check)
    {
        static const uint32_t deltas[] = {0, 1, 63, 64, 255, 256, 65535, 65536};
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(deltas); i += 1)
        {
            char source[256];
            snprintf(source, sizeof(source), ".text\ncfi_delta: .cfi_startproc\n.zero %u\n.cfi_def_cfa_offset 16\nret\n.cfi_endproc\n", deltas[i]);
            asm_test_oracle(test, source, directory, X86_ARCH_BASELINE);
        }
    }
}
