// Hosted-only independent assembler witness fixture for issue #3132.
// Assemble with pinned Clang/LLVM 21 for AArch64, enabling LSE and RCpc, and
// compare each emitted instruction word with the adjacent comment. No local
// build or assembler run is implied by this checked-in source fixture.
.text
// 01: pinned LLVM MC sample; expected word 0x08207ca2
casp w0, w1, w2, w3, [x5]
// 02: pinned LLVM MC sample; expected word 0x08207ca2
casp w0, w1, w2, w3, [x5, #0]
// 03: pinned LLVM MC sample; expected word 0x08247fe6
casp w4, w5, w6, w7, [sp]
// 04: pinned LLVM MC sample; expected word 0x48207c42
casp x0, x1, x2, x3, [x2]
// 05: pinned LLVM MC sample; expected word 0x48247fe6
casp x4, x5, x6, x7, [sp]
// 06: pinned LLVM MC sample; expected word 0x08607ca2
caspa w0, w1, w2, w3, [x5]
// 07: pinned LLVM MC sample; expected word 0x08647fe6
caspa w4, w5, w6, w7, [sp]
// 08: pinned LLVM MC sample; expected word 0x48607c42
caspa x0, x1, x2, x3, [x2]
// 09: pinned LLVM MC sample; expected word 0x48647fe6
caspa x4, x5, x6, x7, [sp]
// 10: pinned LLVM MC sample; expected word 0x0820fca2
caspl w0, w1, w2, w3, [x5]
// 11: pinned LLVM MC sample; expected word 0x0824ffe6
caspl w4, w5, w6, w7, [sp]
// 12: pinned LLVM MC sample; expected word 0x4820fc42
caspl x0, x1, x2, x3, [x2]
// 13: pinned LLVM MC sample; expected word 0x4824ffe6
caspl x4, x5, x6, x7, [sp]
// 14: pinned LLVM MC sample; expected word 0x0860fca2
caspal w0, w1, w2, w3, [x5]
// 15: pinned LLVM MC sample; expected word 0x0864ffe6
caspal w4, w5, w6, w7, [sp]
// 16: pinned LLVM MC sample; expected word 0x4860fc42
caspal x0, x1, x2, x3, [x2]
// 17: pinned LLVM MC sample; expected word 0x4864ffe6
caspal x4, x5, x6, x7, [sp]
// 18: pinned LLVM MC sample; expected word 0x38bfc000
ldaprb w0, [x0]
// 19: pinned LLVM MC sample; expected word 0x38bfc000
ldaprb w0, [x0, #0]
// 20: pinned LLVM MC sample; expected word 0x78bfc220
ldaprh w0, [x17]
// 21: pinned LLVM MC sample; expected word 0xb8bfc012
ldapr w18, [x0]
// 22: pinned LLVM MC sample; expected word 0xf8bfc00f
ldapr x15, [x0]
// 23: pinned LLVM MC sample; expected word 0xb8410883
ldtr w3, [x4, #16]
// 24: pinned LLVM MC sample; expected word 0xf8410883
ldtr x3, [x4, #16]
// 25: pinned LLVM MC sample; expected word 0x38410883
ldtrb w3, [x4, #16]
// 26: pinned LLVM MC sample; expected word 0x38c00869
ldtrsb w9, [x3]
// 27: pinned LLVM MC sample; expected word 0x38880be2
ldtrsb x2, [sp, #128]
// 28: pinned LLVM MC sample; expected word 0x78410883
ldtrh w3, [x4, #16]
// 29: pinned LLVM MC sample; expected word 0x78c20be3
ldtrsh w3, [sp, #32]
// 30: pinned LLVM MC sample; expected word 0x78818925
ldtrsh x5, [x9, #24]
// 31: pinned LLVM MC sample; expected word 0xb8980be9
ldtrsw x9, [sp, #-128]
// 32: pinned LLVM MC sample; expected word 0xb8014885
sttr w5, [x4, #20]
// 33: pinned LLVM MC sample; expected word 0xf8000864
sttr x4, [x3]
// 34: pinned LLVM MC sample; expected word 0x38000864
sttrb w4, [x3]
// 35: pinned LLVM MC sample; expected word 0x78020be2
sttrh w2, [sp, #32]
// 36: bare-offset or imm9 endpoint probe; expected word 0xb8410883
ldtr w3, [x4, 16]
// 37: bare-offset or imm9 endpoint probe; expected word 0xb8014885
sttr w5, [x4, 20]
// 38: bare-offset or imm9 endpoint probe; expected word 0xb8500820
ldtr w0, [x1, #-256]
// 39: bare-offset or imm9 endpoint probe; expected word 0xb8500820
ldtr w0, [x1, -256]
// 40: bare-offset or imm9 endpoint probe; expected word 0xb84ff820
ldtr w0, [x1, #255]
// 41: bare-offset or imm9 endpoint probe; expected word 0xb84ff820
ldtr w0, [x1, 255]
// 42: bare-offset or imm9 endpoint probe; expected word 0xf8100820
sttr x0, [x1, #-256]
// 43: bare-offset or imm9 endpoint probe; expected word 0xf8100820
sttr x0, [x1, -256]
// 44: bare-offset or imm9 endpoint probe; expected word 0xf80ff820
sttr x0, [x1, #255]
// 45: bare-offset or imm9 endpoint probe; expected word 0xf80ff820
sttr x0, [x1, 255]
