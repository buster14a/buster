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
// The fixtures are runtime data (#2053), tests/tier1/masks/'s catalog and IR files (the comment above
// tier1_masks_test_fixture_root): and, or, xor, not and copies at the four widths; constants (zero, every bit and others); a loop whose
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
// interpreter through SSA and the optimizer, and compiled by Tier 1 natively. --print prints each fixture's records,
// as GNU as reads them.

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
    // #2830: the functions whose code named ymm0 to zmm15, so a vzeroupper before their calls and rets.
    uint64_t upper_dirty;
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
    // x86-64-v4's forms with AVX512-VNNI's, for the fixtures of its dot products (#2780, Tier1MasksFixture.vnni).
    uint8_t* vnni;
    const uint8_t* forms;
    // x86-64-v4's features, which the typed programs' lowering takes as its output target (ir_lower_typed_target).
    uint64_t v4_features;
    bool native;
    // Whether the host has AVX512-FP16 too, which a fixture of its rows needs to run natively; and AVX512-VNNI, which a
    // fixture of dot products needs (#2780).
    bool native_fp16;
    bool native_vnni;
    // The fixtures that ran natively, by both paths, with and without rematerialization.
    uint32_t native_fixtures;
    // --print: each fixture's records, as GNU as reads them.
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
    // The gathers, compressing stores and truncating stores (#1840, tier1_masks_test_gathers,
    // tier1_masks_test_compress_stores and tier1_masks_test_truncating_stores): those tried, and those through every
    // stage as the IR's.
    uint32_t gather_attempts;
    uint32_t gather_rows;
    uint32_t scatter_attempts;
    uint32_t scatter_rows;
    uint32_t compress_store_attempts;
    uint32_t compress_store_rows;
    uint32_t truncating_store_attempts;
    uint32_t truncating_store_rows;
    // The scalar fused multiply-adds (#1840, tier1_masks_test_fmas): those tried, and those through every stage as the
    // IR's IR_FMA.
    uint32_t fma_attempts;
    uint32_t fma_rows;
    // One lane's moves and the lanes' sign bits (#1840, tier1_masks_test_lane_moves): those tried, and those through every
    // stage as the IR's.
    uint32_t lane_attempts;
    uint32_t lane_rows;
    // The reductions (#1840, tier1_masks_test_reductions): those tried, and those through every stage as the IR's.
    uint32_t reduce_attempts;
    uint32_t reduce_rows;
    // The lanes' unary operations (#1840, tier1_masks_test_lane_unaries): those tried, and those through every stage as
    // the IR's.
    uint32_t unary_attempts;
    uint32_t unary_rows;
    // The floating operations at their own rounding (#1840, tier1_masks_test_frounds): those tried, and those through
    // every stage as the IR's.
    uint32_t fround_attempts;
    uint32_t fround_rows;
    // The truncating conversions of lane 0 (#1840, tier1_masks_test_cvtts): those tried, and those through every stage.
    uint32_t cvtt_attempts;
    uint32_t cvtt_rows;
    // The byte shuffles by an immediate (#1840, tier1_masks_test_shuffle_immediates): those tried, and those through every
    // stage as the IR's shuffle by a constant, which Tier 1 emits as the immediate form.
    uint32_t shuffle_attempts;
    uint32_t shuffle_rows;
    // Forms whose records tier1_masks_test_typed_pipeline counts in Tier 1's code by both paths (X86_FORM_NONE for none),
    // and their counts.
    uint16_t watched_forms[2];
    uint32_t watched_records[2];
    // The SSE carriers (#1840, tier1_masks_test_carriers): those tried, and those through every stage as the IR's
    // lane_extract into a carrier and extend of one.
    uint32_t carrier_attempts;
    uint32_t carrier_rows;
    // The truncations into an SSE carrier and the permutes of two carriers (#2780, tier1_masks_test_carrier_results):
    // those tried, and those through every stage as the IR's.
    uint32_t carrier_result_attempts;
    uint32_t carrier_result_rows;
    // The bit shuffles into a mask (#1840, tier1_masks_test_bitshuffles): those tried, and those through every stage as the
    // IR's bitshuffle under the writemask.
    uint32_t bitshuffle_attempts;
    uint32_t bitshuffle_rows;
    // The quadword operations (#1840, tier1_masks_test_quadword_ops): those tried, and those through every stage as the
    // IR's quadword_op.
    uint32_t quadword_attempts;
    uint32_t quadword_rows;
    // The pairwise operations and the dot products (#2780, tier1_masks_test_pairwise_dots): those tried, and those through
    // every stage as the IR's pairwise or dot.
    uint32_t pairwise_attempts;
    uint32_t pairwise_rows;
    // The blends by a vector's signs (#2780, tier1_masks_test_blendvs): those tried, and those through every stage as the
    // IR's select, Tier 1's one blend.
    uint32_t blendv_attempts;
    uint32_t blendv_rows;
    // The funnel shifts (#1840, tier1_masks_test_funnels): those tried, and those through every stage as the IR's funnel.
    uint32_t funnel_attempts;
    uint32_t funnel_rows;
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
    // Operations under a mask as a select's true arm (#1840, BUSTER_TIER1_VECTOR_MASKED_BINARY): how many of its records
    // are an arithmetic one with an opmask decoration (no compare, blend or move), by either path. Checked where it isn't 0.
    uint32_t masked_operations;
    // A row of AVX512-FP16's (#1283's review): its forms are test->fp16's, and it runs natively only where the host has
    // AVX512-FP16 (not the 9700X's Zen 5).
    bool fp16;
    // Dot products of AVX512-VNNI's (#2780): its forms are test->vnni's, and it runs natively only where the host has it.
    bool vnni;
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

// The fixtures are runtime data (#2053): tests/tier1/masks/ under the fixture root, a catalog and a file of a function's
// IR text a case, which the test reads when it runs, so that editing a case or adding one compiles, links and bakes
// nothing. The catalog's first line is `buster-fixtures v1 tier1-masks`; each later line not starting with '#' is a case,
// four tab-separated fields: its ID, unique, of letters, digits, '.', '_' and '-'; its FILE, in the same directory,
// unique, of the same characters, ending in .ir; its FLAGS, comma-separated, or - for none; and its NAME, what it checks,
// which a failure prints. The flags are Tier1MasksFixture's fields: count=N, spills, calls, cycle, frame-accesses=N,
// divisions, vector-cycle, borrows, v3, avx, v2, baseline, foundation, rows=N, row-moves=N, masked-compares=N,
// masked-operations=N, fp16, vnni, exclusive and asm-body; and its C functions by name, model=NAME of
// tier1_masks_test_models' and prepare=NAME of tier1_masks_test_prepares'. A case's file is its text's bytes as they
// are; one with a NUL in it is refused, since the parser reads the text to its first NUL. The cases run in the catalog's
// order. A missing, unreadable or malformed catalog, a missing or unreadable file, a repeated ID or file, an unknown or
// repeated flag, a file in the directory that no case names, or no case at all fails the run: no case is skipped.
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <sys/stat.h>

// The fixture root: tests/ under the working directory, or test-tier1-masks' --fixtures DIR.
static const char* tier1_masks_test_fixture_root = "tests";
static const char tier1_masks_test_fixture_header[] = "buster-fixtures v1 tier1-masks";

enum
{
    // A catalog's cases, and a catalog's or a case file's bytes, at most.
    BUSTER_TIER1_MASKS_TEST_CASES = 1024,
    BUSTER_TIER1_MASKS_TEST_FILE_BYTES = 1 << 20,
};

// The runtime fixtures: their cases, and their names and texts, in `storage`.
typedef struct Tier1MasksFixtures Tier1MasksFixtures;
struct Tier1MasksFixtures
{
    Arena storage;
    Tier1MasksFixture* cases;
    const char** files;
    uint32_t count;
    char problem[512];
};

// The C models a catalog's model=NAME names: the buffer after a run where the interpreter can't run the fixture.
typedef struct Tier1MasksTestModel Tier1MasksTestModel;
struct Tier1MasksTestModel
{
    const char* name;
    void (*model)(const uint8_t* input, uint64_t count, uint8_t* output);
};

static const Tier1MasksTestModel tier1_masks_test_models[] = {
    { "asm", tier1_masks_test_asm_model },
    { "rows", tier1_masks_test_rows_model },
    { "vex-rows", tier1_masks_test_vex_rows_model },
    { "pressure64", tier1_masks_test_pressure_model64 },
    { "pressure32", tier1_masks_test_pressure_model32 },
    { "read-only", tier1_masks_test_read_only_model },
    { "floating", tier1_masks_test_floating_model },
    { "carriers", tier1_masks_test_carriers_model },
    { "vector-rows", tier1_masks_test_vector_rows_model },
    { "gather", tier1_masks_test_gather_model },
    { "complex", tier1_masks_test_complex_model },
};

// The C preparations a catalog's prepare=NAME names: what makes a random input one the fixture can run on.
typedef struct Tier1MasksTestPrepare Tier1MasksTestPrepare;
struct Tier1MasksTestPrepare
{
    const char* name;
    void (*prepare)(uint8_t* input);
};

static const Tier1MasksTestPrepare tier1_masks_test_prepares[] = {
    { "gather", tier1_masks_test_gather_prepare },
};

// The flags of a field of its own: a bool's, or a count's (NAME=N).
typedef struct Tier1MasksTestFlag Tier1MasksTestFlag;
struct Tier1MasksTestFlag
{
    const char* name;
    uint32_t offset;
    bool count;
};

static const Tier1MasksTestFlag tier1_masks_test_flags[] = {
    { "count", offsetof(Tier1MasksFixture, count), true },
    { "spills", offsetof(Tier1MasksFixture, spills), false },
    { "calls", offsetof(Tier1MasksFixture, calls), false },
    { "cycle", offsetof(Tier1MasksFixture, cycle), false },
    { "frame-accesses", offsetof(Tier1MasksFixture, frame_accesses), true },
    { "divisions", offsetof(Tier1MasksFixture, divisions), false },
    { "vector-cycle", offsetof(Tier1MasksFixture, vector_cycle), false },
    { "borrows", offsetof(Tier1MasksFixture, borrows), false },
    { "v3", offsetof(Tier1MasksFixture, v3), false },
    { "avx", offsetof(Tier1MasksFixture, avx), false },
    { "v2", offsetof(Tier1MasksFixture, v2), false },
    { "baseline", offsetof(Tier1MasksFixture, baseline), false },
    { "foundation", offsetof(Tier1MasksFixture, foundation), false },
    { "rows", offsetof(Tier1MasksFixture, rows), true },
    { "row-moves", offsetof(Tier1MasksFixture, row_moves), true },
    { "masked-compares", offsetof(Tier1MasksFixture, masked_compares), true },
    { "masked-operations", offsetof(Tier1MasksFixture, masked_operations), true },
    { "fp16", offsetof(Tier1MasksFixture, fp16), false },
    { "vnni", offsetof(Tier1MasksFixture, vnni), false },
    { "exclusive", offsetof(Tier1MasksFixture, exclusive), false },
    { "asm-body", offsetof(Tier1MasksFixture, asm_body), false },
};

// The bytes of the file at `path`, NUL-terminated, in `storage`, and their count in `length`; null, with the reason in
// `fixtures->problem`, when it can't be read, is larger than BUSTER_TIER1_MASKS_TEST_FILE_BYTES or holds a NUL.
static char* tier1_masks_test_fixture_read(Tier1MasksFixtures* fixtures, const char* path, uint64_t* length)
{
    char* bytes = 0;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    const bool regular = file >= 0 && fstat(file, &status) == 0 && S_ISREG(status.st_mode);
    if (!regular)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s", path);
    }
    else if (status.st_size > BUSTER_TIER1_MASKS_TEST_FILE_BYTES)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s is larger than %u bytes", path, (unsigned)BUSTER_TIER1_MASKS_TEST_FILE_BYTES);
    }
    else
    {
        const uint64_t size = (uint64_t)status.st_size;
        char* buffer = arena_push(&fixtures->storage, size + 1, 1);
        uint64_t done = 0;
        ssize_t got = 1;
        while (buffer != 0 && done < size && got > 0)
        {
            got = read(file, buffer + done, size - done);
            done += got > 0 ? (uint64_t)got : 0;
        }
        if (buffer == 0 || done != size)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "can't read %s whole", path);
        }
        else if (memchr(buffer, 0, size) != 0)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s holds a NUL byte", path);
        }
        else
        {
            buffer[size] = 0;
            *length = size;
            bytes = buffer;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return bytes;
}

// Whether `text` is 1 to `limit` letters, digits, '.', '_' and '-'.
static bool tier1_masks_test_fixture_word(const char* text, size_t limit)
{
    const size_t length = strlen(text);
    bool word = length != 0 && length <= limit;
    for (size_t i = 0; word && i < length; i += 1)
    {
        const char c = text[i];
        word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
    }
    return word;
}

// One flag into `fixture`, `seen` marking the fields set; false for an unknown or repeated one, or a count that isn't a
// decimal number from 1 to UINT32_MAX.
static bool tier1_masks_test_fixture_flag(Tier1MasksFixture* fixture, const char* flag, uint32_t* seen)
{
    const char* equals = strchr(flag, '=');
    const size_t name_length = equals != 0 ? (size_t)(equals - flag) : strlen(flag);
    const uint32_t flag_count = (uint32_t)BUSTER_ARRAY_LENGTH(tier1_masks_test_flags);
    const uint32_t model_bit = flag_count;
    const uint32_t prepare_bit = flag_count + 1;
    bool known = false;
    for (uint32_t f = 0; !known && f < flag_count; f += 1)
    {
        const Tier1MasksTestFlag* entry = &tier1_masks_test_flags[f];
        const bool named = strlen(entry->name) == name_length && strncmp(entry->name, flag, name_length) == 0 && (*seen & (UINT32_C(1) << f)) == 0;
        if (named && entry->count && equals != 0)
        {
            char* end = 0;
            const unsigned long long value = equals[1] >= '1' && equals[1] <= '9' ? strtoull(equals + 1, &end, 10) : 0;
            known = end != 0 && *end == 0 && value <= UINT32_MAX;
            if (known)
            {
                const uint32_t number = (uint32_t)value;
                memcpy((uint8_t*)fixture + entry->offset, &number, sizeof(number));
            }
        }
        else if (named && !entry->count && equals == 0)
        {
            const bool set = true;
            memcpy((uint8_t*)fixture + entry->offset, &set, sizeof(set));
            known = true;
        }
        *seen |= known ? UINT32_C(1) << f : 0;
    }
    if (!known && equals != 0 && name_length == 5 && strncmp(flag, "model", 5) == 0 && (*seen & (UINT32_C(1) << model_bit)) == 0)
    {
        for (uint32_t m = 0; !known && m < BUSTER_ARRAY_LENGTH(tier1_masks_test_models); m += 1)
        {
            known = strcmp(equals + 1, tier1_masks_test_models[m].name) == 0;
            fixture->model = known ? tier1_masks_test_models[m].model : fixture->model;
        }
        *seen |= known ? UINT32_C(1) << model_bit : 0;
    }
    if (!known && equals != 0 && name_length == 7 && strncmp(flag, "prepare", 7) == 0 && (*seen & (UINT32_C(1) << prepare_bit)) == 0)
    {
        for (uint32_t p = 0; !known && p < BUSTER_ARRAY_LENGTH(tier1_masks_test_prepares); p += 1)
        {
            known = strcmp(equals + 1, tier1_masks_test_prepares[p].name) == 0;
            fixture->prepare = known ? tier1_masks_test_prepares[p].prepare : fixture->prepare;
        }
        *seen |= known ? UINT32_C(1) << prepare_bit : 0;
    }
    return known;
}

// A case's comma-separated flags, or -, into `fixture`; false, with the reason in `fixtures->problem`, for an unknown or
// repeated one.
static bool tier1_masks_test_fixture_flags(Tier1MasksFixtures* fixtures, char* flags, Tier1MasksFixture* fixture, uint32_t line)
{
    bool known = true;
    uint32_t seen = 0;
    if (strcmp(flags, "-") != 0)
    {
        char* flag = flags;
        while (known && flag != 0)
        {
            char* comma = strchr(flag, ',');
            if (comma != 0)
            {
                *comma = 0;
            }
            known = tier1_masks_test_fixture_flag(fixture, flag, &seen);
            if (!known)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "catalog line %u: unknown or repeated flag '%s'", line, flag);
            }
            flag = comma != 0 ? comma + 1 : 0;
        }
    }
    return known;
}

// Whether no file in `directory` but catalog.tsv is outside the catalog's.
static bool tier1_masks_test_fixture_orphans(Tier1MasksFixtures* fixtures, const char* directory)
{
    DIR* listing = opendir(directory);
    bool clean = listing != 0;
    if (!clean)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "can't list %s", directory);
    }
    for (struct dirent* entry = clean ? readdir(listing) : 0; clean && entry != 0; entry = readdir(listing))
    {
        const char* name = entry->d_name;
        bool named = strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "catalog.tsv") == 0;
        for (uint32_t i = 0; !named && i < fixtures->count; i += 1)
        {
            named = strcmp(fixtures->files[i], name) == 0;
        }
        if (!named)
        {
            snprintf(fixtures->problem, sizeof(fixtures->problem), "%s/%s is in no catalog line", directory, name);
            clean = false;
        }
    }
    if (listing != 0)
    {
        closedir(listing);
    }
    return clean;
}

