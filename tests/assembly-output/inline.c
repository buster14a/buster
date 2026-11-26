// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Original inline-assembly round-trip coverage for #1362.

__asm__(
    ".section .text.inline_file,\"ax\",@progbits\n"
    ".globl inline_file\n"
    ".type inline_file,@function\n"
    "inline_file:\n"
    ".cfi_startproc\n"
    "leal 3(%rdi), %eax\n"
    "ret\n"
    ".cfi_endproc\n"
    ".size inline_file,.-inline_file\n"
    ".section .rodata.inline_file,\"a\",@progbits\n"
    ".p2align 3\n"
    ".globl inline_value\n"
    ".type inline_value,@object\n"
    "inline_value: .quad 19\n"
    ".size inline_value,8\n"
    ".text\n");

extern int inline_file(int value);
extern const long inline_value;

__attribute__((naked, noinline)) int inline_naked(int value)
{
    __asm__(
        "pushq %rbp\n"
        ".cfi_def_cfa_offset 16\n"
        ".cfi_offset %rbp,-16\n"
        "movq %rsp,%rbp\n"
        ".cfi_def_cfa_register %rbp\n"
        "jmp 1f\n"
        ".long 0x12345678\n"
        ".p2align 2\n"
        "1: leal 5(%rdi),%eax\n"
        "popq %rbp\n"
        ".cfi_def_cfa %rsp,8\n"
        ".cfi_restore %rbp\n"
        "ret\n");
}

__attribute__((naked, noinline)) int inline_records(int value)
{
    __asm__("testl %edi,%edi\n jz 1f\n leal 7(%rdi),%eax\n ret\n 1: movl $11,%eax\n ret");
}

__attribute__((naked, noinline)) int inline_literals(void)
{
    __asm__(
        "leaq .Linline_offset(%rip),%rax\n"
        "movslq (%rax),%rdx\n"
        "addq %rdx,%rax\n"
        "movl (%rax),%eax\n"
        "ret\n"
        ".p2align 2\n"
        ".Linline_offset: .long inline_value-.\n");
}

__attribute__((noinline)) int inline_helper(int value)
{
    int result;
    __asm__ volatile("call inline_file" : "=a"(result) : "D"(value) : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "cc", "memory");
    return result;
}

__attribute__((noinline)) int inline_placed(int value)
{
    int result;
    __asm__("leal 1(%1),%0" : "=r"(result) : "r"(value));
    return result;
}

__attribute__((noinline)) int inline_directives(int value)
{
    __asm__ volatile(".p2align 3\n addl $9,%0" : "+r"(value) : : "cc", "memory");
    return value;
}

__attribute__((noinline)) int inline_labels(int value)
{
    __asm__ goto("testl %0,%0\n jz %l1" : : "r"(value) : "cc" : zero);
    return 13;
zero:
    return 17;
}

int main(void)
{
    const int valid = inline_file(2) == 5 && inline_value == 19 && inline_naked(2) == 7 &&
                      inline_records(2) == 9 && inline_records(0) == 11 && inline_placed(2) == 3 &&
                      inline_directives(2) == 11 && inline_labels(2) == 13 && inline_labels(0) == 17 &&
                      inline_literals() == 19 && inline_helper(2) == 5;
    return !valid;
}
