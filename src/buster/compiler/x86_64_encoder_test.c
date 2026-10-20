// The x86-64 encoder tests. The compiler runs them as a command:
//
//   build/compiler test-x86-encoder [--count N] [--seed N] [--address32 | --apx | --mode32 | --mode16]
//   build/compiler test-x86-encoder --print-assembly N [--seed N] [--address32 | --apx | --mode32 | --mode16]
//   build/compiler test-x86-encoder --compare-object object N [--seed N] [--address32 | --apx | --mode32 | --mode16]
//
// main.c includes this file after x86_64_encoder.c and calls run_x86_encoder_tests for the command. The checks, in this
// order:
//   - The templates: every form's longest encoding fits in a slot, and with 32-bit addresses, every one's but those
//     of the FS and GS forms with no room for the 67; and for APX, REX2's positions are in every legacy form of maps 0
//     and 1 but the XSAVE family's, and only there, but in the FS and GS forms with no room for its payload.
//   - Known encodings: x86_encode_scalar and x86_encode on x86_known_encodings, instructions on the paths most worth
//     pinning down (REX, SIB, RBP and R13 bases, RSP and R12 bases, RIP, absolute and index-only addresses,
//     displacement sizes, byte registers, prefixes, opcode maps; VEX2 and VEX3, BMI, opmasks, EVEX's opmasks,
//     zeroing, broadcasts, rounding, compressed displacements and registers 16 to 31), with bytes from GNU as, and each
//     again with the fixup flag, whose end both must report.
//   - Known fixups: x86_known_fixups, marked instructions with a symbolic field (branches, RIP-relative, absolute,
//     index-only and base operands, with and without an immediate after them, imm32s and 64-bit immediates), where
//     the reported end minus x86_fixup_distance must be where GNU as puts the field's relocation.
//   - The reference digest: a hash of x86_encode_scalar's output for BUSTER_X86_TEST_DIGEST_INSTRUCTIONS random
//     instructions (from the stream --print-assembly prints with --seed BUSTER_X86_TEST_DIGEST_SEED), recorded when
//     GNU as encoded the same stream to the same bytes. A change to the form table or the scalar encoder changes it;
//     cross-check the new output with GNU as (as AGENTS.md shows) before recording the new value.
//   - Random streams: x86_encode at every batch width (x86_encode_4 ... x86_encode_64) and the gather and store
//     variants against x86_encode_scalar, byte for byte, offset for offset and fixup end for fixup end, with and without
//     each of the offsets and the fixup ends, on random instructions of every form (with empty X86_FORM_NONE
//     instructions among them, and one in BUSTER_X86_TEST_MARKED marked with the fixup flag): every count from 0 to
//     BUSTER_X86_TEST_SHORT_COUNTS - 1, then N instructions (BUSTER_X86_TEST_DEFAULT_COUNT by default) at once.
//   - Page edges: instructions that end exactly at the end of a readable page, and an output capacity (or, for the
//     variants that write only the instructions' bytes, the machine code), offsets and fixup ends (or their capacity)
//     that end exactly at the end of a writable one, with a PROT_NONE page after them, for every count from 0 to
//     BUSTER_X86_TEST_SHORT_COUNTS - 1. A read or write past the end faults, and the fault handler names the count and
//     exits with status 1.
//   - Invalid instructions: random valid instructions with one operand broken in ways x86_encode checks (a high byte
//     register with a REX prefix, RSP as an index, RIP with a base, a register past its class's last, a group's
//     register that isn't a multiple of its size, vvvv or reg in a form without them, decorations the form can't have,
//     unknown mode bits): x86_encode must reject them and x86_validate must find them. And with an immediate, rel8 or
//     rel16 just outside its field's range, an /is4 register outside its bits, a control or segment register that
//     doesn't exist, MOV to CS, a complex multiplication into one of its sources, an AMX product of tiles with one
//     twice, push2 or pop2 with RSP, pop2 with one register twice, a VSIB operand without an index, an EVEX gather or
//     scatter without an opmask, a gather whose destination, index and mask aren't different registers, a SIB operand
//     without a SIB byte or with SIB's none and a scale, a RIP-relative operand of MPX's bndmk or a MIB operand, a MIB
//     operand's scaled index, a 3DNow! form's immediate other than its operation byte, or a NOTRACK form with a segment
//     override, which only x86_validate checks: it must find them.
// Then the same again with 32-bit addresses (x86_test_mode, from the seed's first state again), but the known fixups:
// x86_encode_address32 and x86_encode_scalar_address32 on x86_known_encodings_address32, a digest of their own
// (BUSTER_X86_TEST_DIGEST_ADDRESS32), x86_encode_64_address32 on the random streams and at page edges, and the invalid
// instructions, with two more breaks: an MPX memory operand, which only x86_validate_address32 checks, and a memory
// operand in a form without room for the 67, which x86_encode_address32 rejects too. --address32 runs only this pass.
// And again for APX, but the known fixups, which is all --apx runs: x86_encode_apx and x86_encode_scalar_apx on
// x86_known_encodings_apx, the instructions of x86_known_invalid_apx, which they and x86_validate_apx must reject (as GNU
// as does), a digest of their own (BUSTER_X86_TEST_DIGEST_APX), x86_encode_64_apx on the random streams (in which half
// the forms have REX2 and a quarter are EVEX ones, whose general registers, bases and general indexes are 16 to 31 half
// the time) and at page edges, and the invalid instructions, with three more breaks: a high byte register with REX2, a
// general register, base or index from 16 to 31 in a form without REX2 or EVEX, and a vector register rm from 16 to 31
// in a form with REX2.
// And in 32-bit mode and in 16-bit mode, which is all --mode32 and --mode16 run, with x86_encode_scalar_32 and
// x86_encode_scalar_16 alone (they have no vectorized encoder yet, so no random streams or page edges): the known
// encodings of x86_known_encodings_32 and x86_known_encodings_16, the known fixups of x86_known_fixups_32 and
// x86_known_fixups_16 (with x86_fixup_distance_address16 for a 16-bit address), a digest of each
// (BUSTER_X86_TEST_DIGEST_32 and BUSTER_X86_TEST_DIGEST_16), and the invalid instructions, with five more breaks: a
// register that would need REX, a memory operand the instruction's address size can't have, a form that doesn't exist
// in the mode (which 64-bit mode's passes break with too), an instruction longer than 15 bytes, and a segment override
// in the decorations that the instruction can't take.
//
// --print-assembly prints N random instructions in GNU as Intel syntax, each with its bytes from x86_encode_scalar in a
// comment, choosing operands GNU as encodes with the same form (with {load}, {store}, {vex} and {evex} where GNU as
// would pick another, and a default flags value as {dfv=...}), and has GNU as record each one's length in a .lengths
// section. Some instructions are marked (x86_mark_symbolic, with a random sequence of their own, so the instructions
// are the same ones either way), and their fields that GNU as would encode the same way with a symbol (32-bit
// displacements, a rel32 or loop's and jrcxz's rel8, imm32 and 64-bit immediates) are printed as symbols plus the
// field's value, for which GNU as writes zeros and a relocation. --compare-object reads the .text, .lengths and
// .rela.text sections of the object GNU as makes of that and compares them with x86_encode_scalar's bytes, with the
// symbolic fields zeroed where its fixup ends put them, and with the relocations the fixup ends and x86_fixup_distance
// give: offset, type, addend and symbol. With --address32, both take the stream with 32-bit addresses, which the
// printer writes with 32-bit registers, and with addr32 where no general register says the address's size; with --apx,
// the stream with APX's registers, whose registers 16 to 31 the printer writes as r16 to r31 (r16d, r16w and r16b); and
// with --mode32 or --mode16, the stream of 32-bit or 16-bit mode, for GNU as's .code32 or .code16, with both address
// sizes (addr16 or addr32 where nothing says the size), the mode's mnemonics (jecxz and jcxz, data16 and data32 where
// a mnemonic can't say the operand size, lcall and ljmp), segment overrides in the decorations as prefixes, and a test
// register's move between .arch push, .arch i386 and .arch pop. The exit status is 1 if the arguments are invalid or a
// check fails.
//
// Layout, in this order:
//   - Random instructions: x86_test_random, x86_random_register, x86_random_displacement, x86_random_immediate, the
//     entry point the checks are for (x86_test_mode, and its encoders and validation), x86_random_instruction and
//     x86_mark_random; the symbolic fields, x86_symbolic_fields and x86_mark_symbolic.
//   - Printing: register names, x86_print_memory and x86_print_instruction.
//   - The checks: x86_check_templates, x86_known_encodings (and x86_known_encodings_address32,
//     x86_known_encodings_apx, x86_known_encodings_32 and x86_known_encodings_16) and x86_check_known,
//     x86_known_invalid_apx and x86_check_known_invalid_apx, x86_known_fixups (and x86_known_fixups_32 and
//     x86_known_fixups_16) and x86_check_known_fixups, x86_check_digest, x86_compare_encoders and x86_check_random,
//     the page-edge check (x86_test_report_fault and x86_check_page_edges), and x86_check_invalid.
//   - The cross-check modes (x86_print_assembly, the object reader x86_elf_section and x86_compare_object),
//     x86_check_mode (one entry point's checks) and run_x86_encoder_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Random streams: every count below BUSTER_X86_TEST_SHORT_COUNTS, then this many instructions at once.
#define BUSTER_X86_TEST_SHORT_COUNTS 200
#define BUSTER_X86_TEST_DEFAULT_COUNT 300000
// One instruction in this many of the random streams has the fixup flag.
#define BUSTER_X86_TEST_MARKED 8
// --print-assembly prints one field in this many of those that can be symbolic as a symbol.
#define BUSTER_X86_TEST_SYMBOLIC 4
// The digest's stream and the hash of x86_encode_scalar's bytes for it (FNV-1a, 64-bit).
#define BUSTER_X86_TEST_DIGEST_SEED 1
#define BUSTER_X86_TEST_DIGEST_INSTRUCTIONS 100000
#define BUSTER_X86_TEST_DIGEST 0xf0b33adc74a14fb9ull
// The same for the stream with 32-bit addresses (--address32), for the one with APX's registers (--apx), and for those
// of 32-bit and 16-bit modes (--mode32 and --mode16).
#define BUSTER_X86_TEST_DIGEST_ADDRESS32 0x89d61221b2e298c7ull
#define BUSTER_X86_TEST_DIGEST_APX 0x9417f9289e033f50ull
#define BUSTER_X86_TEST_DIGEST_32 0xc5d2141a55e25336ull
#define BUSTER_X86_TEST_DIGEST_16 0x7fa96fd9f6a44a14ull
// 2^64 divided by the golden ratio: the first state of the random sequence, and the multiplier that turns --seed N
// into one (N * BUSTER_X86_TEST_GOLDEN_RATIO + 1).
#define BUSTER_X86_TEST_GOLDEN_RATIO 0x9E3779B97F4A7C15ull
// Each check prints the first differences of this many inputs.
#define BUSTER_X86_TEST_REPORTED 3
// Broken copies of each random instruction in the invalid-instruction check.
#define BUSTER_X86_TEST_INVALID_INSTRUCTIONS 20000

// ---------------------------------------------------------------------------------------------------------------------
// Random instructions

// The entry point the instructions are for, which --address32, --apx, --mode32 and --mode16 and the passes of
// x86_encode_address32, x86_encode_apx, x86_encode_scalar_32 and x86_encode_scalar_16 set: with 32-bit addresses, which
// x86_validate_address32 checks, x86_encode_scalar_address32 encodes and the printer writes with 32-bit registers; with
// APX's general registers 16 to 31 in the forms with REX2 (x86_form_has_rex2), which x86_validate_apx checks and
// x86_encode_scalar_apx encodes; or in 32-bit or 16-bit mode, without REX, which x86_validate_32 and x86_validate_16
// check and x86_encode_scalar_32 and x86_encode_scalar_16 encode (they have no vectorized encoder yet).
static X86EncoderMode x86_test_mode;

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
// and 13, and in half of an EVEX form's vector registers, 16 to 31, as in half of the general registers with `extended`:
// APX's 20, 21, 28 and 29 among them) and, for a legacy byte register, sometimes AH to BH; for a control or segment
// register, one that exists; and for a group, its first register.
static uint8_t x86_random_register(uint64_t* state, uint16_t operand, uint8_t family, bool extended)
{
    static const uint8_t special[] = { 4, 5, 12, 13 };
    static const uint8_t control[] = { 0, 2, 3, 4, 8 };
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
    else if (extended && class >= BUSTER_X86_CLASS_GPR8 && class <= BUSTER_X86_CLASS_GPR64 && x86_random_below(state, 2) == 0)
    {
        result |= 16;
    }
    else if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST || class == BUSTER_X86_CLASS_MMX || class == BUSTER_X86_CLASS_DEBUG || class == BUSTER_X86_CLASS_TILE)
    {
        result &= 7;
    }
    else if (class == BUSTER_X86_CLASS_BND)
    {
        result &= 3;
    }
    else if (class == BUSTER_X86_CLASS_CONTROL)
    {
        result = control[x86_random_below(state, BUSTER_ARRAY_LENGTH(control))];
    }
    else if (class == BUSTER_X86_CLASS_SEGMENT)
    {
        result = (uint8_t)x86_random_below(state, 6);
    }
    result &= (operand & BUSTER_X86_OPERAND_PAIR) ? 0xfe : 0xff;
    result &= (operand & BUSTER_X86_OPERAND_BLOCK) ? 0xfc : 0xff;
    // In 16- and 32-bit modes, without REX: 0 to 7 (but CR8, AMD's LOCK prefix on a move of CR0), and for a legacy byte
    // register, AH to BH for 4 to 7.
    if (x86_mode_is_legacy(x86_test_mode) && class != BUSTER_X86_CLASS_CONTROL)
    {
        const bool byte = class == BUSTER_X86_CLASS_GPR8 && family == BUSTER_X86_FAMILY_LEGACY;
        result = (uint8_t)((result & 7) | (byte && (result & 7) >= 4 ? BUSTER_X86_REGISTER_HIGH_BYTE : 0));
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

// An immediate of `bytes` bytes (as X86Instruction.immediate holds it: a ptr16:32's offset, or a 64-bit one's low
// half), in the range a sign-extended one has if `sign_extended`.
static int32_t x86_random_immediate(uint64_t* state, uint32_t bytes, bool sign_extended)
{
    int32_t result = 0;
    switch (bytes)
    {
        case 1: result = sign_extended ? (int32_t)x86_random_below(state, 256) - 128 : (int32_t)x86_random_below(state, 256); break;
        case 2: result = sign_extended ? (int32_t)x86_random_below(state, 65536) - 32768 : (int32_t)x86_random_below(state, 65536); break;
        case 3: result = (int32_t)x86_random_below(state, 1u << 24); break;
        case 4: result = x86_random_below(state, 3) == 0 ? (int32_t)x86_random_below(state, 256) - 128 : (int32_t)(uint32_t)x86_test_random(state); break;
        case 6: result = (int32_t)(uint32_t)x86_test_random(state); break;
        case 8: result = (int32_t)(uint32_t)x86_test_random(state); break;
        default: break;
    }
    return result;
}

static bool x86_fits_int8(int32_t value)
{
    return value >= -128 && value <= 127;
}

// A form's opcode, without its prefixes, as one number (for the forms GNU as encodes differently), and for APX's map 4
// forms, the legacy opcode byte they take.
static uint32_t x86_form_opcode(const X86FormDescription* form)
{
    const bool map4 = form->family == BUSTER_X86_FAMILY_EVEX && x86_opcode_map(form) == 4;
    return form->opcode_bytes == 1 || map4 ? form->opcode & 0xff : 0x10000 | form->opcode;
}

// The mode's scalar encoder and validation (see x86_test_mode), and its vectorized encoder: x86_encode's,
// x86_encode_address32's or x86_encode_apx's (and in 16- and 32-bit modes, the scalar one).
static int64_t x86_test_encode_scalar(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_encode_scalar_address32(instructions, count, output, capacity, offsets, fixup_ends)
           : x86_test_mode == X86_ENCODER_MODE_APX     ? x86_encode_scalar_apx(instructions, count, output, capacity, offsets, fixup_ends)
           : x86_test_mode == X86_ENCODER_MODE_32      ? x86_encode_scalar_32(instructions, count, output, capacity, offsets, fixup_ends)
           : x86_test_mode == X86_ENCODER_MODE_16      ? x86_encode_scalar_16(instructions, count, output, capacity, offsets, fixup_ends)
                                                       : x86_encode_scalar(instructions, count, output, capacity, offsets, fixup_ends);
}

static int64_t x86_test_encode(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint64_t capacity, uint32_t* offsets, uint32_t* fixup_ends)
{
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_encode_address32(instructions, count, output, capacity, offsets, fixup_ends)
           : x86_test_mode == X86_ENCODER_MODE_APX     ? x86_encode_apx(instructions, count, output, capacity, offsets, fixup_ends)
           : x86_mode_is_legacy(x86_test_mode)         ? x86_test_encode_scalar(instructions, count, output, capacity, offsets, fixup_ends)
                                                       : x86_encode(instructions, count, output, capacity, offsets, fixup_ends);
}

static uint64_t x86_test_validate(const X86Instruction* instructions, uint64_t count)
{
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_validate_address32(instructions, count)
           : x86_test_mode == X86_ENCODER_MODE_APX     ? x86_validate_apx(instructions, count)
           : x86_test_mode == X86_ENCODER_MODE_32      ? x86_validate_32(instructions, count)
           : x86_test_mode == X86_ENCODER_MODE_16      ? x86_validate_16(instructions, count)
                                                       : x86_validate(instructions, count);
}

static bool x86_test_instruction_is_valid(const X86Instruction* instruction)
{
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_instruction_is_valid_address32(instruction)
           : x86_test_mode == X86_ENCODER_MODE_APX     ? x86_instruction_is_valid_apx(instruction)
           : x86_test_mode == X86_ENCODER_MODE_32      ? x86_instruction_is_valid_32(instruction)
           : x86_test_mode == X86_ENCODER_MODE_16      ? x86_instruction_is_valid_16(instruction)
                                                       : x86_instruction_is_valid(instruction);
}

// The mode's words in the checks' lines: none for x86_encode's, or the addresses, registers or mode.
static const char* x86_test_mode_words(void)
{
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? " with 32-bit addresses"
           : x86_test_mode == X86_ENCODER_MODE_APX     ? " with APX's registers"
           : x86_test_mode == X86_ENCODER_MODE_32      ? " in 32-bit mode"
           : x86_test_mode == X86_ENCODER_MODE_16      ? " in 16-bit mode"
                                                       : "";
}

// Whether the instruction's memory operand, or the addresses it makes itself, are 16-bit: in 16-bit mode, and in 32-bit
// mode with BUSTER_X86_MODE_ADDRESS_SIZE.
static bool x86_test_address16(const X86Instruction* instruction)
{
    const bool other = (instruction->mode & BUSTER_X86_MODE_ADDRESS_SIZE) != 0;
    return (x86_test_mode == X86_ENCODER_MODE_16 && !other) || (x86_test_mode == X86_ENCODER_MODE_32 && other);
}

// Whether the form exists in the mode: in 64-bit mode's, the rows before BUSTER_X86_FORM_LEGACY_MODES, and in 16- and
// 32-bit modes those x86_form_legacy says.
static bool x86_test_form_exists(uint32_t form_index)
{
    const uint32_t row = (form_index & BUSTER_X86_FORM_NUMBER) % X86_FORM_COUNT;
    const uint8_t in_mode = x86_test_mode == X86_ENCODER_MODE_16 ? BUSTER_X86_LEGACY_IN_16 : BUSTER_X86_LEGACY_IN_32;
    return x86_mode_is_legacy(x86_test_mode) ? (x86_form_legacy[row] & in_mode) != 0 : row < BUSTER_X86_FORM_LEGACY_MODES;
}

// Whether the form's register operand is an address, which takes the address's size: MOVDIR64B's, ENQCMD's and
// ENQCMDS's (in ModRM.reg), and in 16- and 32-bit modes, UMONITOR's (in ModRM.rm).
static bool x86_form_has_address_register(uint32_t form_index)
{
    const uint32_t row = (form_index & BUSTER_X86_FORM_NUMBER) % X86_FORM_COUNT;
    return row == X86_FORM_MOVDIR64B_R64_M512 || row == X86_FORM_ENQCMD_R64_M512 || row == X86_FORM_ENQCMDS_R64_M512 || row == X86_FORM_MOVDIR64B_R64_M512_EVEX ||
           row == X86_FORM_ENQCMD_R64_M512_EVEX || row == X86_FORM_ENQCMDS_R64_M512_EVEX || row == X86_FORM_MOVDIR64B_R32_M512 || row == X86_FORM_ENQCMD_R32_M512 ||
           row == X86_FORM_ENQCMDS_R32_M512 || row == X86_FORM_UMONITOR_R32;
}

// The class an address register is printed with: the address's size.
static uint16_t x86_address_register_operand(const X86Instruction* instruction, uint16_t operand)
{
    const bool address32 = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 || x86_mode_is_legacy(x86_test_mode);
    return !x86_form_has_address_register(instruction->form) || !address32 ? operand : x86_test_address16(instruction) ? BUSTER_X86_OPERAND_GW : BUSTER_X86_OPERAND_GD;
}

// The address size prefix the printer writes the instruction with, or none: addr32 for a 32-bit address in 64-bit mode,
// and in 16- and 32-bit modes the other address size's (addr32 or addr16), where nothing in the instruction says the
// address's size: no general register (an absolute address or a VSIB operand without a base, whose index is a vector
// register) and, without a memory operand, an instruction that makes its addresses itself but jcxz and jecxz. (An
// address register's name says it too, and a SIB operand's index of 4 is none, which x86_print_memory doesn't print.)
static const char* x86_printed_address_prefix(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool none = (form->rm & BUSTER_X86_OPERAND_SIB) && instruction->index == X86_REGISTER_RSP;
    const uint8_t printed_mode = none ? (uint8_t)(instruction->mode & ~BUSTER_X86_MODE_INDEX) : instruction->mode;
    const bool general_index = (printed_mode & BUSTER_X86_MODE_INDEX) && !(form->rm & BUSTER_X86_OPERAND_VSIB);
    const bool general_register = (printed_mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_RIP)) || general_index;
    const bool memory = (printed_mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool shown = general_register || x86_form_has_address_register(instruction->form);
    const bool jcxz = form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1 && form->opcode == 0xe3;
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    const bool other = legacy && (instruction->mode & BUSTER_X86_MODE_ADDRESS_SIZE) && (memory ? !shown : !jcxz && !x86_form_has_address_register(instruction->form));
    // A moffs form's address size is its own.
    const uint8_t moffs = legacy ? x86_form_legacy[form - x86_form_descriptions] & (BUSTER_X86_LEGACY_ADDRESS_16 | BUSTER_X86_LEGACY_ADDRESS_32) : 0;
    const bool other_moffs = moffs == (x86_test_mode == X86_ENCODER_MODE_16 ? BUSTER_X86_LEGACY_ADDRESS_32 : BUSTER_X86_LEGACY_ADDRESS_16);
    const char* result = "";
    if (x86_test_mode == X86_ENCODER_MODE_ADDRESS32 && memory && !shown)
    {
        result = "addr32 ";
    }
    else if (other)
    {
        result = x86_test_address16(instruction) ? "addr16 " : "addr32 ";
    }
    else if (other_moffs)
    {
        result = moffs == BUSTER_X86_LEGACY_ADDRESS_16 ? "addr16 " : "addr32 ";
    }
    return result;
}

// Whether the form's immediate is an MSR index: map 7's (VEX's, and APX's EVEX forms), unsigned (GNU as rejects a
// negative one).
static bool x86_form_has_msr_index(const X86FormDescription* form)
{
    return form->family != BUSTER_X86_FAMILY_LEGACY && x86_opcode_map(form) == 7;
}

// Whether an MPX form takes only memory, so that it has no instruction with a 32-bit address.
static bool x86_form_is_bound_memory(uint32_t form_index)
{
    const X86FormDescription* form = x86_form_description(form_index);
    return (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_BND && (form->rm & BUSTER_X86_OPERAND_MEMORY) && !(form->rm & BUSTER_X86_OPERAND_REGISTER);
}

// Whether GNU as would encode the instruction with another form of the same mnemonic (a shorter one, usually), so
// that --print-assembly avoids it.
static bool x86_assembler_prefers_another_form(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    // The form without its segment override (or the fixup flag).
    const uint32_t row = (instruction->form & BUSTER_X86_FORM_NUMBER) % X86_FORM_COUNT;
    const bool register_direct = (instruction->mode & BUSTER_X86_MODE_MEMORY) == 0 && form->rm != BUSTER_X86_OPERAND_NONE;
    const uint32_t opcode = x86_form_opcode(form);
    // The immediate as the operand's size makes it: 65499 is -37 for a 16-bit operand.
    const int32_t immediate = form->immediate_bytes == 1 ? (int8_t)instruction->immediate : form->immediate_bytes == 2 ? (int16_t)instruction->immediate
                                                                                                                       : instruction->immediate;
    const bool legacy = form->family == BUSTER_X86_FAMILY_LEGACY;
    // APX's map 4 forms of the legacy instructions, among which GNU as picks as among those.
    const bool map4 = form->family == BUSTER_X86_FAMILY_EVEX && x86_opcode_map(form) == 4;
    bool result = false;
    // MOVQ (and VEX's VMOVQ) between an XMM register and memory is F3 0F 7E or 66 0F D6, not the W1 forms of MOVD; and
    // EVEX's the other way around. Between an MMX register and memory, it's 0F 6F or 0F 7F.
    if (!register_direct && (row == X86_FORM_MOVQ_X_RM64 || row == X86_FORM_MOVQ_RM64_X || row == X86_FORM_VMOVQ_X_RM64 || row == X86_FORM_VMOVQ_RM64_X ||
                             row == X86_FORM_VMOVQ_X_XM64_EVEX || row == X86_FORM_VMOVQ_XM64_X_EVEX || row == X86_FORM_MOVQ_MM_RM64 ||
                             row == X86_FORM_MOVQ_RM64_MM))
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
    // Immediates that fit in a sign-extended byte take the imm8 forms (in map 4 too), also instead of the AX, EAX and
    // RAX forms of the arithmetic instructions (05, 0D ... 3D).
    if ((legacy || map4) && (opcode == 0x81 || opcode == 0x69 || opcode == 0x68) && form->immediate_bytes != 1 && x86_fits_int8(immediate))
    {
        result = true;
    }
    if (legacy && (opcode & 0xc7) == 0x05 && form->syntax == X86_SYNTAX_ACC_IMM && x86_fits_int8(immediate))
    {
        result = true;
    }
    // A shift by 1 is D0/D1 (in map 4 too), and INT 3 is INT3.
    if ((legacy || map4) && (opcode == 0xc0 || opcode == 0xc1) && instruction->immediate == 1)
    {
        result = true;
    }
    if (legacy && opcode == 0xcd && instruction->immediate == 3)
    {
        result = true;
    }
    // FMA4 between registers is W1 (the third operand in the immediate and the fourth in ModRM.rm), not W0. The blends
    // before it in 0F 3A have no W1 form.
    if (register_direct && form->syntax == X86_SYNTAX_REG_VVVV_RM_IS4 && form->opcode >= 0x0f3a5c)
    {
        result = true;
    }
    // XOP's vpcmov and vpperm, and vpermil2ps and vpermil2pd, are W0 between registers: no pseudo-prefix picks W1.
    if (register_direct && form->w &&
        ((form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM && x86_opcode_map(form) >= 8) || form->syntax == X86_SYNTAX_REG_VVVV_IS4_RM_IMM))
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
    // GNU as takes no prefix before ccmp or ctest with a default flags value: not the {load} of ccmp's register forms
    // with ModRM.reg as the destination (3A and 3B), nor addr32.
    if (form->vvvv == BUSTER_X86_OPERAND_DFV && instruction->vvvv != 0 &&
        ((register_direct && form->syntax == X86_SYNTAX_REG_RM) || *x86_printed_address_prefix(instruction) != 0))
    {
        result = true;
    }
    // GNU as warns about a segment override on LEA, where it does nothing (in 16- and 32-bit modes, the decorations'
    // too).
    const bool override = x86_mode_is_legacy(x86_test_mode) && (instruction->decorations & BUSTER_X86_DECORATION_SEGMENT) != 0;
    if (legacy && opcode == 0x8d && (x86_form_segment(instruction->form) != 0 || override))
    {
        result = true;
    }
    // An x87 operation on ST(0) and ST(0) as fop st(0), st (DC) is the same as fop st, st(0), which is D8.
    if (legacy && register_direct && opcode == 0xdc && instruction->rm == 0)
    {
        result = true;
    }
    // With a 32-bit address, and in 16- and 32-bit modes, MOV between the accumulator and an absolute address is A0 to
    // A3, with a moffs of the address's size (but XRELEASE's MOV isn't).
    const bool short_moffs = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 || x86_mode_is_legacy(x86_test_mode);
    if (short_moffs && legacy && opcode >= 0x88 && opcode <= 0x8b && instruction->reg == 0 &&
        (instruction->mode & ~BUSTER_X86_MODE_ADDRESS_SIZE) == BUSTER_X86_MEMORY_ABSOLUTE && !x86_form_has_prefix(form, 0xf3))
    {
        result = true;
    }
    // A general index scaled by 1 without a base becomes the base (a VSIB one can't, and GNU as keeps a SIB one's and
    // MPX's, as bndmk's lower bound is its base, and with a 32-bit address, in 16- and 32-bit modes too).
    if ((x86_test_mode == X86_ENCODER_MODE_64 || x86_test_mode == X86_ENCODER_MODE_APX) &&
        (instruction->mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX)) == BUSTER_X86_MODE_INDEX && (instruction->mode & BUSTER_X86_MODE_SCALE) == 0 &&
        !(form->rm & (BUSTER_X86_OPERAND_VSIB | BUSTER_X86_OPERAND_SIB)) && (form->reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_BND)
    {
        result = true;
    }
    // In 16- and 32-bit modes, INC and DEC of a 16- or 32-bit register are 40+r and 48+r.
    if (x86_mode_is_legacy(x86_test_mode) && legacy && register_direct && opcode == 0xff &&
        (form->encoding == BUSTER_X86_ENCODING_D0 || form->encoding == BUSTER_X86_ENCODING_D1))
    {
        result = true;
    }
    // In 16-bit mode, GNU as makes a jmp or a jcc by a rel32 (data32's) whose target a rel8 reaches one by a rel8.
    if (x86_test_mode == X86_ENCODER_MODE_16 && form->syntax == X86_SYNTAX_REL && form->relative_bytes == 4 && opcode != 0xe8 && opcode != 0x1c7f8 &&
        instruction->displacement >= -140 && instruction->displacement <= 140)
    {
        result = true;
    }
    return result;
}

// Whether, in 16- and 32-bit modes, the instruction can take a segment override in its decorations: it has an address
// (a memory operand in a form without FS or GS, a moffs, or one it makes itself but a branch's) and no NOTRACK.
static bool x86_test_takes_override(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint8_t properties = x86_form_legacy[form - x86_form_descriptions];
    const bool moffs = (properties & (BUSTER_X86_LEGACY_ADDRESS_16 | BUSTER_X86_LEGACY_ADDRESS_32)) != 0;
    const bool implicit = (properties & BUSTER_X86_LEGACY_IMPLICIT_ADDRESS) && form->syntax != X86_SYNTAX_REL;
    const bool notrack = form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_has_prefix(form, 0x3e);
    return ((instruction->mode & BUSTER_X86_MODE_MEMORY) || moffs || implicit) && x86_form_segment(instruction->form) == 0 && !notrack;
}

// A random memory operand for 16- and 32-bit modes (x86_random_instruction's): of the mode's address size, or one time
// in four the other one (BUSTER_X86_MODE_ADDRESS_SIZE), but always 32-bit for a VSIB, SIB or MPX operand, which can't
// be 16-bit. A 16-bit address is BX or BP and SI or DI, one of the four alone or absolute, with a displacement modulo
// 2^16, signed or not; a 32-bit one a base, a base and an index, an index alone or absolute. And an FS or GS segment
// override one time in eight.
static void x86_random_legacy_memory(uint64_t* state, X86Instruction* instruction, const X86FormDescription* form)
{
    const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
    const bool sib = (form->rm & BUSTER_X86_OPERAND_SIB) != 0;
    const bool bound = (form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_BND;
    const bool mode16 = x86_test_mode == X86_ENCODER_MODE_16;
    const bool chosen = x86_random_below(state, 4) == 0;
    const bool other = vsib || sib || bound ? mode16 : chosen;
    if (mode16 != other)
    {
        static const uint8_t bases[] = { X86_REGISTER_RBX, X86_REGISTER_RBP, X86_REGISTER_RSI, X86_REGISTER_RDI };
        const uint32_t kind = x86_random_below(state, 8);
        instruction->mode = kind < 3 ? BUSTER_X86_MEMORY_BASE_INDEX(0) : kind < 6 ? BUSTER_X86_MEMORY_BASE : BUSTER_X86_MEMORY_ABSOLUTE;
        instruction->rm = kind < 3 ? bases[x86_random_below(state, 2)] : kind < 6 ? bases[x86_random_below(state, 4)] : 0;
        instruction->index = kind < 3 ? bases[2 + x86_random_below(state, 2)] : 0;
        const int32_t displacement = x86_random_displacement(state, form->disp8_scale);
        instruction->displacement = x86_random_below(state, 2) == 0 ? (int32_t)(int16_t)displacement : (int32_t)(uint16_t)displacement;
    }
    else
    {
        const uint8_t scale = (uint8_t)(x86_random_below(state, 4) & ((form->rm & BUSTER_X86_OPERAND_UNSCALED) ? 0 : 3));
        const uint32_t kind = x86_random_below(state, 17);
        instruction->mode = kind < 8    ? BUSTER_X86_MEMORY_BASE
                            : kind < 13 ? BUSTER_X86_MEMORY_BASE_INDEX(scale)
                            : kind < 15 ? BUSTER_X86_MEMORY_INDEX(scale)
                                        : BUSTER_X86_MEMORY_ABSOLUTE;
        // A VSIB operand always has an index, a vector register of its class, and a base three times in four.
        if (vsib)
        {
            instruction->mode = x86_random_below(state, 4) == 0 ? BUSTER_X86_MEMORY_INDEX(scale) : BUSTER_X86_MEMORY_BASE_INDEX(scale);
        }
        if (instruction->mode & BUSTER_X86_MODE_BASE)
        {
            instruction->rm = x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, false);
        }
        if (instruction->mode & BUSTER_X86_MODE_INDEX)
        {
            instruction->index = vsib ? x86_random_register(state, form->rm, form->family, false) : x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, false);
        }
        // A SIB operand's base alone is the base and SIB's none (ESP's index).
        if (sib && instruction->mode == BUSTER_X86_MEMORY_BASE)
        {
            instruction->mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
            instruction->index = X86_REGISTER_RSP;
        }
        instruction->displacement = x86_random_displacement(state, form->disp8_scale);
    }
    instruction->mode |= other ? BUSTER_X86_MODE_ADDRESS_SIZE : 0;
    if (x86_random_below(state, 8) == 0)
    {
        instruction->form = (uint16_t)(instruction->form + (x86_random_below(state, 2) == 0 ? BUSTER_X86_FORM_FS : BUSTER_X86_FORM_GS));
    }
}

// A random valid instruction of the form. For --print-assembly (`for_assembler`), one GNU as encodes with this form.
// With APX, a form with REX2 or an EVEX form has general registers, bases and general indexes from 16 to 31 half the
// time (and with a segment override, maybe one that has no REX2, which the loop tries again).
static X86Instruction x86_random_instruction(uint64_t* state, uint16_t form_index, bool for_assembler)
{
    const X86FormDescription* form = &x86_form_descriptions[form_index];
    const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
    const bool sib = (form->rm & BUSTER_X86_OPERAND_SIB) != 0;
    const bool extended = x86_test_mode == X86_ENCODER_MODE_APX && x86_form_has_apx_registers(form_index);
    X86Instruction result;
    bool done = false;
    while (!done)
    {
        result = (X86Instruction){ .form = form_index };
        if (form->encoding == BUSTER_X86_ENCODING_R)
        {
            result.reg = x86_random_register(state, form->reg, form->family, extended);
        }
        if (form->vvvv != BUSTER_X86_OPERAND_NONE)
        {
            result.vvvv = x86_random_register(state, form->vvvv, form->family, extended);
        }
        if (form->rm != BUSTER_X86_OPERAND_NONE)
        {
            const bool can_register = (form->rm & BUSTER_X86_OPERAND_REGISTER) != 0;
            const bool can_memory = (form->rm & BUSTER_X86_OPERAND_MEMORY) != 0;
            if (can_register && (!can_memory || x86_random_below(state, 2) == 0))
            {
                result.rm = x86_random_register(state, form->rm, form->family, extended);
            }
            else if (x86_mode_is_legacy(x86_test_mode))
            {
                x86_random_legacy_memory(state, &result, form);
            }
            else
            {
                const uint8_t scale = (uint8_t)(x86_random_below(state, 4) & ((form->rm & BUSTER_X86_OPERAND_UNSCALED) ? 0 : 3));
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
                // A VSIB operand always has an index, a vector register of its class, and a base three times in four.
                if (vsib)
                {
                    result.mode = x86_random_below(state, 4) == 0 ? BUSTER_X86_MEMORY_INDEX(scale) : BUSTER_X86_MEMORY_BASE_INDEX(scale);
                }
                if (result.mode & BUSTER_X86_MODE_BASE)
                {
                    result.rm = x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, extended);
                }
                if (result.mode & BUSTER_X86_MODE_INDEX)
                {
                    result.index = vsib ? x86_random_register(state, form->rm, form->family, false)
                                        : x86_random_register(state, BUSTER_X86_OPERAND_GQ, BUSTER_X86_FAMILY_LEGACY, extended);
                }
                // A SIB operand's base alone is the base and SIB's none (RSP's index); it can't be RIP-relative, and
                // none takes no scale (the loop tries again).
                if (sib && result.mode == BUSTER_X86_MEMORY_BASE)
                {
                    result.mode = BUSTER_X86_MEMORY_BASE_INDEX(0);
                    result.index = X86_REGISTER_RSP;
                }
                // An FS or GS segment override one time in eight.
                if (x86_random_below(state, 8) == 0)
                {
                    result.form = (uint16_t)(result.form + (x86_random_below(state, 2) == 0 ? BUSTER_X86_FORM_FS : BUSTER_X86_FORM_GS));
                }
                result.displacement = x86_random_displacement(state, form->disp8_scale);
            }
        }
        // An opmask half the time (and always for a gather or scatter, which needs one), zeroing half of those (not into
        // memory), and any rounding mode.
        if ((form->evex & BUSTER_X86_EVEX_MASK) && (vsib || x86_random_below(state, 2) == 0))
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
        // In 16- and 32-bit modes, a segment override in the decorations one time in eight, where the instruction has
        // an address that can take one (see x86_test_takes_override).
        if (x86_mode_is_legacy(x86_test_mode) && x86_test_takes_override(&result) && x86_random_below(state, 8) == 0)
        {
            result.decorations |= (uint8_t)BUSTER_X86_SEGMENT_OVERRIDE(x86_random_below(state, 6));
        }
        const uint32_t opcode = x86_form_opcode(form);
        const bool sign_extended = opcode == 0x83 || opcode == 0x6b || opcode == 0x6a;
        result.immediate = x86_random_immediate(state, form->immediate_bytes, sign_extended || (for_assembler && form->immediate_bytes == 4));
        if (x86_form_has_operation_byte(form))
        {
            result.immediate = (int32_t)(form->opcode & 0xff);
        }
        if (x86_form_has_is4(form))
        {
            result.immediate = x86_random_register(state, form->reg, form->family, false) << 4;
            result.immediate |= x86_form_has_is4_selector(form) ? (int32_t)x86_random_below(state, 16) : 0;
        }
        if (form->immediate_bytes == 8)
        {
            result.displacement = (int32_t)(uint32_t)x86_test_random(state);
        }
        else if (form->immediate_bytes == 6)
        {
            // A ptr16:32's selector.
            result.displacement = (int32_t)x86_random_below(state, 65536);
        }
        // In 16- and 32-bit modes, the other address size one time in four for an instruction that makes its addresses
        // itself.
        const uint32_t row = (uint32_t)(form - x86_form_descriptions);
        if (x86_mode_is_legacy(x86_test_mode) && (x86_form_legacy[row] & BUSTER_X86_LEGACY_IMPLICIT_ADDRESS) && !(result.mode & BUSTER_X86_MODE_MEMORY) &&
            x86_random_below(state, 4) == 0)
        {
            result.mode |= BUSTER_X86_MODE_ADDRESS_SIZE;
        }
        if (form->relative_bytes == 1)
        {
            result.displacement = (int32_t)x86_random_below(state, 256) - 128;
        }
        else if (form->relative_bytes == 2)
        {
            result.displacement = (int32_t)x86_random_below(state, 65536) - 32768;
        }
        else if (form->relative_bytes == 4)
        {
            result.displacement = x86_random_displacement(state, 1);
        }
        done = x86_test_instruction_is_valid(&result) && !(for_assembler && x86_assembler_prefers_another_form(&result));
    }
    return result;
}

// A random form of the mode other than X86_FORM_NONE (see x86_test_form_exists), or with `with_none`, X86_FORM_NONE one
// time in 16. With 32-bit addresses, one that has instructions with them; with APX, half the time one with REX2 and a
// quarter of the time an EVEX one, whose general registers can be 16 to 31 (the others' can't, and REX2's forms are a
// third of them).
static uint16_t x86_random_form(uint64_t* state, bool with_none)
{
    // The rows 64-bit mode's entry points have are the first BUSTER_X86_FORM_LEGACY_MODES.
    const uint32_t rows = x86_mode_is_legacy(x86_test_mode) ? X86_FORM_COUNT : BUSTER_X86_FORM_LEGACY_MODES;
    uint16_t result = (uint16_t)(1 + x86_random_below(state, rows - 1));
    while ((x86_test_mode == X86_ENCODER_MODE_ADDRESS32 && x86_form_is_bound_memory(result)) || !x86_test_form_exists(result))
    {
        result = (uint16_t)(1 + x86_random_below(state, rows - 1));
    }
    const uint32_t kind = x86_test_mode == X86_ENCODER_MODE_APX ? x86_random_below(state, 4) : 3;
    while ((kind <= 1 && !x86_form_has_rex2(result)) || (kind == 2 && x86_form_description(result)->family != BUSTER_X86_FAMILY_EVEX))
    {
        result = (uint16_t)(1 + x86_random_below(state, rows - 1));
    }
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

// Marks one instruction in BUSTER_X86_TEST_MARKED with the fixup flag, whatever its form (empty ones too): the encoders
// report the end of any marked instruction.
static void x86_mark_random(uint64_t* state, X86Instruction* instructions, uint64_t count)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        instructions[i].form |= x86_random_below(state, BUSTER_X86_TEST_MARKED) == 0 ? BUSTER_X86_FORM_FIXUP : 0;
    }
}

static uint64_t x86_count_marked(const X86Instruction* instructions, uint64_t count)
{
    uint64_t result = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        result += (instructions[i].form & BUSTER_X86_FORM_FIXUP) != 0;
    }
    return result;
}

// The fields of an instruction that --print-assembly prints as symbols (a bit per X86FixupField).
#define BUSTER_X86_TEST_SYMBOLIC_DISPLACEMENT (1u << X86_FIXUP_DISPLACEMENT)
#define BUSTER_X86_TEST_SYMBOLIC_IMMEDIATE (1u << X86_FIXUP_IMMEDIATE)

// Whether the instruction's memory operand has a full displacement field, 32-bit or with a 16-bit address 16-bit:
// always without a base, and after one unless the displacement is zero (but after RBP or R13, or BP alone) or a
// multiple of the form's N whose quotient fits in 8 bits (modulo 2^16 with a 16-bit address).
static bool x86_displacement_is_full(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool address16 = x86_test_address16(instruction);
    const int32_t displacement = address16 ? (int16_t)instruction->displacement : instruction->displacement;
    const int32_t scale = form->disp8_scale;
    const bool bp = address16 ? instruction->rm == X86_REGISTER_RBP && !(instruction->mode & BUSTER_X86_MODE_INDEX) : (instruction->rm & 7) == 5;
    const bool none = displacement == 0 && !bp;
    const bool fits8 = displacement % scale == 0 && displacement / scale >= -128 && displacement / scale <= 127;
    return (instruction->mode & BUSTER_X86_MODE_MEMORY) && (!(instruction->mode & BUSTER_X86_MODE_BASE) || (!none && !fits8));
}

// The fields of the instruction that GNU as encodes the same way with a symbol in them (emitting a relocation for it):
// a full displacement (which GNU as always gives a symbol, even after a base), a rel32, or the rel8 of loop, loope,
// loopne and jrcxz (GNU as makes jmp and jcc to a symbol rel32), and an imm32, a 64-bit immediate or a 64-bit address;
// and in 16- and 32-bit modes, a rel16, an imm16 (not extrq's and insertq's two imm8s) and a far pointer's offset too.
static uint32_t x86_symbolic_fields(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint32_t opcode = x86_form_opcode(form);
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    uint32_t result = 0;
    if (form->relative_bytes == 4 || (form->relative_bytes == 2 && legacy) || (form->relative_bytes == 1 && opcode >= 0xe0 && opcode <= 0xe3) ||
        x86_displacement_is_full(instruction))
    {
        result |= BUSTER_X86_TEST_SYMBOLIC_DISPLACEMENT;
    }
    const bool two_imm8 = form->syntax == X86_SYNTAX_RM_IMM8_IMM8 || form->syntax == X86_SYNTAX_REG_RM_IMM8_IMM8;
    const bool far_pointer = form->syntax == X86_SYNTAX_FAR_POINTER;
    if ((form->immediate_bytes == 4 && !x86_form_has_is4(form)) || form->immediate_bytes == 8 || (form->immediate_bytes == 2 && legacy && !two_imm8) || far_pointer)
    {
        result |= BUSTER_X86_TEST_SYMBOLIC_IMMEDIATE;
    }
    return result;
}

// Chooses one in BUSTER_X86_TEST_SYMBOLIC of the instructions' fields that can be symbolic, `symbolic` a byte of them per
// instruction, and marks the instructions with any. With a random sequence of its own (from the seed), so that the
// instructions, and so their bytes, are the ones --print-assembly makes without marks.
static void x86_mark_symbolic(uint64_t seed, X86Instruction* instructions, uint64_t count, uint8_t* symbolic)
{
    uint64_t state = ~(seed * BUSTER_X86_TEST_GOLDEN_RATIO + 1);
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint32_t fields = x86_symbolic_fields(&instructions[i]);
        uint32_t chosen = 0;
        for (uint32_t field = X86_FIXUP_DISPLACEMENT; field <= X86_FIXUP_IMMEDIATE; field += 1)
        {
            chosen |= (fields & (1u << field)) && x86_random_below(&state, BUSTER_X86_TEST_SYMBOLIC) == 0 ? 1u << field : 0;
        }
        symbolic[i] = (uint8_t)chosen;
        instructions[i].form |= chosen != 0 ? BUSTER_X86_FORM_FIXUP : 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Printing, in GNU as Intel syntax

static const char* const x86_gpr8_names[32] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b",
                                                "r11b", "r12b", "r13b", "r14b", "r15b", "r16b", "r17b", "r18b", "r19b", "r20b", "r21b",
                                                "r22b", "r23b", "r24b", "r25b", "r26b", "r27b", "r28b", "r29b", "r30b", "r31b" };
static const char* const x86_gpr8_high_names[4] = { "ah", "ch", "dh", "bh" };
static const char* const x86_gpr16_names[32] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w",
                                                 "r11w", "r12w", "r13w", "r14w", "r15w", "r16w", "r17w", "r18w", "r19w", "r20w", "r21w",
                                                 "r22w", "r23w", "r24w", "r25w", "r26w", "r27w", "r28w", "r29w", "r30w", "r31w" };
static const char* const x86_gpr32_names[32] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d",
                                                 "r11d", "r12d", "r13d", "r14d", "r15d", "r16d", "r17d", "r18d", "r19d", "r20d", "r21d",
                                                 "r22d", "r23d", "r24d", "r25d", "r26d", "r27d", "r28d", "r29d", "r30d", "r31d" };
static const char* const x86_gpr64_names[32] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10",
                                                 "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
                                                 "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31" };
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
static const char* const x86_mmx_names[8] = { "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7" };
static const char* const x86_control_names[16] = { "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7",
                                                   "cr8", "cr9", "cr10", "cr11", "cr12", "cr13", "cr14", "cr15" };
static const char* const x86_debug_names[8] = { "dr0", "dr1", "dr2", "dr3", "dr4", "dr5", "dr6", "dr7" };
static const char* const x86_test_register_names[8] = { "tr0", "tr1", "tr2", "tr3", "tr4", "tr5", "tr6", "tr7" };
static const char* const x86_tile_names[8] = { "tmm0", "tmm1", "tmm2", "tmm3", "tmm4", "tmm5", "tmm6", "tmm7" };
static const char* const x86_bound_names[4] = { "bnd0", "bnd1", "bnd2", "bnd3" };
// The segment registers, and the two numbers past GS that x86_validate rejects.
static const char* const x86_segment_names[8] = { "es", "cs", "ss", "ds", "fs", "gs", "sreg6", "sreg7" };
// The rounding modes (BUSTER_X86_DECORATION_ROUNDING), as GNU as writes them.
static const char* const x86_rounding_names[4] = { "{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}" };

// A register's name (an invalid one's, too: the numbers wrap around).
static const char* x86_register_name(uint8_t value, uint16_t operand)
{
    const uint8_t number = value & BUSTER_X86_REGISTER_NUMBER;
    const char* result;
    switch (operand & BUSTER_X86_OPERAND_CLASS)
    {
        case BUSTER_X86_CLASS_GPR8: result = (value & BUSTER_X86_REGISTER_HIGH_BYTE) ? x86_gpr8_high_names[number & 3] : x86_gpr8_names[number]; break;
        case BUSTER_X86_CLASS_GPR16: result = x86_gpr16_names[number]; break;
        case BUSTER_X86_CLASS_GPR32: result = x86_gpr32_names[number]; break;
        case BUSTER_X86_CLASS_GPR64: result = x86_gpr64_names[number]; break;
        case BUSTER_X86_CLASS_YMM: result = x86_ymm_names[number]; break;
        case BUSTER_X86_CLASS_ZMM: result = x86_zmm_names[number]; break;
        case BUSTER_X86_CLASS_OPMASK: result = x86_opmask_names[number & 7]; break;
        case BUSTER_X86_CLASS_BND: result = x86_bound_names[number & 3]; break;
        case BUSTER_X86_CLASS_ST: result = x86_st_names[number & 7]; break;
        case BUSTER_X86_CLASS_MMX: result = x86_mmx_names[number & 7]; break;
        case BUSTER_X86_CLASS_CONTROL: result = x86_control_names[number & 15]; break;
        case BUSTER_X86_CLASS_DEBUG: result = (operand & BUSTER_X86_OPERAND_TEST) ? x86_test_register_names[number & 7] : x86_debug_names[number & 7]; break;
        case BUSTER_X86_CLASS_SEGMENT: result = x86_segment_names[number & 7]; break;
        case BUSTER_X86_CLASS_TILE: result = x86_tile_names[number & 7]; break;
        default: result = x86_xmm_names[number]; break;
    }
    return result;
}

// The number of the form's legacy prefixes.
static uint32_t x86_prefix_count(const X86FormDescription* form)
{
    uint32_t result = 0;
    for (uint32_t prefixes = form->prefixes; prefixes != 0; prefixes >>= 8)
    {
        result += 1;
    }
    return result;
}

static const char* x86_accumulator_name(uint32_t bytes)
{
    return bytes == 1 ? "al" : bytes == 2 ? "ax" : bytes == 4 ? "eax" : "rax";
}

// A memory operand; a VSIB one's index is a vector register of the rm operand's class, and a SIB one's index 4 is none,
// which isn't printed (GNU as reads riz as a symbol in Intel syntax). With a symbolic displacement (`symbolic`), the
// displacement is dsym plus the field's value.
static void x86_print_memory(FILE* file, const X86Instruction* instruction, uint32_t operand_bytes, uint32_t symbolic)
{
    const uint16_t rm = x86_form_description(instruction->form)->rm;
    const bool none = (rm & BUSTER_X86_OPERAND_SIB) && instruction->index == X86_REGISTER_RSP;
    const uint8_t mode = none ? (uint8_t)(instruction->mode & ~BUSTER_X86_MODE_INDEX) : instruction->mode;
    // A 16-bit address's registers (BX, BP, SI and DI, without a scale), or a 32-bit one's.
    const bool address16 = x86_test_address16(instruction);
    const bool address32 = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 || (x86_mode_is_legacy(x86_test_mode) && !address16);
    const char* const* address_names = address16 ? x86_gpr16_names : address32 ? x86_gpr32_names : x86_gpr64_names;
    const char* index = (rm & BUSTER_X86_OPERAND_VSIB) ? x86_register_name(instruction->index, rm) : address_names[instruction->index & BUSTER_X86_REGISTER_NUMBER];
    const uint32_t override = x86_form_segment(instruction->form);
    const char* segment = override == 0 ? "" : override == 1 ? "fs:" : "gs:";
    const char* symbol = (symbolic & BUSTER_X86_TEST_SYMBOLIC_DISPLACEMENT) ? "dsym" : "";
    // A far pointer of 6 bytes is an fword; the x87 environment and state areas (14, 28, 94 and 108 bytes) and MOVDIR64B's
    // 64 bytes (0 in its row) have no size keyword.
    const char* size = operand_bytes == 1 ? "byte ptr " : operand_bytes == 2 ? "word ptr " : operand_bytes == 4 ? "dword ptr " : operand_bytes == 6 ? "fword ptr " :
                       operand_bytes == 8 ? "qword ptr " : operand_bytes == 10 ? "tbyte ptr " : operand_bytes == 16 ? "xmmword ptr " :
                       operand_bytes == 32 ? "ymmword ptr " : operand_bytes == 64 ? "zmmword ptr " : "";
    if ((mode & (BUSTER_X86_MODE_BASE | BUSTER_X86_MODE_INDEX | BUSTER_X86_MODE_RIP)) == 0)
    {
        // A 16-bit absolute address is unsigned.
        const int32_t displacement = address16 && *symbol == 0 ? (int32_t)(uint16_t)instruction->displacement : instruction->displacement;
        fprintf(file, *symbol != 0 ? "%s%s%s%+d" : "%s%s%s%d", size, *segment != 0 ? segment : "ds:", symbol, displacement);
    }
    else
    {
        fprintf(file, "%s%s[", size, segment);
        if (mode & BUSTER_X86_MODE_RIP)
        {
            fprintf(file, x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? "eip" : "rip");
        }
        if (mode & BUSTER_X86_MODE_BASE)
        {
            fprintf(file, "%s", address_names[instruction->rm & BUSTER_X86_REGISTER_NUMBER]);
        }
        if (mode & BUSTER_X86_MODE_INDEX)
        {
            fprintf(file, address16 ? "%s%s" : "%s%s*%d", (mode & BUSTER_X86_MODE_BASE) ? "+" : "", index, 1 << (mode & BUSTER_X86_MODE_SCALE));
        }
        fprintf(file, "%s%s%+d]", *symbol != 0 ? "+" : "", symbol, instruction->displacement);
    }
}

// Whether the form is a far branch through memory (call or jmp to a far pointer: FF /3 and FF /5).
static bool x86_form_is_far_branch(const X86FormDescription* form)
{
    const uint8_t digit = form->encoding & 7;
    return form->family == BUSTER_X86_FAMILY_LEGACY && form->opcode_bytes == 1 && form->opcode == 0xff && (form->encoding & BUSTER_X86_ENCODING_DIGIT) &&
           (digit == 3 || digit == 5);
}

// Whether the form is a near call or jmp through a 32-bit register or doubleword (with NOTRACK or BND too).
static bool x86_form_is_near32_branch(uint32_t row)
{
    return row == X86_FORM_CALL_RM32 || row == X86_FORM_JMP_RM32 || row == X86_FORM_NOTRACK_CALL_RM32 || row == X86_FORM_NOTRACK_JMP_RM32 ||
           row == X86_FORM_BND_CALL_RM32 || row == X86_FORM_BND_JMP_RM32;
}

// Whether the printer writes the instruction's memory operand without a size, as its mnemonic and prefix say it: in 16-
// and 32-bit modes, a far branch's (lcall or ljmp) and a 6-byte pseudo-descriptor's (lgdtw's and the others', which GNU
// as takes with its suffix alone), and in 16-bit mode, a near call's or jmp's through a doubleword, which GNU as takes
// for a far one there.
static bool x86_printed_without_size(const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    const bool descriptor = form->rm == BUSTER_X86_OPERAND_M && form->operand_bytes == 6;
    return x86_mode_is_legacy(x86_test_mode) && (instruction->mode & BUSTER_X86_MODE_MEMORY) &&
           (x86_form_is_far_branch(form) || descriptor || (x86_test_mode == X86_ENCODER_MODE_16 && x86_form_is_near32_branch(row)));
}

// A broadcast's memory operand is its element, whose size is the disp8 scale (and see x86_printed_without_size).
static void x86_print_rm(FILE* file, const X86Instruction* instruction, const X86FormDescription* form, uint32_t symbolic)
{
    if (instruction->mode & BUSTER_X86_MODE_MEMORY)
    {
        const uint32_t operand_bytes = (form->evex & BUSTER_X86_EVEX_BROADCAST) ? form->disp8_scale : x86_printed_without_size(instruction) ? 0 : form->operand_bytes;
        x86_print_memory(file, instruction, operand_bytes, symbolic);
    }
    else
    {
        fprintf(file, "%s", x86_register_name(instruction->rm, x86_address_register_operand(instruction, form->rm)));
    }
}

// The operands of a syntax, in order (r for reg, v for vvvv, m for rm, i for the immediate, c for CL, 1 for 1, s for
// ST(0), a for the accumulator, 4 for the /is4 register and z for the selector below it), for the syntaxes
// x86_print_instruction doesn't print itself.
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
        case X86_SYNTAX_REG_VVVV_RM_IS4: result = "rvm4"; break;
        case X86_SYNTAX_REG_VVVV_IS4_RM: result = "rv4m"; break;
        case X86_SYNTAX_REG_VVVV_RM_IS4_IMM: result = "rvm4z"; break;
        case X86_SYNTAX_REG_VVVV_IS4_RM_IMM: result = "rv4mz"; break;
        case X86_SYNTAX_IMM_RM: result = "im"; break;
        case X86_SYNTAX_REG: result = "r"; break;
        case X86_SYNTAX_VVVV_REG_RM: result = "vrm"; break;
        case X86_SYNTAX_VVVV_RM_REG: result = "vmr"; break;
        case X86_SYNTAX_VVVV_RM_ONE: result = "vm1"; break;
        case X86_SYNTAX_VVVV_RM_CL: result = "vmc"; break;
        case X86_SYNTAX_VVVV_RM_REG_IMM: result = "vmri"; break;
        case X86_SYNTAX_VVVV_RM_REG_CL: result = "vmrc"; break;
        default: break;
    }
    return result;
}

// Whether the form is a VEX one whose mnemonic an EVEX form has too, from a table of every form that the first call
// fills.
static bool x86_vex_form_shares_mnemonic(uint32_t form)
{
    static bool filled = false;
    static bool shares[X86_FORM_COUNT];
    if (!filled)
    {
        for (uint32_t vex = 0; vex < X86_FORM_COUNT; vex += 1)
        {
            if (x86_form_descriptions[vex].family == BUSTER_X86_FAMILY_VEX)
            {
                for (uint32_t evex = 0; evex < X86_FORM_COUNT && !shares[vex]; evex += 1)
                {
                    shares[vex] = x86_form_descriptions[evex].family == BUSTER_X86_FAMILY_EVEX &&
                                  strcmp(x86_form_descriptions[evex].mnemonic, x86_form_descriptions[vex].mnemonic) == 0;
                }
            }
        }
        filled = true;
    }
    return shares[(form & BUSTER_X86_FORM_NUMBER) % X86_FORM_COUNT];
}

// Whether the form is one of cfcmovcc's, whose NF picks the store form: GNU as takes {store} for that, but no {nf}.
static bool x86_form_is_cfcmov(const X86FormDescription* form)
{
    return strncmp(form->mnemonic, "cfcmov", 6) == 0;
}

// A far pointer's offset, signed: a ptr16:16's low 16 bits, or a ptr16:32's immediate.
static int32_t x86_far_offset(const X86Instruction* instruction)
{
    return x86_form_description(instruction->form)->immediate_bytes == 4 ? (int32_t)(int16_t)instruction->immediate : instruction->immediate;
}

// The names of the flags in a default flags value, from bit 3 to bit 0.
static const char* const x86_dfv_names[] = { "of", "sf", "zf", "cf" };

// Prints the form's mnemonic as GNU as takes it in the mode. In 16- and 32-bit modes, jrcxz is jecxz or jcxz by the
// address size, sysexitd and sysretd are sysexit and sysret, and a far branch through memory is lcall or ljmp, which
// like a far branch to a pointer in the immediate takes data16 or data32 for a pointer of the other operand size (a
// 16-bit offset is a doubleword pointer, a 32-bit one a fword one). In 16-bit mode, whose operand size is 16-bit, a
// 16-bit form's data16 is the mode's, a branch by a rel32, xbegin's and a near call or jmp through a doubleword in
// memory take data32, and the 32-bit forms of the x87 environment and state instructions (28 and 108 bytes) a d suffix,
// which GNU as takes for their 66.
static void x86_print_mnemonic(FILE* file, const X86Instruction* instruction)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const uint32_t row = (uint32_t)(form - x86_form_descriptions);
    const char* mnemonic = form->mnemonic;
    const char* prefix = "";
    const char* suffix = "";
    if (x86_mode_is_legacy(x86_test_mode))
    {
        const bool far = x86_form_is_far_branch(form);
        const bool far_pointer = far || form->syntax == X86_SYNTAX_FAR_POINTER;
        const bool call = (form->encoding & 7) == 3;
        mnemonic = row == X86_FORM_JRCXZ_REL8 ? (x86_test_address16(instruction) ? "jcxz" : "jecxz")
                   : row == X86_FORM_SYSEXIT  ? "sysexit"
                   : row == X86_FORM_SYSRET   ? "sysret"
                   : far                      ? (call ? "lcall" : "ljmp")
                                              : mnemonic;
        const bool far16 = far_pointer && form->operand_bytes == 4;
        prefix = far_pointer && far16 != (x86_test_mode == X86_ENCODER_MODE_16) ? (far16 ? "data16 " : "data32 ") : "";
    }
    if (x86_test_mode == X86_ENCODER_MODE_16)
    {
        const bool environment = form->family == BUSTER_X86_FAMILY_LEGACY && (form->operand_bytes == 28 || form->operand_bytes == 108);
        const bool near32 = x86_form_is_near32_branch(row) && (instruction->mode & BUSTER_X86_MODE_MEMORY);
        mnemonic += strncmp(mnemonic, "data16 ", 7) == 0 ? 7 : 0;
        prefix = form->relative_bytes == 4 || near32 ? "data32 " : prefix;
        suffix = environment && (x86_form_legacy[row] & BUSTER_X86_LEGACY_SIZE_32) ? "d" : "";
    }
    fprintf(file, "%s%s%s", prefix, mnemonic, suffix);
}

// Prints the instruction in GNU as Intel syntax. A register-to-register move or arithmetic instruction gets {load} or
// {store}, which picks the form with ModRM.reg as the destination or the source (or with ND, the first source or the
// second: a destination in EVEX.vvvv) (and keeps GNU as from swapping a VEX
// move's operands to make it VEX2, and picks XOP's W1 shifts). An EVEX form gets {evex}, which GNU as otherwise only
// picks when the operands need it (and an APX NF form {nf} too, but cfcmov's), and a VEX form whose mnemonic an EVEX
// form has gets {vex}: GNU as picks EVEX for some of those (vpdpbusd, vpmadd52luq and vcvtneps2bf16, whose AVX-512
// forms came first). ccmp and ctest, which only have EVEX forms, get neither {evex} nor, with a default flags value
// ({dfv=...} after the mnemonic, which GNU as doesn't take after a pseudo-prefix), {load} or {store}. The opmask and
// zeroing follow the first operand, a broadcast follows the memory operand, and embedded rounding or suppressed
// exceptions come after the registers. The fields in `symbolic` are symbols plus the fields' values: dsym for the
// displacement (a memory operand's or a branch's target) and offset isym for the immediate (or ds:isym for a 64-bit
// address).
static void x86_print_instruction(FILE* file, const X86Instruction* instruction, uint32_t symbolic)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    // In 16- and 32-bit modes, a segment override in the decorations as a prefix, which GNU as keeps even where it's
    // the address's default segment (in the memory operand, it drops that: ds:[bx], ss:[bp]).
    const uint32_t override = x86_mode_is_legacy(x86_test_mode) ? (instruction->decorations & BUSTER_X86_DECORATION_SEGMENT) >> 3 : 0;
    if (override >= 1 && override <= 6)
    {
        fprintf(file, "%s ", x86_segment_names[override - 1]);
    }
    const bool symbolic_immediate = (symbolic & BUSTER_X86_TEST_SYMBOLIC_IMMEDIATE) != 0;
    const bool register_direct = (instruction->mode & BUSTER_X86_MODE_MEMORY) == 0 && form->rm != BUSTER_X86_OPERAND_NONE;
    const uint8_t decorations = instruction->decorations;
    // A 64-bit immediate continues in the displacement, and an MSR index is unsigned.
    const bool msr_index = x86_form_has_msr_index(form);
    const int64_t immediate = form->immediate_bytes == 8 ? (int64_t)((uint64_t)(uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32)) : msr_index ? (int64_t)(uint32_t)instruction->immediate : instruction->immediate;
    const bool dfv = form->vvvv == BUSTER_X86_OPERAND_DFV;
    // A moffs of 16 or 32 bits (in 16- and 32-bit modes) is unsigned.
    const unsigned long long moffs_mask = form->immediate_bytes == 8 ? ~0ull : (1ull << (8 * form->immediate_bytes)) - 1;
    if (form->family == BUSTER_X86_FAMILY_EVEX && !dfv)
    {
        fprintf(file, (form->evex & BUSTER_X86_EVEX_NF) && !x86_form_is_cfcmov(form) ? "{evex} {nf} " : "{evex} ");
    }
    if (form->family == BUSTER_X86_FAMILY_VEX && x86_vex_form_shares_mnemonic(instruction->form))
    {
        fprintf(file, "{vex} ");
    }
    if (register_direct && form->reg != BUSTER_X86_OPERAND_NONE && !(dfv && instruction->vvvv != 0) &&
        (form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_REG_RM || form->syntax == X86_SYNTAX_REG_VVVV_RM ||
         form->syntax == X86_SYNTAX_VVVV_RM_REG || form->syntax == X86_SYNTAX_VVVV_REG_RM))
    {
        // XOP's shifts and rotates by a vector of counts swap their sources with W1, which {store} picks.
        const bool swapped = form->w && form->syntax == X86_SYNTAX_REG_VVVV_RM && x86_opcode_map(form) >= 8;
        fprintf(file, "%s ", form->syntax == X86_SYNTAX_RM_REG || form->syntax == X86_SYNTAX_VVVV_RM_REG || swapped ? "{store}" : "{load}");
    }
    if (form->syntax == X86_SYNTAX_REL && form->relative_bytes >= 2 && form->opcode != 0xe8)
    {
        // In 16-bit mode, {disp16} for a rel16, and for a rel32 nothing (data32 picks it: see x86_print_mnemonic).
        fprintf(file, x86_test_mode != X86_ENCODER_MODE_16 ? "{disp32} " : form->relative_bytes == 2 ? "{disp16} " : "");
    }
    fprintf(file, "%s", x86_printed_address_prefix(instruction));
    x86_print_mnemonic(file, instruction);
    if (dfv && instruction->vvvv != 0)
    {
        const char* separator = " {dfv=";
        for (uint32_t i = 0; i < 4; i += 1)
        {
            if (instruction->vvvv & (8 >> i))
            {
                fprintf(file, "%s%s", separator, x86_dfv_names[i]);
                separator = ", ";
            }
        }
        fprintf(file, "}");
    }
    switch (form->syntax)
    {
        case X86_SYNTAX_IMM_IMM:
            fprintf(file, " %d, %d", instruction->immediate & 0xffff, (instruction->immediate >> 16) & 0xff);
            break;
        case X86_SYNTAX_REL:
            // `.` is the start of the instruction (its prefixes too), and the displacement counts from its end.
            if (symbolic & BUSTER_X86_TEST_SYMBOLIC_DISPLACEMENT)
            {
                fprintf(file, " dsym%+d", instruction->displacement);
            }
            else
            {
                // The instruction's length, which the mode's prefixes change in 16- and 32-bit modes (or for an invalid
                // instruction, the form's).
                uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
                const int64_t length = x86_test_encode_scalar(instruction, 1, bytes, sizeof(bytes), 0, 0);
                const int64_t own = length > 0 ? length : (int64_t)(x86_prefix_count(form) + form->opcode_bytes + form->relative_bytes);
                fprintf(file, " .%+lld", (long long)instruction->displacement + own);
            }
            break;
        case X86_SYNTAX_ACC_IMM:
            fprintf(file, symbolic_immediate ? " %s, offset isym%+lld" : " %s, %lld", x86_accumulator_name(form->operand_bytes), (long long)immediate);
            break;
        case X86_SYNTAX_IMM_ACC:
            fprintf(file, " %lld, %s", (long long)immediate, x86_accumulator_name(form->operand_bytes));
            break;
        case X86_SYNTAX_ACC_DX:
            fprintf(file, " %s, dx", x86_accumulator_name(form->operand_bytes));
            break;
        case X86_SYNTAX_DX_ACC:
            fprintf(file, " dx, %s", x86_accumulator_name(form->operand_bytes));
            break;
        case X86_SYNTAX_MEMORY_RCX_ACC:
            fprintf(file, " [rcx], %s", x86_accumulator_name(form->operand_bytes));
            break;
        case X86_SYNTAX_ACC_MOFFS:
            if (symbolic_immediate)
            {
                fprintf(file, " %s, ds:isym%+lld", x86_accumulator_name(form->operand_bytes), (long long)immediate);
            }
            else
            {
                fprintf(file, " %s, ds:0x%llx", x86_accumulator_name(form->operand_bytes), (unsigned long long)immediate & moffs_mask);
            }
            break;
        case X86_SYNTAX_MOFFS_ACC:
            if (symbolic_immediate)
            {
                fprintf(file, " ds:isym%+lld, %s", (long long)immediate, x86_accumulator_name(form->operand_bytes));
            }
            else
            {
                fprintf(file, " ds:0x%llx, %s", (unsigned long long)immediate & moffs_mask, x86_accumulator_name(form->operand_bytes));
            }
            break;
        case X86_SYNTAX_FAR_POINTER:
        {
            // A ptr16:16 in the immediate, or a ptr16:32's offset in it and its selector in the displacement (with a
            // symbolic offset, isym plus its value, signed).
            const bool far16 = form->immediate_bytes == 4;
            const uint32_t offset = far16 ? (uint32_t)instruction->immediate & 0xffff : (uint32_t)instruction->immediate;
            const uint32_t selector = far16 ? (uint32_t)instruction->immediate >> 16 : (uint32_t)instruction->displacement & 0xffff;
            fprintf(file, symbolic_immediate ? " %u:isym%+d" : " %u:%u", selector, symbolic_immediate ? x86_far_offset(instruction) : (int32_t)offset);
            break;
        }
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
                    case 'r': fprintf(file, "%s", x86_register_name(instruction->reg, x86_address_register_operand(instruction, form->reg))); break;
                    case 'v': fprintf(file, "%s", x86_register_name(instruction->vvvv, form->vvvv)); break;
                    case 'm':
                        x86_print_rm(file, instruction, form, symbolic);
                        if ((instruction->mode & BUSTER_X86_MODE_MEMORY) && (form->evex & BUSTER_X86_EVEX_BROADCAST))
                        {
                            fprintf(file, "{1to%u}", (uint32_t)form->operand_bytes / form->disp8_scale);
                        }
                        break;
                    case 'i': fprintf(file, symbolic_immediate ? "offset isym%+lld" : "%lld", (long long)immediate); break;
                    case 'j': fprintf(file, "%d", instruction->immediate & 0xff); break;
                    case 'k': fprintf(file, "%d", (instruction->immediate >> 8) & 0xff); break;
                    case 'x': fprintf(file, "xmm0"); break;
                    case '4': fprintf(file, "%s", x86_register_name((uint8_t)((uint32_t)instruction->immediate >> 4), form->reg)); break;
                    case 'z': fprintf(file, "%d", instruction->immediate & 15); break;
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

// Whether the form is one of APX's map 4 forms with an imm32 and a memory operand, whose longest encoding (with a SIB
// byte and a 32-bit displacement) is 15 bytes, leaving no room for a segment override or a 67.
static bool x86_form_is_map4_imm32(uint32_t form_index)
{
    const X86FormDescription* form = x86_form_description(form_index);
    return form->family == BUSTER_X86_FAMILY_EVEX && x86_opcode_map(form) == 4 && form->immediate_bytes == 4 && (form->rm & BUSTER_X86_OPERAND_MEMORY);
}

// Counts the forms whose template in the mode has no room for a memory operand (x86_form_lacks_room), with a segment
// override and without, and returns how many shouldn't: with 64-bit addresses, only map 4's imm32 forms with a segment
// override lack room (REX2's forms take x86_encode's template instead), and with 32-bit addresses, any form with one,
// and map 4's imm32 forms without one.
static uint32_t x86_check_room(X86EncoderMode encoder, uint32_t* with_override, uint32_t* without_override)
{
    uint32_t unexpected = 0;
    *with_override = 0;
    *without_override = 0;
    for (uint32_t form = 1; form < BUSTER_X86_FORM_TEMPLATES; form += 1)
    {
        const bool unfit = x86_form_lacks_room(form, encoder);
        const bool segment = x86_form_segment(form) != 0;
        const bool expected = encoder == X86_ENCODER_MODE_ADDRESS32 ? segment || x86_form_is_map4_imm32(form) : segment && x86_form_is_map4_imm32(form);
        if (unfit && !expected && unexpected < BUSTER_X86_TEST_REPORTED)
        {
            printf("  form %u (%s, segment override %u) has no room for a memory operand (mode %u)\n", form, x86_form_description(form)->mnemonic, x86_form_segment(form),
                   (unsigned)encoder);
        }
        unexpected += unfit && !expected;
        *with_override += unfit && segment;
        *without_override += unfit && !segment;
    }
    return unexpected;
}

static bool x86_check_templates(void)
{
    bool result = x86_encoder_initialize();
    uint32_t with_override = 0;
    uint32_t without_override = 0;
    const uint32_t unexpected_64 = x86_check_room(X86_ENCODER_MODE_64, &with_override, &without_override);
    printf("templates: %u forms (%u of 16- and 32-bit modes alone, which take no instruction), and each with FS and GS, %s, but %u with a segment "
           "override, which have no room for it\n",
           (unsigned)X86_FORM_COUNT, (unsigned)BUSTER_X86_LEGACY_MODE_FORMS, result && unexpected_64 == 0 ? "all fit" : "SOME DON'T FIT", with_override);
    if (!result)
    {
        for (uint32_t form = 0; form < BUSTER_X86_FORM_TEMPLATES; form += 1)
        {
            X86Template template;
            if (!build_template(x86_form_description(form), x86_form_segment(form), X86_ENCODER_MODE_64, &template))
            {
                printf("  form %u (%s, segment override %u) doesn't fit\n", form, x86_form_description(form)->mnemonic, x86_form_segment(form));
            }
        }
    }
    // For 32-bit addresses: forms with a segment override, and map 4's imm32 forms, may have no room for a 67.
    const bool address32 = x86_encoder_initialize_address32();
    const uint32_t unexpected = x86_check_room(X86_ENCODER_MODE_ADDRESS32, &with_override, &without_override);
    printf("templates for 32-bit addresses: %s, but %u with a segment override and %u without, which have no room for a 67\n",
           address32 && unexpected == 0 ? "all fit" : "SOME DON'T FIT", with_override, without_override);
    // For APX: REX2's positions in every legacy form of maps 0 and 1 but the XSAVE family's (x86_form_takes_rex2), and
    // only there, but in those with a segment override that have no room for its payload (x86_form_has_rex2), and in
    // the forms of 16- and 32-bit modes alone, which take no instruction.
    const bool apx = x86_encoder_initialize_apx();
    uint32_t rex2_forms = 0;
    uint32_t without_room = 0;
    uint32_t unexpected_apx = 0;
    for (uint32_t form = 0; form < BUSTER_X86_FORM_TEMPLATES; form += 1)
    {
        const bool legacy_only = x86_form_description(form) - x86_form_descriptions >= BUSTER_X86_FORM_LEGACY_MODES;
        const bool takes = x86_form_takes_rex2(x86_form_description(form)) && !legacy_only;
        const bool has = x86_form_has_rex2(form) && !legacy_only;
        const bool wrong = (takes && !has && x86_form_segment(form) == 0) || (has && !takes);
        if (wrong && unexpected_apx < BUSTER_X86_TEST_REPORTED)
        {
            printf("  form %u (%s, segment override %u) %s REX2's positions\n", form, x86_form_description(form)->mnemonic, x86_form_segment(form), has ? "has" : "lacks");
        }
        rex2_forms += has && x86_form_segment(form) == 0;
        without_room += takes && !has;
        unexpected_apx += wrong;
    }
    const uint32_t unexpected_room_apx = x86_check_room(X86_ENCODER_MODE_APX, &with_override, &without_override);
    printf("templates for APX: %s, %u forms with REX2's positions, and their FS and GS copies but %u without room for its payload, and %u with a segment "
           "override, which have no room for it\n",
           apx && unexpected_apx == 0 && unexpected_room_apx == 0 ? "all fit" : "SOME DON'T FIT", rex2_forms, without_room, with_override);
    result = result && unexpected_64 == 0 && address32 && unexpected == 0 && apx && unexpected_apx == 0 && unexpected_room_apx == 0;
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
    // enter 16, 2 and enterw 0x1234, 5 (66); leavew; endbr64; lock cmpxchg word ptr [rax], cx; crc32 eax, word ptr
    // [rax]; lock cmpxchg16b xmmword ptr [r8]
    { { .form = X86_FORM_ENTER, .immediate = 16 | (2 << 16) }, 4, { 0xc8, 0x10, 0x00, 0x02 } },
    { { .form = X86_FORM_ENTERW, .immediate = 0x1234 | (5 << 16) }, 5, { 0x66, 0xc8, 0x34, 0x12, 0x05 } },
    { { .form = X86_FORM_LEAVEW }, 2, { 0x66, 0xc9 } },
    { { .form = X86_FORM_ENDBR64 }, 4, { 0xf3, 0x0f, 0x1e, 0xfa } },
    // ud0 and ud1 (without ModRM); ud0 eax, ebx; {load} ud1 r9, r10; ud1 ax, word ptr [rbx]; udb; int1
    { { .form = X86_FORM_UD0 }, 2, { 0x0f, 0xff } },
    { { .form = X86_FORM_UD1 }, 2, { 0x0f, 0xb9 } },
    { { .form = X86_FORM_UD0_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX }, 3, { 0x0f, 0xff, 0xc3 } },
    { { .form = X86_FORM_UD1_R64_RM64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10 }, 4, { 0x4d, 0x0f, 0xb9, 0xca } },
    { { .form = X86_FORM_UD1_R16_RM16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0xb9, 0x03 } },
    { { .form = X86_FORM_UDB }, 1, { 0xd6 } },
    { { .form = X86_FORM_INT1 }, 1, { 0xf1 } },
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
    // APX's EVEX forms, here with registers 0 to 15: {evex} andn eax, ebx, dword ptr [rax+64] (an 8-bit 64: N is 1);
    // {nf} andn eax, ebx, ecx and {nf} blsr eax, ecx (NF: P2's bit 2); pdep rax, rbx, qword ptr [rcx]; rorx eax, dword
    // ptr [rcx+64], 3; in map 4, tzcnt ax, word ptr [rcx+64] (66), {nf} tzcnt rax, rcx and {nf} popcnt ax, cx; adcx
    // eax, ebx, dword ptr [rcx+64] (ND: the destination in vvvv), adcx rax, rcx and adox r9, r10; kmovq k1, rax, kmovd
    // k1, dword ptr [rax+64] and kmovb k1, k2; cmpbexadd dword ptr [rax+64], ecx, edx; tileloadd tmm1, [rax+rcx*1+64]
    // and t2rpntlvwz1rs tmm2, [rax+rcx*1] (map 5); urdmsr rax, 0x12345678 and wrmsrns 0x12345678, r9 (map 7), and
    // urdmsr rax, rcx (map 4); crc32 eax, spl and crc32 rax, qword ptr [rcx]; {store} movbe eax, ecx (a register) and
    // movbe word ptr [rcx], ax; movdir64b rax, [rcx+64]; enqcmds r9, [r10]; invept rax, [rcx]; wrussq qword ptr [rcx],
    // rax; aor dword ptr [rcx], eax; and movrs sil, byte ptr [rax]
    { { .form = X86_FORM_ANDN_R32_R32_RM32_EVEX, .reg = X86_REGISTER_RAX, .vvvv = X86_REGISTER_RBX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf2, 0x64, 0x08, 0xf2, 0x40, 0x40 } },
    { { .form = X86_FORM_ANDN_R32_R32_RM32_NF, .reg = X86_REGISTER_RAX, .vvvv = X86_REGISTER_RBX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf2, 0x64, 0x0c, 0xf2, 0xc1 } },
    { { .form = X86_FORM_BLSR_R32_RM32_NF, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf2, 0x7c, 0x0c, 0xf3, 0xc9 } },
    { { .form = X86_FORM_PDEP_R64_R64_RM64_EVEX, .reg = X86_REGISTER_RAX, .vvvv = X86_REGISTER_RBX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0xe7, 0x08, 0xf5, 0x01 } },
    { { .form = X86_FORM_RORX_R32_RM32_IMM8_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .immediate = 3 }, 8, { 0x62, 0xf3, 0x7f, 0x08, 0xf0, 0x41, 0x40, 0x03 } },
    { { .form = X86_FORM_TZCNT_R16_RM16_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf4, 0x7d, 0x08, 0xf4, 0x41, 0x40 } },
    { { .form = X86_FORM_TZCNT_R64_RM64_NF, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0xfc, 0x0c, 0xf4, 0xc1 } },
    { { .form = X86_FORM_POPCNT_R16_RM16_NF, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7d, 0x0c, 0x88, 0xc1 } },
    { { .form = X86_FORM_ADCX_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RBX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf4, 0x7d, 0x18, 0x66, 0x59, 0x40 } },
    { { .form = X86_FORM_ADCX_R64_RM64_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0xfd, 0x08, 0x66, 0xc1 } },
    { { .form = X86_FORM_ADOX_R64_RM64_EVEX, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10 }, 6, { 0x62, 0x54, 0xfe, 0x08, 0x66, 0xca } },
    { { .form = X86_FORM_KMOVQ_K_R64_EVEX, .reg = 1, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf1, 0xff, 0x08, 0x92, 0xc8 } },
    { { .form = X86_FORM_KMOVD_K_M32_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf1, 0xfd, 0x08, 0x90, 0x48, 0x40 } },
    { { .form = X86_FORM_KMOVB_K_K_EVEX, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf1, 0x7d, 0x08, 0x90, 0xca } },
    { { .form = X86_FORM_CMPBEXADD_M32_R32_R32_EVEX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64, .reg = X86_REGISTER_RCX, .vvvv = X86_REGISTER_RDX }, 7, { 0x62, 0xf2, 0x6d, 0x08, 0xe6, 0x48, 0x40 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 64 }, 8, { 0x62, 0xf2, 0x7f, 0x08, 0x4b, 0x4c, 0x08, 0x40 } },
    { { .form = X86_FORM_T2RPNTLVWZ1RS_TMM_SIBMEM_EVEX, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xf5, 0x7d, 0x08, 0xf8, 0x14, 0x08 } },
    { { .form = X86_FORM_URDMSR_R64_IMM32_EVEX, .rm = X86_REGISTER_RAX, .immediate = 0x12345678 }, 10, { 0x62, 0xf7, 0x7f, 0x08, 0xf8, 0xc0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_WRMSRNS_IMM32_R64_EVEX, .rm = X86_REGISTER_R9, .immediate = 0x12345678 }, 10, { 0x62, 0xd7, 0x7e, 0x08, 0xf6, 0xc1, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_URDMSR_R64_R64_EVEX, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7f, 0x08, 0xf8, 0xc8 } },
    { { .form = X86_FORM_CRC32_R32_RM8_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RSP }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xf0, 0xc4 } },
    { { .form = X86_FORM_CRC32_R64_RM64_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0xfc, 0x08, 0xf1, 0x01 } },
    { { .form = X86_FORM_MOVBE_RM32_R32, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x61, 0xc8 } },
    { { .form = X86_FORM_MOVBE_RM16_R16, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7d, 0x08, 0x61, 0x01 } },
    { { .form = X86_FORM_MOVDIR64B_R64_M512_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf4, 0x7d, 0x08, 0xf8, 0x41, 0x40 } },
    { { .form = X86_FORM_ENQCMDS_R64_M512_EVEX, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0x54, 0x7e, 0x08, 0xf8, 0x0a } },
    { { .form = X86_FORM_INVEPT_R64_M128_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7e, 0x08, 0xf0, 0x01 } },
    { { .form = X86_FORM_WRUSSQ_M64_R64_EVEX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0xfd, 0x08, 0x65, 0x01 } },
    { { .form = X86_FORM_AOR_M32_R32_EVEX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7f, 0x08, 0xfc, 0x01 } },
    { { .form = X86_FORM_MOVRS_R8_M8_EVEX, .reg = X86_REGISTER_RSI, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x8a, 0x30 } },
    // APX's map 4 forms of the legacy arithmetic, here with registers 0 to 15: {store} and {load} add eax, ecx; add
    // eax, ecx, edx both ways and add eax, ecx, dword ptr [rdx+64] (ND: the destination in vvvv); {nf} add eax, ecx and
    // {nf} add eax, ecx, edx; add eax, 5 and 500, and add eax, ecx, 5; add ax, cx (66); add al, cl, dl; add rax, rcx,
    // rdx; add byte ptr [rcx], 5; add sil, dil, al; the longest, 15 bytes: add dword ptr [rax+rbx*4+0x12345678],
    // 0x12345678; adc eax, ecx; sbb eax, 5; shl eax, 1, cl and 3; shl eax, ecx, 1 and cl; {nf} sar eax, ecx, 3; {nf}
    // rol al, 1; rcr eax, ecx, cl; inc eax, ecx, {nf} inc eax and inc al; not eax, ecx; {nf} mul ecx; idiv rcx; mul
    // byte ptr [rax]; imul eax, ecx, and eax, ecx, edx; {nf} imul eax, ecx, 5; imulzu ax, cx, 500 and {nf} imulzu ax,
    // cx, 5 (ZU: ND's bit); shld eax, ecx, 3, and eax, ecx, edx, cl; shrd rax, rcx, rdx, 1; cmovb eax, ecx, dword ptr
    // [rdx] and cmovp ax, cx, dx; setb al, setnp byte ptr [rax] and setzua al (F2)
    { { .form = X86_FORM_ADD_RM32_R32_EVEX, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x01, 0xc8 } },
    { { .form = X86_FORM_ADD_R32_RM32_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x03, 0xc1 } },
    { { .form = X86_FORM_ADD_R32_RM32_R32, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0x01, 0xd1 } },
    { { .form = X86_FORM_ADD_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0x03, 0xca } },
    { { .form = X86_FORM_ADD_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xf4, 0x7c, 0x18, 0x03, 0x4a, 0x40 } },
    { { .form = X86_FORM_ADD_RM32_R32_NF, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0x01, 0xc8 } },
    { { .form = X86_FORM_ADD_R32_RM32_R32_NF, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x1c, 0x01, 0xd1 } },
    { { .form = X86_FORM_ADD_RM32_IMM8_EVEX, .rm = X86_REGISTER_RAX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7c, 0x08, 0x83, 0xc0, 0x05 } },
    { { .form = X86_FORM_ADD_RM32_IMM32_EVEX, .rm = X86_REGISTER_RAX, .immediate = 500 }, 10, { 0x62, 0xf4, 0x7c, 0x08, 0x81, 0xc0, 0xf4, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_ADD_R32_RM32_IMM8, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7c, 0x18, 0x83, 0xc1, 0x05 } },
    { { .form = X86_FORM_ADD_RM16_R16_EVEX, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7d, 0x08, 0x01, 0xc8 } },
    { { .form = X86_FORM_ADD_R8_RM8_R8, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0x00, 0xd1 } },
    { { .form = X86_FORM_ADD_R64_RM64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0xfc, 0x18, 0x01, 0xd1 } },
    { { .form = X86_FORM_ADD_RM8_IMM8_EVEX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7c, 0x08, 0x80, 0x01, 0x05 } },
    { { .form = X86_FORM_ADD_R8_RM8_R8, .vvvv = X86_REGISTER_RSI, .rm = X86_REGISTER_RDI, .reg = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x4c, 0x18, 0x00, 0xc7 } },
    { { .form = X86_FORM_ADD_RM32_IMM32_EVEX, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12345678, .immediate = 0x12345678 }, 15, { 0x62, 0xf4, 0x7c, 0x08, 0x81, 0x84, 0x98, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_ADC_RM32_R32_EVEX, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x11, 0xc8 } },
    { { .form = X86_FORM_SBB_RM32_IMM8_EVEX, .rm = X86_REGISTER_RAX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7c, 0x08, 0x83, 0xd8, 0x05 } },
    { { .form = X86_FORM_SHL_RM32_1_EVEX, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xd1, 0xe0 } },
    { { .form = X86_FORM_SHL_RM32_CL_EVEX, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xd3, 0xe0 } },
    { { .form = X86_FORM_SHL_RM32_IMM8_EVEX, .rm = X86_REGISTER_RAX, .immediate = 3 }, 7, { 0x62, 0xf4, 0x7c, 0x08, 0xc1, 0xe0, 0x03 } },
    { { .form = X86_FORM_SHL_R32_RM32_1, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xd1, 0xe1 } },
    { { .form = X86_FORM_SHL_R32_RM32_CL, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xd3, 0xe1 } },
    { { .form = X86_FORM_SAR_R32_RM32_IMM8_NF, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 3 }, 7, { 0x62, 0xf4, 0x7c, 0x1c, 0xc1, 0xf9, 0x03 } },
    { { .form = X86_FORM_ROL_RM8_1_NF, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0xd0, 0xc0 } },
    { { .form = X86_FORM_RCR_R32_RM32_CL, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xd3, 0xd9 } },
    { { .form = X86_FORM_INC_R32_RM32, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xff, 0xc1 } },
    { { .form = X86_FORM_INC_RM32_NF, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0xff, 0xc0 } },
    { { .form = X86_FORM_INC_RM8_EVEX, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xfe, 0xc0 } },
    { { .form = X86_FORM_NOT_R32_RM32, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xf7, 0xd1 } },
    { { .form = X86_FORM_MUL_RM32_NF, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0xf7, 0xe1 } },
    { { .form = X86_FORM_IDIV_RM64_EVEX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0xfc, 0x08, 0xf7, 0xf9 } },
    { { .form = X86_FORM_MUL_RM8_EVEX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xf6, 0x20 } },
    { { .form = X86_FORM_IMUL_R32_RM32_EVEX, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0xaf, 0xc1 } },
    { { .form = X86_FORM_IMUL_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xaf, 0xca } },
    { { .form = X86_FORM_IMUL_R32_RM32_IMM8_NF, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7c, 0x0c, 0x6b, 0xc1, 0x05 } },
    { { .form = X86_FORM_IMULZU_R16_RM16_IMM16, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 500 }, 8, { 0x62, 0xf4, 0x7d, 0x18, 0x69, 0xc1, 0xf4, 0x01 } },
    { { .form = X86_FORM_IMULZU_R16_RM16_IMM8_NF, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x7d, 0x1c, 0x6b, 0xc1, 0x05 } },
    { { .form = X86_FORM_SHLD_RM32_R32_IMM8_EVEX, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .immediate = 3 }, 7, { 0x62, 0xf4, 0x7c, 0x08, 0x24, 0xc8, 0x03 } },
    { { .form = X86_FORM_SHLD_R32_RM32_R32_CL, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xa5, 0xd1 } },
    { { .form = X86_FORM_SHRD_R64_RM64_R64_IMM8, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX, .immediate = 1 }, 7, { 0x62, 0xf4, 0xfc, 0x18, 0x2c, 0xd1, 0x01 } },
    { { .form = X86_FORM_CMOVB_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0x42, 0x0a } },
    { { .form = X86_FORM_CMOVP_R16_R16_RM16, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7d, 0x18, 0x4a, 0xca } },
    { { .form = X86_FORM_SETB_RM8_EVEX, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7f, 0x08, 0x42, 0xc0 } },
    { { .form = X86_FORM_SETNP_RM8_EVEX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7f, 0x08, 0x4b, 0x00 } },
    { { .form = X86_FORM_SETZUA_R8, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7f, 0x18, 0x47, 0xc0 } },
    // APX's new instructions, here with registers 0 to 15: ccmpb {dfv=of, sf} eax, ecx (the default flags value in
    // vvvv as it is, and the source condition in P2's bits 3-0) and {load} ccmpb eax, ecx; ccmpe {dfv=cf} rax, 5; ccmpt
    // {dfv=zf} byte ptr [rax], 5 (t: 10); ccmpf {dfv=of, sf, zf, cf} word ptr [rax], cx (f: 11); ccmpo ax, word ptr
    // [rcx]; ccmpb {dfv=sf} word ptr [rax], 0x1234; the longest, ccmpb {dfv=of, cf} dword ptr [rax+rbx*4+0x12345678],
    // 0x12345678; ctestb {dfv=of} eax, ecx; ctestae {dfv=cf} rax, 0x12345678; ctestb {dfv=sf} byte ptr [rax], 0x12;
    // cfcmovb eax, ecx, {store} cfcmovb eax, ecx (NF), cfcmovb dword ptr [rax], ecx and cfcmovb eax, ecx, edx (ND and
    // NF); cfcmovp eax, ecx; cfcmovb ax, word ptr [rax]; cfcmovb rax, rcx, qword ptr [rdx]; push2 rax, rbx (ND), push2p
    // rax, rbx (W1), push2 rax, rax and pop2 rax, rbx
    { { .form = X86_FORM_CCMPB_RM32_R32, .vvvv = 8 | 4, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x64, 0x02, 0x39, 0xc8 } },
    { { .form = X86_FORM_CCMPB_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x04, 0x02, 0x3b, 0xc1 } },
    { { .form = X86_FORM_CCMPE_RM64_IMM8, .vvvv = 1, .rm = X86_REGISTER_RAX, .immediate = 5 }, 7, { 0x62, 0xf4, 0x8c, 0x04, 0x83, 0xf8, 0x05 } },
    { { .form = X86_FORM_CCMPT_RM8_IMM8, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 7, { 0x62, 0xf4, 0x14, 0x0a, 0x80, 0x38, 0x05 } },
    { { .form = X86_FORM_CCMPF_RM16_R16, .vvvv = 15, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7d, 0x0b, 0x39, 0x08 } },
    { { .form = X86_FORM_CCMPO_R16_RM16, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x05, 0x00, 0x3b, 0x01 } },
    { { .form = X86_FORM_CCMPB_RM16_IMM16, .vvvv = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x1234 }, 8, { 0x62, 0xf4, 0x25, 0x02, 0x81, 0x38, 0x34, 0x12 } },
    { { .form = X86_FORM_CCMPB_RM32_IMM32, .vvvv = 8 | 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12345678, .immediate = 0x12345678 }, 15, { 0x62, 0xf4, 0x4c, 0x02, 0x81, 0xbc, 0x98, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_CTESTB_RM32_R32, .vvvv = 8, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x44, 0x02, 0x85, 0xc8 } },
    { { .form = X86_FORM_CTESTAE_RM64_IMM32, .vvvv = 1, .rm = X86_REGISTER_RAX, .immediate = 0x12345678 }, 10, { 0x62, 0xf4, 0x8c, 0x03, 0xf7, 0xc0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_CTESTB_RM8_IMM8, .vvvv = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x12 }, 7, { 0x62, 0xf4, 0x24, 0x02, 0xf6, 0x00, 0x12 } },
    { { .form = X86_FORM_CFCMOVB_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x42, 0xc1 } },
    { { .form = X86_FORM_CFCMOVB_RM32_R32, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0x42, 0xc8 } },
    { { .form = X86_FORM_CFCMOVB_RM32_R32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .reg = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x0c, 0x42, 0x08 } },
    { { .form = X86_FORM_CFCMOVB_R32_R32_RM32, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX }, 6, { 0x62, 0xf4, 0x7c, 0x1c, 0x42, 0xca } },
    { { .form = X86_FORM_CFCMOVP_R32_RM32, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf4, 0x7c, 0x08, 0x4a, 0xc1 } },
    { { .form = X86_FORM_CFCMOVB_R16_RM16, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0x7d, 0x08, 0x42, 0x00 } },
    { { .form = X86_FORM_CFCMOVB_R64_R64_RM64, .vvvv = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf4, 0xfc, 0x1c, 0x42, 0x0a } },
    { { .form = X86_FORM_PUSH2_R64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xff, 0xf3 } },
    { { .form = X86_FORM_PUSH2P_R64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX }, 6, { 0x62, 0xf4, 0xfc, 0x18, 0xff, 0xf3 } },
    { { .form = X86_FORM_PUSH2_R64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0xff, 0xf0 } },
    { { .form = X86_FORM_POP2_R64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RBX }, 6, { 0x62, 0xf4, 0x7c, 0x18, 0x8f, 0xc3 } },
    // And those that always have REX2, whose payload is REX's place here: pushp rax and pushp r8 (W, the hint, and B3);
    // popp r15; and jmpabs 0x123456789abcdef0 (W0)
    { { .form = X86_FORM_PUSHP_R64, .rm = X86_REGISTER_RAX }, 3, { 0xd5, 0x08, 0x50 } },
    { { .form = X86_FORM_PUSHP_R64, .rm = X86_REGISTER_R8 }, 3, { 0xd5, 0x09, 0x50 } },
    { { .form = X86_FORM_POPP_R64, .rm = X86_REGISTER_R15 }, 3, { 0xd5, 0x09, 0x5f } },
    { { .form = X86_FORM_JMPABS_IMM64, .immediate = (int32_t)0x9abcdef0, .displacement = 0x12345678 }, 11, { 0xd5, 0x00, 0xa1, 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
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
    // ffreep st(3); fneni, fndisi, fnsetpm and frstpm; the waiting forms (9B first): finit, fclex, feni, fdisi, fsetpm,
    // fstsw ax, fstcw word ptr [rax], fstsw word ptr fs:[rax], fstenv gs:[r8+r9*2+0x100], fstenvw [rax], fsave
    // fs:[rax] and fsavew [rax] (9B before 66)
    { { .form = X86_FORM_FFREEP_ST, .rm = 3 }, 2, { 0xdf, 0xc3 } },
    { { .form = X86_FORM_FNENI }, 2, { 0xdb, 0xe0 } },
    { { .form = X86_FORM_FNDISI }, 2, { 0xdb, 0xe1 } },
    { { .form = X86_FORM_FNSETPM }, 2, { 0xdb, 0xe4 } },
    { { .form = X86_FORM_FRSTPM }, 2, { 0xdb, 0xe5 } },
    { { .form = X86_FORM_FINIT }, 3, { 0x9b, 0xdb, 0xe3 } },
    { { .form = X86_FORM_FCLEX }, 3, { 0x9b, 0xdb, 0xe2 } },
    { { .form = X86_FORM_FENI }, 3, { 0x9b, 0xdb, 0xe0 } },
    { { .form = X86_FORM_FDISI }, 3, { 0x9b, 0xdb, 0xe1 } },
    { { .form = X86_FORM_FSETPM }, 3, { 0x9b, 0xdb, 0xe4 } },
    { { .form = X86_FORM_FSTSW_AX }, 3, { 0x9b, 0xdf, 0xe0 } },
    { { .form = X86_FORM_FSTCW_M16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x9b, 0xd9, 0x38 } },
    { { .form = X86_FORM_FSTSW_M16 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x64, 0xdd, 0x38 } },
    { { .form = X86_FORM_FSTENV_M28 + BUSTER_X86_FORM_GS, .rm = X86_REGISTER_R8, .index = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1), .displacement = 0x100 },
      10,
      { 0x9b, 0x65, 0x43, 0xd9, 0xb4, 0x48, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_FSTENVW_M14, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x66, 0xd9, 0x30 } },
    { { .form = X86_FORM_FSAVE_M108 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x64, 0xdd, 0x30 } },
    { { .form = X86_FORM_FSAVEW_M94, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x66, 0xdd, 0x30 } },
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
    { { .form = X86_FORM_PUSHW_IMM8, .immediate = -5 }, 3, { 0x66, 0x6a, 0xfb } },
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
    // xbegin .+0x100 and data16 xbegin .+20 (a rel16); prefetchit0 byte ptr [rip+0x10]; movdir64b rax, [rcx]; rdpid rax
    { { .form = X86_FORM_XBEGIN_REL32, .displacement = 0xfa }, 6, { 0xc7, 0xf8, 0xfa, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_XBEGIN_REL16, .displacement = 15 }, 5, { 0x66, 0xc7, 0xf8, 0x0f, 0x00 } },
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
    // AVX512BW: vpaddusb xmm17{k1}{z}, xmm18, xmm19; vpmulhrsw zmm1, zmm2, zmmword ptr [rax+0x40] (N 64); vpcmpgtw
    // k1{k2}, xmm2, xmmword ptr [rax+0x40] (N 16); vpcmpuw k1, ymm2, ymm3, 5; vdbpsadbw xmm1, xmm2, xmm3, 5; vpshufhw
    // xmm1{k1}, xmm2, 5; vpabsw ymm1, ymm2; vpsraw zmm1{k1}, zmmword ptr [rax+0x40], 3 (into vvvv); vpslldq xmm1, xmm2,
    // 3 (without an opmask); vpsllw ymm1, ymm2, xmm3; vpmovwb qword ptr [rax+0x40], xmm1 (N 8); vpmovm2w xmm1, k1 and
    // vpmovw2m k1, ymm1; vpbroadcastw zmm1, eax; vpextrb byte ptr [rax+0x40], xmm17, 3 (N 1); vpextrw eax, xmm1, 3 (0F
    // C5); and vpinsrw xmm1, xmm2, word ptr [rax+0x40], 3 (N 2)
    { { .form = X86_FORM_VPADDUSB_X_X_XM128_EVEX, .reg = 17, .vvvv = 18, .rm = 19, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xa1, 0x6d, 0x81, 0xdc, 0xcb } },
    { { .form = X86_FORM_VPMULHRSW_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x6d, 0x48, 0x0b, 0x48, 0x01 } },
    { { .form = X86_FORM_VPCMPGTW_K_X_XM128, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 2 },
      7,
      { 0x62, 0xf1, 0x6d, 0x0a, 0x65, 0x48, 0x04 } },
    { { .form = X86_FORM_VPCMPUW_K_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf3, 0xed, 0x28, 0x3e, 0xcb, 0x05 } },
    { { .form = X86_FORM_VDBPSADBW_X_X_XM128_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf3, 0x6d, 0x08, 0x42, 0xcb, 0x05 } },
    { { .form = X86_FORM_VPSHUFHW_X_XM128_IMM8_EVEX, .reg = 1, .rm = 2, .immediate = 5, .decorations = 1 }, 7, { 0x62, 0xf1, 0x7e, 0x09, 0x70, 0xca, 0x05 } },
    { { .form = X86_FORM_VPABSW_Y_YM256_EVEX, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf2, 0x7d, 0x28, 0x1d, 0xca } },
    { { .form = X86_FORM_VPSRAW_Z_ZM512_IMM8, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3, .decorations = 1 },
      8,
      { 0x62, 0xf1, 0x75, 0x49, 0x71, 0x60, 0x01, 0x03 } },
    { { .form = X86_FORM_VPSLLDQ_X_XM128_IMM8, .vvvv = 1, .rm = 2, .immediate = 3 }, 7, { 0x62, 0xf1, 0x75, 0x08, 0x73, 0xfa, 0x03 } },
    { { .form = X86_FORM_VPSLLW_Y_Y_XM128_EVEX, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf1, 0x6d, 0x28, 0xf1, 0xcb } },
    { { .form = X86_FORM_VPMOVWB_XM64_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7e, 0x08, 0x30, 0x48, 0x08 } },
    { { .form = X86_FORM_VPMOVM2W_X_K, .reg = 1, .rm = 1 }, 6, { 0x62, 0xf2, 0xfe, 0x08, 0x28, 0xc9 } },
    { { .form = X86_FORM_VPMOVW2M_K_Y, .reg = 1, .rm = 1 }, 6, { 0x62, 0xf2, 0xfe, 0x28, 0x29, 0xc9 } },
    { { .form = X86_FORM_VPBROADCASTW_Z_R32, .reg = 1, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf2, 0x7d, 0x48, 0x7b, 0xc8 } },
    { { .form = X86_FORM_VPEXTRB_RM32_X_IMM8_EVEX, .reg = 17, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3 },
      8,
      { 0x62, 0xe3, 0x7d, 0x08, 0x14, 0x48, 0x40, 0x03 } },
    { { .form = X86_FORM_VPEXTRW_R32_X_IMM8_EVEX, .reg = X86_REGISTER_RAX, .rm = 1, .immediate = 3 }, 7, { 0x62, 0xf1, 0x7d, 0x08, 0xc5, 0xc1, 0x03 } },
    { { .form = X86_FORM_VPINSRW_X_X_RM32_IMM8_EVEX, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 3 },
      8,
      { 0x62, 0xf1, 0x6d, 0x08, 0xc4, 0x48, 0x20, 0x03 } },
    // AVX512DQ and CD: vandpd xmm1, xmm2, qword ptr [rax]{1to2}; vfpclassps k1, dword ptr [rax]{1to4}, 5 and vfpclasssd
    // k1, qword ptr [rax+0x40], 5; vbroadcastf32x2 ymm1, qword ptr [rax+0x40] (N 8) and vbroadcasti32x8 zmm1, ymmword
    // ptr [rax+0x40] (N 32); vcvtqq2ps xmm1, qword ptr [rax]{1to4}; vcvtps2qq zmm1, ymm2, {rn-sae}; vreduceps zmm1,
    // zmm2, {sae}, 3; vextractf64x2 xmm1, ymm2, 1; vinsertf32x8 zmm1, zmm2, ymmword ptr [rax+0x40], 1 (N 32);
    // vpconflictq ymm1, qword ptr [rax]{1to4}; and vpbroadcastmb2q xmm1, k1
    { { .form = X86_FORM_VANDPD_X_X_M64BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf1, 0xed, 0x18, 0x54, 0x08 } },
    { { .form = X86_FORM_VFPCLASSPS_K_M32BCST4_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 7, { 0x62, 0xf3, 0x7d, 0x18, 0x66, 0x08, 0x05 } },
    { { .form = X86_FORM_VFPCLASSSD_K_XM64_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 5 },
      8,
      { 0x62, 0xf3, 0xfd, 0x08, 0x67, 0x48, 0x08, 0x05 } },
    { { .form = X86_FORM_VBROADCASTF32X2_Y_XM64, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x28, 0x19, 0x48, 0x08 } },
    { { .form = X86_FORM_VBROADCASTI32X8_Z_M256, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x7d, 0x48, 0x5b, 0x48, 0x02 } },
    { { .form = X86_FORM_VCVTQQ2PS_X_M64BCST4, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf1, 0xfc, 0x38, 0x5b, 0x08 } },
    { { .form = X86_FORM_VCVTPS2QQ_Z_Y_ER, .reg = 1, .rm = 2, .decorations = BUSTER_X86_ROUND_NEAREST }, 6, { 0x62, 0xf1, 0x7d, 0x18, 0x7b, 0xca } },
    { { .form = X86_FORM_VREDUCEPS_Z_Z_SAE_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 7, { 0x62, 0xf3, 0x7d, 0x18, 0x56, 0xca, 0x03 } },
    { { .form = X86_FORM_VEXTRACTF64X2_XM128_Y_IMM8, .rm = 1, .reg = 2, .immediate = 1 }, 7, { 0x62, 0xf3, 0xfd, 0x28, 0x19, 0xd1, 0x01 } },
    { { .form = X86_FORM_VINSERTF32X8_Z_Z_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .immediate = 1 },
      8,
      { 0x62, 0xf3, 0x6d, 0x48, 0x1a, 0x48, 0x02, 0x01 } },
    { { .form = X86_FORM_VPCONFLICTQ_Y_M64BCST, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0xfd, 0x38, 0xc4, 0x08 } },
    { { .form = X86_FORM_VPBROADCASTMB2Q_X_K, .reg = 1, .rm = 1 }, 6, { 0x62, 0xf2, 0xfe, 0x08, 0x2a, 0xc9 } },
    // Zen 5's other AVX-512 extensions: vp2intersectd k2, xmm1, xmm2, vp2intersectq k6, zmm1, qword ptr [rax]{1to8} and
    // vp2intersectd k0, ymm17, ymmword ptr [rax+0x40] (an opmask pair); vpshldd xmm1{k1}, xmm2, dword ptr [rax]{1to4},
    // 5; vpshrdvw zmm1, zmm2, zmmword ptr [rax+0x40]; vpcompressw xmmword ptr [rax+0x40]{k1}, xmm1 (N 2); vpexpandb
    // xmm1{k1}{z}, xmm2; vpopcntw ymm1, ymm2; vpshufbitqmb k1{k2}, ymm2, ymm3; vpermi2b xmm1, xmm2, xmm3; vpdpwssds ymm1,
    // ymm2, dword ptr [rax]{1to8}; vpmadd52huq xmm1, xmm2, xmmword ptr [rax+0x40]; vcvtneps2bf16 xmm1, dword ptr
    // [rax]{1to8}; vdpbf16ps zmm1{k1}, zmm2, zmm3; vgf2p8affineqb ymm1, ymm2, qword ptr [rax]{1to4}, 5; vgf2p8mulb xmm1,
    // xmm2, xmm3; vaesenc zmm1, zmm2, zmmword ptr [rax+0x40]; vaesdeclast xmm17, xmm18, xmm19; and vpclmulqdq ymm17,
    // ymm18, ymm19, 0x11
    { { .form = X86_FORM_VP2INTERSECTD_K_X_XM128, .reg = 2, .vvvv = 1, .rm = 2 }, 6, { 0x62, 0xf2, 0x77, 0x08, 0x68, 0xd2 } },
    { { .form = X86_FORM_VP2INTERSECTQ_K_Z_M64BCST, .reg = 6, .vvvv = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0xf7, 0x58, 0x68, 0x30 } },
    { { .form = X86_FORM_VP2INTERSECTD_K_Y_YM256, .reg = 0, .vvvv = 17, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 },
      7,
      { 0x62, 0xf2, 0x77, 0x20, 0x68, 0x40, 0x02 } },
    { { .form = X86_FORM_VPSHLDD_X_X_M32BCST_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5, .decorations = 1 },
      7,
      { 0x62, 0xf3, 0x6d, 0x19, 0x71, 0x08, 0x05 } },
    { { .form = X86_FORM_VPSHRDVW_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0xed, 0x48, 0x72, 0x48, 0x01 } },
    { { .form = X86_FORM_VPCOMPRESSW_XM128_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40, .decorations = 1 },
      7,
      { 0x62, 0xf2, 0xfd, 0x09, 0x63, 0x48, 0x20 } },
    { { .form = X86_FORM_VPEXPANDB_X_XM128, .reg = 1, .rm = 2, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf2, 0x7d, 0x89, 0x62, 0xca } },
    { { .form = X86_FORM_VPOPCNTW_Y_YM256, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf2, 0xfd, 0x28, 0x54, 0xca } },
    { { .form = X86_FORM_VPSHUFBITQMB_K_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 2 }, 6, { 0x62, 0xf2, 0x6d, 0x2a, 0x8f, 0xcb } },
    // AVX512_BMM (map 6): vbitrevb zmm1{k1}{z}, zmm2; vbitrevb zmm1, [rax+0x40] and ymm17{k7}, [r9+0x20] (N = 64 and
    // 32); vbmacxor16x16x16 zmm1, zmm2, zmm3 (W1); vbmacor16x16x16 ymm1, ymm2, [rax+0x20]
    { { .form = X86_FORM_VBITREVB_Z_ZM512, .reg = 1, .rm = 2, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf6, 0x7c, 0xc9, 0x81, 0xca } },
    { { .form = X86_FORM_VBITREVB_Z_ZM512, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf6, 0x7c, 0x48, 0x81, 0x48, 0x01 } },
    { { .form = X86_FORM_VBITREVB_Y_YM256, .reg = 17, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x20, .decorations = 7 }, 7, { 0x62, 0xc6, 0x7c, 0x2f, 0x81, 0x49, 0x01 } },
    { { .form = X86_FORM_VBMACXOR16X16X16_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf6, 0xec, 0x48, 0x80, 0xcb } },
    { { .form = X86_FORM_VBMACOR16X16X16_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x20 }, 7, { 0x62, 0xf6, 0x6c, 0x28, 0x80, 0x48, 0x01 } },
    { { .form = X86_FORM_VPERMI2B_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6d, 0x08, 0x75, 0xcb } },
    { { .form = X86_FORM_VPDPWSSDS_Y_Y_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x6d, 0x38, 0x53, 0x08 } },
    { { .form = X86_FORM_VPMADD52HUQ_X_X_XM128_EVEX, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 },
      7,
      { 0x62, 0xf2, 0xed, 0x08, 0xb5, 0x48, 0x04 } },
    { { .form = X86_FORM_VCVTNEPS2BF16_X_M32BCST8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x7e, 0x38, 0x72, 0x08 } },
    { { .form = X86_FORM_VDPBF16PS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 }, 6, { 0x62, 0xf2, 0x6e, 0x49, 0x52, 0xcb } },
    { { .form = X86_FORM_VGF2P8AFFINEQB_Y_Y_M64BCST_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 },
      7,
      { 0x62, 0xf3, 0xed, 0x38, 0xce, 0x08, 0x05 } },
    { { .form = X86_FORM_VGF2P8MULB_X_X_XM128_EVEX, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6d, 0x08, 0xcf, 0xcb } },
    { { .form = X86_FORM_VAESENC_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0x6d, 0x48, 0xdc, 0x48, 0x01 } },
    { { .form = X86_FORM_VAESDECLAST_X_X_XM128_EVEX, .reg = 17, .vvvv = 18, .rm = 19 }, 6, { 0x62, 0xa2, 0x6d, 0x00, 0xdf, 0xcb } },
    { { .form = X86_FORM_VPCLMULQDQ_Y_Y_YM256_IMM8_EVEX, .reg = 17, .vvvv = 18, .rm = 19, .immediate = 0x11 }, 7, { 0x62, 0xa3, 0x6d, 0x20, 0x44, 0xcb, 0x11 } },
    // The opmask instructions at each width: kandb k1, k2, k3 (VEX2 with 66); kaddd k1, k2, k3 (W1, VEX3); knotb k1, k2;
    // kortestd k1, k2; ktestw k1, k2; kshiftrb k1, k2, 3 and kshiftld k1, k2, 3 (the next opcode, W0); and kxnorq k1, k2,
    // k3
    { { .form = X86_FORM_KANDB_K_K_K, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xed, 0x41, 0xcb } },
    { { .form = X86_FORM_KADDD_K_K_K, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe1, 0xed, 0x4a, 0xcb } },
    { { .form = X86_FORM_KNOTB_K_K, .reg = 1, .rm = 2 }, 4, { 0xc5, 0xf9, 0x44, 0xca } },
    { { .form = X86_FORM_KORTESTD_K_K, .reg = 1, .rm = 2 }, 5, { 0xc4, 0xe1, 0xf9, 0x98, 0xca } },
    { { .form = X86_FORM_KTESTW_K_K, .reg = 1, .rm = 2 }, 4, { 0xc5, 0xf8, 0x99, 0xca } },
    { { .form = X86_FORM_KSHIFTRB_K_K_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 6, { 0xc4, 0xe3, 0x79, 0x30, 0xca, 0x03 } },
    { { .form = X86_FORM_KSHIFTLD_K_K_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 6, { 0xc4, 0xe3, 0x79, 0x33, 0xca, 0x03 } },
    { { .form = X86_FORM_KXNORQ_K_K_K, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe1, 0xec, 0x46, 0xcb } },
    // AES, carry-less multiplication, SHA and GFNI: aesenc xmm9, xmmword ptr [rax] (66, REX.R, 0F 38); aeskeygenassist
    // xmm1, xmm2, 3; pclmulqdq xmm1, xmm2, 0x11; sha256rnds2 xmm1, xmm2, xmm0 (no prefix, and XMM0 implicit); sha1rnds4
    // xmm1, xmmword ptr [rax], 3; and gf2p8affineinvqb xmm1, xmm2, 5
    { { .form = X86_FORM_AESENC_X_XM128, .reg = 9, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0x44, 0x0f, 0x38, 0xdc, 0x08 } },
    { { .form = X86_FORM_AESKEYGENASSIST_X_XM128_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 6, { 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x03 } },
    { { .form = X86_FORM_PCLMULQDQ_X_XM128_IMM8, .reg = 1, .rm = 2, .immediate = 0x11 }, 6, { 0x66, 0x0f, 0x3a, 0x44, 0xca, 0x11 } },
    { { .form = X86_FORM_SHA256RNDS2_X_XM128_XMM0, .reg = 1, .rm = 2 }, 4, { 0x0f, 0x38, 0xcb, 0xca } },
    { { .form = X86_FORM_SHA1RNDS4_X_XM128_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 5, { 0x0f, 0x3a, 0xcc, 0x08, 0x03 } },
    { { .form = X86_FORM_GF2P8AFFINEINVQB_X_XM128_IMM8, .reg = 1, .rm = 2, .immediate = 5 }, 6, { 0x66, 0x0f, 0x3a, 0xcf, 0xca, 0x05 } },
    // Key Locker and PadLock: aesdec128kl xmm1, [rax] (F3); aesdec256kl xmm1, fs:[rax] (the override before F3);
    // aesencwide256kl [r9] (/2, REX.B); encodekey128 r8d, ecx; loadiwkey xmm1, xmm2 (aesenc128kl's opcode, with
    // registers); xstore (0F A7 C0, the ModRM byte part of the opcode); rep xcryptcbc; and rep xsha256
    { { .form = X86_FORM_AESDEC128KL_X_M384, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf3, 0x0f, 0x38, 0xdd, 0x08 } },
    { { .form = X86_FORM_AESDEC256KL_X_M512 + BUSTER_X86_FORM_FS, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      6,
      { 0x64, 0xf3, 0x0f, 0x38, 0xdf, 0x08 } },
    { { .form = X86_FORM_AESENCWIDE256KL_M512, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0xf3, 0x41, 0x0f, 0x38, 0xd8, 0x11 } },
    { { .form = X86_FORM_ENCODEKEY128_R32_R32, .reg = X86_REGISTER_R8, .rm = X86_REGISTER_RCX }, 6, { 0xf3, 0x44, 0x0f, 0x38, 0xfa, 0xc1 } },
    { { .form = X86_FORM_LOADIWKEY_X_X, .reg = 1, .rm = 2 }, 5, { 0xf3, 0x0f, 0x38, 0xdc, 0xca } },
    { { .form = X86_FORM_XSTORE }, 3, { 0x0f, 0xa7, 0xc0 } },
    { { .form = X86_FORM_REP_XCRYPTCBC }, 4, { 0xf3, 0x0f, 0xa7, 0xd0 } },
    { { .form = X86_FORM_REP_XSHA256 }, 4, { 0xf3, 0x0f, 0xa6, 0xd0 } },
    // Zhaoxin's: xrng2, xsha384, xsha512, montmul2, xmodexp, sm2 (F2), sm3 and sm4
    { { .form = X86_FORM_XRNG2 }, 4, { 0xf3, 0x0f, 0xa7, 0xf8 } },
    { { .form = X86_FORM_XSHA384 }, 4, { 0xf3, 0x0f, 0xa6, 0xd8 } },
    { { .form = X86_FORM_XSHA512 }, 4, { 0xf3, 0x0f, 0xa6, 0xe0 } },
    { { .form = X86_FORM_MONTMUL2 }, 4, { 0xf3, 0x0f, 0xa6, 0xf0 } },
    { { .form = X86_FORM_XMODEXP }, 4, { 0xf3, 0x0f, 0xa6, 0xf8 } },
    { { .form = X86_FORM_SM2 }, 4, { 0xf2, 0x0f, 0xa6, 0xc0 } },
    { { .form = X86_FORM_SM3 }, 4, { 0xf3, 0x0f, 0xa6, 0xe8 } },
    { { .form = X86_FORM_SM4 }, 4, { 0xf3, 0x0f, 0xa7, 0xf0 } },
    // VEX: vpdpbusd xmm1, xmm2, xmm3 (AVX-VNNI's, which GNU as takes with {vex}); vpmadd52luq ymm1, ymm2, ymmword ptr
    // [rax] (W1); vcvtneps2bf16 xmm1, ymmword ptr [rax] (L1 into an XMM register); vaesenc ymm9, ymm10, ymm11 (VAES,
    // with inverted R, vvvv and B); vaesimc xmm1, xmm2; vaeskeygenassist xmm1, xmmword ptr [rax], 3; vpclmulqdq ymm1,
    // ymm2, ymm3, 0x11; vgf2p8affineqb ymm1, ymm2, ymm3, 5 (W1); and vgf2p8mulb xmm1, xmm2, xmm3
    { { .form = X86_FORM_VPDPBUSD_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x69, 0x50, 0xcb } },
    { { .form = X86_FORM_VPMADD52LUQ_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0xed, 0xb4, 0x08 } },
    { { .form = X86_FORM_VCVTNEPS2BF16_X_YM256, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x7e, 0x72, 0x08 } },
    { { .form = X86_FORM_VAESENC_Y_Y_YM256, .reg = 9, .vvvv = 10, .rm = 11 }, 5, { 0xc4, 0x42, 0x2d, 0xdc, 0xcb } },
    { { .form = X86_FORM_VAESIMC_X_XM128, .reg = 1, .rm = 2 }, 5, { 0xc4, 0xe2, 0x79, 0xdb, 0xca } },
    { { .form = X86_FORM_VAESKEYGENASSIST_X_XM128_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 },
      6,
      { 0xc4, 0xe3, 0x79, 0xdf, 0x08, 0x03 } },
    { { .form = X86_FORM_VPCLMULQDQ_Y_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 0x11 }, 6, { 0xc4, 0xe3, 0x6d, 0x44, 0xcb, 0x11 } },
    { { .form = X86_FORM_VGF2P8AFFINEQB_Y_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 6, { 0xc4, 0xe3, 0xed, 0xce, 0xcb, 0x05 } },
    { { .form = X86_FORM_VGF2P8MULB_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x69, 0xcf, 0xcb } },
    // AVX-NE-CONVERT, AVX-VNNI-INT8 and INT16, SHA512, SM3 and SM4: vbcstnesh2ps ymm1, word ptr [rax] (66);
    // vcvtneoph2ps xmm1, xmmword ptr [rax] (no prefix); vpdpbsud ymm1, ymm2, ymm3 (F3); vpdpwusds xmm1, xmm2, xmm3 (66);
    // vsha512msg2 ymm1, ymm2 (F2); vsha512rnds2 ymm1, ymm2, xmm3; vsm3msg1 xmm1, xmm2, xmmword ptr [rax]; vsm3rnds2 xmm1,
    // xmm2, xmm3, 5 (0F 3A); and vsm4rnds4 ymm1, ymm2, ymm3
    { { .form = X86_FORM_VBCSTNESH2PS_Y_M16, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x7d, 0xb1, 0x08 } },
    { { .form = X86_FORM_VCVTNEOPH2PS_X_M128, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x78, 0xb0, 0x08 } },
    { { .form = X86_FORM_VPDPBSUD_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x6e, 0x50, 0xcb } },
    { { .form = X86_FORM_VPDPWUSDS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x69, 0xd3, 0xcb } },
    { { .form = X86_FORM_VSHA512MSG2_Y_Y, .reg = 1, .rm = 2 }, 5, { 0xc4, 0xe2, 0x7f, 0xcd, 0xca } },
    { { .form = X86_FORM_VSHA512RNDS2_Y_Y_X, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x6f, 0xcb, 0xcb } },
    { { .form = X86_FORM_VSM3MSG1_X_X_XM128, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x68, 0xda, 0x08 } },
    { { .form = X86_FORM_VSM3RNDS2_X_X_XM128_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 6, { 0xc4, 0xe3, 0x69, 0xde, 0xcb, 0x05 } },
    { { .form = X86_FORM_VSM4RNDS4_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0xc4, 0xe2, 0x6f, 0xda, 0xcb } },
    // MMX: emms; movd mm1, eax and movd r9d, mm1 (REX.B for the general register); movq mm0, rax (REX.W); movq mm1, mm2
    // (0F 6F) and {store} movq mm1, mm2 (0F 7F, ModRM.rm the destination); movq qword ptr [rax], mm1; paddb mm1, qword
    // ptr [r9] (REX.B for the base); paddq mm1, qword ptr fs:[rax]; psllw mm1, 3 (/6); psrad mm7, mm0; punpcklbw mm1,
    // dword ptr [rax] (32 bits); pshufw mm1, mm2, 3; pinsrw mm1, word ptr [rax], 3; pextrw r9d, mm1, 3 (REX.R);
    // pmovmskb eax, mm1; maskmovq mm1, mm2; and movntq qword ptr [rax], mm1
    { { .form = X86_FORM_EMMS }, 2, { 0x0f, 0x77 } },
    { { .form = X86_FORM_MOVD_MM_RM32, .reg = 1, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x6e, 0xc8 } },
    { { .form = X86_FORM_MOVD_RM32_MM, .reg = 1, .rm = X86_REGISTER_R9 }, 4, { 0x41, 0x0f, 0x7e, 0xc9 } },
    { { .form = X86_FORM_MOVQ_MM_RM64, .reg = 0, .rm = X86_REGISTER_RAX }, 4, { 0x48, 0x0f, 0x6e, 0xc0 } },
    { { .form = X86_FORM_MOVQ_MM_MM, .reg = 1, .rm = 2 }, 3, { 0x0f, 0x6f, 0xca } },
    { { .form = X86_FORM_MOVQ_MM_MM_STORE, .reg = 2, .rm = 1 }, 3, { 0x0f, 0x7f, 0xd1 } },
    { { .form = X86_FORM_MOVQ_M64_MM, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x7f, 0x08 } },
    { { .form = X86_FORM_PADDB_MM_M64, .reg = 1, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x41, 0x0f, 0xfc, 0x09 } },
    { { .form = X86_FORM_PADDQ_MM_M64 + BUSTER_X86_FORM_FS, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x64, 0x0f, 0xd4, 0x08 } },
    { { .form = X86_FORM_PSLLW_MM_IMM8, .rm = 1, .immediate = 3 }, 4, { 0x0f, 0x71, 0xf1, 0x03 } },
    { { .form = X86_FORM_PSRAD_MM_MM, .reg = 7, .rm = 0 }, 3, { 0x0f, 0xe2, 0xf8 } },
    { { .form = X86_FORM_PUNPCKLBW_MM_M32, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x60, 0x08 } },
    { { .form = X86_FORM_PSHUFW_MM_MM_IMM8, .reg = 1, .rm = 2, .immediate = 3 }, 4, { 0x0f, 0x70, 0xca, 0x03 } },
    { { .form = X86_FORM_PINSRW_MM_RM32_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 4, { 0x0f, 0xc4, 0x08, 0x03 } },
    { { .form = X86_FORM_PEXTRW_R32_MM_IMM8, .reg = X86_REGISTER_R9, .rm = 1, .immediate = 3 }, 5, { 0x44, 0x0f, 0xc5, 0xc9, 0x03 } },
    { { .form = X86_FORM_PMOVMSKB_R32_MM, .reg = X86_REGISTER_RAX, .rm = 1 }, 3, { 0x0f, 0xd7, 0xc1 } },
    { { .form = X86_FORM_MASKMOVQ_MM_MM, .reg = 1, .rm = 2 }, 3, { 0x0f, 0xf7, 0xca } },
    { { .form = X86_FORM_MOVNTQ_M64_MM, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0xe7, 0x08 } },
    // MMX's SSSE3 forms and conversions: pshufb mm1, mm2 (0F 38); palignr mm1, qword ptr [rax], 3 (0F 3A); cvtpi2ps
    // xmm9, mm2 (REX.R for the XMM register); cvttps2pi mm1, xmm9 (REX.B); cvtpd2pi mm1, xmmword ptr [rax] (66);
    // movdq2q mm1, xmm9 (F2, REX.B); and movq2dq xmm9, mm2 (F3, REX.R)
    { { .form = X86_FORM_PSHUFB_MM_MM, .reg = 1, .rm = 2 }, 4, { 0x0f, 0x38, 0x00, 0xca } },
    { { .form = X86_FORM_PALIGNR_MM_M64_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 5, { 0x0f, 0x3a, 0x0f, 0x08, 0x03 } },
    { { .form = X86_FORM_CVTPI2PS_X_MM, .reg = 9, .rm = 2 }, 4, { 0x44, 0x0f, 0x2a, 0xca } },
    { { .form = X86_FORM_CVTTPS2PI_MM_XM64, .reg = 1, .rm = 9 }, 4, { 0x41, 0x0f, 0x2c, 0xc9 } },
    { { .form = X86_FORM_CVTPD2PI_MM_XM128, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x2d, 0x08 } },
    { { .form = X86_FORM_MOVDQ2Q_MM_X, .reg = 1, .rm = 9 }, 5, { 0xf2, 0x41, 0x0f, 0xd6, 0xc9 } },
    { { .form = X86_FORM_MOVQ2DQ_X_MM, .reg = 9, .rm = 2 }, 5, { 0xf3, 0x44, 0x0f, 0xd6, 0xca } },
    // A register in the immediate (/is4, its number times 16): vblendvps xmm1, xmm2, xmm3, xmm4; vblendvps ymm1, ymm2,
    // ymmword ptr gs:[rax], ymm4; vblendvpd xmm9, xmm10, xmm11, xmm12 (inverted R, vvvv and B, and 12 in the
    // immediate); vpblendvb ymm1, ymm2, ymm3, ymm15; and vpblendvb xmm1, xmm2, xmmword ptr [r9+rax*8+8], xmm4 (the
    // immediate after the displacement)
    { { .form = X86_FORM_VBLENDVPS_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 4 << 4 }, 6, { 0xc4, 0xe3, 0x69, 0x4a, 0xcb, 0x40 } },
    { { .form = X86_FORM_VBLENDVPS_Y_Y_YM256_Y + BUSTER_X86_FORM_GS, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 4 << 4 },
      7,
      { 0x65, 0xc4, 0xe3, 0x6d, 0x4a, 0x08, 0x40 } },
    { { .form = X86_FORM_VBLENDVPD_X_X_XM128_X, .reg = 9, .vvvv = 10, .rm = 11, .immediate = 12 << 4 }, 6, { 0xc4, 0x43, 0x29, 0x4b, 0xcb, 0xc0 } },
    { { .form = X86_FORM_VPBLENDVB_Y_Y_YM256_Y, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 15 << 4 }, 6, { 0xc4, 0xe3, 0x6d, 0x4c, 0xcb, 0xf0 } },
    { { .form = X86_FORM_VPBLENDVB_X_X_XM128_X,
        .reg = 1,
        .vvvv = 2,
        .rm = X86_REGISTER_R9,
        .index = X86_REGISTER_RAX,
        .mode = BUSTER_X86_MEMORY_BASE_INDEX(3),
        .displacement = 8,
        .immediate = 4 << 4 },
      8,
      { 0xc4, 0xc3, 0x69, 0x4c, 0x4c, 0xc1, 0x08, 0x40 } },
    // FMA4: vfmaddps xmm1, xmm2, xmm3, xmm4 (W1, the fourth operand in ModRM.rm, as GNU as writes it between registers);
    // vfmaddps xmm1, xmm2, xmmword ptr [rax], xmm4 (W0); vfmaddps xmm1, xmm2, xmm4, xmm12 (W1, B); vfmaddpd ymm1, ymm2,
    // ymm3, ymmword ptr [rax]; vfmaddss xmm1, xmm2, dword ptr [rax], xmm4; vfnmsubsd xmm1, xmm2, xmm3, qword ptr [rax];
    // vfmaddsubps ymm1, ymm2, ymm3, ymm4; and W0 between registers, which GNU as doesn't write (objdump reads these
    // bytes as vfmaddps xmm1, xmm2, xmm3, xmm4 and vfmaddsd xmm1, xmm2, xmm3, xmm12)
    { { .form = X86_FORM_VFMADDPS_X_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 4, .immediate = 3 << 4 }, 6, { 0xc4, 0xe3, 0xe9, 0x68, 0xcc, 0x30 } },
    { { .form = X86_FORM_VFMADDPS_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 4 << 4 },
      6,
      { 0xc4, 0xe3, 0x69, 0x68, 0x08, 0x40 } },
    { { .form = X86_FORM_VFMADDPS_X_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 12, .immediate = 4 << 4 }, 6, { 0xc4, 0xc3, 0xe9, 0x68, 0xcc, 0x40 } },
    { { .form = X86_FORM_VFMADDPD_Y_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 },
      6,
      { 0xc4, 0xe3, 0xed, 0x69, 0x08, 0x30 } },
    { { .form = X86_FORM_VFMADDSS_X_X_XM32_X, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 4 << 4 },
      6,
      { 0xc4, 0xe3, 0x69, 0x6a, 0x08, 0x40 } },
    { { .form = X86_FORM_VFNMSUBSD_X_X_X_XM64, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 },
      6,
      { 0xc4, 0xe3, 0xe9, 0x7f, 0x08, 0x30 } },
    { { .form = X86_FORM_VFMADDSUBPS_Y_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = 4, .immediate = 3 << 4 }, 6, { 0xc4, 0xe3, 0xed, 0x5c, 0xcc, 0x30 } },
    { { .form = X86_FORM_VFMADDPS_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 4 << 4 }, 6, { 0xc4, 0xe3, 0x69, 0x68, 0xcb, 0x40 } },
    { { .form = X86_FORM_VFMADDSD_X_X_XM64_X, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 12 << 4 }, 6, { 0xc4, 0xe3, 0x69, 0x6b, 0xcb, 0xc0 } },
    // Segment registers: mov word ptr [rax], es (no 66); mov word ptr gs:[r9+rax*2+8], ds; mov ax, es (66); mov r9d,
    // gs; rex.w mov r9, gs; mov ss, r9w (no 66); mov ds, word ptr fs:[rax]; push fs; pushw gs; popw fs; and pop gs
    { { .form = X86_FORM_MOV_M16_SREG, .reg = 0, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8c, 0x00 } },
    { { .form = X86_FORM_MOV_M16_SREG + BUSTER_X86_FORM_GS,
        .reg = 3,
        .rm = X86_REGISTER_R9,
        .index = X86_REGISTER_RAX,
        .mode = BUSTER_X86_MEMORY_BASE_INDEX(1),
        .displacement = 8 },
      6,
      { 0x65, 0x41, 0x8c, 0x5c, 0x41, 0x08 } },
    { { .form = X86_FORM_MOV_R16_SREG, .reg = 0, .rm = X86_REGISTER_RAX }, 3, { 0x66, 0x8c, 0xc0 } },
    { { .form = X86_FORM_MOV_R32_SREG, .reg = 5, .rm = X86_REGISTER_R9 }, 3, { 0x41, 0x8c, 0xe9 } },
    { { .form = X86_FORM_MOV_R64_SREG, .reg = 5, .rm = X86_REGISTER_R9 }, 3, { 0x49, 0x8c, 0xe9 } },
    { { .form = X86_FORM_MOV_SREG_RM16, .reg = 2, .rm = X86_REGISTER_R9 }, 3, { 0x41, 0x8e, 0xd1 } },
    { { .form = X86_FORM_MOV_SREG_RM16 + BUSTER_X86_FORM_FS, .reg = 3, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x64, 0x8e, 0x18 } },
    { { .form = X86_FORM_PUSH_FS }, 2, { 0x0f, 0xa0 } },
    { { .form = X86_FORM_PUSHW_GS }, 3, { 0x66, 0x0f, 0xa8 } },
    { { .form = X86_FORM_POPW_FS }, 3, { 0x66, 0x0f, 0xa1 } },
    { { .form = X86_FORM_POP_GS }, 2, { 0x0f, 0xa9 } },
    // Control and debug registers: mov cr0, rax; mov rax, cr8 (REX.R); mov cr8, r9 (REX.R and B); mov dr0, rax; and
    // mov r9, dr7
    { { .form = X86_FORM_MOV_CR_R64, .reg = 0, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x22, 0xc0 } },
    { { .form = X86_FORM_MOV_R64_CR, .reg = 8, .rm = X86_REGISTER_RAX }, 4, { 0x44, 0x0f, 0x20, 0xc0 } },
    { { .form = X86_FORM_MOV_CR_R64, .reg = 8, .rm = X86_REGISTER_R9 }, 4, { 0x45, 0x0f, 0x22, 0xc1 } },
    { { .form = X86_FORM_MOV_DR_R64, .reg = 0, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x23, 0xc0 } },
    { { .form = X86_FORM_MOV_R64_DR, .reg = 7, .rm = X86_REGISTER_R9 }, 4, { 0x41, 0x0f, 0x21, 0xf9 } },
    // AVX512-FP16 and AVX10.2 (maps 5 and 6): vaddph zmm1, zmm2, zmm3; vaddph zmm17{k1}{z}, zmm18, zmm19; vaddph zmm1,
    // zmm2, word ptr [rax+2]{1to32} (N 2); vaddph zmm1, zmm2, zmm3, {rz-sae}; vaddsh xmm1, xmm2, word ptr [rax+2];
    // vfmadd132ph zmm1, zmm2, zmmword ptr [rax+64]; vfmadd231sh xmm1{k1}, xmm2, xmm3, {rd-sae}; vfmaddcph zmm1, zmm2,
    // dword ptr [rax+4]{1to16} (pairs as 32-bit elements); vfcmulcsh xmm1, xmm2, dword ptr [rax+4]; vcmpph k1{k2},
    // zmm2, zmm3, {sae}, 5 (0F 3A); and vfpclassph k1, word ptr [rax]{1to32}, 3
    { { .form = X86_FORM_VADDPH_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf5, 0x6c, 0x48, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPH_Z_Z_ZM512, .reg = 17, .vvvv = 18, .rm = 19, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      6,
      { 0x62, 0xa5, 0x6c, 0xc1, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPH_Z_Z_M16BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 2 },
      7,
      { 0x62, 0xf5, 0x6c, 0x58, 0x58, 0x48, 0x01 } },
    { { .form = X86_FORM_VADDPH_Z_Z_Z_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = BUSTER_X86_ROUND_ZERO }, 6, { 0x62, 0xf5, 0x6c, 0x78, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDSH_X_X_XM16, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 2 },
      7,
      { 0x62, 0xf5, 0x6e, 0x08, 0x58, 0x48, 0x01 } },
    { { .form = X86_FORM_VFMADD132PH_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 },
      7,
      { 0x62, 0xf6, 0x6d, 0x48, 0x98, 0x48, 0x01 } },
    { { .form = X86_FORM_VFMADD231SH_X_X_X_ER, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_ROUND_DOWN },
      6,
      { 0x62, 0xf6, 0x6d, 0x39, 0xb9, 0xcb } },
    { { .form = X86_FORM_VFMADDCPH_Z_Z_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 4 },
      7,
      { 0x62, 0xf6, 0x6e, 0x58, 0x56, 0x48, 0x01 } },
    { { .form = X86_FORM_VFCMULCSH_X_X_XM32, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 4 },
      7,
      { 0x62, 0xf6, 0x6f, 0x08, 0xd7, 0x48, 0x01 } },
    { { .form = X86_FORM_VCMPPH_K_Z_Z_SAE_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5, .decorations = 2 },
      7,
      { 0x62, 0xf3, 0x6c, 0x1a, 0xc2, 0xcb, 0x05 } },
    { { .form = X86_FORM_VFPCLASSPH_K_M16BCST32_IMM8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 },
      7,
      { 0x62, 0xf3, 0x7c, 0x58, 0x66, 0x08, 0x03 } },
    // Conversions and moves: vcvtph2pd zmm1, xmmword ptr [rax+16] (N 16, a quarter of the vector); vcvtpd2ph xmm1,
    // zmmword ptr [rax+64]; vcvtqq2ph xmm1, qword ptr [rax]{1to8}; vcvtdq2ph ymm1, zmm2, {rn-sae}; vcvtsh2si rax, xmm1,
    // {rz-sae}; vcvtsi2sh xmm1, xmm2, rax, {rd-sae}; vmovsh word ptr [rax+2]{k1}, xmm1; vmovw eax, xmm1; and vmovw
    // xmm1, xmm2 (zero extending, F3)
    { { .form = X86_FORM_VCVTPH2PD_Z_XM128, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x10 },
      7,
      { 0x62, 0xf5, 0x7c, 0x48, 0x5a, 0x48, 0x01 } },
    { { .form = X86_FORM_VCVTPD2PH_X_ZM512, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 },
      7,
      { 0x62, 0xf5, 0xfd, 0x48, 0x5a, 0x48, 0x01 } },
    { { .form = X86_FORM_VCVTQQ2PH_X_M64BCST8, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf5, 0xfc, 0x58, 0x5b, 0x08 } },
    { { .form = X86_FORM_VCVTDQ2PH_Y_Z_ER, .reg = 1, .rm = 2, .decorations = BUSTER_X86_ROUND_NEAREST }, 6, { 0x62, 0xf5, 0x7c, 0x18, 0x5b, 0xca } },
    { { .form = X86_FORM_VCVTSH2SI_R64_X_ER, .reg = X86_REGISTER_RAX, .rm = 1, .decorations = BUSTER_X86_ROUND_ZERO },
      6,
      { 0x62, 0xf5, 0xfe, 0x78, 0x2d, 0xc1 } },
    { { .form = X86_FORM_VCVTSI2SH_X_X_R64_ER, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .decorations = BUSTER_X86_ROUND_DOWN },
      6,
      { 0x62, 0xf5, 0xee, 0x38, 0x2a, 0xc8 } },
    { { .form = X86_FORM_VMOVSH_M16_X, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 2, .decorations = 1 },
      7,
      { 0x62, 0xf5, 0x7e, 0x09, 0x11, 0x48, 0x01 } },
    { { .form = X86_FORM_VMOVW_RM32_X, .reg = 1, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xf5, 0x7d, 0x08, 0x7e, 0xc8 } },
    { { .form = X86_FORM_VMOVW_X_X, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf5, 0x7e, 0x08, 0x6e, 0xca } },
    // AVX10.2: vaddbf16 ymm1{k1}{z}, ymm2, word ptr [rax]{1to16}; vfmadd213bf16 zmm1, zmm2, zmm3; vminmaxpd zmm1, zmm2,
    // zmm3, {sae}, 5; vcvtbiasph2bf8 ymm1, zmm2, zmm3 (0F 38); vcvtph2hf8 xmm1, word ptr [rax]{1to16}; vcvthf82ph zmm1,
    // ymmword ptr [rax+32]; vpdpbssd zmm1{k1}, zmm2, dword ptr [rax]{1to16}; vmovrsq zmm1{k1}{z}, zmmword ptr [rax+64];
    // vcomxsd xmm1, xmm2, {sae} (map 1); vmpsadbw zmm1, zmm2, zmm3, 5; vsm4key4 zmm1, zmm2, zmm3 (no opmask); vmovd
    // xmm1, xmm2 (zero extending); vcvttsd2sis rax, xmm1, {sae}; and vdpphps zmm1, zmm2, zmm3
    { { .form = X86_FORM_VADDBF16_Y_Y_M16BCST,
        .reg = 1,
        .vvvv = 2,
        .rm = X86_REGISTER_RAX,
        .mode = BUSTER_X86_MEMORY_BASE,
        .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      6,
      { 0x62, 0xf5, 0x6d, 0xb9, 0x58, 0x08 } },
    { { .form = X86_FORM_VFMADD213BF16_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf6, 0x6c, 0x48, 0xa8, 0xcb } },
    { { .form = X86_FORM_VMINMAXPD_Z_Z_Z_SAE_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf3, 0xed, 0x18, 0x52, 0xcb, 0x05 } },
    { { .form = X86_FORM_VCVTBIASPH2BF8_Y_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6c, 0x48, 0x74, 0xcb } },
    { { .form = X86_FORM_VCVTPH2HF8_X_M16BCST16, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      6,
      { 0x62, 0xf5, 0x7e, 0x38, 0x18, 0x08 } },
    { { .form = X86_FORM_VCVTHF82PH_Z_YM256, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x20 },
      7,
      { 0x62, 0xf5, 0x7f, 0x48, 0x1e, 0x48, 0x01 } },
    { { .form = X86_FORM_VPDPBSSD_Z_Z_M32BCST, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 },
      6,
      { 0x62, 0xf2, 0x6f, 0x59, 0x50, 0x08 } },
    { { .form = X86_FORM_VMOVRSQ_Z_M512,
        .reg = 1,
        .rm = X86_REGISTER_RAX,
        .mode = BUSTER_X86_MEMORY_BASE,
        .displacement = 0x40,
        .decorations = 1 | BUSTER_X86_DECORATION_ZEROING },
      7,
      { 0x62, 0xf5, 0xfe, 0xc9, 0x6f, 0x48, 0x01 } },
    { { .form = X86_FORM_VCOMXSD_X_X_SAE, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf1, 0xff, 0x18, 0x2f, 0xca } },
    { { .form = X86_FORM_VMPSADBW_Z_Z_ZM512_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 5 }, 7, { 0x62, 0xf3, 0x6e, 0x48, 0x42, 0xcb, 0x05 } },
    { { .form = X86_FORM_VSM4KEY4_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6e, 0x48, 0xda, 0xcb } },
    { { .form = X86_FORM_VMOVD_X_X, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf1, 0x7e, 0x08, 0x7e, 0xca } },
    { { .form = X86_FORM_VCVTTSD2SIS_R64_X_SAE, .reg = X86_REGISTER_RAX, .rm = 1 }, 6, { 0x62, 0xf5, 0xff, 0x18, 0x6d, 0xc1 } },
    { { .form = X86_FORM_VDPPHPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6c, 0x48, 0x52, 0xcb } },
    // XOP (8F in place of C4, maps 8 to 0A): vfrczps xmm1, xmm2; vpcmov ymm9, ymm10, ymm11, ymm12 (R, vvvv, B and 12 in
    // the immediate); vpcmov ymm1, ymm2, ymm3, ymmword ptr [r9] (W1); W1 between registers, which GNU as doesn't write
    // (objdump reads these bytes as vpcmov xmm1, xmm2, xmm3, xmm4); vpperm xmm1, xmm2, xmm3, xmm4; vpermil2ps xmm1,
    // xmm2, xmm3, xmm4, 3 (VEX, with a selector below the register) and vpermil2pd ymm1, ymm2, ymm3, ymmword ptr [rax],
    // 15; vpcomb xmm1, xmm2, xmm3, 200; vprotb xmm1, xmm2, xmm3 and {store} vprotb xmm1, xmm2, xmm3 (W0 and W1); vprotq
    // xmm1, xmm2, 200 (map 8); vpshlw xmm1, xmmword ptr [rax], xmm3; vphaddbw xmm1, xmm2; and vpmacssww xmm1, xmm2,
    // xmmword ptr [rax], xmm4
    { { .form = X86_FORM_VFRCZPS_X_XM128, .reg = 1, .rm = 2 }, 5, { 0x8f, 0xe9, 0x78, 0x80, 0xca } },
    { { .form = X86_FORM_VPCMOV_Y_Y_YM256_Y, .reg = 9, .vvvv = 10, .rm = 11, .immediate = 12 << 4 }, 6, { 0x8f, 0x48, 0x2c, 0xa2, 0xcb, 0xc0 } },
    { { .form = X86_FORM_VPCMOV_Y_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 },
      6,
      { 0x8f, 0xc8, 0xec, 0xa2, 0x09, 0x30 } },
    { { .form = X86_FORM_VPCMOV_X_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 4, .immediate = 3 << 4 }, 6, { 0x8f, 0xe8, 0xe8, 0xa2, 0xcc, 0x30 } },
    { { .form = X86_FORM_VPPERM_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 4 << 4 }, 6, { 0x8f, 0xe8, 0x68, 0xa3, 0xcb, 0x40 } },
    { { .form = X86_FORM_VPERMIL2PS_X_X_XM128_X_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 4 << 4 | 3 }, 6, { 0xc4, 0xe3, 0x69, 0x48, 0xcb, 0x43 } },
    { { .form = X86_FORM_VPERMIL2PD_Y_Y_Y_YM256_IMM8, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 | 15 },
      6,
      { 0xc4, 0xe3, 0xed, 0x49, 0x08, 0x3f } },
    { { .form = X86_FORM_VPCOMB_X_X_XM128_IMM8, .reg = 1, .vvvv = 2, .rm = 3, .immediate = 200 }, 6, { 0x8f, 0xe8, 0x68, 0xcc, 0xcb, 0xc8 } },
    { { .form = X86_FORM_VPROTB_X_XM128_X, .reg = 1, .vvvv = 3, .rm = 2 }, 5, { 0x8f, 0xe9, 0x60, 0x90, 0xca } },
    { { .form = X86_FORM_VPROTB_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 5, { 0x8f, 0xe9, 0xe8, 0x90, 0xcb } },
    { { .form = X86_FORM_VPROTQ_X_XM128_IMM8, .reg = 1, .rm = 2, .immediate = 200 }, 6, { 0x8f, 0xe8, 0x78, 0xc3, 0xca, 0xc8 } },
    { { .form = X86_FORM_VPSHLW_X_XM128_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x8f, 0xe9, 0x60, 0x95, 0x08 } },
    { { .form = X86_FORM_VPHADDBW_X_XM128, .reg = 1, .rm = 2 }, 5, { 0x8f, 0xe9, 0x78, 0xc1, 0xca } },
    { { .form = X86_FORM_VPMACSSWW_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 4 << 4 },
      6,
      { 0x8f, 0xe8, 0x68, 0x85, 0x08, 0x40 } },
    // TBM and LWP: blcfill eax, dword ptr fs:[rax] (the override before 8F); tzmsk r9, qword ptr [r10] (into VEX.vvvv,
    // W1); bextr r9, r10, 0x12345 (map 0A, a 32-bit immediate); llwpcb rax; lwpval r9, r10d, 0x12345 (W1 for the 64-bit
    // register); and lwpins eax, dword ptr [rax], 0x12345
    { { .form = X86_FORM_BLCFILL_R32_RM32 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
      6,
      { 0x64, 0x8f, 0xe9, 0x78, 0x01, 0x08 } },
    { { .form = X86_FORM_TZMSK_R64_RM64, .vvvv = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE },
      5,
      { 0x8f, 0xc9, 0xb0, 0x01, 0x22 } },
    { { .form = X86_FORM_BEXTR_R64_RM64_IMM32, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .immediate = 0x12345 },
      9,
      { 0x8f, 0x4a, 0xf8, 0x10, 0xca, 0x45, 0x23, 0x01, 0x00 } },
    { { .form = X86_FORM_LLWPCB_R64, .rm = X86_REGISTER_RAX }, 5, { 0x8f, 0xe9, 0xf8, 0x12, 0xc0 } },
    { { .form = X86_FORM_LWPVAL_R64_RM32_IMM32, .vvvv = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .immediate = 0x12345 },
      9,
      { 0x8f, 0xca, 0xb0, 0x12, 0xca, 0x45, 0x23, 0x01, 0x00 } },
    { { .form = X86_FORM_LWPINS_R32_RM32_IMM32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x12345 },
      9,
      { 0x8f, 0xea, 0x78, 0x12, 0x00, 0x45, 0x23, 0x01, 0x00 } },
    // Gathers and scatters (VSIB): vgatherdps xmm1, dword ptr [rax+xmm2*4], xmm3 (the mask in VEX.vvvv); vpgatherdd by
    // XMM4 (an index, not none) after RBP (an 8-bit zero); vgatherqpd by YMM12 (X) after RSP; vgatherdpd ymm1, qword
    // ptr [xmm2*8+16], ymm3 (no base, so a 32-bit displacement, and 256 bits by an XMM index); vpgatherqd xmm1, dword
    // ptr [r9+ymm14*2], xmm15 (128 bits by a YMM index); EVEX's vgatherdps by ZMM20 (V') after R13 (64, 16 units of
    // N = 4), vpgatherqq zmm30{k7} by ZMM31 (R', X and V') with -1024 (-128 units of 8), vgatherqps ymm1 by ZMM12,
    // vpgatherdq zmm1 by YMM4, vgatherdpd with 4 (not a multiple of N = 8) and vpgatherdd with 508 (127 units of 4);
    // and vpscatterdd, vscatterqpd by XMM16 without a base, and vscatterdps with FS
    { { .form = X86_FORM_VGATHERDPS_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) },
      6,
      { 0xc4, 0xe2, 0x61, 0x92, 0x0c, 0x90 } },
    { { .form = X86_FORM_VPGATHERDD_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RBP, .index = 4, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) },
      7,
      { 0xc4, 0xe2, 0x61, 0x90, 0x4c, 0x25, 0x00 } },
    { { .form = X86_FORM_VGATHERQPD_Y_VM64Y_Y, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RSP, .index = 12, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = -8 },
      7,
      { 0xc4, 0xa2, 0xe5, 0x93, 0x4c, 0xe4, 0xf8 } },
    { { .form = X86_FORM_VGATHERDPD_Y_VM32X_Y, .reg = 1, .vvvv = 3, .index = 2, .mode = BUSTER_X86_MEMORY_INDEX(3), .displacement = 16 },
      10,
      { 0xc4, 0xe2, 0xe5, 0x92, 0x0c, 0xd5, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_VPGATHERQD_X_VM64Y_X, .reg = 1, .vvvv = 15, .rm = X86_REGISTER_R9, .index = 14, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1) },
      6,
      { 0xc4, 0x82, 0x05, 0x91, 0x0c, 0x71 } },
    { { .form = X86_FORM_VGATHERDPS_Z_VM32Z, .reg = 1, .rm = X86_REGISTER_R13, .index = 20, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 64, .decorations = 1 },
      8,
      { 0x62, 0xd2, 0x7d, 0x41, 0x92, 0x4c, 0xa5, 0x10 } },
    { { .form = X86_FORM_VPGATHERQQ_Z_VM64Z, .reg = 30, .rm = X86_REGISTER_RAX, .index = 31, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = -1024, .decorations = 7 },
      8,
      { 0x62, 0x22, 0xfd, 0x47, 0x91, 0x74, 0xf8, 0x80 } },
    { { .form = X86_FORM_VGATHERQPS_Y_VM64Z, .reg = 1, .rm = X86_REGISTER_RAX, .index = 12, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 },
      7,
      { 0x62, 0xb2, 0x7d, 0x49, 0x93, 0x0c, 0xa0 } },
    { { .form = X86_FORM_VPGATHERDQ_Z_VM32Y, .reg = 1, .rm = X86_REGISTER_RAX, .index = 4, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .decorations = 1 },
      7,
      { 0x62, 0xf2, 0xfd, 0x49, 0x90, 0x0c, 0xe0 } },
    { { .form = X86_FORM_VGATHERDPD_X_VM32X, .reg = 1, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 4, .decorations = 1 },
      11,
      { 0x62, 0xf2, 0xfd, 0x09, 0x92, 0x8c, 0xd0, 0x04, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_VPGATHERDD_X_VM32X, .reg = 5, .rm = X86_REGISTER_RDX, .index = 6, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1), .displacement = 508, .decorations = 3 },
      8,
      { 0x62, 0xf2, 0x7d, 0x0b, 0x90, 0x6c, 0x72, 0x7f } },
    { { .form = X86_FORM_VPSCATTERDD_VM32Z_Z, .reg = 1, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 },
      7,
      { 0x62, 0xf2, 0x7d, 0x49, 0xa0, 0x0c, 0x90 } },
    { { .form = X86_FORM_VSCATTERQPD_VM64X_X, .reg = 3, .index = 16, .mode = BUSTER_X86_MEMORY_INDEX(3), .displacement = 256, .decorations = 2 },
      11,
      { 0x62, 0xf2, 0xfd, 0x02, 0xa3, 0x1c, 0xc5, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_VSCATTERDPS_VM32Y_Y + BUSTER_X86_FORM_FS, .reg = 6, .rm = X86_REGISTER_RAX, .index = 5, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 },
      8,
      { 0x64, 0x62, 0xf2, 0x7d, 0x29, 0xa2, 0x34, 0xa8 } },
    // System instructions: lar ax, cx (66) and rex.w lar r9d, r10d (REX.W with R and B); lsl eax, word ptr [rax]; sldt
    // r9w (66 and REX.B) and sldt to memory (no 66); rex.w str rax; smsw r9 (REX.W from GNU as itself); lgdt tbyte ptr
    // fs:[rax]; in ax, 0x10 and out dx, eax; rep insw (66 before F3) and repne outsd; iretw, sysretq, sysexitd and
    // sysexitq; xsave64 [rax]; incsspq r9 (F3 before REX.W); wrussq (66, REX.W and 0F 38); vmread r9, r10 (no REX.W);
    // invpcid r9, xmmword ptr [r10]; urdmsr r9, 0x80 and wrmsrns 0x12345678, rax (VEX map 7, with a 32-bit immediate);
    // hreset 255 (its ModRM byte in the opcode); enqcmd r9, [r10]; uwrmsr r9, r10; and pvalidate
    { { .form = X86_FORM_LAR_R16_RM16, .rm = X86_REGISTER_RCX }, 4, { 0x66, 0x0f, 0x02, 0xc1 } },
    { { .form = X86_FORM_LAR_R64_R64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10 }, 4, { 0x4d, 0x0f, 0x02, 0xca } },
    { { .form = X86_FORM_LSL_R32_M16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x03, 0x00 } },
    { { .form = X86_FORM_SLDT_R16, .rm = X86_REGISTER_R9 }, 5, { 0x66, 0x41, 0x0f, 0x00, 0xc1 } },
    { { .form = X86_FORM_SLDT_M16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x00, 0x00 } },
    { { .form = X86_FORM_STR_R64 }, 4, { 0x48, 0x0f, 0x00, 0xc8 } },
    { { .form = X86_FORM_SMSW_R64, .rm = X86_REGISTER_R9 }, 4, { 0x49, 0x0f, 0x01, 0xe1 } },
    { { .form = X86_FORM_LGDT_M80 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x64, 0x0f, 0x01, 0x10 } },
    { { .form = X86_FORM_IN_AX_IMM8, .immediate = 0x10 }, 3, { 0x66, 0xe5, 0x10 } },
    { { .form = X86_FORM_OUT_DX_EAX }, 1, { 0xef } },
    { { .form = X86_FORM_REP_INS_W }, 3, { 0x66, 0xf3, 0x6d } },
    { { .form = X86_FORM_REPNE_OUTS_D }, 2, { 0xf2, 0x6f } },
    { { .form = X86_FORM_IRET }, 2, { 0x66, 0xcf } },
    { { .form = X86_FORM_SYSRET64 }, 3, { 0x48, 0x0f, 0x07 } },
    { { .form = X86_FORM_SYSEXIT }, 2, { 0x0f, 0x35 } },
    { { .form = X86_FORM_SYSEXITQ }, 3, { 0x48, 0x0f, 0x35 } },
    { { .form = X86_FORM_XSAVE64_M, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x48, 0x0f, 0xae, 0x20 } },
    { { .form = X86_FORM_INCSSPQ_R64, .rm = X86_REGISTER_R9 }, 5, { 0xf3, 0x49, 0x0f, 0xae, 0xe9 } },
    { { .form = X86_FORM_WRUSSQ_M64_R64, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0x48, 0x0f, 0x38, 0xf5, 0x08 } },
    { { .form = X86_FORM_VMREAD_RM64_R64, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R9 }, 4, { 0x45, 0x0f, 0x78, 0xd1 } },
    { { .form = X86_FORM_INVPCID_R64_M128, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0x45, 0x0f, 0x38, 0x82, 0x0a } },
    { { .form = X86_FORM_URDMSR_R64_IMM32, .rm = X86_REGISTER_R9, .immediate = 0x80 }, 9, { 0xc4, 0xc7, 0x7b, 0xf8, 0xc1, 0x80, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_WRMSRNS_IMM32_R64, .immediate = 0x12345678 }, 9, { 0xc4, 0xe7, 0x7a, 0xf6, 0xc0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_HRESET_IMM8, .immediate = 255 }, 6, { 0xf3, 0x0f, 0x3a, 0xf0, 0xc0, 0xff } },
    { { .form = X86_FORM_ENQCMD_R64_M512, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0xf2, 0x45, 0x0f, 0x38, 0xf8, 0x0a } },
    { { .form = X86_FORM_UWRMSR_R64_R64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R10 }, 6, { 0xf3, 0x45, 0x0f, 0x38, 0xf8, 0xca } },
    { { .form = X86_FORM_PVALIDATE_RAX_ECX_EDX }, 4, { 0xf2, 0x0f, 0x01, 0xff } },
    // sysretd; enclv; vmgexit (vmmcall's F3); rmpquery; rmpread [rcx], rax
    { { .form = X86_FORM_SYSRET }, 2, { 0x0f, 0x07 } },
    { { .form = X86_FORM_ENCLV }, 3, { 0x0f, 0x01, 0xc0 } },
    { { .form = X86_FORM_VMGEXIT }, 4, { 0xf3, 0x0f, 0x01, 0xd9 } },
    { { .form = X86_FORM_RMPQUERY_RAX_RCX_RDX }, 4, { 0xf3, 0x0f, 0x01, 0xfd } },
    { { .form = X86_FORM_RMPREAD_RAX_RCX }, 4, { 0xf2, 0x0f, 0x01, 0xfd } },
    // AMX: ldtilecfg [rax] and sttilecfg [r9] (VEX.B); tilerelease; tilezero tmm7 (ModRM.reg); tileloadd tmm1,
    // [rax+rcx*1] and its SIB operand with none for the index (RSP's: [rax], [rbp] with an 8-bit zero and [r12]) or
    // without a base ([rcx*1+0x10], which GNU as keeps, [rcx*2+0x10] and ds:0x10); tileloadd tmm7, [r9+r10*8-0x80] (X
    // and B), and with FS; tilestored gs:[rbp-0x10], tmm7; tileloaddt1, tileloaddrs and tileloaddrst1; tdpbssd, and
    // tdpbusd tmm7, tmm6, tmm5; tdpbf8ps and tdphf8ps (VEX map 5); tcmmimfp16ps; tcvtrowd2ps zmm1, tmm2, 5 (EVEX map 3)
    // and its forms with a register in EVEX.vvvv (map 2): ecx, r9d with zmm17 (R') and r15d; tilemovrow zmm31 (R and
    // R'); and both forms of tcvtrowps2phl (whose pp differ)
    { { .form = X86_FORM_LDTILECFG_M512, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xe2, 0x78, 0x49, 0x00 } },
    { { .form = X86_FORM_STTILECFG_M512, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xc4, 0xc2, 0x79, 0x49, 0x01 } },
    { { .form = X86_FORM_TILERELEASE }, 5, { 0xc4, 0xe2, 0x78, 0x49, 0xc0 } },
    { { .form = X86_FORM_TILEZERO_TMM, .reg = 7 }, 5, { 0xc4, 0xe2, 0x7b, 0x49, 0xf8 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x08 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x20 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0xc4, 0xe2, 0x7b, 0x4b, 0x4c, 0x25, 0x00 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_R12, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xc2, 0x7b, 0x4b, 0x0c, 0x24 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(0), .displacement = 0x10 }, 10, { 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x0d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(1), .displacement = 0x10 }, 10, { 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x4d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x10 }, 10, { 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x25, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 7, .rm = X86_REGISTER_R9, .index = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = -0x80 }, 7, { 0xc4, 0x82, 0x7b, 0x4b, 0x7c, 0xd1, 0x80 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM + BUSTER_X86_FORM_FS, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x64, 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x08 } },
    { { .form = X86_FORM_TILESTORED_SIBMEM_TMM + BUSTER_X86_FORM_GS, .reg = 7, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = -0x10 }, 8, { 0x65, 0xc4, 0xe2, 0x7a, 0x4b, 0x7c, 0x25, 0xf0 } },
    { { .form = X86_FORM_TILELOADDT1_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x79, 0x4b, 0x0c, 0x08 } },
    { { .form = X86_FORM_TILELOADDRS_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x7b, 0x4a, 0x0c, 0x08 } },
    { { .form = X86_FORM_TILELOADDRST1_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x79, 0x4a, 0x0c, 0x08 } },
    { { .form = X86_FORM_TDPBSSD_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x63, 0x5e, 0xca } },
    { { .form = X86_FORM_TDPBUSD_TMM_TMM_TMM, .reg = 7, .rm = 6, .vvvv = 5 }, 5, { 0xc4, 0xe2, 0x51, 0x5e, 0xfe } },
    { { .form = X86_FORM_TDPBF8PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe5, 0x60, 0xfd, 0xca } },
    { { .form = X86_FORM_TDPHF8PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe5, 0x61, 0xfd, 0xca } },
    { { .form = X86_FORM_TCMMIMFP16PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x61, 0x6c, 0xca } },
    { { .form = X86_FORM_TCVTROWD2PS_Z_TMM_IMM8, .reg = 1, .rm = 2, .immediate = 5 }, 7, { 0x62, 0xf3, 0x7e, 0x48, 0x07, 0xca, 0x05 } },
    { { .form = X86_FORM_TCVTROWD2PS_Z_TMM_R32, .reg = 1, .rm = 2, .vvvv = X86_REGISTER_RCX }, 6, { 0x62, 0xf2, 0x76, 0x48, 0x4a, 0xca } },
    { { .form = X86_FORM_TCVTROWD2PS_Z_TMM_R32, .reg = 17, .rm = 2, .vvvv = X86_REGISTER_R9 }, 6, { 0x62, 0xe2, 0x36, 0x48, 0x4a, 0xca } },
    { { .form = X86_FORM_TCVTROWD2PS_Z_TMM_R32, .reg = 1, .rm = 7, .vvvv = X86_REGISTER_R15 }, 6, { 0x62, 0xf2, 0x06, 0x48, 0x4a, 0xcf } },
    { { .form = X86_FORM_TILEMOVROW_Z_TMM_IMM8, .reg = 31, .rm = 2, .immediate = 255 }, 7, { 0x62, 0x63, 0x7d, 0x48, 0x07, 0xfa, 0xff } },
    { { .form = X86_FORM_TCVTROWPS2PHL_Z_TMM_IMM8, .reg = 1, .rm = 2, .immediate = 5 }, 7, { 0x62, 0xf3, 0x7f, 0x48, 0x77, 0xca, 0x05 } },
    { { .form = X86_FORM_TCVTROWPS2PHL_Z_TMM_R32, .reg = 1, .rm = 2, .vvvv = X86_REGISTER_RCX }, 6, { 0x62, 0xf2, 0x75, 0x48, 0x6d, 0xca } },
    // AMX-TRANSPOSE and AMX-TF32: t2rpntlvwz0 tmm2, [rax+rcx*1] (a pair of tiles), t2rpntlvwz1t1 (66 and 6F),
    // t2rpntlvwz0rs and t2rpntlvwz1rst1 (VEX map 5), t2rpntlvwz0 tmm6, [r9+r10*8-0x80] and tmm0 with none for the
    // index; ttransposed tmm7, tmm6 and tconjtfp16 (two tiles); and tconjtcmmimfp16ps, ttcmmrlfp16ps, ttdpfp16ps,
    // tmmultf32ps and ttmmultf32ps
    { { .form = X86_FORM_T2RPNTLVWZ0_TMM_SIBMEM, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x78, 0x6e, 0x14, 0x08 } },
    { { .form = X86_FORM_T2RPNTLVWZ1T1_TMM_SIBMEM, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x79, 0x6f, 0x14, 0x08 } },
    { { .form = X86_FORM_T2RPNTLVWZ0RS_TMM_SIBMEM, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe5, 0x78, 0xf8, 0x14, 0x08 } },
    { { .form = X86_FORM_T2RPNTLVWZ1RST1_TMM_SIBMEM, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe5, 0x79, 0xf9, 0x14, 0x08 } },
    { { .form = X86_FORM_T2RPNTLVWZ0_TMM_SIBMEM, .reg = 6, .rm = X86_REGISTER_R9, .index = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = -0x80 }, 7, { 0xc4, 0x82, 0x78, 0x6e, 0x74, 0xd1, 0x80 } },
    { { .form = X86_FORM_T2RPNTLVWZ0_TMM_SIBMEM, .reg = 0, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 6, { 0xc4, 0xe2, 0x78, 0x6e, 0x04, 0x20 } },
    { { .form = X86_FORM_TTRANSPOSED_TMM_TMM, .reg = 7, .rm = 6 }, 5, { 0xc4, 0xe2, 0x7a, 0x5f, 0xfe } },
    { { .form = X86_FORM_TCONJTFP16_TMM_TMM, .reg = 1, .rm = 2 }, 5, { 0xc4, 0xe2, 0x79, 0x6b, 0xca } },
    { { .form = X86_FORM_TCONJTCMMIMFP16PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x60, 0x6b, 0xca } },
    { { .form = X86_FORM_TTCMMRLFP16PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x62, 0x6b, 0xca } },
    { { .form = X86_FORM_TTDPFP16PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x63, 0x6c, 0xca } },
    { { .form = X86_FORM_TMMULTF32PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x61, 0x48, 0xca } },
    { { .form = X86_FORM_TTMMULTF32PS_TMM_TMM_TMM, .reg = 1, .rm = 2, .vvvv = 3 }, 5, { 0xc4, 0xe2, 0x60, 0x48, 0xca } },
    // MPX: bndmk [rax], and with a scaled index and with an index but no base (which GNU as keeps, as bndmk's lower
    // bound is its base); bndcl with RAX and R9 (REX.B after F3), memory and RIP; bndcu, bndcn and bndmov between bound
    // registers, from memory and to memory with FS; bndldx through a MIB operand, without an index, with R12 and R13 (B
    // and X) and without a base, and bndstx. 3DNow!: femms, and pfadd's operation byte after a register, memory and
    // RIP's displacement, pavgusb's after an 8-bit one, pswapd, pi2fw after a SIB byte and a 32-bit displacement with
    // REX, and pfrcpit1 with FS. Xeon Phi: vexp2ps, vexp2pd with a broadcast, an opmask and zeroing, vrcp28ps zmm30 (R
    // and R') with suppressed exceptions, vrsqrt28pd with N = 64, vrcp28ss and vrsqrt28sd; the prefetching gathers and
    // scatters by ZMM1, ZMM17 (V'), YMM1 (vgatherpf0dpd), with N = 4 and ZMM31 after R9 (X, B and V') under K7; and
    // v4fmaddps from ZMM4 and ZMM28 (V') with an opmask, zeroing and N = 16, v4fnmaddss, vp4dpwssds and v4fmaddss into
    // XMM17 (R')
    { { .form = X86_FORM_BNDMK_BND_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf3, 0x0f, 0x1b, 0x00 } },
    { { .form = X86_FORM_BNDMK_BND_M64, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x100 }, 9, { 0xf3, 0x0f, 0x1b, 0x8c, 0xc8, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_BNDMK_BND_M64, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(0), .displacement = 0x10 }, 9, { 0xf3, 0x0f, 0x1b, 0x04, 0x0d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .reg = 1, .rm = X86_REGISTER_RAX }, 4, { 0xf3, 0x0f, 0x1a, 0xc8 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .rm = X86_REGISTER_R9 }, 5, { 0xf3, 0x41, 0x0f, 0x1a, 0xc1 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf3, 0x0f, 0x1a, 0x08 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 16 }, 8, { 0xf3, 0x0f, 0x1a, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BNDCU_BND_RM64, .reg = 2, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 8 }, 6, { 0xf2, 0x0f, 0x1a, 0x54, 0x88, 0x08 } },
    { { .form = X86_FORM_BNDCN_BND_RM64, .reg = 3, .rm = X86_REGISTER_RCX }, 4, { 0xf2, 0x0f, 0x1b, 0xd9 } },
    { { .form = X86_FORM_BNDMOV_BND_BND, .rm = 1 }, 4, { 0x66, 0x0f, 0x1a, 0xc1 } },
    { { .form = X86_FORM_BNDMOV_BND_M128, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x1a, 0x00 } },
    { { .form = X86_FORM_BNDMOV_M128_BND + BUSTER_X86_FORM_FS, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x64, 0x66, 0x0f, 0x1b, 0x08 } },
    { { .form = X86_FORM_BNDLDX_BND_MIB, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 4, { 0x0f, 0x1a, 0x04, 0x08 } },
    { { .form = X86_FORM_BNDLDX_BND_MIB, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x1a, 0x00 } },
    { { .form = X86_FORM_BNDLDX_BND_MIB, .rm = X86_REGISTER_R12, .index = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 5, { 0x43, 0x0f, 0x1a, 0x04, 0x2c } },
    { { .form = X86_FORM_BNDSTX_MIB_BND, .reg = 3, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 0x80 }, 8, { 0x0f, 0x1b, 0x9c, 0x08, 0x80, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BNDLDX_BND_MIB, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(0), .displacement = 16 }, 8, { 0x0f, 0x1a, 0x04, 0x0d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_FEMMS }, 2, { 0x0f, 0x0e } },
    { { .form = X86_FORM_PFADD_MM_MM, .rm = 1, .immediate = 0x9e }, 4, { 0x0f, 0x0f, 0xc1, 0x9e } },
    { { .form = X86_FORM_PFADD_MM_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x9e }, 4, { 0x0f, 0x0f, 0x00, 0x9e } },
    { { .form = X86_FORM_PAVGUSB_MM_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8, .immediate = 0xbf }, 5, { 0x0f, 0x0f, 0x40, 0x08, 0xbf } },
    { { .form = X86_FORM_PSWAPD_MM_MM, .reg = 2, .rm = 3, .immediate = 0xbb }, 4, { 0x0f, 0x0f, 0xd3, 0xbb } },
    { { .form = X86_FORM_PFADD_MM_M64, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 16, .immediate = 0x9e }, 8, { 0x0f, 0x0f, 0x05, 0x10, 0x00, 0x00, 0x00, 0x9e } },
    { { .form = X86_FORM_PI2FW_MM_M64, .reg = 1, .rm = X86_REGISTER_R9, .index = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x100, .immediate = 0x0c }, 10, { 0x43, 0x0f, 0x0f, 0x8c, 0xd1, 0x00, 0x01, 0x00, 0x00, 0x0c } },
    { { .form = X86_FORM_PFRCPIT1_MM_M64 + BUSTER_X86_FORM_FS, .reg = 7, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0xa6 }, 5, { 0x64, 0x0f, 0x0f, 0x38, 0xa6 } },
    { { .form = X86_FORM_VEXP2PS_Z_ZM512, .reg = 1, .rm = 2 }, 6, { 0x62, 0xf2, 0x7d, 0x48, 0xc8, 0xca } },
    { { .form = X86_FORM_VEXP2PD_Z_M64BCST, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf2, 0xfd, 0xd9, 0xc8, 0x08 } },
    { { .form = X86_FORM_VRCP28PS_Z_Z_SAE, .reg = 30, .rm = 2 }, 6, { 0x62, 0x62, 0x7d, 0x18, 0xca, 0xf2 } },
    { { .form = X86_FORM_VRSQRT28PD_Z_ZM512, .reg = 1, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x40 }, 7, { 0x62, 0xf2, 0xfd, 0x48, 0xcc, 0x48, 0x01 } },
    { { .form = X86_FORM_VRCP28SS_X_X_X_SAE, .reg = 1, .vvvv = 2, .rm = 3 }, 6, { 0x62, 0xf2, 0x6d, 0x18, 0xcb, 0xcb } },
    { { .form = X86_FORM_VRSQRT28SD_X_X_XM64, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = 1 }, 6, { 0x62, 0xf2, 0xed, 0x09, 0xcd, 0x08 } },
    { { .form = X86_FORM_VGATHERPF0DPS_VM32Z, .rm = X86_REGISTER_RAX, .index = 1, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 7, { 0x62, 0xf2, 0x7d, 0x49, 0xc6, 0x0c, 0x88 } },
    { { .form = X86_FORM_VGATHERPF0DPS_VM32Z, .rm = X86_REGISTER_RAX, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 7, { 0x62, 0xf2, 0x7d, 0x41, 0xc6, 0x0c, 0x88 } },
    { { .form = X86_FORM_VSCATTERPF1QPD_VM64Z, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .decorations = 2 }, 7, { 0x62, 0xf2, 0xfd, 0x4a, 0xc7, 0x34, 0xd0 } },
    { { .form = X86_FORM_VGATHERPF0DPD_VM32Y, .rm = X86_REGISTER_RAX, .index = 1, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .decorations = 1 }, 7, { 0x62, 0xf2, 0xfd, 0x49, 0xc6, 0x0c, 0xc8 } },
    { { .form = X86_FORM_VGATHERPF1QPS_VM64Z, .rm = X86_REGISTER_RAX, .index = 1, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x40, .decorations = 1 }, 8, { 0x62, 0xf2, 0x7d, 0x49, 0xc7, 0x54, 0x88, 0x10 } },
    { { .form = X86_FORM_VSCATTERPF0DPS_VM32Z, .rm = X86_REGISTER_R9, .index = 31, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = -0x200, .decorations = 7 }, 8, { 0x62, 0x92, 0x7d, 0x47, 0xc6, 0x6c, 0xb9, 0x80 } },
    { { .form = X86_FORM_V4FMADDPS_Z_Z_M128, .reg = 1, .vvvv = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x5f, 0x48, 0x9a, 0x08 } },
    { { .form = X86_FORM_V4FMADDPS_Z_Z_M128, .reg = 1, .vvvv = 28, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x1f, 0x40, 0x9a, 0x08 } },
    { { .form = X86_FORM_V4FMADDPS_Z_Z_M128, .reg = 1, .vvvv = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x10, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 7, { 0x62, 0xf2, 0x5f, 0xc9, 0x9a, 0x48, 0x01 } },
    { { .form = X86_FORM_V4FNMADDSS_X_X_M128, .reg = 1, .vvvv = 4, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf2, 0x5f, 0x08, 0xab, 0x08 } },
    { { .form = X86_FORM_VP4DPWSSDS_Z_Z_M128, .reg = 1, .vvvv = 8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 16, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 7, { 0x62, 0xf2, 0x3f, 0xc9, 0x53, 0x48, 0x01 } },
    { { .form = X86_FORM_V4FMADDSS_X_X_M128, .reg = 17, .vvvv = 8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xe2, 0x3f, 0x08, 0x9b, 0x08 } },
    // Prefixes: HLE's xacquire and xrelease on lock add, before lock and after 66 with FS (64 66 F2 F0), on lock xadd
    // with REX, lock bts with all eight of a template's own bytes (64 66 F2 F0, REX, 0F BA and the ModRM digit), lock
    // cmpxchg8b, lock not, lock cmpxchg with REX.W, lock sub with an imm32, and xchg without lock (66 before F2), and
    // xrelease mov to memory from a register and an immediate; bnd on jmp (rel8 and rel32), call, ret (with an imm16
    // too), jmp and call through a register and memory, with FS, and on jz and jge; the branch hints, ds (taken) and cs
    // (not taken), at both widths; notrack jmp and call through memory, and notrack bnd (3E F2) with REX.B; and the
    // 16-bit near branches (66 with a rel16): jmp, call (to a displacement GNU as wraps to 16 bits), jz and jg
    { { .form = X86_FORM_XACQUIRE_LOCK_ADD_M32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf2, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_XRELEASE_LOCK_ADD_M32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf3, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_ADD_M16_R16 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x64, 0x66, 0xf2, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_XADD_M16_R16 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_R10, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 8, { 0x64, 0x66, 0xf2, 0xf0, 0x45, 0x0f, 0xc1, 0x11 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_BTS_M16_IMM8 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 }, 8, { 0x64, 0x66, 0xf2, 0xf0, 0x0f, 0xba, 0x28, 0x03 } },
    { { .form = X86_FORM_XRELEASE_LOCK_CMPXCHG8B_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf3, 0xf0, 0x0f, 0xc7, 0x08 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_NOT_M8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf2, 0xf0, 0xf6, 0x10 } },
    { { .form = X86_FORM_XRELEASE_LOCK_CMPXCHG_M64_R64, .reg = X86_REGISTER_R9, .rm = X86_REGISTER_R8, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0xf3, 0xf0, 0x4d, 0x0f, 0xb1, 0x08 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_SUB_M64_IMM32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x12345678 }, 9, { 0xf2, 0xf0, 0x48, 0x81, 0x28, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_XACQUIRE_XCHG_M32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0xf2, 0x87, 0x08 } },
    { { .form = X86_FORM_XACQUIRE_XCHG_M16_R16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0xf2, 0x87, 0x08 } },
    { { .form = X86_FORM_XRELEASE_MOV_M16_R16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0xf3, 0x89, 0x08 } },
    { { .form = X86_FORM_XRELEASE_MOV_M64_IMM32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 1 }, 8, { 0xf3, 0x48, 0xc7, 0x00, 0x01, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BND_JMP_REL8, .displacement = 29 }, 3, { 0xf2, 0xeb, 0x1d } },
    { { .form = X86_FORM_BND_JMP_REL32, .displacement = 26 }, 6, { 0xf2, 0xe9, 0x1a, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BND_CALL_REL32, .displacement = 26 }, 6, { 0xf2, 0xe8, 0x1a, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BND_RET }, 2, { 0xf2, 0xc3 } },
    { { .form = X86_FORM_BND_RET_IMM16, .immediate = 8 }, 4, { 0xf2, 0xc2, 0x08, 0x00 } },
    { { .form = X86_FORM_BND_JMP_RM64, .rm = X86_REGISTER_RAX }, 3, { 0xf2, 0xff, 0xe0 } },
    { { .form = X86_FORM_BND_CALL_RM64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0xf2, 0xff, 0x10 } },
    { { .form = X86_FORM_BND_JMP_RM64 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x64, 0xf2, 0xff, 0x20 } },
    { { .form = X86_FORM_BND_JE_REL8, .displacement = 29 }, 3, { 0xf2, 0x74, 0x1d } },
    { { .form = X86_FORM_BND_JGE_REL32, .displacement = -263 }, 7, { 0xf2, 0x0f, 0x8d, 0xf9, 0xfe, 0xff, 0xff } },
    { { .form = X86_FORM_JE_REL8_TAKEN, .displacement = 29 }, 3, { 0x3e, 0x74, 0x1d } },
    { { .form = X86_FORM_JNE_REL8_NOT_TAKEN, .displacement = -19 }, 3, { 0x2e, 0x75, 0xed } },
    { { .form = X86_FORM_JE_REL32_TAKEN, .displacement = 25 }, 7, { 0x3e, 0x0f, 0x84, 0x19, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_JG_REL32_NOT_TAKEN, .displacement = 4089 }, 7, { 0x2e, 0x0f, 0x8f, 0xf9, 0x0f, 0x00, 0x00 } },
    { { .form = X86_FORM_NOTRACK_JMP_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x3e, 0xff, 0x20 } },
    { { .form = X86_FORM_NOTRACK_CALL_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 4, { 0x3e, 0xff, 0x50, 0x08 } },
    { { .form = X86_FORM_NOTRACK_BND_JMP_R64, .rm = X86_REGISTER_RAX }, 4, { 0x3e, 0xf2, 0xff, 0xe0 } },
    { { .form = X86_FORM_NOTRACK_BND_CALL_M64, .rm = X86_REGISTER_R9, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x3e, 0xf2, 0x41, 0xff, 0x11 } },
    { { .form = X86_FORM_JMP_REL16, .displacement = 28 }, 4, { 0x66, 0xe9, 0x1c, 0x00 } },
    { { .form = X86_FORM_CALL_REL16, .displacement = 32764 }, 4, { 0x66, 0xe8, 0xfc, 0x7f } },
    { { .form = X86_FORM_JE_REL16, .displacement = 763 }, 5, { 0x66, 0x0f, 0x84, 0xfb, 0x02 } },
    { { .form = X86_FORM_JG_REL16, .displacement = -21 }, 5, { 0x66, 0x0f, 0x8f, 0xeb, 0xff } },
};

// The same with 32-bit addresses (x86_encode_address32), from GNU as.
static const KnownEncoding x86_known_encodings_address32[] = {
    // mov eax, [eax]; mov rax, [r9d+r10d*8-0x80] (67 before REX); [ebp] and [r13d] with an 8-bit zero, [esp] and [r12d]
    // with SIB; [ecx*2+0x10], and [ecx*1+0x10], which GNU as keeps with a 32-bit address; an absolute address (with
    // addr32) and [eip+0x10]; mov rax, fs:[eax] (64 67 48); lea eax, [ecx+edx*2]; add eax, ecx (no memory, no 67);
    // EVEX's vmovdqu64 with N = 64 and VEX's vaddps after R8D (67 before C4 and 62); gathers by XMM2 and ZMM20 (V');
    // tileloadd with an index and without (SIB's none); xacquire lock add with 66 and FS (64 67 66 F2 F0); pfadd; call
    // and a far jmp through memory; notrack (3E before 67) and notrack bnd (3E 67 F2); lock cmpxchg16b with GS and REX;
    // crc32 of a word with FS (64 67 66 F2); movdir64b eax; prefetchit0 [eip+0x10] and mov eax, gs:[eip+0x10]; bndcl
    // bnd1, rax (no memory, no 67); an absolute address in EVEX (a 32-bit displacement); fsave [eax] (9B before 67);
    // and the longest, 15 bytes: lock add qword ptr fs:[eax+ecx*4+0x12345678], 0x12345678
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x67, 0x8b, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = X86_REGISTER_R9, .index = X86_REGISTER_R10, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = -0x80 }, 6, { 0x67, 0x4b, 0x8b, 0x44, 0xd1, 0x80 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x67, 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x67, 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_R13, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x67, 0x41, 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_R12, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x67, 0x41, 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R32_RM32, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(1), .displacement = 0x10 }, 8, { 0x67, 0x8b, 0x04, 0x4d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(0), .displacement = 0x10 }, 8, { 0x67, 0x8b, 0x04, 0x0d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x10 }, 8, { 0x67, 0x8b, 0x0c, 0x25, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 7, { 0x67, 0x8b, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x64, 0x67, 0x48, 0x8b, 0x00 } },
    { { .form = X86_FORM_LEA_R32_M, .rm = X86_REGISTER_RCX, .index = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1) }, 4, { 0x67, 0x8d, 0x04, 0x51 } },
    { { .form = X86_FORM_ADD_RM32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX }, 2, { 0x01, 0xc8 } },
    { { .form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x40 }, 9, { 0x67, 0x62, 0xf1, 0xfe, 0x48, 0x6f, 0x4c, 0x88, 0x01 } },
    { { .form = X86_FORM_VADDPS_Y_Y_YM256, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_R8, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x67, 0xc4, 0xc1, 0x6c, 0x58, 0x08 } },
    { { .form = X86_FORM_VPGATHERDD_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) }, 7, { 0x67, 0xc4, 0xe2, 0x61, 0x90, 0x0c, 0x90 } },
    { { .form = X86_FORM_VGATHERDPS_Z_VM32Z, .reg = 1, .rm = X86_REGISTER_R9, .index = 20, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 8, { 0x67, 0x62, 0xd2, 0x7d, 0x41, 0x92, 0x0c, 0xa1 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x67, 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x08 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM, .reg = 1, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x67, 0xc4, 0xe2, 0x7b, 0x4b, 0x0c, 0x20 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_ADD_M16_R16 + BUSTER_X86_FORM_FS, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 7, { 0x64, 0x67, 0x66, 0xf2, 0xf0, 0x01, 0x08 } },
    { { .form = X86_FORM_PFADD_MM_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x9e }, 5, { 0x67, 0x0f, 0x0f, 0x00, 0x9e } },
    { { .form = X86_FORM_CALL_RM64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x67, 0xff, 0x10 } },
    { { .form = X86_FORM_JMP_FAR_M48, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x67, 0xff, 0x28 } },
    { { .form = X86_FORM_NOTRACK_JMP_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x3e, 0x67, 0xff, 0x20 } },
    { { .form = X86_FORM_NOTRACK_BND_JMP_M64, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x3e, 0x67, 0xf2, 0xff, 0x20 } },
    { { .form = X86_FORM_LOCK_CMPXCHG16B_M128 + BUSTER_X86_FORM_GS, .rm = X86_REGISTER_R8, .mode = BUSTER_X86_MEMORY_BASE }, 7, { 0x65, 0x67, 0xf0, 0x49, 0x0f, 0xc7, 0x08 } },
    { { .form = X86_FORM_CRC32_R32_RM16 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 8, { 0x64, 0x67, 0x66, 0xf2, 0x0f, 0x38, 0xf1, 0x00 } },
    { { .form = X86_FORM_MOVDIR64B_R64_M512, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x67, 0x66, 0x0f, 0x38, 0xf8, 0x00 } },
    { { .form = X86_FORM_PREFETCHIT0_M8, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 8, { 0x67, 0x0f, 0x18, 0x3d, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32 + BUSTER_X86_FORM_GS, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 8, { 0x65, 0x67, 0x8b, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .reg = 1, .rm = X86_REGISTER_RAX }, 4, { 0xf3, 0x0f, 0x1a, 0xc8 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = -0x40 }, 12, { 0x67, 0x62, 0xf1, 0x6c, 0x48, 0x58, 0x0c, 0x25, 0xc0, 0xff, 0xff, 0xff } },
    { { .form = X86_FORM_FSAVE_M108, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x67, 0xdd, 0x30 } },
    { { .form = X86_FORM_LOCK_ADD_M64_IMM32 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12345678, .immediate = 0x12345678 }, 15, { 0x64, 0x67, 0xf0, 0x48, 0x81, 0x84, 0x88, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 } },
};

// The same with APX's registers 16 to 31 (x86_encode_apx), from GNU as.
static const KnownEncoding x86_known_encodings_apx[] = {
    // Map 0: r16 in rm and in reg; add r31, r17 (W); add r16w, ax (66 before D5); mov sil, r16b (SIL with REX2); test
    // r16b, r17b; and the +r forms: mov r16b, 1 (B0+r), mov r31d, 1 (B8+r, B3 too), movabs r16, push r31, pop r17 and
    // xchg r16, rax (90+r)
    { { .form = X86_FORM_ADD_RM32_R32, .rm = 16 }, 4, { 0xd5, 0x10, 0x01, 0xc0 } },
    { { .form = X86_FORM_ADD_RM32_R32, .reg = 16 }, 4, { 0xd5, 0x40, 0x01, 0xc0 } },
    { { .form = X86_FORM_ADD_RM64_R64, .reg = 17, .rm = 31 }, 4, { 0xd5, 0x59, 0x01, 0xcf } },
    { { .form = X86_FORM_ADD_RM16_R16, .rm = 16 }, 5, { 0x66, 0xd5, 0x10, 0x01, 0xc0 } },
    { { .form = X86_FORM_MOV_RM8_R8, .reg = 16, .rm = X86_REGISTER_RSI }, 4, { 0xd5, 0x40, 0x88, 0xc6 } },
    { { .form = X86_FORM_TEST_RM8_R8, .reg = 17, .rm = 16 }, 4, { 0xd5, 0x50, 0x84, 0xc8 } },
    { { .form = X86_FORM_MOV_R8_IMM8, .rm = 16, .immediate = 1 }, 4, { 0xd5, 0x10, 0xb0, 0x01 } },
    { { .form = X86_FORM_MOV_R32_IMM32, .rm = 31, .immediate = 1 }, 7, { 0xd5, 0x11, 0xbf, 0x01, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R64_IMM64, .rm = 16, .immediate = 0x55667788, .displacement = 0x11223344 }, 11, { 0xd5, 0x18, 0xb8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 } },
    { { .form = X86_FORM_PUSH_R64, .rm = 31 }, 3, { 0xd5, 0x11, 0x57 } },
    { { .form = X86_FORM_POP_R64, .rm = 17 }, 3, { 0xd5, 0x10, 0x59 } },
    { { .form = X86_FORM_XCHG_R64_RAX, .rm = 16 }, 3, { 0xd5, 0x18, 0x90 } },
    // Bases: [r16]; [r20] and [r28] with a SIB byte (as RSP's), [r21] and [r29] with an 8-bit zero (as RBP's); [r16 +
    // r17*4 + 8]; [rbx + r20] (X4: an index, not SIB's none); [r28 + r29*8]; an index alone ([r16*4 + 0x10]); lea r16,
    // [r17 + r18]; mov r16, [rip + 0x10]; fs:[r16] (64 before D5); lock add [r16], eax (F0 before D5); and the longest,
    // 15 bytes: lock add qword ptr fs:[r16 + r17*4 + 0x12345678], 0x12345678
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0x18, 0x8b, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 20, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xd5, 0x18, 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 21, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xd5, 0x18, 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 28, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xd5, 0x19, 0x8b, 0x04, 0x24 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 29, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xd5, 0x19, 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 8 }, 6, { 0xd5, 0x38, 0x8b, 0x44, 0x88, 0x08 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = X86_REGISTER_RBX, .index = 20, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 5, { 0xd5, 0x28, 0x8b, 0x04, 0x23 } },
    { { .form = X86_FORM_MOV_R64_RM64, .rm = 28, .index = 29, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3) }, 5, { 0xd5, 0x3b, 0x8b, 0x04, 0xec } },
    { { .form = X86_FORM_MOV_R32_RM32, .index = 16, .mode = BUSTER_X86_MEMORY_INDEX(2), .displacement = 0x10 }, 9, { 0xd5, 0x20, 0x8b, 0x04, 0x85, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_LEA_R64_M, .reg = 16, .rm = 17, .index = 18, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 5, { 0xd5, 0x78, 0x8d, 0x04, 0x11 } },
    { { .form = X86_FORM_MOV_R64_RM64, .reg = 16, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x10 }, 8, { 0xd5, 0x48, 0x8b, 0x05, 0x10, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32 + BUSTER_X86_FORM_FS, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x64, 0xd5, 0x10, 0x8b, 0x00 } },
    { { .form = X86_FORM_LOCK_ADD_M32_R32, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf0, 0xd5, 0x10, 0x01, 0x00 } },
    { { .form = X86_FORM_LOCK_ADD_M64_IMM32 + BUSTER_X86_FORM_FS, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12345678, .immediate = 0x12345678 }, 15, { 0x64, 0xf0, 0xd5, 0x38, 0x81, 0x84, 0x88, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 } },
    // call r16; jmp [r16 + 8]; notrack jmp [r16] (3E before D5); fld dword ptr [r16]; fsave [r16] (9B before D5); imul
    // r16, r17, 100; shl r16, 1; movsxd r16, r17d; mov qword ptr [r31 + r30*8 + 0x12345678], 0x12345678
    { { .form = X86_FORM_CALL_RM64, .rm = 16 }, 4, { 0xd5, 0x10, 0xff, 0xd0 } },
    { { .form = X86_FORM_JMP_RM64, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 5, { 0xd5, 0x10, 0xff, 0x60, 0x08 } },
    { { .form = X86_FORM_NOTRACK_JMP_M64, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x3e, 0xd5, 0x10, 0xff, 0x20 } },
    { { .form = X86_FORM_FLD_M32, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0x10, 0xd9, 0x00 } },
    { { .form = X86_FORM_FSAVE_M108, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x9b, 0xd5, 0x10, 0xdd, 0x30 } },
    { { .form = X86_FORM_IMUL_R64_RM64_IMM8, .reg = 16, .rm = 17, .immediate = 100 }, 5, { 0xd5, 0x58, 0x6b, 0xc1, 0x64 } },
    { { .form = X86_FORM_SHL_RM64_1, .rm = 16 }, 4, { 0xd5, 0x18, 0xd1, 0xe0 } },
    { { .form = X86_FORM_MOVSXD_R64_RM32, .reg = 16, .rm = 17 }, 4, { 0xd5, 0x58, 0x63, 0xc1 } },
    { { .form = X86_FORM_MOV_RM64_IMM32, .rm = 31, .index = 30, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x12345678, .immediate = 0x12345678 }, 13, { 0xd5, 0x3b, 0xc7, 0x84, 0xf7, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 } },
    // Map 1 (M0 replaces 0F): movzx both ways, imul r20, r21, cmovl, sete r16b, bswap r31 (0F C8+r), bt r16, 3, shld,
    // SSE with a general register (66 and F2 before D5), pinsrw, movaps xmm1, [r16], movnti, CR0 and CR8 (R3) with r16,
    // rdrand, ptwrite, cmpxchg16b, bndcl, 3DNow!'s pfadd (whose second 0F stays), lock cmpxchg, tzcnt and popcnt (F3)
    { { .form = X86_FORM_MOVZX_R32_RM8, .reg = 16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0xc0, 0xb6, 0x00 } },
    { { .form = X86_FORM_MOVZX_R32_RM8, .rm = 16 }, 4, { 0xd5, 0x90, 0xb6, 0xc0 } },
    { { .form = X86_FORM_IMUL_R64_RM64, .reg = 20, .rm = 21 }, 4, { 0xd5, 0xd8, 0xaf, 0xe5 } },
    { { .form = X86_FORM_CMOVL_R64_RM64, .reg = 16, .rm = 17, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 5, { 0xd5, 0xd8, 0x4c, 0x41, 0x08 } },
    { { .form = X86_FORM_SETE_RM8, .rm = 16 }, 4, { 0xd5, 0x90, 0x94, 0xc0 } },
    { { .form = X86_FORM_BSWAP_R64, .rm = 31 }, 3, { 0xd5, 0x99, 0xcf } },
    { { .form = X86_FORM_BT_RM64_IMM8, .rm = 16, .immediate = 3 }, 5, { 0xd5, 0x98, 0xba, 0xe0, 0x03 } },
    { { .form = X86_FORM_SHLD_RM64_R64_IMM8, .reg = 17, .rm = 16, .immediate = 5 }, 5, { 0xd5, 0xd8, 0xa4, 0xc8, 0x05 } },
    { { .form = X86_FORM_MOVD_X_RM32, .rm = 16 }, 5, { 0x66, 0xd5, 0x90, 0x6e, 0xc0 } },
    { { .form = X86_FORM_CVTSI2SD_X_RM64, .rm = 16 }, 5, { 0xf2, 0xd5, 0x98, 0x2a, 0xc0 } },
    { { .form = X86_FORM_PINSRW_X_RM32_IMM8, .reg = 1, .rm = 16, .immediate = 2 }, 6, { 0x66, 0xd5, 0x90, 0xc4, 0xc8, 0x02 } },
    { { .form = X86_FORM_MOVAPS_X_XM, .reg = 1, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0x90, 0x28, 0x08 } },
    { { .form = X86_FORM_MOVNTI_M32_R32, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0x90, 0xc3, 0x00 } },
    { { .form = X86_FORM_MOV_R64_CR, .rm = 16 }, 4, { 0xd5, 0x90, 0x20, 0xc0 } },
    { { .form = X86_FORM_MOV_CR_R64, .reg = 8, .rm = 16 }, 4, { 0xd5, 0x94, 0x22, 0xc0 } },
    { { .form = X86_FORM_RDRAND_R64, .rm = 16 }, 4, { 0xd5, 0x98, 0xc7, 0xf0 } },
    { { .form = X86_FORM_PTWRITE_RM64, .rm = 16 }, 5, { 0xf3, 0xd5, 0x98, 0xae, 0xe0 } },
    { { .form = X86_FORM_CMPXCHG16B_M128, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xd5, 0x98, 0xc7, 0x08 } },
    { { .form = X86_FORM_BNDCL_BND_RM64, .rm = 16 }, 5, { 0xf3, 0xd5, 0x90, 0x1a, 0xc0 } },
    { { .form = X86_FORM_PFADD_MM_M64, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x9e }, 5, { 0xd5, 0x90, 0x0f, 0x00, 0x9e } },
    { { .form = X86_FORM_LOCK_CMPXCHG_M32_R32, .reg = 17, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf0, 0xd5, 0xd0, 0xb1, 0x08 } },
    { { .form = X86_FORM_TZCNT_R64_RM64, .reg = 16, .rm = 17 }, 5, { 0xf3, 0xd5, 0xd8, 0xbc, 0xc1 } },
    { { .form = X86_FORM_POPCNT_R64_RM64, .reg = 16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf3, 0xd5, 0xc8, 0xb8, 0x00 } },
    // EVEX: B4 (P0's bit 3) for a base, X4 (P1's bit 2, inverted) for a general index, both, a SIB byte for R20, R20 as an
    // index (not SIB's none), R', V', X, B, B4 and X4 at once; a general register rm (B4, not X), reg (R') and vvvv (V',
    // in AMX-AVX512's tcvtrowd2ps); VSIB with B4, and with V' for its index; and FS before 62
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf9, 0x6c, 0x48, 0x58, 0x08 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) }, 7, { 0x62, 0xf1, 0x68, 0x48, 0x58, 0x0c, 0x88 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 64 }, 8, { 0x62, 0xf9, 0x68, 0x48, 0x58, 0x4c, 0x88, 0x01 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 20, .mode = BUSTER_X86_MEMORY_BASE }, 7, { 0x62, 0xf9, 0x6c, 0x48, 0x58, 0x0c, 0x24 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = X86_REGISTER_RAX, .index = 20, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xf1, 0x68, 0x48, 0x58, 0x0c, 0x20 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 17, .vvvv = 18, .rm = 31, .index = 30, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3) }, 7, { 0x62, 0x89, 0x68, 0x40, 0x58, 0x0c, 0xf7 } },
    { { .form = X86_FORM_VMOVD_X_RM32_EVEX, .reg = 1, .rm = 16 }, 6, { 0x62, 0xf9, 0x7d, 0x08, 0x6e, 0xc8 } },
    { { .form = X86_FORM_VMOVD_RM32_X_EVEX, .reg = 1, .rm = 16 }, 6, { 0x62, 0xf9, 0x7d, 0x08, 0x7e, 0xc8 } },
    { { .form = X86_FORM_VMOVQ_X_RM64_EVEX, .reg = 17, .rm = 31 }, 6, { 0x62, 0xc9, 0xfd, 0x08, 0x6e, 0xcf } },
    { { .form = X86_FORM_VPBROADCASTD_Z_R32, .reg = 1, .rm = 16 }, 6, { 0x62, 0xfa, 0x7d, 0x48, 0x7c, 0xc8 } },
    { { .form = X86_FORM_VCVTSI2SS_X_X_RM32_EVEX, .reg = 1, .vvvv = 2, .rm = 16 }, 6, { 0x62, 0xf9, 0x6e, 0x08, 0x2a, 0xc8 } },
    { { .form = X86_FORM_VPINSRD_X_X_RM32_IMM8_EVEX, .reg = 1, .vvvv = 2, .rm = 16, .immediate = 1 }, 7, { 0x62, 0xfb, 0x6d, 0x08, 0x22, 0xc8, 0x01 } },
    { { .form = X86_FORM_VPEXTRD_RM32_X_IMM8_EVEX, .reg = 1, .rm = 16, .immediate = 1 }, 7, { 0x62, 0xfb, 0x7d, 0x08, 0x16, 0xc8, 0x01 } },
    { { .form = X86_FORM_VCVTTSS2SI_R64_XM32_EVEX, .reg = 16, .rm = 1 }, 6, { 0x62, 0xe1, 0xfe, 0x08, 0x2c, 0xc1 } },
    { { .form = X86_FORM_VCVTTSS2SI_R64_XM32_EVEX, .reg = 31, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0x69, 0xfe, 0x08, 0x2c, 0x38 } },
    { { .form = X86_FORM_TCVTROWD2PS_Z_TMM_R32, .reg = 1, .rm = 2, .vvvv = 16 }, 6, { 0x62, 0xf2, 0x7e, 0x40, 0x4a, 0xca } },
    { { .form = X86_FORM_VGATHERDPS_Z_VM32Z, .reg = 1, .rm = 16, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 7, { 0x62, 0xfa, 0x7d, 0x49, 0x92, 0x0c, 0x90 } },
    { { .form = X86_FORM_VGATHERDPS_Z_VM32Z, .reg = 1, .rm = 31, .index = 18, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 7, { 0x62, 0xda, 0x7d, 0x41, 0x92, 0x0c, 0x97 } },
    { { .form = X86_FORM_VPSCATTERDD_VM32Z_Z, .reg = 1, .rm = 16, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .decorations = 1 }, 7, { 0x62, 0xfa, 0x7d, 0x49, 0xa0, 0x0c, 0x90 } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512 + BUSTER_X86_FORM_FS, .reg = 1, .vvvv = 2, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 7, { 0x64, 0x62, 0xf9, 0x6c, 0x48, 0x58, 0x08 } },
    // APX's EVEX forms of general register instructions: andn r16d, eax, ecx (R'); blsi r16d, ecx (V'); blsi ecx, r17d
    // (B4); {nf} blsi ecx, dword ptr [r17+r18*4+0x1000] (B4 and X4); bextr eax, r20d, r21d; shlx r16, r17, r18; rorx
    // r16d, ecx, 7; kmovw k1, r16d; kmovq r31, k7; kmovb k1, byte ptr [r16]; crc32 eax, r16b and crc32 r16d, eax; adox
    // r16, rcx; adcx r16, r17, qword ptr [r18+r19*8] (ND); {nf} popcnt r16w, r17w; lzcnt r31, qword ptr [r20]; invpcid
    // r16, [r17]; movdir64b r16, [r17]; movbe r16d, dword ptr [rax] and movbe r17, r16; movrs r16b, byte ptr [rax];
    // cmpbexadd dword ptr [r16+r17], r18d, r19d; tileloadd tmm1, [r16+r17*2+0x100] and [rax+r17*1+64] (X4 for a SIB
    // operand's index), [rax+r20*1] (R20, not SIB's none) and [r20] (SIB's none after R20); t2rpntlvwz0rs tmm2,
    // [r16+rcx*1+64]; ldtilecfg [r16+64]; urdmsr r16, 0x12345678 and urdmsr r16, r17; aadd qword ptr [r16+r17], r18;
    // wrssd dword ptr [r16], r17d; and movdiri qword ptr [r31], r30
    { { .form = X86_FORM_ANDN_R32_R32_RM32_EVEX, .reg = 16, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xe2, 0x7c, 0x08, 0xf2, 0xc1 } },
    { { .form = X86_FORM_BLSI_R32_RM32_EVEX, .vvvv = 16, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xf2, 0x7c, 0x00, 0xf3, 0xd9 } },
    { { .form = X86_FORM_BLSI_R32_RM32_EVEX, .vvvv = X86_REGISTER_RCX, .rm = 17 }, 6, { 0x62, 0xfa, 0x74, 0x08, 0xf3, 0xd9 } },
    { { .form = X86_FORM_BLSI_R32_RM32_NF, .vvvv = X86_REGISTER_RCX, .rm = 17, .index = 18, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x1000 }, 11, { 0x62, 0xfa, 0x70, 0x0c, 0xf3, 0x9c, 0x91, 0x00, 0x10, 0x00, 0x00 } },
    { { .form = X86_FORM_BEXTR_R32_RM32_R32_EVEX, .reg = X86_REGISTER_RAX, .rm = 20, .vvvv = 21 }, 6, { 0x62, 0xfa, 0x54, 0x00, 0xf7, 0xc4 } },
    { { .form = X86_FORM_SHLX_R64_RM64_R64_EVEX, .reg = 16, .rm = 17, .vvvv = 18 }, 6, { 0x62, 0xea, 0xed, 0x00, 0xf7, 0xc1 } },
    { { .form = X86_FORM_RORX_R32_RM32_IMM8_EVEX, .reg = 16, .rm = X86_REGISTER_RCX, .immediate = 7 }, 7, { 0x62, 0xe3, 0x7f, 0x08, 0xf0, 0xc1, 0x07 } },
    { { .form = X86_FORM_KMOVW_K_R32_EVEX, .reg = 1, .rm = 16 }, 6, { 0x62, 0xf9, 0x7c, 0x08, 0x92, 0xc8 } },
    { { .form = X86_FORM_KMOVQ_R64_K_EVEX, .reg = 31, .rm = 7 }, 6, { 0x62, 0x61, 0xff, 0x08, 0x93, 0xff } },
    { { .form = X86_FORM_KMOVB_K_M8_EVEX, .reg = 1, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xf9, 0x7d, 0x08, 0x90, 0x08 } },
    { { .form = X86_FORM_CRC32_R32_RM8_EVEX, .reg = X86_REGISTER_RAX, .rm = 16 }, 6, { 0x62, 0xfc, 0x7c, 0x08, 0xf0, 0xc0 } },
    { { .form = X86_FORM_CRC32_R32_RM32_EVEX, .reg = 16, .rm = X86_REGISTER_RAX }, 6, { 0x62, 0xe4, 0x7c, 0x08, 0xf1, 0xc0 } },
    { { .form = X86_FORM_ADOX_R64_RM64_EVEX, .reg = 16, .rm = X86_REGISTER_RCX }, 6, { 0x62, 0xe4, 0xfe, 0x08, 0x66, 0xc1 } },
    { { .form = X86_FORM_ADCX_R64_R64_RM64, .vvvv = 16, .reg = 17, .rm = 18, .index = 19, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3) }, 7, { 0x62, 0xec, 0xf9, 0x10, 0x66, 0x0c, 0xda } },
    { { .form = X86_FORM_POPCNT_R16_RM16_NF, .reg = 16, .rm = 17 }, 6, { 0x62, 0xec, 0x7d, 0x0c, 0x88, 0xc1 } },
    { { .form = X86_FORM_LZCNT_R64_RM64_EVEX, .reg = 31, .rm = 20, .mode = BUSTER_X86_MEMORY_BASE }, 7, { 0x62, 0x6c, 0xfc, 0x08, 0xf5, 0x3c, 0x24 } },
    { { .form = X86_FORM_INVPCID_R64_M128_EVEX, .reg = 16, .rm = 17, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xec, 0x7e, 0x08, 0xf2, 0x01 } },
    { { .form = X86_FORM_MOVDIR64B_R64_M512_EVEX, .reg = 16, .rm = 17, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xec, 0x7d, 0x08, 0xf8, 0x01 } },
    { { .form = X86_FORM_MOVBE_R32_RM32, .reg = 16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xe4, 0x7c, 0x08, 0x60, 0x00 } },
    { { .form = X86_FORM_MOVBE_R64_RM64, .reg = 17, .rm = 16 }, 6, { 0x62, 0xec, 0xfc, 0x08, 0x60, 0xc8 } },
    { { .form = X86_FORM_MOVRS_R8_M8_EVEX, .reg = 16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xe4, 0x7c, 0x08, 0x8a, 0x00 } },
    { { .form = X86_FORM_CMPBEXADD_M32_R32_R32_EVEX, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .reg = 18, .vvvv = 19 }, 7, { 0x62, 0xea, 0x61, 0x00, 0xe6, 0x14, 0x08 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM_EVEX, .reg = 1, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1), .displacement = 0x100 }, 11, { 0x62, 0xfa, 0x7b, 0x08, 0x4b, 0x8c, 0x48, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 64 }, 8, { 0x62, 0xf2, 0x7b, 0x08, 0x4b, 0x4c, 0x08, 0x40 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM_EVEX, .reg = 1, .rm = X86_REGISTER_RAX, .index = 20, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xf2, 0x7b, 0x08, 0x4b, 0x0c, 0x20 } },
    { { .form = X86_FORM_TILELOADD_TMM_SIBMEM_EVEX, .reg = 1, .rm = 20, .index = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xfa, 0x7f, 0x08, 0x4b, 0x0c, 0x24 } },
    { { .form = X86_FORM_T2RPNTLVWZ0RS_TMM_SIBMEM_EVEX, .reg = 2, .rm = 16, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 64 }, 8, { 0x62, 0xfd, 0x7c, 0x08, 0xf8, 0x54, 0x08, 0x40 } },
    { { .form = X86_FORM_LDTILECFG_M512_EVEX, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 }, 7, { 0x62, 0xfa, 0x7c, 0x08, 0x49, 0x40, 0x40 } },
    { { .form = X86_FORM_URDMSR_R64_IMM32_EVEX, .rm = 16, .immediate = 0x12345678 }, 10, { 0x62, 0xff, 0x7f, 0x08, 0xf8, 0xc0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_URDMSR_R64_R64_EVEX, .rm = 16, .reg = 17 }, 6, { 0x62, 0xec, 0x7f, 0x08, 0xf8, 0xc8 } },
    { { .form = X86_FORM_AADD_M64_R64_EVEX, .rm = 16, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .reg = 18 }, 7, { 0x62, 0xec, 0xf8, 0x08, 0xfc, 0x14, 0x08 } },
    { { .form = X86_FORM_WRSSD_M32_R32_EVEX, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE, .reg = 17 }, 6, { 0x62, 0xec, 0x7c, 0x08, 0x66, 0x08 } },
    { { .form = X86_FORM_MOVDIRI_M64_R64_EVEX, .rm = 31, .mode = BUSTER_X86_MEMORY_BASE, .reg = 30 }, 6, { 0x62, 0x4c, 0xfc, 0x08, 0xf9, 0x37 } },
    // APX's map 4 forms of the legacy arithmetic: add r16d, r17d, r18d both ways; add r31, qword ptr [r20+r21*8+0x80],
    // r22 (V', R', B4 and X4); {nf} sub r16, r17, 0x12345678; {nf} xor r16b, r17b; or r18w, 0x1234; sar r16d, r17d,
    // cl; {nf} shl r20, r21, 3; rcr r16b, 1; inc r16, r17; {nf} neg r31d, dword ptr [r16]; not r20; {nf} mul r16; idiv
    // byte ptr [r17+r18]; imul r16, r17, r18; {nf} imul r16d, dword ptr [r17], 0x12345678; imulzu r16w, r17w, 5; shld
    // r16d, r17d, r18d, 7; {nf} shrd r16, r17, r18, cl; cmovl r16, r17, r18; cmovge r16d, r17d, dword ptr [rip+0x100];
    // setb r16b; and setzug r17b
    { { .form = X86_FORM_ADD_R32_RM32_R32, .vvvv = 16, .rm = 17, .reg = 18 }, 6, { 0x62, 0xec, 0x7c, 0x10, 0x01, 0xd1 } },
    { { .form = X86_FORM_ADD_R32_R32_RM32, .vvvv = 16, .reg = 17, .rm = 18 }, 6, { 0x62, 0xec, 0x7c, 0x10, 0x03, 0xca } },
    { { .form = X86_FORM_ADD_R64_RM64_R64, .vvvv = 31, .rm = 20, .index = 21, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3), .displacement = 0x80, .reg = 22 }, 11, { 0x62, 0xec, 0x80, 0x10, 0x01, 0xb4, 0xec, 0x80, 0x00, 0x00, 0x00 } },
    { { .form = X86_FORM_SUB_R64_RM64_IMM32_NF, .vvvv = 16, .rm = 17, .immediate = 0x12345678 }, 10, { 0x62, 0xfc, 0xfc, 0x14, 0x81, 0xe9, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_XOR_RM8_R8_NF, .rm = 16, .reg = 17 }, 6, { 0x62, 0xec, 0x7c, 0x0c, 0x30, 0xc8 } },
    { { .form = X86_FORM_OR_RM16_IMM16_EVEX, .rm = 18, .immediate = 0x1234 }, 8, { 0x62, 0xfc, 0x7d, 0x08, 0x81, 0xca, 0x34, 0x12 } },
    { { .form = X86_FORM_SAR_R32_RM32_CL, .vvvv = 16, .rm = 17 }, 6, { 0x62, 0xfc, 0x7c, 0x10, 0xd3, 0xf9 } },
    { { .form = X86_FORM_SHL_R64_RM64_IMM8_NF, .vvvv = 20, .rm = 21, .immediate = 3 }, 7, { 0x62, 0xfc, 0xdc, 0x14, 0xc1, 0xe5, 0x03 } },
    { { .form = X86_FORM_RCR_RM8_1_EVEX, .rm = 16 }, 6, { 0x62, 0xfc, 0x7c, 0x08, 0xd0, 0xd8 } },
    { { .form = X86_FORM_INC_R64_RM64, .vvvv = 16, .rm = 17 }, 6, { 0x62, 0xfc, 0xfc, 0x10, 0xff, 0xc1 } },
    { { .form = X86_FORM_NEG_R32_RM32_NF, .vvvv = 31, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x62, 0xfc, 0x04, 0x14, 0xf7, 0x18 } },
    { { .form = X86_FORM_NOT_RM64_EVEX, .rm = 20 }, 6, { 0x62, 0xfc, 0xfc, 0x08, 0xf7, 0xd4 } },
    { { .form = X86_FORM_MUL_RM64_NF, .rm = 16 }, 6, { 0x62, 0xfc, 0xfc, 0x0c, 0xf7, 0xe0 } },
    { { .form = X86_FORM_IDIV_RM8_EVEX, .rm = 17, .index = 18, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xfc, 0x78, 0x08, 0xf6, 0x3c, 0x11 } },
    { { .form = X86_FORM_IMUL_R64_R64_RM64, .vvvv = 16, .reg = 17, .rm = 18 }, 6, { 0x62, 0xec, 0xfc, 0x10, 0xaf, 0xca } },
    { { .form = X86_FORM_IMUL_R32_RM32_IMM32_NF, .reg = 16, .rm = 17, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 0x12345678 }, 10, { 0x62, 0xec, 0x7c, 0x0c, 0x69, 0x01, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_IMULZU_R16_RM16_IMM8, .reg = 16, .rm = 17, .immediate = 5 }, 7, { 0x62, 0xec, 0x7d, 0x18, 0x6b, 0xc1, 0x05 } },
    { { .form = X86_FORM_SHLD_R32_RM32_R32_IMM8, .vvvv = 16, .rm = 17, .reg = 18, .immediate = 7 }, 7, { 0x62, 0xec, 0x7c, 0x10, 0x24, 0xd1, 0x07 } },
    { { .form = X86_FORM_SHRD_R64_RM64_R64_CL_NF, .vvvv = 16, .rm = 17, .reg = 18 }, 6, { 0x62, 0xec, 0xfc, 0x14, 0xad, 0xd1 } },
    { { .form = X86_FORM_CMOVL_R64_R64_RM64, .vvvv = 16, .reg = 17, .rm = 18 }, 6, { 0x62, 0xec, 0xfc, 0x10, 0x4c, 0xca } },
    { { .form = X86_FORM_CMOVGE_R32_R32_RM32, .vvvv = 16, .reg = 17, .mode = BUSTER_X86_MEMORY_RIP, .displacement = 0x100 }, 10, { 0x62, 0xe4, 0x7c, 0x10, 0x4d, 0x0d, 0x00, 0x01, 0x00, 0x00 } },
    { { .form = X86_FORM_SETB_RM8_EVEX, .rm = 16 }, 6, { 0x62, 0xfc, 0x7f, 0x08, 0x42, 0xc0 } },
    { { .form = X86_FORM_SETZUG_R8, .rm = 17 }, 6, { 0x62, 0xfc, 0x7f, 0x18, 0x4f, 0xc1 } },
    // APX's new instructions: ccmpg {dfv=sf} r16, r17 (GNU as's ccmpnle); {load} ccmpae r16b, r17b and ccmpae {dfv=of}
    // r16b, byte ptr [r17+r18*2] (B4 and X4 beside the default flags value); ctests {dfv=zf} r16b, r17b; cfcmovl r16d,
    // r17d, dword ptr [r18+r19]; push2 r16, r17, push2 r30, r31, pop2 r31, r30 and pop2p r16, r17
    { { .form = X86_FORM_CCMPG_RM64_R64, .vvvv = 4, .rm = 16, .reg = 17 }, 6, { 0x62, 0xec, 0xa4, 0x0f, 0x39, 0xc8 } },
    { { .form = X86_FORM_CCMPAE_R8_RM8, .reg = 16, .rm = 17 }, 6, { 0x62, 0xec, 0x04, 0x03, 0x3a, 0xc1 } },
    { { .form = X86_FORM_CCMPAE_R8_RM8, .vvvv = 8, .reg = 16, .rm = 17, .index = 18, .mode = BUSTER_X86_MEMORY_BASE_INDEX(1) }, 7, { 0x62, 0xec, 0x40, 0x03, 0x3a, 0x04, 0x51 } },
    { { .form = X86_FORM_CTESTS_RM8_R8, .vvvv = 2, .rm = 16, .reg = 17 }, 6, { 0x62, 0xec, 0x14, 0x08, 0x84, 0xc8 } },
    { { .form = X86_FORM_CFCMOVL_R32_R32_RM32, .vvvv = 16, .reg = 17, .rm = 18, .index = 19, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 7, { 0x62, 0xec, 0x78, 0x14, 0x4c, 0x0c, 0x1a } },
    { { .form = X86_FORM_PUSH2_R64_R64, .vvvv = 16, .rm = 17 }, 6, { 0x62, 0xfc, 0x7c, 0x10, 0xff, 0xf1 } },
    { { .form = X86_FORM_PUSH2_R64_R64, .vvvv = 30, .rm = 31 }, 6, { 0x62, 0xdc, 0x0c, 0x10, 0xff, 0xf7 } },
    { { .form = X86_FORM_POP2_R64_R64, .vvvv = 31, .rm = 30 }, 6, { 0x62, 0xdc, 0x04, 0x10, 0x8f, 0xc6 } },
    { { .form = X86_FORM_POP2P_R64_R64, .vvvv = 16, .rm = 17 }, 6, { 0x62, 0xfc, 0xfc, 0x10, 0x8f, 0xc1 } },
    // And those that always have REX2, here REX's position's D5 and map 0's payload: pushp r16 and r31 (B4 and B3),
    // popp r20 and popp rax (REX2 without registers 16 to 31), and jmpabs 0x123456789abcdef0
    { { .form = X86_FORM_PUSHP_R64, .rm = 16 }, 3, { 0xd5, 0x18, 0x50 } },
    { { .form = X86_FORM_PUSHP_R64, .rm = 31 }, 3, { 0xd5, 0x19, 0x57 } },
    { { .form = X86_FORM_POPP_R64, .rm = 20 }, 3, { 0xd5, 0x18, 0x5c } },
    { { .form = X86_FORM_POPP_R64, .rm = X86_REGISTER_RAX }, 3, { 0xd5, 0x08, 0x58 } },
    { { .form = X86_FORM_JMPABS_IMM64, .immediate = (int32_t)0x9abcdef0, .displacement = 0x12345678 }, 11, { 0xd5, 0x00, 0xa1, 0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    // Without registers 16 to 31, as for x86_encode: REX's add rax, r8; VEX and EVEX; map 2's crc32; xsave [r15]
    { { .form = X86_FORM_ADD_RM64_R64, .reg = 8 }, 3, { 0x4c, 0x01, 0xc0 } },
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xe8, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xcb } },
    { { .form = X86_FORM_CRC32_R32_RM16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 6, { 0x66, 0xf2, 0x0f, 0x38, 0xf1, 0x00 } },
    { { .form = X86_FORM_XSAVE_M, .rm = X86_REGISTER_R15, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x41, 0x0f, 0xae, 0x27 } },
};

// Known encodings in 32-bit mode (x86_encode_scalar_32), with bytes from GNU as's .code32: the mode's operand and
// address sizes and the other ones', and its own forms.
static const KnownEncoding x86_known_encodings_32[] = {
    // 32- and 16-bit operands, and a high byte register: add eax, ebx; add ax, bx; add dl, ah
    { { .form = X86_FORM_ADD_RM32_R32, .reg = X86_REGISTER_RBX }, 2, { 0x01, 0xd8 } },
    { { .form = X86_FORM_ADD_RM16_R16, .reg = X86_REGISTER_RBX }, 3, { 0x66, 0x01, 0xd8 } },
    { { .form = X86_FORM_ADD_RM8_R8, .reg = X86_REGISTER_AH, .rm = X86_REGISTER_RDX }, 2, { 0x00, 0xe2 } },
    // 32-bit addresses: mov bh, byte ptr [esi]; mov eax, dword ptr [ebp]; mov eax, dword ptr [esp+8]; mov eax, dword
    // ptr [eax+ecx*4+305419896]; mov ecx, dword ptr ds:305419896; mov ecx, dword ptr [edx*8+16]
    { { .form = X86_FORM_MOV_R8_RM8, .reg = X86_REGISTER_BH, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8a, 0x3e } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x8b, 0x45, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 }, 4, { 0x8b, 0x44, 0x24, 0x08 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2), .displacement = 0x12345678 }, 7, { 0x8b, 0x84, 0x88, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x12345678 }, 6, { 0x8b, 0x0d, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .index = X86_REGISTER_RDX, .mode = BUSTER_X86_MEMORY_INDEX(3), .displacement = 16 }, 7, { 0x8b, 0x0c, 0xd5, 0x10, 0x00, 0x00, 0x00 } },
    // 16-bit addresses (a 67), [bx+65535] as [bx-1]: mov ax, word ptr [bx+si]; mov ecx, dword ptr [bp]; addr16 mov ecx,
    // dword ptr ds:4660; mov ecx, dword ptr [bx+di-2]; mov ecx, dword ptr [bp+si+4660]; lea eax, [bx+65535]
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) | BUSTER_X86_MODE_ADDRESS_SIZE }, 4, { 0x67, 0x66, 0x8b, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 4, { 0x67, 0x8b, 0x4e, 0x00 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 0x1234 }, 5, { 0x67, 0x8b, 0x0e, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = -2 }, 4, { 0x67, 0x8b, 0x49, 0xfe } },
    { { .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 0x1234 }, 5, { 0x67, 0x8b, 0x8a, 0x34, 0x12 } },
    { { .form = X86_FORM_LEA_R32_M, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 0xffff }, 4, { 0x67, 0x8d, 0x47, 0xff } },
    // Pushes and pops: push eax; pop ebx; push dword ptr [eax]; pop dword ptr [ecx+4]; push ax; push 5; push 305419896;
    // pushd -2; pushd 305419896; pushw 4660; pushfd; popfd; pushfw
    { { .form = X86_FORM_PUSH_R32, .rm = X86_REGISTER_RAX }, 1, { 0x50 } },
    { { .form = X86_FORM_POP_R32, .rm = X86_REGISTER_RBX }, 1, { 0x5b } },
    { { .form = X86_FORM_PUSH_RM32, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x30 } },
    { { .form = X86_FORM_POP_RM32, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 4 }, 3, { 0x8f, 0x41, 0x04 } },
    { { .form = X86_FORM_PUSH_R16, .rm = X86_REGISTER_RAX }, 2, { 0x66, 0x50 } },
    { { .form = X86_FORM_PUSH_IMM8, .immediate = 5 }, 2, { 0x6a, 0x05 } },
    { { .form = X86_FORM_PUSH_IMM32, .immediate = 0x12345678 }, 5, { 0x68, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_PUSHD_IMM8, .immediate = -2 }, 2, { 0x6a, 0xfe } },
    { { .form = X86_FORM_PUSHD_IMM32, .immediate = 0x12345678 }, 5, { 0x68, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_PUSHW_IMM16, .immediate = 0x1234 }, 4, { 0x66, 0x68, 0x34, 0x12 } },
    { { .form = X86_FORM_PUSHFD }, 1, { 0x9c } },
    { { .form = X86_FORM_POPFD }, 1, { 0x9d } },
    { { .form = X86_FORM_PUSHFW }, 2, { 0x66, 0x9c } },
    // Branches: call eax; jmp dword ptr [ebx]; call .+305419901; data16 call .+4664; {disp32} jne .+305419902; {disp32}
    // data16 jne .-4655; {disp32} cs jne .+4103; {disp32} bnd jmp .+4102
    { { .form = X86_FORM_CALL_RM32, .rm = X86_REGISTER_RAX }, 2, { 0xff, 0xd0 } },
    { { .form = X86_FORM_JMP_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x23 } },
    { { .form = X86_FORM_CALL_REL32, .displacement = 0x12345678 }, 5, { 0xe8, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_CALL_REL16, .displacement = 0x1234 }, 4, { 0x66, 0xe8, 0x34, 0x12 } },
    { { .form = X86_FORM_JNE_REL32, .displacement = 0x12345678 }, 6, { 0x0f, 0x85, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JNE_REL16, .displacement = -0x1234 }, 5, { 0x66, 0x0f, 0x85, 0xcc, 0xed } },
    { { .form = X86_FORM_JNE_REL32_NOT_TAKEN, .displacement = 0x1000 }, 7, { 0x2e, 0x0f, 0x85, 0x00, 0x10, 0x00, 0x00 } },
    { { .form = X86_FORM_BND_JMP_REL32, .displacement = 0x1000 }, 6, { 0xf2, 0xe9, 0x00, 0x10, 0x00, 0x00 } },
    // The addresses an instruction makes itself, of either size: jecxz .+18; jcxz .-13; addr16 loop .+1; movsd; addr16
    // movsb; addr16 rep stosd; addr16 xlatb; addr16 maskmovq mm0, mm1
    { { .form = X86_FORM_JRCXZ_REL8, .displacement = 16 }, 2, { 0xe3, 0x10 } },
    { { .form = X86_FORM_JRCXZ_REL8, .displacement = -16, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 3, { 0x67, 0xe3, 0xf0 } },
    { { .form = X86_FORM_LOOP_REL8, .displacement = -2, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 3, { 0x67, 0xe2, 0xfe } },
    { { .form = X86_FORM_MOVS_D }, 1, { 0xa5 } },
    { { .form = X86_FORM_MOVS_B, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 2, { 0x67, 0xa4 } },
    { { .form = X86_FORM_REP_STOS_D, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 3, { 0x67, 0xf3, 0xab } },
    { { .form = X86_FORM_XLATB, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 2, { 0x67, 0xd7 } },
    { { .form = X86_FORM_MASKMOVQ_MM_MM, .reg = 0, .rm = 1, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 4, { 0x67, 0x0f, 0xf7, 0xc1 } },
    // Control, debug, test and segment registers, and CR8 with AMD's LOCK prefix: mov cr0, eax; mov eax, cr3; mov dr7,
    // ecx; mov eax, ds; mov cr8, eax; mov eax, cr8; mov eax, tr6; mov tr7, edx
    { { .form = X86_FORM_MOV_CR_R32, .reg = 0, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x22, 0xc0 } },
    { { .form = X86_FORM_MOV_R32_CR, .reg = 3, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x20, 0xd8 } },
    { { .form = X86_FORM_MOV_DR_R32, .reg = 7, .rm = X86_REGISTER_RCX }, 3, { 0x0f, 0x23, 0xf9 } },
    { { .form = X86_FORM_MOV_R32_SREG, .reg = 3, .rm = X86_REGISTER_RAX }, 2, { 0x8c, 0xd8 } },
    { { .form = X86_FORM_MOV_CR_R32, .reg = 8, .rm = X86_REGISTER_RAX }, 4, { 0xf0, 0x0f, 0x22, 0xc0 } },
    { { .form = X86_FORM_MOV_R32_CR, .reg = 8, .rm = X86_REGISTER_RAX }, 4, { 0xf0, 0x0f, 0x20, 0xc0 } },
    { { .form = X86_FORM_MOV_R32_TR, .reg = 6, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x24, 0xf0 } },
    { { .form = X86_FORM_MOV_TR_R32, .reg = 7, .rm = X86_REGISTER_RDX }, 3, { 0x0f, 0x26, 0xfa } },
    // VEX and EVEX, with a 16-bit address's disp8*N too: vaddps xmm1, xmm2, xmm3; vaddps zmm1{k1}{z}, zmm2, zmm3;
    // vmovaps zmm0, zmmword ptr [bx+192]; vpgatherdd xmm1, dword ptr [eax+xmm2*4], xmm3
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xe8, 0x58, 0xcb } },
    { { .form = X86_FORM_VADDPS_Z_Z_ZM512, .reg = 1, .vvvv = 2, .rm = 3, .decorations = 1 | BUSTER_X86_DECORATION_ZEROING }, 6, { 0x62, 0xf1, 0x6c, 0xc9, 0x58, 0xcb } },
    { { .form = X86_FORM_VMOVAPS_Z_ZM512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 192 }, 8, { 0x67, 0x62, 0xf1, 0x7c, 0x48, 0x28, 0x47, 0x03 } },
    { { .form = X86_FORM_VPGATHERDD_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) }, 6, { 0xc4, 0xe2, 0x61, 0x90, 0x0c, 0x90 } },
    // The x87 environment: fnstenv [eax]; fnstenvw [eax]; fstenv [eax]
    { { .form = X86_FORM_FNSTENV_M28, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xd9, 0x30 } },
    { { .form = X86_FORM_FNSTENVW_M14, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xd9, 0x30 } },
    { { .form = X86_FORM_FSTENV_M28, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x9b, 0xd9, 0x30 } },
    // The forms with fixed sizes, and prefixes: adcx eax, ebx; movnti dword ptr [eax], ecx; crc32 eax, bl; lock add
    // dword ptr fs:[eax], 1; xacquire lock add word ptr [ebx], 5; cmpxchg8b qword ptr [esi]
    { { .form = X86_FORM_ADCX_R32_RM32, .rm = X86_REGISTER_RBX }, 5, { 0x66, 0x0f, 0x38, 0xf6, 0xc3 } },
    { { .form = X86_FORM_MOVNTI_M32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0xc3, 0x08 } },
    { { .form = X86_FORM_CRC32_R32_RM8, .rm = X86_REGISTER_RBX }, 5, { 0xf2, 0x0f, 0x38, 0xf0, 0xc3 } },
    { { .form = X86_FORM_LOCK_ADD_M32_IMM8 + BUSTER_X86_FORM_FS, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 1 }, 5, { 0x64, 0xf0, 0x83, 0x00, 0x01 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_ADD_M16_IMM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 6, { 0x66, 0xf2, 0xf0, 0x83, 0x03, 0x05 } },
    { { .form = X86_FORM_CMPXCHG8B_M64, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0xc7, 0x0e } },
    // Far branches, returns and the rest: lcall [ebx]; data16 lcall [ebx]; retfd; iretd; iretw; cwde; cbw; in eax, dx;
    // in ax, dx; sysexit; enter 16, 1; enterw 16, 1; push fs; pushw fs; bswap edx
    { { .form = X86_FORM_CALL_FAR_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x1b } },
    { { .form = X86_FORM_CALL_FAR_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xff, 0x1b } },
    { { .form = X86_FORM_RETFD }, 1, { 0xcb } },
    { { .form = X86_FORM_IRETD }, 1, { 0xcf } },
    { { .form = X86_FORM_IRET }, 2, { 0x66, 0xcf } },
    { { .form = X86_FORM_CWDE }, 1, { 0x98 } },
    { { .form = X86_FORM_CBW }, 2, { 0x66, 0x98 } },
    { { .form = X86_FORM_IN_EAX_DX }, 1, { 0xed } },
    { { .form = X86_FORM_IN_AX_DX }, 2, { 0x66, 0xed } },
    { { .form = X86_FORM_SYSEXIT }, 2, { 0x0f, 0x35 } },
    { { .form = X86_FORM_ENTER, .immediate = 16 | 1 << 16 }, 4, { 0xc8, 0x10, 0x00, 0x01 } },
    { { .form = X86_FORM_ENTERW, .immediate = 16 | 1 << 16 }, 5, { 0x66, 0xc8, 0x10, 0x00, 0x01 } },
    { { .form = X86_FORM_PUSH_FS }, 2, { 0x0f, 0xa0 } },
    { { .form = X86_FORM_PUSHW_FS }, 3, { 0x66, 0x0f, 0xa0 } },
    { { .form = X86_FORM_BSWAP_R32, .rm = X86_REGISTER_RDX }, 2, { 0x0f, 0xca } },
    // What 64-bit mode doesn't have: the BCD adjustments, salc and into: aaa; aas; daa; das; aam 10; aad 7; salc; into
    { { .form = X86_FORM_AAA }, 1, { 0x37 } },
    { { .form = X86_FORM_AAS }, 1, { 0x3f } },
    { { .form = X86_FORM_DAA }, 1, { 0x27 } },
    { { .form = X86_FORM_DAS }, 1, { 0x2f } },
    { { .form = X86_FORM_AAM_IMM8, .immediate = 10 }, 2, { 0xd4, 0x0a } },
    { { .form = X86_FORM_AAD_IMM8, .immediate = 7 }, 2, { 0xd5, 0x07 } },
    { { .form = X86_FORM_SALC }, 1, { 0xd6 } },
    { { .form = X86_FORM_INTO }, 1, { 0xce } },
    // bound, arpl, lds and les: bound eax, qword ptr [ebx]; bound ax, dword ptr [ebx]; arpl word ptr [ebx], ax; arpl
    // cx, ax; lds eax, [ebx]; les ax, dword ptr [ebx]
    { { .form = X86_FORM_BOUND_R32_M64, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x62, 0x03 } },
    { { .form = X86_FORM_BOUND_R16_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0x62, 0x03 } },
    { { .form = X86_FORM_ARPL_RM16_R16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x63, 0x03 } },
    { { .form = X86_FORM_ARPL_RM16_R16, .rm = X86_REGISTER_RCX }, 2, { 0x63, 0xc1 } },
    { { .form = X86_FORM_LDS_R32_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xc5, 0x03 } },
    { { .form = X86_FORM_LES_R16_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xc4, 0x03 } },
    // pusha and popa, and pushes and pops of segment registers: pushad; pushaw; popad; popaw; pushd es; pushw es; pushd
    // cs; popd ds; popw ss; pushd fs
    { { .form = X86_FORM_PUSHAD }, 1, { 0x60 } },
    { { .form = X86_FORM_PUSHAW }, 2, { 0x66, 0x60 } },
    { { .form = X86_FORM_POPAD }, 1, { 0x61 } },
    { { .form = X86_FORM_POPAW }, 2, { 0x66, 0x61 } },
    { { .form = X86_FORM_PUSHD_ES }, 1, { 0x06 } },
    { { .form = X86_FORM_PUSHW_ES }, 2, { 0x66, 0x06 } },
    { { .form = X86_FORM_PUSHD_CS }, 1, { 0x0e } },
    { { .form = X86_FORM_POPD_DS }, 1, { 0x1f } },
    { { .form = X86_FORM_POPW_SS }, 2, { 0x66, 0x17 } },
    { { .form = X86_FORM_PUSHD_FS }, 2, { 0x0f, 0xa0 } },
    // Far branches to a pointer in the immediate: call 4660:1450744508; data16 call 4660:22136; jmp 4660:1450744508;
    // data16 jmp 4660:22136
    { { .form = X86_FORM_CALL_FAR_PTR32, .immediate = 0x56789abc, .displacement = 0x1234 }, 7, { 0x9a, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_CALL_FAR_PTR16, .immediate = 0x5678 | 0x1234 << 16 }, 6, { 0x66, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JMP_FAR_PTR32, .immediate = 0x56789abc, .displacement = 0x1234 }, 7, { 0xea, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JMP_FAR_PTR16, .immediate = 0x5678 | 0x1234 << 16 }, 6, { 0x66, 0xea, 0x78, 0x56, 0x34, 0x12 } },
    // inc and dec of a register in the opcode: inc eax; inc ax; dec ecx; dec cx
    { { .form = X86_FORM_INC_R32, .rm = X86_REGISTER_RAX }, 1, { 0x40 } },
    { { .form = X86_FORM_INC_R16, .rm = X86_REGISTER_RAX }, 2, { 0x66, 0x40 } },
    { { .form = X86_FORM_DEC_R32, .rm = X86_REGISTER_RCX }, 1, { 0x49 } },
    { { .form = X86_FORM_DEC_R16, .rm = X86_REGISTER_RCX }, 2, { 0x66, 0x49 } },
    // The descriptor table registers' 6-byte operands: lgdtd [ebx]; lgdtw [ebx]; sidtd [ebx]; sgdtw [ebx]
    { { .form = X86_FORM_LGDTD_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x01, 0x13 } },
    { { .form = X86_FORM_LGDTW_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x01, 0x13 } },
    { { .form = X86_FORM_SIDTD_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x01, 0x0b } },
    { { .form = X86_FORM_SGDTW_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x01, 0x03 } },
    // The 32-bit forms of VMX's, MPX's and rdpid's 64-bit ones: vmread eax, ecx; vmwrite ecx, dword ptr [ebx]; invept
    // eax, xmmword ptr [ebx]; invpcid eax, xmmword ptr [ebx]; bndcl bnd0, eax; bndcu bnd2, dword ptr [ebx]; bndcn bnd3,
    // ecx; rdpid eax
    { { .form = X86_FORM_VMREAD_RM32_R32, .reg = X86_REGISTER_RCX }, 3, { 0x0f, 0x78, 0xc8 } },
    { { .form = X86_FORM_VMWRITE_R32_RM32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x79, 0x0b } },
    { { .form = X86_FORM_INVEPT_R32_M128, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0x80, 0x03 } },
    { { .form = X86_FORM_INVPCID_R32_M128, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0x82, 0x03 } },
    { { .form = X86_FORM_BNDCL_BND_RM32 }, 4, { 0xf3, 0x0f, 0x1a, 0xc0 } },
    { { .form = X86_FORM_BNDCU_BND_RM32, .reg = 2, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0xf2, 0x0f, 0x1a, 0x13 } },
    { { .form = X86_FORM_BNDCN_BND_RM32, .reg = 3, .rm = X86_REGISTER_RCX }, 4, { 0xf2, 0x0f, 0x1b, 0xd9 } },
    { { .form = X86_FORM_RDPID_R32 }, 4, { 0xf3, 0x0f, 0xc7, 0xf8 } },
    // Addresses in a register, of either size: umonitor eax; umonitor ax; movdir64b eax, [ebx]; movdir64b ax, [bx];
    // enqcmd eax, [ebx]; enqcmds eax, [ebx]
    { { .form = X86_FORM_UMONITOR_R32 }, 4, { 0xf3, 0x0f, 0xae, 0xf0 } },
    { { .form = X86_FORM_UMONITOR_R32, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 5, { 0x67, 0xf3, 0x0f, 0xae, 0xf0 } },
    { { .form = X86_FORM_MOVDIR64B_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0xf8, 0x03 } },
    { { .form = X86_FORM_MOVDIR64B_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 6, { 0x67, 0x66, 0x0f, 0x38, 0xf8, 0x07 } },
    { { .form = X86_FORM_ENQCMD_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf2, 0x0f, 0x38, 0xf8, 0x03 } },
    { { .form = X86_FORM_ENQCMDS_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf3, 0x0f, 0x38, 0xf8, 0x03 } },
    // Near branches through a register or memory, with NOTRACK and BND: notrack call eax; notrack jmp dword ptr [ebx];
    // bnd call eax; bnd jmp dword ptr [ebx]; call ax; jmp word ptr [ebx]; notrack call ax; bnd call ax
    { { .form = X86_FORM_NOTRACK_CALL_RM32 }, 3, { 0x3e, 0xff, 0xd0 } },
    { { .form = X86_FORM_NOTRACK_JMP_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x3e, 0xff, 0x23 } },
    { { .form = X86_FORM_BND_CALL_RM32 }, 3, { 0xf2, 0xff, 0xd0 } },
    { { .form = X86_FORM_BND_JMP_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0xf2, 0xff, 0x23 } },
    { { .form = X86_FORM_CALL_RM16 }, 3, { 0x66, 0xff, 0xd0 } },
    { { .form = X86_FORM_JMP_RM16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xff, 0x23 } },
    { { .form = X86_FORM_NOTRACK_CALL_RM16 }, 4, { 0x3e, 0x66, 0xff, 0xd0 } },
    { { .form = X86_FORM_BND_CALL_RM16 }, 4, { 0x66, 0xf2, 0xff, 0xd0 } },
    // Explicit sizes of ret, leave and enter: retw; retw 8; retd; retd 8; leaved; enterd 16, 1
    { { .form = X86_FORM_RETW }, 2, { 0x66, 0xc3 } },
    { { .form = X86_FORM_RETW_IMM16, .immediate = 8 }, 4, { 0x66, 0xc2, 0x08, 0x00 } },
    { { .form = X86_FORM_RETD }, 1, { 0xc3 } },
    { { .form = X86_FORM_RETD_IMM16, .immediate = 8 }, 3, { 0xc2, 0x08, 0x00 } },
    { { .form = X86_FORM_LEAVED }, 1, { 0xc9 } },
    { { .form = X86_FORM_ENTERD, .immediate = 16 | 1 << 16 }, 4, { 0xc8, 0x10, 0x00, 0x01 } },
    // moffs32 and moffs16, with a 67: mov al, ds:0x12345678; mov ax, ds:0x12345678; mov eax, ds:0x12345678; mov
    // ds:0x12345678, al; mov ds:0x12345678, eax; addr16 mov al, ds:0x1234; addr16 mov eax, ds:0x1234; addr16 mov
    // ds:0x1234, ax
    { { .form = X86_FORM_MOV_AL_MOFFS32, .immediate = 0x12345678 }, 5, { 0xa0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_AX_MOFFS32, .immediate = 0x12345678 }, 6, { 0x66, 0xa1, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_EAX_MOFFS32, .immediate = 0x12345678 }, 5, { 0xa1, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_MOFFS32_AL, .immediate = 0x12345678 }, 5, { 0xa2, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_MOFFS32_EAX, .immediate = 0x12345678 }, 5, { 0xa3, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_AL_MOFFS16, .immediate = 0x1234 }, 4, { 0x67, 0xa0, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_EAX_MOFFS16, .immediate = 0x1234 }, 4, { 0x67, 0xa1, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_MOFFS16_AX, .immediate = 0x1234 }, 5, { 0x67, 0x66, 0xa3, 0x34, 0x12 } },
    // Segment overrides in the decorations, an SS one on ESP's default segment too, on a memory operand, a string
    // instruction's source and a moffs, and before the 67: es mov eax, dword ptr [ebx]; ss mov eax, dword ptr [esp+4];
    // cs movsb; fs mov al, ds:0x12345678; ds mov ax, word ptr [bx+si]
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 3, { 0x26, 0x8b, 0x03 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 4, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(2) }, 5, { 0x36, 0x8b, 0x44, 0x24, 0x04 } },
    { { .form = X86_FORM_MOVS_B, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(1) }, 2, { 0x2e, 0xa4 } },
    { { .form = X86_FORM_MOV_AL_MOFFS32, .immediate = 0x12345678, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(4) }, 6, { 0x64, 0xa0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) | BUSTER_X86_MODE_ADDRESS_SIZE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(3) }, 5, { 0x3e, 0x67, 0x66, 0x8b, 0x00 } },
    // VIA's montmul, with each address size: montmul; addr16 montmul
    { { .form = X86_FORM_MONTMUL }, 4, { 0xf3, 0x0f, 0xa6, 0xc0 } },
    { { .form = X86_FORM_MONTMUL, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 5, { 0x67, 0xf3, 0x0f, 0xa6, 0xc0 } },
};

// Known encodings in 16-bit mode (x86_encode_scalar_16), with bytes from GNU as's .code16: ModRM's 16-bit addresses,
// the mode's operand and address sizes and the other ones', and its own forms.
static const KnownEncoding x86_known_encodings_16[] = {
    // 16- and 32-bit operands, and high byte registers: add ax, bx; add eax, ebx; add bh, ch
    { { .form = X86_FORM_ADD_RM16_R16, .reg = X86_REGISTER_RBX }, 2, { 0x01, 0xd8 } },
    { { .form = X86_FORM_ADD_RM32_R32, .reg = X86_REGISTER_RBX }, 3, { 0x66, 0x01, 0xd8 } },
    { { .form = X86_FORM_ADD_RM8_R8, .reg = X86_REGISTER_CH, .rm = X86_REGISTER_BH }, 2, { 0x00, 0xef } },
    // ModRM's eight 16-bit addresses, an absolute one and lea: mov al, byte ptr [bx+si]; mov al, byte ptr [bx+di]; mov
    // al, byte ptr [bp+si]; mov al, byte ptr [bp+di]; mov al, byte ptr [si]; mov al, byte ptr [di]; mov al, byte ptr
    // [bp]; mov al, byte ptr [bx]; mov cl, byte ptr ds:4660; lea ax, [bx+di]
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 2, { 0x8a, 0x00 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 2, { 0x8a, 0x01 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 2, { 0x8a, 0x02 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 2, { 0x8a, 0x03 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8a, 0x04 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8a, 0x05 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x8a, 0x46, 0x00 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x8a, 0x07 } },
    { { .form = X86_FORM_MOV_R8_RM8, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = 0x1234 }, 4, { 0x8a, 0x0e, 0x34, 0x12 } },
    { { .form = X86_FORM_LEA_R16_M, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) }, 2, { 0x8d, 0x01 } },
    // 16-bit displacements, modulo 2^16: mov cl, byte ptr ds:65534; mov al, byte ptr [bx+si+5]; mov al, byte ptr
    // [bx+4660]; mov al, byte ptr [bx-129]; mov al, byte ptr [bx+65535]; mov al, byte ptr [bp+di+32768]
    { { .form = X86_FORM_MOV_R8_RM8, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE, .displacement = -2 }, 4, { 0x8a, 0x0e, 0xfe, 0xff } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 5 }, 3, { 0x8a, 0x40, 0x05 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 0x1234 }, 4, { 0x8a, 0x87, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = -129 }, 4, { 0x8a, 0x87, 0x7f, 0xff } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 65535 }, 3, { 0x8a, 0x47, 0xff } },
    { { .form = X86_FORM_MOV_R8_RM8, .rm = X86_REGISTER_RBP, .index = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 32768 }, 4, { 0x8a, 0x83, 0x00, 0x80 } },
    // 32-bit addresses (a 67): mov ax, word ptr [eax+ecx*4+8]; mov eax, dword ptr [ebx]; addr32 mov cx, word ptr
    // ds:305419896; mov cx, word ptr [ebp]
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RAX, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 8 }, 5, { 0x67, 0x8b, 0x44, 0x88, 0x08 } },
    { { .form = X86_FORM_MOV_R32_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 4, { 0x67, 0x66, 0x8b, 0x03 } },
    { { .form = X86_FORM_MOV_R16_RM16, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = 0x12345678 }, 7, { 0x67, 0x8b, 0x0d, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_R16_RM16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 4, { 0x67, 0x8b, 0x4d, 0x00 } },
    // Pushes and pops: push ax; push eax; pop dword ptr [si]; push 5; pushd 5; pushw 4660; pushd 305419896; pushfw;
    // pushfd
    { { .form = X86_FORM_PUSH_R16, .rm = X86_REGISTER_RAX }, 1, { 0x50 } },
    { { .form = X86_FORM_PUSH_R32, .rm = X86_REGISTER_RAX }, 2, { 0x66, 0x50 } },
    { { .form = X86_FORM_POP_RM32, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0x8f, 0x04 } },
    { { .form = X86_FORM_PUSH_IMM8, .immediate = 5 }, 2, { 0x6a, 0x05 } },
    { { .form = X86_FORM_PUSHD_IMM8, .immediate = 5 }, 3, { 0x66, 0x6a, 0x05 } },
    { { .form = X86_FORM_PUSHW_IMM16, .immediate = 0x1234 }, 3, { 0x68, 0x34, 0x12 } },
    { { .form = X86_FORM_PUSHD_IMM32, .immediate = 0x12345678 }, 6, { 0x66, 0x68, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_PUSHFW }, 1, { 0x9c } },
    { { .form = X86_FORM_PUSHFD }, 2, { 0x66, 0x9c } },
    // Branches: call eax; data32 call [bx]; data32 jmp [di]; lcall [bx]; data32 lcall [bx]; ljmp [si]; call .+4663;
    // data32 call .+305419902; {disp16} jne .+4664; data32 jne .-305419889; {disp16} jmp .+8; data32 cs jne .+4104;
    // data32 bnd jmp .+4103; {disp16} xbegin .+260; data32 xbegin .+263
    { { .form = X86_FORM_CALL_RM32, .rm = X86_REGISTER_RAX }, 3, { 0x66, 0xff, 0xd0 } },
    { { .form = X86_FORM_CALL_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xff, 0x17 } },
    { { .form = X86_FORM_JMP_RM32, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xff, 0x25 } },
    { { .form = X86_FORM_CALL_FAR_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x1f } },
    { { .form = X86_FORM_CALL_FAR_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xff, 0x1f } },
    { { .form = X86_FORM_JMP_FAR_M32, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x2c } },
    { { .form = X86_FORM_CALL_REL16, .displacement = 0x1234 }, 3, { 0xe8, 0x34, 0x12 } },
    { { .form = X86_FORM_CALL_REL32, .displacement = 0x12345678 }, 6, { 0x66, 0xe8, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JNE_REL16, .displacement = 0x1234 }, 4, { 0x0f, 0x85, 0x34, 0x12 } },
    { { .form = X86_FORM_JNE_REL32, .displacement = -0x12345678 }, 7, { 0x66, 0x0f, 0x85, 0x88, 0xa9, 0xcb, 0xed } },
    { { .form = X86_FORM_JMP_REL16, .displacement = 5 }, 3, { 0xe9, 0x05, 0x00 } },
    { { .form = X86_FORM_JNE_REL32_NOT_TAKEN, .displacement = 0x1000 }, 8, { 0x66, 0x2e, 0x0f, 0x85, 0x00, 0x10, 0x00, 0x00 } },
    { { .form = X86_FORM_BND_JMP_REL32, .displacement = 0x1000 }, 7, { 0x66, 0xf2, 0xe9, 0x00, 0x10, 0x00, 0x00 } },
    { { .form = X86_FORM_XBEGIN_REL16, .displacement = 0x100 }, 4, { 0xc7, 0xf8, 0x00, 0x01 } },
    { { .form = X86_FORM_XBEGIN_REL32, .displacement = 0x100 }, 7, { 0x66, 0xc7, 0xf8, 0x00, 0x01, 0x00, 0x00 } },
    // The addresses an instruction makes itself, of either size: jcxz .+18; jecxz .+19; addr32 loop .+1; movsw; movsd;
    // addr32 movsb
    { { .form = X86_FORM_JRCXZ_REL8, .displacement = 16 }, 2, { 0xe3, 0x10 } },
    { { .form = X86_FORM_JRCXZ_REL8, .displacement = 16, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 3, { 0x67, 0xe3, 0x10 } },
    { { .form = X86_FORM_LOOP_REL8, .displacement = -2, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 3, { 0x67, 0xe2, 0xfe } },
    { { .form = X86_FORM_MOVS_W }, 1, { 0xa5 } },
    { { .form = X86_FORM_MOVS_D }, 2, { 0x66, 0xa5 } },
    { { .form = X86_FORM_MOVS_B, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 2, { 0x67, 0xa4 } },
    // The x87 environment: fnstenvd [bx]; fnstenvw [bx]; fstenvd [bx]
    { { .form = X86_FORM_FNSTENV_M28, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xd9, 0x37 } },
    { { .form = X86_FORM_FNSTENVW_M14, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xd9, 0x37 } },
    { { .form = X86_FORM_FSTENV_M28, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x9b, 0x66, 0xd9, 0x37 } },
    // Control registers (CR8 with AMD's LOCK prefix) and test registers, VEX and EVEX, with a 16-bit address's disp8*N,
    // and a gather's 32-bit address: mov cr0, eax; mov eax, cr8; mov cr8, ebx; mov eax, tr3; mov tr4, esi; vaddps
    // xmm1, xmm2, xmm3; vmovaps zmm0, zmmword ptr [bx+si+128]; vpgatherdd xmm1, dword ptr [eax+xmm2*4], xmm3
    { { .form = X86_FORM_MOV_CR_R32, .reg = 0, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x22, 0xc0 } },
    { { .form = X86_FORM_MOV_R32_CR, .reg = 8, .rm = X86_REGISTER_RAX }, 4, { 0xf0, 0x0f, 0x20, 0xc0 } },
    { { .form = X86_FORM_MOV_CR_R32, .reg = 8, .rm = X86_REGISTER_RBX }, 4, { 0xf0, 0x0f, 0x22, 0xc3 } },
    { { .form = X86_FORM_MOV_R32_TR, .reg = 3, .rm = X86_REGISTER_RAX }, 3, { 0x0f, 0x24, 0xd8 } },
    { { .form = X86_FORM_MOV_TR_R32, .reg = 4, .rm = X86_REGISTER_RSI }, 3, { 0x0f, 0x26, 0xe6 } },
    { { .form = X86_FORM_VADDPS_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 3 }, 4, { 0xc5, 0xe8, 0x58, 0xcb } },
    { { .form = X86_FORM_VMOVAPS_Z_ZM512, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0), .displacement = 128 }, 7, { 0x62, 0xf1, 0x7c, 0x48, 0x28, 0x40, 0x02 } },
    { { .form = X86_FORM_VPGATHERDD_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = X86_REGISTER_RAX, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) | BUSTER_X86_MODE_ADDRESS_SIZE }, 7, { 0x67, 0xc4, 0xe2, 0x61, 0x90, 0x0c, 0x90 } },
    // The forms with fixed sizes, and prefixes: movnti dword ptr [bx], ecx; crc32 eax, bl; crc32 eax, ebx; adcx eax,
    // ebx; bswap eax; xacquire lock add dword ptr [bx], 5
    { { .form = X86_FORM_MOVNTI_M32_R32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0xc3, 0x0f } },
    { { .form = X86_FORM_CRC32_R32_RM8, .rm = X86_REGISTER_RBX }, 5, { 0xf2, 0x0f, 0x38, 0xf0, 0xc3 } },
    { { .form = X86_FORM_CRC32_R32_RM32, .rm = X86_REGISTER_RBX }, 6, { 0x66, 0xf2, 0x0f, 0x38, 0xf1, 0xc3 } },
    { { .form = X86_FORM_ADCX_R32_RM32, .rm = X86_REGISTER_RBX }, 5, { 0x66, 0x0f, 0x38, 0xf6, 0xc3 } },
    { { .form = X86_FORM_BSWAP_R32, .rm = X86_REGISTER_RAX }, 3, { 0x66, 0x0f, 0xc8 } },
    { { .form = X86_FORM_XACQUIRE_LOCK_ADD_M32_IMM8, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 5 }, 6, { 0x66, 0xf2, 0xf0, 0x83, 0x07, 0x05 } },
    // Returns and the rest: iretd; iretw; retfd; cwde; cbw; enterw 16, 1; enter 16, 1; push fs; movzx eax, ah; in eax,
    // dx
    { { .form = X86_FORM_IRETD }, 2, { 0x66, 0xcf } },
    { { .form = X86_FORM_IRET }, 1, { 0xcf } },
    { { .form = X86_FORM_RETFD }, 2, { 0x66, 0xcb } },
    { { .form = X86_FORM_CWDE }, 2, { 0x66, 0x98 } },
    { { .form = X86_FORM_CBW }, 1, { 0x98 } },
    { { .form = X86_FORM_ENTERW, .immediate = 16 | 1 << 16 }, 4, { 0xc8, 0x10, 0x00, 0x01 } },
    { { .form = X86_FORM_ENTER, .immediate = 16 | 1 << 16 }, 4, { 0xc8, 0x10, 0x00, 0x01 } },
    { { .form = X86_FORM_PUSH_FS }, 2, { 0x0f, 0xa0 } },
    { { .form = X86_FORM_MOVZX_R32_RM8, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_AH }, 4, { 0x66, 0x0f, 0xb6, 0xc4 } },
    { { .form = X86_FORM_IN_EAX_DX }, 2, { 0x66, 0xed } },
    // What 64-bit mode doesn't have: the BCD adjustments and salc: aaa; das; aam 10; salc
    { { .form = X86_FORM_AAA }, 1, { 0x37 } },
    { { .form = X86_FORM_DAS }, 1, { 0x2f } },
    { { .form = X86_FORM_AAM_IMM8, .immediate = 10 }, 2, { 0xd4, 0x0a } },
    { { .form = X86_FORM_SALC }, 1, { 0xd6 } },
    // bound, arpl, lds: bound ax, dword ptr [bx]; bound eax, qword ptr [bx]; arpl word ptr [bx], ax; lds ax, dword ptr
    // [bx]; lds eax, [bx]
    { { .form = X86_FORM_BOUND_R16_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x62, 0x07 } },
    { { .form = X86_FORM_BOUND_R32_M64, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0x62, 0x07 } },
    { { .form = X86_FORM_ARPL_RM16_R16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0x63, 0x07 } },
    { { .form = X86_FORM_LDS_R16_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xc5, 0x07 } },
    { { .form = X86_FORM_LDS_R32_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x66, 0xc5, 0x07 } },
    // pusha and popa, and pushes and pops of segment registers: pushaw; pushad; popad; pushw es; pushd es; popd ds;
    // pushd fs; popd gs
    { { .form = X86_FORM_PUSHAW }, 1, { 0x60 } },
    { { .form = X86_FORM_PUSHAD }, 2, { 0x66, 0x60 } },
    { { .form = X86_FORM_POPAD }, 2, { 0x66, 0x61 } },
    { { .form = X86_FORM_PUSHW_ES }, 1, { 0x06 } },
    { { .form = X86_FORM_PUSHD_ES }, 2, { 0x66, 0x06 } },
    { { .form = X86_FORM_POPD_DS }, 2, { 0x66, 0x1f } },
    { { .form = X86_FORM_PUSHD_FS }, 3, { 0x66, 0x0f, 0xa0 } },
    { { .form = X86_FORM_POPD_GS }, 3, { 0x66, 0x0f, 0xa9 } },
    // Far branches to a pointer in the immediate: call 4660:22136; data32 call 4660:1450744508; jmp 4660:22136; data32
    // jmp 4660:1450744508
    { { .form = X86_FORM_CALL_FAR_PTR16, .immediate = 0x5678 | 0x1234 << 16 }, 5, { 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_CALL_FAR_PTR32, .immediate = 0x56789abc, .displacement = 0x1234 }, 8, { 0x66, 0x9a, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JMP_FAR_PTR16, .immediate = 0x5678 | 0x1234 << 16 }, 5, { 0xea, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_JMP_FAR_PTR32, .immediate = 0x56789abc, .displacement = 0x1234 }, 8, { 0x66, 0xea, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12 } },
    // inc and dec of a register in the opcode: inc ax; inc eax; dec di; dec edi
    { { .form = X86_FORM_INC_R16, .rm = X86_REGISTER_RAX }, 1, { 0x40 } },
    { { .form = X86_FORM_INC_R32, .rm = X86_REGISTER_RAX }, 2, { 0x66, 0x40 } },
    { { .form = X86_FORM_DEC_R16, .rm = X86_REGISTER_RDI }, 1, { 0x4f } },
    { { .form = X86_FORM_DEC_R32, .rm = X86_REGISTER_RDI }, 2, { 0x66, 0x4f } },
    // The descriptor table registers' 6-byte operands: lgdtw [bx]; lgdtd [bx]; sidtd [bx]
    { { .form = X86_FORM_LGDTW_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x01, 0x17 } },
    { { .form = X86_FORM_LGDTD_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x01, 0x17 } },
    { { .form = X86_FORM_SIDTD_M48, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x66, 0x0f, 0x01, 0x0f } },
    // The 32-bit forms of VMX's, MPX's (with a 32-bit address) and rdpid's 64-bit ones: vmread eax, ecx; vmwrite ecx,
    // dword ptr [bx]; invept eax, xmmword ptr [bx]; bndcl bnd0, eax; bndcl bnd1, dword ptr [ebx]; rdpid eax
    { { .form = X86_FORM_VMREAD_RM32_R32, .reg = X86_REGISTER_RCX }, 3, { 0x0f, 0x78, 0xc8 } },
    { { .form = X86_FORM_VMWRITE_R32_RM32, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0x0f, 0x79, 0x0f } },
    { { .form = X86_FORM_INVEPT_R32_M128, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0x80, 0x07 } },
    { { .form = X86_FORM_BNDCL_BND_RM32 }, 4, { 0xf3, 0x0f, 0x1a, 0xc0 } },
    { { .form = X86_FORM_BNDCL_BND_RM32, .reg = 1, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 5, { 0x67, 0xf3, 0x0f, 0x1a, 0x0b } },
    { { .form = X86_FORM_RDPID_R32 }, 4, { 0xf3, 0x0f, 0xc7, 0xf8 } },
    // Addresses in a register, of either size: umonitor ax; umonitor eax; movdir64b ax, [bx]; movdir64b eax, [ebx];
    // enqcmd ax, [bx]
    { { .form = X86_FORM_UMONITOR_R32 }, 4, { 0xf3, 0x0f, 0xae, 0xf0 } },
    { { .form = X86_FORM_UMONITOR_R32, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 5, { 0x67, 0xf3, 0x0f, 0xae, 0xf0 } },
    { { .form = X86_FORM_MOVDIR64B_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0x66, 0x0f, 0x38, 0xf8, 0x07 } },
    { { .form = X86_FORM_MOVDIR64B_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE }, 6, { 0x67, 0x66, 0x0f, 0x38, 0xf8, 0x03 } },
    { { .form = X86_FORM_ENQCMD_R32_M512, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 5, { 0xf2, 0x0f, 0x38, 0xf8, 0x07 } },
    // Near branches through a register or memory, with NOTRACK and BND: notrack call ax; notrack call eax; data32
    // notrack call [bx]; bnd call eax; bnd jmp word ptr [bx]; call ax; call word ptr [bx]
    { { .form = X86_FORM_NOTRACK_CALL_RM16 }, 3, { 0x3e, 0xff, 0xd0 } },
    { { .form = X86_FORM_NOTRACK_CALL_RM32 }, 4, { 0x3e, 0x66, 0xff, 0xd0 } },
    { { .form = X86_FORM_NOTRACK_CALL_RM32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 4, { 0x3e, 0x66, 0xff, 0x17 } },
    { { .form = X86_FORM_BND_CALL_RM32 }, 4, { 0x66, 0xf2, 0xff, 0xd0 } },
    { { .form = X86_FORM_BND_JMP_RM16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 3, { 0xf2, 0xff, 0x27 } },
    { { .form = X86_FORM_CALL_RM16 }, 2, { 0xff, 0xd0 } },
    { { .form = X86_FORM_CALL_RM16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE }, 2, { 0xff, 0x17 } },
    // Explicit sizes of ret, leave and enter: retw; retd; retd 8; leaved; enterd 16, 1
    { { .form = X86_FORM_RETW }, 1, { 0xc3 } },
    { { .form = X86_FORM_RETD }, 2, { 0x66, 0xc3 } },
    { { .form = X86_FORM_RETD_IMM16, .immediate = 8 }, 4, { 0x66, 0xc2, 0x08, 0x00 } },
    { { .form = X86_FORM_LEAVED }, 2, { 0x66, 0xc9 } },
    { { .form = X86_FORM_ENTERD, .immediate = 16 | 1 << 16 }, 5, { 0x66, 0xc8, 0x10, 0x00, 0x01 } },
    // moffs16, and moffs32 with a 67: mov al, ds:0x1234; mov ax, ds:0x1234; mov eax, ds:0x1234; mov ds:0x1234, al;
    // addr32 mov al, ds:0x12345678; addr32 mov eax, ds:0x12345678
    { { .form = X86_FORM_MOV_AL_MOFFS16, .immediate = 0x1234 }, 3, { 0xa0, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_AX_MOFFS16, .immediate = 0x1234 }, 3, { 0xa1, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_EAX_MOFFS16, .immediate = 0x1234 }, 4, { 0x66, 0xa1, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_MOFFS16_AL, .immediate = 0x1234 }, 3, { 0xa2, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_AL_MOFFS32, .immediate = 0x12345678 }, 6, { 0x67, 0xa0, 0x78, 0x56, 0x34, 0x12 } },
    { { .form = X86_FORM_MOV_EAX_MOFFS32, .immediate = 0x12345678 }, 7, { 0x67, 0x66, 0xa1, 0x78, 0x56, 0x34, 0x12 } },
    // Segment overrides in the decorations, SS's and DS's on BP's default segment and the other one too, on a memory
    // operand, the addresses instructions make themselves, a moffs, VEX and lds: es mov ax, word ptr [bx]; cs mov cx,
    // word ptr [si+4]; ss mov ax, word ptr [bp]; ds mov ax, word ptr [bp]; es lodsw; ss xlatb; ds monitor; es addr32
    // rep movsb; gs mov ax, ds:0x1234; es vmovaps xmm0, xmmword ptr [bx]; es lds ax, dword ptr [bx]
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 3, { 0x26, 0x8b, 0x07 } },
    { { .form = X86_FORM_MOV_R16_RM16, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 4, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(1) }, 4, { 0x2e, 0x8b, 0x4c, 0x04 } },
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(2) }, 4, { 0x36, 0x8b, 0x46, 0x00 } },
    { { .form = X86_FORM_MOV_R16_RM16, .rm = X86_REGISTER_RBP, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(3) }, 4, { 0x3e, 0x8b, 0x46, 0x00 } },
    { { .form = X86_FORM_LODS_W, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 2, { 0x26, 0xad } },
    { { .form = X86_FORM_XLATB, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(2) }, 2, { 0x36, 0xd7 } },
    { { .form = X86_FORM_MONITOR, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(3) }, 4, { 0x3e, 0x0f, 0x01, 0xc8 } },
    { { .form = X86_FORM_REP_MOVS_B, .mode = BUSTER_X86_MODE_ADDRESS_SIZE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 4, { 0x26, 0x67, 0xf3, 0xa4 } },
    { { .form = X86_FORM_MOV_AX_MOFFS16, .immediate = 0x1234, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(5) }, 4, { 0x65, 0xa1, 0x34, 0x12 } },
    { { .form = X86_FORM_VMOVAPS_X_XM128, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 5, { 0x26, 0xc5, 0xf8, 0x28, 0x07 } },
    { { .form = X86_FORM_LDS_R16_M32, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_X86_SEGMENT_OVERRIDE(0) }, 3, { 0x26, 0xc5, 0x07 } },
    // VIA's montmul, with each address size: montmul; addr32 montmul
    { { .form = X86_FORM_MONTMUL }, 4, { 0xf3, 0x0f, 0xa6, 0xc0 } },
    { { .form = X86_FORM_MONTMUL, .mode = BUSTER_X86_MODE_ADDRESS_SIZE }, 5, { 0x67, 0xf3, 0x0f, 0xa6, 0xc0 } },
};

// The mode's known encodings.
static const KnownEncoding* x86_mode_known_encodings(uint64_t* count)
{
    *count = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? BUSTER_ARRAY_LENGTH(x86_known_encodings_address32)
             : x86_test_mode == X86_ENCODER_MODE_APX     ? BUSTER_ARRAY_LENGTH(x86_known_encodings_apx)
             : x86_test_mode == X86_ENCODER_MODE_32      ? BUSTER_ARRAY_LENGTH(x86_known_encodings_32)
             : x86_test_mode == X86_ENCODER_MODE_16      ? BUSTER_ARRAY_LENGTH(x86_known_encodings_16)
                                                         : BUSTER_ARRAY_LENGTH(x86_known_encodings);
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_known_encodings_address32
           : x86_test_mode == X86_ENCODER_MODE_APX     ? x86_known_encodings_apx
           : x86_test_mode == X86_ENCODER_MODE_32      ? x86_known_encodings_32
           : x86_test_mode == X86_ENCODER_MODE_16      ? x86_known_encodings_16
                                                       : x86_known_encodings;
}

// Checks one encoder of the mode on its known encodings, encoding each alone, and again with the fixup flag, whose end
// it must report.
static uint64_t x86_check_known_with(const char* name, bool vectorized)
{
    static uint8_t output[BUSTER_X86_ENCODER_CAPACITY(1)];
    uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)];
    uint64_t count = 0;
    const KnownEncoding* encodings = x86_mode_known_encodings(&count);
    uint64_t failures = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const KnownEncoding* known = &encodings[i];
        for (uint32_t marked = 0; marked < 2; marked += 1)
        {
            X86Instruction instruction = known->instruction;
            instruction.form |= marked ? BUSTER_X86_FORM_FIXUP : 0;
            fixup_ends[0] = UINT32_MAX;
            const int64_t length = vectorized ? x86_test_encode(&instruction, 1, output, sizeof(output), 0, fixup_ends)
                                              : x86_test_encode_scalar(&instruction, 1, output, sizeof(output), 0, fixup_ends);
            if (length != known->length || memcmp(output, known->bytes, known->length) != 0 || (marked && fixup_ends[0] != known->length))
            {
                if (failures < BUSTER_X86_TEST_REPORTED)
                {
                    printf("  %s: ", name);
                    x86_print_instruction(stdout, &instruction, 0);
                    printf(": got ");
                    x86_print_bytes(output, length > 0 ? (uint64_t)length : 0);
                    printf(" (%lld bytes", (long long)length);
                    if (marked)
                    {
                        printf(", fixup end %u", fixup_ends[0]);
                    }
                    printf("), expected ");
                    x86_print_bytes(known->bytes, known->length);
                    printf("\n");
                }
                failures += 1;
            }
        }
    }
    return failures;
}

// Checks the mode's encoders on its known encodings: in 16- and 32-bit modes, the scalar one alone.
static bool x86_check_known(void)
{
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    const char* scalar_name = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? "x86_encode_scalar_address32"
                              : x86_test_mode == X86_ENCODER_MODE_APX     ? "x86_encode_scalar_apx"
                              : x86_test_mode == X86_ENCODER_MODE_32      ? "x86_encode_scalar_32"
                              : x86_test_mode == X86_ENCODER_MODE_16      ? "x86_encode_scalar_16"
                                                                          : "x86_encode_scalar";
    const char* vectorized_name = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? "x86_encode_address32" : x86_test_mode == X86_ENCODER_MODE_APX ? "x86_encode_apx" : "x86_encode";
    const uint64_t scalar = x86_check_known_with(scalar_name, false);
    const uint64_t vectorized = legacy ? 0 : x86_check_known_with(vectorized_name, true);
    uint64_t count = 0;
    x86_mode_known_encodings(&count);
    if (legacy)
    {
        printf("known encodings%s: %lu instructions, %lu wrong\n", x86_test_mode_words(), (unsigned long)count, (unsigned long)scalar);
    }
    else
    {
        printf("known encodings%s: %lu instructions, %lu wrong (scalar), %lu wrong (vectorized)\n", x86_test_mode_words(), (unsigned long)count, (unsigned long)scalar,
               (unsigned long)vectorized);
    }
    return scalar == 0 && vectorized == 0;
}

// Instructions that x86_encode_apx, x86_encode_scalar_apx and x86_validate_apx must reject, as GNU as does: the XSAVE
// family with a base or index from 16 to 31 (REX2 is #UD with it), a high byte register with REX2, XMM16 to XMM31 in
// SSE's forms (which only EVEX has), and registers 16 to 31 in VEX forms that have no EVEX one.
static const X86Instruction x86_known_invalid_apx[] = {
    // xsave [r16]; xrstor64 [rax + r17]; xsaveopt [r31 + 8]; xsavec64 [r20]; xsaves [r16]; xrstors64 [rbx + r29*8]
    { .form = X86_FORM_XSAVE_M, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_XRSTOR64_M, .rm = X86_REGISTER_RAX, .index = 17, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) },
    { .form = X86_FORM_XSAVEOPT_M, .rm = 31, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 },
    { .form = X86_FORM_XSAVEC64_M, .rm = 20, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_XSAVES_M, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_XRSTORS64_M, .rm = X86_REGISTER_RBX, .index = 29, .mode = BUSTER_X86_MEMORY_BASE_INDEX(3) },
    // mov ah, r16b; mov r17b, ch; mov byte ptr [r16], ah; movzx r16d, bh; add dh, byte ptr [rax + r21]
    { .form = X86_FORM_MOV_RM8_R8, .reg = 16, .rm = X86_REGISTER_AH },
    { .form = X86_FORM_MOV_RM8_R8, .reg = X86_REGISTER_CH, .rm = 17 },
    { .form = X86_FORM_MOV_RM8_R8, .reg = X86_REGISTER_AH, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_MOVZX_R32_RM8, .reg = 16, .rm = X86_REGISTER_BH },
    { .form = X86_FORM_ADD_R8_RM8, .reg = X86_REGISTER_DH, .rm = X86_REGISTER_RAX, .index = 21, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0) },
    // movaps xmm16, xmm0; movaps xmm0, xmm16; movaps xmm16, xmmword ptr [r16]; movd xmm16, eax
    { .form = X86_FORM_MOVAPS_X_XM, .reg = 16 },
    { .form = X86_FORM_MOVAPS_X_XM, .rm = 16 },
    { .form = X86_FORM_MOVAPS_X_XM, .reg = 16, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_MOVD_X_RM32, .reg = 16 },
    // VEX forms without an EVEX one, so with no way to registers 16 to 31: vtestps xmm1, xmmword ptr [r16]; vpblendvb
    // xmm1, xmm2, xmmword ptr [r16], xmm3; vldmxcsr dword ptr [r16]; vpgatherdd xmm1, [rax + xmm2*4 + 0], xmm3 with r16
    // as its base; XOP's vpcmov xmm1, xmm2, xmm3, xmmword ptr [r17]
    { .form = X86_FORM_VTESTPS_X_XM128, .reg = 1, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_VPBLENDVB_X_X_XM128_X, .reg = 1, .vvvv = 2, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 },
    { .form = X86_FORM_VLDMXCSR_M32, .rm = 16, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_VPGATHERDD_X_VM32X_X, .reg = 1, .vvvv = 3, .rm = 16, .index = 2, .mode = BUSTER_X86_MEMORY_BASE_INDEX(2) },
    { .form = X86_FORM_VPCMOV_X_X_X_XM128, .reg = 1, .vvvv = 2, .rm = 17, .mode = BUSTER_X86_MEMORY_BASE, .immediate = 3 << 4 },
    // The VEX and legacy map 2 forms of instructions with an APX EVEX form, which takes registers 16 to 31 instead:
    // andn r16d, eax, ecx; kmovw k1, r16d; crc32 eax, r16b; movbe r16d, dword ptr [rax]
    { .form = X86_FORM_ANDN_R32_R32_RM32, .reg = 16, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX },
    { .form = X86_FORM_KMOVW_K_R32, .reg = 1, .rm = 16 },
    { .form = X86_FORM_CRC32_R32_RM8, .reg = X86_REGISTER_RAX, .rm = 16 },
    { .form = X86_FORM_MOVBE_R32_M32, .reg = 16, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
    // Map 4's forms: setzub byte ptr [rax] (ZU takes only a register), and a high byte register, which no EVEX form can
    // have, in vvvv, rm and reg: add ah, cl, dl; add al, ah, dl; {evex} add cl, ah
    { .form = X86_FORM_SETZUB_R8, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_BASE },
    { .form = X86_FORM_ADD_R8_RM8_R8, .vvvv = X86_REGISTER_AH, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_RDX },
    { .form = X86_FORM_ADD_R8_RM8_R8, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_AH, .reg = X86_REGISTER_RDX },
    { .form = X86_FORM_ADD_RM8_R8_EVEX, .rm = X86_REGISTER_RCX, .reg = X86_REGISTER_AH },
    // A default flags value past 15, where a general register in vvvv can be 16 to 31: ccmpb {dfv=16} eax, ecx; ctestb
    // with an opmask, which APX's forms don't have: ctestb eax{k1}, ecx; and push2 rax, qword ptr [rcx] (push2 and pop2
    // take only registers)
    { .form = X86_FORM_CCMPB_RM32_R32, .vvvv = 16, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX },
    { .form = X86_FORM_CTESTB_RM32_R32, .rm = X86_REGISTER_RAX, .reg = X86_REGISTER_RCX, .decorations = 1 },
    { .form = X86_FORM_PUSH2_R64_R64, .vvvv = X86_REGISTER_RAX, .rm = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_BASE },
};

static bool x86_check_known_invalid_apx(void)
{
    uint8_t output[BUSTER_X86_ENCODER_CAPACITY(1)];
    uint64_t failures = 0;
    for (uint64_t i = 0; i < BUSTER_ARRAY_LENGTH(x86_known_invalid_apx); i += 1)
    {
        const X86Instruction* instruction = &x86_known_invalid_apx[i];
        const bool rejected = x86_validate_apx(instruction, 1) == 0 &&
                              x86_encode_scalar_apx(instruction, 1, output, sizeof(output), 0, 0) == X86_ENCODER_ERROR_INVALID_INSTRUCTION &&
                              x86_encode_apx(instruction, 1, output, sizeof(output), 0, 0) == X86_ENCODER_ERROR_INVALID_INSTRUCTION;
        if (!rejected && failures < BUSTER_X86_TEST_REPORTED)
        {
            printf("  accepted: ");
            x86_print_instruction(stdout, instruction, 0);
            printf("\n");
        }
        failures += !rejected;
    }
    printf("known invalid instructions with APX's registers: %lu instructions, %lu accepted\n", (unsigned long)BUSTER_ARRAY_LENGTH(x86_known_invalid_apx),
           (unsigned long)failures);
    return failures == 0;
}

typedef struct KnownFixup KnownFixup;
struct KnownFixup
{
    X86Instruction instruction;
    X86FixupField field;
    // Where GNU as puts the field's relocation, from the instruction's start, and the instruction's length.
    uint8_t offset;
    uint8_t length;
};

// Marked instructions with a symbolic field, and where GNU as puts its relocation, so that the tests check
// x86_fixup_distance and the reported ends without GNU as. The comments are GNU as's relocations (a PC-relative field's
// addend from the field is the symbol's minus the distance from the end).
static const KnownFixup x86_known_fixups[] = {
    // call f; jmp f; je f; xbegin f (R_X86_64_PLT32 f-4)
    { { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 5 },
    { { .form = X86_FORM_JMP_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 5 },
    { { .form = X86_FORM_JE_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 2, 6 },
    { { .form = X86_FORM_XBEGIN_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 2, 6 },
    // loop f; jrcxz f (R_X86_64_PC8 f-1)
    { { .form = X86_FORM_LOOP_REL8 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 2 },
    { { .form = X86_FORM_JRCXZ_REL8 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 2 },
    // lea rax, [rip + x]; lea r12, [rip + x] (R_X86_64_PC32 x-4)
    { { .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_RAX, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 3, 7 },
    { { .form = X86_FORM_LEA_R64_M | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_R12, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 3, 7 },
    // cmp byte ptr [rip + x], 1 (x-5)
    { { .form = X86_FORM_CMP_RM8_IMM8 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP, .immediate = 1 }, X86_FIXUP_DISPLACEMENT, 2, 7 },
    // mov dword ptr [rip + x], 0x12345678 (x-8)
    { { .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP, .immediate = 0x12345678 }, X86_FIXUP_DISPLACEMENT, 2, 10 },
    // mov dword ptr [rip + x], offset y (x-8, and R_X86_64_32 y); mov qword ptr [rip + x], offset y (x-8, R_X86_64_32S y)
    { { .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 2, 10 },
    { { .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_IMMEDIATE, 6, 10 },
    { { .form = X86_FORM_MOV_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 3, 11 },
    { { .form = X86_FORM_MOV_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_IMMEDIATE, 7, 11 },
    // mov word ptr [rip + x], 0x1234 (x-6)
    { { .form = X86_FORM_MOV_RM16_IMM16 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP, .immediate = 0x1234 }, X86_FIXUP_DISPLACEMENT, 3, 9 },
    // mov eax, dword ptr gs:[rip + x] (x-4)
    { { .form = (X86_FORM_MOV_R32_RM32 + BUSTER_X86_FORM_GS) | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 3, 7 },
    // vpternlogd zmm0, zmm1, zmmword ptr [rip + x], 5 (x-5)
    { { .form = X86_FORM_VPTERNLOGD_Z_Z_ZM512_IMM8 | BUSTER_X86_FORM_FIXUP, .vvvv = 1, .mode = BUSTER_X86_MEMORY_RIP, .immediate = 5 }, X86_FIXUP_DISPLACEMENT, 6,
      11 },
    // vpblendvb xmm1, xmm2, xmmword ptr [rip + x], xmm3 (x-5)
    { { .form = X86_FORM_VPBLENDVB_X_X_XM128_X | BUSTER_X86_FORM_FIXUP, .reg = 1, .vvvv = 2, .mode = BUSTER_X86_MEMORY_RIP, .immediate = 3 << 4 },
      X86_FIXUP_DISPLACEMENT, 5, 10 },
    // bextr rax, qword ptr [rip + x], offset y (x-8, and R_X86_64_32S y)
    { { .form = X86_FORM_BEXTR_R64_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_DISPLACEMENT, 5, 13 },
    { { .form = X86_FORM_BEXTR_R64_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_RIP }, X86_FIXUP_IMMEDIATE, 9, 13 },
    // mov eax, dword ptr [rbx + x], with a placeholder that doesn't fit in 8 bits; mov eax, dword ptr ds:x;
    // mov eax, dword ptr [x + rcx*4] (R_X86_64_32S x)
    { { .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = INT32_MIN },
      X86_FIXUP_DISPLACEMENT, 2, 6 },
    { { .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_ABSOLUTE }, X86_FIXUP_DISPLACEMENT, 3, 7 },
    { { .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP, .index = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_INDEX(2) }, X86_FIXUP_DISPLACEMENT, 3, 7 },
    // movabs rax, offset y; movabs al, ds:y; jmpabs y (REX2 always) (R_X86_64_64 y)
    { { .form = X86_FORM_MOV_R64_IMM64 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 2, 10 },
    { { .form = X86_FORM_MOVABS_AL_MOFFS | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 9 },
    { { .form = X86_FORM_JMPABS_IMM64 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 3, 11 },
    // push offset y; add rcx, offset y (R_X86_64_32S y); mov eax, offset y (R_X86_64_32 y)
    { { .form = X86_FORM_PUSH_IMM32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 5 },
    { { .form = X86_FORM_ADD_RM64_IMM32 | BUSTER_X86_FORM_FIXUP, .rm = X86_REGISTER_RCX }, X86_FIXUP_IMMEDIATE, 3, 7 },
    { { .form = X86_FORM_MOV_R32_IMM32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 5 },
};

// The same in 32-bit mode, where an address can be 16-bit (x86_fixup_distance_address16): call f; data16 call f (PC32
// f-4 and PC16 f-2); jecxz f (PC8 f-1); mov ecx, dword ptr [ebx + x], with a placeholder that doesn't fit in 8 bits;
// mov cx, word ptr [bx + x], with one that doesn't fit modulo 2^16 either (R_X86_64_32 x and R_X86_64_16 x); mov ax,
// offset y (R_X86_64_16 y); mov dword ptr ds:x, offset y; push offset y (R_X86_64_32 x and y); jmp 8:y and data16
// call 8:y, far pointers whose offset is the immediate's start (R_X86_64_32 y and R_X86_64_16 y); mov eax, ds:y and
// addr16 mov ds:y, ax (R_X86_64_32 y and R_X86_64_16 y).
static const KnownFixup x86_known_fixups_32[] = {
    { { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 5 },
    { { .form = X86_FORM_CALL_REL16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 2, 4 },
    { { .form = X86_FORM_JRCXZ_REL8 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 2 },
    { { .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = INT32_MIN },
      X86_FIXUP_DISPLACEMENT, 2, 6 },
    { { .form = X86_FORM_MOV_R16_RM16 | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE,
        .displacement = INT16_MIN },
      X86_FIXUP_DISPLACEMENT, 4, 6 },
    { { .form = X86_FORM_MOV_R16_IMM16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 2, 4 },
    { { .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_ABSOLUTE }, X86_FIXUP_DISPLACEMENT, 2, 10 },
    { { .form = X86_FORM_MOV_RM32_IMM32 | BUSTER_X86_FORM_FIXUP, .mode = BUSTER_X86_MEMORY_ABSOLUTE }, X86_FIXUP_IMMEDIATE, 6, 10 },
    { { .form = X86_FORM_PUSH_IMM32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 5 },
    { { .form = X86_FORM_JMP_FAR_PTR32 | BUSTER_X86_FORM_FIXUP, .displacement = 8 }, X86_FIXUP_IMMEDIATE, 1, 7 },
    { { .form = X86_FORM_CALL_FAR_PTR16 | BUSTER_X86_FORM_FIXUP, .immediate = 8 << 16 }, X86_FIXUP_IMMEDIATE, 2, 6 },
    { { .form = X86_FORM_MOV_EAX_MOFFS32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 5 },
    { { .form = X86_FORM_MOV_MOFFS16_AX | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 3, 5 },
};

// The same in 16-bit mode: call f; data32 call f; jne f; loop f (PC16 f-2, PC32 f-4, PC16 f-2 and PC8 f-1); mov cx,
// word ptr [bx + x]; mov cx, word ptr ds:x; mov word ptr [bx + si + x], offset y (R_X86_64_16 x, and y); mov eax, dword
// ptr [ebx + x] (R_X86_64_32 x); mov ax, offset y; pushw offset y (R_X86_64_16 y); add eax, offset y (R_X86_64_32 y);
// jmp 8:y and data32 jmp 8:y (R_X86_64_16 y and R_X86_64_32 y); mov al, ds:y and addr32 mov eax, ds:y (R_X86_64_16 y
// and R_X86_64_32 y).
static const KnownFixup x86_known_fixups_16[] = {
    { { .form = X86_FORM_CALL_REL16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 3 },
    { { .form = X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 2, 6 },
    { { .form = X86_FORM_JNE_REL16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 2, 4 },
    { { .form = X86_FORM_LOOP_REL8 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_DISPLACEMENT, 1, 2 },
    { { .form = X86_FORM_MOV_R16_RM16 | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_RCX, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE, .displacement = INT16_MIN },
      X86_FIXUP_DISPLACEMENT, 2, 4 },
    { { .form = X86_FORM_MOV_R16_RM16 | BUSTER_X86_FORM_FIXUP, .reg = X86_REGISTER_RCX, .mode = BUSTER_X86_MEMORY_ABSOLUTE }, X86_FIXUP_DISPLACEMENT, 2, 4 },
    { { .form = X86_FORM_MOV_RM16_IMM16 | BUSTER_X86_FORM_FIXUP, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0),
        .displacement = INT16_MIN },
      X86_FIXUP_DISPLACEMENT, 2, 6 },
    { { .form = X86_FORM_MOV_RM16_IMM16 | BUSTER_X86_FORM_FIXUP, .rm = X86_REGISTER_RBX, .index = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE_INDEX(0),
        .displacement = INT16_MIN },
      X86_FIXUP_IMMEDIATE, 4, 6 },
    { { .form = X86_FORM_MOV_R32_RM32 | BUSTER_X86_FORM_FIXUP, .rm = X86_REGISTER_RBX, .mode = BUSTER_X86_MEMORY_BASE | BUSTER_X86_MODE_ADDRESS_SIZE, .displacement = INT32_MIN },
      X86_FIXUP_DISPLACEMENT, 4, 8 },
    { { .form = X86_FORM_MOV_R16_IMM16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 3 },
    { { .form = X86_FORM_PUSHW_IMM16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 3 },
    { { .form = X86_FORM_ADD_EAX_IMM32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 2, 6 },
    { { .form = X86_FORM_JMP_FAR_PTR16 | BUSTER_X86_FORM_FIXUP, .immediate = 8 << 16 }, X86_FIXUP_IMMEDIATE, 1, 5 },
    { { .form = X86_FORM_JMP_FAR_PTR32 | BUSTER_X86_FORM_FIXUP, .displacement = 8 }, X86_FIXUP_IMMEDIATE, 2, 8 },
    { { .form = X86_FORM_MOV_AL_MOFFS16 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 1, 3 },
    { { .form = X86_FORM_MOV_EAX_MOFFS32 | BUSTER_X86_FORM_FIXUP }, X86_FIXUP_IMMEDIATE, 3, 7 },
};

// Checks the mode's encoders' fixup ends and x86_fixup_distance (x86_fixup_distance_address16 with a 16-bit address)
// on its known fixups: the reported end minus the distance must be where GNU as puts the relocation. In 16- and 32-bit
// modes, the scalar encoder's alone.
static bool x86_check_known_fixups(void)
{
    uint8_t output[BUSTER_X86_ENCODER_CAPACITY(1)];
    uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)];
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    const KnownFixup* fixups = x86_test_mode == X86_ENCODER_MODE_32 ? x86_known_fixups_32 : x86_test_mode == X86_ENCODER_MODE_16 ? x86_known_fixups_16 : x86_known_fixups;
    const uint64_t count = x86_test_mode == X86_ENCODER_MODE_32   ? BUSTER_ARRAY_LENGTH(x86_known_fixups_32)
                           : x86_test_mode == X86_ENCODER_MODE_16 ? BUSTER_ARRAY_LENGTH(x86_known_fixups_16)
                                                                  : BUSTER_ARRAY_LENGTH(x86_known_fixups);
    uint64_t failures = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const KnownFixup* known = &fixups[i];
        const bool address16 = x86_test_address16(&known->instruction) && (known->instruction.mode & BUSTER_X86_MODE_MEMORY);
        for (uint32_t vectorized = 0; vectorized < (legacy ? 1u : 2u); vectorized += 1)
        {
            fixup_ends[0] = UINT32_MAX;
            const int64_t length = vectorized ? x86_test_encode(&known->instruction, 1, output, sizeof(output), 0, fixup_ends)
                                              : x86_test_encode_scalar(&known->instruction, 1, output, sizeof(output), 0, fixup_ends);
            const uint32_t distance = address16 ? x86_fixup_distance_address16(known->instruction.form, known->field) : x86_fixup_distance(known->instruction.form, known->field);
            if (length != known->length || fixup_ends[0] != known->length || fixup_ends[0] - distance != known->offset)
            {
                if (failures < BUSTER_X86_TEST_REPORTED)
                {
                    printf("  %s: ", vectorized ? "x86_encode" : "x86_encode_scalar");
                    x86_print_instruction(stdout, &known->instruction, 1u << known->field);
                    printf(": %lld bytes, end %u, field at %d; expected %u bytes, field at %u\n", (long long)length, fixup_ends[0],
                           (int)fixup_ends[0] - (int)distance, known->length, known->offset);
                }
                failures += 1;
            }
        }
    }
    printf("known fixups%s: %lu fields, %lu wrong\n", x86_test_mode_words(), (unsigned long)count, (unsigned long)failures);
    return failures == 0;
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
    const int64_t length = x86_test_encode_scalar(instructions, BUSTER_X86_TEST_DIGEST_INSTRUCTIONS, output, BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_TEST_DIGEST_INSTRUCTIONS), 0, 0);
    const uint64_t digest = length >= 0 ? x86_fnv1a(output, (uint64_t)length) : 0;
    const uint64_t recorded = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? BUSTER_X86_TEST_DIGEST_ADDRESS32
                              : x86_test_mode == X86_ENCODER_MODE_APX     ? BUSTER_X86_TEST_DIGEST_APX
                              : x86_test_mode == X86_ENCODER_MODE_32      ? BUSTER_X86_TEST_DIGEST_32
                              : x86_test_mode == X86_ENCODER_MODE_16      ? BUSTER_X86_TEST_DIGEST_16
                                                                          : BUSTER_X86_TEST_DIGEST;
    const bool result = digest == recorded;
    printf("reference digest%s: %d instructions, %lld bytes, 0x%016llx (%s)\n", x86_test_mode_words(), BUSTER_X86_TEST_DIGEST_INSTRUCTIONS,
           (long long)length, (unsigned long long)digest, result ? "matches the one GNU as checked" : "DIFFERENT FROM THE ONE GNU AS CHECKED");
    return result;
}

typedef int64_t X86EncodeFunction(const X86Instruction* instructions, uint64_t count, uint8_t* output, uint32_t* offsets, uint32_t* fixup_ends);

typedef struct X86Variant X86Variant;
struct X86Variant
{
    const char* name;
    X86EncodeFunction* encode;
    // Writes nothing past the instructions' bytes (rather than nothing past the capacity).
    bool exact;
    // Writes nothing past the marked instructions' fixup ends (rather than nothing past their capacity).
    bool exact_fixups;
};

static const X86Variant x86_test_variants[] = {
    { "x86_encode_4", x86_encode_4, false, false },
    { "x86_encode_8", x86_encode_8, false, false },
    { "x86_encode_16", x86_encode_16, false, false },
    { "x86_encode_32", x86_encode_32, false, false },
    { "x86_encode_64", x86_encode_64, false, false },
    { "x86_encode_16_forms_from_records", x86_encode_16_forms_from_records, false, false },
    { "x86_encode_16_masked_loads", x86_encode_16_masked_loads, false, false },
    { "x86_encode_16_masked_stores", x86_encode_16_masked_stores, true, false },
    { "x86_encode_16_compress_stores", x86_encode_16_compress_stores, true, false },
    { "x86_encode_64_fixup_compress_stores", x86_encode_64_fixup_compress_stores, false, true },
};

static const X86Variant x86_test_variants_address32[] = {
    { "x86_encode_64_address32", x86_encode_64_address32, false, false },
};

static const X86Variant x86_test_variants_apx[] = {
    { "x86_encode_64_apx", x86_encode_64_apx, false, false },
};

// The variants of the mode: every width and alternative for x86_encode, and the production loop for 32-bit addresses
// and for APX (and none for 16- and 32-bit modes, which have the scalar encoder alone).
static const X86Variant* x86_mode_variants(uint64_t* count)
{
    *count = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? BUSTER_ARRAY_LENGTH(x86_test_variants_address32)
             : x86_test_mode == X86_ENCODER_MODE_APX     ? BUSTER_ARRAY_LENGTH(x86_test_variants_apx)
             : x86_mode_is_legacy(x86_test_mode)         ? 0
                                                         : BUSTER_ARRAY_LENGTH(x86_test_variants);
    return x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? x86_test_variants_address32 : x86_test_mode == X86_ENCODER_MODE_APX ? x86_test_variants_apx : x86_test_variants;
}

// Each variant runs with each of its loops: without offsets or fixup ends, with each of them, and with both.
#define BUSTER_X86_TEST_WITH_OFFSETS 1u
#define BUSTER_X86_TEST_WITH_FIXUP_ENDS 2u
#define BUSTER_X86_TEST_OUTPUT_COMBINATIONS 4u

typedef struct X86Buffers X86Buffers;
struct X86Buffers
{
    uint8_t* expected;
    uint8_t* output;
    uint32_t* expected_offsets;
    uint32_t* offsets;
    uint32_t* expected_fixup_ends;
    uint32_t* fixup_ends;
};

// Compares a variant with the mode's scalar encoder on `count` instructions, with each combination of offsets and fixup
// ends. Prints the first difference if `report`.
static bool x86_compare_encoders(const X86Variant* variant, const X86Instruction* instructions, uint64_t count, X86Buffers* buffers, bool report)
{
    const int64_t expected = x86_test_encode_scalar(instructions, count, buffers->expected, BUSTER_X86_ENCODER_CAPACITY(count), buffers->expected_offsets,
                                                    buffers->expected_fixup_ends);
    const uint64_t marked = x86_count_marked(instructions, count);
    bool result = expected >= 0;
    for (uint32_t outputs = 0; outputs < BUSTER_X86_TEST_OUTPUT_COMBINATIONS; outputs += 1)
    {
        const bool with_offsets = (outputs & BUSTER_X86_TEST_WITH_OFFSETS) != 0;
        const bool with_fixup_ends = (outputs & BUSTER_X86_TEST_WITH_FIXUP_ENDS) != 0;
        const int64_t length = variant->encode(instructions, count, buffers->output, with_offsets ? buffers->offsets : 0, with_fixup_ends ? buffers->fixup_ends : 0);
        const bool same = expected >= 0 && length == expected && memcmp(buffers->output, buffers->expected, (uint64_t)expected) == 0 &&
                          (!with_offsets || memcmp(buffers->offsets, buffers->expected_offsets, count * sizeof(uint32_t)) == 0) &&
                          (!with_fixup_ends || memcmp(buffers->fixup_ends, buffers->expected_fixup_ends, marked * sizeof(uint32_t)) == 0);
        if (!same && report)
        {
            printf("  %s%s%s on %lu instructions: %lld bytes, expected %lld\n", variant->name, with_offsets ? " with offsets" : "",
                   with_fixup_ends ? " with fixup ends" : "", (unsigned long)count, (long long)length, (long long)expected);
            // The first instruction whose offset or bytes differ (where the expected offsets put it).
            uint64_t i = 0;
            bool same_instruction = length >= 0 && expected >= 0;
            while (same_instruction && i < count)
            {
                const uint32_t start = buffers->expected_offsets[i];
                const uint32_t end = i + 1 < count ? buffers->expected_offsets[i + 1] : (uint32_t)expected;
                same_instruction = (!with_offsets || buffers->offsets[i] == start) && (uint64_t)length >= end &&
                                   memcmp(buffers->output + start, buffers->expected + start, end - start) == 0;
                i += same_instruction;
            }
            if (length >= 0 && expected >= 0 && i < count)
            {
                const uint32_t start = buffers->expected_offsets[i];
                const uint32_t end = i + 1 < count ? buffers->expected_offsets[i + 1] : (uint32_t)expected;
                printf("    instruction %lu: ", (unsigned long)i);
                x86_print_instruction(stdout, &instructions[i], 0);
                printf("\n    expected at %u: ", start);
                x86_print_bytes(buffers->expected + start, end - start);
                printf("\n    got at %u:      ", with_offsets ? buffers->offsets[i] : start);
                x86_print_bytes(buffers->output + start, (uint64_t)length >= end ? end - start : 0);
                printf("\n");
            }
            // The first fixup end that differs.
            uint64_t k = 0;
            while (with_fixup_ends && k < marked && buffers->fixup_ends[k] == buffers->expected_fixup_ends[k])
            {
                k += 1;
            }
            if (with_fixup_ends && k < marked)
            {
                printf("    fixup end %lu of %lu: %u, expected %u\n", (unsigned long)k, (unsigned long)marked, buffers->fixup_ends[k],
                       buffers->expected_fixup_ends[k]);
            }
        }
        result = result && same;
    }
    return result;
}

static bool x86_check_random(uint64_t* state, uint64_t count, X86Instruction* instructions, X86Buffers* buffers)
{
    bool result = true;
    uint64_t variants = 0;
    const X86Variant* mode_variants = x86_mode_variants(&variants);
    for (uint64_t v = 0; v < variants; v += 1)
    {
        const X86Variant* variant = &mode_variants[v];
        uint64_t failures = 0;
        uint64_t marked = 0;
        for (uint64_t n = 0; n < BUSTER_X86_TEST_SHORT_COUNTS; n += 1)
        {
            x86_random_instructions(state, instructions, n, true, false);
            x86_mark_random(state, instructions, n);
            marked += x86_count_marked(instructions, n);
            failures += !x86_compare_encoders(variant, instructions, n, buffers, failures < BUSTER_X86_TEST_REPORTED);
        }
        x86_random_instructions(state, instructions, count, true, false);
        x86_mark_random(state, instructions, count);
        marked += x86_count_marked(instructions, count);
        failures += !x86_compare_encoders(variant, instructions, count, buffers, failures < BUSTER_X86_TEST_REPORTED);
        printf("random: %-35s %d short streams and %lu instructions (%lu marked), %lu differ\n", variant->name, BUSTER_X86_TEST_SHORT_COUNTS,
               (unsigned long)count, (unsigned long)marked, (unsigned long)failures);
        result = result && failures == 0;
    }
    return result;
}

// The page-edge check's input, for the fault handler.
static volatile uint64_t x86_test_page_edge_count;
static volatile uint32_t x86_test_page_edge_outputs;
static volatile const char* x86_test_page_edge_variant;

static void x86_test_report_fault(int signal_number)
{
    (void)signal_number;
    char message[200];
    const uint32_t outputs = x86_test_page_edge_outputs;
    const int length = snprintf(message, sizeof(message), "page edges: %s%s%s faulted on %lu instructions\n", x86_test_page_edge_variant,
                                (outputs & BUSTER_X86_TEST_WITH_OFFSETS) ? " with offsets" : "", (outputs & BUSTER_X86_TEST_WITH_FIXUP_ENDS) ? " with fixup ends" : "",
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
    uint64_t fixup_ends_mapped = 0;
    uint8_t* input_end = x86_map_guarded(BUSTER_X86_TEST_SHORT_COUNTS * sizeof(X86Instruction), page, &input_mapped);
    uint8_t* output_end = x86_map_guarded(BUSTER_X86_ENCODER_CAPACITY(BUSTER_X86_TEST_SHORT_COUNTS), page, &output_mapped);
    uint8_t* offsets_end = x86_map_guarded(BUSTER_X86_TEST_SHORT_COUNTS * sizeof(uint32_t), page, &offsets_mapped);
    uint8_t* fixup_ends_end = x86_map_guarded(BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_X86_TEST_SHORT_COUNTS) * sizeof(uint32_t), page, &fixup_ends_mapped);
    uint64_t failures = 0;
    bool result = input_end != 0 && output_end != 0 && offsets_end != 0 && fixup_ends_end != 0;
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
        uint64_t variants = 0;
        const X86Variant* mode_variants = x86_mode_variants(&variants);
        for (uint64_t v = 0; v < variants; v += 1)
        {
            const X86Variant* variant = &mode_variants[v];
            x86_test_page_edge_variant = variant->name;
            for (uint32_t outputs = 0; outputs < BUSTER_X86_TEST_OUTPUT_COMBINATIONS; outputs += 1)
            {
                const bool with_offsets = (outputs & BUSTER_X86_TEST_WITH_OFFSETS) != 0;
                const bool with_fixup_ends = (outputs & BUSTER_X86_TEST_WITH_FIXUP_ENDS) != 0;
                x86_test_page_edge_outputs = outputs;
                for (uint64_t n = 0; n < BUSTER_X86_TEST_SHORT_COUNTS; n += 1)
                {
                    x86_test_page_edge_count = n;
                    x86_random_instructions(state, scratch, n, true, false);
                    x86_mark_random(state, scratch, n);
                    X86Instruction* instructions = (X86Instruction*)(input_end - n * sizeof(X86Instruction));
                    memcpy(instructions, scratch, n * sizeof(X86Instruction));
                    const int64_t expected = x86_test_encode_scalar(instructions, n, buffers->expected, BUSTER_X86_ENCODER_CAPACITY(n), buffers->expected_offsets,
                                                               buffers->expected_fixup_ends);
                    const uint64_t marked = x86_count_marked(instructions, n);
                    // The output ends where the instructions do (for an exact variant) or where its capacity does, and
                    // the fixup ends where the marked instructions' do or where their capacity does.
                    uint8_t* output = output_end - (variant->exact ? (uint64_t)(expected > 0 ? expected : 0) : BUSTER_X86_ENCODER_CAPACITY(n));
                    uint32_t* offsets = (uint32_t*)(offsets_end - n * sizeof(uint32_t));
                    uint32_t* fixup_ends = (uint32_t*)(fixup_ends_end - (variant->exact_fixups ? marked : BUSTER_X86_ENCODER_FIXUP_CAPACITY(marked)) * sizeof(uint32_t));
                    const int64_t length = variant->encode(instructions, n, output, with_offsets ? offsets : 0, with_fixup_ends ? fixup_ends : 0);
                    const bool same = length == expected && memcmp(output, buffers->expected, (uint64_t)(expected > 0 ? expected : 0)) == 0 &&
                                      (!with_offsets || memcmp(offsets, buffers->expected_offsets, n * sizeof(uint32_t)) == 0) &&
                                      (!with_fixup_ends || memcmp(fixup_ends, buffers->expected_fixup_ends, marked * sizeof(uint32_t)) == 0);
                    if (!same && failures < BUSTER_X86_TEST_REPORTED)
                    {
                        printf("  %s%s%s on %lu instructions at page edges: %lld bytes, expected %lld\n", variant->name, with_offsets ? " with offsets" : "",
                               with_fixup_ends ? " with fixup ends" : "", (unsigned long)n, (long long)length, (long long)expected);
                    }
                    failures += !same;
                }
            }
        }
        sigaction(SIGSEGV, &previous_segv, 0);
        sigaction(SIGBUS, &previous_bus, 0);
        printf("page edges%s: 0 to %d instructions ending at a page end, machine code (or its capacity), offsets and fixup ends (or their capacity) too, %lu "
               "differ\n",
               x86_test_mode_words(), BUSTER_X86_TEST_SHORT_COUNTS - 1, (unsigned long)failures);
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
    if (fixup_ends_end != 0)
    {
        munmap(fixup_ends_end - (fixup_ends_mapped - 2 * page) - page, fixup_ends_mapped);
    }
    return result;
}

// The lowest bit of a register operand of the class (in a form of the family) past its last register (or of a default
// flags value, past 15), or for a group, its low bit (for control registers, past CR15: x86_validate alone finds the
// numbers below without a register). With `extended` (APX's forms with REX2), a general register's last is R31.
static uint8_t x86_past_last_register(uint16_t operand, uint8_t family, bool extended)
{
    const uint8_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint8_t result = 0x10;
    if (class == BUSTER_X86_CLASS_OPMASK || class == BUSTER_X86_CLASS_ST || class == BUSTER_X86_CLASS_MMX || class == BUSTER_X86_CLASS_DEBUG ||
        class == BUSTER_X86_CLASS_SEGMENT || class == BUSTER_X86_CLASS_TILE)
    {
        result = 0x08;
    }
    else if (class == BUSTER_X86_CLASS_BND)
    {
        result = 0x04;
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && family == BUSTER_X86_FAMILY_EVEX)
    {
        result = 0x20;
    }
    else if (extended && x86_operand_is_general(operand))
    {
        result = 0x20;
    }
    result = (operand & (BUSTER_X86_OPERAND_PAIR | BUSTER_X86_OPERAND_BLOCK)) ? 0x01 : result;
    return result;
}

// The field with a range that a form has: 1, 2 or 3 for an immediate of that many bytes, 4 for a rel8, 5 for an /is4
// register, 6 for a rel16, 7 for a ptr16:32's selector (in 16- and 32-bit modes), and 0 for none (imm32, a 64-bit
// immediate and rel32 take any value).
static uint32_t x86_range_kind(const X86FormDescription* form)
{
    return x86_form_has_is4(form)        ? 5
           : form->relative_bytes == 1   ? 4
           : form->relative_bytes == 2   ? 6
           : form->immediate_bytes == 6  ? 7
           : form->immediate_bytes <= 3  ? form->immediate_bytes
                                         : 0;
}


// In 16- and 32-bit modes, the longest instruction of a form whose memory operand is a general one: with a segment
// override and a 32-bit address (with a 67 in 16-bit mode), a base, an index and a 32-bit displacement, and the form's
// other operands random.
static X86Instruction x86_longest_legacy_instruction(uint64_t* state, uint16_t row)
{
    X86Instruction result = x86_random_instruction(state, row, false);
    result.form = (uint16_t)(row + BUSTER_X86_FORM_FS);
    result.mode = (uint8_t)(BUSTER_X86_MEMORY_BASE_INDEX(2) | (x86_test_mode == X86_ENCODER_MODE_16 ? BUSTER_X86_MODE_ADDRESS_SIZE : 0));
    result.rm = X86_REGISTER_RAX;
    result.index = X86_REGISTER_RCX;
    result.displacement = 0x12345678;
    return result;
}

// Whether, in 16- and 32-bit modes, the form's longest instruction (x86_longest_legacy_instruction's) is longer than
// x86's 15 bytes: in 16-bit mode, with the 66 of a 32-bit form and a 67, HLE's locked arithmetic by an imm32.
static bool x86_form_overflows_in_mode(uint64_t* state, uint16_t row)
{
    const X86FormDescription* form = &x86_form_descriptions[row];
    const bool general = (form->rm & BUSTER_X86_OPERAND_MEMORY) && !(form->rm & (BUSTER_X86_OPERAND_VSIB | BUSTER_X86_OPERAND_SIB));
    bool result = false;
    if (general && x86_test_form_exists(row))
    {
        const X86Instruction longest = x86_longest_legacy_instruction(state, row);
        uint8_t bytes[32];
        result = encode_one(&longest, bytes, x86_test_mode) > BUSTER_X86_MAX_INSTRUCTION_BYTES;
    }
    return result;
}

// Breaks one operand of a valid instruction in one of the ways x86_encode checks, or in one that only x86_validate
// checks (and then sets *validate_only). Returns false if this instruction can't be broken that way.
static bool x86_break_instruction(uint64_t* state, X86Instruction* instruction, bool* validate_only)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool memory = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
    const bool vsib = (form->rm & BUSTER_X86_OPERAND_VSIB) != 0;
    const bool sib = (form->rm & BUSTER_X86_OPERAND_SIB) != 0;
    // With APX, a form whose general registers, base and general index can be 16 to 31.
    const bool extended = x86_test_mode == X86_ENCODER_MODE_APX && x86_form_has_apx_registers(instruction->form);
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    bool result = true;
    // The ways after the first 23 are for some modes alone: 23 for 32-bit addresses, 24 and 28 for all, 25 to 27 for
    // APX, and 29 to 31 for 16- and 32-bit modes.
    static const uint8_t ways_64[] = { 24, 28 };
    static const uint8_t ways_address32[] = { 23, 24, 28 };
    static const uint8_t ways_apx[] = { 24, 25, 26, 27, 28 };
    static const uint8_t ways_legacy[] = { 24, 28, 29, 30, 31 };
    const uint8_t* ways = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? ways_address32 : x86_test_mode == X86_ENCODER_MODE_APX ? ways_apx : legacy ? ways_legacy : ways_64;
    const uint32_t mode_ways = x86_test_mode == X86_ENCODER_MODE_ADDRESS32 ? BUSTER_ARRAY_LENGTH(ways_address32)
                               : x86_test_mode == X86_ENCODER_MODE_APX     ? BUSTER_ARRAY_LENGTH(ways_apx)
                               : legacy                                    ? BUSTER_ARRAY_LENGTH(ways_legacy)
                                                                           : BUSTER_ARRAY_LENGTH(ways_64);
    const uint32_t pick = x86_random_below(state, 23 + mode_ways);
    switch (pick < 23 ? pick : ways[pick - 23])
    {
        case 0:
            // A high byte register with an extended register (and so a REX prefix, or with APX, maybe REX2).
            if ((form->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_GPR8 && form->encoding == BUSTER_X86_ENCODING_R &&
                (form->rm & BUSTER_X86_OPERAND_REGISTER) && !memory)
            {
                instruction->reg = X86_REGISTER_AH;
                instruction->rm = (uint8_t)(8 + x86_random_below(state, extended ? 24 : 8));
            }
            else
            {
                result = false;
            }
            break;
        case 1:
            // RSP as a general index (a VSIB one can be XMM4, and a SIB one's is none).
            if (memory && (instruction->mode & BUSTER_X86_MODE_INDEX) && !vsib && !sib)
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
            // A register number past the class's last (15, or 31 for an EVEX form's vector registers and for APX's
            // general registers in a form with REX2, or 7 for an opmask or an x87, MMX, debug or segment register), in
            // reg, in a register rm, or in a memory operand's base or index (general registers, so 15 or 31, but for a
            // VSIB index of an EVEX form, 31).
            if (form->encoding == BUSTER_X86_ENCODING_R && x86_random_below(state, 2) == 0)
            {
                instruction->reg |= x86_past_last_register(form->reg, form->family, extended);
            }
            else if (form->rm != BUSTER_X86_OPERAND_NONE && !memory)
            {
                instruction->rm |= x86_past_last_register(form->rm, form->family, extended);
            }
            else if (memory && (instruction->mode & BUSTER_X86_MODE_BASE))
            {
                instruction->rm |= extended ? 0x20 : 0x10;
            }
            else if (memory && (instruction->mode & BUSTER_X86_MODE_INDEX))
            {
                instruction->index |= vsib ? x86_past_last_register(form->rm, form->family, false) : extended ? 0x20 : 0x10;
            }
            else
            {
                result = false;
            }
            break;
        case 6:
            // vvvv past the class's last, or in a form without it.
            instruction->vvvv = form->vvvv != BUSTER_X86_OPERAND_NONE ? (uint8_t)(instruction->vvvv | x86_past_last_register(form->vvvv, form->family, extended))
                                                                     : (uint8_t)(1 + x86_random_below(state, 31));
            break;
        case 7:
        {
            // A decoration the form can't have (bits 3 and 4 never can, but in 16- and 32-bit modes, where bits 3 to 5
            // are a segment override, which case 31 breaks, only the others).
            const uint8_t forbidden = (uint8_t)((x86_template_checks(form) >> 24) & (legacy ? ~BUSTER_X86_DECORATION_SEGMENT : 0xff));
            uint32_t bit = x86_random_below(state, 8);
            while (forbidden != 0 && ((forbidden >> bit) & 1) == 0)
            {
                bit = x86_random_below(state, 8);
            }
            instruction->decorations |= (uint8_t)(forbidden != 0 ? 1u << bit : 0);
            result = forbidden != 0;
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
            // Unknown mode bits (but in 16- and 32-bit modes, BUSTER_X86_MODE_ADDRESS_SIZE's).
            if (form->rm != BUSTER_X86_OPERAND_NONE)
            {
                instruction->mode |= x86_random_below(state, 2) == 0 && !legacy ? 0x40 : 0x80;
            }
            else
            {
                result = false;
            }
            break;
        case 11:
            // An index register (1 to 31) or a scale without an index.
            if (form->rm != BUSTER_X86_OPERAND_NONE && !(instruction->mode & BUSTER_X86_MODE_INDEX))
            {
                if (x86_random_below(state, 2) == 0)
                {
                    instruction->index = (uint8_t)(1 + x86_random_below(state, 31));
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
        case 14:
        {
            // An immediate or a relative displacement just outside its field's range, above or below: imm8's [-128,
            // 255], imm16's [-32768, 65535], ENTER's [0, 2^24), rel8's [-128, 127] or rel16's [-32768, 32767] (imm32, a
            // 64-bit immediate and rel32 take any value), or in 16- and 32-bit modes a ptr16:32's selector's [-32768,
            // 65535]; or an /is4 register past the last (16) or with one of bits 3 to 0 set (or below 0, for one with a
            // selector there). x86_encode writes the field's low bytes, so only x86_validate checks this. In a new
            // instruction of a random form with such a field, each kind of field as often, as only one form (ENTER) has
            // the third and few have a rel8, a rel16 or a far pointer.
            const uint32_t kind = 1 + x86_random_below(state, legacy ? 7 : 6);
            uint16_t form_index = x86_random_form(state, false);
            while (x86_range_kind(&x86_form_descriptions[form_index]) != kind)
            {
                form_index = x86_random_form(state, false);
            }
            *instruction = x86_random_instruction(state, form_index, false);
            const bool above = x86_random_below(state, 2) == 0;
            switch (kind)
            {
                case 1: instruction->immediate = above ? 256 : -129; break;
                case 2: instruction->immediate = above ? 65536 : -32769; break;
                case 3: instruction->immediate = above ? 1 << 24 : -1; break;
                case 5:
                {
                    const bool selector = x86_form_has_is4_selector(&x86_form_descriptions[form_index]);
                    instruction->immediate = above ? 16 << 4 : selector ? -1 : instruction->immediate | (int32_t)(1 + x86_random_below(state, 15));
                    break;
                }
                case 6: instruction->displacement = above ? 32768 : -32769; break;
                case 7: instruction->displacement = above ? 65536 : -32769; break;
                default: instruction->displacement = above ? 128 : -129; break;
            }
            *validate_only = true;
            break;
        }
        case 15:
        {
            // A control register that doesn't exist (CR1, CR5 to CR7 or CR9 to CR15), a segment register past GS (6 or
            // 7), or CS as MOV's destination: numbers the encoding has, which only x86_validate checks. In a new
            // instruction of a form with a control or segment register in ModRM.reg.
            // (In 16- and 32-bit modes, the moves to and from control registers are 32-bit.)
            static const uint16_t forms[] = { X86_FORM_MOV_CR_R64, X86_FORM_MOV_R64_CR, X86_FORM_MOV_SREG_RM16, X86_FORM_MOV_R32_SREG };
            static const uint16_t legacy_forms[] = { X86_FORM_MOV_CR_R32, X86_FORM_MOV_R32_CR, X86_FORM_MOV_SREG_RM16, X86_FORM_MOV_R32_SREG };
            static const uint8_t missing_control[] = { 1, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15 };
            const uint32_t chosen = x86_random_below(state, BUSTER_ARRAY_LENGTH(forms));
            const uint16_t form_index = legacy ? legacy_forms[chosen] : forms[chosen];
            *instruction = x86_random_instruction(state, form_index, false);
            if ((x86_form_descriptions[form_index].reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_CONTROL)
            {
                instruction->reg = missing_control[x86_random_below(state, BUSTER_ARRAY_LENGTH(missing_control))];
            }
            else if (form_index == X86_FORM_MOV_SREG_RM16 && x86_random_below(state, 2) == 0)
            {
                instruction->reg = 1;
            }
            else
            {
                instruction->reg = (uint8_t)(6 + x86_random_below(state, 2));
            }
            *validate_only = true;
            break;
        }
        case 16:
        {
            // A complex multiplication (AVX512-FP16's vfmulcph and the rest) into one of its sources, an AMX product
            // of tiles with one twice, or push2 or pop2 with RSP or pop2 with one register twice, which only
            // x86_validate checks: in a new instruction of such a form, ModRM.reg's register in VEX.V'vvvv or a
            // register rm, or for a product of tiles, also its rm tile in vvvv; and for push2 and pop2, RSP in vvvv or
            // rm, or for pop2, rm's register in vvvv.
            uint16_t form_index = x86_random_form(state, false);
            const X86FormDescription* chosen = &x86_form_descriptions[form_index];
            while (!x86_form_has_distinct_destination(chosen) && !x86_form_has_distinct_tiles(chosen) && !x86_form_pushes_or_pops_two(chosen))
            {
                form_index = x86_random_form(state, false);
                chosen = &x86_form_descriptions[form_index];
            }
            *instruction = x86_random_instruction(state, form_index, false);
            if (x86_form_pushes_or_pops_two(chosen))
            {
                const uint32_t pick = x86_random_below(state, chosen->opcode == 0x048f ? 3 : 2);
                if (pick == 0)
                {
                    instruction->vvvv = X86_REGISTER_RSP;
                }
                else if (pick == 1)
                {
                    instruction->rm = X86_REGISTER_RSP;
                }
                else
                {
                    instruction->vvvv = instruction->rm;
                }
            }
            else
            {
                const uint32_t pair = x86_random_below(state, x86_form_has_distinct_tiles(chosen) ? 3 : 2);
                if (instruction->mode == BUSTER_X86_REGISTER_DIRECT && pair == 0)
                {
                    instruction->rm = instruction->reg;
                }
                else if (pair == 2)
                {
                    instruction->vvvv = instruction->rm;
                }
                else
                {
                    instruction->vvvv = instruction->reg;
                }
            }
            *validate_only = true;
            break;
        }
        case 17:
        {
            // A VSIB operand without an index (a base alone, an absolute address or RIP), an EVEX gather or scatter
            // without an opmask, or a gather whose destination is its index or AVX2's mask, or whose index is that
            // mask, which only x86_validate checks: in a new instruction of a gather or scatter form.
            uint16_t form_index = x86_random_form(state, false);
            while (!(x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_VSIB))
            {
                form_index = x86_random_form(state, false);
            }
            const X86FormDescription* vsib_form = &x86_form_descriptions[form_index];
            const bool evex = vsib_form->family == BUSTER_X86_FAMILY_EVEX;
            const bool gather = vsib_form->encoding == BUSTER_X86_ENCODING_R && vsib_form->syntax != X86_SYNTAX_RM_REG;
            *instruction = x86_random_instruction(state, form_index, false);
            // AVX2's forms are all gathers, without an opmask; a scatter's registers can be the same, and a prefetching
            // gather has no destination.
            uint32_t kind = x86_random_below(state, 3);
            kind = kind == 1 && !evex ? 2 : kind == 2 && !gather ? 1 : kind;
            if (kind == 0)
            {
                static const uint8_t modes[] = { BUSTER_X86_MEMORY_BASE, BUSTER_X86_MEMORY_ABSOLUTE, BUSTER_X86_MEMORY_RIP };
                instruction->mode = modes[x86_random_below(state, BUSTER_ARRAY_LENGTH(modes))];
                instruction->rm = (instruction->mode & BUSTER_X86_MODE_BASE) ? instruction->rm : 0;
                instruction->index = 0;
            }
            else if (kind == 1)
            {
                instruction->decorations &= (uint8_t)~BUSTER_X86_DECORATION_MASK;
            }
            else
            {
                const uint32_t pair = vsib_form->vvvv != BUSTER_X86_OPERAND_NONE ? x86_random_below(state, 3) : 0;
                if (pair == 0)
                {
                    instruction->index = instruction->reg;
                }
                else if (pair == 1)
                {
                    instruction->vvvv = instruction->reg;
                }
                else
                {
                    instruction->vvvv = instruction->index;
                }
            }
            *validate_only = true;
            break;
        }
        case 18:
        {
            // A SIB operand (AMX's) without a SIB byte (a base alone or RIP), or with SIB's none and a scale, which
            // only x86_validate checks: in a new instruction of a form with one (none in 16- and 32-bit modes, where
            // AMX doesn't exist).
            if (legacy)
            {
                result = false;
            }
            else
            {
                uint16_t form_index = x86_random_form(state, false);
                while (!(x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_SIB))
                {
                    form_index = x86_random_form(state, false);
                }
                *instruction = x86_random_instruction(state, form_index, false);
                const uint32_t kind = x86_random_below(state, 3);
                if (kind == 0)
                {
                    instruction->mode = BUSTER_X86_MEMORY_BASE;
                    instruction->rm = (uint8_t)x86_random_below(state, 16);
                    instruction->index = 0;
                }
                else if (kind == 1)
                {
                    instruction->mode = BUSTER_X86_MEMORY_RIP;
                    instruction->rm = 0;
                    instruction->index = 0;
                }
                else
                {
                    instruction->mode = (uint8_t)((instruction->mode & BUSTER_X86_MODE_BASE) | BUSTER_X86_MEMORY_INDEX(1 + x86_random_below(state, 3)));
                    instruction->index = X86_REGISTER_RSP;
                }
                *validate_only = true;
            }
            break;
        }
        case 19:
        {
            // A group's register that isn't a multiple of its size (an opmask or tile pair's odd one, or a block of four's
            // with either low bit), which x86_encode rejects: in a new instruction of a form with one, in ModRM.reg or
            // VEX.vvvv.
            const uint16_t groups = BUSTER_X86_OPERAND_PAIR | BUSTER_X86_OPERAND_BLOCK;
            uint16_t form_index = x86_random_form(state, false);
            while (!((x86_form_descriptions[form_index].reg | x86_form_descriptions[form_index].vvvv) & groups))
            {
                form_index = x86_random_form(state, false);
            }
            const X86FormDescription* group_form = &x86_form_descriptions[form_index];
            *instruction = x86_random_instruction(state, form_index, false);
            const uint16_t group = (group_form->reg & groups) ? group_form->reg : group_form->vvvv;
            const uint8_t low = (uint8_t)(1 + x86_random_below(state, (group & BUSTER_X86_OPERAND_BLOCK) ? 3 : 1));
            if (group_form->reg & groups)
            {
                instruction->reg |= low;
            }
            else
            {
                instruction->vvvv |= low;
            }
            break;
        }
        case 20:
        {
            // A RIP-relative operand of MPX's bndmk or a MIB operand, or a MIB operand's scaled index, which only
            // x86_validate checks: in a new instruction of a form with one. Those forms take only memory, so with
            // 32-bit addresses, x86_random_form doesn't pick them (and case 23 breaks them).
            if (x86_test_mode == X86_ENCODER_MODE_ADDRESS32)
            {
                result = false;
            }
            else
            {
                uint16_t form_index = x86_random_form(state, false);
                while (!(x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_NO_RIP) || (x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_SIB))
                {
                    form_index = x86_random_form(state, false);
                }
                *instruction = x86_random_instruction(state, form_index, false);
                if ((x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_UNSCALED) && x86_random_below(state, 2) == 0)
                {
                    instruction->index = (instruction->mode & BUSTER_X86_MODE_INDEX) ? instruction->index : X86_REGISTER_RCX;
                    instruction->mode = (uint8_t)((instruction->mode & BUSTER_X86_MODE_BASE) | BUSTER_X86_MEMORY_INDEX(1 + x86_random_below(state, 3)));
                }
                else
                {
                    instruction->mode = BUSTER_X86_MEMORY_RIP;
                    instruction->rm = 0;
                    instruction->index = 0;
                }
                *validate_only = true;
            }
            break;
        }
        case 21:
        {
            // A 3DNow! form's immediate other than its operation byte, which only x86_validate checks: in a new instruction
            // of such a form.
            uint16_t form_index = x86_random_form(state, false);
            while (!x86_form_has_operation_byte(&x86_form_descriptions[form_index]))
            {
                form_index = x86_random_form(state, false);
            }
            *instruction = x86_random_instruction(state, form_index, false);
            instruction->immediate ^= (int32_t)(1 + x86_random_below(state, 255));
            *validate_only = true;
            break;
        }
        case 22:
        {
            // A NOTRACK form through memory with a segment override, which only x86_validate checks: in a new
            // instruction of such a form (none in 16- and 32-bit modes yet).
            if (legacy)
            {
                result = false;
            }
            else
            {
                uint16_t form_index = x86_random_form(state, false);
                while (!(x86_form_has_prefix(&x86_form_descriptions[form_index], 0x3e) && (x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_MEMORY)))
                {
                    form_index = x86_random_form(state, false);
                }
                *instruction = x86_random_instruction(state, form_index, false);
                instruction->form = (uint16_t)((instruction->form % X86_FORM_COUNT) + (x86_random_below(state, 2) == 0 ? BUSTER_X86_FORM_FS : BUSTER_X86_FORM_GS));
                *validate_only = true;
            }
            break;
        }
        case 23:
        {
            // With 32-bit addresses, an MPX memory operand (#UD with a 67), which only x86_validate checks: a base
            // alone, which every MPX form with a memory operand can have with 64-bit addresses (bndmk and the MIB
            // operands too), in a new instruction of one. By hand, as x86_random_instruction finds none of the forms
            // that take only memory (and x86_random_form doesn't pick them).
            uint16_t form_index = (uint16_t)(1 + x86_random_below(state, X86_FORM_COUNT - 1));
            while ((x86_form_descriptions[form_index].reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_BND || !(x86_form_descriptions[form_index].rm & BUSTER_X86_OPERAND_MEMORY))
            {
                form_index = (uint16_t)(1 + x86_random_below(state, X86_FORM_COUNT - 1));
            }
            *instruction = (X86Instruction){
                .form = form_index,
                .reg = (uint8_t)x86_random_below(state, 4),
                .rm = (uint8_t)x86_random_below(state, 16),
                .mode = BUSTER_X86_MEMORY_BASE,
                .displacement = x86_random_displacement(state, 1),
            };
            *validate_only = true;
            break;
        }
        case 24:
        {
            // A memory operand in a form whose template has no room for its bytes (a segment override's, or a 32-bit
            // address's 67: see x86_form_lacks_room), which the encoder rejects too: in a new instruction of one such
            // form's row, chosen evenly (none if every template has room), with a memory operand (bextr's, LWP's and
            // map 4's rm can be a register), given the form's segment override. In 16- and 32-bit modes, which have no
            // templates, an instruction longer than 15 bytes: a form's longest (x86_longest_legacy_instruction), chosen
            // evenly among those that overflow in the mode (none in 32-bit mode).
            if (legacy)
            {
                static uint16_t overflowing[2][X86_FORM_COUNT];
                static uint32_t overflowing_count[2];
                static bool listed[2];
                const uint32_t mode16 = x86_test_mode == X86_ENCODER_MODE_16;
                for (uint16_t row = 1; row < X86_FORM_COUNT && !listed[mode16]; row += 1)
                {
                    if (x86_form_overflows_in_mode(state, row))
                    {
                        overflowing[mode16][overflowing_count[mode16]] = row;
                        overflowing_count[mode16] += 1;
                    }
                }
                listed[mode16] = true;
                if (overflowing_count[mode16] == 0)
                {
                    result = false;
                }
                else
                {
                    const uint16_t row = overflowing[mode16][x86_random_below(state, overflowing_count[mode16])];
                    *instruction = x86_longest_legacy_instruction(state, row);
                }
            }
            else
            {
                uint32_t unfit = 0;
                for (uint32_t form_index = 1; form_index < BUSTER_X86_FORM_TEMPLATES; form_index += 1)
                {
                    unfit += x86_form_lacks_room(form_index, x86_test_mode);
                }
                const uint32_t chosen = unfit != 0 ? x86_random_below(state, unfit) : 0;
                uint32_t form_index = 1;
                uint32_t seen = 0;
                while (unfit != 0 && !(x86_form_lacks_room(form_index, x86_test_mode) && seen == chosen))
                {
                    seen += x86_form_lacks_room(form_index, x86_test_mode);
                    form_index += 1;
                }
                if (unfit == 0)
                {
                    result = false;
                }
                else
                {
                    // As the 64-bit mode makes it, since the row's memory operands may have no room in this mode either
                    // (map 4's imm32 forms with a 67).
                    const uint16_t row = (uint16_t)(form_index % X86_FORM_COUNT);
                    const X86EncoderMode mode = x86_test_mode;
                    x86_test_mode = X86_ENCODER_MODE_64;
                    *instruction = x86_random_instruction(state, row, false);
                    while (!(instruction->mode & BUSTER_X86_MODE_MEMORY))
                    {
                        *instruction = x86_random_instruction(state, row, false);
                    }
                    x86_test_mode = mode;
                    instruction->form = (uint16_t)form_index;
                }
            }
            break;
        }
        case 25:
        {
            // With APX, a high byte register with REX2: in a new instruction of a form with REX2 and a byte register in
            // ModRM.reg, AH to BH there, and 16 to 31 in the register rm, the base or else the index.
            uint16_t form_index = x86_random_form(state, false);
            while (!x86_form_has_rex2(form_index) || (x86_form_descriptions[form_index].reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_GPR8 ||
                   x86_form_descriptions[form_index].encoding != BUSTER_X86_ENCODING_R)
            {
                form_index = x86_random_form(state, false);
            }
            *instruction = x86_random_instruction(state, form_index, false);
            instruction->reg = (uint8_t)((4 + x86_random_below(state, 4)) | BUSTER_X86_REGISTER_HIGH_BYTE);
            const uint8_t high = (uint8_t)(16 + x86_random_below(state, 16));
            if (instruction->mode == BUSTER_X86_REGISTER_DIRECT || (instruction->mode & BUSTER_X86_MODE_BASE))
            {
                instruction->rm = high;
            }
            else if (instruction->mode & BUSTER_X86_MODE_INDEX)
            {
                instruction->index = high;
            }
            else
            {
                result = false;
            }
            break;
        }
        case 26:
        {
            // With APX, a general register, base or general index from 16 to 31 in a form without REX2 or EVEX (the XSAVE
            // family, maps 2 and 3, VEX, and HLE's locked arithmetic by an imm32 with a segment override, which has no
            // room for REX2's payload): in a new instruction of one with any of those, the first of them from a random
            // one, in the order reg, vvvv, a register rm, the base and the index.
            uint32_t chosen = 5;
            while (chosen == 5)
            {
                *instruction = x86_random_instruction(state, x86_random_form(state, false), false);
                const X86FormDescription* candidate = x86_form_description(instruction->form);
                const bool memory_operand = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
                const bool parts[5] = {
                    candidate->encoding == BUSTER_X86_ENCODING_R && x86_operand_is_general(candidate->reg),
                    x86_operand_is_general(candidate->vvvv),
                    !memory_operand && x86_operand_is_general(candidate->rm),
                    memory_operand && (instruction->mode & BUSTER_X86_MODE_BASE),
                    memory_operand && (instruction->mode & BUSTER_X86_MODE_INDEX) && !(candidate->rm & BUSTER_X86_OPERAND_VSIB),
                };
                const uint32_t first = x86_random_below(state, 5);
                for (uint32_t k = 0; k < 5 && !x86_form_has_apx_registers(instruction->form); k += 1)
                {
                    chosen = chosen == 5 && parts[(first + k) % 5] ? (first + k) % 5 : chosen;
                }
            }
            switch (chosen)
            {
                case 0: instruction->reg |= 16; break;
                case 1: instruction->vvvv |= 16; break;
                case 4: instruction->index |= 16; break;
                default: instruction->rm |= 16; break;
            }
            break;
        }
        case 27:
        {
            // With APX, a register rm from 16 to 31 that isn't a general one (an XMM register, of SSE) in a form with REX2
            // whose rm can be memory too, so that its template's checks let a base be 16 to 31: in a new instruction of
            // one, register direct.
            uint16_t form_index = x86_random_form(state, false);
            while (!x86_form_has_rex2(form_index) || x86_operand_is_general(x86_form_descriptions[form_index].rm) ||
                   (x86_form_descriptions[form_index].rm & (BUSTER_X86_OPERAND_REGISTER | BUSTER_X86_OPERAND_MEMORY)) != (BUSTER_X86_OPERAND_REGISTER | BUSTER_X86_OPERAND_MEMORY))
            {
                form_index = x86_random_form(state, false);
            }
            *instruction = x86_random_instruction(state, form_index, false);
            instruction->form = form_index;
            instruction->mode = BUSTER_X86_REGISTER_DIRECT;
            instruction->rm = (uint8_t)(16 + x86_random_below(state, 16));
            instruction->index = 0;
            instruction->displacement = 0;
            break;
        }
        case 28:
        {
            // A form that doesn't exist in the mode, which the encoders reject: in 64-bit mode's entry points, one of
            // 16- and 32-bit modes alone (whose templates take no instruction), and in those modes one of 64-bit mode
            // alone, or in 16-bit mode push's of an imm32 (which pushd's form is there). In a new instruction of such a
            // form, made in a mode that has it.
            const X86EncoderMode mode = x86_test_mode;
            uint16_t row = (uint16_t)(1 + x86_random_below(state, X86_FORM_COUNT - 1));
            while (x86_test_form_exists(row))
            {
                row = (uint16_t)(1 + x86_random_below(state, X86_FORM_COUNT - 1));
            }
            x86_test_mode = row >= BUSTER_X86_FORM_LEGACY_MODES || (x86_form_legacy[row] & BUSTER_X86_LEGACY_IN_32) ? X86_ENCODER_MODE_32 : X86_ENCODER_MODE_64;
            *instruction = x86_random_instruction(state, row, false);
            x86_test_mode = mode;
            break;
        }
        case 29:
        {
            // In 16- and 32-bit modes, a register that would need REX: 8 to 15 in reg (but a control register's: CR8
            // is AMD's LOCK prefix there), vvvv, a register rm, the base or the index, or SPL to DIL (AH to BH without
            // the high byte flag). In a new instruction with any of those, the first of them from a random one.
            uint32_t chosen = 5;
            while (chosen == 5)
            {
                *instruction = x86_random_instruction(state, x86_random_form(state, false), false);
                const X86FormDescription* candidate = x86_form_description(instruction->form);
                const bool memory_operand = (instruction->mode & BUSTER_X86_MODE_MEMORY) != 0;
                const bool parts[5] = {
                    candidate->encoding == BUSTER_X86_ENCODING_R && (candidate->reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_CONTROL,
                    candidate->vvvv != BUSTER_X86_OPERAND_NONE,
                    !memory_operand && candidate->rm != BUSTER_X86_OPERAND_NONE,
                    memory_operand && (instruction->mode & BUSTER_X86_MODE_BASE),
                    memory_operand && (instruction->mode & BUSTER_X86_MODE_INDEX),
                };
                const uint32_t first = x86_random_below(state, 5);
                for (uint32_t k = 0; k < 5; k += 1)
                {
                    chosen = chosen == 5 && parts[(first + k) % 5] ? (first + k) % 5 : chosen;
                }
            }
            uint8_t* part = chosen == 0 ? &instruction->reg : chosen == 1 ? &instruction->vvvv : chosen == 4 ? &instruction->index : &instruction->rm;
            *part = (*part & BUSTER_X86_REGISTER_HIGH_BYTE) && x86_random_below(state, 2) == 0 ? (uint8_t)(*part & ~BUSTER_X86_REGISTER_HIGH_BYTE) : (uint8_t)(*part | 8);
            break;
        }
        case 30:
        {
            // In 16- and 32-bit modes, a memory operand the instruction's address size can't have: RIP-relative, a
            // VSIB, SIB or MPX operand with a 16-bit address, or a 16-bit address with a base other than BX, BP, SI and
            // DI, an index other than SI and DI, SI or DI as the base with an index, an index alone, a scale or a
            // displacement outside [-32768, 65535]. In a new instruction with a memory operand.
            static const uint8_t not_bases[] = { X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RSP };
            static const uint8_t not_indexes[] = { X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RBX, X86_REGISTER_RSP, X86_REGISTER_RBP };
            *instruction = x86_random_instruction(state, x86_random_form(state, false), false);
            while (!(instruction->mode & BUSTER_X86_MODE_MEMORY))
            {
                *instruction = x86_random_instruction(state, x86_random_form(state, false), false);
            }
            const X86FormDescription* chosen = x86_form_description(instruction->form);
            const bool needs32 = (chosen->rm & (BUSTER_X86_OPERAND_VSIB | BUSTER_X86_OPERAND_SIB)) || (chosen->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_BND;
            // The mode bit of a 16-bit address.
            const uint8_t address16 = x86_test_mode == X86_ENCODER_MODE_16 ? 0 : BUSTER_X86_MODE_ADDRESS_SIZE;
            const uint8_t base = x86_random_below(state, 2) == 0 ? X86_REGISTER_RBX : X86_REGISTER_RBP;
            const uint8_t index = x86_random_below(state, 2) == 0 ? X86_REGISTER_RSI : X86_REGISTER_RDI;
            const uint32_t kind = x86_random_below(state, 7);
            if (kind == 0)
            {
                instruction->mode = (uint8_t)(BUSTER_X86_MEMORY_RIP | (instruction->mode & BUSTER_X86_MODE_ADDRESS_SIZE));
                instruction->rm = 0;
                instruction->index = 0;
            }
            else if (needs32)
            {
                instruction->mode ^= BUSTER_X86_MODE_ADDRESS_SIZE;
            }
            else
            {
                static const uint8_t modes[] = { BUSTER_X86_MEMORY_BASE, BUSTER_X86_MEMORY_BASE_INDEX(0), BUSTER_X86_MEMORY_BASE_INDEX(0), BUSTER_X86_MEMORY_INDEX(0),
                                                 BUSTER_X86_MEMORY_BASE_INDEX(1), BUSTER_X86_MEMORY_BASE };
                const uint8_t scale = (uint8_t)x86_random_below(state, 3);
                instruction->mode = (uint8_t)((modes[kind - 1] + (kind == 5 ? scale : 0)) | address16);
                instruction->rm = kind == 1 ? not_bases[x86_random_below(state, 4)] : kind == 3 ? (uint8_t)(index ^ 1) : kind == 4 ? 0 : base;
                instruction->index = kind == 2 ? not_indexes[x86_random_below(state, 6)] : kind == 1 || kind == 6 ? 0 : index;
                instruction->displacement = kind == 6 ? (x86_random_below(state, 2) == 0 ? 65536 : -32769) : instruction->displacement;
            }
            break;
        }
        case 31:
        {
            // In 16- and 32-bit modes, a segment override in the decorations that can't be there: on an instruction
            // without an address (or with jcxz's or loop's, which make none), in a form with FS or GS already, or on a
            // NOTRACK form; or one past GS's.
            if (x86_test_takes_override(instruction) && x86_random_below(state, 2) == 0)
            {
                instruction->decorations = (uint8_t)((instruction->decorations & ~BUSTER_X86_DECORATION_SEGMENT) | BUSTER_X86_SEGMENT_OVERRIDE(6));
            }
            else if (!x86_test_takes_override(instruction))
            {
                instruction->decorations |= (uint8_t)BUSTER_X86_SEGMENT_OVERRIDE(x86_random_below(state, 6));
            }
            else
            {
                result = false;
            }
            break;
        }
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
    uint64_t broken_for_validate = 0;
    uint64_t failures = 0;
    for (uint64_t trial = 0; trial < BUSTER_X86_TEST_INVALID_INSTRUCTIONS; trial += 1)
    {
        // A short stream with one broken instruction somewhere in it, and some marked ones.
        const uint64_t count = 1 + x86_random_below(state, 40);
        x86_random_instructions(state, instructions, count, true, false);
        x86_mark_random(state, instructions, count);
        const uint64_t victim = x86_random_below(state, (uint32_t)count);
        bool validate_only = false;
        if (x86_break_instruction(state, &instructions[victim], &validate_only))
        {
            broken += 1;
            broken_for_validate += validate_only;
            const bool validated = x86_test_validate(instructions, count) == victim;
            // The scalar encoder validates the instructions first, so it rejects them all.
            bool rejected = x86_test_encode_scalar(instructions, count, buffers->output, BUSTER_X86_ENCODER_CAPACITY(count), 0, 0) == X86_ENCODER_ERROR_INVALID_INSTRUCTION;
            uint64_t variants = 0;
            const X86Variant* mode_variants = x86_mode_variants(&variants);
            for (uint64_t v = 0; v < variants && !validate_only; v += 1)
            {
                rejected = rejected && mode_variants[v].encode(instructions, count, buffers->output, 0, 0) == X86_ENCODER_ERROR_INVALID_INSTRUCTION &&
                           mode_variants[v].encode(instructions, count, buffers->output, 0, buffers->fixup_ends) == X86_ENCODER_ERROR_INVALID_INSTRUCTION;
            }
            if (!(validated && rejected))
            {
                if (failures < BUSTER_X86_TEST_REPORTED)
                {
                    printf("  instruction %lu of %lu (form %s, reg 0x%02x, rm 0x%02x, index 0x%02x, mode 0x%02x): %s%s\n", (unsigned long)victim,
                           (unsigned long)count, x86_form_description(instructions[victim].form)->mnemonic, instructions[victim].reg, instructions[victim].rm,
                           instructions[victim].index, instructions[victim].mode, validated ? "" : "x86_validate missed it ",
                           rejected ? "" : "an encoder accepted it");
                }
                failures += 1;
            }
        }
    }
    if (x86_mode_is_legacy(x86_test_mode))
    {
        printf("invalid instructions%s: %lu broken, %lu not caught\n", x86_test_mode_words(), (unsigned long)broken, (unsigned long)failures);
    }
    else
    {
        printf("invalid instructions%s: %lu broken (%lu in a way only x86_validate checks), %lu not caught\n", x86_test_mode_words(), (unsigned long)broken,
               (unsigned long)broken_for_validate, (unsigned long)failures);
    }
    return failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The cross-check modes, and the command

// Prints the instructions for GNU as, each with its bytes from x86_encode_scalar in a comment, and GNU as's length
// for each one in a .lengths section, a byte per instruction, so that --compare-object can find each instruction
// in GNU as's output even after one whose length differs. The marked instructions' symbolic fields are symbols plus
// the fields' values (see x86_mark_symbolic).
static bool x86_print_assembly(uint64_t seed, uint64_t* state, uint64_t count, X86Instruction* instructions, uint8_t* symbolic, uint8_t* output)
{
    x86_random_instructions(state, instructions, count, false, true);
    x86_mark_symbolic(seed, instructions, count, symbolic);
    // MPX, AMX-TRANSPOSE and AMX-TF32 aren't in GNU as's default architecture.
    printf(".intel_syntax noprefix\n.arch .mpx\n.arch .amx_transpose\n.arch .amx_tf32\n.text\n%s",
           x86_test_mode == X86_ENCODER_MODE_32 ? ".code32\n" : x86_test_mode == X86_ENCODER_MODE_16 ? ".code16\n" : "");
    for (uint64_t i = 0; i < count; i += 1)
    {
        const int64_t length = x86_test_encode_scalar(&instructions[i], 1, output, BUSTER_X86_ENCODER_CAPACITY(1), 0, 0);
        // GNU as takes the test registers with the 386's and 486's architectures alone: the 386's, as the 486's would
        // tune for the 486 too, which .arch pop keeps, and GNU as then writes a shift by 1 with an imm8.
        const bool test_register = (x86_form_description(instructions[i].form)->reg & BUSTER_X86_OPERAND_TEST) != 0;
        printf(test_register ? ".arch push\n.arch i386\n0: " : "0: ");
        x86_print_instruction(stdout, &instructions[i], symbolic[i]);
        printf("\n1: .pushsection .lengths, \"a\"; .byte 1b - 0b; .popsection # ");
        x86_print_bytes(output, length > 0 ? (uint64_t)length : 0);
        printf(test_register ? "\n.arch pop\n" : "\n");
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

// An ELF object's section, by type and name: its contents and header, or null if the file has none (or isn't a 64-bit
// little-endian ELF file whose section headers and that section are inside it).
static const uint8_t* x86_elf_section(const uint8_t* file, uint64_t length, uint32_t type, const char* name, const Elf64_Shdr** header)
{
    const uint8_t* result = 0;
    const Elf64_Ehdr* elf = (const Elf64_Ehdr*)file;
    const bool valid = length >= sizeof(Elf64_Ehdr) && memcmp(elf->e_ident, ELFMAG, SELFMAG) == 0 && elf->e_ident[EI_CLASS] == ELFCLASS64 &&
                       elf->e_ident[EI_DATA] == ELFDATA2LSB && elf->e_shentsize == sizeof(Elf64_Shdr) && elf->e_shoff <= length &&
                       (length - elf->e_shoff) / sizeof(Elf64_Shdr) >= elf->e_shnum && elf->e_shstrndx < elf->e_shnum;
    if (valid)
    {
        const Elf64_Shdr* sections = (const Elf64_Shdr*)(file + elf->e_shoff);
        const Elf64_Shdr* names = &sections[elf->e_shstrndx];
        const bool names_valid = names->sh_offset <= length && names->sh_size <= length - names->sh_offset;
        for (uint32_t i = 0; names_valid && result == 0 && i < elf->e_shnum; i += 1)
        {
            const Elf64_Shdr* section = &sections[i];
            const char* section_name = (const char*)file + names->sh_offset + section->sh_name;
            const bool named = section->sh_name < names->sh_size && strnlen(section_name, names->sh_size - section->sh_name) == strlen(name) &&
                               memcmp(section_name, name, strlen(name)) == 0;
            if (named && section->sh_type == type && section->sh_offset <= length && section->sh_size <= length - section->sh_offset)
            {
                result = file + section->sh_offset;
                *header = section;
            }
        }
    }
    return result;
}

// A relocation as the ELF object has it: the offset in .text, the type (R_X86_64_*), the addend and the symbol's name;
// and for an expected one, its instruction's index.
typedef struct X86Relocation X86Relocation;
struct X86Relocation
{
    uint64_t offset;
    uint32_t type;
    int64_t addend;
    const char* symbol;
    uint64_t instruction;
};

// The relocation GNU as emits for a symbolic field of a marked instruction that ends at `end`: its field starts
// x86_fixup_distance before the end (x86_fixup_distance_address16 with a 16-bit address). A PC-relative one (a branch,
// or a RIP-relative operand) counts from the end, so its addend is the field's value minus that distance. GNU as emits
// R_X86_64_PLT32 for a branch to a symbol without an addend and R_X86_64_PC32 with one; for another address
// R_X86_64_32S (the processor sign-extends it), but R_X86_64_32 for LEA into a 16- or 32-bit register (which truncates
// it); and for an imm32 R_X86_64_32S if the operand is 64-bit and R_X86_64_32 if not, or if it's map 7's MSR index
// (unsigned, as the printer has it). In 16- and 32-bit modes, R_X86_64_PC32 for every rel32, R_X86_64_PC16 for a rel16,
// and R_X86_64_32 or R_X86_64_16 for a 32- or 16-bit address, immediate or far pointer's offset.
static X86Relocation x86_expected_relocation(const X86Instruction* instruction, X86FixupField field, uint64_t end)
{
    const X86FormDescription* form = x86_form_description(instruction->form);
    const bool address16 = x86_test_address16(instruction) && (instruction->mode & BUSTER_X86_MODE_MEMORY);
    const uint32_t distance = address16 ? x86_fixup_distance_address16(instruction->form, field) : x86_fixup_distance(instruction->form, field);
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    X86Relocation result = { .offset = end - distance };
    if (field == X86_FIXUP_DISPLACEMENT)
    {
        const bool relative = form->relative_bytes != 0 || (instruction->mode & BUSTER_X86_MODE_RIP);
        const bool truncated = form->family == BUSTER_X86_FAMILY_LEGACY && x86_form_opcode(form) == 0x8d && (form->reg & BUSTER_X86_OPERAND_CLASS) != BUSTER_X86_CLASS_GPR64;
        const bool address32 = x86_test_mode == X86_ENCODER_MODE_ADDRESS32;
        result.addend = relative ? (int64_t)instruction->displacement - distance : instruction->displacement;
        result.type = form->relative_bytes == 1   ? R_X86_64_PC8
                      : form->relative_bytes == 2 ? R_X86_64_PC16
                      : form->relative_bytes == 4 ? (instruction->displacement == 0 && !legacy ? R_X86_64_PLT32 : R_X86_64_PC32)
                      : relative                  ? R_X86_64_PC32
                      : address16                 ? R_X86_64_16
                      : truncated || address32 || legacy ? R_X86_64_32
                                                         : R_X86_64_32S;
        result.symbol = "dsym";
    }
    else
    {
        // A 64-bit immediate or address continues in the displacement, and a far pointer's selector follows its offset.
        const bool msr_index = x86_form_has_msr_index(form);
        const bool far_pointer = form->syntax == X86_SYNTAX_FAR_POINTER;
        result.addend = form->immediate_bytes == 8 ? (int64_t)((uint64_t)(uint32_t)instruction->immediate | ((uint64_t)(uint32_t)instruction->displacement << 32))
                        : msr_index                ? (int64_t)(uint32_t)instruction->immediate
                        : far_pointer              ? x86_far_offset(instruction)
                                                   : instruction->immediate;
        result.type = form->immediate_bytes == 8                     ? R_X86_64_64
                      : form->immediate_bytes == 2 || (far_pointer && form->immediate_bytes == 4) ? R_X86_64_16
                      : (form->w || form->operand_bytes == 8) && !msr_index && !legacy ? R_X86_64_32S
                                                                                       : R_X86_64_32;
        result.symbol = "isym";
    }
    return result;
}

static void x86_print_relocation(const char* name, const X86Relocation* relocation)
{
    printf("  %-17s offset %llu, type %u, %s%+lld\n", name, (unsigned long long)relocation->offset, relocation->type, relocation->symbol,
           (long long)relocation->addend);
}

// Compares x86_encode_scalar's bytes and fixup ends with GNU as's object of the instructions --print-assembly printed:
// its .text, with the marked instructions' symbolic fields zeroed where the fixup ends put them (as GNU as leaves them),
// its .lengths and its relocations, which must be the expected ones of those fields, in order. x86_encode's fixup ends
// must be the scalar encoder's too.
static bool x86_compare_object(uint64_t seed, uint64_t* state, const char* object_path, uint64_t count, X86Instruction* instructions, uint8_t* symbolic,
                               X86Buffers* buffers)
{
    bool result = false;
    x86_random_instructions(state, instructions, count, false, true);
    x86_mark_symbolic(seed, instructions, count, symbolic);
    const uint64_t marked = x86_count_marked(instructions, count);
    uint8_t* output = buffers->expected;
    uint32_t* offsets = buffers->expected_offsets;
    uint32_t* fixup_ends = buffers->expected_fixup_ends;
    const int64_t length = x86_test_encode_scalar(instructions, count, output, BUSTER_X86_ENCODER_CAPACITY(count), offsets, fixup_ends);
    const int64_t vectorized = x86_test_encode(instructions, count, buffers->output, BUSTER_X86_ENCODER_CAPACITY(count), 0, buffers->fixup_ends);
    const bool same_fixup_ends = vectorized == length && memcmp(buffers->fixup_ends, fixup_ends, marked * sizeof(uint32_t)) == 0;

    uint64_t file_length = 0;
    uint8_t* file = x86_read_file(object_path, &file_length);
    const Elf64_Shdr* text_header = 0;
    const Elf64_Shdr* lengths_header = 0;
    const Elf64_Shdr* relocations_header = 0;
    const Elf64_Shdr* symbols_header = 0;
    const Elf64_Shdr* names_header = 0;
    const uint8_t* text = file != 0 ? x86_elf_section(file, file_length, SHT_PROGBITS, ".text", &text_header) : 0;
    const uint8_t* lengths = file != 0 ? x86_elf_section(file, file_length, SHT_PROGBITS, ".lengths", &lengths_header) : 0;
    const uint8_t* relocations = file != 0 ? x86_elf_section(file, file_length, SHT_RELA, ".rela.text", &relocations_header) : 0;
    const uint8_t* symbols = file != 0 ? x86_elf_section(file, file_length, SHT_SYMTAB, ".symtab", &symbols_header) : 0;
    const uint8_t* names = file != 0 ? x86_elf_section(file, file_length, SHT_STRTAB, ".strtab", &names_header) : 0;
    // An object without symbolic fields has no relocations. (x86_read_file reports a file it can't read.)
    const uint64_t relocation_count = relocations != 0 ? relocations_header->sh_size / sizeof(Elf64_Rela) : 0;
    const uint64_t symbol_count = symbols != 0 ? symbols_header->sh_size / sizeof(Elf64_Sym) : 0;
    if (file != 0 && (text == 0 || lengths == 0 || (relocations == 0 && marked != 0) || symbols == 0 || names == 0))
    {
        fprintf(stderr, "%s doesn't have the .text, .lengths, .rela.text, .symtab and .strtab sections of GNU as's object\n", object_path);
    }
    else if (file != 0 && lengths_header->sh_size != count)
    {
        fprintf(stderr, "%s has %lu lengths for %lu instructions\n", object_path, (unsigned long)lengths_header->sh_size, (unsigned long)count);
    }
    else if (file != 0)
    {
        // Zero the symbolic fields, at the offsets that the fixup ends and x86_fixup_distance give, and list the
        // relocations GNU as should have made of them.
        X86Relocation* expected = malloc((2 * marked + 1) * sizeof(X86Relocation));
        uint64_t expected_count = 0;
        uint64_t k = 0;
        uint64_t symbolic_fields[2] = { 0 };
        uint64_t types[R_X86_64_NUM] = { 0 };
        for (uint64_t i = 0; expected != 0 && i < count; i += 1)
        {
            if (instructions[i].form & BUSTER_X86_FORM_FIXUP)
            {
                const X86FormDescription* form = x86_form_description(instructions[i].form);
                for (uint32_t field = X86_FIXUP_DISPLACEMENT; field <= X86_FIXUP_IMMEDIATE; field += 1)
                {
                    if (symbolic[i] & (1u << field))
                    {
                        X86Relocation relocation = x86_expected_relocation(&instructions[i], (X86FixupField)field, fixup_ends[k]);
                        relocation.instruction = i;
                        const bool address16 = x86_test_address16(&instructions[i]) && (instructions[i].mode & BUSTER_X86_MODE_MEMORY);
                        // A far pointer's offset is its immediate but the selector after it.
                        const uint32_t immediate_bytes = form->syntax == X86_SYNTAX_FAR_POINTER ? form->immediate_bytes - 2u : form->immediate_bytes;
                        const uint32_t bytes = field == X86_FIXUP_IMMEDIATE ? immediate_bytes : form->relative_bytes != 0 ? form->relative_bytes : address16 ? 2 : 4;
                        if (relocation.offset + bytes <= (uint64_t)length)
                        {
                            memset(output + relocation.offset, 0, bytes);
                        }
                        expected[expected_count] = relocation;
                        expected_count += 1;
                        symbolic_fields[field] += 1;
                        types[relocation.type] += 1;
                    }
                }
                k += 1;
            }
        }

        // The bytes, instruction by instruction.
        uint64_t differences = 0;
        uint64_t assembled = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint32_t start = offsets[i];
            const uint32_t end = i + 1 < count ? offsets[i + 1] : (uint32_t)length;
            const bool same = assembled + lengths[i] <= text_header->sh_size && lengths[i] == end - start && memcmp(text + assembled, output + start, end - start) == 0;
            if (!same)
            {
                if (differences < 40)
                {
                    printf("instruction %lu: ", (unsigned long)i);
                    x86_print_instruction(stdout, &instructions[i], symbolic[i]);
                    printf("\n  x86_encode_scalar: ");
                    x86_print_bytes(output + start, end - start);
                    printf("\n  GNU as:            ");
                    x86_print_bytes(text + assembled, assembled + lengths[i] <= text_header->sh_size ? lengths[i] : 0);
                    printf("\n");
                }
                differences += 1;
            }
            assembled += lengths[i];
        }

        // The relocations. GNU as's are mostly in offset order (not a relaxed branch's), and the expected ones are in
        // instruction order, so each of GNU as's finds the expected one at its offset by binary search.
        uint64_t relocation_differences = 0;
        bool* matched = calloc(expected_count + 1, sizeof(bool));
        for (uint64_t r = 0; expected != 0 && matched != 0 && r < relocation_count; r += 1)
        {
            Elf64_Rela record;
            memcpy(&record, relocations + r * sizeof(Elf64_Rela), sizeof(record));
            const uint64_t symbol_index = ELF64_R_SYM(record.r_info);
            const char* symbol = "(none)";
            if (symbol_index < symbol_count)
            {
                Elf64_Sym symbol_record;
                memcpy(&symbol_record, symbols + symbol_index * sizeof(Elf64_Sym), sizeof(symbol_record));
                const bool named = symbol_record.st_name < names_header->sh_size &&
                                   memchr(names + symbol_record.st_name, 0, names_header->sh_size - symbol_record.st_name) != 0;
                symbol = named ? (const char*)names + symbol_record.st_name : "(bad name)";
            }
            const X86Relocation assembled_relocation = { record.r_offset, (uint32_t)ELF64_R_TYPE(record.r_info), record.r_addend, symbol };
            uint64_t low = 0;
            uint64_t high = expected_count;
            while (low < high)
            {
                const uint64_t middle = low + (high - low) / 2;
                low = expected[middle].offset < assembled_relocation.offset ? middle + 1 : low;
                high = expected[middle].offset < assembled_relocation.offset ? high : middle;
            }
            const bool found = low < expected_count && expected[low].offset == assembled_relocation.offset && !matched[low];
            const X86Relocation* reported = found ? &expected[low] : 0;
            const bool same = found && assembled_relocation.type == reported->type && assembled_relocation.addend == reported->addend &&
                              strcmp(assembled_relocation.symbol, reported->symbol) == 0;
            matched[low] = matched[low] || found;
            if (!same)
            {
                if (relocation_differences < 40)
                {
                    printf("relocation %lu:\n", (unsigned long)r);
                    x86_print_relocation("GNU as:", &assembled_relocation);
                    if (found)
                    {
                        x86_print_relocation("from fixup ends:", reported);
                        printf("  instruction %lu: ", (unsigned long)reported->instruction);
                        x86_print_instruction(stdout, &instructions[reported->instruction], symbolic[reported->instruction]);
                        printf("\n");
                    }
                    else
                    {
                        printf("  from fixup ends:  none at that offset\n");
                    }
                }
                relocation_differences += 1;
            }
        }
        // The expected relocations GNU as doesn't have.
        for (uint64_t e = 0; expected != 0 && matched != 0 && e < expected_count; e += 1)
        {
            if (!matched[e])
            {
                if (relocation_differences < 40)
                {
                    printf("relocation from fixup ends without GNU as's:\n");
                    x86_print_relocation("from fixup ends:", &expected[e]);
                    printf("  instruction %lu: ", (unsigned long)expected[e].instruction);
                    x86_print_instruction(stdout, &instructions[expected[e].instruction], symbolic[expected[e].instruction]);
                    printf("\n");
                }
                relocation_differences += 1;
            }
        }

        // The forms that had a symbolic field of each kind, of those that can have one (their forms without a segment
        // override).
        static bool covered[2][X86_FORM_COUNT];
        static bool coverable[2][X86_FORM_COUNT];
        uint64_t covered_forms[2] = { 0 };
        uint64_t coverable_forms[2] = { 0 };
        for (uint64_t i = 0; i < count; i += 1)
        {
            const uint32_t row = (instructions[i].form & BUSTER_X86_FORM_NUMBER) % X86_FORM_COUNT;
            const uint32_t fields = x86_symbolic_fields(&instructions[i]);
            for (uint32_t field = X86_FIXUP_DISPLACEMENT; field <= X86_FIXUP_IMMEDIATE; field += 1)
            {
                coverable_forms[field] += (fields & (1u << field)) && !coverable[field][row];
                coverable[field][row] = coverable[field][row] || (fields & (1u << field));
                covered_forms[field] += (symbolic[i] & (1u << field)) && !covered[field][row];
                covered[field][row] = covered[field][row] || (symbolic[i] & (1u << field));
            }
        }

        printf("compared %lu instructions (%lld bytes, GNU as %lu): %lu differ\n", (unsigned long)count, (long long)length, (unsigned long)text_header->sh_size,
               (unsigned long)differences);
        printf("compared %lu relocations (GNU as %lu) of %lu marked instructions: %lu displacements of %lu of the %lu forms that had one, %lu immediates of "
               "%lu of %lu; PC32 %lu, PLT32 %lu, PC16 %lu, PC8 %lu, 32S %lu, 32 %lu, 16 %lu, 64 %lu; %lu differ\n",
               (unsigned long)expected_count, (unsigned long)relocation_count, (unsigned long)marked, (unsigned long)symbolic_fields[X86_FIXUP_DISPLACEMENT],
               (unsigned long)covered_forms[X86_FIXUP_DISPLACEMENT], (unsigned long)coverable_forms[X86_FIXUP_DISPLACEMENT],
               (unsigned long)symbolic_fields[X86_FIXUP_IMMEDIATE], (unsigned long)covered_forms[X86_FIXUP_IMMEDIATE],
               (unsigned long)coverable_forms[X86_FIXUP_IMMEDIATE], (unsigned long)types[R_X86_64_PC32], (unsigned long)types[R_X86_64_PLT32],
               (unsigned long)types[R_X86_64_PC16], (unsigned long)types[R_X86_64_PC8], (unsigned long)types[R_X86_64_32S], (unsigned long)types[R_X86_64_32],
               (unsigned long)types[R_X86_64_16], (unsigned long)types[R_X86_64_64], (unsigned long)relocation_differences);
        printf("x86_encode's fixup ends: %s\n", same_fixup_ends ? "the same" : "DIFFERENT");
        result = expected != 0 && matched != 0 && differences == 0 && text_header->sh_size == (uint64_t)length && relocation_differences == 0 &&
                 relocation_count == expected_count && same_fixup_ends;
        free(expected);
        free(matched);
    }
    free(file);
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

// The checks after the templates', for the entry point x86_test_mode says, from the seed's first state: for 32-bit
// addresses and for APX, all but the known fixups, which are x86_encode's (--compare-object checks the others'
// relocations), and for 16- and 32-bit modes, which have the scalar encoder alone, the known encodings and fixups, the
// digest and the invalid instructions.
static bool x86_check_mode(uint64_t seed, uint64_t count, X86Instruction* instructions, X86Buffers* buffers)
{
    uint64_t state = seed * BUSTER_X86_TEST_GOLDEN_RATIO + 1;
    const bool legacy = x86_mode_is_legacy(x86_test_mode);
    bool passed = x86_check_known();
    if (x86_test_mode == X86_ENCODER_MODE_64 || legacy)
    {
        passed = x86_check_known_fixups() && passed;
    }
    if (x86_test_mode == X86_ENCODER_MODE_APX)
    {
        passed = x86_check_known_invalid_apx() && passed;
    }
    passed = x86_check_digest(instructions, buffers->output) && passed;
    if (!legacy)
    {
        passed = x86_check_random(&state, count, instructions, buffers) && passed;
        passed = x86_check_page_edges(&state, instructions, buffers) && passed;
    }
    passed = x86_check_invalid(&state, instructions, buffers) && passed;
    return passed;
}

static int run_x86_encoder_tests(int argc, char** argv)
{
    uint64_t count = BUSTER_X86_TEST_DEFAULT_COUNT;
    uint64_t seed = 0;
    uint64_t print_count = 0;
    uint64_t compare_count = 0;
    const char* compare_path = 0;
    // --address32, --apx, --mode32 or --mode16: the one entry point to check.
    X86EncoderMode mode = X86_ENCODER_MODE_64;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        const bool address32 = strcmp(argv[i], "--address32") == 0;
        const bool apx = strcmp(argv[i], "--apx") == 0;
        const bool mode32 = strcmp(argv[i], "--mode32") == 0;
        const bool mode16 = strcmp(argv[i], "--mode16") == 0;
        if (address32 || apx || mode32 || mode16)
        {
            valid = valid && mode == X86_ENCODER_MODE_64;
            mode = address32 ? X86_ENCODER_MODE_ADDRESS32 : apx ? X86_ENCODER_MODE_APX : mode32 ? X86_ENCODER_MODE_32 : X86_ENCODER_MODE_16;
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
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
        else if (strcmp(argv[i], "--compare-object") == 0 && i + 2 < argc)
        {
            compare_path = argv[i + 1];
            valid = valid && x86_parse_u64(argv[i + 2], &compare_count);
            i += 2;
        }
        else
        {
            valid = false;
        }
    }

    const uint64_t largest = count > BUSTER_X86_TEST_DIGEST_INSTRUCTIONS ? count : BUSTER_X86_TEST_DIGEST_INSTRUCTIONS;
    const uint64_t capacity = largest > print_count ? (largest > compare_count ? largest : compare_count) : (print_count > compare_count ? print_count : compare_count);
    X86Instruction* instructions = malloc(capacity * sizeof(X86Instruction));
    uint8_t* symbolic = malloc(capacity);
    X86Buffers buffers = {
        .expected = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .output = malloc(BUSTER_X86_ENCODER_CAPACITY(capacity)),
        .expected_offsets = malloc(capacity * sizeof(uint32_t)),
        .offsets = malloc(capacity * sizeof(uint32_t)),
        .expected_fixup_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(capacity) * sizeof(uint32_t)),
        .fixup_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(capacity) * sizeof(uint32_t)),
    };
    const uint64_t effective_seed = seed == 0 ? BUSTER_X86_TEST_DIGEST_SEED : seed;
    uint64_t state = effective_seed * BUSTER_X86_TEST_GOLDEN_RATIO + 1;
    int result = 1;
    if (!valid || count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS || print_count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS ||
        compare_count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        fprintf(stderr, "usage: test-x86-encoder [--count N] [--seed N] [--print-assembly N | --compare-object object N] [--address32 | --apx | --mode32 | --mode16]\n");
    }
    else if (instructions == 0 || symbolic == 0 || buffers.expected == 0 || buffers.output == 0 || buffers.expected_offsets == 0 || buffers.offsets == 0 ||
             buffers.expected_fixup_ends == 0 || buffers.fixup_ends == 0)
    {
        fprintf(stderr, "out of memory\n");
    }
    else if (!x86_encoder_initialize() || !x86_encoder_initialize_address32() || !x86_encoder_initialize_apx() || !x86_encoder_initialize_legacy())
    {
        x86_check_templates();
    }
    else if (print_count != 0)
    {
        x86_test_mode = mode;
        result = x86_print_assembly(effective_seed, &state, print_count, instructions, symbolic, buffers.output) ? 0 : 1;
    }
    else if (compare_path != 0)
    {
        x86_test_mode = mode;
        result = x86_compare_object(effective_seed, &state, compare_path, compare_count, instructions, symbolic, &buffers) ? 0 : 1;
    }
    else
    {
        // Every check for x86_encode, then again for 32-bit addresses, for APX, and in 32-bit and 16-bit modes: all
        // five passes, or the one that --address32, --apx, --mode32 or --mode16 asks for.
        bool passed = x86_check_templates();
        for (uint32_t pass = X86_ENCODER_MODE_64; pass <= X86_ENCODER_MODE_16; pass += 1)
        {
            if (mode == X86_ENCODER_MODE_64 || mode == pass)
            {
                x86_test_mode = (X86EncoderMode)pass;
                passed = x86_check_mode(effective_seed, count, instructions, &buffers) && passed;
            }
        }
        result = passed ? 0 : 1;
    }
    free(instructions);
    free(symbolic);
    free(buffers.expected);
    free(buffers.output);
    free(buffers.expected_offsets);
    free(buffers.offsets);
    free(buffers.expected_fixup_ends);
    free(buffers.fixup_ends);
    return result;
}
