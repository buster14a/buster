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
// fixpoint's (a displacement's magnitude only grows with the growths in its span). Calls and other symbolic operands
// are records marked BUSTER_X86_FORM_FIXUP, which sizing leaves alone; it changes forms and lengths, never indices.
//
// Branch space. The rounds work on the branches alone. With every branch short, branch k (at record r_k, aiming at
// record t_k) has the displacement D0_k = offset(t_k) - offset(r_k) - 2 that x86_measure's offsets give (the first
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
// pushed out of range by the next takes a round per link. Functions shaped like compiled code settle in at most four
// (docs/branch-sizing.md); past BUSTER_BRANCH_SIZING_ROUNDS rounds, the rare cases' path (branch_sizing_worklist) grows
// the rest one branch at a time, recomputing only the displacements of the short branches near each growth: so sizing
// takes time linear in the branches whatever the function's shape.
//
// Entry points: size_branches (the vector path), size_branches_scalar (the scalar path: the same phases, a branch or a
// record at a time, its twin and the tests' oracle; both give identical results, rounds included),
// branch_sizing_validate, and branch_sizing_validate_branches with its twin branch_sizing_validate_branches_scalar.
// Like x86_encode, the two paths are the code generator's, internal functions: they take records and branch columns
// that branch_sizing_validate accepts, which the code generator makes so, and check nothing about them in their loops
// (#182's rule for the encoder: validation belongs to the tests and to debugging). branch_sizing_validate_branches is
// the part of that check a code generator whose records are valid by construction still needs: the branch columns,
// which the two paths read and write by, without x86_validate (the C driver's emission of Tier 0's code, #532). They
// require x86_encoder_initialize and take their memory from the thread's scratch arenas (lane.c). Files that use them
// include this file after x86_64_encoder.c and lane.c; every function in it is static.
//
// Layout, in this order:
//   - API: BranchSizingError, BranchSizing, BranchSizingCheck, size_branches, size_branches_scalar,
//     branch_sizing_validate, branch_sizing_validate_branches and branch_sizing_validate_branches_scalar.
//   - Forms: the branches' rel8 forms (branch_sizing_forms, and as a bitmap branch_sizing_form_words),
//     branch_sizing_growth and the rel8 range.
//   - Columns: BranchSizingColumns and branch_sizing_allocate.
//   - Validation: branch_sizing_check, branch_sizing_validate_branches_scalar, branch_sizing_validate_branches and
//     branch_sizing_validate.
//   - The phases, each a vector function and a scalar one: lengths (x86_measure's offsets: the first round), setup (D0,
//     each branch's growth if it's long, and the first round's growths), ranks (only when a branch grew), round, prefix,
//     the worklist's marks (branch_sizing_mark_vector and branch_sizing_mark_scalar) and offsets; then the shared ones,
//     branch_sizing_worklist (the rare cases) and branch_sizing_write (the records).
//   - The driver: branch_sizing_run.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum BranchSizingError
{
    BRANCH_SIZING_OK,
    // More records than x86_encode takes in one call (BUSTER_X86_ENCODER_MAX_INSTRUCTIONS).
    BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS,
    // The validation's alone (branch_sizing_validate's and branch_sizing_validate_branches'): a branch's record index
    // isn't below the record count, or isn't above the branch before it's (`index` is the branch's).
    BRANCH_SIZING_ERROR_BRANCH_INDEX,
    // The validation's alone: a branch's target is past the record count (`index` is the branch's).
    BRANCH_SIZING_ERROR_TARGET,
    // The validation's alone: a branch's record isn't JMP_REL8 or a Jcc_REL8, without a segment override or the fixup
    // flag (`index` is the branch's).
    BRANCH_SIZING_ERROR_FORM,
    // branch_sizing_validate's alone: x86_validate rejects a record (`index` is the record's).
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
    // How many times the displacements were computed for every branch: 1 when every branch fits with all of them short.
    uint32_t rounds;
    // The branches that ended long (rel32).
    uint64_t long_branches;
    // The displacements branch_sizing_worklist computed, one at a time (0 unless the rounds reached
    // BUSTER_BRANCH_SIZING_ROUNDS).
    uint64_t evaluations;
};

typedef struct BranchSizingCheck BranchSizingCheck;
struct BranchSizingCheck
{
    BranchSizingError error;
    // The branch or the record the error is about.
    uint64_t index;
};

// Sizes the branches of a function of `count` records: `branches` are the branch records' indices, ascending, and
// `targets` each branch's target record (`count` for the function's end), `branch_count` of each. On success, writes
// each branch's form (its rel8 one or the rel32 one after it) and displacement into its record, and each record's offset
// into `offsets` (`count` of them), and returns the function's length. Every rel8 is in range, so x86_validate passes
// for the branches, and x86_encode gives the bytes. It takes what branch_sizing_validate accepts and checks nothing
// about it: with anything else, what it reads and writes is undefined. It fails only with too many records, a function
// that could pass INT32_MAX bytes, or no scratch memory; then the records are as they were and the offsets' contents
// are unspecified. Requires x86_encoder_initialize.
static __attribute__((unused)) BranchSizing size_branches(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                          uint64_t branch_count, uint32_t* offsets);
// The same, a branch or a record at a time: the vector path's twin and oracle, with the same result.
static __attribute__((unused)) BranchSizing size_branches_scalar(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                                 uint64_t branch_count, uint32_t* offsets);
// Whether size_branches takes the function, for the tests and for debugging: the record count, then each branch in
// order (its record index below the count and above the branch before it's, its target at most the count, and its
// record's form one of the branches' rel8 forms, without a segment override or the fixup flag), then every record
// (x86_validate). Returns the first error, or BRANCH_SIZING_OK. Requires x86_encoder_initialize.
static __attribute__((unused)) BranchSizingCheck branch_sizing_validate(const X86Instruction* records, uint64_t count, const uint32_t* branches,
                                                                        const uint32_t* targets, uint64_t branch_count);
