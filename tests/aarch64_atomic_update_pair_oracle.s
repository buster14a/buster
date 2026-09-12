// Independent mnemonic spelling for the expanded update words used by the
// machine regression. Assemble with Clang/LLVM or GNU as for AArch64; this is
// not a restatement of production bytes through .word directives.
.text
.p2align 2
.globl atomic_pair_cas_acq_rel_oracle
atomic_pair_cas_acq_rel_oracle:
.Lretry:
    ldr x11, [x28]
    ldr x12, [x28, #8]
    ldr x15, [x28, #16]
    ldr x17, [x28, #24]
    ldaxp x9, x14, [x10]
    cmp x9, x11
    ccmp x14, x12, #0, eq
    csel x11, x15, x9, eq
    csel x12, x17, x14, eq
    stlxp w13, x11, x12, [x10]
    cbnz w13, .Lretry
    str x9, [x28, #32]
    str x14, [x28, #40]
    ret

.globl atomic_pair_arithmetic_words_oracle
atomic_pair_arithmetic_words_oracle:
    adds x11, x9, x11
    adc x12, x14, x12
    subs x11, x9, x11
    sbc x12, x14, x12
    and x11, x9, x11
    and x12, x14, x12
    orr x11, x9, x11
    orr x12, x14, x12
    eor x11, x9, x11
    eor x12, x14, x12
    ldxp x9, x14, [x10]
    stxp w13, x11, x12, [x10]
    ret
