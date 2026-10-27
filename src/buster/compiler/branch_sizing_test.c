// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The branch sizing tests, for the test-branch-sizing command:
//
//   build/compiler test-branch-sizing [--seed N] [--count N] [--cross-check] [--keep]
//
// main.c includes this file after branch_sizing.c, branch_sizing_generate.c and x86_64_encoder_test.c, whose random
// instructions and printer it takes, and calls run_branch_sizing_tests for the command. Every function sized must be one
// branch_sizing_validate accepts, as the code generator's are, and so must both paths of the branch columns' check
// (branch_sizing_validate_branches and its twin); every check runs both paths, size_branches and size_branches_scalar,
// which must give identical results: the records, the offsets, the length and the counts (rounds, long branches and the
// worklist's evaluations), or the same error. Where a function is sized successfully, the result
// must also be the reference's, branch_sizing_test_reference (relaxation over the records, as an assembler does it,
// with each record's length from x86_encode_scalar), and keep the properties branch_sizing_test_properties checks:
// x86_validate passes, x86_encode_scalar's offsets and length are the result's, every branch's target worked out from
// the final offsets is its label's record, a branch is long exactly when its displacement is out of rel8's range, and
// every other record and field is as it was. The checks, in this order:
//   - Forms: every form number, with and without a segment override or the fixup flag, as a branch's record:
//     branch_sizing_validate accepts JMP_REL8 and the 16 Jcc_REL8 (found from the form table's opcodes), which size and
//     grow by 3 and 4 to the rel32 forms after them, and refuses every other with BRANCH_SIZING_ERROR_FORM; and both
//     paths of the branch columns' check do the same (the vector path's bitmap of the forms against the scalar path's
//     list).
//   - Errors, branch_sizing_validate's: a branch index equal to the one before, below it, at the count and past it, a
//     target past the count, a form that isn't a branch's, a branch's form with FS, GS or the fixup flag, at every place
//     among 40 branches (the first error is the one reported), more branches than records, more records than
//     x86_encode takes (which both paths refuse too), a record x86_validate rejects (after a branch's error), and the
//     length past which a function is too large. Both paths of the branch columns' check must give each error but
//     x86_validate's, and take the records x86_validate rejects.
//   - Boundaries: a probe branch, forward and backward, jmp and jcc, at every displacement from 100 to 135 and from -140
//     to -105 with every branch short, over 0 to 3 branches (jmp or jcc) that grow or don't: it must end long exactly
//     when its displacement with theirs is past 127 or below -128.
//   - Random functions (--count N, BUSTER_BRANCH_SIZING_TEST_FUNCTIONS by default): generated ones of up to 100,000
//     records, a branch every 1 to 12 records, near and far targets at uniform or compiler-like distances, filler from
//     branch_sizing_generate.c's patterns or random instructions of every form; also with the worklist from rounds 1, 2
//     and 3 and with no round limit, which must give the same records and offsets.
//   - Columns: random functions of up to BUSTER_BRANCH_SIZING_TEST_COLUMN_RECORDS records broken 0 to 3 times at random
//     (branch_sizing_test_break): both paths of the branch columns' check must give branch_sizing_validate's first error,
//     or take the function where only x86_validate refuses it.
//   - Chains: forward and backward chains (branch_sizing_chain) of 1 to 1,000 links, which take a round per link: the
//     rounds must be the links' plus one up to BUSTER_BRANCH_SIZING_ROUNDS, and past it the worklist must finish.
//   - Page edges: the records, the branch columns and the offsets each ending at a PROT_NONE page, for every count up
//     to 70 records with branches at every density, and every branch count up to 40, checked by both paths of the
//     branch columns' check and sized: a read or write past an end faults, and the fault handler names the case and
//     exits with status 1.
//   - Memory: a function sized with no scratch memory to spare, then 64 bytes more at a time until it succeeds: every
//     failure must be BRANCH_SIZING_ERROR_MEMORY and leave the records and offsets as they were.
//   - With --cross-check, GNU as: random functions, the boundaries' probes and chains printed as GNU as source in a
//     section each, every branch to a label at its target record, which GNU as relaxes as sizing does (every branch
//     short at first, growing those out of range until none is). Each section's bytes must be x86_encode_scalar's of
//     the sized records, and an object that can't be read or a function left uncompared fails the check. The file and
//     the object go in a directory under $TMPDIR (or /tmp), removed unless --keep or a failure.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally, the functions and their outcomes (branch_sizing_test_size), the reference and the
// properties, the checks, GNU as, and run_branch_sizing_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>

// OS-dependent includes
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Each check prints its first failures, this many.
#define BUSTER_BRANCH_SIZING_TEST_REPORTED 3
// Random functions by default, and the most records one has.
#define BUSTER_BRANCH_SIZING_TEST_FUNCTIONS 1500
#define BUSTER_BRANCH_SIZING_TEST_MAX_RECORDS 100000
// The test's own arena: the functions, their outcomes and the reference's buffers.
#define BUSTER_BRANCH_SIZING_TEST_ARENA_BYTES (16ull << 30)
// The tail of filler after a function's labels, which makes a branch to the function's end long.
#define BUSTER_BRANCH_SIZING_TEST_TAIL_BYTES 200
// Random functions and chains in the GNU as cross-check.
#define BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS 300
#define BUSTER_BRANCH_SIZING_TEST_PATH_BYTES 512
#define BUSTER_BRANCH_SIZING_TEST_RUN_SECONDS 120

typedef struct BranchSizingTestTally BranchSizingTestTally;
struct BranchSizingTestTally
{
    const char* check;
    uint64_t checked;
    uint64_t failures;
};

static void branch_sizing_test_expect(BranchSizingTestTally* tally, bool condition, const char* what, uint64_t index)
{
    tally->checked += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_BRANCH_SIZING_TEST_REPORTED)
        {
            printf("  %s: %s (%llu)\n", tally->check, what, (unsigned long long)index);
        }
        tally->failures += 1;
    }
}

static bool branch_sizing_test_report(const BranchSizingTestTally* tally)
{
    printf("%s: %llu checks, %llu failed\n", tally->check, (unsigned long long)tally->checked, (unsigned long long)tally->failures);
    return tally->failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Functions and outcomes

// A function to size: its records and its branch columns.
typedef struct BranchSizingTestFunction BranchSizingTestFunction;
struct BranchSizingTestFunction
{
    X86Instruction* records;
    uint32_t* branches;
    uint32_t* targets;
    uint64_t count;
    uint64_t branch_count;
};

// A function's buffers, for up to `count` records and branches, from the arena.
static BranchSizingTestFunction branch_sizing_test_function(Arena* arena, uint64_t count)
{
    BranchSizingTestFunction result = {
        .records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, count + 1),
        .branches = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1),
        .targets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1),
    };
    return result;
}

// What sizing made of a function: the result, and the records and offsets it wrote.
typedef struct BranchSizingTestOutcome BranchSizingTestOutcome;
struct BranchSizingTestOutcome
{
    BranchSizing result;
    X86Instruction* records;
    uint32_t* offsets;
};

// The paths a check runs: the vector one, the scalar one, and each with another round limit.
typedef enum BranchSizingTestPath
{
    BRANCH_SIZING_TEST_VECTOR,
    BRANCH_SIZING_TEST_SCALAR,
} BranchSizingTestPath;

// Sizes a copy of the function's records on the path, with the round limit, into an outcome from the arena.
static BranchSizingTestOutcome branch_sizing_test_size(Arena* arena, const BranchSizingTestFunction* function, BranchSizingTestPath path, uint32_t round_limit)
{
    BranchSizingTestOutcome result = {
        .records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, function->count + 1),
        .offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, function->count + 1),
    };
    memcpy(result.records, function->records, function->count * sizeof(X86Instruction));
    memset(result.offsets, 0, function->count * sizeof(uint32_t));
    result.result = branch_sizing_run(result.records, function->count, function->branches, function->targets, function->branch_count, result.offsets,
                                      path == BRANCH_SIZING_TEST_VECTOR, round_limit);
    return result;
}

