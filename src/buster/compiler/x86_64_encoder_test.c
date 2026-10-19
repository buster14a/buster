// The x86-64 encoder tests. The compiler runs them as a command:
//
//   build/compiler test-x86-encoder [--count N] [--seed N]
//   build/compiler test-x86-encoder --print-assembly N [--seed N]
//   build/compiler test-x86-encoder --compare-binary text lengths N [--seed N]
//
// main.c includes this file after x86_64_encoder.c and calls run_x86_encoder_tests for the command. The checks, in this
// order:
//   - The templates: every form's longest encoding fits in a slot.
//   - Known encodings: x86_encode_scalar and x86_encode on x86_known_encodings, instructions on the paths most worth
//     pinning down (REX, SIB, RBP and R13 bases, RSP and R12 bases, RIP, absolute and index-only addresses,
//     displacement sizes, byte registers, prefixes, opcode maps; VEX2 and VEX3, BMI, opmasks, EVEX's opmasks,
//     zeroing, broadcasts, rounding, compressed displacements and registers 16 to 31), with bytes from GNU as.
//   - The reference digest: a hash of x86_encode_scalar's output for BUSTER_X86_TEST_DIGEST_INSTRUCTIONS random
//     instructions (from the stream --print-assembly prints with --seed BUSTER_X86_TEST_DIGEST_SEED), recorded when
//     GNU as encoded the same stream to the same bytes. A change to the form table or the scalar encoder changes it;
//     cross-check the new output with GNU as (as AGENTS.md shows) before recording the new value.
//   - Random streams: x86_encode at every batch width (x86_encode_4 ... x86_encode_64) and the gather and store
//     variants against x86_encode_scalar, byte for byte and offset for offset, on random instructions of every form
//     (with empty X86_FORM_NONE instructions among them): every count from 0 to BUSTER_X86_TEST_SHORT_COUNTS - 1, then
//     N instructions (BUSTER_X86_TEST_DEFAULT_COUNT by default) at once.
//   - Page edges: instructions that end exactly at the end of a readable page, and an output capacity (or, for the
//     variants that write only the instructions' bytes, the machine code) and offsets that end exactly at the end of
//     a writable one, with a PROT_NONE page after them, for every count from 0 to BUSTER_X86_TEST_SHORT_COUNTS - 1. A
//     read or write past the end faults, and the fault handler names the count and exits with status 1.
//   - Invalid instructions: random valid instructions with one operand broken in ways x86_encode checks (a high byte
//     register with a REX prefix, RSP as an index, RIP with a base, a register past its class's last, vvvv or reg in
//     a form without them, decorations the form can't have, unknown mode bits): x86_encode must reject them and
//     x86_validate must find them.
//
// --print-assembly prints N random instructions in GNU as Intel syntax, each with its bytes from x86_encode_scalar in
// a comment, choosing operands GNU as encodes with the same form (with {load}, {store} and {evex} where GNU as would
// pick another), and has GNU as record each one's length in a .lengths section; --compare-binary compares the .text and .lengths sections of the object GNU as makes of that (as objcopy
// extracts them) with x86_encode_scalar's bytes for the same instructions. The exit status is 1 if the arguments are
// invalid or a check fails.
//
// Layout, in this order:
//   - Random instructions: x86_test_random, x86_random_register, x86_random_displacement, x86_random_immediate and
//     x86_random_instruction.
//   - Printing: register names, x86_print_memory and x86_print_instruction.
//   - The checks: x86_check_templates, x86_known_encodings and x86_check_known, x86_check_digest, x86_compare_encoders and
//     x86_check_random, the page-edge check (x86_test_report_fault and x86_check_page_edges), and x86_check_invalid.
//   - The cross-check modes (x86_print_assembly and x86_compare_binary) and run_x86_encoder_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Random streams: every count below BUSTER_X86_TEST_SHORT_COUNTS, then this many instructions at once.
#define BUSTER_X86_TEST_SHORT_COUNTS 200
#define BUSTER_X86_TEST_DEFAULT_COUNT 300000
// The digest's stream and the hash of x86_encode_scalar's bytes for it (FNV-1a, 64-bit).
#define BUSTER_X86_TEST_DIGEST_SEED 1
#define BUSTER_X86_TEST_DIGEST_INSTRUCTIONS 100000
#define BUSTER_X86_TEST_DIGEST 0xc3614ad6c5518e94ull
// 2^64 divided by the golden ratio: the first state of the random sequence, and the multiplier that turns --seed N
// into one (N * BUSTER_X86_TEST_GOLDEN_RATIO + 1).
#define BUSTER_X86_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// Each check prints the first differences of this many inputs.
#define BUSTER_X86_TEST_REPORTED 3
// Broken copies of each random instruction in the invalid-instruction check.
#define BUSTER_X86_TEST_INVALID_INSTRUCTIONS 20000

// ---------------------------------------------------------------------------------------------------------------------
// Random instructions

// xorshift64*.
static uint64_t x86_test_random(uint64_t* state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static uint32_t x86_random_below(uint64_t* state, uint32_t bound)
{
    return (uint32_t)((x86_test_random(state) >> 32) * bound >> 32);
}

// A register of the class (in a form of the family), more often one of the numbers with special encodings (4, 5, 12
// and 13, and in half of an EVEX form's vector registers, 16 to 31) and, for a legacy byte register, sometimes AH to
// BH.
static uint8_t x86_random_register(uint64_t* state, uint8_t operand, uint8_t family)
{
    static const uint8_t special[] = { 4, 5, 12, 13 };
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = x86_random_below(state, 4) == 0 ? special[x86_random_below(state, 4)] : (uint8_t)x86_random_below(state, 16);
    if (class == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY && x86_random_below(state, 6) == 0)
    {
        result = (uint8_t)((4 + x86_random_below(state, 4)) | BUSTER_X86_REGISTER_HIGH_BYTE);
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX && x86_random_below(state, 2) == 0)
    {
        result |= 16;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST)
    {
        result &= 7;
    }
    return result;
}

// Mostly the values at the edges of each displacement size, and for an EVEX form's N (`scale`, a power of two), its
// multiples, those at the edges of the 8-bit range in units of N among them, and their neighbors.
static int32_t x86_random_displacement(uint64_t* state, uint32_t scale)
{
    static const int32_t edges[] = { 0, 1, -1, 127, 128, -128, -129, 255, 256, 32767, -32768, 0x7fffffff, (int32_t)0x80000000 };
    int32_t result;
    switch (x86_random_below(state, scale > 1 ? 6 : 4))
    {
        case 0: result = edges[x86_random_below(state, BUSTER_ARRAY_LENGTH(edges))]; break;
        case 1: result = (int32_t)x86_random_below(state, 256) - 128; break;
        case 2: result = (int32_t)x86_random_below(state, 65536) - 32768; break;
        case 3: result = (int32_t)(uint32_t)x86_test_random(state); break;
        case 4: result = ((int32_t)x86_random_below(state, 258) - 129) * (int32_t)scale; break;
        default: result = edges[x86_random_below(state, 7)] * (int32_t)scale + (int32_t)x86_random_below(state, 3) - 1; break;
    }
    return result;
}

// An immediate of `bytes` bytes (as X86Instruction.immediate holds it), in the range a sign-extended one has if
// `sign_extended`.
static int32_t x86_random_immediate(uint64_t* state, uint32_t bytes, bool sign_extended)
{
    int32_t result = 0;
    switch (bytes)
    {
        case 1: result = sign_extended ? (int32_t)x86_random_below(state, 256) - 128 : (int32_t)x86_random_below(state, 256); break;
        case 2: result = sign_extended ? (int32_t)x86_random_below(state, 65536) - 32768 : (int32_t)x86_random_below(state, 65536); break;
        case 3: result = (int32_t)x86_random_below(state, 1u << 24); break;
        case 4: result = x86_random_below(state, 3) == 0 ? (int32_t)x86_random_below(state, 256) - 128 : (int32_t)(uint32_t)x86_test_random(state); break;
        case 8: result = (int32_t)(uint32_t)x86_test_random(state); break;
        default: break;
    }
    return result;
}

static bool x86_fits_int8(int32_t value)
{
    return value >= -128 && value <= 127;
}

// A form's opcode, without its prefixes, as one number (for the forms GNU as encodes differently).
static uint32_t x86_form_opcode(const X86FormDescription* form)
{
    return form->opcode_bytes == 1 ? form->opcode : 0x10000 | form->opcode;
}

// Whether GNU as would encode the instruction with another form of the same mnemonic (a shorter one, usually), so
// that --print-assembly avoids it.
static bool x86_assembler_prefers_another_form(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    // The form without its segment override.
    const uint32_t row = instruction->form % X86_FORM_COUNT;
    const bool register_direct = instruction->mode == 0 && form->rm != BUSTER_X86_OPERAND_NONE;
    const uint32_t opcode = x86_form_opcode(form);
    // The immediate as the operand's size makes it: 65499 is -37 for a 16-bit operand.
    const int32_t immediate = form->immediate_bytes == 1 ? (int8_t)instruction->immediate : form->immediate_bytes == 2 ? (int16_t)instruction->immediate
                                                                                                                       : instruction->immediate;
    const bool legacy = form->family == BUSTER_X86_FAMILY_LEGACY;
    bool result = false;
    // MOVQ (and VEX's VMOVQ) between an XMM register and memory is F3 0F 7E or 66 0F D6, not the W1 forms of MOVD; and
    // EVEX's the other way around.
    if (!register_direct && (row == X86_FORM_MOVQ_X_RM64 || row == X86_FORM_MOVQ_RM64_X || row == X86_FORM_VMOVQ_X_RM64 || row == X86_FORM_VMOVQ_RM64_X ||
                             row == X86_FORM_VMOVQ_X_XM64_EVEX || row == X86_FORM_VMOVQ_XM64_X_EVEX))
    {
        result = true;
    }
    // The rest are legacy opcodes. 80 and 81 /n, F6 and F7 /0 (test), C6 and C7 /0 (mov) and 87 (xchg) have shorter
    // accumulator forms.
    if (legacy && register_direct && instruction->rm == 0 &&
        (opcode == 0x80 || opcode == 0x81 || ((opcode == 0xf6 || opcode == 0xf7) && form->encoding == BUSTER_X86_ENCODING_D0)))
    {
        result = true;
    }
    if (legacy && register_direct && opcode == 0x87 && (instruction->rm == 0 || instruction->reg == 0))
    {
        result = true;
    }
    // MOV r/m, imm into a register is B0+r or B8+r, except in 64-bit forms.
    if (legacy && register_direct && (opcode == 0xc6 || opcode == 0xc7) && !form->w)
    {
        result = true;
    }
    // Immediates that fit in a sign-extended byte take the imm8 forms, also instead of the AX, EAX and RAX forms of
    // the arithmetic instructions (05, 0D ... 3D).
    if (legacy && (opcode == 0x81 || opcode == 0x69 || opcode == 0x68) && form->immediate_bytes != 1 && x86_fits_int8(immediate))
    {
        result = true;
    }
    if (legacy && (opcode & 0xc7) == 0x05 && form->syntax == X86_SYNTAX_ACC_IMM && x86_fits_int8(immediate))
    {
        result = true;
    }
    // A shift by 1 is D0/D1, and INT 3 is INT3.
    if (legacy && (opcode == 0xc0 || opcode == 0xc1) && instruction->immediate == 1)
    {
        result = true;
    }
    if (legacy && opcode == 0xcd && instruction->immediate == 3)
    {
        result = true;
    }
    // PUSH and POP of a register are 50+r and 58+r.
    if (legacy && register_direct && (opcode == 0xff || opcode == 0x8f) && (form->encoding == BUSTER_X86_ENCODING_D6 || opcode == 0x8f))
    {
        result = true;
    }
    // XCHG EAX, EAX with 90 would be NOP (which doesn't clear the upper half), so it's 87 C0; XCHG RAX, RAX is NOP.
    if ((row == X86_FORM_XCHG_R32_EAX || row == X86_FORM_XCHG_R64_RAX) && instruction->rm == 0)
    {
        result = true;
    }
    // PREFETCHIT0 and PREFETCHIT1 take only RIP-relative addresses (with others, they're NOPs, which GNU as warns about).
    if ((row == X86_FORM_PREFETCHIT0_M8 || row == X86_FORM_PREFETCHIT1_M8) && !(instruction->mode & BUSTER_X86_MODE_RIP))
    {
        result = true;
    }
    // GNU as warns about a segment override on LEA, where it does nothing.
    if (legacy && opcode == 0x8d && x86_form_segment(instruction->form) != 0)
    {
        result = true;
    }
    // An x87 operation on ST(0) and ST(0) as fop st(0), st (DC) is the same as fop st, st(0), which is D8.
    if (legacy && register_direct && opcode == 0xdc && instruction->rm == 0)
    {
        result = true;
    }
    // An index scaled by 1 without a base becomes the base.
    if ((instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX)) == BUSTER_X86_MODE_INDEX && (instruction->mode & BUSTER_X86_MODE_SCALE) == 0)
    {
        result = true;
    }
    return result;
}

