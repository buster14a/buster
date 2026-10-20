// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Branch sizing: the rel8 or rel32 form of each branch inside a function, chosen before the function is encoded (#144).
// The code generator emits every branch to a label in its rel8 form (JMP_REL8 or a Jcc_REL8) with displacement 0, and
// two columns beside the records: the branch records' indices, ascending, and each branch's target, the index of the
// record its label marks (the function's end is the record count). Sizing starts every branch short, computes every
// offset, grows each branch whose displacement doesn't fit in [-128, 127] to its rel32 form (3 bytes longer for jmp, 4
// for jcc) and repeats until no branch grows. Then it writes each branch's form and displacement into its record, and
// each record's offset. Lengths only grow, so it ends; and every order of growing out-of-range branches ends at the
// same fixpoint, the smallest one, since a branch grows only when it is out of range with lengths no longer than that
// fixpoint's. Calls and other symbolic operands are records marked BUSTER_X86_FORM_FIXUP, which sizing leaves alone; it
// changes forms and lengths, never indices.
//
// Branch space. The rounds work on the branches alone. With every branch short, branch k (at record r_k, aiming at
// record t_k) has the displacement D0_k = offset(t_k) - offset(r_k) - 2 that x86_encode's offsets give (the first
// round's lengths). With growths g (0 for a short branch, 3 or 4 for a long one) and P their exclusive prefix sums over
// the branches, its displacement is D0_k + P[rank_k] - P[k + 1], where rank_k is the number of branches before record
// t_k: the growth between its end and its target, which for a backward branch is negative and holds its own. So a round
// is one pass over the branches, 16 at a time: gather P at the ranks, compare the displacements with the rel8 range
// into a mask, grow the masked branches, and take the next round's prefix sums with in-register scans. The offsets
// change once, at the end, by the growth before each record.
//
// A short branch's reach. Every branch is at least 2 bytes, so one with 64 branches or more between it and its target
// is out of range with all of them short, and grows in the first round. A branch that is short after that has at most
// 63 branches in its span, so a growth can only change the displacements of the short branches within 63 branches of
// it (BUSTER_BRANCH_SIZING_REACH), and their displacements are sums of at most 64 growths.
//
// Each round settles the branches whose spans held a branch that grew in the round before, so a chain of branches each
// pushed out of range by the next takes a round per link. Generated functions settle in one to three; past
// BUSTER_BRANCH_SIZING_ROUNDS rounds, the rare cases' path (branch_sizing_worklist) grows the rest one branch at a time,
// recomputing only the displacements of the short branches near each growth: so sizing takes time linear in the
// branches whatever the function's shape.
//
// Entry points: size_branches (the vector path) and size_branches_scalar (the scalar path: the same phases, a branch or
// a record at a time, its twin and the tests' oracle; both give identical results, rounds included). Both require
// x86_encoder_initialize and take their memory from the thread's scratch arenas (lane.c). Files that use them include
// this file after x86_64_encoder.c and lane.c; every function in it is static.
//
// Layout, in this order:
//   - API: BranchSizingError, BranchSizing, size_branches and size_branches_scalar.
//   - Forms: branch_sizing_growth and branch_sizing_growth_vector.
//   - Columns: BranchSizingColumns and branch_sizing_allocate.
//   - The phases, each a vector function and a scalar one: validate (the columns, and each branch's growth), lengths
//     (x86_encode's offsets: the first round), setup (D0 and the first round's growths), ranks (only when a branch
//     grew), round, prefix and offsets; then the shared ones, branch_sizing_worklist (the rare cases) and
//     branch_sizing_write (the records).
//   - The driver: branch_sizing_run.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum BranchSizingError
{
    BRANCH_SIZING_OK,
    // More records than x86_encode takes in one call (BUSTER_X86_ENCODER_MAX_INSTRUCTIONS).
    BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS,
    // A branch's record index isn't below the record count, or isn't above the branch before it's (`index` is the
    // branch's).
    BRANCH_SIZING_ERROR_BRANCH_INDEX,
    // A branch's target is past the record count (`index` is the branch's).
    BRANCH_SIZING_ERROR_TARGET,
    // A branch's record isn't JMP_REL8 or a Jcc_REL8, without a segment override or the fixup flag (`index` is the
    // branch's).
    BRANCH_SIZING_ERROR_FORM,
    // x86_encode rejects a record (`index` is the first that x86_validate rejects).
    BRANCH_SIZING_ERROR_INVALID_INSTRUCTION,
    // With every branch long, the function could pass INT32_MAX bytes, past what a rel32 reaches.
    BRANCH_SIZING_ERROR_TOO_LARGE,
    // The scratch arena has no room for the columns.
    BRANCH_SIZING_ERROR_MEMORY,
} BranchSizingError;

typedef struct BranchSizing BranchSizing;
struct BranchSizing
{
    // The function's bytes.
    uint64_t length;
    BranchSizingError error;
    // The branch or the record the error is about.
    uint64_t index;
    // How many times the displacements were computed for every branch: 1 when every branch fits with all of them short.
    uint32_t rounds;
    // The branches that ended long (rel32).
    uint64_t long_branches;
    // The displacements branch_sizing_worklist computed, one at a time (0 unless the rounds reached
    // BUSTER_BRANCH_SIZING_ROUNDS).
    uint64_t evaluations;
};