// Whether two outcomes are the same: the result (with the rounds and evaluations if `counts`), and on success the
// records and offsets.
static bool branch_sizing_test_same(const BranchSizingTestOutcome* a, const BranchSizingTestOutcome* b, uint64_t count, bool counts)
{
    bool result = a->result.error == b->result.error && a->result.length == b->result.length && a->result.long_branches == b->result.long_branches;
    result = result && (!counts || (a->result.rounds == b->result.rounds && a->result.evaluations == b->result.evaluations));
    if (result && a->result.error == BRANCH_SIZING_OK)
    {
        result = memcmp(a->records, b->records, count * sizeof(X86Instruction)) == 0 && memcmp(a->offsets, b->offsets, count * sizeof(uint32_t)) == 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The reference and the properties

// The reference: branch relaxation over the records, as an assembler does it. Each record's length is
// x86_encode_scalar's, every branch short; then rounds compute every offset from the lengths and grow every short branch
// whose displacement is out of range (to the form after its rel8 one, re-encoded for its length), until a round grows
// none, which is the smallest fixpoint. Writes the records and offsets as sizing would, and returns the length (or -1
// if x86_encode_scalar rejects the records).
static int64_t branch_sizing_test_reference(Arena* arena, const BranchSizingTestFunction* function, X86Instruction* records, uint32_t* offsets)
{
    const uint64_t count = function->count;
    const uint64_t mark = arena->length;
    uint8_t* bytes = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(count), 64);
    uint32_t* lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1);
    memcpy(records, function->records, count * sizeof(X86Instruction));
    int64_t result = x86_encode_scalar(records, count, bytes, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0);
    for (uint64_t i = 0; result >= 0 && i < count; i += 1)
    {
        lengths[i] = (i + 1 < count ? offsets[i + 1] : (uint32_t)result) - offsets[i];
    }
    bool changed = result >= 0;
    uint32_t length = 0;
    while (changed)
    {
        changed = false;
        length = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            offsets[i] = length;
            length += lengths[i];
        }
        for (uint64_t k = 0; k < function->branch_count; k += 1)
        {
            const uint32_t record = function->branches[k];
            const uint32_t target = function->targets[k];
            const uint32_t from = record + 1 < count ? offsets[record + 1] : length;
            const uint32_t to = target < count ? offsets[target] : length;
            const int32_t displacement = (int32_t)(to - from);
            if (records[record].form == function->records[record].form && (displacement < -128 || displacement > 127))
            {
                records[record].form += 1;
                lengths[record] = (uint32_t)x86_encode_scalar(&records[record], 1, bytes, BUSTER_X86_ENCODER_CAPACITY(1), 0, 0);
                changed = true;
            }
        }
    }
    for (uint64_t k = 0; result >= 0 && k < function->branch_count; k += 1)
    {
        const uint32_t record = function->branches[k];
        const uint32_t target = function->targets[k];
        const uint32_t from = record + 1 < count ? offsets[record + 1] : length;
        const uint32_t to = target < count ? offsets[target] : length;
        records[record].displacement = (int32_t)(to - from);
    }
    result = result >= 0 ? (int64_t)length : result;
    arena_pop_to(arena, mark);
    return result;
}

// Checks a successful outcome's properties: x86_validate passes for the records; x86_encode_scalar's offsets and length
// are the outcome's; every record but the branches is as it was, and every branch's record but its form, the rel8 one
// or the one after it, and its displacement; every branch's end plus its displacement is its target's offset; a branch
// is long exactly when its displacement is out of rel8's range; and the long branches are counted.
static void branch_sizing_test_properties(BranchSizingTestTally* tally, Arena* arena, const BranchSizingTestFunction* function, const BranchSizingTestOutcome* outcome,
                                          uint64_t index)
{
    const uint64_t count = function->count;
    const uint64_t mark = arena->length;
    uint8_t* bytes = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(count), 64);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count + 1);
    branch_sizing_test_expect(tally, outcome->result.error == BRANCH_SIZING_OK, "sized", index);
    if (outcome->result.error == BRANCH_SIZING_OK)
    {
        branch_sizing_test_expect(tally, x86_validate(outcome->records, count) == count, "x86_validate passes", index);
        const int64_t length = x86_encode_scalar(outcome->records, count, bytes, BUSTER_X86_ENCODER_CAPACITY(count), offsets, 0);
        branch_sizing_test_expect(tally, length >= 0 && (uint64_t)length == outcome->result.length, "the length is x86_encode_scalar's", index);
        branch_sizing_test_expect(tally, memcmp(offsets, outcome->offsets, count * sizeof(uint32_t)) == 0, "the offsets are x86_encode_scalar's", index);
        bool same = true;
        uint64_t k = 0;
        for (uint64_t i = 0; i < count; i += 1)
        {
            X86Instruction expected = function->records[i];
            if (k < function->branch_count && function->branches[k] == i)
            {
                expected.form = outcome->records[i].form == expected.form + 1 ? (uint16_t)(expected.form + 1) : expected.form;
                expected.displacement = outcome->records[i].displacement;
                k += 1;
            }
            same = same && memcmp(&expected, &outcome->records[i], sizeof(X86Instruction)) == 0;
        }
        branch_sizing_test_expect(tally, same, "every record and field but the branches' forms and displacements is as it was", index);
        bool aimed = true;
        bool consistent = true;
        uint64_t long_branches = 0;
        for (k = 0; k < function->branch_count; k += 1)
        {
            const uint32_t record = function->branches[k];
            const uint32_t target = function->targets[k];
            const uint64_t from = record + 1 < count ? offsets[record + 1] : (uint64_t)length;
            const uint64_t to = target < count ? offsets[target] : (uint64_t)length;
            const int32_t displacement = outcome->records[record].displacement;
            const bool grown = outcome->records[record].form != function->records[record].form;
            aimed = aimed && (int64_t)from + displacement == (int64_t)to;
            consistent = consistent && grown == (displacement < -128 || displacement > 127);
            long_branches += grown;
        }
        branch_sizing_test_expect(tally, aimed, "every branch's end plus its displacement is its target's offset", index);
        branch_sizing_test_expect(tally, consistent, "a branch is long exactly when its displacement doesn't fit in a rel8", index);
        branch_sizing_test_expect(tally, long_branches == outcome->result.long_branches, "the long branches are counted", index);
    }
    arena_pop_to(arena, mark);
}

// What the branch columns' check (branch_sizing_validate_branches) must give for a function that branch_sizing_validate
// gives `full` for: the same, but for a record x86_validate rejects, which it takes.
static BranchSizingCheck branch_sizing_test_columns_of(BranchSizingCheck full)
{
    const BranchSizingCheck taken = {
        .error = BRANCH_SIZING_OK,
    };
    return full.error == BRANCH_SIZING_ERROR_INVALID_INSTRUCTION ? taken : full;
}

// Both paths of the branch columns' check, branch_sizing_validate_branches and its twin, must give `expected` for the
// function: the error and its index.
static void branch_sizing_test_columns(BranchSizingTestTally* tally, const BranchSizingTestFunction* function, BranchSizingCheck expected, const char* what,
                                       uint64_t index)
{
    const BranchSizingCheck vector = branch_sizing_validate_branches(function->records, function->count, function->branches, function->targets, function->branch_count);
    const BranchSizingCheck scalar = branch_sizing_validate_branches_scalar(function->records, function->count, function->branches, function->targets,
                                                                            function->branch_count);
    const bool same = vector.error == expected.error && vector.index == expected.index && scalar.error == expected.error && scalar.index == expected.index;
    if (!same && tally->failures < BUSTER_BRANCH_SIZING_TEST_REPORTED)
    {
        printf("  %s: %s (%llu): the branch columns' check gives error %u at %llu on the vector path and %u at %llu on the scalar one, not %u at %llu\n",
               tally->check, what, (unsigned long long)index, (unsigned)vector.error, (unsigned long long)vector.index, (unsigned)scalar.error,
               (unsigned long long)scalar.index, (unsigned)expected.error, (unsigned long long)expected.index);
    }
    tally->checked += 1;
    tally->failures += same ? 0 : 1;
}

// Sizes the function on both paths and with the reference: the function must be one branch_sizing_validate and both
// paths of the branch columns' check accept, the paths' outcomes must be the same, and on success the reference's, with
// the properties. Returns the vector path's outcome (in the arena, which the caller pops).
static BranchSizingTestOutcome branch_sizing_test_all(BranchSizingTestTally* tally, Arena* arena, const BranchSizingTestFunction* function, uint64_t index)
{
    const BranchSizingCheck check = branch_sizing_validate(function->records, function->count, function->branches, function->targets, function->branch_count);
    branch_sizing_test_expect(tally, check.error == BRANCH_SIZING_OK, "branch_sizing_validate accepts the function", index);
    branch_sizing_test_columns(tally, function, check, "the branch columns' check accepts the function", index);
    const BranchSizingTestOutcome vector = branch_sizing_test_size(arena, function, BRANCH_SIZING_TEST_VECTOR, BUSTER_BRANCH_SIZING_ROUNDS);
    const uint64_t mark = arena->length;
    const BranchSizingTestOutcome scalar = branch_sizing_test_size(arena, function, BRANCH_SIZING_TEST_SCALAR, BUSTER_BRANCH_SIZING_ROUNDS);
    branch_sizing_test_expect(tally, branch_sizing_test_same(&vector, &scalar, function->count, true), "the vector and scalar paths agree", index);
    if (vector.result.error == BRANCH_SIZING_OK)
    {
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, function->count + 1);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, function->count + 1);
        const int64_t length = branch_sizing_test_reference(arena, function, records, offsets);
        const bool same = length >= 0 && (uint64_t)length == vector.result.length && memcmp(records, vector.records, function->count * sizeof(X86Instruction)) == 0 &&
                          memcmp(offsets, vector.offsets, function->count * sizeof(uint32_t)) == 0;
        branch_sizing_test_expect(tally, same, "the result is the reference's", index);
        branch_sizing_test_properties(tally, arena, function, &vector, index);
    }
    arena_pop_to(arena, mark);
    return vector;
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks

