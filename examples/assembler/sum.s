# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
.intel_syntax noprefix
.text
.globl assembly_sum
.type assembly_sum, @function
assembly_sum:
    push rbp
    mov rbp, rsp
    xor eax, eax
    xor ecx, ecx
    jmp check
again:
    add rax, qword ptr [rdi+rcx*8]
    inc rcx
check:
    cmp rcx, rsi
    jb again
    add rax, 200
    mov rdi, rax
    call external_twice
    add rax, qword ptr [rip+bias]
    pop rbp
    ret
.size assembly_sum, .-assembly_sum

.section .rodata
.balign 8
.type bias, @object
bias:
    .quad 3
.size bias, 8