// The cases of tests/tier1/masks/'s catalog under the fixture root, and their texts; false, with the reason in
// `fixtures->problem`, when any of it is wrong (the comment above tier1_masks_test_fixture_root).
static bool tier1_masks_test_fixtures_load(Tier1MasksFixtures* fixtures)
{
    *fixtures = (Tier1MasksFixtures){ .storage = arena_reserve(UINT64_C(1) << 28) };
    char directory[PATH_MAX];
    char path[PATH_MAX + 64];
    snprintf(directory, sizeof(directory), "%s/tier1/masks", tier1_masks_test_fixture_root);
    snprintf(path, sizeof(path), "%s/catalog.tsv", directory);
    fixtures->cases = arena_push(&fixtures->storage, sizeof(Tier1MasksFixture) * BUSTER_TIER1_MASKS_TEST_CASES, 8);
    fixtures->files = arena_push(&fixtures->storage, sizeof(const char*) * BUSTER_TIER1_MASKS_TEST_CASES, 8);
    const char** identifiers = arena_push(&fixtures->storage, sizeof(const char*) * BUSTER_TIER1_MASKS_TEST_CASES, 8);
    uint64_t length = 0;
    char* catalog = fixtures->cases != 0 && fixtures->files != 0 && identifiers != 0 ? tier1_masks_test_fixture_read(fixtures, path, &length) : 0;
    bool valid = catalog != 0;
    if (valid && (length == 0 || catalog[length - 1] != '\n'))
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s doesn't end with a newline", path);
        valid = false;
    }
    uint32_t line = 0;
    char* cursor = catalog;
    while (valid && cursor < catalog + length)
    {
        char* end = strchr(cursor, '\n');
        *end = 0;
        line += 1;
        if (line == 1)
        {
            valid = strcmp(cursor, tier1_masks_test_fixture_header) == 0;
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s's first line isn't '%s'", path, tier1_masks_test_fixture_header);
            }
        }
        else if (cursor[0] != '#')
        {
            char* fields[4] = { cursor, 0, 0, 0 };
            uint32_t count = 1;
            for (char* tab = strchr(cursor, '\t'); tab != 0 && count < 5; tab = strchr(tab + 1, '\t'))
            {
                *tab = 0;
                if (count < 4)
                {
                    fields[count] = tab + 1;
                }
                count += 1;
            }
            const size_t file_length = count == 4 ? strlen(fields[1]) : 0;
            valid = count == 4 && tier1_masks_test_fixture_word(fields[0], 64) && tier1_masks_test_fixture_word(fields[1], 128) && file_length > 3 &&
                    strcmp(fields[1] + file_length - 3, ".ir") == 0 && fields[3][0] != 0 && fixtures->count < BUSTER_TIER1_MASKS_TEST_CASES;
            if (!valid)
            {
                snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u isn't ID, FILE.ir, FLAGS and NAME, tab-separated", path, line);
            }
            for (uint32_t i = 0; valid && i < fixtures->count; i += 1)
            {
                valid = strcmp(identifiers[i], fields[0]) != 0 && strcmp(fixtures->files[i], fields[1]) != 0;
                if (!valid)
                {
                    snprintf(fixtures->problem, sizeof(fixtures->problem), "%s line %u repeats an ID or a file", path, line);
                }
            }
            Tier1MasksFixture* fixture = valid ? &fixtures->cases[fixtures->count] : 0;
            if (valid)
            {
                *fixture = (Tier1MasksFixture){ .name = fields[3] };
                valid = tier1_masks_test_fixture_flags(fixtures, fields[2], fixture, line);
            }
            if (valid)
            {
                char text_path[PATH_MAX + 256];
                snprintf(text_path, sizeof(text_path), "%s/%s", directory, fields[1]);
                uint64_t text_length = 0;
                fixture->text = tier1_masks_test_fixture_read(fixtures, text_path, &text_length);
                valid = fixture->text != 0;
            }
            if (valid)
            {
                identifiers[fixtures->count] = fields[0];
                fixtures->files[fixtures->count] = fields[1];
                fixtures->count += 1;
            }
        }
        cursor = end + 1;
    }
    if (valid && fixtures->count == 0)
    {
        snprintf(fixtures->problem, sizeof(fixtures->problem), "%s names no case", path);
        valid = false;
    }
    return valid && tier1_masks_test_fixture_orphans(fixtures, directory);
}

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

// A function from IR text, and its dominance (dominators.c's), in the test's output arena.
static bool tier1_masks_test_dominance(Tier1MasksTest* test, const IrFunction* function, IrDominance* dominance);