// A random valid instruction of the form. For --print-assembly (`for_assembler`), one GNU as encodes with this form.
static X86Instruction x86_random_instruction(uint64_t* state, uint16_t form_index, bool for_assembler)
{
    const X86FormDescription* form = &x86_form_descriptions[form_index];
    X86Instruction result;
    bool done = false;
    while (!done)
    {
        result = (X86Instruction){ .form = form_index };
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result.reg = x86_random_register(state, form->reg, form->family);
        }
        if (form->vvvv != BUSTER_X86_OPERAND_NONE)
        {
            result.vvvv = x86_random_register(state, form->vvvv, form->family);
        }
        if (form->rm != BUSTER_X86_OPERAND_NONE)
        {
            const bool can_register = (form->rm & BUSTER_X86_OPERAND_REGISTER) != 0;
            const bool can_memory = (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0;
            if (can_register && (!can_memory || x86_random_below(state, 2) == 0))
            {
                result.rm = x86_random_register(state, form->rm, form->family);
            }
            else
            {
                const uint8_t scale = (uint8_t)x86_random_below(state, 4);
                switch (x86_random_below(state, 20))
                {
                    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
                        result.mode = BUSTER_X86_MEMORY_BASE;
                        break;
                    case 8: case 9: case 10: case 11: case 12:
                        result.mode = BUSTER_X86_MEMORY_BASE_INDEX(scale);
                        break;
                    case 13: case 14:
                        result.mode = BUSTER_X86_MEMORY_INDEX(scale);
                        break;
                    case 15: case 16:
                        result.mode = BUSTER_X86_MEMORY_ABSOLUTE;
                        break;
                    default:
                        result.mode = BUSTER_X86_MEMORY_RIP;
                        break;
                }
                if (result.mode & BUSTER_X86_MODE_BASE)
                {
                    result.rm = x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY);
                }
                if (result.mode & BUSTER_X86_MODE_INDEX)
                {
                    result.index = x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY);
                }
                // An FS or GS segment override one time in eight.
                if (x86_random_below(state, 8) == 0)
                {
                    result.form = (uint16_t)(result.form + (x86_random_below(state, 2) == 0 ? BUSTER_X86_FORM_FS : BUSTER_X86_FORM_GS));
                }
                result.displacement = x86_random_displacement(state, form->disp8_scale);
            }
        }
        // An opmask half the time, zeroing half of those (not into memory), and any rounding mode.
        if ((form->evex & BUSTER_X86_EVEX_MASK) && x86_random_below(state, 2) == 0)
        {
            const bool memory_destination = (result.mode & BUSTER_X86_MODE_MEMORY) && (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_RM_REG_IMM);
            result.decorations = (uint8_t)(1 + x86_random_below(state, 7));
            if ((form->evex & BUSTER_X86_EVEX_ZEROING) && !memory_destination && x86_random_below(state, 2) == 0)
            {
                result.decorations |= BUSTER_X86_DECORATION_ZEROING;
            }
        }
        if (form->evex & BUSTER_X86_EVEX_ROUNDING)
        {
            result.decorations |= (uint8_t)(x86_random_below(state, 4) << 5);
        }
        const uint32_t opcode = x86_form_opcode(form);
        const bool sign_extended = opcode == 0x83 || opcode == 0x6b || opcode == 0x6a;
        result.immediate = x86_random_immediate(state, form->immediate_bytes, sign_extended || (for_assembler && form->immediate_bytes == 4));
        if (form->immediate_bytes == 8)
        {
            result.displacement = (int32_t)(uint32_t)x86_test_random(state);
        }
        if (form->relative_bytes == 1)
        {
            result.displacement = (int32_t)x86_random_below(state, 256) - 128;
        }
        else if (form->relative_bytes == 4)
        {
            result.displacement = x86_random_displacement(state, 1);
        }
        done = x86_instruction_is_valid(&result) && !(for_assembler && x86_assembler_prefers_another_form(&result));
    }
    return result;
}

// A random form other than X86_FORM_NONE, or with `with_none`, X86_FORM_NONE one time in 16.
static uint16_t x86_random_form(uint64_t* state, bool with_none)
{
    uint16_t result = (uint16_t)(1 + x86_random_below(state, X86_FORM_COUNT - 1));
    if (with_none && x86_random_below(state, 16) == 0)
    {
        result = X86_FORM_NONE;
    }
    return result;
}

static void x86_random_instructions(uint64_t* state, X86Instruction* instructions, uint64_t count, bool with_none, bool for_assembler)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        instructions[i] = x86_random_instruction(state, x86_random_form(state, with_none), for_assembler);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Printing, in GNU as Intel syntax

static const char* const x86_gpr8_names[16] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" };
static const char* const x86_gpr8_high_names[4] = { "ah", "ch", "dh", "bh" };
static const char* const x86_gpr16_names[16] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" };
static const char* const x86_gpr32_names[16] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
static const char* const x86_gpr64_names[16] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
static const char* const x86_xmm_names[32] = { "xmm0",  "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",  "xmm7",  "xmm8",  "xmm9",  "xmm10",
                                           "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "xmm16", "xmm17", "xmm18", "xmm19", "xmm20", "xmm21",
                                           "xmm22", "xmm23", "xmm24", "xmm25", "xmm26", "xmm27", "xmm28", "xmm29", "xmm30", "xmm31" };
static const char* const x86_ymm_names[32] = { "ymm0",  "ymm1",  "ymm2",  "ymm3",  "ymm4",  "ymm5",  "ymm6",  "ymm7",  "ymm8",  "ymm9",  "ymm10",
                                           "ymm11", "ymm12", "ymm13", "ymm14", "ymm15", "ymm16", "ymm17", "ymm18", "ymm19", "ymm20", "ymm21",
                                           "ymm22", "ymm23", "ymm24", "ymm25", "ymm26", "ymm27", "ymm28", "ymm29", "ymm30", "ymm31" };
static const char* const x86_zmm_names[32] = { "zmm0",  "zmm1",  "zmm2",  "zmm3",  "zmm4",  "zmm5",  "zmm6",  "zmm7",  "zmm8",  "zmm9",  "zmm10",
                                           "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21",
                                           "zmm22", "zmm23", "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31" };
static const char* const x86_opmask_names[8] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
static const char* const x86_st_names[8] = { "st(0)", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)" };
// The rounding modes (BUSTER_X86_DECORATION_ROUNDING), as GNU as writes them.
static const char* const x86_rounding_names[4] = { "{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}" };

// A register's name (an invalid one's, too: the numbers wrap around).
static const char* x86_register_name(uint8_t value, uint8_t operand)
{
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    const char* result;
    switch (operand & BUSTER_X86_OPERAND_CLASS)
    {
        case BUSTER_X86_CLASS_GPR8: result = (value & BUSTER_X86_REGISTER_HIGH_BYTE) ? x86_gpr8_high_names[number & 3] : x86_gpr8_names[number & 15]; break;
        case BUSTER_X86_CLASS_GPR16: result = x86_gpr16_names[number & 15]; break;
        case BUSTER_X86_CLASS_GPR32: result = x86_gpr32_names[number & 15]; break;
        case BUSTER_X86_CLASS_GPR64: result = x86_gpr64_names[number & 15]; break;
        case BUSTER_X86_CLASS_YMM: result = x86_ymm_names[number]; break;
        case BUSTER_X86_CLASS_ZMM: result = x86_zmm_names[number]; break;
        case BUSTER_X86_CLASS_OPMASK: result = x86_opmask_names[number & 7]; break;
        case BUSTER_X86_CLASS_ST: result = x86_st_names[number & 7]; break;
        default: result = x86_xmm_names[number]; break;
    }
    return result;
}

static const char* x86_accumulator_name(uint32_t bytes)
{
    return bytes == 1 ? "al" : bytes == 2 ? "ax" : bytes == 4 ? "eax" : "rax";
}

static void x86_print_memory(FILE* file, const X86Instruction* instruction, uint32_t operand_bytes)
{
    const uint8_t mode = instruction->mode;
    const uint32_t override = x86_form_segment(instruction->form);
    const char* segment = override == 0 ? "" : override == 1 ? "fs:" : "gs:";
    // A far pointer of 6 bytes is an fword; the x87 environment and state areas (14, 28, 94 and 108 bytes) and MOVDIR64B's
    // 64 bytes (0 in its row) have no size keyword.
    const char* size = operand_bytes == 1 ? "byte ptr " : operand_bytes == 2 ? "word ptr " : operand_bytes == 4 ? "dword ptr " : operand_bytes == 6 ? "fword ptr " :
                       operand_bytes == 8 ? "qword ptr " : operand_bytes == 10 ? "tbyte ptr " : operand_bytes == 16 ? "xmmword ptr " :
                       operand_bytes == 32 ? "ymmword ptr " : operand_bytes == 64 ? "zmmword ptr " : "";
    if ((mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | BUSTER_X86_MODE_RIP)) == 0)
    {
        fprintf(file, "%s%s%d", size, *segment != 0 ? segment : "ds:", instruction->displacement);
    }
    else
    {
        fprintf(file, "%s%s[", size, segment);
        if (mode & BUSTER_X86_MODE_RIP)
        {
            fprintf(file, "rip");
        }
        if (mode & BUSTER_X86_MODE_BASE)
        {
            fprintf(file, "%s", x86_gpr64_names[instruction->rm & 15]);
        }
        if (mode & BUSTER_X86_MODE_INDEX)
        {
            fprintf(file, "%s%s*%d", (mode & BUSTER_X86_MODE_BASE) ? "+" : "", x86_gpr64_names[instruction->index & 15], 1 << (mode & BUSTER_X86_MODE_SCALE));
        }
        fprintf(file, "%+d]", instruction->displacement);
    }
}