// branch_sizing_validate without x86_validate: the record count, then each branch in order, with the same first error,
// but it takes records x86_validate would reject. For a code generator whose records are valid by construction, which
// still owes size_branches branch columns inside the function: past them, the paths would read and write outside the
// records and their offsets. 16 branches at a time, with the branch records' forms gathered.
static __attribute__((unused)) BranchSizingCheck branch_sizing_validate_branches(const X86Instruction* records, uint64_t count, const uint32_t* branches,
                                                                                 const uint32_t* targets, uint64_t branch_count);
// The same, a branch at a time: the vector path's twin and oracle, and branch_sizing_validate's first part.
static __attribute__((unused)) BranchSizingCheck branch_sizing_validate_branches_scalar(const X86Instruction* records, uint64_t count,
                                                                                        const uint32_t* branches, const uint32_t* targets,
                                                                                        uint64_t branch_count);

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
// Rounds before the rare cases' path takes over (see the file's comment): the fewest that the benchmark's functions
// shaped like compiled code never reach (they take at most 4), since every round before the worklist costs a chain a
// pass over its branches. On the 9700X, 2 and 3 cost those functions 45% and 5% more in the rounds, and each round past 4
// costs a chain of 1,000 links 2.5% more.
#define BUSTER_BRANCH_SIZING_ROUNDS 4
// The ranks' table has an entry per 32 records: their branch bits, and the branches before them in the high half.
#define BUSTER_BRANCH_SIZING_RANK_RECORDS 32
// The vector path's map of branch records has a byte per record, read 64 at a time.
#define BUSTER_BRANCH_SIZING_MAP_RECORDS 64

// The branches' rel8 forms, the jmp and the 16 jcc, whose rel32 forms follow them in the form table.
static const uint16_t branch_sizing_forms[] = {
    X86_FORM_JMP_REL8,
    X86_FORM_JO_REL8,
    X86_FORM_JNO_REL8,
    X86_FORM_JB_REL8,
    X86_FORM_JAE_REL8,
    X86_FORM_JE_REL8,
    X86_FORM_JNE_REL8,
    X86_FORM_JBE_REL8,
    X86_FORM_JA_REL8,
    X86_FORM_JS_REL8,
    X86_FORM_JNS_REL8,
    X86_FORM_JP_REL8,
    X86_FORM_JNP_REL8,
    X86_FORM_JL_REL8,
    X86_FORM_JGE_REL8,
    X86_FORM_JLE_REL8,
    X86_FORM_JG_REL8,
};
_Static_assert(X86_FORM_JMP_REL32 == X86_FORM_JMP_REL8 + 1, "a branch's rel32 form follows its rel8 one");
#define BUSTER_BRANCH_SIZING_FOLLOWS(CC) _Static_assert(X86_FORM_J##CC##_REL32 == X86_FORM_J##CC##_REL8 + 1, "a branch's rel32 form follows its rel8 one");
BUSTER_BRANCH_SIZING_FOLLOWS(O)
BUSTER_BRANCH_SIZING_FOLLOWS(NO)
BUSTER_BRANCH_SIZING_FOLLOWS(B)
BUSTER_BRANCH_SIZING_FOLLOWS(AE)
BUSTER_BRANCH_SIZING_FOLLOWS(E)
BUSTER_BRANCH_SIZING_FOLLOWS(NE)
BUSTER_BRANCH_SIZING_FOLLOWS(BE)
BUSTER_BRANCH_SIZING_FOLLOWS(A)
BUSTER_BRANCH_SIZING_FOLLOWS(S)
BUSTER_BRANCH_SIZING_FOLLOWS(NS)
BUSTER_BRANCH_SIZING_FOLLOWS(P)
BUSTER_BRANCH_SIZING_FOLLOWS(NP)
BUSTER_BRANCH_SIZING_FOLLOWS(L)
BUSTER_BRANCH_SIZING_FOLLOWS(GE)
BUSTER_BRANCH_SIZING_FOLLOWS(LE)
BUSTER_BRANCH_SIZING_FOLLOWS(G)
#undef BUSTER_BRANCH_SIZING_FOLLOWS

