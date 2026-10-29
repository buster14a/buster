// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// What the x86 forms a code generator emits mean to the compiler (#221). The encoder (x86_64_encoder.c) writes a record
// already selected and trusts it; this file says what the record does, and which record keeps the value a selector asks
// for, in tables of its own keyed by X86Form, so that X86Instruction stays the encoder's 16 bytes:
//   - Effects (X86Effects, one per covered form): which explicit operands, by encoding role (reg, rm and vvvv), the form
//     reads and writes (one both read and written is tied), the fixed registers it reads and writes besides them (div's
//     RDX:RAX, a shift's CL, push's RSP), how much of a destination register it defines and what happens to the rest (an
//     8- or 16-bit write keeps it, a 32-bit one zeroes it, SSE's scalar operations keep the rest of the XMM register, and
//     legacy SSE keeps the bits above 127), the flags it reads, defines and leaves undefined, memory (an operand read or
//     written, lea's address alone, the stack, alignment, atomicity, fences), control transfers, faults, MXCSR, and how
//     its immediate's bits become a value. A form outside the covered subset maps to a conservative record that reads and
//     writes everything, so a missing row never looks free of effects. The effects are architectural: a call's clobbers
//     are the calling convention's (#137).
//   - The immediate's contract: a selector asks for a value (an integer of the operation's width), and a form's field
//     holds bits that the form extends (x86_immediate_encodes, x86_immediate_field and x86_immediate_value). add rax, 200
//     needs an imm32: an imm8 of 0xc8 encodes, but adds -56.
//   - Operand-resolved effects (x86_instructions_effects, AVX-512, 8 records at a time, and its scalar twin
//     x86_instructions_effects_scalar): each record's registers, flags and memory read and written, as two 64-bit sets
//     (BUSTER_X86_RESOURCE_*), with what its operands decide: a memory operand's base and index, a zero idiom (xor eax,
//     eax reads nothing), a shift by an immediate (by zero, the flags stay).
//   - Form choice (x86_choose_forms, AVX-512, 32 requests at a time, and its scalar twin x86_choose_forms_scalar): for a
//     machine operation with an immediate (a row: the operation and its width), the first of the row's candidate forms
//     whose conditions the request meets: the value's class (it fits a sign-extended imm8, an imm32, ...), the
//     destination (a register, RAX) and the flags live after it. Encodings of the same instruction come shortest first;
//     another instruction (inc for add 1, xor for mov 0, test r, r for cmp 0, a narrower test) only where the live flags
//     allow, which initialization works out from the effects (x86_flags_substitutable). Which forms the generated code
//     may use is #180's, and which sequence is faster #181's: the rows hold baseline forms only.
// Entry points: x86_semantics_initialize (builds the tables; call it once), x86_effects, x86_effects_role,
// x86_immediate_encodes, x86_immediate_field, x86_immediate_value, x86_instruction_flags, x86_flags_substitutable (and
// x86_instruction_flags_substitutable), x86_instructions_effects (and _scalar), x86_choice_row, x86_choose_forms (and
// _scalar) and x86_choice_instruction.
// docs/x86-semantics.md has the contract, the covered subset, how to add a form and the 9700X's numbers. Files that use
// it include it after the encoder; every function in it is static.
//
// Layout, in this order:
//   - Effects: the flags (BUSTER_X86_FLAG_*), the explicit operands' roles (BUSTER_X86_ROLE_*) and what a write does to
//     the rest of a register (BUSTER_X86_REST_*), memory (BUSTER_X86_MEMORY_EFFECT_*), the properties
//     (BUSTER_X86_EFFECT_*), the immediate's kinds (BUSTER_X86_IMMEDIATE_*), control (BUSTER_X86_CONTROL_*),
//     X86Operation, X86Effects, the covered forms (BUSTER_X86_EFFECT_ROWS, with X86Pattern), the tables
//     (x86_effects_index, x86_effects_by_form and x86_effects_records), x86_semantics_row, x86_effects and
//     x86_effects_role.
//   - The immediate's contract (x86_immediate_encodes, x86_immediate_field and x86_immediate_value) and the flags
//     (x86_instruction_flags, X86FlagEffects, x86_flags_substitutable and x86_instruction_flags_substitutable).
//   - Operand-resolved effects: BUSTER_X86_RESOURCE_*, x86_instruction_effects_scalar, x86_instructions_effects_scalar
//     and x86_instructions_effects.
//   - Form choice: the classes and the context (BUSTER_X86_CHOICE_* and BUSTER_X86_CONTEXT_*), the rows
//     (BUSTER_X86_CHOICE_SOURCES, X86ChoiceSource), X86ChoiceRow and its tables, x86_choice_row, x86_choice_have (the
//     scalar classification), x86_choose_forms_scalar, x86_choice_classes (the vector one), x86_choose_forms and
//     x86_choice_instruction.
//   - Initialization: x86_effects_slots, x86_effects_build, x86_choice_build and x86_semantics_initialize.

// ---------------------------------------------------------------------------------------------------------------------
// Effects

// The flags, as X86Effects's masks have them: the status flags in RFLAGS's order, then DF.
#define BUSTER_X86_FLAG_CF 0x01
#define BUSTER_X86_FLAG_PF 0x02
#define BUSTER_X86_FLAG_AF 0x04
#define BUSTER_X86_FLAG_ZF 0x08
#define BUSTER_X86_FLAG_SF 0x10
#define BUSTER_X86_FLAG_OF 0x20
#define BUSTER_X86_FLAG_DF 0x40
#define BUSTER_X86_FLAGS_STATUS 0x3f
#define BUSTER_X86_FLAGS_ALL 0x7f
// What the logical instructions define (CF and OF as zero): all the status flags but AF, which they leave undefined.
#define BUSTER_X86_FLAGS_LOGICAL (BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_AF)
// What inc and dec define: all but CF, which they keep.
#define BUSTER_X86_FLAGS_STEP (BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_CF)
// What multiplication defines (and rotation affects): CF and OF.
#define BUSTER_X86_FLAGS_CO (BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_OF)
#define BUSTER_X86_FLAGS_SZAP (BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAGS_CO)

// A general register as an implicit operand's bit (X86Effects.implicit_reads and implicit_writes).
#define BUSTER_X86_GPR(name) (1u << X86_REGISTER_##name)

// The explicit operands by encoding role, as X86Instruction has them: ModRM.reg, ModRM.rm (or a +r form's register, or
// a memory operand) and VEX.vvvv. X86Effects.operands has BUSTER_X86_ROLE_BITS bits per role: whether the form reads the
// operand and writes it (both: a tied operand), what a register write does to the rest of the register
// (BUSTER_X86_REST_*), and whether the register is a vector one (XMM, YMM or ZMM; otherwise a general register). With a
// memory operand in rm, READ and WRITE are the memory's: a load and a store (but for lea, which reads neither:
// BUSTER_X86_MEMORY_EFFECT_ADDRESS).
#define BUSTER_X86_ROLE_REG 0
#define BUSTER_X86_ROLE_RM 1
#define BUSTER_X86_ROLE_VVVV 2
#define BUSTER_X86_ROLES 3
#define BUSTER_X86_ROLE_BITS 5
#define BUSTER_X86_ROLE_READ 0x01
#define BUSTER_X86_ROLE_WRITE 0x02
#define BUSTER_X86_ROLE_REST_SHIFT 2
#define BUSTER_X86_ROLE_REST 0x0c
#define BUSTER_X86_ROLE_VECTOR 0x10
// What a register write does to the register's bits past the ones the form defines (X86Effects.write_bytes): nothing to
// keep (a 64-bit write, or a whole XMM register's); zeroes them (a 32-bit general register; movd, movq and a load of
// movss or movsd into an XMM register); keeps them (an 8- or 16-bit general register, AH among them; SSE's scalar
// operations, whose element is the low 4 or 8 bytes), so the write reads the register too; or keeps them when rm is a
// register and zeroes them when it is memory (movss's and movsd's loads). Legacy SSE forms keep the bits above 127 too
// (BUSTER_X86_EFFECT_KEEP_UPPER).
#define BUSTER_X86_REST_WHOLE 0
#define BUSTER_X86_REST_ZERO 1
#define BUSTER_X86_REST_MERGE 2
#define BUSTER_X86_REST_MERGE_IF_REGISTER 3

// X86Effects.memory: what a memory operand (in rm) is, and the memory the form reaches without one.
//   - ADDRESS: rm's address is computed (from its base and index) but not accessed: lea.
//   - ALIGNED: a memory operand must be aligned to its size or the instruction faults (legacy SSE's 16-byte operands).
//   - STACK_READ and STACK_WRITE: reads [rsp] (pop, ret; leave's [rbp]) or writes below it (push and call's [rsp - 8],
//     pushw's [rsp - 2]).
//   - ATOMIC: a memory operand is read and written in one locked access (lock, xchg with memory), which orders every
//     memory access around it.
//   - FENCE: orders memory accesses, without an operand (mfence).
//   - BIT_STRING: bt's register offset reaches past the operand, to the bit at that offset from its address.
#define BUSTER_X86_MEMORY_EFFECT_ADDRESS 0x01
#define BUSTER_X86_MEMORY_EFFECT_ALIGNED 0x02
#define BUSTER_X86_MEMORY_EFFECT_STACK_READ 0x04
#define BUSTER_X86_MEMORY_EFFECT_STACK_WRITE 0x08
#define BUSTER_X86_MEMORY_EFFECT_ATOMIC 0x10
#define BUSTER_X86_MEMORY_EFFECT_FENCE 0x20
#define BUSTER_X86_MEMORY_EFFECT_BIT_STRING 0x40
#define BUSTER_X86_MEMORY_EFFECTS_ALL 0x7f

// X86Effects.properties:
//   - FAULT: can fault without a memory operand: a divide error (div, idiv), #UD (ud2), #BP (int3). (Any memory access
//     can fault too.)
//   - ZERO_IDIOM: with the same register in its two sources (reg and rm; a VEX or EVEX form's vvvv and rm, which write
//     reg), the result is zero whatever the register held, so it reads nothing of what it writes (xor, sub and their
//     vector kin; for a general register only where the write leaves no old bits). A legacy SSE one keeps its
//     register's bits above 127 (KEEP_UPPER), which its resolved effects read.
//   - KEEP_UPPER: a legacy SSE form: a vector register it writes keeps its bits above 127. It is the top bit, bit 63 of
//     the record's first 8 bytes, which x86_instructions_effects spreads over a lane with one arithmetic shift.
//   - MXCSR: reads MXCSR's controls (rounding, DAZ and FTZ).
//   - FP_EXCEPTION: may raise floating-point exceptions (setting MXCSR's status flags, or an x87 form's in its status
//     word, or trapping if unmasked).
//   - COUNT: a shift or rotate by a count (CL, or an immediate that x86_instruction_flags reads): a count of zero (after
//     masking) leaves the flags as they were, and one other than 1 leaves OF undefined. X86Effects describes any count,
//     so it reads the flags it may keep and leaves undefined those it may change; those are kept for a count of zero, so
//     x86_flags_substitutable doesn't take them for flags nothing may rely on.
//   - ZERO_SOURCE: bsf and bsr: with a zero source the destination is undefined (Intel) or kept (AMD), so it is read.
//   - CONDITIONAL: writes its destination (cmov), or rm and RAX (cmpxchg), only under a condition; cmov's 32-bit form
//     zeroes the upper half either way, and cmpxchg's RAX keeps all of its bits when it isn't written.
//   - X87: an x87 form: reads and writes the x87's state (its registers, the stack's top and tags, and its status word,
//     whose condition codes every x87 instruction may change) and reads its control word (precision and rounding). An
//     x87 register operand (ST(i)) is part of that state, not a role. X87_CONTROL: writes the control word too (fldcw).
//   - ZERO_UPPER: zeroes the bits above 127 of ZMM0 to ZMM15 and keeps the rest (vzeroupper): reads and writes them.
#define BUSTER_X86_EFFECT_FAULT 0x0001
#define BUSTER_X86_EFFECT_ZERO_IDIOM 0x0002
#define BUSTER_X86_EFFECT_MXCSR 0x0008
#define BUSTER_X86_EFFECT_FP_EXCEPTION 0x0010
#define BUSTER_X86_EFFECT_COUNT 0x0020
#define BUSTER_X86_EFFECT_ZERO_SOURCE 0x0040
#define BUSTER_X86_EFFECT_CONDITIONAL 0x0080
#define BUSTER_X86_EFFECT_X87 0x0100
#define BUSTER_X86_EFFECT_X87_CONTROL 0x0200
#define BUSTER_X86_EFFECT_ZERO_UPPER 0x0400
#define BUSTER_X86_EFFECT_KEEP_UPPER 0x8000

// How the form's immediate becomes a value (X86Effects.kinds's low nibble; NONE without an immediate):
//   - VALUE: an integer operand: the field's bytes sign-extended to the form's width (an imm8 in add rax, imm8 is -128
//     to 127; an imm32 in a 32-bit form is any 32-bit pattern).
//   - COUNT: a shift's or rotate's count: the field masked to 5 bits, or 6 for a 64-bit operand.
//   - BIT: bt's bit index: the field masked to the width's bits.
//   - CONTROL: bits the instruction decodes, the field as an unsigned number: ret's byte count, roundsd's mode, cmpsd's
//     predicate.
#define BUSTER_X86_IMMEDIATE_NONE 0
#define BUSTER_X86_IMMEDIATE_VALUE 1
#define BUSTER_X86_IMMEDIATE_COUNT 2
#define BUSTER_X86_IMMEDIATE_BIT 3
#define BUSTER_X86_IMMEDIATE_CONTROL 4

// Where control goes (X86Effects.kinds's high nibble): on (NONE), to a jump's target, to a conditional branch's target
// or on, to a call's target and back, to the return address, to a trap's handler (int3, ud2), or anywhere (an uncovered
// form).
#define BUSTER_X86_CONTROL_NONE 0
#define BUSTER_X86_CONTROL_JUMP 1
#define BUSTER_X86_CONTROL_BRANCH 2
#define BUSTER_X86_CONTROL_CALL 3
#define BUSTER_X86_CONTROL_RETURN 4
#define BUSTER_X86_CONTROL_TRAP 5
#define BUSTER_X86_CONTROL_UNKNOWN 6

// What a covered form computes, for its consumers to group forms by (and the form choice's rows).
typedef enum X86Operation
{
    X86_OPERATION_NONE,
    // The arithmetic group, in its ModRM digits' order.
    X86_OPERATION_ADD,
    X86_OPERATION_OR,
    X86_OPERATION_ADC,
    X86_OPERATION_SBB,
    X86_OPERATION_AND,
    X86_OPERATION_SUB,
    X86_OPERATION_XOR,
    X86_OPERATION_CMP,
    X86_OPERATION_TEST,
    X86_OPERATION_MOV,
    X86_OPERATION_MOVZX,
    // movsx and movsxd.
    X86_OPERATION_MOVSX,
    X86_OPERATION_LEA,
    X86_OPERATION_XCHG,
    X86_OPERATION_NOT,
    X86_OPERATION_NEG,
    X86_OPERATION_INC,
    X86_OPERATION_DEC,
    X86_OPERATION_MUL,
    // The one-operand form (RDX:RAX), the two-operand one and the one with an immediate.
    X86_OPERATION_IMUL,
    X86_OPERATION_DIV,
    X86_OPERATION_IDIV,
    X86_OPERATION_ROL,
    X86_OPERATION_ROR,
    X86_OPERATION_SHL,
    X86_OPERATION_SHR,
    X86_OPERATION_SAR,
    X86_OPERATION_PUSH,
    X86_OPERATION_POP,
    X86_OPERATION_CALL,
    X86_OPERATION_JMP,
    X86_OPERATION_JCC,
    X86_OPERATION_SETCC,
    X86_OPERATION_CMOVCC,
    X86_OPERATION_RET,
    X86_OPERATION_LEAVE,
    // cbw, cwde and cdqe: the accumulator's lower half sign-extended into it.
    X86_OPERATION_SIGN_EXTEND,
    // cwd, cdq and cqo: the accumulator's sign into RDX (EDX, DX).
    X86_OPERATION_SIGN_SPLIT,
    X86_OPERATION_NOP,
    // int3 and ud2.
    X86_OPERATION_TRAP,
    X86_OPERATION_ENDBR,
    X86_OPERATION_FENCE,
    X86_OPERATION_BSWAP,
    X86_OPERATION_BT,
    X86_OPERATION_BSF,
    X86_OPERATION_BSR,
    X86_OPERATION_TZCNT,
    X86_OPERATION_LZCNT,
    X86_OPERATION_POPCNT,
    X86_OPERATION_CMPXCHG,
    X86_OPERATION_XADD,
    // SSE and SSE2, and SSE4.1's roundss and roundsd: movss and movsd.
    X86_OPERATION_FMOVE,
    X86_OPERATION_FADD,
    X86_OPERATION_FSUB,
    X86_OPERATION_FMUL,
    X86_OPERATION_FDIV,
    X86_OPERATION_FMIN,
    X86_OPERATION_FMAX,
    X86_OPERATION_FSQRT,
    X86_OPERATION_FROUND,
    // ucomiss and ucomisd, comiss and comisd (which signal on a quiet NaN too), and cmpss and cmpsd (a mask).
    X86_OPERATION_FCOMPARE,
    X86_OPERATION_FCOMPARE_SIGNALING,
    X86_OPERATION_FCOMPARE_MASK,
    // cvtsi2ss and cvtsi2sd, cvttss2si and cvttsd2si, and cvtss2sd and cvtsd2ss.
    X86_OPERATION_INT_TO_FLOAT,
    X86_OPERATION_FLOAT_TO_INT,
    X86_OPERATION_FLOAT_TO_FLOAT,
    // movd and movq, between general and XMM registers or memory, or between XMM registers.
    X86_OPERATION_MOVD,
    // movaps, movups, movapd, movupd, movdqa and movdqu.
    X86_OPERATION_VECTOR_MOVE,
    X86_OPERATION_VECTOR_AND,
    X86_OPERATION_VECTOR_ANDN,
    X86_OPERATION_VECTOR_OR,
    X86_OPERATION_VECTOR_XOR,
    X86_OPERATION_SHLD,
    X86_OPERATION_SHRD,
    // vzeroupper.
    X86_OPERATION_ZERO_UPPER,
    // stmxcsr.
    X86_OPERATION_STORE_MXCSR,
    // The x87: fld, fild and the constants (fldz, fld1); fst, fstp, fist and fistp; the arithmetic (fadd, fsub, fsubr,
    // fmul, fdiv and fdivr, with a pop or without, fchs and fabs); fcmovcc; fcomi, fcomip, fucomi and fucomip, which
    // set EFLAGS; and fldcw and fnstcw.
    X86_OPERATION_X87_LOAD,
    X86_OPERATION_X87_STORE,
    X86_OPERATION_X87_ARITHMETIC,
    X86_OPERATION_X87_CMOVCC,
    X86_OPERATION_X87_COMPARE,
    X86_OPERATION_X87_CONTROL,
    X86_OPERATION_COUNT,
} X86Operation;

