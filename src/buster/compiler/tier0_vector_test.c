// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier0's vectors check (#136): VECTOR values and X86 entries in Tier 0. Functions over them are compiled by both
// paths, which must give the same records, encoded by both encoders, which must give the same bytes, and, when the host
// supports the emitted instructions, run natively in a child process, called from C through function pointers:
//   - random programs: statements that each write a vector of 4, 8, 16, 32 or 64 bytes, an integer or a float into an
//     output, or a masked store's bytes, or drop a vector, from inputs, constants (zero ones and words past a
//     sign-extended imm32), frame objects of each width (LOCAL, STORE_LOCAL and SET_LOCAL), DUP, STORE's kept value and
//     ?:, and the rows of tier0_test_vector_kinds (VECTOR, integer and floating operands and results, masks, scratch
//     registers, /is4, memory and VSIB operands, a row of no records), under live values on both stacks (an integer and
//     a float under a statement, stored after it), against this file's own stack machine over the same stream
//     (tier0_test_vector_interpret), whose rows' model is plain C (tier0_test_vector_model);
//   - placement: each row under as many floating values as leave its VECTOR registers room, past 16 for the EVEX ones,
//     and one past the room of a VEX form's, and a scratch register past the integer depths' table, which both paths
//     take through the deep slots (#780);
//   - aggregates in one vector register: functions of 16-, 32- and 64-byte vector parameters (in XMM, YMM and ZMM
//     registers and on the stack) and results, as objects, as a front end hands them, called from C; and functions
//     calling C functions of them, a vector argument on the stack among them;
//   - the rows' rules (tier0_operation_valid) and the stream's for vectors, each break refused, and their valid
//     neighbours taken.
// tier0_test.c includes this file after tier0_int128_test.c, whose helpers it uses.

// ---------------------------------------------------------------------------------------------------------------------
// The rows

#define BUSTER_TIER0_TEST_VECTOR_PROGRAMS 300
#define BUSTER_TIER0_TEST_VECTOR_STATEMENTS 8
// The programs' frame objects, one per vector type (of 16, 32, 64, 8 and 4 bytes): locals 4 to 8 after their four
// pointer parameters.
#define BUSTER_TIER0_TEST_VECTOR_OBJECTS 5
// Each statement's two output slots (its value's, and its live values' under it), then each object's.
#define BUSTER_TIER0_TEST_VECTOR_OUTPUTS (2 * BUSTER_TIER0_TEST_VECTOR_STATEMENTS + BUSTER_TIER0_TEST_VECTOR_OBJECTS)
#define BUSTER_TIER0_TEST_VECTOR_IN 8
#define BUSTER_TIER0_TEST_VECTOR_INTS 16
#define BUSTER_TIER0_TEST_VECTOR_FLOATS 16
#define BUSTER_TIER0_TEST_VECTOR_ENTRIES 4096
#define BUSTER_TIER0_TEST_VECTOR_CONSTANTS 128
#define BUSTER_TIER0_TEST_VECTOR_TASKS 512
#define BUSTER_TIER0_TEST_VECTOR_PATCHES 128

// The values the generator makes and the rows take: vectors of 16, 32, 64, 8 and 4 bytes (the first five types, each
// with a frame object of its own), integers of four classes, floats, and three kinds of U64 a row takes as an address:
// one of an input's 64 bytes, one of an output slot's, and the inputs' first byte (a gather's base), with a gather's
// indices: a 64-byte vector of dwords below 128, which index the inputs.
#define BUSTER_TIER0_TEST_VECTOR_V16 0
#define BUSTER_TIER0_TEST_VECTOR_V32 1
#define BUSTER_TIER0_TEST_VECTOR_V64 2
#define BUSTER_TIER0_TEST_VECTOR_V8 3
#define BUSTER_TIER0_TEST_VECTOR_V4 4
#define BUSTER_TIER0_TEST_VECTOR_I32 5
#define BUSTER_TIER0_TEST_VECTOR_U16 6
#define BUSTER_TIER0_TEST_VECTOR_U32 7
#define BUSTER_TIER0_TEST_VECTOR_U64 8
#define BUSTER_TIER0_TEST_VECTOR_F32 9
#define BUSTER_TIER0_TEST_VECTOR_INPUT 10
#define BUSTER_TIER0_TEST_VECTOR_OUTPUT 11
#define BUSTER_TIER0_TEST_VECTOR_BASE 12
#define BUSTER_TIER0_TEST_VECTOR_INDICES 13
#define BUSTER_TIER0_TEST_VECTOR_NONE 14

static uint32_t tier0_test_vector_type_class(uint32_t type)
{
    static const uint8_t classes[BUSTER_TIER0_TEST_VECTOR_NONE + 1] = {
        BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VECTOR,
        BUSTER_TYPED_CLASS_I32,    BUSTER_TYPED_CLASS_U16,    BUSTER_TYPED_CLASS_U32,    BUSTER_TYPED_CLASS_U64,    BUSTER_TYPED_CLASS_F32,
        BUSTER_TYPED_CLASS_U64,    BUSTER_TYPED_CLASS_U64,    BUSTER_TYPED_CLASS_U64,    BUSTER_TYPED_CLASS_VECTOR, BUSTER_TYPED_CLASS_VOID,
    };
    return classes[type];
}

// A vector type's bytes (0 for the others).
static uint32_t tier0_test_vector_type_bytes(uint32_t type)
{
    static const uint8_t bytes[BUSTER_TIER0_TEST_VECTOR_NONE + 1] = { 16, 32, 64, 8, 4, [BUSTER_TIER0_TEST_VECTOR_INDICES] = 64 };
    return bytes[type];
}

// The rows this file's programs and fixtures use, each a Tier0Operation built by tier0_test_vector_row, modelled by
// tier0_test_vector_model:
#define BUSTER_TIER0_TEST_VECTOR_ADD_D 0
#define BUSTER_TIER0_TEST_VECTOR_SUB_B 1
#define BUSTER_TIER0_TEST_VECTOR_XOR_Q 2
#define BUSTER_TIER0_TEST_VECTOR_MASK_ADD 3
#define BUSTER_TIER0_TEST_VECTOR_EQUAL_B 4
#define BUSTER_TIER0_TEST_VECTOR_SPLAT_D 5
#define BUSTER_TIER0_TEST_VECTOR_LOW_D 6
#define BUSTER_TIER0_TEST_VECTOR_SPLAT_S 7
#define BUSTER_TIER0_TEST_VECTOR_LOW_S 8
#define BUSTER_TIER0_TEST_VECTOR_BLEND_B 9
#define BUSTER_TIER0_TEST_VECTOR_MASK_LOAD 10
#define BUSTER_TIER0_TEST_VECTOR_NOT_LOW 11
#define BUSTER_TIER0_TEST_VECTOR_TERNARY 12
#define BUSTER_TIER0_TEST_VECTOR_PERMUTE_B 13
#define BUSTER_TIER0_TEST_VECTOR_SHIFT_D 14
#define BUSTER_TIER0_TEST_VECTOR_LOW_HALF 15
#define BUSTER_TIER0_TEST_VECTOR_ADD_S 16
#define BUSTER_TIER0_TEST_VECTOR_MASK_STORE 17
#define BUSTER_TIER0_TEST_VECTOR_GATHER 18
#define BUSTER_TIER0_TEST_VECTOR_SUB_Q 19
#define BUSTER_TIER0_TEST_VECTOR_ADD_W8 20
#define BUSTER_TIER0_TEST_VECTOR_XOR_4 21
#define BUSTER_TIER0_TEST_VECTOR_ADD_Q8 22
#define BUSTER_TIER0_TEST_VECTOR_EXTEND_B4 23
#define BUSTER_TIER0_TEST_VECTOR_LOW_8 24
#define BUSTER_TIER0_TEST_VECTOR_IN_8 25
#define BUSTER_TIER0_TEST_VECTOR_OUT_8 26
#define BUSTER_TIER0_TEST_VECTOR_OUT_4 27
#define BUSTER_TIER0_TEST_VECTOR_KINDS 28

// A row's shape, as this file has it apart from the row: its operands' and result's types, one past the highest VECTOR
// register its VEX forms name (which the placement fixtures' expectations take), and whether random programs use it
// (ADD_S's float sums of random bits would compare NaNs' payloads; MASK_STORE is a statement of its own).
typedef struct Tier0TestVectorKind Tier0TestVectorKind;
struct Tier0TestVectorKind
{
    const char* name;
    uint8_t operands[4];
    uint8_t operand_count;
    uint8_t result;
    uint8_t vex_reach;
    bool random;
};