// A function of a branch whose record has the form, aiming past the tail at the function's end, so that it grows.
static void branch_sizing_test_one_branch(BranchSizingTestFunction* function, uint16_t form)
{
    function->records[0] = (X86Instruction){ .form = form };
    function->count = 1 + branch_sizing_filler_of(BUSTER_BRANCH_SIZING_TEST_TAIL_BYTES, function->records + 1);
    function->branches[0] = 0;
    function->targets[0] = (uint32_t)function->count;
    function->branch_count = 1;
}

// Whether a form is a branch's rel8 one, from the form table: jmp's EB or a jcc's 70 to 7F, of one opcode byte with no
// prefix and a rel8 (not loop's E0 to E2, jrcxz's E3, or the BND- or hint-prefixed forms), without a segment override or
// the fixup flag.
static bool branch_sizing_test_is_branch(uint32_t form)
{
    bool result = form < X86_FORM_COUNT;
    if (result)
    {
        const X86FormDescription* description = &x86_form_descriptions[form];
        result = description->relative_bytes == 1 && description->prefixes == 0 && description->opcode_bytes == 1 &&
                 (description->opcode == 0xeb || (description->opcode >= 0x70 && description->opcode <= 0x7f));
    }
    return result;
}

static bool branch_sizing_test_forms(Arena* arena)
{
    BranchSizingTestTally tally = {
        .check = "forms: every form number as a branch's",
    };
    BranchSizingTestFunction function = branch_sizing_test_function(arena, 64);
    uint32_t sized = 0;
    for (uint32_t form = 0; form <= 0xffff; form += 1)
    {
        const uint64_t mark = arena->length;
        branch_sizing_test_one_branch(&function, (uint16_t)form);
        const BranchSizingCheck check = branch_sizing_validate(function.records, function.count, function.branches, function.targets, function.branch_count);
        // The vector path's bitmap of the forms, against the scalar path's list and branch_sizing_validate (whose
        // filler x86_validate passes).
        branch_sizing_test_columns(&tally, &function, check, "the branch columns' check takes the form as branch_sizing_validate does", form);
        if (branch_sizing_test_is_branch(form))
        {
            const bool jump = x86_form_descriptions[form].opcode == 0xeb;
            branch_sizing_test_expect(&tally, check.error == BRANCH_SIZING_OK, "branch_sizing_validate accepts a branch's rel8 form", form);
            const BranchSizingTestOutcome vector = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_VECTOR, BUSTER_BRANCH_SIZING_ROUNDS);
            const BranchSizingTestOutcome scalar = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_SCALAR, BUSTER_BRANCH_SIZING_ROUNDS);
            branch_sizing_test_expect(&tally, branch_sizing_test_same(&vector, &scalar, function.count, true), "the vector and scalar paths agree", form);
            const uint64_t length = 5 + (jump ? 0 : 1) + BUSTER_BRANCH_SIZING_TEST_TAIL_BYTES;
            branch_sizing_test_expect(&tally, vector.result.error == BRANCH_SIZING_OK && vector.result.long_branches == 1 && vector.result.length == length,
                                      "a branch's rel8 form grows by 3 for jmp and 4 for jcc", form);
            const X86FormDescription* grown = &x86_form_descriptions[vector.records[0].form];
            branch_sizing_test_expect(&tally, vector.records[0].form == form + 1 && grown->relative_bytes == 4 && strcmp(grown->mnemonic, x86_form_descriptions[form].mnemonic) == 0,
                                      "to the rel32 form after it, of the same mnemonic", form);
            branch_sizing_test_expect(&tally, vector.records[0].displacement == BUSTER_BRANCH_SIZING_TEST_TAIL_BYTES, "with the displacement past the tail", form);
            sized += 1;
        }
        else
        {
            branch_sizing_test_expect(&tally, check.error == BRANCH_SIZING_ERROR_FORM && check.index == 0, "branch_sizing_validate refuses any other form", form);
        }
        arena_pop_to(arena, mark);
    }
    branch_sizing_test_expect(&tally, sized == BUSTER_ARRAY_LENGTH(branch_sizing_forms), "17 forms are branches", sized);
    return branch_sizing_test_report(&tally);
}

// branch_sizing_validate must refuse the function with the error at the index, and both paths of the branch columns'
// check with the same, but take a record x86_validate rejects.
static void branch_sizing_test_refused(BranchSizingTestTally* tally, const BranchSizingTestFunction* function, BranchSizingError error, uint64_t index, const char* what)
{
    const BranchSizingCheck check = branch_sizing_validate(function->records, function->count, function->branches, function->targets, function->branch_count);
    branch_sizing_test_expect(tally, check.error == error && check.index == index, what, index);
    const BranchSizingCheck expected = {
        .error = error,
        .index = index,
    };
    branch_sizing_test_columns(tally, function, branch_sizing_test_columns_of(expected), what, index);
}

#define BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES 40

// A function of BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES branches, each followed by filler, aiming at the next one.
static void branch_sizing_test_error_function(uint64_t* state, BranchSizingTestFunction* function)
{
    function->count = 0;
    for (uint64_t k = 0; k < BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES; k += 1)
    {
        function->branches[k] = (uint32_t)function->count;
        function->records[function->count] = branch_sizing_branch(state, branch_sizing_below(state, 2) == 0);
        function->count += 1;
        function->records[function->count] = branch_sizing_random_filler(state);
        function->count += 1;
    }
    for (uint64_t k = 0; k < BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES; k += 1)
    {
        function->targets[k] = k + 1 < BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES ? function->branches[k + 1] : (uint32_t)function->count;
    }
    function->branch_count = BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES;
}

static bool branch_sizing_test_errors(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "errors: what branch_sizing_validate refuses",
    };
    uint64_t state = seed;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, 4 * BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES);
    for (uint64_t k = 0; k < BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES; k += 1)
    {
        branch_sizing_test_error_function(&state, &function);
        const BranchSizingTestOutcome fine = branch_sizing_test_all(&tally, arena, &function, k);
        branch_sizing_test_expect(&tally, fine.result.error == BRANCH_SIZING_OK, "the function is fine before it's broken", k);
        const uint32_t record = function.branches[k];
        const uint32_t target = function.targets[k];
        const uint16_t form = function.records[record].form;
        // Indices: equal to the one before, below it, at the count and past it; and the first of two errors.
        if (k != 0)
        {
            function.branches[k] = function.branches[k - 1];
            branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, k, "an index equal to the one before");
            function.branches[k] = function.branches[k - 1] - (k > 1 ? 1 : 0);
            branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, k, "an index below the one before");
        }
        function.branches[k] = (uint32_t)function.count;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, k, "an index at the count");
        function.branches[k] = UINT32_MAX;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, k, "an index past the count");
        function.targets[k] = (uint32_t)function.count + 1;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, k, "an index's error before a target's");
        function.branches[k] = record;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_TARGET, k, "a target past the count");
        function.targets[k] = UINT32_MAX;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_TARGET, k, "a target far past the count");
        function.records[record].form = X86_FORM_MOV_RM64_R64;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_TARGET, k, "a target's error before a form's");
        function.targets[k] = target;
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "a form that isn't a branch's");
        function.records[record].form = (uint16_t)(form + 1);
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "a branch's rel32 form");
        function.records[record].form = (uint16_t)(form + BUSTER_X86_FORM_FS);
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "a branch's form with FS");
        function.records[record].form = (uint16_t)(form + BUSTER_X86_FORM_GS);
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "a branch's form with GS");
        function.records[record].form = (uint16_t)(form | BUSTER_X86_FORM_FIXUP);
        branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "a branch's form with the fixup flag");
        if (k + 5 < BUSTER_BRANCH_SIZING_TEST_ERROR_BRANCHES)
        {
            const uint32_t later = function.targets[k + 5];
            function.targets[k + 5] = UINT32_MAX;
            branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_FORM, k, "the first of two errors");
            function.targets[k + 5] = later;
        }
        function.records[record].form = form;
    }
    // More branches than records: the first count + 1 can't all be ascending below the count.
    branch_sizing_test_error_function(&state, &function);
    for (uint64_t k = 0; k < function.count + 2; k += 1)
    {
        function.branches[k] = (uint32_t)k;
        function.targets[k] = 0;
        function.records[k] = branch_sizing_branch(&state, true);
    }
    function.branch_count = function.count + 2;
    branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_BRANCH_INDEX, function.count, "more branches than records");
    // More records than x86_encode takes: refused by branch_sizing_validate, and by both paths before anything is read.
    function.count = BUSTER_X86_ENCODER_MAX_INSTRUCTIONS + 1ull;
    function.branch_count = 0;
    branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS, 0, "more records than x86_encode takes");
    BranchSizing result = size_branches(function.records, function.count, function.branches, function.targets, 0, 0);
    branch_sizing_test_expect(&tally, result.error == BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS, "size_branches refuses more records than x86_encode takes", 0);
    result = size_branches_scalar(function.records, function.count, function.branches, function.targets, 0, 0);
    branch_sizing_test_expect(&tally, result.error == BRANCH_SIZING_ERROR_TOO_MANY_INSTRUCTIONS, "size_branches_scalar refuses more records than x86_encode takes", 0);
    // A record x86_validate rejects (AH with a REX prefix, which R8B needs): the index is x86_validate's, and a branch's
    // error comes first.
    branch_sizing_test_error_function(&state, &function);
    const uint64_t broken = function.branches[7] + 1;
    function.records[broken] = (X86Instruction){
        .form = X86_FORM_MOV_RM8_R8,
        .reg = X86_REGISTER_AH,
        .rm = X86_REGISTER_R8,
    };
    branch_sizing_test_expect(&tally, x86_validate(function.records, function.count) == broken, "x86_validate rejects the record", broken);
    branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_INVALID_INSTRUCTION, broken, "a record x86_validate rejects");
    function.targets[30] = UINT32_MAX;
    branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_TARGET, 30, "a branch's error before a record's");
    // A branch's record with an operand its form doesn't have: x86_validate's error, at the record.
    branch_sizing_test_error_function(&state, &function);
    function.records[function.branches[12]].reg = X86_REGISTER_RCX;
    branch_sizing_test_refused(&tally, &function, BRANCH_SIZING_ERROR_INVALID_INSTRUCTION, function.branches[12], "a branch's record with another operand");
    // The length past which a function is too large: every branch long would pass INT32_MAX.
    branch_sizing_test_expect(&tally, !branch_sizing_too_large(INT32_MAX - 4 * 1000, 1000), "a function that fits", 0);
    branch_sizing_test_expect(&tally, branch_sizing_too_large(INT32_MAX - 4 * 1000 + 1, 1000), "a function that could pass INT32_MAX bytes", 0);
    branch_sizing_test_expect(&tally, branch_sizing_too_large(INT32_MAX + 1ull, 0), "a function past INT32_MAX bytes", 0);
    return branch_sizing_test_report(&tally);
}