// A form's effects, 16 bytes: x86_effects's answer.
typedef struct X86Effects X86Effects;
struct X86Effects
{
    // General registers read and written besides the explicit operands: RAX = bit 0 ... R15 = bit 15. An implicit write
    // of a form narrower than 4 bytes (width) keeps the register's other bits, so the register is a read too.
    uint16_t implicit_reads;
    uint16_t implicit_writes;
    // BUSTER_X86_ROLE_BITS per role, BUSTER_X86_ROLE_REG's first (x86_effects_role).
    uint16_t operands;
    // BUSTER_X86_EFFECT_*.
    uint16_t properties;
    // The flags (BUSTER_X86_FLAG_*) read, written with a defined value, and changed to an undefined one (or maybe kept:
    // nothing after the instruction may rely on them). A status flag in none of the three is kept.
    uint8_t flags_read;
    uint8_t flags_written;
    uint8_t flags_undefined;
    // BUSTER_X86_MEMORY_EFFECT_*.
    uint8_t memory;
    // X86Operation, X86_OPERATION_NONE for an uncovered form.
    uint8_t operation;
    // The operation's operand size in bytes: an integer form's (the destination register's for movzx, movsx, lea and
    // cvttsd2si), a floating-point one's element (its memory operand's size).
    uint8_t width;
    // How many bytes of a destination register the form defines, the rest as BUSTER_X86_REST_* says; 0 without one.
    uint8_t write_bytes;
    // The immediate's kind (BUSTER_X86_IMMEDIATE_*) in the low nibble and control (BUSTER_X86_CONTROL_*) in the high one.
    uint8_t kinds;
};
_Static_assert(sizeof(X86Effects) == 16, "a form's effects are one XMM register");

// How a form accesses the operands its syntax lists, in that order (the syntax's ACC and CL are the implicit RAX and
// RCX): MODIFY reads and writes the first and reads the rest (add, cmov, a shift's count), READ reads them all (cmp,
// push, a branch through a register), MOVE writes the first and reads the rest (mov, lea, setcc, pop), SWAP reads and
// writes them all (xchg, xadd), and NONE touches none (a multi-byte nop's operand isn't accessed).
typedef enum X86Pattern
{
    X86_PATTERN_NONE,
    X86_PATTERN_MODIFY,
    X86_PATTERN_READ,
    X86_PATTERN_MOVE,
    X86_PATTERN_SWAP,
} X86Pattern;

// A vector destination's write in an effects row: the rest's kind (BUSTER_X86_REST_*) and the bytes defined (to 64).
#define BUSTER_X86_VECTOR_WRITE(rest, bytes) ((BUSTER_X86_REST_##rest) << 8 | (bytes))