#define BUSTER_TIER0_TEST_VECTOR_KIND(name, a, b, c, d, n, result, vex, random)                                                                \
    {                                                                                                                                     \
        name, { BUSTER_TIER0_TEST_VECTOR_##a, BUSTER_TIER0_TEST_VECTOR_##b, BUSTER_TIER0_TEST_VECTOR_##c, BUSTER_TIER0_TEST_VECTOR_##d }, n, \
            BUSTER_TIER0_TEST_VECTOR_##result, vex, random                                                                                \
    }
static const Tier0TestVectorKind tier0_test_vector_kinds[BUSTER_TIER0_TEST_VECTOR_KINDS] = {
    BUSTER_TIER0_TEST_VECTOR_KIND("vpaddd zmm", V64, V64, NONE, NONE, 2, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpsubb ymm (VEX)", V32, V32, NONE, NONE, 2, V32, 2, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpxorq xmm", V16, V16, NONE, NONE, 2, V16, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("kmovw, vpaddd zmm{k}", V64, U32, V64, V64, 4, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpcmpeqb k, kmovq r64", V64, V64, NONE, NONE, 2, U64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpbroadcastd zmm, r32", I32, NONE, NONE, NONE, 1, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovd r32, xmm", V16, NONE, NONE, NONE, 1, I32, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vbroadcastss zmm, xmm", F32, NONE, NONE, NONE, 1, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovaps xmm (VEX), a float", V16, NONE, NONE, NONE, 1, F32, 1, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpblendvb ymm (VEX, /is4)", V32, V32, V32, NONE, 3, V32, 3, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("kmovq, vmovdqu8 zmm{k}{z}, [r]", INPUT, U64, NONE, NONE, 2, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovd scratch, andn", V16, U32, NONE, NONE, 2, U32, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpternlogd zmm", V64, V64, V64, NONE, 3, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpermb zmm", V64, V64, NONE, NONE, 2, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovd xmm, r32, vpsrld zmm", V64, U32, NONE, NONE, 2, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("no records (the low half)", V64, NONE, NONE, NONE, 1, V32, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vaddps zmm", V64, V64, NONE, NONE, 2, V64, 0, false),
    BUSTER_TIER0_TEST_VECTOR_KIND("kmovw, vmovdqu32 [r]{k}", OUTPUT, U32, V64, NONE, 3, NONE, 0, false),
    BUSTER_TIER0_TEST_VECTOR_KIND("kxnorw, vpgatherdd zmm{k}", BASE, INDICES, NONE, NONE, 2, V64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpsubq xmm", V16, V16, NONE, NONE, 2, V16, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpaddw xmm, 8 bytes", V8, V8, NONE, NONE, 2, V8, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpxord xmm, 4 bytes", V4, V4, NONE, NONE, 2, V4, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpaddq xmm, 8 bytes to 16", V8, V8, NONE, NONE, 2, V16, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vpmovzxbd xmm, 4 bytes to 16", V4, NONE, NONE, NONE, 1, V16, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("no records (the low 8 bytes)", V16, NONE, NONE, NONE, 1, V8, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovq xmm, r64", U64, NONE, NONE, NONE, 1, V8, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovq r64, xmm", V8, NONE, NONE, NONE, 1, U64, 0, true),
    BUSTER_TIER0_TEST_VECTOR_KIND("vmovd r32, xmm, 4 bytes", V4, NONE, NONE, NONE, 1, U32, 0, true),
};
#undef BUSTER_TIER0_TEST_VECTOR_KIND

static X86Instruction tier0_test_vector_record(uint32_t form, uint8_t reg, uint8_t vvvv, uint8_t rm)
{
    return (X86Instruction){ .form = (uint16_t)form, .reg = reg, .vvvv = vvvv, .rm = rm, .mode = BUSTER_X86_REGISTER_DIRECT };
}

// Kind k's row: its records with their roles, and its operands and result as tier0_test_vector_kinds has them.
static Tier0Operation tier0_test_vector_row(uint32_t k)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint8_t v3 = BUSTER_TIER0_ROLE_VECTOR(3);
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    X86Instruction* r = row.records;
    uint32_t count = 1;
    switch (k)
    {
    case BUSTER_TIER0_TEST_VECTOR_ADD_D: r[0] = tier0_test_vector_record(X86_FORM_VPADDD_Z_Z_ZM512, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_SUB_B: r[0] = tier0_test_vector_record(X86_FORM_VPSUBB_Y_Y_YM256, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_XOR_Q: r[0] = tier0_test_vector_record(X86_FORM_VPXORQ_X_X_XM128, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_ADD:
        // W, a mask, A and B: W's elements where the mask's bits are clear, A + B's where they are set.
        r[0] = tier0_test_vector_record(X86_FORM_KMOVW_K_R32, 1, 0, BUSTER_TIER0_ROLE_INTEGER(1));
        r[1] = tier0_test_vector_record(X86_FORM_VPADDD_Z_Z_ZM512, v0, v2, v3);
        r[1].decorations = 1;
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_EQUAL_B:
        r[0] = tier0_test_vector_record(X86_FORM_VPCMPEQB_K_Z_ZM512, 2, v0, v1);
        r[1] = tier0_test_vector_record(X86_FORM_KMOVQ_R64_K, BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, 2);
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_SPLAT_D: r[0] = tier0_test_vector_record(X86_FORM_VPBROADCASTD_Z_R32, v0, 0, BUSTER_TIER0_ROLE_INTEGER(0)); break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_D: r[0] = tier0_test_vector_record(X86_FORM_VMOVD_RM32_X_EVEX, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT); break;
    case BUSTER_TIER0_TEST_VECTOR_SPLAT_S: r[0] = tier0_test_vector_record(X86_FORM_VBROADCASTSS_Z_XM32, v0, 0, BUSTER_TIER0_ROLE_FLOATING(0)); break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_S: r[0] = tier0_test_vector_record(X86_FORM_VMOVAPS_X_XM128, BUSTER_TIER0_ROLE_FLOATING_RESULT, 0, v0); break;
    case BUSTER_TIER0_TEST_VECTOR_BLEND_B:
        // A, B and a mask M: B's bytes where M's have their top bit, A's elsewhere; M in the immediate's bits 7 to 4.
        r[0] = tier0_test_vector_record(X86_FORM_VPBLENDVB_Y_Y_YM256_Y, v0, v0, v1);
        r[0].immediate = 2 << 4;
        break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_LOAD:
        r[0] = tier0_test_vector_record(X86_FORM_KMOVQ_K_R64, 3, 0, BUSTER_TIER0_ROLE_INTEGER(1));
        r[1] = (X86Instruction){ .form = X86_FORM_VMOVDQU8_Z_ZM512, .reg = v0, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE,
                                 .decorations = 3 | BUSTER_X86_DECORATION_ZEROING };
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_NOT_LOW:
        // ~(the vector's low dword) & y, through a scratch register.
        r[0] = tier0_test_vector_record(X86_FORM_VMOVD_RM32_X_EVEX, v0, 0, BUSTER_TIER0_ROLE_SCRATCH(0));
        r[1] = tier0_test_vector_record(X86_FORM_ANDN_R32_R32_RM32, BUSTER_TIER0_ROLE_INTEGER_RESULT, BUSTER_TIER0_ROLE_SCRATCH(0), BUSTER_TIER0_ROLE_INTEGER(1));
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_TERNARY:
        r[0] = tier0_test_vector_record(X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8, v0, v1, v2);
        r[0].immediate = 0x96;
        break;
    case BUSTER_TIER0_TEST_VECTOR_PERMUTE_B: r[0] = tier0_test_vector_record(X86_FORM_VPERMB_Z_Z_ZM512, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_SHIFT_D:
        // The count into VECTOR(1), which no VECTOR operand takes.
        r[0] = tier0_test_vector_record(X86_FORM_VMOVD_X_RM32_EVEX, v1, 0, BUSTER_TIER0_ROLE_INTEGER(1));
        r[1] = tier0_test_vector_record(X86_FORM_VPSRLD_Z_Z_XM128, v0, v0, v1);
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_HALF: count = 0; break;
    case BUSTER_TIER0_TEST_VECTOR_ADD_S: r[0] = tier0_test_vector_record(X86_FORM_VADDPS_Z_Z_ZM512, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_STORE:
        r[0] = tier0_test_vector_record(X86_FORM_KMOVW_K_R32, 4, 0, BUSTER_TIER0_ROLE_INTEGER(1));
        r[1] = (X86Instruction){ .form = X86_FORM_VMOVDQU32_ZM512_Z, .reg = v2, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = 4 };
        count = 2;
        break;
    case BUSTER_TIER0_TEST_VECTOR_GATHER:
        r[0] = tier0_test_vector_record(X86_FORM_KXNORW_K_K_K, 5, 5, 5);
        r[1] = (X86Instruction){ .form = X86_FORM_VPGATHERDD_Z_VM32Z, .reg = v0, .rm = BUSTER_TIER0_ROLE_INTEGER(0), .index = v1,
                                 .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 5 };
        count = 2;
        break;
    // Narrow vectors in XMM forms, their registers' bytes past them zero: VPADDQ's second qwords and VPMOVZXBD's dwords
    // past the first 4 bytes show it.
    case BUSTER_TIER0_TEST_VECTOR_ADD_W8: r[0] = tier0_test_vector_record(X86_FORM_VPADDW_X_X_XM128_EVEX, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_XOR_4: r[0] = tier0_test_vector_record(X86_FORM_VPXORD_X_X_XM128, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_ADD_Q8: r[0] = tier0_test_vector_record(X86_FORM_VPADDQ_X_X_XM128_EVEX, v0, v0, v1); break;
    case BUSTER_TIER0_TEST_VECTOR_EXTEND_B4: r[0] = tier0_test_vector_record(X86_FORM_VPMOVZXBD_X_XM32_EVEX, v0, 0, v0); break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_8: count = 0; break;
    case BUSTER_TIER0_TEST_VECTOR_IN_8: r[0] = tier0_test_vector_record(X86_FORM_VMOVQ_X_RM64_EVEX, v0, 0, BUSTER_TIER0_ROLE_INTEGER(0)); break;
    case BUSTER_TIER0_TEST_VECTOR_OUT_8: r[0] = tier0_test_vector_record(X86_FORM_VMOVQ_RM64_X_EVEX, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT); break;
    case BUSTER_TIER0_TEST_VECTOR_OUT_4: r[0] = tier0_test_vector_record(X86_FORM_VMOVD_RM32_X_EVEX, v0, 0, BUSTER_TIER0_ROLE_INTEGER_RESULT); break;
    default: r[0] = tier0_test_vector_record(X86_FORM_VPSUBQ_X_X_XM128_EVEX, v0, v0, v1); break;
    }
    (void)v3;
    row.record_count = (uint8_t)count;
    const Tier0TestVectorKind* kind = &tier0_test_vector_kinds[k];
    row.operand_count = kind->operand_count;
    for (uint32_t j = 0; j < kind->operand_count; j += 1)
    {
        row.operand_classes[j] = (uint8_t)tier0_test_vector_type_class(kind->operands[j]);
        row.operand_bytes[j] = (uint8_t)tier0_test_vector_type_bytes(kind->operands[j]);
    }
    row.result_class = (uint8_t)tier0_test_vector_type_class(kind->result);
    row.result_bytes = (uint8_t)tier0_test_vector_type_bytes(kind->result);
    return row;
}

// The rows of every kind, in kind order: every function of this file has them all as its operations.
static Tier0Operation tier0_test_vector_rows[BUSTER_TIER0_TEST_VECTOR_KINDS];

static void tier0_test_vector_build_rows(void)
{
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_KINDS; k += 1)
    {
        tier0_test_vector_rows[k] = tier0_test_vector_row(k);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The model

// A value of the stack machine: a vector's bytes, an integer's (its class's bits, sign- or zero-extended to 64, little-
// endian) or a float's bits, from bytes[0].
typedef struct Tier0TestVectorValue Tier0TestVectorValue;
struct Tier0TestVectorValue
{
    uint8_t bytes[64];
};

static uint32_t tier0_test_vector_dword(const Tier0TestVectorValue* value, uint32_t i)
{
    uint32_t word = 0;
    memcpy(&word, value->bytes + 4 * i, 4);
    return word;
}

static void tier0_test_vector_set_dword(Tier0TestVectorValue* value, uint32_t i, uint32_t word)
{
    memcpy(value->bytes + 4 * i, &word, 4);
}

static uint64_t tier0_test_vector_bits(const Tier0TestVectorValue* value)
{
    uint64_t bits = 0;
    memcpy(&bits, value->bytes, 8);
    return bits;
}

static Tier0TestVectorValue tier0_test_vector_integer(uint32_t class, uint64_t bits)
{
    Tier0TestVectorValue value;
    memset(&value, 0, sizeof(value));
    const uint64_t normal = tier0_test_x87_integer(class, bits);
    memcpy(value.bytes, &normal, 8);
    return value;
}

// Kind k's result from its operands, in plain C: an element at a time, as the instructions' definitions have them.
static Tier0TestVectorValue tier0_test_vector_model(uint32_t k, const Tier0TestVectorValue* operands)
{
    Tier0TestVectorValue result;
    memset(&result, 0, sizeof(result));
    const Tier0TestVectorValue* a = &operands[0];
    const Tier0TestVectorValue* b = &operands[1];
    const Tier0TestVectorValue* c = &operands[2];
    switch (k)
    {
    case BUSTER_TIER0_TEST_VECTOR_ADD_D:
        for (uint32_t i = 0; i < 16; i += 1)
        {
            tier0_test_vector_set_dword(&result, i, tier0_test_vector_dword(a, i) + tier0_test_vector_dword(b, i));
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_SUB_B:
        for (uint32_t i = 0; i < 32; i += 1)
        {
            result.bytes[i] = (uint8_t)(a->bytes[i] - b->bytes[i]);
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_XOR_Q:
        for (uint32_t i = 0; i < 16; i += 1)
        {
            result.bytes[i] = a->bytes[i] ^ b->bytes[i];
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_ADD:
    {
        const uint64_t mask = tier0_test_vector_bits(b);
        for (uint32_t i = 0; i < 16; i += 1)
        {
            const uint32_t sum = tier0_test_vector_dword(c, i) + tier0_test_vector_dword(&operands[3], i);
            tier0_test_vector_set_dword(&result, i, (mask >> i & 1) != 0 ? sum : tier0_test_vector_dword(a, i));
        }
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_EQUAL_B:
    {
        uint64_t mask = 0;
        for (uint32_t i = 0; i < 64; i += 1)
        {
            mask |= a->bytes[i] == b->bytes[i] ? 1ull << i : 0;
        }
        result = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U64, mask);
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_SPLAT_D:
    case BUSTER_TIER0_TEST_VECTOR_SPLAT_S:
        for (uint32_t i = 0; i < 16; i += 1)
        {
            tier0_test_vector_set_dword(&result, i, tier0_test_vector_dword(a, 0));
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_D: result = tier0_test_vector_integer(BUSTER_TYPED_CLASS_I32, tier0_test_vector_dword(a, 0)); break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_S: tier0_test_vector_set_dword(&result, 0, tier0_test_vector_dword(a, 0)); break;
    case BUSTER_TIER0_TEST_VECTOR_BLEND_B:
        for (uint32_t i = 0; i < 32; i += 1)
        {
            result.bytes[i] = (c->bytes[i] & 0x80) != 0 ? b->bytes[i] : a->bytes[i];
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_LOAD:
    {
        const uint8_t* address = (const uint8_t*)(uintptr_t)tier0_test_vector_bits(a);
        const uint64_t mask = tier0_test_vector_bits(b);
        for (uint32_t i = 0; i < 64; i += 1)
        {
            result.bytes[i] = (mask >> i & 1) != 0 ? address[i] : 0;
        }
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_NOT_LOW:
        result = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U32, ~tier0_test_vector_dword(a, 0) & (uint32_t)tier0_test_vector_bits(b));
        break;
    case BUSTER_TIER0_TEST_VECTOR_TERNARY:
        for (uint32_t i = 0; i < 64; i += 1)
        {
            result.bytes[i] = a->bytes[i] ^ b->bytes[i] ^ c->bytes[i];
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_PERMUTE_B:
        for (uint32_t i = 0; i < 64; i += 1)
        {
            result.bytes[i] = b->bytes[a->bytes[i] & 63];
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_SHIFT_D:
    {
        const uint32_t count = (uint32_t)tier0_test_vector_bits(b);
        for (uint32_t i = 0; i < 16; i += 1)
        {
            tier0_test_vector_set_dword(&result, i, count < 32 ? tier0_test_vector_dword(a, i) >> count : 0);
        }
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_LOW_HALF: memcpy(result.bytes, a->bytes, 32); break;
    case BUSTER_TIER0_TEST_VECTOR_ADD_S:
        for (uint32_t i = 0; i < 16; i += 1)
        {
            float x = 0;
            float y = 0;
            memcpy(&x, a->bytes + 4 * i, 4);
            memcpy(&y, b->bytes + 4 * i, 4);
            const float sum = x + y;
            memcpy(result.bytes + 4 * i, &sum, 4);
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_MASK_STORE:
    {
        uint8_t* address = (uint8_t*)(uintptr_t)tier0_test_vector_bits(a);
        const uint64_t mask = tier0_test_vector_bits(b);
        for (uint32_t i = 0; i < 16; i += 1)
        {
            if ((mask >> i & 1) != 0)
            {
                memcpy(address + 4 * i, c->bytes + 4 * i, 4);
            }
        }
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_GATHER:
    {
        const uint8_t* base = (const uint8_t*)(uintptr_t)tier0_test_vector_bits(a);
        for (uint32_t i = 0; i < 16; i += 1)
        {
            uint32_t word = 0;
            memcpy(&word, base + 4 * (uint64_t)tier0_test_vector_dword(b, i), 4);
            tier0_test_vector_set_dword(&result, i, word);
        }
        break;
    }
    case BUSTER_TIER0_TEST_VECTOR_ADD_W8:
        for (uint32_t i = 0; i < 4; i += 1)
        {
            uint16_t x = 0;
            uint16_t y = 0;
            memcpy(&x, a->bytes + 2 * i, 2);
            memcpy(&y, b->bytes + 2 * i, 2);
            const uint16_t sum = (uint16_t)(x + y);
            memcpy(result.bytes + 2 * i, &sum, 2);
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_XOR_4:
        for (uint32_t i = 0; i < 4; i += 1)
        {
            result.bytes[i] = a->bytes[i] ^ b->bytes[i];
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_EXTEND_B4:
        for (uint32_t i = 0; i < 4; i += 1)
        {
            tier0_test_vector_set_dword(&result, i, a->bytes[i]);
        }
        break;
    case BUSTER_TIER0_TEST_VECTOR_LOW_8:
    case BUSTER_TIER0_TEST_VECTOR_IN_8: memcpy(result.bytes, a->bytes, 8); break;
    case BUSTER_TIER0_TEST_VECTOR_OUT_8: result = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U64, tier0_test_vector_bits(a)); break;
    case BUSTER_TIER0_TEST_VECTOR_OUT_4: result = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U32, tier0_test_vector_dword(a, 0)); break;
    default:
        // SUB_Q's differences, and ADD_Q8's sums (its operands' second qwords, past their 8 bytes, are zero).
        for (uint32_t i = 0; i < 2; i += 1)
        {
            uint64_t x = 0;
            uint64_t y = 0;
            memcpy(&x, a->bytes + 8 * i, 8);
            memcpy(&y, b->bytes + 8 * i, 8);
            const uint64_t value = k == BUSTER_TIER0_TEST_VECTOR_ADD_Q8 ? x + y : x - y;
            memcpy(result.bytes + 8 * i, &value, 8);
        }
        break;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The programs

// A program's stream, its vector constants, and its ?:'s patches.
typedef struct Tier0TestVectorProgram Tier0TestVectorProgram;
struct Tier0TestVectorProgram
{
    uint8_t kinds[BUSTER_TIER0_TEST_VECTOR_ENTRIES];
    uint8_t classes[BUSTER_TIER0_TEST_VECTOR_ENTRIES];
    uint64_t values[BUSTER_TIER0_TEST_VECTOR_ENTRIES];
    uint32_t count;
    uint64_t constants[8 * BUSTER_TIER0_TEST_VECTOR_CONSTANTS];
    uint8_t constant_bytes[BUSTER_TIER0_TEST_VECTOR_CONSTANTS];
    uint32_t constant_count;
    uint32_t patches[BUSTER_TIER0_TEST_VECTOR_PATCHES];
    uint32_t patch_count;
    bool overflow;
};

static void tier0_test_vector_emit(Tier0TestVectorProgram* program, uint32_t kind, uint32_t class, uint64_t value)
{
    if (program->count < BUSTER_TIER0_TEST_VECTOR_ENTRIES)
    {
        program->kinds[program->count] = (uint8_t)kind;
        program->classes[program->count] = (uint8_t)class;
        program->values[program->count] = value;
        program->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

// Byte `offset` of parameter `parameter`'s memory (in, ints, floats or out).
static void tier0_test_vector_address(Tier0TestVectorProgram* program, uint32_t parameter, uint64_t offset)
{
    tier0_test_vector_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, parameter);
    tier0_test_vector_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, offset);
    tier0_test_vector_emit(program, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
}

// The last operand's class is the entry before's (tier0_validate's rule): a DUP and DISCARD of `class` where the entry
// before has another, as a front end's cancelling pair restores it.
static void tier0_test_vector_adjacent(Tier0TestVectorProgram* program, uint32_t class)
{
    if (program->count != 0 && program->classes[program->count - 1] != class)
    {
        tier0_test_vector_emit(program, BUSTER_TYPED_DUP, class, 0);
        tier0_test_vector_emit(program, BUSTER_TYPED_DISCARD, class, 0);
    }
}

// A random 64-bit word: now and then a boundary's (0, 1, -1, the extremes of 32 bits and their neighbours), else random
// bits, some of whose words fit a sign-extended imm32.
static uint64_t tier0_test_vector_word(uint64_t* state)
{
    static const uint64_t boundaries[8] = { 0, 1, ~0ull, 0x7fffffffull, 0xffffffff80000000ull, 0x80000000ull, 0xffffffffull, 0x0101010101010101ull };
    const uint32_t shape = tier0_generate_below(state, 4);
    return shape == 0 ? boundaries[tier0_generate_below(state, 8)] : shape == 1 ? (uint64_t)(int64_t)(int32_t)tier0_generate_random(state) : tier0_generate_random(state);
}

// A new constant of `bytes` (all zeros now and then, which vpxord makes), and its CONST.
static void tier0_test_vector_constant(Tier0TestVectorProgram* program, uint32_t bytes, uint64_t* state, bool indices)
{
    if (program->constant_count >= BUSTER_TIER0_TEST_VECTOR_CONSTANTS)
    {
        program->overflow = true;
    }
    else
    {
        const uint32_t k = program->constant_count;
        program->constant_count += 1;
        const bool zero = !indices && tier0_generate_chance(state, 15);
        for (uint32_t w = 0; w < 8; w += 1)
        {
            // A gather's indices: dwords below 128, which index the inputs' 512 bytes.
            // A 4-byte constant's word has its low half alone.
            const uint64_t random = indices ? (tier0_generate_random(state) & 0x0000007f0000007full) : tier0_test_vector_word(state);
            const uint64_t kept = bytes - 8 * w >= 8 ? random : random & 0xffffffffull;
            program->constants[8 * k + w] = zero || 8 * w >= bytes ? 0 : kept;
        }
        program->constant_bytes[k] = (uint8_t)bytes;
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, k);
    }
}

// The generator's tasks, popped from a stack: an expression to make (of a type, of a budget of nodes), an entry to emit
// once its operands are, and ?:'s branch, jump and labels (`value` its patch).
#define BUSTER_TIER0_TEST_VECTOR_TASK_EXPRESSION 0
#define BUSTER_TIER0_TEST_VECTOR_TASK_ENTRY 1
#define BUSTER_TIER0_TEST_VECTOR_TASK_BRANCH 2
#define BUSTER_TIER0_TEST_VECTOR_TASK_JUMP 3
#define BUSTER_TIER0_TEST_VECTOR_TASK_LABEL 4

typedef struct Tier0TestVectorTask Tier0TestVectorTask;
struct Tier0TestVectorTask
{
    uint8_t task;
    uint8_t kind;
    uint8_t class;
    uint8_t type;
    uint32_t budget;
    uint64_t value;
};

typedef struct Tier0TestVectorTasks Tier0TestVectorTasks;
struct Tier0TestVectorTasks
{
    Tier0TestVectorTask items[BUSTER_TIER0_TEST_VECTOR_TASKS];
    uint32_t count;
};

static void tier0_test_vector_push(Tier0TestVectorProgram* program, Tier0TestVectorTasks* tasks, Tier0TestVectorTask task)
{
    if (tasks->count < BUSTER_TIER0_TEST_VECTOR_TASKS)
    {
        tasks->items[tasks->count] = task;
        tasks->count += 1;
    }
    else
    {
        program->overflow = true;
    }
}

static Tier0TestVectorTask tier0_test_vector_entry(uint32_t kind, uint32_t class, uint64_t value)
{
    return (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_ENTRY, .kind = (uint8_t)kind, .class = (uint8_t)class, .value = value };
}

static Tier0TestVectorTask tier0_test_vector_expression(uint32_t type, uint32_t budget)
{
    return (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_EXPRESSION, .type = (uint8_t)type, .budget = budget };
}

// A leaf of `type`: a vector from an input, a constant or the frame object of its bytes; an integer from the integers or
// a constant; a float from the floats or a constant; an address of an input's or an output slot's bytes, or the inputs'
// first; a gather's indices, a constant of them.
static void tier0_test_vector_leaf(Tier0TestVectorProgram* program, uint32_t type, uint64_t* state)
{
    const uint32_t class = tier0_test_vector_type_class(type);
    const uint32_t bytes = tier0_test_vector_type_bytes(type);
    const uint32_t choice = tier0_generate_below(state, 3);
    if (type == BUSTER_TIER0_TEST_VECTOR_INDICES)
    {
        tier0_test_vector_constant(program, 64, state, true);
    }
    else if (class == BUSTER_TYPED_CLASS_VECTOR && choice == 0)
    {
        tier0_test_vector_address(program, 0, 64 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_IN));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
    }
    else if (class == BUSTER_TYPED_CLASS_VECTOR && choice == 1)
    {
        tier0_test_vector_constant(program, bytes, state, false);
    }
    else if (class == BUSTER_TYPED_CLASS_VECTOR)
    {
        tier0_test_vector_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 4 + type);
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_INPUT || type == BUSTER_TIER0_TEST_VECTOR_BASE)
    {
        tier0_test_vector_address(program, 0, type == BUSTER_TIER0_TEST_VECTOR_BASE ? 0 : 64 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_IN));
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_F32 && choice != 2)
    {
        tier0_test_vector_address(program, 2, 4 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_FLOATS));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F32, 0);
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_F32)
    {
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, (uint32_t)tier0_generate_random(state));
    }
    else if (choice != 2)
    {
        tier0_test_vector_address(program, 1, 8 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_INTS));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, class, 0);
    }
    else
    {
        // A shift's count is below 32 now and then, so that its result isn't zero.
        const uint64_t word = tier0_test_vector_word(state);
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, class, type == BUSTER_TIER0_TEST_VECTOR_U32 && tier0_generate_chance(state, 50) ? word & 31 : word);
    }
}

// Runs the task stack until it is empty: each expression becomes a leaf (its budget spent, or by chance) or an X86 entry
// of a row whose result is its type, with its operands new tasks pushed after it in reverse, so that they come out in
// postorder; a vector's also a DUP's twice-used operand, a frame object's STORE_LOCAL, an output's STORE or a ?:; an
// integer's also a conversion of another integer class's row.
static void tier0_test_vector_run_tasks(Tier0TestVectorProgram* program, Tier0TestVectorTasks* tasks, uint64_t* state)
{
    while (tasks->count != 0 && !program->overflow)
    {
        tasks->count -= 1;
        const Tier0TestVectorTask task = tasks->items[tasks->count];
        if (task.task == BUSTER_TIER0_TEST_VECTOR_TASK_ENTRY)
        {
            tier0_test_vector_emit(program, task.kind, task.class, task.value);
        }
        else if (task.task == BUSTER_TIER0_TEST_VECTOR_TASK_BRANCH || task.task == BUSTER_TIER0_TEST_VECTOR_TASK_JUMP)
        {
            const bool branch = task.task == BUSTER_TIER0_TEST_VECTOR_TASK_BRANCH;
            program->patches[task.value] = program->count;
            tier0_test_vector_emit(program, branch ? BUSTER_TYPED_BRANCH_FALSE : BUSTER_TYPED_JUMP_VALUE, branch ? BUSTER_TYPED_CLASS_I32 : task.class, 0);
        }
        else if (task.task == BUSTER_TIER0_TEST_VECTOR_TASK_LABEL)
        {
            program->values[program->patches[task.value]] = program->count;
        }
        else
        {
            const uint32_t type = task.type;
            const uint32_t class = tier0_test_vector_type_class(type);
            const uint32_t bytes = tier0_test_vector_type_bytes(type);
            const bool fixed = type >= BUSTER_TIER0_TEST_VECTOR_INPUT;
            // The rows whose result is the type, and for an integer the integer rows (converted).
            uint32_t candidates[BUSTER_TIER0_TEST_VECTOR_KINDS];
            uint32_t candidate_count = 0;
            const bool integer = tier0_class_integer(class) && !fixed;
            for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_KINDS && !fixed; k += 1)
            {
                const uint32_t result = tier0_test_vector_kinds[k].result;
                const bool same = result == type || (integer && result != BUSTER_TIER0_TEST_VECTOR_NONE && tier0_class_integer(tier0_test_vector_type_class(result)));
                if (tier0_test_vector_kinds[k].random && same)
                {
                    candidates[candidate_count] = k;
                    candidate_count += 1;
                }
            }
            const uint32_t choice = tier0_generate_below(state, 100);
            const uint32_t budget = task.budget > 1 ? task.budget - 1 : 1;
            if (fixed || task.budget <= 1 || candidate_count == 0 || tier0_generate_chance(state, 20))
            {
                tier0_test_vector_leaf(program, type, state);
            }
            else if (class == BUSTER_TYPED_CLASS_VECTOR && choice < 8)
            {
                // A row of two operands of the type, both the same value: DUP.
                const uint32_t k = bytes == 64   ? BUSTER_TIER0_TEST_VECTOR_ADD_D
                                   : bytes == 32 ? BUSTER_TIER0_TEST_VECTOR_SUB_B
                                   : bytes == 16 ? BUSTER_TIER0_TEST_VECTOR_XOR_Q
                                   : bytes == 8  ? BUSTER_TIER0_TEST_VECTOR_ADD_W8
                                                 : BUSTER_TIER0_TEST_VECTOR_XOR_4;
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, k));
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_VECTOR, 0));
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(type, budget));
            }
            else if (class == BUSTER_TYPED_CLASS_VECTOR && choice < 14)
            {
                // The value stored into the frame object of its bytes, and kept.
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 4 + type));
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(type, budget));
            }
            else if (class == BUSTER_TYPED_CLASS_VECTOR && choice < 20)
            {
                // The value stored into an output slot (a later statement's, which writes it again, or the last one's), and
                // kept: STORE.
                const uint64_t slot = 2 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_STATEMENTS);
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_STORE, BUSTER_TYPED_CLASS_VECTOR, bytes));
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(type, budget));
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1));
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 64 * slot));
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 3));
            }
            else if (class == BUSTER_TYPED_CLASS_VECTOR && choice < 28 && program->patch_count + 2 <= BUSTER_TIER0_TEST_VECTOR_PATCHES && budget >= 3)
            {
                // ints[k] & 1 ? x : y, the arms' value carried by JUMP_VALUE.
                const uint32_t branch = program->patch_count;
                const uint32_t jump = program->patch_count + 1;
                program->patch_count += 2;
                const uint32_t half = budget / 2 != 0 ? budget / 2 : 1;
                tier0_test_vector_push(program, tasks, (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_LABEL, .value = jump });
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(type, half));
                tier0_test_vector_push(program, tasks, (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_LABEL, .value = branch });
                tier0_test_vector_push(program, tasks,
                                       (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_JUMP, .class = BUSTER_TYPED_CLASS_VECTOR, .value = jump });
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(type, half));
                tier0_test_vector_push(program, tasks, (Tier0TestVectorTask){ .task = BUSTER_TIER0_TEST_VECTOR_TASK_BRANCH, .value = branch });
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0));
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1));
                tier0_test_vector_push(program, tasks, tier0_test_vector_expression(BUSTER_TIER0_TEST_VECTOR_I32, 1));
            }
            else
            {
                const uint32_t k = candidates[tier0_generate_below(state, candidate_count)];
                const Tier0TestVectorKind* kind = &tier0_test_vector_kinds[k];
                const uint32_t result = tier0_test_vector_type_class(kind->result);
                // An integer row of another class than the expression's, converted to it.
                if (result != class)
                {
                    tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_CONVERT, class, 0));
                }
                tier0_test_vector_push(program, tasks, tier0_test_vector_entry(BUSTER_TYPED_X86, result, k));
                const uint32_t share = budget / kind->operand_count != 0 ? budget / kind->operand_count : 1;
                for (uint32_t j = kind->operand_count; j > 0; j -= 1)
                {
                    tier0_test_vector_push(program, tasks, tier0_test_vector_expression(kind->operands[j - 1], share));
                }
            }
        }
    }
}