// A broadcast's memory operand is its element, whose size is the disp8 scale.
static void x86_print_rm(FILE* file, const X86Instruction* instruction, const X86FormDescription* form)
{
    if (instruction->mode != 0)
    {
        x86_print_memory(file, instruction, (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : form->operand_bytes);
    }
    else
    {
        fprintf(file, "%s", x86_register_name(instruction->rm, form->rm));
    }
}

// The operands of a syntax, in order (r for reg, v for vvvv, m for rm, i for the immediate, c for CL, 1 for 1, s for
// ST(0) and a for the accumulator), for the syntaxes x86_print_instruction doesn't print itself.
static const char* x86_syntax_operands(uint8_t syntax)
{
    const char* result = "";
    switch (syntax)
    {
        case X86_SYNTAX_RM: result = "m"; break;
        case X86_SYNTAX_RM_REG: result = "mr"; break;
        case X86_SYNTAX_REG_RM: result = "rm"; break;
        case X86_SYNTAX_RM_IMM: result = "mi"; break;
        case X86_SYNTAX_REG_RM_IMM: result = "rmi"; break;
        case X86_SYNTAX_RM_REG_IMM: result = "mri"; break;
        case X86_SYNTAX_RM_REG_CL: result = "mrc"; break;
        case X86_SYNTAX_RM_ONE: result = "m1"; break;
        case X86_SYNTAX_RM_CL: result = "mc"; break;
        case X86_SYNTAX_IMM: result = "i"; break;
        case X86_SYNTAX_REG_VVVV_RM: result = "rvm"; break;
        case X86_SYNTAX_REG_VVVV_RM_IMM: result = "rvmi"; break;
        case X86_SYNTAX_REG_RM_VVVV: result = "rmv"; break;
        case X86_SYNTAX_VVVV_RM: result = "vm"; break;
        case X86_SYNTAX_VVVV_RM_IMM: result = "vmi"; break;
        case X86_SYNTAX_ST_RM: result = "sm"; break;
        case X86_SYNTAX_RM_ST: result = "ms"; break;
        case X86_SYNTAX_ACC: result = "a"; break;
        case X86_SYNTAX_RM_ACC: result = "ma"; break;
        case X86_SYNTAX_RM_REG_VVVV: result = "mrv"; break;
        case X86_SYNTAX_REG_RM_XMM0: result = "rmx"; break;
        case X86_SYNTAX_RM_IMM8_IMM8: result = "mjk"; break;
        case X86_SYNTAX_REG_RM_IMM8_IMM8: result = "rmjk"; break;
        case X86_SYNTAX_RM_VVVV_REG: result = "mvr"; break;
        default: break;
    }
    return result;
}

// Prints the instruction in GNU as Intel syntax. A register-to-register move or arithmetic instruction gets {load} or
// {store}, which picks the form with ModRM.reg as the destination or the source (and keeps GNU as from swapping a VEX
// move's operands to make it VEX2), and an EVEX form gets {evex}, which GNU as otherwise only picks when the operands
// need it. The opmask and zeroing follow the first operand, a broadcast follows the memory operand, and embedded
// rounding or suppressed exceptions come after the registers.
static void x86_print_instruction(FILE* file, const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool register_direct = instruction->mode == 0 && form->rm != BUSTER_X86_OPERAND_NONE;
    const uint8_t decorations = instruction->decorations;
    const int64_t immediate = form->immediate_bytes == 8 ? (int64_t)((uint64_t)(uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32))
                                                         : instruction->immediate;
    if (form->family == BUSTER_X86_FAMILY_EVEX)
    {
        fprintf(file, "{evex} ");
    }
    if (register_direct && form->reg != BUSTER_X86_OPERAND_NONE &&
        (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_VVVV_RM))
    {
        fprintf(file, "%s ", form->syntax == X86_SYNTAX_RM_REG ? "{store}" : "{load}");
    }
    if (form->syntax == X86_SYNTAX_REL && form->relative_bytes == 4 && form->opcode != 0xe8)
    {
        fprintf(file, "{disp32} ");
    }
    fprintf(file, "%s", form->mnemonic);
    switch (form->syntax)
    {
        case X86_SYNTAX_IMM_IMM:
            fprintf(file, " %d, %d", instruction->immediate & 0xffff, (instruction->immediate >> 16) & 0xff);
            break;
        case X86_SYNTAX_REL:
            // `.` is the start of the instruction, and the displacement counts from its end.
            fprintf(file, " .%+lld", (long long)instruction->displacement + form->opcode_bytes + form->relative_bytes);
            break;
        case X86_SYNTAX_ACC_IMM:
            fprintf(file, " %s, %lld", x86_accumulator_name(form->operand_bytes), (long long)immediate);
            break;
        case X86_SYNTAX_ACC_MOFFS:
            fprintf(file, " %s, ds:0x%llx", x86_accumulator_name(form->operand_bytes), (unsigned long long)immediate);
            break;
        case X86_SYNTAX_MOFFS_ACC:
            fprintf(file, " ds:0x%llx, %s", (unsigned long long)immediate, x86_accumulator_name(form->operand_bytes));
            break;
        default:
        {
            const char* operands = x86_syntax_operands(form->syntax);
            const bool rounding = (form->evex & (BUSTER_X86_EVEX_ROUNDING | BUSTER_X86_EVEX_SAE)) != 0;
            const char* rounding_name = (form->evex & BUSTER_X86_EVEX_ROUNDING) ? x86_rounding_names[(decorations & BUSTER_X86_DECORATION_ROUNDING) >> 5] : "{sae}";
            bool rounding_printed = false;
            for (uint32_t i = 0; operands[i] != 0; i += 1)
            {
                fprintf(file, i == 0 ? " " : ", ");
                if (operands[i] == 'i' && rounding)
                {
                    fprintf(file, "%s, ", rounding_name);
                    rounding_printed = true;
                }
                switch (operands[i])
                {
                    case 'r': fprintf(file, "%s", x86_register_name(instruction->reg, form->reg)); break;
                    case 'v': fprintf(file, "%s", x86_register_name(instruction->vvvv, form->vvvv)); break;
                    case 'm':
                        x86_print_rm(file, instruction, form);
                        if (instruction->mode != 0 && (form->evex & BUSTER_X86_EVEX_BROADCAST))
                        {
                            fprintf(file, "{1to%u}", (uint32_t)form->operand_bytes / form->disp8_scale);
                        }
                        break;
                    case 'i': fprintf(file, "%lld", (long long)immediate); break;
                    case 'j': fprintf(file, "%d", instruction->immediate & 0xff); break;
                    case 'k': fprintf(file, "%d", (instruction->immediate >> 8) & 0xff); break;
                    case 'x': fprintf(file, "xmm0"); break;
                    case 'c': fprintf(file, "cl"); break;
                    case 's': fprintf(file, "st"); break;
                    case 'a': fprintf(file, "%s", x86_accumulator_name(form->operand_bytes)); break;
                    default: fprintf(file, "1"); break;
                }
                if (i == 0 && (decorations & BUSTER_X86_DECORATION_MASK))
                {
                    fprintf(file, "{%s}", x86_opmask_names[decorations & BUSTER_X86_DECORATION_MASK]);
                }
                if (i == 0 && (decorations & BUSTER_X86_DECORATION_ZEROING))
                {
                    fprintf(file, "{z}");
                }
            }
            if (rounding && !rounding_printed)
            {
                fprintf(file, ", %s", rounding_name);
            }
            break;
        }
    }
}

static void x86_print_bytes(const uint8_t* bytes, uint64_t length)
{
    for (uint64_t i = 0; i < length; i += 1)
    {
        printf("%s%02x", i == 0 ? "" : " ", bytes[i]);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

static bool x86_check_templates(void)
{
    const bool result = x86_encoder_initialize();
    printf("templates: %u forms, and each with FS and GS, %s\n", (unsigned)X86_FORM_COUNT, result ? "all fit" : "SOME DON'T FIT");
    if (!result)
    {
        for (uint32_t form = 0; form < BUSTER_X86_FORM_TEMPLATES; form += 1)
        {
            X86Template template;
            if (!build_template(x86_form_description(form), x86_form_segment(form), &template))
            {
                printf("  form %u (%s, segment override %u) doesn't fit\n", form, x86_form_description(form)->mnemonic, x86_form_segment(form));
            }
        }
    }
    return result;
}

typedef struct KnownEncoding KnownEncoding;
struct KnownEncoding
{
    X86Instruction instruction;
    uint8_t length;
    uint8_t bytes[BUSTER_X86_MAX_INSTRUCTION_BYTES];
};

// Instructions on the paths most worth pinning down, with their bytes from GNU as.
static const KnownEncoding x86_known_encodings[] = {
    // mov eax, ebx; {load} mov eax, ebx; mov r12, rax; mov spl, al; mov ah, bl
    { { .form = X86_FORM_MOV_RM32_R32, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RBX }, 2, { 0x89, 0xd8 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX }, 2, { 0x8b, 0xc3 } },
    { { .form = X86_FORM_MOV_RM64_R64, .rm = X86_REGISTER_R12, .reg = X86_REGISTER_RAX }, 3, { 0x49, 0x89, 0xc4 } },
    { { .form = X86_FORM_MOV_RM8_R8, .rm = X86_REGISTER_RSP, .reg = X86_REGISTER_RAX }, 3, { 0x40, 0x88, 0xc4 } },
    { { .form = X86_FORM_MOV_RM8_R8, .rm = X86_REGISTER_AH, .reg = X86_REGISTER_RBX }, 2, { 0x88, 0xdc } },
    // mov eax, [rbx]; [rbp]; [r13]; [rsp]; [r12]; [rbx+0x10]; [rbx-0x80]; [rbx+0x80]
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8b, 0x03 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x41, 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_R12, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x41, 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x10 }, 3, { 0x8b, 0x43, 0x10 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -0x80 }, 3, { 0x8b, 0x43, 0x80 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x80 }, 6, { 0x8b, 0x83, 0x80, 0x00, 0x00, 0x00 } },
    // mov r9, [r10+r11*8+0x12345678]; mov eax, [rbp+rcx*4]; mov eax, [rcx*4+0x10]; mov eax, ds:0x1000; mov eax, [rip+0x100]
    { { .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .index = X86_REGISTER_R11, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x12345678 },
      8, { 0x4f, 0x8b, 0x8c, 0xda, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) }, 4, { 0x8b, 0x44, 0x8d, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(2), .displacement = 0x10 }, 7, { 0x8b, 0x04, 0x8d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x1000 }, 7, { 0x8b, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x100 }, 6, { 0x8b, 0x05, 0x00, 0x01, 0x00, 0x00 } },
    // add dword ptr ds:0x1000, eax (the historical encoder made this [rax+0x1000])
    { { .form = X86_FORM_ADD_RM32_R32, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x1000 }, 7, { 0x01, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00 } },
    // mov byte ptr [r12+rbx*4+0x12], 5; mov qword ptr [rsp+0x12345678], 0x11223344
    { { .form = X86_FORM_MOV_RM8_IMM8, .rm = X86_REGISTER_R12, .index = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12, .immediate = 5 },
      6, { 0x41, 0xc6, 0x44, 0x9c, 0x12, 0x05 } },
    { { .form = X86_FORM_MOV_RM64_IMM32, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x12345678, .immediate = 0x11223344 },
      12, { 0x48, 0xc7, 0x84, 0x24, 0x78, 0x56, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11 } },
    // movabs rax, 0x1122334455667788; mov r15d, 1; mov r8b, -1; movabs al, ds:0x1122334455667788
    { { .form = X86_FORM_MOV_R64_IMM64, .rm = X86_REGISTER_RAX, .immediate = 0x55667788, .displacement = 0x11223344 },
      10, { 0x48, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 } },
    { { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_R15, .immediate = 1 }, 6, { 0x41, 0xbf, 0x01, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R8_IMM8, .rm = X86_REGISTER_R8, .immediate = 255 }, 3, { 0x41, 0xb0, 0xff } },
    { { .form = X86_FORM_MOVABS_AL_MOFFS, .immediate = 0x55667788, .displacement = 0x11223344 }, 9, { 0xa0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 } },
    // add eax, 1 (83); add ecx, 1000 (81); add eax, 1000 (05); add rax, -1; add ax, 0x1234; add al, 200
    { { .form = X86_FORM_ADD_RM32_IMM8, .rm = X86_REGISTER_RAX, .immediate = 1 }, 3, { 0x83, 0xc0, 0x01 } },
    { { .form = X86_FORM_ADD_RM32_IMM32, .rm = X86_REGISTER_RCX, .immediate = 1000 }, 6, { 0x81, 0xc1, 0xe8, 0x03, 0x00, 0x00 } },
    { { .form = X86_FORM_ADD_EAX_IMM32, .immediate = 1000 }, 5, { 0x05, 0xe8, 0x03, 0x00, 0x00 } },
    { { .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RAX, .immediate = -1 }, 4, { 0x48, 0x83, 0xc0, 0xff } },
    { { .form = X86_FORM_ADD_AX_IMM16, .immediate = 0x1234 }, 4, { 0x66, 0x05, 0x34, 0x12 } },
    { { .form = X86_FORM_ADD_AL_IMM8, .immediate = 200 }, 2, { 0x04, 0xc8 } },
    // movzx eax, sil; movzx r8, ah is invalid, so movzx ecx, ah; movsxd rax, ecx; lea rax, [rip+0x10]; lea eax, [rax+rax*1]
    { { .form = X86_FORM_MOVZX_R32_RM8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSI }, 4, { 0x40, 0x0f, 0xb6, 0xc6 } },
    { { .form = X86_FORM_MOVZX_R32_RM8, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_AH }, 3, { 0x0f, 0xb6, 0xcc } },
    { { .form = X86_FORM_MOVSXD_R64_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 3, { 0x48, 0x63, 0xc1 } },
    { { .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 7, { 0x48, 0x8d, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_LEA_R32_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 3, { 0x8d, 0x04, 0x00 } },
    // shl r9, 1; sar eax, cl; rol word ptr [rax], 3; imul r10, r11, 100; imul eax, ecx, 1000
    { { .form = X86_FORM_SHL_RM64_1, .rm = X86_REGISTER_R9 }, 3, { 0x49, 0xd1, 0xe1 } },
    { { .form = X86_FORM_SAR_RM32_CL, .rm = X86_REGISTER_RAX }, 2, { 0xd3, 0xf8 } },
    { { .form = X86_FORM_ROL_RM16_IMM8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 4, { 0x66, 0xc1, 0x00, 0x03 } },
    { { .form = X86_FORM_IMUL_R64_RM64_IMM8, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R11, .immediate = 100 }, 4, { 0x4d, 0x6b, 0xd3, 0x64 } },
    { { .form = X86_FORM_IMUL_R32_RM32_IMM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 1000 }, 6, { 0x69, 0xc1, 0xe8, 0x03, 0x00, 0x00 } },
    // push r12; pop rbx; push 1000; call .+0x105 (rel32 0x100); jmp .+0x12 (rel8 0x10); jne .+0x106 (rel32 0x100); ret; ret 8
    { { .form = X86_FORM_PUSH_R64, .rm = X86_REGISTER_R12 }, 2, { 0x41, 0x54 } },
    { { .form = X86_FORM_POP_R64, .rm = X86_REGISTER_RBX }, 1, { 0x5b } },
    { { .form = X86_FORM_PUSH_IMM32, .immediate = 1000 }, 5, { 0x68, 0xe8, 0x03, 0x00, 0x00 } },
    { { .form = X86_FORM_CALL_REL32, .displacement = 0x100 }, 5, { 0xe8, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_JMP_REL8, .displacement = 0x10 }, 2, { 0xeb, 0x10 } },
    { { .form = X86_FORM_JNE_REL32, .displacement = 0x100 }, 6, { 0x0f, 0x85, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_RET }, 1, { 0xc3 } },
    { { .form = X86_FORM_RET_IMM16, .immediate = 8 }, 3, { 0xc2, 0x08, 0x00 } },
    // call qword ptr [rax+8]; jmp r11; sete r10b; cmovl r8, qword ptr [rsp+8]
    { { .form = X86_FORM_CALL_RM64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 3, { 0xff, 0x50, 0x08 } },
    { { .form = X86_FORM_JMP_RM64, .rm = X86_REGISTER_R11 }, 3, { 0x41, 0xff, 0xe3 } },
    { { .form = X86_FORM_SETE_RM8, .rm = X86_REGISTER_R10 }, 4, { 0x41, 0x0f, 0x94, 0xc2 } },
    { { .form = X86_FORM_CMOVL_R64_RM64, .reg = X86_REGISTER_R8, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 },
      6, { 0x4c, 0x0f, 0x4c, 0x44, 0x24, 0x08 } },
    // enter 16, 2; endbr64; lock cmpxchg word ptr [rax], cx; crc32 eax, word ptr [rax]; lock cmpxchg16b xmmword ptr [r8]
    { { .form = X86_FORM_ENTER, .immediate = 16 | (2 << 16) }, 4, { 0xc8, 0x10, 0x00, 0x02 } },
    { { .form = X86_FORM_ENDBR64 }, 4, { 0xf3, 0x0f, 0x1e, 0xfa } },
    { { .form = X86_FORM_LOCK_CMPXCHG_M16_R16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0xf0, 0x0f, 0xb1, 0x08 } },
    { { .form = X86_FORM_CRC32_R32_RM16, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0xf2, 0x0f, 0x38, 0xf1, 0x00 } },
    { { .form = X86_FORM_LOCK_CMPXCHG16B_M128, .rm = X86_REGISTER_R8, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf0, 0x49, 0x0f, 0xc7, 0x08 } },
    // movss xmm9, dword ptr [rbp-8]; movsd qword ptr [rsp], xmm15; cvtsi2sd xmm1, rax; palignr xmm1, xmmword ptr [rsp], 3; pextrq r9, xmm10, 1
    { { .form = X86_FORM_MOVSS_X_XM32, .reg = 9, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -8 }, 6, { 0xf3, 0x44, 0x0f, 0x10, 0x4d, 0xf8 } },
    { { .form = X86_FORM_MOVSD_XM64_X, .reg = 15, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0xf2, 0x44, 0x0f, 0x11, 0x3c, 0x24 } },
    { { .form = X86_FORM_CVTSI2SD_X_RM64, .reg = 1, .rm = X86_REGISTER_RAX }, 5, { 0xf2, 0x48, 0x0f, 0x2a, 0xc8 } },
    { { .form = X86_FORM_PALIGNR_X_XM128_IMM8, .reg = 1, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 },
      7, { 0x66, 0x0f, 0x3a, 0x0f, 0x0c, 0x24, 0x03 } },
    { { .form = X86_FORM_PEXTRQ_RM64_X_IMM8, .reg = 10, .rm = X86_REGISTER_R9, .immediate = 1 }, 7, { 0x66, 0x4d, 0x0f, 0x3a, 0x16, 0xd1, 0x01 } },
    // VEX2 and VEX3: vaddps xmm1, xmm2, xmm3; vaddps xmm9, xmm2, xmm3 (R in VEX2); vaddps xmm1, xmm2, xmm9 (B);
    // {load} vmovaps xmm1, xmm9; {load} vmovss xmm1, xmm2, xmm9; vmovq xmm1, rax (W1); vpermq ymm1, ymmword ptr [rax],
    // 0x1b (0F 3A); vfmadd231sd xmm1, xmm2, qword ptr [rax+8]; vpslld xmm1, xmm2, 3 (into vvvv); vzeroupper
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xe8, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 9, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0x68, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 9 }, 5, { 0xc4, 0xc1, 0x68, 0x58, 0xc9 } },
    { { .form = X86_FORM_VMOVAPS_X_XM128, .reg = 1, .rm = 9 }, 5, { 0xc4, 0xc1, 0x78, 0x28, 0xc9 } },
    { { .form = X86_FORM_VMOVSS_X_X_X, .reg = 1, .vvvv = 2, .rm = 9 }, 5, { 0xc4, 0xc1, 0x6a, 0x10, 0xc9 } },
    { { .form = X86_FORM_VMOVQ_X_RM64, .reg = 1, .rm = X86_REGISTER_RAX }, 5, { 0xc4, 0xe1, 0xf9, 0x6e, 0xc8 } },
    { { .form = X86_FORM_VPERMQ_Y_YM256_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x1b }, 6, { 0xc4, 0xe3, 0xfd, 0x00, 0x08, 0x1b } },
    { { .form = X86_FORM_VFMADD231SD_X_X_XM64, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 },
      6, { 0xc4, 0xe2, 0xe9, 0xb9, 0x48, 0x08 } },
    { { .form = X86_FORM_VPSLLD_X_X_IMM8, .vvvv = 1, .rm = 2, .immediate = 3 }, 5, { 0xc5, 0xf1, 0x72, 0xf2, 0x03 } },
    { { .form = X86_FORM_VZEROUPPER }, 3, { 0xc5, 0xf8, 0x77 } },
    // BMI and opmasks: andn eax, ebx, ecx; bextr rax, rbx, rcx; blsr r9, qword ptr [r10]; sarx eax, dword ptr [rsp], ecx;
    // rorx r8, r9, 3; mulx rax, rbx, qword ptr [rcx]; kmovq k1, rax; kandw k1, k2, k3; kshiftlw k1, k2, 3
    { { .form = X86_FORM_ANDN_R32_R32_RM32, .reg = X86_REGISTER_RAX, .vvvv = X86_REGISTER_RBX, .rm = X86_REGISTER_RCX }, 5, { 0xc4, 0xe2, 0x60, 0xf2, 0xc1 } },
    { { .form = X86_FORM_BEXTR_R64_RM64_R64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX, .vvvv = X86_REGISTER_RCX }, 5, { 0xc4, 0xe2, 0xf0, 0xf7, 0xc3 } },
    { { .form = X86_FORM_BLSR_R64_RM64, .vvvv = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xc2, 0xb0, 0xf3, 0x0a } },
    { { .form = X86_FORM_SARX_R32_RM32_R32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .vvvv = X86_REGISTER_RCX },
      6, { 0xc4, 0xe2, 0x72, 0xf7, 0x04, 0x24 } },
    { { .form = X86_FORM_RORX_R64_RM64_IMM8, .reg = X86_REGISTER_R8, .rm = X86_REGISTER_R9, .immediate = 3 }, 6, { 0xc4, 0x43, 0xfb, 0xf0, 0xc1, 0x03 } },
    { { .form = X86_FORM_MULX_R64_R64_RM64, .reg = X86_REGISTER_RAX, .vvvv = X86_REGISTER_RBX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE },
      5, { 0xc4, 0xe2, 0xe3, 0xf6, 0x01 } },
    { { .form = X86_FORM_KMOVQ_K_R64, .reg = 1, .rm = X86_REGISTER_RAX }, 5, { 0xc4, 0xe1, 0xfb, 0x92, 0xc8 } },
    { { .form = X86_FORM_KANDW_K_K_K, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xec, 0x41, 0xcb } },
    { { .form = X86_FORM_KSHIFTLW_K_K_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 6, { 0xc4, 0xe3, 0xf9, 0x32, 0xca, 0x03 } },
    // EVEX: vaddps zmm1{k1}{z}, zmm2, zmm3; vaddps zmm1, zmm2, zmmword ptr [rax+64] (8-bit 1 in units of 64); vaddps
    // zmm1, zmm2, dword ptr [rax+64]{1to16} (16 in units of 4); vaddps zmm1, zmm2, zmm3, {rz-sae}; vmaxps zmm1, zmm2,
    // zmm3, {sae}; vaddsd xmm1{k1}{z}, xmm2, xmm3, {rz-sae}
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 },
      7, { 0x62, 0xf1, 0x6c, 0x48, 0x58, 0x48, 0x01 } },
    { { .form = X86_FORM_VADDPS_Z_Z_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 },
      7, { 0x62, 0xf1, 0x6c, 0x58, 0x58, 0x48, 0x10 } },
    { { .form = X86_FORM_VADDPS_Z_Z_Z_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = BUSTER_X86_ROUND_ZERO }, 6, { 0x62, 0xf1, 0x6c, 0x78, 0x58, 0xcb } },
    { { .form = X86_FORM_VMAXPS_Z_Z_Z_SAE, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf1, 0x6c, 0x18, 0x5f, 0xcb } },
    { { .form = X86_FORM_VADDSD_X_X_X_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING | BUSTER_X86_ROUND_ZERO },
      6, { 0x62, 0xf1, 0xef, 0xf9, 0x58, 0xcb } },
    // vmovaps zmmword ptr [rax]{k1}, zmm1; vpcmpeqd k1{k2}, zmm1, zmm2; vpternlogd zmm1{k1}{z}, zmm2, dword ptr
    // [rbp-8]{1to16}, 0x96; vpslld zmm1{k1}, zmmword ptr [rax], 3; vpcompressb zmmword ptr [rax+64]{k1}, zmm1 (in units
    // of 1); vpsrldq zmm1, zmm2, 3
    { { .form = X86_FORM_VMOVAPS_ZM512_Z, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 }, 6, { 0x62, 0xf1, 0x7c, 0x49, 0x29, 0x08 } },
    { { .form = X86_FORM_VPCMPEQD_K_Z_ZM512, .reg = 1, .vvvv = 1, .rm = 2, .decorations = 2 }, 6, { 0x62, 0xf1, 0x75, 0x4a, 0x76, 0xca } },
    { { .form = X86_FORM_VPTERNLOGD_Z_Z_M32BCST_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -8,
        .immediate = 0x96, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      8, { 0x62, 0xf3, 0x6d, 0xd9, 0x25, 0x4d, 0xfe, 0x96 } },
    { { .form = X86_FORM_VPSLLD_Z_ZM512_IMM8, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3, .decorations = 1 },
      7, { 0x62, 0xf1, 0x75, 0x49, 0x72, 0x30, 0x03 } },
    { { .form = X86_FORM_VPCOMPRESSB_ZM512_Z, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .decorations = 1 },
      7, { 0x62, 0xf2, 0x7d, 0x49, 0x63, 0x48, 0x40 } },
    { { .form = X86_FORM_VPSRLDQ_Z_ZM512_IMM8, .vvvv = 1, .rm = 2, .immediate = 3 }, 7, { 0x62, 0xf1, 0x75, 0x48, 0x73, 0xda, 0x03 } },
    // Registers 16 to 31 (R', V' and a register rm's X): vpaddd xmm17, xmm18, xmm19; vpaddd ymm31{k7}, ymm30, ymmword
    // ptr [r13+r12*8-128]; vmovdqu64 zmm16{k1}{z}, zmmword ptr [rsp+rbx*4+8192] (128 units of 64: 32-bit);
    // vpbroadcastd zmm1, eax; {evex} vmovq xmm1, qword ptr [rax+8]
    { { .form = X86_FORM_VPADDD_X_X_XM128_EVEX, .reg = 17, .vvvv = 18, .rm = 19 }, 6, { 0x62, 0xa1, 0x6d, 0x00, 0xfe, 0xcb } },
    { { .form = X86_FORM_VPADDD_Y_Y_YM256_EVEX, .reg = 31, .vvvv = 30, .rm = X86_REGISTER_R13, .index = X86_REGISTER_R12, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3),
        .displacement = -128, .decorations = 7 },
      8, { 0x62, 0x01, 0x0d, 0x27, 0xfe, 0x7c, 0xe5, 0xfc } },
    { { .form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = 16, .rm = X86_REGISTER_RSP, .index = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2),
        .displacement = 8192, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      11, { 0x62, 0xe1, 0xfe, 0xc9, 0x6f, 0x84, 0x9c, 0x00, 0x20, 0x00, 0x00 } },
    { { .form = X86_FORM_VPBROADCASTD_Z_R32, .reg = 1, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf2, 0x7d, 0x48, 0x7c, 0xc8 } },
    { { .form = X86_FORM_VMOVQ_X_RM64_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 7, { 0x62, 0xf1, 0xfd, 0x08, 0x6e, 0x48, 0x01 } },
    // x87: fadd st, st(1); fsub st(1), st; fsubrp st(1), st; fdivr st(1), st; fcom st(1); fucomip st, st(2);
    // fcmovnbe st, st(2)
    { { .form = X86_FORM_FADD_ST0_ST, .rm = 1 }, 2, { 0xd8, 0xc1 } },
    { { .form = X86_FORM_FSUB_ST_ST0, .rm = 1 }, 2, { 0xdc, 0xe9 } },
    { { .form = X86_FORM_FSUBRP_ST_ST0, .rm = 1 }, 2, { 0xde, 0xe1 } },
    { { .form = X86_FORM_FDIVR_ST_ST0, .rm = 1 }, 2, { 0xdc, 0xf1 } },
    { { .form = X86_FORM_FCOM_ST, .rm = 1 }, 2, { 0xd8, 0xd1 } },
    { { .form = X86_FORM_FUCOMIP_ST0_ST, .rm = 2 }, 2, { 0xdf, 0xea } },
    { { .form = X86_FORM_FCMOVNBE_ST0_ST, .rm = 2 }, 2, { 0xdb, 0xd2 } },
    // fld tbyte ptr [rax]; fstp tbyte ptr [rsp+8]; fstp tbyte ptr [rip+0x10]; fild qword ptr [r12+r13*8+0x80];
    // fistp qword ptr [rbx]; fisttp dword ptr [rcx]
    { { .form = X86_FORM_FLD_M80, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xdb, 0x28 } },
    { { .form = X86_FORM_FSTP_M80, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 4, { 0xdb, 0x7c, 0x24, 0x08 } },
    { { .form = X86_FORM_FSTP_M80, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 6, { 0xdb, 0x3d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_FILD_M64, .rm = X86_REGISTER_R12, .index = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x80 },
      8,
      { 0x43, 0xdf, 0xac, 0xec, 0x80, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_FISTP_M64, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xdf, 0x3b } },
    { { .form = X86_FORM_FISTTP_M32, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xdb, 0x09 } },
    // fnstsw ax; fnstsw word ptr [rax]; fldcw word ptr [rax]; fnstenvw [rax]; fxch st(3); fwait; fld1; fsincos
    { { .form = X86_FORM_FNSTSW_AX }, 2, { 0xdf, 0xe0 } },
    { { .form = X86_FORM_FNSTSW_M16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xdd, 0x38 } },
    { { .form = X86_FORM_FLDCW_M16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xd9, 0x28 } },
    { { .form = X86_FORM_FNSTENVW_M14, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xd9, 0x30 } },
    { { .form = X86_FORM_FXCH_ST, .rm = 3 }, 2, { 0xd9, 0xcb } },
    { { .form = X86_FORM_FWAIT }, 1, { 0x9b } },
    { { .form = X86_FORM_FLD1 }, 2, { 0xd9, 0xe8 } },
    { { .form = X86_FORM_FSINCOS }, 2, { 0xd9, 0xfb } },
    // lock add word ptr [rax], cx; lock add qword ptr [rax], 0x12; lock btc word ptr [rax], cx; lock cmpxchg8b qword
    // ptr [rax]
    { { .form = X86_FORM_LOCK_ADD_M16_R16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_LOCK_ADD_M64_IMM8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x12 }, 5, { 0xf0, 0x48, 0x83, 0x00, 0x12 } },
    { { .form = X86_FORM_LOCK_BTC_M16_R16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0xf0, 0x0f, 0xbb, 0x08 } },
    { { .form = X86_FORM_LOCK_CMPXCHG8B_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf0, 0x0f, 0xc7, 0x08 } },
    // movsb; cmpsd; rep movsw; rep movsq; repne scasq
    { { .form = X86_FORM_MOVS_B }, 1, { 0xa4 } },
    { { .form = X86_FORM_CMPS_D }, 1, { 0xa7 } },
    { { .form = X86_FORM_REP_MOVS_W }, 3, { 0x66, 0xf3, 0xa5 } },
    { { .form = X86_FORM_REP_MOVS_Q }, 3, { 0xf3, 0x48, 0xa5 } },
    { { .form = X86_FORM_REPNE_SCAS_Q }, 3, { 0xf2, 0x48, 0xaf } },
    // xchg cx, ax; xchg rcx, rax; movabs ax, ds:0x1122334455667788; loop .+0x10; push ax; pushw 0x1234
    { { .form = X86_FORM_XCHG_R16_AX, .rm = X86_REGISTER_RCX }, 2, { 0x66, 0x91 } },
    { { .form = X86_FORM_XCHG_R64_RAX, .rm = X86_REGISTER_RCX }, 2, { 0x48, 0x91 } },
    { { .form = X86_FORM_MOVABS_AX_MOFFS, .immediate = 0x55667788, .displacement = 0x11223344 },
      10,
      { 0x66, 0xa1, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 } },
    { { .form = X86_FORM_LOOP_REL8, .displacement = 0x0e }, 2, { 0xe2, 0x0e } },
    { { .form = X86_FORM_PUSH_R16, .rm = X86_REGISTER_RAX }, 2, { 0x66, 0x50 } },
    { { .form = X86_FORM_PUSHW_IMM16, .immediate = 0x1234 }, 4, { 0x66, 0x68, 0x34, 0x12 } },
    // rcl eax, 5; tzcnt ax, cx; adcx rax, qword ptr [rcx]; cmpbexadd dword ptr [rax], ecx, edx
    { { .form = X86_FORM_RCL_RM32_IMM8, .rm = X86_REGISTER_RAX, .immediate = 5 }, 3, { 0xc1, 0xd0, 0x05 } },
    { { .form = X86_FORM_TZCNT_R16_RM16, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 5, { 0x66, 0xf3, 0x0f, 0xbc, 0xc1 } },
    { { .form = X86_FORM_ADCX_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0x48, 0x0f, 0x38, 0xf6, 0x01 } },
    { { .form = X86_FORM_CMPBEXADD_M32_R32_R32, .reg = X86_REGISTER_RCX, .vvvv = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      5,
      { 0xc4, 0xe2, 0x69, 0xe6, 0x08 } },
    // call fword ptr [rax+r9*4+0x10]; rex.w call fword ptr [r9]; rex.w lss r15d, fword ptr [r13+8]; retfq 8
    { { .form = X86_FORM_CALL_FAR_M48, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x10 },
      5,
      { 0x42, 0xff, 0x5c, 0x88, 0x10 } },
    { { .form = X86_FORM_CALL_FAR_M80, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x49, 0xff, 0x19 } },
    { { .form = X86_FORM_LSS_R64_M80, .reg = X86_REGISTER_R15, .rm = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 5, { 0x4d, 0x0f, 0xb2, 0x7d, 0x08 } },
    { { .form = X86_FORM_RETFQ_IMM16, .immediate = 8 }, 4, { 0x48, 0xca, 0x08, 0x00 } },
    // xbegin .+0x100; prefetchit0 byte ptr [rip+0x10]; movdir64b rax, [rcx]; rdpid rax
    { { .form = X86_FORM_XBEGIN_REL32, .displacement = 0xfa }, 6, { 0xc7, 0xf8, 0xfa, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_PREFETCHIT0_M8, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 7, { 0x0f, 0x18, 0x3d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOVDIR64B_R64_M512, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0xf8, 0x01 } },
    { { .form = X86_FORM_RDPID_R64, .rm = X86_REGISTER_RAX }, 4, { 0xf3, 0x0f, 0xc7, 0xf8 } },
    // Segment overrides: mov rax, qword ptr fs:[rax]; mov rax, qword ptr fs:0; mov rax, qword ptr fs:[rip+0x10]; lock
    // add word ptr fs:[rax], cx; inc byte ptr fs:[rax]; push qword ptr gs:[rax]; fld tbyte ptr fs:[rax]; vmovdqu ymm0,
    // ymmword ptr fs:[rax]; vmovdqu64 zmm0, zmmword ptr gs:[r9+rax*8+0x40]; and notrack jmp rax
    { { .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x64, 0x48, 0x8b, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, .mode = BUSTER_X86_MEMORY_ABSOLUTE }, 9, { 0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 8, { 0x64, 0x48, 0x8b, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_LOCK_ADD_M16_R16 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      5,
      { 0x64, 0x66, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_INC_RM8 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x64, 0xfe, 0x00 } },
    { { .form = X86_FORM_PUSH_RM64 + BUSTER_X86_FORM_GS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x65, 0xff, 0x30 } },
    { { .form = X86_FORM_FLD_M80 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x64, 0xdb, 0x28 } },
    { { .form = X86_FORM_VMOVDQU_Y_YM256 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x64, 0xc5, 0xfe, 0x6f, 0x00 } },
    { { .form = X86_FORM_VMOVDQU64_Z_ZM512 + BUSTER_X86_FORM_GS, .rm = X86_REGISTER_R9, .index = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x40 },
      9,
      { 0x65, 0x62, 0xd1, 0xfe, 0x48, 0x6f, 0x44, 0xc1, 0x01 } },
    { { .form = X86_FORM_NOTRACK_JMP_R64, .rm = X86_REGISTER_RAX }, 3, { 0x3e, 0xff, 0xe0 } },
    // SSE to SSE4.2: psllw xmm9, 3 (a /digit with REX.B); pslldq xmm1, 4; pextrw eax, xmm1, 3 and pextrw word ptr [rax],
    // xmm1, 3 (SSE2's register and SSE4.1's memory form); pinsrw xmm1, word ptr [rax], 3; extrq xmm1, 3, 4 and insertq
    // xmm1, xmm2, 3, 4 (two immediates); blendvpd xmm9, xmmword ptr [rax], xmm0; pcmpestriq xmm0, xmm1, 5 (66, then
    // REX.W); movnti qword ptr [rax], r9; cvtsd2si r10, xmm3; pmovsxbq xmm1, word ptr [rax]; fxsave64 [rax]; ldmxcsr
    // dword ptr [rax]; cmpsd xmm1, qword ptr [rax], 3; movhps qword ptr [rax], xmm1; maskmovdqu xmm1, xmm2; phminposuw
    // xmm12, xmm1; and pmovzxbd xmm1, dword ptr fs:[rax]
    { { .form = X86_FORM_PSLLW_X_IMM8, .rm = 9, .immediate = 3 }, 6, { 0x66, 0x41, 0x0f, 0x71, 0xf1, 0x03 } },
    { { .form = X86_FORM_PSLLDQ_X_IMM8, .rm = 1, .immediate = 4 }, 5, { 0x66, 0x0f, 0x73, 0xf9, 0x04 } },
    { { .form = X86_FORM_PEXTRW_R32_X_IMM8, .reg = X86_REGISTER_RAX, .rm = 1, .immediate = 3 }, 5, { 0x66, 0x0f, 0xc5, 0xc1, 0x03 } },
    { { .form = X86_FORM_PEXTRW_M16_X_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 6, { 0x66, 0x0f, 0x3a, 0x15, 0x08, 0x03 } },
    { { .form = X86_FORM_PINSRW_X_RM32_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 5, { 0x66, 0x0f, 0xc4, 0x08, 0x03 } },
    { { .form = X86_FORM_EXTRQ_X_IMM8_IMM8, .rm = 1, .immediate = 3 | 4 << 8 }, 6, { 0x66, 0x0f, 0x78, 0xc1, 0x03, 0x04 } },
    { { .form = X86_FORM_INSERTQ_X_X_IMM8_IMM8, .reg = 1, .rm = 2, .immediate = 3 | 4 << 8 }, 6, { 0xf2, 0x0f, 0x78, 0xca, 0x03, 0x04 } },
    { { .form = X86_FORM_BLENDVPD_X_XM128_XMM0, .reg = 9, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0x44, 0x0f, 0x38, 0x15, 0x08 } },
    { { .form = X86_FORM_PCMPESTRIQ_X_XM128_IMM8, .reg = 0, .rm = 1, .immediate = 5 }, 7, { 0x66, 0x48, 0x0f, 0x3a, 0x61, 0xc1, 0x05 } },
    { { .form = X86_FORM_MOVNTI_M64_R64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x4c, 0x0f, 0xc3, 0x08 } },
    { { .form = X86_FORM_CVTSD2SI_R64_XM64, .reg = X86_REGISTER_R10, .rm = 3 }, 5, { 0xf2, 0x4c, 0x0f, 0x2d, 0xd3 } },
    { { .form = X86_FORM_PMOVSXBQ_X_XM16, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0x22, 0x08 } },
    { { .form = X86_FORM_FXSAVE64_M, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x48, 0x0f, 0xae, 0x00 } },
    { { .form = X86_FORM_LDMXCSR_M32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0xae, 0x10 } },
    { { .form = X86_FORM_CMPSD_X_XM64_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 5, { 0xf2, 0x0f, 0xc2, 0x08, 0x03 } },
    { { .form = X86_FORM_MOVHPS_M64_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x17, 0x08 } },
    { { .form = X86_FORM_MASKMOVDQU_X_X, .reg = 1, .rm = 2 }, 4, { 0x66, 0x0f, 0xf7, 0xca } },
    { { .form = X86_FORM_PHMINPOSUW_X_XM128, .reg = 12, .rm = 1 }, 6, { 0x66, 0x44, 0x0f, 0x38, 0x41, 0xe1 } },
    { { .form = X86_FORM_PMOVZXBD_X_XM32 + BUSTER_X86_FORM_FS, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x64, 0x66, 0x0f, 0x38, 0x31, 0x08 } },
    // AVX, AVX2 and F16C: vmaskmovps xmmword ptr [rax], xmm1, xmm2 and vpmaskmovq ymmword ptr [rax], ymm1, ymm2 (memory,
    // mask, source); vpcmpestriq xmm1, xmm2, 5; vpextrw eax, xmm1, 3; vpinsrw xmm1, xmm2, eax, 3; vcvtpd2dq xmm1,
    // ymmword ptr [rax] (L1 into an XMM register); vcvtps2ph qword ptr [rax], xmm1, 3 and vcvtps2ph xmm1, ymm2, 3;
    // vldmxcsr dword ptr [rax]; vmovhps xmm1, xmm2, qword ptr [rax]; vmovhlps xmm1, xmm2, xmm3; vpermpd ymm1, ymm2, 3;
    // vperm2f128 ymm1, ymm2, ymm3, 0x21; vpsllw ymm1, ymm2, xmmword ptr [rax] and vpsllw ymm1, ymm2, 3; vpmovsxbw ymm1,
    // xmmword ptr [rax]; vroundss xmm1, xmm2, dword ptr [rax], 3; vcvtss2si rax, xmm1 (W1, so VEX3); vpsrlvq ymm1,
    // ymm2, ymm3; vbroadcastf128 ymm1, xmmword ptr [rax]; vpmaxud ymm9, ymm10, ymmword ptr [r13+r11*2+8] (VEX's
    // inverted R, X and B); and vmovntdq ymmword ptr gs:[rax], ymm9
    { { .form = X86_FORM_VMASKMOVPS_M128_X_X, .reg = 2, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x71, 0x2e, 0x10 } },
    { { .form = X86_FORM_VPMASKMOVQ_M256_Y_Y, .reg = 2, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0xf5, 0x8e, 0x10 } },
    { { .form = X86_FORM_VPCMPESTRIQ_X_XM128_IMM8, .reg = 1, .rm = 2, .immediate = 5 }, 6, { 0xc4, 0xe3, 0xf9, 0x61, 0xca, 0x05 } },
    { { .form = X86_FORM_VPEXTRW_R32_X_IMM8, .reg = X86_REGISTER_RAX, .rm = 1, .immediate = 3 }, 5, { 0xc5, 0xf9, 0xc5, 0xc1, 0x03 } },
    { { .form = X86_FORM_VPINSRW_X_X_RM32_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .immediate = 3 }, 5, { 0xc5, 0xe9, 0xc4, 0xc8, 0x03 } },
    { { .form = X86_FORM_VCVTPD2DQ_X_YM256, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xc5, 0xff, 0xe6, 0x08 } },
    { { .form = X86_FORM_VCVTPS2PH_XM64_X_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 6, { 0xc4, 0xe3, 0x79, 0x1d, 0x08, 0x03 } },
    { { .form = X86_FORM_VCVTPS2PH_XM128_Y_IMM8, .reg = 2, .rm = 1, .immediate = 3 }, 6, { 0xc4, 0xe3, 0x7d, 0x1d, 0xd1, 0x03 } },
    { { .form = X86_FORM_VLDMXCSR_M32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xc5, 0xf8, 0xae, 0x10 } },
    { { .form = X86_FORM_VMOVHPS_X_X_M64, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xc5, 0xe8, 0x16, 0x08 } },
    { { .form = X86_FORM_VMOVHLPS_X_X_X, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xe8, 0x12, 0xcb } },
    { { .form = X86_FORM_VPERMPD_Y_YM256_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 6, { 0xc4, 0xe3, 0xfd, 0x01, 0xca, 0x03 } },
    { { .form = X86_FORM_VPERM2F128_Y_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 0x21 }, 6, { 0xc4, 0xe3, 0x6d, 0x06, 0xcb, 0x21 } },
    { { .form = X86_FORM_VPSLLW_Y_Y_XM128, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xc5, 0xed, 0xf1, 0x08 } },
    { { .form = X86_FORM_VPSLLW_Y_Y_IMM8, .vvvv = 1, .rm = 2, .immediate = 3 }, 5, { 0xc5, 0xf5, 0x71, 0xf2, 0x03 } },
    { { .form = X86_FORM_VPMOVSXBW_Y_XM128, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x7d, 0x20, 0x08 } },
    { { .form = X86_FORM_VROUNDSS_X_X_XM32_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 6, { 0xc4, 0xe3, 0x69, 0x0a, 0x08, 0x03 } },
    { { .form = X86_FORM_VCVTSS2SI_R64_XM32, .reg = X86_REGISTER_RAX, .rm = 1 }, 5, { 0xc4, 0xe1, 0xfa, 0x2d, 0xc1 } },
    { { .form = X86_FORM_VPSRLVQ_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0xed, 0x45, 0xcb } },
    { { .form = X86_FORM_VBROADCASTF128_Y_M128, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x7d, 0x1a, 0x08 } },
    { { .form = X86_FORM_VPMAXUD_Y_Y_YM256, .reg = 9, .vvvv = 10, .rm = X86_REGISTER_R13, .index = X86_REGISTER_R11, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1), .displacement = 8 },
      7,
      { 0xc4, 0x02, 0x2d, 0x3f, 0x4c, 0x5d, 0x08 } },
    { { .form = X86_FORM_VMOVNTDQ_M256_Y + BUSTER_X86_FORM_GS, .reg = 9, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x65, 0xc5, 0x7d, 0xe7, 0x08 } },
    // FMA3: vfmadd132ps ymm1, ymm2, ymm3; vfmsub213pd xmm9, xmm10, xmmword ptr [r13] (W1, R and B, an 8-bit zero
    // displacement); vfnmsub231sd xmm1, xmm2, qword ptr [rax]; vfmaddsub132ps xmm1, xmm2, xmm3; vfmsubadd231pd ymm1,
    // ymm2, ymmword ptr [rax]; and vfnmadd213ss xmm1, xmm2, dword ptr [rax]
    { { .form = X86_FORM_VFMADD132PS_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x6d, 0x98, 0xcb } },
    { { .form = X86_FORM_VFMSUB213PD_X_X_XM128, .reg = 9, .vvvv = 10, .rm = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0xc4, 0x42, 0xa9, 0xaa, 0x4d, 0x00 } },
    { { .form = X86_FORM_VFNMSUB231SD_X_X_XM64, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0xe9, 0xbf, 0x08 } },
    { { .form = X86_FORM_VFMADDSUB132PS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x69, 0x96, 0xcb } },
    { { .form = X86_FORM_VFMSUBADD231PD_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0xed, 0xb7, 0x08 } },
    { { .form = X86_FORM_VFNMADD213SS_X_X_XM32, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x69, 0xad, 0x08 } },
    // AVX-512F's conversions: vcvtdq2pd zmm1, dword ptr [rax+0x40]{1to8} (a broadcast for half the vector, N 4);
    // vcvtpd2dq ymm1{k1}{z}, zmm2, {rz-sae}; vcvtsd2si rax, xmm1, {ru-sae} and vcvtusi2ss xmm1, xmm2, rax, {rd-sae}
    // (rounding without an opmask); vcvtsi2sd xmm1, xmm2, dword ptr [rax+0x40] (N 4); and vcvtps2ph ymmword ptr
    // [rax+0x40]{k1}, zmm2, 5 (N 32)
    { { .form = X86_FORM_VCVTDQ2PD_Z_M32BCST, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0x7e, 0x58, 0xe6, 0x48, 0x10 } },
    { { .form = X86_FORM_VCVTPD2DQ_Y_Z_ER, .reg = 1, .rm = 2, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING | BUSTER_X86_ROUND_ZERO }, 6, { 0x62, 0xf1, 0xff, 0xf9, 0xe6, 0xca } },
    { { .form = X86_FORM_VCVTSD2SI_R64_X_ER, .reg = X86_REGISTER_RAX, .rm = 1, .decorations = BUSTER_X86_ROUND_UP }, 6, { 0x62, 0xf1, 0xff, 0x58, 0x2d, 0xc1 } },
    { { .form = X86_FORM_VCVTUSI2SS_X_X_R64_ER, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .decorations = BUSTER_X86_ROUND_DOWN }, 6, { 0x62, 0xf1, 0xee, 0x38, 0x7b, 0xc8 } },
    { { .form = X86_FORM_VCVTSI2SD_X_X_RM32_EVEX, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 },
      7,
      { 0x62, 0xf1, 0x6f, 0x08, 0x2a, 0x48, 0x10 } },
    { { .form = X86_FORM_VCVTPS2PH_YM256_Z_IMM8, .reg = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 5, .decorations = 1 },
      8,
      { 0x62, 0xf3, 0x7d, 0x49, 0x1d, 0x50, 0x02, 0x05 } },
    // Suppressed exceptions and rounding in AVX-512F's other forms: vcomisd xmm1, xmm2, {sae} (without an opmask);
    // vcmpss k1{k2}, xmm2, xmm3, {sae}, 5; vscalefsd xmm1{k1}, xmm2, xmm3, {rn-sae}; vrndscalesd xmm1, xmm2, xmm3,
    // {sae}, 4; vfixupimmps zmm1, zmm2, zmm3, {sae}, 5; and with a broadcast, vrndscaleps zmm1, dword ptr [rax]{1to16},
    // 4; and FMA3's other orders, vfmadd132pd zmm1{k1}, zmm2, qword ptr [rax]{1to8} and vfnmsub213ss xmm1, xmm2, xmm3,
    // {rz-sae}
    { { .form = X86_FORM_VCOMISD_X_X_SAE, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf1, 0xfd, 0x18, 0x2f, 0xca } },
    { { .form = X86_FORM_VCMPSS_K_X_X_SAE_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5, .decorations = 2 }, 7, { 0x62, 0xf1, 0x6e, 0x1a, 0xc2, 0xcb, 0x05 } },
    { { .form = X86_FORM_VSCALEFSD_X_X_X_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_ROUND_NEAREST }, 6, { 0x62, 0xf2, 0xed, 0x19, 0x2d, 0xcb } },
    { { .form = X86_FORM_VRNDSCALESD_X_X_X_SAE_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 4 }, 7, { 0x62, 0xf3, 0xed, 0x18, 0x0b, 0xcb, 0x04 } },
    { { .form = X86_FORM_VFIXUPIMMPS_Z_Z_Z_SAE_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf3, 0x6d, 0x18, 0x54, 0xcb, 0x05 } },
    { { .form = X86_FORM_VRNDSCALEPS_Z_M32BCST_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 4 }, 7, { 0x62, 0xf3, 0x7d, 0x58, 0x08, 0x08, 0x04 } },
    { { .form = X86_FORM_VFMADD132PD_Z_Z_M64BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 }, 6, { 0x62, 0xf2, 0xed, 0x59, 0x98, 0x08 } },
    { { .form = X86_FORM_VFNMSUB213SS_X_X_X_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = BUSTER_X86_ROUND_ZERO }, 6, { 0x62, 0xf2, 0x6d, 0x78, 0xaf, 0xcb } },
    // AVX-512F's moves, and their N: vpmovusqb qword ptr [rax+0x40]{k1}, zmm2 (8); vpmovsxbq zmm1, qword ptr [rax+0x40]
    // (8); vinsertf64x4 zmm1, zmm2, ymmword ptr [rax+0x40], 1 (32); vextractf32x4 xmmword ptr [rax+0x40]{k1}, zmm2, 3
    // (16); vextractps dword ptr [rax+0x40], xmm17, 3 (4); {store} vmovq xmm1, xmm2 (66 0F D6); vmovhps qword ptr
    // [rax+0x40], xmm17 (8); vmovntdqa zmm1, zmmword ptr [rax+0x40] (64); and vcompressps zmmword ptr [rax+0x40]{k1},
    // zmm1 (4)
    { { .form = X86_FORM_VPMOVUSQB_XM64_Z, .reg = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 },
      7,
      { 0x62, 0xf2, 0x7e, 0x49, 0x12, 0x50, 0x08 } },
    { { .form = X86_FORM_VPMOVSXBQ_Z_XM64, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x48, 0x22, 0x48, 0x08 } },
    { { .form = X86_FORM_VINSERTF64X4_Z_Z_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 1 },
      8,
      { 0x62, 0xf3, 0xed, 0x48, 0x1a, 0x48, 0x02, 0x01 } },
    { { .form = X86_FORM_VEXTRACTF32X4_XM128_Z_IMM8, .reg = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3, .decorations = 1 },
      8,
      { 0x62, 0xf3, 0x7d, 0x49, 0x19, 0x50, 0x04, 0x03 } },
    { { .form = X86_FORM_VEXTRACTPS_RM32_X_IMM8_EVEX, .reg = 17, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3 },
      8,
      { 0x62, 0xe3, 0x7d, 0x08, 0x17, 0x48, 0x10, 0x03 } },
    { { .form = X86_FORM_VMOVQ_XM64_X_EVEX, .rm = 1, .reg = 2 }, 6, { 0x62, 0xf1, 0xfd, 0x08, 0xd6, 0xd1 } },
    { { .form = X86_FORM_VMOVHPS_M64_X_EVEX, .reg = 17, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xe1, 0x7c, 0x08, 0x17, 0x48, 0x08 } },
    { { .form = X86_FORM_VMOVNTDQA_Z_M512, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x48, 0x2a, 0x48, 0x01 } },
    { { .form = X86_FORM_VCOMPRESSPS_ZM512_Z, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 },
      7,
      { 0x62, 0xf2, 0x7d, 0x49, 0x8a, 0x48, 0x10 } },
    // And vprolq zmm1, qword ptr [rax]{1to8}, 5 (into vvvv); vpermq zmm1, zmm2, 0x1b (its immediate form); vpsraq zmm1,
    // zmm2, xmmword ptr [rax+0x40] (N 16); and cmpnlexadd qword ptr gs:[rax], rcx, rdx, a segment override on the last
    // form (a template past 8192, whose number's bit 13 the prefix leaves out)
    { { .form = X86_FORM_VPROLQ_Z_M64BCST_IMM8, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 7, { 0x62, 0xf1, 0xf5, 0x58, 0x72, 0x08, 0x05 } },
    { { .form = X86_FORM_VPERMQ_Z_ZM512_IMM8, .reg = 1, .rm = 2, .immediate = 0x1b }, 7, { 0x62, 0xf3, 0xfd, 0x48, 0x00, 0xca, 0x1b } },
    { { .form = X86_FORM_VPSRAQ_Z_Z_XM128, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0xed, 0x48, 0xe2, 0x48, 0x04 } },
    { { .form = X86_FORM_CMPNLEXADD_M64_R64_R64 + BUSTER_X86_FORM_GS, .reg = X86_REGISTER_RCX, .vvvv = X86_REGISTER_RDX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      6,
      { 0x65, 0xc4, 0xe2, 0xe9, 0xef, 0x08 } },
    // AVX512VL (AVX-512F at 128 and 256 bits): vaddps xmm17{k1}, xmm18, xmm19; vaddpd ymm1, ymm2, qword ptr
    // [rax+0x40]{1to4} (N 8); vpaddq xmm1, xmm2, xmmword ptr [rax+0x40] (N 16); vpandd ymm1{k1}{z}, ymm2, ymmword ptr
    // [rax+0x40] (N 32); vfmadd213ps xmm1, xmm2, dword ptr [rax]{1to4}; vpcmpgtd k1{k2}, xmm2, dword ptr [rax]{1to4};
    // vcmpps k1, ymm2, ymm3, 5; and at 256 bits only, vpermq ymm1, ymm2, ymm3 and vshufi64x2 ymm1, ymm2, ymm3, 1
    { { .form = X86_FORM_VADDPS_X_X_XM128_EVEX, .reg = 17, .vvvv = 18, .rm = 19, .decorations = 1 }, 6, { 0x62, 0xa1, 0x6c, 0x01, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPD_Y_Y_M64BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0xed, 0x38, 0x58, 0x48, 0x08 } },
    { { .form = X86_FORM_VPADDQ_X_X_XM128_EVEX, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0xed, 0x08, 0xd4, 0x48, 0x04 } },
    { { .form = X86_FORM_VPANDD_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      7,
      { 0x62, 0xf1, 0x6d, 0xa9, 0xdb, 0x48, 0x02 } },
    { { .form = X86_FORM_VFMADD213PS_X_X_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x6d, 0x18, 0xa8, 0x08 } },
    { { .form = X86_FORM_VPCMPGTD_K_X_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 2 }, 6, { 0x62, 0xf1, 0x6d, 0x1a, 0x66, 0x08 } },
    { { .form = X86_FORM_VCMPPS_K_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf1, 0x6c, 0x28, 0xc2, 0xcb, 0x05 } },
    { { .form = X86_FORM_VPERMQ_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0xed, 0x28, 0x36, 0xcb } },
    { { .form = X86_FORM_VSHUFI64X2_Y_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 1 }, 7, { 0x62, 0xf3, 0xed, 0x28, 0x43, 0xcb, 0x01 } },
    // Its conversions and moves, and their N: vcvtpd2dq xmm1, qword ptr [rax]{1to4} (from 256 bits into an XMM
    // register); vcvtpd2ps xmm1{k1}, xmmword ptr [rax+0x40] (16); vcvtdq2pd xmm1, dword ptr [rax+0x40]{1to2} (4);
    // vcvtps2ph qword ptr [rax+0x40], xmm1, 5 (8); vpmovqb word ptr [rax+0x40]{k1}, xmm1 (2); vpmovzxbq xmm1, word ptr
    // [rax+0x40] (2); vpcompressq xmmword ptr [rax+0x40]{k1}, xmm1 (8); vmovntdqa ymm1, ymmword ptr [rax+0x40] (32);
    // vmovddup xmm1, qword ptr [rax+0x40] (8); vmovdqu64 ymm17, ymmword ptr [rax]; vextracti32x4 xmm1, ymm2, 1;
    // vbroadcastsd ymm1{k1}{z}, xmm2; and vpbroadcastq ymm1, rax
    { { .form = X86_FORM_VCVTPD2DQ_X_M64BCST4, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf1, 0xff, 0x38, 0xe6, 0x08 } },
    { { .form = X86_FORM_VCVTPD2PS_X_XM128_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 },
      7,
      { 0x62, 0xf1, 0xfd, 0x09, 0x5a, 0x48, 0x04 } },
    { { .form = X86_FORM_VCVTDQ2PD_X_M32BCST, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0x7e, 0x18, 0xe6, 0x48, 0x10 } },
    { { .form = X86_FORM_VCVTPS2PH_XM64_X_IMM8_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 5 },
      8,
      { 0x62, 0xf3, 0x7d, 0x08, 0x1d, 0x48, 0x08, 0x05 } },
    { { .form = X86_FORM_VPMOVQB_XM16_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 }, 7, { 0x62, 0xf2, 0x7e, 0x09, 0x32, 0x48, 0x20 } },
    { { .form = X86_FORM_VPMOVZXBQ_X_XM16_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x08, 0x32, 0x48, 0x20 } },
    { { .form = X86_FORM_VPCOMPRESSQ_XM128_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 },
      7,
      { 0x62, 0xf2, 0xfd, 0x09, 0x8b, 0x48, 0x08 } },
    { { .form = X86_FORM_VMOVNTDQA_Y_M256_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x28, 0x2a, 0x48, 0x02 } },
    { { .form = X86_FORM_VMOVDDUP_X_XM64_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf1, 0xff, 0x08, 0x12, 0x48, 0x08 } },
    { { .form = X86_FORM_VMOVDQU64_Y_YM256, .reg = 17, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xe1, 0xfe, 0x28, 0x6f, 0x08 } },
    { { .form = X86_FORM_VEXTRACTI32X4_XM128_Y_IMM8, .rm = 1, .reg = 2, .immediate = 1 }, 7, { 0x62, 0xf3, 0x7d, 0x28, 0x39, 0xd1, 0x01 } },
    { { .form = X86_FORM_VBROADCASTSD_Y_XM64_EVEX, .reg = 1, .rm = 2, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf2, 0xfd, 0xa9, 0x19, 0xca } },
    { { .form = X86_FORM_VPBROADCASTQ_Y_R64, .reg = 1, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf2, 0xfd, 0x28, 0x7c, 0xc8 } },
    // And its shifts: vpsrad xmm1, xmmword ptr [rax+0x40], 3 (into vvvv, N 16) and vpsraq ymm1, ymm2, xmm3
    { { .form = X86_FORM_VPSRAD_X_XM128_IMM8, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3 },
      8,
      { 0x62, 0xf1, 0x75, 0x08, 0x72, 0x60, 0x04, 0x03 } },
    { { .form = X86_FORM_VPSRAQ_Y_Y_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf1, 0xed, 0x28, 0xe2, 0xcb } },
};

// Checks one encoder on the known encodings, encoding each alone and all of them at once.
static uint64_t x86_check_known_with(const char* name, bool vectorized)
{
    static uint8_t output[BUSTER_X86_ENCODER_CAPACITY(BUSTER_ARRAY_LENGTH(x86_known_encodings))];
    uint64_t failures = 0;
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_known_encodings); i += 1)
    {
        const KnownEncoding* known = &x86_known_encodings[i];
        const int64_t length = vectorized ? x86_encode(&known->instruction, 1, output, sizeof(output), 0) : x86_encode_scalar(&known->instruction, 1, output, sizeof(output), 0);
        if (length != known->length || memcmp(output, known->bytes, known->length) != 0)
        {
            if (failures < BUSTER_X86_TEST_REPORTED)
            {
                printf("  %s: ", name);
                x86_print_instruction(stdout, &known->instruction);
                printf(": got ");
                x86_print_bytes(output, length > 0 ? (uint64_t)length : 0);
                printf(" (%lld bytes), expected ", (long long)length);
                x86_print_bytes(known->bytes, known->length);
                printf("\n");
            }
            failures += 1;
        }
    }
    return failures;
}

static bool x86_check_known(void)
{
    const uint64_t scalar = x86_check_known_with("x86_encode_scalar", false);
    const uint64_t vectorized = x86_check_known_with("x86_encode", true);
    printf("known encodings: %lu instructions, %lu wrong (scalar), %lu wrong (vectorized)\n", (unsigned long)BUSTER_ARRAY_LENGTH(x86_known_encodings), (unsigned long)scalar,
           (unsigned long)vectorized);
    return scalar == 0 && vectorized == 0;
}

static uint64_t x86_fnv1a(const uint8_t* bytes, uint64_t length)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint64_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    return hash;
}

static bool x86_check_digest(X86Instruction* instructions, uint8_t* output)
{
    uint64_t state = BUSTER_X86_TEST_DIGEST_SEED * BUSTER_X86_TEST_GOLDEN_RATIO + 1;
    x86_random_instructions(&state, instructions, BUSTER_X86_TEST_DIGEST_INSTRUCTIONS, false, true);
    const int64_t length = x86_encode_scalar(instructions, BUSTER_X86_TEST_DIGEST_INSTRUCTIONS, output, BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_TEST_DIGEST_INSTRUCTIONS), 0);
    const uint64_t digest = length >= 0 ? x86_fnv1a(output, (uint64_t)length) : 0;
    const bool result = digest == BUSTER_X86_TEST_DIGEST;
    printf("reference digest: %d instructions, %lld bytes, 0x%016llx (%s)\n", BUSTER_X86_TEST_DIGEST_INSTRUCTIONS, (long long)length, (unsigned long long)digest,
           result ? "matches the one GNU as checked" : "DIFFERENT FROM THE ONE GNU AS CHECKED");
    return result;
}

typedef int64_t X86EncodeFunction(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets);

typedef struct X86Variant X86Variant;
struct X86Variant
{
    const char* name;
    X86EncodeFunction* encode;
    // Writes nothing past the instructions' bytes (rather than nothing past the capacity).
    bool exact;
};

static const X86Variant x86_test_variants[] = {
    { "x86_encode_4", x86_encode_4, false },
    { "x86_encode_8", x86_encode_8, false },
    { "x86_encode_16", x86_encode_16, false },
    { "x86_encode_32", x86_encode_32, false },
    { "x86_encode_64", x86_encode_64, false },
    { "x86_encode_16_forms_from_records", x86_encode_16_forms_from_records, false },
    { "x86_encode_16_masked_loads", x86_encode_16_masked_loads, false },
    { "x86_encode_16_masked_stores", x86_encode_16_masked_stores, true },
    { "x86_encode_16_compress_stores", x86_encode_16_compress_stores, true },
};

typedef struct X86Buffers X86Buffers;
struct X86Buffers
{
    uint8_t* expected;
    uint8_t* output;
    uint32_t* expected_offsets;
    uint32_t* offsets;
};

// Compares a variant with x86_encode_scalar on `count` instructions. Prints the first difference if `report`.
static bool x86_compare_encoders(const X86Variant* variant, const X86Instruction* instructions, uint64_t count, X86Buffers* buffers, bool report)
{
    const int64_t expected = x86_encode_scalar(instructions, count, buffers->expected, BUSTER_X86_ENCODER_CAPACITY(count), buffers->expected_offsets);
    const int64_t length = variant->encode(instructions, count, buffers->output, buffers->offsets);
    bool result = expected >= 0 && length == expected && memcmp(buffers->output, buffers->expected, (uint64_t)expected) == 0 &&
                  memcmp(buffers->offsets, buffers->expected_offsets, count * sizeof(uint32_t)) == 0;
    if (!result && report)
    {
        printf("  %s on %lu instructions: %lld bytes, expected %lld\n", variant->name, (unsigned long)count, (long long)length, (long long)expected);
        // The first instruction whose offset or bytes differ.
        uint64_t i = 0;
        bool same = length >= 0 && expected >= 0;
        while (same && i < count)
        {
            const uint32_t start = buffers->expected_offsets[i];
            const uint32_t end = i + 1 < count ? buffers->expected_offsets[i + 1] : (uint32_t)expected;
            same = buffers->offsets[i] == start && (uint64_t)length >= end && memcmp(buffers->output + start, buffers->expected + start, end - start) == 0;
            i += same;
        }
        if (length >= 0 && expected >= 0 && i < count)
        {
            const uint32_t start = buffers->expected_offsets[i];
            const uint32_t end = i + 1 < count ? buffers->expected_offsets[i + 1] : (uint32_t)expected;
            printf("    instruction %lu: ", (unsigned long)i);
            x86_print_instruction(stdout, &instructions[i]);
            printf("\n    expected at %u: ", start);
            x86_print_bytes(buffers->expected + start, end - start);
            printf("\n    got at %u:      ", buffers->offsets[i]);
            x86_print_bytes(buffers->output + start, (uint64_t)length >= end ? end - start : 0);
            printf("\n");
        }
    }
    return result;
}

static bool x86_check_random(uint64_t* state, uint64_t count, X86Instruction* instructions, X86Buffers* buffers)
{
    bool result = true;
    for (uint64_t v = 0; v < BUSTER_ARRAY_LENGTH(x86_test_variants); v += 1)
    {
        const X86Variant* variant = &x86_test_variants[v];
        uint64_t failures = 0;
        for (uint64_t n = 0; n < BUSTER_X86_TEST_SHORT_COUNTS; n += 1)
        {
            x86_random_instructions(state, instructions, n, true, false);
            failures += !x86_compare_encoders(variant, instructions, n, buffers, failures < BUSTER_X86_TEST_REPORTED);
        }
        x86_random_instructions(state, instructions, count, true, false);
        failures += !x86_compare_encoders(variant, instructions, count, buffers, failures < BUSTER_X86_TEST_REPORTED);
        printf("random: %-28s %d short streams and %lu instructions, %lu differ\n", variant->name, BUSTER_X86_TEST_SHORT_COUNTS, (unsigned long)count,
               (unsigned long)failures);
        result = result && failures == 0;
    }
    return result;
}

// The page-edge check's input, for the fault handler.
static volatile uint64_t x86_test_page_edge_count;
static volatile const char* x86_test_page_edge_variant;

static void x86_test_report_fault(int signal_number)
{
    (void)signal_number;
    char message[160];
    const int length = snprintf(message, sizeof(message), "page edges: %s faulted on %lu instructions\n", x86_test_page_edge_variant,
                                (unsigned long)x86_test_page_edge_count);
    if (length > 0 && write(STDOUT_FILENO, message, (size_t)length) < 0)
    {
        _exit(1);
    }
    _exit(1);
}

// Maps `bytes` rounded up to whole pages between two PROT_NONE pages, and returns the end of the readable part.
static uint8_t* x86_map_guarded(uint64_t bytes, uint64_t page, uint64_t* mapped_bytes)
{
    const uint64_t pages = (bytes + page - 1) / page;
    const uint64_t total = (pages + 2) * page;
    uint8_t* base = mmap(0, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* result = 0;
    if (base != MAP_FAILED && (pages == 0 || mprotect(base + page, pages * page, PROT_READ | PROT_WRITE) == 0))
    {
        result = base + page + pages * page;
        *mapped_bytes = total;
    }
    return result;
}

static bool x86_check_page_edges(uint64_t* state, X86Instruction* scratch, X86Buffers* buffers)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t input_mapped = 0;
    uint64_t output_mapped = 0;
    uint64_t offsets_mapped = 0;
    uint8_t* input_end = x86_map_guarded(BUSTER_X86_TEST_SHORT_COUNTS * sizeof(X86Instruction), page, &input_mapped);
    uint8_t* output_end = x86_map_guarded(BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_TEST_SHORT_COUNTS), page, &output_mapped);
    uint8_t* offsets_end = x86_map_guarded(BUSTER_X86_TEST_SHORT_COUNTS * sizeof(uint32_t), page, &offsets_mapped);
    uint64_t failures = 0;
    bool result = input_end != 0 && output_end != 0 && offsets_end != 0;
    if (!result)
    {
        printf("page edges: can't map the guarded buffers\n");
    }
    else
    {
        struct sigaction action;
        struct sigaction previous_segv;
        struct sigaction previous_bus;
        memset(&action, 0, sizeof(action));
        action.sa_handler = x86_test_report_fault;
        sigaction(SIGSEGV, &action, &previous_segv);
        sigaction(SIGBUS, &action, &previous_bus);
        for (uint64_t v = 0; v < BUSTER_ARRAY_LENGTH(x86_test_variants); v += 1)
        {
            const X86Variant* variant = &x86_test_variants[v];
            {
                x86_test_page_edge_variant = variant->name;
                for (uint64_t n = 0; n < BUSTER_X86_TEST_SHORT_COUNTS; n += 1)
                {
                    x86_test_page_edge_count = n;
                    x86_random_instructions(state, scratch, n, true, false);
                    X86Instruction* instructions = (X86Instruction*)(input_end - n * sizeof(X86Instruction));
                    memcpy(instructions, scratch, n * sizeof(X86Instruction));
                    const int64_t expected = x86_encode_scalar(instructions, n, buffers->expected, BUSTER_X86_ENCODER_CAPACITY(n), buffers->expected_offsets);
                    // The output ends where the instructions do (for an exact variant) or where its capacity does.
                    uint8_t* output = output_end - (variant->exact ? (uint64_t)(expected > 0 ? expected : 0) : BUSTER_X86_ENCODER_CAPACITY(n));
                    uint32_t* offsets = (uint32_t*)(offsets_end - n * sizeof(uint32_t));
                    const int64_t length = variant->encode(instructions, n, output, offsets);
                    const bool same = length == expected && memcmp(output, buffers->expected, (uint64_t)(expected > 0 ? expected : 0)) == 0 &&
                                      memcmp(offsets, buffers->expected_offsets, n * sizeof(uint32_t)) == 0;
                    if (!same && failures < BUSTER_X86_TEST_REPORTED)
                    {
                        printf("  %s on %lu instructions at page edges: %lld bytes, expected %lld\n", variant->name, (unsigned long)n, (long long)length,
                               (long long)expected);
                    }
                    failures += !same;
                }
            }
        }
        sigaction(SIGSEGV, &previous_segv, 0);
        sigaction(SIGBUS, &previous_bus, 0);
        printf("page edges: 0 to %d instructions ending at a page end, machine code (or its capacity) and offsets too, %lu differ\n", BUSTER_X86_TEST_SHORT_COUNTS - 1,
               (unsigned long)failures);
        result = failures == 0;
    }
    if (input_end != 0)
    {
        munmap(input_end - (input_mapped - 2 * page) - page, input_mapped);
    }
    if (output_end != 0)
    {
        munmap(output_end - (output_mapped - 2 * page) - page, output_mapped);
    }
    if (offsets_end != 0)
    {
        munmap(offsets_end - (offsets_mapped - 2 * page) - page, offsets_mapped);
    }
    return result;
}

// The lowest bit of a register operand of the class (in a form of the family) past its last register.
static uint8_t x86_past_last_register(uint8_t operand, uint8_t family)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = 0x10;
    if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST)
    {
        result = 0x08;
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX)
    {
        result = 0x20;
    }
    return result;
}

// Breaks one operand of a valid instruction in one of the ways x86_encode checks. Returns false if this instruction
// can't be broken that way.
static bool x86_break_instruction(uint64_t* state, X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
    bool result = true;
    switch (x86_random_below(state, 14))
    {
        case 0:
            // A high byte register with an extended register (and so a REX prefix).
            if ((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && form->encoding == BUSTER_X86_ENCODING_R &&
                (form->rm & BUSTER_X86_OPERAND_REGISTER) && !memory)
            {
                instruction->reg = X86_REGISTER_AH;
                instruction->rm = (uint8_t)(8 + x86_random_below(state, 8));
            }
            else
            {
                result = false;
            }
            break;
        case 1:
            // RSP as an index.
            if (memory && (instruction->mode & BUSTER_X86_MODE_INDEX))
            {
                instruction->index = X86_REGISTER_RSP;
            }
            else
            {
                result = false;
            }
            break;
        case 2:
            // RIP with a base.
            if (memory)
            {
                instruction->mode = BUSTER_X86_MEMORY_RIP | BUSTER_X86_MODE_BASE;
            }
            else
            {
                result = false;
            }
            break;
        case 3:
            // A register number past the class's last (15, or 31 for an EVEX form's vector registers, or 7 for an
            // opmask or an x87 register), in reg, in a register rm, or in a memory operand's base or index (general
            // registers, so 15).
            if (form->encoding == BUSTER_X86_ENCODING_R && x86_random_below(state, 2) == 0)
            {
                instruction->reg |= x86_past_last_register(form->reg, form->family);
            }
            else if (form->rm != BUSTER_X86_OPERAND_NONE && !memory)
            {
                instruction->rm |= x86_past_last_register(form->rm, form->family);
            }
            else if (memory && (instruction->mode & BUSTER_X86_MODE_BASE))
            {
                instruction->rm |= 0x10;
            }
            else if (memory && (instruction->mode & BUSTER_X86_MODE_INDEX))
            {
                instruction->index |= 0x10;
            }
            else
            {
                result = false;
            }
            break;
        case 6:
            // vvvv past the class's last, or in a form without it.
            instruction->vvvv = form->vvvv != BUSTER_X86_OPERAND_NONE ? (uint8_t)(instruction->vvvv | x86_past_last_register(form->vvvv, form->family))
                                                                     : (uint8_t)(1 + x86_random_below(state, 31));
            break;
        case 7:
        {
            // A decoration the form can't have (bits 3 and 4 never can).
            const uint8_t forbidden = (uint8_t)(x86_template_checks(form) >> 24);
            uint32_t bit = x86_random_below(state, 8);
            while (((forbidden >> bit) & 1) == 0)
            {
                bit = x86_random_below(state, 8);
            }
            instruction->decorations |= (uint8_t)(1u << bit);
            break;
        }
        case 8:
            // ModRM.reg in a form without a reg operand.
            if (form->encoding != BUSTER_X86_ENCODING_R)
            {
                instruction->reg = (uint8_t)(1 + x86_random_below(state, 15));
            }
            else
            {
                result = false;
            }
            break;
        case 9:
            // A memory operand in a form whose rm can't be one: a register-only rm, or none (whose rm stays 0). The
            // base is one any register class has, so that only the operand's kind is wrong.
            if (!(form->rm & BUSTER_X86_OPERAND_MEMORY))
            {
                instruction->mode = BUSTER_X86_MEMORY_BASE;
                instruction->rm = form->rm == BUSTER_X86_OPERAND_NONE ? 0 : (uint8_t)x86_random_below(state, 8);
                instruction->index = 0;
            }
            else
            {
                result = false;
            }
            break;
        case 10:
            // A register in a form whose rm can only be memory.
            if ((form->rm & BUSTER_X86_OPERAND_MEMORY) && !(form->rm & BUSTER_X86_OPERAND_REGISTER))
            {
                instruction->mode = BUSTER_X86_REGISTER_DIRECT;
                instruction->rm = (uint8_t)x86_random_below(state, 8);
                instruction->index = 0;
                instruction->displacement = 0;
            }
            else
            {
                result = false;
            }
            break;
        case 4:
            // A segment override's form with an operand that isn't memory (a register, or none).
            if (!memory)
            {
                instruction->form = (uint16_t)(instruction->form + (x86_random_below(state, 2) == 0 ? BUSTER_X86_FORM_FS : BUSTER_X86_FORM_GS));
            }
            else
            {
                result = false;
            }
            break;
        case 12:
            // Unknown mode bits.
            if (form->rm != BUSTER_X86_OPERAND_NONE)
            {
                instruction->mode |= x86_random_below(state, 2) == 0 ? 0x40 : 0x80;
            }
            else
            {
                result = false;
            }
            break;
        case 11:
            // An index register or a scale without an index.
            if (form->rm != BUSTER_X86_OPERAND_NONE && !(instruction->mode & BUSTER_X86_MODE_INDEX))
            {
                if (x86_random_below(state, 2) == 0)
                {
                    instruction->index = (uint8_t)(1 + x86_random_below(state, 15));
                }
                else
                {
                    instruction->mode |= (uint8_t)(1 + x86_random_below(state, 3));
                }
            }
            else
            {
                result = false;
            }
            break;
        default:
            // Memory parts without a memory operand.
            if (form->rm != BUSTER_X86_OPERAND_NONE && !memory)
            {
                instruction->mode = BUSTER_X86_MODE_BASE;
            }
            else
            {
                result = false;
            }
            break;
    }
    return result;
}

static bool x86_check_invalid(uint64_t* state, X86Instruction* instructions, X86Buffers* buffers)
{
    uint64_t broken = 0;
    uint64_t failures = 0;
    for (uint64_t trial = 0; trial < BUSTER_X86_TEST_INVALID_INSTRUCTIONS; trial += 1)
    {
        // A short stream with one broken instruction somewhere in it.
        const uint64_t count = 1 + x86_random_below(state, 40);
        x86_random_instructions(state, instructions, count, true, false);
        const uint64_t victim = x86_random_below(state, (uint32_t)count);
        if (x86_break_instruction(state, &instructions[victim]))
        {
            broken += 1;
            const bool validated = x86_validate(instructions, count) == victim;
            bool rejected = true;
            for (uint64_t v = 0; v < BUSTER_ARRAY_LENGTH(x86_test_variants); v += 1)
            {
                rejected = rejected && x86_test_variants[v].encode(instructions, count, buffers->output, 0) == X86_ENCODER_ERROR_INVALID_INSTRUCTION;
            }
            if (!(validated && rejected))
            {
                if (failures < BUSTER_X86_TEST_REPORTED)
                {
                    printf("  instruction %lu of %lu (form %s, reg 0x%02x, rm 0x%02x, index 0x%02x, mode 0x%02x): %s%s\n", (unsigned long)victim,
                           (unsigned long)count, x86_form_description(instructions[victim].form)->mnemonic, instructions[victim].reg, instructions[victim].rm,
                           instructions[victim].index, instructions[victim].mode, validated ? "" : "x86_validate missed it ",
                           rejected ? "" : "x86_encode accepted it");
                }
                failures += 1;
            }
        }
    }
    printf("invalid instructions: %lu broken, %lu not caught\n", (unsigned long)broken, (unsigned long)failures);
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cross-check modes, and the command

// Prints the instructions for GNU as, each with its bytes from x86_encode_scalar in a comment, and GNU as's length
// for each one in a .lengths section, a byte per instruction, so that --compare-binary can find each instruction
// in GNU as's output even after one whose length differs.
static bool x86_print_assembly(uint64_t* state, uint64_t count, X86Instruction* instructions, uint8_t* output)
{
    x86_random_instructions(state, instructions, count, false, true);
    printf(".intel_syntax noprefix\n.text\n");
    for (uint64_t i = 0; i < count; i += 1)
    {
        const int64_t length = x86_encode_scalar(&instructions[i], 1, output, BUSTER_X86_ENCODER_CAPACITY(1), 0);
        printf("0: ");
        x86_print_instruction(stdout, &instructions[i]);
        printf("\n1: .pushsection .lengths, \"a\"; .byte 1b - 0b; .popsection # ");
        x86_print_bytes(output, length > 0 ? (uint64_t)length : 0);
        printf("\n");
    }
    return true;
}

// The bytes of a file, or null (malloc'd; the caller frees them).
static uint8_t* x86_read_file(const char* path, uint64_t* length)
{
    uint8_t* result = 0;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat file_stats;
    if (fd != -1 && fstat(fd, &file_stats) == 0)
    {
        *length = (uint64_t)file_stats.st_size;
        result = malloc(*length + 1);
        if (result != 0 && read(fd, result, *length) != (ssize_t)*length)
        {
            free(result);
            result = 0;
        }
    }
    if (result == 0)
    {
        fprintf(stderr, "can't read %s\n", path);
    }
    if (fd != -1)
    {
        close(fd);
    }
    return result;
}

// Compares x86_encode_scalar's bytes with GNU as's for the instructions --print-assembly printed: `text_path` is
// GNU as's .text section and `lengths_path` its .lengths section.
static bool x86_compare_binary(uint64_t* state, const char* text_path, const char* lengths_path, uint64_t count, X86Instruction* instructions, uint8_t* output,
                               uint32_t* offsets)
{
    bool result = false;
    x86_random_instructions(state, instructions, count, false, true);
    const int64_t length = x86_encode_scalar(instructions, count, output, BUSTER_X86_ENCODER_CAPACITY(count), offsets);
    uint64_t text_length = 0;
    uint64_t lengths_length = 0;
    uint8_t* text = x86_read_file(text_path, &text_length);
    uint8_t* lengths = x86_read_file(lengths_path, &lengths_length);
    if (text != 0 && lengths != 0 && lengths_length != count)
    {
        fprintf(stderr, "%s has %lu lengths for %lu instructions\n", lengths_path, (unsigned long)lengths_length, (unsigned long)count);
    }
    else if (text != 0 && lengths != 0)
    {
        uint64_t differences = 0;
        uint64_t assembled = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint32_t start = offsets[i];
            const uint32_t end = i + 1 < count ? offsets[i + 1] : (uint32_t)length;
            const bool same = assembled + lengths[i] <= text_length && lengths[i] == end - start && memcmp(text + assembled, output + start, end - start) == 0;
            if (!same)
            {
                if (differences < 40)
                {
                    printf("instruction %lu: ", (unsigned long)i);
                    x86_print_instruction(stdout, &instructions[i]);
                    printf("\n  x86_encode_scalar: ");
                    x86_print_bytes(output + start, end - start);
                    printf("\n  GNU as:            ");
                    x86_print_bytes(text + assembled, assembled + lengths[i] <= text_length ? lengths[i] : 0);
                    printf("\n");
                }
                differences += 1;
            }
            assembled += lengths[i];
        }
        printf("compared %lu instructions (%lld bytes, GNU as %lu): %lu differ\n", (unsigned long)count, (long long)length, (unsigned long)text_length,
               (unsigned long)differences);
        result = differences == 0 && text_length == (uint64_t)length;
    }
    free(text);
    free(lengths);
    return result;
}

static bool x86_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 10);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0;
    *value = result ? (uint64_t)parsed : 0;
    return result;
}

static int run_x86_encoder_tests(int argc, char** argv)
{
    uint64_t count = BUSTER_X86_TEST_DEFAULT_COUNT;
    uint64_t seed = 0;
    uint64_t print_count = 0;
    uint64_t compare_count = 0;
    const char* compare_path = 0;
    const char* compare_lengths_path = 0;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            valid = valid && x86_parse_u64(argv[i + 1], &count);
            i += 1;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && x86_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--print-assembly") == 0 && i + 1 < argc)
        {
            valid = valid && x86_parse_u64(argv[i + 1], &print_count);
            i += 1;
        }
        else if (strcmp(argv[i], "--compare-binary") == 0 && i + 3 < argc)
        {
            compare_path = argv[i + 1];
            compare_lengths_path = argv[i + 2];
            valid = valid && x86_parse_u64(argv[i + 3], &compare_count);
            i += 3;
        }
        else
        {
            valid = false;
        }
    }

    const uint64_t largest = count > BUSTER_X86_TEST_DIGEST_INSTRUCTIONS ? count : BUSTER_X86_TEST_DIGEST_INSTRUCTIONS;
    const uint64_t capacity = largest > print_count ? (largest > compare_count ? largest : compare_count) : (print_count > compare_count ? print_count : compare_count);
    X86Instruction* instructions = malloc(capacity * sizeof(X86Instruction));
    X86Buffers buffers = {
        .expected = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .output = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .expected_offsets = malloc(capacity * sizeof(uint32_t)),
        .offsets = malloc(capacity * sizeof(uint32_t)),
    };
    uint64_t state = (seed == 0 ? BUSTER_X86_TEST_DIGEST_SEED : seed) * BUSTER_X86_TEST_GOLDEN_RATIO + 1;
    int result = 1;
    if (!valid || count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        fprintf(stderr, "usage: test-x86-encoder [--count N] [--seed N] [--print-assembly N | --compare-binary text lengths N]\n");
    }
    else if (instructions == 0 || buffers.expected == 0 || buffers.output == 0 || buffers.expected_offsets == 0 || buffers.offsets == 0)
    {
        fprintf(stderr, "out of memory\n");
    }
    else if (!x86_encoder_initialize())
    {
        x86_check_templates();
    }
    else if (print_count != 0)
    {
        result = x86_print_assembly(&state, print_count, instructions, buffers.output) ? 0 : 1;
    }
    else if (compare_path != 0)
    {
        result = x86_compare_binary(&state, compare_path, compare_lengths_path, compare_count, instructions, buffers.output, buffers.offsets) ? 0 : 1;
    }
    else
    {
        bool passed = x86_check_templates();
        passed = x86_check_known() && passed;
        passed = x86_check_digest(instructions, buffers.output) && passed;
        passed = x86_check_random(&state, count, instructions, &buffers) && passed;
        passed = x86_check_page_edges(&state, instructions, &buffers) && passed;
        passed = x86_check_invalid(&state, instructions, &buffers) && passed;
        result = passed ? 0 : 1;
    }
    free(instructions);
    free(buffers.expected);
    free(buffers.output);
    free(buffers.expected_offsets);
    free(buffers.offsets);
    return result;
}