// Sizes the branches of a function of `count` records: `branches` are the branch records' indices, ascending, and
// `targets` each branch's target record (`count` for the function's end), `branch_count` of each. On success, writes
// each branch's form (its rel8 one or the rel32 one after it) and displacement into its record, and each record's offset
// into `offsets` (`count` of them), and returns the function's length. Every rel8 is in range, so x86_validate passes
// for the branches, and x86_encode gives the bytes. On an error, the records are as they were and the offsets'
// contents are unspecified. Requires x86_encoder_initialize.
static __attribute__((unused)) BranchSizing size_branches(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                          uint64_t branch_count, uint32_t* offsets);
// The same, a branch or a record at a time: the vector path's twin and oracle, with the same result.
static __attribute__((unused)) BranchSizing size_branches_scalar(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                                 uint64_t branch_count, uint32_t* offsets);

// ---------------------------------------------------------------------------------------------------------------------
// Forms

// A short branch's length, and how much each grows by: JMP_REL8's EB rel8 to E9 rel32, and a Jcc_REL8's 7x rel8 to
// 0F 8x rel32.
#define BUSTER_BRANCH_SIZING_SHORT_BYTES 2
#define BUSTER_BRANCH_SIZING_JMP_GROWTH 3
#define BUSTER_BRANCH_SIZING_JCC_GROWTH 4
#define BUSTER_BRANCH_SIZING_MAX_GROWTH BUSTER_BRANCH_SIZING_JCC_GROWTH
// The rel8 range: a displacement d fits when d + 128, as an unsigned number, is at most 255.
#define BUSTER_BRANCH_SIZING_REL8_BIAS 128
#define BUSTER_BRANCH_SIZING_REL8_SPAN 255
// The branches a short branch's span holds, at most: 63, of at least 2 bytes each, past which it's out of range.
#define BUSTER_BRANCH_SIZING_REACH 63
// Dwords in a ZMM register: branches or records per vector operation.
#define BUSTER_BRANCH_SIZING_LANES 16
// Records per x86_encode call of the first round, whose bytes go into one scratch buffer and are dropped: 1,024
// records' 15 KB stay in L1, where the whole function's could take megabytes of fresh pages.
#define BUSTER_BRANCH_SIZING_CHUNK 1024
// Rounds before the rare cases' path takes over (see the file's comment).
#define BUSTER_BRANCH_SIZING_ROUNDS 8
// The ranks' table has an entry per 32 records: their branch bits, and the branches before them in the high half.
#define BUSTER_BRANCH_SIZING_RANK_RECORDS 32
// The vector path's map of branch records has a byte per record, read 64 at a time.
#define BUSTER_BRANCH_SIZING_MAP_RECORDS 64

// A condition's forms are 6 rows of the form table apart (Jcc_REL8, Jcc_REL32, SETcc and three CMOVcc), and the 16
// conditions' in a row; a branch's rel32 form is the row after its rel8 one. The vector path divides by the stride with
// a multiplication: (delta * 43) >> 8 is delta / 6 for every delta below 16 * 6.
#define BUSTER_BRANCH_SIZING_CONDITION_STRIDE (X86_FORM_JNO_REL8 - X86_FORM_JO_REL8)
#define BUSTER_BRANCH_SIZING_CONDITIONS 16
#define BUSTER_BRANCH_SIZING_STRIDE_RECIPROCAL 43
#define BUSTER_BRANCH_SIZING_STRIDE_SHIFT 8
_Static_assert(BUSTER_BRANCH_SIZING_CONDITION_STRIDE == 6, "the vector path divides by the condition stride with a multiplication");
_Static_assert(X86_FORM_JG_REL8 == X86_FORM_JO_REL8 + 15 * BUSTER_BRANCH_SIZING_CONDITION_STRIDE, "the conditions' forms are in a row");
_Static_assert(X86_FORM_JMP_REL32 == X86_FORM_JMP_REL8 + 1, "a branch's rel32 form follows its rel8 one");
#define BUSTER_BRANCH_SIZING_CHECK_CONDITION(CC, K) \
    _Static_assert(X86_FORM_J##CC##_REL8 == X86_FORM_JO_REL8 + K * BUSTER_BRANCH_SIZING_CONDITION_STRIDE && X86_FORM_J##CC##_REL32 == X86_FORM_J##CC##_REL8 + 1, \
                   "the condition's rel8 form is at its place, and its rel32 form follows it");
BUSTER_BRANCH_SIZING_CHECK_CONDITION(O, 0)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(NO, 1)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(B, 2)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(AE, 3)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(E, 4)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(NE, 5)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(BE, 6)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(A, 7)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(S, 8)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(NS, 9)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(P, 10)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(NP, 11)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(L, 12)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(GE, 13)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(LE, 14)
BUSTER_BRANCH_SIZING_CHECK_CONDITION(G, 15)
#undef BUSTER_BRANCH_SIZING_CHECK_CONDITION
_Static_assert(__builtin_offsetof(X86Instruction, form) == 12, "the vector path gathers a record's form as its fourth dword");
_Static_assert(BUSTER_X86_ENCODER_MAX_INSTRUCTIONS < (1u << 30), "a record's index, doubled for the form's gather, fits in a signed dword");

// How many bytes a branch of the form grows by from rel8 to rel32: BUSTER_BRANCH_SIZING_JMP_GROWTH for JMP_REL8,
// BUSTER_BRANCH_SIZING_JCC_GROWTH for a Jcc_REL8, and 0 for any other form (or flag: a segment override or
// BUSTER_X86_FORM_FIXUP), which isn't a branch sizing takes.
static uint32_t branch_sizing_growth(uint32_t form)
{
    const uint32_t delta = form - X86_FORM_JO_REL8;
    uint32_t result = form == X86_FORM_JMP_REL8 ? BUSTER_BRANCH_SIZING_JMP_GROWTH : 0;
    if (delta < BUSTER_BRANCH_SIZING_CONDITIONS * BUSTER_BRANCH_SIZING_CONDITION_STRIDE && delta % BUSTER_BRANCH_SIZING_CONDITION_STRIDE == 0)
    {
        result = BUSTER_BRANCH_SIZING_JCC_GROWTH;
    }
    return result;
}