// The kinds of statements: a value of a type into its output slot (SET), a masked store into it, a vector dropped
// (DISCARD).
static void tier0_test_vector_statement(Tier0TestVectorProgram* program, uint32_t s, uint64_t* state)
{
    static const uint8_t types[10] = {
        BUSTER_TIER0_TEST_VECTOR_V16, BUSTER_TIER0_TEST_VECTOR_V32, BUSTER_TIER0_TEST_VECTOR_V64, BUSTER_TIER0_TEST_VECTOR_V8,  BUSTER_TIER0_TEST_VECTOR_V4,
        BUSTER_TIER0_TEST_VECTOR_V64, BUSTER_TIER0_TEST_VECTOR_I32, BUSTER_TIER0_TEST_VECTOR_U32, BUSTER_TIER0_TEST_VECTOR_U64, BUSTER_TIER0_TEST_VECTOR_F32,
    };
    Tier0TestVectorTasks tasks;
    tasks.count = 0;
    const uint32_t budget = 2 + tier0_generate_below(state, 8);
    const uint32_t shape = tier0_generate_below(state, 10);
    if (shape == 0)
    {
        // kmovw, vmovdqu32 [r]{k}: an output slot's address, a mask (a U16's, extended to the row's U32, since kmovw's
        // field is a 32-bit register's), a vector.
        tier0_test_vector_address(program, 3, 64 * (uint64_t)(2 * s));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_entry(BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VOID, BUSTER_TIER0_TEST_VECTOR_MASK_STORE));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_expression(BUSTER_TIER0_TEST_VECTOR_V64, budget));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_entry(BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U32, 0));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_expression(BUSTER_TIER0_TEST_VECTOR_U16, 2));
        tier0_test_vector_run_tasks(program, &tasks, state);
    }
    else if (shape == 1)
    {
        const uint32_t type = types[tier0_generate_below(state, 5)];
        tier0_test_vector_push(program, &tasks, tier0_test_vector_entry(BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_VECTOR, 0));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_expression(type, budget));
        tier0_test_vector_run_tasks(program, &tasks, state);
    }
    else
    {
        const uint32_t type = types[tier0_generate_below(state, 10)];
        const uint32_t class = tier0_test_vector_type_class(type);
        tier0_test_vector_address(program, 3, 64 * (uint64_t)(2 * s));
        tier0_test_vector_push(program, &tasks, tier0_test_vector_expression(type, budget));
        tier0_test_vector_run_tasks(program, &tasks, state);
        tier0_test_vector_adjacent(program, class);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, class, tier0_test_vector_type_bytes(type));
    }
}