// A probe branch at record 0 (forward) or after its gap (backward), whose displacement with every branch short is
// `displacement`, over `inner` branches spread among the gap's filler: inner branch i is a jcc if bit i of `jccs` is
// set, and grows (aiming at the function's end, past the tail) if bit i of `grows` is set, or doesn't (aiming at the
// next record). The tail's filler comes last. Returns the inner branches' growth.
static uint32_t branch_sizing_test_probe(uint64_t* state, BranchSizingTestFunction* function, int32_t displacement, bool jcc, uint32_t inner, uint32_t jccs,
                                         uint32_t grows)
{
    const bool forward = displacement >= 0;
    // The gap's bytes: a forward probe's displacement, or a backward one's less its own 2 bytes.
    const uint32_t gap = forward ? (uint32_t)displacement : (uint32_t)(-displacement - BUSTER_BRANCH_SIZING_SHORT_BYTES);
    uint32_t filler = gap - BUSTER_BRANCH_SIZING_SHORT_BYTES * inner;
    uint32_t growth = 0;
    uint64_t count = 0;
    uint64_t branch_count = 0;
    if (forward)
    {
        function->branches[branch_count] = 0;
        function->records[count] = branch_sizing_branch(state, !jcc);
        count += 1;
        branch_count += 1;
    }
    for (uint32_t i = 0; i <= inner; i += 1)
    {
        const uint32_t piece = i == inner ? filler : branch_sizing_below(state, filler + 1);
        count += branch_sizing_filler_of(piece, function->records + count);
        filler -= piece;
        if (i < inner)
        {
            const bool inner_jcc = (jccs >> i) & 1;
            function->branches[branch_count] = (uint32_t)count;
            function->targets[branch_count] = (grows >> i) & 1 ? UINT32_MAX : (uint32_t)count + 1;
            function->records[count] = branch_sizing_branch(state, !inner_jcc);
            growth += (grows >> i) & 1 ? (inner_jcc ? BUSTER_BRANCH_SIZING_JCC_GROWTH : BUSTER_BRANCH_SIZING_JMP_GROWTH) : 0;
            count += 1;
            branch_count += 1;
        }
    }
    if (forward)
    {
        function->targets[0] = (uint32_t)count;
    }
    else
    {
        function->branches[branch_count] = (uint32_t)count;
        function->targets[branch_count] = 0;
        function->records[count] = branch_sizing_branch(state, !jcc);
        count += 1;
        branch_count += 1;
    }
    count += branch_sizing_filler_of(BUSTER_BRANCH_SIZING_TEST_TAIL_BYTES, function->records + count);
    for (uint64_t k = 0; k < branch_count; k += 1)
    {
        function->targets[k] = function->targets[k] == UINT32_MAX ? (uint32_t)count : function->targets[k];
    }
    function->count = count;
    function->branch_count = branch_count;
    return growth;
}

// The probes' displacements: around 127 and 128 forward, and around -128 and -129 backward, by more than the inner
// branches can grow by.
#define BUSTER_BRANCH_SIZING_TEST_FORWARD_FIRST 100
#define BUSTER_BRANCH_SIZING_TEST_FORWARD_LAST 135
#define BUSTER_BRANCH_SIZING_TEST_BACKWARD_FIRST (-140)
#define BUSTER_BRANCH_SIZING_TEST_BACKWARD_LAST (-105)
#define BUSTER_BRANCH_SIZING_TEST_INNER 3

static bool branch_sizing_test_boundaries(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "boundaries: probes at every displacement around rel8's limits",
    };
    uint64_t state = seed;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, 256);
    uint64_t index = 0;
    uint64_t grown = 0;
    for (uint32_t direction = 0; direction < 2; direction += 1)
    {
        const int32_t first = direction == 0 ? BUSTER_BRANCH_SIZING_TEST_FORWARD_FIRST : BUSTER_BRANCH_SIZING_TEST_BACKWARD_FIRST;
        const int32_t last = direction == 0 ? BUSTER_BRANCH_SIZING_TEST_FORWARD_LAST : BUSTER_BRANCH_SIZING_TEST_BACKWARD_LAST;
        for (int32_t displacement = first; displacement <= last; displacement += 1)
        {
            for (uint32_t inner = 0; inner <= BUSTER_BRANCH_SIZING_TEST_INNER; inner += 1)
            {
                for (uint32_t jccs = 0; jccs < (1u << inner); jccs += 1)
                {
                    for (uint32_t grows = 0; grows < (1u << inner); grows += 1)
                    {
                        for (uint32_t jcc = 0; jcc < 2; jcc += 1)
                        {
                            const uint64_t mark = arena->length;
                            const uint32_t growth = branch_sizing_test_probe(&state, &function, displacement, jcc != 0, inner, jccs, grows);
                            const BranchSizingTestOutcome outcome = branch_sizing_test_all(&tally, arena, &function, index);
                            // The probe is the first branch forward and the last backward.
                            const uint32_t probe = function.branches[direction == 0 ? 0 : function.branch_count - 1];
                            const int64_t final = direction == 0 ? (int64_t)displacement + growth : (int64_t)displacement - growth;
                            const bool expected = final < -128 || final > 127;
                            const bool long_probe = outcome.records[probe].form != function.records[probe].form;
                            branch_sizing_test_expect(&tally, outcome.result.error == BRANCH_SIZING_OK && long_probe == expected,
                                                      "the probe is long exactly when it's out of range with the inner branches' growth", index);
                            grown += long_probe;
                            arena_pop_to(arena, mark);
                            index += 1;
                        }
                    }
                }
            }
        }
    }
    branch_sizing_test_expect(&tally, grown != 0 && grown != index, "some probes grow and some don't", grown);
    return branch_sizing_test_report(&tally);
}

// A random shape for the random functions: mostly small and medium functions, sometimes large ones, half of them
// uniform and half compiler-like.
static BranchSizingShape branch_sizing_test_shape(uint64_t* state)
{
    const uint32_t size = branch_sizing_below(state, 100);
    BranchSizingShape result = {
        .count = size < 40   ? branch_sizing_below(state, 65)
                 : size < 80 ? 65 + branch_sizing_below(state, 2000)
                 : size < 98 ? 2065 + branch_sizing_below(state, 18000)
                             : 20065 + branch_sizing_below(state, BUSTER_BRANCH_SIZING_TEST_MAX_RECORDS - 20065),
        .branch_period = 1 + branch_sizing_below(state, 12),
        .near = 1 + branch_sizing_below(state, 100),
        .far_per_mille = branch_sizing_below(state, 400),
        .jmp_per_mille = branch_sizing_below(state, 1001),
        .backward_per_mille = branch_sizing_below(state, 1001),
        .compiler_like = branch_sizing_below(state, 2) == 0,
    };
    return result;
}