// branch_sizing_growth of 16 forms.
BUSTER_INLINE __m512i branch_sizing_growth_vector(__m512i form)
{
    const __m512i stride = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_CONDITION_STRIDE);
    const __m512i delta = _mm512_sub_epi32(form, _mm512_set1_epi32(X86_FORM_JO_REL8));
    const __m512i quotient = _mm512_srli_epi32(_mm512_mullo_epi32(delta, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_STRIDE_RECIPROCAL)), BUSTER_BRANCH_SIZING_STRIDE_SHIFT);
    const __mmask16 below = _mm512_cmplt_epu32_mask(delta, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_CONDITIONS * BUSTER_BRANCH_SIZING_CONDITION_STRIDE));
    const __mmask16 condition = _mm512_mask_cmpeq_epi32_mask(below, _mm512_mullo_epi32(quotient, stride), delta);
    const __mmask16 jump = _mm512_cmpeq_epi32_mask(form, _mm512_set1_epi32(X86_FORM_JMP_REL8));
    const __m512i growth = _mm512_maskz_mov_epi32(condition, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JCC_GROWTH));
    return _mm512_mask_mov_epi32(growth, jump, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JMP_GROWTH));
}

// Whether a displacement doesn't fit in a rel8.
static bool branch_sizing_out_of_range(int32_t displacement)
{
    return (uint32_t)displacement + BUSTER_BRANCH_SIZING_REL8_BIAS > BUSTER_BRANCH_SIZING_REL8_SPAN;
}

BUSTER_INLINE __mmask16 branch_sizing_out_of_range_vector(__m512i displacement)
{
    const __m512i biased = _mm512_add_epi32(displacement, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_REL8_BIAS));
    return _mm512_cmpgt_epu32_mask(biased, _mm512_set1_epi32(BUSTER_BRANCH_SIZING_REL8_SPAN));
}

// The inclusive prefix sums of 16 dwords: four shifts by 1, 2, 4 and 8 lanes (valignd with zeros) and adds.
BUSTER_INLINE __m512i branch_sizing_scan(__m512i x)
{
    const __m512i zero = _mm512_setzero_si512();
    x = _mm512_add_epi32(x, _mm512_alignr_epi32(x, zero, 15));
    x = _mm512_add_epi32(x, _mm512_alignr_epi32(x, zero, 14));
    x = _mm512_add_epi32(x, _mm512_alignr_epi32(x, zero, 12));
    x = _mm512_add_epi32(x, _mm512_alignr_epi32(x, zero, 8));
    return x;
}

// The last lane in every lane.
BUSTER_INLINE __m512i branch_sizing_last(__m512i x)
{
    return _mm512_permutexvar_epi32(_mm512_set1_epi32(BUSTER_BRANCH_SIZING_LANES - 1), x);
}