// A program: its frame objects given values first (constants, by SET_LOCAL), each statement, with an integer and a float
// live under it now and then (pushed before it, each with its slot's address, and stored into the statement's second
// slot after it), then each object copied out into its own slot (after the statements'), then RETURN.
static void tier0_test_vector_program(Tier0TestVectorProgram* program, uint64_t* state)
{
    memset(program, 0, sizeof(*program));
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_OBJECTS; k += 1)
    {
        tier0_test_vector_constant(program, tier0_test_vector_type_bytes(k), state, false);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 4 + k);
    }
    const uint32_t statements = 1 + tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_STATEMENTS);
    for (uint32_t s = 0; s < statements && !program->overflow; s += 1)
    {
        const bool live = tier0_generate_chance(state, 40);
        const uint64_t slot = 64 * (uint64_t)(2 * s + 1);
        if (live)
        {
            tier0_test_vector_address(program, 3, slot);
            tier0_test_vector_address(program, 1, 8 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_INTS));
            tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_vector_address(program, 3, slot + 8);
            tier0_test_vector_address(program, 2, 4 * (uint64_t)tier0_generate_below(state, BUSTER_TIER0_TEST_VECTOR_FLOATS));
            tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F32, 0);
        }
        tier0_test_vector_statement(program, s, state);
        if (live)
        {
            tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_F32);
            tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F32, 0);
            tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_I64);
            tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0);
        }
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_OBJECTS; k += 1)
    {
        tier0_test_vector_address(program, 3, 64 * (uint64_t)(2 * BUSTER_TIER0_TEST_VECTOR_STATEMENTS + k));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 4 + k);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, tier0_test_vector_type_bytes(k));
    }
    tier0_test_vector_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

// The programs' and fixtures' locals: four pointer parameters (the inputs, the integers, the floats and the outputs) in
// rdi, rsi, rdx and rcx, and the frame objects of 16, 32, 64, 8 and 4 bytes, aligned to 16.
static const uint8_t tier0_test_vector_local_classes[4 + BUSTER_TIER0_TEST_VECTOR_OBJECTS] = {
    BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64,  BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID,
    BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID,
};
static const uint8_t tier0_test_vector_local_flags[4 + BUSTER_TIER0_TEST_VECTOR_OBJECTS] = {
    0, 0, 0, 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
    BUSTER_TYPED_LOCAL_ADDRESS_TAKEN,
};
static const uint32_t tier0_test_vector_local_sizes[4 + BUSTER_TIER0_TEST_VECTOR_OBJECTS] = { 0, 0, 0, 0, 16, 32, 64, 8, 4 };
static const uint8_t tier0_test_vector_local_alignments[4 + BUSTER_TIER0_TEST_VECTOR_OBJECTS] = { 0, 0, 0, 0, 4, 4, 4, 4, 4 };
static const AbiLocation tier0_test_vector_parameters[4] = {
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } },
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE } },
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE } },
    { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RCX, BUSTER_ABI_REGISTER_NONE } },
};

static Tier0Function tier0_test_vector_function(const Tier0TestVectorProgram* program)
{
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = program->kinds;
    function.classes = program->classes;
    function.values = program->values;
    function.count = program->count;
    function.local_classes = tier0_test_vector_local_classes;
    function.local_flags = tier0_test_vector_local_flags;
    function.local_sizes = tier0_test_vector_local_sizes;
    function.local_alignments = tier0_test_vector_local_alignments;
    function.local_count = 4 + BUSTER_TIER0_TEST_VECTOR_OBJECTS;
    function.parameter_count = 4;
    function.parameters = tier0_test_vector_parameters;
    function.vector_constants = program->constants;
    function.vector_constant_bytes = program->constant_bytes;
    function.vector_constant_count = program->constant_count;
    function.operations = tier0_test_vector_rows;
    function.operation_count = BUSTER_TIER0_TEST_VECTOR_KINDS;
    function.vectors = true;
    return function;
}

// ---------------------------------------------------------------------------------------------------------------------
// The interpreter

// What the programs reach in the interpreter, which the default count must cover whole. When native execution is
// available, include only programs whose native run matched it. A bit per VECTOR kind and width, a bit per row,
// the arms of a ?: of a vector, and a CONST of zeros and one of a word past a sign-extended imm32.
#define BUSTER_TIER0_TEST_VECTOR_WIDTH(bytes) ((bytes) == 16 ? 0u : (bytes) == 32 ? 1u : (bytes) == 64 ? 2u : (bytes) == 8 ? 3u : 4u)
typedef struct Tier0TestVectorReach Tier0TestVectorReach;
struct Tier0TestVectorReach
{
    uint64_t kinds[BUSTER_TIER0_TEST_VECTOR_OBJECTS];
    uint32_t rows;
    uint8_t arms;
    uint8_t constants;
};

#define BUSTER_TIER0_TEST_VECTOR_VISITED_RAN 1
#define BUSTER_TIER0_TEST_VECTOR_VISITED_TAKEN 2
#define BUSTER_TIER0_TEST_VECTOR_VISITED_NOT_TAKEN 4

// The program's stream run as a stack machine on `parameters` (the inputs', the integers', the floats' and the outputs'
// addresses), with the frame objects' bytes of its own, marking the entries it runs and each branch's way, and what it
// reaches into *reach.
static bool tier0_test_vector_interpret(const Tier0Function* function, const uint64_t parameters[4], uint8_t* visited, Tier0TestVectorReach* reach)
{
    uint8_t objects[BUSTER_TIER0_TEST_VECTOR_OBJECTS][64];
    memset(objects, 0, sizeof(objects));
    memset(visited, 0, function->count);
    Tier0TestVectorValue stack[2][48];
    uint32_t depths[2] = { 0, 0 };
    uint32_t i = 0;
    bool valid = true;
    bool done = false;
    while (valid && !done && i < function->count)
    {
        const uint32_t kind = function->kinds[i];
        const uint32_t class = function->classes[i];
        const uint64_t value = function->values[i];
        const bool vector = tier0_class_vector(class);
        const uint32_t s = tier0_class_floating(class) ? 1 : 0;
        uint32_t next = i + 1;
        valid = depths[0] + 2 < 48 && depths[1] + 2 < 48;
        visited[i] |= BUSTER_TIER0_TEST_VECTOR_VISITED_RAN;
        Tier0TestVectorValue* top = &stack[s][depths[s] != 0 ? depths[s] - 1 : 0];
        if (kind == BUSTER_TYPED_CONST && vector)
        {
            memset(&stack[0][depths[0]], 0, sizeof(Tier0TestVectorValue));
            memcpy(stack[0][depths[0]].bytes, &function->vector_constants[8 * value], 64);
            depths[0] += 1;
            const uint32_t bytes = function->vector_constant_bytes[value];
            reach->kinds[BUSTER_TIER0_TEST_VECTOR_WIDTH(bytes)] |= 1ull << kind;
            bool zero = true;
            bool wide = false;
            for (uint32_t w = 0; w < (bytes + 7) / 8; w += 1)
            {
                zero = zero && function->vector_constants[8 * value + w] == 0;
                wide = wide || !tier0_fits_int32((int64_t)function->vector_constants[8 * value + w]);
            }
            reach->constants |= (zero ? 1 : 0) | (wide ? 2 : 0);
        }
        else if (kind == BUSTER_TYPED_CONST)
        {
            stack[s][depths[s]] = tier0_test_vector_integer(s == 1 ? BUSTER_TYPED_CLASS_U32 : class, value);
            depths[s] += 1;
        }
        else if (kind == BUSTER_TYPED_LOCAL && vector)
        {
            memcpy(stack[0][depths[0]].bytes, objects[value - 4], 64);
            depths[0] += 1;
            reach->kinds[value - 4] |= 1ull << kind;
        }
        else if (kind == BUSTER_TYPED_LOCAL)
        {
            stack[0][depths[0]] = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U64, parameters[value]);
            depths[0] += 1;
        }
        else if (kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL)
        {
            memcpy(objects[value - 4], top->bytes, function->local_sizes[value]);
            depths[0] -= kind == BUSTER_TYPED_SET_LOCAL ? 1 : 0;
            reach->kinds[value - 4] |= 1ull << kind;
        }
        else if (kind == BUSTER_TYPED_PTR_ADD)
        {
            const uint64_t address = tier0_test_vector_bits(&stack[0][depths[0] - 2]) + tier0_test_vector_bits(&stack[0][depths[0] - 1]) * value;
            stack[0][depths[0] - 2] = tier0_test_vector_integer(BUSTER_TYPED_CLASS_U64, address);
            depths[0] -= 1;
        }
        else if (kind == BUSTER_TYPED_LOAD)
        {
            const uint8_t* address = (const uint8_t*)(uintptr_t)tier0_test_vector_bits(&stack[0][depths[0] - 1]);
            depths[0] -= 1;
            Tier0TestVectorValue loaded;
            memset(&loaded, 0, sizeof(loaded));
            uint64_t bits = 0;
            memcpy(vector ? (void*)loaded.bytes : (void*)&bits, address, vector ? value : tier0_class_bytes(class));
            stack[s][depths[s]] = vector ? loaded : tier0_test_vector_integer(s == 1 ? BUSTER_TYPED_CLASS_U32 : class, bits);
            depths[s] += 1;
            reach->kinds[BUSTER_TIER0_TEST_VECTOR_WIDTH(value)] |= vector ? 1ull << kind : 0;
        }
        else if (kind == BUSTER_TYPED_SET || kind == BUSTER_TYPED_STORE)
        {
            const Tier0TestVectorValue stored = *top;
            depths[s] -= 1;
            uint8_t* address = (uint8_t*)(uintptr_t)tier0_test_vector_bits(&stack[0][depths[0] - 1]);
            depths[0] -= 1;
            memcpy(address, stored.bytes, vector ? value : tier0_class_bytes(class));
            if (kind == BUSTER_TYPED_STORE)
            {
                stack[s][depths[s]] = stored;
                depths[s] += 1;
            }
            reach->kinds[BUSTER_TIER0_TEST_VECTOR_WIDTH(value)] |= vector ? 1ull << kind : 0;
        }
        else if (kind == BUSTER_TYPED_DUP)
        {
            stack[s][depths[s]] = *top;
            depths[s] += 1;
            reach->kinds[0] |= vector ? 1ull << kind : 0;
        }
        else if (kind == BUSTER_TYPED_DISCARD)
        {
            depths[s] -= 1;
            reach->kinds[0] |= vector ? 1ull << kind : 0;
        }
        else if (kind == BUSTER_TYPED_AND || kind == BUSTER_TYPED_XOR)
        {
            // An integer AND (a ?:'s condition) and XOR (the live integers').
            const uint64_t x = tier0_test_vector_bits(&stack[0][depths[0] - 2]);
            const uint64_t y = tier0_test_vector_bits(&stack[0][depths[0] - 1]);
            stack[0][depths[0] - 2] = tier0_test_vector_integer(class, kind == BUSTER_TYPED_AND ? x & y : x ^ y);
            depths[0] -= 1;
        }
        else if (kind == BUSTER_TYPED_ADD && class == BUSTER_TYPED_CLASS_F32)
        {
            // The live floats' sum, an addss's.
            float x = 0;
            float y = 0;
            memcpy(&x, stack[1][depths[1] - 2].bytes, 4);
            memcpy(&y, stack[1][depths[1] - 1].bytes, 4);
            const float sum = x + y;
            memset(&stack[1][depths[1] - 2], 0, sizeof(Tier0TestVectorValue));
            memcpy(stack[1][depths[1] - 2].bytes, &sum, 4);
            depths[1] -= 1;
        }
        else if (kind == BUSTER_TYPED_CONVERT)
        {
            // A conversion between the integer classes (an integer row's).
            stack[0][depths[0] - 1] = tier0_test_vector_integer(class, tier0_test_vector_bits(&stack[0][depths[0] - 1]));
        }
        else if (kind == BUSTER_TYPED_X86)
        {
            const Tier0Operation* row = &function->operations[value];
            Tier0TestVectorValue operands[BUSTER_TIER0_OPERATION_OPERANDS];
            uint32_t taken[2] = { 0, 0 };
            for (uint32_t j = row->operand_count; j > 0; j -= 1)
            {
                const uint32_t from = tier0_class_floating(row->operand_classes[j - 1]) ? 1 : 0;
                taken[from] += 1;
                operands[j - 1] = stack[from][depths[from] - taken[from]];
                // A VECTOR operand's register has zeros past its bytes, as Tier 0 loads it.
                if (tier0_class_vector(row->operand_classes[j - 1]))
                {
                    memset(operands[j - 1].bytes + row->operand_bytes[j - 1], 0, 64 - row->operand_bytes[j - 1]);
                }
            }
            depths[0] -= taken[0];
            depths[1] -= taken[1];
            const Tier0TestVectorValue result = tier0_test_vector_model((uint32_t)value, operands);
            if (row->result_class != BUSTER_TYPED_CLASS_VOID)
            {
                stack[s][depths[s]] = result;
                depths[s] += 1;
            }
            reach->rows |= 1u << value;
        }
        else if (kind == BUSTER_TYPED_BRANCH_FALSE)
        {
            depths[0] -= 1;
            const bool zero = (uint32_t)tier0_test_vector_bits(&stack[0][depths[0]]) == 0;
            next = zero ? (uint32_t)value : next;
            visited[i] |= zero ? BUSTER_TIER0_TEST_VECTOR_VISITED_TAKEN : BUSTER_TIER0_TEST_VECTOR_VISITED_NOT_TAKEN;
            reach->arms |= zero ? 2 : 1;
        }
        else if (kind == BUSTER_TYPED_JUMP_VALUE)
        {
            next = (uint32_t)value;
            reach->kinds[0] |= vector ? 1ull << kind : 0;
        }
        else if (kind == BUSTER_TYPED_RETURN)
        {
            done = true;
        }
        else
        {
            valid = false;
        }
        i = next;
    }
    return valid && done;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compiling and running

// What the native runs share with the test: the inputs, the outputs, and the aggregates' arguments and results.
typedef struct Tier0TestVectorShared Tier0TestVectorShared;
struct Tier0TestVectorShared
{
    uint8_t in[BUSTER_TIER0_TEST_VECTOR_IN][64] __attribute__((aligned(64)));
    uint64_t ints[BUSTER_TIER0_TEST_VECTOR_INTS];
    uint32_t floats[BUSTER_TIER0_TEST_VECTOR_FLOATS];
    uint8_t out[BUSTER_TIER0_TEST_VECTOR_OUTPUTS][64] __attribute__((aligned(64)));
    uint8_t arguments[12][64] __attribute__((aligned(64)));
    uint8_t results[4][64] __attribute__((aligned(64)));
};

typedef void (*Tier0TestVectorRun)(const void* code, Tier0TestVectorShared* shared);