// Filler of random instructions of every form (x86_64_encoder_test.c's, ones GNU as encodes with the same form), but
// the relative branches, in place of the patterns' filler: lengths up to 15 bytes, of every kind of instruction.
static void branch_sizing_test_random_forms(uint64_t* state, BranchSizingTestFunction* function)
{
    uint64_t k = 0;
    for (uint64_t i = 0; i < function->count; i += 1)
    {
        if (k < function->branch_count && function->branches[k] == i)
        {
            k += 1;
        }
        else
        {
            uint16_t form = x86_random_form(state, false);
            while (x86_form_description(form)->relative_bytes != 0)
            {
                form = x86_random_form(state, false);
            }
            function->records[i] = x86_random_instruction(state, form, true);
        }
    }
}

// Each of the round limits but the default, with which both paths must give the same records and offsets.
static const uint32_t branch_sizing_test_limits[] = { 1, 2, 3, UINT32_MAX };

static bool branch_sizing_test_random(Arena* arena, uint64_t seed, uint64_t functions)
{
    BranchSizingTestTally tally = {
        .check = "random functions",
    };
    uint64_t state = seed;
    uint64_t x86_state = seed * 0x9E3779B97F4A7C15ull + 1;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, BUSTER_BRANCH_SIZING_TEST_MAX_RECORDS);
    uint64_t records = 0;
    uint64_t branches = 0;
    uint64_t long_branches = 0;
    // The functions that settled in 1, 2, 3 and more rounds (with the default limit).
    uint64_t rounds[5] = { 0 };
    uint64_t listed = 0;
    for (uint64_t i = 0; i < functions; i += 1)
    {
        const uint64_t mark = arena->length;
        const BranchSizingShape shape = branch_sizing_test_shape(&state);
        function.count = shape.count;
        function.branch_count = branch_sizing_generate(&state, &shape, function.records, function.branches, function.targets);
        if (branch_sizing_below(&state, 4) == 0 && function.count <= 20000)
        {
            branch_sizing_test_random_forms(&x86_state, &function);
        }
        const BranchSizingTestOutcome outcome = branch_sizing_test_all(&tally, arena, &function, i);
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(branch_sizing_test_limits); l += 1)
        {
            const uint64_t inner = arena->length;
            const BranchSizingTestOutcome vector = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_VECTOR, branch_sizing_test_limits[l]);
            const BranchSizingTestOutcome scalar = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_SCALAR, branch_sizing_test_limits[l]);
            branch_sizing_test_expect(&tally, branch_sizing_test_same(&vector, &scalar, function.count, true), "the paths agree with another round limit", i);
            branch_sizing_test_expect(&tally, branch_sizing_test_same(&vector, &outcome, function.count, false), "another round limit gives the same records and offsets", i);
            arena_pop_to(arena, inner);
        }
        records += function.count;
        branches += function.branch_count;
        long_branches += outcome.result.long_branches;
        rounds[outcome.result.rounds < 4 ? outcome.result.rounds : 4] += 1;
        listed += outcome.result.evaluations != 0;
        arena_pop_to(arena, mark);
    }
    printf("random functions: %llu functions, %llu records, %llu branches, %llu long; %llu settled in 1 round, %llu in 2, %llu in 3 and %llu in more (%llu "
           "with the worklist)\n",
           (unsigned long long)functions, (unsigned long long)records, (unsigned long long)branches, (unsigned long long)long_branches, (unsigned long long)rounds[1],
           (unsigned long long)rounds[2], (unsigned long long)rounds[3], (unsigned long long)rounds[4], (unsigned long long)listed);
    return branch_sizing_test_report(&tally);
}

// Random functions for the branch columns' check, and the most records one has: a few vectors of branches to many.
#define BUSTER_BRANCH_SIZING_TEST_COLUMN_FUNCTIONS 4000
#define BUSTER_BRANCH_SIZING_TEST_COLUMN_RECORDS 1200

// Breaks a function 0 to 3 times at random: a branch's index (equal to the one before, below it, at the count, past it
// or anywhere below it), a target (past the count, or anywhere up to it), a branch's form (any number, its rel32 form,
// with FS, GS or the fixup flag), a branch's record with other operands beside its form (the vvvv and decorations bytes
// that share the form's dword, which the vector path gathers), or a record x86_validate rejects (AH with a REX prefix).
// Some breaks happen to leave the function valid, and a later one can hide an earlier one.
static void branch_sizing_test_break(uint64_t* state, BranchSizingTestFunction* function)
{
    const uint32_t breaks = function->count != 0 ? branch_sizing_below(state, 4) : 0;
    for (uint32_t b = 0; b < breaks; b += 1)
    {
        const uint32_t count = (uint32_t)function->count;
        const uint64_t k = function->branch_count != 0 ? branch_sizing_below(state, (uint32_t)function->branch_count) : 0;
        const uint32_t kind = function->branch_count != 0 ? branch_sizing_below(state, 13) : 11;
        const uint32_t before = k != 0 ? function->branches[k - 1] : 0;
        const uint32_t record = function->branch_count != 0 ? function->branches[k] : count;
        const uint16_t form = record < count ? function->records[record].form : 0;
        const uint16_t variants[] = {
            (uint16_t)(form + 1),
            (uint16_t)(form + BUSTER_X86_FORM_FS),
            (uint16_t)(form + BUSTER_X86_FORM_GS),
            (uint16_t)(form | BUSTER_X86_FORM_FIXUP),
        };
        if (kind == 0)
        {
            function->branches[k] = before;
        }
        else if (kind == 1)
        {
            function->branches[k] = before - (before != 0 ? 1 : 0);
        }
        else if (kind == 2)
        {
            function->branches[k] = count;
        }
        else if (kind == 3)
        {
            function->branches[k] = branch_sizing_below(state, 2) == 0 ? count + 1 + branch_sizing_below(state, 1000) : UINT32_MAX;
        }
        else if (kind == 4)
        {
            function->branches[k] = branch_sizing_below(state, count);
        }
        else if (kind == 5)
        {
            function->targets[k] = branch_sizing_below(state, 2) == 0 ? count + 1 + branch_sizing_below(state, 1000) : UINT32_MAX;
        }
        else if (kind == 6)
        {
            function->targets[k] = branch_sizing_below(state, count + 1);
        }
        else if (kind <= 8 && record < count)
        {
            function->records[record].form = (uint16_t)branch_sizing_below(state, 0x10000);
        }
        else if (kind <= 10 && record < count)
        {
            function->records[record].form = variants[branch_sizing_below(state, BUSTER_ARRAY_LENGTH(variants))];
        }
        else if (kind == 11)
        {
            function->records[branch_sizing_below(state, count)] = (X86Instruction){
                .form = X86_FORM_MOV_RM8_R8,
                .reg = X86_REGISTER_AH,
                .rm = X86_REGISTER_R8,
            };
        }
        else if (kind == 12 && record < count)
        {
            function->records[record].vvvv = (uint8_t)(1 + branch_sizing_below(state, 255));
            function->records[record].decorations = (uint8_t)branch_sizing_below(state, 256);
        }
    }
}

