// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-tier1-masks (#179): hand-written IR (ir_parse.c's text, #175) of AVX-512's masks through Tier 1's selection and
// CFG consumer, whose allocation gives them the opmask class (k1 to k7, k0 its scratch), run natively and checked
// against the shared IR's interpreter (#176). Include after tier1_pipeline_test.c, whose child-process wait it shares.
//
// The IR's masks are m8 to m64 (#732), with no argument or result of a mask, so each fixture is a function of a pointer
// to a buffer, and of a count where it loops, that loads masks of every width from the buffer's first half, computes,
// and stores masks into its second half. Each is parsed by both of
// ir_parse.c's paths into the same function, planned and emitted by both of Tier 1's paths for x86-64-v4 (AVX512F, BW,
// DQ and VL), its allocation checked by tier1_cfg_verify, its records by the target's allow-list, and encoded by both
// encoders into the same bytes. Where the host has every feature of x86-64-v4, it runs in a child process on random
// buffers, and the buffer it leaves must be the interpreter's, byte for byte (the bytes no store writes among them).
//
// The fixtures: and, or, xor, not and copies at the four widths; constants (zero, every bit and others); a loop whose
// mask phis swap, a cycle through k0, beside a mask accumulated from the buffer; more masks live at once than k1 to k7
// hold, so that some are spilled and reloaded at their widths; and masks live across calls, outside a loop and in one,
// of a helper that clobbers every opmask register before it returns its argument plus one; masks of every width stored
// into a frame object and loaded back, each access's object folded into its kmov's memory operand; and masks live
// across divisions by a register and by a constant's reciprocal (#141's fixed registers, saved in the general call-save
// rows) and across a call. Each runs with rematerialization (#178, as the C driver's --tier1 compiles) and without: no
// mask may have a recipe or be marked to be recomputed. The helper is generated code too, beside the function: the
// interpreter calls a C function that returns the same. And the refusals: a vector (#141's), a mask's select, a target
// without AVX-512 (no opmask form), and one with AVX512F alone (kmovw and kandw, but no kandb or kmovq); the selector's
// vector scan by both paths, which must take masks and refuse a vector at any place, its columns ending at a PROT_NONE
// page; and each width's kmov forms, none for a width no mask has. Then masks from the typed stream: Tier 0's random mask
// programs (tier1_masks_test_typed) and every _k* builtin's row (tier1_masks_test_builtins) lowered to the IR, run by its
// interpreter through SSA and the optimizer, and compiled by Tier 1 natively. --print prints each fixture's records by
// the vector path, as GNU as reads them.

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "host_math.h"

#define BUSTER_TIER1_MASKS_TEST_BUFFER 256
// The native entry's xmm0 and xmm1 (#1280, tier1_masks_test_call): a double's and a float's bits, signaling NaNs past
// them.
#define BUSTER_TIER1_MASKS_TEST_DOUBLE UINT64_C(0x400921fb54442d18)
#define BUSTER_TIER1_MASKS_TEST_FLOAT UINT32_C(0x3fc00000)
#define BUSTER_TIER1_MASKS_TEST_DIRTY UINT64_C(0x7f8000017f800001)
#define BUSTER_TIER1_MASKS_TEST_INPUTS 16
#define BUSTER_TIER1_MASKS_TEST_HELPER 9u
#define BUSTER_TIER1_MASKS_TEST_ENTRY 1u
#define BUSTER_TIER1_MASKS_TEST_BINDING 100u

typedef struct Tier1MasksTest Tier1MasksTest;
struct Tier1MasksTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
    uint8_t* v4;
    // x86-64-v3's forms (#1281): AVX2's and F16C's, no AVX-512, which the fixtures marked `v3` are emitted for.
    uint8_t* v3;
    // x86-64-v2's and the x86-64 baseline's forms (#141's SSE targets).
    uint8_t* v2;
    uint8_t* baseline;
    // x86-64-v2's with AVX, Sandy Bridge's (#1281's review): AVX's without AVX2, which the fixtures marked `avx` are emitted for.
    uint8_t* avx;
    // x86-64-v3's with AVX512F but not VL (#1281's review), which the fixtures marked `foundation` are emitted for.
    const uint8_t* foundation;
    // x86-64-v4's forms with AVX512-FP16's complex multiplication, which no target of Tier 1's allows yet, for the
    // fixtures of rows of it (Tier1MasksFixture.fp16); and the forms of the fixture that runs.
    uint8_t* fp16;
    const uint8_t* forms;
    // x86-64-v4's features, which the typed programs' lowering takes as its output target (ir_lower_typed_target).
    uint64_t v4_features;
    bool native;
    // Whether the host has AVX512-FP16 too, which a fixture of its rows needs to run natively.
    bool native_fp16;
    // The fixtures that ran natively, by both paths, with and without rematerialization.
    uint32_t native_fixtures;
    // --print: each fixture's records, by the vector path, as GNU as reads them.
    bool print;
    Arena output;
    Arena scratch;
    Arena execution;
    // What the fixtures did, over both paths: native runs, mask spills and reloads, masks saved around calls, edge and
    // split moves of masks, moves through k0, records.
    uint64_t native_runs;
    uint64_t mask_stores;
    uint64_t mask_loads;
    uint64_t call_saves;
    uint64_t mask_moves;
    uint64_t scratch_moves;
    uint64_t records;
    uint64_t frame_accesses;
    // General values recomputed (#178) in the runs with rematerialization, beside the masks, which never are.
    uint64_t recomputed;
    uint32_t refused;
    // Tier 0's mask programs through the IR (tier1_masks_test_typed): those lowered and run through every stage, those of
    // them with a machine row (#141), which the interpreter refuses, and their native runs by Tier 1.
    uint32_t typed_programs;
    uint32_t typed_machine;
    uint64_t typed_runs;
    // The front end's opmask rows (tier1_masks_test_builtins): those through every stage, and Tier 0's native runs of them.
    uint32_t builtin_attempts;
    uint32_t builtin_rows;
    uint32_t builtin_native;
    // The front end's comparisons into masks (#1623, tier1_masks_test_compares): those tried, those through every stage,
    // those of them the IR's comparisons (the others machine rows), and Tier 0's native runs of them.
    uint32_t compare_attempts;
    uint32_t compare_rows;
    uint32_t compare_decoded;
    uint32_t compare_native;
    // The front end's masked moves' selects and undefined vectors (#1840, tier1_masks_test_selects): those tried, and
    // those through every stage as the IR's vselect or zero vector.
    uint32_t select_attempts;
    uint32_t select_rows;
    // The front end's width changes (#1840, tier1_masks_test_widths): those tried, and those through every stage as the
    // IR's extract, widen or insert.
    uint32_t width_attempts;
    uint32_t width_rows;
    // The front end's permutes (#1840, tier1_masks_test_permutes): those tried, and those through every stage as the IR's.
    uint32_t permute_attempts;
    uint32_t permute_rows;
    // The conversions (#1840, tier1_masks_test_conversions): those tried, and those through every stage as the IR's.
    uint32_t convert_attempts;
    uint32_t convert_rows;
    // The shifts by an int (#1840, tier1_masks_test_shifts): those tried, and those through every stage as the IR's.
    uint32_t shift_attempts;
    uint32_t shift_rows;
    // The masks to vectors and back (#1840, tier1_masks_test_mask_vectors): those tried, and those through every stage as
    // the IR's.
    uint32_t mask_vector_attempts;
    uint32_t mask_vector_rows;
    // The lane operations (#1840, tier1_masks_test_lane_ops): those tried, and those through every stage as the IR's.
    uint32_t lane_op_attempts;
    uint32_t lane_op_rows;
    // The permutes of two tables (#1840, tier1_masks_test_permute2s): those tried, and those through every stage as the
    // IR's.
    uint32_t permute2_attempts;
    uint32_t permute2_rows;
    // The bitwise functions of three vectors (#1840, tier1_masks_test_ternlogs): those tried, and those through every
    // stage as the IR's.
    uint32_t ternlog_attempts;
    uint32_t ternlog_rows;
    // The compresses and expands (#1840, tier1_masks_test_compresses): those tried, and those through every stage as the
    // IR's.
    uint32_t compress_attempts;
    uint32_t compress_rows;
    // The masked loads and stores (#1840, tier1_masks_test_masked_memory): those tried, and those through every stage as
    // the IR's.
    uint32_t masked_attempts;
    uint32_t masked_rows;
    // One lane's moves and the lanes' sign bits (#1840, tier1_masks_test_lane_moves): those tried, and those through every
    // stage as the IR's.
    uint32_t lane_attempts;
    uint32_t lane_rows;
    // The floating operations at their own rounding (#1840, tier1_masks_test_frounds): those tried, and those through
    // every stage as the IR's.
    uint32_t fround_attempts;
    uint32_t fround_rows;
    // The truncating conversions of lane 0 (#1840, tier1_masks_test_cvtts): those tried, and those through every stage.
    uint32_t cvtt_attempts;
    uint32_t cvtt_rows;
    // The machine rows' mutants (#1284, tier1_masks_test_row_mutants): borrowed opmask registers' reloads left out, staging
    // copies renamed into a live value's register; and of each, those the code's certificate refused.
    uint32_t row_reloads;
    uint32_t row_reloads_refused;
    // The asm bodies' code certified (#1839, tier1_masks_test_asm_mutants), and their mutants: a mask's reload after the
    // body left out, a body's record changed; and those refused.
    uint32_t asm_certified;
    uint32_t asm_reloads;
    uint32_t asm_reloads_refused;
    uint32_t asm_records;
    uint32_t asm_records_refused;
    uint32_t row_stagings;
    uint32_t row_stagings_refused;
    // Claimed placements whose roles share a register Tier 0's placement keeps apart, and whose opmask register named by
    // number is k0 (tier1_masks_test_claim_mutants), each with the row's records placed by it; and those refused.
    uint32_t row_roles;
    uint32_t row_roles_refused;
    uint32_t row_k0;
    uint32_t row_k0_refused;
    // The rows whose first operand is read in its own register up to the result's first write (#1283's split), and their
    // claims of it corrupted (the split cleared, its early register made the result's), and those refused.
    uint32_t row_splits;
    uint32_t row_split_mutants;
    uint32_t row_split_refused;
    // Among the roles sharing a register, the temporaries claimed in a live operand's register.
    uint32_t row_overlaps;
    uint32_t row_overlaps_refused;
    // The vectors' moves of ymm and zmm registers narrowed to xmm ones (#1284), and those refused.
    uint32_t vector_moves;
    uint32_t vector_moves_refused;
};

typedef struct Tier1MasksFixture Tier1MasksFixture;
struct Tier1MasksFixture
{
    const char* name;
    const char* text;
    // The count it loops to, at most this (0 for a function of the pointer alone), and what it must reach.
    uint32_t count;
    bool spills;
    bool calls;
    bool cycle;
    // The masks' loads and stores whose address is a frame object's, folded into the kmov's memory operand.
    uint32_t frame_accesses;
    // A division by a register and one by a constant's reciprocal (#141's fixed registers, rax and rdx) beside the masks.
    bool divisions;
    // A cycle of vectors' phis (#141), which goes through xmm15, the vector class's scratch register.
    bool vector_cycle;
    // A machine row short of free opmask registers (#141), which borrows live masks' and reloads them after it.
    bool borrows;
    // Emitted for x86-64-v3 (#1281), a target without AVX-512: its vectors by VEX forms in xmm0 to xmm15 alone, its rows'
    // roles below 16, and every record one that target has.
    bool v3;
    // Emitted for Sandy Bridge, a target with AVX but not AVX2 (#1281's review): its 32-byte vectors by AVX's forms alone.
    bool avx;
    // Emitted for x86-64-v2 and for the x86-64 baseline, targets without AVX (#141's SSE targets): its 16-byte vectors by
    // legacy SSE's forms in xmm0 to xmm15, and every record one that target has.
    bool v2;
    bool baseline;
    // Emitted for x86-64-v3 with AVX512F but not AVX512VL, as Knights Landing has AVX-512 (#1281's review): zmm16 and on
    // and EVEX's 512-bit forms, but no EVEX form of an xmm or a ymm register.
    bool foundation;
    // Its machine rows and the moves around them into their roles' registers and out of them (#1283,
    // Tier1EmitResult.row_moves), by either production path with and without rematerialization: both use the same
    // register-assignment policy (#1395). Checked where `rows` isn't 0.
    uint32_t rows;
    uint32_t row_moves;
    // Comparisons into a mask under another mask as their writemask (#1847, tier1_select_masked_compare): how many of
    // its records are a vpcmp or a vcmpp with an opmask decoration, by either path. Checked where it isn't 0.
    uint32_t masked_compares;
    // A row of AVX512-FP16's (#1283's review): its forms are test->fp16's, and it runs natively only where the host has
    // AVX512-FP16 (not the 9700X's Zen 5).
    bool fp16;
    // Rows whose destination can't be a source's register (#1283's review): a gather's or a complex multiplication's,
    // whose records the test checks for distinct registers.
    bool exclusive;
    // Makes the random input one the fixture can run on (a gather's indices in its buffer), before its model reads it.
    void (*prepare)(uint8_t* input);
    // The buffer after a run, from the input and the count, where the interpreter can't run the fixture: a machine row's
    // (#141), which it has no model of. Null for the interpreter's.
    void (*model)(const uint8_t* input, uint64_t count, uint8_t* output);
    // Its one call is an asm body's (#1839, tier1_masks_test_asm_body), which the text has no form for: after parsing,
    // the call becomes a body call of records that write k1 to k7, around which the live masks must be saved.
    bool asm_body;
};

// The asm body of the fixture marked `asm_body` (#1839): a template's records that zero eax and write k1 to k7 from it,
// as `xorl %eax, %eax; kmovw %eax, %k1; ...; kmovw %eax, %k7` would assemble, touching rax and writing every opmask
// register Tier 1 allocates (IrAsmBody.masks).
static const X86Instruction tier1_masks_test_asm_records[] = {
    { .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RAX, .form = X86_FORM_XOR_R32_RM32 },
    { .reg = 1, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 2, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 3, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 4, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 5, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 6, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
    { .reg = 7, .rm = X86_REGISTER_RAX, .form = X86_FORM_KMOVW_K_R32 },
};

static IrAsmBody tier1_masks_test_asm_body = {
    .records = tier1_masks_test_asm_records,
    .count = BUSTER_ARRAY_LENGTH(tier1_masks_test_asm_records),
    .touched = 1u << X86_REGISTER_RAX,
    .masks = 0xfe,
};

// The asm body fixture's buffer (#1839): its m16 at 0 complemented at 128, its m64 at 8 at 136 and its m8 at 16 at 144,
// each live in an opmask register across the body that writes them all.
static void tier1_masks_test_asm_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint16_t word = 0;
    memcpy(&word, input, 2);
    word = (uint16_t)~word;
    memcpy(output + 128, &word, 2);
    memcpy(output + 136, input + 8, 8);
    output[144] = input[16];
}

static bool tier1_masks_test_expect(Tier1MasksTest* test, bool condition, const char* message)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        if (test->failures <= 20)
        {
            printf("test-tier1-masks: %s\n", message);
        }
    }
    return condition;
}

static uint64_t tier1_masks_test_next(Tier1MasksTest* test)
{
    test->random = test->random * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return test->random >> 17;
}

// The interpreter's helper: what the generated one returns, without its clobbers.
static uint64_t tier1_masks_test_successor(uint64_t value)
{
    return value + 1;
}

// The machine rows' fixture's buffer (#141): its seven word masks back at 128 on, (~x) & 0xffff at 160 (through k1 and k2,
// named by number), the masks' (a & b) ^ a at 164 (through a mask scratch register and the result read as its first
// operand), x + y at 168 (the result read as its first operand) and y at 172 (a row of no records, its result its
// operand), of x and y at 16 and 20.
static void tier1_masks_test_rows_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint16_t masks[7];
    uint32_t x = 0;
    uint32_t y = 0;
    memcpy(masks, input, sizeof(masks));
    memcpy(&x, input + 16, 4);
    memcpy(&y, input + 20, 4);
    memcpy(output + 128, masks, sizeof(masks));
    const uint32_t complement = ~x & 0xffffu;
    const uint16_t mixed = (uint16_t)((masks[0] & masks[1]) ^ masks[0]);
    const uint32_t sum = x + y;
    memcpy(output + 160, &complement, 4);
    memcpy(output + 164, &mixed, 2);
    memcpy(output + 168, &sum, 4);
    memcpy(output + 172, &y, 4);
}

// The machine rows' buffer at x86-64-v3 (#1281): of the 32-byte vectors x and y at 0 and 32, the bytes of x + y (a VEX
// row) added to x as doublewords at 128 and x again at 160; of the floats a and b at 96 and 100, their bits added as
// integers at 192 (the result read as its first operand), (a + b) ^ a at 196 (a row that writes its second floating
// operand first), and a and b again at 200 and 204; and the doublewords of the 16-byte vector at 64 reversed (vpshufd's)
// at 224.
static void tier1_masks_test_vex_rows_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint8_t sum[32];
    for (uint32_t i = 0; i < 32; i += 1)
    {
        sum[i] = (uint8_t)(input[i] + input[32 + i]);
    }
    for (uint32_t lane = 0; lane < 8; lane += 1)
    {
        uint32_t x = 0;
        uint32_t z = 0;
        memcpy(&x, input + 4 * lane, 4);
        memcpy(&z, sum + 4 * lane, 4);
        const uint32_t w = x + z;
        memcpy(output + 128 + 4 * lane, &w, 4);
    }
    memcpy(output + 160, input, 32);
    uint32_t a = 0;
    uint32_t b = 0;
    memcpy(&a, input + 96, 4);
    memcpy(&b, input + 100, 4);
    const uint32_t first = a + b;
    const uint32_t second = (a + b) ^ a;
    memcpy(output + 192, &first, 4);
    memcpy(output + 196, &second, 4);
    memcpy(output + 200, &a, 4);
    memcpy(output + 204, &b, 4);
    for (uint32_t lane = 0; lane < 4; lane += 1)
    {
        memcpy(output + 224 + 4 * lane, input + 64 + 4 * (3 - lane), 4);
    }
}

// The pressure fixtures' buffer (#1281's review): of the 15 vectors of `bytes` bytes at 0, 4, ..., 56, the exclusive or
// of the last 13 at 128, and the row's a ^ (a + b) of the first two at 128 + `bytes`, as doublewords.
static void tier1_masks_test_pressure(const uint8_t* input, uint8_t* output, uint32_t bytes)
{
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    for (uint32_t lane = 0; lane < bytes / 4; lane += 1)
    {
        uint32_t folded = 0;
        for (uint32_t v = 2; v < 15; v += 1)
        {
            uint32_t x = 0;
            memcpy(&x, input + 4 * v + 4 * lane, 4);
            folded ^= x;
        }
        uint32_t a = 0;
        uint32_t b = 0;
        memcpy(&a, input + 4 * lane, 4);
        memcpy(&b, input + 4 + 4 * lane, 4);
        const uint32_t row = a ^ (a + b);
        memcpy(output + 128 + 4 * lane, &folded, 4);
        memcpy(output + 128 + bytes + 4 * lane, &row, 4);
    }
}

static void tier1_masks_test_pressure_model64(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    tier1_masks_test_pressure(input, output, 64);
}

static void tier1_masks_test_pressure_model32(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    tier1_masks_test_pressure(input, output, 32);
}

// The read-only row's buffer (#1293's review): of the 15 32-byte vectors at 0, 4, ..., 56, the exclusive or of the last 14
// at 128, and the first, the row's result, at 160.
static void tier1_masks_test_read_only_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    for (uint32_t lane = 0; lane < 8; lane += 1)
    {
        uint32_t folded = 0;
        for (uint32_t v = 1; v < 15; v += 1)
        {
            uint32_t x = 0;
            memcpy(&x, input + 4 * v + 4 * lane, 4);
            folded ^= x;
        }
        memcpy(output + 128 + 4 * lane, &folded, 4);
    }
    memcpy(output + 160, input, 32);
}

// The floating row's buffer (#141): the low lanes of a float's and another's bits added as integers at 128, the result
// read as its first operand, of the floats at 0 and 4.
static void tier1_masks_test_floating_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint32_t a = 0;
    uint32_t b = 0;
    memcpy(&a, input, 4);
    memcpy(&b, input + 4, 4);
    const uint32_t sum = a + b;
    memcpy(output + 128, &sum, 4);
}

// The SSE carriers' buffer (#1280): rows of no records, each its 4- or 8-byte vector operand's register whole as a
// 16-byte result, at 128 the double parameter's (its bits, then zeros), at 144 the float parameter's (its bits, then
// zeros) and at 160 the helper's double result's (count + 1, then zeros): each operand zero-extended into its role,
// though its register's bytes past it aren't zero.
static void tier1_masks_test_carriers_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    const uint64_t parameter = BUSTER_TIER1_MASKS_TEST_DOUBLE;
    const uint32_t single = BUSTER_TIER1_MASKS_TEST_FLOAT;
    const uint64_t result = count + 1;
    memset(output + 128, 0, 48);
    memcpy(output + 128, &parameter, 8);
    memcpy(output + 144, &single, 4);
    memcpy(output + 160, &result, 8);
}

// The vector rows' buffer (#1283), of a and b, 16 doublewords at 0 and 64, and k, the word at 0: at 128, w ^ z ^ m, where
// s = a + b, t = s ^ a ^ b, m = a + t where k has a bit and a elsewhere, n = (b + b)'s low 4 doublewords, w = n + n in its
// low 4 and 0 past them, z = n's 16 bytes zero-extended; at 192, the bytes of lo (at 0) where c's (at 64) top bit is
// clear and of hi (at 32) where it is set; n at 224 and m's first doubleword doubled at 240.
static void tier1_masks_test_vector_rows_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint32_t a[16];
    uint32_t b[16];
    uint16_t k = 0;
    memcpy(a, input, sizeof(a));
    memcpy(b, input + 64, sizeof(b));
    memcpy(&k, input, sizeof(k));
    uint32_t m[16];
    uint32_t n[4];
    uint32_t f[16];
    for (uint32_t i = 0; i < 16; i += 1)
    {
        const uint32_t sum = a[i] + b[i];
        const uint32_t t = sum ^ a[i] ^ b[i];
        m[i] = ((k >> i) & 1) != 0 ? a[i] + t : a[i];
    }
    for (uint32_t i = 0; i < 4; i += 1)
    {
        n[i] = b[i] + b[i];
    }
    uint8_t bytes[16];
    memcpy(bytes, n, sizeof(bytes));
    for (uint32_t i = 0; i < 16; i += 1)
    {
        const uint32_t w = i < 4 ? n[i] + n[i] : 0;
        f[i] = w ^ (uint32_t)bytes[i] ^ m[i];
    }
    memcpy(output + 128, f, sizeof(f));
    for (uint32_t i = 0; i < 32; i += 1)
    {
        output[192 + i] = (input[64 + i] & 0x80) != 0 ? input[32 + i] : input[i];
    }
    memcpy(output + 224, n, sizeof(n));
    const uint32_t doubled = m[0] + m[0];
    memcpy(output + 240, &doubled, sizeof(doubled));
}

// The gathers' fixture's input (#1283's review): its 16 doubleword indices at 64 each below 16, so that they gather from the
// buffer's first 64 bytes.
static void tier1_masks_test_gather_prepare(uint8_t* input)
{
    for (uint32_t i = 0; i < 16; i += 1)
    {
        const uint32_t index = input[64 + 4 * i] & 15u;
        memcpy(input + 64 + 4 * i, &index, 4);
    }
}

// The gathers' buffer: of the doublewords d at 0, the indices x at 64 and the word mask k at 0, each lane i where k has a
// bit is d[x[i]], at 128 elsewhere x[i] (the source is the index) and at 192 d[i] (the source is d).
static void tier1_masks_test_gather_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    uint32_t d[16];
    uint32_t x[16];
    uint16_t k = 0;
    memcpy(d, input, sizeof(d));
    memcpy(x, input + 64, sizeof(x));
    memcpy(&k, input, sizeof(k));
    for (uint32_t i = 0; i < 16; i += 1)
    {
        const uint32_t first = ((k >> i) & 1) != 0 ? d[x[i]] : x[i];
        const uint32_t second = ((k >> i) & 1) != 0 ? d[x[i]] : d[i];
        memcpy(output + 128 + 4 * i, &first, 4);
        memcpy(output + 192 + 4 * i, &second, 4);
    }
}

// The complex multiplications' buffer: of the half-precision complex numbers a at 0 and b at 64, a times b at 128 and at
// 192, as the host's vfmulcph computes them (its own rounding, NaNs and denormals), where the host has AVX512-FP16: the
// encoder's records of a load of each, the product and its store, run from a page of their own, so that the test's own
// code needs no AVX512-FP16 (this compiler's build of it, the self-host's, takes no intrinsics of it at x86-64).
// JIT instructions have no Clang function-type prefix, so UBSan's indirect-call check must not read before the page.
static __attribute__((no_sanitize("function"))) void tier1_masks_test_complex_model(const uint8_t* input, uint64_t count, uint8_t* output)
{
    (void)count;
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    const X86Instruction records[] = {
        { .form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = 0, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_VMOVDQU64_Z_ZM512, .reg = 1, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 64 },
        { .form = X86_FORM_VFMULCPH_Z_Z_ZM512, .reg = 2, .vvvv = 0, .rm = 1, .mode = BUSTER_X86_REGISTER_DIRECT },
        { .form = X86_FORM_VMOVDQU64_ZM512_Z, .reg = 2, .rm = X86_REGISTER_RSI, .mode = BUSTER_X86_MEMORY_BASE },
        { .form = X86_FORM_RET },
    };
    const uint32_t count_records = sizeof(records) / sizeof(records[0]);
    uint8_t* code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const int64_t size = code != MAP_FAILED ? x86_encode(records, count_records, code, 4096, 0, 0) : -1;
    if (size > 0 && mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0)
    {
        void (*product)(const uint8_t*, uint8_t*) = (void (*)(const uint8_t*, uint8_t*))(void*)code;
        product(input, output + 128);
        memcpy(output + 192, output + 128, 64);
    }
    if (code != MAP_FAILED)
    {
        munmap(code, 4096);
    }
}

// The fixtures of AVX512-FP16's rows, the array's last, which run natively only where the host has it.
#define BUSTER_TIER1_MASKS_TEST_FP16_FIXTURES 1

static const Tier1MasksFixture tier1_masks_test_fixtures[] = {
    {
        .name = "logic at every width",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=1\n"
                "  %3 = load.m8 %2 align=1\n"
                "  %4 = and.m8 %1, %3\n"
                "  %5 = or.m8 %1, %3\n"
                "  %6 = xor.m8 %4, %5\n"
                "  %7 = not.m8 %6\n"
                "  %8 = copy.m8 %7\n"
                "  %9 = address.ptr %0 offset=128\n"
                "  %10 = store.void %9, %8 align=1\n"
                "  %11 = address.ptr %0 offset=129\n"
                "  %12 = store.void %11, %4 align=1\n"
                "  %13 = address.ptr %0 offset=2\n"
                "  %14 = load.m16 %13 align=2\n"
                "  %15 = address.ptr %0 offset=4\n"
                "  %16 = load.m16 %15 align=2\n"
                "  %17 = and.m16 %14, %16\n"
                "  %18 = or.m16 %14, %16\n"
                "  %19 = xor.m16 %17, %18\n"
                "  %20 = not.m16 %19\n"
                "  %21 = address.ptr %0 offset=130\n"
                "  %22 = store.void %21, %20 align=2\n"
                "  %23 = address.ptr %0 offset=132\n"
                "  %24 = store.void %23, %18 align=2\n"
                "  %25 = address.ptr %0 offset=8\n"
                "  %26 = load.m32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=12\n"
                "  %28 = load.m32 %27 align=4\n"
                "  %29 = and.m32 %26, %28\n"
                "  %30 = or.m32 %26, %28\n"
                "  %31 = xor.m32 %29, %30\n"
                "  %32 = not.m32 %31\n"
                "  %33 = address.ptr %0 offset=136\n"
                "  %34 = store.void %33, %32 align=4\n"
                "  %35 = address.ptr %0 offset=140\n"
                "  %36 = store.void %35, %30 align=4\n"
                "  %37 = address.ptr %0 offset=16\n"
                "  %38 = load.m64 %37 align=8\n"
                "  %39 = address.ptr %0 offset=24\n"
                "  %40 = load.m64 %39 align=8\n"
                "  %41 = and.m64 %38, %40\n"
                "  %42 = or.m64 %38, %40\n"
                "  %43 = xor.m64 %41, %42\n"
                "  %44 = not.m64 %43\n"
                "  %45 = copy.m64 %44\n"
                "  %46 = address.ptr %0 offset=144\n"
                "  %47 = store.void %46, %45 align=8\n"
                "  %48 = address.ptr %0 offset=152\n"
                "  %49 = store.void %48, %42 align=8\n"
                "  %50 = ret.void\n"
                "}\n",
    },
    {
        .name = "constants",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = const.m8 0x00000000000000000000000000000000\n"
                "  %2 = address.ptr %0 offset=128\n"
                "  %3 = store.void %2, %1 align=1\n"
                "  %4 = const.m8 0x000000000000000000000000000000ff\n"
                "  %5 = address.ptr %0 offset=129\n"
                "  %6 = store.void %5, %4 align=1\n"
                "  %7 = const.m8 0x0000000000000000000000000000005a\n"
                "  %8 = address.ptr %0 offset=130\n"
                "  %9 = store.void %8, %7 align=1\n"
                "  %10 = const.m16 0x0000000000000000000000000000ffff\n"
                "  %11 = address.ptr %0 offset=132\n"
                "  %12 = store.void %11, %10 align=2\n"
                "  %13 = const.m16 0x0000000000000000000000000000a5c3\n"
                "  %14 = address.ptr %0 offset=134\n"
                "  %15 = store.void %14, %13 align=2\n"
                "  %16 = const.m32 0x000000000000000000000000ffffffff\n"
                "  %17 = address.ptr %0 offset=136\n"
                "  %18 = store.void %17, %16 align=4\n"
                "  %19 = const.m32 0x00000000000000000000000080000001\n"
                "  %20 = address.ptr %0 offset=140\n"
                "  %21 = store.void %20, %19 align=4\n"
                "  %22 = const.m64 0x0000000000000000ffffffffffffffff\n"
                "  %23 = address.ptr %0 offset=144\n"
                "  %24 = store.void %23, %22 align=8\n"
                "  %25 = const.m64 0x00000000000000008000000000000001\n"
                "  %26 = address.ptr %0 offset=152\n"
                "  %27 = store.void %26, %25 align=8\n"
                "  %28 = const.m64 0x00000000000000000000000012345678\n"
                "  %29 = address.ptr %0 offset=160\n"
                "  %30 = store.void %29, %28 align=8\n"
                "  %31 = const.m64 0x00000000000000000000000000000000\n"
                "  %32 = address.ptr %0 offset=168\n"
                "  %33 = store.void %32, %31 align=8\n"
                "  %34 = const.m16 0x00000000000000000000000000000000\n"
                "  %35 = address.ptr %0 offset=176\n"
                "  %36 = store.void %35, %34 align=2\n"
                "  %37 = ret.void\n"
                "}\n",
    },
    {
        .name = "a loop whose mask phis swap",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m16 %0 align=2\n"
                "  %3 = address.ptr %0 offset=2\n"
                "  %4 = load.m16 %3 align=2\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.m32 %5 align=4\n"
                "  %7 = const.i64 0x00000000000000000000000000000000\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.m16 [%2, b0], [%10, b1]\n"
                "  %10 = phi.m16 [%4, b0], [%9, b1]\n"
                "  %11 = phi.m32 [%6, b0], [%16, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%18, b1]\n"
                "  %13 = address.ptr %0 + %12 * 4 offset=16\n"
                "  %14 = load.m32 %13 align=4\n"
                "  %15 = not.m32 %14\n"
                "  %16 = xor.m32 %11, %15\n"
                "  %17 = const.i64 0x00000000000000000000000000000001\n"
                "  %18 = add.i64 %12, %17\n"
                "  %19 = icmp.i1 %18, %1 ult\n"
                "  %20 = cbr.void %19, b1, b2\n"
                "b2:\n"
                "  %21 = address.ptr %0 offset=128\n"
                "  %22 = store.void %21, %9 align=2\n"
                "  %23 = address.ptr %0 offset=130\n"
                "  %24 = store.void %23, %10 align=2\n"
                "  %25 = address.ptr %0 offset=132\n"
                "  %26 = store.void %25, %11 align=4\n"
                "  %27 = address.ptr %0 offset=136\n"
                "  %28 = store.void %27, %16 align=4\n"
                "  %29 = ret.void\n"
                "}\n",
        .count = 20,
        .cycle = true,
    },
    {
        .name = "more masks than k1 to k7",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=1\n"
                "  %3 = load.m8 %2 align=1\n"
                "  %4 = address.ptr %0 offset=2\n"
                "  %5 = load.m16 %4 align=2\n"
                "  %6 = address.ptr %0 offset=4\n"
                "  %7 = load.m16 %6 align=2\n"
                "  %8 = address.ptr %0 offset=8\n"
                "  %9 = load.m32 %8 align=4\n"
                "  %10 = address.ptr %0 offset=12\n"
                "  %11 = load.m32 %10 align=4\n"
                "  %12 = address.ptr %0 offset=16\n"
                "  %13 = load.m64 %12 align=8\n"
                "  %14 = address.ptr %0 offset=24\n"
                "  %15 = load.m64 %14 align=8\n"
                "  %16 = address.ptr %0 offset=32\n"
                "  %17 = load.m64 %16 align=8\n"
                "  %18 = address.ptr %0 offset=40\n"
                "  %19 = load.m16 %18 align=2\n"
                "  %20 = xor.m8 %1, %3\n"
                "  %21 = and.m16 %5, %7\n"
                "  %22 = or.m32 %9, %11\n"
                "  %23 = xor.m64 %13, %15\n"
                "  %24 = and.m64 %23, %17\n"
                "  %25 = not.m16 %19\n"
                "  %26 = address.ptr %0 offset=128\n"
                "  %27 = store.void %26, %1 align=1\n"
                "  %28 = address.ptr %0 offset=129\n"
                "  %29 = store.void %28, %3 align=1\n"
                "  %30 = address.ptr %0 offset=130\n"
                "  %31 = store.void %30, %5 align=2\n"
                "  %32 = address.ptr %0 offset=132\n"
                "  %33 = store.void %32, %7 align=2\n"
                "  %34 = address.ptr %0 offset=136\n"
                "  %35 = store.void %34, %9 align=4\n"
                "  %36 = address.ptr %0 offset=140\n"
                "  %37 = store.void %36, %11 align=4\n"
                "  %38 = address.ptr %0 offset=144\n"
                "  %39 = store.void %38, %13 align=8\n"
                "  %40 = address.ptr %0 offset=152\n"
                "  %41 = store.void %40, %15 align=8\n"
                "  %42 = address.ptr %0 offset=160\n"
                "  %43 = store.void %42, %17 align=8\n"
                "  %44 = address.ptr %0 offset=168\n"
                "  %45 = store.void %44, %19 align=2\n"
                "  %46 = address.ptr %0 offset=170\n"
                "  %47 = store.void %46, %20 align=1\n"
                "  %48 = address.ptr %0 offset=172\n"
                "  %49 = store.void %48, %21 align=2\n"
                "  %50 = address.ptr %0 offset=176\n"
                "  %51 = store.void %50, %22 align=4\n"
                "  %52 = address.ptr %0 offset=184\n"
                "  %53 = store.void %52, %24 align=8\n"
                "  %54 = address.ptr %0 offset=192\n"
                "  %55 = store.void %54, %25 align=2\n"
                "  %56 = ret.void\n"
                "}\n",
        .spills = true,
    },
    {
        // #1839: masks live in opmask registers across an asm body whose records write k1 to k7, which Tier 1 saves
        // and reloads around it as around a call (tier1_emit_cfg_asm).
        .name = "masks across an asm body",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m16 %0 align=2\n"
                "  %2 = address.ptr %0 offset=8\n"
                "  %3 = load.m64 %2 align=8\n"
                "  %4 = address.ptr %0 offset=16\n"
                "  %5 = load.m8 %4 align=1\n"
                "  %6 = call.void @9() fixed=() variadic=0\n"
                "  %7 = not.m16 %1\n"
                "  %8 = address.ptr %0 offset=128\n"
                "  %9 = store.void %8, %7 align=2\n"
                "  %10 = address.ptr %0 offset=136\n"
                "  %11 = store.void %10, %3 align=8\n"
                "  %12 = address.ptr %0 offset=144\n"
                "  %13 = store.void %12, %5 align=1\n"
                "  %14 = ret.void\n"
                "}\n",
        .calls = true,
        .asm_body = true,
        .model = tier1_masks_test_asm_model,
    },
    {
        .name = "masks across calls",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m16 %0 align=2\n"
                "  %3 = address.ptr %0 offset=8\n"
                "  %4 = load.m64 %3 align=8\n"
                "  %5 = address.ptr %0 offset=16\n"
                "  %6 = load.m8 %5 align=1\n"
                "  %7 = call.i64 @9(%1) fixed=(i64) variadic=0\n"
                "  %8 = not.m16 %2\n"
                "  %9 = call.i64 @9(%7) fixed=(i64) variadic=0\n"
                "  %10 = not.m64 %4\n"
                "  %11 = address.ptr %0 offset=128\n"
                "  %12 = store.void %11, %2 align=2\n"
                "  %13 = address.ptr %0 offset=130\n"
                "  %14 = store.void %13, %8 align=2\n"
                "  %15 = address.ptr %0 offset=136\n"
                "  %16 = store.void %15, %4 align=8\n"
                "  %17 = address.ptr %0 offset=144\n"
                "  %18 = store.void %17, %10 align=8\n"
                "  %19 = address.ptr %0 offset=152\n"
                "  %20 = store.void %19, %6 align=1\n"
                "  %21 = address.ptr %0 offset=160\n"
                "  %22 = store.void %21, %9 align=8\n"
                "  %23 = ret.void\n"
                "}\n",
        .count = 1000,
        .calls = true,
    },
    {
        .name = "a mask phi across a call in a loop",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m32 %0 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.m8 %3 align=1\n"
                "  %5 = const.i64 0x00000000000000000000000000000000\n"
                "  %6 = br.void b1\n"
                "b1:\n"
                "  %7 = phi.m32 [%2, b0], [%10, b1]\n"
                "  %8 = phi.i64 [%5, b0], [%9, b1]\n"
                "  %9 = call.i64 @9(%8) fixed=(i64) variadic=0\n"
                "  %10 = not.m32 %7\n"
                "  %11 = icmp.i1 %9, %1 ult\n"
                "  %12 = cbr.void %11, b1, b2\n"
                "b2:\n"
                "  %13 = address.ptr %0 offset=128\n"
                "  %14 = store.void %13, %10 align=4\n"
                "  %15 = address.ptr %0 offset=132\n"
                "  %16 = store.void %15, %4 align=1\n"
                "  %17 = address.ptr %0 offset=136\n"
                "  %18 = store.void %17, %9 align=8\n"
                "  %19 = ret.void\n"
                "}\n",
        .count = 9,
        .calls = true,
    },
    {
        // Each access's frame object folded into its kmov's memory operand: the frame's base at the object's offset.
        .name = "masks through a frame object",
        .text = "function (ptr) -> void {\n"
                "  slot $0 size=16 align=8 type=void flags=0x01\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m8 %0 align=1\n"
                "  %2 = address.ptr %0 offset=2\n"
                "  %3 = load.m16 %2 align=2\n"
                "  %4 = address.ptr %0 offset=4\n"
                "  %5 = load.m32 %4 align=4\n"
                "  %6 = address.ptr %0 offset=8\n"
                "  %7 = load.m64 %6 align=8\n"
                "  %8 = stack_addr.ptr $0\n"
                "  %9 = store.void %8, %7 align=8\n"
                "  %10 = stack_addr.ptr $0\n"
                "  %11 = address.ptr %10 offset=8\n"
                "  %12 = store.void %11, %1 align=1\n"
                "  %13 = stack_addr.ptr $0\n"
                "  %14 = address.ptr %13 offset=10\n"
                "  %15 = store.void %14, %3 align=2\n"
                "  %16 = stack_addr.ptr $0\n"
                "  %17 = address.ptr %16 offset=12\n"
                "  %18 = store.void %17, %5 align=4\n"
                "  %19 = stack_addr.ptr $0\n"
                "  %20 = load.m64 %19 align=8\n"
                "  %21 = stack_addr.ptr $0\n"
                "  %22 = address.ptr %21 offset=8\n"
                "  %23 = load.m8 %22 align=1\n"
                "  %24 = stack_addr.ptr $0\n"
                "  %25 = address.ptr %24 offset=10\n"
                "  %26 = load.m16 %25 align=2\n"
                "  %27 = stack_addr.ptr $0\n"
                "  %28 = address.ptr %27 offset=12\n"
                "  %29 = load.m32 %28 align=4\n"
                "  %30 = not.m64 %20\n"
                "  %31 = xor.m8 %23, %1\n"
                "  %32 = not.m16 %26\n"
                "  %33 = xor.m32 %29, %5\n"
                "  %34 = address.ptr %0 offset=128\n"
                "  %35 = store.void %34, %30 align=8\n"
                "  %36 = address.ptr %0 offset=136\n"
                "  %37 = store.void %36, %31 align=1\n"
                "  %38 = address.ptr %0 offset=138\n"
                "  %39 = store.void %38, %32 align=2\n"
                "  %40 = address.ptr %0 offset=140\n"
                "  %41 = store.void %40, %33 align=4\n"
                "  %42 = address.ptr %0 offset=144\n"
                "  %43 = store.void %42, %26 align=2\n"
                "  %44 = ret.void\n"
                "}\n",
        .frame_accesses = 8,
    },
    {
        // Masks live across divisions by a constant's reciprocal and by a register, whose fixed registers (rax and rdx)
        // the consumer saves in the general call-save rows, and across a call, whose masks' rows come after those; and
        // eight 64-bit constants, too wide for an immediate, live across all of them, which rematerialization recomputes.
        .name = "masks across divisions and a call",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.m16 %0 align=2\n"
                "  %3 = address.ptr %0 offset=8\n"
                "  %4 = load.m64 %3 align=8\n"
                "  %5 = address.ptr %0 offset=16\n"
                "  %6 = load.m8 %5 align=1\n"
                "  %7 = address.ptr %0 offset=24\n"
                "  %8 = load.i64 %7 align=8\n"
                "  %9 = address.ptr %0 offset=32\n"
                "  %10 = load.i64 %9 align=8\n"
                "  %11 = const.i64 0x0000000000000000123456789abcdef0\n"
                "  %12 = const.i64 0x00000000000000000fedcba987654321\n"
                "  %13 = const.i64 0x00000000000000008000000000000001\n"
                "  %14 = const.i64 0x0000000000000000a5a5a5a55a5a5a5a\n"
                "  %15 = const.i64 0x00000000000000000000000700000003\n"
                "  %16 = const.i64 0x0000000000000000ffffffff00000000\n"
                "  %17 = const.i64 0x00000000000000007fffffffffffffff\n"
                "  %18 = const.i64 0x00000000000000000123456700000000\n"
                "  %19 = const.i64 0x00000000000000000000000000000007\n"
                "  %20 = udiv.i64 %8, %19\n"
                "  %21 = srem.i64 %10, %19\n"
                "  %22 = const.i64 0x00000000000000000000000000000001\n"
                "  %23 = or.i64 %1, %22\n"
                "  %24 = sdiv.i64 %10, %23\n"
                "  %25 = urem.i64 %8, %23\n"
                "  %26 = call.i64 @9(%20) fixed=(i64) variadic=0\n"
                "  %27 = not.m16 %2\n"
                "  %28 = not.m64 %4\n"
                "  %29 = xor.i64 %11, %12\n"
                "  %30 = xor.i64 %29, %13\n"
                "  %31 = xor.i64 %30, %14\n"
                "  %32 = xor.i64 %31, %15\n"
                "  %33 = xor.i64 %32, %16\n"
                "  %34 = xor.i64 %33, %17\n"
                "  %35 = xor.i64 %34, %18\n"
                "  %36 = address.ptr %0 offset=128\n"
                "  %37 = store.void %36, %27 align=2\n"
                "  %38 = address.ptr %0 offset=136\n"
                "  %39 = store.void %38, %28 align=8\n"
                "  %40 = address.ptr %0 offset=144\n"
                "  %41 = store.void %40, %6 align=1\n"
                "  %42 = address.ptr %0 offset=152\n"
                "  %43 = store.void %42, %20 align=8\n"
                "  %44 = address.ptr %0 offset=160\n"
                "  %45 = store.void %44, %21 align=8\n"
                "  %46 = address.ptr %0 offset=168\n"
                "  %47 = store.void %46, %24 align=8\n"
                "  %48 = address.ptr %0 offset=176\n"
                "  %49 = store.void %48, %25 align=8\n"
                "  %50 = address.ptr %0 offset=184\n"
                "  %51 = store.void %50, %26 align=8\n"
                "  %52 = address.ptr %0 offset=192\n"
                "  %53 = store.void %52, %2 align=2\n"
                "  %54 = address.ptr %0 offset=200\n"
                "  %55 = store.void %54, %35 align=8\n"
                "  %56 = ret.void\n"
                "}\n",
        .count = 1000,
        .calls = true,
        .divisions = true,
    },
    // The IR's vectors (#141), v4i32, v8i32 and v16i32, beside the masks their comparisons make: their operations,
    // constants, phis, spills and saves around calls at their widths.
    {
        .name = "vector operations and comparisons at every width",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = add.v4i32 %2, %4\n"
                "  %6 = sub.v4i32 %2, %4\n"
                "  %7 = mul.v4i32 %2, %4\n"
                "  %8 = and.v4i32 %2, %4\n"
                "  %9 = or.v4i32 %2, %4\n"
                "  %10 = xor.v4i32 %2, %4\n"
                "  %11 = not.v4i32 %2\n"
                "  %12 = copy.v4i32 %11\n"
                "  %13 = vcmp.m8 %5, %4 eq\n"
                "  %14 = vselect.v4i32 %13, %6, %10\n"
                "  %15 = xor.v4i32 %12, %14\n"
                "  %16 = vcmp.m8 %6, %9 ne\n"
                "  %17 = vselect.v4i32 %16, %7, %11\n"
                "  %18 = add.v4i32 %15, %17\n"
                "  %19 = vcmp.m8 %7, %4 lt\n"
                "  %20 = vselect.v4i32 %19, %8, %5\n"
                "  %21 = sub.v4i32 %18, %20\n"
                "  %22 = vcmp.m8 %8, %11 le\n"
                "  %23 = vselect.v4i32 %22, %9, %6\n"
                "  %24 = xor.v4i32 %21, %23\n"
                "  %25 = vcmp.m8 %9, %4 gt\n"
                "  %26 = vselect.v4i32 %25, %10, %7\n"
                "  %27 = add.v4i32 %24, %26\n"
                "  %28 = vcmp.m8 %10, %6 ge\n"
                "  %29 = vselect.v4i32 %28, %11, %8\n"
                "  %30 = sub.v4i32 %27, %29\n"
                "  %31 = vcmp.m8 %11, %4 ult\n"
                "  %32 = vselect.v4i32 %31, %5, %9\n"
                "  %33 = xor.v4i32 %30, %32\n"
                "  %34 = vcmp.m8 %5, %8 ule\n"
                "  %35 = vselect.v4i32 %34, %6, %10\n"
                "  %36 = add.v4i32 %33, %35\n"
                "  %37 = vcmp.m8 %6, %4 ugt\n"
                "  %38 = vselect.v4i32 %37, %7, %11\n"
                "  %39 = sub.v4i32 %36, %38\n"
                "  %40 = vcmp.m8 %7, %10 uge\n"
                "  %41 = vselect.v4i32 %40, %8, %5\n"
                "  %42 = xor.v4i32 %39, %41\n"
                "  %43 = address.ptr %0 offset=128\n"
                "  %44 = store.void %43, %42 align=1\n"
                "  %45 = address.ptr %0 offset=242\n"
                "  %46 = store.void %45, %25 align=1\n"
                "  %47 = address.ptr %0 offset=32\n"
                "  %48 = load.v8i32 %47 align=1\n"
                "  %49 = address.ptr %0 offset=64\n"
                "  %50 = load.v8i32 %49 align=1\n"
                "  %51 = add.v8i32 %48, %50\n"
                "  %52 = sub.v8i32 %48, %50\n"
                "  %53 = mul.v8i32 %48, %50\n"
                "  %54 = and.v8i32 %48, %50\n"
                "  %55 = or.v8i32 %48, %50\n"
                "  %56 = xor.v8i32 %48, %50\n"
                "  %57 = not.v8i32 %48\n"
                "  %58 = copy.v8i32 %57\n"
                "  %59 = vcmp.m8 %51, %50 eq\n"
                "  %60 = vselect.v8i32 %59, %52, %56\n"
                "  %61 = xor.v8i32 %58, %60\n"
                "  %62 = vcmp.m8 %52, %55 ne\n"
                "  %63 = vselect.v8i32 %62, %53, %57\n"
                "  %64 = add.v8i32 %61, %63\n"
                "  %65 = vcmp.m8 %53, %50 lt\n"
                "  %66 = vselect.v8i32 %65, %54, %51\n"
                "  %67 = sub.v8i32 %64, %66\n"
                "  %68 = vcmp.m8 %54, %57 le\n"
                "  %69 = vselect.v8i32 %68, %55, %52\n"
                "  %70 = xor.v8i32 %67, %69\n"
                "  %71 = vcmp.m8 %55, %50 gt\n"
                "  %72 = vselect.v8i32 %71, %56, %53\n"
                "  %73 = add.v8i32 %70, %72\n"
                "  %74 = vcmp.m8 %56, %52 ge\n"
                "  %75 = vselect.v8i32 %74, %57, %54\n"
                "  %76 = sub.v8i32 %73, %75\n"
                "  %77 = vcmp.m8 %57, %50 ult\n"
                "  %78 = vselect.v8i32 %77, %51, %55\n"
                "  %79 = xor.v8i32 %76, %78\n"
                "  %80 = vcmp.m8 %51, %54 ule\n"
                "  %81 = vselect.v8i32 %80, %52, %56\n"
                "  %82 = add.v8i32 %79, %81\n"
                "  %83 = vcmp.m8 %52, %50 ugt\n"
                "  %84 = vselect.v8i32 %83, %53, %57\n"
                "  %85 = sub.v8i32 %82, %84\n"
                "  %86 = vcmp.m8 %53, %56 uge\n"
                "  %87 = vselect.v8i32 %86, %54, %51\n"
                "  %88 = xor.v8i32 %85, %87\n"
                "  %89 = address.ptr %0 offset=144\n"
                "  %90 = store.void %89, %88 align=1\n"
                "  %91 = address.ptr %0 offset=243\n"
                "  %92 = store.void %91, %74 align=1\n"
                "  %93 = address.ptr %0 offset=0\n"
                "  %94 = load.v16i32 %93 align=1\n"
                "  %95 = address.ptr %0 offset=64\n"
                "  %96 = load.v16i32 %95 align=1\n"
                "  %97 = add.v16i32 %94, %96\n"
                "  %98 = sub.v16i32 %94, %96\n"
                "  %99 = mul.v16i32 %94, %96\n"
                "  %100 = and.v16i32 %94, %96\n"
                "  %101 = or.v16i32 %94, %96\n"
                "  %102 = xor.v16i32 %94, %96\n"
                "  %103 = not.v16i32 %94\n"
                "  %104 = copy.v16i32 %103\n"
                "  %105 = vcmp.m16 %97, %96 eq\n"
                "  %106 = vselect.v16i32 %105, %98, %102\n"
                "  %107 = xor.v16i32 %104, %106\n"
                "  %108 = vcmp.m16 %98, %101 ne\n"
                "  %109 = vselect.v16i32 %108, %99, %103\n"
                "  %110 = add.v16i32 %107, %109\n"
                "  %111 = vcmp.m16 %99, %96 lt\n"
                "  %112 = vselect.v16i32 %111, %100, %97\n"
                "  %113 = sub.v16i32 %110, %112\n"
                "  %114 = vcmp.m16 %100, %103 le\n"
                "  %115 = vselect.v16i32 %114, %101, %98\n"
                "  %116 = xor.v16i32 %113, %115\n"
                "  %117 = vcmp.m16 %101, %96 gt\n"
                "  %118 = vselect.v16i32 %117, %102, %99\n"
                "  %119 = add.v16i32 %116, %118\n"
                "  %120 = vcmp.m16 %102, %98 ge\n"
                "  %121 = vselect.v16i32 %120, %103, %100\n"
                "  %122 = sub.v16i32 %119, %121\n"
                "  %123 = vcmp.m16 %103, %96 ult\n"
                "  %124 = vselect.v16i32 %123, %97, %101\n"
                "  %125 = xor.v16i32 %122, %124\n"
                "  %126 = vcmp.m16 %97, %100 ule\n"
                "  %127 = vselect.v16i32 %126, %98, %102\n"
                "  %128 = add.v16i32 %125, %127\n"
                "  %129 = vcmp.m16 %98, %96 ugt\n"
                "  %130 = vselect.v16i32 %129, %99, %103\n"
                "  %131 = sub.v16i32 %128, %130\n"
                "  %132 = vcmp.m16 %99, %102 uge\n"
                "  %133 = vselect.v16i32 %132, %100, %97\n"
                "  %134 = xor.v16i32 %131, %133\n"
                "  %135 = address.ptr %0 offset=176\n"
                "  %136 = store.void %135, %134 align=1\n"
                "  %137 = address.ptr %0 offset=240\n"
                "  %138 = store.void %137, %114 align=2\n"
                "  %139 = ret.void\n"
                "}\n",
    },
    // A comparison into a mask ANDed with another mask (#1847, #1631's form of a masked comparison's row): the AND's
    // pattern takes the comparison under the other mask as its writemask, one vpcmp or vcmpps, with the comparison
    // either operand, at 64 and 32 bytes, of integers and of floats (NaNs among the random ones); and a comparison the
    // function stores as well, which stays its own record and the AND a kand.
    {
        .name = "masked comparisons: a comparison into a mask ANDed with another, as its writemask",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v16i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=64\n"
                "  %4 = load.v16i32 %3 align=1\n"
                "  %5 = load.m16 %0 align=2\n"
                "  %6 = address.ptr %0 offset=2\n"
                "  %7 = load.m8 %6 align=1\n"
                "  %8 = vcmp.m16 %2, %4 lt\n"
                "  %9 = and.m16 %8, %5\n"
                "  %10 = address.ptr %0 offset=192\n"
                "  %11 = store.void %10, %9 align=2\n"
                "  %12 = address.ptr %0 offset=32\n"
                "  %13 = load.v8i32 %12 align=1\n"
                "  %14 = address.ptr %0 offset=96\n"
                "  %15 = load.v8i32 %14 align=1\n"
                "  %16 = vcmp.m8 %13, %15 ugt\n"
                "  %17 = and.m8 %7, %16\n"
                "  %18 = address.ptr %0 offset=194\n"
                "  %19 = store.void %18, %17 align=1\n"
                "  %20 = vcmp.m16 %4, %2 le lanes=f32\n"
                "  %21 = and.m16 %20, %5\n"
                "  %22 = address.ptr %0 offset=196\n"
                "  %23 = store.void %22, %21 align=2\n"
                "  %24 = vcmp.m16 %2, %4 eq\n"
                "  %25 = and.m16 %24, %5\n"
                "  %26 = address.ptr %0 offset=198\n"
                "  %27 = store.void %26, %25 align=2\n"
                "  %28 = address.ptr %0 offset=200\n"
                "  %29 = store.void %28, %24 align=2\n"
                "  %30 = ret.void\n"
                "}\n",
        .masked_compares = 3,
    },
    // The IR's lanes (#141's element types), at each width: 8-, 16- and 64-bit integers' arithmetic, products and shifts
    // by each lane's count (masked to twice the lane's bits, so that half shift past it), floats' and doubles'
    // arithmetic, and comparisons of each lanes by each predicate, into masks of a bit per lane and selections at those
    // lanes; the interpreter's model of them against Tier 1's code, NaNs, infinities and denormals among the random
    // floats. Generated by a script (the PR's description), kept as text so that a reader sees the IR.
    {
        .name = "16-byte vectors' lanes: 8-, 16- and 64-bit integers, floats and doubles (#141's element types)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=112\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = xor.v4i32 %2, %4\n"
                "  %6 = add.v16i8 %2, %4\n"
                "  %7 = sub.v16i8 %2, %4\n"
                "  %8 = vcmp.m16 %2, %7 eq lanes=i8\n"
                "  %9 = vselect.v16i8 %8, %6, %7\n"
                "  %10 = xor.v4i32 %5, %9\n"
                "  %11 = vcmp.m16 %7, %4 ne lanes=i8\n"
                "  %12 = vselect.v16i8 %11, %7, %6\n"
                "  %13 = xor.v4i32 %10, %12\n"
                "  %14 = vcmp.m16 %2, %4 lt lanes=i8\n"
                "  %15 = vselect.v16i8 %14, %6, %7\n"
                "  %16 = xor.v4i32 %13, %15\n"
                "  %17 = vcmp.m16 %7, %6 le lanes=i8\n"
                "  %18 = vselect.v16i8 %17, %7, %6\n"
                "  %19 = xor.v4i32 %16, %18\n"
                "  %20 = vcmp.m16 %2, %4 gt lanes=i8\n"
                "  %21 = vselect.v16i8 %20, %6, %7\n"
                "  %22 = xor.v4i32 %19, %21\n"
                "  %23 = vcmp.m16 %7, %4 ge lanes=i8\n"
                "  %24 = vselect.v16i8 %23, %7, %6\n"
                "  %25 = xor.v4i32 %22, %24\n"
                "  %26 = vcmp.m16 %2, %7 ult lanes=i8\n"
                "  %27 = vselect.v16i8 %26, %6, %7\n"
                "  %28 = xor.v4i32 %25, %27\n"
                "  %29 = vcmp.m16 %7, %4 ule lanes=i8\n"
                "  %30 = vselect.v16i8 %29, %7, %6\n"
                "  %31 = xor.v4i32 %28, %30\n"
                "  %32 = vcmp.m16 %2, %4 ugt lanes=i8\n"
                "  %33 = vselect.v16i8 %32, %6, %7\n"
                "  %34 = xor.v4i32 %31, %33\n"
                "  %35 = vcmp.m16 %7, %6 uge lanes=i8\n"
                "  %36 = vselect.v16i8 %35, %7, %6\n"
                "  %37 = xor.v4i32 %34, %36\n"
                "  %38 = xor.v4i32 %37, %6\n"
                "  %39 = xor.v4i32 %38, %7\n"
                "  %40 = add.v8i16 %2, %4\n"
                "  %41 = sub.v8i16 %2, %4\n"
                "  %42 = mul.v8i16 %2, %4\n"
                "  %43 = vconst.v4i32 0x001f001f001f001f001f001f001f001f\n"
                "  %44 = and.v4i32 %4, %43\n"
                "  %45 = shl.v8i16 %2, %44\n"
                "  %46 = lshr.v8i16 %2, %44\n"
                "  %47 = ashr.v8i16 %2, %44\n"
                "  %48 = vcmp.m8 %2, %41 eq lanes=i16\n"
                "  %49 = vselect.v8i16 %48, %40, %41\n"
                "  %50 = xor.v4i32 %39, %49\n"
                "  %51 = vcmp.m8 %41, %4 ne lanes=i16\n"
                "  %52 = vselect.v8i16 %51, %41, %42\n"
                "  %53 = xor.v4i32 %50, %52\n"
                "  %54 = vcmp.m8 %2, %4 lt lanes=i16\n"
                "  %55 = vselect.v8i16 %54, %42, %45\n"
                "  %56 = xor.v4i32 %53, %55\n"
                "  %57 = vcmp.m8 %45, %46 le lanes=i16\n"
                "  %58 = vselect.v8i16 %57, %45, %46\n"
                "  %59 = xor.v4i32 %56, %58\n"
                "  %60 = vcmp.m8 %2, %4 gt lanes=i16\n"
                "  %61 = vselect.v8i16 %60, %46, %47\n"
                "  %62 = xor.v4i32 %59, %61\n"
                "  %63 = vcmp.m8 %47, %4 ge lanes=i16\n"
                "  %64 = vselect.v8i16 %63, %47, %40\n"
                "  %65 = xor.v4i32 %62, %64\n"
                "  %66 = vcmp.m8 %2, %41 ult lanes=i16\n"
                "  %67 = vselect.v8i16 %66, %40, %41\n"
                "  %68 = xor.v4i32 %65, %67\n"
                "  %69 = vcmp.m8 %41, %4 ule lanes=i16\n"
                "  %70 = vselect.v8i16 %69, %41, %42\n"
                "  %71 = xor.v4i32 %68, %70\n"
                "  %72 = vcmp.m8 %2, %4 ugt lanes=i16\n"
                "  %73 = vselect.v8i16 %72, %42, %45\n"
                "  %74 = xor.v4i32 %71, %73\n"
                "  %75 = vcmp.m8 %45, %46 uge lanes=i16\n"
                "  %76 = vselect.v8i16 %75, %45, %46\n"
                "  %77 = xor.v4i32 %74, %76\n"
                "  %78 = xor.v4i32 %77, %40\n"
                "  %79 = xor.v4i32 %78, %41\n"
                "  %80 = xor.v4i32 %79, %42\n"
                "  %81 = xor.v4i32 %80, %45\n"
                "  %82 = xor.v4i32 %81, %46\n"
                "  %83 = xor.v4i32 %82, %47\n"
                "  %84 = add.v2i64 %2, %4\n"
                "  %85 = sub.v2i64 %2, %4\n"
                "  %86 = mul.v2i64 %2, %4\n"
                "  %87 = vconst.v4i32 0x000000000000007f000000000000007f\n"
                "  %88 = and.v4i32 %4, %87\n"
                "  %89 = shl.v2i64 %2, %88\n"
                "  %90 = lshr.v2i64 %2, %88\n"
                "  %91 = ashr.v2i64 %2, %88\n"
                "  %92 = vcmp.m8 %2, %85 eq lanes=i64\n"
                "  %93 = vselect.v2i64 %92, %84, %85\n"
                "  %94 = xor.v4i32 %83, %93\n"
                "  %95 = vcmp.m8 %85, %4 ne lanes=i64\n"
                "  %96 = vselect.v2i64 %95, %85, %86\n"
                "  %97 = xor.v4i32 %94, %96\n"
                "  %98 = vcmp.m8 %2, %4 lt lanes=i64\n"
                "  %99 = vselect.v2i64 %98, %86, %89\n"
                "  %100 = xor.v4i32 %97, %99\n"
                "  %101 = vcmp.m8 %89, %90 le lanes=i64\n"
                "  %102 = vselect.v2i64 %101, %89, %90\n"
                "  %103 = xor.v4i32 %100, %102\n"
                "  %104 = vcmp.m8 %2, %4 gt lanes=i64\n"
                "  %105 = vselect.v2i64 %104, %90, %91\n"
                "  %106 = xor.v4i32 %103, %105\n"
                "  %107 = vcmp.m8 %91, %4 ge lanes=i64\n"
                "  %108 = vselect.v2i64 %107, %91, %84\n"
                "  %109 = xor.v4i32 %106, %108\n"
                "  %110 = vcmp.m8 %2, %85 ult lanes=i64\n"
                "  %111 = vselect.v2i64 %110, %84, %85\n"
                "  %112 = xor.v4i32 %109, %111\n"
                "  %113 = vcmp.m8 %85, %4 ule lanes=i64\n"
                "  %114 = vselect.v2i64 %113, %85, %86\n"
                "  %115 = xor.v4i32 %112, %114\n"
                "  %116 = vcmp.m8 %2, %4 ugt lanes=i64\n"
                "  %117 = vselect.v2i64 %116, %86, %89\n"
                "  %118 = xor.v4i32 %115, %117\n"
                "  %119 = vcmp.m8 %89, %90 uge lanes=i64\n"
                "  %120 = vselect.v2i64 %119, %89, %90\n"
                "  %121 = xor.v4i32 %118, %120\n"
                "  %122 = xor.v4i32 %121, %84\n"
                "  %123 = xor.v4i32 %122, %85\n"
                "  %124 = xor.v4i32 %123, %86\n"
                "  %125 = xor.v4i32 %124, %89\n"
                "  %126 = xor.v4i32 %125, %90\n"
                "  %127 = xor.v4i32 %126, %91\n"
                "  %128 = fadd.v4f32 %2, %4\n"
                "  %129 = fsub.v4f32 %2, %4\n"
                "  %130 = fmul.v4f32 %2, %4\n"
                "  %131 = fdiv.v4f32 %2, %4\n"
                "  %132 = vcmp.m8 %2, %129 eq lanes=f32\n"
                "  %133 = vselect.v4f32 %132, %128, %129\n"
                "  %134 = xor.v4i32 %127, %133\n"
                "  %135 = vcmp.m8 %129, %4 ne lanes=f32\n"
                "  %136 = vselect.v4f32 %135, %129, %130\n"
                "  %137 = xor.v4i32 %134, %136\n"
                "  %138 = vcmp.m8 %2, %4 lt lanes=f32\n"
                "  %139 = vselect.v4f32 %138, %130, %131\n"
                "  %140 = xor.v4i32 %137, %139\n"
                "  %141 = vcmp.m8 %131, %128 le lanes=f32\n"
                "  %142 = vselect.v4f32 %141, %131, %128\n"
                "  %143 = xor.v4i32 %140, %142\n"
                "  %144 = vcmp.m8 %2, %4 gt lanes=f32\n"
                "  %145 = vselect.v4f32 %144, %128, %129\n"
                "  %146 = xor.v4i32 %143, %145\n"
                "  %147 = vcmp.m8 %129, %4 ge lanes=f32\n"
                "  %148 = vselect.v4f32 %147, %129, %130\n"
                "  %149 = xor.v4i32 %146, %148\n"
                "  %150 = xor.v4i32 %149, %128\n"
                "  %151 = xor.v4i32 %150, %129\n"
                "  %152 = xor.v4i32 %151, %130\n"
                "  %153 = xor.v4i32 %152, %131\n"
                "  %154 = fadd.v2f64 %2, %4\n"
                "  %155 = fsub.v2f64 %2, %4\n"
                "  %156 = fmul.v2f64 %2, %4\n"
                "  %157 = fdiv.v2f64 %2, %4\n"
                "  %158 = vcmp.m8 %2, %155 eq lanes=f64\n"
                "  %159 = vselect.v2f64 %158, %154, %155\n"
                "  %160 = xor.v4i32 %153, %159\n"
                "  %161 = vcmp.m8 %155, %4 ne lanes=f64\n"
                "  %162 = vselect.v2f64 %161, %155, %156\n"
                "  %163 = xor.v4i32 %160, %162\n"
                "  %164 = vcmp.m8 %2, %4 lt lanes=f64\n"
                "  %165 = vselect.v2f64 %164, %156, %157\n"
                "  %166 = xor.v4i32 %163, %165\n"
                "  %167 = vcmp.m8 %157, %154 le lanes=f64\n"
                "  %168 = vselect.v2f64 %167, %157, %154\n"
                "  %169 = xor.v4i32 %166, %168\n"
                "  %170 = vcmp.m8 %2, %4 gt lanes=f64\n"
                "  %171 = vselect.v2f64 %170, %154, %155\n"
                "  %172 = xor.v4i32 %169, %171\n"
                "  %173 = vcmp.m8 %155, %4 ge lanes=f64\n"
                "  %174 = vselect.v2f64 %173, %155, %156\n"
                "  %175 = xor.v4i32 %172, %174\n"
                "  %176 = xor.v4i32 %175, %154\n"
                "  %177 = xor.v4i32 %176, %155\n"
                "  %178 = xor.v4i32 %177, %156\n"
                "  %179 = xor.v4i32 %178, %157\n"
                "  %180 = address.ptr %0 offset=240\n"
                "  %181 = store.void %180, %179 align=1\n"
                "  %182 = address.ptr %0 offset=128\n"
                "  %183 = store.void %182, %158 align=1\n"
                "  %184 = address.ptr %0 offset=129\n"
                "  %185 = store.void %184, %161 align=1\n"
                "  %186 = address.ptr %0 offset=130\n"
                "  %187 = store.void %186, %164 align=1\n"
                "  %188 = address.ptr %0 offset=131\n"
                "  %189 = store.void %188, %167 align=1\n"
                "  %190 = address.ptr %0 offset=132\n"
                "  %191 = store.void %190, %170 align=1\n"
                "  %192 = address.ptr %0 offset=133\n"
                "  %193 = store.void %192, %173 align=1\n"
                "  %194 = ret.void\n"
                "}\n",
    },
    {
        .name = "32-byte vectors' lanes: 8-, 16- and 64-bit integers, floats and doubles (#141's element types)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v8i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=96\n"
                "  %4 = load.v8i32 %3 align=1\n"
                "  %5 = xor.v8i32 %2, %4\n"
                "  %6 = add.v32i8 %2, %4\n"
                "  %7 = sub.v32i8 %2, %4\n"
                "  %8 = vcmp.m32 %2, %7 eq lanes=i8\n"
                "  %9 = vselect.v32i8 %8, %6, %7\n"
                "  %10 = xor.v8i32 %5, %9\n"
                "  %11 = vcmp.m32 %7, %4 ne lanes=i8\n"
                "  %12 = vselect.v32i8 %11, %7, %6\n"
                "  %13 = xor.v8i32 %10, %12\n"
                "  %14 = vcmp.m32 %2, %4 lt lanes=i8\n"
                "  %15 = vselect.v32i8 %14, %6, %7\n"
                "  %16 = xor.v8i32 %13, %15\n"
                "  %17 = vcmp.m32 %7, %6 le lanes=i8\n"
                "  %18 = vselect.v32i8 %17, %7, %6\n"
                "  %19 = xor.v8i32 %16, %18\n"
                "  %20 = vcmp.m32 %2, %4 gt lanes=i8\n"
                "  %21 = vselect.v32i8 %20, %6, %7\n"
                "  %22 = xor.v8i32 %19, %21\n"
                "  %23 = vcmp.m32 %7, %4 ge lanes=i8\n"
                "  %24 = vselect.v32i8 %23, %7, %6\n"
                "  %25 = xor.v8i32 %22, %24\n"
                "  %26 = vcmp.m32 %2, %7 ult lanes=i8\n"
                "  %27 = vselect.v32i8 %26, %6, %7\n"
                "  %28 = xor.v8i32 %25, %27\n"
                "  %29 = vcmp.m32 %7, %4 ule lanes=i8\n"
                "  %30 = vselect.v32i8 %29, %7, %6\n"
                "  %31 = xor.v8i32 %28, %30\n"
                "  %32 = vcmp.m32 %2, %4 ugt lanes=i8\n"
                "  %33 = vselect.v32i8 %32, %6, %7\n"
                "  %34 = xor.v8i32 %31, %33\n"
                "  %35 = vcmp.m32 %7, %6 uge lanes=i8\n"
                "  %36 = vselect.v32i8 %35, %7, %6\n"
                "  %37 = xor.v8i32 %34, %36\n"
                "  %38 = xor.v8i32 %37, %6\n"
                "  %39 = xor.v8i32 %38, %7\n"
                "  %40 = add.v16i16 %2, %4\n"
                "  %41 = sub.v16i16 %2, %4\n"
                "  %42 = mul.v16i16 %2, %4\n"
                "  %43 = vconst.v8i32 0x001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f\n"
                "  %44 = and.v8i32 %4, %43\n"
                "  %45 = shl.v16i16 %2, %44\n"
                "  %46 = lshr.v16i16 %2, %44\n"
                "  %47 = ashr.v16i16 %2, %44\n"
                "  %48 = vcmp.m16 %2, %41 eq lanes=i16\n"
                "  %49 = vselect.v16i16 %48, %40, %41\n"
                "  %50 = xor.v8i32 %39, %49\n"
                "  %51 = vcmp.m16 %41, %4 ne lanes=i16\n"
                "  %52 = vselect.v16i16 %51, %41, %42\n"
                "  %53 = xor.v8i32 %50, %52\n"
                "  %54 = vcmp.m16 %2, %4 lt lanes=i16\n"
                "  %55 = vselect.v16i16 %54, %42, %45\n"
                "  %56 = xor.v8i32 %53, %55\n"
                "  %57 = vcmp.m16 %45, %46 le lanes=i16\n"
                "  %58 = vselect.v16i16 %57, %45, %46\n"
                "  %59 = xor.v8i32 %56, %58\n"
                "  %60 = vcmp.m16 %2, %4 gt lanes=i16\n"
                "  %61 = vselect.v16i16 %60, %46, %47\n"
                "  %62 = xor.v8i32 %59, %61\n"
                "  %63 = vcmp.m16 %47, %4 ge lanes=i16\n"
                "  %64 = vselect.v16i16 %63, %47, %40\n"
                "  %65 = xor.v8i32 %62, %64\n"
                "  %66 = vcmp.m16 %2, %41 ult lanes=i16\n"
                "  %67 = vselect.v16i16 %66, %40, %41\n"
                "  %68 = xor.v8i32 %65, %67\n"
                "  %69 = vcmp.m16 %41, %4 ule lanes=i16\n"
                "  %70 = vselect.v16i16 %69, %41, %42\n"
                "  %71 = xor.v8i32 %68, %70\n"
                "  %72 = vcmp.m16 %2, %4 ugt lanes=i16\n"
                "  %73 = vselect.v16i16 %72, %42, %45\n"
                "  %74 = xor.v8i32 %71, %73\n"
                "  %75 = vcmp.m16 %45, %46 uge lanes=i16\n"
                "  %76 = vselect.v16i16 %75, %45, %46\n"
                "  %77 = xor.v8i32 %74, %76\n"
                "  %78 = xor.v8i32 %77, %40\n"
                "  %79 = xor.v8i32 %78, %41\n"
                "  %80 = xor.v8i32 %79, %42\n"
                "  %81 = xor.v8i32 %80, %45\n"
                "  %82 = xor.v8i32 %81, %46\n"
                "  %83 = xor.v8i32 %82, %47\n"
                "  %84 = add.v4i64 %2, %4\n"
                "  %85 = sub.v4i64 %2, %4\n"
                "  %86 = mul.v4i64 %2, %4\n"
                "  %87 = vconst.v8i32 0x000000000000007f000000000000007f000000000000007f000000000000007f\n"
                "  %88 = and.v8i32 %4, %87\n"
                "  %89 = shl.v4i64 %2, %88\n"
                "  %90 = lshr.v4i64 %2, %88\n"
                "  %91 = ashr.v4i64 %2, %88\n"
                "  %92 = vcmp.m8 %2, %85 eq lanes=i64\n"
                "  %93 = vselect.v4i64 %92, %84, %85\n"
                "  %94 = xor.v8i32 %83, %93\n"
                "  %95 = vcmp.m8 %85, %4 ne lanes=i64\n"
                "  %96 = vselect.v4i64 %95, %85, %86\n"
                "  %97 = xor.v8i32 %94, %96\n"
                "  %98 = vcmp.m8 %2, %4 lt lanes=i64\n"
                "  %99 = vselect.v4i64 %98, %86, %89\n"
                "  %100 = xor.v8i32 %97, %99\n"
                "  %101 = vcmp.m8 %89, %90 le lanes=i64\n"
                "  %102 = vselect.v4i64 %101, %89, %90\n"
                "  %103 = xor.v8i32 %100, %102\n"
                "  %104 = vcmp.m8 %2, %4 gt lanes=i64\n"
                "  %105 = vselect.v4i64 %104, %90, %91\n"
                "  %106 = xor.v8i32 %103, %105\n"
                "  %107 = vcmp.m8 %91, %4 ge lanes=i64\n"
                "  %108 = vselect.v4i64 %107, %91, %84\n"
                "  %109 = xor.v8i32 %106, %108\n"
                "  %110 = vcmp.m8 %2, %85 ult lanes=i64\n"
                "  %111 = vselect.v4i64 %110, %84, %85\n"
                "  %112 = xor.v8i32 %109, %111\n"
                "  %113 = vcmp.m8 %85, %4 ule lanes=i64\n"
                "  %114 = vselect.v4i64 %113, %85, %86\n"
                "  %115 = xor.v8i32 %112, %114\n"
                "  %116 = vcmp.m8 %2, %4 ugt lanes=i64\n"
                "  %117 = vselect.v4i64 %116, %86, %89\n"
                "  %118 = xor.v8i32 %115, %117\n"
                "  %119 = vcmp.m8 %89, %90 uge lanes=i64\n"
                "  %120 = vselect.v4i64 %119, %89, %90\n"
                "  %121 = xor.v8i32 %118, %120\n"
                "  %122 = xor.v8i32 %121, %84\n"
                "  %123 = xor.v8i32 %122, %85\n"
                "  %124 = xor.v8i32 %123, %86\n"
                "  %125 = xor.v8i32 %124, %89\n"
                "  %126 = xor.v8i32 %125, %90\n"
                "  %127 = xor.v8i32 %126, %91\n"
                "  %128 = fadd.v8f32 %2, %4\n"
                "  %129 = fsub.v8f32 %2, %4\n"
                "  %130 = fmul.v8f32 %2, %4\n"
                "  %131 = fdiv.v8f32 %2, %4\n"
                "  %132 = vcmp.m8 %2, %129 eq lanes=f32\n"
                "  %133 = vselect.v8f32 %132, %128, %129\n"
                "  %134 = xor.v8i32 %127, %133\n"
                "  %135 = vcmp.m8 %129, %4 ne lanes=f32\n"
                "  %136 = vselect.v8f32 %135, %129, %130\n"
                "  %137 = xor.v8i32 %134, %136\n"
                "  %138 = vcmp.m8 %2, %4 lt lanes=f32\n"
                "  %139 = vselect.v8f32 %138, %130, %131\n"
                "  %140 = xor.v8i32 %137, %139\n"
                "  %141 = vcmp.m8 %131, %128 le lanes=f32\n"
                "  %142 = vselect.v8f32 %141, %131, %128\n"
                "  %143 = xor.v8i32 %140, %142\n"
                "  %144 = vcmp.m8 %2, %4 gt lanes=f32\n"
                "  %145 = vselect.v8f32 %144, %128, %129\n"
                "  %146 = xor.v8i32 %143, %145\n"
                "  %147 = vcmp.m8 %129, %4 ge lanes=f32\n"
                "  %148 = vselect.v8f32 %147, %129, %130\n"
                "  %149 = xor.v8i32 %146, %148\n"
                "  %150 = xor.v8i32 %149, %128\n"
                "  %151 = xor.v8i32 %150, %129\n"
                "  %152 = xor.v8i32 %151, %130\n"
                "  %153 = xor.v8i32 %152, %131\n"
                "  %154 = fadd.v4f64 %2, %4\n"
                "  %155 = fsub.v4f64 %2, %4\n"
                "  %156 = fmul.v4f64 %2, %4\n"
                "  %157 = fdiv.v4f64 %2, %4\n"
                "  %158 = vcmp.m8 %2, %155 eq lanes=f64\n"
                "  %159 = vselect.v4f64 %158, %154, %155\n"
                "  %160 = xor.v8i32 %153, %159\n"
                "  %161 = vcmp.m8 %155, %4 ne lanes=f64\n"
                "  %162 = vselect.v4f64 %161, %155, %156\n"
                "  %163 = xor.v8i32 %160, %162\n"
                "  %164 = vcmp.m8 %2, %4 lt lanes=f64\n"
                "  %165 = vselect.v4f64 %164, %156, %157\n"
                "  %166 = xor.v8i32 %163, %165\n"
                "  %167 = vcmp.m8 %157, %154 le lanes=f64\n"
                "  %168 = vselect.v4f64 %167, %157, %154\n"
                "  %169 = xor.v8i32 %166, %168\n"
                "  %170 = vcmp.m8 %2, %4 gt lanes=f64\n"
                "  %171 = vselect.v4f64 %170, %154, %155\n"
                "  %172 = xor.v8i32 %169, %171\n"
                "  %173 = vcmp.m8 %155, %4 ge lanes=f64\n"
                "  %174 = vselect.v4f64 %173, %155, %156\n"
                "  %175 = xor.v8i32 %172, %174\n"
                "  %176 = xor.v8i32 %175, %154\n"
                "  %177 = xor.v8i32 %176, %155\n"
                "  %178 = xor.v8i32 %177, %156\n"
                "  %179 = xor.v8i32 %178, %157\n"
                "  %180 = address.ptr %0 offset=224\n"
                "  %181 = store.void %180, %179 align=1\n"
                "  %182 = address.ptr %0 offset=128\n"
                "  %183 = store.void %182, %158 align=1\n"
                "  %184 = address.ptr %0 offset=129\n"
                "  %185 = store.void %184, %161 align=1\n"
                "  %186 = address.ptr %0 offset=130\n"
                "  %187 = store.void %186, %164 align=1\n"
                "  %188 = address.ptr %0 offset=131\n"
                "  %189 = store.void %188, %167 align=1\n"
                "  %190 = address.ptr %0 offset=132\n"
                "  %191 = store.void %190, %170 align=1\n"
                "  %192 = address.ptr %0 offset=133\n"
                "  %193 = store.void %192, %173 align=1\n"
                "  %194 = ret.void\n"
                "}\n",
    },
    {
        .name = "64-byte vectors' lanes: 8-, 16- and 64-bit integers, floats and doubles (#141's element types)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v16i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=64\n"
                "  %4 = load.v16i32 %3 align=1\n"
                "  %5 = xor.v16i32 %2, %4\n"
                "  %6 = add.v64i8 %2, %4\n"
                "  %7 = sub.v64i8 %2, %4\n"
                "  %8 = vcmp.m64 %2, %7 eq lanes=i8\n"
                "  %9 = vselect.v64i8 %8, %6, %7\n"
                "  %10 = xor.v16i32 %5, %9\n"
                "  %11 = vcmp.m64 %7, %4 ne lanes=i8\n"
                "  %12 = vselect.v64i8 %11, %7, %6\n"
                "  %13 = xor.v16i32 %10, %12\n"
                "  %14 = vcmp.m64 %2, %4 lt lanes=i8\n"
                "  %15 = vselect.v64i8 %14, %6, %7\n"
                "  %16 = xor.v16i32 %13, %15\n"
                "  %17 = vcmp.m64 %7, %6 le lanes=i8\n"
                "  %18 = vselect.v64i8 %17, %7, %6\n"
                "  %19 = xor.v16i32 %16, %18\n"
                "  %20 = vcmp.m64 %2, %4 gt lanes=i8\n"
                "  %21 = vselect.v64i8 %20, %6, %7\n"
                "  %22 = xor.v16i32 %19, %21\n"
                "  %23 = vcmp.m64 %7, %4 ge lanes=i8\n"
                "  %24 = vselect.v64i8 %23, %7, %6\n"
                "  %25 = xor.v16i32 %22, %24\n"
                "  %26 = vcmp.m64 %2, %7 ult lanes=i8\n"
                "  %27 = vselect.v64i8 %26, %6, %7\n"
                "  %28 = xor.v16i32 %25, %27\n"
                "  %29 = vcmp.m64 %7, %4 ule lanes=i8\n"
                "  %30 = vselect.v64i8 %29, %7, %6\n"
                "  %31 = xor.v16i32 %28, %30\n"
                "  %32 = vcmp.m64 %2, %4 ugt lanes=i8\n"
                "  %33 = vselect.v64i8 %32, %6, %7\n"
                "  %34 = xor.v16i32 %31, %33\n"
                "  %35 = vcmp.m64 %7, %6 uge lanes=i8\n"
                "  %36 = vselect.v64i8 %35, %7, %6\n"
                "  %37 = xor.v16i32 %34, %36\n"
                "  %38 = xor.v16i32 %37, %6\n"
                "  %39 = xor.v16i32 %38, %7\n"
                "  %40 = add.v32i16 %2, %4\n"
                "  %41 = sub.v32i16 %2, %4\n"
                "  %42 = mul.v32i16 %2, %4\n"
                "  %43 = vconst.v16i32 0x001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f001f\n"
                "  %44 = and.v16i32 %4, %43\n"
                "  %45 = shl.v32i16 %2, %44\n"
                "  %46 = lshr.v32i16 %2, %44\n"
                "  %47 = ashr.v32i16 %2, %44\n"
                "  %48 = vcmp.m32 %2, %41 eq lanes=i16\n"
                "  %49 = vselect.v32i16 %48, %40, %41\n"
                "  %50 = xor.v16i32 %39, %49\n"
                "  %51 = vcmp.m32 %41, %4 ne lanes=i16\n"
                "  %52 = vselect.v32i16 %51, %41, %42\n"
                "  %53 = xor.v16i32 %50, %52\n"
                "  %54 = vcmp.m32 %2, %4 lt lanes=i16\n"
                "  %55 = vselect.v32i16 %54, %42, %45\n"
                "  %56 = xor.v16i32 %53, %55\n"
                "  %57 = vcmp.m32 %45, %46 le lanes=i16\n"
                "  %58 = vselect.v32i16 %57, %45, %46\n"
                "  %59 = xor.v16i32 %56, %58\n"
                "  %60 = vcmp.m32 %2, %4 gt lanes=i16\n"
                "  %61 = vselect.v32i16 %60, %46, %47\n"
                "  %62 = xor.v16i32 %59, %61\n"
                "  %63 = vcmp.m32 %47, %4 ge lanes=i16\n"
                "  %64 = vselect.v32i16 %63, %47, %40\n"
                "  %65 = xor.v16i32 %62, %64\n"
                "  %66 = vcmp.m32 %2, %41 ult lanes=i16\n"
                "  %67 = vselect.v32i16 %66, %40, %41\n"
                "  %68 = xor.v16i32 %65, %67\n"
                "  %69 = vcmp.m32 %41, %4 ule lanes=i16\n"
                "  %70 = vselect.v32i16 %69, %41, %42\n"
                "  %71 = xor.v16i32 %68, %70\n"
                "  %72 = vcmp.m32 %2, %4 ugt lanes=i16\n"
                "  %73 = vselect.v32i16 %72, %42, %45\n"
                "  %74 = xor.v16i32 %71, %73\n"
                "  %75 = vcmp.m32 %45, %46 uge lanes=i16\n"
                "  %76 = vselect.v32i16 %75, %45, %46\n"
                "  %77 = xor.v16i32 %74, %76\n"
                "  %78 = xor.v16i32 %77, %40\n"
                "  %79 = xor.v16i32 %78, %41\n"
                "  %80 = xor.v16i32 %79, %42\n"
                "  %81 = xor.v16i32 %80, %45\n"
                "  %82 = xor.v16i32 %81, %46\n"
                "  %83 = xor.v16i32 %82, %47\n"
                "  %84 = add.v8i64 %2, %4\n"
                "  %85 = sub.v8i64 %2, %4\n"
                "  %86 = mul.v8i64 %2, %4\n"
                "  %87 = vconst.v16i32 0x000000000000007f000000000000007f000000000000007f000000000000007f000000000000007f000000000000007f000000000000007f000000000000007f\n"
                "  %88 = and.v16i32 %4, %87\n"
                "  %89 = shl.v8i64 %2, %88\n"
                "  %90 = lshr.v8i64 %2, %88\n"
                "  %91 = ashr.v8i64 %2, %88\n"
                "  %92 = vcmp.m8 %2, %85 eq lanes=i64\n"
                "  %93 = vselect.v8i64 %92, %84, %85\n"
                "  %94 = xor.v16i32 %83, %93\n"
                "  %95 = vcmp.m8 %85, %4 ne lanes=i64\n"
                "  %96 = vselect.v8i64 %95, %85, %86\n"
                "  %97 = xor.v16i32 %94, %96\n"
                "  %98 = vcmp.m8 %2, %4 lt lanes=i64\n"
                "  %99 = vselect.v8i64 %98, %86, %89\n"
                "  %100 = xor.v16i32 %97, %99\n"
                "  %101 = vcmp.m8 %89, %90 le lanes=i64\n"
                "  %102 = vselect.v8i64 %101, %89, %90\n"
                "  %103 = xor.v16i32 %100, %102\n"
                "  %104 = vcmp.m8 %2, %4 gt lanes=i64\n"
                "  %105 = vselect.v8i64 %104, %90, %91\n"
                "  %106 = xor.v16i32 %103, %105\n"
                "  %107 = vcmp.m8 %91, %4 ge lanes=i64\n"
                "  %108 = vselect.v8i64 %107, %91, %84\n"
                "  %109 = xor.v16i32 %106, %108\n"
                "  %110 = vcmp.m8 %2, %85 ult lanes=i64\n"
                "  %111 = vselect.v8i64 %110, %84, %85\n"
                "  %112 = xor.v16i32 %109, %111\n"
                "  %113 = vcmp.m8 %85, %4 ule lanes=i64\n"
                "  %114 = vselect.v8i64 %113, %85, %86\n"
                "  %115 = xor.v16i32 %112, %114\n"
                "  %116 = vcmp.m8 %2, %4 ugt lanes=i64\n"
                "  %117 = vselect.v8i64 %116, %86, %89\n"
                "  %118 = xor.v16i32 %115, %117\n"
                "  %119 = vcmp.m8 %89, %90 uge lanes=i64\n"
                "  %120 = vselect.v8i64 %119, %89, %90\n"
                "  %121 = xor.v16i32 %118, %120\n"
                "  %122 = xor.v16i32 %121, %84\n"
                "  %123 = xor.v16i32 %122, %85\n"
                "  %124 = xor.v16i32 %123, %86\n"
                "  %125 = xor.v16i32 %124, %89\n"
                "  %126 = xor.v16i32 %125, %90\n"
                "  %127 = xor.v16i32 %126, %91\n"
                "  %128 = fadd.v16f32 %2, %4\n"
                "  %129 = fsub.v16f32 %2, %4\n"
                "  %130 = fmul.v16f32 %2, %4\n"
                "  %131 = fdiv.v16f32 %2, %4\n"
                "  %132 = vcmp.m16 %2, %129 eq lanes=f32\n"
                "  %133 = vselect.v16f32 %132, %128, %129\n"
                "  %134 = xor.v16i32 %127, %133\n"
                "  %135 = vcmp.m16 %129, %4 ne lanes=f32\n"
                "  %136 = vselect.v16f32 %135, %129, %130\n"
                "  %137 = xor.v16i32 %134, %136\n"
                "  %138 = vcmp.m16 %2, %4 lt lanes=f32\n"
                "  %139 = vselect.v16f32 %138, %130, %131\n"
                "  %140 = xor.v16i32 %137, %139\n"
                "  %141 = vcmp.m16 %131, %128 le lanes=f32\n"
                "  %142 = vselect.v16f32 %141, %131, %128\n"
                "  %143 = xor.v16i32 %140, %142\n"
                "  %144 = vcmp.m16 %2, %4 gt lanes=f32\n"
                "  %145 = vselect.v16f32 %144, %128, %129\n"
                "  %146 = xor.v16i32 %143, %145\n"
                "  %147 = vcmp.m16 %129, %4 ge lanes=f32\n"
                "  %148 = vselect.v16f32 %147, %129, %130\n"
                "  %149 = xor.v16i32 %146, %148\n"
                "  %150 = xor.v16i32 %149, %128\n"
                "  %151 = xor.v16i32 %150, %129\n"
                "  %152 = xor.v16i32 %151, %130\n"
                "  %153 = xor.v16i32 %152, %131\n"
                "  %154 = fadd.v8f64 %2, %4\n"
                "  %155 = fsub.v8f64 %2, %4\n"
                "  %156 = fmul.v8f64 %2, %4\n"
                "  %157 = fdiv.v8f64 %2, %4\n"
                "  %158 = vcmp.m8 %2, %155 eq lanes=f64\n"
                "  %159 = vselect.v8f64 %158, %154, %155\n"
                "  %160 = xor.v16i32 %153, %159\n"
                "  %161 = vcmp.m8 %155, %4 ne lanes=f64\n"
                "  %162 = vselect.v8f64 %161, %155, %156\n"
                "  %163 = xor.v16i32 %160, %162\n"
                "  %164 = vcmp.m8 %2, %4 lt lanes=f64\n"
                "  %165 = vselect.v8f64 %164, %156, %157\n"
                "  %166 = xor.v16i32 %163, %165\n"
                "  %167 = vcmp.m8 %157, %154 le lanes=f64\n"
                "  %168 = vselect.v8f64 %167, %157, %154\n"
                "  %169 = xor.v16i32 %166, %168\n"
                "  %170 = vcmp.m8 %2, %4 gt lanes=f64\n"
                "  %171 = vselect.v8f64 %170, %154, %155\n"
                "  %172 = xor.v16i32 %169, %171\n"
                "  %173 = vcmp.m8 %155, %4 ge lanes=f64\n"
                "  %174 = vselect.v8f64 %173, %155, %156\n"
                "  %175 = xor.v16i32 %172, %174\n"
                "  %176 = xor.v16i32 %175, %154\n"
                "  %177 = xor.v16i32 %176, %155\n"
                "  %178 = xor.v16i32 %177, %156\n"
                "  %179 = xor.v16i32 %178, %157\n"
                "  %180 = address.ptr %0 offset=192\n"
                "  %181 = store.void %180, %179 align=1\n"
                "  %182 = address.ptr %0 offset=128\n"
                "  %183 = store.void %182, %158 align=1\n"
                "  %184 = address.ptr %0 offset=129\n"
                "  %185 = store.void %184, %161 align=1\n"
                "  %186 = address.ptr %0 offset=130\n"
                "  %187 = store.void %186, %164 align=1\n"
                "  %188 = address.ptr %0 offset=131\n"
                "  %189 = store.void %188, %167 align=1\n"
                "  %190 = address.ptr %0 offset=132\n"
                "  %191 = store.void %190, %170 align=1\n"
                "  %192 = address.ptr %0 offset=133\n"
                "  %193 = store.void %192, %173 align=1\n"
                "  %194 = ret.void\n"
                "}\n",
    },
    // The SSE targets' vectors (#141): 16-byte vectors' integer arithmetic of each lane width and the bitwise operations
    // in every operand order (the destination the left operand, the right one or neither: the two-operand expansion's
    // copies, and xmm15's for a subtraction into its right operand), floats' and doubles' arithmetic, a floating
    // subtraction and division into their right operand's register, and constants, by legacy SSE's forms; at x86-64-v2
    // with SSE4.1's pmulld, at the baseline without it. Generated by a script, kept as text.
    {
        .name = "16-byte vectors' lanes by legacy SSE's forms at x86-64-v2 (#141's SSE targets)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=32\n"
                "  %6 = load.v4i32 %5 align=1\n"
                "  %7 = address.ptr %0 offset=48\n"
                "  %8 = load.v4i32 %7 align=1\n"
                "  %9 = vconst.v4i32 0x00000000000000000000000000000000\n"
                "  %10 = vconst.v4i32 0xffffffffffffffffffffffffffffffff\n"
                "  %11 = vconst.v4i32 0x12345679123456791234567912345679\n"
                "  %12 = vconst.v4i32 0xdaa66d2b3c6ef3729e3779b900000000\n"
                "  %13 = xor.v4i32 %2, %12\n"
                "  %14 = add.v4i32 %13, %11\n"
                "  %15 = add.v16i8 %14, %4\n"
                "  %16 = xor.v4i32 %14, %15\n"
                "  %17 = sub.v16i8 %15, %16\n"
                "  %18 = xor.v4i32 %16, %17\n"
                "  %19 = add.v8i16 %2, %6\n"
                "  %20 = xor.v4i32 %18, %19\n"
                "  %21 = sub.v8i16 %20, %19\n"
                "  %22 = xor.v4i32 %20, %21\n"
                "  %23 = mul.v8i16 %21, %22\n"
                "  %24 = xor.v4i32 %22, %23\n"
                "  %25 = add.v4i32 %2, %6\n"
                "  %26 = xor.v4i32 %24, %25\n"
                "  %27 = sub.v4i32 %26, %25\n"
                "  %28 = xor.v4i32 %26, %27\n"
                "  %29 = add.v2i64 %27, %28\n"
                "  %30 = xor.v4i32 %28, %29\n"
                "  %31 = sub.v2i64 %2, %6\n"
                "  %32 = xor.v4i32 %30, %31\n"
                "  %33 = and.v4i32 %32, %31\n"
                "  %34 = xor.v4i32 %32, %33\n"
                "  %35 = or.v4i32 %33, %34\n"
                "  %36 = xor.v4i32 %34, %35\n"
                "  %37 = xor.v4i32 %2, %6\n"
                "  %38 = xor.v4i32 %36, %37\n"
                "  %39 = mul.v4i32 %38, %37\n"
                "  %40 = xor.v4i32 %38, %39\n"
                "  %41 = fadd.v4f32 %6, %8\n"
                "  %42 = fadd.v2f64 %6, %8\n"
                "  %43 = fsub.v4f32 %8, %6\n"
                "  %44 = fsub.v2f64 %8, %6\n"
                "  %45 = fmul.v4f32 %6, %8\n"
                "  %46 = fmul.v2f64 %6, %8\n"
                "  %47 = fdiv.v4f32 %6, %8\n"
                "  %48 = fdiv.v2f64 %6, %8\n"
                "  %49 = fsub.v4f32 %41, %42\n"
                "  %50 = fdiv.v2f64 %49, %41\n"
                "  %51 = xor.v4i32 %40, %41\n"
                "  %52 = xor.v4i32 %51, %42\n"
                "  %53 = xor.v4i32 %52, %43\n"
                "  %54 = xor.v4i32 %53, %44\n"
                "  %55 = xor.v4i32 %54, %45\n"
                "  %56 = xor.v4i32 %55, %46\n"
                "  %57 = xor.v4i32 %56, %47\n"
                "  %58 = xor.v4i32 %57, %48\n"
                "  %59 = xor.v4i32 %58, %49\n"
                "  %60 = xor.v4i32 %59, %50\n"
                "  %61 = or.v4i32 %60, %9\n"
                "  %62 = and.v4i32 %61, %10\n"
                "  %63 = address.ptr %0 offset=128\n"
                "  %64 = store.void %63, %62 align=1\n"
                "  %65 = address.ptr %0 offset=144\n"
                "  %66 = store.void %65, %39 align=1\n"
                "  %67 = ret.void\n"
                "}\n",
        .v2 = true,
    },
    {
        .name = "16-byte vectors' lanes by legacy SSE's forms at the x86-64 baseline (#141's SSE targets)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=32\n"
                "  %6 = load.v4i32 %5 align=1\n"
                "  %7 = address.ptr %0 offset=48\n"
                "  %8 = load.v4i32 %7 align=1\n"
                "  %9 = vconst.v4i32 0x00000000000000000000000000000000\n"
                "  %10 = vconst.v4i32 0xffffffffffffffffffffffffffffffff\n"
                "  %11 = vconst.v4i32 0x12345679123456791234567912345679\n"
                "  %12 = vconst.v4i32 0xdaa66d2b3c6ef3729e3779b900000000\n"
                "  %13 = xor.v4i32 %2, %12\n"
                "  %14 = add.v4i32 %13, %11\n"
                "  %15 = add.v16i8 %14, %4\n"
                "  %16 = xor.v4i32 %14, %15\n"
                "  %17 = sub.v16i8 %15, %16\n"
                "  %18 = xor.v4i32 %16, %17\n"
                "  %19 = add.v8i16 %2, %6\n"
                "  %20 = xor.v4i32 %18, %19\n"
                "  %21 = sub.v8i16 %20, %19\n"
                "  %22 = xor.v4i32 %20, %21\n"
                "  %23 = mul.v8i16 %21, %22\n"
                "  %24 = xor.v4i32 %22, %23\n"
                "  %25 = add.v4i32 %2, %6\n"
                "  %26 = xor.v4i32 %24, %25\n"
                "  %27 = sub.v4i32 %26, %25\n"
                "  %28 = xor.v4i32 %26, %27\n"
                "  %29 = add.v2i64 %27, %28\n"
                "  %30 = xor.v4i32 %28, %29\n"
                "  %31 = sub.v2i64 %2, %6\n"
                "  %32 = xor.v4i32 %30, %31\n"
                "  %33 = and.v4i32 %32, %31\n"
                "  %34 = xor.v4i32 %32, %33\n"
                "  %35 = or.v4i32 %33, %34\n"
                "  %36 = xor.v4i32 %34, %35\n"
                "  %37 = xor.v4i32 %2, %6\n"
                "  %38 = xor.v4i32 %36, %37\n"
                "  %39 = fadd.v4f32 %6, %8\n"
                "  %40 = fadd.v2f64 %6, %8\n"
                "  %41 = fsub.v4f32 %8, %6\n"
                "  %42 = fsub.v2f64 %8, %6\n"
                "  %43 = fmul.v4f32 %6, %8\n"
                "  %44 = fmul.v2f64 %6, %8\n"
                "  %45 = fdiv.v4f32 %6, %8\n"
                "  %46 = fdiv.v2f64 %6, %8\n"
                "  %47 = fsub.v4f32 %39, %40\n"
                "  %48 = fdiv.v2f64 %47, %39\n"
                "  %49 = xor.v4i32 %38, %39\n"
                "  %50 = xor.v4i32 %49, %40\n"
                "  %51 = xor.v4i32 %50, %41\n"
                "  %52 = xor.v4i32 %51, %42\n"
                "  %53 = xor.v4i32 %52, %43\n"
                "  %54 = xor.v4i32 %53, %44\n"
                "  %55 = xor.v4i32 %54, %45\n"
                "  %56 = xor.v4i32 %55, %46\n"
                "  %57 = xor.v4i32 %56, %47\n"
                "  %58 = xor.v4i32 %57, %48\n"
                "  %59 = or.v4i32 %58, %9\n"
                "  %60 = and.v4i32 %59, %10\n"
                "  %61 = address.ptr %0 offset=128\n"
                "  %62 = store.void %61, %60 align=1\n"
                "  %63 = address.ptr %0 offset=144\n"
                "  %64 = store.void %63, %37 align=1\n"
                "  %65 = ret.void\n"
                "}\n",
        .baseline = true,
    },
    {
        // A complement into another register (%2 stays live) and in place (%6 dies), #1425's review.
        .name = "a 16-byte vector's complement at the x86-64 baseline, apart and in place (#1425's review)",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = not.v4i32 %2\n"
                "  %6 = add.v4i32 %5, %4\n"
                "  %7 = not.v4i32 %6\n"
                "  %8 = xor.v4i32 %7, %2\n"
                "  %9 = address.ptr %0 offset=128\n"
                "  %10 = store.void %9, %8 align=1\n"
                "  %11 = address.ptr %0 offset=144\n"
                "  %12 = store.void %11, %5 align=1\n"
                "  %13 = ret.void\n"
                "}\n",
        .baseline = true,
    },
    {
        .name = "vector constants: zeros, every bit, a splat and others",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v4i32 %1 align=1\n"
                "  %3 = vconst.v4i32 0x00000000000000000000000000000000\n"
                "  %4 = vconst.v4i32 0xffffffffffffffffffffffffffffffff\n"
                "  %5 = vconst.v4i32 0x12345679123456791234567912345679\n"
                "  %6 = vconst.v4i32 0xdaa66d2b3c6ef3729e3779b900000000\n"
                "  %7 = vconst.v4i32 0x00000000fffffffffffffffefffffffd\n"
                "  %8 = add.v4i32 %2, %5\n"
                "  %9 = xor.v4i32 %8, %6\n"
                "  %10 = sub.v4i32 %9, %7\n"
                "  %11 = vcmp.m8 %10, %3 ne\n"
                "  %12 = vselect.v4i32 %11, %10, %4\n"
                "  %13 = undef.v4i32\n"
                "  %14 = or.v4i32 %12, %3\n"
                "  %15 = address.ptr %0 offset=128\n"
                "  %16 = store.void %15, %14 align=1\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v8i32 %17 align=1\n"
                "  %19 = vconst.v8i32 0x0000000000000000000000000000000000000000000000000000000000000000\n"
                "  %20 = vconst.v8i32 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
                "  %21 = vconst.v8i32 0x1234567a1234567a1234567a1234567a1234567a1234567a1234567a1234567a\n"
                "  %22 = vconst.v8i32 0x53845410b54cda571715609e78dde6e5daa66d2c3c6ef3739e3779ba00000001\n"
                "  %23 = vconst.v8i32 0x0000000400000003000000020000000100000000fffffffffffffffefffffffd\n"
                "  %24 = add.v8i32 %18, %21\n"
                "  %25 = xor.v8i32 %24, %22\n"
                "  %26 = sub.v8i32 %25, %23\n"
                "  %27 = vcmp.m8 %26, %19 ne\n"
                "  %28 = vselect.v8i32 %27, %26, %20\n"
                "  %29 = undef.v8i32\n"
                "  %30 = or.v8i32 %28, %19\n"
                "  %31 = address.ptr %0 offset=144\n"
                "  %32 = store.void %31, %30 align=1\n"
                "  %33 = address.ptr %0 offset=0\n"
                "  %34 = load.v16i32 %33 align=1\n"
                "  %35 = vconst.v16i32 0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\n"
                "  %36 = vconst.v16i32 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
                "  %37 = vconst.v16i32 0x1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b1234567b\n"
                "  %38 = vconst.v16i32 0x454021d9a708a82008d12e676a99b4aecc623af52e2ac13c8ff34783f1bbcdca53845411b54cda581715609f78dde6e6daa66d2d3c6ef3749e3779bb00000002\n"
                "  %39 = vconst.v16i32 0x0000000c0000000b0000000a00000009000000080000000700000006000000050000000400000003000000020000000100000000fffffffffffffffefffffffd\n"
                "  %40 = add.v16i32 %34, %37\n"
                "  %41 = xor.v16i32 %40, %38\n"
                "  %42 = sub.v16i32 %41, %39\n"
                "  %43 = vcmp.m16 %42, %35 ne\n"
                "  %44 = vselect.v16i32 %43, %42, %36\n"
                "  %45 = undef.v16i32\n"
                "  %46 = or.v16i32 %44, %35\n"
                "  %47 = address.ptr %0 offset=176\n"
                "  %48 = store.void %47, %46 align=1\n"
                "  %49 = ret.void\n"
                "}\n",
    },
    {
        .name = "a loop whose vector phis swap",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.v8i32 %0 align=1\n"
                "  %3 = address.ptr %0 offset=32\n"
                "  %4 = load.v8i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=64\n"
                "  %6 = load.v16i32 %5 align=1\n"
                "  %7 = const.i64 0x00000000000000000000000000000000\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.v8i32 [%2, b0], [%10, b1]\n"
                "  %10 = phi.v8i32 [%4, b0], [%9, b1]\n"
                "  %11 = phi.v16i32 [%6, b0], [%15, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%17, b1]\n"
                "  %13 = address.ptr %0 + %12 * 4 offset=0\n"
                "  %14 = load.v16i32 %13 align=4\n"
                "  %15 = add.v16i32 %11, %14\n"
                "  %16 = const.i64 0x00000000000000000000000000000001\n"
                "  %17 = add.i64 %12, %16\n"
                "  %18 = icmp.i1 %17, %1 ult\n"
                "  %19 = cbr.void %18, b1, b2\n"
                "b2:\n"
                "  %20 = address.ptr %0 offset=128\n"
                "  %21 = store.void %20, %9 align=1\n"
                "  %22 = address.ptr %0 offset=160\n"
                "  %23 = store.void %22, %10 align=1\n"
                "  %24 = address.ptr %0 offset=192\n"
                "  %25 = store.void %24, %15 align=1\n"
                "  %26 = ret.void\n"
                "}\n",
        .count = 16,
        .vector_cycle = true,
    },
    {
        .name = "more vectors than xmm0 to xmm14",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v16i32 %1 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.v16i32 %3 align=4\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.v16i32 %5 align=4\n"
                "  %7 = address.ptr %0 offset=12\n"
                "  %8 = load.v16i32 %7 align=4\n"
                "  %9 = address.ptr %0 offset=16\n"
                "  %10 = load.v16i32 %9 align=4\n"
                "  %11 = address.ptr %0 offset=20\n"
                "  %12 = load.v16i32 %11 align=4\n"
                "  %13 = address.ptr %0 offset=24\n"
                "  %14 = load.v16i32 %13 align=4\n"
                "  %15 = address.ptr %0 offset=28\n"
                "  %16 = load.v16i32 %15 align=4\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v16i32 %17 align=4\n"
                "  %19 = address.ptr %0 offset=36\n"
                "  %20 = load.v16i32 %19 align=4\n"
                "  %21 = address.ptr %0 offset=40\n"
                "  %22 = load.v16i32 %21 align=4\n"
                "  %23 = address.ptr %0 offset=44\n"
                "  %24 = load.v16i32 %23 align=4\n"
                "  %25 = address.ptr %0 offset=48\n"
                "  %26 = load.v16i32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=52\n"
                "  %28 = load.v16i32 %27 align=4\n"
                "  %29 = address.ptr %0 offset=56\n"
                "  %30 = load.v16i32 %29 align=4\n"
                "  %31 = address.ptr %0 offset=60\n"
                "  %32 = load.v16i32 %31 align=4\n"
                "  %33 = address.ptr %0 offset=0\n"
                "  %34 = load.v16i32 %33 align=4\n"
                "  %35 = address.ptr %0 offset=4\n"
                "  %36 = load.v16i32 %35 align=4\n"
                "  %37 = xor.v16i32 %2, %34\n"
                "  %38 = sub.v16i32 %37, %32\n"
                "  %39 = mul.v16i32 %38, %30\n"
                "  %40 = add.v16i32 %39, %28\n"
                "  %41 = xor.v16i32 %40, %26\n"
                "  %42 = sub.v16i32 %41, %24\n"
                "  %43 = mul.v16i32 %42, %22\n"
                "  %44 = add.v16i32 %43, %20\n"
                "  %45 = xor.v16i32 %44, %18\n"
                "  %46 = sub.v16i32 %45, %16\n"
                "  %47 = mul.v16i32 %46, %14\n"
                "  %48 = add.v16i32 %47, %12\n"
                "  %49 = xor.v16i32 %48, %10\n"
                "  %50 = sub.v16i32 %49, %8\n"
                "  %51 = mul.v16i32 %50, %6\n"
                "  %52 = add.v16i32 %51, %4\n"
                "  %53 = xor.v16i32 %52, %2\n"
                "  %54 = xor.v16i32 %36, %34\n"
                "  %55 = add.v16i32 %54, %32\n"
                "  %56 = xor.v16i32 %55, %30\n"
                "  %57 = add.v16i32 %56, %28\n"
                "  %58 = xor.v16i32 %57, %26\n"
                "  %59 = add.v16i32 %58, %24\n"
                "  %60 = xor.v16i32 %59, %22\n"
                "  %61 = add.v16i32 %60, %20\n"
                "  %62 = xor.v16i32 %61, %18\n"
                "  %63 = add.v16i32 %62, %16\n"
                "  %64 = xor.v16i32 %63, %14\n"
                "  %65 = add.v16i32 %64, %12\n"
                "  %66 = xor.v16i32 %65, %10\n"
                "  %67 = add.v16i32 %66, %8\n"
                "  %68 = xor.v16i32 %67, %6\n"
                "  %69 = add.v16i32 %68, %4\n"
                "  %70 = xor.v16i32 %69, %2\n"
                "  %71 = address.ptr %0 offset=128\n"
                "  %72 = store.void %71, %53 align=1\n"
                "  %73 = address.ptr %0 offset=192\n"
                "  %74 = store.void %73, %70 align=1\n"
                "  %75 = ret.void\n"
                "}\n",
        .spills = true,
    },
    {
        .name = "vectors across calls",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.v16i32 %0 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=32\n"
                "  %6 = load.v8i32 %5 align=1\n"
                "  %7 = call.i64 @9(%1) fixed=(i64) variadic=0\n"
                "  %8 = add.v16i32 %2, %2\n"
                "  %9 = call.i64 @9(%7) fixed=(i64) variadic=0\n"
                "  %10 = not.v4i32 %4\n"
                "  %11 = xor.v8i32 %6, %6\n"
                "  %12 = sub.v8i32 %11, %6\n"
                "  %13 = address.ptr %0 offset=128\n"
                "  %14 = store.void %13, %8 align=1\n"
                "  %15 = address.ptr %0 offset=192\n"
                "  %16 = store.void %15, %10 align=1\n"
                "  %17 = address.ptr %0 offset=208\n"
                "  %18 = store.void %17, %12 align=1\n"
                "  %19 = address.ptr %0 offset=240\n"
                "  %20 = store.void %19, %9 align=8\n"
                "  %21 = ret.void\n"
                "}\n",
        .count = 1000,
        .calls = true,
    },
    {
        // #141: machine rows beside seven word masks live in k1 to k7 across them: one naming k1 and k2 by number and one
        // taking a mask scratch register, both of which borrow live masks' registers; and rows reading their result as
        // their first operand, as Tier 0 places them, and a row of no records, its result its operand.
        .name = "machine rows under opmask pressure",
        .text = "function (ptr) -> void {\n"
                "  row #0 (u32) -> u32 [KMOVW_K_R32 0x01 0x00 0x80 0x00 0x00 0x00 0 0] [KNOTW_K_K 0x02 0x00 0x01 0x00 0x00 0x00 0 0] [KMOVW_R32_K 0x90 0x00 0x02 0x00 0x00 0x00 0 0]\n"
                "  row #1 (mask2, mask2) -> mask2 [KANDW_K_K_K 0xa0 0x80 0x81 0x00 0x00 0x00 0 0] [KXORW_K_K_K 0x90 0xa0 0x90 0x00 0x00 0x00 0 0]\n"
                "  row #2 (u32, u32) -> u32 [ADD_R32_RM32 0x90 0x00 0x81 0x00 0x00 0x00 0 0]\n"
                "  row #3 (u32) -> u32\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.m16 %0 align=2\n"
                "  %2 = address.ptr %0 offset=2\n"
                "  %3 = load.m16 %2 align=2\n"
                "  %4 = address.ptr %0 offset=4\n"
                "  %5 = load.m16 %4 align=2\n"
                "  %6 = address.ptr %0 offset=6\n"
                "  %7 = load.m16 %6 align=2\n"
                "  %8 = address.ptr %0 offset=8\n"
                "  %9 = load.m16 %8 align=2\n"
                "  %10 = address.ptr %0 offset=10\n"
                "  %11 = load.m16 %10 align=2\n"
                "  %12 = address.ptr %0 offset=12\n"
                "  %13 = load.m16 %12 align=2\n"
                "  %14 = address.ptr %0 offset=16\n"
                "  %15 = load.i32 %14 align=4\n"
                "  %16 = address.ptr %0 offset=20\n"
                "  %17 = load.i32 %16 align=4\n"
                "  %18 = call.i32 row #0(%15) fixed=(i32) variadic=0\n"
                "  %19 = call.m16 row #1(%1, %3) fixed=(m16, m16) variadic=0\n"
                "  %20 = call.i32 row #2(%15, %17) fixed=(i32, i32) variadic=0\n"
                "  %21 = call.i32 row #3(%17) fixed=(i32) variadic=0\n"
                "  %22 = address.ptr %0 offset=128\n"
                "  %23 = store.void %22, %1 align=2\n"
                "  %24 = address.ptr %0 offset=130\n"
                "  %25 = store.void %24, %3 align=2\n"
                "  %26 = address.ptr %0 offset=132\n"
                "  %27 = store.void %26, %5 align=2\n"
                "  %28 = address.ptr %0 offset=134\n"
                "  %29 = store.void %28, %7 align=2\n"
                "  %30 = address.ptr %0 offset=136\n"
                "  %31 = store.void %30, %9 align=2\n"
                "  %32 = address.ptr %0 offset=138\n"
                "  %33 = store.void %32, %11 align=2\n"
                "  %34 = address.ptr %0 offset=140\n"
                "  %35 = store.void %34, %13 align=2\n"
                "  %36 = address.ptr %0 offset=160\n"
                "  %37 = store.void %36, %18 align=4\n"
                "  %38 = address.ptr %0 offset=164\n"
                "  %39 = store.void %38, %19 align=2\n"
                "  %40 = address.ptr %0 offset=168\n"
                "  %41 = store.void %40, %20 align=4\n"
                "  %42 = address.ptr %0 offset=172\n"
                "  %43 = store.void %42, %21 align=4\n"
                "  %44 = ret.void\n"
                "}\n",
        .borrows = true,
        .model = tier1_masks_test_rows_model,
    },
    {
        // #141: a floating row of an EVEX form, its floating roles in zmm16 and on, its result read as its first operand.
        .name = "a floating machine row",
        .text = "function (ptr) -> void {\n"
                "  row #0 (f32, f32) -> f32 [VPADDD_X_X_XM128_EVEX 0xb0 0xb0 0xa1 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.f32 %0 align=4\n"
                "  %2 = address.ptr %0 offset=4\n"
                "  %3 = load.f32 %2 align=4\n"
                "  %4 = call.f32 row #0(%1, %3) fixed=(f32, f32) variadic=0\n"
                "  %5 = address.ptr %0 offset=128\n"
                "  %6 = store.void %5, %4 align=4\n"
                "  %7 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_floating_model,
    },
    {
        // #1281: its VEX twin, whose floating roles only registers below 16 can be: its result in its destination, read
        // as its first operand.
        .name = "a floating machine row of a VEX form",
        .text = "function (ptr) -> void {\n"
                "  row #0 (f32, f32) -> f32 [VPADDD_X_X_XM128 0xb0 0xb0 0xa1 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.f32 %0 align=4\n"
                "  %2 = address.ptr %0 offset=4\n"
                "  %3 = load.f32 %2 align=4\n"
                "  %4 = call.f32 row #0(%1, %3) fixed=(f32, f32) variadic=0\n"
                "  %5 = address.ptr %0 offset=128\n"
                "  %6 = store.void %5, %4 align=4\n"
                "  %7 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_floating_model,
    },
    {
        // #1281: the same at a target without AVX-512, its copies movaps.
        .name = "a floating machine row of a VEX form at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "  row #0 (f32, f32) -> f32 [VPADDD_X_X_XM128 0xb0 0xb0 0xa1 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.f32 %0 align=4\n"
                "  %2 = address.ptr %0 offset=4\n"
                "  %3 = load.f32 %2 align=4\n"
                "  %4 = call.f32 row #0(%1, %3) fixed=(f32, f32) variadic=0\n"
                "  %5 = address.ptr %0 offset=128\n"
                "  %6 = store.void %5, %4 align=4\n"
                "  %7 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_floating_model,
        .v3 = true,
    },
    {
        // #1281: machine rows at a target without AVX-512, their vector roles in free registers below 16 and
        // their floating ones below 16 too: a VEX row of 32-byte vectors, x live past it; a floating row whose result is
        // read as its first operand, and one that writes its second floating operand, a and b live past both; and a
        // 16-byte vector's row. Its vectors move, spill and compute by VEX forms.
        .name = "machine rows at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector32, vector32) -> vector32 [VPADDB_Y_Y_YM256 0x80 0x80 0x81 0x00 0x00 0x00 0 0]\n"
                "  row #1 (f32, f32) -> f32 [VPADDD_X_X_XM128 0xb0 0xb0 0xa1 0x00 0x00 0x00 0 0]\n"
                "  row #2 (f32, f32) -> f32 [VPADDD_X_X_XM128 0xa1 0xa1 0xa0 0x00 0x00 0x00 0 0] [VPXOR_X_X_XM128 0xb0 0xa1 0xb0 0x00 0x00 0x00 0 0]\n"
                "  row #3 (vector16) -> vector16 [VPSHUFD_X_XM128_IMM8 0x80 0x00 0x80 0x00 0x00 0x00 27 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v8i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=32\n"
                "  %3 = load.v8i32 %2 align=1\n"
                "  %4 = call.v8i32 row #0(%1, %3) fixed=(v8i32, v8i32) variadic=0\n"
                "  %5 = add.v8i32 %4, %1\n"
                "  %6 = address.ptr %0 offset=96\n"
                "  %7 = load.f32 %6 align=4\n"
                "  %8 = address.ptr %0 offset=100\n"
                "  %9 = load.f32 %8 align=4\n"
                "  %10 = call.f32 row #1(%7, %9) fixed=(f32, f32) variadic=0\n"
                "  %11 = call.f32 row #2(%7, %9) fixed=(f32, f32) variadic=0\n"
                "  %12 = address.ptr %0 offset=64\n"
                "  %13 = load.v4i32 %12 align=1\n"
                "  %14 = call.v4i32 row #3(%13) fixed=(v4i32) variadic=0\n"
                "  %15 = address.ptr %0 offset=128\n"
                "  %16 = store.void %15, %5 align=1\n"
                "  %17 = address.ptr %0 offset=160\n"
                "  %18 = store.void %17, %1 align=1\n"
                "  %19 = address.ptr %0 offset=192\n"
                "  %20 = store.void %19, %10 align=4\n"
                "  %21 = address.ptr %0 offset=196\n"
                "  %22 = store.void %21, %11 align=4\n"
                "  %23 = address.ptr %0 offset=200\n"
                "  %24 = store.void %23, %7 align=4\n"
                "  %25 = address.ptr %0 offset=204\n"
                "  %26 = store.void %25, %9 align=4\n"
                "  %27 = address.ptr %0 offset=224\n"
                "  %28 = store.void %27, %14 align=1\n"
                "  %29 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_vex_rows_model,
        .v3 = true,
    },
    // #1281: the IR's vectors at x86-64-v3, a target without AVX-512, by VEX forms: v4i32's and v8i32's operations,
    // constants (zeros by vpxor, every bit by vpcmpeqd, a splat by vmovd and vpshufd or vpbroadcastd, others loaded) and
    // copies; their phis' swap through xmm15; their spills past xmm14; and their saves around calls.
    {
        .name = "vector operations and constants at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v4i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=16\n"
                "  %3 = load.v4i32 %2 align=1\n"
                "  %4 = add.v4i32 %1, %3\n"
                "  %5 = sub.v4i32 %4, %3\n"
                "  %6 = mul.v4i32 %5, %3\n"
                "  %7 = and.v4i32 %6, %1\n"
                "  %8 = or.v4i32 %7, %3\n"
                "  %9 = xor.v4i32 %8, %4\n"
                "  %10 = copy.v4i32 %9\n"
                "  %11 = vconst.v4i32 0x00000000000000000000000000000000\n"
                "  %12 = vconst.v4i32 0xffffffffffffffffffffffffffffffff\n"
                "  %13 = vconst.v4i32 0x12345679123456791234567912345679\n"
                "  %14 = vconst.v4i32 0xdaa66d2b3c6ef3729e3779b900000000\n"
                "  %15 = add.v4i32 %10, %13\n"
                "  %16 = xor.v4i32 %15, %14\n"
                "  %17 = sub.v4i32 %16, %12\n"
                "  %18 = or.v4i32 %17, %11\n"
                "  %19 = undef.v4i32\n"
                "  %20 = address.ptr %0 offset=128\n"
                "  %21 = store.void %20, %18 align=1\n"
                "  %22 = address.ptr %0 offset=32\n"
                "  %23 = load.v8i32 %22 align=1\n"
                "  %24 = address.ptr %0 offset=64\n"
                "  %25 = load.v8i32 %24 align=1\n"
                "  %26 = add.v8i32 %23, %25\n"
                "  %27 = sub.v8i32 %26, %25\n"
                "  %28 = mul.v8i32 %27, %25\n"
                "  %29 = and.v8i32 %28, %23\n"
                "  %30 = or.v8i32 %29, %25\n"
                "  %31 = xor.v8i32 %30, %26\n"
                "  %32 = copy.v8i32 %31\n"
                "  %33 = vconst.v8i32 0x0000000000000000000000000000000000000000000000000000000000000000\n"
                "  %34 = vconst.v8i32 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
                "  %35 = vconst.v8i32 0x1234567a1234567a1234567a1234567a1234567a1234567a1234567a1234567a\n"
                "  %36 = vconst.v8i32 0x53845410b54cda571715609e78dde6e5daa66d2c3c6ef3739e3779ba00000001\n"
                "  %37 = add.v8i32 %32, %35\n"
                "  %38 = xor.v8i32 %37, %36\n"
                "  %39 = sub.v8i32 %38, %34\n"
                "  %40 = or.v8i32 %39, %33\n"
                "  %41 = undef.v8i32\n"
                "  %42 = address.ptr %0 offset=144\n"
                "  %43 = store.void %42, %40 align=1\n"
                "  %44 = address.ptr %0 offset=176\n"
                "  %45 = store.void %44, %34 align=1\n"
                "  %46 = address.ptr %0 offset=208\n"
                "  %47 = store.void %46, %35 align=1\n"
                "  %48 = address.ptr %0 offset=240\n"
                "  %49 = store.void %48, %13 align=1\n"
                "  %50 = ret.void\n"
                "}\n",
        .v3 = true,
    },
    {
        .name = "a loop whose vector phis swap at x86-64-v3",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.v8i32 %0 align=1\n"
                "  %3 = address.ptr %0 offset=32\n"
                "  %4 = load.v8i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=64\n"
                "  %6 = load.v4i32 %5 align=1\n"
                "  %7 = const.i64 0x00000000000000000000000000000000\n"
                "  %8 = br.void b1\n"
                "b1:\n"
                "  %9 = phi.v8i32 [%2, b0], [%10, b1]\n"
                "  %10 = phi.v8i32 [%4, b0], [%9, b1]\n"
                "  %11 = phi.v4i32 [%6, b0], [%15, b1]\n"
                "  %12 = phi.i64 [%7, b0], [%17, b1]\n"
                "  %13 = address.ptr %0 + %12 * 4 offset=0\n"
                "  %14 = load.v4i32 %13 align=4\n"
                "  %15 = add.v4i32 %11, %14\n"
                "  %16 = const.i64 0x00000000000000000000000000000001\n"
                "  %17 = add.i64 %12, %16\n"
                "  %18 = icmp.i1 %17, %1 ult\n"
                "  %19 = cbr.void %18, b1, b2\n"
                "b2:\n"
                "  %20 = address.ptr %0 offset=128\n"
                "  %21 = store.void %20, %9 align=1\n"
                "  %22 = address.ptr %0 offset=160\n"
                "  %23 = store.void %22, %10 align=1\n"
                "  %24 = address.ptr %0 offset=192\n"
                "  %25 = store.void %24, %15 align=1\n"
                "  %26 = ret.void\n"
                "}\n",
        .count = 16,
        .vector_cycle = true,
        .v3 = true,
    },
    {
        .name = "more vectors than xmm0 to xmm14 at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v8i32 %1 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.v8i32 %3 align=4\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.v8i32 %5 align=4\n"
                "  %7 = address.ptr %0 offset=12\n"
                "  %8 = load.v8i32 %7 align=4\n"
                "  %9 = address.ptr %0 offset=16\n"
                "  %10 = load.v8i32 %9 align=4\n"
                "  %11 = address.ptr %0 offset=20\n"
                "  %12 = load.v8i32 %11 align=4\n"
                "  %13 = address.ptr %0 offset=24\n"
                "  %14 = load.v8i32 %13 align=4\n"
                "  %15 = address.ptr %0 offset=28\n"
                "  %16 = load.v8i32 %15 align=4\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v8i32 %17 align=4\n"
                "  %19 = address.ptr %0 offset=36\n"
                "  %20 = load.v8i32 %19 align=4\n"
                "  %21 = address.ptr %0 offset=40\n"
                "  %22 = load.v8i32 %21 align=4\n"
                "  %23 = address.ptr %0 offset=44\n"
                "  %24 = load.v8i32 %23 align=4\n"
                "  %25 = address.ptr %0 offset=48\n"
                "  %26 = load.v8i32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=52\n"
                "  %28 = load.v8i32 %27 align=4\n"
                "  %29 = address.ptr %0 offset=56\n"
                "  %30 = load.v8i32 %29 align=4\n"
                "  %31 = address.ptr %0 offset=60\n"
                "  %32 = load.v8i32 %31 align=4\n"
                "  %33 = address.ptr %0 offset=64\n"
                "  %34 = load.v8i32 %33 align=4\n"
                "  %35 = xor.v8i32 %2, %34\n"
                "  %36 = sub.v8i32 %35, %32\n"
                "  %37 = mul.v8i32 %36, %30\n"
                "  %38 = add.v8i32 %37, %28\n"
                "  %39 = xor.v8i32 %38, %26\n"
                "  %40 = sub.v8i32 %39, %24\n"
                "  %41 = mul.v8i32 %40, %22\n"
                "  %42 = add.v8i32 %41, %20\n"
                "  %43 = xor.v8i32 %42, %18\n"
                "  %44 = sub.v8i32 %43, %16\n"
                "  %45 = mul.v8i32 %44, %14\n"
                "  %46 = add.v8i32 %45, %12\n"
                "  %47 = xor.v8i32 %46, %10\n"
                "  %48 = sub.v8i32 %47, %8\n"
                "  %49 = mul.v8i32 %48, %6\n"
                "  %50 = add.v8i32 %49, %4\n"
                "  %51 = xor.v8i32 %34, %32\n"
                "  %52 = add.v8i32 %51, %30\n"
                "  %53 = xor.v8i32 %52, %28\n"
                "  %54 = add.v8i32 %53, %26\n"
                "  %55 = xor.v8i32 %54, %24\n"
                "  %56 = add.v8i32 %55, %22\n"
                "  %57 = xor.v8i32 %56, %20\n"
                "  %58 = add.v8i32 %57, %18\n"
                "  %59 = xor.v8i32 %58, %16\n"
                "  %60 = add.v8i32 %59, %14\n"
                "  %61 = xor.v8i32 %60, %12\n"
                "  %62 = add.v8i32 %61, %10\n"
                "  %63 = xor.v8i32 %62, %8\n"
                "  %64 = add.v8i32 %63, %6\n"
                "  %65 = xor.v8i32 %64, %4\n"
                "  %66 = add.v8i32 %65, %2\n"
                "  %67 = address.ptr %0 offset=128\n"
                "  %68 = store.void %67, %50 align=1\n"
                "  %69 = address.ptr %0 offset=192\n"
                "  %70 = store.void %69, %66 align=1\n"
                "  %71 = ret.void\n"
                "}\n",
        .spills = true,
        .v3 = true,
    },
    {
        // #1281's review: 32-byte vectors at a target with AVX but not AVX2, whose ymm integer forms are AVX2's: their
        // bitwise operations by vandps, vorps and vxorps, every bit by vpcmpeqd of the xmm register and vinsertf128, a
        // splat by vmovd, vpshufd and vinsertf128, zeros by vpxor of the xmm register, others loaded, and copies, loads
        // and stores by vmovdqa and vmovdqu; and 16-byte vectors' AVX operations beside them. Its native runs start with
        // a signaling NaN in every ymm register and MXCSR's flags clear, which must stay clear: no record reads a
        // register's old bits as floats (#1293's review).
        .name = "vectors at a target with AVX but not AVX2",
        .text = "function (ptr) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v8i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=32\n"
                "  %3 = load.v8i32 %2 align=1\n"
                "  %4 = and.v8i32 %1, %3\n"
                "  %5 = or.v8i32 %4, %1\n"
                "  %6 = xor.v8i32 %5, %3\n"
                "  %7 = copy.v8i32 %6\n"
                "  %8 = vconst.v8i32 0x0000000000000000000000000000000000000000000000000000000000000000\n"
                "  %9 = vconst.v8i32 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
                "  %10 = vconst.v8i32 0x1234567a1234567a1234567a1234567a1234567a1234567a1234567a1234567a\n"
                "  %11 = vconst.v8i32 0x53845410b54cda571715609e78dde6e5daa66d2c3c6ef3739e3779ba00000001\n"
                "  %12 = xor.v8i32 %7, %10\n"
                "  %13 = and.v8i32 %12, %11\n"
                "  %14 = or.v8i32 %13, %8\n"
                "  %15 = xor.v8i32 %14, %9\n"
                "  %16 = address.ptr %0 offset=128\n"
                "  %17 = store.void %16, %15 align=1\n"
                "  %18 = address.ptr %0 offset=160\n"
                "  %19 = store.void %18, %9 align=1\n"
                "  %20 = address.ptr %0 offset=192\n"
                "  %21 = store.void %20, %10 align=1\n"
                "  %22 = address.ptr %0 offset=64\n"
                "  %23 = load.v4i32 %22 align=1\n"
                "  %24 = address.ptr %0 offset=80\n"
                "  %25 = load.v4i32 %24 align=1\n"
                "  %26 = mul.v4i32 %23, %25\n"
                "  %27 = add.v4i32 %26, %23\n"
                "  %28 = sub.v4i32 %27, %25\n"
                "  %29 = vconst.v4i32 0x12345679123456791234567912345679\n"
                "  %30 = vconst.v4i32 0xffffffffffffffffffffffffffffffff\n"
                "  %31 = xor.v4i32 %28, %29\n"
                "  %32 = and.v4i32 %31, %30\n"
                "  %33 = address.ptr %0 offset=224\n"
                "  %34 = store.void %33, %32 align=1\n"
                "  %35 = ret.void\n"
                "}\n",
        .avx = true,
    },
    {
        // #1281's review: a 64-byte EVEX row at x86-64-v3 with AVX512F but not VL (Knights Landing's AVX-512), its two
        // operands dying at it and 13 vectors live across it, so that xmm0 to xmm14 are taken and only xmm15 is free
        // below 16: its roles stay in place or take zmm16, rather than a run of two free registers below 16, which isn't
        // there.
        .name = "a 64-byte row under register pressure at AVX512F without VL",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector64, vector64) -> vector64 [VPADDD_Z_Z_ZM512 0x81 0x81 0x80 0x00 0x00 0x00 0 0] [VPXORD_Z_Z_ZM512 0x80 0x80 0x81 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v16i32 %1 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.v16i32 %3 align=4\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.v16i32 %5 align=4\n"
                "  %7 = address.ptr %0 offset=12\n"
                "  %8 = load.v16i32 %7 align=4\n"
                "  %9 = address.ptr %0 offset=16\n"
                "  %10 = load.v16i32 %9 align=4\n"
                "  %11 = address.ptr %0 offset=20\n"
                "  %12 = load.v16i32 %11 align=4\n"
                "  %13 = address.ptr %0 offset=24\n"
                "  %14 = load.v16i32 %13 align=4\n"
                "  %15 = address.ptr %0 offset=28\n"
                "  %16 = load.v16i32 %15 align=4\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v16i32 %17 align=4\n"
                "  %19 = address.ptr %0 offset=36\n"
                "  %20 = load.v16i32 %19 align=4\n"
                "  %21 = address.ptr %0 offset=40\n"
                "  %22 = load.v16i32 %21 align=4\n"
                "  %23 = address.ptr %0 offset=44\n"
                "  %24 = load.v16i32 %23 align=4\n"
                "  %25 = address.ptr %0 offset=48\n"
                "  %26 = load.v16i32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=52\n"
                "  %28 = load.v16i32 %27 align=4\n"
                "  %29 = address.ptr %0 offset=56\n"
                "  %30 = load.v16i32 %29 align=4\n"
                "  %31 = call.v16i32 row #0(%2, %4) fixed=(v16i32, v16i32) variadic=0\n"
                "  %32 = xor.v16i32 %6, %8\n"
                "  %33 = xor.v16i32 %32, %10\n"
                "  %34 = xor.v16i32 %33, %12\n"
                "  %35 = xor.v16i32 %34, %14\n"
                "  %36 = xor.v16i32 %35, %16\n"
                "  %37 = xor.v16i32 %36, %18\n"
                "  %38 = xor.v16i32 %37, %20\n"
                "  %39 = xor.v16i32 %38, %22\n"
                "  %40 = xor.v16i32 %39, %24\n"
                "  %41 = xor.v16i32 %40, %26\n"
                "  %42 = xor.v16i32 %41, %28\n"
                "  %43 = xor.v16i32 %42, %30\n"
                "  %44 = address.ptr %0 offset=128\n"
                "  %45 = store.void %44, %43 align=1\n"
                "  %46 = address.ptr %0 offset=192\n"
                "  %47 = store.void %46, %31 align=1\n"
                "  %48 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_pressure_model64,
        .foundation = true,
    },
    {
        // #1281's review: its VEX twin at x86-64-v3, 13 32-byte vectors live across it beside its two dying operands: its
        // roles stay in place (the result in a dying operand's register), or take xmm15, the one free register below 16,
        // one at a time, not a run of two.
        .name = "a VEX row under register pressure at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector32, vector32) -> vector32 [VPADDD_Y_Y_YM256 0x81 0x81 0x80 0x00 0x00 0x00 0 0] [VPXOR_Y_Y_YM256 0x80 0x80 0x81 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v8i32 %1 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.v8i32 %3 align=4\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.v8i32 %5 align=4\n"
                "  %7 = address.ptr %0 offset=12\n"
                "  %8 = load.v8i32 %7 align=4\n"
                "  %9 = address.ptr %0 offset=16\n"
                "  %10 = load.v8i32 %9 align=4\n"
                "  %11 = address.ptr %0 offset=20\n"
                "  %12 = load.v8i32 %11 align=4\n"
                "  %13 = address.ptr %0 offset=24\n"
                "  %14 = load.v8i32 %13 align=4\n"
                "  %15 = address.ptr %0 offset=28\n"
                "  %16 = load.v8i32 %15 align=4\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v8i32 %17 align=4\n"
                "  %19 = address.ptr %0 offset=36\n"
                "  %20 = load.v8i32 %19 align=4\n"
                "  %21 = address.ptr %0 offset=40\n"
                "  %22 = load.v8i32 %21 align=4\n"
                "  %23 = address.ptr %0 offset=44\n"
                "  %24 = load.v8i32 %23 align=4\n"
                "  %25 = address.ptr %0 offset=48\n"
                "  %26 = load.v8i32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=52\n"
                "  %28 = load.v8i32 %27 align=4\n"
                "  %29 = address.ptr %0 offset=56\n"
                "  %30 = load.v8i32 %29 align=4\n"
                "  %31 = call.v8i32 row #0(%2, %4) fixed=(v8i32, v8i32) variadic=0\n"
                "  %32 = xor.v8i32 %6, %8\n"
                "  %33 = xor.v8i32 %32, %10\n"
                "  %34 = xor.v8i32 %33, %12\n"
                "  %35 = xor.v8i32 %34, %14\n"
                "  %36 = xor.v8i32 %35, %16\n"
                "  %37 = xor.v8i32 %36, %18\n"
                "  %38 = xor.v8i32 %37, %20\n"
                "  %39 = xor.v8i32 %38, %22\n"
                "  %40 = xor.v8i32 %39, %24\n"
                "  %41 = xor.v8i32 %40, %26\n"
                "  %42 = xor.v8i32 %41, %28\n"
                "  %43 = xor.v8i32 %42, %30\n"
                "  %44 = address.ptr %0 offset=128\n"
                "  %45 = store.void %44, %43 align=1\n"
                "  %46 = address.ptr %0 offset=160\n"
                "  %47 = store.void %46, %31 align=1\n"
                "  %48 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_pressure_model32,
        .v3 = true,
    },
    {
        // #1293's review: a VEX row whose result is its first operand, which no record writes, and which writes its second
        // operand, live past it, at x86-64-v3 under the same pressure: 14 vectors live across it beside its dying first
        // operand, so that only xmm15 is free below 16. VECTOR(0) stays in the first operand's register, taking none, and
        // the second operand is staged in xmm15.
        .name = "a VEX row of a read-only result under register pressure at x86-64-v3",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector32, vector32) -> vector32 [VPADDD_Y_Y_YM256 0x81 0x81 0x80 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = address.ptr %0 offset=0\n"
                "  %2 = load.v8i32 %1 align=4\n"
                "  %3 = address.ptr %0 offset=4\n"
                "  %4 = load.v8i32 %3 align=4\n"
                "  %5 = address.ptr %0 offset=8\n"
                "  %6 = load.v8i32 %5 align=4\n"
                "  %7 = address.ptr %0 offset=12\n"
                "  %8 = load.v8i32 %7 align=4\n"
                "  %9 = address.ptr %0 offset=16\n"
                "  %10 = load.v8i32 %9 align=4\n"
                "  %11 = address.ptr %0 offset=20\n"
                "  %12 = load.v8i32 %11 align=4\n"
                "  %13 = address.ptr %0 offset=24\n"
                "  %14 = load.v8i32 %13 align=4\n"
                "  %15 = address.ptr %0 offset=28\n"
                "  %16 = load.v8i32 %15 align=4\n"
                "  %17 = address.ptr %0 offset=32\n"
                "  %18 = load.v8i32 %17 align=4\n"
                "  %19 = address.ptr %0 offset=36\n"
                "  %20 = load.v8i32 %19 align=4\n"
                "  %21 = address.ptr %0 offset=40\n"
                "  %22 = load.v8i32 %21 align=4\n"
                "  %23 = address.ptr %0 offset=44\n"
                "  %24 = load.v8i32 %23 align=4\n"
                "  %25 = address.ptr %0 offset=48\n"
                "  %26 = load.v8i32 %25 align=4\n"
                "  %27 = address.ptr %0 offset=52\n"
                "  %28 = load.v8i32 %27 align=4\n"
                "  %29 = address.ptr %0 offset=56\n"
                "  %30 = load.v8i32 %29 align=4\n"
                "  %31 = call.v8i32 row #0(%2, %4) fixed=(v8i32, v8i32) variadic=0\n"
                "  %32 = xor.v8i32 %4, %6\n"
                "  %33 = xor.v8i32 %32, %8\n"
                "  %34 = xor.v8i32 %33, %10\n"
                "  %35 = xor.v8i32 %34, %12\n"
                "  %36 = xor.v8i32 %35, %14\n"
                "  %37 = xor.v8i32 %36, %16\n"
                "  %38 = xor.v8i32 %37, %18\n"
                "  %39 = xor.v8i32 %38, %20\n"
                "  %40 = xor.v8i32 %39, %22\n"
                "  %41 = xor.v8i32 %40, %24\n"
                "  %42 = xor.v8i32 %41, %26\n"
                "  %43 = xor.v8i32 %42, %28\n"
                "  %44 = xor.v8i32 %43, %30\n"
                "  %45 = address.ptr %0 offset=128\n"
                "  %46 = store.void %45, %44 align=1\n"
                "  %47 = address.ptr %0 offset=160\n"
                "  %48 = store.void %47, %31 align=1\n"
                "  %49 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_read_only_model,
        .v3 = true,
    },
    {
        .name = "vectors across calls at x86-64-v3",
        .text = "function (ptr, i64) -> void {\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = load.v8i32 %0 align=1\n"
                "  %3 = address.ptr %0 offset=16\n"
                "  %4 = load.v4i32 %3 align=1\n"
                "  %5 = address.ptr %0 offset=32\n"
                "  %6 = load.v8i32 %5 align=1\n"
                "  %7 = call.i64 @9(%1) fixed=(i64) variadic=0\n"
                "  %8 = add.v8i32 %2, %2\n"
                "  %9 = call.i64 @9(%7) fixed=(i64) variadic=0\n"
                "  %10 = mul.v4i32 %4, %4\n"
                "  %11 = xor.v8i32 %6, %6\n"
                "  %12 = sub.v8i32 %11, %6\n"
                "  %13 = address.ptr %0 offset=128\n"
                "  %14 = store.void %13, %8 align=1\n"
                "  %15 = address.ptr %0 offset=160\n"
                "  %16 = store.void %15, %10 align=1\n"
                "  %17 = address.ptr %0 offset=176\n"
                "  %18 = store.void %17, %12 align=1\n"
                "  %19 = address.ptr %0 offset=240\n"
                "  %20 = store.void %19, %9 align=8\n"
                "  %21 = ret.void\n"
                "}\n",
        .count = 1000,
        .calls = true,
        .v3 = true,
    },
    {
        // #1280: SSE carriers from a double and a float parameter and a call's double result, whose registers' bytes
        // past them aren't zero, read whole by rows of no records: each zero-extended into its role.
        .name = "SSE carriers with dirty upper lanes",
        .text = "function (ptr, i64, f64, f32) -> void {\n"
                "  row #0 (vector8) -> vector16\n"
                "  row #1 (vector4) -> vector16\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = param.f64 2\n"
                "  %3 = param.f32 3\n"
                "  %4 = call.v4i32 row #0(%2) fixed=(f64) variadic=0\n"
                "  %5 = call.v4i32 row #1(%3) fixed=(f32) variadic=0\n"
                "  %6 = call.f64 @9(%1) fixed=(i64) variadic=0\n"
                "  %7 = call.v4i32 row #0(%6) fixed=(f64) variadic=0\n"
                "  %8 = address.ptr %0 offset=128\n"
                "  %9 = store.void %8, %4 align=1\n"
                "  %10 = address.ptr %0 offset=144\n"
                "  %11 = store.void %10, %5 align=1\n"
                "  %12 = address.ptr %0 offset=160\n"
                "  %13 = store.void %12, %7 align=1\n"
                "  %14 = ret.void\n"
                "}\n",
        .count = 1000,
        .model = tier1_masks_test_carriers_model,
    },
    {
        // #1281: the same at a target without AVX-512, each zero-extended by VEX vmovq or vinsertps.
        .name = "SSE carriers with dirty upper lanes at x86-64-v3",
        .text = "function (ptr, i64, f64, f32) -> void {\n"
                "  row #0 (vector8) -> vector16\n"
                "  row #1 (vector4) -> vector16\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = param.i64 1\n"
                "  %2 = param.f64 2\n"
                "  %3 = param.f32 3\n"
                "  %4 = call.v4i32 row #0(%2) fixed=(f64) variadic=0\n"
                "  %5 = call.v4i32 row #1(%3) fixed=(f32) variadic=0\n"
                "  %6 = call.f64 @9(%1) fixed=(i64) variadic=0\n"
                "  %7 = call.v4i32 row #0(%6) fixed=(f64) variadic=0\n"
                "  %8 = address.ptr %0 offset=128\n"
                "  %9 = store.void %8, %4 align=1\n"
                "  %10 = address.ptr %0 offset=144\n"
                "  %11 = store.void %10, %5 align=1\n"
                "  %12 = address.ptr %0 offset=160\n"
                "  %13 = store.void %12, %7 align=1\n"
                "  %14 = ret.void\n"
                "}\n",
        .count = 1000,
        .model = tier1_masks_test_carriers_model,
        .v3 = true,
    },
    {
        // #1283: vector rows placed one role at a time: a two-operand form's (its first operand read in its own register
        // up to the result's first write, which the destination takes), a tied operand's (copied into the destination),
        // a merging opmask's, an operand written and dying at the node (kept in its own register) under an integer
        // result, a VEX form's /is4 blend in the run below xmm16, a narrow result written at a wider width (moved at its
        // width), a narrow operand read at a wider one (copied at its width) and a widening conversion's pure write.
        .name = "vector machine rows",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector64, vector64) -> vector64 [VPADDD_Z_Z_ZM512 0x80 0x80 0x81 0x00 0x00 0x00 0 0]\n"
                "  row #1 (vector64, vector64, vector64) -> vector64 [VPTERNLOGD_Z_Z_ZM512_IMM8 0x80 0x81 0x82 0x00 0x00 0x00 150 0]\n"
                "  row #2 (vector64) -> u32 [VPADDD_Z_Z_ZM512 0x80 0x80 0x80 0x00 0x00 0x00 0 0] [VMOVD_RM32_X_EVEX 0x80 0x00 0x90 0x00 0x00 0x00 0 0]\n"
                "  row #3 (vector32, vector32, vector32) -> vector32 [VPBLENDVB_Y_Y_YM256_Y 0x80 0x80 0x81 0x00 0x00 0x00 32 0]\n"
                "  row #4 (vector64, vector64, mask2) -> vector64 [VPADDD_Z_Z_ZM512 0x80 0x80 0x81 0x00 0x00 0x0a 0 0]\n"
                "  row #5 (vector64) -> vector16 [VPADDD_Z_Z_ZM512 0x80 0x80 0x80 0x00 0x00 0x00 0 0]\n"
                "  row #6 (vector16) -> vector64 [VPADDD_Z_Z_ZM512 0x80 0x80 0x80 0x00 0x00 0x00 0 0]\n"
                "  row #7 (vector16) -> vector64 [VPMOVZXBD_Z_XM128 0x80 0x00 0x80 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v16i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=64\n"
                "  %3 = load.v16i32 %2 align=1\n"
                "  %4 = load.m16 %0 align=2\n"
                "  %5 = call.v16i32 row #0(%1, %3) fixed=(v16i32, v16i32) variadic=0\n"
                "  %6 = call.v16i32 row #1(%5, %1, %3) fixed=(v16i32, v16i32, v16i32) variadic=0\n"
                "  %7 = call.v16i32 row #4(%1, %6, %4) fixed=(v16i32, v16i32, m16) variadic=0\n"
                "  %8 = call.v4i32 row #5(%3) fixed=(v16i32) variadic=0\n"
                "  %9 = call.v16i32 row #6(%8) fixed=(v4i32) variadic=0\n"
                "  %10 = call.v16i32 row #7(%8) fixed=(v4i32) variadic=0\n"
                "  %11 = call.v16i32 row #1(%9, %10, %7) fixed=(v16i32, v16i32, v16i32) variadic=0\n"
                "  %12 = call.i32 row #2(%7) fixed=(v16i32) variadic=0\n"
                "  %13 = address.ptr %0 offset=128\n"
                "  %14 = store.void %13, %11 align=1\n"
                "  %15 = load.v8i32 %0 align=1\n"
                "  %16 = address.ptr %0 offset=32\n"
                "  %17 = load.v8i32 %16 align=1\n"
                "  %18 = load.v8i32 %2 align=1\n"
                "  %19 = call.v8i32 row #3(%15, %17, %18) fixed=(v8i32, v8i32, v8i32) variadic=0\n"
                "  %20 = address.ptr %0 offset=192\n"
                "  %21 = store.void %20, %19 align=1\n"
                "  %22 = address.ptr %0 offset=224\n"
                "  %23 = store.void %22, %8 align=1\n"
                "  %24 = address.ptr %0 offset=240\n"
                "  %25 = store.void %24, %12 align=4\n"
                "  %26 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_vector_rows_model,
        // Tier 0's placement moved 25: each vector operand in (17) and each vector result out (8). What is left, by
        // either path's allocation: the merging opmask's first operand in and the result out (its destination is
        // the second operand's register), the narrow result's move into itself at its width, the narrow operand's
        // copy at its width, and the tied operand's in and out (its destination is the second's).
        .rows = 9,
        .row_moves = 6,
    },
    {
        // #1283's review: a gather (the front end's row of _mm512_mask_i32gather_epi32) whose destination can be none of
        // its index's and mask's registers, #UD otherwise: its source and index one value dying at it, whose register
        // the allocation gives the result, so the gather writes another and the result moves there; and one of a source
        // and an index apart, the index dying at it.
        .name = "gathers whose index dies at them",
        .text = "function (ptr) -> void {\n"
                "  row #0 (vector64, u64, vector64, mask2) -> vector64 [VPGATHERDD_Z_VM32Z 0x80 0x00 0x81 0x82 0x1e 0x0b 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = ptrtoint.i64 %0\n"
                "  %2 = address.ptr %0 offset=64\n"
                "  %3 = load.v16i32 %2 align=1\n"
                "  %4 = load.m16 %0 align=2\n"
                "  %5 = call.v16i32 row #0(%3, %1, %3, %4) fixed=(v16i32, i64, v16i32, m16) variadic=0\n"
                "  %6 = load.v16i32 %0 align=1\n"
                "  %7 = load.v16i32 %2 align=1\n"
                "  %8 = load.m16 %0 align=2\n"
                "  %9 = call.v16i32 row #0(%6, %1, %7, %8) fixed=(v16i32, i64, v16i32, m16) variadic=0\n"
                "  %10 = address.ptr %0 offset=128\n"
                "  %11 = store.void %10, %5 align=1\n"
                "  %12 = address.ptr %0 offset=192\n"
                "  %13 = store.void %12, %9 align=1\n"
                "  %14 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_gather_model,
        .prepare = tier1_masks_test_gather_prepare,
        .exclusive = true,
        // Each gather's source copied into zmm16, its mask (which it clears) into a free opmask register and its result
        // moved out, on either production path.
        .rows = 2,
        .row_moves = 6,
    },
    {
        // #1283's review: AVX512-FP16's complex multiplication, whose destination can be neither factor's register, #UD
        // otherwise: a row whose result is the product alone, its factors dying at it, one of whose registers the
        // allocation gives the result, so it writes another and the result moves there; and the front end's row of
        // _mm512_fmul_pch, its result the copy of the product its third operand (the first factor again) holds.
        .name = "complex multiplications whose factors die at them",
        .text = "function (ptr) -> void {\n"
                "  row #0 (u32, vector64, vector64) -> vector64 [VFMULCPH_Z_Z_ZM512 0x80 0x81 0x82 0x00 0x00 0x00 0 0]\n"
                "  row #1 (vector64, vector64, vector64, u32) -> vector64 [VFMULCPH_Z_Z_ZM512 0x82 0x80 0x81 0x00 0x00 0x00 0 0] [VMOVDQU64_Z_ZM512 0x80 0x00 0x82 0x00 0x00 0x00 0 0]\n"
                "b0:\n"
                "  %0 = param.ptr 0\n"
                "  %1 = load.v16i32 %0 align=1\n"
                "  %2 = address.ptr %0 offset=64\n"
                "  %3 = load.v16i32 %2 align=1\n"
                "  %4 = const.i32 0x00000000000000000000000000000000\n"
                "  %5 = call.v16i32 row #0(%4, %1, %3) fixed=(i32, v16i32, v16i32) variadic=0\n"
                "  %6 = load.v16i32 %0 align=1\n"
                "  %7 = load.v16i32 %2 align=1\n"
                "  %8 = call.v16i32 row #1(%6, %7, %6, %4) fixed=(v16i32, v16i32, v16i32, i32) variadic=0\n"
                "  %9 = address.ptr %0 offset=128\n"
                "  %10 = store.void %9, %5 align=1\n"
                "  %11 = address.ptr %0 offset=192\n"
                "  %12 = store.void %11, %8 align=1\n"
                "  %13 = ret.void\n"
                "}\n",
        .model = tier1_masks_test_complex_model,
        .fp16 = true,
        .exclusive = true,
        // The first's product moved out of zmm16; the second's third operand copied out of the first factor's register
        // it shares, and its result written in the destination, the dying second factor's register, after it is read.
        .rows = 2,
        .row_moves = 2,
    },
};

// Refusals: a vector, a mask's select.
// A function returning a vector (#141), which no convention's place Tier 1 has (tier1_select_vector_signature): its
// vectors within are Tier 1's.
static const char tier1_masks_test_vector[] = "function (ptr) -> v4i32 {\n"
                                              "b0:\n"
                                              "  %0 = param.ptr 0\n"
                                              "  %1 = load.v4i32 %0 align=16\n"
                                              "  %2 = ret.void %1\n"
                                              "}\n";

// A 16-byte vector's comparison and selection, whose mask is an opmask: no SSE target has one, so the baseline refuses
// them at emission (#1425's review).
static const char tier1_masks_test_sse_compare[] = "function (ptr) -> void {\n"
                                                   "b0:\n"
                                                   "  %0 = param.ptr 0\n"
                                                   "  %1 = address.ptr %0 offset=0\n"
                                                   "  %2 = load.v4i32 %1 align=1\n"
                                                   "  %3 = address.ptr %0 offset=16\n"
                                                   "  %4 = load.v4i32 %3 align=1\n"
                                                   "  %5 = vcmp.m8 %2, %4 lt\n"
                                                   "  %6 = vselect.v4i32 %5, %2, %4\n"
                                                   "  %7 = address.ptr %0 offset=128\n"
                                                   "  %8 = store.void %7, %6 align=1\n"
                                                   "  %9 = ret.void\n"
                                                   "}\n";

static const char tier1_masks_test_select[] = "function (ptr, i64) -> void {\n"
                                              "b0:\n"
                                              "  %0 = param.ptr 0\n"
                                              "  %1 = param.i64 1\n"
                                              "  %2 = load.m16 %0 align=2\n"
                                              "  %3 = not.m16 %2\n"
                                              "  %4 = const.i64 0x00000000000000000000000000000000\n"
                                              "  %5 = icmp.i1 %1, %4 ne\n"
                                              "  %6 = select.m16 %5, %2, %3\n"
                                              "  %7 = address.ptr %0 offset=128\n"
                                              "  %8 = store.void %7, %6 align=2\n"
                                              "  %9 = ret.void\n"
                                              "}\n";

// A word mask alone: kmovw and kandw, AVX512F's.
static const char tier1_masks_test_words[] = "function (ptr) -> void {\n"
                                             "b0:\n"
                                             "  %0 = param.ptr 0\n"
                                             "  %1 = load.m16 %0 align=2\n"
                                             "  %2 = address.ptr %0 offset=2\n"
                                             "  %3 = load.m16 %2 align=2\n"
                                             "  %4 = and.m16 %1, %3\n"
                                             "  %5 = address.ptr %0 offset=128\n"
                                             "  %6 = store.void %5, %4 align=2\n"
                                             "  %7 = ret.void\n"
                                             "}\n";

// A function from IR text, by both of ir_parse.c's paths, which must agree, and its dominance (dominators.c's), in the
// test's output arena.
static bool tier1_masks_test_dominance(Tier1MasksTest* test, const IrFunction* function, IrDominance* dominance);

static bool tier1_masks_test_parse(Tier1MasksTest* test, const char* text, IrFunction* function, IrDominance* dominance)
{
    const String8 source = { .pointer = (char*)text, .length = strlen(text) };
    IrFunction twin;
    const IrParseResult vector = ir_parse(source, &test->output, &test->scratch, function);
    const IrParseResult scalar = ir_parse_scalar(source, &test->output, &test->scratch, &twin);
    bool ok = tier1_masks_test_expect(test, vector.error == IR_PARSE_OK && scalar.error == IR_PARSE_OK && vector.consumed == scalar.consumed, "both IR parsers take the fixture");
    if (!ok)
    {
        printf("test-tier1-masks: parse error %u line %" PRIu64 ", scalar %u line %" PRIu64 "\n", (unsigned)vector.error, vector.line, (unsigned)scalar.error, scalar.line);
    }
    ok = ok && tier1_masks_test_expect(test, function->instruction_count == twin.instruction_count && memcmp(function->op, twin.op, function->instruction_count) == 0 && memcmp(function->type, twin.type, function->instruction_count) == 0, "both parsers make the same function");
    return ok && tier1_masks_test_dominance(test, function, dominance);
}

// A function's dominance, from its CFG, as Tier 1's selection takes it.
static bool tier1_masks_test_dominance(Tier1MasksTest* test, const IrFunction* function, IrDominance* dominance)
{
    IrCfg cfg;
    bool ok = tier1_masks_test_expect(test, ir_build_cfg(function, &test->output, &test->scratch, &cfg) == IR_OK, "build the fixture's CFG");
    if (ok)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.outputs = 0;
        Dominators result;
        ok = tier1_masks_test_expect(test, dominators_analyze(&test->output, &test->scratch, &graph, options, &result) == DOMINATOR_OK, "the fixture's dominance");
        *dominance = (IrDominance){
            .enter = result.preorder,
            .leave = result.subtree_end,
            .block_count = result.block_count,
        };
    }
    return ok;
}

// The helper's records: kxnorq into every opmask register, then rdi + 1 into rax, and ret. Its result is also xmm0's,
// in both of its quadwords (vpbroadcastq), so that a call of it as a double's (#1280) returns one whose upper lanes,
// which the convention leaves undefined, aren't zero.
static uint32_t tier1_masks_test_helper(X86Instruction* records)
{
    uint32_t count = 0;
    for (uint8_t k = 0; k < 8; k += 1)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_KXNORQ_K_K_K, .reg = k, .vvvv = k, .rm = k };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_LEA_R64_M, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE, .displacement = 1 };
    records[count++] = (X86Instruction){ .form = X86_FORM_VPBROADCASTQ_X_R64, .reg = 0, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT };
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    return count;
}

// The entry takes xmm0 and xmm1 too (#1280), which a fixture of a double and a float parameter reads as them: their low
// bytes the parameters, and past them signaling NaNs, the bytes the convention leaves undefined.
typedef long long Tier1MasksTestXmm __attribute__((vector_size(16)));
typedef void (*Tier1MasksTestEntry)(uint8_t*, uint64_t, Tier1MasksTestXmm, Tier1MasksTestXmm);

static __attribute__((no_sanitize("function"))) void tier1_masks_test_call(const uint8_t* code, uint8_t* buffer, uint64_t count)
{
    Tier1MasksTestEntry entry;
    memcpy(&entry, &code, sizeof(entry));
    const Tier1MasksTestXmm xmm0 = { (long long)BUSTER_TIER1_MASKS_TEST_DOUBLE, (long long)BUSTER_TIER1_MASKS_TEST_DIRTY };
    const Tier1MasksTestXmm xmm1 = { (long long)((BUSTER_TIER1_MASKS_TEST_DIRTY & ~UINT64_C(0xffffffff)) | BUSTER_TIER1_MASKS_TEST_FLOAT), (long long)BUSTER_TIER1_MASKS_TEST_DIRTY };
    entry(buffer, count, xmm0, xmm1);
}

// A native run of an `avx` fixture (#1293's review), with every ymm register a signaling NaN and MXCSR's status flags
// clear, its invalid and denormal exceptions unmasked, so that a record reading a register's old bits as floats traps
// (SIGFPE): whether MXCSR's status flags are still clear after it. MXCSR is restored after the call; the stack pointer is
// kept in rbx, below the red zone and aligned to 32 bytes for the call, and the state in r12, so that no operand is
// addressed from rsp once it moves.
static __attribute__((no_sanitize("function"))) bool tier1_masks_test_call_seeded(const uint8_t* code, uint8_t* buffer, uint64_t count)
{
    // The signaling NaN's bits, MXCSR's control for the run (every exception masked but invalid and denormal), its value
    // after the run, and its value before, restored.
    static uint32_t state[4] = { 0x7fa00000u, 0x1f80u & ~0x0180u, 0, 0 };
    uint8_t* first = buffer;
    uint64_t second = count;
    __asm__ volatile("mov %[state], %%r12\n\t"
                     "stmxcsr 12(%%r12)\n\t"
                     "vbroadcastss (%%r12), %%ymm0\n\t"
                     "vmovaps %%ymm0, %%ymm1\n\t"
                     "vmovaps %%ymm0, %%ymm2\n\t"
                     "vmovaps %%ymm0, %%ymm3\n\t"
                     "vmovaps %%ymm0, %%ymm4\n\t"
                     "vmovaps %%ymm0, %%ymm5\n\t"
                     "vmovaps %%ymm0, %%ymm6\n\t"
                     "vmovaps %%ymm0, %%ymm7\n\t"
                     "vmovaps %%ymm0, %%ymm8\n\t"
                     "vmovaps %%ymm0, %%ymm9\n\t"
                     "vmovaps %%ymm0, %%ymm10\n\t"
                     "vmovaps %%ymm0, %%ymm11\n\t"
                     "vmovaps %%ymm0, %%ymm12\n\t"
                     "vmovaps %%ymm0, %%ymm13\n\t"
                     "vmovaps %%ymm0, %%ymm14\n\t"
                     "vmovaps %%ymm0, %%ymm15\n\t"
                     "ldmxcsr 4(%%r12)\n\t"
                     "mov %%rsp, %%rbx\n\t"
                     "sub $128, %%rsp\n\t"
                     "and $-32, %%rsp\n\t"
                     "call *%[entry]\n\t"
                     "mov %%rbx, %%rsp\n\t"
                     "stmxcsr 8(%%r12)\n\t"
                     "ldmxcsr 12(%%r12)\n\t"
                     "vzeroupper"
                     : "+D"(first), "+S"(second)
                     : [state] "r"(state), [entry] "r"(code)
                     : "rax", "rbx", "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
                       "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
    return (state[2] & 0x3fu) == 0;
}

// The emitted function's records, sized, encoded by both encoders (which must agree) with the helper after them, its
// calls resolved to the helper, in an executable mapping; MAP_FAILED, the failure reported, otherwise.
static uint8_t* tier1_masks_test_load(Tier1MasksTest* test, Tier1EmitResult* emitted, uint64_t* mapped)
{
    Tier1SelectCode* code = &emitted->code;
    X86Instruction helper[16];
    const uint32_t helper_count = tier1_masks_test_helper(helper);
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t helper_start = (capacity + 63) & ~UINT64_C(63);
    *mapped = (helper_start + BUSTER_X86_ENCODER_CAPACITY(helper_count) + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* scalar = malloc(capacity);
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    uint32_t* scalar_ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    bool ok = tier1_masks_test_expect(test, machine != MAP_FAILED && scalar != 0 && offsets != 0 && ends != 0 && scalar_ends != 0, "the encoders' buffers");
    if (ok)
    {
        const BranchSizingCheck checked = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
        ok = tier1_masks_test_expect(test, checked.error == BRANCH_SIZING_OK, "the function's branches validate");
    }
    if (ok)
    {
        const BranchSizing sized = size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets);
        ok = tier1_masks_test_expect(test, sized.error == BRANCH_SIZING_OK && x86_target_validate(test->forms, code->records, code->count) == code->count, "the sized records obey their target's forms");
        bool covered = true;
        for (uint32_t r = 0; ok && r < code->count; r += 1)
        {
            covered &= x86_effects(code->records[r].form)->operation != X86_OPERATION_NONE;
        }
        ok = ok && tier1_masks_test_expect(test, covered, "every record has a form x86_semantics.c covers");
        // #181: and a complete Zen 5 row (x86_zen5_rows.c), each of x86-64-v4's forms, which Zen 5 runs: AVX512-FP16's,
        // which it doesn't (the fp16 fixtures'), have none.
        uint32_t missing = 0;
        for (uint32_t r = 0; ok && r < code->count; r += 1)
        {
            const bool runs = x86_target_form_allowed(test->v4, code->records[r].form);
            missing += runs && !x86_zen5_accounts(x86_zen5_id(&code->records[r]), BUSTER_X86_COST_ALL_COLUMNS);
        }
        ok = ok && tier1_masks_test_expect(test, missing == 0, "every record Zen 5 runs has a complete Zen 5 row");
    }
    for (uint32_t r = 0; ok && test->print && r < code->count; r += 1)
    {
        printf("    ");
        x86_print_instruction(stdout, &code->records[r], 0);
        printf("\n");
    }
    int64_t size = -1;
    if (ok)
    {
        size = x86_encode(code->records, code->count, machine, capacity, 0, ends);
        const int64_t scalar_size = x86_encode_scalar(code->records, code->count, scalar, capacity, 0, scalar_ends);
        ok = tier1_masks_test_expect(test, size > 0 && size == scalar_size && memcmp(machine, scalar, (uint64_t)size) == 0 && memcmp(ends, scalar_ends, (uint64_t)emitted->reference_count * sizeof(uint32_t)) == 0, "both encoders make the same bytes");
        const int64_t helper_size = x86_encode(helper, helper_count, machine + helper_start, BUSTER_X86_ENCODER_CAPACITY(helper_count), 0, 0);
        ok = ok && tier1_masks_test_expect(test, helper_size > 0, "encode the helper");
        test->records += code->count;
    }
    for (uint32_t reference = 0; ok && reference < emitted->reference_count; reference += 1)
    {
        const LinkReference* fixup = &emitted->references[reference];
        const uint32_t end = ends[reference];
        const uint32_t distance = x86_fixup_distance(code->records[fixup->instruction].form, fixup->field);
        ok = tier1_masks_test_expect(test, fixup->kind == BUSTER_LINK_FIXUP_CALL && fixup->target == BUSTER_TIER1_MASKS_TEST_HELPER && distance == 4 && end >= distance, "each call is the helper's");
        if (ok)
        {
            const int32_t relative = (int32_t)((int64_t)helper_start - (int64_t)end);
            memcpy(machine + end - distance, &relative, sizeof(relative));
        }
    }
    ok = ok && tier1_masks_test_expect(test, mprotect(machine, *mapped, PROT_READ | PROT_EXEC) == 0, "make the code executable");
    free(scalar);
    free(offsets);
    free(ends);
    free(scalar_ends);
    if (!ok && machine != MAP_FAILED)
    {
        munmap(machine, *mapped);
    }
    return ok ? machine : MAP_FAILED;
}

// The native MXCSR fixtures (#1450): a function of four float lanes, `left` and `right` by `op` (an equality vcmp where
// `op` is IR_VCMP), its lanes compared with `expected`'s as integers unless it is the comparison, so that the floating
// value reaches the result through an integer mask alone, and returned as an i8. Optimized, selected for x86-64-v4,
// emitted and loaded (MAP_FAILED where a step fails). `folded` says whether SCCP made the returned mask a constant
// while keeping the floating operation's row for its effects (ir_optimize_simd_effect). The arenas are the caller's to
// pop once the code is unmapped.
static uint8_t* tier1_masks_test_mxcsr_code(Tier1MasksTest* test, uint32_t op, uint32_t left, uint32_t right, uint32_t expected, bool* folded, uint64_t* mapped)
{
    IrFunction input = { 0 };
    const bool compare = op == IR_VCMP;
    const IrSizes sizes = { .instruction_count = compare ? 5 : 7, .block_count = 1, .vector_constant_count = compare ? 2 : 3 };
    bool ok = tier1_masks_test_expect(test, ir_create(&test->execution, &sizes, &input) == IR_OK, "create MXCSR optimizer fixture");
    uint8_t* code = MAP_FAILED;
    *folded = false;
    if (ok)
    {
        input.result_type = IR_I8;
        input.block_offsets[1] = sizes.instruction_count;
        for (uint32_t lane = 0; lane < 4; lane += 1)
        {
            ir_simd_lane_write(&input.vector_constants[0], 32, lane, left);
            ir_simd_lane_write(&input.vector_constants[1], 32, lane, right);
            if (!compare)
            {
                ir_simd_lane_write(&input.vector_constants[2], 32, lane, expected);
            }
        }
        ir_optimize_test_instruction(&input, 0, IR_VCONST, IR_V4I32, 0, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&input, 1, IR_VCONST, IR_V4I32, 1, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&input, 2, op, compare ? IR_M8 : IR_V4I32, 0, 1, compare ? IR_EQ : BUSTER_IR_NONE);
        input.flags[2] = IR_SIMD_LANE_F32;
        if (!compare)
        {
            ir_optimize_test_instruction(&input, 3, IR_VCONST, IR_V4I32, 2, BUSTER_IR_NONE, BUSTER_IR_NONE);
            ir_optimize_test_instruction(&input, 4, IR_VCMP, IR_M8, 2, 3, IR_EQ);
            input.flags[4] = IR_SIMD_LANE_I32;
        }
        const uint32_t cast = compare ? 3 : 5;
        ir_optimize_test_instruction(&input, cast, IR_BITCAST, IR_I8, compare ? 2 : 4, BUSTER_IR_NONE, BUSTER_IR_NONE);
        ir_optimize_test_instruction(&input, cast + 1, IR_RET, IR_VOID, cast, BUSTER_IR_NONE, BUSTER_IR_NONE);
        IrOptimizeResult optimized = { 0 };
        const IrCheck check = ir_optimize(&input, &test->output, &test->scratch, ir_optimize_options(), &optimized);
        ok = tier1_masks_test_expect(test, check.error == IR_OK, "optimize MXCSR fixture");
        const IrFunction* out = &optimized.function;
        if (ok)
        {
            uint32_t effects = 0;
            for (uint32_t i = 0; i < out->instruction_count; i += 1)
            {
                effects += ir_optimize_simd_effect(out, i);
            }
            const uint32_t ret = out->instruction_count - 1;
            *folded = out->op[ret] == IR_RET && out->op[out->a[ret]] == IR_CONST && effects == 1;
        }
        IrDominance dominance;
        ok = ok && tier1_masks_test_dominance(test, out, &dominance);
        Tier1SelectPlan plan;
        uint32_t node = BUSTER_IR_NONE;
        const Tier1SelectError selected = ok ? tier1_select_plan(out, &dominance, &test->output, &test->scratch, true, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
        ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, "select MXCSR fixture");
        const Tier1EmitOptions options = { .certify = true, .register_mask = tier1_liveness_register_mask(), .vector = true, .rematerialize = true };
        Tier1EmitResult emitted = { 0 };
        const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->v4, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        ok = tier1_masks_test_expect(test, error == TIER1_EMIT_OK, "emit MXCSR fixture");
        code = ok ? tier1_masks_test_load(test, &emitted, mapped) : MAP_FAILED;
    }
    return code;
}

// The fixture's code called under `mxcsr` (its status flags clear unless the caller sets them): its result, and in
// `flags` the status flags it raised. The caller's MXCSR is restored.
static __attribute__((no_sanitize("function"))) uint8_t tier1_masks_test_mxcsr_call(const uint8_t* code, uint32_t mxcsr, uint32_t* flags)
{
    typedef uint8_t (*Entry)(void);
    Entry entry;
    memcpy(&entry, &code, sizeof(entry));
    const uint32_t saved = _mm_getcsr();
    _mm_setcsr(mxcsr);
    const uint8_t result = entry();
    *flags = _mm_getcsr() & 0x3fu;
    _mm_setcsr(saved);
    return result;
}

// Constant SIMD operations must still use the caller's MXCSR after SCCP. Execute the actual optimized Tier 1
// output under both default and changed controls; the integer mask compares its result bits without FP arithmetic.
// Each of these results depends on the environment, so SCCP must not fold it.
static void tier1_masks_test_mxcsr(Tier1MasksTest* test)
{
    if (!test->native)
    {
        return;
    }
    // Rounding upward of 1 + 2^-24; flush-to-zero of min-normal * 0.5; denormals-are-zero comparison.
    static const uint8_t ops[] = { IR_FADD, IR_FMUL, IR_VCMP };
    static const uint32_t left[] = { 0x3f800000u, 0x00800000u, 1u };
    static const uint32_t right[] = { 0x33800000u, 0x3f000000u, 0u };
    static const uint32_t expected[] = { 0x3f800000u, 0x00400000u, 0u };
    static const uint32_t controls[] = { 0x5f80u, 0x9f80u, 0x1fc0u };
    for (uint32_t shape = 0; shape < 3; shape += 1)
    {
        const uint64_t input_mark = test->execution.length;
        const uint64_t output_mark = test->output.length;
        const bool compare = ops[shape] == IR_VCMP;
        bool folded = true;
        uint64_t mapped = 0;
        uint8_t* code = tier1_masks_test_mxcsr_code(test, ops[shape], left[shape], right[shape], expected[shape], &folded, &mapped);
        tier1_masks_test_expect(test, !folded, "SCCP folds no SIMD result that rounding, FTZ or DAZ changes");
        if (code != MAP_FAILED)
        {
            uint32_t flags = 0;
            const uint8_t standard = tier1_masks_test_mxcsr_call(code, 0x1f80u, &flags);
            const uint8_t changed = tier1_masks_test_mxcsr_call(code, controls[shape], &flags);
            tier1_masks_test_expect(test, standard == (compare ? 0 : 15) && changed == (compare ? 15 : 0), "optimized native SIMD observes runtime rounding, FTZ and DAZ");
            munmap(code, mapped);
        }
        arena_pop_to(&test->output, output_mark);
        arena_pop_to(&test->execution, input_mark);
    }
}

// A floating SIMD operation SCCP folds keeps its row (#1450's review): the optimized native code still raises its
// exception. 0/0, a quiet NaN plus a signaling one and an equality comparison of a signaling NaN set MXCSR's invalid
// flag, 1/0 its divide-by-zero flag, and with that exception unmasked each traps (SIGFPE, in a child process).
static void tier1_masks_test_folded_effects(Tier1MasksTest* test)
{
    if (!test->native)
    {
        return;
    }
    static const uint8_t ops[] = { IR_FDIV, IR_FDIV, IR_FADD, IR_VCMP };
    static const uint32_t left[] = { 0u, 0x3f800000u, 0x7fc00001u, 0x7f800001u };
    static const uint32_t right[] = { 0u, 0u, 0x7f800002u, 0x3f800000u };
    // The default NaN, an infinity, the first operand's quiet NaN (x86's rule), and an unordered lane's 0.
    static const uint32_t expected[] = { 0xffc00000u, 0x7f800000u, 0x7fc00001u, 0u };
    // MXCSR's IE and ZE status flags; each one's mask bit is 7 bits above it.
    static const uint32_t raised[] = { 0x01u, 0x04u, 0x01u, 0x01u };
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        const uint64_t input_mark = test->execution.length;
        const uint64_t output_mark = test->output.length;
        const bool compare = ops[shape] == IR_VCMP;
        bool folded = false;
        uint64_t mapped = 0;
        uint8_t* code = tier1_masks_test_mxcsr_code(test, ops[shape], left[shape], right[shape], expected[shape], &folded, &mapped);
        tier1_masks_test_expect(test, folded, "SCCP folds an environment-free SIMD result and keeps its floating row");
        if (code != MAP_FAILED)
        {
            uint32_t flags = 0;
            const uint8_t result = tier1_masks_test_mxcsr_call(code, 0x1f80u, &flags);
            tier1_masks_test_expect(test, result == (compare ? 0 : 15) && (flags & raised[shape]) != 0, "a folded SIMD operation still raises its status flag natively");
            fflush(0);
            const pid_t child = fork();
            if (child == 0)
            {
                alarm(5);
                signal(SIGFPE, SIG_DFL);
                uint32_t unused = 0;
                (void)tier1_masks_test_mxcsr_call(code, 0x1f80u & ~(raised[shape] << 7), &unused);
                _exit(0);
            }
            int status = 0;
            const bool waited = child > 0 && waitpid(child, &status, 0) == child;
            tier1_masks_test_expect(test, waited && WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE, "a folded SIMD operation still traps where its exception is unmasked");
            munmap(code, mapped);
        }
        arena_pop_to(&test->output, output_mark);
        arena_pop_to(&test->execution, input_mark);
    }
}

// The interpreter's buffer after the function, from `input`: the buffer bound as memory, the pointer its address.
static bool tier1_masks_test_interpret(Tier1MasksTest* test, const IrFunction* function, const uint8_t* input, uint64_t count, uint8_t* output)
{
    memcpy(output, input, BUSTER_TIER1_MASKS_TEST_BUFFER);
    const IrInterpreterFunction entry = { .symbol = BUSTER_TIER1_MASKS_TEST_ENTRY, .function = function };
    const InterpreterBinding binding = { .symbol = BUSTER_TIER1_MASKS_TEST_BINDING, .bytes = output, .size = BUSTER_TIER1_MASKS_TEST_BUFFER, .writable = true };
    const InterpreterNativeBinding native = { .symbol = BUSTER_TIER1_MASKS_TEST_HELPER, .address = (void*)tier1_masks_test_successor };
    const IrInterpreterProgram program = {
        .functions = &entry,
        .function_count = 1,
        .bindings = &binding,
        .binding_count = 1,
        .natives = &native,
        .native_count = 1,
    };
    const InterpreterValue arguments[2] = {
        { .low = (uint64_t)(uintptr_t)output },
        { .low = count },
    };
    const InterpreterResult result = ir_interpret(&program, BUSTER_TIER1_MASKS_TEST_ENTRY, arguments, function->parameter_count, 0, &test->execution);
    return tier1_masks_test_expect(test, result.status == INTERPRETER_OK, "the interpreter runs the fixture");
}

// One fixture by one path: planned, emitted, its allocation checked, and where the host can, run on every input.
// Whether the code's certificate refuses the code as it is now.
static bool tier1_masks_test_refused(Tier1MasksTest* test, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted)
{
    Tier1CertEmittedStats stats = { 0 };
    return tier1_certificate_emitted_check(plan, emitted, &test->output, &test->scratch, 0, &stats) >= TIER1_CERT_EMITTED_REFUSED;
}

// The general registers record `record`'s fields name (Tier 0's fields of a placed record: its register fields, a memory
// operand's base and index), each renamed `from` to `to` where `rename`.
static uint32_t tier1_masks_test_general(X86Instruction* record, bool rename, uint8_t from, uint8_t to)
{
    const Tier0Fields fields = tier0_operation_fields(record);
    uint32_t named = 0;
    for (uint32_t k = 0; k < fields.count; k += 1)
    {
        const uint8_t class = fields.classes[k];
        const bool general = class == BUSTER_X86_CLASS_GPR8 || class == BUSTER_X86_CLASS_GPR16 || class == BUSTER_X86_CLASS_GPR32 || class == BUSTER_X86_CLASS_GPR64;
        named |= general ? 1u << (*fields.bytes[k] & 15) : 0;
        *fields.bytes[k] = general && rename && *fields.bytes[k] == from ? to : *fields.bytes[k];
    }
    return named;
}

// The roles a row's template names (#1284): VECTOR(j)'s bit j, SCRATCH(k)'s 16 + k, MASK_SCRATCH(k)'s 20 + k and the
// opmask register k's by number 24 + k.
static uint32_t tier1_masks_test_roles(const Tier0Operation* row)
{
    uint32_t roles = 0;
    for (uint32_t r = 0; r < row->record_count; r += 1)
    {
        X86Instruction record = row->records[r];
        const Tier0Fields fields = tier0_operation_fields(&record);
        for (uint32_t k = 0; k < fields.count; k += 1)
        {
            const uint8_t role = *fields.bytes[k];
            const uint8_t class = fields.classes[k];
            const bool vector = class == BUSTER_X86_CLASS_XMM || class == BUSTER_X86_CLASS_YMM || class == BUSTER_X86_CLASS_ZMM;
            const bool general = class == BUSTER_X86_CLASS_GPR8 || class == BUSTER_X86_CLASS_GPR16 || class == BUSTER_X86_CLASS_GPR32 || class == BUSTER_X86_CLASS_GPR64;
            const bool mask = class == BUSTER_X86_CLASS_OPMASK;
            roles |= vector && role >= BUSTER_TIER0_ROLE && role < BUSTER_TIER0_ROLE_VECTOR(BUSTER_TIER0_OPERATION_VECTORS) ? 1u << (role & 15) : 0;
            roles |= general && role >= BUSTER_TIER0_ROLE_SCRATCH(0) && role < BUSTER_TIER0_ROLE_SCRATCH(BUSTER_TIER0_SCRATCH) ? 1u << (16 + (role & 3)) : 0;
            roles |= mask && role >= BUSTER_TIER0_ROLE_MASK_SCRATCH(0) && role < BUSTER_TIER0_ROLE_MASK_SCRATCH(BUSTER_TIER0_SCRATCH) ? 1u << (20 + (role & 3)) : 0;
            roles |= mask && role < 8 ? 1u << (24 + role) : 0;
        }
        roles |= fields.is4 ? 1u << (((uint32_t)record.immediate >> 4) & 15) : 0;
        const uint32_t decoration = record.decorations & BUSTER_TIER0_DECORATION_ROLE;
        const uint32_t index = record.decorations & BUSTER_X86_DECORATION_MASK;
        roles |= decoration == BUSTER_TIER0_DECORATION_MASK_SCRATCH(0) ? 1u << (20 + (index & 3)) : 0;
        roles |= decoration == 0 && index != 0 ? 1u << (24 + index) : 0;
    }
    return roles;
}

// Machine row node `node`'s claimed placement corrupted (#1284), its records [first, first + the row's) placed again by the
// corrupted claim as the certificate places them (tier1_cert_emitted_row_place), so that only the claim's own checks can
// refuse it: each non-operand role (SCRATCH(k), MASK_SCRATCH(k), a VECTOR(j) no vector operand has) made the result's
// register; the result made the register of an operand of its class that Tier 0's own placement doesn't give it
// (tier0_operation_placement, tier0_operation_mask_place: its oracle); and each opmask register the row names by number,
// k1 to k7, made k0, which in a decoration is no mask. Each must be refused; everything is put back after.
static void tier1_masks_test_claim_mutants(Tier1MasksTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted, uint32_t node, uint32_t first)
{
    const IrFunction* function = plan->function;
    const Tier0Operation* row = &function->rows[(uint32_t)plan->patterns[node].immediate];
    X86Instruction* records = emitted->code.records;
    Tier1RowPlacement* claimed = &emitted->row_placements[node];
    const Tier1RowPlacement original = *claimed;
    const bool fits = row->record_count <= BUSTER_TIER0_OPERATION_RECORDS && first + row->record_count <= emitted->code.count;
    X86Instruction saved[BUSTER_TIER0_OPERATION_RECORDS];
    for (uint32_t r = 0; fits && r < row->record_count; r += 1)
    {
        saved[r] = records[first + r];
    }
    // Tier 0's placement of the row with its operands at the bottom of each stack, its registers apart.
    uint32_t integers = 0;
    uint32_t floatings = 0;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        integers += tier0_stack(row->operand_classes[j]) == 1 ? 1 : 0;
        floatings += tier0_stack(row->operand_classes[j]) == 2 ? 1 : 0;
    }
    Tier0Placement tier0 = tier0_operation_placement(row, (int32_t)integers, (int32_t)floatings);
    const Tier0MaskPlan mask_plan = tier0_operation_mask_plan(row, tier0_operation_masks(row).operands);
    tier0_operation_mask_place(row, &mask_plan, &tier0);
    const uint32_t roles = tier1_masks_test_roles(row);
    const uint32_t result = row->result_class;
    uint32_t vector_operands = 0;
    for (uint32_t j = 0; j < row->operand_count; j += 1)
    {
        vector_operands |= tier0_class_vector(row->operand_classes[j]) ? 1u << j : 0;
    }
    // VECTOR(j)'s uses (#1283): the result's VECTOR(0) holds its register from the row's entry where it is the first
    // operand's role there, else from its first write; a temporary VECTOR(j) from its first write to its last naming.
    const Tier1SelectRowVectors facts = tier1_select_row_vectors(row);
    const bool split = original.split != 0;
    const uint32_t result_start = (vector_operands & 1) != 0 && !split ? 0 : facts.first_write[0];
    test->row_splits += split;
    // Mutant m: 0 to 15 VECTOR(j) onto the result, 16 to 19 SCRATCH(k) or MASK_SCRATCH(k), 20 to 27 the result onto operand
    // j, 28 to 34 opmask k (1 to 7) by number made k0; 35 and 36 a split's claim corrupted, its split cleared or its early
    // register made the result's; 37 to 52 a temporary VECTOR(j), written before a vector operand's last naming, onto that
    // operand's register (only the certificate's rule on the roles' uses refuses it: the operand is where the row reads it
    // first, and the temporary's write overwrites it before its last read).
    for (uint32_t m = 0; fits && m < 53; m += 1)
    {
        Tier1RowPlacement claim = original;
        bool k0 = false;
        bool split_mutant = false;
        bool overlap = false;
        bool mutated = false;
        // A temporary onto the result: where its uses reach past the result's first write (one that ends before it is done
        // with the register, which the result may take).
        const bool overlapping = m < 16 && (result_start == 0 || facts.last_named[m] > result_start);
        if (m < 16 && overlapping && ((roles >> m) & 1) != 0 && ((vector_operands >> m) & 1) == 0 && (tier0_class_floating(result) || (tier0_class_vector(result) && m != 0)))
        {
            claim.vectors[m] = tier0_class_floating(result) ? claim.floating_result : claim.vectors[0];
            mutated = claim.vectors[m] != original.vectors[m];
        }
        else if (m >= 16 && m < 20 && tier0_class_integer(result) && ((roles >> m) & 1) != 0)
        {
            claim.scratch[m - 16] = claim.integer_result;
            mutated = claim.scratch[m - 16] != original.scratch[m - 16];
        }
        else if (m >= 16 && m < 20 && tier0_class_mask(result) && ((roles >> (m + 4)) & 1) != 0)
        {
            claim.mask_scratch[m - 16] = claim.mask_result;
            mutated = claim.mask_scratch[m - 16] != original.mask_scratch[m - 16];
        }
        else if (m >= 20 && m < 28 && m - 20 < row->operand_count)
        {
            const uint32_t j = m - 20;
            const uint32_t class = row->operand_classes[j];
            if (tier0_class_integer(result) && tier0_class_integer(class) && tier0.operands[j] != tier0.integer_result)
            {
                claim.integer_result = claim.operands[j];
                mutated = claim.integer_result != original.integer_result;
            }
            else if (tier0_class_floating(result) && tier0_class_floating(class) && tier0.operands[j] != tier0.floating_result)
            {
                claim.floating_result = claim.operands[j];
                mutated = claim.floating_result != original.floating_result;
            }
            else if (tier0_class_mask(result) && tier0_class_mask(class) && tier0.masks[j] != tier0.mask_result)
            {
                claim.mask_result = claim.masks[j];
                mutated = claim.mask_result != original.mask_result;
            }
        }
        else if (m >= 28 && m < 35 && ((roles >> (24 + m - 27)) & 1) != 0)
        {
            claim.renamed[m - 27] = 0;
            k0 = true;
            mutated = true;
        }
        else if ((m == 35 || m == 36) && split && claim.early != claim.vectors[0])
        {
            // The first operand read where VECTOR(0)'s later value is, before it is written: the split cleared, or its
            // early register claimed the result's.
            claim.split = m == 35 ? 0 : claim.split;
            claim.position = m == 35 ? 0 : claim.position;
            claim.early = m == 35 ? 0 : claim.vectors[0];
            split_mutant = true;
            mutated = true;
        }
        else if (m >= 37 && m - 37 < 16 && m - 37 != 0 && ((roles >> (m - 37)) & 1) != 0 && ((vector_operands >> (m - 37)) & 1) == 0 && facts.first_write[m - 37] != 0)
        {
            const uint32_t t = m - 37;
            // The first vector operand (but a split's first, read elsewhere) whose last naming comes after t's first write.
            uint32_t live = UINT32_MAX;
            for (uint32_t j = 0; live == UINT32_MAX && j < row->operand_count && j < 16; j += 1)
            {
                live = ((vector_operands >> j) & 1) != 0 && !(j == 0 && split) && facts.last_named[j] > facts.first_write[t] ? j : live;
            }
            claim.vectors[t] = live != UINT32_MAX ? claim.vectors[live] : claim.vectors[t];
            mutated = live != UINT32_MAX && claim.vectors[t] != original.vectors[t];
            overlap = mutated;
        }
        bool placed = mutated;
        for (uint32_t r = 0; placed && r < row->record_count; r += 1)
        {
            uint64_t named = 0;
            uint64_t registers = 0;
            uint32_t masks = 0;
            placed = tier1_cert_emitted_row_place(row->records[r], r, &claim, &records[first + r], &named, &registers, &masks);
        }
        if (placed)
        {
            *claimed = claim;
            const bool refused = tier1_masks_test_refused(test, plan, emitted);
            test->row_roles += k0 || split_mutant ? 0 : 1;
            test->row_roles_refused += !k0 && !split_mutant && refused;
            test->row_k0 += k0 ? 1 : 0;
            test->row_k0_refused += k0 && refused;
            test->row_split_mutants += split_mutant ? 1 : 0;
            test->row_split_refused += split_mutant && refused;
            test->row_overlaps += overlap ? 1 : 0;
            test->row_overlaps_refused += overlap && refused;
            tier1_masks_test_expect(test, refused, k0             ? "a machine row's opmask register named by number, claimed k0, is refused (#1284)"
                                                   : split_mutant ? "a machine row's first operand claimed where VECTOR(0)'s later value is, before its first write, is refused (#1284)"
                                                                  : "a machine row's roles claimed in one register while both hold their values are refused (#1284)");
        }
        *claimed = original;
        for (uint32_t r = 0; mutated && r < row->record_count; r += 1)
        {
            records[first + r] = saved[r];
        }
    }
}

// The certificate's rule on VECTOR roles sharing a register (#1283, tier1_cert_emitted_row_before), on the template
// `vpaddd V2, V0, V1; vpaddd V0, V2, V1` by its own facts (tier1_select_row_vectors): the operand V1 and the temporary V2
// may share the result V0's register, each last read in the record whose pure write makes V0; V1 and V2 may not, V2
// written before V1's last read; nor V1 and V0 where V0's first write merges, or where that record's destination can't
// be one of its sources. Whether each answer is the expected one.
static bool tier1_masks_test_row_occupancy(void)
{
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    row.record_count = 2;
    row.records[0] = (X86Instruction){ .form = X86_FORM_VPADDD_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(2), .vvvv = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_VECTOR(1) };
    row.records[1] = (X86Instruction){ .form = X86_FORM_VPADDD_Z_Z_ZM512, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .vvvv = BUSTER_TIER0_ROLE_VECTOR(2), .rm = BUSTER_TIER0_ROLE_VECTOR(1) };
    const Tier1SelectRowVectors facts = tier1_select_row_vectors(&row);
    const Tier1CertEmittedRole operand = { .operand = 1, .file = 1, .vector = 1, .start = 0, .end = facts.last_named[1] };
    const Tier1CertEmittedRole temporary = { .operand = UINT32_MAX, .file = 1, .vector = 2, .start = facts.first_write[2], .end = facts.last_named[2] };
    const Tier1CertEmittedRole result = { .operand = UINT32_MAX, .file = 1, .vector = 0, .result = true, .start = facts.first_write[0], .end = 3 };
    Tier1SelectRowVectors merging = facts;
    merging.pure &= ~1u;
    Tier1SelectRowVectors exclusive = facts;
    exclusive.exclusive |= 1u << 1;
    const bool shapes = facts.first_write[2] == 1 && facts.first_write[0] == 2 && facts.last_named[1] == 2 && facts.last_named[2] == 2 && (facts.pure & 5) == 5 && facts.exclusive == 0;
    const bool operand_then_result = tier1_cert_emitted_row_before(&row, &facts, operand, result);
    const bool temporary_then_result = tier1_cert_emitted_row_before(&row, &facts, temporary, result);
    const bool operand_temporary = tier1_cert_emitted_row_before(&row, &facts, operand, temporary) || tier1_cert_emitted_row_before(&row, &facts, temporary, operand);
    const bool merged = tier1_cert_emitted_row_before(&row, &merging, operand, result);
    const bool excluded = tier1_cert_emitted_row_before(&row, &exclusive, operand, result);
    return shapes && operand_then_result && temporary_then_result && !operand_temporary && !merged && !excluded;
}

// The code's certificate refuses a corrupted machine row (#1284). For each row of the function: each borrowed opmask
// register's reload after it (its node's kmov from the frame) made its register's move into itself, the mask the row
// borrowed the register from lost; and each of its staging copies into a general register renamed to a register that
// holds a value live across the row, through the row's records, its result's move and its claimed placement
// (Tier1RowPlacement), so that the row computes as it did but overwrites the live value. Each must be refused; everything
// is put back after.
static void tier1_masks_test_row_mutants(Tier1MasksTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    const uint32_t base = 2 * (emitted->cfg_input.block_count - function->block_count);
    X86Instruction* records = emitted->code.records;
    for (uint32_t node = 0; emitted->row_placements != 0 && node < function->instruction_count; node += 1)
    {
        const uint32_t index = (uint32_t)plan->patterns[node].immediate;
        const bool row = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ROW && index < function->row_count;
        const uint32_t own = tier1_origin(TIER1_ORIGIN_NODE, node);
        const uint32_t staging = tier1_origin(TIER1_ORIGIN_MOVE, base + 2 * node + 1);
        uint32_t first = 0;
        while (row && first < emitted->code.count && emitted->origins[first] != own)
        {
            first += 1;
        }
        uint32_t end = first;
        while (row && end < emitted->code.count && emitted->origins[end] == own)
        {
            end += 1;
        }
        const uint32_t rows_end = row && first < emitted->code.count ? first + function->rows[index].record_count : end;
        for (uint32_t r = rows_end; row && r < end; r += 1)
        {
            const X86Instruction record = records[r];
            for (uint32_t width = 1; width <= 8; width *= 2)
            {
                const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
                if (record.form == forms->load && record.mode == BUSTER_X86_MEMORY_BASE && record.rm == emitted->frame.base_register)
                {
                    records[r] = (X86Instruction){ .form = forms->move, .reg = record.reg, .rm = record.reg };
                    const bool refused = tier1_masks_test_refused(test, plan, emitted);
                    records[r] = record;
                    test->row_reloads += 1;
                    test->row_reloads_refused += refused;
                    tier1_masks_test_expect(test, refused, "a machine row's borrowed opmask register left without its reload is refused (#1284)");
                }
            }
        }
        if (row && first < emitted->code.count)
        {
            tier1_masks_test_claim_mutants(test, plan, emitted, node, first);
        }
        // The row's staging copies, the moves at its definition phase right before its records.
        uint32_t staging_first = first;
        while (row && staging_first > 0 && staging_first <= emitted->code.count && emitted->origins[staging_first - 1] == staging)
        {
            staging_first -= 1;
        }
        uint32_t named = 0;
        for (uint32_t r = staging_first; row && r < end; r += 1)
        {
            X86Instruction record = records[r];
            named |= tier1_masks_test_general(&record, false, 0, 0);
        }
        for (uint32_t s = staging_first; row && s < first; s += 1)
        {
            const X86Instruction copy = records[s];
            if (copy.form == X86_FORM_MOV_R64_RM64 && copy.mode == BUSTER_X86_REGISTER_DIRECT && copy.reg < 16 && copy.rm < 16 && copy.reg != copy.rm)
            {
                // A general register holding a value across the row, at its definition phase and at the next node's use,
                // that the row's records and moves don't name.
                uint8_t to = UINT8_MAX;
                for (uint32_t v = 0; to == UINT8_MAX && v < emitted->cfg_input.value_count; v += 1)
                {
                    uint32_t where = 0;
                    uint8_t kind = 0;
                    uint32_t after = 0;
                    uint8_t after_kind = 0;
                    const bool general = emitted->cfg_input.classes[v] == BUSTER_TIER1_ALLOCATE_GPR && emitted->value_nodes[v] != node;
                    const bool held = general && tier1_cfg_location(&emitted->cfg, v, base + 2 * node + 1, &where, &kind) && kind == BUSTER_TIER1_ALLOCATE_REGISTER && where < 16 &&
                                      tier1_cfg_location(&emitted->cfg, v, base + 2 * node + 2, &after, &after_kind) && after_kind == BUSTER_TIER1_ALLOCATE_REGISTER && after == where;
                    to = held && where != X86_REGISTER_RSP && ((named >> where) & 1) == 0 ? (uint8_t)where : UINT8_MAX;
                }
                if (to != UINT8_MAX)
                {
                    X86Instruction saved[BUSTER_TIER1_EMIT_ROW_RECORDS + 64];
                    const uint32_t count = end - s < BUSTER_ARRAY_LENGTH(saved) ? end - s : 0;
                    const Tier1RowPlacement placement = emitted->row_placements[node];
                    Tier1RowPlacement* claimed = &emitted->row_placements[node];
                    for (uint32_t r = 0; r < count; r += 1)
                    {
                        saved[r] = records[s + r];
                        tier1_masks_test_general(&records[s + r], true, copy.reg, to);
                    }
                    for (uint32_t j = 0; j < BUSTER_TIER0_OPERATION_OPERANDS; j += 1)
                    {
                        claimed->operands[j] = claimed->operands[j] == copy.reg ? to : claimed->operands[j];
                    }
                    for (uint32_t k = 0; k < BUSTER_TIER0_SCRATCH; k += 1)
                    {
                        claimed->scratch[k] = claimed->scratch[k] == copy.reg ? to : claimed->scratch[k];
                    }
                    claimed->integer_result = claimed->integer_result == copy.reg ? to : claimed->integer_result;
                    const bool refused = count != 0 && tier1_masks_test_refused(test, plan, emitted);
                    for (uint32_t r = 0; r < count; r += 1)
                    {
                        records[s + r] = saved[r];
                    }
                    *claimed = placement;
                    test->row_stagings += count != 0;
                    test->row_stagings_refused += refused;
                    tier1_masks_test_expect(test, count == 0 || refused, "a machine row's staged operand overwriting a live register is refused (#1284)");
                }
            }
        }
    }
    // The vectors' moves (#1284): each reload of a ymm or zmm register's value, and each of its moves between registers (an
    // edge's or a fragment's), narrowed to its xmm register's, which leaves the register's upper bytes as they were. Its
    // stores are left as they are: a narrowed one may store again into a slot that holds the value already. A call's saves
    // of the widest vector's bytes, of whatever value the register holds, are no value's moves.
    static const uint16_t wide_forms[][2] = {
        { X86_FORM_VMOVDQA64_Y_YM256, X86_FORM_VMOVDQA64_X_XM128 },
        { X86_FORM_VMOVDQA64_Z_ZM512, X86_FORM_VMOVDQA64_X_XM128 },
        { X86_FORM_VMOVDQU32_Y_YM256, X86_FORM_VMOVDQU32_X_XM128 },
        { X86_FORM_VMOVDQU32_Z_ZM512, X86_FORM_VMOVDQU32_X_XM128 },
    };
    for (uint32_t r = 0; r < emitted->code.count; r += 1)
    {
        const X86Instruction record = records[r];
        const Tier1Origin kind = (Tier1Origin)(emitted->origins[r] >> BUSTER_TIER1_ORIGIN_SHIFT);
        const uint32_t value = emitted->origin_values[r];
        const bool wide = value < function->instruction_count && emitted->prepared.sizes[value] > 16;
        for (uint32_t k = 0; wide && (kind == TIER1_ORIGIN_MOVE || kind == TIER1_ORIGIN_EDGE) && k < BUSTER_ARRAY_LENGTH(wide_forms); k += 1)
        {
            if (record.form == wide_forms[k][0] && record.reg < 16 && (record.mode != BUSTER_X86_REGISTER_DIRECT || record.rm < 16))
            {
                records[r].form = wide_forms[k][1];
                const bool refused = tier1_masks_test_refused(test, plan, emitted);
                records[r] = record;
                test->vector_moves += 1;
                test->vector_moves_refused += refused;
                tier1_masks_test_expect(test, refused, "a vector's move narrowed to its xmm register's is refused (#1284)");
            }
        }
    }
    // The emitted code, as it was, still certified.
    tier1_masks_test_expect(test, emitted->row_placements == 0 || !tier1_masks_test_refused(test, plan, emitted), "the machine rows' code, put back, is certified");
}

// The code's certificate takes an asm body's code (#1839, tier1_cert_emitted_asm) and refuses it corrupted. For each asm
// body of the function, its code is certified as it is; then each mask's reload after it (a kmov from the frame, the
// restore of a mask live in a register the body writes) is made its register's move into itself, the mask lost; and each
// of its records gets another immediate, no longer the body's. Each must be refused; everything is put back after.
static void tier1_masks_test_asm_mutants(Tier1MasksTest* test, const Tier1SelectPlan* plan, Tier1EmitResult* emitted)
{
    const IrFunction* function = plan->function;
    X86Instruction* records = emitted->code.records;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        const bool body = !plan->covered[node] && plan->patterns[node].rule == TIER1_RULE_ASM;
        const uint32_t own = tier1_origin(TIER1_ORIGIN_NODE, node);
        uint32_t first = 0;
        while (body && first < emitted->code.count && emitted->origins[first] != own)
        {
            first += 1;
        }
        uint32_t end = first;
        while (body && end < emitted->code.count && emitted->origins[end] == own)
        {
            end += 1;
        }
        if (body)
        {
            Tier1CertEmittedStats stats = { 0 };
            const Tier1CertEmittedReason reason = tier1_certificate_emitted_check(plan, emitted, &test->output, &test->scratch, 0, &stats);
            test->asm_certified += reason == TIER1_CERT_EMITTED_OK;
            tier1_masks_test_expect(test, reason == TIER1_CERT_EMITTED_OK, "an asm body's code is certified (#1839)");
        }
        // The restores after the body, up to the next node's records.
        for (uint32_t r = end; body && r < emitted->code.count && tier1_cert_emitted_kind(emitted->origins[r]) == TIER1_ORIGIN_MOVE; r += 1)
        {
            const X86Instruction record = records[r];
            for (uint32_t width = 1; width <= 8; width *= 2)
            {
                const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
                if (record.form == forms->load && record.mode == BUSTER_X86_MEMORY_BASE && record.rm == emitted->frame.base_register)
                {
                    records[r] = (X86Instruction){ .form = forms->move, .reg = record.reg, .rm = record.reg };
                    const bool refused = tier1_masks_test_refused(test, plan, emitted);
                    records[r] = record;
                    test->asm_reloads += 1;
                    test->asm_reloads_refused += refused;
                    tier1_masks_test_expect(test, refused, "a mask live across an asm body left without its reload is refused (#1839)");
                }
            }
        }
        for (uint32_t r = first; body && r < end; r += 1)
        {
            const X86Instruction record = records[r];
            records[r].immediate += 1;
            const bool refused = tier1_masks_test_refused(test, plan, emitted);
            records[r] = record;
            test->asm_records += 1;
            test->asm_records_refused += refused;
            tier1_masks_test_expect(test, refused, "an asm body's record changed from the body's is refused (#1839)");
        }
    }
}

static void tier1_masks_test_path(Tier1MasksTest* test, const Tier1MasksFixture* fixture, const IrFunction* function, const IrDominance* dominance, bool vector, bool rematerialize, const uint8_t* inputs,
                                  const uint64_t* counts, const uint8_t* expected)
{
    const uint64_t output_mark = test->output.length;
    // The scans for vectors that selection and the CFG consumer make, which must take the requested path (#141's review):
    // a scalar run none by the vector path.
    const uint64_t scans = ir_scan_vector_paths;
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = tier1_select_plan(function, dominance, &test->output, &test->scratch, vector, &plan, &node);
    bool ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, fixture->name);
    if (!ok)
    {
        printf("test-tier1-masks: %s, %s path: selection error %u at node %u\n", fixture->name, vector ? "vector" : "scalar", (unsigned)selected, node);
    }
    const Tier1EmitOptions options = {
        .certify = true,
        .register_mask = tier1_liveness_register_mask(),
        .vector = vector,
        .rematerialize = rematerialize,
    };
    Tier1EmitResult emitted = { 0 };
    if (ok)
    {
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->forms, &test->output, &test->scratch, &emitted, &node);
        ok = tier1_masks_test_expect(test, error == TIER1_EMIT_OK && emitted.general, fixture->name);
        if (!ok)
        {
            printf("test-tier1-masks: %s, %s path: emission error %u at node %u\n", fixture->name, vector ? "vector" : "scalar", (unsigned)error, node);
        }
        tier1_masks_test_expect(test, vector ? !BUSTER_HOST_VECTOR || ir_scan_vector_paths > scans : ir_scan_vector_paths == scans,
                                "selection's and the CFG consumer's scans for vectors take the requested path: none the vector path on a scalar run");
    }
    if (ok)
    {
        tier1_masks_test_row_mutants(test, &plan, &emitted);
        tier1_masks_test_asm_mutants(test, &plan, &emitted);
    }
    if (ok)
    {
        // Every value a call stores, a mask's opmask row among them, is loaded back (#1116).
        ok = tier1_masks_test_expect(test, tier1_emit_test_saves_loaded(&emitted) && tier1_emit_test_saves_seen(&emitted), "every value a call stores is loaded back");
        ok = tier1_masks_test_expect(test, tier1_emit_test_general_rows_used(&emitted), "a general call-save row is a register some call stores (#177)") && ok;
        // The opmask class's allocation, by the independent checker; its moves through k0 and of masks.
        Tier1CfgCheck check;
        ok = tier1_masks_test_expect(test, tier1_cfg_verify(&emitted.cfg_input, &emitted.cfg, &test->scratch, &check), "the CFG checker accepts the masks' allocation");
        if (!ok)
        {
            printf("test-tier1-masks: %s: %s (block %u value %u position %u)\n", fixture->name, check.reason, check.block, check.value, check.position);
        }
        uint64_t scratch_moves = 0;
        for (uint32_t m = 0; m < emitted.cfg.move_count; m += 1)
        {
            const uint32_t k0 = BUSTER_TIER1_EMIT_MASK_LOCATION;
            const bool mask = emitted.cfg_input.classes[emitted.cfg.move_values[m]] == BUSTER_TIER1_ALLOCATE_MASK;
            test->mask_moves += mask;
            scratch_moves += mask && (emitted.cfg.move_sources[m] == k0 || emitted.cfg.move_destinations[m] == k0);
        }
        test->scratch_moves += scratch_moves;
        uint32_t mask_values = 0;
        for (uint32_t v = 0; v < emitted.cfg_input.value_count; v += 1)
        {
            mask_values += emitted.cfg_input.classes[v] == BUSTER_TIER1_ALLOCATE_MASK;
        }
        // A fixture of vectors alone (#141) has none.
        bool masked = false;
        for (uint32_t n = 0; n < function->instruction_count; n += 1)
        {
            masked |= function->type[n] >= IR_M8 && function->type[n] <= IR_M64;
        }
        tier1_masks_test_expect(test, !masked || mask_values != 0, "the fixture's masks are the opmask class's");
        tier1_masks_test_expect(test, !fixture->cycle || scratch_moves != 0, "the swap's cycle goes through k0");
        uint32_t vector_scratch_moves = 0;
        for (uint32_t m = 0; m < emitted.cfg.move_count; m += 1)
        {
            const uint32_t xmm15 = BUSTER_TIER1_EMIT_VECTOR_LOCATION + BUSTER_TIER1_SELECT_FLOAT_SCRATCH;
            vector_scratch_moves += emitted.cfg.move_sources[m] == xmm15 || emitted.cfg.move_destinations[m] == xmm15;
        }
        tier1_masks_test_expect(test, !fixture->vector_cycle || vector_scratch_moves != 0, "the vectors' swap's cycle goes through xmm15");
        tier1_masks_test_expect(test, !fixture->spills || (emitted.spill_stores != 0 && emitted.spill_loads != 0), "masks past k1 to k7 are spilled and reloaded");
        tier1_masks_test_expect(test, !fixture->calls || (emitted.call_stores != 0 && emitted.call_loads != 0), "masks live across a call are saved and reloaded");
        tier1_masks_test_expect(test, !fixture->borrows || (emitted.call_stores != 0 && emitted.call_loads != 0), "a machine row short of opmask registers borrows live masks' and reloads them");
        // A gather's and a complex multiplication's destination is none of their sources' registers (#1283's review),
        // which would be #UD: tier1_emit_cfg_row stages its result where the allocation gave the result such a register
        // (a dying index's or factor's), and the encoder's validation, which refuses the function otherwise, passes.
        uint32_t exclusive = 0;
        uint32_t distinct = 0;
        for (uint32_t r = 0; fixture->exclusive && r < emitted.code.count; r += 1)
        {
            const X86Instruction* record = &emitted.code.records[r];
            const X86FormDescription* description = x86_form_description(record->form);
            const bool memory = record->mode != BUSTER_X86_REGISTER_DIRECT;
            const bool vsib = memory && (description->rm & BUSTER_X86_OPERAND_VSIB) != 0;
            const bool complex = x86_form_has_distinct_destination(description);
            exclusive += vsib || complex ? 1 : 0;
            distinct += (vsib && record->reg != record->index) || (complex && record->reg != record->vvvv && (memory || record->reg != record->rm)) ? 1 : 0;
        }
        tier1_masks_test_expect(test, !fixture->exclusive || (exclusive != 0 && distinct == exclusive), "a gather's and a complex multiplication's destination is none of their sources' registers");
        tier1_masks_test_expect(test, x86_validate(emitted.code.records, emitted.code.count) == emitted.code.count, "every record the function emits is valid");
        const uint32_t row_moves = fixture->row_moves;
        if (fixture->rows != 0 && !tier1_masks_test_expect(test, emitted.machine_rows == fixture->rows && emitted.row_moves == row_moves, "a fixture's machine rows take the moves into and out of place it counts"))
        {
            printf("test-tier1-masks: %s, %s path: %u rows, %u moves around them (%u and %u expected)\n", fixture->name, vector ? "vector" : "scalar", emitted.machine_rows, emitted.row_moves, fixture->rows, row_moves);
        }
        // The masked comparisons' records (#1847): a vpcmp or a vcmpp under a writemask.
        uint32_t masked_compares = 0;
        for (uint32_t r = 0; fixture->masked_compares != 0 && r < emitted.code.count; r += 1)
        {
            const X86Instruction* record = &emitted.code.records[r];
            const char* mnemonic = x86_form_description(record->form)->mnemonic;
            const bool compare = strncmp(mnemonic, "vpcmp", 5) == 0 || strncmp(mnemonic, "vcmpp", 5) == 0;
            masked_compares += compare && (record->decorations & BUSTER_X86_DECORATION_MASK) != 0 ? 1 : 0;
        }
        if (fixture->masked_compares != 0 && !tier1_masks_test_expect(test, masked_compares == fixture->masked_compares, "a comparison ANDed with a mask is one compare under it as its writemask"))
        {
            printf("test-tier1-masks: %s, %s path: %u masked comparisons (%u expected)\n", fixture->name, vector ? "vector" : "scalar", masked_compares, fixture->masked_compares);
        }
        // A mask's load or store through a frame object: its node's kmov, its last record (after any transitions), at the
        // frame's base, as the object's lea's.
        uint32_t frame_accesses = 0;
        uint32_t frame_records = 0;
        for (uint32_t n = 0; n < function->instruction_count; n += 1)
        {
            const Tier1SelectPattern* pattern = &plan.patterns[n];
            const bool store = pattern->rule == TIER1_RULE_MASK_STORE_ADDRESS;
            const bool folded = plan.covered[n] == 0 && (pattern->rule == TIER1_RULE_MASK_LOAD_ADDRESS || store) && pattern->stack_object != BUSTER_IR_NONE;
            const uint32_t first = emitted.code.node_first[n];
            const uint32_t end = emitted.code.node_first[n + 1];
            const X86Instruction* record = folded && end > first ? &emitted.code.records[end - 1] : 0;
            const Tier1SelectMaskForms* forms = tier1_select_mask_forms(pattern->width);
            frame_accesses += folded;
            frame_records += record != 0 && record->form == (store ? forms->store : forms->load) && record->rm == (uint8_t)emitted.frame.base_register && record->mode == BUSTER_X86_MEMORY_BASE;
        }
        tier1_masks_test_expect(test, frame_accesses == fixture->frame_accesses && frame_records == frame_accesses, "masks through a frame object fold it into their kmov's memory operand");
        test->frame_accesses += frame_accesses;
        // No mask is recomputed (#179's class has no recipe): no mask node has a recipe, and no mask value is marked for
        // the allocator, whose dead definitions (#178's tier1_emit_cfg_dead_definition) read the marks.
        bool recipes = true;
        for (uint32_t n = 0; n < function->instruction_count; n += 1)
        {
            recipes &= tier1_select_mask_width(function->type[n]) == 0 || emitted.prepared.recipes.operations[n] == 0;
        }
        for (uint32_t v = 0; emitted.cfg_input.rematerializable != 0 && v < emitted.cfg_input.value_count; v += 1)
        {
            recipes &= emitted.cfg_input.classes[v] != BUSTER_TIER1_ALLOCATE_MASK || emitted.cfg_input.rematerializable[v] == 0;
        }
        tier1_masks_test_expect(test, recipes, "no mask has a recipe or is marked to be recomputed");
        test->recomputed += rematerialize ? emitted.rematerializations : 0;
        // #141's fixed registers beside the masks: a division by a register and one by a constant's reciprocal.
        uint32_t divides = 0;
        uint32_t reciprocals = 0;
        for (uint32_t n = 0; n < function->instruction_count; n += 1)
        {
            divides += plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_DIVIDE;
            reciprocals += plan.covered[n] == 0 && plan.patterns[n].rule == TIER1_RULE_DIVIDE_RECIPROCAL;
        }
        tier1_masks_test_expect(test, !fixture->divisions || (divides != 0 && reciprocals != 0), "the masks live across a division by a register and one by a reciprocal");
        test->mask_stores += emitted.spill_stores;
        test->mask_loads += emitted.spill_loads;
        test->call_saves += emitted.call_stores;
    }
    uint64_t mapped = 0;
    const bool print = test->print;
    test->print = print && vector;
    if (test->print && ok)
    {
        printf("test-tier1-masks: %s:\n", fixture->name);
    }
    uint8_t* machine = ok ? tier1_masks_test_load(test, &emitted, &mapped) : MAP_FAILED;
    test->print = print;
    ok = machine != MAP_FAILED;
    uint8_t* buffers = ok && test->native ? mmap(0, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (ok && test->native)
    {
        ok = tier1_masks_test_expect(test, buffers != MAP_FAILED, "the native runs' shared buffers");
    }
    if (ok && test->native)
    {
        memcpy(buffers, inputs, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS);
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            // An `avx` fixture's runs start from signaling NaNs in every ymm register, and must leave MXCSR's status flags
            // clear (#1293's review).
            bool clear = true;
            for (uint32_t input = 0; input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
            {
                uint8_t* bytes = buffers + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
                if (fixture->avx)
                {
                    clear &= tier1_masks_test_call_seeded(machine, bytes, counts[input]);
                }
                else
                {
                    tier1_masks_test_call(machine, bytes, counts[input]);
                }
            }
            _exit(clear ? 0 : 1);
        }
        ok = tier1_masks_test_expect(test, child > 0 && tier1_pipeline_wait(child), "the native code exits normally within its budget, raising no floating exception where it starts from signaling NaNs");
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
        {
            const uint8_t* actual = buffers + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            const uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            const bool equal = memcmp(actual, wanted, BUSTER_TIER1_MASKS_TEST_BUFFER) == 0;
            tier1_masks_test_expect(test, equal, "the native buffer is the interpreter's");
            for (uint32_t byte = 0; !equal && byte < BUSTER_TIER1_MASKS_TEST_BUFFER && test->failures < 20; byte += 1)
            {
                if (actual[byte] != wanted[byte])
                {
                    printf("test-tier1-masks: %s, %s path, input %u: byte %u is %02x, the interpreter's %02x\n", fixture->name, vector ? "vector" : "scalar", input, byte, actual[byte], wanted[byte]);
                    byte = BUSTER_TIER1_MASKS_TEST_BUFFER;
                }
            }
            test->native_runs += 1;
        }
    }
    if (buffers != MAP_FAILED)
    {
        munmap(buffers, BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS);
    }
    if (machine != MAP_FAILED)
    {
        munmap(machine, mapped);
    }
    tier1_masks_test_expect(test, test->scratch.length == 0, "selection and emission return the scratch arena");
    arena_pop_to(&test->output, output_mark);
}

static void tier1_masks_test_fixture(Tier1MasksTest* test, const Tier1MasksFixture* fixture)
{
    const uint64_t output_mark = test->output.length;
    // A fixture of AVX512-FP16's rows runs natively only where the host has it, and its model only then.
    const bool native = test->native;
    test->native = native && (!fixture->fp16 || test->native_fp16);
    test->native_fixtures += test->native ? 1 : 0;
    test->forms = fixture->fp16 ? test->fp16 : fixture->foundation ? test->foundation : fixture->avx ? test->avx : fixture->v3 ? test->v3 : fixture->v2 ? test->v2 :
                  fixture->baseline ? test->baseline : test->v4;
    IrFunction function;
    IrDominance dominance;
    bool parsed = tier1_masks_test_parse(test, fixture->text, &function, &dominance);
    // The asm body fixture's call becomes a body call of its records (#1839): no symbol, the body's index.
    for (uint32_t node = 0; parsed && fixture->asm_body && node < function.instruction_count; node += 1)
    {
        if (function.op[node] == IR_CALL)
        {
            function.calls[function.b[node]].body = 1;
            function.a[node] = 0;
            function.asm_bodies = &tier1_masks_test_asm_body;
            function.asm_body_count = 1;
        }
    }
    if (parsed)
    {
        uint8_t inputs[BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS];
        uint8_t expected[BUSTER_TIER1_MASKS_TEST_BUFFER * BUSTER_TIER1_MASKS_TEST_INPUTS];
        uint64_t counts[BUSTER_TIER1_MASKS_TEST_INPUTS];
        bool ok = true;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_INPUTS; input += 1)
        {
            uint8_t* bytes = inputs + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER;
            for (uint32_t byte = 0; byte < BUSTER_TIER1_MASKS_TEST_BUFFER; byte += 1)
            {
                // Every fourth input's masks all zeros or all ones, the others random.
                const uint64_t random = tier1_masks_test_next(test);
                bytes[byte] = input % 4 == 1 ? 0 : input % 4 == 2 ? 0xff : (uint8_t)random;
            }
            counts[input] = fixture->count != 0 ? 1 + tier1_masks_test_next(test) % fixture->count : 0;
            if (fixture->prepare != NULL)
            {
                fixture->prepare(bytes);
            }
            if (fixture->model != NULL && test->native)
            {
                fixture->model(bytes, counts[input], expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER);
            }
            else if (fixture->model == NULL)
            {
                ok = tier1_masks_test_interpret(test, &function, bytes, counts[input], expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_BUFFER);
            }
        }
        // Both paths, each with rematerialization (as the C driver's --tier1 runs) and without.
        for (uint32_t run = 0; ok && run < 4; run += 1)
        {
            tier1_masks_test_path(test, fixture, &function, &dominance, (run & 1) == 0, (run & 2) != 0, inputs, counts, expected);
        }
    }
    tier1_masks_test_expect(test, test->scratch.length == 0, "parsing returns the scratch arena");
    arena_pop_to(&test->output, output_mark);
    test->native = native;
    test->forms = test->v4;
}

// ir_function_has_vectors_path's vector path against its scalar twin and the expected answer: operation and type columns
// of every length to 200, each ending at a PROT_NONE page, of masks' operations and types, which hold no vector, then
// with a vector type or operation at each place, which it must find.
static void tier1_masks_test_scan(Tier1MasksTest* test)
{
    const long page_result = sysconf(_SC_PAGESIZE);
    const uint64_t page = page_result > 0 ? (uint64_t)page_result : 4096;
    uint8_t* columns[2] = { MAP_FAILED, MAP_FAILED };
    bool ready = true;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        columns[k] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = ready && columns[k] != MAP_FAILED && mprotect(columns[k] + page, page, PROT_NONE) == 0;
    }
    tier1_masks_test_expect(test, ready, "guarded vector scan columns");
    static const uint8_t vectors[6][2] = {
        { IR_ADD, IR_V4I32 },
        { IR_LOAD, IR_V8I32 },
        { IR_PHI, IR_V16I32 },
        { IR_VCONST, IR_M16 },
        { IR_VCMP, IR_M8 },
        { IR_VSELECT, IR_I32 },
    };
    static const uint8_t masks[4] = { IR_M8, IR_M16, IR_M32, IR_M64 };
    for (uint32_t count = 0; ready && count <= 200; count += 1)
    {
        uint8_t* op = columns[0] + page - count;
        uint8_t* type = columns[1] + page - count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            op[i] = i % 3 == 0 ? IR_AND : i % 3 == 1 ? IR_LOAD : IR_NOT;
            type[i] = masks[i % 4];
        }
        const IrFunction function = { .op = op, .type = type, .instruction_count = count };
        tier1_masks_test_expect(test, !ir_function_has_vectors_path(&function, true) && !ir_function_has_vectors_path(&function, false), "masks' operations hold no vector");
        for (uint32_t place = 0; place < count; place += 1)
        {
            const uint8_t saved_op = op[place];
            const uint8_t saved_type = type[place];
            for (uint32_t v = 0; v < 6; v += 1)
            {
                op[place] = vectors[v][0];
                type[place] = vectors[v][1];
                tier1_masks_test_expect(test, ir_function_has_vectors_path(&function, true) && ir_function_has_vectors_path(&function, false), "a vector type or operation at any place, the tail's last among them");
            }
            op[place] = saved_op;
            type[place] = saved_type;
        }
    }
    for (uint32_t k = 0; k < 2; k += 1)
    {
        if (columns[k] != MAP_FAILED)
        {
            munmap(columns[k], page * 2);
        }
    }
}

// tier1_select_mask_forms by width: each mask width's own kmov, and for any other width X86_FORM_NONE throughout, which no
// target allows, so that the CFG consumer refuses a record of a width no mask has at emission (TIER1_EMIT_TARGET).
static void tier1_masks_test_forms(Tier1MasksTest* test)
{
    static const uint16_t loads[9] = { 0, X86_FORM_KMOVB_K_M8, X86_FORM_KMOVW_K_M16, 0, X86_FORM_KMOVD_K_M32, 0, 0, 0, X86_FORM_KMOVQ_K_M64 };
    bool right = true;
    for (uint32_t width = 0; width <= 16; width += 1)
    {
        const Tier1SelectMaskForms* forms = tier1_select_mask_forms(width);
        const bool mask = width == 1 || width == 2 || width == 4 || width == 8;
        const uint16_t fields[9] = { forms->move, forms->load, forms->store, forms->from_general, forms->and_forms, forms->or_forms, forms->xor_forms, forms->xnor_forms, forms->not_forms };
        bool none = true;
        bool some = true;
        for (uint32_t f = 0; f < 9; f += 1)
        {
            none &= fields[f] == X86_FORM_NONE;
            some &= fields[f] != X86_FORM_NONE;
        }
        right &= mask ? some && forms->load == loads[width] : none;
    }
    tier1_masks_test_expect(test, right, "each mask width has its own kmov, and any other width none");
    tier1_masks_test_expect(test, !x86_target_form_allowed(test->v4, X86_FORM_NONE), "no target allows a record of no form");
}

// What the selector or the consumer must refuse: a vector result and a mask's select at selection, a target without the
// forms at emission, on both paths.
static void tier1_masks_test_refusals(Tier1MasksTest* test, const uint8_t* baseline, const uint8_t* foundation)
{
    const char* const texts[] = {
        tier1_masks_test_vector,
        tier1_masks_test_select,
        tier1_masks_test_fixtures[0].text,
        tier1_masks_test_fixtures[0].text,
        tier1_masks_test_words,
        tier1_masks_test_sse_compare,
    };
    // The target each is emitted for (selection refusals stop first), and whether it is refused.
    const uint8_t* const targets[] = {
        test->v4,
        test->v4,
        baseline,
        foundation,
        foundation,
        baseline,
    };
    const bool refused[] = {
        true,
        true,
        true,
        true,
        false,
        true,
    };
    const char* const names[] = {
        "a vector result is refused at selection",
        "a mask's select is refused at selection",
        "the baseline target has no opmask form",
        "AVX512F alone has no kandb, kmovd or kmovq",
        "AVX512F alone has kmovw and kandw",
        "the baseline refuses a vector comparison and selection, which have no SSE form",
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(texts); c += 1)
    {
        const uint64_t output_mark = test->output.length;
        IrFunction function;
        IrDominance dominance;
        if (tier1_masks_test_parse(test, texts[c], &function, &dominance))
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, path == 0, &plan, &node);
                const Tier1EmitOptions options = {
                    .certify = true,
                    .register_mask = tier1_liveness_register_mask(),
                    .vector = path == 0,
                };
                Tier1EmitResult emitted = { 0 };
                const Tier1EmitError error = selected == TIER1_SELECT_OK ? tier1_emit_function(&plan, &options, targets[c], &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_UNSUPPORTED;
                const bool selection = c < 2;
                const bool expected = selection ? selected == TIER1_SELECT_ERROR_UNSUPPORTED : refused[c] ? selected == TIER1_SELECT_OK && error == TIER1_EMIT_TARGET : selected == TIER1_SELECT_OK && error == TIER1_EMIT_OK;
                tier1_masks_test_expect(test, expected, names[c]);
                if (!expected)
                {
                    printf("test-tier1-masks: %s, %s path: selection %u, emission %u, node %u\n", names[c], path == 0 ? "vector" : "scalar", (unsigned)selected, (unsigned)error, node);
                }
                test->refused += refused[c] && expected;
            }
        }
        tier1_masks_test_expect(test, test->scratch.length == 0, "refusals return the scratch arena");
        arena_pop_to(&test->output, output_mark);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Tier 0's mask programs through the IR (#179)

#define BUSTER_TIER1_MASKS_TEST_PROGRAMS 64
#define BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS 4
#define BUSTER_TIER1_MASKS_TEST_IN (BUSTER_TIER0_TEST_MASK_SLOTS * 2 * 8)
#define BUSTER_TIER1_MASKS_TEST_OUT (BUSTER_TIER0_TEST_MASK_SLOTS * 8 * 2)

// The IR's interpreter on a typed program's functions (the program, symbol 1, calling its callee, symbol 2): `in` and
// `out` bound as memory, the outputs into `out`.
static bool tier1_masks_test_typed_interpret(Tier1MasksTest* test, const IrFunction* functions, const uint64_t* in, uint8_t* out)
{
    const IrInterpreterFunction entries[2] = {
        { .symbol = BUSTER_TIER0_TEST_MASK_ENTRY, .function = &functions[0] },
        { .symbol = BUSTER_TIER0_TEST_MASK_LEAF, .function = &functions[1] },
    };
    const InterpreterBinding bindings[2] = {
        { .symbol = BUSTER_TIER1_MASKS_TEST_BINDING, .bytes = (uint8_t*)(uintptr_t)in, .size = BUSTER_TIER1_MASKS_TEST_IN, .writable = false },
        { .symbol = BUSTER_TIER1_MASKS_TEST_BINDING + 1, .bytes = out, .size = BUSTER_TIER1_MASKS_TEST_OUT, .writable = true },
    };
    const IrInterpreterProgram program = {
        .functions = entries,
        .function_count = 2,
        .bindings = bindings,
        .binding_count = 2,
    };
    const InterpreterValue arguments[2] = {
        { .low = (uint64_t)(uintptr_t)in },
        { .low = (uint64_t)(uintptr_t)out },
    };
    const InterpreterResult result = ir_interpret(&program, BUSTER_TIER0_TEST_MASK_ENTRY, arguments, 2, 0, &test->execution);
    return result.status == INTERPRETER_OK;
}

// Whether two lowerings made the same function, column for column.
static bool tier1_masks_test_same_ir(const IrFunction* a, const IrFunction* b)
{
    const uint32_t count = a->instruction_count;
    return count == b->instruction_count && a->block_count == b->block_count && a->slot_count == b->slot_count && a->constant_count == b->constant_count &&
           memcmp(a->op, b->op, count) == 0 && memcmp(a->type, b->type, count) == 0 && memcmp(a->a, b->a, (uint64_t)count * sizeof(uint32_t)) == 0 &&
           memcmp(a->b, b->b, (uint64_t)count * sizeof(uint32_t)) == 0 && memcmp(a->c, b->c, (uint64_t)count * sizeof(uint32_t)) == 0 &&
           memcmp(a->block_offsets, b->block_offsets, ((uint64_t)a->block_count + 1) * sizeof(uint32_t)) == 0 &&
           memcmp(a->constants, b->constants, (uint64_t)a->constant_count * sizeof(IrConstant)) == 0;
}

typedef void (*Tier1MasksTestTypedEntry)(const uint64_t*, uint8_t*);

static __attribute__((no_sanitize("function"))) void tier1_masks_test_typed_call(const uint8_t* code, const uint64_t* in, uint8_t* out)
{
    Tier1MasksTestTypedEntry entry;
    memcpy(&entry, &code, sizeof(entry));
    entry(in, out);
}

// A typed program's two functions by Tier 1: each sized and encoded by both encoders (which must agree), one after the
// other, the program's calls resolved to its callee's start, and, where the host has AVX-512, run in a child on each
// input: its outputs must be the model's.
static void tier1_masks_test_typed_native(Tier1MasksTest* test, const char* name, Tier1EmitResult* emitted, const uint64_t* inputs, const uint8_t* expected)
{
    uint64_t starts[2] = { 0, 0 };
    uint64_t total = 0;
    for (uint32_t f = 0; f < 2; f += 1)
    {
        starts[f] = (total + 63) & ~UINT64_C(63);
        total = starts[f] + BUSTER_X86_ENCODER_CAPACITY(emitted[f].code.count);
    }
    const uint64_t mapped = (total + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ok = tier1_masks_test_expect(test, machine != MAP_FAILED, "the typed programs' code mapping");
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        Tier1SelectCode* code = &emitted[f].code;
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
        uint8_t* scalar = malloc(capacity);
        uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
        uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted[f].reference_count) * sizeof(uint32_t));
        ok = tier1_masks_test_expect(test, scalar != 0 && offsets != 0 && ends != 0, "the encoders' buffers");
        ok = ok && tier1_masks_test_expect(test, branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count).error == BRANCH_SIZING_OK &&
                                                     size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK &&
                                                     x86_target_validate(test->v4, code->records, code->count) == code->count,
                                           "a typed program's branches size and its records obey x86-64-v4");
        const int64_t size = ok ? x86_encode(code->records, code->count, machine + starts[f], capacity, 0, ends) : -1;
        const int64_t scalar_size = ok ? x86_encode_scalar(code->records, code->count, scalar, capacity, 0, 0) : -1;
        ok = ok && tier1_masks_test_expect(test, size > 0 && size == scalar_size && memcmp(machine + starts[f], scalar, (uint64_t)size) == 0, "both encoders make a typed program's bytes");
        for (uint32_t r = 0; ok && r < emitted[f].reference_count; r += 1)
        {
            const LinkReference* fixup = &emitted[f].references[r];
            const uint32_t distance = x86_fixup_distance(code->records[fixup->instruction].form, fixup->field);
            ok = tier1_masks_test_expect(test, fixup->kind == BUSTER_LINK_FIXUP_CALL && fixup->target == BUSTER_TIER0_TEST_MASK_LEAF && distance == 4, "a typed program's calls are its callee's");
            if (ok)
            {
                const int64_t end = (int64_t)starts[f] + ends[r];
                const int32_t relative = (int32_t)((int64_t)starts[1] - end);
                memcpy(machine + end - distance, &relative, sizeof(relative));
            }
        }
        test->records += code->count;
        free(scalar);
        free(offsets);
        free(ends);
    }
    ok = ok && tier1_masks_test_expect(test, mprotect(machine, mapped, PROT_READ | PROT_EXEC) == 0, "make the typed program executable");
    const uint64_t pair = BUSTER_TIER1_MASKS_TEST_IN + BUSTER_TIER1_MASKS_TEST_OUT;
    uint8_t* buffers = ok && test->native ? mmap(0, pair * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (ok && test->native && tier1_masks_test_expect(test, buffers != MAP_FAILED, "the typed runs' shared buffers"))
    {
        for (uint32_t input = 0; input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            memcpy(buffers + input * pair, inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8), BUSTER_TIER1_MASKS_TEST_IN);
            memset(buffers + input * pair + BUSTER_TIER1_MASKS_TEST_IN, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
        }
        fflush(0);
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            for (uint32_t input = 0; input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint8_t* base = buffers + input * pair;
                tier1_masks_test_typed_call(machine, (const uint64_t*)(void*)base, base + BUSTER_TIER1_MASKS_TEST_IN);
            }
            _exit(0);
        }
        ok = tier1_masks_test_expect(test, child > 0 && tier1_pipeline_wait(child), "a typed program's native code exits normally");
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            const bool same = memcmp(buffers + input * pair + BUSTER_TIER1_MASKS_TEST_IN, expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT, BUSTER_TIER1_MASKS_TEST_OUT) == 0;
            if (!tier1_masks_test_expect(test, same, "a typed program's native outputs are the model's"))
            {
                printf("test-tier1-masks: %s, input %u\n", name, input);
            }
            test->typed_runs += 1;
        }
    }
    if (buffers != MAP_FAILED)
    {
        munmap(buffers, pair * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    }
    if (machine != MAP_FAILED)
    {
        munmap(machine, mapped);
    }
}

// A typed program's two functions (the program and its callee, symbol 2) through the IR, on `inputs` against `expected`
// (BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS of each): lowered by both of ir_lower's typed-target paths, for x86-64-v4, into
// the same functions; the all-in-memory IR run by the IR's interpreter on each input; promoted by both SSA paths and run
// again; optimized (ir_optimize's SCCP, dead code and CFG cleanup) and run again; then planned and emitted by both of
// Tier 1's paths (with rematerialization, as --tier1 compiles), its allocation checked by tier1_cfg_verify, and run
// natively. A program that isn't `decodable` has a row the IR's decoders don't take, which both paths lower as a machine
// row (#141, a row call): the interpreter refuses it at every stage, having no model of x86 records, and Tier 1's native
// runs alone check it against the model, the row's records placed by tier1_emit_cfg_row. Each stage's output in an arena
// of its own (`arenas`: the promotions', the optimizer's and Tier 1's), which owns none of its input (ssa_ir_promote's and
// the others' rule), as the C driver's --tier1 gives each a step's. Returns whether it went through every stage, *machine
// whether its program had a machine row.
static bool tier1_masks_test_typed_pipeline(Tier1MasksTest* test, const char* name, const Tier0Function* functions, bool decodable, const uint64_t* inputs, const uint8_t* expected, Arena* arenas,
                                            bool* machine)
{
    uint8_t* outputs = malloc(BUSTER_TIER1_MASKS_TEST_OUT);
    bool ok = tier1_masks_test_expect(test, outputs != 0, "the typed pipeline's outputs");
    static const IrType results[2] = { IR_VOID, IR_I64 };
    *machine = false;
    // Lowered by both paths: the vector path's functions in lowered[0], the scalar twin's in lowered[1].
    IrFunction lowered[2][2];
    IrCheck checks[2][2];
    for (uint32_t path = 0; ok && path < 2; path += 1)
    {
        for (uint32_t f = 0; f < 2; f += 1)
        {
            checks[path][f] = path == 0 ? ir_lower_typed_target(&functions[f], results[f], 3, test->v4_features, &test->output, &test->scratch, &lowered[path][f])
                                        : ir_lower_typed_target_scalar(&functions[f], results[f], 3, test->v4_features, &test->output, &test->scratch, &lowered[path][f]);
        }
    }
    if (ok && !decodable)
    {
        *machine = checks[0][0].error == IR_OK && checks[1][0].error == IR_OK && lowered[0][0].row_count != 0 && lowered[1][0].row_count == lowered[0][0].row_count;
        tier1_masks_test_expect(test, *machine, "a row the IR's decoders don't take is the typed program's machine row, by both paths");
    }
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        ok = tier1_masks_test_expect(test, checks[0][f].error == IR_OK && checks[1][f].error == IR_OK, "both paths lower the typed program") &&
             tier1_masks_test_expect(test, tier1_masks_test_same_ir(&lowered[0][f], &lowered[1][f]), "both paths lower it into the same function");
        if (!ok)
        {
            printf("test-tier1-masks: %s, function %u: lowering errors %u at %u and %u at %u\n", name, f, (unsigned)checks[0][f].error, checks[0][f].instruction, (unsigned)checks[1][f].error,
                   checks[1][f].instruction);
        }
    }
    // The IR's interpreter on each stage: all in memory, promoted by each path, optimized.
    IrFunction promoted[2][2];
    IrFunction optimized[2];
    const IrFunction* stages[4] = { lowered[0], promoted[0], promoted[1], optimized };
    for (uint32_t path = 0; ok && path < 2; path += 1)
    {
        for (uint32_t f = 0; ok && f < 2; f += 1)
        {
            const IrCheck check = path == 0 ? ssa_ir_promote(&lowered[0][f], &arenas[0], &test->scratch, &promoted[path][f]) : ssa_ir_promote_scalar(&lowered[0][f], &arenas[0], &test->scratch, &promoted[path][f]);
            ok = tier1_masks_test_expect(test, check.error == IR_OK, "both SSA paths promote the typed program's masks");
            if (!ok)
            {
                printf("test-tier1-masks: %s, function %u, %s promotion: error %u at %u\n", name, f, path == 0 ? "vector" : "scalar", (unsigned)check.error, check.instruction);
            }
        }
    }
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        IrOptimizeResult result;
        const IrCheck check = ir_optimize(&promoted[0][f], &arenas[1], &test->scratch, ir_optimize_options(), &result);
        ok = tier1_masks_test_expect(test, check.error == IR_OK, "SCCP, dead code and CFG cleanup take the typed program's masks");
        optimized[f] = result.function;
    }
    for (uint32_t stage = 0; ok && stage < 4; stage += 1)
    {
        // A machine row's program: the interpreter refuses it, on the first input.
        for (uint32_t input = 0; ok && input < (*machine ? 1 : BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS); input += 1)
        {
            memset(outputs, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            const bool ran = tier1_masks_test_typed_interpret(test, stages[stage], inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8), outputs);
            ok = *machine ? tier1_masks_test_expect(test, !ran, "the IR's interpreter refuses a machine row at every stage") :
                            tier1_masks_test_expect(test, ran && memcmp(outputs, expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT, BUSTER_TIER1_MASKS_TEST_OUT) == 0,
                                                    "the IR's interpreter gives the model's outputs at every stage");
            if (!ok)
            {
                printf("test-tier1-masks: %s, stage %u, input %u: %s\n", name, stage, input, ran ? "the outputs differ" : "the interpreter refused");
            }
        }
    }
    // Tier 1 by both paths.
    for (uint32_t path = 0; ok && path < 2; path += 1)
    {
        const uint64_t mark = arenas[2].length;
        Tier1EmitResult emitted[2];
        for (uint32_t f = 0; ok && f < 2; f += 1)
        {
            IrDominance dominance;
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            ok = tier1_masks_test_dominance(test, &optimized[f], &dominance);
            const Tier1SelectError selected = ok ? tier1_select_plan(&optimized[f], &dominance, &arenas[2], &test->scratch, path == 0, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
            ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, "Tier 1 selects the typed program's masks");
            const Tier1EmitOptions options = {
                .certify = true,
                .register_mask = tier1_liveness_register_mask(),
                .vector = path == 0,
                .rematerialize = true,
            };
            memset(&emitted[f], 0, sizeof(emitted[f]));
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->v4, &arenas[2], &test->scratch, &emitted[f], &node) : TIER1_EMIT_OK;
            ok = ok && tier1_masks_test_expect(test, error == TIER1_EMIT_OK && emitted[f].general, "Tier 1's CFG consumer emits the typed program");
            if (!ok)
            {
                printf("test-tier1-masks: %s, function %u, %s path: selection %u, emission %u, at node %u (op %u)\n", name, f, path == 0 ? "vector" : "scalar", (unsigned)selected, (unsigned)error, node,
                       node < optimized[f].instruction_count ? optimized[f].op[node] : 0);
            }
            Tier1CfgCheck check;
            ok = ok && tier1_masks_test_expect(test, tier1_cfg_verify(&emitted[f].cfg_input, &emitted[f].cfg, &test->scratch, &check), "the CFG checker accepts the typed program's allocation");
            if (ok)
            {
                tier1_masks_test_row_mutants(test, &plan, &emitted[f]);
            }
        }
        if (ok)
        {
            tier1_masks_test_typed_native(test, name, emitted, inputs, expected);
        }
        arena_pop_to(&arenas[2], mark);
    }
    free(outputs);
    return ok;
}

// Tier 0's mask programs (tier0_mask_test.c's generator) through the IR (tier1_masks_test_typed_pipeline), against the
// generator's own model of the stream (tier0_test_mask_model). A program with a row the IR's decoders don't take (FROM's
// and TO's, an integer in a row; NUMBERED's, opmask registers by number) has it as a machine row (#141), which Tier 1's
// native runs alone check.
static void tier1_masks_test_typed(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    Tier0TestMaskTasks* tasks = malloc(sizeof(*tasks));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && tasks != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the typed programs' memory");
    tier0_test_mask_build_rows();
    uint64_t state = test->random | 1;
    for (uint32_t p = 0; ready && p < BUSTER_TIER1_MASKS_TEST_PROGRAMS; p += 1)
    {
        const uint64_t output_mark = test->output.length;
        const uint32_t shape = p % 4;
        program->rows = (shape & 1) != 0;
        program->objects = shape >= 2;
        tier0_test_mask_program(program, tasks, &state);
        char name[64];
        snprintf(name, sizeof(name), "typed program %u", p);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the typed program fits");
        bool decodable = true;
        for (uint32_t i = 0; i < program->count; i += 1)
        {
            const uint64_t row = program->values[i];
            const uint64_t kind = row % BUSTER_TIER0_TEST_MASK_ROW_KINDS;
            decodable &= program->kinds[i] != BUSTER_TYPED_X86 || (row < BUSTER_TIER0_TEST_MASK_ROW_NUMBERED && kind != BUSTER_TIER0_TEST_MASK_ROW_FROM && kind != BUSTER_TIER0_TEST_MASK_ROW_TO);
        }
        const Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            Tier0TestMaskReach reach;
            memset(&reach, 0, sizeof(reach));
            ok = tier1_masks_test_expect(test, tier0_test_mask_model(program, in, wanted, &reach), "the model runs the typed program");
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, decodable, inputs, expected, arenas, &machine);
        test->typed_programs += ok;
        test->typed_machine += machine;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the typed pipeline returns the scratch arena");
        arena_pop_to(&test->output, output_mark);
        arena_pop_to(&arenas[0], 0);
        arena_pop_to(&arenas[1], 0);
        arena_pop_to(&arenas[2], 0);
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(tasks);
    free(program);
}

// A front-end opmask rule's row's result (#179), as Intel's manual defines its instruction, on masks a and b of the
// rule's bytes (their bits past them ignored): KBINARY's of a and b (kunpck's: a's low half above b's), KSHIFT's of a by
// `count` (the immediate's low byte; 0 from the mask's bits on), KUNARY's of a, KMOV's a, and KTEST's flag (ktest's ZF
// where a & b is 0 and CF where ~a & b is; kortest's ZF where a | b is 0 and CF where it has every bit), 1 or 0.
static uint64_t tier1_masks_test_rule_model(const CheckX86LowerRule* rule, uint64_t count, uint64_t a, uint64_t b)
{
    static const uint16_t forms[][4] = {
        { X86_FORM_KANDB_K_K_K, X86_FORM_KANDW_K_K_K, X86_FORM_KANDD_K_K_K, X86_FORM_KANDQ_K_K_K },
        { X86_FORM_KANDNB_K_K_K, X86_FORM_KANDNW_K_K_K, X86_FORM_KANDND_K_K_K, X86_FORM_KANDNQ_K_K_K },
        { X86_FORM_KORB_K_K_K, X86_FORM_KORW_K_K_K, X86_FORM_KORD_K_K_K, X86_FORM_KORQ_K_K_K },
        { X86_FORM_KXORB_K_K_K, X86_FORM_KXORW_K_K_K, X86_FORM_KXORD_K_K_K, X86_FORM_KXORQ_K_K_K },
        { X86_FORM_KXNORB_K_K_K, X86_FORM_KXNORW_K_K_K, X86_FORM_KXNORD_K_K_K, X86_FORM_KXNORQ_K_K_K },
        { X86_FORM_KADDB_K_K_K, X86_FORM_KADDW_K_K_K, X86_FORM_KADDD_K_K_K, X86_FORM_KADDQ_K_K_K },
        { X86_FORM_NONE, X86_FORM_KUNPCKBW_K_K_K, X86_FORM_KUNPCKWD_K_K_K, X86_FORM_KUNPCKDQ_K_K_K },
        { X86_FORM_KNOTB_K_K, X86_FORM_KNOTW_K_K, X86_FORM_KNOTD_K_K, X86_FORM_KNOTQ_K_K },
        { X86_FORM_KSHIFTLB_K_K_IMM8, X86_FORM_KSHIFTLW_K_K_IMM8, X86_FORM_KSHIFTLD_K_K_IMM8, X86_FORM_KSHIFTLQ_K_K_IMM8 },
        { X86_FORM_KSHIFTRB_K_K_IMM8, X86_FORM_KSHIFTRW_K_K_IMM8, X86_FORM_KSHIFTRD_K_K_IMM8, X86_FORM_KSHIFTRQ_K_K_IMM8 },
        { X86_FORM_KTESTB_K_K, X86_FORM_KTESTW_K_K, X86_FORM_KTESTD_K_K, X86_FORM_KTESTQ_K_K },
        { X86_FORM_KORTESTB_K_K, X86_FORM_KORTESTW_K_K, X86_FORM_KORTESTD_K_K, X86_FORM_KORTESTQ_K_K },
    };
    const uint32_t bits = 8u * rule->bytes;
    const uint64_t all = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    const uint32_t shift = (uint32_t)(count & 0xff);
    a &= all;
    b &= all;
    uint32_t operation = BUSTER_ARRAY_LENGTH(forms);
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(forms); k += 1)
    {
        for (uint32_t w = 0; w < 4; w += 1)
        {
            operation = forms[k][w] != X86_FORM_NONE && forms[k][w] == rule->form ? k : operation;
        }
    }
    const bool carry = rule->fixed == CHECK_X86_CC_B;
    uint64_t result = a;
    switch (rule->family == CHECK_X86_LOWER_KMOV ? BUSTER_ARRAY_LENGTH(forms) : operation)
    {
    case 0: result = a & b; break;
    case 1: result = ~a & b; break;
    case 2: result = a | b; break;
    case 3: result = a ^ b; break;
    case 4: result = ~(a ^ b); break;
    case 5: result = a + b; break;
    case 6: result = (a & (all >> bits / 2)) << bits / 2 | (b & (all >> bits / 2)); break;
    case 7: result = ~a; break;
    case 8: result = shift >= bits ? 0 : a << shift; break;
    case 9: result = shift >= bits ? 0 : a >> shift; break;
    case 10: result = (carry ? ~a & b & all : a & b) == 0; break;
    case 11: result = carry ? (a | b) == all : (a | b) == 0; break;
    default: break;
    }
    return rule->family == CHECK_X86_LOWER_KTEST ? result : result & all;
}

// The front end's opmask rows through Tier 0 and the IR to Tier 1 (#179): every rule of the families whose rows take and
// give masks alone (KBINARY's, ktestzdi's among them, KSHIFT's at counts around every width, KUNARY's and KMOV's) and
// KTEST's, at x86-64-v4, each row as check_x86_lower_masks moves it into the mask class, as the program's only row: 8
// statements, each the row of the masks of two input words (converted from their U64s, which truncates them) stored into
// an output slot, the mask's bytes or a test's int. Tier 0 compiles it by both paths and, where the host has AVX-512,
// runs it: the processor's instructions are the C model's oracle (tier1_masks_test_rule_model). Then
// tier1_masks_test_typed_pipeline takes it through the IR's interpreter, SSA, the optimizer and Tier 1, natively, against
// the model too.
static void tier1_masks_test_builtins(Tier1MasksTest* test)
{
    static const uint64_t counts[] = { 0, 1, 3, 7, 8, 15, 16, 31, 32, 63, 64, 255 };
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* image = malloc(BUSTER_TIER0_TEST_MASK_CODE);
    Tier0TestMaskShared* shared = malloc(sizeof(*shared));
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    Arena tier0 = arena_reserve(64u << 20);
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && image != 0 && shared != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0 &&
                                                   tier0.base != 0,
                                         "the front end's rows' memory");
    // Tier 0's templates, which its compile and check_x86_lower_masks's row check (tier0_operation_valid) take.
    ready = ready && tier1_masks_test_expect(test, tier0_initialize(), "Tier 0's tables");
    uint64_t state = test->random ^ UINT64_C(0x9e3779b97f4a7c15);
    uint32_t taken = 0;
    uint32_t native = 0;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool shift = rule->family == CHECK_X86_LOWER_KSHIFT;
        const bool masks = rule->family == CHECK_X86_LOWER_KBINARY || shift || rule->family == CHECK_X86_LOWER_KUNARY || rule->family == CHECK_X86_LOWER_KMOV ||
                           rule->family == CHECK_X86_LOWER_KTEST;
        for (uint32_t c = 0; masks && c < (shift ? BUSTER_ARRAY_LENGTH(counts) : 1); c += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->builtin_attempts += 1;
            Tier0Operation row;
            const bool made = check_x86_lower_row_of(&target, rule, counts[c], 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &target);
            char name[128];
            snprintf(name, sizeof(name), "%s (count %" PRIu64 ")", rule->name, counts[c]);
            bool ok = tier1_masks_test_expect(test, made, "the front end's opmask row, in the mask class at x86-64-v4");
            // The program: 8 statements of the row, then its return.
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            const bool test_row = row.result_class != BUSTER_TYPED_CLASS_MASK;
            for (uint32_t k = 0; ok && k < 8; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k);
                for (uint32_t j = 0; j < row.operand_count; j += 1)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 2 * k + j);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, row.operand_bytes[j]);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, test_row ? 0 : row.result_bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the front end's row's program fits");
            Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
            functions[0].operations = &row;
            functions[0].operation_count = 1;
            // The inputs, and the model's outputs: each slot's result in its first bytes (a test's int's four), the
            // rest as the buffer was.
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
                uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
                tier0_test_mask_inputs(in, &state);
                memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
                for (uint32_t k = 0; k < 8; k += 1)
                {
                    const uint64_t value = tier1_masks_test_rule_model(rule, counts[c], in[2 * k], in[2 * k + 1]);
                    memcpy(wanted + 8 * k, &value, test_row ? 4 : rule->bytes);
                }
            }
            // Tier 0's code by both paths, run natively: the instructions the model describes.
            Tier0TestTally tally = { 0 };
            const uint32_t length = ok ? tier0_test_mask_build(&tally, name, functions, 2, &tier0, image) : 0;
            ok = tier1_masks_test_expect(test, length != 0 && tally.failures == 0, "Tier 0 compiles the front end's row by both paths");
            for (uint32_t input = 0; ok && test->native && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                memset(shared, 0, sizeof(*shared));
                memcpy(shared->in, inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8), BUSTER_TIER1_MASKS_TEST_IN);
                memset(shared->out, 0xa5, sizeof(shared->out));
                const bool ran = tier0_test_mask_native(image, length, shared);
                const bool same = ran && memcmp(shared->out, expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT, BUSTER_TIER1_MASKS_TEST_OUT) == 0;
                if (!tier1_masks_test_expect(test, same, "Tier 0's code of the front end's row gives the model's outputs natively"))
                {
                    printf("test-tier1-masks: %s, input %u: %s\n", name, input, ran ? "the outputs differ" : "the run failed");
                }
                native += same;
            }
            arena_pop_to(&tier0, 0);
            bool refused = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &refused);
            taken += ok;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the front end's rows return the scratch arena");
            arena_pop_to(&test->output, output_mark);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&arenas[k], 0);
            }
        }
    }
    test->builtin_rows = taken;
    test->builtin_native = native;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    arena_release(&tier0);
    free(shared);
    free(image);
    free(expected);
    free(inputs);
    free(program);
}

// The C model of a comparison into a mask (#1623): `lanes` lanes of the rule's elements of `a` and `b` compared by `code`,
// the immediate clang takes, under `writemask`: an integer compare's vpcmp predicate (==, <, <=, false, !=, >=, >, true;
// signed or not as the rule's), and a float compare's as its low 4 bits say (LLVM's selection keeps no more): EQ_OQ,
// LT_OS, LE_OS, UNORD_Q, NEQ_UQ, NLT_US, NLE_US, ORD_Q, EQ_UQ, NGE_US, NGT_US, FALSE_OQ, NEQ_OQ, GE_OS, GT_OS and
// TRUE_UQ, by C's comparisons of the lanes widened to double, NaNs unordered.
static uint64_t tier1_masks_test_compare_model(const CheckX86LowerRule* rule, uint32_t code, const uint8_t* a, const uint8_t* b, uint64_t writemask)
{
    const uint32_t element = rule->element;
    const uint32_t lanes = rule->bytes / element;
    uint64_t result = 0;
    for (uint32_t lane = 0; lane < lanes; lane += 1)
    {
        bool bit = false;
        if (rule->form != X86_FORM_NONE)
        {
            double x = 0;
            double y = 0;
            if (element == 4)
            {
                float fx = 0;
                float fy = 0;
                memcpy(&fx, a + 4 * lane, 4);
                memcpy(&fy, b + 4 * lane, 4);
                x = fx;
                y = fy;
            }
            else
            {
                memcpy(&x, a + 8 * lane, 8);
                memcpy(&y, b + 8 * lane, 8);
            }
            const bool unordered = x != x || y != y;
            switch (code & 15)
            {
            case 0: bit = x == y; break;
            case 1: bit = x < y; break;
            case 2: bit = x <= y; break;
            case 3: bit = unordered; break;
            case 4: bit = x != y; break;
            case 5: bit = !(x < y); break;
            case 6: bit = !(x <= y); break;
            case 7: bit = !unordered; break;
            case 8: bit = unordered || x == y; break;
            case 9: bit = !(x >= y); break;
            case 10: bit = !(x > y); break;
            case 11: bit = false; break;
            case 12: bit = !unordered && x != y; break;
            case 13: bit = x >= y; break;
            case 14: bit = x > y; break;
            default: bit = true; break;
            }
        }
        else
        {
            uint64_t ux = 0;
            uint64_t uy = 0;
            memcpy(&ux, a + element * lane, element);
            memcpy(&uy, b + element * lane, element);
            const uint32_t shift = 64 - 8 * element;
            const int64_t sx = (int64_t)(ux << shift) >> shift;
            const int64_t sy = (int64_t)(uy << shift) >> shift;
            const bool less = rule->is_signed ? sx < sy : ux < uy;
            const bool equal = ux == uy;
            switch (code & 7)
            {
            case 0: bit = equal; break;
            case 1: bit = less; break;
            case 2: bit = less || equal; break;
            case 3: bit = false; break;
            case 4: bit = !equal; break;
            case 5: bit = !less; break;
            case 6: bit = !less && !equal; break;
            default: bit = true; break;
            }
        }
        result |= (uint64_t)bit << lane;
    }
    return result & writemask;
}

// The front end's comparisons into masks through Tier 0 and the IR to Tier 1 (#1623): every COMPARE rule of integer and
// packed float lanes (not FP16's, whose lanes the IR hasn't, nor a scalar compare's) at every immediate clang takes, a
// float's as LLVM selects it, each row as check_x86_lower_masks moves it into the mask class, as the program's only row:
// 8 statements, each the row of two vectors loaded from the input buffer (the last's second at its first's place, every
// lane equal), its writemask a constant of every bit, as clang's intrinsics without a mask give it, or, in the odd
// statements, an input word, its mask stored into an output slot. Tier 0 compiles it by both paths and, where the host
// has AVX-512, runs it against the C model (tier1_masks_test_compare_model). Then tier1_masks_test_typed_pipeline takes
// it through the IR: a row of an IR predicate as the IR's comparison (ir_simd_typed_compare_mask), which the IR's
// interpreter runs at every stage, and any other as its machine row; and through Tier 1 natively, against the model too.
static void tier1_masks_test_compares(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* image = malloc(BUSTER_TIER0_TEST_MASK_CODE);
    Tier0TestMaskShared* shared = malloc(sizeof(*shared));
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    Arena tier0 = arena_reserve(64u << 20);
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && image != 0 && shared != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0 &&
                                                   tier0.base != 0,
                                         "the comparisons' memory");
    uint64_t state = test->random ^ UINT64_C(0x6a09e667f3bcc909);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool floating = rule->form != X86_FORM_NONE;
        const bool taken = rule->family == CHECK_X86_LOWER_COMPARE && (!floating || (rule->fixed == CHECK_X86_PREDICATE_FCMP && rule->element >= 4));
        for (uint32_t code = 0; taken && code < (floating ? 32u : 8u); code += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->compare_attempts += 1;
            Tier0Operation row;
            const bool made = check_x86_lower_row_of(&target, rule, code, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &target);
            char name[128];
            snprintf(name, sizeof(name), "%s (code %u)", rule->name, code);
            bool ok = tier1_masks_test_expect(test, made && row.operand_count == 3 && row.result_class == BUSTER_TYPED_CLASS_MASK, "the front end's comparison row, in the mask class at x86-64-v4");
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            for (uint32_t k = 0; ok && k < 8; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 2 * k);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, rule->bytes);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k == 7 ? 2 * k : 2 * k + 8);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, rule->bytes);
                if ((k & 1) != 0)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                }
                else
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, row.operand_bytes[2]);
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, row.result_bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the comparison's program fits");
            Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
            functions[0].operations = &row;
            functions[0].operation_count = 1;
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
                uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
                tier0_test_mask_inputs(in, &state);
                memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
                for (uint32_t k = 0; k < 8; k += 1)
                {
                    const uint8_t* a = (const uint8_t*)in + 16 * k;
                    const uint8_t* b = (const uint8_t*)in + (k == 7 ? 16 * k : 16 * k + 64);
                    const uint64_t value = tier1_masks_test_compare_model(rule, code, a, b, (k & 1) != 0 ? in[k] : UINT64_MAX);
                    memcpy(wanted + 8 * k, &value, row.result_bytes);
                }
            }
            Tier0TestTally tally = { 0 };
            const uint32_t length = ok ? tier0_test_mask_build(&tally, name, functions, 2, &tier0, image) : 0;
            ok = tier1_masks_test_expect(test, length != 0 && tally.failures == 0, "Tier 0 compiles the front end's comparison by both paths");
            for (uint32_t input = 0; ok && test->native && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                memset(shared, 0, sizeof(*shared));
                memcpy(shared->in, inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8), BUSTER_TIER1_MASKS_TEST_IN);
                memset(shared->out, 0xa5, sizeof(shared->out));
                const bool ran = tier0_test_mask_native(image, length, shared);
                const bool same = ran && memcmp(shared->out, expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT, BUSTER_TIER1_MASKS_TEST_OUT) == 0;
                if (!tier1_masks_test_expect(test, same, "Tier 0's code of the front end's comparison gives the model's masks natively"))
                {
                    printf("test-tier1-masks: %s, input %u: %s\n", name, input, ran ? "the outputs differ" : "the run failed");
                }
                test->compare_native += same;
            }
            arena_pop_to(&tier0, 0);
            // The IR's predicates, written out: vpcmp's but false and true; a float's EQ_OQ, LT_OS, LE_OS, NEQ_UQ, GE_OS
            // and GT_OS in the low 4 bits.
            const uint32_t low = code & 15;
            const bool decodable = floating ? low == 0 || low == 1 || low == 2 || low == 4 || low == 13 || low == 14 : code != 3 && code != 7;
            bool machine = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, decodable, inputs, expected, arenas, &machine);
            test->compare_rows += ok;
            test->compare_decoded += ok && decodable;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the comparisons return the scratch arena");
            arena_pop_to(&test->output, output_mark);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&arenas[k], 0);
            }
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    arena_release(&tier0);
    free(shared);
    free(image);
    free(expected);
    free(inputs);
    free(program);
}

// The front end's width changes through the IR to Tier 1 (#1840): every insert rule (BINARY_IMMEDIATE's vinsert forms) and
// masked extract rule (EXTRACT_LANE's, in the mask class) at each of its parts, and the casts' rows of no record at every
// pair of widths, each as the program's only row, in 4 statements: of the input buffer's vectors k and k + 1 (modulo 4,
// of the row's operands' bytes), an extract's writemask the input word k in the odd statements and a constant of every
// lane in the even ones, as clang's intrinsics without a mask give it, the result stored into the output's vector k.
// tier1_masks_test_typed_pipeline takes each through the IR as its extract, widen or insert (and an extract's vselect),
// which the interpreter runs at every stage, and Tier 1 natively, against a C model of the bytes. Three round trips, a
// cast of a wide vector to a narrower width and back, check that a widen zeroes the bytes past its source even where its
// register held the wide vector's (the narrower value's extract of part 0 moves nothing). Tier 0 doesn't run them here,
// as the selects' (tier1_masks_test_selects).
static void tier1_masks_test_widths(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the width changes' memory");
    uint64_t state = test->random ^ UINT64_C(0x3c6ef372fe94f82b);
    // The rules' cases, then the 6 casts' (case past the rules: a pair of widths), then the 3 round trips'.
    const uint32_t rules = BUSTER_ARRAY_LENGTH(check_x86_lower_rules);
    for (uint32_t i = 0; ready && i < rules + 9; i += 1)
    {
        const CheckX86LowerRule* rule = i < rules ? check_x86_lower_rules + i : 0;
        const char* mnemonic = rule != 0 && rule->form != X86_FORM_NONE ? x86_form_description(rule->form)->mnemonic : "";
        const bool insert = rule != 0 && rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE && rule->other != 0 && strncmp(mnemonic, "vinsert", 7) == 0 &&
                            strcmp(mnemonic, "vinsertps") != 0;
        const bool extract = rule != 0 && rule->family == CHECK_X86_LOWER_EXTRACT_LANE;
        const bool cast = rule == 0;
        const bool round = i >= rules + 6;
        static const uint8_t pairs[9][2] = { { 16, 32 }, { 16, 64 }, { 32, 64 }, { 32, 16 }, { 64, 16 }, { 64, 32 }, { 64, 16 }, { 32, 16 }, { 64, 32 } };
        const uint32_t parts = insert || extract ? (uint32_t)rule->bytes / rule->other : 1;
        for (uint32_t code = 0; (insert || extract || cast) && code < parts; code += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->width_attempts += 1;
            Tier0Operation rows[2] = { 0 };
            Tier0Operation* const row_pointer = rows;
            bool made = true;
            char name[128];
            for (uint32_t r = 0; cast && r < 2; r += 1)
            {
                rows[r].result_class = BUSTER_TYPED_CLASS_VECTOR;
                rows[r].result_bytes = pairs[i - rules][1 - r];
                rows[r].operand_count = 1;
                rows[r].operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
                rows[r].operand_bytes[0] = pairs[i - rules][r];
            }
            if (cast)
            {
                snprintf(name, sizeof(name), round ? "a cast of %u bytes to %u and back" : "a cast of %u bytes to %u", rows[0].operand_bytes[0], rows[0].result_bytes);
            }
            else
            {
                made = check_x86_lower_row_of(&target, rule, code, 4, 0, false, row_pointer) == NULL && (!extract || check_x86_lower_masks(row_pointer, &target));
                snprintf(name, sizeof(name), "%s (part %u)", rule->name, code);
            }
            const Tier0Operation* const last = rows + (round ? 1 : 0);
            const uint32_t bytes = last->result_bytes;
            const uint32_t a_bytes = rows[0].operand_bytes[0];
            const uint32_t b_bytes = rows[0].operand_count > 1 ? rows[0].operand_bytes[1] : 0;
            // A round trip's bytes of the source vector it keeps: the narrower width's.
            const uint32_t narrow = round ? rows[0].result_bytes : bytes;
            bool ok = tier1_masks_test_expect(test, made && last->result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's width change's row");
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            for (uint32_t k = 0; ok && k < 4; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * a_bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, a_bytes);
                if (b_bytes != 0)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + 1) * b_bytes / 8 % 32);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, b_bytes);
                }
                if (extract && (k & 1) != 0)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                }
                else if (extract)
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
                }
                if (extract)
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, rows[0].operand_bytes[2]);
                }
                for (uint32_t r = 0; r < (round ? 2u : 1u); r += 1)
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_X86, rows[r].result_class, r);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, last->result_class, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the width change's program fits");
            Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
            functions[0].operations = rows;
            functions[0].operation_count = round ? 2 : 1;
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
                uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
                tier0_test_mask_inputs(in, &state);
                memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    const uint8_t* a = (const uint8_t*)in + k * a_bytes % 256;
                    const uint8_t* b = (const uint8_t*)in + (k + 1) * b_bytes % 256;
                    const uint64_t mask = extract && (k & 1) != 0 ? in[k] : UINT64_MAX;
                    uint8_t* out = wanted + k * bytes;
                    for (uint32_t byte = 0; byte < bytes; byte += 1)
                    {
                        const uint32_t start = code * (insert ? b_bytes : bytes);
                        const bool inserted = insert && byte >= start && byte < start + b_bytes;
                        const bool kept = !extract || ((mask >> (byte / rule->element)) & 1) != 0;
                        out[byte] = insert                     ? (inserted ? b[byte - start] : a[byte]) :
                                    extract                    ? (kept ? a[start + byte] : b[byte]) :
                                    byte < narrow && byte < a_bytes ? a[byte] :
                                                                 0;
                    }
                }
            }
            bool machine = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
            test->width_rows += ok;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the width changes return the scratch arena");
            arena_pop_to(&test->output, output_mark);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&arenas[k], 0);
            }
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// A C model of a front-end permute's x86 instruction (#1840), from Intel's manual, lane by lane: `out` of `bytes` bytes, of
// table or first source a and index, control or second source b, its elements `element` bytes and its immediate `code`.
// vpermb to vpermq: each element is a's element at the index in b's same element, modulo the elements; vpshufb: each
// byte is 0 where b's byte is negative, else a's byte in the same 16 bytes at b's byte's low 4 bits; valignd and valignq:
// the elements of b then a, from the immediate modulo the elements on; vshufi32x4 and vshufi64x2: each 16-byte lane of the
// result's low half is a's and of its high half b's, the one the immediate's next 1 (32 bytes) or 2 (64 bytes) bits name.
static void tier1_masks_test_permute_model(uint32_t kind, uint32_t element, uint32_t bytes, uint32_t code, const uint8_t* a, const uint8_t* b, uint8_t* out)
{
    const uint32_t elements = bytes / element;
    if (kind == 0 || kind == 2)
    {
        for (uint32_t e = 0; e < elements; e += 1)
        {
            uint64_t index = 0;
            memcpy(&index, b + e * element, element);
            const uint32_t from = kind == 0 ? (uint32_t)(index % elements) : e + code % elements;
            const uint8_t* source = kind == 0 ? a + from * element : from < elements ? b + from * element : a + (from - elements) * element;
            memcpy(out + e * element, source, element);
        }
    }
    else if (kind == 1)
    {
        for (uint32_t byte = 0; byte < bytes; byte += 1)
        {
            out[byte] = (int8_t)b[byte] < 0 ? 0 : a[(byte & ~15u) + (b[byte] & 15u)];
        }
    }
    else
    {
        const uint32_t lanes = bytes / 16;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint32_t selected = lanes == 2 ? (code >> lane) & 1 : (code >> (2 * lane)) & 3;
            memcpy(out + 16 * lane, (lane < lanes / 2 ? a : b) + 16 * selected, 16);
        }
    }
}

// The front end's permutes through the IR to Tier 1 (#1840): every rule of vpermw, vpermd, vpermq, vpshufb, valignd,
// valignq, vshufi32x4 and vshufi64x2 that the decoder takes (vpermb's need AVX512VBMI, which the pipeline's x86-64-v4
// hasn't: test-ir-simd decodes them), at 8 immediates for the ones that take one, as the
// program's only row in 4 statements of the input buffer's vectors k and k + 1 (modulo 4), stored into the output's
// vector k; tier1_masks_test_typed_pipeline takes each through the IR as its permute, which the interpreter runs at
// every stage, and Tier 1 natively, against tier1_masks_test_permute_model. Tier 0 doesn't run them here, as the selects'.
static void tier1_masks_test_permutes(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the permutes' memory");
    uint64_t state = test->random ^ UINT64_C(0xa54ff53a5f1d36f1);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const char* mnemonic = rule->form != X86_FORM_NONE ? x86_form_description(rule->form)->mnemonic : "";
        const bool binary = rule->family == CHECK_X86_LOWER_BINARY && rule->other == 0;
        const bool immediate = rule->family == CHECK_X86_LOWER_BINARY_IMMEDIATE && rule->other == 0;
        const uint32_t kind = binary && rule->swap && (strcmp(mnemonic, "vpermw") == 0 ||
                                                       (strcmp(mnemonic, "vpermd") == 0 && rule->bytes >= 32) || (strcmp(mnemonic, "vpermq") == 0 && rule->bytes == 64)) ? 0 :
                              binary && !rule->swap && strcmp(mnemonic, "vpshufb") == 0                                                                                   ? 1 :
                              immediate && (strcmp(mnemonic, "valignd") == 0 || strcmp(mnemonic, "valignq") == 0)                                                           ? 2 :
                              immediate && rule->bytes >= 32 && (strcmp(mnemonic, "vshufi32x4") == 0 || strcmp(mnemonic, "vshufi64x2") == 0)                               ? 3 :
                                                                                                                                                                              4;
        for (uint32_t code = 0; kind < 4 && code < (kind >= 2 ? 8u : 1u); code += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->permute_attempts += 1;
            Tier0Operation row;
            // A shuffle of lanes' immediates past the 8 low ones: every lane of each half named.
            const uint32_t immediate_value = kind == 3 ? (code * 0x5b + 0x1e) & 0xff : code;
            const bool made = check_x86_lower_row_of(&target, rule, immediate_value, 4, 0, false, &row) == NULL;
            char name[128];
            snprintf(name, sizeof(name), "%s (immediate %u)", rule->name, immediate_value);
            const uint32_t bytes = rule->bytes;
            bool ok = tier1_masks_test_expect(test, made && row.operand_count == 2 && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's permute's row");
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            for (uint32_t k = 0; ok && k < 4; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + 1) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the permute's program fits");
            Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
            functions[0].operations = &row;
            functions[0].operation_count = 1;
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
                uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
                tier0_test_mask_inputs(in, &state);
                memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    tier1_masks_test_permute_model(kind, rule->element, bytes, immediate_value, (const uint8_t*)in + k * bytes % 256, (const uint8_t*)in + (k + 1) * bytes % 256,
                                                   wanted + k * bytes);
                }
            }
            bool machine = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
            test->permute_rows += ok;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the permutes return the scratch arena");
            arena_pop_to(&test->output, output_mark);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&arenas[k], 0);
            }
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The front end's shifts by an int through the IR to Tier 1 (#1840): every CHECK_X86_LOWER_SHIFT rule, as the program's
// only row in 4 statements of the input buffer's vector k shifted by a count, stored into the output's vector k, twice:
// the count an I32 loaded from the input's last slot (5, the lanes' bits less 1, their bits, and 65537, which a 16-bit
// lane's count would wrap to 1, one per input), which Tier 1 moves by vmovd; and a constant count per statement (1, the
// lanes' bits less 1, their bits plus 1, and 300, past an immediate's 255), which it folds into the immediate form.
// tier1_masks_test_typed_pipeline takes each through the IR as its shift, which the interpreter runs at every stage, and
// Tier 1 natively, against C's shifts of each lane by the count read unsigned, 0 or the sign past the lane's bits.
static void tier1_masks_test_shifts(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the shifts' memory");
    uint64_t state = test->random ^ UINT64_C(0x3c6ef372fe94f82b);
    const uint32_t count_slot = BUSTER_TIER1_MASKS_TEST_IN / 8 - 1;
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const char* name = rule->name;
        const size_t prefix = sizeof("__builtin_ia32_ps") - 1;
        if (rule->family != CHECK_X86_LOWER_SHIFT || strncmp(name, "__builtin_ia32_ps", prefix) != 0 || strlen(name) < prefix + 3)
        {
            continue;
        }
        const uint32_t kind = name[prefix] == 'l' ? 0 : name[prefix + 1] == 'l' ? 1 : 2;
        const uint32_t size = name[prefix + 2] == 'w' ? 2 : name[prefix + 2] == 'd' ? 4 : 8;
        const uint32_t bits = 8 * size;
        const uint32_t loaded[4] = { 5, bits - 1, bits, 65537 };
        const uint32_t constants[4] = { 1, bits - 1, bits + 1, 300 };
        for (uint32_t constant = 0; constant < 2; constant += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->shift_attempts += 1;
            Tier0Operation row;
            const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL;
            char label[128];
            snprintf(label, sizeof(label), "%s (%s counts)", name, constant != 0 ? "constant" : "loaded");
            const uint32_t bytes = rule->bytes;
            bool ok = tier1_masks_test_expect(test, made && row.operand_count == 2 && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's shift's row");
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            for (uint32_t k = 0; ok && k < 4; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                if (constant != 0)
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, constants[k]);
                }
                else
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, count_slot);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_I32, 0);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the shift's program fits");
            Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
            functions[0].operations = &row;
            functions[0].operation_count = 1;
            for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
            {
                uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
                uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
                tier0_test_mask_inputs(in, &state);
                // The loaded count's slot: its low 32 bits the count, its high ones what the I32 load doesn't read.
                in[count_slot] = (in[count_slot] & UINT64_C(0xffffffff00000000)) | loaded[input % 4];
                memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
                for (uint32_t k = 0; k < 4; k += 1)
                {
                    const uint8_t* a = (const uint8_t*)in + k * bytes % 256;
                    const uint32_t c = constant != 0 ? constants[k] : loaded[input % 4];
                    for (uint32_t lane = 0; lane < bytes / size; lane += 1)
                    {
                        uint64_t raw = 0;
                        memcpy(&raw, a + lane * size, size);
                        const int64_t signed_value = size == 2 ? (int16_t)raw : size == 4 ? (int32_t)raw : (int64_t)raw;
                        const uint64_t value = kind == 0 ? (c >= bits ? 0 : raw << c) :
                                               kind == 1 ? (c >= bits ? 0 : raw >> c) :
                                                           (uint64_t)(c >= bits ? (signed_value < 0 ? -1 : 0) : signed_value >> c);
                        memcpy(wanted + k * bytes + lane * size, &value, size);
                    }
                }
            }
            bool machine = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, label, functions, true, inputs, expected, arenas, &machine);
            ok = ok && tier1_masks_test_expect(test, !machine, "a shift by an int is the IR's shift, not a machine row");
            test->shift_rows += ok;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the shifts return the scratch arena");
            arena_pop_to(&test->output, output_mark);
            for (uint32_t k = 0; k < 3; k += 1)
            {
                arena_pop_to(&arenas[k], 0);
            }
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The front end's masks to vectors and back through the IR to Tier 1 (#1840): every MASK_VECTOR rule (vpmovm2b to
// vpmovm2q) and VECTOR_MASK rule (vpmovb2m to vpmovq2m) at each width, as the opmask conversion leaves its row, the
// program's only row in 4 statements: a mask from the input's word k into a vector stored at the output's vector k, or
// the input's vector k into a mask stored, zero-extended, at the output's word k. tier1_masks_test_typed_pipeline takes
// each through the IR as its vselect of ones and zeros or its comparison less than zero, which the interpreter runs at
// every stage, and Tier 1 natively as the single instruction, against a model from Intel's manual: each lane all ones
// where its mask bit is set, and each mask bit its lane's sign bit.
static void tier1_masks_test_mask_vectors(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the mask conversions' memory");
    uint64_t state = test->random ^ UINT64_C(0x510e527fade682d1);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool expand = rule->family == CHECK_X86_LOWER_MASK_VECTOR;
        if (!expand && rule->family != CHECK_X86_LOWER_VECTOR_MASK)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->mask_vector_attempts += 1;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &target);
        const uint32_t bytes = rule->bytes;
        const uint32_t lanes = bytes / rule->element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        static const uint8_t integers[9] = { 0, BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_U16, 0, BUSTER_TYPED_CLASS_U32, 0, 0, 0, BUSTER_TYPED_CLASS_U64 };
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == 1, "the front end's mask conversion's row, in the mask class at x86-64-v4");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            if (expand)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, integers[mask_bytes], mask_bytes);
                if (mask_bytes != 8)
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
            }
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the mask conversion's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                if (expand)
                {
                    for (uint32_t lane = 0; lane < lanes; lane += 1)
                    {
                        memset(wanted + k * bytes + lane * rule->element, (in[k] >> lane & 1) != 0 ? 0xff : 0, rule->element);
                    }
                }
                else
                {
                    const uint8_t* a = (const uint8_t*)in + k * bytes % 256;
                    uint64_t bits = 0;
                    for (uint32_t lane = 0; lane < lanes; lane += 1)
                    {
                        bits |= (uint64_t)(a[lane * rule->element + rule->element - 1] >> 7) << lane;
                    }
                    memcpy(wanted + 8 * k, &bits, 8);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a mask conversion is the IR's, not a machine row");
        test->mask_vector_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the mask conversions return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            arena_pop_to(&arenas[k], 0);
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The front end's elementwise minima, maxima and saturations through the IR to Tier 1 (#1840): every form the producer
// has (check_vector_elementwise_forms, the row's one record of VECTOR(0) and VECTOR(1) into VECTOR(0)), of each element
// size, signedness and width, as the program's only row in 4 statements of the input buffer's vectors k and k + 1
// (modulo 4), stored into the output's vector k; tier1_masks_test_typed_pipeline takes each through the IR as its
// lane_op, which the interpreter runs at every stage, and Tier 1 natively, against C's minimum, maximum and clamped sum
// and difference of each lane.
static void tier1_masks_test_lane_ops(Tier1MasksTest* test)
{
    static const CBuiltinId builtins[4] = { CBUILTIN_ELEMENTWISE_MIN, CBUILTIN_ELEMENTWISE_MAX, CBUILTIN_ELEMENTWISE_ADD_SAT, CBUILTIN_ELEMENTWISE_SUB_SAT };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the lane operations' memory");
    uint64_t state = test->random ^ UINT64_C(0x9b05688c2b3e6c1f);
    for (uint32_t form = 0; ready && form < 4 * 4 * 2 * 3; form += 1)
    {
        const uint32_t k = form / 24;
        const uint32_t size = (form / 6) % 4;
        const uint32_t sign = (form / 3) % 2;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const CheckVectorShape shape = { .lanes = bytes >> size, .bytes = bytes, .element = (uint8_t)element, .integer = true, .is_signed = sign != 0 };
        uint64_t needed = 0;
        const CheckVectorForms forms = check_vector_elementwise_forms(builtins[k], shape, &needed);
        if (forms.forms[width] == 0)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->lane_op_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = check_vector_lower_record(forms.forms[width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(1));
        row.record_count = 1;
        row.operand_count = 2;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[0] = (uint8_t)bytes;
        row.operand_bytes[1] = (uint8_t)bytes;
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "elementwise %u of %u-byte elements (signed %u) at %u bytes", k, element, sign, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s + 1) * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the lane operation's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // Every fourth word of extreme lanes, at the saturations' edges.
            for (uint32_t w = input; w < BUSTER_TIER1_MASKS_TEST_IN / 8; w += 4)
            {
                in[w] = w % 8 < 4 ? UINT64_C(0x7f80ff007fff8000) : UINT64_C(0x807f00ff80007fff);
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint8_t* a = (const uint8_t*)in + s * bytes % 256;
                const uint8_t* b = (const uint8_t*)in + (s + 1) * bytes % 256;
                for (uint32_t lane = 0; lane < bytes / element; lane += 1)
                {
                    uint64_t ua = 0;
                    uint64_t ub = 0;
                    memcpy(&ua, a + lane * element, element);
                    memcpy(&ub, b + lane * element, element);
                    const int64_t sa = element == 1 ? (int8_t)ua : element == 2 ? (int16_t)ua : element == 4 ? (int32_t)ua : (int64_t)ua;
                    const int64_t sb = element == 1 ? (int8_t)ub : element == 2 ? (int16_t)ub : element == 4 ? (int32_t)ub : (int64_t)ub;
                    const bool less = sign != 0 ? sa < sb : ua < ub;
                    const int64_t high = sign != 0 ? (element == 1 ? INT8_MAX : INT16_MAX) : (element == 1 ? UINT8_MAX : UINT16_MAX);
                    const int64_t low = sign != 0 ? -high - 1 : 0;
                    const int64_t exact = k == 2 ? (sign != 0 ? sa + sb : (int64_t)(ua + ub)) : (sign != 0 ? sa - sb : (int64_t)ua - (int64_t)ub);
                    const uint64_t value = k == 0 ? (less ? ua : ub) : k == 1 ? (less ? ub : ua) : (uint64_t)(exact > high ? high : exact < low ? low : exact);
                    memcpy(wanted + s * bytes + lane * element, &value, element);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a lane operation is the IR's lane_op, not a machine row");
        test->lane_op_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the lane operations return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The permutes of two tables through the IR to Tier 1 (#1840): vpermt2w to vpermt2q at each width (vpermt2b is VBMI's,
// which the x86-64-v4 target hasn't), also of one 16-byte table twice and of two tables of half the width, widened, the indices in vvvv and
// the second table in rm (the permutex2var builtins' rows) or the other way around (__builtin_shufflevector's), each as
// the program's only row in 4 statements of the input buffer's vectors k, k + 1 and k + 2 (modulo 4), stored into the
// output's vector k; tier1_masks_test_typed_pipeline takes each through the IR as its permute2, which the interpreter
// runs at every stage, and Tier 1 natively, against each lane picked from the two tables by its index's low bits.
static void tier1_masks_test_permute2s(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the permutes of two tables' memory");
    uint64_t state = test->random ^ UINT64_C(0x2f6c9a3d81e4b057);
    for (uint32_t form = 0; ready && form < 4 * 4 * 3; form += 1)
    {
        const uint32_t size = form / 12;
        // 0 and 1: the permutex2var builtins' and __builtin_shufflevector's orders; 2: one 16-byte table, twice; 3: two
        // tables of half the result's width in __builtin_shufflevector's order, which the decoder widens.
        const uint32_t shape = (form / 3) % 4;
        const uint32_t swapped = shape == 1 || shape == 3;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        if (shape >= 2 && width == 0)
        {
            continue;
        }
        const uint32_t table_bytes = shape == 2 ? 16 : shape == 3 ? bytes / 2 : bytes;
        const uint32_t operand_count = shape == 2 ? 2 : 3;
        const uint32_t indices_place = swapped != 0 ? 2 : 1;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        // The x86-64-v4 target's alone: vpermt2b is VBMI's, which test-ir-simd decodes at a target that has it.
        const uint64_t needed = x86_target_requirement(ir_simd_permute2_forms[size][width]) & ~BUSTER_X86_TARGET_KNOWN;
        if ((needed & ~test->v4_features) != 0)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->permute2_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        const uint8_t indices_role = (uint8_t)BUSTER_TIER0_ROLE_VECTOR(indices_place);
        const uint8_t table_role = shape == 2 ? BUSTER_TIER0_ROLE_VECTOR(0) : swapped != 0 ? BUSTER_TIER0_ROLE_VECTOR(1) : BUSTER_TIER0_ROLE_VECTOR(2);
        row.records[0] = check_vector_lower_record(ir_simd_permute2_forms[size][width], BUSTER_TIER0_ROLE_VECTOR(0), indices_role, table_role);
        row.record_count = 1;
        row.operand_count = (uint8_t)operand_count;
        for (uint32_t o = 0; o < operand_count; o += 1)
        {
            row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[o] = (uint8_t)(o == indices_place ? bytes : table_bytes);
        }
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "permute2 of %u-byte lanes (shape %u) at %u bytes", element, shape, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
            for (uint32_t o = 0; o < operand_count; o += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s + o) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, row.operand_bytes[o]);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the permute of two tables' program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint8_t* operands[3] = {
                    (const uint8_t*)in + s * bytes % 256,
                    (const uint8_t*)in + (s + 1) * bytes % 256,
                    (const uint8_t*)in + (s + 2) * bytes % 256,
                };
                // The tables zero-extended to the result's width, as the IR's widen gives them.
                uint8_t tables[2][64];
                memset(tables, 0, sizeof(tables));
                memcpy(tables[0], operands[0], table_bytes);
                memcpy(tables[1], operands[shape == 2 ? 0 : swapped != 0 ? 1 : 2], table_bytes);
                const uint8_t* indices = operands[indices_place];
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    uint64_t index = 0;
                    memcpy(&index, indices + lane * element, element);
                    const uint32_t selected = (uint32_t)(index & (2 * lanes - 1));
                    memcpy(wanted + s * bytes + lane * element, tables[selected < lanes ? 0 : 1] + (selected & (lanes - 1)) * element, element);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a permute of two tables is the IR's permute2, not a machine row");
        test->permute2_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the permutes of two tables return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The bitwise functions of three vectors through the IR to Tier 1 (#1840): vpternlogd and vpternlogq at each width under a
// writemask, merging into the first operand or zeroing, of 4 truth tables, each as the program's only row in 4 statements
// of the input buffer's vectors k, k + 1 and k + 2 (modulo 4) and a mask from one of its words, stored into the output's
// vector k; tier1_masks_test_typed_pipeline takes each through the IR as its ternlog and a select, which the interpreter
// runs at every stage, and Tier 1 natively, against each bit's table entry and each lane's mask bit.
static void tier1_masks_test_ternlogs(Tier1MasksTest* test)
{
    static const uint8_t tables[4] = { 0x96, 0xe8, 0xca, 0x3c };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the bitwise functions of three vectors' memory");
    uint64_t state = test->random ^ UINT64_C(0x5be0cd19137e2179);
    for (uint32_t form = 0; ready && form < 4 * 2 * 2 * 3; form += 1)
    {
        const uint32_t table = tables[form / 12];
        const uint32_t lanes_log2 = (form / 6) % 2;
        const bool zeroing = (form / 3) % 2 != 0;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 4u << lanes_log2;
        const uint32_t lanes = bytes / element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const uint64_t output_mark = test->output.length;
        test->ternlog_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = check_vector_lower_record(ir_simd_ternlog_forms[lanes_log2][width], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(1), BUSTER_TIER0_ROLE_VECTOR(2));
        row.records[0].decorations = (uint8_t)(BUSTER_TIER0_DECORATION_MASK(3) | (zeroing ? BUSTER_X86_DECORATION_ZEROING : 0));
        row.records[0].immediate = (int32_t)table;
        row.record_count = 1;
        row.operand_count = 4;
        for (uint32_t o = 0; o < 3; o += 1)
        {
            row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
            row.operand_bytes[o] = (uint8_t)bytes;
        }
        row.operand_classes[3] = BUSTER_TYPED_CLASS_MASK;
        row.operand_bytes[3] = (uint8_t)mask_bytes;
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "ternlog 0x%02x of %u-byte lanes (zeroing %u) at %u bytes", table, element, zeroing ? 1u : 0u, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
            for (uint32_t o = 0; o < 3; o += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s + o) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * 7 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the bitwise function's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = in[s * 7 % 32];
                for (uint32_t w = 0; w < bytes / 8; w += 1)
                {
                    uint64_t operand[3];
                    for (uint32_t o = 0; o < 3; o += 1)
                    {
                        memcpy(&operand[o], (const uint8_t*)in + (s + o) * bytes % 256 + w * 8, 8);
                    }
                    uint64_t value = 0;
                    for (uint32_t bit = 0; bit < 64; bit += 1)
                    {
                        const uint32_t index = (uint32_t)((operand[0] >> bit & 1) << 2 | (operand[1] >> bit & 1) << 1 | (operand[2] >> bit & 1));
                        value |= (uint64_t)(table >> index & 1) << bit;
                    }
                    for (uint32_t part = 0; part < 8 / element; part += 1)
                    {
                        const uint32_t lane = w * 8 / element + part;
                        const uint64_t lane_bits = element == 8 ? UINT64_MAX : (UINT64_C(1) << 32) - 1;
                        const uint64_t place = lane_bits << (part * element * 8);
                        if ((mask >> lane & 1) == 0)
                        {
                            value = (value & ~place) | (zeroing ? 0 : operand[0] & place);
                        }
                    }
                    memcpy(wanted + s * bytes + w * 8, &value, 8);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a bitwise function of three vectors is the IR's ternlog, not a machine row");
        test->ternlog_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the bitwise functions return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// One lane's moves and the lanes' sign bits through the IR to Tier 1 (#1840): vmovd, vmovq and vpextrb to vpextrq of a
// vector of each width, of its first lane, its low 16 bytes' last and a lane of its last 16 bytes (after vextracti32x4);
// vpinsrb to vpinsrq into a 16-byte vector's first and last lanes; and vpmovmskb, vmovmskps and vmovmskpd of a 16- and a
// 32-byte vector, each as the program's only row in 4 statements of the input buffer's vector k: an extraction's or a
// movemask's integer stored into the output's word k, an insertion's of the input's word 30 - k into the output's
// 16 bytes k. tier1_masks_test_typed_pipeline takes each through the IR as its lane_extract, lane_insert or movemask,
// which the interpreter runs at every stage, and Tier 1 natively, against C's lanes.
static void tier1_masks_test_lane_moves(Tier1MasksTest* test)
{
    static const uint8_t extract_classes[4] = { BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_U16, BUSTER_TYPED_CLASS_I32, BUSTER_TYPED_CLASS_I64 };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the lane moves' memory");
    uint64_t state = test->random ^ UINT64_C(0x510e527fade682d1);
    // Extractions (4 sizes, 3 widths, 3 lanes), insertions (4 sizes, 2 lanes), movemasks (3 sizes, 2 widths).
    for (uint32_t form = 0; ready && form < 36 + 8 + 6; form += 1)
    {
        const uint32_t kind = form < 36 ? 0 : form < 44 ? 1 : 2;
        const uint32_t index = kind == 0 ? form : kind == 1 ? form - 36 : form - 44;
        const uint32_t size = kind == 2 ? (index % 3 == 0 ? 0 : index % 3 + 1) : index % 4;
        const uint32_t width = kind == 0 ? (index / 4) % 3 : kind == 1 ? 0 : index / 3;
        const uint32_t pick = kind == 0 ? index / 12 : kind == 1 ? index / 4 : 0;
        const uint32_t bytes = 16u << width;
        const uint32_t per = 16u >> size;
        const uint32_t parts = bytes / 16;
        const uint32_t part = kind == 0 && pick == 2 ? parts - 1 : 0;
        const uint32_t sub = pick == 0 ? 0 : pick == 1 ? per - 1 : (parts == 1 ? per / 2 : 1 % per);
        const uint32_t lane = part * per + sub;
        const uint32_t element = 1u << size;
        const uint64_t output_mark = test->output.length;
        test->lane_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
        const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
        const uint8_t out = BUSTER_TIER0_ROLE_INTEGER_RESULT;
        uint8_t result_class = BUSTER_TYPED_CLASS_I32;
        if (kind == 0)
        {
            const uint8_t from = part != 0 ? v1 : v0;
            uint32_t r = 0;
            if (part != 0)
            {
                row.records[r++] = (X86Instruction){ .form = ir_simd_part_extract_forms[width == 2], .reg = v0, .rm = v1, .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = part };
            }
            const bool whole = sub == 0 && size >= 2;
            const bool word = !whole && size == 1;
            row.records[r++] = (X86Instruction){ .form = whole ? (size == 2 ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_VMOVQ_RM64_X_EVEX) : ir_simd_lane_extract_forms[size],
                                                 .reg = word ? out : from,
                                                 .rm = word ? from : out,
                                                 .mode = BUSTER_X86_REGISTER_DIRECT,
                                                 .immediate = whole ? 0 : sub };
            row.record_count = r;
            row.operand_count = 1;
            result_class = extract_classes[size];
        }
        else if (kind == 1)
        {
            row.records[0] = (X86Instruction){ .form = ir_simd_lane_insert_forms[size], .reg = v0, .vvvv = v0, .rm = BUSTER_TIER0_ROLE_INTEGER(1), .mode = BUSTER_X86_REGISTER_DIRECT, .immediate = sub };
            row.record_count = 1;
            row.operand_count = 2;
            row.operand_classes[1] = size == 3 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
            result_class = BUSTER_TYPED_CLASS_VECTOR;
            row.result_bytes = 16;
        }
        else
        {
            row.records[0] = (X86Instruction){ .form = ir_simd_movemask_forms[size][width], .reg = out, .rm = v0, .mode = BUSTER_X86_REGISTER_DIRECT };
            row.record_count = 1;
            row.operand_count = 1;
        }
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[0] = (uint8_t)bytes;
        row.result_class = result_class;
        char name[96];
        snprintf(name, sizeof(name), "%s of %u-byte lane %u at %u bytes", kind == 0 ? "lane_extract" : kind == 1 ? "lane_insert" : "movemask", element, lane, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, kind == 1 ? s * 2 : s);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            if (kind == 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 30 - s);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, row.operand_classes[1], 0);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, result_class, kind == 1 ? 16 : 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the lane move's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint8_t* vector = (const uint8_t*)in + s * bytes % 256;
                if (kind == 0)
                {
                    uint64_t value = 0;
                    memcpy(&value, vector + lane * element, element);
                    memcpy(wanted + s * 8, &value, size == 3 ? 8 : size == 2 ? 4 : element);
                }
                else if (kind == 1)
                {
                    memcpy(wanted + s * 16, vector, 16);
                    memcpy(wanted + s * 16 + lane * element, in + 30 - s, element);
                }
                else
                {
                    uint32_t bits = 0;
                    for (uint32_t l = 0; l < bytes / element; l += 1)
                    {
                        bits |= (uint32_t)(vector[l * element + element - 1] >> 7) << l;
                    }
                    memcpy(wanted + s * 8, &bits, 4);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a lane move is the IR's, not a machine row");
        test->lane_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the lane moves return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// A floating operation at its own rounding's row (#1840): `flags` the IR's fround flags (operation, lanes, rounding), one
// record of its _ER form, VECTOR(0) in vvvv and VECTOR(1) in rm into VECTOR(0), the rounding in its decorations, as the
// front end's rule rows of _mm512_add_round_ps to _mm512_div_round_pd have it. ir_simd_typed_test.c reads it too.
static Tier0Operation tier1_masks_test_fround_row(uint32_t flags)
{
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    row.records[0] = check_vector_lower_record(ir_simd_fround_forms[flags & 3][(flags >> 2) & 1], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(1));
    row.records[0].decorations = (uint8_t)(((flags >> 3) & 3) << 5);
    row.record_count = 1;
    row.operand_count = 2;
    row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
    row.operand_bytes[0] = 64;
    row.operand_bytes[1] = 64;
    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
    row.result_bytes = 64;
    return row;
}

// C's operation of two lanes in the rounding mode the flags name, by host_fesetround and volatile operands: the
// expectation of the floating operations at their own rounding.
static uint64_t tier1_masks_test_fround_oracle(uint32_t flags, uint64_t a, uint64_t b)
{
    static const int modes[4] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    const int saved = host_fegetround();
    const uint32_t operation = flags & 3;
    uint64_t result = 0;
    host_fesetround(modes[(flags >> 3) & 3]);
    if ((flags & 4) == 0)
    {
        volatile float x = 0;
        volatile float y = 0;
        volatile float z = 0;
        const uint32_t xa = (uint32_t)a;
        const uint32_t yb = (uint32_t)b;
        memcpy((void*)&x, &xa, sizeof(xa));
        memcpy((void*)&y, &yb, sizeof(yb));
        z = operation == 0 ? x + y : operation == 1 ? x - y : operation == 2 ? x * y : x / y;
        uint32_t word = 0;
        memcpy(&word, (const void*)&z, sizeof(word));
        result = word;
    }
    else
    {
        volatile double x = 0;
        volatile double y = 0;
        volatile double z = 0;
        memcpy((void*)&x, &a, sizeof(a));
        memcpy((void*)&y, &b, sizeof(b));
        z = operation == 0 ? x + y : operation == 1 ? x - y : operation == 2 ? x * y : x / y;
        memcpy(&result, (const void*)&z, sizeof(result));
    }
    host_fesetround(saved);
    return result;
}

// The floating operations at their own rounding through the IR to Tier 1 (#1840): every operation, lanes and rounding's
// row (tier1_masks_test_fround_row) as the program's only row in 4 statements of the input buffer's vectors k and k + 1
// into the output's vector k, inputs whose lanes are random floats of nearby magnitudes, ties and specials.
// tier1_masks_test_typed_pipeline takes each through the IR as its fround, which the interpreter runs at every stage, and
// Tier 1 natively, against C's operation in that rounding mode.
static void tier1_masks_test_frounds(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the floating operations' memory");
    uint64_t state = test->random ^ UINT64_C(0x9b05688c2b3e6c1f);
    for (uint32_t flags = 0; ready && flags < 32; flags += 1)
    {
        const uint32_t lane_bits = (flags & 4) != 0 ? 64 : 32;
        const uint64_t output_mark = test->output.length;
        test->fround_attempts += 1;
        Tier0Operation row = tier1_masks_test_fround_row(flags);
        char name[96];
        snprintf(name, sizeof(name), "fround 0x%x", flags);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t st = 0; st < 4; st += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, st * 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, st * 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, 64);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (st + 1) * 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, 64);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, 64);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the floating operation's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // Floats of a few binades around 1 (sums and products that round), and now and then a special.
            static const uint64_t specials[2][4] = { { 0, UINT64_C(0x7f800000), UINT64_C(0x7fc00000), UINT64_C(0x7f7fffff) },
                                                     { 0, UINT64_C(0x7ff0000000000000), UINT64_C(0x7ff8000000000000), UINT64_C(0x7fefffffffffffff) } };
            for (uint32_t lane = 0; lane < BUSTER_TIER1_MASKS_TEST_IN * 8 / lane_bits; lane += 1)
            {
                const uint64_t random = tier0_test_mask_random(&state);
                const uint64_t sign = (random & 1) << (lane_bits - 1);
                const uint64_t value = (random & 0x1e) == 0 ? specials[lane_bits == 64][(random >> 5) & 3] | sign :
                                       lane_bits == 32 ? sign | (UINT64_C(124) + ((random >> 5) & 7)) << 23 | ((random >> 8) & UINT64_C(0x7fffff)) :
                                                         sign | (UINT64_C(1020) + ((random >> 5) & 7)) << 52 | ((random >> 8) & UINT64_C(0xfffffffffffff));
                memcpy((uint8_t*)in + lane * (lane_bits / 8), &value, lane_bits / 8);
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t st = 0; st < 4; st += 1)
            {
                const uint8_t* a = (const uint8_t*)in + st * 64 % 256;
                const uint8_t* b = (const uint8_t*)in + (st + 1) * 64 % 256;
                for (uint32_t lane = 0; lane < 512 / lane_bits; lane += 1)
                {
                    uint64_t x = 0;
                    uint64_t y = 0;
                    memcpy(&x, a + lane * (lane_bits / 8), lane_bits / 8);
                    memcpy(&y, b + lane * (lane_bits / 8), lane_bits / 8);
                    const uint64_t z = tier1_masks_test_fround_oracle(flags, x, y);
                    memcpy(wanted + st * 64 + lane * (lane_bits / 8), &z, lane_bits / 8);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a floating operation at its own rounding is the IR's, not a machine row");
        test->fround_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the floating operations return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// A truncating conversion of lane 0's row (#1840): `flags` the IR's cvtt flags (bit 0 a double, bit 1 a 64-bit result),
// one record of ir_simd_cvtt_forms', INTEGER_RESULT in reg and VECTOR(0) in rm, as the front end's rule rows of
// _mm_cvttss_si32 to _mm_cvttsd_si64 have it. ir_simd_typed_test.c reads it too.
static Tier0Operation tier1_masks_test_cvtt_row(uint32_t flags)
{
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    row.records[0] = check_vector_lower_record(ir_simd_cvtt_forms[flags & 1][(flags >> 1) & 1], BUSTER_TIER0_ROLE_INTEGER_RESULT, 0, BUSTER_TIER0_ROLE_VECTOR(0));
    row.record_count = 1;
    row.operand_count = 1;
    row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row.operand_bytes[0] = 16;
    row.result_class = (flags & 2) != 0 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_I32;
    return row;
}

// The truncating conversions of lane 0 through the IR to Tier 1 (#1840): each lane and result's row as the program's only
// row in 4 statements of the input buffer's vector k into the output's word k, lanes of every magnitude around both
// ranges' edges, NaNs and infinities. tier1_masks_test_typed_pipeline takes each through the IR as its cvtt, which the
// interpreter runs at every stage, and Tier 1 natively, against x86's result written out with long double bounds.
static void tier1_masks_test_cvtts(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the truncating conversions' memory");
    uint64_t state = test->random ^ UINT64_C(0x3c6ef372a54ff53a);
    for (uint32_t flags = 0; ready && flags < 4; flags += 1)
    {
        const bool is_double = (flags & 1) != 0;
        const bool wide = (flags & 2) != 0;
        const uint64_t output_mark = test->output.length;
        test->cvtt_attempts += 1;
        Tier0Operation row = tier1_masks_test_cvtt_row(flags);
        char name[64];
        snprintf(name, sizeof(name), "cvtt %u", flags);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t st = 0; st < 4; st += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, st);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, st * 2);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, 16);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the truncating conversion's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t st = 0; st < 4; st += 1)
            {
                // A value of 2^-2 to 2^65 in magnitude, either sign, now and then an edge, an infinity or a NaN.
                const uint64_t random = tier0_test_mask_random(&state);
                static const double edges[8] = { 2147483647.5, 2147483648.0, -2147483648.75, -2147483649.0, 0x1.fffffffffffffp62, -0x1p63, INFINITY, NAN };
                const double magnitude = ldexp(1.0 + (double)(random >> 12) * 0x1p-52, (int)((random >> 4) % 68) - 2);
                const double x = (random & 7) == 0 ? edges[(random >> 3) & 7] : ((random >> 3) & 1) != 0 ? -magnitude : magnitude;
                const double value = is_double ? x : (double)(float)x;
                in[st * 2 + 1] = 0;
                if (is_double)
                {
                    memcpy(&in[st * 2], &value, sizeof(value));
                }
                else
                {
                    const float single = (float)value;
                    uint32_t word = 0;
                    memcpy(&word, &single, sizeof(word));
                    in[st * 2] = word;
                }
                const long double limit = wide ? 0x1p63L : 0x1p31L;
                const bool fits = value == value && (long double)value > -limit - 1.0L && (long double)value < limit;
                const uint64_t result = !fits ? (wide ? UINT64_C(0x8000000000000000) : UINT64_C(0x80000000)) : wide ? (uint64_t)(int64_t)value : (uint64_t)(uint32_t)(int32_t)value;
                memcpy(wanted + st * 8, &result, wide ? 8 : 4);
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a truncating conversion is the IR's, not a machine row");
        test->cvtt_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the truncating conversions return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The masked loads and stores through the IR to Tier 1 (#1840): vmovdqu8 to vmovdqu64 at each width, a load's merged
// under a mask from the input into a passthrough and moved into the result, a store's of an input vector under one, each
// as the program's only row in 4 statements: a load's from the input buffer's vector k into the output's vector k, its
// passthrough the input's vector k + 1 (modulo 4), and a store's of the input's vector k into the output's. The fourth
// statement's access starts half a vector before its buffer's end, its mask's upper half cleared, so that its masked-off
// lanes lie past the binding the interpreter checks every access against: a masked-off lane is never read or written.
// tier1_masks_test_typed_pipeline takes each through the IR as its masked_load or masked_store, which the interpreter runs
// at every stage, and Tier 1 natively, against C's lanes; the output's bytes no store writes stay the harness's 0xa5.
static void tier1_masks_test_masked_memory(Tier1MasksTest* test)
{
    static const uint16_t moves[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the masked accesses' memory");
    uint64_t state = test->random ^ UINT64_C(0x5be0cd19137e2179);
    for (uint32_t form = 0; ready && form < 2 * 4 * 3; form += 1)
    {
        const uint32_t store = form / 12;
        const uint32_t size = (form / 3) % 4;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const uint64_t half = (UINT64_C(1) << (lanes / 2)) - 1;
        const uint64_t output_mark = test->output.length;
        test->masked_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = (X86Instruction){ .form = ir_simd_masked_moves[size][store][width], .reg = BUSTER_TIER0_ROLE_VECTOR(1), .rm = BUSTER_TIER0_ROLE_INTEGER(0),
                                           .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        row.records[1] = check_vector_lower_record(moves[width], BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(1));
        row.record_count = store != 0 ? 1 : 2;
        row.operand_count = 3;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        row.operand_bytes[1] = (uint8_t)bytes;
        row.operand_bytes[2] = (uint8_t)mask_bytes;
        row.result_class = store != 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = store != 0 ? 0 : (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "masked %s of %u-byte lanes at %u bytes", store != 0 ? "store" : "load", element, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        // Each statement's access, in words: the fourth's half a vector before its buffer's end.
        uint32_t at[4];
        for (uint32_t s = 0; s < 4; s += 1)
        {
            at[s] = s < 3 ? s * bytes / 8 : 32 - bytes / 16;
            if (store == 0)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, at[s]);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s + 1) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, at[s]);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * 7 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            if (store == 0)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
            }
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the masked access's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // The fourth statement's mask: its lanes within the buffer alone.
            in[3 * 7 % 32] &= half;
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = in[s * 7 % 32];
                const uint8_t* bytes_in = (const uint8_t*)in;
                const uint8_t* source = bytes_in + (store == 0 ? at[s] * 8 : s * bytes % 256);
                uint8_t* out = wanted + (store == 0 ? s * bytes : at[s] * 8);
                if (store == 0)
                {
                    memcpy(out, bytes_in + (s + 1) * bytes % 256, bytes);
                }
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    if ((mask >> lane & 1) != 0)
                    {
                        memcpy(out + lane * element, source + lane * element, element);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a masked load or store is the IR's, not a machine row");
        test->masked_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the masked accesses return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The compresses and expands through the IR to Tier 1 (#1840): vpcompressd, vpcompressq, vpexpandd and vpexpandq at each
// width (bytes and words are VBMI2's, which the x86-64-v4 target hasn't), merged under a mask from the input into the
// passthrough and moved into the result, each as the program's only row in 4 statements of the input buffer's vectors k
// and k + 1 (modulo 4), stored into the output's vector k; tier1_masks_test_typed_pipeline takes each through the IR as
// its compress or expand, which the interpreter runs at every stage, and Tier 1 natively, against C's lanes.
static void tier1_masks_test_compresses(Tier1MasksTest* test)
{
    static const uint16_t moves[3] = { X86_FORM_VMOVDQU64_X_XM128, X86_FORM_VMOVDQU64_Y_YM256, X86_FORM_VMOVDQU64_Z_ZM512 };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the compresses' memory");
    uint64_t state = test->random ^ UINT64_C(0x1f83d9abfb41bd6b);
    for (uint32_t form = 0; ready && form < 2 * 2 * 3; form += 1)
    {
        const uint32_t expand = form / 6;
        const uint32_t size = 2 + (form / 3) % 2;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        const uint32_t mask_bytes = lanes <= 8 ? 1 : lanes / 8;
        const uint64_t output_mark = test->output.length;
        test->compress_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        const uint16_t record_form = expand != 0 ? ir_simd_expand_forms[size][width] : ir_simd_compress_forms[size][width];
        row.records[0] = check_vector_lower_record(record_form, expand != 0 ? BUSTER_TIER0_ROLE_VECTOR(1) : BUSTER_TIER0_ROLE_VECTOR(0), 0,
                                                   expand != 0 ? BUSTER_TIER0_ROLE_VECTOR(0) : BUSTER_TIER0_ROLE_VECTOR(1));
        row.records[0].decorations = BUSTER_TIER0_DECORATION_MASK(2);
        row.records[1] = check_vector_lower_record(moves[width], BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(1));
        row.record_count = 2;
        row.operand_count = 3;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        row.operand_bytes[0] = (uint8_t)bytes;
        row.operand_bytes[1] = (uint8_t)bytes;
        row.operand_bytes[2] = (uint8_t)mask_bytes;
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "%s of %u-byte lanes at %u bytes", expand != 0 ? "expand" : "compress", element, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
            for (uint32_t o = 0; o < 2; o += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s + o) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * 7 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the compress's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = in[s * 7 % 32];
                const uint8_t* source = (const uint8_t*)in + s * bytes % 256;
                const uint8_t* passthrough = (const uint8_t*)in + (s + 1) * bytes % 256;
                uint8_t* out = wanted + s * bytes;
                memcpy(out, passthrough, bytes);
                uint32_t next = 0;
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    if ((mask >> lane & 1) != 0)
                    {
                        memcpy(out + (expand != 0 ? lane : next) * element, source + (expand != 0 ? next : lane) * element, element);
                        next += 1;
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a compress or expand is the IR's, not a machine row");
        test->compress_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the compresses return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t r = 0; r < 3; r += 1)
        {
            arena_pop_to(&arenas[r], 0);
        }
    }
    for (uint32_t r = 0; r < 3; r += 1)
    {
        arena_release(&arenas[r]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// A C model of an integer lane's conversion (#1840): `raw`, a lane of `from` bytes, to `to` bytes, by C's conversions: a
// wider lane by its signedness, a narrower one wrapped, or saturated as a signed lane to the narrower one's signed range
// (`mode` 1, vpmovs) or as an unsigned lane to its unsigned range (2, vpmovus: Intel's manual's unsigned source).
static uint64_t tier1_masks_test_convert_lane(uint64_t raw, uint32_t from, uint32_t to, bool is_signed, uint32_t mode)
{
    const int64_t value = from == 1 ? (int8_t)raw : from == 2 ? (int16_t)raw : from == 4 ? (int32_t)raw : (int64_t)raw;
    const int64_t high = to == 1 ? INT8_MAX : to == 2 ? INT16_MAX : INT32_MAX;
    const uint64_t unsigned_high = to == 1 ? UINT8_MAX : to == 2 ? UINT16_MAX : UINT32_MAX;
    const uint64_t result = to > from ? (is_signed ? (uint64_t)value : raw) :
                            mode == 1  ? (uint64_t)(value > high ? high : value < -high - 1 ? -high - 1 : value) :
                            mode == 2  ? (raw > unsigned_high ? unsigned_high : raw) :
                                         raw;
    return to == 8 ? result : result & ((UINT64_C(1) << (8 * to)) - 1);
}

// The conversions through the IR to Tier 1 (#1840): __builtin_convertvector's rows between integer vectors of other
// element sizes (check_vector_convert_row's, every pair, signedness and result width, the operand the source's or 16
// bytes where those are fewer), and every TRUNCATE rule's row whose lanes fill its result, merged under an input word in
// the odd statements and unmasked in the even ones, each as the program's only row in 4 statements of the input buffer's
// vectors, stored into the output's; through the IR as its extend or truncate, which the interpreter runs at every stage,
// and Tier 1 natively, against tier1_masks_test_convert_lane lane by lane. Tier 0 doesn't run them here, as the selects'.
static void tier1_masks_test_conversions(Tier1MasksTest* test)
{
    static const TypeId elements[2][4] = {
        { BUSTER_TYPE_UCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_UINT, BUSTER_TYPE_ULLONG },
        { BUSTER_TYPE_SCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_INT, BUSTER_TYPE_LLONG },
    };
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    TypeTestTable table = {0};
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0 &&
                                                   type_test_table_open(&table, 512),
                                         "the conversions' memory and type table");
    uint64_t state = test->random ^ UINT64_C(0x510e527fade682d1);
    // The cases: convertvector's by (sign, from, to, width), then the rules'.
    const uint32_t conversions = 2 * 4 * 4 * 3;
    for (uint32_t c = 0; ready && c < conversions + BUSTER_ARRAY_LENGTH(check_x86_lower_rules); c += 1)
    {
        Tier0Operation row;
        bool made = false;
        bool masked = false;
        uint32_t from = 0;
        uint32_t to = 0;
        uint32_t mode = 0;
        bool is_signed = false;
        char name[128];
        if (c < conversions)
        {
            const uint32_t sign = c / 48;
            from = 1u << (c / 12 % 4);
            to = 1u << (c / 3 % 4);
            const uint32_t bytes = 16u << (c % 3);
            const uint32_t lanes = bytes / to;
            is_signed = sign != 0;
            if (from == to || lanes * from > 64)
            {
                continue;
            }
            const TypeId source = type_vector(&table.table, elements[sign][__builtin_ctz(from)], lanes, false);
            const TypeId result = type_vector(&table.table, elements[sign][__builtin_ctz(to)], lanes, false);
            const uint32_t operand = lanes * from < 16 ? 16 : lanes * from;
            if (to < from && operand != lanes * from)
            {
                continue;
            }
            made = check_vector_convert_row(&table.table, &target, source, operand, result, &row) == NULL;
            snprintf(name, sizeof(name), "convertvector of %u lanes of %u bytes (signed %u) to %u", lanes, from, sign, to);
        }
        else
        {
            const CheckX86LowerRule* rule = check_x86_lower_rules + (c - conversions);
            if (rule->family != CHECK_X86_LOWER_TRUNCATE)
            {
                continue;
            }
            const char* mnemonic = x86_form_description(rule->form)->mnemonic;
            const char last = mnemonic[strlen(mnemonic) - 1];
            from = rule->element;
            to = last == 'b' ? 1 : last == 'w' ? 2 : 4;
            mode = strncmp(mnemonic, "vpmovus", 7) == 0 ? 2 : strncmp(mnemonic, "vpmovs", 6) == 0 ? 1 : 0;
            if ((uint32_t)rule->bytes / from * to != rule->other)
            {
                continue;
            }
            masked = true;
            made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &target);
            snprintf(name, sizeof(name), "%s", rule->name);
        }
        const uint64_t output_mark = test->output.length;
        test->convert_attempts += 1;
        const uint32_t bytes = row.result_bytes;
        const uint32_t a_bytes = row.operand_bytes[0];
        bool ok = tier1_masks_test_expect(test, made && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's conversion's row");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * a_bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, a_bytes);
            if (masked)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + 1) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                if ((k & 1) != 0)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                }
                else
                {
                    tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, row.operand_bytes[2]);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the conversion's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const uint8_t* a = (const uint8_t*)in + k * a_bytes % 256;
                const uint8_t* w = (const uint8_t*)in + (k + 1) * bytes % 256;
                const uint64_t mask = masked && (k & 1) != 0 ? in[k] : UINT64_MAX;
                uint8_t* out = wanted + k * bytes;
                memset(out, 0, bytes);
                const uint32_t lanes = to > from ? bytes / to : a_bytes / from;
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    uint64_t raw = 0;
                    memcpy(&raw, a + lane * from, from);
                    const uint64_t value = tier1_masks_test_convert_lane(raw, from, to, is_signed, mode);
                    memcpy(out + lane * to, ((mask >> lane) & 1) != 0 ? (const uint8_t*)&value : w + lane * to, to);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        test->convert_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the conversions return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            arena_pop_to(&arenas[k], 0);
        }
    }
    type_test_table_close(&table);
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

// The front end's masked moves' selects and undefined vectors through Tier 0 and the IR to Tier 1 (#1840): every SELECT
// rule (`__builtin_ia32_select*` of each element and width) as check_x86_lower_masks moves its mask into the mask class,
// and every UNDEFINED rule, each as the program's only row, in 4 statements: a select of the vectors at k and k + 1
// (modulo 4, of the rule's bytes each) from the input buffer under the input word k, converted to the mask, stored into
// the output's vector k; an undefined vector's zeros stored there. tier1_masks_test_typed_pipeline takes it through the
// IR as its vselect or zero vector, which the IR's interpreter runs at every stage, and through Tier 1 natively, each
// against a C model: each lane the first vector's where the mask's bit is set, the second's elsewhere; zeros. Tier 0 runs
// no row of them here: its register-direct moves have no Zen 5 row (x86_zen5_rows.c) yet, which its builds require.
static void tier1_masks_test_selects(Tier1MasksTest* test)
{
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the selects' memory");
    uint64_t state = test->random ^ UINT64_C(0xbb67ae8584caa73b);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool select = rule->family == CHECK_X86_LOWER_SELECT;
        if (!select && rule->family != CHECK_X86_LOWER_UNDEFINED)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->select_attempts += 1;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL && (!select || check_x86_lower_masks(&row, &target));
        const uint32_t bytes = rule->bytes;
        const uint32_t slots = bytes / 8;
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == (select ? 3 : 0) && row.result_class == BUSTER_TYPED_CLASS_VECTOR && row.result_bytes == bytes,
                                          "the front end's select or undefined vector row, in the mask class at x86-64-v4");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * slots);
            if (select)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, row.operand_bytes[0]);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * slots);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, ((k + 1) & 3) * slots);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the select's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const uint8_t* w = (const uint8_t*)in + bytes * k;
                const uint8_t* a = (const uint8_t*)in + bytes * ((k + 1) & 3);
                uint8_t* out = wanted + bytes * k;
                for (uint32_t lane = 0; lane < bytes / rule->element; lane += 1)
                {
                    const bool set = select && (in[k] >> lane & 1) != 0;
                    if (select)
                    {
                        memcpy(out + rule->element * lane, (set ? w : a) + rule->element * lane, rule->element);
                    }
                    else
                    {
                        memset(out + rule->element * lane, 0, rule->element);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        test->select_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the selects return the scratch arena");
        arena_pop_to(&test->output, output_mark);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            arena_pop_to(&arenas[k], 0);
        }
    }
    for (uint32_t k = 0; k < 3; k += 1)
    {
        arena_release(&arenas[k]);
    }
    free(expected);
    free(inputs);
    free(program);
}

static __attribute__((unused)) int run_tier1_mask_tests(int argc, char** argv)
{
    uint64_t seed = 179;
    bool valid = true;
    bool print = false;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--print") == 0)
        {
            print = true;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            i += 1;
            char* end = 0;
            seed = strtoull(argv[i], &end, 0);
            valid = argv[i][0] != 0 && *end == 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-masks [--seed N] [--print]\n");
    }
    else
    {
        Tier1MasksTest test = {
            .random = seed * 2 + 1,
            .print = print,
            .v4 = malloc(X86_FORM_COUNT),
            .v3 = malloc(X86_FORM_COUNT),
            .v2 = malloc(X86_FORM_COUNT),
            .avx = malloc(X86_FORM_COUNT),
            .fp16 = malloc(X86_FORM_COUNT),
            .output = arena_reserve(64u << 20),
            .scratch = arena_reserve(64u << 20),
            .execution = arena_reserve(64u << 20),
        };
        uint8_t* baseline = malloc(X86_FORM_COUNT);
        uint8_t* foundation = malloc(X86_FORM_COUNT);
        const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && test.v4 != 0 && test.fp16 != 0 && test.v3 != 0 && test.v2 != 0 && test.avx != 0 && baseline != 0 && foundation != 0 && test.output.base != 0 && test.scratch.base != 0 && test.execution.base != 0;
        if (tier1_masks_test_expect(&test, ready, "initialize the tables and arenas"))
        {
            X86Target v4 = { 0 };
            const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
            tier1_masks_test_expect(&test, parsed, "the x86-64-v4 target");
            x86_target_forms(&v4, test.v4);
            memcpy(test.fp16, test.v4, X86_FORM_COUNT);
            test.fp16[X86_FORM_VFMULCPH_Z_Z_ZM512] = 1;
            test.forms = test.v4;
            const X86Target base = { .architecture = X86_ARCH_BASELINE };
            x86_target_forms(&base, baseline);
            test.baseline = baseline;
            X86Target v3 = { 0 };
            x86_target_parse("x86-64-v3", 9, &v3);
            x86_target_forms(&v3, test.v3);
            // x86-64-v2 with AVX, as Sandy Bridge is: AVX without AVX2.
            X86Target v2 = { 0 };
            x86_target_parse("x86-64-v2", 9, &v2);
            x86_target_forms(&v2, test.v2);
            const X86Target sandybridge = { .features = v2.features | BUSTER_X86_FEATURE(AVX), .architecture = X86_ARCH_OTHER };
            tier1_masks_test_expect(&test, (sandybridge.features & BUSTER_X86_FEATURE(AVX2)) == 0, "x86-64-v2 with AVX has no AVX2");
            x86_target_forms(&sandybridge, test.avx);
            const X86Target f = { .features = v3.features | BUSTER_X86_FEATURE(AVX512F), .architecture = X86_ARCH_OTHER };
            x86_target_forms(&f, foundation);
            test.foundation = foundation;
            // The native runs execute code checked against x86-64-v4's forms (the fixtures', the typed programs' and Tier
            // 0's rows), so they need every feature of it: AVX512F's words, DQ's bytes, BW's doublewords and quadwords,
            // and VL's EVEX forms at 128 and 256 bits, which the vectors' fixtures take (#1204's review).
            const X86Target host = x86_target_native();
            test.native = parsed && (host.features & v4.features) == v4.features;
            test.native_fp16 = (host.extensions & BUSTER_X86_EXTENSION(AVX512FP16)) != 0;
            for (uint32_t f_index = 0; f_index < BUSTER_ARRAY_LENGTH(tier1_masks_test_fixtures); f_index += 1)
            {
                tier1_masks_test_fixture(&test, &tier1_masks_test_fixtures[f_index]);
            }
            test.v4_features = v4.features;
            tier1_masks_test_mxcsr(&test);
            tier1_masks_test_folded_effects(&test);
            tier1_masks_test_typed(&test);
            tier1_masks_test_builtins(&test);
            tier1_masks_test_compares(&test);
            tier1_masks_test_selects(&test);
            tier1_masks_test_widths(&test);
            tier1_masks_test_permutes(&test);
            tier1_masks_test_conversions(&test);
            tier1_masks_test_shifts(&test);
            tier1_masks_test_mask_vectors(&test);
            tier1_masks_test_lane_ops(&test);
            tier1_masks_test_permute2s(&test);
            tier1_masks_test_ternlogs(&test);
            tier1_masks_test_compresses(&test);
            tier1_masks_test_masked_memory(&test);
            tier1_masks_test_lane_moves(&test);
            tier1_masks_test_frounds(&test);
            tier1_masks_test_cvtts(&test);
            tier1_masks_test_refusals(&test, baseline, foundation);
            tier1_masks_test_scan(&test);
            tier1_masks_test_forms(&test);
            const uint32_t runnable = test.native_fp16 ? BUSTER_ARRAY_LENGTH(tier1_masks_test_fixtures) : BUSTER_ARRAY_LENGTH(tier1_masks_test_fixtures) - BUSTER_TIER1_MASKS_TEST_FP16_FIXTURES;
            const uint64_t expected_runs = test.native ? 4u * runnable * BUSTER_TIER1_MASKS_TEST_INPUTS : 0;
            tier1_masks_test_expect(&test, test.native_fixtures == (test.native ? runnable : 0) && test.native_runs == expected_runs,
                                    "every fixture runs natively on every input by both paths, with rematerialization and without, where the host can");
            tier1_masks_test_expect(&test, test.recomputed != 0, "with rematerialization, general values beside the masks are recomputed");
            tier1_masks_test_expect(&test, test.refused == 10, "every refusal on both paths");
            // Tier 0's programs through the IR: every one taken whole, some with machine rows, and natively by both paths.
            tier1_masks_test_expect(&test, test.typed_programs == BUSTER_TIER1_MASKS_TEST_PROGRAMS && test.typed_machine != 0 && test.typed_machine < test.typed_programs,
                                    "Tier 0's mask programs go through the IR and Tier 1, those whose rows the decoders refuse as machine rows");
            tier1_masks_test_expect(&test, !test.native || test.typed_runs == 2u * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS * (test.typed_programs + test.builtin_rows + test.compare_rows + test.select_rows + test.width_rows + test.permute_rows + test.convert_rows +
                                                                               test.shift_rows + test.mask_vector_rows + test.lane_op_rows + test.permute2_rows + test.ternlog_rows + test.compress_rows +
                                                                               test.masked_rows + test.lane_rows + test.fround_rows + test.cvtt_rows),
                                    "every typed program and front-end row runs natively by both of Tier 1's paths");
            // The comparisons: 24 integer rules (bytes to quadwords, signed and unsigned, at 3 widths) at 8 immediates, 6
            // of them the IR's; 6 packed float rules at 32, 12 of them the IR's.
            tier1_masks_test_expect(&test, test.compare_attempts == 24 * 8 + 6 * 32 && test.compare_rows == test.compare_attempts && test.compare_decoded == 24 * 6 + 6 * 12,
                                    "every front-end comparison into a mask goes through Tier 0, the IR and Tier 1, those of an IR predicate as the IR's comparisons");
            tier1_masks_test_expect(&test, !test.native || test.compare_native == BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS * test.compare_rows, "Tier 0 runs every front-end comparison natively");
            // The selects (#1840): 24 SELECT rules (bytes to quadwords, floats, doubles, halves and bf16s at 3 widths) and 3
            // UNDEFINED ones, every one the IR's own operation.
            tier1_masks_test_expect(&test, test.select_attempts == 24 + 3 && test.select_rows == test.select_attempts,
                                    "every front-end select and undefined vector goes through Tier 0, the IR and Tier 1 as the IR's vselect or zero vector");
            // The width changes (#1840): every insert and masked extract rule at each part, and the 6 casts.
            tier1_masks_test_expect(&test, test.width_attempts > 6 && test.width_rows == test.width_attempts,
                                    "every front-end width change goes through the IR and Tier 1 as the IR's extract, widen or insert");
            // The permutes (#1840): vpermw at 3 widths, vpermd at 2 and vpermq at 1; vpshufb at 3; valignd and valignq at
            // 3 widths and the two shuffles of lanes at 2, at 8 immediates.
            tier1_masks_test_expect(&test, test.permute_attempts == 6 + 3 + 6 * 8 + 4 * 8 && test.permute_rows == test.permute_attempts,
                                    "every front-end permute goes through the IR and Tier 1 as the IR's permute");
        printf("test-tier1-masks: the front end's selects and undefined vectors: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its vselect and zero vector\n",
               test.select_rows);
            // The conversions (#1840): convertvector's and the masked truncations whose lanes fill their results.
            tier1_masks_test_expect(&test, test.convert_attempts > 40 && test.convert_rows == test.convert_attempts,
                                    "every front-end conversion goes through the IR and Tier 1 as the IR's extend or truncate");
            // The front end's rows: every rule of the five families, the shifts at every count.
            tier1_masks_test_expect(&test, test.builtin_attempts != 0 && test.builtin_rows == test.builtin_attempts, "every front-end opmask row goes through Tier 0, the IR and Tier 1");
            tier1_masks_test_expect(&test, !test.native || test.builtin_native == BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS * test.builtin_rows, "Tier 0 runs every front-end row natively");
        }
        printf("test-tier1-masks: Tier 0's mask programs: %u through the IR's interpreter, SSA, the optimizer and Tier 1 (%" PRIu64 " native runs), %u of them with machine rows, natively alone\n",
               test.typed_programs, test.typed_runs, test.typed_machine);
        printf("test-tier1-masks: the front end's opmask rows: %u through Tier 0 natively (%u runs), the IR's interpreter, SSA, the optimizer and Tier 1\n", test.builtin_rows, test.builtin_native);
        printf("test-tier1-masks: the front end's comparisons into masks: %u through Tier 0 natively (%u runs) and Tier 1, %u of them the IR's comparisons through its interpreter, SSA and the "
               "optimizer, the others machine rows\n",
               test.compare_rows, test.compare_native, test.compare_decoded);
        printf("test-tier1-masks: the front end's width changes: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its extract, widen and insert\n",
               test.width_rows);
        printf("test-tier1-masks: the front end's conversions: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its extend and truncate\n",
               test.convert_rows);
        tier1_masks_test_expect(&test, test.shift_attempts == 2 * 27 && test.shift_rows == test.shift_attempts,
                                "every front-end shift by an int goes through the IR and Tier 1 as the IR's shift, by a register and by an immediate");
        tier1_masks_test_expect(&test, test.mask_vector_attempts == 24 && test.mask_vector_rows == test.mask_vector_attempts,
                                "every front-end mask to vector and back goes through the IR and Tier 1 as the IR's select or comparison");
        tier1_masks_test_expect(&test, test.lane_op_attempts == 72 && test.lane_op_rows == test.lane_op_attempts,
                                "every front-end elementwise minimum, maximum and saturation goes through the IR and Tier 1 as the IR's lane_op");
        printf("test-tier1-masks: the front end's elementwise minima, maxima and saturations: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its lane_op\n",
               test.lane_op_rows);
        tier1_masks_test_expect(&test, test.permute2_attempts == 3 * (2 * 3 + 2 * 2) && test.permute2_rows == test.permute2_attempts,
                                "every permute of two tables goes through the IR and Tier 1 as the IR's permute2");
        printf("test-tier1-masks: the permutes of two tables: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its permute2\n",
               test.permute2_rows);
        tier1_masks_test_expect(&test, test.ternlog_attempts == 48 && test.ternlog_rows == test.ternlog_attempts,
                                "every bitwise function of three vectors goes through the IR and Tier 1 as the IR's ternlog");
        printf("test-tier1-masks: the bitwise functions of three vectors: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its ternlog\n",
               test.ternlog_rows);
        tier1_masks_test_expect(&test, test.compress_attempts == 12 && test.compress_rows == test.compress_attempts,
                                "every compress and expand goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the compresses and expands: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its compress and expand\n",
               test.compress_rows);
        tier1_masks_test_expect(&test, test.masked_attempts == 24 && test.masked_rows == test.masked_attempts,
                                "every masked load and store goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the masked loads and stores: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its masked_load and "
               "masked_store\n",
               test.masked_rows);
        tier1_masks_test_expect(&test, test.lane_attempts == 50 && test.lane_rows == test.lane_attempts, "every lane move goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the one-lane moves and movemasks: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its lane_extract, "
               "lane_insert and movemask\n",
               test.lane_rows);
        tier1_masks_test_expect(&test, test.fround_attempts == 32 && test.fround_rows == test.fround_attempts, "every floating operation at its own rounding goes through the IR and Tier 1");
        printf("test-tier1-masks: the floating operations at their own rounding: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its fround\n",
               test.fround_rows);
        tier1_masks_test_expect(&test, test.cvtt_attempts == 4 && test.cvtt_rows == test.cvtt_attempts, "every truncating conversion goes through the IR and Tier 1");
        printf("test-tier1-masks: the truncating conversions of lane 0: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its cvtt\n", test.cvtt_rows);
        printf("test-tier1-masks: the front end's masks to vectors and back: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as vpmovm2 and vpmov2m\n",
               test.mask_vector_rows);
        printf("test-tier1-masks: the front end's shifts by an int: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its shift\n", test.shift_rows);
        printf("test-tier1-masks: the front end's permutes: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its permute, shuffle, align and "
               "shuffle_lanes\n",
               test.permute_rows);
        tier1_masks_test_expect(&test, tier1_masks_test_row_occupancy(),
                                "VECTOR roles share a register where one is done with it before the other's pure write in a record whose destination may be a source, and only there (#1283, #1284)");
        tier1_masks_test_expect(&test, test.row_reloads != 0 && test.row_stagings != 0 && test.row_roles != 0 && test.row_k0 != 0 && test.row_splits != 0 && test.row_split_mutants != 0 && test.vector_moves != 0,
                                "the machine rows' and vectors' mutants include reloads left out, staged operands overwriting live registers, roles sharing a register, opmask registers made k0, splits and narrowed "
                                "moves (#1284)");
        printf("test-tier1-masks: machine rows' code certified, %u of their claims with a split; mutants refused: borrowed opmask registers' reloads left out %u of %u, staged operands overwriting a live register %u of %u, "
               "claimed roles sharing a register while both hold their values %u of %u (temporaries over a live operand %u of %u), opmask registers named by number claimed k0 %u of %u, splits corrupted %u of %u, "
               "vectors' moves narrowed to xmm %u of %u\n",
               test.row_splits, test.row_reloads_refused, test.row_reloads, test.row_stagings_refused, test.row_stagings, test.row_roles_refused, test.row_roles, test.row_overlaps_refused, test.row_overlaps,
               test.row_k0_refused, test.row_k0,
               test.row_split_refused, test.row_split_mutants, test.vector_moves_refused, test.vector_moves);
        tier1_masks_test_expect(&test, test.asm_certified != 0 && test.asm_reloads != 0 && test.asm_records != 0,
                                "the asm bodies' code is certified, and their mutants include masks' reloads left out and records changed (#1839)");
        printf("test-tier1-masks: asm bodies' code certified %u times; mutants refused: masks' reloads after a body left out %u of %u, a body's records changed %u of %u\n", test.asm_certified,
               test.asm_reloads_refused, test.asm_reloads, test.asm_records_refused, test.asm_records);
        printf("test-tier1-masks: %" PRIu64 " checks, %" PRIu64 " failures; %s: %" PRIu64 " runs against the interpreter; %" PRIu64 " records, %" PRIu64 " mask moves, %" PRIu64 " through k0, %" PRIu64 " mask spills, %" PRIu64 " reloads, %" PRIu64 " masks saved around calls, %" PRIu64 " through frame objects, %" PRIu64 " general values recomputed beside them; %u refusals (seed=%" PRIu64 ")\n",
               test.checks, test.failures, test.native ? "native" : "no AVX-512 here, compiled only", test.native_runs, test.records, test.mask_moves, test.scratch_moves, test.mask_stores, test.mask_loads, test.call_saves, test.frame_accesses,
               test.recomputed, test.refused, seed);
        result = test.failures == 0 ? 0 : 1;
        arena_release(&test.execution);
        arena_release(&test.scratch);
        arena_release(&test.output);
        free(foundation);
        free(baseline);
        free(test.v4);
        free(test.v3);
        free(test.v2);
        free(test.avx);
        free(test.fp16);
    }
    return result;
}