// Runs `run` on `code` (`length` bytes) in a child, which must exit with status 0: the code mapped executable at a page of
// its own, `run` calling it from C.
// The generated rows include AVX2, BMI and AVX-512 with byte, VL and VBMI forms. The shared native detector includes
// OS save-state support independently of the host build's selected ISA, and remains readable by our own checker.
static bool tier0_test_vector_can_run(void)
{
    const X86Target native = x86_target_native();
    const uint64_t required = BUSTER_X86_FEATURE(AVX2) | BUSTER_X86_FEATURE(BMI) | BUSTER_X86_FEATURE(AVX512F) |
                              BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL) |
                              BUSTER_X86_FEATURE(AVX512VBMI);
    return (native.features & required) == required;
}

static bool tier0_test_vector_native(const uint8_t* code, uint32_t length, Tier0TestVectorRun run, Tier0TestVectorShared* shared)
{
    const pid_t child = fork();
    if (child == 0)
    {
        alarm(BUSTER_TIER0_TEST_RUN_SECONDS);
        uint8_t* page = mmap(0, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
        {
            _exit(3);
        }
        memcpy(page, code, length);
        if (mprotect(page, 4 * BUSTER_TIER0_TEST_PAGE, PROT_READ | PROT_EXEC) != 0)
        {
            _exit(4);
        }
        run(page, shared);
        _exit(0);
    }
    int status = 0;
    const bool waited = child > 0 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void tier0_test_vector_run_body(const void* code, Tier0TestVectorShared* shared)
{
    typedef void (*Body)(const void*, const void*, const void*, void*);
    Body body = 0;
    memcpy(&body, &code, sizeof(body));
    body(shared->in, shared->ints, shared->floats, shared->out);
}

// A function compiled by both paths, which must agree (and tier0_validate take it), its records valid, its branches
// sized, and encoded by both encoders into `image` (the vector one's) and `scratch` (the scalar one's), which must be the
// same bytes. Returns the code's length, 0 for a failure or a refusal, and in *error Tier 0's error: only `allowed` may
// refuse it (TIER0_ERROR_DEPTH where the depths may pass the tables), and every other must compile.
static uint32_t tier0_test_vector_encode(Tier0TestTally* tally, const char* name, const Tier0Function* function, Arena* arena, uint8_t* image, uint8_t* scratch,
                                         Tier0Error allowed, Tier0Error* error)
{
    const Tier0Check check = tier0_validate(function);
    const Tier0Code scalar = tier0_compile_scalar(function, arena);
    const Tier0Code vector = tier0_compile(function, arena);
    *error = scalar.error;
    bool ready = tier0_test_expect(tally, check.error == TIER0_OK && tier0_test_same_code(&scalar, &vector), "vectors", "%s: rule \"%s\" at %u, or the paths differ (%d, %d)",
                                   name, check.rule != 0 ? check.rule : "", check.node, scalar.error, vector.error);
    ready = ready && tier0_test_expect(tally, scalar.error == TIER0_OK || scalar.error == allowed, "vectors", "%s: both paths refuse it with error %d at entry %u", name,
                                       scalar.error, scalar.node);
    ready = ready && scalar.error == TIER0_OK;
    ready = ready && tier0_test_expect(tally, x86_validate(scalar.records, scalar.count) == scalar.count &&
                                                  branch_sizing_validate(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count).error ==
                                                      BRANCH_SIZING_OK,
                                       "vectors", "%s: an invalid record or an invalid branch column", name);
    ready = ready && tier0_test_covered(tally, "vectors", name, &scalar);
    uint32_t* offsets = ready ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1) : 0;
    const BranchSizing sizing = offsets != 0 ? size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets) : (BranchSizing){ 0 };
    ready = ready && tier0_test_expect(tally, offsets != 0 && sizing.error == BRANCH_SIZING_OK, "vectors", "%s: the branches can't be sized", name);
    const uint64_t room = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
    const int64_t vector_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode(scalar.records, scalar.count, image, room, 0, 0) : -1;
    const int64_t scalar_length = ready && room <= 4 * BUSTER_TIER0_TEST_PAGE ? x86_encode_scalar(scalar.records, scalar.count, scratch, room, 0, 0) : -1;
    ready = ready && tier0_test_expect(tally, vector_length > 0 && vector_length == scalar_length && memcmp(image, scratch, (size_t)vector_length) == 0, "vectors",
                                       "%s: the encoders differ, or fail (%" PRId64 ", %" PRId64 ")", name, vector_length, scalar_length);
    return ready ? (uint32_t)vector_length : 0;
}

// Run the interpreter and, where supported, the generated code on the same inputs. Native outputs must match every
// interpreter byte. Add to *reached what the interpreter reached after the available checks succeed.
static bool tier0_test_vector_check(Tier0TestTally* tally, const char* name, const Tier0Function* function, const uint8_t* image, uint32_t length,
                                    Tier0TestVectorShared* shared, Tier0TestVectorReach* reached)
{
    uint8_t* visited = calloc(1, (size_t)function->count + 1);
    memset(shared->out, 0xa5, sizeof(shared->out));
    const bool native = tier0_test_vector_can_run();
    const bool ran = !native || tier0_test_expect(tally, tier0_test_vector_native(image, length, tier0_test_vector_run_body, shared), "vectors", "%s: the native run failed", name);
    uint8_t expected[sizeof(shared->out)] __attribute__((aligned(64)));
    memset(expected, 0xa5, sizeof(expected));
    const uint64_t parameters[4] = { (uint64_t)(uintptr_t)shared->in, (uint64_t)(uintptr_t)shared->ints, (uint64_t)(uintptr_t)shared->floats, (uint64_t)(uintptr_t)expected };
    Tier0TestVectorReach reach = { { 0 }, 0, 0, 0 };
    const bool interpreted = tier0_test_expect(tally, visited != 0 && tier0_test_vector_interpret(function, parameters, visited, &reach), "vectors",
                                               "%s: the interpreter can't run it", name);
    bool same = ran && interpreted;
    for (uint32_t o = 0; native && o < BUSTER_TIER0_TEST_VECTOR_OUTPUTS && same; o += 1)
    {
        same = tier0_test_expect(tally, memcmp(shared->out[o], expected + 64 * o, 64) == 0, "vectors", "%s: output slot %u differs from the interpreter's", name, o);
    }
    if (same && reached != 0)
    {
        for (uint32_t w = 0; w < BUSTER_TIER0_TEST_VECTOR_OBJECTS; w += 1)
        {
            reached->kinds[w] |= reach.kinds[w];
        }
        reached->rows |= reach.rows;
        reached->arms |= reach.arms;
        reached->constants |= reach.constants;
    }
    free(visited);
    return same;
}

// The inputs: random bytes, integers and floats (finite ones: a vaddps's sums compare as bits).
static void tier0_test_vector_inputs(Tier0TestVectorShared* shared, uint64_t* state)
{
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_IN; k += 1)
    {
        for (uint32_t w = 0; w < 8; w += 1)
        {
            const uint64_t word = tier0_test_vector_word(state);
            memcpy(shared->in[k] + 8 * w, &word, 8);
        }
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_INTS; k += 1)
    {
        shared->ints[k] = tier0_test_vector_word(state);
    }
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_FLOATS; k += 1)
    {
        const float f = (float)((int32_t)tier0_generate_below(state, 2000) - 1000) / 8.0f;
        memcpy(&shared->floats[k], &f, 4);
    }
}

// The random programs: each compiled, run and compared with the interpreter; with the default count, their runs must
// reach every VECTOR kind at every width it has, every row they use, both arms of a ?: of a vector, and constants of
// zeros and of words past an imm32.
static void tier0_test_vector_programs(Tier0TestTally* tally, uint64_t seed, uint32_t count, Tier0TestVectorShared* shared, uint32_t* too_deep, uint64_t* entries)
{
    Tier0TestVectorReach reached = { { 0 }, 0, 0, 0 };
    Tier0TestVectorProgram* program = calloc(1, sizeof(Tier0TestVectorProgram));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 26);
    bool ready = tier0_test_expect(tally, program != 0 && image != 0 && arena.base != 0, "vectors", "no memory");
    for (uint32_t p = 0; p < count && ready; p += 1)
    {
        uint64_t state = seed ^ (0x5eccull << 32) ^ p;
        tier0_test_vector_program(program, &state);
        arena_pop_to(&arena, 0);
        char name[64];
        snprintf(name, sizeof(name), "program %u", p);
        if (!tier0_test_expect(tally, !program->overflow, "vectors", "%s: too large for the generator's columns", name))
        {
            continue;
        }
        const Tier0Function function = tier0_test_vector_function(program);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_vector_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, TIER0_ERROR_DEPTH, &error);
        *too_deep += error == TIER0_ERROR_DEPTH ? 1 : 0;
        *entries += program->count;
        if (length != 0)
        {
            tier0_test_vector_inputs(shared, &state);
            tier0_test_vector_check(tally, name, &function, image, length, shared, &reached);
        }
    }
    // Each VECTOR kind at each width it takes from its entries (DUP, DISCARD and JUMP_VALUE at the first's alone).
    const uint64_t valued = 1ull << BUSTER_TYPED_CONST | 1ull << BUSTER_TYPED_LOAD | 1ull << BUSTER_TYPED_STORE | 1ull << BUSTER_TYPED_SET |
                            1ull << BUSTER_TYPED_LOCAL | 1ull << BUSTER_TYPED_STORE_LOCAL | 1ull << BUSTER_TYPED_SET_LOCAL;
    const uint64_t wanted[BUSTER_TIER0_TEST_VECTOR_OBJECTS] = {
        valued | 1ull << BUSTER_TYPED_DUP | 1ull << BUSTER_TYPED_DISCARD | 1ull << BUSTER_TYPED_JUMP_VALUE, valued, valued, valued, valued,
    };
    uint32_t wanted_rows = 0;
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_KINDS; k += 1)
    {
        wanted_rows |= tier0_test_vector_kinds[k].random || k == BUSTER_TIER0_TEST_VECTOR_MASK_STORE ? 1u << k : 0;
    }
    for (uint32_t w = 0; w < BUSTER_TIER0_TEST_VECTOR_OBJECTS; w += 1)
    {
        tier0_test_expect(tally, count < BUSTER_TIER0_TEST_VECTOR_PROGRAMS || (wanted[w] & ~reached.kinds[w]) == 0, "vectors",
                          "the checked programs' interpreter runs don't reach every kind of %u-byte vectors (missing %#" PRIx64 ")", tier0_test_vector_type_bytes(w),
                          wanted[w] & ~reached.kinds[w]);
    }
    tier0_test_expect(tally, count < BUSTER_TIER0_TEST_VECTOR_PROGRAMS || ((wanted_rows & ~reached.rows) == 0 && reached.arms == 3 && reached.constants == 3), "vectors",
                      "the checked programs' interpreter runs don't reach every row (missing %#x), both arms (%#x) and both kinds of constants (%#x)", wanted_rows & ~reached.rows,
                      reached.arms, reached.constants);
    arena_release(&arena);
    free(image);
    free(program);
}

// ---------------------------------------------------------------------------------------------------------------------
// Placement

// A leaf of `type` that needs no address of its own where it can: a vector's and the indices' constants, an integer's and
// a float's constants, an address's (an input's 64 bytes, an output slot's, the inputs' first byte).
static void tier0_test_vector_simple_leaf(Tier0TestVectorProgram* program, uint32_t type, uint64_t* state)
{
    const uint32_t class = tier0_test_vector_type_class(type);
    if (type == BUSTER_TIER0_TEST_VECTOR_INDICES || class == BUSTER_TYPED_CLASS_VECTOR)
    {
        tier0_test_vector_constant(program, tier0_test_vector_type_bytes(type), state, type == BUSTER_TIER0_TEST_VECTOR_INDICES);
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_OUTPUT)
    {
        tier0_test_vector_address(program, 3, 0);
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_INPUT || type == BUSTER_TIER0_TEST_VECTOR_BASE)
    {
        tier0_test_vector_address(program, 0, type == BUSTER_TIER0_TEST_VECTOR_INPUT ? 64 : 0);
    }
    else if (type == BUSTER_TIER0_TEST_VECTOR_F32)
    {
        const float f = 3.25f;
        uint32_t bits = 0;
        memcpy(&bits, &f, 4);
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, bits);
    }
    else
    {
        const uint64_t word = tier0_test_vector_word(state);
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, class, type == BUSTER_TIER0_TEST_VECTOR_U32 ? word & 15 : word);
    }
}

// A function of one X86 entry of kind k on simple leaves, under `floats` floats and `ints` integers live (each stack's
// values pushed first, with their slot's address under them, then summed and xored and stored after it), its result
// stored into output slot 0 (a masked store's into slot 0 itself).
static void tier0_test_vector_placed(Tier0TestVectorProgram* program, uint32_t k, uint32_t floats, uint32_t ints, uint64_t* state)
{
    memset(program, 0, sizeof(*program));
    const Tier0TestVectorKind* kind = &tier0_test_vector_kinds[k];
    if (floats != 0)
    {
        tier0_test_vector_address(program, 3, 64);
    }
    for (uint32_t f = 0; f < floats; f += 1)
    {
        tier0_test_vector_address(program, 2, 4 * (uint64_t)(f % BUSTER_TIER0_TEST_VECTOR_FLOATS));
        tier0_test_vector_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_F32, 0);
    }
    if (ints != 0)
    {
        tier0_test_vector_address(program, 3, 64 + 8);
    }
    for (uint32_t n = 0; n < ints; n += 1)
    {
        tier0_test_vector_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, tier0_test_vector_word(state));
    }
    const uint32_t result = tier0_test_vector_type_class(kind->result);
    if (result != BUSTER_TYPED_CLASS_VOID)
    {
        tier0_test_vector_address(program, 3, 0);
    }
    for (uint32_t j = 0; j < kind->operand_count; j += 1)
    {
        tier0_test_vector_simple_leaf(program, kind->operands[j], state);
        if (k == BUSTER_TIER0_TEST_VECTOR_ADD_S && !program->overflow)
        {
            // The placement leaves are arbitrary bits. Make each lane a finite normal float so the scalar model and
            // vaddps have a bit-exact result even on machines that choose different NaN payloads.
            const uint32_t constant = program->constant_count - 1;
            for (uint32_t w = 0; w < 8; w += 1)
            {
                uint64_t* word = &program->constants[8 * constant + w];
                *word = (*word & 0x807fffff807fffffull) | 0x3e8000003e800000ull;
            }
        }
    }
    tier0_test_vector_emit(program, BUSTER_TYPED_X86, result, k);
    if (result != BUSTER_TYPED_CLASS_VOID)
    {
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, result, tier0_test_vector_type_bytes(kind->result));
    }
    for (uint32_t n = 1; n < ints; n += 1)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_I64);
        tier0_test_vector_emit(program, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_I64, 0);
    }
    if (ints != 0)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_I64);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_I64, 0);
    }
    for (uint32_t f = 1; f < floats; f += 1)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_F32);
        tier0_test_vector_emit(program, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_F32, 0);
    }
    if (floats != 0)
    {
        tier0_test_vector_adjacent(program, BUSTER_TYPED_CLASS_F32);
        tier0_test_vector_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F32, 0);
    }
    tier0_test_vector_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
}