// The same forms as a bitmap of the BUSTER_BRANCH_SIZING_FORM_WINDOW form numbers from JMP_REL8's, a dword per 32 of
// them, in which branch_sizing_validate_branches looks a form up with vpermd: bit f - JMP_REL8 is set for each form f
// of branch_sizing_forms (which test-branch-sizing compares form by form).
#define BUSTER_BRANCH_SIZING_FORM_WINDOW 512
#define BUSTER_BRANCH_SIZING_FORM_BIT(name, word) \
    ((X86_FORM_##name##_REL8 - X86_FORM_JMP_REL8) / 32 == (word) ? 1u << (X86_FORM_##name##_REL8 - X86_FORM_JMP_REL8) % 32 : 0u)
#define BUSTER_BRANCH_SIZING_FORM_WORD(word)                                                                                                         \
    (BUSTER_BRANCH_SIZING_FORM_BIT(JMP, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JO, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JNO, word) |                 \
     BUSTER_BRANCH_SIZING_FORM_BIT(JB, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JAE, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JE, word) |                  \
     BUSTER_BRANCH_SIZING_FORM_BIT(JNE, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JBE, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JA, word) |                 \
     BUSTER_BRANCH_SIZING_FORM_BIT(JS, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JNS, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JP, word) |                  \
     BUSTER_BRANCH_SIZING_FORM_BIT(JNP, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JL, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JGE, word) |                 \
     BUSTER_BRANCH_SIZING_FORM_BIT(JLE, word) | BUSTER_BRANCH_SIZING_FORM_BIT(JG, word))
// Each jcc's rel8 form is in the bitmap's window, past JMP_REL8 (whose bit is the first).
#define BUSTER_BRANCH_SIZING_WINDOWED(CC) _Static_assert((unsigned)(X86_FORM_J##CC##_REL8 - X86_FORM_JMP_REL8) < BUSTER_BRANCH_SIZING_FORM_WINDOW, "a jcc's rel8 form is in the bitmap");
static const uint32_t branch_sizing_form_words[BUSTER_BRANCH_SIZING_FORM_WINDOW / 32] __attribute__((aligned(64))) = {
    BUSTER_BRANCH_SIZING_FORM_WORD(0),
    BUSTER_BRANCH_SIZING_FORM_WORD(1),
    BUSTER_BRANCH_SIZING_FORM_WORD(2),
    BUSTER_BRANCH_SIZING_FORM_WORD(3),
    BUSTER_BRANCH_SIZING_FORM_WORD(4),
    BUSTER_BRANCH_SIZING_FORM_WORD(5),
    BUSTER_BRANCH_SIZING_FORM_WORD(6),
    BUSTER_BRANCH_SIZING_FORM_WORD(7),
    BUSTER_BRANCH_SIZING_FORM_WORD(8),
    BUSTER_BRANCH_SIZING_FORM_WORD(9),
    BUSTER_BRANCH_SIZING_FORM_WORD(10),
    BUSTER_BRANCH_SIZING_FORM_WORD(11),
    BUSTER_BRANCH_SIZING_FORM_WORD(12),
    BUSTER_BRANCH_SIZING_FORM_WORD(13),
    BUSTER_BRANCH_SIZING_FORM_WORD(14),
    BUSTER_BRANCH_SIZING_FORM_WORD(15),
};
BUSTER_BRANCH_SIZING_WINDOWED(O)
BUSTER_BRANCH_SIZING_WINDOWED(NO)
BUSTER_BRANCH_SIZING_WINDOWED(B)
BUSTER_BRANCH_SIZING_WINDOWED(AE)
BUSTER_BRANCH_SIZING_WINDOWED(E)
BUSTER_BRANCH_SIZING_WINDOWED(NE)
BUSTER_BRANCH_SIZING_WINDOWED(BE)
BUSTER_BRANCH_SIZING_WINDOWED(A)
BUSTER_BRANCH_SIZING_WINDOWED(S)
BUSTER_BRANCH_SIZING_WINDOWED(NS)
BUSTER_BRANCH_SIZING_WINDOWED(P)
BUSTER_BRANCH_SIZING_WINDOWED(NP)
BUSTER_BRANCH_SIZING_WINDOWED(L)
BUSTER_BRANCH_SIZING_WINDOWED(GE)
BUSTER_BRANCH_SIZING_WINDOWED(LE)
BUSTER_BRANCH_SIZING_WINDOWED(G)
#undef BUSTER_BRANCH_SIZING_WINDOWED
#undef BUSTER_BRANCH_SIZING_FORM_WORD
#undef BUSTER_BRANCH_SIZING_FORM_BIT
_Static_assert(__builtin_offsetof(X86Instruction, form) == 12, "the vector path gathers a record's form as its fourth dword");
_Static_assert(BUSTER_X86_ENCODER_MAX_INSTRUCTIONS < (1u << 30), "a record's index, doubled for the form's gather, fits in a signed dword");

// How many bytes a branch of the form grows by from rel8 to rel32: BUSTER_BRANCH_SIZING_JMP_GROWTH for JMP_REL8 and
// BUSTER_BRANCH_SIZING_JCC_GROWTH for a Jcc_REL8, the only forms a branch has.
static uint32_t branch_sizing_growth(uint32_t form)
{
    return form == X86_FORM_JMP_REL8 ? BUSTER_BRANCH_SIZING_JMP_GROWTH : BUSTER_BRANCH_SIZING_JCC_GROWTH;
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
    // The vector path's map: a byte per record, up to a whole BUSTER_BRANCH_SIZING_MAP_RECORDS past the count, 1 for a
    // branch's.
    uint8_t* map;
    // An entry per BUSTER_BRANCH_SIZING_RANK_RECORDS records, up to the count's: their branch bits in the low half, and
    // the number of branches before them in the high half.
    uint64_t* ranks;
    // Per branch: its growth if it's long (BUSTER_BRANCH_SIZING_JMP_GROWTH or BUSTER_BRANCH_SIZING_JCC_GROWTH), its
    // displacement with every branch short (D0), its target's rank, its growth now (0 while it's short) and its
    // displacement from the last round. The vector path keeps every one of them zero past the branches, to the end of
    // the last vector.
    uint32_t* grow;
    int32_t* displacement0;
    uint32_t* rank;
    uint32_t* growth;
    int32_t* displacement;
    // The exclusive prefix sums of the growths (prefix[i][k] is the growth of the branches before k), of this round's
    // growths and the last round's: rounds read one and write the other.
    uint32_t* prefix[2];
    // branch_sizing_worklist's dirty bits, one per branch, and a word more (branch_sizing_mark_vector's spill).
    uint64_t* dirty;
    bool valid;
};

// The bytes of `count` elements of `bytes` each, rounded up to whole cache lines.
static uint64_t branch_sizing_lines(uint64_t count, uint64_t bytes)
{
    return (count * bytes + 63) & ~(uint64_t)63;
}

// The columns of a function of `count` records and `branch_count` branches, with the vector path's map if `vector`:
// one push, cut into cache-line-aligned columns (a push per column cost small functions tens of cycles).
static BranchSizingColumns branch_sizing_allocate(Arena* arena, uint64_t count, uint64_t branch_count, bool vector)
{
    (void)vector;

    const uint64_t padded = ((branch_count + BUSTER_BRANCH_SIZING_LANES - 1) & ~(uint64_t)(BUSTER_BRANCH_SIZING_LANES - 1)) + BUSTER_BRANCH_SIZING_LANES;
    const uint64_t units = count / BUSTER_BRANCH_SIZING_MAP_RECORDS + 1;
    const uint64_t map_bytes = BUSTER_VECTOR_PATH(vector) ? branch_sizing_lines(units * BUSTER_BRANCH_SIZING_MAP_RECORDS, 1) : 0;
    const uint64_t ranks_bytes = branch_sizing_lines(units * (BUSTER_BRANCH_SIZING_MAP_RECORDS / BUSTER_BRANCH_SIZING_RANK_RECORDS), sizeof(uint64_t));
    const uint64_t column_bytes = branch_sizing_lines(padded, sizeof(uint32_t));
    const uint64_t prefix_bytes = branch_sizing_lines(padded + BUSTER_BRANCH_SIZING_LANES, sizeof(uint32_t));
    const uint64_t dirty_bytes = branch_sizing_lines(branch_count / 64 + 2, sizeof(uint64_t));
    uint8_t* base = arena_push(arena, map_bytes + ranks_bytes + 5 * column_bytes + 2 * prefix_bytes + dirty_bytes, 64);
    BranchSizingColumns result = {
        .valid = base != 0,
    };
    if (base != 0)
    {
        result.map = BUSTER_VECTOR_PATH(vector) ? base : 0;
        uint8_t* columns = base + map_bytes;
        result.ranks = (uint64_t*)columns;
        columns += ranks_bytes;
        result.grow = (uint32_t*)columns;
        result.displacement0 = (int32_t*)(columns + column_bytes);
        result.rank = (uint32_t*)(columns + 2 * column_bytes);
        result.growth = (uint32_t*)(columns + 3 * column_bytes);
        result.displacement = (int32_t*)(columns + 4 * column_bytes);
        columns += 5 * column_bytes;
        result.prefix[0] = (uint32_t*)columns;
        result.prefix[1] = (uint32_t*)(columns + prefix_bytes);
        result.dirty = (uint64_t*)(columns + 2 * prefix_bytes);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Validation: the tests' and debugging's, which the two paths leave to the code generator; and its branch columns'
// part, which a code generator of valid records runs.

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
    else
    {
        bool branch = false;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(branch_sizing_forms); i += 1)
        {
            branch = branch || records[record].form == branch_sizing_forms[i];
        }
        result = branch ? BRANCH_SIZING_OK : BRANCH_SIZING_ERROR_FORM;
    }
    return result;
}

static BranchSizingCheck branch_sizing_validate_branches_scalar(const X86Instruction* records, uint64_t count, const uint32_t* branches,
                                                                const uint32_t* targets, uint64_t branch_count)
{
    BranchSizingCheck result = {
        .error = count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS ? BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS : BRANCH_SIZING_OK,
    };
    for (uint64_t k = 0; result.error == BRANCH_SIZING_OK && k < branch_count; k += 1)
    {
        result.error = branch_sizing_check(records, count, branches, targets, k);
        result.index = k;
    }
    result.index = result.error != BRANCH_SIZING_OK ? result.index : 0;
    return result;
}

// 16 branches at a time: each lane's errors as masks, and the first lane with one, its error the first of the scalar
// check's order (the index's, the target's, the form's). The forms are gathered only for records below the count, and
// looked up in branch_sizing_form_words.
static BranchSizingCheck branch_sizing_validate_branches(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                         uint64_t branch_count)
{
#if !BUSTER_HOST_VECTOR
    return branch_sizing_validate_branches_scalar(records, count, branches, targets, branch_count);
#else
    BranchSizingCheck result = {
        .error = count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS ? BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS : BRANCH_SIZING_OK,
    };
    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i count_vector = _mm512_set1_epi32((int32_t)count);
    const __m512i form_bits = _mm512_set1_epi32(0xffff);
    const __m512i first_form = _mm512_set1_epi32(X86_FORM_JMP_REL8);
    const __m512i window = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_FORM_WINDOW);
    const __m512i form_words = _mm512_load_si512(branch_sizing_form_words);
    const uint8_t* forms = (const uint8_t*)records + __builtin_offsetof(X86Instruction, form);
    // The vector before's branch records: its last lane is the branch before this vector's first.
    __m512i previous = zero;
    for (uint64_t k = 0; result.error == BRANCH_SIZING_OK && k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(k, branch_count);
        const __m512i record = _mm512_maskz_loadu_epi32(lanes, branches + k);
        const __m512i target = _mm512_maskz_loadu_epi32(lanes, targets + k);
        // Each branch's record must be above the one before it, which the function's first branch hasn't.
        const __m512i before = _mm512_alignr_epi32(record, previous, BUSTER_BRANCH_SIZING_LANES - 1);
        const __mmask16 ordered = k != 0 ? lanes : (__mmask16)(lanes & ~1u);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, record, count_vector);
        const __mmask16 index_errors = (__mmask16)((lanes & ~inside) | _mm512_mask_cmple_epu32_mask(ordered, record, before));
        const __mmask16 target_errors = _mm512_mask_cmpgt_epu32_mask(lanes, target, count_vector);
        // A record's form is its fourth dword's low half (record * 16 bytes: twice the index, scaled by 8), a branch's
        // when it's in the bitmap's window and its bit is set: vpermd takes the window's dword (the low four bits of
        // the form's offset over 32), and vprorvd rotates the form's bit (its offset's low five bits) to bit 0.
        const __m512i form = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, inside, _mm512_slli_epi32(record, 1), forms, 8), form_bits);
        const __m512i offset = _mm512_sub_epi32(form, first_form);
        const __mmask16 windowed = _mm512_mask_cmplt_epu32_mask(inside, offset, window);
        const __m512i word = _mm512_permutexvar_epi32(_mm512_srli_epi32(offset, 5), form_words);
        const __mmask16 branch = _mm512_mask_test_epi32_mask(windowed, _mm512_rorv_epi32(word, offset), one);
        const __mmask16 form_errors = (__mmask16)(inside & ~branch);
        // The first lane with an error (16 without one, whose bit no mask has).
        const uint32_t lane = (uint32_t)__builtin_ctz((uint32_t)(index_errors | target_errors | form_errors) | (1u << BUSTER_BRANCH_SIZING_LANES));
        const uint32_t bit = 1u << lane;
        result.error = (index_errors & bit) != 0  ? BRANCH_SIZING_ERROR_BRANCH_INDEX :
                       (target_errors & bit) != 0 ? BRANCH_SIZING_ERROR_TARGET :
                       (form_errors & bit) != 0   ? BRANCH_SIZING_ERROR_FORM :
                                                    BRANCH_SIZING_OK;
        result.index = k + lane;
        previous = record;
    }
    result.index = result.error != BRANCH_SIZING_OK ? result.index : 0;
    return result;