// The branch columns' check against branch_sizing_validate on random functions broken at random: both paths must give
// its first error, but take a record x86_validate rejects. The functions have up to
// BUSTER_BRANCH_SIZING_TEST_COLUMN_RECORDS records, a branch every 1 to 12, so that errors fall in every lane of the
// vector path's first vector, its last (partial) one and those between.
static bool branch_sizing_test_columns_random(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "columns: random breaks, against branch_sizing_validate",
    };
    uint64_t state = seed ^ 0x5bd1e995u;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, BUSTER_BRANCH_SIZING_TEST_COLUMN_RECORDS);
    // The functions the check took, and those it refused by each error.
    uint64_t outcomes[BRANCH_SIZING_ERROR_MEMORY + 1] = { 0 };
    for (uint64_t i = 0; i < BUSTER_BRANCH_SIZING_TEST_COLUMN_FUNCTIONS; i += 1)
    {
        BranchSizingShape shape = branch_sizing_test_shape(&state);
        shape.count = branch_sizing_below(&state, BUSTER_BRANCH_SIZING_TEST_COLUMN_RECORDS + 1);
        function.count = shape.count;
        function.branch_count = branch_sizing_generate(&state, &shape, function.records, function.branches, function.targets);
        branch_sizing_test_break(&state, &function);
        const BranchSizingCheck full = branch_sizing_validate(function.records, function.count, function.branches, function.targets, function.branch_count);
        const BranchSizingCheck expected = branch_sizing_test_columns_of(full);
        branch_sizing_test_columns(&tally, &function, expected, "a function broken at random", i);
        outcomes[expected.error] += 1;
    }
    printf("columns: %llu functions, %llu taken, %llu refused by an index, %llu by a target and %llu by a form\n",
           (unsigned long long)BUSTER_BRANCH_SIZING_TEST_COLUMN_FUNCTIONS, (unsigned long long)outcomes[BRANCH_SIZING_OK],
           (unsigned long long)outcomes[BRANCH_SIZING_ERROR_BRANCH_INDEX], (unsigned long long)outcomes[BRANCH_SIZING_ERROR_TARGET],
           (unsigned long long)outcomes[BRANCH_SIZING_ERROR_FORM]);
    branch_sizing_test_expect(&tally, outcomes[BRANCH_SIZING_OK] != 0 && outcomes[BRANCH_SIZING_ERROR_BRANCH_INDEX] != 0 && outcomes[BRANCH_SIZING_ERROR_TARGET] != 0 &&
                                         outcomes[BRANCH_SIZING_ERROR_FORM] != 0,
                              "the functions reach every outcome", 0);
    return branch_sizing_test_report(&tally);
}

// The chains' links.
static const uint32_t branch_sizing_test_links[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 63, 64, 65, 100, 1000 };

static bool branch_sizing_test_chains(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "chains: a round per link, then the worklist",
    };
    uint64_t state = seed;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, BUSTER_BRANCH_SIZING_CHAIN_RECORDS(1000));
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(branch_sizing_test_links); i += 1)
    {
        for (uint32_t backward = 0; backward < 2; backward += 1)
        {
            const uint64_t mark = arena->length;
            const uint32_t links = branch_sizing_test_links[i];
            function.count = branch_sizing_chain(&state, links, backward != 0, function.records, function.branches, function.targets);
            function.branch_count = links;
            const BranchSizingTestOutcome outcome = branch_sizing_test_all(&tally, arena, &function, links);
            const uint32_t rounds = (links < BUSTER_BRANCH_SIZING_ROUNDS ? links : BUSTER_BRANCH_SIZING_ROUNDS) + 1;
            branch_sizing_test_expect(&tally, outcome.result.long_branches == links, "every link grows", links);
            branch_sizing_test_expect(&tally, outcome.result.rounds == rounds, "a round per link, up to the limit, and one more", links);
            branch_sizing_test_expect(&tally, (outcome.result.evaluations != 0) == (links > BUSTER_BRANCH_SIZING_ROUNDS), "the worklist past the limit", links);
            const BranchSizingTestOutcome unlimited = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_VECTOR, UINT32_MAX);
            branch_sizing_test_expect(&tally, unlimited.result.rounds == links + 1 && branch_sizing_test_same(&outcome, &unlimited, function.count, false),
                                      "without the worklist, a round per link and the same result", links);
            arena_pop_to(arena, mark);
        }
    }
    return branch_sizing_test_report(&tally);
}

// The page-edge check's case, for the fault handler: the records, the branches, the path, and whether it was checking
// the branch columns (branch_sizing_validate_branches or its twin) or sizing.
static volatile uint64_t branch_sizing_test_edge_count;
static volatile uint64_t branch_sizing_test_edge_branches;
static volatile uint32_t branch_sizing_test_edge_path;
static volatile bool branch_sizing_test_edge_checking;

static void branch_sizing_test_report_fault(int signal_number)
{
    (void)signal_number;
    char message[200];
    const int length = snprintf(message, sizeof(message), "page edges: the %s path faulted %s on %lu records and %lu branches\n",
                                branch_sizing_test_edge_path == BRANCH_SIZING_TEST_VECTOR ? "vector" : "scalar",
                                branch_sizing_test_edge_checking ? "checking the branch columns" : "sizing", (unsigned long)branch_sizing_test_edge_count,
                                (unsigned long)branch_sizing_test_edge_branches);
    if (length > 0 && write(STDOUT_FILENO, message, (size_t)length) < 0)
    {
        _exit(1);
    }
    _exit(1);
}

// `bytes` rounded up to whole pages between two PROT_NONE pages: the mapping, and the end of its readable part.
typedef struct BranchSizingTestGuarded BranchSizingTestGuarded;
struct BranchSizingTestGuarded
{
    uint8_t* mapping;
    uint64_t mapped;
    uint8_t* end;
};

static BranchSizingTestGuarded branch_sizing_test_guard(uint64_t bytes, uint64_t page)
{
    const uint64_t pages = (bytes + page - 1) / page + 1;
    BranchSizingTestGuarded result = {
        .mapped = (pages + 2) * page,
    };
    uint8_t* mapping = mmap(0, result.mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED && mprotect(mapping + page, pages * page, PROT_READ | PROT_WRITE) == 0)
    {
        result.mapping = mapping;
        result.end = mapping + page + pages * page;
    }
    return result;
}

#define BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS 70
#define BUSTER_BRANCH_SIZING_TEST_EDGE_BRANCHES 40

static bool branch_sizing_test_page_edges(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "page edges: every column ending at a PROT_NONE page",
    };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t state = seed;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS);
    BranchSizingTestGuarded records = branch_sizing_test_guard(BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS * sizeof(X86Instruction), page);
    BranchSizingTestGuarded branches = branch_sizing_test_guard(BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS * sizeof(uint32_t), page);
    BranchSizingTestGuarded targets = branch_sizing_test_guard(BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS * sizeof(uint32_t), page);
    BranchSizingTestGuarded offsets = branch_sizing_test_guard(BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS * sizeof(uint32_t), page);
    const bool mapped = records.mapping != 0 && branches.mapping != 0 && targets.mapping != 0 && offsets.mapping != 0;
    branch_sizing_test_expect(&tally, mapped, "the guarded buffers are mapped", 0);
    struct sigaction action = {
        .sa_handler = branch_sizing_test_report_fault,
    };
    struct sigaction previous_segv;
    struct sigaction previous_bus;
    sigaction(SIGSEGV, &action, &previous_segv);
    sigaction(SIGBUS, &action, &previous_bus);
    uint64_t cases = 0;
    for (uint64_t count = 0; mapped && count <= BUSTER_BRANCH_SIZING_TEST_EDGE_RECORDS; count += 1)
    {
        for (uint32_t density = 1; density <= 4; density += 1)
        {
            const uint64_t mark = arena->length;
            const BranchSizingShape shape = {
                .count = count,
                .branch_period = density,
                .near = 40,
                .far_per_mille = 100,
                .jmp_per_mille = 500,
            };
            function.count = count;
            function.branch_count = branch_sizing_generate(&state, &shape, function.records, function.branches, function.targets);
            // Every branch count up to BUSTER_BRANCH_SIZING_TEST_EDGE_BRANCHES, from the generated function's first branches.
            const uint64_t most = function.branch_count < BUSTER_BRANCH_SIZING_TEST_EDGE_BRANCHES ? function.branch_count : BUSTER_BRANCH_SIZING_TEST_EDGE_BRANCHES;
            for (uint64_t branch_count = density == 1 ? 0 : most; branch_count <= most; branch_count += 1)
            {
                BranchSizingTestFunction cut = function;
                cut.branch_count = branch_count;
                const uint64_t inner = arena->length;
                const BranchSizingTestOutcome expected = branch_sizing_test_size(arena, &cut, BRANCH_SIZING_TEST_SCALAR, BUSTER_BRANCH_SIZING_ROUNDS);
                X86Instruction* edge_records = (X86Instruction*)(records.end - count * sizeof(X86Instruction));
                uint32_t* edge_branches = (uint32_t*)(branches.end - branch_count * sizeof(uint32_t));
                uint32_t* edge_targets = (uint32_t*)(targets.end - branch_count * sizeof(uint32_t));
                uint32_t* edge_offsets = (uint32_t*)(offsets.end - count * sizeof(uint32_t));
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    memcpy(edge_records, function.records, count * sizeof(X86Instruction));
                    memcpy(edge_branches, function.branches, branch_count * sizeof(uint32_t));
                    memcpy(edge_targets, function.targets, branch_count * sizeof(uint32_t));
                    branch_sizing_test_edge_count = count;
                    branch_sizing_test_edge_branches = branch_count;
                    branch_sizing_test_edge_path = path;
                    branch_sizing_test_edge_checking = true;
                    const BranchSizingCheck columns = path == BRANCH_SIZING_TEST_VECTOR ?
                                                          branch_sizing_validate_branches(edge_records, count, edge_branches, edge_targets, branch_count) :
                                                          branch_sizing_validate_branches_scalar(edge_records, count, edge_branches, edge_targets, branch_count);
                    branch_sizing_test_expect(&tally, columns.error == BRANCH_SIZING_OK,
                                              path == BRANCH_SIZING_TEST_VECTOR ? "the vector path's branch columns' check takes the function" :
                                                                                  "the scalar path's branch columns' check takes the function",
                                              count);
                    branch_sizing_test_edge_checking = false;
                    const BranchSizing result = branch_sizing_run(edge_records, count, edge_branches, edge_targets, branch_count, edge_offsets,
                                                                  path == BRANCH_SIZING_TEST_VECTOR, BUSTER_BRANCH_SIZING_ROUNDS);
                    const bool same = result.error == expected.result.error && result.length == expected.result.length && result.rounds == expected.result.rounds &&
                                      memcmp(edge_records, expected.records, count * sizeof(X86Instruction)) == 0 &&
                                      (result.error != BRANCH_SIZING_OK || memcmp(edge_offsets, expected.offsets, count * sizeof(uint32_t)) == 0);
                    branch_sizing_test_expect(&tally, same, path == BRANCH_SIZING_TEST_VECTOR ? "the vector path's result" : "the scalar path's result", count);
                }
                arena_pop_to(arena, inner);
                cases += 1;
            }
            arena_pop_to(arena, mark);
        }
    }
    sigaction(SIGSEGV, &previous_segv, 0);
    sigaction(SIGBUS, &previous_bus, 0);
    if (mapped)
    {
        munmap(records.mapping, records.mapped);
        munmap(branches.mapping, branches.mapped);
        munmap(targets.mapping, targets.mapped);
        munmap(offsets.mapping, offsets.mapped);
    }
    printf("page edges: %llu cases\n", (unsigned long long)cases);
    return branch_sizing_test_report(&tally);
}