// How many floating values kind k takes from the floating stack, and whether its result goes on it.
static uint32_t tier0_test_vector_floating_operands(const Tier0TestVectorKind* kind)
{
    uint32_t count = 0;
    for (uint32_t j = 0; j < kind->operand_count; j += 1)
    {
        count += kind->operands[j] == BUSTER_TIER0_TEST_VECTOR_F32 ? 1 : 0;
    }
    return count;
}

// Each kind at placements whose outcome this file works out apart from Tier 0: under no live values and under 8 floats
// and 3 integers (which every kind fits); under 15 floats, the most a stream keeps live across an entry of another class
// (their consumer's DUP, before it, takes the 16th: tier0_test_vector_adjacent), for the kinds without a VEX form, whose
// VECTOR registers then go from xmm15 on past the XMM registers (from VECTOR(0) on where a floating operand takes the
// 16th); and a VEX form's edge, under as many floats as leave its registers below 16 (its VECTOR registers from the
// floating stack's depth, one past a floating result's where no floating operand takes its place), and one more; and a
// scratch register (the andn row's) at the integer table's last depth, and past it. Each compiles by both paths and runs
// against the model: past a table, its stack's values in their deep slots and the row's operands in its first registers
// (tier0_select_deep, #780), as under 15 floats a VEX form's row is.
static void tier0_test_vector_placement(Tier0TestTally* tally, uint64_t seed, Tier0TestVectorShared* shared)
{
    Tier0TestVectorProgram* program = calloc(1, sizeof(Tier0TestVectorProgram));
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    bool ready = tier0_test_expect(tally, program != 0 && image != 0 && arena.base != 0, "vectors", "no memory");
    uint64_t state = seed ^ 0x9ace0ull;
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_KINDS && ready; k += 1)
    {
        const Tier0TestVectorKind* kind = &tier0_test_vector_kinds[k];
        const uint32_t floating = tier0_test_vector_floating_operands(kind);
        const uint32_t past = kind->result == BUSTER_TIER0_TEST_VECTOR_F32 && floating == 0 ? 1 : 0;
        uint32_t floats[6] = { 0, 8, BUSTER_TIER0_FLOATING_DEPTHS - 1, 0, 0, 0 };
        uint32_t ints[6] = { 0, 3, 0, 0, 0, 0 };
        uint32_t cases = 3;
        if (kind->vex_reach != 0)
        {
            const uint32_t room = BUSTER_TIER0_FLOATING_DEPTHS - floating - past - kind->vex_reach;
            floats[3] = room;
            floats[4] = room + 1;
            cases = 5;
        }
        if (k == BUSTER_TIER0_TEST_VECTOR_NOT_LOW)
        {
            // Its integers: the live ones' address and themselves, the output's address, its two operands, then the scratch
            // register at the depth after them: 14, the table's last, under 10 live integers, and 15 under 11.
            ints[3] = 10;
            ints[4] = 11;
            cases = 5;
        }
        for (uint32_t c = 0; c < cases; c += 1)
        {
            tier0_test_vector_placed(program, k, floats[c], ints[c], &state);
            arena_pop_to(&arena, 0);
            char name[96];
            snprintf(name, sizeof(name), "%s under %u floats and %u integers", kind->name, floats[c], ints[c]);
            const Tier0Function function = tier0_test_vector_function(program);
            Tier0Error error = TIER0_OK;
            const uint32_t length = tier0_test_vector_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, TIER0_OK, &error);
            tier0_test_expect(tally, error == TIER0_OK, "vectors", "%s: must compile (error %d)", name, error);
            if (length != 0)
            {
                tier0_test_vector_inputs(shared, &state);
                tier0_test_vector_check(tally, name, &function, image, length, shared, 0);
            }
        }
    }
    arena_release(&arena);
    free(image);
    free(program);
}

// ---------------------------------------------------------------------------------------------------------------------
// Aggregates in one vector register

// The C functions the hand-written functions call, as Tier 0's code calls them (through their addresses): each argument
// in a place of its own in the result, so that one in another's place shows.
static __m256i tier0_test_vector_mix(__m128i a, __m512i b, __m256i c, float f)
{
    const __m256i low = _mm512_castsi512_si256(b);
    const __m256i high = _mm512_extracti64x4_epi64(b, 1);
    const __m256i spread = _mm256_set1_epi32((int)(f * 4.0f));
    return _mm256_add_epi32(_mm256_sub_epi8(_mm256_xor_si256(low, c), high), _mm256_add_epi64(spread, _mm256_zextsi128_si256(a)));
}

static __m128i tier0_test_vector_sum9(__m128i a0, __m128i a1, __m128i a2, __m128i a3, __m128i a4, __m128i a5, __m128i a6, __m128i a7, __m128i a8)
{
    const __m128i parts[9] = { a0, a1, a2, a3, a4, a5, a6, a7, a8 };
    __m128i sum = _mm_setzero_si128();
    for (uint32_t k = 0; k < 9; k += 1)
    {
        sum = _mm_add_epi64(_mm_slli_epi64(sum, 1), parts[k]);
    }
    return sum;
}

// The signatures of the check's functions and calls, and their locations from one abi_assign batch, with AVX-512's
// vector registers (vector_bytes 64):
//   - PICK: __m512i (__m512i a, __m256i b, __m128i c, double d, __m512i e, void* out), in zmm0, ymm1, xmm2, xmm3, zmm4
//     and rdi, its result in zmm0: it writes b, c and d to out and returns vpermb's of a's indices into e;
//   - MIX: __m256i (__m128i, __m512i, __m256i, float), tier0_test_vector_mix's; and CALLER: __m256i (__m512i x, __m128i y),
//     which returns MIX(y, x, x's low half, 2.5f);
//   - STACKED: __m128i (__m128i a0 ... a7, __m128i s, __m512i t), s and t on the stack (no vector register is left), which
//     returns a0 - a1 - ... - a7 - s - t's last 16 bytes (vpsubq's);
//   - SUM9: __m128i (nine __m128i), tier0_test_vector_sum9's, the ninth on the stack; and SUMMER: __m128i (__m128i x,
//     __m128i y), which returns SUM9(x, y, x, y, x, y, x, y, x - y);
//   - KEEP: float (float a, float b, __m512i v), in xmm0, xmm1 and zmm2, which returns b after a row of no records that
//     takes v and a (#556's review);
//   - WIDEN: __m512i (__m512i a), which returns a's dwords plus a 16-byte constant's, read as a 64-byte vector.
#define BUSTER_TIER0_TEST_VECTOR_PICK 0
#define BUSTER_TIER0_TEST_VECTOR_MIX 1
#define BUSTER_TIER0_TEST_VECTOR_CALLER 2
#define BUSTER_TIER0_TEST_VECTOR_STACKED 3
#define BUSTER_TIER0_TEST_VECTOR_SUM9 4
#define BUSTER_TIER0_TEST_VECTOR_SUMMER 5
#define BUSTER_TIER0_TEST_VECTOR_KEEP 6
#define BUSTER_TIER0_TEST_VECTOR_WIDEN 7
#define BUSTER_TIER0_TEST_VECTOR_SIGNATURES 8

typedef struct Tier0TestVectorAbi Tier0TestVectorAbi;
struct Tier0TestVectorAbi
{
    AbiLocation locations[40];
    uint32_t first[BUSTER_TIER0_TEST_VECTOR_SIGNATURES];
    AbiCall calls[BUSTER_TIER0_TEST_VECTOR_SIGNATURES];
};

static bool tier0_test_vector_abi(Tier0Tables* tables, Tier0TestVectorAbi* abi)
{
    const TypeId v16 = type_vector(&tables->types, BUSTER_TYPE_LLONG, 2, false);
    const TypeId v32 = type_vector(&tables->types, BUSTER_TYPE_LLONG, 4, false);
    const TypeId v64 = type_vector(&tables->types, BUSTER_TYPE_LLONG, 8, false);
    const TypeId p = BUSTER_TYPE_ULONG;
    AbiTable table;
    Arena table_arena = arena_reserve(abi_capacity_bytes(tables->types.types.capacity));
    bool valid = v16 != 0 && v32 != 0 && v64 != 0 && abi_initialize(&table, &table_arena, &tables->types, 64);
    if (valid)
    {
        abi_classify_all(&table, &tables->types);
        const TypeId results[BUSTER_TIER0_TEST_VECTOR_SIGNATURES] = { v64, v32, v32, v16, v16, v16, BUSTER_TYPE_FLOAT, v64 };
        uint32_t counts[BUSTER_TIER0_TEST_VECTOR_SIGNATURES] = { 6, 4, 2, 10, 9, 2, 3, 1 };
        uint32_t named[BUSTER_TIER0_TEST_VECTOR_SIGNATURES] = { 6, 4, 2, 10, 9, 2, 3, 1 };
        uint8_t variadic[BUSTER_TIER0_TEST_VECTOR_SIGNATURES] = { 0 };
        const TypeId arguments[] = {
            v64, v32, v16, BUSTER_TYPE_DOUBLE, v64, p,
            v16, v64, v32, BUSTER_TYPE_FLOAT,
            v64, v16,
            v16, v16, v16, v16, v16, v16, v16, v16, v16, v64,
            v16, v16, v16, v16, v16, v16, v16, v16, v16,
            v16, v16,
            BUSTER_TYPE_FLOAT, BUSTER_TYPE_FLOAT, v64,
            v64,
        };
        uint32_t at = 0;
        for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_SIGNATURES; k += 1)
        {
            abi->first[k] = at;
            at += counts[k];
        }
        const AbiSignatures batch = {
            .results = results,
            .counts = counts,
            .named = named,
            .variadic = variadic,
            .count = BUSTER_TIER0_TEST_VECTOR_SIGNATURES,
            .arguments = arguments,
        };
        valid = at == BUSTER_ARRAY_LENGTH(arguments) && at <= BUSTER_ARRAY_LENGTH(abi->locations);
        if (valid)
        {
            abi_assign(&table, batch, abi->locations, abi->calls);
        }
    }
    arena_release(&table_arena);
    return valid;
}

// A hand-written function: its entries, its locals (objects for the vectors, values for the rest) and its calls, as Tier
// 0 takes them.
typedef struct Tier0TestVectorWritten Tier0TestVectorWritten;
struct Tier0TestVectorWritten
{
    uint8_t kinds[96];
    uint8_t classes[96];
    uint64_t values[96];
    uint32_t count;
    uint8_t local_classes[16];
    uint8_t local_flags[16];
    uint32_t local_sizes[16];
    uint8_t local_alignments[16];
    uint32_t local_count;
    uint32_t parameter_count;
    SymbolId call_targets[1];
    uint32_t call_first[1];
    uint32_t call_counts[1];
    uint8_t call_variadic[1];
    uint32_t call_result_objects[1];
    AbiCall calls[1];
    uint8_t argument_classes[12];
    AbiLocation argument_locations[12];
    uint32_t argument_sizes[12];
    uint32_t call_count;
    uint32_t argument_count;
    uint64_t constants[8];
    uint8_t constant_bytes[1];
    uint32_t constant_count;
    // Its operations, or null for the kinds' rows.
    const Tier0Operation* operations;
    uint32_t operation_count;
};

static void tier0_test_vector_put(Tier0TestVectorWritten* written, uint32_t kind, uint32_t class, uint64_t value)
{
    written->kinds[written->count] = (uint8_t)kind;
    written->classes[written->count] = (uint8_t)class;
    written->values[written->count] = value;
    written->count += 1;
}

// Local `local`: a vector's object of `bytes` (aligned to 16), or a value of `class` for 0 bytes.
static void tier0_test_vector_local(Tier0TestVectorWritten* written, uint32_t local, uint32_t class, uint32_t bytes)
{
    written->local_classes[local] = (uint8_t)(bytes != 0 ? BUSTER_TYPED_CLASS_VOID : class);
    written->local_flags[local] = bytes != 0 ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
    written->local_sizes[local] = bytes;
    written->local_alignments[local] = bytes != 0 ? 4 : 0;
    written->local_count = local + 1 > written->local_count ? local + 1 : written->local_count;
}

// The call of `signature`'s C function at `address`, its arguments' classes and sizes (an aggregate's bytes, 0 for a
// value) as given, its result into object `object`.
static void tier0_test_vector_written_call(Tier0TestVectorWritten* written, const Tier0TestVectorAbi* abi, uint32_t signature, const uint8_t* classes,
                                           const uint32_t* sizes, uint32_t count, uint32_t object)
{
    written->call_targets[0] = 0;
    written->call_first[0] = 0;
    written->call_counts[0] = count;
    written->call_variadic[0] = 0;
    written->call_result_objects[0] = 1 + object;
    written->calls[0] = abi->calls[signature];
    for (uint32_t a = 0; a < count; a += 1)
    {
        written->argument_classes[a] = classes[a];
        written->argument_sizes[a] = sizes[a];
        written->argument_locations[a] = abi->locations[abi->first[signature] + a];
    }
    written->call_count = 1;
    written->argument_count = count;
}

static Tier0Function tier0_test_vector_written_function(const Tier0TestVectorWritten* written, const Tier0TestVectorAbi* abi, uint32_t signature, uint32_t result_size)
{
    Tier0Function function;
    memset(&function, 0, sizeof(function));
    function.kinds = written->kinds;
    function.classes = written->classes;
    function.values = written->values;
    function.count = written->count;
    function.local_classes = written->local_classes;
    function.local_flags = written->local_flags;
    function.local_sizes = written->local_sizes;
    function.local_alignments = written->local_alignments;
    function.local_count = written->local_count;
    function.parameter_count = written->parameter_count;
    function.parameters = abi->locations + abi->first[signature];
    function.result = abi->calls[signature].result;
    function.result_size = result_size;
    function.call_targets = written->call_targets;
    function.call_first = written->call_first;
    function.call_argument_counts = written->call_counts;
    function.call_variadic = written->call_variadic;
    function.calls = written->calls;
    function.argument_locations = written->argument_locations;
    function.argument_classes = written->argument_classes;
    function.call_count = written->call_count;
    function.argument_count = written->argument_count;
    function.argument_sizes = written->call_count != 0 ? written->argument_sizes : 0;
    function.call_result_objects = written->call_count != 0 ? written->call_result_objects : 0;
    function.vector_constants = written->constants;
    function.vector_constant_bytes = written->constant_bytes;
    function.vector_constant_count = written->constant_count;
    function.operations = written->operations != 0 ? written->operations : tier0_test_vector_rows;
    function.operation_count = written->operations != 0 ? written->operation_count : BUSTER_TIER0_TEST_VECTOR_KINDS;
    function.vectors = true;
    return function;
}

// The arguments the runs pass, from the shared memory's arguments: vector k of each width as its bytes.
static __m512i tier0_test_vector_z(const Tier0TestVectorShared* shared, uint32_t k)
{
    return _mm512_loadu_si512(shared->arguments[k]);
}

static __m256i tier0_test_vector_y(const Tier0TestVectorShared* shared, uint32_t k)
{
    return _mm256_loadu_si256((const __m256i*)shared->arguments[k]);
}