#endif
}

static BranchSizingCheck branch_sizing_validate(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets,
                                                uint64_t branch_count)
{
    BranchSizingCheck result = branch_sizing_validate_branches_scalar(records, count, branches, targets, branch_count);
    if (result.error == BRANCH_SIZING_OK)
    {
        result.index = x86_validate(records, count);
        result.error = result.index != count ? BRANCH_SIZING_ERROR_INVALID_INSTRUCTION : BRANCH_SIZING_OK;
        result.index = result.error != BRANCH_SIZING_OK ? result.index : 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lengths: the first round's offsets, directly from the encoder's length rules, without assembling discarded bytes
// or rebasing chunks. The driver has already checked the count; records and offsets are disjoint bounded slices.
// The scalar twin derives its lengths independently by encoding one record at a time into a local buffer.

static uint64_t branch_sizing_lengths(const X86Instruction* records, uint64_t count, uint32_t* offsets, bool vector)
{
    (void)vector;

    return (uint64_t)(BUSTER_VECTOR_PATH(vector) ? x86_measure(records, count, offsets) : x86_measure_scalar(records, count, offsets));
}

// Whether a function of `length` bytes with every branch short could pass INT32_MAX bytes with every branch long.
// Below it, every offset, displacement and prefix sum fits in a signed dword.
static bool branch_sizing_too_large(uint64_t length, uint64_t branch_count)
{
    return length + BUSTER_BRANCH_SIZING_MAX_GROWTH * branch_count > INT32_MAX;
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup: each branch's growth if it's long, from its record's form, and its displacement with every branch short,
// D0 = offset(t) - offset(r) - 2 (the function's length for a target at its end), which is the first round's: the
// branches out of range grow, the prefix sums are theirs, and the prefix sums of no growth, the round before's, are
// zero. Returns whether any branch grew.

static bool branch_sizing_setup_scalar(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                       const uint32_t* offsets, uint32_t length, BranchSizingColumns* columns)
{
    uint32_t* prefix = columns->prefix[0];
    uint32_t sum = 0;
    prefix[0] = 0;
    columns->prefix[1][0] = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        const uint32_t record = branches[k];
        const uint32_t target = targets[k];
        const uint32_t end = target < count ? offsets[target] : length;
        const int32_t displacement = (int32_t)(end - offsets[record] - BUSTER_BRANCH_SIZING_SHORT_BYTES);
        const uint32_t grow = branch_sizing_growth(records[record].form);
        const uint32_t growth = branch_sizing_out_of_range(displacement) ? grow : 0;
        columns->grow[k] = grow;
        columns->displacement0[k] = displacement;
        columns->displacement[k] = displacement;
        columns->growth[k] = growth;
        sum += growth;
        prefix[k + 1] = sum;
        columns->prefix[1][k + 1] = 0;
    }
    return sum != 0;
}

// The same, 16 branches at a time, with the records' and the targets' offsets gathered, and the records' forms
// (as their fourth dwords).
static bool branch_sizing_setup_vector(const X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                       const uint32_t* offsets, uint32_t length, BranchSizingColumns* columns)
{
#if !BUSTER_HOST_VECTOR
    return branch_sizing_setup_scalar(records, count, branches, targets, branch_count, offsets, length, columns);
#else
    const __m512i zero = _mm512_setzero_si512();
    const __m512i count_vector = _mm512_set1_epi32((int32_t)count);
    const __m512i length_vector = _mm512_set1_epi32((int32_t)length);
    const __m512i short_bytes = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_SHORT_BYTES);
    const __m512i form_bits = _mm512_set1_epi32(0xffff);
    const __m512i jump_form = _mm512_set1_epi32(X86_FORM_JMP_REL8);
    const __m512i jump_growth = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JMP_GROWTH);
    const __m512i condition_growth = _mm512_set1_epi32(BUSTER_BRANCH_SIZING_JCC_GROWTH);
    const uint8_t* forms = (const uint8_t*)records + __builtin_offsetof(X86Instruction, form);
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
        const __m512i form = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, lanes, _mm512_slli_epi32(record, 1), forms, 8), form_bits);
        const __mmask16 jump = _mm512_cmpeq_epi32_mask(form, jump_form);
        const __m512i grow = _mm512_maskz_mov_epi32(lanes, _mm512_mask_blend_epi32(jump, condition_growth, jump_growth));
        const __m512i displacement = _mm512_maskz_sub_epi32(lanes, end, _mm512_add_epi32(start, short_bytes));
        // Lanes past the branches have no growth to take (their grow is 0).
        const __m512i growth = _mm512_maskz_mov_epi32(branch_sizing_out_of_range_vector(displacement), grow);
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(growth), carry);
        _mm512_store_si512(columns->grow + k, grow);
        _mm512_store_si512(columns->displacement0 + k, displacement);
        _mm512_store_si512(columns->displacement + k, displacement);
        _mm512_store_si512(columns->growth + k, growth);
        _mm512_storeu_si512(prefix + k + 1, sums);
        _mm512_storeu_si512(columns->prefix[1] + k + 1, zero);
        carry = branch_sizing_last(sums);
        grown |= _mm512_test_epi32_mask(growth, growth);
    }
    return grown != 0;
