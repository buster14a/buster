.text
.globl _start
_start:
adrp x0, object+8
add x0, x0, :lo12:object+8
adrp x1, object+8
ldr x2, [x1, :lo12:object+8]
adrp x3, object-8
add x3, x3, :lo12:object-8
adrp x4, object-8
ldr x5, [x4, :lo12:object-8]
ret