static __m128i tier0_test_vector_x(const Tier0TestVectorShared* shared, uint32_t k)
{
    return _mm_loadu_si128((const __m128i*)shared->arguments[k]);
}

static void tier0_test_vector_run_pick(const void* code, Tier0TestVectorShared* shared)
{
    typedef __m512i (*Pick)(__m512i, __m256i, __m128i, double, __m512i, void*);
    Pick pick = 0;
    memcpy(&pick, &code, sizeof(pick));
    const __m512i result = pick(tier0_test_vector_z(shared, 0), tier0_test_vector_y(shared, 1), tier0_test_vector_x(shared, 2), 1.0 / 3.0, tier0_test_vector_z(shared, 3),
                                shared->out);
    _mm512_storeu_si512(shared->results[0], result);
}

static void tier0_test_vector_run_caller(const void* code, Tier0TestVectorShared* shared)
{
    typedef __m256i (*Caller)(__m512i, __m128i);
    Caller caller = 0;
    memcpy(&caller, &code, sizeof(caller));
    _mm256_storeu_si256((__m256i*)shared->results[0], caller(tier0_test_vector_z(shared, 0), tier0_test_vector_x(shared, 1)));
}

static void tier0_test_vector_run_stacked(const void* code, Tier0TestVectorShared* shared)
{
    typedef __m128i (*Stacked)(__m128i, __m128i, __m128i, __m128i, __m128i, __m128i, __m128i, __m128i, __m128i, __m512i);
    Stacked stacked = 0;
    memcpy(&stacked, &code, sizeof(stacked));
    const __m128i result = stacked(tier0_test_vector_x(shared, 0), tier0_test_vector_x(shared, 1), tier0_test_vector_x(shared, 2), tier0_test_vector_x(shared, 3),
                                   tier0_test_vector_x(shared, 4), tier0_test_vector_x(shared, 5), tier0_test_vector_x(shared, 6), tier0_test_vector_x(shared, 7),
                                   tier0_test_vector_x(shared, 8), tier0_test_vector_z(shared, 9));
    _mm_storeu_si128((__m128i*)shared->results[0], result);
}

static void tier0_test_vector_run_summer(const void* code, Tier0TestVectorShared* shared)
{
    typedef __m128i (*Summer)(__m128i, __m128i);
    Summer summer = 0;
    memcpy(&summer, &code, sizeof(summer));
    _mm_storeu_si128((__m128i*)shared->results[0], summer(tier0_test_vector_x(shared, 0), tier0_test_vector_x(shared, 1)));
}

static void tier0_test_vector_run_keep(const void* code, Tier0TestVectorShared* shared)
{
    typedef float (*Keep)(float, float, __m512i);
    Keep keep = 0;
    memcpy(&keep, &code, sizeof(keep));
    const float result = keep(1.5f, 2.25f, tier0_test_vector_z(shared, 0));
    memcpy(shared->results[0], &result, sizeof(result));
}

static void tier0_test_vector_run_widen(const void* code, Tier0TestVectorShared* shared)
{
    typedef __m512i (*Widen)(__m512i);
    Widen widen = 0;
    memcpy(&widen, &code, sizeof(widen));
    _mm512_storeu_si512(shared->results[0], widen(tier0_test_vector_z(shared, 0)));
}

// Each hand-written function compiled by both paths and run natively, called from C (whose code, clang's, is the
// oracle of the convention for the vectors), its result and what it wrote against this file's C of the same operations.
static void tier0_test_vector_aggregates(Tier0TestTally* tally, const Tier0TestVectorAbi* abi, Tier0TestVectorShared* shared, uint64_t seed)
{
    uint8_t* image = calloc(2, 4 * BUSTER_TIER0_TEST_PAGE);
    Arena arena = arena_reserve(1ull << 24);
    const bool ready = tier0_test_expect(tally, image != 0 && arena.base != 0, "vectors", "no memory");
    uint64_t state = seed ^ 0xa66e6a7eull;
    // KEEP's and WIDEN's rows past the kinds': a row of no records from a VECTOR and an F32 operand to the F32, whose
    // operand's load writes VECTOR(0), and ADD_D's with a 16-byte second operand, which its ZMM form reads whole.
    Tier0Operation rows[BUSTER_TIER0_TEST_VECTOR_KINDS + 2];
    memcpy(rows, tier0_test_vector_rows, sizeof(tier0_test_vector_rows));
    memset(&rows[BUSTER_TIER0_TEST_VECTOR_KINDS], 0, sizeof(rows[0]));
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS].operand_count = 2;
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS].operand_bytes[0] = 64;
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS].operand_classes[1] = BUSTER_TYPED_CLASS_F32;
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS].result_class = BUSTER_TYPED_CLASS_F32;
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS + 1] = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_ADD_D);
    rows[BUSTER_TIER0_TEST_VECTOR_KINDS + 1].operand_bytes[1] = 16;
    for (uint32_t f = 0; f < 6 && ready; f += 1)
    {
        Tier0TestVectorWritten written;
        memset(&written, 0, sizeof(written));
        uint32_t signature = 0;
        uint32_t result_size = 0;
        Tier0TestVectorRun run = 0;
        const char* name = "";
        if (f == 0)
        {
            // PICK: locals a, b, c, d, e, out and the result's object.
            name = "PICK: vectors of three widths in zmm0, ymm1, xmm2 and zmm4, a double and a pointer";
            signature = BUSTER_TIER0_TEST_VECTOR_PICK;
            result_size = 64;
            run = tier0_test_vector_run_pick;
            tier0_test_vector_local(&written, 0, 0, 64);
            tier0_test_vector_local(&written, 1, 0, 32);
            tier0_test_vector_local(&written, 2, 0, 16);
            tier0_test_vector_local(&written, 3, BUSTER_TYPED_CLASS_F64, 0);
            tier0_test_vector_local(&written, 4, 0, 64);
            tier0_test_vector_local(&written, 5, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_local(&written, 6, 0, 64);
            written.parameter_count = 6;
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, 32);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 32);
            tier0_test_vector_put(&written, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, 16);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 5);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 48);
            tier0_test_vector_put(&written, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F64, 3);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_F64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 4);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_PERMUTE_B);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 6);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 6);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (f == 1)
        {
            // CALLER: locals x, y, x's low half and MIX's result's object.
            name = "CALLER: MIX's vectors of three widths as aggregates, and its result into an object";
            signature = BUSTER_TIER0_TEST_VECTOR_CALLER;
            result_size = 32;
            run = tier0_test_vector_run_caller;
            tier0_test_vector_local(&written, 0, 0, 64);
            tier0_test_vector_local(&written, 1, 0, 16);
            tier0_test_vector_local(&written, 2, 0, 32);
            tier0_test_vector_local(&written, 3, 0, 32);
            written.parameter_count = 2;
            static const uint8_t classes[4] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_F32 };
            static const uint32_t sizes[4] = { 16, 64, 32, 0 };
            tier0_test_vector_written_call(&written, abi, BUSTER_TIER0_TEST_VECTOR_MIX, classes, sizes, 4, 3);
            const float spread = 2.5f;
            uint32_t bits = 0;
            memcpy(&bits, &spread, 4);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_LOW_HALF);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_vector_mix);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, bits);
            tier0_test_vector_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (f == 2)
        {
            // STACKED: locals a0 to a7, s, t and the result's object.
            name = "STACKED: eight vectors in registers, then a 16- and a 64-byte one on the stack";
            signature = BUSTER_TIER0_TEST_VECTOR_STACKED;
            result_size = 16;
            run = tier0_test_vector_run_stacked;
            for (uint32_t l = 0; l < 9; l += 1)
            {
                tier0_test_vector_local(&written, l, 0, 16);
            }
            tier0_test_vector_local(&written, 9, 0, 64);
            tier0_test_vector_local(&written, 10, 0, 16);
            written.parameter_count = 10;
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            for (uint32_t l = 1; l < 9; l += 1)
            {
                tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, l);
                tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_SUB_Q);
            }
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 9);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 48);
            tier0_test_vector_put(&written, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, 16);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_SUB_Q);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 10);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 10);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (f == 3)
        {
            // SUMMER: locals x, y, x - y and SUM9's result's object.
            name = "SUMMER: SUM9's nine vectors as aggregates, the ninth on the stack";
            signature = BUSTER_TIER0_TEST_VECTOR_SUMMER;
            result_size = 16;
            run = tier0_test_vector_run_summer;
            tier0_test_vector_local(&written, 0, 0, 16);
            tier0_test_vector_local(&written, 1, 0, 16);
            tier0_test_vector_local(&written, 2, 0, 16);
            tier0_test_vector_local(&written, 3, 0, 16);
            written.parameter_count = 2;
            uint8_t classes[9];
            uint32_t sizes[9];
            for (uint32_t a = 0; a < 9; a += 1)
            {
                classes[a] = BUSTER_TYPED_CLASS_U64;
                sizes[a] = 16;
            }
            tier0_test_vector_written_call(&written, abi, BUSTER_TIER0_TEST_VECTOR_SUM9, classes, sizes, 9, 3);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_SUB_Q);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, (uint64_t)(uintptr_t)&tier0_test_vector_sum9);
            for (uint32_t a = 0; a < 9; a += 1)
            {
                tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, a == 8 ? 2 : a % 2);
            }
            tier0_test_vector_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 3);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (f == 4)
        {
            // KEEP: locals a, b and v. a is live on the floating stack when the row loads v into VECTOR(0), xmm1, the
            // register past it, which b arrives in; b, read once the row's result (a) is dropped, must be b.
            name = "KEEP: a float parameter's register past a live float, through a row of no records";
            signature = BUSTER_TIER0_TEST_VECTOR_KEEP;
            run = tier0_test_vector_run_keep;
            tier0_test_vector_local(&written, 0, BUSTER_TYPED_CLASS_F32, 0);
            tier0_test_vector_local(&written, 1, BUSTER_TYPED_CLASS_F32, 0);
            tier0_test_vector_local(&written, 2, 0, 64);
            written.parameter_count = 3;
            written.operations = rows;
            written.operation_count = BUSTER_TIER0_TEST_VECTOR_KINDS + 2;
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F32, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_F32, BUSTER_TIER0_TEST_VECTOR_KINDS);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F32, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_F32, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_F32, 0);
        }
        else
        {
            // WIDEN: locals a and the result's object. The 16-byte constant's slot held a's 64 bytes before it, which a
            // load of the slot whole would add to a's upper dwords.
            name = "WIDEN: a 16-byte operand read by a ZMM form, its upper bytes zero";
            signature = BUSTER_TIER0_TEST_VECTOR_WIDEN;
            result_size = 64;
            run = tier0_test_vector_run_widen;
            tier0_test_vector_local(&written, 0, 0, 64);
            tier0_test_vector_local(&written, 1, 0, 64);
            written.parameter_count = 1;
            written.operations = rows;
            written.operation_count = BUSTER_TIER0_TEST_VECTOR_KINDS + 2;
            written.constants[0] = 0x0000000100000002ull;
            written.constants[1] = 0x0000000300000004ull;
            written.constant_bytes[0] = 16;
            written.constant_count = 1;
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, BUSTER_TIER0_TEST_VECTOR_KINDS + 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_VECTOR, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
        }
        arena_pop_to(&arena, 0);
        const Tier0Function function = tier0_test_vector_written_function(&written, abi, signature, result_size);
        Tier0Error error = TIER0_OK;
        const uint32_t length = tier0_test_vector_encode(tally, name, &function, &arena, image, image + 4 * BUSTER_TIER0_TEST_PAGE, TIER0_OK, &error);
        for (uint32_t k = 0; k < 12; k += 1)
        {
            for (uint32_t w = 0; w < 8; w += 1)
            {
                const uint64_t word = tier0_test_vector_word(&state);
                memcpy(shared->arguments[k] + 8 * w, &word, 8);
            }
        }
        memset(shared->out, 0xa5, sizeof(shared->out));
        memset(shared->results, 0xa5, sizeof(shared->results));
        // The C helpers' wide-vector calling convention also requires a vector host build.
        const bool ran = length != 0 && BUSTER_HOST_VECTOR && tier0_test_vector_can_run() &&
                         tier0_test_expect(tally, tier0_test_vector_native(image, length, run, shared), "vectors", "%s: the native run failed", name);
        uint8_t expected[64];
        memset(expected, 0xa5, sizeof(expected));
        uint8_t written_out[64];
        memset(written_out, 0xa5, sizeof(written_out));
        if (f == 0)
        {
            Tier0TestVectorValue operands[2];
            memcpy(operands[0].bytes, shared->arguments[0], 64);
            memcpy(operands[1].bytes, shared->arguments[3], 64);
            const Tier0TestVectorValue picked = tier0_test_vector_model(BUSTER_TIER0_TEST_VECTOR_PERMUTE_B, operands);
            memcpy(expected, picked.bytes, 64);
            const double d = 1.0 / 3.0;
            memcpy(written_out, shared->arguments[1], 32);
            memcpy(written_out + 32, shared->arguments[2], 16);
            memcpy(written_out + 48, &d, 8);
        }
        else if (f == 1)
        {
            const __m256i mixed = tier0_test_vector_mix(tier0_test_vector_x(shared, 1), tier0_test_vector_z(shared, 0), tier0_test_vector_y(shared, 0), 2.5f);
            memcpy(expected, &mixed, 32);
        }
        else if (f == 2)
        {
            __m128i difference = tier0_test_vector_x(shared, 0);
            for (uint32_t k = 1; k < 9; k += 1)
            {
                difference = _mm_sub_epi64(difference, tier0_test_vector_x(shared, k));
            }
            difference = _mm_sub_epi64(difference, _mm_loadu_si128((const __m128i*)(shared->arguments[9] + 48)));
            memcpy(expected, &difference, 16);
        }
        else if (f == 3)
        {
            const __m128i x = tier0_test_vector_x(shared, 0);
            const __m128i y = tier0_test_vector_x(shared, 1);
            const __m128i summed = tier0_test_vector_sum9(x, y, x, y, x, y, x, y, _mm_sub_epi64(x, y));
            memcpy(expected, &summed, 16);
        }
        else if (f == 4)
        {
            const float b = 2.25f;
            memcpy(expected, &b, sizeof(b));
        }
        else
        {
            uint8_t constant[64];
            memset(constant, 0, sizeof(constant));
            memcpy(constant, written.constants, 16);
            const __m512i sum = _mm512_add_epi32(tier0_test_vector_z(shared, 0), _mm512_loadu_si512(constant));
            memcpy(expected, &sum, 64);
        }
        // KEEP returns its float in xmm0 rather than through an object.
        const uint32_t compared = f == 4 ? 4 : result_size;
        if (ran)
        {
            tier0_test_expect(tally, memcmp(shared->results[0], expected, compared) == 0, "vectors", "%s: its result differs from C's", name);
            tier0_test_expect(tally, f != 0 || memcmp(shared->out[0], written_out, 56) == 0, "vectors", "%s: what it wrote differs from its arguments", name);
        }
    }
    arena_release(&arena);
    free(image);
}

