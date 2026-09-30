        .data
        .globl relocation_table
        .p2align 3
relocation_table:
        .rept 65536
        .quad relocation_marker
        .endr
