// Independent Windows ARM64 variadic ABI probes. There are no native OS calls
// or exception handlers. Assemble with Clang for Windows ARM64 and define
// BUSTER_PLATFORM_VA_ABI_PROBE=1 when compiling both this file and the C fixture.
#if BUSTER_PLATFORM_VA_ABI_PROBE
.text

.macro boundary_check reg, value
    cmp \reg, #\value
    cset w10, eq
    and w9, w9, w10
.endm

.macro boundary_stack offset, value
    ldr x8, [sp, #\offset]
    boundary_check x8, \value
.endm

.macro boundary_six_registers
    boundary_check x1, 11
    boundary_check x2, 22
    boundary_check x3, 33
    boundary_check x4, 44
    boundary_check x5, 55
    boundary_check x6, 66
.endm

.globl windows_va_asm_split
.p2align 2
windows_va_asm_split:
    mov w9, #1
    boundary_check w0, 6
    boundary_six_registers
    boundary_check x7, 71
    boundary_stack 0, 83
    boundary_stack 8, 97
    mov w0, w9
    ret

.globl windows_va_asm_i128_register
.p2align 2
windows_va_asm_i128_register:
    mov w9, #1
    boundary_check w0, 0
    // X1 is padding. The sixteen-byte argument starts at even register X2.
    boundary_check x2, 71
    boundary_check x3, 83
    boundary_check x4, 97
    mov w0, w9
    ret

.globl windows_va_asm_i128_overflow
.p2align 2
windows_va_asm_i128_overflow:
    mov w9, #1
    boundary_check w0, 6
    boundary_six_registers
    // X7 is padding; the aligned pair begins at the incoming stack pointer.
    boundary_stack 0, 71
    boundary_stack 8, 83
    boundary_stack 16, 97
    mov w0, w9
    ret

.globl windows_va_asm_i128_stack
.p2align 2
windows_va_asm_i128_stack:
    mov w9, #1
    boundary_check w0, 8
    boundary_six_registers
    boundary_check x7, 77
    boundary_stack 0, 88
    // SP+8 is padding; the pair must begin at the next sixteen-byte address.
    boundary_stack 16, 71
    boundary_stack 24, 83
    boundary_stack 32, 97
    mov w0, w9
    ret

.globl windows_va_asm_named_pair
.p2align 2
windows_va_asm_named_pair:
    mov w9, #1
    boundary_check x0, 1
    boundary_check x1, 2
    boundary_check x2, 3
    boundary_check x3, 4
    boundary_check x4, 5
    boundary_check x5, 6
    boundary_check x6, 7
    boundary_check x7, 71
    boundary_stack 0, 83
    ldr w8, [sp, #8]
    boundary_check w8, 11
    boundary_stack 16, 97
    ldr x8, [sp, #24]
    movz x11, #0x4004, lsl #48
    cmp x8, x11
    cset w10, eq
    and w0, w9, w10
    ret

.macro boundary_record
    cmp w0, #1
    cset w8, ne
    orr w19, w19, w8
.endm

.macro boundary_produce_six
    mov x1, #11
    mov x2, #22
    mov x3, #33
    mov x4, #44
    mov x5, #55
    mov x6, #66
.endm

.globl windows_va_probe_all
.p2align 2
windows_va_probe_all:
    // Forty bytes of outgoing arguments, with callee-save homes above them.
    sub sp, sp, #64
    stp x19, x30, [sp, #48]
    mov w19, #0

    mov w0, #6
    boundary_produce_six
    mov x7, #71
    mov x8, #83
    mov x9, #97
    stp x8, x9, [sp]
    bl windows_va_split
    boundary_record

    mov w0, #0
    mov x1, #999
    mov x2, #71
    mov x3, #83
    mov x4, #97
    bl windows_va_i128
    boundary_record

    mov w0, #6
    boundary_produce_six
    mov x7, #999
    mov x8, #71
    mov x9, #83
    stp x8, x9, [sp]
    mov x8, #97
    str x8, [sp, #16]
    bl windows_va_i128
    boundary_record

    mov w0, #8
    boundary_produce_six
    mov x7, #77
    mov x8, #88
    mov x9, #999
    stp x8, x9, [sp]
    mov x8, #71
    mov x9, #83
    stp x8, x9, [sp, #16]
    mov x8, #97
    str x8, [sp, #32]
    bl windows_va_i128
    boundary_record

    mov x0, #1
    mov x1, #2
    mov x2, #3
    mov x3, #4
    mov x4, #5
    mov x5, #6
    mov x6, #7
    mov x7, #71
    mov x8, #83
    mov x9, #11
    stp x8, x9, [sp]
    mov x8, #97
    movz x9, #0x4004, lsl #48
    stp x8, x9, [sp, #16]
    bl windows_va_named_pair
    boundary_record

    mov w0, w19
    ldp x19, x30, [sp, #48]
    add sp, sp, #64
    ret
#endif
