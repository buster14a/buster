# Independent GNU as / LLVM integrated-assembler oracle for issue #267.
# No .byte instruction literals and no local definition of the branch target:
# a numeric zero would allow a short jump and conceal the relocation contract.
.section .text.forward,"ax",@progbits
.globl forward_atexit, forward_quick_exit
.type forward_atexit,@function
forward_atexit:
    xor %esi, %esi
    xor %edx, %edx
    jmp __cxa_atexit
.size forward_atexit, .-forward_atexit
.type forward_quick_exit,@function
forward_quick_exit:
    xor %esi, %esi
    xor %edx, %edx
    jmp __cxa_at_quick_exit
.size forward_quick_exit, .-forward_quick_exit
.section .text.jump,"ax",@progbits
.globl jump_atexit, jump_quick_exit
.type jump_atexit,@function
jump_atexit:
    jmp _crt_atexit
.size jump_atexit, .-jump_atexit
.type jump_quick_exit,@function
jump_quick_exit:
    jmp _crt_at_quick_exit
.size jump_quick_exit, .-jump_quick_exit
.section .note.GNU-stack,"",@progbits