// The lanes of the 16 items from `start` that are below `count`.
BUSTER_INLINE __mmask16 branch_sizing_lanes(uint64_t start, uint64_t count)
{
    const uint64_t left = count - start;
    return left >= BUSTER_BRANCH_SIZING_LANES ? (__mmask16)0xffff : (__mmask16)((1u << left) - 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// Columns

// The columns of a function's branches, padded to whole vectors and one more (the vector path's loads and stores of
// the prefix sums at k + 1, and its unaligned loads of the growths when it expands them over the records), and the
// record space's buffers. All of it is scratch memory, of the sizes branch_sizing_allocate takes.
typedef struct BranchSizingColumns BranchSizingColumns;
struct BranchSizingColumns
{
    // The first round's bytes, BUSTER_BRANCH_SIZING_CHUNK records at a time.
    uint8_t* encoded;
    // The vector path's map: a byte per record, up to a whole BUSTER_BRANCH_SIZING_MAP_RECORDS past the count, 1 for a
    // branch's.
    uint8_t* map;
    // An entry per BUSTER_BRANCH_SIZING_RANK_RECORDS records, up to the count's: their branch bits in the low half, and
    // the number of branches before them in the high half.
    uint64_t* ranks;
    // Per branch: its growth if it's long (BUSTER_BRANCH_SIZING_JMP_GROWTH or BUSTER_BRANCH_SIZING_JCC_GROWTH), its
    // displacement with every branch short (D0), its target's rank, its growth now (0 while it's short) and its
    // displacement from the last round.
    uint32_t* grow;
    int32_t* displacement0;
    uint32_t* rank;
    uint32_t* growth;
    int32_t* displacement;
    // The exclusive prefix sums of the growths (prefix[i][k] is the growth of the branches before k), of this round's
    // growths and the last round's: rounds read one and write the other.
    uint32_t* prefix[2];
    // branch_sizing_worklist's dirty bits, one per branch.
    uint64_t* dirty;
    bool valid;
};

// The columns of a function of `count` records and `branch_count` branches, with the vector path's map if `vector`.
static BranchSizingColumns branch_sizing_allocate(Arena* arena, uint64_t count, uint64_t branch_count, bool vector)
{
    const uint64_t padded = ((branch_count + BUSTER_BRANCH_SIZING_LANES - 1) & ~(uint64_t)(BUSTER_BRANCH_SIZING_LANES - 1)) + BUSTER_BRANCH_SIZING_LANES;
    const uint64_t units = count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1;
    BranchSizingColumns result = {
        .encoded = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(BUSTER_BRANCH_SIZING_CHUNK), 64),
        .map = vector ? arena_push(arena, units * BUSTER_BRANCH_SIZING_MAP_RECORDS, 64) : 0,
        .ranks = arena_push_array(arena, units * (BUSTER_BRANCH_SIZING_MAP_RECORDS / BUSTER_BRANCH_SIZING_RANK_RECORDS), sizeof(uint64_t), 64),
        .grow = arena_push_array(arena, padded, sizeof(uint32_t), 64),
        .displacement0 = arena_push_array(arena, padded, sizeof(int32_t), 64),
        .rank = arena_push_array(arena, padded, sizeof(uint32_t), 64),
        .growth = arena_push_array(arena, padded, sizeof(uint32_t), 64),
        .displacement = arena_push_array(arena, padded, sizeof(int32_t), 64),
        .prefix = {
            arena_push_array(arena, padded + BUSTER_BRANCH_SIZING_LANES, sizeof(uint32_t), 64),
            arena_push_array(arena, padded + BUSTER_BRANCH_SIZING_LANES, sizeof(uint32_t), 64),
        },
        .dirty = arena_push_array(arena, branch_count / 64 + 1, sizeof(uint64_t), 64),
    };
    result.valid = result.encoded != 0 && (result.map != 0 || !vector) && result.ranks != 0 && result.grow != 0 && result.displacement0 != 0 &&
                   result.rank != 0 && result.growth != 0 && result.displacement != 0 && result.prefix[0] != 0 && result.prefix[1] != 0 && result.dirty != 0;
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validate: the branch columns and the branches' forms, and each branch's growth if it's long.

// Branch k's error, or BRANCH_SIZING_OK: its record must be below `count` and above the branch before it's, its
// target at most `count`, and its record's form one of the branches' rel8 forms.
static BranchSizingError branch_sizing_check(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t k)
{
    const uint32_t record = branches[k];
    BranchSizingError result = BRANCH_SIZING_OK;
    if (record >= count || (k != 0 && record <= branches[k - 1]))
    {
        result = BRANCH_SIZING_ERROR_BRANCH_INDEX;
    }
    else if (targets[k] > count)
    {
        result = BRANCH_SIZING_ERROR_TARGET;
    }
    else if (branch_sizing_growth(records[record].form) == 0)
    {
        result = BRANCH_SIZING_ERROR_FORM;
    }
    return result;
}

// Validates the branches one at a time and fills `grow`. Returns the first invalid branch's index, or branch_count.
static uint64_t branch_sizing_validate_scalar(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                              uint64_t branch_count, uint32_t* grow)
{
    uint64_t result = branch_count;
    for (uint64_t k = 0; result == branch_count && k < branch_count; k += 1)
    {
        if (branch_sizing_check(records, count, branches, targets, k) != BRANCH_SIZING_OK)
        {
            result = k;
        }
        else
        {
            grow[k] = branch_sizing_growth(records[branches[k]].form);
        }
    }
    return result;
}

// The same, 16 branches at a time: each lane's record against the count and against the lane before (the last block's
// last lane for the first), its target against the count, and its form, gathered as its record's fourth dword, by
// branch_sizing_growth_vector. The first invalid lane is the first invalid branch, since the ones before it are valid.
static uint64_t branch_sizing_validate_vector(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                              uint64_t branch_count, uint32_t* grow)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i count_vector = _mm512_set1_epi32((int32_t)count);
    const __m512i form_bits = _mm512_set1_epi32(0xffff);
    const uint8_t* forms = (const uint8_t*)records + __builtin_offsetof(X86Instruction, form);
    __m512i previous = zero;
    uint64_t result = branch_count;
    for (uint64_t k = 0; result == branch_count && k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i record = _mm512_maskz_loadu_epi32(lanes, branches + k);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        const __m512i before = _mm512_alignr_epi32(record, previous, BUSTER_BRANCH_SIZING_LANES - 1);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, record, count_vector);
        const __mmask16 ascending = _mm512_cmpgt_epu32_mask(record, before) | (k == 0 ? 1 : 0);
        const __mmask16 aimed = _mm512_cmple_epu32_mask(target, count_vector);
        const __m512i form = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, inside, _mm512_slli_epi32(record, 1), forms, 8), form_bits);
        const __m512i growth = _mm512_maskz_mov_epi32(lanes, branch_sizing_growth_vector(form));
        const __mmask16 valid = inside & ascending & aimed & _mm512_test_epi32_mask(growth, growth);
        _mm512_store_si512(grow + k, growth);
        if (valid != lanes)
        {
            result = k + (uint64_t)__builtin_ctz((uint32_t)(lanes & ~valid));
        }
        previous = record;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lengths: the first round's offsets, x86_encode's, BUSTER_BRANCH_SIZING_CHUNK records at a time into one buffer, each
// chunk's moved by the bytes before it. Returns the function's length with every branch short, or a negative
// X86EncoderError.

static int64_t branch_sizing_lengths(const X86Instruction* records, uint64_t count, uint32_t* offsets, uint8_t* encoded, bool vector)
{
    int64_t result = 0;
    for (uint64_t start = 0; result >= 0 && start < count; start += BUSTER_BRANCH_SIZING_CHUNK)
    {
        const uint64_t chunk = count - start < BUSTER_BRANCH_SIZING_CHUNK ? count - start : BUSTER_BRANCH_SIZING_CHUNK;
        const int64_t length = x86_encode(records + start, chunk, encoded, BUSTER_X86_ENCODER_CAPACITY(BUSTER_BRANCH_SIZING_CHUNK), offsets + start, 0);
        if (length < 0)
        {
            result = length;
        }
        else if (vector)
        {
            const __m512i base = _mm512_set1_epi32((int32_t)result);
            for (uint64_t i = 0; i < chunk; i += BUSTER_BRANCH_SIZING_LANES)
            {
                const __mmask16 lanes = branch_sizing_lanes(i, chunk);
                const __m512i offset = _mm512_maskz_loadu_epi32(lanes, offsets + start + i);
                _mm512_mask_storeu_epi32(offsets + start + i, lanes, _mm512_add_epi32(offset, base));
            }
            result += length;
        }
        else
        {
            for (uint64_t i = 0; i < chunk; i += 1)
            {
                offsets[start + i] += (uint32_t)result;
            }
            result += length;
        }
    }
    return result;
}

