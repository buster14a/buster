.text
.globl main
.p2align 2
main:
    adrp x8, .Ldata
    add x8, x8, :lo12:.Ldata
    mov w0, #0
    ret
.section .rodata.cst8,"aM",@progbits,8
.Ldata:
    .quad 0x8000000000000000
