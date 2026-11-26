# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Original matched-record/data-fragment fixture for #1362.
.text
.globl mixed
.type mixed,@function
mixed:
    leaq .Ltable(%rip),%rax
    movl (%rax),%ecx
    testl %ecx,%ecx
    jz .Lfinish
    call external
    jmp .Lfinish
    .byte 0x11,0x22,0x33
    .p2align 3
.Ltable:
    .long external-.
    .quad external+7
.Lfinish:
    .byte 0x66
    nop
    movl external(,%eax,4),%ecx
    ret
.size mixed,.-mixed
.section .text.bytes,"ax",@progbits
.globl byte_function
.type byte_function,@function
byte_function:
    .byte 0xc3
.size byte_function,1
.section .init,"ax",@progbits
    nop
.section .fini,"ax",@progbits
    nop
.section .note.GNU-stack,"",@progbits