static bool branch_sizing_test_memory(Arena* arena, uint64_t seed)
{
    BranchSizingTestTally tally = {
        .check = "memory: no scratch to spare, then 64 bytes more at a time",
    };
    uint64_t state = seed;
    BranchSizingTestFunction function = branch_sizing_test_function(arena, 2000);
    const BranchSizingShape shape = {
        .count = 2000,
        .branch_period = 5,
        .near = 60,
        .far_per_mille = 200,
        .jmp_per_mille = 300,
    };
    function.count = shape.count;
    function.branch_count = branch_sizing_generate(&state, &shape, function.records, function.branches, function.targets);
    uint64_t attempts = 0;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const uint64_t mark = arena->length;
        const BranchSizingTestOutcome expected = branch_sizing_test_size(arena, &function, (BranchSizingTestPath)path, BUSTER_BRANCH_SIZING_ROUNDS);
        X86Instruction* records = BUSTER_ARENA_PUSH_ARRAY(arena, X86Instruction, function.count);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, function.count);
        bool sized = false;
        for (uint64_t room = 0; !sized && room < (64ull << 20); room += 64)
        {
            memcpy(records, function.records, function.count * sizeof(X86Instruction));
            memset(offsets, 0xa5, function.count * sizeof(uint32_t));
            Scratch scratch = scratch_begin(0);
            const uint64_t free_bytes = scratch.arena->capacity - scratch.arena->length;
            arena_push(scratch.arena, free_bytes - room, 1);
            const BranchSizing result = branch_sizing_run(records, function.count, function.branches, function.targets, function.branch_count, offsets,
                                                          path == BRANCH_SIZING_TEST_VECTOR, BUSTER_BRANCH_SIZING_ROUNDS);
            scratch_end(scratch);
            sized = result.error == BRANCH_SIZING_OK;
            if (!sized)
            {
                bool untouched = memcmp(records, function.records, function.count * sizeof(X86Instruction)) == 0;
                for (uint64_t i = 0; i < function.count; i += 1)
                {
                    untouched = untouched && offsets[i] == 0xa5a5a5a5u;
                }
                branch_sizing_test_expect(&tally, result.error == BRANCH_SIZING_ERROR_MEMORY, "a failure is BRANCH_SIZING_ERROR_MEMORY", room);
                branch_sizing_test_expect(&tally, untouched, "and leaves the records and the offsets as they were", room);
            }
            else
            {
                branch_sizing_test_expect(&tally, room != 0, "some scratch memory is needed", room);
                const bool same = result.length == expected.result.length && memcmp(records, expected.records, function.count * sizeof(X86Instruction)) == 0 &&
                                  memcmp(offsets, expected.offsets, function.count * sizeof(uint32_t)) == 0;
                branch_sizing_test_expect(&tally, same, "then the function is sized as with room to spare", room);
            }
            attempts += 1;
        }
        branch_sizing_test_expect(&tally, sized, "the function is sized in the end", path);
        arena_pop_to(arena, mark);
    }
    printf("memory: %llu attempts\n", (unsigned long long)attempts);
    return branch_sizing_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// GNU as