#endif
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
    // The branches are ascending, so an entry's bits gather in a register, stored whole each time rather than ORed into
    // memory, which would make each branch wait for the store before.
    uint64_t entry = 0;
    uint64_t bits = 0;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        const uint64_t at = branches[k] / BUSTER_BRANCH_SIZING_RANK_RECORDS;
        bits = (at == entry ? bits : 0) | 1ull << (branches[k] % BUSTER_BRANCH_SIZING_RANK_RECORDS);
        entry = at;
        ranks[at] = bits;
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
#if !BUSTER_HOST_VECTOR
    branch_sizing_ranks_scalar(count, branches, targets, branch_count, columns);
#else
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
#endif
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
        // A select rather than a branch on the long branches, which are 20% to 40% of compiled code's.
        const uint32_t grows = (uint32_t)(old != 0) | (uint32_t)branch_sizing_out_of_range(displacement);
        const uint32_t growth = grows != 0 ? columns->grow[k] : 0;
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
#if !BUSTER_HOST_VECTOR
    return branch_sizing_round_scalar(branch_count, columns, current);
#else
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
#endif
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
#if !BUSTER_HOST_VECTOR
    branch_sizing_prefix_scalar(branch_count, columns, into);
#else
    uint32_t* prefix = columns->prefix[into];
    __m512i carry = _mm512_setzero_si512();
    prefix[0] = 0;
    for (uint64_t k = 0; k < branch_count; k += BUSTER_BRANCH_SIZING_LANES)
    {
        const __m512i sums = _mm512_add_epi32(branch_sizing_scan(_mm512_load_si512(columns->growth + k)), carry);
        _mm512_storeu_si512(prefix + k + 1, sums);
        carry = branch_sizing_last(sums);
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// The rare cases: branch_sizing_worklist, after a round that grew some branches, grows the branches that are still out
// of range one at a time, until none is. A short branch's displacement changes only when a branch in its span grows, so
// the dirty bits mark the short branches whose spans hold a branch that grew: at first those of the round's growths,
// and then those of each growth here. A short branch's span holds at most BUSTER_BRANCH_SIZING_REACH branches (see the
// file's comment), so the marks are among the branches within that many of a growth, and a displacement is a sum of at
// most that many growths and one: each growth here costs a bounded amount of work. It takes the dirty branches lowest
// first, though any order would do. Returns how many displacements it computed.

// Marks the short branches whose spans hold branch k, and returns the word of the lowest one it could have marked. The
// marks gather in a register and go to each word once: an OR into memory per branch made each wait for the one before
// (a store and its reload).
static uint64_t branch_sizing_mark_scalar(uint64_t branch_count, uint64_t k, const uint32_t* rank, const uint32_t* growth, uint64_t* dirty)
{
    const uint64_t first = k >= BUSTER_BRANCH_SIZING_REACH ? k - BUSTER_BRANCH_SIZING_REACH : 0;
    const uint64_t end = branch_count - k > BUSTER_BRANCH_SIZING_REACH ? k + BUSTER_BRANCH_SIZING_REACH + 1 : branch_count;
    uint64_t marks = 0;
    for (uint64_t i = first; i < end; i += 1)
    {
        // A forward branch's span holds the branches after it and before its target; a backward one's, the branches from
        // its target's on, up to itself.
        const bool holds = (i < k && rank[i] > k) || (i > k && rank[i] <= k);
        marks |= (uint64_t)(holds && growth[i] == 0) << (i % 64);
        if (i % 64 == 63 || i + 1 == end)
        {
            dirty[i / 64] |= marks;
            marks = 0;
        }
    }
    return first / 64;
}

// The same, 16 branches at a time: their indices and ranks against k, and their growths against zero, make a mask of 16
// marks, which goes to the dirty word it starts in and the next (which branch_sizing_allocate has room for).
static uint64_t branch_sizing_mark_vector(uint64_t branch_count, uint64_t k, const uint32_t* rank, const uint32_t* growth, uint64_t* dirty)
{
#if !BUSTER_HOST_VECTOR
    return branch_sizing_mark_scalar(branch_count, k, rank, growth, dirty);
#else
    const uint64_t first = k >= BUSTER_BRANCH_SIZING_REACH ? k - BUSTER_BRANCH_SIZING_REACH : 0;
    const uint64_t end = branch_count - k > BUSTER_BRANCH_SIZING_REACH ? k + BUSTER_BRANCH_SIZING_REACH + 1 : branch_count;
    const __m512i key = _mm512_set1_epi32((int32_t)k);
    const __m512i lane = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t i = first; i < end; i += BUSTER_BRANCH_SIZING_LANES)
    {
        const __mmask16 lanes = branch_sizing_lanes(i, end);
        const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int32_t)i), lane);
        const __m512i ranks = _mm512_maskz_loadu_epi32(lanes, rank + i);
        const __m512i growths = _mm512_maskz_loadu_epi32(lanes, growth + i);
        const __mmask16 forward = _mm512_cmplt_epu32_mask(index, key) & _mm512_cmpgt_epu32_mask(ranks, key);
        const __mmask16 backward = _mm512_cmpgt_epu32_mask(index, key) & _mm512_cmple_epu32_mask(ranks, key);
        const unsigned __int128 marks = (unsigned __int128)(uint16_t)((forward | backward) & _mm512_testn_epi32_mask(growths, growths) & lanes) << (i % 64);
        dirty[i / 64] |= (uint64_t)marks;
        dirty[i / 64 + 1] |= (uint64_t)(marks >> 64);
    }
    return first / 64;
