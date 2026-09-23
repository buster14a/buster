# Independent GNU as / LLVM integrated assembler oracle for the bounded LEA
# source-layout slice. The two sections must have identical bytes and fixups:
# external_disp is a full disp32 (R_X86_64_32S), and external_rip is PC32 -4.
.section .text.intel,"ax"
.intel_syntax noprefix
lea rax, [rbp]
lea rax, [r13]
lea rax, [r12+r9*4+127]
lea rax, [r12+r9*4+128]
lea rax, [r12+r9*4+external_disp]
lea eax, [rip+external_rip]

.section .text.att,"ax"
.att_syntax prefix
leaq (%rbp), %rax
leaq (%r13), %rax
leaq 127(%r12,%r9,4), %rax
leaq 128(%r12,%r9,4), %rax
leaq external_disp(%r12,%r9,4), %rax
leal external_rip(%rip), %eax