// Runs `arguments` (a program on PATH and its arguments, null-terminated) with stdout and stderr into the file at
// `log_path`, for up to BUSTER_BRANCH_SIZING_TEST_RUN_SECONDS, and returns its exit status: -1 if it couldn't start, was
// killed by a signal or ran out of time.
static int branch_sizing_test_run(char* const* arguments, const char* log_path)
{
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 1, log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_adddup2(&actions, 1, 2) == 0;
        if (ready && posix_spawnp(&child, arguments[0], &actions, 0, arguments, environ) == 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 1000000,
            };
            for (uint64_t milliseconds = 0; waited == 0 && milliseconds < 1000ull * BUSTER_BRANCH_SIZING_TEST_RUN_SECONDS; milliseconds += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            else if (waited == child && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

// Prints the function as GNU as source in a section of its own, .text.fN for function N: a label .LN_i before each
// record that a branch aims at (.LN_count at the end), each branch as its mnemonic and its target's label, and the
// other records as x86_print_instruction writes them (records of no bytes as nothing).
static void branch_sizing_test_print(FILE* file, uint64_t number, const BranchSizingTestFunction* function, uint8_t* aimed)
{
    memset(aimed, 0, function->count + 1);
    for (uint64_t k = 0; k < function->branch_count; k += 1)
    {
        aimed[function->targets[k]] = 1;
    }
    fprintf(file, ".section .text.f%llu, \"ax\", @progbits\n", (unsigned long long)number);
    uint64_t k = 0;
    for (uint64_t i = 0; i <= function->count; i += 1)
    {
        if (aimed[i])
        {
            fprintf(file, ".L%llu_%llu:\n", (unsigned long long)number, (unsigned long long)i);
        }
        if (i < function->count && k < function->branch_count && function->branches[k] == i)
        {
            fprintf(file, "%s .L%llu_%u\n", x86_form_description(function->records[i].form)->mnemonic, (unsigned long long)number, function->targets[k]);
            k += 1;
        }
        else if (i < function->count && function->records[i].form != X86_FORM_NONE)
        {
            x86_print_instruction(file, &function->records[i], 0);
            fputc('\n', file);
        }
    }
}

// Replaces each filler record GNU as would encode with another form (x86_assembler_prefers_another_form: an
// accumulator's shorter form, say) with MOV RAX, RCX.
static void branch_sizing_test_for_assembler(BranchSizingTestFunction* function)
{
    uint64_t k = 0;
    for (uint64_t i = 0; i < function->count; i += 1)
    {
        if (k < function->branch_count && function->branches[k] == i)
        {
            k += 1;
        }
        else if (x86_assembler_prefers_another_form(&function->records[i]))
        {
            function->records[i] = (X86Instruction){
                .form = X86_FORM_MOV_RM64_R64,
                .reg = X86_REGISTER_RCX,
            };
        }
    }
}

// The functions of the cross-check: random ones (a quarter with random instructions of every form), the boundaries'
// probes, and chains of both directions. Writes function `number` into `function` and returns whether there is one.
static bool branch_sizing_test_assembler_function(uint64_t* state, uint64_t* x86_state, uint64_t number, BranchSizingTestFunction* function)
{
    const uint64_t chains = 2 * BUSTER_ARRAY_LENGTH(branch_sizing_test_links);
    const uint64_t probes = 2 * (BUSTER_BRANCH_SIZING_TEST_FORWARD_LAST - BUSTER_BRANCH_SIZING_TEST_FORWARD_FIRST + 1);
    bool result = true;
    if (number < BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS)
    {
        BranchSizingShape shape = branch_sizing_test_shape(state);
        shape.count = shape.count < 20000 ? shape.count : 20000;
        function->count = shape.count;
        function->branch_count = branch_sizing_generate(state, &shape, function->records, function->branches, function->targets);
        if (number % 4 == 0)
        {
            branch_sizing_test_random_forms(x86_state, function);
        }
    }
    else if (number < BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS + probes)
    {
        const uint64_t probe = number - BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS;
        const int32_t displacement = probe % 2 == 0 ? BUSTER_BRANCH_SIZING_TEST_FORWARD_FIRST + (int32_t)(probe / 2)
                                                    : BUSTER_BRANCH_SIZING_TEST_BACKWARD_FIRST + (int32_t)(probe / 2);
        branch_sizing_test_probe(state, function, displacement, probe % 4 < 2, 3, branch_sizing_below(state, 8), branch_sizing_below(state, 8));
    }
    else if (number < BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS + probes + chains)
    {
        const uint64_t chain = number - BUSTER_BRANCH_SIZING_TEST_ASSEMBLER_FUNCTIONS - probes;
        const uint32_t links = branch_sizing_test_links[chain / 2];
        function->count = branch_sizing_chain(state, links, chain % 2 != 0, function->records, function->branches, function->targets);
        function->branch_count = links;
    }
    else
    {
        result = false;
    }
    if (result)
    {
        branch_sizing_test_for_assembler(function);
    }
    return result;
}

static bool branch_sizing_test_assembler(Arena* arena, uint64_t seed, const char* parent, bool keep)
{
    BranchSizingTestTally tally = {
        .check = "GNU as: its relaxation of the functions with labels",
    };
    char directory[BUSTER_BRANCH_SIZING_TEST_PATH_BYTES];
    char source[BUSTER_BRANCH_SIZING_TEST_PATH_BYTES];
    char object[BUSTER_BRANCH_SIZING_TEST_PATH_BYTES];
    char log[BUSTER_BRANCH_SIZING_TEST_PATH_BYTES];
    const int length = snprintf(directory, sizeof(directory), "%s/buster-branch-sizing-XXXXXX", parent);
    const bool made = length > 0 && (uint64_t)length + 32 < sizeof(directory) && mkdtemp(directory) != 0;
    branch_sizing_test_expect(&tally, made, "a directory for the files", 0);
    snprintf(source, sizeof(source), "%s/functions.s", directory);
    snprintf(object, sizeof(object), "%s/functions.o", directory);
    snprintf(log, sizeof(log), "%s/as.log", directory);
    FILE* file = made ? fopen(source, "w") : 0;
    branch_sizing_test_expect(&tally, !made || file != 0, "the source file", 0);
    BranchSizingTestFunction function = branch_sizing_test_function(arena, BUSTER_BRANCH_SIZING_CHAIN_RECORDS(1000) + 20000);
    uint8_t* aimed = arena_push(arena, BUSTER_BRANCH_SIZING_CHAIN_RECORDS(1000) + 20001, 64);
    uint64_t functions = 0;
    if (file != 0)
    {
        // MPX, AMX-TRANSPOSE and AMX-TF32 aren't in GNU as's default architecture (as x86_print_assembly says).
        fprintf(file, ".intel_syntax noprefix\n.arch .mpx\n.arch .amx_transpose\n.arch .amx_tf32\n");
        uint64_t state = seed;
        uint64_t x86_state = seed * 0x9E3779B97F4A7C15ull + 7;
        while (branch_sizing_test_assembler_function(&state, &x86_state, functions, &function))
        {
            branch_sizing_test_print(file, functions, &function, aimed);
            functions += 1;
        }
        fclose(file);
    }
    char* arguments[] = { "as", "--64", "-o", object, source, 0 };
    const int status = file != 0 ? branch_sizing_test_run(arguments, log) : -1;
    branch_sizing_test_expect(&tally, status == 0, "GNU as assembles the functions (see as.log)", (uint64_t)status);
    uint64_t object_length = 0;
    uint8_t* bytes = status == 0 ? x86_read_file(object, &object_length) : 0;
    // Without the object nothing below is compared, so its absence must fail the check rather than pass it.
    branch_sizing_test_expect(&tally, status != 0 || bytes != 0, "GNU as's object can be read", 0);
    uint64_t checked = 0;
    uint64_t matched = 0;
    uint64_t long_branches = 0;
    if (bytes != 0)
    {
        uint64_t state = seed;
        uint64_t x86_state = seed * 0x9E3779B97F4A7C15ull + 7;
        uint8_t* encoded = arena_push(arena, BUSTER_X86_ENCODER_CAPACITY(BUSTER_BRANCH_SIZING_CHAIN_RECORDS(1000) + 20000), 64);
        for (uint64_t number = 0; branch_sizing_test_assembler_function(&state, &x86_state, number, &function); number += 1)
        {
            const uint64_t mark = arena->length;
            const BranchSizingTestOutcome outcome = branch_sizing_test_size(arena, &function, BRANCH_SIZING_TEST_VECTOR, BUSTER_BRANCH_SIZING_ROUNDS);
            char name[64];
            snprintf(name, sizeof(name), ".text.f%llu", (unsigned long long)number);
            const Elf64_Shdr* header = 0;
            const uint8_t* section = x86_elf_section(bytes, object_length, SHT_PROGBITS, name, &header);
            const int64_t encoded_length =
                outcome.result.error == BRANCH_SIZING_OK ? x86_encode_scalar(outcome.records, function.count, encoded, BUSTER_X86_ENCODER_CAPACITY(function.count), 0, 0) : -1;
            const bool same = section != 0 && encoded_length >= 0 && header->sh_size == (uint64_t)encoded_length && memcmp(section, encoded, header->sh_size) == 0;
            branch_sizing_test_expect(&tally, same, "the function's bytes are GNU as's", number);
            if (!same && section != 0 && encoded_length >= 0 && tally.failures <= BUSTER_BRANCH_SIZING_TEST_REPORTED)
            {
                uint64_t offset = 0;
                const uint64_t shorter = header->sh_size < (uint64_t)encoded_length ? header->sh_size : (uint64_t)encoded_length;
                while (offset < shorter && section[offset] == encoded[offset])
                {
                    offset += 1;
                }
                uint64_t record = 0;
                while (record + 1 < function.count && outcome.offsets[record + 1] <= offset)
                {
                    record += 1;
                }
                printf("    function %llu: %llu bytes against GNU as's %llu, first differing at byte %llu, in record %llu\n", (unsigned long long)number,
                       (unsigned long long)encoded_length, (unsigned long long)header->sh_size, (unsigned long long)offset, (unsigned long long)record);
            }
            checked += 1;
            matched += same;
            long_branches += outcome.result.long_branches;
            arena_pop_to(arena, mark);
        }
        free(bytes);
    }
    printf("GNU as: %llu functions, %llu compared, %llu the same, with %llu long branches\n", (unsigned long long)functions, (unsigned long long)checked,
           (unsigned long long)matched, (unsigned long long)long_branches);
    branch_sizing_test_expect(&tally, functions != 0 && checked == functions, "every function printed is compared with GNU as's bytes", checked);
    if (made && !keep && tally.failures == 0)
    {
        unlink(source);
        unlink(object);
        unlink(log);
        rmdir(directory);
    }
    else if (made)
    {
        printf("kept %s\n", directory);
    }
    return branch_sizing_test_report(&tally);
}

// ---------------------------------------------------------------------------------------------------------------------
// run_branch_sizing_tests

static bool branch_sizing_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = end != text && *end == 0;
    *value = result ? parsed : *value;
    return result;
}

static int run_branch_sizing_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t functions = BUSTER_BRANCH_SIZING_TEST_FUNCTIONS;
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && branch_sizing_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            valid = valid && branch_sizing_test_parse_u64(argv[i + 1], &functions);
            i += 1;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    Arena arena = valid ? arena_reserve(BUSTER_BRANCH_SIZING_TEST_ARENA_BYTES) : (Arena){ 0 };
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-branch-sizing [--seed N] [--count N] [--cross-check] [--keep]\n");
    }
    else if (arena.base == 0)
    {
        fprintf(stderr, "test-branch-sizing: can't reserve the test's arena\n");
    }
    else if (!x86_encoder_initialized && !x86_encoder_initialize())
    {
        fprintf(stderr, "the encoder's templates can't be built\n");
    }
    else
    {
        // The random instructions and the printer are 64-bit mode's.
        x86_test_mode = X86_ENCODER_MODE_64;
        const char* parent = getenv("TMPDIR") != 0 ? getenv("TMPDIR") : "/tmp";
        bool passed = branch_sizing_test_forms(&arena);
        passed = branch_sizing_test_errors(&arena, seed) && passed;
        passed = branch_sizing_test_boundaries(&arena, seed) && passed;
        passed = branch_sizing_test_random(&arena, seed, functions) && passed;
        passed = branch_sizing_test_columns_random(&arena, seed) && passed;
        passed = branch_sizing_test_chains(&arena, seed) && passed;
        passed = branch_sizing_test_page_edges(&arena, seed) && passed;
        passed = branch_sizing_test_memory(&arena, seed) && passed;
        if (cross_check)
        {
            passed = branch_sizing_test_assembler(&arena, seed, parent, keep) && passed;
        }
        result = passed ? 0 : 1;
    }
    arena_release(&arena);
    return result;
}