#endif
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
// The vector path marks 16 branches at a time, the scalar path one at a time, both the same branches. It stays out of
// line: inlined into branch_sizing_run, its code changed how clang allocated the hot phases' registers, and functions
// that never reach it were sized up to 20% slower on the 9700X.
__attribute__((noinline)) static uint64_t branch_sizing_worklist(uint64_t branch_count, BranchSizingColumns* columns, uint32_t current, bool vector)
{
    (void)vector;

    const uint32_t* now = columns->prefix[current];
    const uint32_t* before = columns->prefix[current ^ 1];
    const uint64_t words = (branch_count + 63) / 64;
    uint64_t* dirty = columns->dirty;
    uint64_t result = 0;
    memset(dirty, 0, (words + 1) * sizeof(uint64_t));
    uint64_t word = words;
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        // The branches the last round grew.
        if (now[k + 1] - now[k] != before[k + 1] - before[k])
        {
            const uint64_t lowest = BUSTER_VECTOR_PATH(vector) ? branch_sizing_mark_vector(branch_count, k, columns->rank, columns->growth, dirty)
                                           : branch_sizing_mark_scalar(branch_count, k, columns->rank, columns->growth, dirty);
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
                const uint64_t lowest = BUSTER_VECTOR_PATH(vector) ? branch_sizing_mark_vector(branch_count, j, columns->rank, columns->growth, dirty)
                                               : branch_sizing_mark_scalar(branch_count, j, columns->rank, columns->growth, dirty);
                word = lowest < word ? lowest : word;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Offsets: each record's offset grows by the growth of the branches before it, the final prefix sum at the record's
// rank (`current` is the buffer with the final growths' prefix sums). The records up to the first long branch's keep
// theirs, so the pass starts at the entry of the ranks' table that holds it. Each record's rank comes from the table, so
// no step waits for the one before.

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
#if !BUSTER_HOST_VECTOR
    return branch_sizing_first_long_scalar(branch_count, growth);
#else
    uint64_t k = 0;
    __mmask16 grown = 0;
    while (k < branch_count && grown == 0)
    {
        const __m512i block = _mm512_load_si512(growth + k);
        grown = _mm512_test_epi32_mask(block, block);
        k += grown == 0 ? BUSTER_BRANCH_SIZING_LANES : (uint64_t)__builtin_ctz(grown);
    }
    return k < branch_count ? k : branch_count;
#endif
}

static void branch_sizing_offsets_scalar(uint64_t count, const uint32_t* branches, uint64_t branch_count, uint32_t* offsets, const BranchSizingColumns* columns,
                                         uint32_t current)
{
    const uint32_t* prefix = columns->prefix[current];
    const uint64_t first = branch_sizing_first_long_scalar(branch_count, columns->growth);
    if (first < branch_count)
    {
        const uint64_t start = branches[first] / BUSTER_BRANCH_SIZING_RANK_RECORDS * BUSTER_BRANCH_SIZING_RANK_RECORDS;
        for (uint64_t i = start; i < count; i += 1)
        {
            offsets[i] += prefix[branch_sizing_rank(columns->ranks, (uint32_t)i)];
        }
    }
}

// The same, 16 records at a time: record i + j's rank is the branches before record i (k, from the ranks' table) and the
// branch bits below j among the 16 (a vpopcntd of the bits masked per lane), at most 15, so a vpermd looks its prefix
// sum up among the 16 from k.
static void branch_sizing_offsets_vector(uint64_t count, const uint32_t* branches, uint64_t branch_count, uint32_t* offsets, const BranchSizingColumns* columns,
                                         uint32_t current)
{
#if !BUSTER_HOST_VECTOR
    branch_sizing_offsets_scalar(count, branches, branch_count, offsets, columns, current);
#else
    const uint32_t* prefix = columns->prefix[current];
    const __m512i below = _mm512_setr_epi32(0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff);
    const uint64_t first = branch_sizing_first_long_vector(branch_count, columns->growth);
    if (first < branch_count)
    {
        const uint64_t start = branches[first] / BUSTER_BRANCH_SIZING_RANK_RECORDS * BUSTER_BRANCH_SIZING_RANK_RECORDS;
        for (uint64_t i = start; i < count; i += BUSTER_BRANCH_SIZING_LANES)
        {
            const __mmask16 lanes = branch_sizing_lanes(i, count);
            const uint64_t entry = columns->ranks[i / BUSTER_BRANCH_SIZING_RANK_RECORDS];
            const uint32_t shift = (uint32_t)(i % BUSTER_BRANCH_SIZING_RANK_RECORDS);
            const uint32_t marks = (uint32_t)(entry >> shift) & 0xffff;
            const uint64_t k = (entry >> 32) + (uint64_t)__builtin_popcount((uint32_t)entry & ((1u << shift) - 1));
            const __m512i ranks = _mm512_popcnt_epi32(_mm512_and_si512(_mm512_set1_epi32((int32_t)marks), below));
            const __m512i growth = _mm512_permutexvar_epi32(ranks, _mm512_loadu_si512(prefix + k));
            const __m512i offset = _mm512_maskz_loadu_epi32(lanes, offsets + i);
            _mm512_mask_storeu_epi32(offsets + i, lanes, _mm512_add_epi32(offset, growth));
        }
    }
#endif
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
    (void)vector;

    BranchSizing result = {
        .error = BRANCH_SIZING_OK,
    };
    if (count > BUSTER_X86_ENCODER_MAX_INSTRUCTIONS)
    {
        result.error = BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS;
    }
    else
    {
        Scratch scratch = scratch_begin(0);
        BranchSizingColumns columns = branch_sizing_allocate(scratch.arena, count, branch_count, BUSTER_VECTOR_PATH(vector));
        uint64_t length = 0;
        if (!columns.valid)
        {
            result.error = BRANCH_SIZING_ERROR_MEMORY;
        }
        else
        {
            length = branch_sizing_lengths(records, count, offsets, BUSTER_VECTOR_PATH(vector));
            result.error = branch_sizing_too_large(length, branch_count) ? BRANCH_SIZING_ERROR_TOO_LARGE : BRANCH_SIZING_OK;
        }
        if (result.error == BRANCH_SIZING_OK)
        {
            bool changed = BUSTER_VECTOR_PATH(vector) ? branch_sizing_setup_vector(records, count, branches, targets, branch_count, offsets, (uint32_t)length, &columns)
                                  : branch_sizing_setup_scalar(records, count, branches, targets, branch_count, offsets, (uint32_t)length, &columns);
            uint32_t current = 0;
            bool listed = false;
            result.rounds = 1;
            if (changed)
            {
                if (BUSTER_VECTOR_PATH(vector))
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
                    result.evaluations = branch_sizing_worklist(branch_count, &columns, current, BUSTER_VECTOR_PATH(vector));
                    if (BUSTER_VECTOR_PATH(vector))
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
                changed = BUSTER_VECTOR_PATH(vector) ? branch_sizing_round_vector(branch_count, &columns, current) : branch_sizing_round_scalar(branch_count, &columns, current);
                current ^= 1;
                result.rounds += 1;
            }
            if (BUSTER_VECTOR_PATH(vector))
            {
                branch_sizing_offsets_vector(count, branches, branch_count, offsets, &columns, current);
            }
            else
            {
                branch_sizing_offsets_scalar(count, branches, branch_count, offsets, &columns, current);
            }
            result.length = length + columns.prefix[current][branch_count];
            result.long_branches = branch_sizing_write(records, branches, branch_count, &columns);
        }
        scratch_end(scratch);
    }
    return result;
}

static BranchSizing size_branches_scalar(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                         uint32_t* offsets);

static BranchSizing size_branches(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                  uint32_t* offsets)
{
#if !BUSTER_HOST_VECTOR
    return size_branches_scalar(records, count, branches, targets, branch_count, offsets);
#else
    return branch_sizing_run(records, count, branches, targets, branch_count, offsets, true, BUSTER_BRANCH_SIZING_ROUNDS);
#endif
}

static BranchSizing size_branches_scalar(X86Instruction* records, uint64_t count, const uint32_t* branches, const uint32_t* targets, uint64_t branch_count,
                                         uint32_t* offsets)
{
    return branch_sizing_run(records, count, branches, targets, branch_count, offsets, false, BUSTER_BRANCH_SIZING_ROUNDS);
}
