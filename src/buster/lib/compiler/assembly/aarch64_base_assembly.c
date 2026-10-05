// Base A64 source-assembly encoder.
//
// Ownership: textual operands for the base A64 instruction set, the common
// AdvSIMD forms and the aliases compilers print (GNU/LLVM syntax), encoded
// directly to one word.
// assembly.c calls the single entry point, `a64_base_assemble`, only after
// its table-driven AArch64 owners refuse a statement, so this module never
// changes the encoding of a spelling those owners already accept.
//
// Map (searchable symbols):
// - Text and operands: `a64_base_operand_parse` classifies one operand
//   (`A64BaseOperand`); `a64_base_operands_split` splits at top-level commas;
//   `a64_base_memory_parse` owns `[base{, offset}]{!}` addressing and
//   `a64_base_list_parse` owns `{ Vt.T, ... }{[lane]}` register lists.
// - Immediates: `a64_base_integer_parse`, `a64_base_float_parse`,
//   `a64_base_logical_immediate` (bitmask N:immr:imms) and
//   `a64_base_float_immediate` (FMOV imm8).
// - Mnemonics: `a64_base_mnemonics` maps each spelling to an
//   `A64BaseFamily` plus family-specific bits; LSE atomics are decoded from
//   their regular spelling by `a64_base_atomic_mnemonic`.
// - Families: one `a64_base_encode_*` function per instruction class, selected
//   by the switch in `a64_base_encode`; `a64_base_feature` gates FP, NEON and
//   FP16 per entry, and `a64_base_system_register` names MRS/MSR operands.
//
// Constraints: no symbols, labels or relocation specifiers (those keep their
// fixup-aware owners); register-31 roles (SP versus ZR) follow each field's
// architectural role; writeback forms reject a base that is also a transfer
// register, as LLVM's assembler does. Feature-gated families report the
// missing feature instead of encoding.

#include <buster/lib/compiler/assembly/aarch64_base_assembly.h>

#define A64_BASE_MAX_OPERANDS 6u
// Unscaled and pre/post-indexed single transfers use a signed nine-bit byte
// offset; pairs use a signed seven-bit offset scaled by the access size.
#define A64_BASE_IMM9_MIN (-256)
#define A64_BASE_IMM9_MAX 255
#define A64_BASE_IMM7_MIN (-64)
#define A64_BASE_IMM7_MAX 63
#define A64_BASE_IMM12_MAX 4095u
// FMOV's imm8 covers +-(16..31)/16 * 2^(-3..4); its finest step is 2^-7,
// which needs seven decimal fraction digits.
#define A64_BASE_FLOAT_FRACTION_DIGITS_MAX 7
#define A64_BASE_SIZE_FROM_REGISTER 4u

typedef enum A64BaseOperandKind
{
    A64_BASE_OPERAND_NONE,
    A64_BASE_OPERAND_GPR,
    A64_BASE_OPERAND_FPR,
    A64_BASE_OPERAND_VECTOR,
    A64_BASE_OPERAND_ELEMENT,
    A64_BASE_OPERAND_IMMEDIATE,
    A64_BASE_OPERAND_FLOAT,
    A64_BASE_OPERAND_MODIFIER,
    A64_BASE_OPERAND_CONDITION,
    A64_BASE_OPERAND_MEMORY,
    A64_BASE_OPERAND_PREFETCH,
    A64_BASE_OPERAND_LIST,
    A64_BASE_OPERAND_NAME,
} A64BaseOperandKind;

// Shift values match the shifted-register `shift` field; extend values minus
// A64_BASE_MODIFIER_UXTB match the extended-register `option` field.
typedef enum A64BaseModifier
{
    A64_BASE_MODIFIER_LSL,
    A64_BASE_MODIFIER_LSR,
    A64_BASE_MODIFIER_ASR,
    A64_BASE_MODIFIER_ROR,
    A64_BASE_MODIFIER_UXTB,
    A64_BASE_MODIFIER_UXTH,
    A64_BASE_MODIFIER_UXTW,
    A64_BASE_MODIFIER_UXTX,
    A64_BASE_MODIFIER_SXTB,
    A64_BASE_MODIFIER_SXTH,
    A64_BASE_MODIFIER_SXTW,
    A64_BASE_MODIFIER_SXTX,
    A64_BASE_MODIFIER_MSL,
    A64_BASE_MODIFIER_COUNT,
} A64BaseModifier;

typedef enum A64BaseAddress
{
    A64_BASE_ADDRESS_BASE,
    A64_BASE_ADDRESS_IMMEDIATE,
    A64_BASE_ADDRESS_REGISTER,
} A64BaseAddress;

typedef struct A64BaseRegister A64BaseRegister;
struct A64BaseRegister
{
    // GPR/FPR: register width. Vector/element: element width.
    u8 bits;
    u8 number;
    u8 lanes;
    u8 index;
    // Register lists: member count; `index_present` marks a lane suffix.
    u8 count;
    bool index_present;
    bool sp;
};

typedef struct A64BaseOperand A64BaseOperand;
struct A64BaseOperand
{
    // Barrier options and system-register names.
    String8 name;
    // Integer magnitude, or the decimal significand of a float immediate.
    u64 magnitude;
    s32 exponent;
    A64BaseRegister reg;
    A64BaseRegister index;
    u8 kind;
    u8 modifier;
    u8 amount;
    u8 address;
    bool amount_present;
    bool modifier_present;
    bool negative;
    bool writeback;
};

typedef enum A64BaseFamily
{
    A64_BASE_FAMILY_ADD_SUB,
    A64_BASE_FAMILY_COMPARE,
    A64_BASE_FAMILY_NEGATE,
    A64_BASE_FAMILY_LOGICAL,
    A64_BASE_FAMILY_TEST,
    A64_BASE_FAMILY_NOT,
    A64_BASE_FAMILY_MOVE,
    A64_BASE_FAMILY_MOVE_WIDE,
    A64_BASE_FAMILY_BITFIELD,
    A64_BASE_FAMILY_SHIFT,
    A64_BASE_FAMILY_EXTEND,
    A64_BASE_FAMILY_BITFIELD_EXTRACT,
    A64_BASE_FAMILY_BITFIELD_INSERT,
    A64_BASE_FAMILY_EXTRACT,
    A64_BASE_FAMILY_CARRY,
    A64_BASE_FAMILY_NEGATE_CARRY,
    A64_BASE_FAMILY_SELECT,
    A64_BASE_FAMILY_SET,
    A64_BASE_FAMILY_SELECT_SAME,
    A64_BASE_FAMILY_COMPARE_CONDITIONAL,
    A64_BASE_FAMILY_DATA2,
    A64_BASE_FAMILY_DATA1,
    A64_BASE_FAMILY_DATA3,
    A64_BASE_FAMILY_MULTIPLY,
    A64_BASE_FAMILY_LOAD_STORE,
    A64_BASE_FAMILY_PAIR,
    A64_BASE_FAMILY_EXCLUSIVE,
    A64_BASE_FAMILY_FP_MOVE,
    A64_BASE_FAMILY_FP_DATA2,
    A64_BASE_FAMILY_FP_DATA1,
    A64_BASE_FAMILY_FP_CONVERT_PRECISION,
    A64_BASE_FAMILY_FP_DATA3,
    A64_BASE_FAMILY_FP_COMPARE,
    A64_BASE_FAMILY_FP_COMPARE_CONDITIONAL,
    A64_BASE_FAMILY_FP_SELECT,
    A64_BASE_FAMILY_FP_TO_INTEGER,
    A64_BASE_FAMILY_FP_FROM_INTEGER,
    A64_BASE_FAMILY_ELEMENT_UNSIGNED_MOVE,
    A64_BASE_FAMILY_ELEMENT_SIGNED_MOVE,
    A64_BASE_FAMILY_ELEMENT_INSERT,
    A64_BASE_FAMILY_ELEMENT_DUPLICATE,
    A64_BASE_FAMILY_SYSTEM_BARRIER,
    A64_BASE_FAMILY_EXCEPTION,
    A64_BASE_FAMILY_SYSTEM_MOVE,
    A64_BASE_FAMILY_VECTOR_IMMEDIATE,
    A64_BASE_FAMILY_VECTOR_LOGICAL_IMMEDIATE,
    A64_BASE_FAMILY_VECTOR_FP_IMMEDIATE,
    A64_BASE_FAMILY_VECTOR_NOT,
    A64_BASE_FAMILY_VECTOR_EXTRACT,
    A64_BASE_FAMILY_VECTOR_NARROW,
    A64_BASE_FAMILY_VECTOR_SHIFT,
    A64_BASE_FAMILY_VECTOR_SHIFT_LONG,
    A64_BASE_FAMILY_VECTOR_LONG,
    A64_BASE_FAMILY_VECTOR_COMPARE_ZERO,
    A64_BASE_FAMILY_VECTOR_LANE,
} A64BaseFamily;

// Vector shift `variant` flags: right shifts, and the upper-half (`2`) and
// zero-shift alias (UXTL/SXTL) spellings of the long shifts.
enum
{
    A64_BASE_VECTOR_RIGHT = 1,
    A64_BASE_VECTOR_UPPER = 1,
    A64_BASE_VECTOR_ALIAS = 2,
};

typedef struct A64BaseMnemonic A64BaseMnemonic;
struct A64BaseMnemonic
{
    String8 name;
    u8 family;
    u8 variant;
    u32 bits;
};

// Load/store `variant` values. The unscaled-only spellings set the flag.
enum
{
    A64_BASE_TRANSFER_STORE,
    A64_BASE_TRANSFER_LOAD,
    A64_BASE_TRANSFER_LOAD_SIGNED,
    A64_BASE_TRANSFER_PREFETCH,
    A64_BASE_TRANSFER_KIND_MASK = 3,
    A64_BASE_TRANSFER_UNSCALED = 4,
};

// Pair `variant` flags.
enum
{
    A64_BASE_PAIR_LOAD = 1,
    A64_BASE_PAIR_NO_ALLOCATE = 2,
    A64_BASE_PAIR_SIGNED_WORD = 4,
};

// Exclusive/ordered `variant` flags; `bits` is the access size or
// A64_BASE_SIZE_FROM_REGISTER.
enum
{
    A64_BASE_EXCLUSIVE_LOAD = 1,
    A64_BASE_EXCLUSIVE_ORDERED = 2,
    A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE = 4,
    A64_BASE_EXCLUSIVE_STATUS = 8,
};

// Multiply-family `variant` flags; `bits` holds op31:o0.
enum
{
    A64_BASE_MULTIPLY_LONG = 1,
    A64_BASE_MULTIPLY_ACCUMULATE = 2,
    A64_BASE_MULTIPLY_X_ONLY = 4,
};

#define A64_BASE_ENTRY(text, family, variant, bits) {S8_INITIALIZER(text), (u8)(family), (u8)(variant), (u32)(bits)}