// Whether a function of `length` bytes with every branch short could pass INT32_MAX bytes with every branch long.
// Below it, every offset, displacement and prefix sum fits in a signed dword.
static bool branch_sizing_too_large(uint64_t length, uint64_t branch_count)
{
    return length + BUSTER_BRANCH_SIZING_MAX_GROWTH * branch_count > INT32_MAX;
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup: each branch's displacement with every branch short, D0 = offset(t) - offset(r) - 2 (the function's length
// for a target at its end), which is the first round's: the branches out of range grow, the prefix sums are theirs,
// and the prefix sums of no growth, the round before's, are zero. Returns whether any branch grew.

static bool branch_sizing_setup_scalar(uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count, const uint32_t* offsets,
                                       uint32_t length, BranchSizingColumns* columns)
{
    uint32_t* prefix = columns->prefix[0];
    uint32_t sum = 0;
    prefix[0] = 0;
    columns->prefix[1][0] = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        const uint32_t target = targets[k];
        const uint32_t end = target < count ? offsets[target] : length;
        const int32_t displacement = (int32_t)(end - offsets[branches[k]] - BUSTER_BRANCH_SIZING_SHORT_BYTES);
        const uint32_t growth = branch_sizing_out_of_range(displacement) ? columns->grow[k] : 0;
        columns->displacement0[k] = displacement;
        columns->displacement[k] = displacement;
        columns->growth[k] = growth;
        sum += growth;
        prefix[k + 1] = sum;
        columns->prefix[1][k + 1] = 0;
    }
    return sum != 0;
}

// The same, 16 branches at a time, with the records' and the targets' offsets gathered.
static bool branch_sizing_setup_vector(uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count, const uint32_t* offsets,
                                       uint32_t length, BranchSizingColumns* columns)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i count_vector = _mm512_set1_epi32((int32_t)count);
    const __m512i length_vector = _mm512_set1_epi32((int32_t)length);
    const __m512i short_bytes = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_SHORT_BYTES);
    uint32_t* prefix = columns->prefix[0];
    __m512i carry = zero;
    __mmask16 grown = 0;
    prefix[0] = 0;
    columns->prefix[1][0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i record = _mm512_maskz_loadu_epi32(lanes, branches + k);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, target, count_vector);
        const __m512i start = _mm512_mask_i32gather_epi32(zero, lanes, record, offsets, 4);
        const __m512i end = _mm512_mask_i32gather_epi32(length_vector, inside, target, offsets, 4);
        const __m512i displacement = _mm512_maskz_sub_epi32(lanes, end, _mm512_add_epi32(start, short_bytes));
        const __mmask16 out = branch_sizing_out_of_range_vector(displacement);
        // Lanes past the branches have no growth to take (their grow is 0).
        const __m512i growth = _mm512_maskz_mov_epi32(out, _mm512_load_si512(columns->grow + k));
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
        _mm512_store_si512(columns->displacement0 + k, displacement);
        _mm512_store_si512(columns->displacement + k, displacement);
        _mm512_store_si512(columns->growth + k, growth);
        _mm512_storeu_si512(prefix + k + 1, sums);
        _mm512_storeu_si512(columns->prefix[1] + k + 1, zero);
        carry = branch_sizing_last(sums);
        grown |= _mm512_test_epi32_mask(growth, growth);
    }
    return grown != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Ranks: each branch's target's rank, the number of branches before its target record, which the rounds need once a
// branch has grown. The table has an entry per 32 records, their branch bits and the branches before them; a rank is
// the entry's count plus the bits below the target's.

static uint32_t branch_sizing_rank(const uint64_t* ranks, uint32_t record)
{
    const uint64_t entry = ranks[record / BUSTER_BRANCH_SIZING_RANK_RECORDS];
    const uint32_t below = (1u << (record % BUSTER_BRANCH_SIZING_RANK_RECORDS)) - 1;
    return (uint32_t)(entry >> 32) + (uint32_t)__builtin_popcount((uint32_t)entry & below);
}

// The table, by setting each branch's bit and summing the bits of each entry before it; then each target's rank.
static void branch_sizing_ranks_scalar(uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count, BranchSizingColumns* columns)
{
    const uint64_t entries = (count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1) * (BUSTER_BRANCH_SIZING_MAP_RECORDS / BUSTER_BRANCH_SIZING_RANK_RECORDS);
    uint64_t* ranks = columns->ranks;
    memset(ranks, 0, entries * sizeof(uint64_t));
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        ranks[branches[k] / BUSTER_BRANCH_SIZING_RANK_RECORDS] |= 1ull << (branches[k] % BUSTER_BRANCH_SIZING_RANK_RECORDS);
    }
    uint64_t before = 0;
    for (uint64_t i = 0; i < entries; i += 1)
    {
        const uint64_t bits = ranks[i];
        ranks[i] = bits | before << 32;
        before += (uint64_t)__builtin_popcountll(bits);
    }
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        columns->rank[k] = branch_sizing_rank(ranks, targets[k]);
    }
}

