    .text
    .globl my_setjmp
    .type my_setjmp, @function
my_setjmp:
    jmp _setjmp
    .size my_setjmp, .-my_setjmp
    .section .note.GNU-stack,"",@progbits