static bool tier1_masks_test_parse(Tier1MasksTest* test, const char* text, IrFunction* function, IrDominance* dominance)
{
    const String8 source = { .pointer = (char*)text, .length = strlen(text) };
    const IrParseResult parsed = ir_parse(source, &test->output, &test->scratch, function);
    const bool ok = tier1_masks_test_expect(test, parsed.error == IR_PARSE_OK, "the IR parser takes the fixture");
    if (!ok)
    {
        printf("test-tier1-masks: parse error %u line %" PRIu64 "\n", (unsigned)parsed.error, parsed.line);
    }
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

// #2830: the upper state at the function's exits. Code that names ymm0 to ymm15 or zmm0 to zmm15 (x86_upper_dirty)
// has a vzeroupper right before each call, where no call passes a ymm or zmm argument (tier1_select_call_wide), and
// before each ret's epilogue (its pops, rsp's add and the frame pointer's reset), where the result isn't a ymm or zmm
// register's (tier1_select_result_wide); code that doesn't has none, but a machine row's own. No legacy SSE record is
// left at a target with AVX (tier1_emit_vex).
static void tier1_masks_test_upper(Tier1MasksTest* test, const Tier1SelectPlan* plan, const Tier1EmitResult* emitted, const uint8_t* forms)
{
    const IrFunction* function = plan->function;
    const Tier1SelectCode* code = &emitted->code;
    const bool dirty = x86_upper_dirty(code->records, code->count);
    bool wide_calls = false;
    bool rows = false;
    bool body = false;
    for (uint32_t node = 0; node < function->instruction_count; node += 1)
    {
        wide_calls = wide_calls || (tier1_select_function_call(function, node) && tier1_select_call_wide(function, &function->calls[function->b[node]]));
        rows = rows || (function->op[node] == IR_CALL && function->calls[function->b[node]].row != 0);
        body = body || tier1_select_body_call(function, node);
    }
    const bool result_wide = tier1_select_result_wide(function);
    const bool avx = x86_target_form_allowed(forms, X86_FORM_VADDSS_X_X_XM32);
    bool cleared = emitted->zero_upper == (dirty && x86_target_form_allowed(forms, X86_FORM_VZEROUPPER));
    uint32_t zeroes = 0;
    uint32_t legacy = 0;
    for (uint32_t r = 0; r < code->count; r += 1)
    {
        const uint32_t form = code->records[r].form & BUSTER_X86_FORM_NUMBER;
        zeroes += form == X86_FORM_VZEROUPPER;
        legacy += avx && !body && x86_vex_forms[form % X86_FORM_COUNT] != 0;
        const bool call = form == X86_FORM_CALL_REL32 || form == X86_FORM_CALL_RM64;
        cleared = cleared && (!call || !emitted->zero_upper || wide_calls || (r != 0 && code->records[r - 1].form == X86_FORM_VZEROUPPER));
        uint32_t before = r;
        while (form == X86_FORM_RET && before != 0 &&
               (code->records[before - 1].form == X86_FORM_POP_R64 || code->records[before - 1].form == X86_FORM_ADD_RM64_IMM8 || code->records[before - 1].form == X86_FORM_ADD_RM64_IMM32 ||
                code->records[before - 1].form == X86_FORM_LEA_R64_M || code->records[before - 1].form == X86_FORM_MOV_RM64_R64))
        {
            before -= 1;
        }
        cleared = cleared && (form != X86_FORM_RET || !emitted->zero_upper || result_wide || (before != 0 && code->records[before - 1].form == X86_FORM_VZEROUPPER));
    }
    tier1_masks_test_expect(test, cleared && (emitted->zero_upper || rows || zeroes == 0), "a vzeroupper before each call and ret of a function that names ymm0 to zmm15, and none elsewhere (#2830)");
    tier1_masks_test_expect(test, legacy == 0, "no legacy SSE record at a target with AVX (#2830)");
    test->upper_dirty += emitted->zero_upper;
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
    // The code's literals after the helper (#2744, tier1_emit_place_literals).
    const uint64_t literal_start = helper_start + BUSTER_X86_ENCODER_CAPACITY(helper_count);
    *mapped = (literal_start + tier1_emit_literal_bytes(emitted) + 4095) & ~UINT64_C(4095);
    uint8_t* machine = mmap(0, *mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
    uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count) * sizeof(uint32_t));
    bool ok = tier1_masks_test_expect(test, machine != MAP_FAILED && offsets != 0 && ends != 0, "the encoder's buffers");
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
        ok = tier1_masks_test_expect(test, size > 0, "encode the code");
        const int64_t helper_size = x86_encode(helper, helper_count, machine + helper_start, BUSTER_X86_ENCODER_CAPACITY(helper_count), 0, 0);
        ok = ok && tier1_masks_test_expect(test, helper_size > 0, "encode the helper");
        test->records += code->count;
    }
    ok = ok && tier1_masks_test_expect(test, tier1_emit_place_literals(emitted, machine, ends, literal_start, *mapped), "the code's literals are placed");
    for (uint32_t reference = 0; ok && reference < emitted->reference_count; reference += 1)
    {
        const LinkReference* fixup = &emitted->references[reference];
        if (fixup->target >= BUSTER_TIER1_EMIT_LITERAL)
        {
            continue;
        }
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
    free(offsets);
    free(ends);
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
        const Tier1SelectError selected = ok ? tier1_select_plan(out, &dominance, &test->output, &test->scratch, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
        ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, "select MXCSR fixture");
        const Tier1EmitOptions options = { .certify = true, .register_mask = tier1_liveness_register_mask(), .rematerialize = true };
        Tier1EmitResult emitted = { 0 };
        const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->v4, &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_INVALID;
        ok = tier1_masks_test_expect(test, error == TIER1_EMIT_OK, "emit MXCSR fixture");
        if (ok)
        {
            tier1_masks_test_upper(test, &plan, &emitted, test->v4);
        }
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

static void tier1_masks_test_path(Tier1MasksTest* test, const Tier1MasksFixture* fixture, const IrFunction* function, const IrDominance* dominance, bool rematerialize, const uint8_t* inputs,
                                  const uint64_t* counts, const uint8_t* expected)
{
    const uint64_t output_mark = test->output.length;
    Tier1SelectPlan plan;
    uint32_t node = BUSTER_IR_NONE;
    const Tier1SelectError selected = tier1_select_plan(function, dominance, &test->output, &test->scratch, &plan, &node);
    bool ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, fixture->name);
    if (!ok)
    {
        printf("test-tier1-masks: %s: selection error %u at node %u\n", fixture->name, (unsigned)selected, node);
    }
    const Tier1EmitOptions options = {
        .certify = true,
        .register_mask = tier1_liveness_register_mask(),
        // These fixtures specify pressure past fifteen vectors or an exact row-move shape under that allocation.
        // Keep that input explicit; tier1_upper_vectors_test.c separately qualifies upper-register pressure and rows.
        .vector_register_mask = fixture->spills || fixture->rows != 0 ? 0x7fffu : 0,
        .rematerialize = rematerialize,
    };
    Tier1EmitResult emitted = { 0 };
    if (ok)
    {
        const Tier1EmitError error = tier1_emit_function(&plan, &options, test->forms, &test->output, &test->scratch, &emitted, &node);
        ok = tier1_masks_test_expect(test, error == TIER1_EMIT_OK && emitted.general, fixture->name);
        if (!ok)
        {
            printf("test-tier1-masks: %s: emission error %u at node %u\n", fixture->name, (unsigned)error, node);
        }
    }
    if (ok)
    {
        tier1_masks_test_row_mutants(test, &plan, &emitted);
        tier1_masks_test_asm_mutants(test, &plan, &emitted);
    }
    if (ok)
    {
        // Every value a call stores, a mask's opmask row among them, is loaded back (#1116).
        tier1_masks_test_expect(test, tier1_emit_test_saves_loaded(&emitted) && tier1_emit_test_saves_seen(&emitted), "every value a call stores is loaded back");
        tier1_masks_test_expect(test, tier1_emit_test_general_rows_used(&emitted), "a general call-save row is a register some call stores (#177)");
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
            printf("test-tier1-masks: %s: %u rows, %u moves around them (%u and %u expected)\n", fixture->name, emitted.machine_rows, emitted.row_moves, fixture->rows, row_moves);
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
            printf("test-tier1-masks: %s: %u masked comparisons (%u expected)\n", fixture->name, masked_compares, fixture->masked_compares);
        }
        // The masked operations' records (#1840): an arithmetic record under a writemask.
        uint32_t masked_operations = 0;
        for (uint32_t r = 0; fixture->masked_operations != 0 && r < emitted.code.count; r += 1)
        {
            const X86Instruction* record = &emitted.code.records[r];
            const char* mnemonic = x86_form_description(record->form)->mnemonic;
            const bool other = strncmp(mnemonic, "vpcmp", 5) == 0 || strncmp(mnemonic, "vcmpp", 5) == 0 || strncmp(mnemonic, "vpblendm", 8) == 0 ||
                               strncmp(mnemonic, "vmov", 4) == 0 || mnemonic[0] == 'k';
            masked_operations += !other && (record->decorations & BUSTER_X86_DECORATION_MASK) != 0 ? 1 : 0;
        }
        if (fixture->masked_operations != 0 && !tier1_masks_test_expect(test, masked_operations == fixture->masked_operations, "a select of an operation is the operation under its mask"))
        {
            printf("test-tier1-masks: %s: %u masked operations (%u expected)\n", fixture->name, masked_operations, fixture->masked_operations);
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
    if (test->print && ok)
    {
        printf("test-tier1-masks: %s:\n", fixture->name);
    }
    if (ok)
    {
        tier1_masks_test_upper(test, &plan, &emitted, test->forms);
    }
    uint8_t* machine = ok ? tier1_masks_test_load(test, &emitted, &mapped) : MAP_FAILED;
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
                    printf("test-tier1-masks: %s, input %u: byte %u is %02x, the interpreter's %02x\n", fixture->name, input, byte, actual[byte], wanted[byte]);
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
    test->native = native && (!fixture->fp16 || test->native_fp16) && (!fixture->vnni || test->native_vnni);
    test->native_fixtures += test->native ? 1 : 0;
    test->forms = fixture->fp16 ? test->fp16 : fixture->vnni ? test->vnni : fixture->foundation ? test->foundation : fixture->avx ? test->avx : fixture->v3 ? test->v3 : fixture->v2 ? test->v2 :
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
        // With rematerialization (as the C driver's --tier1 runs) and without.
        for (uint32_t run = 0; ok && run < 2; run += 1)
        {
            tier1_masks_test_path(test, fixture, &function, &dominance, run != 0, inputs, counts, expected);
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
        tier1_masks_test_expect(test, !ir_function_has_vectors(&function), "masks' operations hold no vector");
        for (uint32_t place = 0; place < count; place += 1)
        {
            const uint8_t saved_op = op[place];
            const uint8_t saved_type = type[place];
            for (uint32_t v = 0; v < 6; v += 1)
            {
                op[place] = vectors[v][0];
                type[place] = vectors[v][1];
                tier1_masks_test_expect(test, ir_function_has_vectors(&function), "a vector type or operation at any place, the tail's last among them");
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
// forms at emission, on both paths. `logic` is the catalog's first fixture, "logic at every width", whose masks of every
// width the targets without them refuse.
static void tier1_masks_test_refusals(Tier1MasksTest* test, const uint8_t* baseline, const uint8_t* foundation, const char* logic)
{
    const char* const texts[] = {
        tier1_masks_test_vector,
        tier1_masks_test_select,
        logic,
        logic,
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
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1SelectError selected = tier1_select_plan(&function, &dominance, &test->output, &test->scratch, &plan, &node);
            const Tier1EmitOptions options = {
                .certify = true,
                .register_mask = tier1_liveness_register_mask(),
            };
            Tier1EmitResult emitted = { 0 };
            const Tier1EmitError error = selected == TIER1_SELECT_OK ? tier1_emit_function(&plan, &options, targets[c], &test->output, &test->scratch, &emitted, &node) : TIER1_EMIT_UNSUPPORTED;
            const bool selection = c < 2;
            const bool expected = selection ? selected == TIER1_SELECT_ERROR_UNSUPPORTED : refused[c] ? selected == TIER1_SELECT_OK && error == TIER1_EMIT_TARGET : selected == TIER1_SELECT_OK && error == TIER1_EMIT_OK;
            tier1_masks_test_expect(test, expected, names[c]);
            if (!expected)
            {
                printf("test-tier1-masks: %s: selection %u, emission %u, node %u\n", names[c], (unsigned)selected, (unsigned)error, node);
            }
            test->refused += refused[c] && expected;
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
        uint32_t* offsets = malloc(((uint64_t)code->count + 1) * sizeof(uint32_t));
        uint32_t* ends = malloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted[f].reference_count) * sizeof(uint32_t));
        ok = tier1_masks_test_expect(test, offsets != 0 && ends != 0, "the encoder's buffers");
        ok = ok && tier1_masks_test_expect(test, branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count).error == BRANCH_SIZING_OK &&
                                                     size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK &&
                                                     x86_target_validate(test->v4, code->records, code->count) == code->count,
                                           "a typed program's branches size and its records obey x86-64-v4");
        const int64_t size = ok ? x86_encode(code->records, code->count, machine + starts[f], capacity, 0, ends) : -1;
        ok = ok && tier1_masks_test_expect(test, size > 0, "encode a typed program");
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
// natively. A program that isn't `decodable` has a row the IR's decoders don't take, which the lowering takes as a machine
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
    IrFunction lowered[2];
    IrCheck checks[2];
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        checks[f] = ir_lower_typed_target(&functions[f], results[f], 3, test->v4_features, &test->output, &test->scratch, &lowered[f]);
    }
    if (ok && !decodable)
    {
        *machine = checks[0].error == IR_OK && lowered[0].row_count != 0;
        tier1_masks_test_expect(test, *machine, "a row the IR's decoders don't take is the typed program's machine row,");
    }
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        ok = tier1_masks_test_expect(test, checks[f].error == IR_OK, "the lowering takes the typed program");
        if (!ok)
        {
            printf("test-tier1-masks: %s, function %u: lowering error %u at %u\n", name, f, (unsigned)checks[f].error, checks[f].instruction);
        }
    }
    // The IR's interpreter on each stage: all in memory, promoted, optimized.
    IrFunction promoted[2];
    IrFunction optimized[2];
    const IrFunction* stages[3] = { lowered, promoted, optimized };
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        const IrCheck check = ssa_ir_promote(&lowered[f], &arenas[0], &test->scratch, &promoted[f]);
        ok = tier1_masks_test_expect(test, check.error == IR_OK, "SSA promotion takes the typed program's masks");
        if (!ok)
        {
            printf("test-tier1-masks: %s, function %u, promotion: error %u at %u\n", name, f, (unsigned)check.error, check.instruction);
        }
    }
    for (uint32_t f = 0; ok && f < 2; f += 1)
    {
        IrOptimizeResult result;
        const IrCheck check = ir_optimize(&promoted[f], &arenas[1], &test->scratch, ir_optimize_options(), &result);
        ok = tier1_masks_test_expect(test, check.error == IR_OK, "SCCP, dead code and CFG cleanup take the typed program's masks");
        optimized[f] = result.function;
    }
    for (uint32_t stage = 0; ok && stage < BUSTER_ARRAY_LENGTH(stages); stage += 1)
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
    if (ok)
    {
        const uint64_t mark = arenas[2].length;
        Tier1EmitResult emitted[2];
        for (uint32_t f = 0; ok && f < 2; f += 1)
        {
            IrDominance dominance;
            Tier1SelectPlan plan;
            uint32_t node = BUSTER_IR_NONE;
            ok = tier1_masks_test_dominance(test, &optimized[f], &dominance);
            const Tier1SelectError selected = ok ? tier1_select_plan(&optimized[f], &dominance, &arenas[2], &test->scratch, &plan, &node) : TIER1_SELECT_ERROR_INVALID;
            ok = tier1_masks_test_expect(test, selected == TIER1_SELECT_OK, "Tier 1 selects the typed program's masks");
            const Tier1EmitOptions options = {
                .certify = true,
                .register_mask = tier1_liveness_register_mask(),
                .rematerialize = true,
            };
            memset(&emitted[f], 0, sizeof(emitted[f]));
            const Tier1EmitError error = ok ? tier1_emit_function(&plan, &options, test->v4, &arenas[2], &test->scratch, &emitted[f], &node) : TIER1_EMIT_OK;
            ok = ok && tier1_masks_test_expect(test, error == TIER1_EMIT_OK && emitted[f].general, "Tier 1's CFG consumer emits the typed program");
            for (uint32_t r = 0; ok && r < emitted[f].code.count; r += 1)
            {
                const uint16_t form = emitted[f].code.records[r].form;
                for (uint32_t w = 0; w < 2; w += 1)
                {
                    test->watched_records[w] += test->watched_forms[w] != X86_FORM_NONE && form == test->watched_forms[w];
                }
            }
            if (!ok)
            {
                printf("test-tier1-masks: %s, function %u: selection %u, emission %u, at node %u (op %u)\n", name, f, (unsigned)selected, (unsigned)error, node,
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
            ok = tier1_masks_test_expect(test, length != 0 && tally.failures == 0, "Tier 0 compiles the front end's row");
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
            ok = tier1_masks_test_expect(test, length != 0 && tally.failures == 0, "Tier 0 compiles the front end's comparison");
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

// A byte shuffle by an immediate's kind (BUSTER_IR_SHUFFLE_IMMEDIATE_*) by its builtin's name (#1840): pshufd at its
// three widths, pshuflw, pshufhw, and the byte shifts pslldqi and psrldqi; BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS for any other
// rule. ir_simd_typed_test.c's decoder test reads the same rules.
static uint32_t tier1_masks_test_shuffle_immediate_kind(const CheckX86LowerRule* rule)
{
    static const char* const prefixes[BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS] = {
        "__builtin_ia32_pshufd",
        "__builtin_ia32_pshuflw",
        "__builtin_ia32_pshufhw",
        "__builtin_ia32_pslldqi",
        "__builtin_ia32_psrldqi",
    };
    uint32_t kind = BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS;
    for (uint32_t k = 0; rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE && k < BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS; k += 1)
    {
        kind = strncmp(rule->name, prefixes[k], strlen(prefixes[k])) == 0 ? k : kind;
    }
    return kind;
}

// The immediates the byte shuffles by an immediate are tried at (#1840), by `code` 0 to 7: the shuffles' broadcasts of
// each source, a reversal and two rotations, none the identity; the byte shifts' counts below 16, 16 and past it.
static uint32_t tier1_masks_test_shuffle_immediate_value(uint32_t kind, uint32_t code)
{
    static const uint8_t shuffles[8] = { 0x00, 0x55, 0xaa, 0xff, 0x1b, 0x4e, 0xb1, 0x93 };
    static const uint8_t shifts[8] = { 1, 3, 7, 8, 15, 16, 200, 255 };
    const bool shift = kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES || kind == BUSTER_IR_SHUFFLE_IMMEDIATE_RIGHT_BYTES;
    return shift ? shifts[code & 7] : shuffles[code & 7];
}

// A byte shuffle by an immediate of `bytes` bytes at `in` into `out` (#1840), as Intel's manual defines each in every
// 16-byte lane: vpshufd's doubleword j the source's that the immediate's bits 2j and 2j + 1 name; vpshuflw's low four
// words the same way and its high quadword the source's, vpshufhw's high four and its low quadword; vpslldq's byte i the
// source's byte i - n, zero below n, and vpsrldq's byte i + n, zero from 16 - n, n the immediate, 16 or more every byte
// zero.
static void tier1_masks_test_shuffle_immediate_model(uint32_t kind, uint32_t immediate, uint32_t bytes, const uint8_t* in, uint8_t* out)
{
    for (uint32_t lane = 0; lane < bytes; lane += 16)
    {
        const uint8_t* source = in + lane;
        uint8_t* result = out + lane;
        if (kind == BUSTER_IR_SHUFFLE_IMMEDIATE_DWORDS)
        {
            for (uint32_t j = 0; j < 4; j += 1)
            {
                memcpy(result + 4 * j, source + 4 * ((immediate >> (2 * j)) & 3), 4);
            }
        }
        else if (kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LOW_WORDS || kind == BUSTER_IR_SHUFFLE_IMMEDIATE_HIGH_WORDS)
        {
            const uint32_t moved = kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LOW_WORDS ? 0 : 8;
            memcpy(result + (8 - moved), source + (8 - moved), 8);
            for (uint32_t w = 0; w < 4; w += 1)
            {
                memcpy(result + moved + 2 * w, source + moved + 2 * ((immediate >> (2 * w)) & 3), 2);
            }
        }
        else
        {
            for (uint32_t i = 0; i < 16; i += 1)
            {
                const bool left = kind == BUSTER_IR_SHUFFLE_IMMEDIATE_LEFT_BYTES;
                const bool zero = immediate >= 16 || (left ? i < immediate : i + immediate >= 16);
                result[i] = zero ? 0 : source[left ? i - immediate : i + immediate];
            }
        }
    }
}

// The front end's byte shuffles by an immediate through the IR to Tier 1 (#1840): pshufd, pshuflw, pshufhw, pslldqi and
// psrldqi at each width (tier1_masks_test_shuffle_immediate_kind), at 8 immediates each, as the program's only row in 4
// statements of the input buffer's vector k into the output's vector k. tier1_masks_test_typed_pipeline takes each through
// the IR as its shuffle by a constant, which the interpreter runs at every stage, and Tier 1 natively, against Intel's
// definitions; Tier 1's code by both paths has the 4 records of the immediate form that the constant's indices match
// (ir_simd_shuffle_immediate_match: a byte shift of 16 or more is vpslldq's of 16) and no vpshufb.
static void tier1_masks_test_shuffle_immediates(Tier1MasksTest* test)
{
    static const uint16_t shuffles[3] = { X86_FORM_VPSHUFB_X_X_XM128_EVEX, X86_FORM_VPSHUFB_Y_Y_YM256_EVEX, X86_FORM_VPSHUFB_Z_Z_ZM512 };
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the byte shuffles' memory");
    uint64_t state = test->random ^ UINT64_C(0x1f83d9abfb41bd6b);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const uint32_t kind = tier1_masks_test_shuffle_immediate_kind(rule);
        for (uint32_t code = 0; kind < BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS && code < 8; code += 1)
        {
            const uint64_t output_mark = test->output.length;
            test->shuffle_attempts += 1;
            const uint32_t immediate = tier1_masks_test_shuffle_immediate_value(kind, code);
            const uint32_t bytes = rule->bytes;
            const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
            Tier0Operation row;
            const bool made = check_x86_lower_row_of(&target, rule, immediate, 4, 0, false, &row) == NULL;
            char name[128];
            snprintf(name, sizeof(name), "%s (immediate %u)", rule->name, immediate);
            bool ok = tier1_masks_test_expect(test, made && row.operand_count == 1 && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's byte shuffle's row");
            // The immediate form Tier 1 emits: the first whose indices the shuffle's are.
            uint8_t indices[64];
            ir_simd_shuffle_immediate_indices(kind, immediate, indices);
            for (uint32_t at = 16; at < bytes; at += 16)
            {
                memcpy(indices + at, indices, 16);
            }
            uint32_t matched = BUSTER_IR_SHUFFLE_IMMEDIATE_KINDS;
            uint32_t matched_immediate = 0;
            ok = ok && tier1_masks_test_expect(test, ir_simd_shuffle_immediate_match(indices, bytes, &matched, &matched_immediate), "a byte shuffle's indices match an immediate form");
            program->count = 0;
            program->overflow = false;
            program->rows = true;
            program->objects = false;
            for (uint32_t k = 0; ok && k < 4; k += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
            ok = ok && tier1_masks_test_expect(test, !program->overflow, "the byte shuffle's program fits");
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
                    tier1_masks_test_shuffle_immediate_model(kind, immediate, bytes, (const uint8_t*)in + k * bytes % 256, wanted + k * bytes);
                }
            }
            test->watched_forms[0] = ok ? ir_simd_shuffle_immediate_forms[matched][width] : X86_FORM_NONE;
            test->watched_forms[1] = shuffles[width];
            test->watched_records[0] = 0;
            test->watched_records[1] = 0;
            bool machine = false;
            ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
            ok = ok && tier1_masks_test_expect(test, !machine && test->watched_records[0] == 4 && test->watched_records[1] == 0,
                                               "a byte shuffle by an immediate is the IR's shuffle by a constant, which Tier 1 emits as the immediate form");
            if (!ok)
            {
                printf("test-tier1-masks: %s: kind %u immediate %u matched %u and %u: %u immediate records, %u vpshufb\n", name, kind, immediate, matched, matched_immediate,
                       test->watched_records[0], test->watched_records[1]);
            }
            test->watched_forms[0] = X86_FORM_NONE;
            test->watched_forms[1] = X86_FORM_NONE;
            test->shuffle_rows += ok;
            tier1_masks_test_expect(test, test->scratch.length == 0, "the byte shuffles return the scratch arena");
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

// The reductions through the IR to Tier 1 (#1840): clang's __builtin_reduce_min, _max (both signs), _add, _mul, _and,
// _or and _xor of doublewords and quadwords at 16, 32 and 64 bytes, the producer's rows (check_vector_reduce_shape_rows,
// two for the doublewords of 64 bytes), each the program's only reduction in 4 statements of the input buffer's vector
// k into the output's word k. tier1_masks_test_typed_pipeline takes each through the IR as its reduce and lane_extract,
// which the interpreter runs at every stage, and Tier 1 natively, against C's reduction of the lanes.
static void tier1_masks_test_reductions(Tier1MasksTest* test)
{
    static const CBuiltinId builtins[7] = { CBUILTIN_REDUCE_MIN, CBUILTIN_REDUCE_MAX, CBUILTIN_REDUCE_ADD, CBUILTIN_REDUCE_MUL, CBUILTIN_REDUCE_AND, CBUILTIN_REDUCE_OR, CBUILTIN_REDUCE_XOR };
    const X86Target every = { .features = UINT64_MAX, .extensions = UINT64_MAX };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the reductions' memory");
    uint64_t state = test->random ^ UINT64_C(0x1f83d9abfb41bd6b);
    for (uint32_t form = 0; ready && form < 9 * 2 * 3; form += 1)
    {
        // Minima and maxima of both signs (0 to 3), then the five others.
        const uint32_t operation = form % 9;
        const uint32_t size = 2 + (form / 9) % 2;
        const uint32_t width = form / 18;
        const uint32_t k = operation < 4 ? operation / 2 : operation - 2;
        const uint32_t bytes = 16u << width;
        const uint32_t element = 1u << size;
        const CheckVectorShape shape = { .lanes = bytes / element, .bytes = bytes, .element = (uint8_t)element, .integer = true, .is_signed = operation < 4 && (operation & 1) != 0 };
        const uint64_t output_mark = test->output.length;
        test->reduce_attempts += 1;
        Tier0Operation rows[BUSTER_CHECK_VECTOR_REDUCE_ROWS];
        uint32_t count = 0;
        bool ok = tier1_masks_test_expect(test, check_vector_reduce_shape_rows(&every, builtins[k], shape, rows, &count) == NULL, "the front end's reduction's rows");
        char name[96];
        snprintf(name, sizeof(name), "reduction %u of %u-byte lanes at %u bytes", operation, element, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        const uint8_t result_class = rows[count - 1].result_class;
        for (uint32_t st = 0; ok && st < 4; st += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, st);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, st * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            for (uint32_t r = 0; r < count; r += 1)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, rows[r].result_class, r);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, result_class, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the reduction's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = rows;
        functions[0].operation_count = count;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t st = 0; st < 4; st += 1)
            {
                const uint8_t* vector = (const uint8_t*)in + st * bytes % 256;
                uint64_t total = 0;
                for (uint32_t lane = 0; lane < bytes / element; lane += 1)
                {
                    uint64_t x = 0;
                    memcpy(&x, vector + lane * element, element);
                    const int64_t sx = size == 2 ? (int64_t)(int32_t)(uint32_t)x : (int64_t)x;
                    const int64_t st_total = size == 2 ? (int64_t)(int32_t)(uint32_t)total : (int64_t)total;
                    const uint64_t combined[9] = { x < total ? x : total, sx < st_total ? x : total, x > total ? x : total, sx > st_total ? x : total, total + x, total * x, total & x, total | x, total ^ x };
                    total = lane == 0 ? x : combined[operation];
                    total = size == 2 ? total & UINT32_MAX : total;
                }
                memcpy(wanted + st * 8, &total, element);
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a reduction is the IR's, not a machine row");
        test->reduce_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the reductions return the scratch arena");
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

// A lane's unary operation's row (#1840), `kind` 0 to 3 popcount of bytes to quadwords (clang's
// __builtin_elementwise_popcount: check_vector_elementwise_forms' form), 4 and 5 the leading zero count of doublewords
// and quadwords of one operand, 6 and 7 of two (__builtin_elementwise_clzg: the count, vptestnm into k1 and vpblendm
// under it, as the front end writes them), 8 and 9 the conflicts (_mm*_conflict_epi32 and _epi64), at `width` (0 to 2);
// its flags into `flags`. ir_simd_typed_test.c's decoder test reads the same rows.
static Tier0Operation tier1_masks_test_lane_unary_row(uint32_t kind, uint32_t width, uint32_t* flags)
{
    static const uint16_t tests[2][3] = {
        { X86_FORM_VPTESTNMD_K_X_XM128, X86_FORM_VPTESTNMD_K_Y_YM256, X86_FORM_VPTESTNMD_K_Z_ZM512 },
        { X86_FORM_VPTESTNMQ_K_X_XM128, X86_FORM_VPTESTNMQ_K_Y_YM256, X86_FORM_VPTESTNMQ_K_Z_ZM512 },
    };
    static const uint16_t blends[2][3] = {
        { X86_FORM_VPBLENDMD_X_X_XM128, X86_FORM_VPBLENDMD_Y_Y_YM256, X86_FORM_VPBLENDMD_Z_Z_ZM512 },
        { X86_FORM_VPBLENDMQ_X_X_XM128, X86_FORM_VPBLENDMQ_Y_Y_YM256, X86_FORM_VPBLENDMQ_Z_Z_ZM512 },
    };
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint32_t bytes = 16u << width;
    const uint32_t size = kind < 4 ? kind : kind < 10 ? 2 + kind % 2 : kind - 10;
    const uint32_t operation = kind < 4 ? 0 : kind < 8 ? 1 : kind < 10 ? 2 : 3;
    const bool fallback = kind == 6 || kind == 7;
    const CheckVectorShape shape = { .lanes = bytes >> size, .bytes = bytes, .element = (uint8_t)(1u << size), .integer = true, .is_signed = operation == 3 };
    uint64_t needed = 0;
    static const CBuiltinId builtins[4] = { CBUILTIN_ELEMENTWISE_POPCOUNT, CBUILTIN_ELEMENTWISE_CLZG, CBUILTIN_ELEMENTWISE_POPCOUNT, CBUILTIN_ELEMENTWISE_ABS };
    const uint16_t form = operation == 2 ? ir_simd_lane_unary_forms[2][size][width] : (uint16_t)check_vector_elementwise_forms(builtins[operation], shape, &needed).forms[width];
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    if (fallback)
    {
        row.records[0] = check_vector_lower_record(form, BUSTER_TIER0_ROLE_VECTOR(2), 0, v0);
        row.records[1] = check_vector_lower_record(tests[size - 2][width], 1, v0, v0);
        row.records[2] = check_vector_lower_record(blends[size - 2][width], v0, BUSTER_TIER0_ROLE_VECTOR(2), BUSTER_TIER0_ROLE_VECTOR(1));
        row.records[2].decorations = 1;
        row.record_count = 3;
    }
    else
    {
        row.records[0] = check_vector_lower_record(form, v0, 0, v0);
        row.record_count = 1;
    }
    row.operand_count = fallback ? 2 : 1;
    for (uint32_t j = 0; j < row.operand_count; j += 1)
    {
        row.operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[j] = (uint8_t)bytes;
    }
    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
    row.result_bytes = (uint8_t)bytes;
    *flags = size | operation << 2;
    return row;
}

// The lanes' unary operations through the IR to Tier 1 (#1840): each row of tier1_masks_test_lane_unary_row at each
// width as the program's only row in 4 statements of the input buffer's vector k (and k + 1 for the two-operand leading
// zero count) into the output's vector k, inputs whose lanes repeat and are often zero or narrow.
// tier1_masks_test_typed_pipeline takes each through the IR as its lane_unary, which the interpreter runs at every
// stage, and Tier 1 natively, against C's counts and conflicts. The population counts are BITALG's and VPOPCNTDQ's,
// past x86-64-v4: their output target has them too, and a host that runs Tier 1's code natively must have them, or
// they are left to test-ir-simd's decoding.
static void tier1_masks_test_lane_unaries(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    // x86-64-v4's target and forms, and with the population counts' features.
    const uint64_t v4 = test->v4_features;
    uint8_t* const v4_forms = test->v4;
    const uint64_t counts = BUSTER_X86_FEATURE(AVX512BITALG) | BUSTER_X86_FEATURE(AVX512VPOPCNTDQ);
    uint8_t* counted_forms = malloc(X86_FORM_COUNT);
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0 && counted_forms != 0,
                                         "the lane unary operations' memory");
    if (ready)
    {
        const X86Target counted = { .features = v4 | counts };
        x86_target_forms(&counted, counted_forms);
    }
    uint64_t state = test->random ^ UINT64_C(0x5be0cd19137e2179);
    const bool counted = !test->native || (x86_target_native().features & counts) == counts;
    for (uint32_t form = counted ? 0 : 4 * 3; ready && form < 14 * 3; form += 1)
    {
        const uint32_t kind = form / 3;
        test->v4_features = kind < 4 ? v4 | counts : v4;
        test->v4 = kind < 4 ? counted_forms : v4_forms;
        const uint32_t width = form % 3;
        const uint32_t bytes = 16u << width;
        uint32_t flags = 0;
        Tier0Operation row = tier1_masks_test_lane_unary_row(kind, width, &flags);
        const uint32_t size = flags & 3;
        const uint32_t operation = flags >> 2;
        const uint32_t element = 1u << size;
        const uint32_t lanes = bytes / element;
        const bool fallback = row.operand_count == 2;
        const uint64_t output_mark = test->output.length;
        test->unary_attempts += 1;
        char name[96];
        snprintf(name, sizeof(name), "lane_unary 0x%x at %u bytes%s", flags, bytes, fallback ? " with a fallback" : "");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t st = 0; st < 4; st += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, st * bytes / 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, st * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            if (fallback)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (st + 1) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the lane unary operation's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // Repeated, zero and narrow lanes, the same through the input buffer at this lane size.
            uint8_t* raw = (uint8_t*)in;
            for (uint32_t lane = 1; lane < BUSTER_TIER1_MASKS_TEST_IN / element; lane += 1)
            {
                const uint64_t random = tier0_test_mask_random(&state);
                if ((random & 3) == 0)
                {
                    memcpy(raw + lane * element, raw + (lane - 1 - (random >> 8) % (lane < 4 ? lane : 4)) * element, element);
                }
                else if ((random & 7) == 1)
                {
                    memset(raw + lane * element, 0, element);
                    raw[lane * element] = (uint8_t)(random >> 8 & (random >> 16));
                }
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t st = 0; st < 4; st += 1)
            {
                const uint8_t* vector = raw + st * bytes % 256;
                const uint8_t* other = raw + (st + 1) * bytes % 256;
                for (uint32_t lane = 0; lane < lanes; lane += 1)
                {
                    uint64_t x = 0;
                    memcpy(&x, vector + lane * element, element);
                    uint64_t value = 0;
                    for (uint32_t bit = 0; operation == 0 && bit < element * 8; bit += 1)
                    {
                        value += (x >> bit) & 1;
                    }
                    for (uint32_t bit = element * 8; operation == 1 && bit > 0 && ((x >> (bit - 1)) & 1) == 0; bit -= 1)
                    {
                        value += 1;
                    }
                    for (uint32_t below = 0; operation == 2 && below < lane; below += 1)
                    {
                        uint64_t y = 0;
                        memcpy(&y, vector + below * element, element);
                        value |= (uint64_t)(x == y) << below;
                    }
                    if (operation == 3)
                    {
                        // The absolute value of the lane's two's complement, the most negative lane itself (#2780).
                        const bool negative = ((x >> (element * 8 - 1)) & 1) != 0;
                        value = negative ? 0 - x : x;
                    }
                    if (fallback && x == 0)
                    {
                        memcpy(&value, other + lane * element, element);
                    }
                    memcpy(wanted + st * bytes + lane * element, &value, element);
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a lane's unary operation is the IR's, not a machine row");
        test->unary_rows += ok;
        test->v4_features = v4;
        test->v4 = v4_forms;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the lane unary operations return the scratch arena");
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
    free(counted_forms);
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

// The gathers through the IR to Tier 1 (#1840): vpgatherdd, vpgatherdq, vpgatherqd and vpgatherqq at each width whose
// index has an element per lane, each at a scale of its own, as the program's only row in 4 statements: from the input
// buffer's first 128 bytes around a base at byte 64, by positive and negative indices from its last 128 bytes, merged under a mask from
// the input into a passthrough of the input's, stored into the output's vector s. The third statement's mask is a constant
// of every lane, its gather's alone; the fourth's index is a second vector whose lanes the mask leaves clear point far past
// every buffer, so that a masked-off lane is never read (the interpreter checks each access against its binding).
// tier1_masks_test_typed_pipeline takes each through the IR as its gather, which the interpreter runs at every stage, and
// Tier 1 natively, against C's lanes.
static void tier1_masks_test_gathers(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the gathers' memory");
    uint64_t state = test->random ^ UINT64_C(0x6a09e667f3bcc908);
    for (uint32_t kind = 0; ready && kind < 2 * 2 * 3; kind += 1)
    {
        const uint32_t lane = kind / 6;
        const uint32_t index = (kind / 3) % 2;
        const uint32_t width = kind % 3;
        const uint16_t form = ir_simd_gather_forms[0][lane][index][width];
        const uint32_t bytes = 16u << width;
        const uint32_t element = 4u << lane;
        const uint32_t count = bytes / element;
        const uint32_t index_element = 4u << index;
        const uint32_t index_bytes = count * index_element;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const uint32_t scale = (lane * 2 + index + width) & 3;
        if (form == X86_FORM_NONE)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->gather_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = (X86Instruction){ .form = form, .reg = BUSTER_TIER0_ROLE_VECTOR(0), .rm = BUSTER_TIER0_ROLE_INTEGER(1), .index = BUSTER_TIER0_ROLE_VECTOR(2),
                                           .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale), .decorations = BUSTER_TIER0_DECORATION_MASK(3) };
        row.record_count = 1;
        row.operand_count = 4;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_U64;
        row.operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[3] = BUSTER_TYPED_CLASS_MASK;
        row.operand_bytes[0] = (uint8_t)bytes;
        row.operand_bytes[2] = (uint8_t)index_bytes;
        row.operand_bytes[3] = (uint8_t)mask_bytes;
        row.result_class = BUSTER_TYPED_CLASS_VECTOR;
        row.result_bytes = (uint8_t)bytes;
        char name[96];
        snprintf(name, sizeof(name), "gather of %u-byte lanes by %u-byte indices at %u bytes", element, index_element, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, s * bytes / 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (s * 2) % 8);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s < 3 ? 16 : 24);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, index_bytes);
            if (s == 2)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the gather's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* bytes_in = (uint8_t*)in;
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // A centered base exercises negative dword and qword indices at every scale. Lanes 0 and 1 select the
            // first and last accessible elements, including in the third statement's all-ones mask. The fourth
            // statement's masked-off lanes still point far past every buffer.
            for (uint32_t i = 0; i < count; i += 1)
            {
                for (uint32_t v = 0; v < 2; v += 1)
                {
                    const bool off = v == 1 && ((in[3] >> i) & 1) == 0;
                    const uint32_t places = ((128u - element) >> scale) + 1u;
                    const uint32_t place = i == 0 ? 0 : i == 1 ? places - 1 : (uint32_t)(tier0_test_mask_random(&state) % places);
                    const int64_t chosen = off ? INT64_C(0x10000000) << (8 * index) : (int64_t)place - (int64_t)(64u >> scale);
                    uint8_t* at = bytes_in + 128 + 64 * v + i * index_element;
                    if (index == 0)
                    {
                        const int32_t narrow = (int32_t)chosen;
                        memcpy(at, &narrow, 4);
                    }
                    else
                    {
                        memcpy(at, &chosen, 8);
                    }
                }
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = s == 2 ? UINT64_MAX : in[s];
                uint8_t* out = wanted + s * bytes;
                memcpy(out, bytes_in + (s * 2) % 8 * 8, bytes);
                for (uint32_t i = 0; i < count; i += 1)
                {
                    int64_t offset = 0;
                    const uint8_t* at = bytes_in + (s < 3 ? 128 : 192) + i * index_element;
                    if (index == 0)
                    {
                        int32_t narrow = 0;
                        memcpy(&narrow, at, 4);
                        offset = narrow;
                    }
                    else
                    {
                        memcpy(&offset, at, 8);
                    }
                    if ((mask >> i & 1) != 0)
                    {
                        memcpy(out + i * element, bytes_in + 64 + offset * (int64_t)(1u << scale), element);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a gather is the IR's, not a machine row");
        test->gather_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the gathers return the scratch arena");
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

// IR_FMA's a * b + c of a type's bits (#1840): the host's vfmadd213sd or ss where it runs Tier 1's code (an x86-64-v4
// host, which has FMA), so that the interpreter's ir_fma_bits and Tier 1's FMA3 forms are both checked against the
// hardware; ir_fma_bits where it doesn't (the interpreter's comparison is then its own).
static uint64_t tier1_masks_test_fma(const Tier1MasksTest* test, bool d, uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t result = ir_fma_bits(d ? IR_F64 : IR_F32, a, b, c);
    if (test->native && d)
    {
        double x = 0;
        double y = 0;
        double z = 0;
        memcpy(&x, &a, 8);
        memcpy(&y, &b, 8);
        memcpy(&z, &c, 8);
        __asm__("vfmadd213sd %2, %1, %0" : "+x"(y) : "x"(x), "x"(z));
        memcpy(&result, &y, 8);
    }
    else if (test->native)
    {
        const uint32_t bits[3] = { (uint32_t)a, (uint32_t)b, (uint32_t)c };
        float x = 0;
        float y = 0;
        float z = 0;
        memcpy(&x, &bits[0], 4);
        memcpy(&y, &bits[1], 4);
        memcpy(&z, &bits[2], 4);
        __asm__("vfmadd213ss %2, %1, %0" : "+x"(y) : "x"(x), "x"(z));
        uint32_t word = 0;
        memcpy(&word, &y, 4);
        result = word;
    }
    return result;
}

// The scalar fused multiply-adds through the IR to Tier 1 (#1840): the front end's rows of __builtin_elementwise_fma and
// of a contracted a * b + c and c + a * b (check_vector_scalar_row, check_vector_contract_row) of floats and doubles at
// x86-64-v4, and a row of their records whose roles the front end doesn't place so (vfmadd213 of F1 into F1 by F2 and F0:
// IR_FMA of F2, F1 and F0, whose a the allocation gives the result's register, Tier 1's 132 form), each the program's
// only row in 4 statements of the input's first three values x, y and z: the row of x, y
// and z; of x, x and y (a == b); of y, z and z (b == c); and of the first statement's result, itself and z. The inputs
// are NaNs of both signs and payloads, a signaling one, infinities, zeros, subnormals and products that round, set so
// that every statement meets them. tier1_masks_test_typed_pipeline takes each through the IR as its IR_FMA, which the
// interpreter runs at every stage (ir_fma_bits), and Tier 1 natively (its FMA3 forms), against the host's vfmadd213.
static void tier1_masks_test_fmas(Tier1MasksTest* test)
{
    static const uint64_t doubles[12] = { 0x7ff8000000000001, 0xfff8000000000002, 0x7ff0000000000003, 0x7ff0000000000000, 0xfff0000000000000, 0x8000000000000000,
                                          0x3ff0000000000001, 0x3fefffffffffffff, 0xbff0000000000000, 0x0000000000000001, 0x7fefffffffffffff, 0 };
    static const uint32_t floats[12] = { 0x7fc00001, 0xffc00002, 0x7f800003, 0x7f800000, 0xff800000, 0x80000000, 0x3f800001, 0x3f7fffff, 0xbf800000, 1,
                                         0x7f7fffff, 0 };
    // Each input's x, y and z by their indices in the tables above: two NaNs first, an infinity times zero plus a NaN,
    // a signaling NaN, and a product that rounds.
    static const uint8_t triples[BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS][3] = { { 1, 0, 6 }, { 3, 11, 2 }, { 7, 6, 8 }, { 2, 9, 10 } };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the fused multiply-adds' memory");
    const X86Target v4 = { .features = test->v4_features };
    for (uint32_t kind = 0; ready && kind < 2 * 4; kind += 1)
    {
        const bool d = kind / 4 == 1;
        const uint32_t shape = kind % 4;
        const uint8_t class = d ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
        Tier0Operation row;
        const char* reason = shape == 0 ? check_vector_scalar_row(&v4, CBUILTIN_ELEMENTWISE_FMA, class, 3, &row) : check_vector_contract_row(&v4, class, shape == 2, &row);
        if (reason == NULL && shape == 3)
        {
            row.records[0].reg = BUSTER_TIER0_ROLE_FLOATING(1);
            row.records[0].vvvv = BUSTER_TIER0_ROLE_FLOATING(2);
            row.records[0].rm = BUSTER_TIER0_ROLE_FLOATING(0);
            row.records[1].rm = BUSTER_TIER0_ROLE_FLOATING(1);
        }
        if (!tier1_masks_test_expect(test, reason == NULL, "a fused multiply-add's row at x86-64-v4"))
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->fma_attempts += 1;
        char name[96];
        snprintf(name, sizeof(name), "%s fused multiply-add's %s row", d ? "a double's" : "a float's",
                 shape == 0 ? "builtin" : shape == 1 ? "contraction" : shape == 2 ? "right-hand contraction" : "permuted");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        // The statements' operands by input slot, BUSTER_IR_NONE where the statement's first result is (the fourth's).
        static const uint32_t statements[4][3] = { { 0, 1, 2 }, { 0, 0, 1 }, { 1, 2, 2 }, { BUSTER_IR_NONE, BUSTER_IR_NONE, 2 } };
        for (uint32_t st = 0; st < 4; st += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, st);
            if (st == 3)
            {
                // The first statement's row again, then its result twice and z.
                for (uint32_t j = 0; j < 3; j += 1)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, j);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, class, 0);
                }
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, class, 0);
                tier0_test_mask_emit(program, BUSTER_TYPED_DUP, class, 0);
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 2);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, class, 0);
            }
            else
            {
                for (uint32_t j = 0; j < 3; j += 1)
                {
                    if (j > 0 && statements[st][j] == statements[st][j - 1])
                    {
                        tier0_test_mask_emit(program, BUSTER_TYPED_DUP, class, 0);
                    }
                    else
                    {
                        tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, statements[st][j]);
                        tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, class, 0);
                    }
                }
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, class, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the fused multiply-add's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        // The row's operands 0, 1 and 2 compute IR_FMA of 0, 1 and 2; of 1, 2 and 0 for c + a * b's; of 2, 1 and 0 for the
        // permuted row's.
        static const uint8_t orders[4][3] = { { 0, 1, 2 }, { 0, 1, 2 }, { 1, 2, 0 }, { 2, 1, 0 } };
        const uint8_t* order = orders[shape];
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            memset(in, 0, BUSTER_TIER1_MASKS_TEST_IN);
            for (uint32_t j = 0; j < 3; j += 1)
            {
                const uint32_t pick = (triples[input][j] + kind) % 12;
                in[j] = d ? doubles[pick] : floats[pick];
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            uint64_t first = 0;
            for (uint32_t st = 0; st < 4; st += 1)
            {
                uint64_t operands[3] = { 0 };
                for (uint32_t j = 0; j < 3; j += 1)
                {
                    operands[j] = statements[st][j] == BUSTER_IR_NONE ? first : in[statements[st][j]];
                }
                const uint64_t value = tier1_masks_test_fma(test, d, operands[order[0]], operands[order[1]], operands[order[2]]);
                first = st == 0 ? value : first;
                memcpy(wanted + st * 8, &value, d ? 8 : 4);
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a fused multiply-add is the IR's, not a machine row");
        test->fma_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the fused multiply-adds return the scratch arena");
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

// The scatters through the IR to Tier 1 (#1840): vpscatterdd to vpscatterqq of each shape whose index has an element per
// lane (ir_simd_scatter_forms), each the program's only row in 4 statements: the input's vector at its byte 128 scattered
// around the output's midpoint by positive and negative indices at the input's byte 64 under a mask from the input (the third
// statement's a constant of every lane), each shape at its own scale. The indices repeat, so that of two lanes at one place the higher
// one's bytes must stay; and in the fourth statement, by the index vector at the input's byte 192, every masked-off
// lane's index points far past every buffer, so that the interpreter's binding checks prove such a lane is never
// written. tier1_masks_test_typed_pipeline takes each through the IR as its scatter, which the interpreter runs at every
// stage, and Tier 1 natively, against C's lanes; the output's bytes no lane writes stay the harness's 0xa5.
static void tier1_masks_test_scatters(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the scatters' memory");
    uint64_t state = test->random ^ UINT64_C(0xbb67ae8584caa73b);
    for (uint32_t kind = 0; ready && kind < 2 * 2 * 3; kind += 1)
    {
        const uint32_t lane = kind / 6;
        const uint32_t index = (kind / 3) % 2;
        const uint32_t width = kind % 3;
        const uint16_t form = ir_simd_scatter_forms[0][lane][index][width];
        const uint32_t bytes = 16u << width;
        const uint32_t element = 4u << lane;
        const uint32_t count = bytes / element;
        const uint32_t index_element = 4u << index;
        const uint32_t index_bytes = count * index_element;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const uint32_t scale = (lane * 2 + index + width) & 3;
        if (form == X86_FORM_NONE)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->scatter_attempts += 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = (X86Instruction){ .form = form, .reg = BUSTER_TIER0_ROLE_VECTOR(3), .rm = BUSTER_TIER0_ROLE_INTEGER(0), .index = BUSTER_TIER0_ROLE_VECTOR(2),
                                           .mode = (uint8_t)BUSTER_X86_MEMORY_BASE_INDEX(scale), .decorations = BUSTER_TIER0_DECORATION_MASK(1) };
        row.record_count = 1;
        row.operand_count = 4;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_MASK;
        row.operand_classes[2] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[3] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[1] = (uint8_t)mask_bytes;
        row.operand_bytes[2] = (uint8_t)index_bytes;
        row.operand_bytes[3] = (uint8_t)bytes;
        row.result_class = BUSTER_TYPED_CLASS_VOID;
        char name[96];
        snprintf(name, sizeof(name), "scatter of %u-byte lanes by %u-byte indices at %u bytes", element, index_element, bytes);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t s = 0; s < 4; s += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, BUSTER_TIER1_MASKS_TEST_OUT / 16);
            if (s == 2)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s < 3 ? 8 : 24);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, index_bytes);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 16);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VOID, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the scatter's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* bytes_in = (uint8_t*)in;
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // The stored vectors' words past the generated slots, so that two lanes at one place differ.
            for (uint32_t k = 16; k < 24; k += 1)
            {
                in[k] = tier0_test_mask_random(&state);
            }
            // Six places around the base, separated by at least an element, so duplicate indices overlap exactly.
            // Lanes 0 and 1 force both signs on even inputs and a repeated negative index on odd inputs, even for the
            // two-lane shapes. The fourth statement's masked-off lanes still point far past every buffer.
            for (uint32_t i = 0; i < count; i += 1)
            {
                for (uint32_t v = 0; v < 2; v += 1)
                {
                    const bool off = v == 1 && ((in[3] >> i) & 1) == 0;
                    const int64_t place = i == 0 || (i == 1 && (input & 1) != 0) ? -3 : i == 1 ? 2 : (int64_t)(tier0_test_mask_random(&state) % 6) - 3;
                    const int64_t chosen = off ? INT64_C(0x10000000) << (8 * index) : place * (int64_t)(element >> scale > 0 ? element >> scale : 1);
                    uint8_t* at = bytes_in + (v == 0 ? 64 : 192) + i * index_element;
                    if (index == 0)
                    {
                        const int32_t narrow = (int32_t)chosen;
                        memcpy(at, &narrow, 4);
                    }
                    else
                    {
                        memcpy(at, &chosen, 8);
                    }
                }
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = s == 2 ? UINT64_MAX : in[s];
                for (uint32_t i = 0; i < count; i += 1)
                {
                    int64_t offset = 0;
                    const uint8_t* at = bytes_in + (s < 3 ? 64 : 192) + i * index_element;
                    if (index == 0)
                    {
                        int32_t narrow = 0;
                        memcpy(&narrow, at, 4);
                        offset = narrow;
                    }
                    else
                    {
                        memcpy(&offset, at, 8);
                    }
                    if ((mask >> i & 1) != 0)
                    {
                        memcpy(wanted + BUSTER_TIER1_MASKS_TEST_OUT / 2 + offset * (int64_t)(1u << scale), bytes_in + 128 + i * element, element);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a scatter is the IR's, not a machine row");
        test->scatter_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the scatters return the scratch arena");
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

// A value's truncation to `to`'s bytes' log2 from `from`'s (#1840, a truncating store's lane): wrapping (mode 0), signed
// saturating (1) or unsigned saturating (2), as vpmov, vpmovs and vpmovus truncate.
static uint64_t tier1_masks_test_truncate(uint64_t value, uint32_t from, uint32_t to, uint32_t mode)
{
    const uint32_t from_bits = 8u << from;
    const uint32_t to_bits = 8u << to;
    const uint64_t to_mask = (UINT64_C(1) << to_bits) - 1;
    const uint64_t raw = from_bits == 64 ? value : value & ((UINT64_C(1) << from_bits) - 1);
    uint64_t result = raw & to_mask;
    if (mode == 1)
    {
        const int64_t wide = from_bits == 64 ? (int64_t)raw : (int64_t)(raw << (64 - from_bits)) >> (64 - from_bits);
        const int64_t high = (INT64_C(1) << (to_bits - 1)) - 1;
        const int64_t low = -high - 1;
        result = (uint64_t)(wide > high ? high : wide < low ? low : wide) & to_mask;
    }
    else if (mode == 2)
    {
        result = raw > to_mask ? to_mask : raw;
    }
    return result;
}

// The compressing stores and the truncating stores under a mask through the IR to Tier 1 (#1840): vpcompressd and
// vpcompressq at each width (bytes and words are VBMI2's, which the x86-64-v4 target hasn't), and vpmovwb to vpmovusqd of the
// sources whose truncated lanes fill 16 bytes or more, or whose 8 lanes fill 8 (#2780, whose masked statements Tier 1
// folds into the truncation's one memory form under the 8 lanes' mask), each as the program's only row in 4 statements: the input's vector
// s under a mask from the input stored into the output at s, the third statement's mask a constant of every lane (its
// plain store's), the fourth's ending at the output's end, its mask's upper half cleared so that no lane it leaves out is
// written past it. tier1_masks_test_typed_pipeline takes each through the IR as its compress_store or its truncation's
// masked_store, which the interpreter runs at every stage, and Tier 1 natively, against C's lanes; the output's bytes no
// store writes stay the harness's 0xa5.
static void tier1_masks_test_scattered_stores(Tier1MasksTest* test)
{
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the compressing and truncating stores' memory");
    uint64_t state = test->random ^ UINT64_C(0xbb67ae8584caa73b);
    // The compressing stores' 6 forms (doublewords and quadwords at 3 widths), then the truncations' 54 (6 pairs, 3 modes,
    // 3 widths), of which those that fill 16 bytes are stores of the IR's.
    for (uint32_t kind = 0; ready && kind < 6 + 54; kind += 1)
    {
        const bool compress = kind < 6;
        const uint32_t pair = compress ? 0 : (kind - 6) / 9;
        const uint32_t mode = compress ? 0 : ((kind - 6) / 3) % 3;
        const uint32_t width = kind % 3;
        const uint32_t bytes = 16u << width;
        const uint32_t from = compress ? 2 + kind / 3 : ir_simd_typed_truncation_pairs[pair][0];
        const uint32_t to = compress ? from : ir_simd_typed_truncation_pairs[pair][1];
        const uint32_t count = bytes >> from;
        const uint32_t stored = count << to;
        const uint32_t mask_bytes = count <= 8 ? 1 : count / 8;
        const uint64_t half = (UINT64_C(1) << (count / 2)) - 1;
        const bool partial = !compress && count == 8 && to == 0;
        if (stored < 16 && !partial)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->compress_store_attempts += compress ? 1 : 0;
        test->truncating_store_attempts += compress ? 0 : 1;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        row.records[0] = (X86Instruction){ .form = compress ? ir_simd_compress_forms[from][width] : ir_simd_typed_truncations[mode][pair][width], .reg = BUSTER_TIER0_ROLE_VECTOR(1),
                                           .rm = BUSTER_TIER0_ROLE_INTEGER(0), .mode = BUSTER_X86_MEMORY_BASE, .decorations = BUSTER_TIER0_DECORATION_MASK(2) };
        row.record_count = 1;
        row.operand_count = 3;
        row.operand_classes[0] = BUSTER_TYPED_CLASS_U64;
        row.operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_classes[2] = BUSTER_TYPED_CLASS_MASK;
        row.operand_bytes[1] = (uint8_t)bytes;
        row.operand_bytes[2] = (uint8_t)mask_bytes;
        row.result_class = BUSTER_TYPED_CLASS_VOID;
        char name[96];
        snprintf(name, sizeof(name), compress ? "compressing store of %u-byte lanes at %u bytes, mode %u" : "truncating store of %u-byte lanes at %u bytes, mode %u", 1u << from, bytes,
                 mode);
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        // Each statement's store, in words: the fourth's ending at the output's end, half its bytes past its address.
        uint32_t at[4];
        for (uint32_t s = 0; s < 4; s += 1)
        {
            at[s] = s < 3 ? s * stored / 8 : partial ? 31 : 32 - stored / 16;
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, at[s]);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            if (s == 2)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_MAX);
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, s * 7 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_MASK, mask_bytes);
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VOID, 0);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        bool ok = tier1_masks_test_expect(test, !program->overflow, "the store's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            in[3 * 7 % 32] &= half;
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t s = 0; s < 4; s += 1)
            {
                const uint64_t mask = s == 2 ? UINT64_MAX : in[s * 7 % 32];
                const uint8_t* source = (const uint8_t*)in + s * bytes % 256;
                uint8_t* out = wanted + at[s] * 8;
                uint32_t written = 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    if ((mask >> i & 1) != 0)
                    {
                        uint64_t value = 0;
                        memcpy(&value, source + (i << from), 1u << from);
                        const uint64_t lane = compress ? value : tier1_masks_test_truncate(value, from, to, mode);
                        memcpy(out + ((compress ? written : i) << to), &lane, 1u << to);
                        written += 1;
                    }
                }
            }
        }
        // VPMOVQB_XM64_Z and the others name the register form too, which the unmasked statement's truncation is: the
        // fold shows as no masked store of the bytes (vmovdqu8).
        test->watched_forms[0] = partial ? row.records[0].form : X86_FORM_NONE;
        test->watched_forms[1] = partial ? ir_simd_masked_moves[0][1][0] : X86_FORM_NONE;
        test->watched_records[0] = 0;
        test->watched_records[1] = 0;
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a compressing or truncating store is the IR's, not a machine row");
        // 8 lanes into 8 bytes (#2780): its three masked statements one vpmov* [p]{k} each, by both paths, and the unmasked
        // one's truncation into a register; no masked store of bytes.
        ok = ok && tier1_masks_test_expect(test, !partial || (test->watched_records[0] == 4 && test->watched_records[1] == 0),
                                           "a truncating store of 8 bytes folds into its one memory form under the narrow mask");
        if (!ok && partial)
        {
            printf("test-tier1-masks: %s: %u truncations, %u masked byte stores\n", name, test->watched_records[0], test->watched_records[1]);
        }
        test->watched_forms[0] = X86_FORM_NONE;
        test->watched_forms[1] = X86_FORM_NONE;
        test->compress_store_rows += ok && compress ? 1 : 0;
        test->truncating_store_rows += ok && !compress ? 1 : 0;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the stores return the scratch arena");
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
// bytes where those are fewer), and every TRUNCATE rule's row (those whose lanes leave their result's upper bytes zero
// too, #1840's partial merges), merged under an input word in the odd statements and unmasked in the even ones, each as the program's only row in 4 statements of the input buffer's
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

// A funnel shift's row (#1840), `index` 0 to 17: clang's __builtin_elementwise_fshl (0 to 8) and fshr (9 to 17) of
// words, doublewords and quadwords at 16, 32 and 64 bytes, as c_check_program_vector_builtins.c writes them (its forms
// check_vector_elementwise_forms'): fshl's vpshldv into VECTOR(0), a, from VECTOR(1), b, by VECTOR(2), and fshr's
// vpshrdv into VECTOR(1) from VECTOR(0) by VECTOR(2), then VECTOR(1) moved into VECTOR(0); its flags into `flags`.
// ir_simd_typed_test.c's decoder test reads the same rows.
static Tier0Operation tier1_masks_test_funnel_row(uint32_t index, uint32_t* flags)
{
    const uint8_t v0 = BUSTER_TIER0_ROLE_VECTOR(0);
    const uint8_t v1 = BUSTER_TIER0_ROLE_VECTOR(1);
    const uint8_t v2 = BUSTER_TIER0_ROLE_VECTOR(2);
    const uint32_t right = index / 9;
    const uint32_t size = 1 + index / 3 % 3;
    const uint32_t width = index % 3;
    const uint32_t bytes = 16u << width;
    const CheckVectorShape shape = { .lanes = bytes >> size, .bytes = bytes, .element = (uint8_t)(1u << size), .integer = true };
    uint64_t needed = 0;
    const uint16_t form = (uint16_t)check_vector_elementwise_forms(right != 0 ? CBUILTIN_ELEMENTWISE_FSHR : CBUILTIN_ELEMENTWISE_FSHL, shape, &needed).forms[width];
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    if (right != 0)
    {
        row.records[0] = check_vector_lower_record(form, v1, v0, v2);
        row.records[1] = check_x86_lower_move_result(bytes, v1);
        row.record_count = 2;
    }
    else
    {
        row.records[0] = check_vector_lower_record(form, v0, v1, v2);
        row.record_count = 1;
    }
    row.operand_count = 3;
    for (uint32_t j = 0; j < 3; j += 1)
    {
        row.operand_classes[j] = BUSTER_TYPED_CLASS_VECTOR;
        row.operand_bytes[j] = (uint8_t)bytes;
    }
    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
    row.result_bytes = (uint8_t)bytes;
    *flags = size | right << 2;
    return row;
}

// A rotate rule's direction and lanes (#1840): __builtin_ia32_prold, prolq, prord and prorq at each width; 0 for any
// other rule, else 1 + right * 2 + (quadwords).
static uint32_t tier1_masks_test_rotate_kind(const CheckX86LowerRule* rule)
{
    static const char* const names[4] = { "__builtin_ia32_prold", "__builtin_ia32_prolq", "__builtin_ia32_prord", "__builtin_ia32_prorq" };
    uint32_t kind = 0;
    for (uint32_t k = 0; rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE && k < 4; k += 1)
    {
        kind = strncmp(rule->name, names[k], strlen(names[k])) == 0 ? k + 1 : kind;
    }
    return kind;
}

// C's funnel shift of one lane of `bits` bits: high:low's high half shifted left, or its low half shifted right, by
// `count` modulo the bits, a bit at a time.
static uint64_t tier1_masks_test_funnel_lane(uint64_t high, uint64_t low, uint64_t count, uint32_t bits, bool right)
{
    const uint32_t shift = (uint32_t)(count % bits);
    uint64_t value = 0;
    for (uint32_t bit = 0; bit < bits; bit += 1)
    {
        const uint32_t at = (right ? bit + shift : bit + bits - shift) % (2 * bits);
        const uint64_t source = at >= bits ? high >> (at - bits) : low >> at;
        value |= (source & 1) << bit;
    }
    return value;
}

// The funnel shifts through the IR to Tier 1 (#1840): each of tier1_masks_test_funnel_row's 18 rows, in 4 statements of
// the input buffer's vectors k, k + 1 and k + 2 (the counts, often past the lanes' bits or zero) into the output's vector
// k; and each rotate rule's row at 6 immediates (0, 1, the lanes' bits less 1, their bits, their bits plus 3 and 255),
// in 4 statements of vector k. tier1_masks_test_typed_pipeline takes each through the IR as its funnel, which the
// interpreter runs at every stage, and Tier 1 natively, against tier1_masks_test_funnel_lane; a rotate's code by both
// paths has its vprold to vprorq form's 4 records, its constant counts folded into the immediate. The funnels are
// VBMI2's, past x86-64-v4: their output target has it too, and a host that runs Tier 1's code natively must have it,
// or they are left to test-ir-simd's decoding.
static void tier1_masks_test_funnels(Tier1MasksTest* test)
{
    const uint64_t v4 = test->v4_features;
    uint8_t* const v4_forms = test->v4;
    const uint64_t vbmi2 = BUSTER_X86_FEATURE(AVX512VBMI2);
    const X86Target target = { .features = v4 };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* funnel_forms = malloc(X86_FORM_COUNT);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && funnel_forms != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the funnel shifts' memory");
    if (ready)
    {
        const X86Target shifting = { .features = v4 | vbmi2 };
        x86_target_forms(&shifting, funnel_forms);
    }
    const bool funnels = !test->native || (x86_target_native().features & vbmi2) != 0;
    uint64_t state = test->random ^ UINT64_C(0x5be0cd191f83d9ab);
    // The cases: the 18 funnels' rows, then each rotate rule at 6 immediates.
    const uint32_t rules = BUSTER_ARRAY_LENGTH(check_x86_lower_rules);
    for (uint32_t c = funnels ? 0 : 18; ready && c < 18 + 6 * rules; c += 1)
    {
        const bool rotate = c >= 18;
        const CheckX86LowerRule* rule = rotate ? check_x86_lower_rules + (c - 18) / 6 : 0;
        const uint32_t kind = rotate ? tier1_masks_test_rotate_kind(rule) : 0;
        if (rotate && kind == 0)
        {
            continue;
        }
        uint32_t flags = 0;
        Tier0Operation row;
        bool made = true;
        uint32_t immediate = 0;
        char name[128];
        if (rotate)
        {
            const uint32_t bits = kind == 2 || kind == 4 ? 64 : 32;
            const uint32_t immediates[6] = { 0, 1, bits - 1, bits, bits + 3, 255 };
            immediate = immediates[(c - 18) % 6];
            made = check_x86_lower_row_of(&target, rule, immediate, 4, 0, false, &row) == NULL;
            flags = (bits == 64 ? 3u : 2u) | (kind >= 3 ? 4u : 0u);
            snprintf(name, sizeof(name), "%s (immediate %u)", rule->name, immediate);
        }
        else
        {
            row = tier1_masks_test_funnel_row(c, &flags);
            snprintf(name, sizeof(name), "%s of %u-byte lanes at %u bytes", (flags & 4) != 0 ? "fshr" : "fshl", 1u << (flags & 3), (uint32_t)row.result_bytes);
        }
        const uint64_t output_mark = test->output.length;
        test->funnel_attempts += 1;
        test->v4_features = v4 | vbmi2;
        test->v4 = funnel_forms;
        const uint32_t bytes = row.result_bytes;
        const uint32_t element = 1u << (flags & 3);
        const uint32_t bits = 8 * element;
        const bool right = (flags & 4) != 0;
        bool ok = tier1_masks_test_expect(test, made && row.result_class == BUSTER_TYPED_CLASS_VECTOR && row.operand_count == (rotate ? 1u : 3u), "the front end's funnel shift's row");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
            for (uint32_t j = 0; j < row.operand_count; j += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + j) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the funnel shift's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // Counts of 0, of the lanes' bits and past them, and small ones, in every lane of the input.
            uint8_t* raw = (uint8_t*)in;
            for (uint32_t lane = 0; !rotate && lane < BUSTER_TIER1_MASKS_TEST_IN / element; lane += 1)
            {
                const uint64_t random = tier0_test_mask_random(&state);
                if ((random & 3) != 3)
                {
                    const uint64_t count = (random & 3) == 0 ? 0 : (random & 3) == 1 ? bits : (random >> 8) % (2 * bits);
                    memcpy(raw + lane * element, &count, element);
                }
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const uint8_t* a = raw + k * bytes % 256;
                const uint8_t* b = rotate ? a : raw + (k + 1) * bytes % 256;
                const uint8_t* counts = raw + (k + 2) * bytes % 256;
                for (uint32_t lane = 0; lane < bytes / element; lane += 1)
                {
                    uint64_t high = 0;
                    uint64_t low = 0;
                    uint64_t count = immediate;
                    memcpy(&high, a + lane * element, element);
                    memcpy(&low, b + lane * element, element);
                    if (!rotate)
                    {
                        memcpy(&count, counts + lane * element, element);
                    }
                    const uint64_t value = tier1_masks_test_funnel_lane(high, low, count, bits, right);
                    memcpy(wanted + k * bytes + lane * element, &value, element);
                }
            }
        }
        // A rotate's immediate form: its 4 records by both paths.
        test->watched_forms[0] = rotate && ok ? row.records[0].form : X86_FORM_NONE;
        test->watched_records[0] = 0;
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine && (!rotate || test->watched_records[0] == 4),
                                           "a funnel shift is the IR's funnel, a rotate its immediate form");
        if (!ok)
        {
            printf("test-tier1-masks: %s: %u immediate records\n", name, test->watched_records[0]);
        }
        test->watched_forms[0] = X86_FORM_NONE;
        test->funnel_rows += ok;
        test->v4_features = v4;
        test->v4 = v4_forms;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the funnel shifts return the scratch arena");
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
    free(funnel_forms);
    free(expected);
    free(inputs);
    free(program);
}

// The quadword operations through the IR to Tier 1 (#1840): each vpmultishiftqb, psadbw, pmuludq and pmuldq (#2780)
// builtin's row at each width as the program's only row in 4 statements of the input buffer's vectors k and k + 1 into
// the output's vector k.
// tier1_masks_test_typed_pipeline takes each through the IR as its quadword_op, which the interpreter runs at every stage,
// and Tier 1 natively, against the instructions' definitions written out here. vpmultishiftqb is VBMI's, past
// x86-64-v4: its output target has it too, and a host that runs Tier 1's code natively must have it, or it is left to
// test-ir-simd's decoding.
static void tier1_masks_test_quadword_ops(Tier1MasksTest* test)
{
    const uint64_t v4 = test->v4_features;
    uint8_t* const v4_forms = test->v4;
    const uint64_t vbmi = BUSTER_X86_FEATURE(AVX512VBMI);
    const X86Target target = { .features = v4 | vbmi };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* vbmi_forms = malloc(X86_FORM_COUNT);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && vbmi_forms != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the quadword operations' memory");
    if (ready)
    {
        x86_target_forms(&target, vbmi_forms);
    }
    const bool multishifts = !test->native || (x86_target_native().features & vbmi) != 0;
    uint64_t state = test->random ^ UINT64_C(0x1f83d9ab5be0cd19);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        const bool multishift = strncmp(rule->name, "__builtin_ia32_vpmultishiftqb", sizeof("__builtin_ia32_vpmultishiftqb") - 1) == 0;
        const bool sad = strncmp(rule->name, "__builtin_ia32_psadbw", sizeof("__builtin_ia32_psadbw") - 1) == 0;
        const bool unsigned_product = strncmp(rule->name, "__builtin_ia32_pmuludq", sizeof("__builtin_ia32_pmuludq") - 1) == 0;
        const bool signed_product = strncmp(rule->name, "__builtin_ia32_pmuldq", sizeof("__builtin_ia32_pmuldq") - 1) == 0;
        if ((!multishift && !sad && !unsigned_product && !signed_product) || (multishift && !multishifts))
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->quadword_attempts += 1;
        test->v4_features = v4 | vbmi;
        test->v4 = vbmi_forms;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL;
        const uint32_t bytes = rule->bytes;
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == 2 && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's quadword operation's row");
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
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the quadword operation's program fits");
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
                const uint8_t* a = (const uint8_t*)in + k * bytes % 256;
                const uint8_t* b = (const uint8_t*)in + (k + 1) * bytes % 256;
                uint8_t* out = wanted + k * bytes;
                for (uint32_t q = 0; q < bytes / 8; q += 1)
                {
                    uint64_t data = 0;
                    memcpy(&data, b + 8 * q, 8);
                    uint64_t sum = 0;
                    for (uint32_t j = 0; j < 8; j += 1)
                    {
                        // vpmultishiftqb: the 8 bits of b's quadword from bit a's byte's low 6 bits, modulo 64; vpsadbw: the
                        // sum of |a's byte - b's byte| over the quadword.
                        uint32_t byte = 0;
                        for (uint32_t bit = 0; bit < 8; bit += 1)
                        {
                            byte |= (uint32_t)((data >> ((a[8 * q + j] + bit) % 64)) & 1) << bit;
                        }
                        out[8 * q + j] = (uint8_t)byte;
                        sum += a[8 * q + j] > b[8 * q + j] ? (uint64_t)(a[8 * q + j] - b[8 * q + j]) : (uint64_t)(b[8 * q + j] - a[8 * q + j]);
                    }
                    if (sad)
                    {
                        memcpy(out + 8 * q, &sum, 8);
                    }
                    // vpmuludq and vpmuldq: the whole product of the quadword's low doublewords, unsigned and signed.
                    uint32_t low_a = 0;
                    uint32_t low_b = 0;
                    memcpy(&low_a, a + 8 * q, 4);
                    memcpy(&low_b, b + 8 * q, 4);
                    const uint64_t product = unsigned_product ? (uint64_t)low_a * low_b : (uint64_t)((int64_t)(int32_t)low_a * (int64_t)(int32_t)low_b);
                    if (unsigned_product || signed_product)
                    {
                        memcpy(out + 8 * q, &product, 8);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a quadword operation is the IR's, not a machine row");
        test->quadword_rows += ok;
        test->v4_features = v4;
        test->v4 = v4_forms;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the quadword operations return the scratch arena");
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
    free(vbmi_forms);
    free(expected);
    free(inputs);
    free(program);
}

// A pairwise operation's or a dot product's x86 builtin (#2780): whether `name` is __builtin_ia32_pmaddwd, pmaddubsw, one
// of the packs, vpdpbusd, vpdpbusds, vpdpwssd or vpdpwssds at 128, 256 or 512 bits, and its IR kind (`dot`) and flags.
static bool tier1_masks_test_pairwise_rule(const char* name, bool* dot, uint32_t* flags)
{
    static const struct
    {
        const char* prefix;
        bool dot;
        uint8_t flags;
    } names[] = {
        { "__builtin_ia32_pmaddwd", false, BUSTER_IR_PAIRWISE_MADD_WORDS },
        { "__builtin_ia32_pmaddubsw", false, BUSTER_IR_PAIRWISE_MADD_BYTES },
        { "__builtin_ia32_packsswb", false, BUSTER_IR_PAIRWISE_PACK_FIRST },
        { "__builtin_ia32_packuswb", false, BUSTER_IR_PAIRWISE_PACK_FIRST + 1 },
        { "__builtin_ia32_packssdw", false, BUSTER_IR_PAIRWISE_PACK_FIRST + 2 },
        { "__builtin_ia32_packusdw", false, BUSTER_IR_PAIRWISE_PACK_FIRST + 3 },
        { "__builtin_ia32_vpdpbusd", true, BUSTER_IR_DOT_BYTES },
        { "__builtin_ia32_vpdpbusds", true, BUSTER_IR_DOT_BYTES_SATURATED },
        { "__builtin_ia32_vpdpwssd", true, BUSTER_IR_DOT_WORDS },
        { "__builtin_ia32_vpdpwssds", true, BUSTER_IR_DOT_WORDS_SATURATED },
    };
    bool found = false;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        const size_t length = strlen(names[i].prefix);
        const char* bits = name + length;
        if (strncmp(name, names[i].prefix, length) == 0 && (strcmp(bits, "128") == 0 || strcmp(bits, "256") == 0 || strcmp(bits, "512") == 0))
        {
            found = true;
            *dot = names[i].dot;
            *flags = names[i].flags;
        }
    }
    return found;
}

// The pairwise operations and the dot products through the IR to Tier 1 (#2780): each pmaddwd, pmaddubsw, pack, vpdpbusd,
// vpdpbusds, vpdpwssd and vpdpwssds builtin's row at each width, by the real producer, as the program's only row in 4
// statements of the input buffer's vectors k, k + 1 (and k + 2 for a dot product) into the output's vector k, inputs whose
// extreme bytes saturate. tier1_masks_test_typed_pipeline takes each through the IR as its pairwise or dot, which the
// interpreter runs at every stage, and Tier 1 natively, against the instructions' definitions written out
// (ir_test_pairwise_oracle). The dot products are AVX512-VNNI's, past x86-64-v4: their output target has it too, and a
// host that runs Tier 1's code natively must have it, or they are left to test-ir-simd's decoding.
static void tier1_masks_test_pairwise_dots(Tier1MasksTest* test)
{
    const uint64_t v4 = test->v4_features;
    uint8_t* const v4_forms = test->v4;
    const uint64_t vnni = BUSTER_X86_FEATURE(AVX512VNNI);
    const X86Target target = { .features = v4 | vnni };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* vnni_forms = malloc(X86_FORM_COUNT);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && vnni_forms != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the pairwise operations' memory");
    if (ready)
    {
        x86_target_forms(&target, vnni_forms);
    }
    const bool dots = !test->native || (x86_target_native().features & vnni) != 0;
    uint64_t state = test->random ^ UINT64_C(0x3c6ef372fe94f82b);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        bool dot = false;
        uint32_t flags = 0;
        if (!tier1_masks_test_pairwise_rule(rule->name, &dot, &flags) || (dot && !dots))
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->pairwise_attempts += 1;
        test->v4_features = v4 | vnni;
        test->v4 = vnni_forms;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL;
        const uint32_t bytes = rule->bytes;
        const uint32_t operands = dot ? 3 : 2;
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == operands && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's pairwise operation's row");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
            for (uint32_t j = 0; j < operands; j += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + j) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the pairwise operation's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            // Extreme bytes and words, which the products and sums saturate at.
            uint8_t* raw = (uint8_t*)in;
            for (uint32_t k = 0; k < BUSTER_TIER1_MASKS_TEST_IN; k += 1)
            {
                const uint64_t random = tier0_test_mask_random(&state);
                raw[k] = (random & 7) == 0 ? 0x80 : (random & 7) == 1 ? 0x7f : (random & 7) == 2 ? 0xff : raw[k];
            }
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const uint8_t* a = raw + k * bytes % 256;
                const uint8_t* b = raw + (k + 1) * bytes % 256;
                const uint8_t* c = raw + (k + 2) * bytes % 256;
                ir_test_pairwise_oracle(dot, flags, a, b, c, bytes, wanted + k * bytes);
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a pairwise operation or a dot product is the IR's, not a machine row");
        test->pairwise_rows += ok;
        test->v4_features = v4;
        test->v4 = v4_forms;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the pairwise operations return the scratch arena");
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
    free(vnni_forms);
    free(expected);
    free(inputs);
    free(program);
}

// A blend by a vector's signs' x86 builtin (#2780): whether `name` is __builtin_ia32_pblendvb128 or 256, blendvps or
// blendvpd at 16 or 32 bytes, and its lanes' size index (0 bytes, 1 doublewords, 2 quadwords).
static bool tier1_masks_test_blendv_rule(const char* name, uint32_t* size)
{
    static const char* const names[6] = { "__builtin_ia32_pblendvb128", "__builtin_ia32_pblendvb256", "__builtin_ia32_blendvps", "__builtin_ia32_blendvps256",
                                           "__builtin_ia32_blendvpd", "__builtin_ia32_blendvpd256" };
    bool found = false;
    for (uint32_t i = 0; i < 6; i += 1)
    {
        if (strcmp(name, names[i]) == 0)
        {
            found = true;
            *size = i / 2;
        }
    }
    return found;
}

// The blends by a vector's signs through the IR to Tier 1 (#2780): each pblendvb, blendvps and blendvpd builtin's row at
// 16 and 32 bytes, by the real producer, as the program's only row in 4 statements of the input buffer's vectors k,
// k + 1 and k + 2 (a, b and the signs) into the output's vector k. tier1_masks_test_typed_pipeline takes each through
// the IR as its vselect under a comparison less than zero, which the interpreter runs at every stage, and Tier 1
// natively, against each lane written out: b's where the sign vector's lane is negative, a's elsewhere. Tier 1's code
// by both paths is the blend's one record per statement, the comparison folded: no vpmovb2m to vpmovq2m.
static void tier1_masks_test_blendvs(Tier1MasksTest* test)
{
    static const uint16_t signs[3][2] = {
        { X86_FORM_VPMOVB2M_K_X, X86_FORM_VPMOVB2M_K_Y },
        { X86_FORM_VPMOVD2M_K_X, X86_FORM_VPMOVD2M_K_Y },
        { X86_FORM_VPMOVQ2M_K_X, X86_FORM_VPMOVQ2M_K_Y },
    };
    X86Target target = { 0 };
    x86_target_parse("x86-64-v4", 9, &target);
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the blends' memory");
    uint64_t state = test->random ^ UINT64_C(0xa4093822299f31d0);
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        uint32_t size = 0;
        if (!tier1_masks_test_blendv_rule(rule->name, &size))
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->blendv_attempts += 1;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL;
        const uint32_t bytes = rule->bytes;
        const uint32_t element = size == 0 ? 1 : size == 1 ? 4 : 8;
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == 3 && row.record_count == 1 && row.result_class == BUSTER_TYPED_CLASS_VECTOR, "the front end's blend's row");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * bytes / 8);
            for (uint32_t j = 0; j < 3; j += 1)
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k + j) * bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the blend's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            const uint8_t* raw = (const uint8_t*)in;
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const uint8_t* a = raw + k * bytes % 256;
                const uint8_t* b = raw + (k + 1) * bytes % 256;
                const uint8_t* s = raw + (k + 2) * bytes % 256;
                for (uint32_t lane = 0; lane < bytes / element; lane += 1)
                {
                    const bool negative = (s[lane * element + element - 1] & 0x80) != 0;
                    memcpy(wanted + k * bytes + lane * element, (negative ? b : a) + lane * element, element);
                }
            }
        }
        test->watched_forms[0] = ok ? row.records[0].form : X86_FORM_NONE;
        test->watched_forms[1] = signs[size][bytes == 16 ? 0 : 1];
        test->watched_records[0] = 0;
        test->watched_records[1] = 0;
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine && test->watched_records[0] == 4 && test->watched_records[1] == 0,
                                           "a blend is the IR's select under a comparison, and Tier 1's one blend");
        test->watched_forms[0] = X86_FORM_NONE;
        test->watched_forms[1] = X86_FORM_NONE;
        test->blendv_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the blends return the scratch arena");
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

// The bit shuffles into a mask through the IR to Tier 1 (#1840): each vpshufbitqmb builtin's row (16, 32 and 64 bytes, its
// masks in the mask class by check_x86_lower_masks), as the program's only row in 8 statements: the input's vectors at
// 16k and 16k + 64 under the input word k in the odd statements and every lane in the even ones, its mask stored into
// the output's quadword k. tier1_masks_test_typed_pipeline takes it through the interpreter at every stage and Tier 1
// natively, against vpshufbitqmb's definition written out here: bit i the bit of the first vector's quadword i / 8 that
// the second's byte i's low 6 bits name, where the writemask has bit i. Tier 1's code by both paths has the form's 8
// records, the writemask's AND folded into the odd ones: no kand.
static void tier1_masks_test_bitshuffles(Tier1MasksTest* test)
{
    static const uint16_t forms[3] = { X86_FORM_VPSHUFBITQMB_K_X_XM128, X86_FORM_VPSHUFBITQMB_K_Y_YM256, X86_FORM_VPSHUFBITQMB_K_Z_ZM512 };
    static const uint16_t ands[3] = { X86_FORM_KANDW_K_K_K, X86_FORM_KANDD_K_K_K, X86_FORM_KANDQ_K_K_K };
    // The bit shuffles are BITALG's, past x86-64-v4: their output target has it too, and a host that runs Tier 1's code
    // natively must have it, or they are left to test-ir-simd's decoding.
    const uint64_t v4 = test->v4_features;
    uint8_t* const v4_forms = test->v4;
    const uint64_t bitalg = BUSTER_X86_FEATURE(AVX512BITALG);
    const X86Target target = { .features = v4 | bitalg };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    uint8_t* shuffled_forms = malloc(X86_FORM_COUNT);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && shuffled_forms != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0,
                                         "the bit shuffles' memory");
    if (ready)
    {
        x86_target_forms(&target, shuffled_forms);
    }
    const bool runs = !test->native || (x86_target_native().features & bitalg) != 0;
    uint64_t state = test->random ^ UINT64_C(0xbb67ae8584caa73b);
    for (uint32_t i = 0; ready && runs && i < BUSTER_ARRAY_LENGTH(check_x86_lower_rules); i += 1)
    {
        const CheckX86LowerRule* rule = check_x86_lower_rules + i;
        if (strncmp(rule->name, "__builtin_ia32_vpshufbitqmb", sizeof("__builtin_ia32_vpshufbitqmb") - 1) != 0)
        {
            continue;
        }
        const uint64_t output_mark = test->output.length;
        test->bitshuffle_attempts += 1;
        test->v4_features = v4 | bitalg;
        test->v4 = shuffled_forms;
        Tier0Operation row;
        const bool made = check_x86_lower_row_of(&target, rule, 0, 4, 0, false, &row) == NULL && check_x86_lower_masks(&row, &target);
        const uint32_t bytes = rule->bytes;
        const uint32_t width = bytes == 16 ? 0 : bytes == 32 ? 1 : 2;
        bool ok = tier1_masks_test_expect(test, made && row.operand_count == 3 && row.result_class == BUSTER_TYPED_CLASS_MASK, "the front end's bit shuffle row, in the mask class");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        for (uint32_t k = 0; ok && k < 8; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, 2 * k);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k == 7 ? 2 * k : 2 * k + 8);
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
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, row.result_class, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, row.result_class, row.result_bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the bit shuffle's program fits");
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
                const uint64_t writemask = (k & 1) != 0 ? in[k] : UINT64_MAX;
                uint64_t value = 0;
                for (uint32_t bit = 0; bit < bytes; bit += 1)
                {
                    uint64_t quadword = 0;
                    memcpy(&quadword, a + 8 * (bit / 8), 8);
                    value |= ((quadword >> (b[bit] & 63)) & 1 & (writemask >> bit)) << bit;
                }
                memcpy(wanted + 8 * k, &value, row.result_bytes);
            }
        }
        test->watched_forms[0] = forms[width];
        test->watched_forms[1] = ands[width];
        test->watched_records[0] = 0;
        test->watched_records[1] = 0;
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, rule->name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine && test->watched_records[0] == 8 && test->watched_records[1] == 0,
                                           "a bit shuffle is the IR's bitshuffle, one vpshufbitqmb by both paths, its writemask folded");
        if (!ok)
        {
            printf("test-tier1-masks: %s: %u vpshufbitqmb, %u kand\n", rule->name, test->watched_records[0], test->watched_records[1]);
        }
        test->watched_forms[0] = X86_FORM_NONE;
        test->watched_forms[1] = X86_FORM_NONE;
        test->bitshuffle_rows += ok;
        test->v4_features = v4;
        test->v4 = v4_forms;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the bit shuffles return the scratch arena");
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
    free(shuffled_forms);
    free(expected);
    free(inputs);
    free(program);
}