#define BUSTER_X86_SHIFT_EFFECTS(X, N, written_1, undefined_1, affected) \
    X(N##_RM8_1, N##_RM64_1, N, MODIFY, 0, 0, 0, written_1, undefined_1, 0, 0, 0, NONE, NONE, 0) \
    X(N##_RM8_CL, N##_RM64_CL, N, MODIFY, 0, 0, affected, 0, affected, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_COUNT) \
    X(N##_RM8_IMM8, N##_RM64_IMM8, N, MODIFY, 0, 0, affected, 0, affected, 0, 0, 0, COUNT, NONE, BUSTER_X86_EFFECT_COUNT)

// A condition's jumps, setcc and cmovs, reading the flags the condition tests.
#define BUSTER_X86_CONDITION_EFFECTS(X, CC, flags) \
    X(J##CC##_REL8, J##CC##_REL32, JCC, NONE, 0, 0, flags, 0, 0, 0, 0, 0, NONE, BRANCH, 0) \
    X(SET##CC##_RM8, SET##CC##_RM8, SETCC, MOVE, 0, 0, flags, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(CMOV##CC##_R16_RM16, CMOV##CC##_R64_RM64, CMOVCC, MODIFY, 0, 0, flags, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_CONDITIONAL)

// An x87 conditional move, fcmovcc ST(0), ST(i), reading the flags its condition tests.
#define BUSTER_X86_X87_FCMOV_EFFECTS(X, CC, flags) \
    X(FCMOV##CC##_ST0_ST, FCMOV##CC##_ST0_ST, X87_CMOVCC, NONE, 0, 0, flags, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS)

// An x87 form that may raise an x87 exception: one that computes, converts, compares, or loads or stores a register of
// the stack, whose underflow or overflow is one (but fnstcw and fldcw).
#define BUSTER_X86_X87_EXCEPTIONS (BUSTER_X86_EFFECT_X87 | BUSTER_X86_EFFECT_FP_EXCEPTION)

// The locked arithmetic on memory: the unlocked group's effects, atomic.
#define BUSTER_X86_LOCKED_EFFECTS(X, N, read, written, undefined) \
    X(LOCK_##N##_M8_R8, LOCK_##N##_M64_IMM8, N, MODIFY, 0, 0, read, written, undefined, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, VALUE, NONE, 0)

// Legacy SSE: an operation on a scalar's low element (the rest of the destination kept), 4 or 8 bytes.
#define BUSTER_X86_SSE_SCALAR_EFFECTS(X, form, operation, pattern, bytes) \
    X(form, form, operation, pattern, 0, BUSTER_X86_VECTOR_WRITE(MERGE, bytes), 0, 0, 0, 0, 0, 0, NONE, NONE, \
      BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER)

// Legacy SSE's bitwise operations on whole registers, with an aligned 16-byte memory operand.
#define BUSTER_X86_SSE_LOGIC_EFFECTS(X, form, operation, properties) \
    X(form, form, operation, MODIFY, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ALIGNED, NONE, NONE, \
      BUSTER_X86_EFFECT_KEEP_UPPER | (properties))

// The covered forms, in runs of consecutive X86Forms that share their meaning: X(first, last, operation, pattern, width,
// vector, flags read, written, undefined, implicit reads, implicit writes, memory, immediate, control, properties), for
// the forms first to last:
//   - pattern: X86Pattern;
//   - width: the operation's operand size, or 0 for the destination general register's size, or without one the form's
//     operand_bytes;
//   - vector: a vector destination's write (BUSTER_X86_VECTOR_WRITE), or 0;
//   - implicit reads and writes: BUSTER_X86_GPR masks, which the syntax's ACC and CL add to;
//   - memory: BUSTER_X86_MEMORY_EFFECT_* bits; immediate: BUSTER_X86_IMMEDIATE_*, for the forms with one; control:
//     BUSTER_X86_CONTROL_*; properties: BUSTER_X86_EFFECT_* bits (a zero idiom only where the destination keeps no old
//     bits: initialization drops it from the 8- and 16-bit forms).
// The flags follow Intel's and AMD's manuals, taking the union where they differ (bt's ZF, which Intel keeps and AMD
// leaves undefined, is undefined). docs/x86-semantics.md lists the subset; test-x86-semantics executes every row.
#define BUSTER_X86_EFFECT_ROWS(X) \
    X(ADD_RM8_R8, ADD_RM64_IMM8, ADD, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, VALUE, NONE, 0) \
    X(OR_RM8_R8, OR_RM64_IMM8, OR, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 0, VALUE, NONE, 0) \
    X(ADC_RM8_R8, ADC_RM64_IMM8, ADC, MODIFY, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, VALUE, NONE, 0) \
    X(SBB_RM8_R8, SBB_RM64_IMM8, SBB, MODIFY, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, VALUE, NONE, 0) \
    X(AND_RM8_R8, AND_RM64_IMM8, AND, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 0, VALUE, NONE, 0) \
    X(SUB_RM8_R8, SUB_RM64_IMM8, SUB, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, VALUE, NONE, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    X(XOR_RM8_R8, XOR_RM64_IMM8, XOR, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 0, VALUE, NONE, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    X(CMP_RM8_R8, CMP_RM64_IMM8, CMP, READ, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, VALUE, NONE, 0) \
    X(MOV_RM8_R8, MOV_RM64_IMM32, MOV, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, VALUE, NONE, 0) \
    X(MOVZX_R16_RM8, MOVZX_R64_RM16, MOVZX, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(MOVSX_R16_RM8, MOVSXD_R64_RM32, MOVSX, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(LEA_R16_M, LEA_R64_M, LEA, MOVE, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ADDRESS, NONE, NONE, 0) \
    X(TEST_RM8_R8, TEST_RAX_IMM32, TEST, READ, 0, 0, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, 0, 0, 0, VALUE, NONE, 0) \
    X(XCHG_RM8_R8, XCHG_RM64_R64, XCHG, SWAP, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    X(NOT_RM8, NOT_RM64, NOT, MODIFY, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(NEG_RM8, NEG_RM64, NEG, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, NONE, NONE, 0) \
    X(MUL_RM8, MUL_RM8, MUL, READ, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, 0) \
    X(MUL_RM16, MUL_RM64, MUL, READ, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, NONE, NONE, 0) \
    X(IMUL_RM8, IMUL_RM8, IMUL, READ, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, 0) \
    X(IMUL_RM16, IMUL_RM64, IMUL, READ, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, NONE, NONE, 0) \
    X(DIV_RM8, DIV_RM8, DIV, READ, 0, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, BUSTER_X86_EFFECT_FAULT) \
    X(DIV_RM16, DIV_RM64, DIV, READ, 0, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, NONE, NONE, BUSTER_X86_EFFECT_FAULT) \
    X(IDIV_RM8, IDIV_RM8, IDIV, READ, 0, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, BUSTER_X86_EFFECT_FAULT) \
    X(IDIV_RM16, IDIV_RM64, IDIV, READ, 0, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), BUSTER_X86_GPR(RAX) | BUSTER_X86_GPR(RDX), 0, NONE, NONE, BUSTER_X86_EFFECT_FAULT) \
    X(INC_RM8, INC_RM64, INC, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, 0, NONE, NONE, 0) \
    X(DEC_RM8, DEC_RM64, DEC, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, 0, NONE, NONE, 0) \
    X(IMUL_R16_RM16, IMUL_R64_RM64, IMUL, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0, 0, 0, NONE, NONE, 0) \
    X(IMUL_R16_RM16_IMM8, IMUL_R64_RM64_IMM32, IMUL, MOVE, 0, 0, 0, BUSTER_X86_FLAGS_CO, BUSTER_X86_FLAGS_SZAP, 0, 0, 0, VALUE, NONE, 0) \
    BUSTER_X86_SHIFT_EFFECTS(X, ROL, BUSTER_X86_FLAGS_CO, 0, BUSTER_X86_FLAGS_CO) \
    BUSTER_X86_SHIFT_EFFECTS(X, ROR, BUSTER_X86_FLAGS_CO, 0, BUSTER_X86_FLAGS_CO) \
    BUSTER_X86_SHIFT_EFFECTS(X, SHL, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, BUSTER_X86_FLAGS_STATUS) \
    BUSTER_X86_SHIFT_EFFECTS(X, SHR, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, BUSTER_X86_FLAGS_STATUS) \
    BUSTER_X86_SHIFT_EFFECTS(X, SAR, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF, BUSTER_X86_FLAGS_STATUS) \
    X(PUSH_R64, PUSH_R64, PUSH, READ, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, NONE, NONE, 0) \
    X(POP_R64, POP_R64, POP, MOVE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_READ, NONE, NONE, 0) \
    X(PUSH_RM64, PUSH_RM64, PUSH, READ, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, NONE, NONE, 0) \
    X(POP_RM64, POP_RM64, POP, MOVE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_READ, NONE, NONE, 0) \
    X(PUSH_IMM8, PUSH_IMM32, PUSH, READ, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, VALUE, NONE, 0) \
    X(PUSHW_IMM16, PUSHW_IMM8, PUSH, READ, 2, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, VALUE, NONE, 0) \
    X(CALL_REL32, CALL_RM64, CALL, READ, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_WRITE, NONE, CALL, 0) \
    X(JMP_REL8, JMP_RM64, JMP, READ, 8, 0, 0, 0, 0, 0, 0, 0, NONE, JUMP, 0) \
    BUSTER_X86_CONDITION_EFFECTS(X, O, BUSTER_X86_FLAG_OF) \
    BUSTER_X86_CONDITION_EFFECTS(X, NO, BUSTER_X86_FLAG_OF) \
    BUSTER_X86_CONDITION_EFFECTS(X, B, BUSTER_X86_FLAG_CF) \
    BUSTER_X86_CONDITION_EFFECTS(X, AE, BUSTER_X86_FLAG_CF) \
    BUSTER_X86_CONDITION_EFFECTS(X, E, BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_CONDITION_EFFECTS(X, NE, BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_CONDITION_EFFECTS(X, BE, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_CONDITION_EFFECTS(X, A, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_CONDITION_EFFECTS(X, S, BUSTER_X86_FLAG_SF) \
    BUSTER_X86_CONDITION_EFFECTS(X, NS, BUSTER_X86_FLAG_SF) \
    BUSTER_X86_CONDITION_EFFECTS(X, P, BUSTER_X86_FLAG_PF) \
    BUSTER_X86_CONDITION_EFFECTS(X, NP, BUSTER_X86_FLAG_PF) \
    BUSTER_X86_CONDITION_EFFECTS(X, L, BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF) \
    BUSTER_X86_CONDITION_EFFECTS(X, GE, BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF) \
    BUSTER_X86_CONDITION_EFFECTS(X, LE, BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF) \
    BUSTER_X86_CONDITION_EFFECTS(X, G, BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF) \
    X(RET, RET_IMM16, RET, NONE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RSP), BUSTER_X86_GPR(RSP), BUSTER_X86_MEMORY_EFFECT_STACK_READ, CONTROL, RETURN, 0) \
    X(NOP, NOP_RM32, NOP, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(INT3, INT3, TRAP, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, TRAP, BUSTER_X86_EFFECT_FAULT) \
    X(UD2, UD2, TRAP, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, TRAP, BUSTER_X86_EFFECT_FAULT) \
    X(LEAVE, LEAVE, LEAVE, NONE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RBP), BUSTER_X86_GPR(RSP) | BUSTER_X86_GPR(RBP), BUSTER_X86_MEMORY_EFFECT_STACK_READ, NONE, NONE, 0) \
    X(CBW, CBW, SIGN_EXTEND, NONE, 2, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, 0) \
    X(CWDE, CWDE, SIGN_EXTEND, NONE, 4, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, 0) \
    X(CDQE, CDQE, SIGN_EXTEND, NONE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), 0, NONE, NONE, 0) \
    X(CWD, CWD, SIGN_SPLIT, NONE, 2, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RDX), 0, NONE, NONE, 0) \
    X(CDQ, CDQ, SIGN_SPLIT, NONE, 4, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RDX), 0, NONE, NONE, 0) \
    X(CQO, CQO, SIGN_SPLIT, NONE, 8, 0, 0, 0, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RDX), 0, NONE, NONE, 0) \
    X(ENDBR64, ENDBR64, ENDBR, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(MFENCE, MFENCE, FENCE, NONE, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_FENCE, NONE, NONE, 0) \
    X(BSWAP_R32, BSWAP_R64, BSWAP, MODIFY, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(BT_RM16_R16, BT_RM16_R16, BT, READ, 0, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STEP, 0, 0, BUSTER_X86_MEMORY_EFFECT_BIT_STRING, NONE, NONE, 0) \
    X(BT_RM16_IMM8, BT_RM16_IMM8, BT, READ, 0, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STEP, 0, 0, 0, BIT, NONE, 0) \
    X(BT_RM32_R32, BT_RM64_R64, BT, READ, 0, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STEP, 0, 0, BUSTER_X86_MEMORY_EFFECT_BIT_STRING, NONE, NONE, 0) \
    X(BT_RM32_IMM8, BT_RM64_IMM8, BT, READ, 0, 0, 0, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STEP, 0, 0, 0, BIT, NONE, 0) \
    X(BSF_R16_RM16, BSF_R64_RM64, BSF, MODIFY, 0, 0, 0, BUSTER_X86_FLAG_ZF, BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_ZF, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_SOURCE) \
    X(BSR_R16_RM16, BSR_R64_RM64, BSR, MODIFY, 0, 0, 0, BUSTER_X86_FLAG_ZF, BUSTER_X86_FLAGS_STATUS & ~BUSTER_X86_FLAG_ZF, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_SOURCE) \
    X(TZCNT_R16_RM16, TZCNT_R64_RM64, TZCNT, MOVE, 0, 0, 0, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF, (BUSTER_X86_FLAGS_SZAP & ~BUSTER_X86_FLAG_ZF) | BUSTER_X86_FLAG_OF, 0, 0, 0, NONE, NONE, 0) \
    X(LZCNT_R16_RM16, LZCNT_R64_RM64, LZCNT, MOVE, 0, 0, 0, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF, (BUSTER_X86_FLAGS_SZAP & ~BUSTER_X86_FLAG_ZF) | BUSTER_X86_FLAG_OF, 0, 0, 0, NONE, NONE, 0) \
    X(POPCNT_R16_RM16, POPCNT_R64_RM64, POPCNT, MOVE, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, NONE, NONE, 0) \
    X(LOCK_CMPXCHG_M8_R8, LOCK_CMPXCHG_M64_R64, CMPXCHG, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_GPR(RAX), BUSTER_X86_GPR(RAX), BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, BUSTER_X86_EFFECT_CONDITIONAL) \
    X(LOCK_XADD_M8_R8, LOCK_XADD_M64_R64, XADD, SWAP, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    BUSTER_X86_LOCKED_EFFECTS(X, ADD, 0, BUSTER_X86_FLAGS_STATUS, 0) \
    BUSTER_X86_LOCKED_EFFECTS(X, OR, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF) \
    BUSTER_X86_LOCKED_EFFECTS(X, ADC, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STATUS, 0) \
    BUSTER_X86_LOCKED_EFFECTS(X, SBB, BUSTER_X86_FLAG_CF, BUSTER_X86_FLAGS_STATUS, 0) \
    BUSTER_X86_LOCKED_EFFECTS(X, AND, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF) \
    BUSTER_X86_LOCKED_EFFECTS(X, SUB, 0, BUSTER_X86_FLAGS_STATUS, 0) \
    BUSTER_X86_LOCKED_EFFECTS(X, XOR, 0, BUSTER_X86_FLAGS_LOGICAL, BUSTER_X86_FLAG_AF) \
    X(LOCK_INC_M8, LOCK_INC_M64, INC, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    X(LOCK_DEC_M8, LOCK_DEC_M64, DEC, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STEP, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    X(LOCK_NOT_M8, LOCK_NOT_M64, NOT, MODIFY, 0, 0, 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    X(LOCK_NEG_M8, LOCK_NEG_M64, NEG, MODIFY, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ATOMIC, NONE, NONE, 0) \
    X(MOVSS_X_XM32, MOVSS_X_XM32, FMOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE_IF_REGISTER, 4), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVSS_XM32_X, MOVSS_XM32_X, FMOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 4), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVSD_X_XM64, MOVSD_X_XM64, FMOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE_IF_REGISTER, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVSD_XM64_X, MOVSD_XM64_X, FMOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, ADDSS_X_XM32, FADD, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, ADDSD_X_XM64, FADD, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MULSS_X_XM32, FMUL, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MULSD_X_XM64, FMUL, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, SUBSS_X_XM32, FSUB, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, SUBSD_X_XM64, FSUB, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MINSS_X_XM32, FMIN, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MINSD_X_XM64, FMIN, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, DIVSS_X_XM32, FDIV, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, DIVSD_X_XM64, FDIV, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MAXSS_X_XM32, FMAX, MODIFY, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, MAXSD_X_XM64, FMAX, MODIFY, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, SQRTSS_X_XM32, FSQRT, MOVE, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, SQRTSD_X_XM64, FSQRT, MOVE, 8) \
    X(UCOMISS_X_XM32, UCOMISD_X_XM64, FCOMPARE, READ, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION) \
    X(COMISS_X_XM32, COMISD_X_XM64, FCOMPARE_SIGNALING, READ, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSI2SS_X_RM32, INT_TO_FLOAT, MOVE, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSI2SS_X_RM64, INT_TO_FLOAT, MOVE, 4) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSI2SD_X_RM32, INT_TO_FLOAT, MOVE, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSI2SD_X_RM64, INT_TO_FLOAT, MOVE, 8) \
    X(CVTTSS2SI_R32_XM32, CVTTSD2SI_R64_XM64, FLOAT_TO_INT, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSS2SD_X_XM32, FLOAT_TO_FLOAT, MOVE, 8) \
    BUSTER_X86_SSE_SCALAR_EFFECTS(X, CVTSD2SS_X_XM64, FLOAT_TO_FLOAT, MOVE, 4) \
    X(MOVAPS_X_XM, MOVAPS_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ALIGNED, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVUPS_X_XM, MOVUPS_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVAPD_X_XM, MOVAPD_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ALIGNED, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVUPD_X_XM, MOVUPD_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVDQA_X_XM, MOVDQA_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, BUSTER_X86_MEMORY_EFFECT_ALIGNED, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVDQU_X_XM, MOVDQU_XM_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, XORPS_X_XM128, VECTOR_XOR, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, XORPD_X_XM128, VECTOR_XOR, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ANDPS_X_XM128, VECTOR_AND, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ANDPD_X_XM128, VECTOR_AND, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ANDNPS_X_XM128, VECTOR_ANDN, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ORPS_X_XM128, VECTOR_OR, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, PXOR_X_XM128, VECTOR_XOR, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, PAND_X_XM128, VECTOR_AND, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, PANDN_X_XM128, VECTOR_ANDN, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, POR_X_XM128, VECTOR_OR, 0) \
    X(MOVD_X_RM32, MOVD_X_RM32, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 4), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVD_RM32_X, MOVD_RM32_X, MOVD, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(MOVQ_X_RM64, MOVQ_X_RM64, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(MOVQ_RM64_X, MOVQ_RM64_X, MOVD, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(MOVQ_X_XM64, MOVQ_XM64_X, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(ROUNDSS_X_XM32_IMM8, ROUNDSS_X_XM32_IMM8, FROUND, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 4), 0, 0, 0, 0, 0, 0, CONTROL, NONE, \
      BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(ROUNDSD_X_XM64_IMM8, ROUNDSD_X_XM64_IMM8, FROUND, MOVE, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 8), 0, 0, 0, 0, 0, 0, CONTROL, NONE, \
      BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ANDNPD_X_XM128, VECTOR_ANDN, 0) \
    BUSTER_X86_SSE_LOGIC_EFFECTS(X, ORPD_X_XM128, VECTOR_OR, 0) \
    X(CMPSS_X_XM32_IMM8, CMPSS_X_XM32_IMM8, FCOMPARE_MASK, MODIFY, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 4), 0, 0, 0, 0, 0, 0, CONTROL, NONE, \
      BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(CMPSD_X_XM64_IMM8, CMPSD_X_XM64_IMM8, FCOMPARE_MASK, MODIFY, 0, BUSTER_X86_VECTOR_WRITE(MERGE, 8), 0, 0, 0, 0, 0, 0, CONTROL, NONE, \
      BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_KEEP_UPPER) \
    X(STMXCSR_M32, STMXCSR_M32, STORE_MXCSR, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_MXCSR) \
    X(SHLD_RM32_R32_IMM8, SHLD_RM64_R64_IMM8, SHLD, MODIFY, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, COUNT, NONE, BUSTER_X86_EFFECT_COUNT) \
    X(SHLD_RM32_R32_CL, SHLD_RM64_R64_CL, SHLD, MODIFY, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_COUNT) \
    X(SHRD_RM32_R32_IMM8, SHRD_RM64_R64_IMM8, SHRD, MODIFY, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, COUNT, NONE, BUSTER_X86_EFFECT_COUNT) \
    X(SHRD_RM32_R32_CL, SHRD_RM64_R64_CL, SHRD, MODIFY, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_COUNT) \
    X(VMOVDQU_X_XM128, VMOVDQU_XM128_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVDQU_Y_YM256, VMOVDQU_YM256_Y, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 32), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VZEROUPPER, VZEROUPPER, ZERO_UPPER, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_UPPER) \
    X(VMOVDQU64_Z_ZM512, VMOVDQU64_ZM512_Z, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 64), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVDQU64_X_XM128, VMOVDQU64_XM128_X, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVDQU64_Y_YM256, VMOVDQU64_YM256_Y, VECTOR_MOVE, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 32), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVD_X_RM32_EVEX, VMOVD_X_RM32_EVEX, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 4), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVD_RM32_X_EVEX, VMOVD_RM32_X_EVEX, MOVD, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVQ_X_RM64_EVEX, VMOVQ_X_RM64_EVEX, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVQ_RM64_X_EVEX, VMOVQ_RM64_X_EVEX, MOVD, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VMOVQ_X_XM64_EVEX, VMOVQ_XM64_X_EVEX, MOVD, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 8), 0, 0, 0, 0, 0, 0, NONE, NONE, 0) \
    X(VPXORD_Z_Z_ZM512, VPXORD_Z_Z_M32BCST, VECTOR_XOR, MOVE, 0, BUSTER_X86_VECTOR_WRITE(WHOLE, 64), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    X(VPXORD_X_X_XM128, VPXORD_X_X_M32BCST, VECTOR_XOR, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 16), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    X(VPXORD_Y_Y_YM256, VPXORD_Y_Y_M32BCST, VECTOR_XOR, MOVE, 0, BUSTER_X86_VECTOR_WRITE(ZERO, 32), 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_ZERO_IDIOM) \
    X(FADD_M32, FDIVRP_ST_ST0, X87_ARITHMETIC, READ, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FLD_M32, FLD_ST, X87_LOAD, READ, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FST_M32, FSTP_ST, X87_STORE, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FILD_M16, FILD_M64, X87_LOAD, READ, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FIST_M16, FISTP_M64, X87_STORE, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, B, BUSTER_X86_FLAG_CF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, E, BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, BE, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, U, BUSTER_X86_FLAG_PF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, NB, BUSTER_X86_FLAG_CF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, NE, BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, NBE, BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_ZF) \
    BUSTER_X86_X87_FCMOV_EFFECTS(X, NU, BUSTER_X86_FLAG_PF) \
    X(FCOMI_ST0_ST, FUCOMIP_ST0_ST, X87_COMPARE, NONE, 0, 0, 0, BUSTER_X86_FLAGS_STATUS, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FLDCW_M16, FLDCW_M16, X87_CONTROL, READ, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_X87 | BUSTER_X86_EFFECT_X87_CONTROL) \
    X(FNSTCW_M16, FNSTCW_M16, X87_CONTROL, MOVE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_EFFECT_X87) \
    X(FCHS, FABS, X87_ARITHMETIC, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS) \
    X(FLD1, FLDZ, X87_LOAD, NONE, 0, 0, 0, 0, 0, 0, 0, 0, NONE, NONE, BUSTER_X86_X87_EXCEPTIONS)

// An effects row, as the initialization reads it.
typedef struct X86EffectsRow X86EffectsRow;
struct X86EffectsRow
{
    uint16_t first;
    uint16_t last;
    uint16_t implicit_reads;
    uint16_t implicit_writes;
    uint16_t properties;
    uint16_t vector;
    uint8_t operation;
    uint8_t pattern;
    uint8_t width;
    uint8_t flags_read;
    uint8_t flags_written;
    uint8_t flags_undefined;
    uint8_t memory;
    uint8_t immediate;
    uint8_t control;
};

#define BUSTER_X86_EFFECT_ROW(first, last, operation, pattern, width, vector, read, written, undefined, implicit_reads, implicit_writes, memory, \
                              immediate, control, properties) \
    { X86_FORM_##first, X86_FORM_##last, implicit_reads, implicit_writes, properties, vector, X86_OPERATION_##operation, X86_PATTERN_##pattern, width, \
      read, written, undefined, memory, BUSTER_X86_IMMEDIATE_##immediate, BUSTER_X86_CONTROL_##control },
static const X86EffectsRow x86_effects_rows[] = { BUSTER_X86_EFFECT_ROWS(BUSTER_X86_EFFECT_ROW) };
#undef BUSTER_X86_EFFECT_ROW

// The covered forms' records, in the rows' order after the conservative record at 0, and each form's record
// (x86_semantics_initialize fills both; 0 for an uncovered form). x86_effects and the vector resolver look a record up by
// its form's number (the fixup flag masked off) in x86_effects_by_form, x86_effects_index three times over (a form's,
// its FS copy's at X86_FORM_COUNT and its GS copy's at twice that) and 0 past them, without x86_semantics_row's
// arithmetic, which took a quarter of the vector resolver's instructions.
#define BUSTER_X86_EFFECTS_CAPACITY 1024
static X86Effects x86_effects_records[BUSTER_X86_EFFECTS_CAPACITY] __attribute__((aligned(64)));
static uint16_t x86_effects_index[X86_FORM_COUNT] __attribute__((aligned(64)));
static uint16_t x86_effects_by_form[BUSTER_X86_FORM_NUMBER + 1] __attribute__((aligned(64)));
_Static_assert(BUSTER_X86_FORM_FS == X86_FORM_COUNT && BUSTER_X86_FORM_GS == 2 * X86_FORM_COUNT && 3 * X86_FORM_COUNT <= BUSTER_X86_FORM_NUMBER + 1,
               "x86_effects_by_form holds a form's record and its FS and GS copies'");
static uint32_t x86_effects_count;
// Whether x86_semantics_initialize has built the tables, for the consumers that build theirs from them on demand (the
// assembler's matcher, x86_match.c).
static bool x86_semantics_initialized;

// The record of an uncovered form: it may read and write every register, flag and byte of memory, fault, and go anywhere.
#define BUSTER_X86_UNKNOWN_ROLE (BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE | BUSTER_X86_REST_MERGE << BUSTER_X86_ROLE_REST_SHIFT)
static const X86Effects x86_effects_unknown = {
    .implicit_reads = 0xffff,
    .implicit_writes = 0xffff,
    .operands = BUSTER_X86_UNKNOWN_ROLE | BUSTER_X86_UNKNOWN_ROLE << BUSTER_X86_ROLE_BITS | BUSTER_X86_UNKNOWN_ROLE << (2 * BUSTER_X86_ROLE_BITS),
    .properties = BUSTER_X86_EFFECT_FAULT | BUSTER_X86_EFFECT_MXCSR | BUSTER_X86_EFFECT_FP_EXCEPTION | BUSTER_X86_EFFECT_CONDITIONAL,
    .flags_read = BUSTER_X86_FLAGS_ALL,
    .flags_written = 0,
    .flags_undefined = BUSTER_X86_FLAGS_ALL,
    .memory = BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_STACK_WRITE | BUSTER_X86_MEMORY_EFFECT_ATOMIC | BUSTER_X86_MEMORY_EFFECT_FENCE |
              BUSTER_X86_MEMORY_EFFECT_BIT_STRING,
    .operation = X86_OPERATION_NONE,
    .width = 0,
    .write_bytes = 0,
    .kinds = BUSTER_X86_IMMEDIATE_NONE | BUSTER_X86_CONTROL_UNKNOWN << 4,
};

// The form table's row of a record's form, without its segment override or fixup flag (form 0, uncovered, past the
// table).
static __attribute__((unused)) uint32_t x86_semantics_row(uint32_t form)
{
    uint32_t row = form & BUSTER_X86_FORM_NUMBER;
    row -= row >= BUSTER_X86_FORM_GS ? BUSTER_X86_FORM_GS : row >= BUSTER_X86_FORM_FS ? BUSTER_X86_FORM_FS : 0;
    return row < X86_FORM_COUNT ? row : 0;
}

// A form's effects (with or without a segment override or the fixup flag, which change none): the conservative record
// for an uncovered one. Requires x86_semantics_initialize.
static const X86Effects* x86_effects(uint32_t form)
{
    return &x86_effects_records[x86_effects_by_form[form & BUSTER_X86_FORM_NUMBER]];
}

// A role's BUSTER_X86_ROLE_BITS bits (BUSTER_X86_ROLE_READ and the rest).
static __attribute__((unused)) uint32_t x86_effects_role(const X86Effects* effects, uint32_t role)
{
    return (uint32_t)(effects->operands >> (role * BUSTER_X86_ROLE_BITS)) & ((1u << BUSTER_X86_ROLE_BITS) - 1);
}

static __attribute__((unused)) uint32_t x86_effects_immediate_kind(const X86Effects* effects)
{
    return effects->kinds & 0x0f;
}

static __attribute__((unused)) uint32_t x86_effects_control(const X86Effects* effects)
{
    return effects->kinds >> 4;
}

// ---------------------------------------------------------------------------------------------------------------------
// The immediate's contract

// The bits of a width's patterns (all 64 for 8 bytes).
static uint64_t x86_width_mask(uint32_t width)
{
    return width >= 8 ? ~0ull : (1ull << (8 * width)) - 1;
}

// Whether `value` is a value of the width: a number that fits it signed or unsigned ([-2^(8w-1), 2^(8w) - 1]), whose
// pattern (value modulo 2^(8w)) is what the operation computes with. At 8 bytes, every int64 is.
static bool x86_value_fits_width(int64_t value, uint32_t width)
{
    const uint64_t bias = width >= 8 ? 0 : 1ull << (8 * width - 1);
    const uint64_t span = width >= 8 ? ~0ull : 3 * bias - 1;
    return (uint64_t)value + bias <= span;
}

// The largest count a form's masking keeps: 31, or 63 for a 64-bit operand.
static uint32_t x86_count_mask(uint32_t width)
{
    return width >= 8 ? 63 : 31;
}

// Whether the form's immediate can stand for `value`, the value the operation is asked to compute with:
//   - VALUE: a value of the form's width (x86_value_fits_width: at 32 bits, 0xffffffff and -1 are one value) whose
//     pattern the field's sign extension gives back: an imm8 in a 64-bit form takes -128 to 127, an imm32 there takes
//     -2^31 to 2^31 - 1, and a field as wide as the form takes any value of its width;
//   - COUNT: a count the masking keeps ([0, 31], or [0, 63] for a 64-bit operand), so that a count past it is refused
//     rather than masked;
//   - BIT: a bit index below the width's bits;
//   - CONTROL: the field's unsigned range.
// A form without an immediate takes none.
static __attribute__((unused)) bool x86_immediate_encodes(uint32_t form, int64_t value)
{
    const X86Effects* effects = x86_effects(form);
    const uint32_t bytes = x86_form_description(form)->immediate_bytes;
    const uint32_t width = effects->width;
    bool result = false;
    switch (x86_effects_immediate_kind(effects))
    {
    case BUSTER_X86_IMMEDIATE_VALUE:
    {
        const uint64_t pattern = (uint64_t)value & x86_width_mask(width);
        const uint32_t shift = 64 - 8 * bytes;
        const uint64_t extended = (uint64_t)((int64_t)(pattern << shift) >> shift) & x86_width_mask(width);
        result = bytes != 0 && bytes <= width && x86_value_fits_width(value, width) && extended == pattern;
    }
    break;
    case BUSTER_X86_IMMEDIATE_COUNT:
        result = (uint64_t)value <= x86_count_mask(width);
        break;
    case BUSTER_X86_IMMEDIATE_BIT:
        result = (uint64_t)value < 8ull * width;
        break;
    case BUSTER_X86_IMMEDIATE_CONTROL:
        result = (uint64_t)value <= x86_width_mask(bytes);
        break;
    default:
        break;
    }
    return result;
}

// The field's bits for `value` (one x86_immediate_encodes accepts), as X86Instruction holds them: `immediate`, signed as
// wide as the field (an imm8's in [-128, 127]), and for a 64-bit immediate `high`, the displacement's.
static __attribute__((unused)) void x86_immediate_field(uint32_t form, int64_t value, int32_t* immediate, int32_t* high)
{
    const uint32_t bytes = x86_form_description(form)->immediate_bytes;
    const uint64_t pattern = (uint64_t)value;
    const uint32_t shift = bytes >= 4 ? 32 : 64 - 8 * bytes;
    *immediate = bytes == 0 ? 0 : (int32_t)((int64_t)(pattern << shift) >> shift);
    *high = bytes == 8 ? (int32_t)(pattern >> 32) : 0;
}

// The value a form's immediate field stands for, as a pattern of the form's width: the field's bits (`immediate`, and for
// a 64-bit immediate `high`) extended or masked as its kind says. For VALUE, x86_immediate_value(form, field) == value
// modulo 2^(8 * width) exactly when x86_immediate_encodes(form, value).
static __attribute__((unused)) uint64_t x86_immediate_value(uint32_t form, int32_t immediate, int32_t high)
{
    const X86Effects* effects = x86_effects(form);
    const uint32_t bytes = x86_form_description(form)->immediate_bytes;
    const uint32_t width = effects->width;
    const uint64_t raw = (uint64_t)(uint32_t)immediate | (bytes == 8 ? (uint64_t)(uint32_t)high << 32 : 0);
    const uint64_t field = raw & x86_width_mask(bytes);
    uint64_t result = 0;
    switch (x86_effects_immediate_kind(effects))
    {
    case BUSTER_X86_IMMEDIATE_VALUE:
    {
        const uint32_t shift = 64 - 8 * bytes;
        result = bytes == 0 ? 0 : (uint64_t)((int64_t)(field << shift) >> shift) & x86_width_mask(width);
    }
    break;
    case BUSTER_X86_IMMEDIATE_COUNT:
        result = field & x86_count_mask(width);
        break;
    case BUSTER_X86_IMMEDIATE_BIT:
        result = field & (8ull * width - 1);
        break;
    case BUSTER_X86_IMMEDIATE_CONTROL:
        result = field;
        break;
    default:
        break;
    }
    return result;
}

// The flags a record reads, defines and changes to undefined values: x86_effects's, refined by a shift's immediate count
// (masked as the processor masks it). With a count of zero the instruction touches no flag; with another it reads none
// and defines CF, and for a shift PF, ZF and SF, and OF only for a count of 1 (undefined otherwise), and leaves AF
// undefined; and an 8- or 16-bit SHL or SHR by its width or more leaves CF undefined (the SDM's exception, which isn't
// SAR's: its CF is the last bit shifted out, the sign bit by then).
static __attribute__((unused)) void x86_instruction_flags(const X86Instruction* instruction, uint8_t* read, uint8_t* written, uint8_t* undefined)
{
    const X86Effects* effects = x86_effects(instruction->form);
    uint8_t flags_read = effects->flags_read;
    uint8_t flags_written = effects->flags_written;
    uint8_t flags_undefined = effects->flags_undefined;
    if ((effects->properties & BUSTER_X86_EFFECT_COUNT) && x86_effects_immediate_kind(effects) == BUSTER_X86_IMMEDIATE_COUNT)
    {
        const uint32_t count = (uint32_t)instruction->immediate & x86_count_mask(effects->width);
        const bool rotate = effects->operation == X86_OPERATION_ROL || effects->operation == X86_OPERATION_ROR;
        const uint8_t overflow = count == 1 ? BUSTER_X86_FLAG_OF : 0;
        flags_read &= ~BUSTER_X86_FLAGS_STATUS;
        flags_written = rotate ? BUSTER_X86_FLAG_CF | overflow : (BUSTER_X86_FLAGS_LOGICAL & ~BUSTER_X86_FLAG_OF) | overflow;
        flags_undefined = (rotate ? 0 : BUSTER_X86_FLAG_AF) | (BUSTER_X86_FLAG_OF & ~overflow);
        const bool logical = effects->operation == X86_OPERATION_SHL || effects->operation == X86_OPERATION_SHR;
        if (logical && count >= 8 * effects->width)
        {
            flags_written &= ~BUSTER_X86_FLAG_CF;
            flags_undefined |= BUSTER_X86_FLAG_CF;
        }
        if (count == 0)
        {
            flags_written = 0;
            flags_undefined = 0;
        }
    }
    *read = flags_read;
    *written = flags_written;
    *undefined = flags_undefined;
}

// The status flags a form or a record changes, as x86_flags_substitutable compares them: those it defines, those it
// leaves undefined, and among those the ones it may keep instead, as its count says (a counted shift's or rotate's whose
// count isn't known: a count of zero keeps them all).
typedef struct X86FlagEffects X86FlagEffects;
struct X86FlagEffects
{
    uint8_t written;
    uint8_t undefined;
    uint8_t counted;
    bool known;
};

// The rule of x86_flags_substitutable and x86_instruction_flags_substitutable.
static bool x86_flag_effects_substitutable(X86FlagEffects original, X86FlagEffects substitute, uint8_t same, uint8_t live)
{
    const uint8_t kept = (uint8_t)~(original.written | original.undefined | substitute.written | substitute.undefined);
    const uint8_t alike = original.written & substitute.written & same;
    // A counted flag is kept or changed as the count says, so only a substitute counted alike stands for it (another
    // encoding of the same shift), and it isn't one nothing may rely on.
    const uint8_t counted_alike = original.counted & substitute.counted & same;
    const uint8_t allowed = kept | alike | counted_alike | (original.undefined & ~original.counted);
    // An uncovered form's may-clobber mask proves no flag undefined or alike, in either direction.
    const uint8_t proven = original.known && substitute.known ? allowed : 0;
    return (live & BUSTER_X86_FLAGS_STATUS & ~proven) == 0;
}

// A form's flags for x86_flag_effects_substitutable: a counted form's undefined ones are counted.
static X86FlagEffects x86_effects_flag_effects(const X86Effects* effects)
{
    const bool counted = (effects->properties & BUSTER_X86_EFFECT_COUNT) != 0;
    return (X86FlagEffects){
        .written = effects->flags_written,
        .undefined = effects->flags_undefined,
        .counted = counted ? effects->flags_undefined : 0,
        .known = effects->operation != X86_OPERATION_NONE,
    };
}

// Whether `substitute` can stand for `original`, two forms that compute the same result, when the status flags in `live`
// are read after them: each live flag must be one both keep, one both define with the same value (`same`: a fact about
// the pair, which the caller knows from what they compute: inc and add 1 define all but CF alike), one the original
// leaves undefined, which nothing may rely on, or for a counted form one it may keep or change as its count says, which
// only a substitute counted alike does (another encoding of the same shift: `same` says so). shl r/m32, imm8 doesn't
// take or r/m32, imm8 with CF live: shl by 0 keeps CF, and or clears it. For two records with their counts,
// x86_instruction_flags_substitutable. With any status flag live, both forms must have covered effects: an uncovered
// form's conservative may-clobber mask doesn't prove its outputs undefined.
static __attribute__((unused)) bool x86_flags_substitutable(const X86Effects* original, const X86Effects* substitute, uint8_t same, uint8_t live)
{
    return x86_flag_effects_substitutable(x86_effects_flag_effects(original), x86_effects_flag_effects(substitute), same, live);
}

// x86_flags_substitutable for two records: an immediate count resolves a shift's flags (x86_instruction_flags), so shl
// by 1 stands for shl r/m32, imm8 of 1 whatever is live, and nothing that changes a live flag stands for one of 0. A count
// in CL leaves the flags counted.
static __attribute__((unused)) bool x86_instruction_flags_substitutable(const X86Instruction* original, const X86Instruction* substitute, uint8_t same, uint8_t live)
{
    X86FlagEffects effects[2] = { 0 };
    const X86Instruction* records[2] = { original, substitute };
    for (uint32_t i = 0; i < 2; i += 1)
    {
        const X86Effects* form = x86_effects(records[i]->form);
        uint8_t read = 0;
        x86_instruction_flags(records[i], &read, &effects[i].written, &effects[i].undefined);
        const bool counted = (form->properties & BUSTER_X86_EFFECT_COUNT) && x86_effects_immediate_kind(form) != BUSTER_X86_IMMEDIATE_COUNT;
        effects[i].counted = counted ? effects[i].undefined : 0;
        effects[i].known = form->operation != X86_OPERATION_NONE;
    }
    return x86_flag_effects_substitutable(effects[0], effects[1], same, live);
}

// ---------------------------------------------------------------------------------------------------------------------
// Operand-resolved effects

// The resources a record reads or writes, as one 64-bit set: the general registers (RAX = bit 0 ... R15 = bit 15), the
// vector registers (each the whole register, ZMM0 to ZMM31 with their XMM and YMM parts: bit 16 ... bit 47; a legacy SSE
// write keeps bits 511 to 128, so it reads its register too), the flags (BUSTER_X86_FLAG_*'s bits from bit 48: CF
// to OF, then DF), MXCSR's controls (read), memory (any byte: alias analysis is the consumer's), and in the write set,
// control (the record can leave the block: a branch, call, return or trap) and a call (whose callee clobbers what the
// calling convention says, #137); and the x87's state (its registers, the stack's top and tags, and its status word)
// and its control word, which an x87 form reads, and fldcw writes. An uncovered form reads and writes every resource.
#define BUSTER_X86_RESOURCE_GPR(number) (1ull << (number))
#define BUSTER_X86_RESOURCE_GPRS 0xffffull
#define BUSTER_X86_RESOURCE_VECTOR_SHIFT 16
#define BUSTER_X86_RESOURCE_VECTOR(number) (1ull << (BUSTER_X86_RESOURCE_VECTOR_SHIFT + (number)))
#define BUSTER_X86_RESOURCE_VECTORS (0xffffffffull << BUSTER_X86_RESOURCE_VECTOR_SHIFT)
#define BUSTER_X86_RESOURCE_FLAGS_SHIFT 48
#define BUSTER_X86_RESOURCE_FLAGS ((uint64_t)BUSTER_X86_FLAGS_ALL << BUSTER_X86_RESOURCE_FLAGS_SHIFT)
#define BUSTER_X86_RESOURCE_STATUS ((uint64_t)BUSTER_X86_FLAGS_STATUS << BUSTER_X86_RESOURCE_FLAGS_SHIFT)
#define BUSTER_X86_RESOURCE_MXCSR (1ull << 55)
#define BUSTER_X86_RESOURCE_MEMORY (1ull << 56)
#define BUSTER_X86_RESOURCE_CONTROL (1ull << 57)
#define BUSTER_X86_RESOURCE_CALL (1ull << 58)
#define BUSTER_X86_RESOURCE_X87 (1ull << 59)
#define BUSTER_X86_RESOURCE_X87_CONTROL (1ull << 60)
// What vzeroupper reads and writes: ZMM0 to ZMM15.
#define BUSTER_X86_RESOURCE_LOWER_VECTORS (0xffffull << BUSTER_X86_RESOURCE_VECTOR_SHIFT)
#define BUSTER_X86_RESOURCE_ALL (~0ull)

// The resources the x87's and vzeroupper's properties add, by the properties' bits 8 to 10 (X87, X87_CONTROL and
// ZERO_UPPER): X87 reads the x87's state and control word and writes the state, X87_CONTROL writes the control word too,
// and ZERO_UPPER reads and writes ZMM0 to ZMM15. A lookup rather than a test per property: on the 9700X the scalar twin
// took 11 instructions and 2% of its cycles per record fewer so.
_Static_assert(BUSTER_X86_EFFECT_X87 == 0x0100 && BUSTER_X86_EFFECT_X87_CONTROL == 0x0200 && BUSTER_X86_EFFECT_ZERO_UPPER == 0x0400,
               "the x87's and vzeroupper's properties are bits 8 to 10");
#define BUSTER_X86_RARE_READS(bits) \
    (((bits) & 1 ? BUSTER_X86_RESOURCE_X87 | BUSTER_X86_RESOURCE_X87_CONTROL : 0) | ((bits) & 4 ? BUSTER_X86_RESOURCE_LOWER_VECTORS : 0))
#define BUSTER_X86_RARE_WRITES(bits) \
    (((bits) & 1 ? BUSTER_X86_RESOURCE_X87 : 0) | ((bits) & 2 ? BUSTER_X86_RESOURCE_X87_CONTROL : 0) | ((bits) & 4 ? BUSTER_X86_RESOURCE_LOWER_VECTORS : 0))
static const uint64_t x86_rare_reads[8] = {
    BUSTER_X86_RARE_READS(0), BUSTER_X86_RARE_READS(1), BUSTER_X86_RARE_READS(2), BUSTER_X86_RARE_READS(3),
    BUSTER_X86_RARE_READS(4), BUSTER_X86_RARE_READS(5), BUSTER_X86_RARE_READS(6), BUSTER_X86_RARE_READS(7),
};
static const uint64_t x86_rare_writes[8] = {
    BUSTER_X86_RARE_WRITES(0), BUSTER_X86_RARE_WRITES(1), BUSTER_X86_RARE_WRITES(2), BUSTER_X86_RARE_WRITES(3),
    BUSTER_X86_RARE_WRITES(4), BUSTER_X86_RARE_WRITES(5), BUSTER_X86_RARE_WRITES(6), BUSTER_X86_RARE_WRITES(7),
};
#undef BUSTER_X86_RARE_READS
#undef BUSTER_X86_RARE_WRITES

// A register operand's resource: a general register (AH to BH are RAX to RBX's) or a vector one. The covered forms take
// general registers 0 to 15, as x86_encode does.
static uint64_t x86_register_resource(uint8_t value, bool vector)
{
    const uint32_t general = (value & BUSTER_X86_REGISTER_HIGH_BYTE) ? (value & 3) : (value & 15);
    return vector ? BUSTER_X86_RESOURCE_VECTOR(value & 31) : BUSTER_X86_RESOURCE_GPR(general);
}

// What one record reads and writes (BUSTER_X86_RESOURCE_*), its operands resolved: a register operand's register; a
// memory operand's base and index (read, as lea's are) and the memory read or written; a narrower write's register (which
// keeps the rest, so it is read), and a legacy SSE write's vector register (which keeps its bits above 127); a zero
// idiom's register not read (but a legacy SSE one's, whose upper bits still pass through); a shift's flags by its
// immediate count (x86_instruction_flags); the stack and fences as memory; control; the x87's state and control word,
// and vzeroupper's vector registers. A record with an opmask (an EVEX form's: x86_validate refuses one in any other's)
// merges or zeroes its destination's elements by a mask register, which the sets have no resource for: it reads and
// writes everything, as an uncovered form does.
static __attribute__((unused)) void x86_instruction_effects_scalar(const X86Instruction* instruction, uint64_t* reads, uint64_t* writes)
{
    const X86Effects* effects = x86_effects(instruction->form);
    uint64_t read = BUSTER_X86_RESOURCE_ALL;
    uint64_t write = BUSTER_X86_RESOURCE_ALL;
    if (effects->operation != X86_OPERATION_NONE && (instruction->decorations & BUSTER_X86_DECORATION_MASK) == 0)
    {
        const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
        uint8_t flags_read;
        uint8_t flags_written;
        uint8_t flags_undefined;
        x86_instruction_flags(instruction, &flags_read, &flags_written, &flags_undefined);
        read = effects->implicit_reads | (uint64_t)flags_read << BUSTER_X86_RESOURCE_FLAGS_SHIFT;
        write = effects->implicit_writes | (uint64_t)(flags_written | flags_undefined) << BUSTER_X86_RESOURCE_FLAGS_SHIFT;
        read |= (effects->properties & BUSTER_X86_EFFECT_MXCSR) ? BUSTER_X86_RESOURCE_MXCSR : 0;
        const uint32_t rare = (effects->properties >> 8) & 7;
        read |= x86_rare_reads[rare];
        write |= x86_rare_writes[rare];
        const uint8_t values[BUSTER_X86_ROLES] = { instruction->reg, instruction->rm, instruction->vvvv };
        for (uint32_t role = 0; role < BUSTER_X86_ROLES; role += 1)
        {
            const uint32_t bits = x86_effects_role(effects, role);
            const uint32_t rest = (bits & BUSTER_X86_ROLE_REST) >> BUSTER_X86_ROLE_REST_SHIFT;
            if (role == BUSTER_X86_ROLE_RM && memory)
            {
                const bool address = (effects->memory & BUSTER_X86_MEMORY_EFFECT_ADDRESS) != 0;
                if ((bits & (BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE)) || address)
                {
                    read |= (instruction->mode & BUSTER_X86_MODE_BASE) ? BUSTER_X86_RESOURCE_GPR(instruction->rm & 15) : 0;
                    read |= (instruction->mode & BUSTER_X86_MODE_INDEX) ? BUSTER_X86_RESOURCE_GPR(instruction->index & 15) : 0;
                }
                read |= (bits & BUSTER_X86_ROLE_READ) && !address ? BUSTER_X86_RESOURCE_MEMORY : 0;
                write |= (bits & BUSTER_X86_ROLE_WRITE) ? BUSTER_X86_RESOURCE_MEMORY : 0;
            }
            else
            {
                const uint64_t resource = x86_register_resource(values[role], (bits & BUSTER_X86_ROLE_VECTOR) != 0);
                // movss's and movsd's loads keep the rest from a register source and zero it from memory.
                const bool merge = rest == BUSTER_X86_REST_MERGE || (rest == BUSTER_X86_REST_MERGE_IF_REGISTER && !memory);
                read |= (bits & BUSTER_X86_ROLE_READ) || ((bits & BUSTER_X86_ROLE_WRITE) && merge) ? resource : 0;
                write |= (bits & BUSTER_X86_ROLE_WRITE) ? resource : 0;
            }
        }
        // A zero idiom's sources (reg and rm, or vvvv and rm) are one register, which it doesn't read.
        const bool three = (x86_effects_role(effects, BUSTER_X86_ROLE_VVVV) & BUSTER_X86_ROLE_READ) != 0;
        const uint8_t source = three ? instruction->vvvv : instruction->reg;
        if ((effects->properties & BUSTER_X86_EFFECT_ZERO_IDIOM) && !memory && source == instruction->rm)
        {
            const uint32_t bits = x86_effects_role(effects, BUSTER_X86_ROLE_RM);
            read &= ~x86_register_resource(instruction->rm, (bits & BUSTER_X86_ROLE_VECTOR) != 0);
        }
        // A legacy SSE write keeps its vector register's bits above 127, so the form reads every vector register it writes,
        // a zero idiom's too (only explicit operands write vector registers).
        read |= (effects->properties & BUSTER_X86_EFFECT_KEEP_UPPER) ? write & BUSTER_X86_RESOURCE_VECTORS : 0;
        read |= (effects->memory & (BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_FENCE)) ? BUSTER_X86_RESOURCE_MEMORY : 0;
        write |= (effects->memory & (BUSTER_X86_MEMORY_EFFECT_STACK_WRITE | BUSTER_X86_MEMORY_EFFECT_FENCE)) ? BUSTER_X86_RESOURCE_MEMORY : 0;
        const uint32_t control = x86_effects_control(effects);
        write |= control != BUSTER_X86_CONTROL_NONE ? BUSTER_X86_RESOURCE_CONTROL : 0;
        write |= control == BUSTER_X86_CONTROL_CALL ? BUSTER_X86_RESOURCE_CALL : 0;
    }
    *reads = read;
    *writes = write;
}

// The records x86_instructions_effects leaves to x86_instruction_effects_scalar: a covered form's (without an opmask)
// with an x87 property, vzeroupper's, or an operand in vvvv. BUSTER_X86_EFFECTS_RARE is the same test on the effects
// record's first 8 bytes (its properties and operands in the upper half), as the vector path makes it.
#define BUSTER_X86_EFFECTS_RARE_PROPERTIES (BUSTER_X86_EFFECT_X87 | BUSTER_X86_EFFECT_X87_CONTROL | BUSTER_X86_EFFECT_ZERO_UPPER)
#define BUSTER_X86_EFFECTS_RARE \
    ((uint64_t)BUSTER_X86_EFFECTS_RARE_PROPERTIES << 48 | (uint64_t)((1u << BUSTER_X86_ROLE_BITS) - 1) << (32 + BUSTER_X86_ROLE_VVVV * BUSTER_X86_ROLE_BITS))
static __attribute__((unused)) bool x86_instruction_rare(const X86Instruction* instruction)
{
    const X86Effects* effects = x86_effects(instruction->form);
    const bool known = effects->operation != X86_OPERATION_NONE && (instruction->decorations & BUSTER_X86_DECORATION_MASK) == 0;
    return known && ((effects->properties & BUSTER_X86_EFFECTS_RARE_PROPERTIES) != 0 || x86_effects_role(effects, BUSTER_X86_ROLE_VVVV) != 0);
}

// The scalar twin of x86_instructions_effects, one record at a time.
static __attribute__((unused)) void x86_instructions_effects_scalar(const X86Instruction* instructions, uint64_t count, uint64_t* reads, uint64_t* writes)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        x86_instruction_effects_scalar(&instructions[i], &reads[i], &writes[i]);
    }
}

// The same 8 records at a time (one per 64-bit lane), the records and the outputs read and written with masked loads and
// stores (so nothing past either array is touched). Each record's form gives its effects record by a scalar lookup (its
// number's entry in x86_effects_by_form, then its 16 bytes into a quarter of a ZMM register: Zen 5's gathers cost more
// than these loads), and then every lane computes as x86_instruction_effects_scalar does, with masks for its branches, a
// record with an opmask an uncovered one. The records of the forms code generators emit rarely, an x87 form's,
// vzeroupper's and a form's with an operand in vvvv (x86_instruction_rare), are that function's: each group ORs its rare
// lanes' facts into a register, and after the loop (whose vector constants a call inside it would make it reload) one
// test of it says whether the function resolves any again, so that the common lane pays the OR alone.
static __attribute__((unused)) void x86_instructions_effects(const X86Instruction* instructions, uint64_t count, uint64_t* reads, uint64_t* writes)
{
#if !BUSTER_HOST_VECTOR
    x86_instructions_effects_scalar(instructions, count, reads, writes);
#else
    const __m512i even = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
    const __m512i odd = _mm512_setr_epi64(1, 3, 5, 7, 9, 11, 13, 15);
    const __m512i bytes = _mm512_set1_epi64(0xff);
    const __m512i words = _mm512_set1_epi64(0xffff);
    const __m512i one = _mm512_set1_epi64(1);
    const __m512i zero = _mm512_setzero_si512();
    // The rare lanes' facts, ORed in a register: a general register's count would take one the lanes' lookups need.
    __m512i rare = zero;
    for (uint64_t first = 0; first < count; first += 8)
    {
        const uint64_t left = count - first;
        const uint32_t lanes = left >= 8 ? 8 : (uint32_t)left;
        const uint32_t qwords = (1u << (2 * lanes)) - 1;
        const __mmask8 live = (__mmask8)((1u << lanes) - 1);
        // The records' halves: (immediate, displacement) and (reg, rm, index, mode, form, vvvv, decorations).
        const __m512i head = _mm512_maskz_loadu_epi64((__mmask8)qwords, instructions + first);
        const __m512i tail = _mm512_maskz_loadu_epi64((__mmask8)(qwords >> 8), instructions + first + 4);
        const __m512i low = _mm512_permutex2var_epi64(head, even, tail);
        const __m512i high = _mm512_permutex2var_epi64(head, odd, tail);
        // Each lane's effects record: (implicit reads and writes, operands, properties) and (flags, memory, operation,
        // width, write_bytes, kinds). A lane past the count gets form 0's, the conservative record, and stores nothing.
        __m128i records[8];
        for (uint32_t lane = 0; lane < 8; lane += 1)
        {
            const uint32_t number = lane < lanes ? instructions[first + lane].form & BUSTER_X86_FORM_NUMBER : 0;
            records[lane] = _mm_load_si128((const __m128i*)&x86_effects_records[x86_effects_by_form[number]]);
        }
        __m512i effects_head = _mm512_castsi128_si512(records[0]);
        __m512i effects_tail = _mm512_castsi128_si512(records[4]);
        effects_head = _mm512_inserti32x4(effects_head, records[1], 1);
        effects_head = _mm512_inserti32x4(effects_head, records[2], 2);
        effects_head = _mm512_inserti32x4(effects_head, records[3], 3);
        effects_tail = _mm512_inserti32x4(effects_tail, records[5], 1);
        effects_tail = _mm512_inserti32x4(effects_tail, records[6], 2);
        effects_tail = _mm512_inserti32x4(effects_tail, records[7], 3);
        const __m512i facts = _mm512_permutex2var_epi64(effects_head, even, effects_tail);
        const __m512i details = _mm512_permutex2var_epi64(effects_head, odd, effects_tail);

        const __m512i operands = _mm512_and_si512(_mm512_srli_epi64(facts, 32), words);
        const __m512i properties = _mm512_srli_epi64(facts, 48);
        const __m512i memory_effects = _mm512_and_si512(_mm512_srli_epi64(details, 24), bytes);
        const __m512i width = _mm512_and_si512(_mm512_srli_epi64(details, 40), bytes);
        const __m512i kinds = _mm512_srli_epi64(details, 56);
        // A covered form's (its operation's byte), without an opmask (the record's last byte). The rare lanes are those
        // of the facts' upper half's BUSTER_X86_EFFECTS_RARE bits.
        const __mmask8 known = _mm512_test_epi64_mask(details, _mm512_set1_epi64(0xffull << 32)) &
                               _mm512_testn_epi64_mask(high, _mm512_set1_epi64((int64_t)BUSTER_X86_DECORATION_MASK << 56));
        const __m512i reg = _mm512_and_si512(high, bytes);
        const __m512i rm = _mm512_and_si512(_mm512_srli_epi64(high, 8), bytes);
        const __m512i index = _mm512_and_si512(_mm512_srli_epi64(high, 16), bytes);
        const __m512i mode = _mm512_and_si512(_mm512_srli_epi64(high, 24), bytes);
        const __mmask8 memory = _mm512_test_epi64_mask(mode, _mm512_set1_epi64(BUSTER_X86_MODE_MEMORY));

        // The flags, with a shift's immediate count: none read with a known count, and none written with a zero one.
        __m512i flags_read = _mm512_and_si512(details, bytes);
        __m512i flags_changed = _mm512_and_si512(_mm512_or_si512(_mm512_srli_epi64(details, 8), _mm512_srli_epi64(details, 16)), bytes);
        const __mmask8 counted = _mm512_test_epi64_mask(properties, _mm512_set1_epi64(BUSTER_X86_EFFECT_COUNT)) &
                                 _mm512_cmpeq_epi64_mask(_mm512_and_si512(kinds, _mm512_set1_epi64(0x0f)), _mm512_set1_epi64(BUSTER_X86_IMMEDIATE_COUNT));
        const __m512i count_mask = _mm512_mask_blend_epi64(_mm512_cmpge_epu64_mask(width, _mm512_set1_epi64(8)), _mm512_set1_epi64(31), _mm512_set1_epi64(63));
        const __mmask8 zero_count = _mm512_testn_epi64_mask(_mm512_and_si512(low, count_mask), count_mask);
        flags_read = _mm512_mask_andnot_epi64(flags_read, counted, _mm512_set1_epi64(BUSTER_X86_FLAGS_STATUS), flags_read);
        flags_changed = _mm512_mask_mov_epi64(flags_changed, counted & zero_count, zero);
        __m512i read = _mm512_or_si512(_mm512_and_si512(facts, words), _mm512_slli_epi64(flags_read, BUSTER_X86_RESOURCE_FLAGS_SHIFT));
        __m512i write = _mm512_or_si512(_mm512_and_si512(_mm512_srli_epi64(facts, 16), words), _mm512_slli_epi64(flags_changed, BUSTER_X86_RESOURCE_FLAGS_SHIFT));
        read = _mm512_mask_or_epi64(read, _mm512_test_epi64_mask(properties, _mm512_set1_epi64(BUSTER_X86_EFFECT_MXCSR)), read,
                                    _mm512_set1_epi64((int64_t)BUSTER_X86_RESOURCE_MXCSR));

        // The registers of reg and rm (a general register's number, AH to BH as RAX to RBX, or a vector register's).
        const __m512i role_reg = _mm512_and_si512(operands, _mm512_set1_epi64(0x1f));
        const __m512i role_rm = _mm512_and_si512(_mm512_srli_epi64(operands, BUSTER_X86_ROLE_BITS), _mm512_set1_epi64(0x1f));
        const __m512i vector_shift = _mm512_set1_epi64(BUSTER_X86_RESOURCE_VECTOR_SHIFT);
        const __m512i high_byte = _mm512_set1_epi64(BUSTER_X86_REGISTER_HIGH_BYTE);
        const __m512i reg_general = _mm512_mask_and_epi64(_mm512_and_si512(reg, _mm512_set1_epi64(15)), _mm512_test_epi64_mask(reg, high_byte), reg,
                                                          _mm512_set1_epi64(3));
        const __m512i rm_general = _mm512_mask_and_epi64(_mm512_and_si512(rm, _mm512_set1_epi64(15)), _mm512_test_epi64_mask(rm, high_byte), rm,
                                                         _mm512_set1_epi64(3));
        const __mmask8 reg_vector = _mm512_test_epi64_mask(role_reg, _mm512_set1_epi64(BUSTER_X86_ROLE_VECTOR));
        const __mmask8 rm_vector = _mm512_test_epi64_mask(role_rm, _mm512_set1_epi64(BUSTER_X86_ROLE_VECTOR));
        const __m512i reg_shift = _mm512_mask_add_epi64(reg_general, reg_vector, _mm512_and_si512(reg, _mm512_set1_epi64(31)), vector_shift);
        const __m512i rm_shift = _mm512_mask_add_epi64(rm_general, rm_vector, _mm512_and_si512(rm, _mm512_set1_epi64(31)), vector_shift);
        const __m512i reg_resource = _mm512_sllv_epi64(one, reg_shift);
        const __m512i rm_resource = _mm512_sllv_epi64(one, rm_shift);

        // A register role reads its register when it reads the operand or writes part of it, and writes it when it writes.
        const __m512i read_bit = _mm512_set1_epi64(BUSTER_X86_ROLE_READ);
        const __m512i write_bit = _mm512_set1_epi64(BUSTER_X86_ROLE_WRITE);
        const __m512i merge_bit = _mm512_set1_epi64(BUSTER_X86_REST_MERGE << BUSTER_X86_ROLE_REST_SHIFT);
        const __m512i rest_bits = _mm512_set1_epi64(BUSTER_X86_ROLE_REST);
        // movss's and movsd's loads (MERGE_IF_REGISTER) merge from a register source only.
        const __mmask8 reg_loads = memory & _mm512_cmpeq_epi64_mask(_mm512_and_si512(role_reg, rest_bits), rest_bits);
        const __mmask8 reg_reads = _mm512_test_epi64_mask(role_reg, read_bit) |
                                   (_mm512_test_epi64_mask(role_reg, write_bit) & _mm512_test_epi64_mask(role_reg, merge_bit) & ~reg_loads);
        const __mmask8 rm_reads = _mm512_test_epi64_mask(role_rm, read_bit) |
                                  (_mm512_test_epi64_mask(role_rm, write_bit) & _mm512_test_epi64_mask(role_rm, merge_bit));
        const __mmask8 reg_writes = _mm512_test_epi64_mask(role_reg, write_bit);
        const __mmask8 rm_writes = _mm512_test_epi64_mask(role_rm, write_bit);
        // A zero idiom's register (reg and rm the same register) isn't read.
        const __mmask8 idiom = _mm512_test_epi64_mask(properties, _mm512_set1_epi64(BUSTER_X86_EFFECT_ZERO_IDIOM)) & ~memory & _mm512_cmpeq_epi64_mask(reg, rm);
        read = _mm512_mask_or_epi64(read, reg_reads & ~idiom, read, reg_resource);
        write = _mm512_mask_or_epi64(write, reg_writes, write, reg_resource);
        read = _mm512_mask_or_epi64(read, rm_reads & ~memory & ~idiom, read, rm_resource);
        write = _mm512_mask_or_epi64(write, rm_writes & ~memory, write, rm_resource);
        // A legacy SSE form reads every vector register it writes (which keeps its bits above 127), a zero idiom's too:
        // write & vectors in the lanes of KEEP_UPPER, the record's top bit, spread over the lane by an arithmetic shift
        // (0x80, all three). A mask register from vptestmq measured slower on the 9700X. It joins the reads at the end.
        const __m512i kept = _mm512_ternarylogic_epi64(write, _mm512_srai_epi64(facts, 63), _mm512_set1_epi64((int64_t)BUSTER_X86_RESOURCE_VECTORS), 0x80);

        // A memory operand: its base and index when it's accessed or its address taken, and the memory read or written.
        const __mmask8 address = _mm512_test_epi64_mask(memory_effects, _mm512_set1_epi64(BUSTER_X86_MEMORY_EFFECT_ADDRESS));
        const __mmask8 accessed = memory & (_mm512_test_epi64_mask(role_rm, _mm512_set1_epi64(BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE)) | address);
        const __m512i base_resource = _mm512_maskz_sllv_epi64(_mm512_test_epi64_mask(mode, _mm512_set1_epi64(BUSTER_X86_MODE_BASE)), one,
                                                              _mm512_and_si512(rm, _mm512_set1_epi64(15)));
        const __m512i index_resource = _mm512_maskz_sllv_epi64(_mm512_test_epi64_mask(mode, _mm512_set1_epi64(BUSTER_X86_MODE_INDEX)), one,
                                                               _mm512_and_si512(index, _mm512_set1_epi64(15)));
        read = _mm512_mask_or_epi64(read, accessed, read, _mm512_or_si512(base_resource, index_resource));
        const __m512i memory_resource = _mm512_set1_epi64((int64_t)BUSTER_X86_RESOURCE_MEMORY);
        read = _mm512_mask_or_epi64(read, memory & _mm512_test_epi64_mask(role_rm, read_bit) & ~address, read, memory_resource);
        write = _mm512_mask_or_epi64(write, memory & rm_writes, write, memory_resource);
        read = _mm512_mask_or_epi64(read, _mm512_test_epi64_mask(memory_effects, _mm512_set1_epi64(BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_FENCE)), read,
                                    memory_resource);
        write = _mm512_mask_or_epi64(write, _mm512_test_epi64_mask(memory_effects, _mm512_set1_epi64(BUSTER_X86_MEMORY_EFFECT_STACK_WRITE | BUSTER_X86_MEMORY_EFFECT_FENCE)), write,
                                     memory_resource);

        // Control, and an uncovered form's everything.
        const __m512i control = _mm512_srli_epi64(kinds, 4);
        write = _mm512_mask_or_epi64(write, _mm512_test_epi64_mask(control, control), write, _mm512_set1_epi64((int64_t)BUSTER_X86_RESOURCE_CONTROL));
        write = _mm512_mask_or_epi64(write, _mm512_cmpeq_epi64_mask(control, _mm512_set1_epi64(BUSTER_X86_CONTROL_CALL)), write,
                                     _mm512_set1_epi64((int64_t)BUSTER_X86_RESOURCE_CALL));
        read = _mm512_mask_or_epi64(_mm512_set1_epi64(-1), known, read, kept);
        write = _mm512_mask_mov_epi64(_mm512_set1_epi64(-1), known, write);
        _mm512_mask_storeu_epi64(reads + first, live, read);
        _mm512_mask_storeu_epi64(writes + first, live, write);
        rare = _mm512_mask_ternarylogic_epi64(rare, known, facts, _mm512_set1_epi64((int64_t)BUSTER_X86_EFFECTS_RARE), 0xf8);
    }
    const bool any = _mm512_test_epi64_mask(rare, rare) != 0;
    for (uint64_t i = 0; any && i < count; i += 1)
    {
        if (x86_instruction_rare(&instructions[i]))
        {
            x86_instruction_effects_scalar(&instructions[i], &reads[i], &writes[i]);
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// Form choice

// A request's classes and context, which a candidate's conditions name. The value's classes, of its pattern at the row's
// width (value modulo 2^(8 * width)):
//   - SIGNED8 and SIGNED32: an imm8's or imm32's sign extension gives the pattern back;
//   - UNSIGNED32: so does its zero extension (a 32-bit register write's: mov r32, imm32 for a 64-bit value);
//   - NONNEGATIVE: the width's sign bit is clear;
//   - ZERO, ONE and MINUS_ONE (every bit set);
//   - VALID: the value is one of the width (x86_value_fits_width; a count row's: a count below the masking's limit).
//     Every candidate needs it, so a value that fits no width gets no form.
// The context, which the caller gives (BUSTER_X86_CONTEXT_*):
//   - REGISTER: the destination is a register (rm's, with mode 0), not memory;
//   - ACCUMULATOR: and it's RAX (EAX, AX or AL; not AH);
//   - INC_DEC: inc and dec may stand for add and sub of 1 and -1: smaller, but whether they're faster is the target's
//     (#181), so they're the caller's choice;
//   - CF_DEAD, AF_DEAD, SF_DEAD and FLAGS_DEAD: CF, AF, SF, or every status flag, isn't live after the instruction (from
//     BUSTER_X86_CONTEXT_LIVE_FLAGS: the flags something reads before they're written again).
#define BUSTER_X86_CHOICE_SIGNED8 0x0001
#define BUSTER_X86_CHOICE_SIGNED32 0x0002
#define BUSTER_X86_CHOICE_UNSIGNED32 0x0004
#define BUSTER_X86_CHOICE_NONNEGATIVE 0x0008
#define BUSTER_X86_CHOICE_ZERO 0x0010
#define BUSTER_X86_CHOICE_ONE 0x0020
#define BUSTER_X86_CHOICE_MINUS_ONE 0x0040
#define BUSTER_X86_CHOICE_VALID 0x0080
#define BUSTER_X86_CHOICE_REGISTER 0x0100
#define BUSTER_X86_CHOICE_ACCUMULATOR 0x0200
#define BUSTER_X86_CHOICE_INC_DEC 0x0400
#define BUSTER_X86_CHOICE_CF_DEAD 0x0800
#define BUSTER_X86_CHOICE_AF_DEAD 0x1000
#define BUSTER_X86_CHOICE_SF_DEAD 0x2000
#define BUSTER_X86_CHOICE_FLAGS_DEAD 0x4000
// No request has it: an empty candidate.
#define BUSTER_X86_CHOICE_NEVER 0x8000

// A request's context (a 16-bit word per request): the status flags live after the instruction (BUSTER_X86_FLAG_*'s
// bits; all of them is the conservative answer), and REGISTER, ACCUMULATOR and INC_DEC as above.
#define BUSTER_X86_CONTEXT_LIVE_FLAGS BUSTER_X86_FLAGS_STATUS
#define BUSTER_X86_CONTEXT_REGISTER BUSTER_X86_CHOICE_REGISTER
#define BUSTER_X86_CONTEXT_ACCUMULATOR BUSTER_X86_CHOICE_ACCUMULATOR
#define BUSTER_X86_CONTEXT_INC_DEC BUSTER_X86_CHOICE_INC_DEC
#define BUSTER_X86_CONTEXT_GIVEN (BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ACCUMULATOR | BUSTER_X86_CHOICE_INC_DEC)

// A row's candidates (each a form and its conditions), at most this many.
#define BUSTER_X86_CHOICE_CANDIDATES 8
// The vector path looks rows up in registers (vpermi2w over two ZMM registers of 32 words each).
#define BUSTER_X86_CHOICE_ROWS_MAX 64
#define BUSTER_X86_CHOICE_NO_ROW 0xffff

typedef struct X86ChoiceCandidate X86ChoiceCandidate;
struct X86ChoiceCandidate
{
    uint16_t form;
    uint16_t conditions;
    // The status flags the candidate defines as the row's operation would (for x86_flags_substitutable against the row's
    // original form); BUSTER_X86_FLAGS_STATUS for another encoding of the same instruction.
    uint8_t same;
};

// A row as written: the operation, its width and kind (VALUE, or COUNT for a shift), the form whose flags the
// candidates must keep (the operation's own), and its candidates in order of preference.
typedef struct X86ChoiceSource X86ChoiceSource;
struct X86ChoiceSource
{
    uint8_t operation;
    uint8_t width;
    uint8_t kind;
    uint16_t original;
    X86ChoiceCandidate candidates[BUSTER_X86_CHOICE_CANDIDATES];
};

#define BUSTER_X86_SOURCE(operation, width, kind, original, ...) \
    { X86_OPERATION_##operation, width, BUSTER_X86_IMMEDIATE_##kind, X86_FORM_##original, { __VA_ARGS__ } },
#define BUSTER_X86_CANDIDATE(form, conditions, same) { X86_FORM_##form, conditions, same }
// What a candidate of another instruction defines alike: inc for add 1 and dec for sub 1 all but CF, dec for add -1 and
// inc for sub -1 all but CF and AF (whose carry out of bit 3 differs), test r, r for cmp r, 0 all but AF (cmp's 0,
// test's undefined), and a test narrowed to 32 bits of a value with bit 31 set all but SF.
#define BUSTER_X86_SAME_ALL BUSTER_X86_FLAGS_STATUS
#define BUSTER_X86_SAME_STEP BUSTER_X86_FLAGS_STEP
#define BUSTER_X86_SAME_REVERSE_STEP (BUSTER_X86_FLAGS_STEP & ~BUSTER_X86_FLAG_AF)
#define BUSTER_X86_SAME_TEST BUSTER_X86_FLAGS_LOGICAL
#define BUSTER_X86_SAME_NARROW_TEST (BUSTER_X86_FLAGS_LOGICAL & ~BUSTER_X86_FLAG_SF)
#define BUSTER_X86_REG_ACC (BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ACCUMULATOR)

// The arithmetic group with an immediate at each width: the sign-extended imm8, the accumulator's form (a byte shorter
// than the imm32's) and the imm32 (at 64 bits only a sign-extended one: past it, the value needs a register).
#define BUSTER_X86_ARITHMETIC_CHOICES(N) \
    BUSTER_X86_SOURCE(N, 1, VALUE, N##_RM8_IMM8, BUSTER_X86_CANDIDATE(N##_AL_IMM8, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 2, VALUE, N##_RM16_IMM16, BUSTER_X86_CANDIDATE(N##_RM16_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_AX_IMM16, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(N##_RM16_IMM16, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 4, VALUE, N##_RM32_IMM32, BUSTER_X86_CANDIDATE(N##_RM32_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_EAX_IMM32, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(N##_RM32_IMM32, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 8, VALUE, N##_RM64_IMM32, BUSTER_X86_CANDIDATE(N##_RM64_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))

// add and sub, with inc and dec first for 1 and -1 (UP is the instruction for 1: inc for add, dec for sub).
#define BUSTER_X86_STEP_CHOICES(N, UP, DOWN) \
    BUSTER_X86_SOURCE(N, 1, VALUE, N##_RM8_IMM8, BUSTER_X86_CANDIDATE(UP##_RM8, BUSTER_X86_CHOICE_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_STEP), \
                      BUSTER_X86_CANDIDATE(DOWN##_RM8, BUSTER_X86_CHOICE_MINUS_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_REVERSE_STEP), \
                      BUSTER_X86_CANDIDATE(N##_AL_IMM8, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(N##_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 2, VALUE, N##_RM16_IMM16, BUSTER_X86_CANDIDATE(UP##_RM16, BUSTER_X86_CHOICE_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_STEP), \
                      BUSTER_X86_CANDIDATE(DOWN##_RM16, BUSTER_X86_CHOICE_MINUS_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_REVERSE_STEP), \
                      BUSTER_X86_CANDIDATE(N##_RM16_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_AX_IMM16, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(N##_RM16_IMM16, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 4, VALUE, N##_RM32_IMM32, BUSTER_X86_CANDIDATE(UP##_RM32, BUSTER_X86_CHOICE_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_STEP), \
                      BUSTER_X86_CANDIDATE(DOWN##_RM32, BUSTER_X86_CHOICE_MINUS_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_REVERSE_STEP), \
                      BUSTER_X86_CANDIDATE(N##_RM32_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_EAX_IMM32, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(N##_RM32_IMM32, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 8, VALUE, N##_RM64_IMM32, BUSTER_X86_CANDIDATE(UP##_RM64, BUSTER_X86_CHOICE_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_STEP), \
                      BUSTER_X86_CANDIDATE(DOWN##_RM64, BUSTER_X86_CHOICE_MINUS_ONE | BUSTER_X86_CHOICE_INC_DEC, BUSTER_X86_SAME_REVERSE_STEP), \
                      BUSTER_X86_CANDIDATE(N##_RM64_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))

// cmp, with test r, r first for a register against 0.
#define BUSTER_X86_COMPARE_CHOICES \
    BUSTER_X86_SOURCE(CMP, 1, VALUE, CMP_RM8_IMM8, BUSTER_X86_CANDIDATE(TEST_RM8_R8, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, BUSTER_X86_SAME_TEST), \
                      BUSTER_X86_CANDIDATE(CMP_AL_IMM8, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(CMP_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(CMP, 2, VALUE, CMP_RM16_IMM16, BUSTER_X86_CANDIDATE(TEST_RM16_R16, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, BUSTER_X86_SAME_TEST), \
                      BUSTER_X86_CANDIDATE(CMP_RM16_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(CMP_AX_IMM16, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(CMP_RM16_IMM16, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(CMP, 4, VALUE, CMP_RM32_IMM32, BUSTER_X86_CANDIDATE(TEST_RM32_R32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, BUSTER_X86_SAME_TEST), \
                      BUSTER_X86_CANDIDATE(CMP_RM32_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(CMP_EAX_IMM32, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(CMP_RM32_IMM32, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(CMP, 8, VALUE, CMP_RM64_IMM32, BUSTER_X86_CANDIDATE(TEST_RM64_R64, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, BUSTER_X86_SAME_TEST), \
                      BUSTER_X86_CANDIDATE(CMP_RM64_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(CMP_RAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(CMP_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))

// test, narrowed on a register where the flags come out the same: to its low byte for a value in [0, 127], and at 64 bits
// to its low 32 bits for one in [0, 2^31), or with SF dead [0, 2^32). (Not to 16 bits: 66 with an imm16 stalls Intel's
// decoders. And not in memory: the narrower access is another access.)
#define BUSTER_X86_NARROW_BYTE (BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_SIGNED8 | BUSTER_X86_CHOICE_NONNEGATIVE)
#define BUSTER_X86_TEST_CHOICES \
    BUSTER_X86_SOURCE(TEST, 1, VALUE, TEST_RM8_IMM8, BUSTER_X86_CANDIDATE(TEST_AL_IMM8, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(TEST, 2, VALUE, TEST_RM16_IMM16, BUSTER_X86_CANDIDATE(TEST_AL_IMM8, BUSTER_X86_NARROW_BYTE | BUSTER_X86_CHOICE_ACCUMULATOR, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM8_IMM8, BUSTER_X86_NARROW_BYTE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_AX_IMM16, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(TEST_RM16_IMM16, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(TEST, 4, VALUE, TEST_RM32_IMM32, BUSTER_X86_CANDIDATE(TEST_AL_IMM8, BUSTER_X86_NARROW_BYTE | BUSTER_X86_CHOICE_ACCUMULATOR, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM8_IMM8, BUSTER_X86_NARROW_BYTE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_EAX_IMM32, BUSTER_X86_REG_ACC, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(TEST_RM32_IMM32, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(TEST, 8, VALUE, TEST_RM64_IMM32, BUSTER_X86_CANDIDATE(TEST_AL_IMM8, BUSTER_X86_NARROW_BYTE | BUSTER_X86_CHOICE_ACCUMULATOR, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM8_IMM8, BUSTER_X86_NARROW_BYTE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_EAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_SIGNED32 | BUSTER_X86_CHOICE_NONNEGATIVE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM32_IMM32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_SIGNED32 | BUSTER_X86_CHOICE_NONNEGATIVE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_EAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_UNSIGNED32, BUSTER_X86_SAME_NARROW_TEST), \
                      BUSTER_X86_CANDIDATE(TEST_RM32_IMM32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_UNSIGNED32, BUSTER_X86_SAME_NARROW_TEST), \
                      BUSTER_X86_CANDIDATE(TEST_RAX_IMM32, BUSTER_X86_REG_ACC | BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(TEST_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))

// mov of a constant: into a register, xor r32, r32 for 0 (flags dead), then the +r forms (a 32-bit write zero-extends:
// mov r32, imm32 for a 64-bit value below 2^32), the sign-extended imm32 and movabs; into memory, the imm forms.
#define BUSTER_X86_MOVE_CHOICES \
    BUSTER_X86_SOURCE(MOV, 1, VALUE, MOV_RM8_IMM8, BUSTER_X86_CANDIDATE(MOV_R8_IMM8, BUSTER_X86_CHOICE_REGISTER, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(MOV_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(MOV, 2, VALUE, MOV_RM16_IMM16, BUSTER_X86_CANDIDATE(MOV_R16_IMM16, BUSTER_X86_CHOICE_REGISTER, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(MOV_RM16_IMM16, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(MOV, 4, VALUE, MOV_RM32_IMM32, BUSTER_X86_CANDIDATE(XOR_RM32_R32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, 0), \
                      BUSTER_X86_CANDIDATE(MOV_R32_IMM32, BUSTER_X86_CHOICE_REGISTER, BUSTER_X86_SAME_ALL), BUSTER_X86_CANDIDATE(MOV_RM32_IMM32, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(MOV, 8, VALUE, MOV_RM64_IMM32, BUSTER_X86_CANDIDATE(XOR_RM32_R32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_ZERO, 0), \
                      BUSTER_X86_CANDIDATE(MOV_R32_IMM32, BUSTER_X86_CHOICE_REGISTER | BUSTER_X86_CHOICE_UNSIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(MOV_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(MOV_R64_IMM64, BUSTER_X86_CHOICE_REGISTER, BUSTER_X86_SAME_ALL))

// A shift or rotate by an immediate count: by 1, the form without one.
#define BUSTER_X86_SHIFT_CHOICES(N) \
    BUSTER_X86_SOURCE(N, 1, COUNT, N##_RM8_IMM8, BUSTER_X86_CANDIDATE(N##_RM8_1, BUSTER_X86_CHOICE_ONE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM8_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 2, COUNT, N##_RM16_IMM8, BUSTER_X86_CANDIDATE(N##_RM16_1, BUSTER_X86_CHOICE_ONE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM16_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 4, COUNT, N##_RM32_IMM8, BUSTER_X86_CANDIDATE(N##_RM32_1, BUSTER_X86_CHOICE_ONE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM32_IMM8, 0, BUSTER_X86_SAME_ALL)) \
    BUSTER_X86_SOURCE(N, 8, COUNT, N##_RM64_IMM8, BUSTER_X86_CANDIDATE(N##_RM64_1, BUSTER_X86_CHOICE_ONE, BUSTER_X86_SAME_ALL), \
                      BUSTER_X86_CANDIDATE(N##_RM64_IMM8, 0, BUSTER_X86_SAME_ALL))

// The rows: the operations a selector gives an immediate.
static const X86ChoiceSource x86_choice_sources[] = {
    BUSTER_X86_STEP_CHOICES(ADD, INC, DEC)
    BUSTER_X86_ARITHMETIC_CHOICES(OR)
    BUSTER_X86_ARITHMETIC_CHOICES(ADC)
    BUSTER_X86_ARITHMETIC_CHOICES(SBB)
    BUSTER_X86_ARITHMETIC_CHOICES(AND)
    BUSTER_X86_STEP_CHOICES(SUB, DEC, INC)
    BUSTER_X86_ARITHMETIC_CHOICES(XOR)
    BUSTER_X86_COMPARE_CHOICES
    BUSTER_X86_TEST_CHOICES
    BUSTER_X86_MOVE_CHOICES
    // imul r, r/m, imm.
    BUSTER_X86_SOURCE(IMUL, 2, VALUE, IMUL_R16_RM16_IMM16, BUSTER_X86_CANDIDATE(IMUL_R16_RM16_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL),
                      BUSTER_X86_CANDIDATE(IMUL_R16_RM16_IMM16, 0, BUSTER_X86_SAME_ALL))
    BUSTER_X86_SOURCE(IMUL, 4, VALUE, IMUL_R32_RM32_IMM32, BUSTER_X86_CANDIDATE(IMUL_R32_RM32_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL),
                      BUSTER_X86_CANDIDATE(IMUL_R32_RM32_IMM32, 0, BUSTER_X86_SAME_ALL))
    BUSTER_X86_SOURCE(IMUL, 8, VALUE, IMUL_R64_RM64_IMM32, BUSTER_X86_CANDIDATE(IMUL_R64_RM64_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL),
                      BUSTER_X86_CANDIDATE(IMUL_R64_RM64_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))
    BUSTER_X86_SHIFT_CHOICES(ROL)
    BUSTER_X86_SHIFT_CHOICES(ROR)
    BUSTER_X86_SHIFT_CHOICES(SHL)
    BUSTER_X86_SHIFT_CHOICES(SHR)
    BUSTER_X86_SHIFT_CHOICES(SAR)
    // push imm: 8 bytes on the stack, sign-extended.
    BUSTER_X86_SOURCE(PUSH, 8, VALUE, PUSH_IMM32, BUSTER_X86_CANDIDATE(PUSH_IMM8, BUSTER_X86_CHOICE_SIGNED8, BUSTER_X86_SAME_ALL),
                      BUSTER_X86_CANDIDATE(PUSH_IMM32, BUSTER_X86_CHOICE_SIGNED32, BUSTER_X86_SAME_ALL))
};
#undef BUSTER_X86_SOURCE
#undef BUSTER_X86_CANDIDATE
_Static_assert(BUSTER_ARRAY_LENGTH(x86_choice_sources) <= BUSTER_X86_CHOICE_ROWS_MAX, "the vector path's tables have room for the rows");

// A row as the paths use it: its candidates' forms and conditions (VALID and the flags they need dead added), for the
// scalar twin; and the same transposed, a word per row and candidate, for the vector path's in-register lookups.
typedef struct X86ChoiceRow X86ChoiceRow;
struct X86ChoiceRow
{
    uint16_t forms[BUSTER_X86_CHOICE_CANDIDATES];
    uint16_t conditions[BUSTER_X86_CHOICE_CANDIDATES];
    uint8_t operation;
    uint8_t width;
    uint8_t kind;
};

static X86ChoiceRow x86_choice_rows[BUSTER_X86_CHOICE_ROWS_MAX];
static uint32_t x86_choice_row_count;
// The row of each operation at each width (log2 of its bytes), or BUSTER_X86_CHOICE_NO_ROW.
static uint16_t x86_choice_row_index[X86_OPERATION_COUNT][4];
static uint16_t x86_choice_forms_by_candidate[BUSTER_X86_CHOICE_CANDIDATES][BUSTER_X86_CHOICE_ROWS_MAX] __attribute__((aligned(64)));
static uint16_t x86_choice_conditions_by_candidate[BUSTER_X86_CHOICE_CANDIDATES][BUSTER_X86_CHOICE_ROWS_MAX] __attribute__((aligned(64)));
// Per row: log2 of its width in bits 0-1, and a count row's bit 2.
static uint16_t x86_choice_row_info[BUSTER_X86_CHOICE_ROWS_MAX] __attribute__((aligned(64)));
#define BUSTER_X86_CHOICE_INFO_COUNT 4

// The row of an operation with an immediate at a width (1, 2, 4 or 8 bytes), for a request's row: BUSTER_X86_CHOICE_NO_ROW
// for one the rows don't have.
static __attribute__((unused)) uint32_t x86_choice_row(X86Operation operation, uint32_t width)
{
    const uint32_t log2 = width == 1 ? 0 : width == 2 ? 1 : width == 4 ? 2 : 3;
    const bool known = operation < X86_OPERATION_COUNT && (width == 1 || width == 2 || width == 4 || width == 8);
    return known ? x86_choice_row_index[operation][log2] : BUSTER_X86_CHOICE_NO_ROW;
}

// The scalar classification: a request's classes and context (BUSTER_X86_CHOICE_*).
static uint16_t x86_choice_have(uint32_t row, int64_t value, uint16_t context)
{
    const X86ChoiceRow* choice = &x86_choice_rows[row];
    const uint32_t width = choice->width;
    const uint64_t mask = x86_width_mask(width);
    const uint64_t pattern = (uint64_t)value & mask;
    const bool valid = choice->kind == BUSTER_X86_IMMEDIATE_COUNT ? (uint64_t)value <= x86_count_mask(width) : x86_value_fits_width(value, width);
    const bool signed8 = ((uint64_t)(int64_t)(int8_t)(uint8_t)pattern & mask) == pattern;
    const bool signed32 = ((uint64_t)(int64_t)(int32_t)(uint32_t)pattern & mask) == pattern;
    const bool unsigned32 = pattern <= UINT32_MAX;
    const bool nonnegative = (pattern >> (8 * width - 1)) == 0;
    const uint32_t live = context & BUSTER_X86_CONTEXT_LIVE_FLAGS;
    uint32_t have = context & BUSTER_X86_CONTEXT_GIVEN;
    have |= signed8 ? BUSTER_X86_CHOICE_SIGNED8 : 0;
    have |= signed32 ? BUSTER_X86_CHOICE_SIGNED32 : 0;
    have |= unsigned32 ? BUSTER_X86_CHOICE_UNSIGNED32 : 0;
    have |= nonnegative ? BUSTER_X86_CHOICE_NONNEGATIVE : 0;
    have |= pattern == 0 ? BUSTER_X86_CHOICE_ZERO : 0;
    have |= pattern == 1 ? BUSTER_X86_CHOICE_ONE : 0;
    have |= pattern == mask ? BUSTER_X86_CHOICE_MINUS_ONE : 0;
    have |= valid ? BUSTER_X86_CHOICE_VALID : 0;
    have |= (live & BUSTER_X86_FLAG_CF) == 0 ? BUSTER_X86_CHOICE_CF_DEAD : 0;
    have |= (live & BUSTER_X86_FLAG_AF) == 0 ? BUSTER_X86_CHOICE_AF_DEAD : 0;
    have |= (live & BUSTER_X86_FLAG_SF) == 0 ? BUSTER_X86_CHOICE_SF_DEAD : 0;
    have |= live == 0 ? BUSTER_X86_CHOICE_FLAGS_DEAD : 0;
    return (uint16_t)have;
}

// The scalar twin of x86_choose_forms: each request's form, the first candidate of its row whose conditions it has, or 0
// (X86_FORM_NONE) when none does: the value fits no immediate of the row (it needs a register) or the width at all.
static __attribute__((unused)) void x86_choose_forms_scalar(const uint16_t* rows, const int64_t* values, const uint16_t* contexts, uint64_t count, uint16_t* forms)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        const X86ChoiceRow* choice = &x86_choice_rows[rows[i]];
        const uint16_t have = x86_choice_have(rows[i], values[i], contexts[i]);
        uint16_t form = X86_FORM_NONE;
        for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES && form == X86_FORM_NONE; candidate += 1)
        {
            form = (choice->conditions[candidate] & ~have) == 0 ? choice->forms[candidate] : X86_FORM_NONE;
        }
        forms[i] = form;
    }
}

// The value classes of 8 requests (x86_choice_have's, without the context) in 64-bit lanes, from their values and their
// rows' info (log2 of the width, and the count bit).
static __m512i x86_choice_classes(__m512i value, __m512i info)
{
    const __m512i log2 = _mm512_and_si512(info, _mm512_set1_epi64(3));
    const __m512i mask = _mm512_permutexvar_epi64(log2, _mm512_setr_epi64(0xff, 0xffff, 0xffffffff, -1, 0, 0, 0, 0));
    const __m512i bias = _mm512_permutexvar_epi64(log2, _mm512_setr_epi64(0x80, 0x8000, 0x80000000, 0, 0, 0, 0, 0));
    const __m512i span = _mm512_permutexvar_epi64(log2, _mm512_setr_epi64(0x17f, 0x17fff, 0x17fffffffll, -1, 0, 0, 0, 0));
    const __m512i count_limit = _mm512_permutexvar_epi64(log2, _mm512_setr_epi64(31, 31, 31, 63, 0, 0, 0, 0));
    const __m512i sign = _mm512_permutexvar_epi64(log2, _mm512_setr_epi64(7, 15, 31, 63, 0, 0, 0, 0));
    const __m512i pattern = _mm512_and_si512(value, mask);
    const __mmask8 count = _mm512_test_epi64_mask(info, _mm512_set1_epi64(BUSTER_X86_CHOICE_INFO_COUNT));
    const __mmask8 valid = (_mm512_mask_cmple_epu64_mask((__mmask8)~count, _mm512_add_epi64(value, bias), span)) |
                           (_mm512_mask_cmple_epu64_mask(count, value, count_limit));
    const __m512i signed8 = _mm512_and_si512(_mm512_srai_epi64(_mm512_slli_epi64(pattern, 56), 56), mask);
    const __m512i signed32 = _mm512_and_si512(_mm512_srai_epi64(_mm512_slli_epi64(pattern, 32), 32), mask);
    __m512i classes = _mm512_maskz_mov_epi64(_mm512_cmpeq_epi64_mask(signed8, pattern), _mm512_set1_epi64(BUSTER_X86_CHOICE_SIGNED8));
    classes = _mm512_mask_or_epi64(classes, _mm512_cmpeq_epi64_mask(signed32, pattern), classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_SIGNED32));
    classes = _mm512_mask_or_epi64(classes, _mm512_cmple_epu64_mask(pattern, _mm512_set1_epi64(UINT32_MAX)), classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_UNSIGNED32));
    classes = _mm512_mask_or_epi64(classes, _mm512_testn_epi64_mask(_mm512_srlv_epi64(pattern, sign), _mm512_set1_epi64(1)), classes,
                                   _mm512_set1_epi64(BUSTER_X86_CHOICE_NONNEGATIVE));
    classes = _mm512_mask_or_epi64(classes, _mm512_testn_epi64_mask(pattern, pattern), classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_ZERO));
    classes = _mm512_mask_or_epi64(classes, _mm512_cmpeq_epi64_mask(pattern, _mm512_set1_epi64(1)), classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_ONE));
    classes = _mm512_mask_or_epi64(classes, _mm512_cmpeq_epi64_mask(pattern, mask), classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_MINUS_ONE));
    classes = _mm512_mask_or_epi64(classes, valid, classes, _mm512_set1_epi64(BUSTER_X86_CHOICE_VALID));
    return classes;
}

// Each request's form, as x86_choose_forms_scalar's, 32 requests at a time: the rows' info, the candidates' forms and
// conditions are looked up in registers (vpermi2w over a row's 64 words); the values are classified 8 at a time
// (x86_choice_classes) and narrowed to the requests' words; the context's flags become the dead-flag bits; and each
// candidate in turn takes the requests it fits that no earlier one took. The columns are read, and the forms written,
// with masked loads and stores, so nothing past them is touched. Requires x86_semantics_initialize.
static __attribute__((unused)) void x86_choose_forms(const uint16_t* rows, const int64_t* values, const uint16_t* contexts, uint64_t count, uint16_t* forms)
{
#if !BUSTER_HOST_VECTOR
    x86_choose_forms_scalar(rows, values, contexts, count, forms);
#else
    const __m512i info_low = _mm512_load_si512(x86_choice_row_info);
    const __m512i info_high = _mm512_load_si512(x86_choice_row_info + 32);
    const __m512i flags = _mm512_set1_epi16(BUSTER_X86_CONTEXT_LIVE_FLAGS);
    for (uint64_t first = 0; first < count; first += 32)
    {
        const uint64_t left = count - first;
        const __mmask32 lanes = left >= 32 ? 0xffffffffu : (__mmask32)((1u << left) - 1);
        const __m512i row = _mm512_maskz_loadu_epi16(lanes, rows + first);
        const __m512i context = _mm512_maskz_loadu_epi16(lanes, contexts + first);
        const __m512i info = _mm512_permutex2var_epi16(info_low, row, info_high);
        const __m512i value0 = _mm512_maskz_loadu_epi64((__mmask8)lanes, values + first);
        const __m512i value1 = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), values + first + 8);
        const __m512i value2 = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 16), values + first + 16);
        const __m512i value3 = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 24), values + first + 24);
        const __m128i narrowed0 = _mm512_cvtepi64_epi16(x86_choice_classes(value0, _mm512_cvtepu16_epi64(_mm512_extracti32x4_epi32(info, 0))));
        const __m128i narrowed1 = _mm512_cvtepi64_epi16(x86_choice_classes(value1, _mm512_cvtepu16_epi64(_mm512_extracti32x4_epi32(info, 1))));
        const __m128i narrowed2 = _mm512_cvtepi64_epi16(x86_choice_classes(value2, _mm512_cvtepu16_epi64(_mm512_extracti32x4_epi32(info, 2))));
        const __m128i narrowed3 = _mm512_cvtepi64_epi16(x86_choice_classes(value3, _mm512_cvtepu16_epi64(_mm512_extracti32x4_epi32(info, 3))));
        const __m256i half_low = _mm256_inserti128_si256(_mm256_castsi128_si256(narrowed0), narrowed1, 1);
        const __m256i half_high = _mm256_inserti128_si256(_mm256_castsi128_si256(narrowed2), narrowed3, 1);
        __m512i have = _mm512_inserti64x4(_mm512_castsi256_si512(half_low), half_high, 1);
        have = _mm512_or_si512(have, _mm512_and_si512(context, _mm512_set1_epi16(BUSTER_X86_CONTEXT_GIVEN)));
        const __m512i live = _mm512_and_si512(context, flags);
        // AVX-512 has no masked OR of words: OR in a zero-masked move.
        have = _mm512_or_si512(have, _mm512_maskz_mov_epi16(_mm512_testn_epi16_mask(live, _mm512_set1_epi16(BUSTER_X86_FLAG_CF)), _mm512_set1_epi16(BUSTER_X86_CHOICE_CF_DEAD)));
        have = _mm512_or_si512(have, _mm512_maskz_mov_epi16(_mm512_testn_epi16_mask(live, _mm512_set1_epi16(BUSTER_X86_FLAG_AF)), _mm512_set1_epi16(BUSTER_X86_CHOICE_AF_DEAD)));
        have = _mm512_or_si512(have, _mm512_maskz_mov_epi16(_mm512_testn_epi16_mask(live, _mm512_set1_epi16(BUSTER_X86_FLAG_SF)), _mm512_set1_epi16(BUSTER_X86_CHOICE_SF_DEAD)));
        have = _mm512_or_si512(have, _mm512_maskz_mov_epi16(_mm512_testn_epi16_mask(live, flags), _mm512_set1_epi16(BUSTER_X86_CHOICE_FLAGS_DEAD)));
        const __m512i missing = _mm512_xor_si512(have, _mm512_set1_epi16(-1));
        __m512i result = _mm512_setzero_si512();
        __mmask32 taken = 0;
        for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES; candidate += 1)
        {
            const uint16_t* conditions = x86_choice_conditions_by_candidate[candidate];
            const uint16_t* candidate_forms = x86_choice_forms_by_candidate[candidate];
            const __m512i needed = _mm512_permutex2var_epi16(_mm512_load_si512(conditions), row, _mm512_load_si512(conditions + 32));
            const __m512i form = _mm512_permutex2var_epi16(_mm512_load_si512(candidate_forms), row, _mm512_load_si512(candidate_forms + 32));
            const __mmask32 fits = _mm512_testn_epi16_mask(needed, missing) & ~taken;
            result = _mm512_mask_mov_epi16(result, fits, form);
            taken |= fits;
        }
        _mm512_mask_storeu_epi16(forms + first, lanes, result);
    }
#endif
}

// The record for a chosen form: `destination` holds the destination operand as the row's operation takes it (a register
// in rm with mode 0, or a memory operand's rm, index, mode and displacement; for imul's three operands, reg the
// destination and rm the source), and `value` the request's value. The accumulator's forms and push's take no rm (the
// form's operands it doesn't have are zero); a zero idiom or test r, r takes the destination twice; and the immediate
// is the field's bits (x86_immediate_field). The caller adds a segment override or the fixup flag.
static __attribute__((unused)) X86Instruction x86_choice_instruction(uint32_t form, X86Instruction destination, int64_t value)
{
    const X86FormDescription* description = x86_form_description(form);
    X86Instruction result = destination;
    result.form = (uint16_t)form;
    if (description->syntax == X86_SYNTAX_ACC_IMM || description->syntax == X86_SYNTAX_IMM)
    {
        result.reg = 0;
        result.rm = 0;
        result.index = 0;
        result.mode = 0;
        result.displacement = 0;
    }
    if (description->syntax == X86_SYNTAX_RM_REG)
    {
        result.reg = result.rm;
    }
    int32_t high = 0;
    x86_immediate_field(form, value, &result.immediate, &high);
    if (description->immediate_bytes == 8)
    {
        result.displacement = high;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Initialization

// The form's operands in its syntax's order (BUSTER_X86_SLOT_*), into `slots`: their count, or UINT32_MAX for a syntax
// the covered forms don't have.
#define BUSTER_X86_SLOT_REG 0
#define BUSTER_X86_SLOT_RM 1
#define BUSTER_X86_SLOT_VVVV 2
#define BUSTER_X86_SLOT_ACC 3
#define BUSTER_X86_SLOT_CL 4
// An immediate, the 1 of a shift by one, or a branch's displacement.
#define BUSTER_X86_SLOT_OTHER 5
static uint32_t x86_effects_slots(uint8_t syntax, uint8_t* slots)
{
    uint32_t count = UINT32_MAX;
    switch (syntax)
    {
    case X86_SYNTAX_NONE:
        count = 0;
        break;
    case X86_SYNTAX_RM:
        slots[0] = BUSTER_X86_SLOT_RM;
        count = 1;
        break;
    case X86_SYNTAX_IMM:
    case X86_SYNTAX_REL:
        slots[0] = BUSTER_X86_SLOT_OTHER;
        count = 1;
        break;
    case X86_SYNTAX_RM_REG:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_REG;
        count = 2;
        break;
    case X86_SYNTAX_REG_RM:
        slots[0] = BUSTER_X86_SLOT_REG;
        slots[1] = BUSTER_X86_SLOT_RM;
        count = 2;
        break;
    case X86_SYNTAX_RM_IMM:
    case X86_SYNTAX_RM_ONE:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_OTHER;
        count = 2;
        break;
    case X86_SYNTAX_RM_CL:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_CL;
        count = 2;
        break;
    case X86_SYNTAX_ACC_IMM:
        slots[0] = BUSTER_X86_SLOT_ACC;
        slots[1] = BUSTER_X86_SLOT_OTHER;
        count = 2;
        break;
    case X86_SYNTAX_REG_RM_IMM:
        slots[0] = BUSTER_X86_SLOT_REG;
        slots[1] = BUSTER_X86_SLOT_RM;
        slots[2] = BUSTER_X86_SLOT_OTHER;
        count = 3;
        break;
    case X86_SYNTAX_RM_REG_IMM:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_REG;
        slots[2] = BUSTER_X86_SLOT_OTHER;
        count = 3;
        break;
    case X86_SYNTAX_RM_REG_CL:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_REG;
        slots[2] = BUSTER_X86_SLOT_CL;
        count = 3;
        break;
    case X86_SYNTAX_REG_VVVV_RM:
        slots[0] = BUSTER_X86_SLOT_REG;
        slots[1] = BUSTER_X86_SLOT_VVVV;
        slots[2] = BUSTER_X86_SLOT_RM;
        count = 3;
        break;
    // The x87's two register operands, ST(0) implicit in the encoding.
    case X86_SYNTAX_ST_RM:
        slots[0] = BUSTER_X86_SLOT_OTHER;
        slots[1] = BUSTER_X86_SLOT_RM;
        count = 2;
        break;
    case X86_SYNTAX_RM_ST:
        slots[0] = BUSTER_X86_SLOT_RM;
        slots[1] = BUSTER_X86_SLOT_OTHER;
        count = 2;
        break;
    default:
        break;
    }
    return count;
}

// A form's record from its row and the form table: the pattern's accesses by role (the syntax's ACC and CL as RAX and
// RCX; an x87 row's ST(i), part of the x87's state, none), each register role's class (vector or general) and, for a
// destination, what its write does to the rest (a general register's by its size; a vector one's from the row), the
// width, and the row's flags, memory, immediate, control and properties. Returns false for a row that doesn't fit its
// form (an access to an operand the form lacks, a register of another class, an x87 register outside an x87 row, a
// vector destination without the row's write, a syntax the slots don't know).
static bool x86_effects_build(uint32_t form, const X86EffectsRow* row, X86Effects* effects)
{
    const X86FormDescription* description = &x86_form_descriptions[form];
    uint8_t slots[4] = { 0 };
    const uint32_t slot_count = x86_effects_slots(description->syntax, slots);
    bool valid = slot_count <= 4;
    uint32_t access[BUSTER_X86_ROLES] = { 0 };
    uint32_t implicit_reads = row->implicit_reads;
    uint32_t implicit_writes = row->implicit_writes;
    for (uint32_t slot = 0; valid && slot < slot_count; slot += 1)
    {
        uint32_t bits = 0;
        switch (row->pattern)
        {
        case X86_PATTERN_MODIFY:
            bits = slot == 0 ? BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE : BUSTER_X86_ROLE_READ;
            break;
        case X86_PATTERN_READ:
            bits = BUSTER_X86_ROLE_READ;
            break;
        case X86_PATTERN_MOVE:
            bits = slot == 0 ? BUSTER_X86_ROLE_WRITE : BUSTER_X86_ROLE_READ;
            break;
        case X86_PATTERN_SWAP:
            bits = BUSTER_X86_ROLE_READ | BUSTER_X86_ROLE_WRITE;
            break;
        default:
            break;
        }
        switch (slots[slot])
        {
        case BUSTER_X86_SLOT_REG:
            access[BUSTER_X86_ROLE_REG] |= bits;
            break;
        case BUSTER_X86_SLOT_RM:
            access[BUSTER_X86_ROLE_RM] |= bits;
            break;
        case BUSTER_X86_SLOT_VVVV:
            access[BUSTER_X86_ROLE_VVVV] |= bits;
            break;
        case BUSTER_X86_SLOT_ACC:
            implicit_reads |= (bits & BUSTER_X86_ROLE_READ) ? BUSTER_X86_GPR(RAX) : 0;
            implicit_writes |= (bits & BUSTER_X86_ROLE_WRITE) ? BUSTER_X86_GPR(RAX) : 0;
            break;
        case BUSTER_X86_SLOT_CL:
            implicit_reads |= BUSTER_X86_GPR(RCX);
            break;
        default:
            break;
        }
    }
    const uint16_t descriptors[BUSTER_X86_ROLES] = { description->reg, description->rm, description->vvvv };
    uint32_t width = row->width;
    uint32_t write_bytes = 0;
    uint32_t destination_rest = BUSTER_X86_REST_WHOLE;
    uint32_t operands = 0;
    for (uint32_t role = 0; role < BUSTER_X86_ROLES; role += 1)
    {
        uint32_t bits = access[role];
        const uint16_t descriptor = descriptors[role];
        const uint32_t class = descriptor & BUSTER_X86_OPERAND_CLASS;
        const bool registers = (descriptor & BUSTER_X86_OPERAND_REGISTER) != 0;
        const bool general = registers && class >= BUSTER_X86_CLASS_GPR8 && class <= BUSTER_X86_CLASS_GPR64;
        const bool vector = registers && class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM;
        bits = registers && class == BUSTER_X86_CLASS_ST && (row->properties & BUSTER_X86_EFFECT_X87) ? 0 : bits;
        valid &= bits == 0 || descriptor != BUSTER_X86_OPERAND_NONE;
        valid &= bits == 0 || !registers || general || vector;
        bits |= vector ? BUSTER_X86_ROLE_VECTOR : 0;
        if ((bits & BUSTER_X86_ROLE_WRITE) && general)
        {
            const uint32_t size = 1u << (class - BUSTER_X86_CLASS_GPR8);
            destination_rest = size < 4 ? BUSTER_X86_REST_MERGE : size == 4 ? BUSTER_X86_REST_ZERO : BUSTER_X86_REST_WHOLE;
            write_bytes = size;
            width = width != 0 ? width : size;
            bits |= destination_rest << BUSTER_X86_ROLE_REST_SHIFT;
        }
        else if ((bits & BUSTER_X86_ROLE_WRITE) && vector)
        {
            valid &= row->vector != 0;
            destination_rest = row->vector >> 8;
            write_bytes = row->vector & 0xff;
            bits |= destination_rest << BUSTER_X86_ROLE_REST_SHIFT;
        }
        operands |= bits << (role * BUSTER_X86_ROLE_BITS);
    }
    width = width != 0 ? width : description->operand_bytes;
    // An implicit write narrower than 4 bytes keeps the rest of the register: it reads the register too.
    implicit_reads |= width != 0 && width < 4 ? implicit_writes : 0;
    uint32_t properties = row->properties;
    // A zero idiom's sources: reg and rm, or a VEX or EVEX form's vvvv and rm.
    const uint16_t first_source = (access[BUSTER_X86_ROLE_VVVV] & BUSTER_X86_ROLE_READ) ? description->vvvv : description->reg;
    const bool both_registers = (first_source & BUSTER_X86_OPERAND_REGISTER) && (description->rm & BUSTER_X86_OPERAND_REGISTER);
    if (!both_registers || destination_rest == BUSTER_X86_REST_MERGE)
    {
        properties &= ~BUSTER_X86_EFFECT_ZERO_IDIOM;
    }
    const uint32_t immediate = description->immediate_bytes != 0 ? row->immediate : BUSTER_X86_IMMEDIATE_NONE;
    valid &= description->immediate_bytes == 0 || immediate != BUSTER_X86_IMMEDIATE_NONE;
    *effects = (X86Effects){
        .implicit_reads = (uint16_t)implicit_reads,
        .implicit_writes = (uint16_t)implicit_writes,
        .operands = (uint16_t)operands,
        .properties = (uint16_t)properties,
        .flags_read = row->flags_read,
        .flags_written = row->flags_written,
        .flags_undefined = row->flags_undefined,
        .memory = row->memory,
        .operation = row->operation,
        .width = (uint8_t)width,
        .write_bytes = (uint8_t)write_bytes,
        .kinds = (uint8_t)(immediate | row->control << 4),
    };
    return valid;
}

// The choice tables from x86_choice_sources: each candidate's conditions with VALID and, for another instruction, the
// flags that must be dead for it to stand for the row's original form (x86_flags_substitutable, with every single
// status flag live in turn: CF, AF and SF have their own bits, and a set past those needs every flag dead). Returns
// false for a row whose candidates don't fit it.
static bool x86_choice_build(void)
{
    bool valid = BUSTER_ARRAY_LENGTH(x86_choice_sources) <= BUSTER_X86_CHOICE_ROWS_MAX;
    for (uint32_t operation = 0; operation < X86_OPERATION_COUNT; operation += 1)
    {
        for (uint32_t log2 = 0; log2 < 4; log2 += 1)
        {
            x86_choice_row_index[operation][log2] = BUSTER_X86_CHOICE_NO_ROW;
        }
    }
    memset(x86_choice_rows, 0, sizeof(x86_choice_rows));
    memset(x86_choice_row_info, 0, sizeof(x86_choice_row_info));
    for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES; candidate += 1)
    {
        for (uint32_t row = 0; row < BUSTER_X86_CHOICE_ROWS_MAX; row += 1)
        {
            x86_choice_forms_by_candidate[candidate][row] = X86_FORM_NONE;
            x86_choice_conditions_by_candidate[candidate][row] = BUSTER_X86_CHOICE_NEVER;
        }
    }
    x86_choice_row_count = 0;
    for (uint32_t source_index = 0; valid && source_index < BUSTER_ARRAY_LENGTH(x86_choice_sources); source_index += 1)
    {
        const X86ChoiceSource* source = &x86_choice_sources[source_index];
        const uint32_t row = x86_choice_row_count;
        const uint32_t log2 = source->width == 1 ? 0 : source->width == 2 ? 1 : source->width == 4 ? 2 : 3;
        valid &= x86_choice_row_index[source->operation][log2] == BUSTER_X86_CHOICE_NO_ROW && (1u << log2) == source->width;
        x86_choice_row_index[source->operation][log2] = (uint16_t)row;
        X86ChoiceRow* choice = &x86_choice_rows[row];
        choice->operation = source->operation;
        choice->width = source->width;
        choice->kind = source->kind;
        x86_choice_row_info[row] = (uint16_t)(log2 | (source->kind == BUSTER_X86_IMMEDIATE_COUNT ? BUSTER_X86_CHOICE_INFO_COUNT : 0));
        const X86Effects* original = x86_effects(source->original);
        valid &= original->operation == source->operation && original->width == source->width;
        for (uint32_t candidate = 0; candidate < BUSTER_X86_CHOICE_CANDIDATES; candidate += 1)
        {
            const X86ChoiceCandidate* entry = &source->candidates[candidate];
            uint32_t conditions = BUSTER_X86_CHOICE_NEVER;
            if (entry->form != X86_FORM_NONE)
            {
                const X86Effects* substitute = x86_effects(entry->form);
                // A shift's candidate for the value 1 stands for the original shifting by 1, whose flags that count resolves
                // (x86_instruction_flags_substitutable); any other stands for the original at every count.
                const bool by_one = source->kind == BUSTER_X86_IMMEDIATE_COUNT && (entry->conditions & BUSTER_X86_CHOICE_ONE);
                const X86Instruction original_by_one = {
                    .form = source->original,
                    .immediate = 1,
                };
                const X86Instruction substitute_by_one = {
                    .form = entry->form,
                    .immediate = 1,
                };
                uint32_t dead = 0;
                for (uint32_t flag = 1; flag <= BUSTER_X86_FLAG_OF; flag <<= 1)
                {
                    const bool substitutable = by_one ? x86_instruction_flags_substitutable(&original_by_one, &substitute_by_one, entry->same, (uint8_t)flag)
                                                      : x86_flags_substitutable(original, substitute, entry->same, (uint8_t)flag);
                    dead |= substitutable ? 0 : flag;
                }
                conditions = entry->conditions | BUSTER_X86_CHOICE_VALID;
                conditions |= (dead & BUSTER_X86_FLAG_CF) ? BUSTER_X86_CHOICE_CF_DEAD : 0;
                conditions |= (dead & BUSTER_X86_FLAG_AF) ? BUSTER_X86_CHOICE_AF_DEAD : 0;
                conditions |= (dead & BUSTER_X86_FLAG_SF) ? BUSTER_X86_CHOICE_SF_DEAD : 0;
                conditions |= (dead & ~(BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_SF)) ? BUSTER_X86_CHOICE_FLAGS_DEAD : 0;
                // A candidate is covered, has the row's kind of immediate or none, and isn't wider than the row.
                const uint32_t kind = x86_effects_immediate_kind(substitute);
                valid &= substitute->operation != X86_OPERATION_NONE && (kind == BUSTER_X86_IMMEDIATE_NONE || kind == source->kind) &&
                         substitute->width <= source->width;
            }
            choice->forms[candidate] = entry->form;
            choice->conditions[candidate] = (uint16_t)conditions;
            x86_choice_forms_by_candidate[candidate][row] = entry->form;
            x86_choice_conditions_by_candidate[candidate][row] = (uint16_t)conditions;
        }
        x86_choice_row_count += 1;
    }
    return valid;
}

// Builds the effects of every covered form and the form choice's tables. Returns false if a row doesn't fit its forms,
// rows overlap, or there are more covered forms than BUSTER_X86_EFFECTS_CAPACITY (a broken table): the test catches it.
static bool x86_semantics_initialize(void)
{
    memset(x86_effects_index, 0, sizeof(x86_effects_index));
    x86_effects_records[0] = x86_effects_unknown;
    x86_effects_count = 1;
    bool valid = true;
    for (uint32_t row = 0; row < BUSTER_ARRAY_LENGTH(x86_effects_rows); row += 1)
    {
        const X86EffectsRow* effects_row = &x86_effects_rows[row];
        valid &= effects_row->first <= effects_row->last;
        for (uint32_t form = effects_row->first; valid && form <= effects_row->last; form += 1)
        {
            valid &= x86_effects_index[form] == 0 && x86_effects_count < BUSTER_X86_EFFECTS_CAPACITY;
            if (valid)
            {
                valid = x86_effects_build(form, effects_row, &x86_effects_records[x86_effects_count]);
                x86_effects_index[form] = (uint16_t)x86_effects_count;
                x86_effects_count += 1;
            }
        }
    }
    for (uint32_t copy = 0; copy < 3; copy += 1)
    {
        memcpy(x86_effects_by_form + copy * X86_FORM_COUNT, x86_effects_index, sizeof(x86_effects_index));
    }
    memset(x86_effects_by_form + 3 * X86_FORM_COUNT, 0, sizeof(x86_effects_by_form) - 3 * sizeof(x86_effects_index));
    valid = valid && x86_choice_build();
    x86_semantics_initialized = valid;
    return valid;
}
