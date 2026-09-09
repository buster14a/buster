.text
.globl clear_sequence
clear_sequence:
    and x11, x9, #0xfffffffffffffffc
    mov x9, x11
.Ldata:
    cmp x9, x10
    b.hs .Ldata_done
    dc cvau, x9
    add x9, x9, #4
    b .Ldata
.Ldata_done:
    dsb ish
    mov x9, x11
.Linstruction:
    cmp x9, x10
    b.hs .Linstruction_done
    ic ivau, x9
    add x9, x9, #4
    b .Linstruction
.Linstruction_done:
    dsb ish
    isb