// The SSE carriers through the IR to Tier 1 (#1840, #1280: vectors of 4 or 8 bytes, an F32 or F64 in the IR), each in 4
// statements of the input buffer's vectors stored into the output's:
// - every whole-lane extension of a carrier (__builtin_convertvector's row from check_vector_convert_row of a vector of 4
//   or 8 bytes, 9 pairs and widths, zero and signed) of a cast of a vector of 16, 32 or 64 bytes into the carrier (the
//   front end's shuffle of its first elements, a row of no record), as clang's headers' _mm256_cvtepu8_epi32 and the like
//   are: the IR's lane_extract of lane 0 into the carrier and its extend, the extension's form one record by both paths;
// - each extension of a carrier loaded from the input buffer;
// - each cast, of 16, 32 and 64 bytes into a carrier of 4 or 8, stored whole;
// - each cast whose vector stays live past it, into a machine row (vpaddd or vpaddq of the vector and the carrier, which
//   the row reads zero-extended), so that the carrier can't take the vector's register: Tier 1 moves it into its own.
// tier1_masks_test_typed_pipeline takes each through Tier 1 natively, and the first three through the interpreter at every
// stage, against tier1_masks_test_convert_lane and the carrier's bytes.
static void tier1_masks_test_carriers(Tier1MasksTest* test)
{
    static const TypeId elements[2][4] = {
        { BUSTER_TYPE_UCHAR, BUSTER_TYPE_USHORT, BUSTER_TYPE_UINT, BUSTER_TYPE_ULLONG },
        { BUSTER_TYPE_SCHAR, BUSTER_TYPE_SHORT, BUSTER_TYPE_INT, BUSTER_TYPE_LLONG },
    };
    static const uint16_t adds[2][3] = {
        { X86_FORM_VPADDD_X_X_XM128_EVEX, X86_FORM_VPADDD_Y_Y_YM256_EVEX, X86_FORM_VPADDD_Z_Z_ZM512 },
        { X86_FORM_VPADDQ_X_X_XM128_EVEX, X86_FORM_VPADDQ_Y_Y_YM256_EVEX, X86_FORM_VPADDQ_Z_Z_ZM512 },
    };
    const X86Target target = { .features = test->v4_features };
    Tier0TestMaskProgram* program = malloc(sizeof(*program));
    uint64_t* inputs = calloc(BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS, BUSTER_TIER1_MASKS_TEST_IN);
    uint8_t* expected = malloc(BUSTER_TIER1_MASKS_TEST_OUT * BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS);
    Arena arenas[3] = { arena_reserve(64u << 20), arena_reserve(64u << 20), arena_reserve(64u << 20) };
    TypeTestTable table = {0};
    bool ready = tier1_masks_test_expect(test, program != 0 && inputs != 0 && expected != 0 && arenas[0].base != 0 && arenas[1].base != 0 && arenas[2].base != 0 &&
                                                   type_test_table_open(&table, 512),
                                         "the carriers' memory and type table");
    uint64_t state = test->random ^ UINT64_C(0x9b05688c2b3e6c1f);
    // The cases: (variant, sign, from, to, width) for the extensions, of a cast (variant 0) or a load (1); then the casts
    // stored (variant 2) and the casts of a live vector (3), by (carrier, width).
    const uint32_t extensions = 2 * 2 * 4 * 4 * 3;
    for (uint32_t c = 0; ready && c < extensions + 2 * 2 * 3; c += 1)
    {
        const uint32_t variant = c < extensions ? c / (extensions / 2) : 2 + (c - extensions) / 6;
        const uint32_t e = c % (extensions / 2);
        const uint32_t sign = e / 48;
        const uint32_t from = 1u << (e / 12 % 4);
        const uint32_t to = 1u << (e / 3 % 4);
        const uint32_t bytes = variant >= 2 ? 4u << ((c - extensions) % 6 / 3) : 16u << (e % 3);
        const uint32_t lanes = bytes / to;
        const uint32_t carrier = variant >= 2 ? bytes : lanes * from;
        // The cast's vector: 16, 32 or 64 bytes, by the case.
        const uint32_t wide = 16u << (c % 3);
        if (variant < 2 && (to <= from || (carrier != 4 && carrier != 8)))
        {
            continue;
        }
        Tier0Operation rows[2];
        memset(rows, 0, sizeof(rows));
        // The cast, check_vector_shuffle_row's of a vector's first elements (in the checker's unit: written out here).
        Tier0Operation* cast = &rows[0];
        cast->result_class = BUSTER_TYPED_CLASS_VECTOR;
        cast->result_bytes = (uint8_t)carrier;
        cast->operand_count = 1;
        cast->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
        cast->operand_bytes[0] = (uint8_t)wide;
        bool made = true;
        char name[128];
        if (variant < 2)
        {
            const TypeId source = type_vector(&table.table, elements[sign][__builtin_ctz(from)], lanes, false);
            const TypeId result = type_vector(&table.table, elements[sign][__builtin_ctz(to)], lanes, false);
            made = check_vector_convert_row(&table.table, &target, source, carrier, result, &rows[variant == 0 ? 1 : 0]) == NULL;
            snprintf(name, sizeof(name), "convertvector of a carrier of %u lanes of %u bytes (signed %u) to %u, %s", lanes, from, sign, to,
                     variant == 0 ? "of a cast" : "loaded");
        }
        else if (variant == 3)
        {
            // The vector and its carrier, added: the carrier's lanes past its own zero.
            Tier0Operation* add = &rows[1];
            add->result_class = BUSTER_TYPED_CLASS_VECTOR;
            add->result_bytes = (uint8_t)wide;
            add->operand_count = 2;
            add->operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
            add->operand_bytes[0] = (uint8_t)wide;
            add->operand_classes[1] = BUSTER_TYPED_CLASS_VECTOR;
            add->operand_bytes[1] = (uint8_t)carrier;
            add->records[0] = check_vector_lower_record(adds[carrier == 8][wide == 16 ? 0 : wide == 32 ? 1 : 2], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(0),
                                                        BUSTER_TIER0_ROLE_VECTOR(1));
            add->record_count = 1;
            snprintf(name, sizeof(name), "a cast of a live vector of %u bytes into a carrier of %u", wide, carrier);
        }
        else
        {
            snprintf(name, sizeof(name), "a cast of %u bytes into a carrier of %u", wide, carrier);
        }
        const Tier0Operation* extension = variant == 0 ? &rows[1] : &rows[0];
        const uint64_t output_mark = test->output.length;
        test->carrier_attempts += 1;
        bool ok = tier1_masks_test_expect(test, made && (variant >= 2 || (extension->record_count == 1 && extension->operand_bytes[0] == carrier)), "the front end's carrier rows");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        // The output's quadword k holds a stored cast's carrier, its vector k any other's result.
        const uint32_t out_bytes = variant == 2 ? 8 : variant == 3 ? wide : bytes;
        const uint32_t in_bytes = variant == 1 ? 8 : wide;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * out_bytes / 8);
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * in_bytes / 8 % 32);
            tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, variant == 1 ? carrier : wide);
            if (variant == 3)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_VECTOR, 0);
            }
            if (variant != 1)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
            }
            if (variant != 2)
            {
                tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, variant == 1 ? 0 : 1);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, variant == 2 ? carrier : out_bytes);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the carrier's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = rows;
        functions[0].operation_count = variant == 0 || variant == 3 ? 2 : 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            for (uint32_t k = 0; k < 4; k += 1)
            {
                // The vector's (or the loaded carrier's) low bytes are the carrier's.
                const uint8_t* a = (const uint8_t*)in + k * in_bytes % 256;
                uint8_t* out = wanted + k * out_bytes;
                if (variant == 2)
                {
                    memcpy(out, a, carrier);
                }
                else if (variant == 3)
                {
                    memcpy(out, a, wide);
                    uint64_t low = 0;
                    memcpy(&low, a, carrier);
                    const uint64_t sum = carrier == 8 ? low + low : (low + low) & UINT32_MAX;
                    memcpy(out, &sum, carrier);
                }
                for (uint32_t lane = 0; variant < 2 && lane < lanes; lane += 1)
                {
                    uint64_t raw = 0;
                    memcpy(&raw, a + lane * from, from);
                    const uint64_t value = tier1_masks_test_convert_lane(raw, from, to, sign != 0, 0);
                    memcpy(out + lane * to, &value, to);
                }
            }
        }
        // An extension of a cast is its form's one record by both paths, in each statement.
        test->watched_forms[0] = variant == 0 && ok ? extension->records[0].form : X86_FORM_NONE;
        test->watched_records[0] = 0;
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, variant != 3, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, machine == (variant == 3), "a carrier's cast and extension are the IR's, not machine rows");
        ok = ok && tier1_masks_test_expect(test, variant != 0 || test->watched_records[0] == 4, "an extension of a cast into a carrier is one record");
        if (!ok)
        {
            printf("test-tier1-masks: %s: %u extension records\n", name, test->watched_records[0]);
        }
        test->watched_forms[0] = X86_FORM_NONE;
        test->carrier_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the carriers return the scratch arena");
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