BUSTER_GLOBAL_LOCAL const A64BaseMnemonic a64_base_mnemonics[] = {
    A64_BASE_ENTRY("add", A64_BASE_FAMILY_ADD_SUB, 0, 0),
    A64_BASE_ENTRY("adds", A64_BASE_FAMILY_ADD_SUB, 1, 0),
    A64_BASE_ENTRY("sub", A64_BASE_FAMILY_ADD_SUB, 2, 0),
    A64_BASE_ENTRY("subs", A64_BASE_FAMILY_ADD_SUB, 3, 0),
    A64_BASE_ENTRY("cmn", A64_BASE_FAMILY_COMPARE, 1, 0),
    A64_BASE_ENTRY("cmp", A64_BASE_FAMILY_COMPARE, 3, 0),
    A64_BASE_ENTRY("neg", A64_BASE_FAMILY_NEGATE, 2, 0),
    A64_BASE_ENTRY("negs", A64_BASE_FAMILY_NEGATE, 3, 0),
    A64_BASE_ENTRY("and", A64_BASE_FAMILY_LOGICAL, 0, 0),
    A64_BASE_ENTRY("orr", A64_BASE_FAMILY_LOGICAL, 1, 0),
    A64_BASE_ENTRY("eor", A64_BASE_FAMILY_LOGICAL, 2, 0),
    A64_BASE_ENTRY("ands", A64_BASE_FAMILY_LOGICAL, 3, 0),
    A64_BASE_ENTRY("bic", A64_BASE_FAMILY_LOGICAL, 4, 0),
    A64_BASE_ENTRY("orn", A64_BASE_FAMILY_LOGICAL, 5, 0),
    A64_BASE_ENTRY("eon", A64_BASE_FAMILY_LOGICAL, 6, 0),
    A64_BASE_ENTRY("bics", A64_BASE_FAMILY_LOGICAL, 7, 0),
    A64_BASE_ENTRY("tst", A64_BASE_FAMILY_TEST, 3, 0),
    A64_BASE_ENTRY("mvn", A64_BASE_FAMILY_NOT, 5, 0),
    A64_BASE_ENTRY("mov", A64_BASE_FAMILY_MOVE, 0, 0),
    A64_BASE_ENTRY("movn", A64_BASE_FAMILY_MOVE_WIDE, 0, 0),
    A64_BASE_ENTRY("movz", A64_BASE_FAMILY_MOVE_WIDE, 2, 0),
    A64_BASE_ENTRY("movk", A64_BASE_FAMILY_MOVE_WIDE, 3, 0),
    A64_BASE_ENTRY("sbfm", A64_BASE_FAMILY_BITFIELD, 0, 0),
    A64_BASE_ENTRY("bfm", A64_BASE_FAMILY_BITFIELD, 1, 0),
    A64_BASE_ENTRY("ubfm", A64_BASE_FAMILY_BITFIELD, 2, 0),
    A64_BASE_ENTRY("lsl", A64_BASE_FAMILY_SHIFT, A64_BASE_MODIFIER_LSL, 0),
    A64_BASE_ENTRY("lsr", A64_BASE_FAMILY_SHIFT, A64_BASE_MODIFIER_LSR, 0),
    A64_BASE_ENTRY("asr", A64_BASE_FAMILY_SHIFT, A64_BASE_MODIFIER_ASR, 0),
    A64_BASE_ENTRY("ror", A64_BASE_FAMILY_SHIFT, A64_BASE_MODIFIER_ROR, 0),
    A64_BASE_ENTRY("sxtb", A64_BASE_FAMILY_EXTEND, 0, 7),
    A64_BASE_ENTRY("sxth", A64_BASE_FAMILY_EXTEND, 0, 15),
    A64_BASE_ENTRY("sxtw", A64_BASE_FAMILY_EXTEND, 0, 31),
    A64_BASE_ENTRY("uxtb", A64_BASE_FAMILY_EXTEND, 2, 7),
    A64_BASE_ENTRY("uxth", A64_BASE_FAMILY_EXTEND, 2, 15),
    A64_BASE_ENTRY("sbfx", A64_BASE_FAMILY_BITFIELD_EXTRACT, 0, 0),
    A64_BASE_ENTRY("bfxil", A64_BASE_FAMILY_BITFIELD_EXTRACT, 1, 0),
    A64_BASE_ENTRY("ubfx", A64_BASE_FAMILY_BITFIELD_EXTRACT, 2, 0),
    A64_BASE_ENTRY("sbfiz", A64_BASE_FAMILY_BITFIELD_INSERT, 0, 0),
    A64_BASE_ENTRY("bfi", A64_BASE_FAMILY_BITFIELD_INSERT, 1, 0),
    A64_BASE_ENTRY("ubfiz", A64_BASE_FAMILY_BITFIELD_INSERT, 2, 0),
    A64_BASE_ENTRY("extr", A64_BASE_FAMILY_EXTRACT, 0, 0),
    A64_BASE_ENTRY("adc", A64_BASE_FAMILY_CARRY, 0, 0),
    A64_BASE_ENTRY("adcs", A64_BASE_FAMILY_CARRY, 1, 0),
    A64_BASE_ENTRY("sbc", A64_BASE_FAMILY_CARRY, 2, 0),
    A64_BASE_ENTRY("sbcs", A64_BASE_FAMILY_CARRY, 3, 0),
    A64_BASE_ENTRY("ngc", A64_BASE_FAMILY_NEGATE_CARRY, 2, 0),
    A64_BASE_ENTRY("ngcs", A64_BASE_FAMILY_NEGATE_CARRY, 3, 0),
    A64_BASE_ENTRY("csel", A64_BASE_FAMILY_SELECT, 0, 0),
    A64_BASE_ENTRY("csinc", A64_BASE_FAMILY_SELECT, 1, 0),
    A64_BASE_ENTRY("csinv", A64_BASE_FAMILY_SELECT, 2, 0),
    A64_BASE_ENTRY("csneg", A64_BASE_FAMILY_SELECT, 3, 0),
    A64_BASE_ENTRY("cset", A64_BASE_FAMILY_SET, 1, 0),
    A64_BASE_ENTRY("csetm", A64_BASE_FAMILY_SET, 2, 0),
    A64_BASE_ENTRY("cinc", A64_BASE_FAMILY_SELECT_SAME, 1, 0),
    A64_BASE_ENTRY("cinv", A64_BASE_FAMILY_SELECT_SAME, 2, 0),
    A64_BASE_ENTRY("cneg", A64_BASE_FAMILY_SELECT_SAME, 3, 0),
    A64_BASE_ENTRY("ccmn", A64_BASE_FAMILY_COMPARE_CONDITIONAL, 0, 0),
    A64_BASE_ENTRY("ccmp", A64_BASE_FAMILY_COMPARE_CONDITIONAL, 1, 0),
    A64_BASE_ENTRY("udiv", A64_BASE_FAMILY_DATA2, 0, 2),
    A64_BASE_ENTRY("sdiv", A64_BASE_FAMILY_DATA2, 0, 3),
    A64_BASE_ENTRY("lslv", A64_BASE_FAMILY_DATA2, 0, 8),
    A64_BASE_ENTRY("lsrv", A64_BASE_FAMILY_DATA2, 0, 9),
    A64_BASE_ENTRY("asrv", A64_BASE_FAMILY_DATA2, 0, 10),
    A64_BASE_ENTRY("rorv", A64_BASE_FAMILY_DATA2, 0, 11),
    // DATA1: variant is the W-form opcode, bits the X-form opcode; 0xff
    // marks a width the spelling does not have.
    A64_BASE_ENTRY("rbit", A64_BASE_FAMILY_DATA1, 0, 0),
    A64_BASE_ENTRY("rev16", A64_BASE_FAMILY_DATA1, 1, 1),
    A64_BASE_ENTRY("rev", A64_BASE_FAMILY_DATA1, 2, 3),
    A64_BASE_ENTRY("rev32", A64_BASE_FAMILY_DATA1, 0xff, 2),
    A64_BASE_ENTRY("rev64", A64_BASE_FAMILY_DATA1, 0xff, 3),
    A64_BASE_ENTRY("clz", A64_BASE_FAMILY_DATA1, 4, 4),
    A64_BASE_ENTRY("cls", A64_BASE_FAMILY_DATA1, 5, 5),
    A64_BASE_ENTRY("madd", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE, 0x0),
    A64_BASE_ENTRY("msub", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE, 0x1),
    A64_BASE_ENTRY("smaddl", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE | A64_BASE_MULTIPLY_LONG, 0x2),
    A64_BASE_ENTRY("smsubl", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE | A64_BASE_MULTIPLY_LONG, 0x3),
    A64_BASE_ENTRY("umaddl", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE | A64_BASE_MULTIPLY_LONG, 0xa),
    A64_BASE_ENTRY("umsubl", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_ACCUMULATE | A64_BASE_MULTIPLY_LONG, 0xb),
    A64_BASE_ENTRY("smulh", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_X_ONLY, 0x4),
    A64_BASE_ENTRY("umulh", A64_BASE_FAMILY_DATA3, A64_BASE_MULTIPLY_X_ONLY, 0xc),
    A64_BASE_ENTRY("mul", A64_BASE_FAMILY_MULTIPLY, 0, 0x0),
    A64_BASE_ENTRY("mneg", A64_BASE_FAMILY_MULTIPLY, 0, 0x1),
    A64_BASE_ENTRY("smull", A64_BASE_FAMILY_MULTIPLY, A64_BASE_MULTIPLY_LONG, 0x2),
    A64_BASE_ENTRY("smnegl", A64_BASE_FAMILY_MULTIPLY, A64_BASE_MULTIPLY_LONG, 0x3),
    A64_BASE_ENTRY("umull", A64_BASE_FAMILY_MULTIPLY, A64_BASE_MULTIPLY_LONG, 0xa),
    A64_BASE_ENTRY("umnegl", A64_BASE_FAMILY_MULTIPLY, A64_BASE_MULTIPLY_LONG, 0xb),
    A64_BASE_ENTRY("ldr", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("str", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("ldrb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD, 0),
    A64_BASE_ENTRY("strb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE, 0),
    A64_BASE_ENTRY("ldrh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD, 1),
    A64_BASE_ENTRY("strh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE, 1),
    A64_BASE_ENTRY("ldrsb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED, 0),
    A64_BASE_ENTRY("ldrsh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED, 1),
    A64_BASE_ENTRY("ldrsw", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED, 2),
    A64_BASE_ENTRY("prfm", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_PREFETCH, 3),
    A64_BASE_ENTRY("ldur", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD | A64_BASE_TRANSFER_UNSCALED, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("stur", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE | A64_BASE_TRANSFER_UNSCALED, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("ldurb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD | A64_BASE_TRANSFER_UNSCALED, 0),
    A64_BASE_ENTRY("sturb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE | A64_BASE_TRANSFER_UNSCALED, 0),
    A64_BASE_ENTRY("ldurh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD | A64_BASE_TRANSFER_UNSCALED, 1),
    A64_BASE_ENTRY("sturh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_STORE | A64_BASE_TRANSFER_UNSCALED, 1),
    A64_BASE_ENTRY("ldursb", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED | A64_BASE_TRANSFER_UNSCALED, 0),
    A64_BASE_ENTRY("ldursh", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED | A64_BASE_TRANSFER_UNSCALED, 1),
    A64_BASE_ENTRY("ldursw", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_LOAD_SIGNED | A64_BASE_TRANSFER_UNSCALED, 2),
    A64_BASE_ENTRY("prfum", A64_BASE_FAMILY_LOAD_STORE, A64_BASE_TRANSFER_PREFETCH | A64_BASE_TRANSFER_UNSCALED, 3),
    A64_BASE_ENTRY("ldp", A64_BASE_FAMILY_PAIR, A64_BASE_PAIR_LOAD, 0),
    A64_BASE_ENTRY("stp", A64_BASE_FAMILY_PAIR, 0, 0),
    A64_BASE_ENTRY("ldnp", A64_BASE_FAMILY_PAIR, A64_BASE_PAIR_LOAD | A64_BASE_PAIR_NO_ALLOCATE, 0),
    A64_BASE_ENTRY("stnp", A64_BASE_FAMILY_PAIR, A64_BASE_PAIR_NO_ALLOCATE, 0),
    A64_BASE_ENTRY("ldpsw", A64_BASE_FAMILY_PAIR, A64_BASE_PAIR_LOAD | A64_BASE_PAIR_SIGNED_WORD, 0),
    A64_BASE_ENTRY("ldxr", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("ldxrb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD, 0),
    A64_BASE_ENTRY("ldxrh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD, 1),
    A64_BASE_ENTRY("ldaxr", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("ldaxrb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 0),
    A64_BASE_ENTRY("ldaxrh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 1),
    A64_BASE_ENTRY("stxr", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("stxrb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS, 0),
    A64_BASE_ENTRY("stxrh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS, 1),
    A64_BASE_ENTRY("stlxr", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("stlxrb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 0),
    A64_BASE_ENTRY("stlxrh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_STATUS | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 1),
    A64_BASE_ENTRY("ldar", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("ldarb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 0),
    A64_BASE_ENTRY("ldarh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_LOAD | A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 1),
    A64_BASE_ENTRY("stlr", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, A64_BASE_SIZE_FROM_REGISTER),
    A64_BASE_ENTRY("stlrb", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 0),
    A64_BASE_ENTRY("stlrh", A64_BASE_FAMILY_EXCLUSIVE, A64_BASE_EXCLUSIVE_ORDERED | A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE, 1),
    A64_BASE_ENTRY("fmov", A64_BASE_FAMILY_FP_MOVE, 0, 0),
    A64_BASE_ENTRY("fmul", A64_BASE_FAMILY_FP_DATA2, 0, 0),
    A64_BASE_ENTRY("fdiv", A64_BASE_FAMILY_FP_DATA2, 1, 0),
    A64_BASE_ENTRY("fadd", A64_BASE_FAMILY_FP_DATA2, 2, 0),
    A64_BASE_ENTRY("fsub", A64_BASE_FAMILY_FP_DATA2, 3, 0),
    A64_BASE_ENTRY("fmax", A64_BASE_FAMILY_FP_DATA2, 4, 0),
    A64_BASE_ENTRY("fmin", A64_BASE_FAMILY_FP_DATA2, 5, 0),
    A64_BASE_ENTRY("fmaxnm", A64_BASE_FAMILY_FP_DATA2, 6, 0),
    A64_BASE_ENTRY("fminnm", A64_BASE_FAMILY_FP_DATA2, 7, 0),
    A64_BASE_ENTRY("fnmul", A64_BASE_FAMILY_FP_DATA2, 8, 0),
    A64_BASE_ENTRY("fabs", A64_BASE_FAMILY_FP_DATA1, 1, 0),
    A64_BASE_ENTRY("fneg", A64_BASE_FAMILY_FP_DATA1, 2, 0),
    A64_BASE_ENTRY("fsqrt", A64_BASE_FAMILY_FP_DATA1, 3, 0),
    A64_BASE_ENTRY("frintn", A64_BASE_FAMILY_FP_DATA1, 8, 0),
    A64_BASE_ENTRY("frintp", A64_BASE_FAMILY_FP_DATA1, 9, 0),
    A64_BASE_ENTRY("frintm", A64_BASE_FAMILY_FP_DATA1, 10, 0),
    A64_BASE_ENTRY("frintz", A64_BASE_FAMILY_FP_DATA1, 11, 0),
    A64_BASE_ENTRY("frinta", A64_BASE_FAMILY_FP_DATA1, 12, 0),
    A64_BASE_ENTRY("frintx", A64_BASE_FAMILY_FP_DATA1, 14, 0),
    A64_BASE_ENTRY("frinti", A64_BASE_FAMILY_FP_DATA1, 15, 0),
    A64_BASE_ENTRY("fcvt", A64_BASE_FAMILY_FP_CONVERT_PRECISION, 0, 0),
    A64_BASE_ENTRY("fmadd", A64_BASE_FAMILY_FP_DATA3, 0, 0),
    A64_BASE_ENTRY("fmsub", A64_BASE_FAMILY_FP_DATA3, 1, 0),
    A64_BASE_ENTRY("fnmadd", A64_BASE_FAMILY_FP_DATA3, 2, 0),
    A64_BASE_ENTRY("fnmsub", A64_BASE_FAMILY_FP_DATA3, 3, 0),
    A64_BASE_ENTRY("fcmp", A64_BASE_FAMILY_FP_COMPARE, 0, 0),
    A64_BASE_ENTRY("fcmpe", A64_BASE_FAMILY_FP_COMPARE, 1, 0),
    A64_BASE_ENTRY("fccmp", A64_BASE_FAMILY_FP_COMPARE_CONDITIONAL, 0, 0),
    A64_BASE_ENTRY("fccmpe", A64_BASE_FAMILY_FP_COMPARE_CONDITIONAL, 1, 0),
    A64_BASE_ENTRY("fcsel", A64_BASE_FAMILY_FP_SELECT, 0, 0),
    // FP/integer conversions: variant is rmode:opcode of the general-register
    // form; bits is the AdvSIMD scalar same-size form (sz clear).
    A64_BASE_ENTRY("fcvtns", A64_BASE_FAMILY_FP_TO_INTEGER, 0x00, 0x5e21a800),
    A64_BASE_ENTRY("fcvtnu", A64_BASE_FAMILY_FP_TO_INTEGER, 0x01, 0x7e21a800),
    A64_BASE_ENTRY("fcvtas", A64_BASE_FAMILY_FP_TO_INTEGER, 0x04, 0x5e21c800),
    A64_BASE_ENTRY("fcvtau", A64_BASE_FAMILY_FP_TO_INTEGER, 0x05, 0x7e21c800),
    A64_BASE_ENTRY("fcvtps", A64_BASE_FAMILY_FP_TO_INTEGER, 0x08, 0x5ea1a800),
    A64_BASE_ENTRY("fcvtpu", A64_BASE_FAMILY_FP_TO_INTEGER, 0x09, 0x7ea1a800),
    A64_BASE_ENTRY("fcvtms", A64_BASE_FAMILY_FP_TO_INTEGER, 0x10, 0x5e21b800),
    A64_BASE_ENTRY("fcvtmu", A64_BASE_FAMILY_FP_TO_INTEGER, 0x11, 0x7e21b800),
    A64_BASE_ENTRY("fcvtzs", A64_BASE_FAMILY_FP_TO_INTEGER, 0x18, 0x5ea1b800),
    A64_BASE_ENTRY("fcvtzu", A64_BASE_FAMILY_FP_TO_INTEGER, 0x19, 0x7ea1b800),
    A64_BASE_ENTRY("scvtf", A64_BASE_FAMILY_FP_FROM_INTEGER, 0x02, 0x5e21d800),
    A64_BASE_ENTRY("ucvtf", A64_BASE_FAMILY_FP_FROM_INTEGER, 0x03, 0x7e21d800),
    A64_BASE_ENTRY("umov", A64_BASE_FAMILY_ELEMENT_UNSIGNED_MOVE, 0, 0),
    A64_BASE_ENTRY("smov", A64_BASE_FAMILY_ELEMENT_SIGNED_MOVE, 0, 0),
    A64_BASE_ENTRY("ins", A64_BASE_FAMILY_ELEMENT_INSERT, 0, 0),
    A64_BASE_ENTRY("dup", A64_BASE_FAMILY_ELEMENT_DUPLICATE, 0, 0),
    // Barriers: variant is op2; bits marks a required option operand.
    A64_BASE_ENTRY("dsb", A64_BASE_FAMILY_SYSTEM_BARRIER, 4, 1),
    A64_BASE_ENTRY("dmb", A64_BASE_FAMILY_SYSTEM_BARRIER, 5, 1),
    A64_BASE_ENTRY("isb", A64_BASE_FAMILY_SYSTEM_BARRIER, 6, 0),
    A64_BASE_ENTRY("clrex", A64_BASE_FAMILY_SYSTEM_BARRIER, 2, 0),
    // Exception generation: bits is the fixed word, variant the imm16 shift.
    A64_BASE_ENTRY("svc", A64_BASE_FAMILY_EXCEPTION, 5, 0xd4000001),
    A64_BASE_ENTRY("hvc", A64_BASE_FAMILY_EXCEPTION, 5, 0xd4000002),
    A64_BASE_ENTRY("smc", A64_BASE_FAMILY_EXCEPTION, 5, 0xd4000003),
    A64_BASE_ENTRY("brk", A64_BASE_FAMILY_EXCEPTION, 5, 0xd4200000),
    A64_BASE_ENTRY("hlt", A64_BASE_FAMILY_EXCEPTION, 5, 0xd4400000),
    A64_BASE_ENTRY("udf", A64_BASE_FAMILY_EXCEPTION, 0, 0x00000000),
    A64_BASE_ENTRY("mrs", A64_BASE_FAMILY_SYSTEM_MOVE, 1, 0),
    A64_BASE_ENTRY("msr", A64_BASE_FAMILY_SYSTEM_MOVE, 0, 0),
    // AdvSIMD forms compilers emit for vectorized and scalar code. Spellings
    // shared with scalar families appear twice; the first encodable entry wins.
    A64_BASE_ENTRY("movi", A64_BASE_FAMILY_VECTOR_IMMEDIATE, 0, 0),
    A64_BASE_ENTRY("mvni", A64_BASE_FAMILY_VECTOR_IMMEDIATE, 1, 0),
    A64_BASE_ENTRY("orr", A64_BASE_FAMILY_VECTOR_LOGICAL_IMMEDIATE, 0, 0),
    A64_BASE_ENTRY("bic", A64_BASE_FAMILY_VECTOR_LOGICAL_IMMEDIATE, 1, 0),
    A64_BASE_ENTRY("fmov", A64_BASE_FAMILY_VECTOR_FP_IMMEDIATE, 0, 0),
    A64_BASE_ENTRY("mvn", A64_BASE_FAMILY_VECTOR_NOT, 0, 0),
    A64_BASE_ENTRY("not", A64_BASE_FAMILY_VECTOR_NOT, 0, 0),
    A64_BASE_ENTRY("ext", A64_BASE_FAMILY_VECTOR_EXTRACT, 0, 0),
    A64_BASE_ENTRY("xtn", A64_BASE_FAMILY_VECTOR_NARROW, 0, 0x0e212800),
    A64_BASE_ENTRY("xtn2", A64_BASE_FAMILY_VECTOR_NARROW, A64_BASE_VECTOR_UPPER, 0x0e212800),
    A64_BASE_ENTRY("shl", A64_BASE_FAMILY_VECTOR_SHIFT, 0, 0x0f005400),
    A64_BASE_ENTRY("sshr", A64_BASE_FAMILY_VECTOR_SHIFT, A64_BASE_VECTOR_RIGHT, 0x0f000400),
    A64_BASE_ENTRY("ushr", A64_BASE_FAMILY_VECTOR_SHIFT, A64_BASE_VECTOR_RIGHT, 0x2f000400),
    A64_BASE_ENTRY("ssra", A64_BASE_FAMILY_VECTOR_SHIFT, A64_BASE_VECTOR_RIGHT, 0x0f001400),
    A64_BASE_ENTRY("usra", A64_BASE_FAMILY_VECTOR_SHIFT, A64_BASE_VECTOR_RIGHT, 0x2f001400),
    A64_BASE_ENTRY("sshll", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, 0, 0x0f00a400),
    A64_BASE_ENTRY("sshll2", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_UPPER, 0x0f00a400),
    A64_BASE_ENTRY("ushll", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, 0, 0x2f00a400),
    A64_BASE_ENTRY("ushll2", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_UPPER, 0x2f00a400),
    A64_BASE_ENTRY("sxtl", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_ALIAS, 0x0f00a400),
    A64_BASE_ENTRY("sxtl2", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_ALIAS | A64_BASE_VECTOR_UPPER, 0x0f00a400),
    A64_BASE_ENTRY("uxtl", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_ALIAS, 0x2f00a400),
    A64_BASE_ENTRY("uxtl2", A64_BASE_FAMILY_VECTOR_SHIFT_LONG, A64_BASE_VECTOR_ALIAS | A64_BASE_VECTOR_UPPER, 0x2f00a400),
    A64_BASE_ENTRY("saddl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x0e200000),
    A64_BASE_ENTRY("saddl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x0e200000),
    A64_BASE_ENTRY("uaddl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x2e200000),
    A64_BASE_ENTRY("uaddl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x2e200000),
    A64_BASE_ENTRY("ssubl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x0e202000),
    A64_BASE_ENTRY("ssubl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x0e202000),
    A64_BASE_ENTRY("usubl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x2e202000),
    A64_BASE_ENTRY("usubl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x2e202000),
    A64_BASE_ENTRY("smlal", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x0e208000),
    A64_BASE_ENTRY("smlal2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x0e208000),
    A64_BASE_ENTRY("umlal", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x2e208000),
    A64_BASE_ENTRY("umlal2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x2e208000),
    A64_BASE_ENTRY("smlsl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x0e20a000),
    A64_BASE_ENTRY("smlsl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x0e20a000),
    A64_BASE_ENTRY("umlsl", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x2e20a000),
    A64_BASE_ENTRY("umlsl2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x2e20a000),
    A64_BASE_ENTRY("smull", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x0e20c000),
    A64_BASE_ENTRY("smull2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x0e20c000),
    A64_BASE_ENTRY("umull", A64_BASE_FAMILY_VECTOR_LONG, 0, 0x2e20c000),
    A64_BASE_ENTRY("umull2", A64_BASE_FAMILY_VECTOR_LONG, A64_BASE_VECTOR_UPPER, 0x2e20c000),
    A64_BASE_ENTRY("cmgt", A64_BASE_FAMILY_VECTOR_COMPARE_ZERO, 0, 0x0e208800),
    A64_BASE_ENTRY("cmeq", A64_BASE_FAMILY_VECTOR_COMPARE_ZERO, 0, 0x0e209800),
    A64_BASE_ENTRY("cmlt", A64_BASE_FAMILY_VECTOR_COMPARE_ZERO, 0, 0x0e20a800),
    A64_BASE_ENTRY("cmge", A64_BASE_FAMILY_VECTOR_COMPARE_ZERO, 0, 0x2e208800),
    A64_BASE_ENTRY("cmle", A64_BASE_FAMILY_VECTOR_COMPARE_ZERO, 0, 0x2e209800),
    A64_BASE_ENTRY("ld1", A64_BASE_FAMILY_VECTOR_LANE, 1, 0),
    A64_BASE_ENTRY("st1", A64_BASE_FAMILY_VECTOR_LANE, 0, 0),
};

BUSTER_GLOBAL_LOCAL char8 a64_base_lower(char8 value)
{
    return value >= 'A' && value <= 'Z' ? (char8)(value + ('a' - 'A')) : value;
}

BUSTER_GLOBAL_LOCAL bool a64_base_space(char8 value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

BUSTER_GLOBAL_LOCAL String8 a64_base_trim(String8 text)
{
    u64 start = 0;
    u64 end = text.length;
    while (start < end && a64_base_space(text.pointer[start]))
    {
        start += 1;
    }
    while (end > start && a64_base_space(text.pointer[end - 1]))
    {
        end -= 1;
    }
    return (String8){.pointer = text.pointer + start, .length = end - start};
}

// Case-insensitive comparison against a lower-case spelling.
BUSTER_GLOBAL_LOCAL bool a64_base_equal(String8 text, String8 lower)
{
    bool equal = text.length == lower.length;
    for (u64 index = 0; equal && index < text.length; index += 1)
    {
        equal = a64_base_lower(text.pointer[index]) == lower.pointer[index];
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL bool a64_base_digit(char8 value)
{
    return value >= '0' && value <= '9';
}

// Parse a decimal register number 0..31 occupying the whole of `text`.
BUSTER_GLOBAL_LOCAL bool a64_base_register_number(String8 text, u8* number)
{
    bool valid = text.length >= 1 && text.length <= 2 && (text.length == 1 || text.pointer[0] != '0');
    u32 value = 0;
    for (u64 index = 0; valid && index < text.length; index += 1)
    {
        valid = a64_base_digit(text.pointer[index]);
        value = value * 10u + (u32)(text.pointer[index] - '0');
    }
    valid = valid && value <= 31;
    if (valid)
    {
        *number = (u8)value;
    }
    return valid;
}

// `[#][+|-]` then decimal, 0x hexadecimal or 0b binary. A multi-digit
// decimal with a leading zero is refused: GNU syntax reads it as octal.
BUSTER_GLOBAL_LOCAL bool a64_base_integer_parse(String8 text, u64* magnitude, bool* negative)
{
    text = a64_base_trim(text);
    if (text.length && text.pointer[0] == '#')
    {
        text = a64_base_trim(string_slice(text, 1, text.length));
    }
    bool sign = false;
    if (text.length && (text.pointer[0] == '-' || text.pointer[0] == '+'))
    {
        sign = text.pointer[0] == '-';
        text = string_slice(text, 1, text.length);
    }
    u32 base = 10;
    if (text.length > 2 && text.pointer[0] == '0' && a64_base_lower(text.pointer[1]) == 'x')
    {
        base = 16;
        text = string_slice(text, 2, text.length);
    }
    else if (text.length > 2 && text.pointer[0] == '0' && a64_base_lower(text.pointer[1]) == 'b')
    {
        base = 2;
        text = string_slice(text, 2, text.length);
    }
    // LLVM prints a zero 64-bit MOVI immediate as sixteen zero digits.
    bool all_zero = true;
    for (u64 index = 0; index < text.length; index += 1)
    {
        all_zero = all_zero && text.pointer[index] == '0';
    }
    bool valid = text.length != 0 && !(base == 10 && text.length > 1 && text.pointer[0] == '0' && !all_zero);
    u64 value = 0;
    for (u64 index = 0; valid && index < text.length; index += 1)
    {
        char8 character = a64_base_lower(text.pointer[index]);
        u32 digit = a64_base_digit(character) ? (u32)(character - '0')
                  : character >= 'a' && character <= 'f' ? (u32)(character - 'a' + 10)
                                                         : base;
        valid = digit < base && value <= (UINT64_MAX - digit) / base;
        value = value * base + digit;
    }
    if (valid)
    {
        *magnitude = value;
        *negative = sign && value != 0;
    }
    return valid;
}

// Decimal float `[#][+|-]digits[.digits][e[+|-]digits]` as an exact
// significand and power-of-ten exponent. Trailing zeros are folded into the
// exponent so equal values compare equal.
BUSTER_GLOBAL_LOCAL bool a64_base_float_parse(String8 text, u64* digits, s32* exponent, bool* negative)
{
    text = a64_base_trim(text);
    if (text.length && text.pointer[0] == '#')
    {
        text = a64_base_trim(string_slice(text, 1, text.length));
    }
    bool sign = false;
    if (text.length && (text.pointer[0] == '-' || text.pointer[0] == '+'))
    {
        sign = text.pointer[0] == '-';
        text = string_slice(text, 1, text.length);
    }
    u64 significand = 0;
    s32 power = 0;
    u32 digit_count = 0;
    bool valid = true;
    bool fraction = false;
    u64 index = 0;
    while (valid && index < text.length && (a64_base_digit(text.pointer[index]) || (text.pointer[index] == '.' && !fraction)))
    {
        if (text.pointer[index] == '.')
        {
            fraction = true;
        }
        else
        {
            u64 digit = (u64)(text.pointer[index] - '0');
            if (significand <= (UINT64_MAX - 9u) / 10u)
            {
                significand = significand * 10u + digit;
                power -= fraction;
            }
            else
            {
                // Beyond u64 precision only zeros keep the value exact.
                valid = digit == 0;
                power += !fraction;
            }
            digit_count += 1;
        }
        index += 1;
    }
    valid = valid && digit_count != 0;
    if (valid && index < text.length && a64_base_lower(text.pointer[index]) == 'e')
    {
        index += 1;
        bool exponent_negative = false;
        if (index < text.length && (text.pointer[index] == '-' || text.pointer[index] == '+'))
        {
            exponent_negative = text.pointer[index] == '-';
            index += 1;
        }
        s32 written = 0;
        u32 exponent_digits = 0;
        while (valid && index < text.length && a64_base_digit(text.pointer[index]))
        {
            written = written * 10 + (s32)(text.pointer[index] - '0');
            exponent_digits += 1;
            valid = written < 10000;
            index += 1;
        }
        valid = valid && exponent_digits != 0;
        power += exponent_negative ? -written : written;
    }
    valid = valid && index == text.length;
    while (valid && significand != 0 && significand % 10u == 0)
    {
        significand /= 10u;
        power += 1;
    }
    if (valid)
    {
        *digits = significand;
        *exponent = significand ? power : 0;
        *negative = sign;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_condition_parse(String8 text, u8* condition)
{
    static const String8 names[] = {
        S8_INITIALIZER("eq"), S8_INITIALIZER("ne"), S8_INITIALIZER("cs"), S8_INITIALIZER("cc"),
        S8_INITIALIZER("mi"), S8_INITIALIZER("pl"), S8_INITIALIZER("vs"), S8_INITIALIZER("vc"),
        S8_INITIALIZER("hi"), S8_INITIALIZER("ls"), S8_INITIALIZER("ge"), S8_INITIALIZER("lt"),
        S8_INITIALIZER("gt"), S8_INITIALIZER("le"), S8_INITIALIZER("al"), S8_INITIALIZER("nv"),
    };
    bool found = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names) && !found; index += 1)
    {
        found = a64_base_equal(text, names[index]);
        if (found)
        {
            *condition = (u8)index;
        }
    }
    if (!found && (a64_base_equal(text, S8("hs")) || a64_base_equal(text, S8("lo"))))
    {
        *condition = a64_base_equal(text, S8("hs")) ? 2 : 3;
        found = true;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool a64_base_modifier_name(String8 text, u8* modifier)
{
    static const String8 names[A64_BASE_MODIFIER_COUNT] = {
        S8_INITIALIZER("lsl"),  S8_INITIALIZER("lsr"),  S8_INITIALIZER("asr"),  S8_INITIALIZER("ror"),
        S8_INITIALIZER("uxtb"), S8_INITIALIZER("uxth"), S8_INITIALIZER("uxtw"), S8_INITIALIZER("uxtx"),
        S8_INITIALIZER("sxtb"), S8_INITIALIZER("sxth"), S8_INITIALIZER("sxtw"), S8_INITIALIZER("sxtx"),
        S8_INITIALIZER("msl"),
    };
    bool found = false;
    for (u32 index = 0; index < A64_BASE_MODIFIER_COUNT && !found; index += 1)
    {
        found = a64_base_equal(text, names[index]);
        if (found)
        {
            *modifier = (u8)index;
        }
    }
    return found;
}

// `lsl #3`, `sxtw`, `uxtw #2`: shifts need an amount, extends do not.
BUSTER_GLOBAL_LOCAL bool a64_base_modifier_parse(String8 text, A64BaseOperand* operand)
{
    u64 name_end = 0;
    while (name_end < text.length && !a64_base_space(text.pointer[name_end]) && text.pointer[name_end] != '#')
    {
        name_end += 1;
    }
    u8 modifier = 0;
    bool valid = a64_base_modifier_name(string_slice(text, 0, name_end), &modifier);
    String8 amount_text = a64_base_trim(string_slice(text, name_end, text.length));
    u64 amount = 0;
    bool negative = false;
    bool amount_present = amount_text.length != 0;
    if (valid && amount_present)
    {
        valid = amount_text.pointer[0] == '#' && a64_base_integer_parse(amount_text, &amount, &negative) && !negative && amount <= 63;
    }
    valid = valid && (amount_present || (modifier >= A64_BASE_MODIFIER_UXTB && modifier <= A64_BASE_MODIFIER_SXTX));
    if (valid)
    {
        operand->kind = A64_BASE_OPERAND_MODIFIER;
        operand->modifier = modifier;
        operand->amount = (u8)amount;
        operand->amount_present = amount_present;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_gpr_parse(String8 text, A64BaseRegister* reg)
{
    bool valid = true;
    if (a64_base_equal(text, S8("sp")) || a64_base_equal(text, S8("wsp")))
    {
        *reg = (A64BaseRegister){.bits = text.length == 2 ? 64 : 32, .number = 31, .sp = true};
    }
    else if (a64_base_equal(text, S8("xzr")) || a64_base_equal(text, S8("wzr")))
    {
        *reg = (A64BaseRegister){.bits = a64_base_lower(text.pointer[0]) == 'x' ? 64 : 32, .number = 31};
    }
    else if (a64_base_equal(text, S8("fp")) || a64_base_equal(text, S8("lr")))
    {
        *reg = (A64BaseRegister){.bits = 64, .number = a64_base_lower(text.pointer[0]) == 'f' ? 29 : 30};
    }
    else
    {
        u8 number = 0;
        char8 prefix = text.length ? a64_base_lower(text.pointer[0]) : 0;
        valid = (prefix == 'w' || prefix == 'x') && a64_base_register_number(string_slice(text, 1, text.length), &number) && number < 31;
        if (valid)
        {
            *reg = (A64BaseRegister){.bits = prefix == 'x' ? 64 : 32, .number = number};
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u8 a64_base_element_bits(char8 suffix)
{
    char8 lower = a64_base_lower(suffix);
    return lower == 'b' ? 8 : lower == 'h' ? 16 : lower == 's' ? 32 : lower == 'd' ? 64 : lower == 'q' ? 128 : 0;
}

// FPR (`s3`), vector (`v3.4s`) or element (`v3.s[1]`).
BUSTER_GLOBAL_LOCAL bool a64_base_simd_parse(String8 text, A64BaseOperand* operand)
{
    char8 prefix = text.length ? a64_base_lower(text.pointer[0]) : 0;
    u64 dot = 1;
    while (dot < text.length && text.pointer[dot] != '.')
    {
        dot += 1;
    }
    u8 number = 0;
    bool valid = text.length >= 2 && a64_base_register_number(string_slice(text, 1, dot), &number);
    if (valid && prefix != 'v')
    {
        u8 bits = a64_base_element_bits(prefix);
        valid = bits != 0 && dot == text.length;
        if (valid)
        {
            operand->kind = A64_BASE_OPERAND_FPR;
            operand->reg = (A64BaseRegister){.bits = bits, .number = number};
        }
    }
    else if (valid)
    {
        String8 suffix = string_slice(text, dot + 1 < text.length ? dot + 1 : text.length, text.length);
        valid = dot < text.length && suffix.length != 0;
        u64 bracket = 0;
        while (valid && bracket < suffix.length && suffix.pointer[bracket] != '[')
        {
            bracket += 1;
        }
        if (valid && bracket < suffix.length)
        {
            u64 index = 0;
            bool negative = false;
            valid = bracket == 1 && suffix.pointer[suffix.length - 1] == ']' &&
                    a64_base_integer_parse(string_slice(suffix, 2, suffix.length - 1), &index, &negative) && !negative;
            u8 bits = valid ? a64_base_element_bits(suffix.pointer[0]) : 0;
            valid = valid && bits != 0 && bits != 128 && index < 128u / bits;
            if (valid)
            {
                operand->kind = A64_BASE_OPERAND_ELEMENT;
                operand->reg = (A64BaseRegister){.bits = bits, .number = number, .index = (u8)index};
            }
        }
        else if (valid)
        {
            u64 lanes = 0;
            bool negative = false;
            valid = suffix.length >= 2 && a64_base_integer_parse(string_slice(suffix, 0, suffix.length - 1), &lanes, &negative);
            u8 bits = valid ? a64_base_element_bits(suffix.pointer[suffix.length - 1]) : 0;
            valid = valid && bits != 0 && bits != 128 && (lanes * bits == 64 || lanes * bits == 128);
            if (valid)
            {
                operand->kind = A64_BASE_OPERAND_VECTOR;
                operand->reg = (A64BaseRegister){.bits = bits, .number = number, .lanes = (u8)lanes};
            }
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_prefetch_parse(String8 text, A64BaseOperand* operand)
{
    // PRFM <prfop>: P{LD,LI,ST} L{1,2,3} {KEEP,STRM} -> type:target:policy.
    bool valid = text.length == 9 && a64_base_lower(text.pointer[0]) == 'p' && a64_base_lower(text.pointer[3]) == 'l';
    u32 type = 0;
    if (valid)
    {
        String8 kind = string_slice(text, 1, 3);
        type = a64_base_equal(kind, S8("ld")) ? 0 : a64_base_equal(kind, S8("li")) ? 1 : a64_base_equal(kind, S8("st")) ? 2 : 3;
        valid = type != 3 && text.pointer[4] >= '1' && text.pointer[4] <= '3';
    }
    String8 policy = valid ? string_slice(text, 5, 9) : (String8){0};
    valid = valid && (a64_base_equal(policy, S8("keep")) || a64_base_equal(policy, S8("strm")));
    if (valid)
    {
        operand->kind = A64_BASE_OPERAND_PREFETCH;
        operand->magnitude = (type << 3) | ((u32)(text.pointer[4] - '1') << 1) | (u32)a64_base_equal(policy, S8("strm"));
    }
    return valid;
}

// `[Xn|SP]`, `[Xn, #imm]`, `[Xn, #imm]!`, `[Xn, Rm{, extend|lsl #s}]`.
BUSTER_GLOBAL_LOCAL bool a64_base_memory_parse(String8 text, A64BaseOperand* operand)
{
    bool writeback = text.length && text.pointer[text.length - 1] == '!';
    if (writeback)
    {
        text = a64_base_trim(string_slice(text, 0, text.length - 1));
    }
    bool valid = text.length >= 2 && text.pointer[0] == '[' && text.pointer[text.length - 1] == ']';
    String8 contents = valid ? a64_base_trim(string_slice(text, 1, text.length - 1)) : (String8){0};
    String8 parts[3] = {0};
    u32 part_count = 0;
    u64 start = 0;
    for (u64 index = 0; valid && index <= contents.length; index += 1)
    {
        if (index == contents.length || contents.pointer[index] == ',')
        {
            valid = part_count < BUSTER_ARRAY_LENGTH(parts);
            if (valid)
            {
                parts[part_count] = a64_base_trim(string_slice(contents, start, index));
                valid = parts[part_count].length != 0;
                part_count += 1;
                start = index + 1;
            }
        }
    }
    A64BaseRegister base = {0};
    valid = valid && a64_base_gpr_parse(parts[0], &base) && base.bits == 64;
    A64BaseOperand result = {.kind = A64_BASE_OPERAND_MEMORY, .reg = base, .address = A64_BASE_ADDRESS_BASE, .writeback = writeback};
    if (valid && part_count >= 2 && parts[1].pointer[0] == '#')
    {
        result.address = A64_BASE_ADDRESS_IMMEDIATE;
        valid = part_count == 2 && a64_base_integer_parse(parts[1], &result.magnitude, &result.negative);
    }
    else if (valid && part_count >= 2)
    {
        result.address = A64_BASE_ADDRESS_REGISTER;
        valid = !writeback && a64_base_gpr_parse(parts[1], &result.index) && !result.index.sp;
        if (valid && part_count == 3)
        {
            A64BaseOperand modifier = {0};
            valid = a64_base_modifier_parse(parts[2], &modifier);
            result.modifier = modifier.modifier;
            result.amount = modifier.amount;
            result.amount_present = modifier.amount_present;
            result.modifier_present = true;
        }
    }
    valid = valid && (!writeback || result.address == A64_BASE_ADDRESS_IMMEDIATE);
    if (valid)
    {
        *operand = result;
    }
    return valid;
}

// `{ Vt.T{, Vt2.T ...} }` (consecutive registers, one arrangement) or
// `{ Vt.Ts ... }[index]` with element-only arrangements.
BUSTER_GLOBAL_LOCAL bool a64_base_list_parse(String8 text, A64BaseOperand* operand)
{
    u64 close = 0;
    while (close < text.length && text.pointer[close] != '}')
    {
        close += 1;
    }
    bool valid = close < text.length;
    String8 contents = valid ? a64_base_trim(string_slice(text, 1, close)) : (String8){0};
    String8 suffix = valid ? a64_base_trim(string_slice(text, close + 1, text.length)) : (String8){0};
    A64BaseRegister first = {0};
    u32 count = 0;
    u64 start = 0;
    for (u64 index = 0; valid && index <= contents.length; index += 1)
    {
        if (index == contents.length || contents.pointer[index] == ',')
        {
            String8 member = a64_base_trim(string_slice(contents, start, index));
            u64 dot = 1;
            while (dot < member.length && member.pointer[dot] != '.')
            {
                dot += 1;
            }
            u8 number = 0;
            valid = member.length > 3 && a64_base_lower(member.pointer[0]) == 'v' && dot + 1 < member.length &&
                    a64_base_register_number(string_slice(member, 1, dot), &number);
            String8 arrangement = valid ? string_slice(member, dot + 1, member.length) : (String8){0};
            u8 bits = valid ? a64_base_element_bits(arrangement.pointer[arrangement.length - 1]) : 0;
            u64 lanes = 0;
            bool negative = false;
            valid = valid && bits != 0 && bits != 128 &&
                    (arrangement.length == 1 || (a64_base_integer_parse(string_slice(arrangement, 0, arrangement.length - 1), &lanes, &negative) &&
                                                 (lanes * bits == 64 || lanes * bits == 128)));
            if (valid && count == 0)
            {
                first = (A64BaseRegister){.bits = bits, .number = number, .lanes = (u8)lanes};
            }
            valid = valid && count < 4 && bits == first.bits && lanes == first.lanes && number == (first.number + count) % 32u;
            count += 1;
            start = index + 1;
        }
    }
    u64 lane = 0;
    if (valid && suffix.length)
    {
        bool negative = false;
        valid = first.lanes == 0 && suffix.length >= 3 && suffix.pointer[0] == '[' && suffix.pointer[suffix.length - 1] == ']' &&
                a64_base_integer_parse(string_slice(suffix, 1, suffix.length - 1), &lane, &negative) && !negative && lane < 128u / first.bits;
    }
    else if (valid)
    {
        valid = first.lanes != 0;
    }
    if (valid)
    {
        operand->kind = A64_BASE_OPERAND_LIST;
        operand->reg = first;
        operand->reg.count = (u8)count;
        operand->reg.index = (u8)lane;
        operand->reg.index_present = suffix.length != 0;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_operand_parse(String8 text, A64BaseOperand* operand)
{
    text = a64_base_trim(text);
    *operand = (A64BaseOperand){0};
    bool valid = text.length != 0;
    char8 first = valid ? text.pointer[0] : 0;
    if (valid && first == '[')
    {
        valid = a64_base_memory_parse(text, operand);
    }
    else if (valid && first == '{')
    {
        valid = a64_base_list_parse(text, operand);
    }
    else if (valid && (first == '#' || first == '-' || first == '+' || a64_base_digit(first)))
    {
        String8 body = text;
        if (body.pointer[0] == '#')
        {
            body = a64_base_trim(string_slice(body, 1, body.length));
        }
        if (body.length && (body.pointer[0] == '-' || body.pointer[0] == '+'))
        {
            body = string_slice(body, 1, body.length);
        }
        bool hexadecimal = body.length > 1 && body.pointer[0] == '0' && (a64_base_lower(body.pointer[1]) == 'x' || a64_base_lower(body.pointer[1]) == 'b');
        bool decimal_float = false;
        for (u64 index = 0; !hexadecimal && index < body.length; index += 1)
        {
            decimal_float = decimal_float || body.pointer[index] == '.' || a64_base_lower(body.pointer[index]) == 'e';
        }
        if (decimal_float)
        {
            operand->kind = A64_BASE_OPERAND_FLOAT;
            valid = a64_base_float_parse(text, &operand->magnitude, &operand->exponent, &operand->negative);
        }
        else
        {
            operand->kind = A64_BASE_OPERAND_IMMEDIATE;
            valid = a64_base_integer_parse(text, &operand->magnitude, &operand->negative);
        }
    }
    else if (valid && a64_base_gpr_parse(text, &operand->reg))
    {
        operand->kind = A64_BASE_OPERAND_GPR;
    }
    else if (valid && a64_base_condition_parse(text, &operand->modifier))
    {
        operand->kind = A64_BASE_OPERAND_CONDITION;
    }
    else if (valid && a64_base_modifier_parse(text, operand))
    {
    }
    else if (valid && a64_base_simd_parse(text, operand))
    {
    }
    else if (valid && a64_base_prefetch_parse(text, operand))
    {
    }
    else if (valid)
    {
        // Barrier options and system registers are resolved by their family.
        for (u64 index = 0; valid && index < text.length; index += 1)
        {
            char8 character = a64_base_lower(text.pointer[index]);
            valid = a64_base_digit(character) || (character >= 'a' && character <= 'z') || character == '_';
        }
        operand->kind = A64_BASE_OPERAND_NAME;
        operand->name = text;
    }
    return valid;
}

// Split at commas outside `[...]`, so post-index `[x0], #16` yields two
// operands and `[x0, x1, lsl #3]` one.
BUSTER_GLOBAL_LOCAL bool a64_base_operands_split(String8 text, A64BaseOperand* operands, u32* count)
{
    text = a64_base_trim(text);
    u32 operand_count = 0;
    u32 depth = 0;
    u64 start = 0;
    bool valid = true;
    for (u64 index = 0; valid && text.length && index <= text.length; index += 1)
    {
        char8 character = index < text.length ? text.pointer[index] : ',';
        if (character == '[' || character == '{')
        {
            depth += 1;
        }
        else if (character == ']' || character == '}')
        {
            valid = depth != 0;
            depth -= valid;
        }
        else if (character == ',' && depth == 0)
        {
            valid = operand_count < A64_BASE_MAX_OPERANDS && a64_base_operand_parse(string_slice(text, start, index), operands + operand_count);
            operand_count += 1;
            start = index + 1;
        }
    }
    valid = valid && depth == 0;
    *count = operand_count;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_is_gpr(A64BaseOperand const* operand, u8 bits)
{
    return operand->kind == A64_BASE_OPERAND_GPR && operand->reg.bits == bits;
}

// A GPR of `bits` whose register-31 spelling matches the field's role.
BUSTER_GLOBAL_LOCAL bool a64_base_gpr_role(A64BaseOperand const* operand, u8 bits, bool stack_pointer)
{
    return a64_base_is_gpr(operand, bits) && (operand->reg.number != 31 || operand->reg.sp == stack_pointer);
}

BUSTER_GLOBAL_LOCAL bool a64_base_gpr_zr(A64BaseOperand const* operand, u8 bits)
{
    return a64_base_gpr_role(operand, bits, false);
}

BUSTER_GLOBAL_LOCAL bool a64_base_gpr_sp(A64BaseOperand const* operand, u8 bits)
{
    return a64_base_gpr_role(operand, bits, true);
}

BUSTER_GLOBAL_LOCAL bool a64_base_is_fpr(A64BaseOperand const* operand, u8 bits)
{
    return operand->kind == A64_BASE_OPERAND_FPR && operand->reg.bits == bits;
}

BUSTER_GLOBAL_LOCAL bool a64_base_unsigned(A64BaseOperand const* operand, u64 maximum, u64* value)
{
    bool valid = operand->kind == A64_BASE_OPERAND_IMMEDIATE && !operand->negative && operand->magnitude <= maximum;
    if (valid)
    {
        *value = operand->magnitude;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_signed(u64 magnitude, bool negative, s64 minimum, s64 maximum, s64* value)
{
    bool valid = negative ? magnitude <= (u64)(-(minimum + 1)) + 1u : magnitude <= (u64)maximum;
    if (valid)
    {
        *value = negative ? (s64)(0u - magnitude) : (s64)magnitude;
    }
    return valid;
}

// An integer immediate as a `bits`-wide pattern; negative values wrap and
// positive values must fit the width unsigned.
BUSTER_GLOBAL_LOCAL bool a64_base_pattern(A64BaseOperand const* operand, u8 bits, u64* value)
{
    u64 mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
    bool valid = operand->kind == A64_BASE_OPERAND_IMMEDIATE &&
                 (operand->negative ? operand->magnitude <= (UINT64_C(1) << (bits - 1)) : operand->magnitude <= mask);
    if (valid)
    {
        *value = (operand->negative ? 0u - operand->magnitude : operand->magnitude) & mask;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u64 a64_base_rotate_right(u64 value, u32 amount, u32 size)
{
    u64 mask = size == 64 ? UINT64_MAX : (UINT64_C(1) << size) - 1u;
    value &= mask;
    return amount == 0 ? value : ((value >> amount) | (value << (size - amount))) & mask;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_popcount(u64 value)
{
    u32 count = 0;
    while (value)
    {
        value &= value - 1u;
        count += 1;
    }
    return count;
}

// Bitmask immediate: a rotated run of ones replicated across 2..64-bit
// elements. Produces N:immr:imms in bits 22..10 of a logical instruction.
BUSTER_GLOBAL_LOCAL bool a64_base_logical_immediate(u64 value, u8 bits, u32* fields)
{
    if (bits == 32)
    {
        value = (value & UINT32_MAX) | (value << 32);
    }
    bool valid = value != 0 && value != UINT64_MAX;
    u32 size = 64;
    while (valid && size > 2)
    {
        u32 half = size / 2;
        u64 mask = (UINT64_C(1) << half) - 1u;
        if ((value & mask) != ((value >> half) & mask))
        {
            break;
        }
        size = half;
    }
    u64 element = size == 64 ? value : value & ((UINT64_C(1) << size) - 1u);
    u32 ones = a64_base_popcount(element);
    u64 run = ones == 64 ? UINT64_MAX : (UINT64_C(1) << ones) - 1u;
    u32 rotation = size;
    for (u32 candidate = 0; valid && candidate < size && rotation == size; candidate += 1)
    {
        if (a64_base_rotate_right(run, candidate, size) == element)
        {
            rotation = candidate;
        }
    }
    valid = valid && rotation < size;
    if (valid)
    {
        u32 n = size == 64;
        u32 imms = ((~(size * 2u - 1u)) & 0x3fu) | (ones - 1u);
        *fields = (n << 12) | (rotation << 6) | imms;
    }
    return valid;
}

// FMOV imm8 = sign:NOT(b):c:d:efgh for +-(16 + efgh)/16 * 2^r, r in -3..4.
BUSTER_GLOBAL_LOCAL bool a64_base_float_immediate(A64BaseOperand const* operand, u32* imm8)
{
    bool valid = operand->kind == A64_BASE_OPERAND_FLOAT || operand->kind == A64_BASE_OPERAND_IMMEDIATE;
    u64 digits = operand->magnitude;
    s32 exponent = operand->kind == A64_BASE_OPERAND_FLOAT ? operand->exponent : 0;
    u64 scale = 1;
    while (valid && exponent > 0)
    {
        valid = digits <= 32u;
        digits *= 10u;
        exponent -= 1;
    }
    valid = valid && exponent >= -A64_BASE_FLOAT_FRACTION_DIGITS_MAX;
    for (s32 step = 0; valid && step < -exponent; step += 1)
    {
        scale *= 10u;
    }
    valid = valid && digits != 0 && digits <= 32u * scale;
    bool found = false;
    for (u32 n = 16; valid && n < 32 && !found; n += 1)
    {
        for (s32 r = -3; r <= 4 && !found; r += 1)
        {
            // n/16 * 2^r == digits/scale  <=>  digits * 2^(4-r) == n * scale.
            if (digits * (UINT64_C(1) << (4 - r)) == (u64)n * scale)
            {
                u32 high = r >= 1 ? (u32)(r - 1) : 4u | (u32)(r + 3);
                *imm8 = ((u32)operand->negative << 7) | (high << 4) | (n - 16u);
                found = true;
            }
        }
    }
    return valid && found;
}

BUSTER_GLOBAL_LOCAL bool a64_base_float_zero(A64BaseOperand const* operand)
{
    return (operand->kind == A64_BASE_OPERAND_FLOAT || operand->kind == A64_BASE_OPERAND_IMMEDIATE) && operand->magnitude == 0 && !operand->negative;
}

// FP type field: S=0, D=1, H=3.
BUSTER_GLOBAL_LOCAL bool a64_base_fp_type(A64BaseOperand const* operand, u32* type)
{
    bool valid = operand->kind == A64_BASE_OPERAND_FPR && (operand->reg.bits == 16 || operand->reg.bits == 32 || operand->reg.bits == 64);
    if (valid)
    {
        *type = operand->reg.bits == 32 ? 0u : operand->reg.bits == 64 ? 1u : 3u;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_sf(u8 bits)
{
    return bits == 64 ? UINT32_C(1) << 31 : 0u;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_rd(A64BaseOperand const* operand)
{
    return operand->reg.number;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_rn(A64BaseOperand const* operand)
{
    return (u32)operand->reg.number << 5;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_rm(A64BaseOperand const* operand)
{
    return (u32)operand->reg.number << 16;
}

// ADD/SUB(S) immediate, shifted register or extended register. Negative
// immediates select the opposite operation, as LLVM's assembler does.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_add_sub(u32 variant, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    A64BaseOperand const* m = operands + 2;
    A64BaseOperand const* modifier = count == 4 ? operands + 3 : 0;
    u32 setflags = variant & 1u;
    u32 op = (variant >> 1) & 1u;
    u8 bits = d->reg.bits;
    bool valid = (count == 3 || count == 4) && d->kind == A64_BASE_OPERAND_GPR && (!modifier || modifier->kind == A64_BASE_OPERAND_MODIFIER);
    u32 encoded = 0;
    if (valid && m->kind == A64_BASE_OPERAND_IMMEDIATE)
    {
        u64 immediate = m->magnitude;
        u32 shift = modifier && modifier->amount == 12;
        valid = a64_base_gpr_role(d, bits, !setflags) && a64_base_gpr_sp(n, bits) &&
                (!modifier || (modifier->modifier == A64_BASE_MODIFIER_LSL && (modifier->amount == 0 || modifier->amount == 12)));
        op ^= (u32)m->negative;
        if (!modifier && immediate > A64_BASE_IMM12_MAX && !(immediate & A64_BASE_IMM12_MAX) && (immediate >> 12) <= A64_BASE_IMM12_MAX)
        {
            shift = 1;
            immediate >>= 12;
        }
        valid = valid && immediate <= A64_BASE_IMM12_MAX;
        encoded = a64_base_sf(bits) | (op << 30) | (setflags << 29) | UINT32_C(0x11000000) | (shift << 22) | ((u32)immediate << 10) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && m->kind == A64_BASE_OPERAND_GPR)
    {
        bool extended = modifier && modifier->modifier >= A64_BASE_MODIFIER_UXTB && modifier->modifier <= A64_BASE_MODIFIER_SXTX;
        bool stack_pointer = (d->reg.number == 31 && d->reg.sp) || (n->reg.number == 31 && n->reg.sp);
        if (extended || stack_pointer)
        {
            u32 option = extended ? (u32)(modifier->modifier - A64_BASE_MODIFIER_UXTB) : bits == 64 ? 3u : 2u;
            u32 amount = modifier ? modifier->amount : 0;
            u8 index_bits = bits == 64 && (option & 3u) == 3u ? 64 : 32;
            valid = (extended || !modifier || modifier->modifier == A64_BASE_MODIFIER_LSL) && amount <= 4 &&
                    a64_base_gpr_role(d, bits, !setflags) && a64_base_gpr_sp(n, bits) && a64_base_gpr_zr(m, index_bits);
            encoded = a64_base_sf(bits) | (op << 30) | (setflags << 29) | UINT32_C(0x0b200000) | a64_base_rm(m) | (option << 13) | (amount << 10) |
                      a64_base_rn(n) | a64_base_rd(d);
        }
        else
        {
            u32 shift = modifier ? modifier->modifier : 0;
            u32 amount = modifier ? modifier->amount : 0;
            valid = shift <= A64_BASE_MODIFIER_ASR && amount < bits && a64_base_gpr_zr(d, bits) && a64_base_gpr_zr(n, bits) && a64_base_gpr_zr(m, bits);
            encoded = a64_base_sf(bits) | (op << 30) | (setflags << 29) | UINT32_C(0x0b000000) | (shift << 22) | a64_base_rm(m) | (amount << 10) |
                      a64_base_rn(n) | a64_base_rd(d);
        }
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// Logical immediate or shifted register. variant = N:opc.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_logical(u32 variant, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    A64BaseOperand const* m = operands + 2;
    A64BaseOperand const* modifier = count == 4 ? operands + 3 : 0;
    u32 opc = variant & 3u;
    u32 invert = (variant >> 2) & 1u;
    u8 bits = d->reg.bits;
    bool valid = (count == 3 || count == 4) && d->kind == A64_BASE_OPERAND_GPR;
    u32 encoded = 0;
    if (valid && m->kind == A64_BASE_OPERAND_IMMEDIATE)
    {
        u64 value = 0;
        u32 fields = 0;
        valid = count == 3 && a64_base_gpr_role(d, bits, opc != 3) && a64_base_gpr_zr(n, bits) && a64_base_pattern(m, bits, &value);
        if (invert)
        {
            value = bits == 64 ? ~value : ~value & UINT32_MAX;
        }
        valid = valid && a64_base_logical_immediate(value, bits, &fields) && (bits == 64 || !(fields >> 12));
        encoded = a64_base_sf(bits) | (opc << 29) | UINT32_C(0x12000000) | (fields << 10) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid)
    {
        u32 shift = modifier ? modifier->modifier : 0;
        u32 amount = modifier ? modifier->amount : 0;
        valid = (!modifier || (modifier->kind == A64_BASE_OPERAND_MODIFIER && shift <= A64_BASE_MODIFIER_ROR)) && amount < bits &&
                a64_base_gpr_zr(d, bits) && a64_base_gpr_zr(n, bits) && a64_base_gpr_zr(m, bits);
        encoded = a64_base_sf(bits) | (opc << 29) | UINT32_C(0x0a000000) | (shift << 22) | (invert << 21) | a64_base_rm(m) | (amount << 10) |
                  a64_base_rn(n) | a64_base_rd(d);
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL A64BaseOperand a64_base_zero_register(u8 bits)
{
    return (A64BaseOperand){.kind = A64_BASE_OPERAND_GPR, .reg = {.bits = bits, .number = 31}};
}

// MOVZ/MOVN/MOVK with an explicit `lsl #16*hw`.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_move_wide(u32 opc, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    u8 bits = d->reg.bits;
    u64 immediate = 0;
    u32 shift = count == 3 ? operands[2].amount : 0;
    bool valid = (count == 2 || (count == 3 && operands[2].kind == A64_BASE_OPERAND_MODIFIER && operands[2].modifier == A64_BASE_MODIFIER_LSL)) &&
                 d->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(d, bits) && a64_base_unsigned(operands + 1, UINT16_MAX, &immediate) &&
                 shift % 16u == 0 && shift < bits;
    if (valid)
    {
        *word = a64_base_sf(bits) | (opc << 29) | UINT32_C(0x12800000) | ((shift / 16u) << 21) | ((u32)immediate << 5) | a64_base_rd(d);
    }
    return valid;
}

// MOV Rd, #imm: MOVZ, then MOVN, then ORR with a bitmask immediate.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_move_immediate(A64BaseOperand const* d, A64BaseOperand const* source, u32* word)
{
    u8 bits = d->reg.bits;
    u64 mask = bits == 64 ? UINT64_MAX : UINT32_MAX;
    u64 value = 0;
    bool valid = a64_base_pattern(source, bits, &value);
    bool found = false;
    bool stack_pointer = d->reg.number == 31 && d->reg.sp;
    for (u32 inverted = 0; valid && !stack_pointer && inverted < 2 && !found; inverted += 1)
    {
        u64 candidate = inverted ? ~value & mask : value;
        for (u32 hw = 0; hw < bits / 16u && !found; hw += 1)
        {
            if ((candidate & ~(UINT64_C(0xffff) << (16u * hw))) == 0)
            {
                u32 opc = inverted ? 0u : 2u;
                *word = a64_base_sf(bits) | (opc << 29) | UINT32_C(0x12800000) | (hw << 21) | ((u32)(candidate >> (16u * hw)) << 5) | a64_base_rd(d);
                found = true;
            }
        }
    }
    u32 fields = 0;
    if (valid && !found && a64_base_logical_immediate(value, bits, &fields))
    {
        *word = a64_base_sf(bits) | (UINT32_C(1) << 29) | UINT32_C(0x12000000) | (fields << 10) | (UINT32_C(31) << 5) | a64_base_rd(d);
        found = true;
    }
    return valid && found;
}

// imm5 for an element of `bits` at `index`: index:1 shifted by log2(bytes).
BUSTER_GLOBAL_LOCAL u32 a64_base_element_imm5(A64BaseRegister reg)
{
    u32 size_log2 = reg.bits == 8 ? 0u : reg.bits == 16 ? 1u : reg.bits == 32 ? 2u : 3u;
    return (((u32)reg.index << 1) | 1u) << size_log2;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_element_size_log2(u8 bits)
{
    return bits == 8 ? 0u : bits == 16 ? 1u : bits == 32 ? 2u : 3u;
}

// UMOV Wd/Xd, Vn.T[i]; the MOV alias accepts only S->W and D->X.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_element_unsigned(A64BaseOperand const* d, A64BaseOperand const* n, bool alias, u32* word)
{
    bool valid = n->kind == A64_BASE_OPERAND_ELEMENT && a64_base_gpr_zr(d, n->reg.bits == 64 ? 64 : 32) && d->kind == A64_BASE_OPERAND_GPR &&
                 (!alias || n->reg.bits >= 32);
    if (valid)
    {
        *word = ((u32)(n->reg.bits == 64) << 30) | UINT32_C(0x0e003c00) | (a64_base_element_imm5(n->reg) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    return valid;
}

// INS Vd.T[i], Rn or INS Vd.T[i], Vn.T[j].
BUSTER_GLOBAL_LOCAL bool a64_base_encode_element_insert(A64BaseOperand const* d, A64BaseOperand const* n, u32* word)
{
    bool valid = d->kind == A64_BASE_OPERAND_ELEMENT;
    u32 encoded = 0;
    if (valid && n->kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_gpr_zr(n, d->reg.bits == 64 ? 64 : 32);
        encoded = UINT32_C(0x4e001c00) | (a64_base_element_imm5(d->reg) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid)
    {
        valid = n->kind == A64_BASE_OPERAND_ELEMENT && n->reg.bits == d->reg.bits;
        u32 imm4 = (u32)n->reg.index << a64_base_element_size_log2(n->reg.bits);
        encoded = UINT32_C(0x6e000400) | (a64_base_element_imm5(d->reg) << 16) | (imm4 << 11) | a64_base_rn(n) | a64_base_rd(d);
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// DUP Vd.T, Rn / DUP Vd.T, Vn.Ts[i] / DUP (scalar) Fd, Vn.T[i].
BUSTER_GLOBAL_LOCAL bool a64_base_encode_element_duplicate(A64BaseOperand const* d, A64BaseOperand const* n, u32* word)
{
    bool valid;
    u32 encoded = 0;
    if (d->kind == A64_BASE_OPERAND_VECTOR && n->kind == A64_BASE_OPERAND_GPR)
    {
        u32 q = d->reg.lanes * d->reg.bits == 128;
        A64BaseRegister size = {.bits = d->reg.bits};
        valid = a64_base_gpr_zr(n, d->reg.bits == 64 ? 64 : 32) && (d->reg.bits != 64 || q);
        encoded = (q << 30) | UINT32_C(0x0e000c00) | (a64_base_element_imm5(size) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (d->kind == A64_BASE_OPERAND_VECTOR && n->kind == A64_BASE_OPERAND_ELEMENT)
    {
        u32 q = d->reg.lanes * d->reg.bits == 128;
        valid = n->reg.bits == d->reg.bits && (d->reg.bits != 64 || q);
        encoded = (q << 30) | UINT32_C(0x0e000400) | (a64_base_element_imm5(n->reg) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (d->kind == A64_BASE_OPERAND_FPR && n->kind == A64_BASE_OPERAND_ELEMENT)
    {
        valid = n->reg.bits == d->reg.bits;
        encoded = UINT32_C(0x5e000400) | (a64_base_element_imm5(n->reg) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_encode_move(A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    bool valid = count == 2;
    if (valid && d->kind == A64_BASE_OPERAND_GPR && n->kind == A64_BASE_OPERAND_GPR)
    {
        u8 bits = d->reg.bits;
        bool stack_pointer = (d->reg.number == 31 && d->reg.sp) || (n->reg.number == 31 && n->reg.sp);
        valid = n->reg.bits == bits && (stack_pointer ? a64_base_gpr_sp(d, bits) && a64_base_gpr_sp(n, bits) : a64_base_gpr_zr(d, bits) && a64_base_gpr_zr(n, bits));
        if (valid)
        {
            *word = stack_pointer ? a64_base_sf(bits) | UINT32_C(0x11000000) | a64_base_rn(n) | a64_base_rd(d)
                                  : a64_base_sf(bits) | UINT32_C(0x2a000000) | a64_base_rm(n) | (UINT32_C(31) << 5) | a64_base_rd(d);
        }
    }
    else if (valid && d->kind == A64_BASE_OPERAND_GPR && n->kind == A64_BASE_OPERAND_IMMEDIATE)
    {
        valid = a64_base_encode_move_immediate(d, n, word);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_encode_element_unsigned(d, n, true, word);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_ELEMENT)
    {
        valid = a64_base_encode_element_insert(d, n, word);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_FPR)
    {
        valid = a64_base_encode_element_duplicate(d, n, word);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_VECTOR && n->kind == A64_BASE_OPERAND_VECTOR)
    {
        // MOV Vd.T, Vn.T is ORR Vd.T, Vn.T, Vn.T for the byte arrangements.
        u32 q = d->reg.lanes * d->reg.bits == 128;
        valid = d->reg.bits == 8 && n->reg.bits == 8 && d->reg.lanes == n->reg.lanes;
        if (valid)
        {
            *word = (q << 30) | UINT32_C(0x0ea01c00) | a64_base_rm(n) | a64_base_rn(n) | a64_base_rd(d);
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_bitfield_word(u32 opc, u8 bits, u32 immr, u32 imms, A64BaseOperand const* n, A64BaseOperand const* d)
{
    u32 sf = bits == 64;
    return a64_base_sf(bits) | (opc << 29) | UINT32_C(0x13000000) | (sf << 22) | (immr << 16) | (imms << 10) | a64_base_rn(n) | a64_base_rd(d);
}

// SBFM/BFM/UBFM and the LSL/LSR/ASR/ROR, extend, extract and insert aliases.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_bitfield(u8 family, u32 variant, u32 bits_field, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    u8 bits = d->reg.bits;
    bool valid = d->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(d, bits) && count >= 2;
    u64 first = 0;
    u64 second = 0;
    u32 encoded = 0;
    if (valid && family == A64_BASE_FAMILY_BITFIELD)
    {
        valid = count == 4 && a64_base_gpr_zr(n, bits) && a64_base_unsigned(operands + 2, bits - 1u, &first) && a64_base_unsigned(operands + 3, bits - 1u, &second);
        encoded = a64_base_bitfield_word(variant, bits, (u32)first, (u32)second, n, d);
    }
    else if (valid && family == A64_BASE_FAMILY_SHIFT && count == 3 && operands[2].kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_gpr_zr(n, bits) && a64_base_gpr_zr(operands + 2, bits);
        encoded = a64_base_sf(bits) | UINT32_C(0x1ac02000) | a64_base_rm(operands + 2) | (variant << 10) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && family == A64_BASE_FAMILY_SHIFT)
    {
        valid = count == 3 && a64_base_gpr_zr(n, bits) && a64_base_unsigned(operands + 2, bits - 1u, &first);
        u32 amount = (u32)first;
        if (variant == A64_BASE_MODIFIER_LSL)
        {
            encoded = a64_base_bitfield_word(2, bits, (bits - amount) % bits, bits - 1u - amount, n, d);
        }
        else if (variant == A64_BASE_MODIFIER_ROR)
        {
            u32 sf = bits == 64;
            encoded = a64_base_sf(bits) | UINT32_C(0x13800000) | (sf << 22) | a64_base_rm(n) | (amount << 10) | a64_base_rn(n) | a64_base_rd(d);
        }
        else
        {
            encoded = a64_base_bitfield_word(variant == A64_BASE_MODIFIER_ASR ? 0u : 2u, bits, amount, bits - 1u, n, d);
        }
    }
    else if (valid && family == A64_BASE_FAMILY_EXTEND)
    {
        // Signed extends write W or X (SXTW only X); unsigned extends write W.
        valid = count == 2 && a64_base_gpr_zr(n, 32) && (variant == 0 ? (bits_field != 31 || bits == 64) : bits == 32);
        encoded = a64_base_bitfield_word(variant, bits, 0, bits_field, n, d);
    }
    else if (valid && (family == A64_BASE_FAMILY_BITFIELD_EXTRACT || family == A64_BASE_FAMILY_BITFIELD_INSERT))
    {
        valid = count == 4 && a64_base_gpr_zr(n, bits) && a64_base_unsigned(operands + 2, bits - 1u, &first) &&
                a64_base_unsigned(operands + 3, bits, &second) && second >= 1 && first + second <= bits;
        encoded = family == A64_BASE_FAMILY_BITFIELD_EXTRACT
                      ? a64_base_bitfield_word(variant, bits, (u32)first, (u32)(first + second - 1u), n, d)
                      : a64_base_bitfield_word(variant, bits, (u32)((bits - first) % bits), (u32)(second - 1u), n, d);
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// Load/store register: unsigned scaled offset, unscaled, pre/post-index and
// register offset. `size_field` is the access size or SIZE_FROM_REGISTER.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_load_store(u32 variant, u32 size_field, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* t = operands;
    A64BaseOperand const* memory = operands + 1;
    u32 kind = variant & A64_BASE_TRANSFER_KIND_MASK;
    bool unscaled = (variant & A64_BASE_TRANSFER_UNSCALED) != 0;
    bool valid = (count == 2 || count == 3) && memory->kind == A64_BASE_OPERAND_MEMORY && a64_base_gpr_sp(&(A64BaseOperand){.kind = A64_BASE_OPERAND_GPR, .reg = memory->reg}, 64);
    u32 size = size_field;
    u32 opc = kind == A64_BASE_TRANSFER_STORE ? 0u : 1u;
    u32 vector = 0;
    u32 scale_log2 = size_field;
    if (valid && size_field == A64_BASE_SIZE_FROM_REGISTER && t->kind == A64_BASE_OPERAND_FPR)
    {
        vector = 1;
        scale_log2 = a64_base_element_size_log2(t->reg.bits) + (t->reg.bits == 128);
        size = t->reg.bits == 128 ? 0u : scale_log2;
        opc += t->reg.bits == 128 ? 2u : 0u;
    }
    else if (valid && size_field == A64_BASE_SIZE_FROM_REGISTER)
    {
        valid = t->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(t, t->reg.bits);
        size = t->reg.bits == 64 ? 3u : 2u;
        scale_log2 = size;
    }
    else if (valid && kind == A64_BASE_TRANSFER_PREFETCH)
    {
        u64 operation = 0;
        valid = t->kind == A64_BASE_OPERAND_PREFETCH || a64_base_unsigned(t, 31, &operation);
        opc = 2;
    }
    else if (valid && kind == A64_BASE_TRANSFER_LOAD_SIGNED)
    {
        valid = t->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(t, t->reg.bits) && (size_field != 2 || t->reg.bits == 64);
        opc = t->reg.bits == 64 ? 2u : 3u;
    }
    else if (valid)
    {
        valid = a64_base_gpr_zr(t, 32);
    }
    u32 rt = t->kind == A64_BASE_OPERAND_PREFETCH || t->kind == A64_BASE_OPERAND_IMMEDIATE ? (u32)t->magnitude : t->reg.number;
    u32 common = (size << 30) | (vector << 26) | (opc << 22) | a64_base_rn(memory) | rt;
    bool post_index = count == 3;
    bool writeback = post_index || memory->writeback;
    // A writeback base that is also the transfer register is unpredictable.
    valid = valid && (!writeback || t->kind != A64_BASE_OPERAND_GPR || t->reg.number == 31 || t->reg.number != memory->reg.number);
    u32 encoded = 0;
    if (valid && writeback)
    {
        A64BaseOperand const* offset = post_index ? operands + 2 : memory;
        s64 value = 0;
        valid = !unscaled && kind != A64_BASE_TRANSFER_PREFETCH && (post_index ? memory->address == A64_BASE_ADDRESS_BASE && offset->kind == A64_BASE_OPERAND_IMMEDIATE : true) &&
                a64_base_signed(offset->magnitude, offset->negative, A64_BASE_IMM9_MIN, A64_BASE_IMM9_MAX, &value);
        encoded = UINT32_C(0x38000000) | common | (((u32)value & 0x1ffu) << 12) | ((post_index ? 1u : 3u) << 10);
    }
    else if (valid && memory->address == A64_BASE_ADDRESS_REGISTER)
    {
        u32 modifier = memory->modifier_present ? memory->modifier : A64_BASE_MODIFIER_LSL;
        u32 option = modifier == A64_BASE_MODIFIER_LSL ? 3u : modifier <= A64_BASE_MODIFIER_SXTX ? modifier - A64_BASE_MODIFIER_UXTB : 0u;
        u8 index_bits = (option & 1u) ? 64 : 32;
        u32 amount = memory->amount;
        // Byte accesses encode S=1 when an amount (#0) is written; wider
        // accesses take #0 (S=0) or #log2(size) (S=1).
        u32 shift = scale_log2 == 0 ? (u32)memory->amount_present : (u32)(amount != 0);
        valid = !unscaled && (option == 2 || option == 3 || option == 6 || option == 7) &&
                (modifier == A64_BASE_MODIFIER_LSL ? !memory->modifier_present || memory->amount_present : true) &&
                (amount == 0 || amount == scale_log2) && a64_base_gpr_zr(&(A64BaseOperand){.kind = A64_BASE_OPERAND_GPR, .reg = memory->index}, index_bits);
        encoded = UINT32_C(0x38200800) | common | ((u32)memory->index.number << 16) | (option << 13) | (shift << 12);
    }
    else if (valid)
    {
        s64 value = 0;
        bool in_range = a64_base_signed(memory->magnitude, memory->negative, INT64_MIN, INT64_MAX, &value);
        u64 scale = UINT64_C(1) << scale_log2;
        bool scaled = !unscaled && in_range && value >= 0 && ((u64)value & (scale - 1u)) == 0 && ((u64)value >> scale_log2) <= A64_BASE_IMM12_MAX;
        if (scaled)
        {
            encoded = UINT32_C(0x39000000) | common | ((u32)((u64)value >> scale_log2) << 10);
        }
        else
        {
            valid = in_range && value >= A64_BASE_IMM9_MIN && value <= A64_BASE_IMM9_MAX;
            encoded = UINT32_C(0x38000000) | common | (((u32)value & 0x1ffu) << 12);
        }
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// LDP/STP/LDNP/STNP/LDPSW: signed offset, pre- and post-index.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_pair(u32 variant, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* t = operands;
    A64BaseOperand const* t2 = operands + 1;
    A64BaseOperand const* memory = operands + 2;
    bool load = (variant & A64_BASE_PAIR_LOAD) != 0;
    bool no_allocate = (variant & A64_BASE_PAIR_NO_ALLOCATE) != 0;
    bool signed_word = (variant & A64_BASE_PAIR_SIGNED_WORD) != 0;
    bool valid = (count == 3 || count == 4) && memory->kind == A64_BASE_OPERAND_MEMORY && memory->address != A64_BASE_ADDRESS_REGISTER &&
                 a64_base_gpr_sp(&(A64BaseOperand){.kind = A64_BASE_OPERAND_GPR, .reg = memory->reg}, 64) && t->kind == t2->kind &&
                 t->reg.bits == t2->reg.bits;
    u32 opc = 0;
    u32 vector = 0;
    u32 scale_log2 = 0;
    if (valid && t->kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_gpr_zr(t, t->reg.bits) && a64_base_gpr_zr(t2, t->reg.bits) && (!signed_word || t->reg.bits == 64);
        opc = signed_word ? 1u : t->reg.bits == 64 ? 2u : 0u;
        scale_log2 = signed_word || t->reg.bits == 32 ? 2u : 3u;
    }
    else if (valid)
    {
        valid = !signed_word && t->kind == A64_BASE_OPERAND_FPR && t->reg.bits >= 32;
        opc = t->reg.bits == 32 ? 0u : t->reg.bits == 64 ? 1u : 2u;
        scale_log2 = t->reg.bits == 32 ? 2u : t->reg.bits == 64 ? 3u : 4u;
        vector = 1;
    }
    bool post_index = count == 4;
    bool writeback = post_index || memory->writeback;
    u32 mode = no_allocate ? 0u : post_index ? 1u : memory->writeback ? 3u : 2u;
    A64BaseOperand const* offset = post_index ? operands + 3 : memory;
    s64 value = 0;
    bool gpr = t->kind == A64_BASE_OPERAND_GPR;
    bool base_conflict = gpr && memory->reg.number != 31 && (t->reg.number == memory->reg.number || t2->reg.number == memory->reg.number);
    valid = valid && !(no_allocate && writeback) && (!post_index || (memory->address == A64_BASE_ADDRESS_BASE && offset->kind == A64_BASE_OPERAND_IMMEDIATE)) &&
            !(load && t->reg.number == t2->reg.number) && !(writeback && base_conflict) &&
            a64_base_signed(offset->magnitude, offset->negative, INT64_MIN, INT64_MAX, &value) && value % ((s64)1 << scale_log2) == 0 &&
            value / ((s64)1 << scale_log2) >= A64_BASE_IMM7_MIN && value / ((s64)1 << scale_log2) <= A64_BASE_IMM7_MAX;
    if (valid)
    {
        u32 scaled = (u32)(value / ((s64)1 << scale_log2)) & 0x7fu;
        *word = (opc << 30) | UINT32_C(0x28000000) | (vector << 26) | (mode << 23) | ((u32)load << 22) | (scaled << 15) |
                ((u32)t2->reg.number << 10) | a64_base_rn(memory) | t->reg.number;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_memory_base_only(A64BaseOperand const* memory)
{
    return memory->kind == A64_BASE_OPERAND_MEMORY && !memory->writeback && memory->reg.bits == 64 && (memory->reg.number != 31 || memory->reg.sp) &&
           (memory->address == A64_BASE_ADDRESS_BASE || (memory->address == A64_BASE_ADDRESS_IMMEDIATE && memory->magnitude == 0));
}

// LDXR/LDAXR/STXR/STLXR/LDAR/STLR and their byte/halfword forms.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_exclusive(u32 variant, u32 size_field, A64BaseOperand const* operands, u32 count, u32* word)
{
    bool status = (variant & A64_BASE_EXCLUSIVE_STATUS) != 0;
    A64BaseOperand const* s = operands;
    A64BaseOperand const* t = operands + status;
    A64BaseOperand const* memory = operands + status + 1;
    bool valid = count == 2u + status && t->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(t, t->reg.bits) && a64_base_memory_base_only(memory) &&
                 (size_field == A64_BASE_SIZE_FROM_REGISTER || t->reg.bits == 32);
    u32 size = size_field == A64_BASE_SIZE_FROM_REGISTER ? (t->reg.bits == 64 ? 3u : 2u) : size_field;
    u32 rs = 31;
    if (valid && status)
    {
        valid = a64_base_gpr_zr(s, 32) && s->reg.number != t->reg.number && (memory->reg.sp || s->reg.number != memory->reg.number);
        rs = s->reg.number;
    }
    if (valid)
    {
        u32 load = (variant & A64_BASE_EXCLUSIVE_LOAD) != 0;
        u32 ordered = (variant & A64_BASE_EXCLUSIVE_ORDERED) != 0;
        u32 acquire_release = (variant & A64_BASE_EXCLUSIVE_ACQUIRE_RELEASE) != 0;
        *word = (size << 30) | UINT32_C(0x08000000) | (ordered << 23) | (load << 22) | (rs << 16) | (acquire_release << 15) | (UINT32_C(31) << 10) |
                a64_base_rn(memory) | t->reg.number;
    }
    return valid;
}

// LSE spellings: LD<op>{A,AL,L}{B,H}, ST<op>{L}{B,H}, SWP{A,AL,L}{B,H} and
// CAS{A,AL,L}{B,H}. `opcode` is o3:opc for LD/ST/SWP; CAS reports 0xff.
BUSTER_GLOBAL_LOCAL bool a64_base_atomic_mnemonic(String8 mnemonic, u32* opcode, bool* store_alias, u32* acquire, u32* release, u32* size)
{
    static const String8 operations[] = {
        S8_INITIALIZER("add"),  S8_INITIALIZER("clr"),  S8_INITIALIZER("eor"),  S8_INITIALIZER("set"),
        S8_INITIALIZER("smax"), S8_INITIALIZER("smin"), S8_INITIALIZER("umax"), S8_INITIALIZER("umin"),
    };
    String8 rest = {0};
    bool found = false;
    *store_alias = false;
    if (mnemonic.length > 3 && (a64_base_equal(string_slice(mnemonic, 0, 3), S8("swp")) || a64_base_equal(string_slice(mnemonic, 0, 3), S8("cas"))))
    {
        *opcode = a64_base_equal(string_slice(mnemonic, 0, 3), S8("swp")) ? 0x8u : 0xffu;
        rest = string_slice(mnemonic, 3, mnemonic.length);
        found = true;
    }
    else if (mnemonic.length == 3 && (a64_base_equal(mnemonic, S8("swp")) || a64_base_equal(mnemonic, S8("cas"))))
    {
        *opcode = a64_base_equal(mnemonic, S8("swp")) ? 0x8u : 0xffu;
        found = true;
    }
    else if (mnemonic.length > 2 && (a64_base_equal(string_slice(mnemonic, 0, 2), S8("ld")) || a64_base_equal(string_slice(mnemonic, 0, 2), S8("st"))))
    {
        String8 tail = string_slice(mnemonic, 2, mnemonic.length);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(operations) && !found; index += 1)
        {
            u64 length = operations[index].length;
            if (tail.length >= length && a64_base_equal(string_slice(tail, 0, length), operations[index]))
            {
                *opcode = index;
                *store_alias = a64_base_lower(mnemonic.pointer[0]) == 's';
                rest = string_slice(tail, length, tail.length);
                found = true;
            }
        }
    }
    *size = A64_BASE_SIZE_FROM_REGISTER;
    if (found && rest.length && (a64_base_lower(rest.pointer[rest.length - 1]) == 'b' || a64_base_lower(rest.pointer[rest.length - 1]) == 'h'))
    {
        *size = a64_base_lower(rest.pointer[rest.length - 1]) == 'b' ? 0u : 1u;
        rest = string_slice(rest, 0, rest.length - 1);
    }
    *acquire = found && (a64_base_equal(rest, S8("a")) || a64_base_equal(rest, S8("al")));
    *release = found && (a64_base_equal(rest, S8("l")) || a64_base_equal(rest, S8("al")));
    found = found && (rest.length == 0 || *acquire || *release) && !(*store_alias && *acquire);
    return found;
}

BUSTER_GLOBAL_LOCAL bool a64_base_encode_atomic(String8 mnemonic, A64BaseOperand const* operands, u32 count, u32* word)
{
    u32 opcode = 0;
    bool store_alias = false;
    u32 acquire = 0;
    u32 release = 0;
    u32 size_field = 0;
    bool valid = a64_base_atomic_mnemonic(mnemonic, &opcode, &store_alias, &acquire, &release, &size_field);
    A64BaseOperand const* s = operands;
    A64BaseOperand zero = a64_base_zero_register(s->reg.bits);
    A64BaseOperand const* t = store_alias ? &zero : operands + 1;
    A64BaseOperand const* memory = operands + (store_alias ? 1 : 2);
    valid = valid && count == (store_alias ? 2u : 3u) && s->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(s, s->reg.bits) &&
            a64_base_gpr_zr(t, s->reg.bits) && a64_base_memory_base_only(memory) && (size_field == A64_BASE_SIZE_FROM_REGISTER || s->reg.bits == 32);
    u32 size = size_field == A64_BASE_SIZE_FROM_REGISTER ? (s->reg.bits == 64 ? 3u : 2u) : size_field;
    if (valid && opcode == 0xffu)
    {
        *word = (size << 30) | UINT32_C(0x08a07c00) | (acquire << 22) | a64_base_rm(s) | (release << 15) | a64_base_rn(memory) | t->reg.number;
    }
    else if (valid)
    {
        *word = (size << 30) | UINT32_C(0x38200000) | (acquire << 23) | (release << 22) | a64_base_rm(s) | (opcode << 12) | a64_base_rn(memory) | t->reg.number;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_encode_fp_move(A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    u32 type = 0;
    u32 imm8 = 0;
    bool valid = count == 2;
    u32 encoded = 0;
    if (valid && d->kind == A64_BASE_OPERAND_FPR && n->kind == A64_BASE_OPERAND_FPR)
    {
        valid = a64_base_fp_type(d, &type) && n->reg.bits == d->reg.bits;
        encoded = UINT32_C(0x1e204000) | (type << 22) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_FPR && a64_base_float_zero(n))
    {
        // FMOV Fd, #0.0 is FMOV Fd, WZR/XZR.
        valid = a64_base_fp_type(d, &type);
        encoded = (d->reg.bits == 64 ? UINT32_C(1) << 31 : 0u) | UINT32_C(0x1e270000) | (type << 22) | (UINT32_C(31) << 5) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_FPR && (n->kind == A64_BASE_OPERAND_FLOAT || n->kind == A64_BASE_OPERAND_IMMEDIATE))
    {
        valid = a64_base_fp_type(d, &type) && a64_base_float_immediate(n, &imm8);
        encoded = UINT32_C(0x1e201000) | (type << 22) | (imm8 << 13) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_GPR && n->kind == A64_BASE_OPERAND_FPR)
    {
        valid = a64_base_fp_type(n, &type) && a64_base_gpr_zr(d, d->reg.bits) && (n->reg.bits == 16 || n->reg.bits == d->reg.bits);
        encoded = a64_base_sf(d->reg.bits) | UINT32_C(0x1e260000) | (type << 22) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_FPR && n->kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_fp_type(d, &type) && a64_base_gpr_zr(n, n->reg.bits) && (d->reg.bits == 16 || d->reg.bits == n->reg.bits);
        encoded = a64_base_sf(n->reg.bits) | UINT32_C(0x1e270000) | (type << 22) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_GPR && n->kind == A64_BASE_OPERAND_ELEMENT)
    {
        valid = a64_base_gpr_zr(d, 64) && n->reg.bits == 64 && n->reg.index == 1;
        encoded = UINT32_C(0x9eae0000) | a64_base_rn(n) | a64_base_rd(d);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_ELEMENT && n->kind == A64_BASE_OPERAND_GPR)
    {
        valid = a64_base_gpr_zr(n, 64) && d->reg.bits == 64 && d->reg.index == 1;
        encoded = UINT32_C(0x9eaf0000) | a64_base_rn(n) | a64_base_rd(d);
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_same_fpr(A64BaseOperand const* operands, u32 count, u32* type)
{
    bool valid = count != 0 && a64_base_fp_type(operands, type);
    for (u32 index = 1; valid && index < count; index += 1)
    {
        valid = a64_base_is_fpr(operands + index, operands[0].reg.bits);
    }
    return valid;
}

// Scalar FP families other than FMOV and the integer conversions.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_fp(u8 family, u32 variant, A64BaseOperand const* operands, u32 count, u32* word)
{
    u32 type = 0;
    bool valid;
    u32 encoded = 0;
    switch (family)
    {
    case A64_BASE_FAMILY_FP_DATA2:
        valid = count == 3 && a64_base_same_fpr(operands, 3, &type);
        encoded = UINT32_C(0x1e200800) | (type << 22) | a64_base_rm(operands + 2) | (variant << 12) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        break;
    case A64_BASE_FAMILY_FP_DATA1:
        valid = count == 2 && a64_base_same_fpr(operands, 2, &type);
        encoded = UINT32_C(0x1e204000) | (type << 22) | (variant << 15) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        break;
    case A64_BASE_FAMILY_FP_CONVERT_PRECISION:
    {
        u32 destination = 0;
        valid = count == 2 && a64_base_fp_type(operands, &destination) && a64_base_fp_type(operands + 1, &type) && destination != type;
        encoded = UINT32_C(0x1e204000) | (type << 22) | ((4u | destination) << 15) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        break;
    }
    case A64_BASE_FAMILY_FP_DATA3:
        valid = count == 4 && a64_base_same_fpr(operands, 4, &type);
        encoded = UINT32_C(0x1f000000) | (type << 22) | ((variant >> 1) << 21) | a64_base_rm(operands + 2) | ((variant & 1u) << 15) |
                  ((u32)operands[3].reg.number << 10) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        break;
    case A64_BASE_FAMILY_FP_COMPARE:
    {
        bool zero = count == 2 && a64_base_float_zero(operands + 1);
        valid = count == 2 && (zero ? a64_base_fp_type(operands, &type) : a64_base_same_fpr(operands, 2, &type));
        u32 rm = zero ? 0u : a64_base_rm(operands + 1);
        encoded = UINT32_C(0x1e202000) | (type << 22) | rm | a64_base_rn(operands) | (variant << 4) | ((u32)zero << 3);
        break;
    }
    case A64_BASE_FAMILY_FP_COMPARE_CONDITIONAL:
    {
        u64 nzcv = 0;
        valid = count == 4 && a64_base_same_fpr(operands, 2, &type) && a64_base_unsigned(operands + 2, 15, &nzcv) &&
                operands[3].kind == A64_BASE_OPERAND_CONDITION;
        encoded = UINT32_C(0x1e200400) | (type << 22) | a64_base_rm(operands + 1) | ((u32)operands[3].modifier << 12) | a64_base_rn(operands) |
                  (variant << 4) | (u32)nzcv;
        break;
    }
    case A64_BASE_FAMILY_FP_SELECT:
        valid = count == 4 && a64_base_same_fpr(operands, 3, &type) && operands[3].kind == A64_BASE_OPERAND_CONDITION;
        encoded = UINT32_C(0x1e200c00) | (type << 22) | a64_base_rm(operands + 2) | ((u32)operands[3].modifier << 12) | a64_base_rn(operands + 1) |
                  a64_base_rd(operands);
        break;
    default:
        valid = false;
        break;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// FCVT*/SCVTF/UCVTF: general-register forms (optionally fixed-point) and
// the AdvSIMD scalar same-size forms.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_fp_convert(bool to_integer, u32 variant, u32 scalar, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* gpr = to_integer ? operands : operands + 1;
    A64BaseOperand const* fpr = to_integer ? operands + 1 : operands;
    u32 type = 0;
    bool valid = count == 2 || count == 3;
    u32 encoded = 0;
    if (valid && count == 2 && operands[0].kind == A64_BASE_OPERAND_FPR && operands[1].kind == A64_BASE_OPERAND_FPR)
    {
        valid = a64_base_same_fpr(operands, 2, &type) && type != 3;
        encoded = scalar | (type << 22) | a64_base_rn(operands + 1) | a64_base_rd(operands);
    }
    else if (valid)
    {
        u8 bits = gpr->reg.bits;
        valid = gpr->kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(gpr, bits) && a64_base_fp_type(fpr, &type);
        u32 rmode_opcode = variant;
        if (valid && count == 3)
        {
            // Fixed-point forms exist only for FCVTZ[SU] and [SU]CVTF.
            u64 fraction = 0;
            valid = (variant & 0x1eu) == 0x18u || (variant & 0x1eu) == 0x02u;
            valid = valid && a64_base_unsigned(operands + 2, bits, &fraction) && fraction >= 1;
            encoded = a64_base_sf(bits) | UINT32_C(0x1e000000) | (type << 22) | (rmode_opcode << 16) | ((u32)(64u - fraction) << 10) | a64_base_rn(operands + 1) |
                      a64_base_rd(operands);
        }
        else
        {
            encoded = a64_base_sf(bits) | UINT32_C(0x1e200000) | (type << 22) | (rmode_opcode << 16) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_fp_operands_half(A64BaseOperand const* operands, u32 count)
{
    bool half = false;
    for (u32 index = 0; index < count; index += 1)
    {
        half = half || (operands[index].kind == A64_BASE_OPERAND_FPR && operands[index].reg.bits == 16);
    }
    return half;
}

BUSTER_GLOBAL_LOCAL bool a64_base_operands_simd(A64BaseOperand const* operands, u32 count)
{
    bool simd = false;
    for (u32 index = 0; index < count; index += 1)
    {
        simd = simd || operands[index].kind == A64_BASE_OPERAND_ELEMENT || operands[index].kind == A64_BASE_OPERAND_VECTOR;
    }
    return simd;
}

BUSTER_GLOBAL_LOCAL bool a64_base_operands_fp(A64BaseOperand const* operands, u32 count)
{
    bool fp = false;
    for (u32 index = 0; index < count; index += 1)
    {
        fp = fp || operands[index].kind == A64_BASE_OPERAND_FPR;
    }
    return fp;
}

BUSTER_GLOBAL_LOCAL u32 a64_base_vector_q(A64BaseRegister reg)
{
    return (u32)(reg.lanes * reg.bits == 128);
}

BUSTER_GLOBAL_LOCAL bool a64_base_same_vector(A64BaseOperand const* left, A64BaseOperand const* right)
{
    return left->kind == A64_BASE_OPERAND_VECTOR && right->kind == A64_BASE_OPERAND_VECTOR && left->reg.bits == right->reg.bits &&
           left->reg.lanes == right->reg.lanes;
}

// A vector whose element is `bits` wide and whose total width is 64 bits
// (`upper` clear) or 128 bits (`upper` set).
BUSTER_GLOBAL_LOCAL bool a64_base_vector_half(A64BaseOperand const* operand, u8 bits, u32 upper)
{
    return operand->kind == A64_BASE_OPERAND_VECTOR && operand->reg.bits == bits && a64_base_vector_q(operand->reg) == upper;
}

// AdvSIMD modified immediate: q:op:abc:cmode:defgh around a fixed pattern.
BUSTER_GLOBAL_LOCAL u32 a64_base_modified_immediate(u32 q, u32 op, u32 cmode, u32 imm8, u32 rd)
{
    return (q << 30) | (op << 29) | UINT32_C(0x0f000400) | ((imm8 >> 5) << 16) | (cmode << 12) | ((imm8 & 0x1fu) << 5) | rd;
}

// MOVI/MVNI (`logical` clear) or ORR/BIC (`logical` set) vector immediates;
// `op` selects MVNI/BIC.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_immediate(u32 op, bool logical, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* modifier = count == 3 ? operands + 2 : 0;
    u64 immediate = operands[1].magnitude;
    bool valid = (count == 2 || (count == 3 && modifier->kind == A64_BASE_OPERAND_MODIFIER)) && operands[1].kind == A64_BASE_OPERAND_IMMEDIATE &&
                 !operands[1].negative;
    u32 shift = modifier ? modifier->amount : 0;
    bool msl = modifier && modifier->modifier == A64_BASE_MODIFIER_MSL;
    valid = valid && (!modifier || modifier->modifier == A64_BASE_MODIFIER_LSL || msl);
    u32 encoded = 0;
    if (valid && ((d->kind == A64_BASE_OPERAND_FPR && d->reg.bits == 64) || (d->kind == A64_BASE_OPERAND_VECTOR && d->reg.bits == 64)))
    {
        // MOVI Dd / Vd.2D: every byte of the 64-bit pattern is 0x00 or 0xff.
        u32 imm8 = 0;
        for (u32 byte = 0; valid && byte < 8; byte += 1)
        {
            u64 value = (immediate >> (byte * 8u)) & 0xffu;
            valid = value == 0 || value == 0xffu;
            imm8 |= (u32)(value != 0) << byte;
        }
        u32 q = d->kind == A64_BASE_OPERAND_VECTOR;
        valid = valid && !logical && op == 0 && !modifier && (!q || a64_base_vector_q(d->reg));
        encoded = a64_base_modified_immediate(q, 1, 14, imm8, d->reg.number);
    }
    else if (valid && d->kind == A64_BASE_OPERAND_VECTOR)
    {
        u32 q = a64_base_vector_q(d->reg);
        u32 cmode = 0;
        valid = immediate <= 0xffu;
        if (d->reg.bits == 8)
        {
            valid = valid && !logical && op == 0 && !msl && shift == 0;
            cmode = 14;
        }
        else if (d->reg.bits == 16)
        {
            valid = valid && !msl && (shift == 0 || shift == 8);
            cmode = 8u | (shift / 8u) << 1;
        }
        else
        {
            valid = valid && (msl ? !logical && (shift == 8 || shift == 16) : shift % 8u == 0 && shift <= 24);
            cmode = msl ? 12u | (shift == 16) : (shift / 8u) << 1;
        }
        encoded = a64_base_modified_immediate(q, op, cmode | (u32)logical, (u32)immediate, d->reg.number);
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

// SHL/SSHR/USHR/SSRA/USRA by immediate, vector or scalar D.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_shift(u32 right, u32 base, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    bool scalar = d->kind == A64_BASE_OPERAND_FPR;
    u32 esize = d->reg.bits;
    u64 shift = 0;
    bool valid = count == 3 && (scalar ? a64_base_is_fpr(d, 64) && a64_base_is_fpr(n, 64) : a64_base_same_vector(d, n) && (esize != 64 || a64_base_vector_q(d->reg))) &&
                 a64_base_unsigned(operands + 2, right ? esize : esize - 1u, &shift) && (!right || shift >= 1);
    if (valid)
    {
        u32 immhb = right ? 2u * esize - (u32)shift : esize + (u32)shift;
        *word = base | (scalar ? UINT32_C(0x50000000) : a64_base_vector_q(d->reg) << 30) | (immhb << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    return valid;
}

// SSHLL/USHLL{2} and the SXTL/UXTL{2} aliases (shift zero).
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_shift_long(u32 variant, u32 base, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    u32 upper = variant & A64_BASE_VECTOR_UPPER;
    bool alias = (variant & A64_BASE_VECTOR_ALIAS) != 0;
    u32 esize = n->reg.bits;
    u64 shift = 0;
    bool valid = count == (alias ? 2u : 3u) && esize <= 32 && a64_base_vector_half(n, (u8)esize, upper) && a64_base_vector_half(d, (u8)(esize * 2u), 1) &&
                 (alias || a64_base_unsigned(operands + 2, esize - 1u, &shift));
    if (valid)
    {
        *word = base | (upper << 30) | ((esize + (u32)shift) << 16) | a64_base_rn(n) | a64_base_rd(d);
    }
    return valid;
}

// XTN{2}: Vd.Tb from the double-width Vn.Ta.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_narrow(u32 upper, u32 base, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    u32 esize = d->reg.bits;
    bool valid = count == 2 && esize <= 32 && a64_base_vector_half(d, (u8)esize, upper) && a64_base_vector_half(n, (u8)(esize * 2u), 1);
    if (valid)
    {
        *word = base | (upper << 30) | (a64_base_element_size_log2((u8)esize) << 22) | a64_base_rn(n) | a64_base_rd(d);
    }
    return valid;
}

// Three-register long forms: double-width Vd.Ta from Vn.Tb and Vm.Tb.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_long(u32 upper, u32 base, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* d = operands;
    A64BaseOperand const* n = operands + 1;
    A64BaseOperand const* m = operands + 2;
    u32 esize = n->reg.bits;
    bool valid = count == 3 && esize <= 32 && a64_base_vector_half(n, (u8)esize, upper) && a64_base_vector_half(m, (u8)esize, upper) &&
                 a64_base_vector_half(d, (u8)(esize * 2u), 1);
    if (valid)
    {
        *word = base | (upper << 30) | (a64_base_element_size_log2((u8)esize) << 22) | a64_base_rm(m) | a64_base_rn(n) | a64_base_rd(d);
    }
    return valid;
}

// LD1/ST1 single lane: `{ Vt.T }[i], [Xn|SP]`, post-indexed by the element
// size (`#bytes`) or by a register.
BUSTER_GLOBAL_LOCAL bool a64_base_encode_vector_lane(u32 load, A64BaseOperand const* operands, u32 count, u32* word)
{
    A64BaseOperand const* list = operands;
    A64BaseOperand const* memory = operands + 1;
    A64BaseRegister reg = list->reg;
    bool valid = (count == 2 || count == 3) && list->kind == A64_BASE_OPERAND_LIST && reg.count == 1 && reg.index_present && a64_base_memory_base_only(memory) &&
                 memory->address == A64_BASE_ADDRESS_BASE;
    u32 index = reg.index;
    u32 q = 0;
    u32 s_bit = 0;
    u32 size = 0;
    u32 opcode = 0;
    if (reg.bits == 8)
    {
        q = index >> 3;
        s_bit = (index >> 2) & 1u;
        size = index & 3u;
    }
    else if (reg.bits == 16)
    {
        q = index >> 2;
        s_bit = (index >> 1) & 1u;
        size = (index & 1u) << 1;
        opcode = 2;
    }
    else if (reg.bits == 32)
    {
        q = index >> 1;
        s_bit = index & 1u;
        opcode = 4;
    }
    else
    {
        q = index;
        size = 1;
        opcode = 4;
    }
    u32 post = count == 3;
    u32 rm = 0;
    if (valid && post)
    {
        A64BaseOperand const* step = operands + 2;
        u64 bytes = 0;
        valid = step->kind == A64_BASE_OPERAND_GPR ? a64_base_gpr_zr(step, 64) && step->reg.number != 31
                                                   : a64_base_unsigned(step, 16, &bytes) && bytes == reg.bits / 8u;
        rm = step->kind == A64_BASE_OPERAND_GPR ? step->reg.number : 31u;
    }
    if (valid)
    {
        *word = (q << 30) | UINT32_C(0x0d000000) | (post << 23) | (load << 22) | (rm << 16) | (opcode << 13) | (s_bit << 12) | (size << 10) |
                a64_base_rn(memory) | reg.number;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_barrier_option(String8 name, u32* option)
{
    static const String8 names[16] = {
        {0}, S8_INITIALIZER("oshld"), S8_INITIALIZER("oshst"), S8_INITIALIZER("osh"), {0}, S8_INITIALIZER("nshld"), S8_INITIALIZER("nshst"),
        S8_INITIALIZER("nsh"), {0}, S8_INITIALIZER("ishld"), S8_INITIALIZER("ishst"), S8_INITIALIZER("ish"), {0}, S8_INITIALIZER("ld"),
        S8_INITIALIZER("st"), S8_INITIALIZER("sy"),
    };
    bool found = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names) && !found; index += 1)
    {
        found = names[index].length && a64_base_equal(name, names[index]);
        if (found)
        {
            *option = index;
        }
    }
    return found;
}

// MRS/MSR register operand: op0-2:op1:CRn:CRm:op2 in bits 19..5.
BUSTER_GLOBAL_LOCAL bool a64_base_system_register(String8 name, bool write, u32* encoding)
{
    typedef struct A64BaseSystemRegister A64BaseSystemRegister;
    struct A64BaseSystemRegister
    {
        String8 name;
        u8 fields[5];
        bool writable;
    };
    static const A64BaseSystemRegister registers[] = {
        {S8_INITIALIZER("nzcv"), {3, 3, 4, 2, 0}, true},
        {S8_INITIALIZER("daif"), {3, 3, 4, 2, 1}, true},
        {S8_INITIALIZER("fpcr"), {3, 3, 4, 4, 0}, true},
        {S8_INITIALIZER("fpsr"), {3, 3, 4, 4, 1}, true},
        {S8_INITIALIZER("tpidr_el0"), {3, 3, 13, 0, 2}, true},
        {S8_INITIALIZER("tpidrro_el0"), {3, 3, 13, 0, 3}, true},
        {S8_INITIALIZER("ctr_el0"), {3, 3, 0, 0, 1}, false},
        {S8_INITIALIZER("dczid_el0"), {3, 3, 0, 0, 7}, false},
        {S8_INITIALIZER("cntfrq_el0"), {3, 3, 14, 0, 0}, true},
        {S8_INITIALIZER("cntpct_el0"), {3, 3, 14, 0, 1}, false},
        {S8_INITIALIZER("cntvct_el0"), {3, 3, 14, 0, 2}, false},
        {S8_INITIALIZER("midr_el1"), {3, 0, 0, 0, 0}, false},
        {S8_INITIALIZER("mpidr_el1"), {3, 0, 0, 0, 5}, false},
        {S8_INITIALIZER("currentel"), {3, 0, 4, 2, 2}, false},
    };
    u32 fields[5] = {0};
    bool found = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(registers) && !found; index += 1)
    {
        found = a64_base_equal(name, registers[index].name) && (!write || registers[index].writable);
        for (u32 field = 0; found && field < 5; field += 1)
        {
            fields[field] = registers[index].fields[field];
        }
    }
    if (found)
    {
        *encoding = ((fields[0] - 2u) << 19) | (fields[1] << 16) | (fields[2] << 12) | (fields[3] << 8) | (fields[4] << 5);
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool a64_base_encode_system(A64BaseMnemonic entry, A64BaseOperand const* operands, u32 count, u32* word)
{
    bool valid;
    u32 encoded = 0;
    if (entry.family == A64_BASE_FAMILY_SYSTEM_BARRIER)
    {
        u32 option = 15;
        u64 immediate = 0;
        if (count == 1 && operands[0].kind == A64_BASE_OPERAND_NAME)
        {
            // ISB names only SY; CLREX takes no named option.
            valid = entry.variant != 2 && a64_base_barrier_option(operands[0].name, &option) && (entry.variant != 6 || option == 15);
        }
        else if (count == 1)
        {
            // DMB/DSB take only the named option encodings; the system owner
            // reserves their other four CRm values. ISB and CLREX take any.
            valid = a64_base_unsigned(operands, 15, &immediate) && (entry.variant <= 2 || entry.variant == 6 || (immediate & 3u) != 0);
            option = (u32)immediate;
        }
        else
        {
            valid = count == 0 && entry.bits == 0;
        }
        encoded = UINT32_C(0xd503301f) | (option << 8) | ((u32)entry.variant << 5);
    }
    else if (entry.family == A64_BASE_FAMILY_EXCEPTION)
    {
        u64 immediate = 0;
        valid = count == 1 && a64_base_unsigned(operands, UINT16_MAX, &immediate);
        encoded = entry.bits | ((u32)immediate << entry.variant);
    }
    else
    {
        // MRS Xt, <sysreg> / MSR <sysreg>, Xt.
        bool read = entry.variant != 0;
        A64BaseOperand const* t = read ? operands : operands + 1;
        A64BaseOperand const* name = read ? operands + 1 : operands;
        u32 encoding = 0;
        valid = count == 2 && a64_base_gpr_zr(t, 64) && name->kind == A64_BASE_OPERAND_NAME && a64_base_system_register(name->name, !read, &encoding);
        encoded = (read ? UINT32_C(0xd5300000) : UINT32_C(0xd5100000)) | encoding | t->reg.number;
    }
    if (valid)
    {
        *word = encoded;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool a64_base_encode(A64BaseMnemonic entry, A64BaseOperand const* operands, u32 count, u32* word)
{
    bool valid;
    A64BaseOperand expanded[A64_BASE_MAX_OPERANDS + 1] = {0};
    u8 bits = operands[0].reg.bits;
    switch (entry.family)
    {
    case A64_BASE_FAMILY_ADD_SUB:
        valid = a64_base_encode_add_sub(entry.variant, operands, count, word);
        break;
    case A64_BASE_FAMILY_COMPARE:
    case A64_BASE_FAMILY_TEST:
        expanded[0] = a64_base_zero_register(bits);
        memcpy(expanded + 1, operands, sizeof(*operands) * count);
        valid = count != 0 && operands[0].kind == A64_BASE_OPERAND_GPR && count + 1 <= A64_BASE_MAX_OPERANDS &&
                (entry.family == A64_BASE_FAMILY_COMPARE ? a64_base_encode_add_sub(entry.variant, expanded, count + 1, word)
                                                         : a64_base_encode_logical(entry.variant, expanded, count + 1, word));
        break;
    case A64_BASE_FAMILY_NEGATE:
    case A64_BASE_FAMILY_NOT:
    case A64_BASE_FAMILY_NEGATE_CARRY:
        valid = count >= 2 && count + 1 <= A64_BASE_MAX_OPERANDS && operands[1].kind == A64_BASE_OPERAND_GPR;
        expanded[0] = operands[0];
        expanded[1] = a64_base_zero_register(bits);
        if (valid)
        {
            memcpy(expanded + 2, operands + 1, sizeof(*operands) * (count - 1));
        }
        if (valid && entry.family == A64_BASE_FAMILY_NEGATE_CARRY)
        {
            valid = count == 2 && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, bits);
            *word = a64_base_sf(bits) | ((u32)entry.variant << 29) | UINT32_C(0x1a000000) | a64_base_rm(operands + 1) | (UINT32_C(31) << 5) | a64_base_rd(operands);
        }
        else if (valid)
        {
            valid = entry.family == A64_BASE_FAMILY_NEGATE ? a64_base_encode_add_sub(entry.variant, expanded, count + 1, word)
                                                           : a64_base_encode_logical(entry.variant, expanded, count + 1, word);
        }
        break;
    case A64_BASE_FAMILY_LOGICAL:
        valid = a64_base_encode_logical(entry.variant, operands, count, word);
        break;
    case A64_BASE_FAMILY_MOVE:
        valid = a64_base_encode_move(operands, count, word);
        break;
    case A64_BASE_FAMILY_MOVE_WIDE:
        valid = a64_base_encode_move_wide(entry.variant, operands, count, word);
        break;
    case A64_BASE_FAMILY_BITFIELD:
    case A64_BASE_FAMILY_SHIFT:
    case A64_BASE_FAMILY_EXTEND:
    case A64_BASE_FAMILY_BITFIELD_EXTRACT:
    case A64_BASE_FAMILY_BITFIELD_INSERT:
        valid = a64_base_encode_bitfield(entry.family, entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_EXTRACT:
    {
        u64 lsb = 0;
        valid = count == 4 && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, bits) && a64_base_gpr_zr(operands + 2, bits) &&
                a64_base_unsigned(operands + 3, bits - 1u, &lsb);
        if (valid)
        {
            *word = a64_base_sf(bits) | UINT32_C(0x13800000) | ((u32)(bits == 64) << 22) | a64_base_rm(operands + 2) | ((u32)lsb << 10) | a64_base_rn(operands + 1) |
                    a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_CARRY:
        valid = count == 3 && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, bits) && a64_base_gpr_zr(operands + 2, bits);
        if (valid)
        {
            *word = a64_base_sf(bits) | ((u32)entry.variant << 29) | UINT32_C(0x1a000000) | a64_base_rm(operands + 2) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    case A64_BASE_FAMILY_SELECT:
    case A64_BASE_FAMILY_SET:
    case A64_BASE_FAMILY_SELECT_SAME:
    {
        A64BaseOperand zero = a64_base_zero_register(bits);
        A64BaseOperand const* n = entry.family == A64_BASE_FAMILY_SET ? &zero : operands + 1;
        A64BaseOperand const* m = entry.family == A64_BASE_FAMILY_SELECT ? operands + 2 : n;
        u32 expected = entry.family == A64_BASE_FAMILY_SELECT ? 4u : entry.family == A64_BASE_FAMILY_SELECT_SAME ? 3u : 2u;
        A64BaseOperand const* condition = operands + expected - 1u;
        // The aliases invert their condition and cannot express AL/NV.
        bool alias = entry.family != A64_BASE_FAMILY_SELECT;
        valid = count == expected && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(n, bits) && a64_base_gpr_zr(m, bits) &&
                condition->kind == A64_BASE_OPERAND_CONDITION && (!alias || condition->modifier < 14);
        if (valid)
        {
            u32 code = alias ? condition->modifier ^ 1u : condition->modifier;
            *word = a64_base_sf(bits) | ((u32)(entry.variant >> 1) << 30) | UINT32_C(0x1a800000) | a64_base_rm(m) | (code << 12) | ((u32)(entry.variant & 1u) << 10) |
                    a64_base_rn(n) | a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_COMPARE_CONDITIONAL:
    {
        u64 nzcv = 0;
        u64 immediate = 0;
        bool register_form = count == 4 && operands[1].kind == A64_BASE_OPERAND_GPR;
        valid = count == 4 && a64_base_gpr_zr(operands, bits) && (register_form ? a64_base_gpr_zr(operands + 1, bits) : a64_base_unsigned(operands + 1, 31, &immediate)) &&
                a64_base_unsigned(operands + 2, 15, &nzcv) && operands[3].kind == A64_BASE_OPERAND_CONDITION;
        if (valid)
        {
            u32 field = register_form ? operands[1].reg.number : (u32)immediate;
            *word = a64_base_sf(bits) | ((u32)entry.variant << 30) | UINT32_C(0x3a400000) | (field << 16) | ((u32)operands[3].modifier << 12) |
                    ((u32)!register_form << 11) | a64_base_rn(operands) | (u32)nzcv;
        }
        break;
    }
    case A64_BASE_FAMILY_DATA2:
        valid = count == 3 && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, bits) && a64_base_gpr_zr(operands + 2, bits);
        if (valid)
        {
            *word = a64_base_sf(bits) | UINT32_C(0x1ac00000) | a64_base_rm(operands + 2) | (entry.bits << 10) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    case A64_BASE_FAMILY_DATA1:
    {
        u32 opcode = bits == 64 ? entry.bits : entry.variant;
        valid = count == 2 && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, bits) && opcode != 0xffu;
        if (valid)
        {
            *word = a64_base_sf(bits) | UINT32_C(0x5ac00000) | (opcode << 10) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_DATA3:
    case A64_BASE_FAMILY_MULTIPLY:
    {
        bool accumulate = (entry.variant & A64_BASE_MULTIPLY_ACCUMULATE) != 0;
        bool widening = (entry.variant & A64_BASE_MULTIPLY_LONG) != 0;
        u8 source_bits = widening ? 32 : bits;
        A64BaseOperand zero = a64_base_zero_register(bits);
        A64BaseOperand const* a = accumulate ? operands + 3 : &zero;
        u32 expected = accumulate ? 4u : 3u;
        valid = count == expected && a64_base_gpr_zr(operands, bits) && a64_base_gpr_zr(operands + 1, source_bits) && a64_base_gpr_zr(operands + 2, source_bits) &&
                a64_base_gpr_zr(a, bits) && ((!widening && !(entry.variant & A64_BASE_MULTIPLY_X_ONLY)) || bits == 64);
        if (valid)
        {
            *word = a64_base_sf(bits) | UINT32_C(0x1b000000) | ((entry.bits >> 1) << 21) | a64_base_rm(operands + 2) | ((entry.bits & 1u) << 15) |
                    ((u32)a->reg.number << 10) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_LOAD_STORE:
        valid = a64_base_encode_load_store(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_PAIR:
        valid = a64_base_encode_pair(entry.variant, operands, count, word);
        break;
    case A64_BASE_FAMILY_EXCLUSIVE:
        valid = a64_base_encode_exclusive(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_FP_MOVE:
        valid = a64_base_encode_fp_move(operands, count, word);
        break;
    case A64_BASE_FAMILY_FP_DATA2:
    case A64_BASE_FAMILY_FP_DATA1:
    case A64_BASE_FAMILY_FP_CONVERT_PRECISION:
    case A64_BASE_FAMILY_FP_DATA3:
    case A64_BASE_FAMILY_FP_COMPARE:
    case A64_BASE_FAMILY_FP_COMPARE_CONDITIONAL:
    case A64_BASE_FAMILY_FP_SELECT:
        valid = a64_base_encode_fp(entry.family, entry.variant, operands, count, word);
        break;
    case A64_BASE_FAMILY_FP_TO_INTEGER:
    case A64_BASE_FAMILY_FP_FROM_INTEGER:
        valid = a64_base_encode_fp_convert(entry.family == A64_BASE_FAMILY_FP_TO_INTEGER, entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_ELEMENT_UNSIGNED_MOVE:
        valid = count == 2 && a64_base_encode_element_unsigned(operands, operands + 1, false, word);
        break;
    case A64_BASE_FAMILY_ELEMENT_SIGNED_MOVE:
    {
        A64BaseOperand const* n = operands + 1;
        valid = count == 2 && n->kind == A64_BASE_OPERAND_ELEMENT && operands[0].kind == A64_BASE_OPERAND_GPR && a64_base_gpr_zr(operands, bits) &&
                n->reg.bits < bits && n->reg.bits <= 32;
        if (valid)
        {
            *word = ((u32)(bits == 64) << 30) | UINT32_C(0x0e002c00) | (a64_base_element_imm5(n->reg) << 16) | a64_base_rn(n) | a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_ELEMENT_INSERT:
        valid = count == 2 && a64_base_encode_element_insert(operands, operands + 1, word);
        break;
    case A64_BASE_FAMILY_ELEMENT_DUPLICATE:
        valid = count == 2 && a64_base_encode_element_duplicate(operands, operands + 1, word);
        break;
    case A64_BASE_FAMILY_SYSTEM_BARRIER:
    case A64_BASE_FAMILY_EXCEPTION:
    case A64_BASE_FAMILY_SYSTEM_MOVE:
        valid = a64_base_encode_system(entry, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_IMMEDIATE:
    case A64_BASE_FAMILY_VECTOR_LOGICAL_IMMEDIATE:
        valid = a64_base_encode_vector_immediate(entry.variant, entry.family == A64_BASE_FAMILY_VECTOR_LOGICAL_IMMEDIATE, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_FP_IMMEDIATE:
    {
        // FMOV Vd.2S/4S/2D, #imm8.
        u32 imm8 = 0;
        bool wide = operands[0].reg.bits == 64;
        valid = count == 2 && operands[0].kind == A64_BASE_OPERAND_VECTOR && (operands[0].reg.bits == 32 || (wide && a64_base_vector_q(operands[0].reg))) &&
                a64_base_float_immediate(operands + 1, &imm8);
        if (valid)
        {
            *word = a64_base_modified_immediate(a64_base_vector_q(operands[0].reg), wide, 15, imm8, operands[0].reg.number);
        }
        break;
    }
    case A64_BASE_FAMILY_VECTOR_NOT:
        valid = count == 2 && a64_base_same_vector(operands, operands + 1) && bits == 8;
        if (valid)
        {
            *word = (a64_base_vector_q(operands[0].reg) << 30) | UINT32_C(0x2e205800) | a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    case A64_BASE_FAMILY_VECTOR_EXTRACT:
    {
        u64 index = 0;
        valid = count == 4 && a64_base_same_vector(operands, operands + 1) && a64_base_same_vector(operands, operands + 2) && bits == 8 &&
                a64_base_unsigned(operands + 3, operands[0].reg.lanes - 1u, &index);
        if (valid)
        {
            *word = (a64_base_vector_q(operands[0].reg) << 30) | UINT32_C(0x2e000000) | a64_base_rm(operands + 2) | ((u32)index << 11) |
                    a64_base_rn(operands + 1) | a64_base_rd(operands);
        }
        break;
    }
    case A64_BASE_FAMILY_VECTOR_NARROW:
        valid = a64_base_encode_vector_narrow(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_SHIFT:
        valid = a64_base_encode_vector_shift(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_SHIFT_LONG:
        valid = a64_base_encode_vector_shift_long(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_LONG:
        valid = a64_base_encode_vector_long(entry.variant, entry.bits, operands, count, word);
        break;
    case A64_BASE_FAMILY_VECTOR_COMPARE_ZERO:
        valid = count == 3 && a64_base_same_vector(operands, operands + 1) && (bits != 64 || a64_base_vector_q(operands[0].reg)) &&
                a64_base_float_zero(operands + 2) && operands[2].kind == A64_BASE_OPERAND_IMMEDIATE;
        if (valid)
        {
            *word = entry.bits | (a64_base_vector_q(operands[0].reg) << 30) | (a64_base_element_size_log2(bits) << 22) | a64_base_rn(operands + 1) |
                    a64_base_rd(operands);
        }
        break;
    case A64_BASE_FAMILY_VECTOR_LANE:
        valid = a64_base_encode_vector_lane(entry.variant, operands, count, word);
        break;
    default:
        valid = false;
        break;
    }
    return valid;
}

// The feature an entry needs for these operands: FP/SIMD registers need
// fp-armv8, AdvSIMD families and vector operands need neon, and scalar
// half-precision arithmetic needs fullfp16 (half loads and stores do not).
BUSTER_GLOBAL_LOCAL A64BaseAssemblyStatus a64_base_feature(Target target, A64BaseMnemonic entry, A64BaseOperand const* operands, u32 count)
{
    bool vector_family = entry.family >= A64_BASE_FAMILY_VECTOR_IMMEDIATE;
    bool scalar_simd = count == 2 && operands[0].kind == A64_BASE_OPERAND_FPR && operands[1].kind == A64_BASE_OPERAND_FPR &&
                       (entry.family == A64_BASE_FAMILY_FP_TO_INTEGER || entry.family == A64_BASE_FAMILY_FP_FROM_INTEGER);
    bool simd = vector_family || scalar_simd || a64_base_operands_simd(operands, count);
    bool fp = simd || a64_base_operands_fp(operands, count);
    bool half = a64_base_fp_operands_half(operands, count) && entry.family != A64_BASE_FAMILY_LOAD_STORE;
    A64BaseAssemblyStatus status = A64_BASE_ASSEMBLY_OK;
    if (fp && !target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_FP_ARMV8))
    {
        status = A64_BASE_ASSEMBLY_REQUIRES_FP;
    }
    else if (simd && !target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_NEON))
    {
        status = A64_BASE_ASSEMBLY_REQUIRES_NEON;
    }
    else if (half && !target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_FULLFP16))
    {
        status = A64_BASE_ASSEMBLY_REQUIRES_FULLFP16;
    }
    return status;
}

// Every table entry spelled like `mnemonic` is tried in table order; the first
// one that encodes wins. A missing feature is reported only when no entry
// encodes, so a scalar spelling never reports a vector form's feature.
A64BaseAssemblyStatus a64_base_assemble(Target target, String8 mnemonic, String8 operands_text, u32* word)
{
    A64BaseAssemblyStatus status = A64_BASE_ASSEMBLY_UNKNOWN_MNEMONIC;
    A64BaseAssemblyStatus feature_status = A64_BASE_ASSEMBLY_OK;
    u32 atomic_opcode = 0;
    bool atomic_store = false;
    u32 atomic_acquire = 0;
    u32 atomic_release = 0;
    u32 atomic_size = 0;
    mnemonic = a64_base_trim(mnemonic);
    bool known = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(a64_base_mnemonics) && !known; index += 1)
    {
        known = a64_base_equal(mnemonic, a64_base_mnemonics[index].name);
    }
    bool atomic = !known && a64_base_atomic_mnemonic(mnemonic, &atomic_opcode, &atomic_store, &atomic_acquire, &atomic_release, &atomic_size);
    A64BaseOperand operands[A64_BASE_MAX_OPERANDS] = {0};
    u32 count = 0;
    bool parsed = (known || atomic) && word && a64_base_operands_split(operands_text, operands, &count);
    u32 encoded = 0;
    if ((known || atomic) && word)
    {
        status = A64_BASE_ASSEMBLY_INVALID_OPERANDS;
    }
    if (parsed && atomic)
    {
        if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_AARCH64_LSE))
        {
            status = A64_BASE_ASSEMBLY_REQUIRES_LSE;
        }
        else if (a64_base_encode_atomic(mnemonic, operands, count, &encoded))
        {
            status = A64_BASE_ASSEMBLY_OK;
        }
    }
    for (u32 index = 0; parsed && !atomic && index < BUSTER_ARRAY_LENGTH(a64_base_mnemonics) && status != A64_BASE_ASSEMBLY_OK; index += 1)
    {
        A64BaseMnemonic entry = a64_base_mnemonics[index];
        if (a64_base_equal(mnemonic, entry.name))
        {
            A64BaseAssemblyStatus entry_status = a64_base_feature(target, entry, operands, count);
            if (entry_status != A64_BASE_ASSEMBLY_OK)
            {
                feature_status = feature_status == A64_BASE_ASSEMBLY_OK ? entry_status : feature_status;
            }
            else if (a64_base_encode(entry, operands, count, &encoded))
            {
                status = A64_BASE_ASSEMBLY_OK;
            }
        }
    }
    if (status == A64_BASE_ASSEMBLY_OK)
    {
        *word = encoded;
    }
    else if (feature_status != A64_BASE_ASSEMBLY_OK)
    {
        status = feature_status;
    }
    return status;
}