// The same, with a byte per record in the map, whose 64 bytes at a time become two entries' bits; and the ranks 16 at a
// time, with each target's entry gathered (8 at a time, as 64-bit elements) and the bits below it counted.
static void branch_sizing_ranks_vector(uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count, BranchSizingColumns* columns)
{
    const uint64_t units = count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1;
    uint8_t* map = columns->map;
    uint64_t* ranks = columns->ranks;
    memset(map, 0, units * BUSTER_BRANCH_SIZING_MAP_RECORDS);
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        map[branches[k]] = 1;
    }
    uint64_t before = 0;
    for (uint64_t unit = 0; unit < units; unit += 1)
    {
        const __m512i bytes = _mm512_load_si512(map + unit * BUSTER_BRANCH_SIZING_MAP_RECORDS);
        const uint64_t bits = _mm512_test_epi8_mask(bytes, bytes);
        ranks[2 * unit] = (uint32_t)bits | before << 32;
        before += (uint64_t)__builtin_popcount((uint32_t)bits);
        ranks[2 * unit + 1] = (bits >> 32) | before << 32;
        before += (uint64_t)__builtin_popcount((uint32_t)(bits >> 32));
    }
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i low_bits = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_RANK_RECORDS - 1);
    const __m512i even = _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    const __m512i odd = _mm512_setr_epi32(1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        const __m512i entry = _mm512_srli_epi32(target, 5);
        const __m512i first = _mm512_mask_i32gather_epi64(zero, (__mmask8)lanes, _mm512_castsi512_si256(entry), ranks, 8);
        const __m512i second = _mm512_mask_i32gather_epi64(zero, (__mmask8)(lanes >> 8), _mm512_extracti64x4_epi64(entry, 1), ranks, 8);
        const __m512i bits = _mm512_permutex2var_epi32(first, even, second);
        const __m512i counts = _mm512_permutex2var_epi32(first, odd, second);
        const __m512i below = _mm512_sub_epi32(_mm512_sllv_epi32(one, _mm512_and_si512(target, low_bits)), one);
        const __m512i rank = _mm512_add_epi32(counts, _mm512_popcnt_epi32(_mm512_and_si512(bits, below)));
        _mm512_store_si512(columns->rank + k, rank);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Rounds: every branch's displacement from the prefix sums of the round before, D0 + P[rank] - P[k + 1]; a branch out of
// range grows (and a long one stays long), and the new growths' prefix sums go to the other buffer. Returns whether a
// growth changed. `current` is the buffer with the prefix sums of the growths in the column.

static bool branch_sizing_round_scalar(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current)
{
    const uint32_t* before = columns->prefix[current];
    uint32_t* after = columns->prefix[current ^ 1];
    uint32_t sum = 0;
    bool result = false;
    after[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        const int32_t displacement = columns->displacement0[k] + (int32_t)(before[columns->rank[k]] - before[k + 1]);
        const uint32_t old = columns->growth[k];
        const uint32_t growth = old != 0 || branch_sizing_out_of_range(displacement) ? columns->grow[k] : 0;
        result |= growth != old;
        columns->growth[k] = growth;
        columns->displacement[k] = displacement;
        sum += growth;
        after[k + 1] = sum;
    }
    return result;
}

// The same, 16 branches at a time, with the prefix sums at the ranks gathered.
static bool branch_sizing_round_vector(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current)
{
    const uint32_t* before = columns->prefix[current];
    uint32_t* after = columns->prefix[current ^ 1];
    __m512i carry = _mm512_setzero_si512();
    __mmask16 changed = 0;
    after[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __m512i rank = _mm512_load_si512(columns->rank + k);
        const __m512i old = _mm512_load_si512(columns->growth + k);
        const __m512i at_target = _mm512_i32gather_epi32(rank, before, 4);
        const __m512i at_end = _mm512_loadu_si512(before + k + 1);
        const __m512i displacement = _mm512_add_epi32(_mm512_load_si512(columns->displacement0 + k), _mm512_sub_epi32(at_target, at_end));
        const __mmask16 grows = _mm512_test_epi32_mask(old, old) | branch_sizing_out_of_range_vector(displacement);
        // Lanes past the branches have no growth to take (their grow is 0).
        const __m512i growth = _mm512_maskz_mov_epi32(grows, _mm512_load_si512(columns->grow + k));
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
        changed |= _mm512_cmpneq_epi32_mask(growth, old);
        _mm512_store_si512(columns->growth + k, growth);
        _mm512_store_si512(columns->displacement + k, displacement);
        _mm512_storeu_si512(after + k + 1, sums);
        carry = branch_sizing_last(sums);
    }
    return changed != 0;
}

// The prefix sums of the growths in the column, into buffer `into`.
static void branch_sizing_prefix_scalar(uint64_t branch_count, BranchSizingColumns* columns, uint32_t into)
{
    uint32_t* prefix = columns->prefix[into];
    uint32_t sum = 0;
    prefix[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        sum += columns->growth[k];
        prefix[k + 1] = sum;
    }
}

static void branch_sizing_prefix_vector(uint64_t branch_count, BranchSizingColumns* columns, uint32_t into)
{
    uint32_t* prefix = columns->prefix[into];
    __m512i carry = _mm512_setzero_si512();
    prefix[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(_mm512_load_si512(columns->growth + k)), carry);
        _mm512_storeu_si512(prefix + k + 1, sums);
        carry = branch_sizing_last(sums);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The rare cases: branch_sizing_worklist, after a round that grew some branches, grows the branches that are still out
// of range one at a time, until none is. A short branch's displacement changes only when a branch in its span grows, so
// the dirty bits mark the short branches whose spans hold a branch that grew: at first those of the round's growths,
// and then those of each growth here. A short branch's span holds at most BUSTER_BRANCH_SIZING_REACH branches (see the
// file's comment), so the marks are among the branches within that many of a growth, and a displacement is a sum of at
// most that many growths and one: each growth here costs a bounded amount of work. It takes the dirty branches lowest
// first, though any order would do. Returns how many displacements it computed.

// Marks the short branches whose spans hold branch k, and returns the word of the lowest one it could have marked.
static uint64_t branch_sizing_mark(uint64_t branch_count, uint64_t k, const uint32_t* rank, const uint32_t* growth, uint64_t* dirty)
{
    const uint64_t first = k >= BUSTER_BRANCH_SIZING_REACH ? k - BUSTER_BRANCH_SIZING_REACH : 0;
    const uint64_t end = branch_count - k > BUSTER_BRANCH_SIZING_REACH ? k + BUSTER_BRANCH_SIZING_REACH + 1 : branch_count;
    for (uint64_t i = first; i < end; i += 1)
    {
        // A forward branch's span holds the branches after it and before its target; a backward one's, the branches from
        // its target's on, up to itself.
        const bool holds = (i < k && rank[i] > k) || (i > k && rank[i] <= k);
        dirty[i / 64] |= (uint64_t)(holds && growth[i] == 0) << (i % 64);
    }
    return first / 64;
}

// Branch j's displacement with the growths in the column: D0 plus the growth in its span, which is negative for a
// backward branch.
static int32_t branch_sizing_displacement(uint64_t j, const int32_t* displacement0, const uint32_t* rank, const uint32_t* growth)
{
    const uint64_t target = rank[j];
    int64_t sum = 0;
    if (target > j)
    {
        for (uint64_t i = j + 1; i < target; i += 1)
        {
            sum += growth[i];
        }
    }
    else
    {
        for (uint64_t i = target; i <= j; i += 1)
        {
            sum -= growth[i];
        }
    }
    return (int32_t)(displacement0[j] + sum);
}

// `current` is the buffer with the prefix sums of the growths in the column, and the other one has the round before's.
static uint64_t branch_sizing_worklist(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current)
{
    const uint32_t* now = columns->prefix[current];
    const uint32_t* before = columns->prefix[current ^ 1];
    const uint64_t words = (branch_count + 63) / 64;
    uint64_t* dirty = columns->dirty;
    uint64_t result = 0;
    memset(dirty, 0, words * sizeof(uint64_t));
    uint64_t word = words;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        // The branches the last round grew.
        if (now[k + 1] - now[k] != before[k + 1] - before[k])
        {
            const uint64_t lowest = branch_sizing_mark(branch_count, k, columns->rank, columns->growth, dirty);
            word = lowest < word ? lowest : word;
        }
    }
    while (word < words)
    {
        if (dirty[word] == 0)
        {
            word += 1;
        }
        else
        {
            const uint64_t j = word * 64 + (uint64_t)__builtin_ctzll(dirty[word]);
            dirty[word] &= dirty[word] - 1;
            result += 1;
            const int32_t displacement = branch_sizing_displacement(j, columns->displacement0, columns->rank, columns->growth);
            if (columns->growth[j] == 0 && branch_sizing_out_of_range(displacement))
            {
                columns->growth[j] = columns->grow[j];
                const uint64_t lowest = branch_sizing_mark(branch_count, j, columns->rank, columns->growth, dirty);
                word = lowest < word ? lowest : word;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Offsets: each record's offset grows by the growth of the branches before it. The records up to the first long
// branch's keep theirs, so the pass starts at the entry of the ranks' table that holds it, where the branches before
// are the entry's count and none has grown. `prefix` holds the final growths' prefix sums.

// The first long branch, or branch_count if none is.
static uint64_t branch_sizing_first_long_scalar(uint64_t branch_count, const uint32_t* growth)
{
    uint64_t k = 0;
    while (k < branch_count && growth[k] == 0)
    {
        k += 1;
    }
    return k;
}

static uint64_t branch_sizing_first_long_vector(uint64_t branch_count, const uint32_t* growth)
{
    uint64_t k = 0;
    __mmask16 grown = 0;
    while (k < branch_count && grown == 0)
    {
        const __m512i block = _mm512_load_si512(growth + k);
        grown = _mm512_test_epi32_mask(block, block);
        k += grown == 0 ? BUSTER_BRANCH_SIZING_LANES : (uint64_t)__builtin_ctz(grown);
    }
    return k < branch_count ? k : branch_count;
}

static void branch_sizing_offsets_scalar(uint64_t count, const uint32_t* branches, uint64_t branch_count, uint32_t* offsets, const BranchSizingColumns* columns)
{
    const uint64_t first = branch_sizing_first_long_scalar(branch_count, columns->growth);
    if (first < branch_count)
    {
        const uint64_t start = branches[first] / BUSTER_BRANCH_SIZING_RANK_RECORDS * BUSTER_BRANCH_SIZING_RANK_RECORDS;
        uint64_t k = columns->ranks[start / BUSTER_BRANCH_SIZING_RANK_RECORDS] >> 32;
        uint32_t growth = 0;
        for (uint64_t i = start; i < count; i += 1)
        {
            offsets[i] += growth;
            if (k < branch_count && branches[k] == i)
            {
                growth += columns->growth[k];
                k += 1;
            }
        }
    }
}

// The same, 16 records at a time: their branch bits (half an entry of the ranks' table) expand the next branches'
// growths over the records, whose exclusive prefix sums, plus the growth before them, move the offsets.
static void branch_sizing_offsets_vector(uint64_t count, const uint32_t* branches, uint64_t branch_count, uint32_t* offsets, const BranchSizingColumns* columns)
{
    const uint64_t first = branch_sizing_first_long_vector(branch_count, columns->growth);
    if (first < branch_count)
    {
        const uint64_t start = branches[first] / BUSTER_BRANCH_SIZING_RANK_RECORDS * BUSTER_BRANCH_SIZING_RANK_RECORDS;
        uint64_t k = columns->ranks[start / BUSTER_BRANCH_SIZING_RANK_RECORDS] >> 32;
        __m512i carry = _mm512_setzero_si512();
        for (uint64_t i = start; i < count; i += BUSTER_BRANCH_SIZING_LANES)
        {
            const __mmask16 lanes = branch_sizing_lanes(i, count);
            const uint64_t entry = columns->ranks[i / BUSTER_BRANCH_SIZING_RANK_RECORDS];
            const __mmask16 marks = (__mmask16)(entry >> (i % BUSTER_BRANCH_SIZING_RANK_RECORDS));
            const __m512i growth = _mm512_maskz_expand_epi32(marks, _mm512_loadu_si512(columns->growth + k));
            const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
            const __m512i offset = _mm512_maskz_loadu_epi32(lanes, offsets + i);
            _mm512_mask_storeu_epi32(offsets + i, lanes, _mm512_add_epi32(offset, _mm512_sub_epi32(sums, growth)));
            carry = branch_sizing_last(sums);
            k += (uint64_t)__builtin_popcount(marks);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The records: each branch's displacement, and the rel32 form for a long one. The records are 16 bytes apart at the
// branches' indices, so these are scalar stores (a scatter would cost more: see the coding style). Returns how many
// branches are long.

static uint64_t branch_sizing_write(X86Instruction* records, const uint32_t* branches, uint64_t branch_count, const BranchSizingColumns* columns)
{
    uint64_t result = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        X86Instruction* record = &records[branches[k]];
        const bool grown = columns->growth[k] != 0;
        record->displacement = columns->displacement[k];
        record->form = (uint16_t)(record->form + grown);
        result += grown;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The driver

// Sizes the branches on the vector path or on the scalar one, going to branch_sizing_worklist after `round_limit`
// rounds (BUSTER_BRANCH_SIZING_ROUNDS for the entry points; the tests take others).
static BranchSizing branch_sizing_run(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                      uint32_t* offsets, bool vector, uint32_t round_limit)
{
    BranchSizing result = {
        .error = BRANCH_SIZING_OK,
    };
    // More branches than records can't be ascending below the count: validation stops at one of the first count + 1.
    const uint64_t branches_checked = branch_count <= count ? branch_count : count + 1;
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result.error = BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else
    {
        Scratch scratch = scratch_begin(0);
        BranchSizingColumns columns = branch_sizing_allocate(scratch.arena, count, branches_checked, vector);
        if (!columns.valid)
        {
            result.error = BRANCH_SIZING_ERROR_MEMORY;
        }
        if (result.error == BRANCH_SIZING_OK)
        {
            const uint64_t invalid = vector ? branch_sizing_validate_vector(records, count, branches, targets, branches_checked, columns.grow)
                                            : branch_sizing_validate_scalar(records, count, branches, targets, branches_checked, columns.grow);
            if (invalid != branches_checked)
            {
                result.error = branch_sizing_check(records, count, branches, targets, invalid);
                result.index = invalid;
            }
        }
        int64_t length = 0;
        if (result.error == BRANCH_SIZING_OK)
        {
            length = branch_sizing_lengths(records, count, offsets, columns.encoded, vector);
            if (length < 0)
            {
                result.error = BRANCH_SIZING_ERROR_INVALID_INSTRUCTION;
                result.index = x86_validate(records, count);
            }
            else if (branch_sizing_too_large((uint64_t)length, branch_count))
            {
                result.error = BRANCH_SIZING_ERROR_TOO_LARGE;
            }
        }
        if (result.error == BRANCH_SIZING_OK)
        {
            bool changed = vector ? branch_sizing_setup_vector(count, branches, targets, branch_count, offsets, (uint32_t)length, &columns)
                                  : branch_sizing_setup_scalar(count, branches, targets, branch_count, offsets, (uint32_t)length, &columns);
            uint32_t current = 0;
            bool listed = false;
            result.rounds = 1;
            if (changed)
            {
                if (vector)
                {
                    branch_sizing_ranks_vector(count, branches, targets, branch_count, &columns);
                }
                else
                {
                    branch_sizing_ranks_scalar(count, branches, targets, branch_count, &columns);
                }
            }
            while (changed)
            {
                if (result.rounds >= round_limit && !listed)
                {
                    result.evaluations = branch_sizing_worklist(branch_count, &columns, current);
                    if (vector)
                    {
                        branch_sizing_prefix_vector(branch_count, &columns, current ^ 1);
                    }
                    else
                    {
                        branch_sizing_prefix_scalar(branch_count, &columns, current ^ 1);
                    }
                    current ^= 1;
                    listed = true;
                }
                changed = vector ? branch_sizing_round_vector(branch_count, &columns, current) : branch_sizing_round_scalar(branch_count, &columns, current);
                current ^= 1;
                result.rounds += 1;
            }
            if (vector)
            {
                branch_sizing_offsets_vector(count, branches, branch_count, offsets, &columns);
            }
            else
            {
                branch_sizing_offsets_scalar(count, branches, branch_count, offsets, &columns);
            }
            result.length = (uint64_t)length + columns.prefix[current][branch_count];
            result.long_branches = branch_sizing_write(records, branches, branch_count, &columns);
        }
        scratch_end(scratch);
    }
    return result;
}

static BranchSizing size_branches(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                  uint32_t* offsets)
{
    return branch_sizing_run(records, count, branches, targets, branch_count, offsets, true, BUSTER_BRANCH_SIZING_ROUNDS);
}

static BranchSizing size_branches_scalar(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                         uint32_t* offsets)
{
    return branch_sizing_run(records, count, branches, targets, branch_count, offsets, false, BUSTER_BRANCH_SIZING_ROUNDS);
}