// ---------------------------------------------------------------------------------------------------------------------
// The rules

// MASK_LOAD's row with an index too, operand 2 of class `index` (scaled by 1).
static Tier0Operation tier0_test_vector_indexed_row(uint32_t index)
{
    Tier0Operation row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD);
    row.records[1].mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
    row.records[1].index = BUSTER_TIER0_ROLE_INTEGER(2);
    row.operand_count = 3;
    row.operand_classes[2] = (uint8_t)index;
    return row;
}

// A row that breaks one of tier0_operation_valid's rules, from kind `k`'s: `b` picks the break.
static Tier0Operation tier0_test_vector_broken_row(uint32_t b)
{
    Tier0Operation row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_ADD_D);
    switch (b)
    {
    case 0: row.records[0].form = X86_FORM_PADDD_X_XM128; break;
    case 1: row.records[0] = (X86Instruction){ .form = X86_FORM_VZEROUPPER }; break;
    case 2: row.records[0].reg = 3; break;
    case 3:
        // A general register's role for a VECTOR operand.
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_SPLAT_D);
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[0] = 64;
        break;
    case 4:
        // A floating operand's role in a ZMM field.
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_SPLAT_S);
        row.records[0].reg = BUSTER_TIER0_ROLE_FLOATING(0);
        break;
    case 5:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_NOT_LOW);
        row.records[0].rm = BUSTER_TIER0_ROLE_SCRATCH(BUSTER_TIER0_SCRATCH);
        break;
    case 6:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD);
        row.records[1].mode = BUSTER_X86_MEMORY_RIP;
        break;
    case 7: row.operand_count = BUSTER_TIER0_OPERATION_OPERANDS + 1; break;
    case 8: row.operand_bytes[1] = 12; break;
    case 9: row.result_bytes = 0; break;
    case 10:
        // A block of registers (V4FMADDPS's four in vvvv), which a row can't name; an opmask pair it can (below).
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD);
        row.records[1] = (X86Instruction){ .form = X86_FORM_V4FMADDPS_Z_Z_M128, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(4),
                                           .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE };
        break;
    case 11:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_NOT_LOW);
        row.records[1].form = X86_FORM_MULX_R32_R32_RM32;
        break;
    case 12: row.record_count = BUSTER_TIER0_OPERATION_RECORDS + 1; break;
    case 13: row.records[0].form |= BUSTER_X86_FORM_FIXUP; break;
    case 14:
        // An integer result's role in a row whose result is a vector.
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_EQUAL_B);
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = 64;
        break;
    case 15:
        // INTEGER(j) past the operands.
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_SPLAT_D);
        row.records[0].rm = BUSTER_TIER0_ROLE_INTEGER(1);
        break;
    // Integer operands narrower than their fields, whose registers' bits past their classes' are unspecified: an
    // address's base, an index, a 64-bit field and a 32-bit one.
    case 16:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD);
        row.operand_classes[0] = BUSTER_TYPED_CLASS_I32;
        break;
    case 17: row = tier0_test_vector_indexed_row(BUSTER_TYPED_CLASS_U32); break;
    case 18:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD);
        row.operand_classes[1] = BUSTER_TYPED_CLASS_U32;
        break;
    default:
        row = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_ADD);
        row.operand_classes[1] = BUSTER_TYPED_CLASS_U16;
        break;
    }
    return row;
}

#define BUSTER_TIER0_TEST_VECTOR_ROW_BREAKS 20

// Each row break refused, and every row of this file's taken; then the stream's rules for vectors, each break refused at
// its entry (or at 0 for a function's columns), and its valid neighbour taken.
static void tier0_test_vector_rules(Tier0TestTally* tally)
{
    for (uint32_t k = 0; k < BUSTER_TIER0_TEST_VECTOR_KINDS; k += 1)
    {
        tier0_test_expect(tally, tier0_operation_valid(&tier0_test_vector_rows[k]), "vectors", "the row of %s refused", tier0_test_vector_kinds[k].name);
    }
    for (uint32_t b = 0; b < BUSTER_TIER0_TEST_VECTOR_ROW_BREAKS; b += 1)
    {
        const Tier0Operation row = tier0_test_vector_broken_row(b);
        tier0_test_expect(tally, !tier0_operation_valid(&row), "vectors", "row break %u taken", b);
    }
    // VP2INTERSECT's opmask pair (k2 and k3), two of the opmask registers no value keeps between entries.
    Tier0Operation pair = tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_ADD_D);
    pair.records[0] = (X86Instruction){ .form = X86_FORM_VP2INTERSECTD_K_Z_ZM512, .reg = 2, .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1) };
    tier0_test_expect(tally, tier0_operation_valid(&pair), "vectors", "VP2INTERSECT's opmask pair refused");
    // The narrow operands' neighbours: an address of I64, an index of U64 and of I64, and a 64-bit operand in a 32-bit
    // field (its low half).
    Tier0Operation neighbours[4] = {
        tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_LOAD),
        tier0_test_vector_indexed_row(BUSTER_TYPED_CLASS_U64),
        tier0_test_vector_indexed_row(BUSTER_TYPED_CLASS_I64),
        tier0_test_vector_row(BUSTER_TIER0_TEST_VECTOR_MASK_ADD),
    };
    neighbours[0].operand_classes[0] = BUSTER_TYPED_CLASS_I64;
    neighbours[3].operand_classes[1] = BUSTER_TYPED_CLASS_U64;
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(neighbours); n += 1)
    {
        tier0_test_expect(tally, tier0_operation_valid(&neighbours[n]), "vectors", "the narrow operands' neighbour %u refused", n);
    }
    // The streams: a function of the programs' locals, and its entries.
    static const struct
    {
        const char* name;
        uint32_t node;
    } breaks[] = {
        { "a valid function", UINT32_MAX },
        { "a vector entry in a function without vectors", 1 },
        { "an X86 entry past the operations", 6 },
        { "an X86 entry of another class than its row's result", 6 },
        { "an X86 entry's operand of another width", 6 },
        { "a vector constant past vector_constants", 1 },
        { "a vector constant of 12 bytes", 0 },
        { "LOAD of 48 bytes", 4 },
        { "SET of 16 bytes of a 32-byte vector", 5 },
        { "LOCAL of an object of 24 bytes", 2 },
        { "LOCAL of a value as a vector", 2 },
        { "DUP of an integer as a vector", 1 },
        { "DISCARD of an integer as a vector", 1 },
        { "?:'s arms of two widths", 9 },
        { "RETURN of a vector", 1 },
        { "NEG of a vector", 1 },
        { "a vector local that isn't an object", 0 },
        { "a call's vector argument as a value", 0 },
        { "an invalid row among the operations", 0 },
        { "a row's operation at the stack's depth", UINT32_MAX },
        { "CONVERT of a vector", 1 },
        { "EQ of two vectors", 2 },
        { "a call's argument class of a vector's stack byte", 0 },
    };
    static const uint8_t local_classes[6] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID,
                                              BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_VECTOR };
    static const uint8_t local_flags[6] = { 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, 0, 0 };
    static const uint32_t local_sizes[6] = { 0, 64, 32, 24, 0, 0 };
    static const uint8_t local_alignments[6] = { 0, 4, 4, 3, 0, 0 };
    static const AbiLocation parameters[1] = { { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } } };
    Tier0Operation rows[BUSTER_TIER0_TEST_VECTOR_KINDS + 1];
    memcpy(rows, tier0_test_vector_rows, sizeof(tier0_test_vector_rows));
    for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(breaks); b += 1)
    {
        Tier0TestVectorWritten written;
        memset(&written, 0, sizeof(written));
        uint64_t constants[24];
        for (uint32_t w = 0; w < 24; w += 1)
        {
            constants[w] = 0x0101010101010101ull * (w + 1);
        }
        uint8_t constant_bytes[3] = { 64, 32, 16 };
        const uint32_t v = BUSTER_TYPED_CLASS_VECTOR;
        // The locals: p (U64, in rdi), objects of 64, 32 and 24 bytes, an I32 value and (for its break) a VECTOR one; the
        // constants: of 64, 32 and 16 bytes.
        uint32_t operations = BUSTER_TIER0_TEST_VECTOR_KINDS;
        const uint32_t constant_count = 3;
        switch (b)
        {
        case 2:
        case 3:
        case 4:
            // p, c64, a local's vector, a DUP's copy dropped and one kept, then the X86 entry at 6.
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, v, b == 4 ? 2 : 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_DUP, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DUP, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, b == 3 ? BUSTER_TYPED_CLASS_U64 : v, b == 2 ? BUSTER_TIER0_TEST_VECTOR_KINDS : BUSTER_TIER0_TEST_VECTOR_ADD_D);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, b == 3 ? BUSTER_TYPED_CLASS_U64 : v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, v, 64);
            break;
        case 7:
        case 8:
            // p, then [p + 0]'s vector (LOAD at 4) stored at p (SET at 5).
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOAD, v, b == 7 ? 48 : 32);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, v, b == 8 ? 16 : 32);
            break;
        case 11:
        case 12:
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 7);
            tier0_test_vector_put(&written, b == 11 ? BUSTER_TYPED_DUP : BUSTER_TYPED_DISCARD, v, 0);
            if (b == 11)
            {
                tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
                tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
            }
            break;
        case 13:
            // 1 ? c64 : c32, its arms' widths apart where they join, at 9.
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_BRANCH_FALSE, BUSTER_TYPED_CLASS_I32, 4);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_JUMP_VALUE, v, 9);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_DUP, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DUP, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            break;
        case 14:
        case 15:
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 0);
            tier0_test_vector_put(&written, b == 14 ? BUSTER_TYPED_RETURN : BUSTER_TYPED_NEG, v, 0);
            if (b == 15)
            {
                tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            }
            break;
        case 20:
            // A conversion of a vector to U64, and a compare of two, which only X86 entries make.
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
            break;
        case 21:
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_I32, 0);
            break;
        case 22:
            // A 16-byte vector passed to a direct call whose argument's class is that vector's stack byte, in rdi.
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_CALL, BUSTER_TYPED_CLASS_VOID, 0);
            break;
        case 19:
        {
            // X86 entries of vectors to vectors, a float to a vector and a vector to a float: taken.
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, v, BUSTER_TIER0_TEST_VECTOR_LOW_HALF);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_F32, 0x40490fdb);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, v, BUSTER_TIER0_TEST_VECTOR_SPLAT_S);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 2);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_F32, BUSTER_TIER0_TEST_VECTOR_LOW_S);
            tier0_test_vector_put(&written, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_F32, 0);
            break;
        }
        default:
            // The base function: [p] = c64 + local 1 (both 64 bytes), local 1 = c64, local 2 = c32.
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, b == 5 ? 3 : 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_LOCAL, v, b == 9 ? 3 : b == 10 ? 4 : 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_X86, v, BUSTER_TIER0_TEST_VECTOR_ADD_D);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET, v, 64);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 0);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, v, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_CONST, v, 1);
            tier0_test_vector_put(&written, BUSTER_TYPED_SET_LOCAL, v, 2);
            break;
        }
        tier0_test_vector_put(&written, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        const bool vectors = b != 1;
        constant_bytes[2] = b == 6 ? 12 : 16;
        const uint32_t locals = b == 16 ? 6 : 5;
        if (b == 18)
        {
            rows[BUSTER_TIER0_TEST_VECTOR_KINDS] = tier0_test_vector_broken_row(0);
            operations = BUSTER_TIER0_TEST_VECTOR_KINDS + 1;
        }
        Tier0Function function;
        memset(&function, 0, sizeof(function));
        function.kinds = written.kinds;
        function.classes = written.classes;
        function.values = written.values;
        function.count = written.count;
        function.local_classes = local_classes;
        function.local_flags = local_flags;
        function.local_sizes = local_sizes;
        function.local_alignments = local_alignments;
        function.local_count = locals;
        function.parameter_count = 1;
        function.parameters = parameters;
        function.vector_constants = constants;
        function.vector_constant_bytes = constant_bytes;
        function.vector_constant_count = constant_count;
        function.operations = rows;
        function.operation_count = operations;
        function.vectors = vectors;
        // A call table of one: an argument of class VECTOR, which no entry calls (break 17), or of a vector's stack byte
        // in rdi, which break 22's entry calls with a vector.
        SymbolId target = 1;
        uint32_t first = 0;
        uint32_t argument_count = 1;
        uint8_t variadic = 0;
        AbiCall call = { 0 };
        uint8_t argument_class = b == 22 ? (uint8_t)(BUSTER_TYPED_CLASS_LIMIT | 1) : BUSTER_TYPED_CLASS_VECTOR;
        AbiLocation location = { .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_REGISTER_VECTOR | 0, BUSTER_ABI_REGISTER_NONE } };
        if (b == 22)
        {
            location = (AbiLocation){ .kind = BUSTER_ABI_LOCATION_REGISTERS, .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE } };
        }
        if (b == 17 || b == 22)
        {
            function.call_targets = &target;
            function.call_first = &first;
            function.call_argument_counts = &argument_count;
            function.call_variadic = &variadic;
            function.calls = &call;
            function.argument_locations = &location;
            function.argument_classes = &argument_class;
            function.call_count = 1;
            function.argument_count = 1;
        }
        const Tier0Check check = tier0_validate(&function);
        const bool expected = breaks[b].node == UINT32_MAX ? check.error == TIER0_OK : check.error == TIER0_ERROR_INVALID && check.node == breaks[b].node;
        tier0_test_expect(tally, expected, "vectors", "%s: tier0_validate says %s at %u (%s)", breaks[b].name, check.error == TIER0_OK ? "valid" : "invalid", check.node,
                          check.rule != 0 ? check.rule : "");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The check

// The vectors check: the rules, the placements, the aggregates, then `count` random programs.
static void tier0_test_vectors(Tier0TestTally* tally, Tier0Tables* tables, uint64_t seed, uint32_t count, uint32_t* too_deep, uint64_t* entries)
{
    tier0_test_vector_build_rows();
    Tier0TestVectorAbi abi;
    Tier0TestVectorShared* shared = mmap(0, sizeof(Tier0TestVectorShared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    const bool ready = tier0_test_expect(tally, shared != MAP_FAILED && tier0_test_vector_abi(tables, &abi), "vectors", "no memory, or the signatures' types");
    if (ready)
    {
        if (!tier0_test_vector_can_run())
        {
            printf("  vectors: native execution skipped (required CPU features or OS save state unavailable); encoding and interpreter checks run\n");
        }
        else if (!BUSTER_HOST_VECTOR)
        {
            printf("  vectors: native wide-vector ABI calls skipped (scalar host build); other native runs execute\n");
        }
        tier0_test_vector_rules(tally);
        tier0_test_vector_placement(tally, seed, shared);
        tier0_test_vector_aggregates(tally, &abi, shared, seed);
        tier0_test_vector_programs(tally, seed, count, shared, too_deep, entries);
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, sizeof(Tier0TestVectorShared));
    }
}