// The truncations into an SSE carrier and the permutes of two carriers through the IR to Tier 1 (#2780), each as the
// program's only row in 4 statements:
// - convertvector of a whole vector of 16, 32 or 64 bytes whose truncated lanes fill 4 or 8 bytes (the producer's
//   check_vector_convert_row, 18 shapes), its carrier stored whole: the IR's truncate and lane 0 of it;
// - vpermt2d and vpermt2q of two carriers of 4 or 8 bytes loaded from the input by 16-byte indices from it, the result
//   stored: the IR's permute2 of the carriers widened, each table's bytes past its carrier zero (as a row reads one).
// tier1_masks_test_typed_pipeline takes each through the interpreter at every stage and Tier 1 natively, against C's
// lanes.
static void tier1_masks_test_carrier_results(Tier1MasksTest* test)
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
                                         "the carrier results' memory and type table");
    uint64_t state = test->random ^ UINT64_C(0x1f83d9abfb41bd6b);
    // The truncations: (sign, from, to, result bytes); then the permutes: (lanes, carrier bytes).
    for (uint32_t c = 0; ready && c < 2 * 4 * 4 * 2 + 2 * 2; c += 1)
    {
        const bool permute = c >= 2 * 4 * 4 * 2;
        const uint32_t sign = c / 32;
        const uint32_t from = c / 8 % 4;
        const uint32_t to = c / 2 % 4;
        const uint32_t carrier = permute ? 4u << (c & 1) : 4u << (c & 1);
        const uint32_t lanes = carrier >> to;
        const uint32_t source_bytes = lanes << from;
        if (!permute && (to >= from || (source_bytes != 16 && source_bytes != 32 && source_bytes != 64)))
        {
            continue;
        }
        // A permute's lanes: doublewords or quadwords.
        const uint32_t size = permute ? 2 + (c - 2 * 4 * 4 * 2) / 2 : 0;
        Tier0Operation row;
        memset(&row, 0, sizeof(row));
        bool made = true;
        char name[128];
        if (permute)
        {
            row.records[0] = check_vector_lower_record(ir_simd_permute2_forms[size][0], BUSTER_TIER0_ROLE_VECTOR(0), BUSTER_TIER0_ROLE_VECTOR(2), BUSTER_TIER0_ROLE_VECTOR(1));
            row.record_count = 1;
            row.operand_count = 3;
            for (uint32_t o = 0; o < 3; o += 1)
            {
                row.operand_classes[o] = BUSTER_TYPED_CLASS_VECTOR;
                row.operand_bytes[o] = (uint8_t)(o == 2 ? 16 : carrier);
            }
            row.result_class = BUSTER_TYPED_CLASS_VECTOR;
            row.result_bytes = 16;
            snprintf(name, sizeof(name), "a permute of %u-byte lanes of two %u-byte carriers", 1u << size, carrier);
        }
        else
        {
            const TypeId source = type_vector(&table.table, elements[sign][from], lanes, false);
            const TypeId result = type_vector(&table.table, elements[sign][to], lanes, false);
            made = check_vector_convert_row(&table.table, &target, source, source_bytes, result, &row) == NULL;
            snprintf(name, sizeof(name), "convertvector of %u lanes of %u bytes (signed %u) into a %u-byte carrier", lanes, 1u << from, sign, carrier);
        }
        const uint64_t output_mark = test->output.length;
        test->carrier_result_attempts += 1;
        bool ok = tier1_masks_test_expect(test, made && row.result_bytes == (permute ? 16 : carrier), "the front end's carrier result rows");
        program->count = 0;
        program->overflow = false;
        program->rows = true;
        program->objects = false;
        const uint32_t out_bytes = permute ? 16 : 8;
        for (uint32_t k = 0; ok && k < 4; k += 1)
        {
            tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_OUT, k * out_bytes / 8);
            if (permute)
            {
                for (uint32_t j = 0; j < 3; j += 1)
                {
                    tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, (k * 6 + j * 2) % 32);
                    tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, j == 2 ? 16 : carrier);
                }
            }
            else
            {
                tier0_test_mask_address(program, BUSTER_TIER0_TEST_MASK_IN, k * source_bytes / 8 % 32);
                tier0_test_mask_emit(program, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, source_bytes);
            }
            tier0_test_mask_emit(program, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, 0);
            tier0_test_mask_emit(program, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, permute ? 16 : carrier);
        }
        tier0_test_mask_emit(program, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_VOID, 0);
        ok = ok && tier1_masks_test_expect(test, !program->overflow, "the carrier result's program fits");
        Tier0Function functions[2] = { tier0_test_mask_function(program), tier0_test_mask_leaf() };
        functions[0].operations = &row;
        functions[0].operation_count = 1;
        for (uint32_t input = 0; ok && input < BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS; input += 1)
        {
            uint64_t* in = inputs + (uint64_t)input * (BUSTER_TIER1_MASKS_TEST_IN / 8);
            uint8_t* wanted = expected + (uint64_t)input * BUSTER_TIER1_MASKS_TEST_OUT;
            tier0_test_mask_inputs(in, &state);
            memset(wanted, 0xa5, BUSTER_TIER1_MASKS_TEST_OUT);
            const uint8_t* raw = (const uint8_t*)in;
            for (uint32_t k = 0; k < 4; k += 1)
            {
                uint8_t* out = wanted + k * out_bytes;
                if (permute)
                {
                    // Each lane: of the first table's 16 bytes then the second's, each zero past its carrier, the one its
                    // index's low bits name.
                    uint8_t tables[32] = {0};
                    memcpy(tables, raw + (k * 6) % 32 * 8, carrier);
                    memcpy(tables + 16, raw + (k * 6 + 2) % 32 * 8, carrier);
                    const uint8_t* indices = raw + (k * 6 + 4) % 32 * 8;
                    const uint32_t element = 1u << size;
                    for (uint32_t lane = 0; lane < 16 / element; lane += 1)
                    {
                        uint64_t index = 0;
                        memcpy(&index, indices + lane * element, element);
                        const uint32_t picked = (uint32_t)(index & (2 * 16 / element - 1));
                        memcpy(out + lane * element, tables + picked * element, element);
                    }
                }
                else
                {
                    const uint8_t* a = raw + k * source_bytes % 256;
                    for (uint32_t lane = 0; lane < lanes; lane += 1)
                    {
                        uint64_t value = 0;
                        memcpy(&value, a + (lane << from), 1u << from);
                        const uint64_t narrowed = tier1_masks_test_convert_lane(value, 1u << from, 1u << to, sign != 0, 0);
                        memcpy(out + (lane << to), &narrowed, 1u << to);
                    }
                }
            }
        }
        bool machine = false;
        ok = ok && tier1_masks_test_typed_pipeline(test, name, functions, true, inputs, expected, arenas, &machine);
        ok = ok && tier1_masks_test_expect(test, !machine, "a truncation into a carrier or a permute of carriers is the IR's, not a machine row");
        test->carrier_result_rows += ok;
        tier1_masks_test_expect(test, test->scratch.length == 0, "the carrier results return the scratch arena");
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
        else if (strcmp(argv[i], "--fixtures") == 0 && i + 1 < argc)
        {
            i += 1;
            tier1_masks_test_fixture_root = argv[i];
            valid = argv[i][0] != 0;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-tier1-masks [--seed N] [--print] [--fixtures DIRECTORY]\n");
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
            .vnni = malloc(X86_FORM_COUNT),
            .output = arena_reserve(64u << 20),
            .scratch = arena_reserve(64u << 20),
            .execution = arena_reserve(64u << 20),
            .watched_forms = { X86_FORM_NONE, X86_FORM_NONE },
        };
        uint8_t* baseline = malloc(X86_FORM_COUNT);
        uint8_t* foundation = malloc(X86_FORM_COUNT);
        const bool ready = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && test.v4 != 0 && test.fp16 != 0 && test.vnni != 0 && test.v3 != 0 && test.v2 != 0 && test.avx != 0 && baseline != 0 && foundation != 0 && test.output.base != 0 && test.scratch.base != 0 && test.execution.base != 0;
        if (tier1_masks_test_expect(&test, ready, "initialize the tables and arenas"))
        {
            X86Target v4 = { 0 };
            const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
            tier1_masks_test_expect(&test, parsed, "the x86-64-v4 target");
            x86_target_forms(&v4, test.v4);
            memcpy(test.fp16, test.v4, X86_FORM_COUNT);
            test.fp16[X86_FORM_VFMULCPH_Z_Z_ZM512] = 1;
            X86Target vnni = v4;
            vnni.features |= BUSTER_X86_FEATURE(AVX512VNNI);
            x86_target_forms(&vnni, test.vnni);
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
            test.native_vnni = (host.features & BUSTER_X86_FEATURE(AVX512VNNI)) != 0;
            Tier1MasksFixtures fixtures;
            const bool loaded = tier1_masks_test_fixtures_load(&fixtures);
            char problem[sizeof(fixtures.problem) + 64];
            snprintf(problem, sizeof(problem), "IR fixtures (#2053): %s", fixtures.problem);
            tier1_masks_test_expect(&test, loaded, problem);
            const uint32_t fixture_count = loaded ? fixtures.count : 0;
            for (uint32_t f_index = 0; f_index < fixture_count; f_index += 1)
            {
                tier1_masks_test_fixture(&test, &fixtures.cases[f_index]);
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
            tier1_masks_test_shuffle_immediates(&test);
            tier1_masks_test_conversions(&test);
            tier1_masks_test_carriers(&test);
            tier1_masks_test_carrier_results(&test);
            tier1_masks_test_bitshuffles(&test);
            tier1_masks_test_quadword_ops(&test);
            tier1_masks_test_pairwise_dots(&test);
            tier1_masks_test_blendvs(&test);
            tier1_masks_test_funnels(&test);
            tier1_masks_test_shifts(&test);
            tier1_masks_test_mask_vectors(&test);
            tier1_masks_test_lane_ops(&test);
            tier1_masks_test_permute2s(&test);
            tier1_masks_test_ternlogs(&test);
            tier1_masks_test_compresses(&test);
            tier1_masks_test_masked_memory(&test);
            tier1_masks_test_gathers(&test);
            tier1_masks_test_scatters(&test);
            tier1_masks_test_scattered_stores(&test);
            tier1_masks_test_fmas(&test);
            tier1_masks_test_lane_moves(&test);
            tier1_masks_test_reductions(&test);
            tier1_masks_test_lane_unaries(&test);
            tier1_masks_test_frounds(&test);
            tier1_masks_test_cvtts(&test);
            if (loaded)
            {
                tier1_masks_test_refusals(&test, baseline, foundation, fixtures.cases[0].text);
            }
            tier1_masks_test_scan(&test);
            tier1_masks_test_forms(&test);
            uint32_t runnable = 0;
            for (uint32_t f_index = 0; f_index < fixture_count; f_index += 1)
            {
                const Tier1MasksFixture* fixture = &fixtures.cases[f_index];
                runnable += (!fixture->fp16 || test.native_fp16) && (!fixture->vnni || test.native_vnni) ? 1 : 0;
            }
            const uint64_t expected_runs = test.native ? 2u * runnable * BUSTER_TIER1_MASKS_TEST_INPUTS : 0;
            tier1_masks_test_expect(&test, test.native_fixtures == (test.native ? runnable : 0) && test.native_runs == expected_runs,
                                    "every fixture runs natively on every input, with rematerialization and without, where the host can");
            tier1_masks_test_expect(&test, test.recomputed != 0, "with rematerialization, general values beside the masks are recomputed");
            tier1_masks_test_expect(&test, test.refused == 5, "every refusal");
            // Tier 0's programs through the IR: every one taken whole, some with machine rows, and natively by both paths.
            tier1_masks_test_expect(&test, test.typed_programs == BUSTER_TIER1_MASKS_TEST_PROGRAMS && test.typed_machine != 0 && test.typed_machine < test.typed_programs,
                                    "Tier 0's mask programs go through the IR and Tier 1, those whose rows the decoders refuse as machine rows");
            tier1_masks_test_expect(&test, !test.native || test.typed_runs == BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS * (test.typed_programs + test.builtin_rows + test.compare_rows + test.select_rows + test.width_rows + test.permute_rows + test.convert_rows +
                                                                               test.shift_rows + test.mask_vector_rows + test.lane_op_rows + test.permute2_rows + test.ternlog_rows + test.compress_rows +
                                                                               test.masked_rows + test.gather_rows + test.scatter_rows + test.compress_store_rows + test.truncating_store_rows + test.fma_rows + test.lane_rows + test.reduce_rows + test.unary_rows + test.fround_rows + test.cvtt_rows +
                                                                               test.shuffle_rows + test.carrier_rows + test.bitshuffle_rows + test.quadword_rows + test.pairwise_rows + test.blendv_rows + test.carrier_result_rows +
                                                                               test.funnel_rows),
                                    "every typed program and front-end row runs natively by Tier 1");
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
            // The funnel shifts (#1840): the 4 rotates at 3 widths at 6 immediates, and the 18 funnels on a host that runs them.
            tier1_masks_test_expect(&test, test.funnel_attempts == 4u * 3 * 6 + (!test.native || (x86_target_native().features & BUSTER_X86_FEATURE(AVX512VBMI2)) != 0 ? 18u : 0u) &&
                                               test.funnel_rows == test.funnel_attempts,
                                    "every funnel shift and rotate goes through the IR and Tier 1 as the IR's funnel");
            // The quadword operations (#1840): vpsadbw, vpmuludq and vpmuldq (#2780) at 3 widths, and vpmultishiftqb at 3 on a
            // host that runs it.
            tier1_masks_test_expect(&test, test.quadword_attempts == 9u + (!test.native || (x86_target_native().features & BUSTER_X86_FEATURE(AVX512VBMI)) != 0 ? 3u : 0u) &&
                                               test.quadword_rows == test.quadword_attempts,
                                    "every multishift, sum of absolute differences and product of doublewords goes through the IR and Tier 1 as the IR's quadword_op");
            // The pairwise operations (#2780): 6 at 3 widths, and the 4 dot products at 3 on a host that runs them.
            tier1_masks_test_expect(&test, test.pairwise_attempts == 18u + (!test.native || (x86_target_native().features & BUSTER_X86_FEATURE(AVX512VNNI)) != 0 ? 12u : 0u) &&
                                               test.pairwise_rows == test.pairwise_attempts,
                                    "every pairwise operation and dot product goes through the IR and Tier 1 as the IR's pairwise or dot");
            // The truncations into an SSE carrier (#2780): 18 shapes; the permutes of two carriers: 2 lanes, 2 carriers.
            tier1_masks_test_expect(&test, test.carrier_result_attempts == 18 + 4 && test.carrier_result_rows == test.carrier_result_attempts,
                                    "every truncation into a carrier and permute of carriers goes through the IR and Tier 1 as the IR's");
            // The blends by a vector's signs (#2780): pblendvb, blendvps and blendvpd at 16 and 32 bytes.
            tier1_masks_test_expect(&test, test.blendv_attempts == 6 && test.blendv_rows == test.blendv_attempts,
                                    "every blend by a vector's signs goes through the IR as its select and Tier 1 as one blend");
            // The bit shuffles into a mask (#1840): vpshufbitqmb at 3 widths, on a host that runs them.
            tier1_masks_test_expect(&test, test.bitshuffle_attempts == (!test.native || (x86_target_native().features & BUSTER_X86_FEATURE(AVX512BITALG)) != 0 ? 3u : 0u) &&
                                               test.bitshuffle_rows == test.bitshuffle_attempts,
                                    "every bit shuffle into a mask goes through the IR and Tier 1 as the IR's bitshuffle under its writemask");
            // The SSE carriers (#1840): 18 extensions of a cast and 18 of a load, 6 casts stored and 6 of a live vector.
            tier1_masks_test_expect(&test, test.carrier_attempts == 18 + 18 + 6 + 6 && test.carrier_rows == test.carrier_attempts,
                                    "every carrier's cast and extension goes through the IR and Tier 1 as the IR's lane_extract and extend");
            // The byte shuffles by an immediate (#1840): pshufd, pshuflw, pshufhw, pslldqi and psrldqi at 3 widths, at 8
            // immediates.
            tier1_masks_test_expect(&test, test.shuffle_attempts == 5 * 3 * 8 && test.shuffle_rows == test.shuffle_attempts,
                                    "every front-end byte shuffle by an immediate goes through the IR and Tier 1 as the IR's shuffle by a constant, and back to its immediate form");
        printf("test-tier1-masks: the front end's selects and undefined vectors: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its vselect and zero vector\n",
               test.select_rows);
            // The conversions (#1840): convertvector's and every masked truncation.
            tier1_masks_test_expect(&test, test.convert_attempts > 40 && test.convert_rows == test.convert_attempts,
                                    "every front-end conversion goes through the IR and Tier 1 as the IR's extend or truncate");
            // The front end's rows: every rule of the five families, the shifts at every count.
            tier1_masks_test_expect(&test, test.builtin_attempts != 0 && test.builtin_rows == test.builtin_attempts, "every front-end opmask row goes through Tier 0, the IR and Tier 1");
            tier1_masks_test_expect(&test, !test.native || test.builtin_native == BUSTER_TIER1_MASKS_TEST_TYPED_INPUTS * test.builtin_rows, "Tier 0 runs every front-end row natively");
            printf("test-tier1-masks: %u IR fixtures from %s/tier1/masks (#2053), %u of them natively\n", fixture_count, tier1_masks_test_fixture_root, test.native_fixtures);
            arena_release(&fixtures.storage);
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
        tier1_masks_test_expect(&test, test.gather_attempts == 10 && test.gather_rows == test.gather_attempts, "every gather goes through the IR and Tier 1 as the IR's");
        tier1_masks_test_expect(&test, test.scatter_attempts == 10 && test.scatter_rows == test.scatter_attempts, "every scatter goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the scatters: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its scatter\n", test.scatter_rows);
        tier1_masks_test_expect(&test, test.compress_store_attempts == 6 && test.compress_store_rows == test.compress_store_attempts &&
                                           test.truncating_store_attempts == 24 + 9 && test.truncating_store_rows == test.truncating_store_attempts,
                                "every compressing and truncating store goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the gathers, compressing stores and truncating stores: %u, %u and %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively "
               "as its gather, compress_store and masked_store of a truncation\n",
               test.gather_rows, test.compress_store_rows, test.truncating_store_rows);
        tier1_masks_test_expect(&test, test.fma_attempts == 8 && test.fma_rows == test.fma_attempts, "every fused multiply-add goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the scalar fused multiply-adds: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its fma\n", test.fma_rows);
        tier1_masks_test_expect(&test, test.lane_attempts == 50 && test.lane_rows == test.lane_attempts, "every lane move goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the one-lane moves and movemasks: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its lane_extract, "
               "lane_insert and movemask\n",
               test.lane_rows);
        tier1_masks_test_expect(&test, test.reduce_attempts == 54 && test.reduce_rows == test.reduce_attempts, "every reduction goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the reductions: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its reduce\n", test.reduce_rows);
        tier1_masks_test_expect(&test, test.unary_attempts >= 18 && test.unary_rows == test.unary_attempts, "every lane unary operation goes through the IR and Tier 1 as the IR's");
        printf("test-tier1-masks: the lanes' population counts, leading zero counts, conflicts and absolute values: %u through the IR's interpreter, SSA, the optimizer and Tier 1 "
               "natively as its lane_unary\n",
               test.unary_rows);
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
        printf("test-tier1-masks: the funnel shifts and rotates: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its funnel\n",
               test.funnel_rows);
        printf("test-tier1-masks: the quadword operations: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its quadword_op\n",
               test.quadword_rows);
        printf("test-tier1-masks: the blends by a vector's signs: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its vselect, one "
               "blend each\n",
               test.blendv_rows);
        printf("test-tier1-masks: the pairwise operations and dot products: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its pairwise "
               "and dot\n",
               test.pairwise_rows);
        printf("test-tier1-masks: the bit shuffles into a mask: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its bitshuffle\n",
               test.bitshuffle_rows);
        printf("test-tier1-masks: the SSE carriers: %u casts and extensions through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its lane_extract "
               "and extend\n",
               test.carrier_rows);
        printf("test-tier1-masks: the front end's byte shuffles by an immediate: %u through the IR's interpreter, SSA, the optimizer and Tier 1 natively as its shuffle by a "
               "constant, emitted as the immediate forms\n",
               test.shuffle_rows);
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
        printf("test-tier1-masks: %" PRIu64 " functions' code named ymm0 to zmm15, each with a vzeroupper before its calls and rets (#2830)\n", test.upper_dirty);
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
        free(test.vnni);
    }
    return result;
}
