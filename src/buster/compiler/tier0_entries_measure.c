// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2874's census (measure-tier0-entries): what a typed entry's records depend on. It is the one-core path's step 4's
// first piece (#2850): before Tier 0's selection is redesigned as a table's lookup on bytes its producer writes, a count
// of what those bytes would have to be, on real functions. It measures no time and changes no compile: the functions are
// the ones -c lowers (benchmark-backend-source's, tier0_source_benchmark.c), compiled by tier0_compile as it is.
//
//   build/compiler measure-tier0-entries [--rows] [--list KIND|all] [--top N] [preprocessor options] file...
//                                        [--next [preprocessor options] file...]...
//
// --next starts another set of files with options of their own, counted into the same census: several corpora as one
// population, each read as it is compiled.
//
// A sample is a group: an entry that makes records (its head) with the entries before it that it absorbs (a fused
// constant, local, pointer addition or compare, which make none), so that every typed entry is in one group and counts
// with its absorber. A group's answer is its record pattern, and its inputs are bytes a producer could write beside
// the head.
//
// The pattern. For a head that takes a template (tier0_templates), the template's own content: each record's operands
// by role (a depth's register, a home's, a fixed one), its immediate and displacement as the constant's or a home's
// hole, its branch and fixup, with the form each record has after the immediates' choice. The census finds the template
// as selection does (tier0_slot, the depth a template requires, tier0_home_case) and checks it against the records
// tier0_compile made: the same count, and every form but a chosen one's. For any other head (a deferred entry: a call, a
// division or a shift where its fixed registers are taken, a copy, the memory classes', or one whose records the
// template doesn't give), the records themselves with each register renamed by its place from the head's depth (a
// general register's in the integer depth table, an XMM's number) and the frame's registers named. A symbol, a branch's
// target, a template's constant and a displacement from a frame's register (a home's, a save's, an outgoing
// argument's) are carried, never part of a pattern.
//
// The inputs (tier0_census_input_names), one byte each: the head's class; the kinds and classes of the three entries
// before it and which of the four are jump targets; a class of each one's value by magnitude, as 64 bits and as its own
// class's width (zero, one, minus one, 8 bits, 32 bits, more); a pointer addition's element size as a SIB scale; whether
// a fused constant fits its consumer (with the element's size, the product) and that product's class by magnitude;
// whether a store's local is the one three back; each named local's home (a general or an XMM register, a slot, an
// object); the two depths; the symbol's access; whether the entry is the function's last; the function's result; a
// call's counts of register and stack arguments, its flags, its result and which of its first eight arguments are
// narrow integers; a copy's bytes; and the frame's kind with the function's rare classes. A branch after a compare's
// test against zero takes the compare and its operands with it (#2877), five entries back: where the stream has that
// shape, the kinds, classes and homes of the two entries before the compare, the nearer one's value by its two classes,
// and whether either is a jump target, are inputs too. No input is a value, a displacement, a symbol or a table's row.
// Three more are the fusion's own answers (its key's kind and class and its variant): lane arithmetic on the others,
// which Tier 0 makes today.
//
// The count, a head's kind at a time, twice: over the raw inputs alone (`keys=raw`), and with the fusion's three
// offered (`keys=fused`). Two figures are counted of a set of inputs. Its conflicts: the entries in keys with two
// patterns, so that a set decides an entry when the entry's key at that set has one pattern (the gate's rule: a single
// odd group makes its whole key's entries undecided). And its exceptions: the entries whose pattern isn't their key's
// commonest, which is what a table with a way out for the rest would get wrong. The entries no input decides are those
// in conflict at every input. For each size of set the census looks for the one with the fewest of each (the fewest
// rows among equals): every set of up to BUSTER_TIER0_CENSUS_EXHAUSTIVE inputs where the kind's distinct samples allow
// that many lookups, then by adding to the best one the input that takes the most away (a pair where no single input
// takes any). So a size's figure is the best of the sets tried, a lower bound past the exhaustive sizes. The kind's
// whole key is the first of those sets that leaves only the whole key's conflicts, less the inputs it can drop. Each
// kind's row has its entries, its whole key's bytes (the kind is the first), that key's rows (its distinct keys), its
// patterns, the share of its entries a key of each size decides and the share whose pattern is their key's commonest,
// and the keys the gate counts (BUSTER_TIER0_CENSUS_GATE_BYTES bytes or fewer). The totals have the groups by their
// entries and by the fusion's variant, those that make no record, both figures for a key of each size, the line of
// #2874's gate, the tables' rows, what the gate's keys decide without each family of inputs (so what needs it), and
// what no key decides.
//
// Tier 0's passes are not called but tier0_compile and the fusion's own inlined body (tier0_fuse_entries), so that no
// function of a compile gains a caller. Include after tier0_source_benchmark.c.
#ifndef BUSTER_TIER0_ENTRIES_MEASURE_INCLUDED
#define BUSTER_TIER0_ENTRIES_MEASURE_INCLUDED

// The most inputs of a set the search tries every combination of, and the most key lookups one size's combinations may
// take for a kind; past either, a size's set is the size before's with the input that decides the most.
#define BUSTER_TIER0_CENSUS_EXHAUSTIVE 4
#define BUSTER_TIER0_CENSUS_PROBES UINT64_C(600000000)
// The bytes of #2874's gate: a key of this many or fewer, the kind among them.
#define BUSTER_TIER0_CENSUS_GATE_BYTES 8
// A pattern's forms kept for the listing.
#define BUSTER_TIER0_CENSUS_FORMS 8

typedef enum Tier0CensusInput
{
    TIER0_CENSUS_CLASS,
    TIER0_CENSUS_CLASS_1,
    TIER0_CENSUS_CLASS_2,
    TIER0_CENSUS_CLASS_3,
    TIER0_CENSUS_KIND_1,
    TIER0_CENSUS_KIND_2,
    TIER0_CENSUS_KIND_3,
    TIER0_CENSUS_LABELS,
    TIER0_CENSUS_VALUE,
    TIER0_CENSUS_VALUE_1,
    TIER0_CENSUS_VALUE_2,
    TIER0_CENSUS_VALUE_3,
    TIER0_CENSUS_NARROW,
    TIER0_CENSUS_NARROW_1,
    TIER0_CENSUS_NARROW_2,
    TIER0_CENSUS_NARROW_3,
    TIER0_CENSUS_SCALE,
    TIER0_CENSUS_SCALE_1,
    TIER0_CENSUS_SCALE_2,
    TIER0_CENSUS_FITS_1,
    TIER0_CENSUS_FITS_2,
    TIER0_CENSUS_PRODUCT,
    TIER0_CENSUS_SAME,
    TIER0_CENSUS_HOME,
    TIER0_CENSUS_HOME_1,
    TIER0_CENSUS_HOME_2,
    TIER0_CENSUS_HOME_3,
    TIER0_CENSUS_DEPTH,
    TIER0_CENSUS_FLOATING_DEPTH,
    TIER0_CENSUS_ACCESS,
    TIER0_CENSUS_LAST,
    TIER0_CENSUS_RESULT,
    TIER0_CENSUS_CALL_INTEGERS,
    TIER0_CENSUS_CALL_VECTORS,
    TIER0_CENSUS_CALL_STACK,
    TIER0_CENSUS_CALL_FLAGS,
    TIER0_CENSUS_CALL_RESULT,
    TIER0_CENSUS_CALL_NARROW,
    TIER0_CENSUS_SIZE,
    TIER0_CENSUS_FRAME,
    TIER0_CENSUS_CLASS_4,
    TIER0_CENSUS_CLASS_5,
    TIER0_CENSUS_KIND_4,
    TIER0_CENSUS_KIND_5,
    TIER0_CENSUS_VALUE_4,
    TIER0_CENSUS_NARROW_4,
    TIER0_CENSUS_HOME_4,
    TIER0_CENSUS_HOME_5,
    // The fusion's answers, which the raw count leaves out.
    TIER0_CENSUS_KEY_KIND,
    TIER0_CENSUS_KEY_CLASS,
    TIER0_CENSUS_VARIANT,
    TIER0_CENSUS_INPUTS,
} Tier0CensusInput;

// The inputs the raw count reads: every one but the fusion's.
#define BUSTER_TIER0_CENSUS_RAW ((1ull << TIER0_CENSUS_KEY_KIND) - 1)
#define BUSTER_TIER0_CENSUS_ALL ((1ull << TIER0_CENSUS_INPUTS) - 1)

static const char* const tier0_census_input_names[TIER0_CENSUS_INPUTS] = {
    [TIER0_CENSUS_CLASS] = "class",
    [TIER0_CENSUS_CLASS_1] = "class-1",
    [TIER0_CENSUS_CLASS_2] = "class-2",
    [TIER0_CENSUS_CLASS_3] = "class-3",
    [TIER0_CENSUS_KIND_1] = "kind-1",
    [TIER0_CENSUS_KIND_2] = "kind-2",
    [TIER0_CENSUS_KIND_3] = "kind-3",
    [TIER0_CENSUS_LABELS] = "labels",
    [TIER0_CENSUS_VALUE] = "value",
    [TIER0_CENSUS_VALUE_1] = "value-1",
    [TIER0_CENSUS_VALUE_2] = "value-2",
    [TIER0_CENSUS_VALUE_3] = "value-3",
    [TIER0_CENSUS_NARROW] = "narrow",
    [TIER0_CENSUS_NARROW_1] = "narrow-1",
    [TIER0_CENSUS_NARROW_2] = "narrow-2",
    [TIER0_CENSUS_NARROW_3] = "narrow-3",
    [TIER0_CENSUS_SCALE] = "scale",
    [TIER0_CENSUS_SCALE_1] = "scale-1",
    [TIER0_CENSUS_SCALE_2] = "scale-2",
    [TIER0_CENSUS_FITS_1] = "fits-1",
    [TIER0_CENSUS_FITS_2] = "fits-2",
    [TIER0_CENSUS_PRODUCT] = "product",
    [TIER0_CENSUS_SAME] = "same-local",
    [TIER0_CENSUS_HOME] = "home",
    [TIER0_CENSUS_HOME_1] = "home-1",
    [TIER0_CENSUS_HOME_2] = "home-2",
    [TIER0_CENSUS_HOME_3] = "home-3",
    [TIER0_CENSUS_DEPTH] = "depth",
    [TIER0_CENSUS_FLOATING_DEPTH] = "floating-depth",
    [TIER0_CENSUS_ACCESS] = "access",
    [TIER0_CENSUS_LAST] = "last",
    [TIER0_CENSUS_RESULT] = "result",
    [TIER0_CENSUS_CALL_INTEGERS] = "call-integers",
    [TIER0_CENSUS_CALL_VECTORS] = "call-vectors",
    [TIER0_CENSUS_CALL_STACK] = "call-stack",
    [TIER0_CENSUS_CALL_FLAGS] = "call-flags",
    [TIER0_CENSUS_CALL_RESULT] = "call-result",
    [TIER0_CENSUS_CALL_NARROW] = "call-narrow",
    [TIER0_CENSUS_SIZE] = "size",
    [TIER0_CENSUS_FRAME] = "frame",
    [TIER0_CENSUS_CLASS_4] = "class-4",
    [TIER0_CENSUS_CLASS_5] = "class-5",
    [TIER0_CENSUS_KIND_4] = "kind-4",
    [TIER0_CENSUS_KIND_5] = "kind-5",
    [TIER0_CENSUS_VALUE_4] = "value-4",
    [TIER0_CENSUS_NARROW_4] = "narrow-4",
    [TIER0_CENSUS_HOME_4] = "home-4",
    [TIER0_CENSUS_HOME_5] = "home-5",
    [TIER0_CENSUS_KEY_KIND] = "key-kind",
    [TIER0_CENSUS_KEY_CLASS] = "key-class",
    [TIER0_CENSUS_VARIANT] = "variant",
};

// What an input is of, for the totals: the head's own class; its neighbours' kinds and classes and the labels; a bit of
// a value; a home's case; a depth; a call's row; the function.
typedef enum Tier0CensusFamily
{
    TIER0_CENSUS_FAMILY_OWN,
    TIER0_CENSUS_FAMILY_NEIGHBOURS,
    TIER0_CENSUS_FAMILY_VALUE,
    TIER0_CENSUS_FAMILY_HOME,
    TIER0_CENSUS_FAMILY_DEPTH,
    TIER0_CENSUS_FAMILY_CALL,
    TIER0_CENSUS_FAMILY_FUNCTION,
    TIER0_CENSUS_FAMILY_FUSED,
    TIER0_CENSUS_FAMILIES,
} Tier0CensusFamily;

static const char* const tier0_census_family_names[TIER0_CENSUS_FAMILIES] = {
    [TIER0_CENSUS_FAMILY_OWN] = "the entry's class",
    [TIER0_CENSUS_FAMILY_NEIGHBOURS] = "the kinds, classes and labels of the entries before",
    [TIER0_CENSUS_FAMILY_VALUE] = "a bit of a value",
    [TIER0_CENSUS_FAMILY_HOME] = "a home's case",
    [TIER0_CENSUS_FAMILY_DEPTH] = "a depth",
    [TIER0_CENSUS_FAMILY_CALL] = "a call's counts and flags",
    [TIER0_CENSUS_FAMILY_FUNCTION] = "the function's (its symbols' access, its result, its frame, its end)",
    [TIER0_CENSUS_FAMILY_FUSED] = "the fusion's key (its kind, class and variant)",
};

static const uint8_t tier0_census_input_families[TIER0_CENSUS_INPUTS] = {
    [TIER0_CENSUS_CLASS] = TIER0_CENSUS_FAMILY_OWN,
    [TIER0_CENSUS_CLASS_1] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_CLASS_2] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_CLASS_3] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_KIND_1] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_KIND_2] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_KIND_3] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_LABELS] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_VALUE] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_VALUE_1] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_VALUE_2] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_VALUE_3] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_NARROW] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_NARROW_1] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_NARROW_2] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_NARROW_3] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_SCALE] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_SCALE_1] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_SCALE_2] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_FITS_1] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_FITS_2] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_PRODUCT] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_SAME] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_HOME] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_HOME_1] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_HOME_2] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_HOME_3] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_DEPTH] = TIER0_CENSUS_FAMILY_DEPTH,
    [TIER0_CENSUS_FLOATING_DEPTH] = TIER0_CENSUS_FAMILY_DEPTH,
    [TIER0_CENSUS_ACCESS] = TIER0_CENSUS_FAMILY_FUNCTION,
    [TIER0_CENSUS_LAST] = TIER0_CENSUS_FAMILY_FUNCTION,
    [TIER0_CENSUS_RESULT] = TIER0_CENSUS_FAMILY_FUNCTION,
    [TIER0_CENSUS_CALL_INTEGERS] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_CALL_VECTORS] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_CALL_STACK] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_CALL_FLAGS] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_CALL_RESULT] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_CALL_NARROW] = TIER0_CENSUS_FAMILY_CALL,
    [TIER0_CENSUS_SIZE] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_FRAME] = TIER0_CENSUS_FAMILY_FUNCTION,
    [TIER0_CENSUS_CLASS_4] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_CLASS_5] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_KIND_4] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_KIND_5] = TIER0_CENSUS_FAMILY_NEIGHBOURS,
    [TIER0_CENSUS_VALUE_4] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_NARROW_4] = TIER0_CENSUS_FAMILY_VALUE,
    [TIER0_CENSUS_HOME_4] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_HOME_5] = TIER0_CENSUS_FAMILY_HOME,
    [TIER0_CENSUS_KEY_KIND] = TIER0_CENSUS_FAMILY_FUSED,
    [TIER0_CENSUS_KEY_CLASS] = TIER0_CENSUS_FAMILY_FUSED,
    [TIER0_CENSUS_VARIANT] = TIER0_CENSUS_FAMILY_FUSED,
};

static const char* const tier0_census_kind_names[BUSTER_TYPED_KINDS] = {
    [BUSTER_TYPED_NONE] = "NONE",
    [BUSTER_TYPED_CONST] = "CONST",
    [BUSTER_TYPED_LOCAL] = "LOCAL",
    [BUSTER_TYPED_LOCAL_ADDRESS] = "LOCAL_ADDRESS",
    [BUSTER_TYPED_GLOBAL] = "GLOBAL",
    [BUSTER_TYPED_GLOBAL_ADDRESS] = "GLOBAL_ADDRESS",
    [BUSTER_TYPED_DUP] = "DUP",
    [BUSTER_TYPED_NEG] = "NEG",
    [BUSTER_TYPED_NOT] = "NOT",
    [BUSTER_TYPED_CONVERT] = "CONVERT",
    [BUSTER_TYPED_LOAD] = "LOAD",
    [BUSTER_TYPED_ADD] = "ADD",
    [BUSTER_TYPED_SUB] = "SUB",
    [BUSTER_TYPED_MUL] = "MUL",
    [BUSTER_TYPED_DIV] = "DIV",
    [BUSTER_TYPED_MOD] = "MOD",
    [BUSTER_TYPED_AND] = "AND",
    [BUSTER_TYPED_OR] = "OR",
    [BUSTER_TYPED_XOR] = "XOR",
    [BUSTER_TYPED_SHL] = "SHL",
    [BUSTER_TYPED_SHR] = "SHR",
    [BUSTER_TYPED_PTR_ADD] = "PTR_ADD",
    [BUSTER_TYPED_EQ] = "EQ",
    [BUSTER_TYPED_NE] = "NE",
    [BUSTER_TYPED_LT] = "LT",
    [BUSTER_TYPED_LE] = "LE",
    [BUSTER_TYPED_GT] = "GT",
    [BUSTER_TYPED_GE] = "GE",
    [BUSTER_TYPED_STORE] = "STORE",
    [BUSTER_TYPED_SET] = "SET",
    [BUSTER_TYPED_STORE_LOCAL] = "STORE_LOCAL",
    [BUSTER_TYPED_SET_LOCAL] = "SET_LOCAL",
    [BUSTER_TYPED_STORE_GLOBAL] = "STORE_GLOBAL",
    [BUSTER_TYPED_SET_GLOBAL] = "SET_GLOBAL",
    [BUSTER_TYPED_CALL] = "CALL",
    [BUSTER_TYPED_DISCARD] = "DISCARD",
    [BUSTER_TYPED_BRANCH_FALSE] = "BRANCH_FALSE",
    [BUSTER_TYPED_BRANCH_TRUE] = "BRANCH_TRUE",
    [BUSTER_TYPED_JUMP] = "JUMP",
    [BUSTER_TYPED_JUMP_VALUE] = "JUMP_VALUE",
    [BUSTER_TYPED_RETURN] = "RETURN",
    [BUSTER_TYPED_COPY] = "COPY",
    [BUSTER_TYPED_ALLOCA] = "ALLOCA",
    [BUSTER_TYPED_STACK_SAVE] = "STACK_SAVE",
    [BUSTER_TYPED_STACK_RESTORE] = "STACK_RESTORE",
    [BUSTER_TYPED_FRAME_ADDRESS] = "FRAME_ADDRESS",
    [BUSTER_TYPED_JUMP_INDIRECT] = "JUMP_INDIRECT",
    [BUSTER_TYPED_TAIL_CALL] = "TAIL_CALL",
    [BUSTER_TYPED_X86] = "X86",
    [BUSTER_TYPED_FENCE] = "FENCE",
    [BUSTER_TYPED_ATOMIC_RMW] = "ATOMIC_RMW",
    [BUSTER_TYPED_ATOMIC_CAS] = "ATOMIC_CAS",
};

// How a head's pattern was made: from its template; from its records, its slot a deferred one's; from its records, its
// template requiring another depth; from its records, the template not giving them (a deep entry's, a segment's or an
// x87 transfer's rewrite).
typedef enum Tier0CensusState
{
    TIER0_CENSUS_TEMPLATED,
    TIER0_CENSUS_DEFERRED_KIND,
    TIER0_CENSUS_DEFERRED_DEPTH,
    TIER0_CENSUS_OTHER,
    TIER0_CENSUS_STATES,
} Tier0CensusState;

static const char* const tier0_census_state_names[TIER0_CENSUS_STATES] = {
    [TIER0_CENSUS_TEMPLATED] = "a template",
    [TIER0_CENSUS_DEFERRED_KIND] = "deferred by its kind",
    [TIER0_CENSUS_DEFERRED_DEPTH] = "deferred by its depth",
    [TIER0_CENSUS_OTHER] = "not its template's records",
};

// What a conditional branch's group takes (#2877): its compare, right before it; its compare, behind the compare's test
// against zero (CONST 0 and NE or EQ: how the lowering writes a condition); the branch alone, the value on the stack.
typedef enum Tier0CensusCondition
{
    TIER0_CENSUS_CONDITION_COMPARE,
    TIER0_CENSUS_CONDITION_TESTED,
    TIER0_CENSUS_CONDITION_ALONE,
    TIER0_CENSUS_CONDITIONS,
} Tier0CensusCondition;

static const char* const tier0_census_condition_names[TIER0_CENSUS_CONDITIONS] = {
    [TIER0_CENSUS_CONDITION_COMPARE] = "the compare before it",
    [TIER0_CENSUS_CONDITION_TESTED] = "a compare behind its test against zero",
    [TIER0_CENSUS_CONDITION_ALONE] = "no entry",
};

// Why a compare's value is made and tested again before a branch (setcc al; movzx eax, al; test eax, eax; je or jne,
// what #2877 counted in the objects): the compare the branch takes is itself a test against zero of that value (a
// negation: two tests in a row, of which the fusion takes one); a jump's target is on the branch, its test or the
// test's constant, where another value can arrive; anything else.
typedef enum Tier0CensusAgain
{
    TIER0_CENSUS_AGAIN_TEST,
    TIER0_CENSUS_AGAIN_TARGET,
    TIER0_CENSUS_AGAIN_OTHER,
    TIER0_CENSUS_AGAINS,
} Tier0CensusAgain;

static const char* const tier0_census_again_names[TIER0_CENSUS_AGAINS] = {
    [TIER0_CENSUS_AGAIN_TEST] = "a test of a test (the compare the branch takes tests that value against zero)",
    [TIER0_CENSUS_AGAIN_TARGET] = "a jump's target on the branch, its test or the constant",
    [TIER0_CENSUS_AGAIN_OTHER] = "other",
};

typedef struct Tier0CensusSample Tier0CensusSample;
struct Tier0CensusSample
{
    uint8_t inputs[TIER0_CENSUS_INPUTS];
    uint8_t kind;
    uint8_t state;
    // The group's entries: the head and what it absorbs; in the count's distinct samples, every such group's.
    uint32_t weight;
    uint32_t pattern;
    uint32_t records;
};

typedef struct Tier0CensusPattern Tier0CensusPattern;
struct Tier0CensusPattern
{
    uint64_t hash;
    uint64_t entries;
    uint32_t heads;
    // The first head's records' forms, for the listing.
    uint16_t forms[BUSTER_TIER0_CENSUS_FORMS];
    uint32_t records;
    uint8_t kind;
    uint8_t state;
};

// The population: every group of every file's functions, and the patterns by their hashes (an open table of indices
// into `patterns`, 0 for an empty slot, an index plus one otherwise).
typedef struct Tier0Census Tier0Census;
struct Tier0Census
{
    // The samples and the patterns, each an array that grows by a push at its arena's end, and the patterns' table.
    Arena sample_arena;
    Arena pattern_arena;
    Arena slot_arena;
    Tier0CensusSample* samples;
    uint64_t sample_count;
    Tier0CensusPattern* patterns;
    uint32_t pattern_count;
    uint32_t* pattern_slots;
    uint32_t pattern_capacity;
    uint64_t files;
    uint64_t left_out;
    uint64_t failures;
    uint64_t functions;
    // Functions tier0_compile refuses now, which the emission compiled: none is expected.
    uint64_t refused;
    uint64_t entries;
    uint64_t heads;
    uint64_t records;
    // The prologues' and epilogues' records, which no entry makes.
    uint64_t frame_records;
    uint64_t branches;
    uint64_t references;
    uint64_t state_heads[TIER0_CENSUS_STATES];
    uint64_t state_entries[TIER0_CENSUS_STATES];
    uint64_t state_records[TIER0_CENSUS_STATES];
    // Templated heads whose slot's template depends on the homes, and those with a record the immediates' choice takes.
    uint64_t home_entries;
    uint64_t choice_entries;
    uint64_t chosen_entries;
    // The conditional branches' groups by what they take, and the sites where a compare's value is made and tested
    // again, by why.
    uint64_t conditions[TIER0_CENSUS_CONDITIONS];
    uint64_t tested_again[TIER0_CENSUS_AGAINS];
    // Functions with deep values, and with each of the rare classes' flags.
    uint64_t deep_functions;
    uint64_t special_functions;
    bool memory;
};

static uint64_t tier0_census_mix(uint64_t hash, uint64_t value)
{
    const uint64_t mixed = (hash ^ value) * UINT64_C(0x9e3779b97f4a7c15);
    return mixed ^ (mixed >> 32);
}

// A value's class by magnitude, as a signed 64-bit number: what an immediate's form asks of it.
static uint8_t tier0_census_value_class(int64_t value)
{
    const uint8_t result = value == 0 ? 0 : value == 1 ? 1 : value == -1 ? 2 : value >= 2 && value <= 127 ? 3 : value == 128 ? 4 :
                           value >= 129 && value <= 255 ? 5 : value >= -128 && value <= -2 ? 6 : value >= 256 && value <= INT32_MAX ? 7 :
                           value > INT32_MAX && value <= UINT32_MAX ? 8 : value < -128 && value >= INT32_MIN ? 9 : 10;
    return result;
}

// A value's class by magnitude in the bytes of `class` (sign-extended from them): a constant of U32 whose bits are all
// ones is minus one to a 32-bit operation.
static uint8_t tier0_census_narrow_class(uint64_t value, uint32_t class)
{
    const uint32_t bytes = tier0_class_integer(class) ? tier0_class_bytes(class) : 8;
    const uint32_t shift = bytes >= 8 ? 0 : 64 - 8 * bytes;
    return tier0_census_value_class((int64_t)(value << shift) >> shift);
}

static uint8_t tier0_census_scale(uint64_t size)
{
    return size == 1 ? 1 : size == 2 ? 2 : size == 4 ? 3 : size == 8 ? 4 : 0;
}

// The home's case of the local entry j names, 0 for an entry that names none: a general register, an XMM one, a value's
// slot, an object.
static uint8_t tier0_census_home(const Tier0Function* function, const Tier0Home* homes, uint32_t local_count, uint32_t j)
{
    const uint8_t kind = function->kinds[j];
    const bool names = kind == BUSTER_TYPED_LOCAL || kind == BUSTER_TYPED_LOCAL_ADDRESS || kind == BUSTER_TYPED_STORE_LOCAL || kind == BUSTER_TYPED_SET_LOCAL;
    uint8_t result = 0;
    if (names && function->values[j] < local_count)
    {
        const uint64_t local = function->values[j];
        const bool floating = tier0_class_floating(function->local_classes[local]);
        result = homes[local].is_register != 0 ? (floating ? 2 : 1) : tier0_is_object(function, local) ? 4 : 3;
    }
    return result;
}

// A register's role in a pattern made from records: a general register by its place in the integer depth table from the
// head's depth (the stack's and the frame's registers by name), a vector register by its number from the floating
// depth, any other by its number.
static uint32_t tier0_census_role(uint32_t reg, uint32_t operand, int32_t depth, int32_t floating_depth, bool pointer, bool based)
{
    const uint32_t class = operand & BUSTER_X86_OPERAND_CLASS;
    uint32_t role = 0x1000 | reg;
    if (class >= BUSTER_X86_CLASS_GPR8 && class <= BUSTER_X86_CLASS_GPR64 && reg < 16)
    {
        int32_t place = -1;
        for (int32_t d = 0; d < BUSTER_TIER0_INTEGER_DEPTHS; d += 1)
        {
            place = tier0_integer_depths[d] == reg ? d : place;
        }
        const bool frame = reg == X86_REGISTER_RSP || (pointer && reg == X86_REGISTER_RBP) || (based && reg == X86_REGISTER_R15);
        role = frame || place < 0 ? 0x100 | reg : 0x200 | (uint32_t)(place - depth + 64);
    }
    else if (class >= BUSTER_X86_CLASS_XMM && class <= BUSTER_X86_CLASS_ZMM && reg < 32)
    {
        role = 0x400 | (uint32_t)((int32_t)reg - floating_depth + 64);
    }
    return role;
}

// Whether a general register is one a frame's places are addressed from.
static bool tier0_census_frame_register(uint32_t reg, bool pointer, bool based)
{
    return reg == X86_REGISTER_RSP || (pointer && reg == X86_REGISTER_RBP) || (based && reg == X86_REGISTER_R15);
}

// The pattern of `count` records as they are (tier0_census's second way): each record's form, mode and decorations, its
// registers by role, its immediate, and its displacement but a frame's, which is a place's (a home, a save, an outgoing
// argument) and carried as a template's is.
static uint64_t tier0_census_records_pattern(const X86Instruction* records, uint32_t count, const LinkReference* references, uint32_t reference_count,
                                             uint32_t first_record, int32_t depth, int32_t floating_depth, bool pointer, bool based)
{
    uint64_t hash = tier0_census_mix(0x7469657230ull, count);
    uint32_t reference = 0;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const X86Instruction* record = &records[r];
        const X86FormDescription* description = x86_form_description(record->form);
        const uint8_t mode = record->mode;
        const bool memory = (mode & BUSTER_X86_MODE_MEMORY) != 0;
        const bool base = memory && (mode & BUSTER_X86_MODE_BASE) != 0;
        const bool index = memory && (mode & BUSTER_X86_MODE_INDEX) != 0;
        const bool frame = base && tier0_census_frame_register(record->rm, pointer, based);
        const uint32_t general = BUSTER_X86_CLASS_GPR64;
        const uint32_t rm_role = !memory ? tier0_census_role(record->rm, description->rm, depth, floating_depth, pointer, based) :
                                 base ? tier0_census_role(record->rm, general, depth, floating_depth, pointer, based) : 0;
        const uint32_t index_class = (description->rm & BUSTER_X86_OPERAND_VSIB) != 0 ? description->rm : general;
        const uint32_t index_role = index ? tier0_census_role(record->index, index_class, depth, floating_depth, pointer, based) : 0;
        const uint32_t reg_role = (description->reg & BUSTER_X86_OPERAND_CLASS) != 0 ? tier0_census_role(record->reg, description->reg, depth, floating_depth, pointer, based) : record->reg;
        const uint32_t vvvv_role = (description->vvvv & BUSTER_X86_OPERAND_CLASS) != 0 ? tier0_census_role(record->vvvv, description->vvvv, depth, floating_depth, pointer, based) : record->vvvv;
        const bool fixup = (record->form & BUSTER_X86_FORM_FIXUP) != 0;
        // A marked record's references, in the code's order: their kinds and fields are the pattern's, their symbols carried.
        uint64_t marks = 0;
        while (reference < reference_count && references[reference].instruction < first_record + r)
        {
            reference += 1;
        }
        while (fixup && reference < reference_count && references[reference].instruction == first_record + r)
        {
            marks = tier0_census_mix(marks, (uint64_t)references[reference].kind << 8 | references[reference].field);
            reference += 1;
        }
        // A marked record's field is its symbol's; a branch's displacement is sized later.
        const int64_t displacement = fixup || !memory || frame ? 0 : record->displacement;
        hash = tier0_census_mix(hash, (uint64_t)record->form << 16 | (uint64_t)mode << 8 | record->decorations);
        hash = tier0_census_mix(hash, (uint64_t)reg_role << 48 | (uint64_t)rm_role << 32 | (uint64_t)index_role << 16 | vvvv_role);
        hash = tier0_census_mix(hash, (uint64_t)(uint32_t)record->immediate << 32 | (uint64_t)(uint32_t)displacement);
        hash = tier0_census_mix(hash, marks);
    }
    return hash;
}

// The pattern of a template with the forms its records have after the immediates' choice: what the template fixes of
// each record, where each hole comes from, and the entries its constant and homes are read from.
static uint64_t tier0_census_template_pattern(const Tier0Template* template, const X86Instruction* records)
{
    uint64_t hash = tier0_census_mix(0x74656d706c617465ull, template->length);
    hash = tier0_census_mix(hash, (uint64_t)template->k_offset << 24 | (uint64_t)template->k_scale << 16 | (uint64_t)template->a_offset << 8 | template->b_offset);
    for (uint32_t r = 0; r < template->length; r += 1)
    {
        const Tier0RecordSpec* spec = &template->specs[r];
        const uint8_t roles = spec->flags & (BUSTER_TIER0_RECORD_BRANCH | BUSTER_TIER0_RECORD_END | BUSTER_TIER0_RECORD_FIXUP);
        hash = tier0_census_mix(hash, (uint64_t)records[r].form << 32 | (uint64_t)spec->reg << 24 | (uint64_t)spec->rm_kind << 16 | (uint64_t)spec->rm << 8 | spec->index);
        hash = tier0_census_mix(hash, (uint64_t)spec->scale << 32 | (uint64_t)spec->immediate_kind << 24 | (uint64_t)roles << 16 | (uint64_t)spec->fixup_kind << 8 | spec->fixup_field);
        hash = tier0_census_mix(hash, (uint64_t)(uint32_t)(spec->immediate_kind == BUSTER_TIER0_IMMEDIATE_STATIC ? spec->immediate : 0) << 32 | (uint32_t)spec->displacement);
    }
    return hash;
}

// The pattern's index, added if it is new: UINT32_MAX without room.
static uint32_t tier0_census_pattern(Tier0Census* census, uint64_t hash, const X86Instruction* records, uint32_t count, uint8_t kind, uint8_t state)
{
    uint32_t result = UINT32_MAX;
    if ((uint64_t)(census->pattern_count + 1) * 2 > census->pattern_capacity)
    {
        // The table again at twice its slots: the old one's bytes are left behind in its arena.
        const uint32_t capacity = census->pattern_capacity == 0 ? 1u << 14 : census->pattern_capacity * 2;
        uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(&census->slot_arena, uint32_t, capacity);
        if (slots != 0)
        {
            memset(slots, 0, (uint64_t)capacity * sizeof(uint32_t));
            for (uint32_t p = 0; p < census->pattern_count; p += 1)
            {
                uint32_t at = (uint32_t)(census->patterns[p].hash & (capacity - 1));
                while (slots[at] != 0)
                {
                    at = (at + 1) & (capacity - 1);
                }
                slots[at] = p + 1;
            }
            census->pattern_slots = slots;
            census->pattern_capacity = capacity;
        }
        else
        {
            census->memory = true;
        }
    }
    if (!census->memory)
    {
        const uint32_t capacity = census->pattern_capacity;
        uint32_t at = (uint32_t)(hash & (capacity - 1));
        while (census->pattern_slots[at] != 0 && census->patterns[census->pattern_slots[at] - 1].hash != hash)
        {
            at = (at + 1) & (capacity - 1);
        }
        if (census->pattern_slots[at] != 0)
        {
            result = census->pattern_slots[at] - 1;
        }
        else
        {
            // The patterns are one array: nothing else is pushed to their arena, so the next one is the array's next.
            Tier0CensusPattern* pattern = BUSTER_ARENA_PUSH_ARRAY(&census->pattern_arena, Tier0CensusPattern, 1);
            if (pattern != 0)
            {
                memset(pattern, 0, sizeof(*pattern));
                pattern->hash = hash;
                pattern->records = count;
                pattern->kind = kind;
                pattern->state = state;
                for (uint32_t r = 0; r < count && r < BUSTER_TIER0_CENSUS_FORMS; r += 1)
                {
                    pattern->forms[r] = records[r].form;
                }
                census->patterns = census->pattern_count == 0 ? pattern : census->patterns;
                result = census->pattern_count;
                census->pattern_slots[at] = result + 1;
                census->pattern_count += 1;
            }
            else
            {
                census->memory = true;
            }
        }
    }
    return result;
}

// The arenas' room: the samples', the patterns' and their table's.
#define BUSTER_TIER0_CENSUS_SAMPLE_BYTES (UINT64_C(1) << 34)
#define BUSTER_TIER0_CENSUS_PATTERN_BYTES (UINT64_C(1) << 32)

static bool tier0_census_begin(Tier0Census* census)
{
    memset(census, 0, sizeof(*census));
    census->sample_arena = arena_reserve(BUSTER_TIER0_CENSUS_SAMPLE_BYTES);
    census->pattern_arena = arena_reserve(BUSTER_TIER0_CENSUS_PATTERN_BYTES);
    census->slot_arena = arena_reserve(BUSTER_TIER0_CENSUS_PATTERN_BYTES);
    return census->sample_arena.base != 0 && census->pattern_arena.base != 0 && census->slot_arena.base != 0;
}

static void tier0_census_end(Tier0Census* census)
{
    arena_release(&census->slot_arena);
    arena_release(&census->pattern_arena);
    arena_release(&census->sample_arena);
}

// A function's groups, added to the census: its code by tier0_compile into `scratch` (popped again), its fusion's
// columns by tier0_fuse_entries. False without room; a function Tier 0 refuses is counted and left out.
static bool tier0_census_function(Tier0Census* census, const Tier0Function* function, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = function->count;
    Tier0Function asked = *function;
    asked.entry_records = true;
    asked.choice_requests = false;
    const Tier0Code code = tier0_compile(&asked, scratch);
    const uint64_t padded = (uint64_t)count + 64;
    Tier0Entries entries;
    memset(&entries, 0, sizeof(entries));
    entries.key_kinds = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries.key_classes = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries.variants = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries.absorbed = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries.integer_effects = BUSTER_ARENA_PUSH_ARRAY(scratch, int8_t, padded);
    entries.floating_effects = BUSTER_ARENA_PUSH_ARRAY(scratch, int8_t, padded);
    entries.depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    entries.floating_depths = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, padded);
    uint64_t* labels = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count / 64 + 2);
    bool room = entries.key_kinds != 0 && entries.key_classes != 0 && entries.variants != 0 && entries.absorbed != 0 && entries.integer_effects != 0 &&
                entries.floating_effects != 0 && entries.depths != 0 && entries.floating_depths != 0 && labels != 0 && code.error != TIER0_ERROR_MEMORY;
    census->refused += room && code.error != TIER0_OK ? 1 : 0;
    // A function with segment overrides is compiled from a stripped copy of its values, which only its compile makes.
    const bool special = function->segments;
    census->special_functions += room && code.error == TIER0_OK && special ? 1 : 0;
    if (room && code.error == TIER0_OK && !special && code.entry_records != 0 && (code.local_homes != 0 || function->local_count == 0))
    {
        const uint8_t* kinds = function->kinds;
        const uint8_t* classes = function->classes;
        const uint64_t* values = function->values;
        const Tier0Home* homes = code.local_homes;
        const uint32_t local_count = function->local_count;
        const uint32_t* entry_records = code.entry_records;
        // The labels (tier0_labels's), the fusion and the depths (tier0_depths_scalar's), as both paths make them.
        memset(labels, 0, ((uint64_t)count / 64 + 2) * sizeof(uint64_t));
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (kinds[i] >= BUSTER_TYPED_BRANCH_FALSE && kinds[i] <= BUSTER_TYPED_JUMP_VALUE)
            {
                const uint64_t target = values[i];
                labels[target >> 6] |= 1ull << (target & 63);
            }
        }
        for (uint32_t k = 0; k < function->label_count; k += 1)
        {
            const uint32_t target = function->label_entries[k];
            labels[target >> 6] |= 1ull << (target & 63);
        }
        const bool columned = function->symbol_access != 0;
        tier0_fuse_entries(function, labels, &entries, columned ? function->symbol_access : 0, columned ? function->symbol_count : 0);
        if (function->vectors)
        {
            tier0_operation_effects(function, &entries);
        }
        int32_t integer = 0;
        int32_t floating = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            entries.depths[i] = (uint8_t)integer;
            entries.floating_depths[i] = (uint8_t)floating;
            integer += entries.integer_effects[i];
            floating += entries.floating_effects[i];
        }
        census->functions += 1;
        census->entries += count;
        census->records += code.count;
        census->frame_records += count != 0 ? entry_records[0] + (code.count - entry_records[count]) : code.count;
        census->branches += code.branch_count;
        census->references += code.reference_count;
        census->deep_functions += code.deep != 0 ? 1 : 0;
        // The frame's kind and what of the function its deferred entries' records read: a frame pointer, a realigned
        // frame and its base, a variadic function's save area, a setjmp caller's snapshots, and the rare classes' slots.
        const uint8_t frame = (uint8_t)((code.frame_pointer ? 1 : 0) | (code.frame_alignment_log2 != 0 ? 2 : 0) | (code.frame_base ? 4 : 0) |
                                        (function->incoming_variadic != 0 ? 8 : 0) | (function->returns_twice ? 16 : 0) | (function->x87 || function->int128 ? 32 : 0) |
                                        (function->vectors ? 64 : 0) | (function->masks ? 128 : 0));
        const uint8_t result = (uint8_t)(function->result.kind | function->result.flags << 4);
        // The conditional branches by what their group takes.
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (kinds[i] == BUSTER_TYPED_BRANCH_FALSE || kinds[i] == BUSTER_TYPED_BRANCH_TRUE)
            {
                const uint8_t variant = entries.variants[i];
                const bool compared = entries.key_kinds[i] >= BUSTER_TYPED_EQ && entries.key_kinds[i] <= BUSTER_TYPED_GE;
                const bool tested = compared && ((variant >= BUSTER_TIER0_VARIANT_TESTED_FALSE && variant <= BUSTER_TIER0_VARIANT_TESTED_FALSE + BUSTER_TIER0_VARIANT_HOME_HOME) ||
                                                 (variant >= BUSTER_TIER0_VARIANT_TESTED_TRUE && variant <= BUSTER_TIER0_VARIANT_TESTED_TRUE + BUSTER_TIER0_VARIANT_HOME_HOME));
                census->conditions[tested ? TIER0_CENSUS_CONDITION_TESTED : compared ? TIER0_CENSUS_CONDITION_COMPARE : TIER0_CENSUS_CONDITION_ALONE] += 1;
            }
        }
        // A compare's value made and tested again, in the records as an object's code has them: each site is given to
        // the entry whose records hold the test, and its reason read from the stream there.
        uint32_t owner = 0;
        for (uint32_t r = 2; r + 1 < code.count; r += 1)
        {
            const X86Instruction* set = &code.records[r - 2];
            const X86Instruction* extend = &code.records[r - 1];
            const X86Instruction* test = &code.records[r];
            const uint32_t jump = code.records[r + 1].form & BUSTER_X86_FORM_NUMBER;
            bool sets = false;
            for (uint32_t cc = 0; cc < 16; cc += 1)
            {
                sets = sets || (set->form & BUSTER_X86_FORM_NUMBER) == tier0_setcc[cc];
            }
            const bool again = sets && set->mode == BUSTER_X86_REGISTER_DIRECT && set->rm == X86_REGISTER_RAX && (extend->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_MOVZX_R32_RM8 &&
                               extend->mode == BUSTER_X86_REGISTER_DIRECT && extend->reg == X86_REGISTER_RAX && extend->rm == X86_REGISTER_RAX &&
                               (test->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_TEST_RM32_R32 && test->mode == BUSTER_X86_REGISTER_DIRECT && test->reg == X86_REGISTER_RAX &&
                               test->rm == X86_REGISTER_RAX && (jump == X86_FORM_JE_REL8 || jump == X86_FORM_JNE_REL8);
            if (again)
            {
                while (owner + 1 < count && entry_records[owner + 1] <= r)
                {
                    owner += 1;
                }
                const uint32_t i = owner;
                const bool branch = kinds[i] == BUSTER_TYPED_BRANCH_FALSE || kinds[i] == BUSTER_TYPED_BRANCH_TRUE;
                const uint8_t variant = entries.variants[i];
                const bool tests = branch && i >= 5 && (entries.key_kinds[i] == BUSTER_TYPED_EQ || entries.key_kinds[i] == BUSTER_TYPED_NE) &&
                                   (variant == BUSTER_TIER0_VARIANT_TESTED_FALSE + BUSTER_TIER0_VARIANT_CONST || variant == BUSTER_TIER0_VARIANT_TESTED_TRUE + BUSTER_TIER0_VARIANT_CONST) &&
                                   values[i - 4] == 0 && kinds[i - 5] >= BUSTER_TYPED_EQ && kinds[i - 5] <= BUSTER_TYPED_GE;
                const bool target = branch && (tier0_label(labels, i) || (i >= 1 && tier0_label(labels, i - 1)) || (i >= 2 && tier0_label(labels, i - 2)));
                census->tested_again[tests ? TIER0_CENSUS_AGAIN_TEST : target ? TIER0_CENSUS_AGAIN_TARGET : TIER0_CENSUS_AGAIN_OTHER] += 1;
            }
        }
        uint32_t absorbed = 0;
        for (uint32_t i = 0; room && i < count; i += 1)
        {
            if (entries.absorbed[i])
            {
                absorbed += 1;
                continue;
            }
            const uint8_t kind = kinds[i];
            const uint8_t class = classes[i];
            const uint32_t first = entry_records[i];
            const uint32_t length = entry_records[i + 1] - first;
            const X86Instruction* records = code.records + first;
            // The head's template, as selection finds it.
            const uint16_t slot = tier0_slot(entries.key_kinds[i], entries.key_classes[i], entries.variants[i]);
            uint8_t state = TIER0_CENSUS_DEFERRED_KIND;
            uint64_t hash = 0;
            if (slot != BUSTER_TIER0_SLOT_DEFERRED && slot != BUSTER_TIER0_SLOT_UNSUPPORTED)
            {
                const Tier0TemplateInfo* info = &tier0_template_infos[slot & BUSTER_TIER0_SLOT_TEMPLATE];
                state = info->required_depth != BUSTER_TIER0_ANY_DEPTH && info->required_depth != entries.depths[i] ? TIER0_CENSUS_DEFERRED_DEPTH : TIER0_CENSUS_TEMPLATED;
            }
            if (state == TIER0_CENSUS_TEMPLATED)
            {
                const uint32_t index = tier0_home_case(function, homes, i, slot);
                const Tier0Template* template = &tier0_templates[index];
                bool same = template->length == length;
                bool chosen = false;
                for (uint32_t r = 0; same && r < length; r += 1)
                {
                    const bool choice = (template->specs[r].flags & BUSTER_TIER0_RECORD_CHOICE) != 0;
                    const bool kept = (uint32_t)(records[r].form & BUSTER_X86_FORM_NUMBER) == template->specs[r].form;
                    same = choice || kept;
                    chosen = chosen || (choice && !kept);
                }
                state = same ? TIER0_CENSUS_TEMPLATED : TIER0_CENSUS_OTHER;
                if (same)
                {
                    hash = tier0_census_template_pattern(template, records);
                    census->home_entries += (slot & BUSTER_TIER0_SLOT_HOME_DEPENDENT) != 0 ? absorbed + 1 : 0;
                    census->choice_entries += tier0_template_infos[index].choice != 0xff ? absorbed + 1 : 0;
                    census->chosen_entries += chosen ? absorbed + 1 : 0;
                }
            }
            if (state != TIER0_CENSUS_TEMPLATED)
            {
                hash = tier0_census_records_pattern(records, length, code.references, code.reference_count, first, entries.depths[i], entries.floating_depths[i],
                                                    code.frame_pointer, code.frame_base);
            }
            const uint32_t pattern = tier0_census_pattern(census, hash, records, length, kind, state);
            // The samples are one array, as the patterns are.
            Tier0CensusSample* sample = pattern != UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(&census->sample_arena, Tier0CensusSample, 1) : 0;
            room = sample != 0;
            if (room)
            {
                census->samples = census->sample_count == 0 ? sample : census->samples;
                memset(sample, 0, sizeof(*sample));
                uint8_t* inputs = sample->inputs;
                inputs[TIER0_CENSUS_CLASS] = class;
                uint8_t marks = tier0_label(labels, i) ? 1 : 0;
                for (uint32_t back = 1; back <= 3 && back <= i; back += 1)
                {
                    const uint32_t j = i - back;
                    inputs[TIER0_CENSUS_CLASS_1 + back - 1] = classes[j];
                    inputs[TIER0_CENSUS_KIND_1 + back - 1] = kinds[j];
                    inputs[TIER0_CENSUS_VALUE + back] = tier0_census_value_class((int64_t)values[j]);
                    inputs[TIER0_CENSUS_NARROW + back] = tier0_census_narrow_class(values[j], classes[j]);
                    inputs[TIER0_CENSUS_HOME + back] = tier0_census_home(function, homes, local_count, j);
                    marks |= tier0_label(labels, j) ? (uint8_t)(1u << back) : 0;
                }
                // A branch after a compare's test against zero takes the compare with it (#2877), and the compare its
                // operands: the group reaches five entries back. Where the stream has that shape, and nowhere else, the
                // two entries before the compare are inputs too.
                const bool tested = (kind == BUSTER_TYPED_BRANCH_FALSE || kind == BUSTER_TYPED_BRANCH_TRUE) && i >= 3 &&
                                    (kinds[i - 1] == BUSTER_TYPED_NE || kinds[i - 1] == BUSTER_TYPED_EQ) && kinds[i - 2] == BUSTER_TYPED_CONST &&
                                    kinds[i - 3] >= BUSTER_TYPED_EQ && kinds[i - 3] <= BUSTER_TYPED_GE;
                for (uint32_t back = 4; tested && back <= 5 && back <= i; back += 1)
                {
                    const uint32_t j = i - back;
                    inputs[TIER0_CENSUS_CLASS_4 + back - 4] = classes[j];
                    inputs[TIER0_CENSUS_KIND_4 + back - 4] = kinds[j];
                    inputs[TIER0_CENSUS_HOME_4 + back - 4] = tier0_census_home(function, homes, local_count, j);
                    marks |= tier0_label(labels, j) ? (uint8_t)(1u << back) : 0;
                }
                inputs[TIER0_CENSUS_VALUE_4] = tested && i >= 4 ? tier0_census_value_class((int64_t)values[i - 4]) : 0;
                inputs[TIER0_CENSUS_NARROW_4] = tested && i >= 4 ? tier0_census_narrow_class(values[i - 4], classes[i - 4]) : 0;
                inputs[TIER0_CENSUS_LABELS] = marks;
                inputs[TIER0_CENSUS_VALUE] = tier0_census_value_class((int64_t)values[i]);
                inputs[TIER0_CENSUS_NARROW] = tier0_census_narrow_class(values[i], class);
                inputs[TIER0_CENSUS_HOME] = tier0_census_home(function, homes, local_count, i);
                inputs[TIER0_CENSUS_SCALE] = kind == BUSTER_TYPED_PTR_ADD ? tier0_census_scale(values[i]) : 0;
                inputs[TIER0_CENSUS_SCALE_1] = i >= 1 && kinds[i - 1] == BUSTER_TYPED_PTR_ADD ? tier0_census_scale(values[i - 1]) : 0;
                inputs[TIER0_CENSUS_SCALE_2] = i >= 2 && kinds[i - 2] == BUSTER_TYPED_PTR_ADD ? tier0_census_scale(values[i - 2]) : 0;
                // Whether the constant before the head fits it (tier0_constant_fits, the head's key class and its own
                // element size), 2 where there is none; whether the constant before a pointer addition one or two
                // back fits that addition's product; and the class of the nearest such product's magnitude plus one
                // (the head's own first), 0 where there is none: `p + 3` and `p + 20` of 8-byte elements have the same
                // constant's class and the same size, and their products take an 8-bit and a 32-bit immediate.
                const uint32_t key_class = tier0_key_class(kind, class, i >= 1 ? classes[i - 1] : BUSTER_TYPED_CLASS_VOID);
                inputs[TIER0_CENSUS_FITS_1] = i >= 1 && kinds[i - 1] == BUSTER_TYPED_CONST ?
                                                  (tier0_constant_fits(kind, key_class, values[i - 1], kind == BUSTER_TYPED_PTR_ADD ? values[i] : 1) ? 1 : 0) : 2;
                uint8_t product = 2;
                uint8_t product_class = kind == BUSTER_TYPED_PTR_ADD && i >= 1 && kinds[i - 1] == BUSTER_TYPED_CONST ?
                                            (uint8_t)(1 + tier0_census_value_class((int64_t)(values[i - 1] * values[i]))) : 0;
                for (uint32_t back = 1; back <= 2 && back + 1 <= i && product == 2; back += 1)
                {
                    const uint32_t j = i - back;
                    if (kinds[j] == BUSTER_TYPED_PTR_ADD && kinds[j - 1] == BUSTER_TYPED_CONST)
                    {
                        product = tier0_constant_fits(BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64, values[j - 1], values[j]) ? 1 : 0;
                        product_class = product_class == 0 ? (uint8_t)(1 + tier0_census_value_class((int64_t)(values[j - 1] * values[j]))) : product_class;
                    }
                }
                inputs[TIER0_CENSUS_FITS_2] = product;
                inputs[TIER0_CENSUS_PRODUCT] = product_class;
                inputs[TIER0_CENSUS_SAME] = i >= 3 && kinds[i - 3] == BUSTER_TYPED_LOCAL && values[i - 3] == values[i] ? 1 : 0;
                inputs[TIER0_CENSUS_DEPTH] = entries.depths[i];
                inputs[TIER0_CENSUS_FLOATING_DEPTH] = entries.floating_depths[i];
                inputs[TIER0_CENSUS_ACCESS] = tier0_global_kind(kind) && columned && values[i] < function->symbol_count ? (uint8_t)(1 + function->symbol_access[values[i]]) : 0;
                inputs[TIER0_CENSUS_LAST] = i + 1 == count ? 1 : 0;
                inputs[TIER0_CENSUS_RESULT] = kind == BUSTER_TYPED_RETURN ? result : 0;
                if ((kind == BUSTER_TYPED_CALL || kind == BUSTER_TYPED_TAIL_CALL) && values[i] < function->call_count)
                {
                    const uint32_t call = (uint32_t)values[i];
                    const uint32_t first_argument = function->call_first[call];
                    bool aggregates = false;
                    for (uint32_t a = 0; function->argument_sizes != 0 && a < function->call_argument_counts[call]; a += 1)
                    {
                        aggregates = aggregates || function->argument_sizes[first_argument + a] != 0;
                    }
                    const uint32_t stack = function->calls[call].stack_bytes / 8;
                    inputs[TIER0_CENSUS_CALL_INTEGERS] = function->calls[call].integer_registers;
                    inputs[TIER0_CENSUS_CALL_VECTORS] = function->calls[call].vector_registers;
                    inputs[TIER0_CENSUS_CALL_STACK] = (uint8_t)(stack < 255 ? stack : 255);
                    inputs[TIER0_CENSUS_CALL_FLAGS] = (uint8_t)((function->call_variadic[call] != 0 ? 1 : 0) | (function->call_targets[call] == 0 ? 2 : 0) | (aggregates ? 4 : 0) |
                                                                (function->call_result_objects != 0 && function->call_result_objects[call] != 0 ? 8 : 0) |
                                                                (function->call_asm != 0 && function->call_asm[call] != 0 ? 16 : 0));
                    inputs[TIER0_CENSUS_CALL_RESULT] = (uint8_t)(function->calls[call].result.kind | function->calls[call].result.flags << 4);
                    // The arguments the call extends in their registers (tier0_extend): its first eight's narrow integers.
                    uint8_t narrow = 0;
                    for (uint32_t a = 0; a < function->call_argument_counts[call] && a < 8; a += 1)
                    {
                        const uint32_t argument = function->argument_classes[first_argument + a];
                        narrow |= tier0_class_integer(argument) && tier0_class_bytes(argument) < 4 ? (uint8_t)(1u << a) : 0;
                    }
                    inputs[TIER0_CENSUS_CALL_NARROW] = narrow;
                }
                inputs[TIER0_CENSUS_SIZE] = kind == BUSTER_TYPED_COPY ? (uint8_t)(values[i] - 1) : 0;
                inputs[TIER0_CENSUS_FRAME] = frame;
                inputs[TIER0_CENSUS_KEY_KIND] = entries.key_kinds[i];
                inputs[TIER0_CENSUS_KEY_CLASS] = entries.key_classes[i];
                inputs[TIER0_CENSUS_VARIANT] = entries.variants[i];
                sample->kind = kind;
                sample->weight = absorbed + 1;
                sample->state = state;
                sample->pattern = pattern;
                sample->records = length;
                census->sample_count += 1;
                census->heads += 1;
                census->state_heads[state] += 1;
                census->state_entries[state] += absorbed + 1;
                census->state_records[state] += length;
                census->patterns[pattern].heads += 1;
                census->patterns[pattern].entries += absorbed + 1;
            }
            absorbed = 0;
        }
    }
    arena_pop_to(scratch, mark);
    census->memory = census->memory || !room;
    return room;
}

// ---------------------------------------------------------------------------------------------------------------------
// The count

// The search's two tables, open ones, cleared by a new epoch: a key of a set of inputs with its entries and the most any
// one pattern has of them, and a key with a pattern.
typedef struct Tier0CensusCell Tier0CensusCell;
struct Tier0CensusCell
{
    uint64_t hash;
    uint64_t entries;
    uint64_t most;
    uint32_t epoch;
    // The first index a distinct sample took (tier0_census_distinct's use of the table).
    uint32_t index;
    bool conflict;
};

typedef struct Tier0CensusPair Tier0CensusPair;
struct Tier0CensusPair
{
    uint64_t hash;
    uint64_t entries;
    uint32_t epoch;
};

typedef struct Tier0CensusSearch Tier0CensusSearch;
struct Tier0CensusSearch
{
    Tier0CensusCell* cells;
    Tier0CensusPair* pairs;
    uint64_t capacity;
    uint32_t epoch;
};

// A set's figures: the entries in keys with two patterns, the entries whose pattern isn't their key's commonest, and
// the keys.
#define BUSTER_TIER0_CENSUS_CONFLICTS 0
#define BUSTER_TIER0_CENSUS_EXCEPTIONS 1
#define BUSTER_TIER0_CENSUS_FIGURES 2
typedef struct Tier0CensusCost Tier0CensusCost;
struct Tier0CensusCost
{
    uint64_t figures[BUSTER_TIER0_CENSUS_FIGURES];
    uint32_t rows;
};

static uint64_t tier0_census_key(const Tier0CensusSample* sample, uint64_t set)
{
    uint64_t hash = 0x6b6579ull;
    for (uint64_t left = set; left != 0; left &= left - 1)
    {
        const uint32_t input = (uint32_t)__builtin_ctzll(left);
        hash = tier0_census_mix(hash, (uint64_t)input << 8 | sample->inputs[input]);
    }
    return hash;
}

static Tier0CensusCost tier0_census_evaluate(Tier0CensusSearch* search, const Tier0CensusSample* samples, uint32_t count, uint64_t set)
{
    search->epoch += 1;
    const uint32_t epoch = search->epoch;
    const uint64_t mask = search->capacity - 1;
    Tier0CensusCell* cells = search->cells;
    Tier0CensusPair* pairs = search->pairs;
    Tier0CensusCost cost = { { 0, 0 }, 0 };
    for (uint32_t m = 0; m < count; m += 1)
    {
        const Tier0CensusSample* sample = &samples[m];
        const uint64_t weight = sample->weight;
        const uint64_t hash = tier0_census_key(sample, set);
        uint64_t at = hash & mask;
        while (cells[at].epoch == epoch && cells[at].hash != hash)
        {
            at = (at + 1) & mask;
        }
        if (cells[at].epoch != epoch)
        {
            cells[at].epoch = epoch;
            cells[at].hash = hash;
            cells[at].entries = 0;
            cells[at].most = 0;
            cells[at].conflict = false;
            cost.rows += 1;
        }
        const uint64_t pair_hash = tier0_census_mix(hash, 0x70616972ull + sample->pattern);
        uint64_t pair = pair_hash & mask;
        while (pairs[pair].epoch == epoch && pairs[pair].hash != pair_hash)
        {
            pair = (pair + 1) & mask;
        }
        if (pairs[pair].epoch != epoch)
        {
            pairs[pair].epoch = epoch;
            pairs[pair].hash = pair_hash;
            pairs[pair].entries = 0;
        }
        pairs[pair].entries += weight;
        // The key's commonest pattern so far: the exceptions are its entries less that pattern's.
        const uint64_t most = pairs[pair].entries > cells[at].most ? pairs[pair].entries : cells[at].most;
        cost.figures[BUSTER_TIER0_CENSUS_EXCEPTIONS] += weight - (most - cells[at].most);
        cells[at].most = most;
        cells[at].entries += weight;
        // A key's entries so far join the conflicts when its second pattern comes, and each one after it as it comes.
        const bool second = !cells[at].conflict && cells[at].most < cells[at].entries;
        cost.figures[BUSTER_TIER0_CENSUS_CONFLICTS] += second ? cells[at].entries : cells[at].conflict ? weight : 0;
        cells[at].conflict = cells[at].conflict || second;
    }
    return cost;
}

// The best set found of each size for one figure: its figure and its rows.
typedef struct Tier0CensusChain Tier0CensusChain;
struct Tier0CensusChain
{
    uint64_t sets[TIER0_CENSUS_INPUTS + 1];
    uint64_t costs[TIER0_CENSUS_INPUTS + 1];
    uint32_t rows[TIER0_CENSUS_INPUTS + 1];
    uint32_t size;
};

// A kind's result over one set of allowed inputs.
typedef struct Tier0CensusRow Tier0CensusRow;
struct Tier0CensusRow
{
    uint64_t entries;
    // The whole key: the set that leaves only what no input decides, its inputs and rows.
    uint64_t set;
    uint32_t inputs;
    uint32_t rows;
    // The entries no input decides (the whole key's conflicts), and the fewest exceptions any set leaves.
    uint64_t undecided;
    uint64_t exceptions;
    // within[figure][k]: the most entries a set of k inputs or fewer leaves out of the figure, of those tried: decided,
    // and with their key's commonest pattern.
    uint64_t within[BUSTER_TIER0_CENSUS_FIGURES][TIER0_CENSUS_INPUTS + 1];
    // The gate's keys: for each figure the set of BUSTER_TIER0_CENSUS_GATE_BYTES - 1 inputs or fewer that leaves the
    // fewest, and its rows.
    uint64_t gate_sets[BUSTER_TIER0_CENSUS_FIGURES];
    uint32_t gate_rows[BUSTER_TIER0_CENSUS_FIGURES];
    // The sizes whose sets were searched exhaustively: every one up to this.
    uint32_t exhaustive;
};

// The kind's totals, which don't depend on the inputs allowed.
typedef struct Tier0CensusKind Tier0CensusKind;
struct Tier0CensusKind
{
    uint64_t entries;
    uint64_t heads;
    uint64_t records;
    uint64_t state_entries[TIER0_CENSUS_STATES];
    uint32_t patterns;
    // Its distinct samples (the same inputs and pattern), which the search reads.
    uint32_t distinct;
};

// The kind's keys among the inputs of `allowed` (tier0_entries_measure.c's header has the rule), over its distinct
// samples (`samples`, `count` of them, each with its groups' entries). `entries` is the kind's.
static void tier0_census_search(Tier0CensusSearch* search, const Tier0CensusSample* samples, uint32_t count, uint64_t allowed, uint64_t entries, Tier0CensusRow* row)
{
    // The allowed inputs that vary over the kind's samples.
    uint32_t active[TIER0_CENSUS_INPUTS];
    uint32_t active_count = 0;
    uint64_t all = 0;
    for (uint32_t input = 0; input < TIER0_CENSUS_INPUTS; input += 1)
    {
        bool varies = false;
        for (uint32_t m = 1; (allowed >> input & 1) != 0 && m < count && !varies; m += 1)
        {
            varies = samples[m].inputs[input] != samples[0].inputs[input];
        }
        if (varies)
        {
            active[active_count++] = input;
            all |= 1ull << input;
        }
    }
    const Tier0CensusCost whole_cost = tier0_census_evaluate(search, samples, count, all);
    const Tier0CensusCost none = tier0_census_evaluate(search, samples, count, 0);
    Tier0CensusChain chains[BUSTER_TIER0_CENSUS_FIGURES];
    for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
    {
        chains[f].sets[0] = 0;
        chains[f].costs[0] = none.figures[f];
        chains[f].rows[0] = none.rows;
        chains[f].size = 0;
    }
    // Every set of each size, while a figure is above the whole key's and the size's combinations are affordable.
    uint32_t size = 0;
    bool affordable = true;
    while (size < active_count && size < BUSTER_TIER0_CENSUS_EXHAUSTIVE && affordable &&
           (chains[0].costs[size] > whole_cost.figures[0] || chains[1].costs[size] > whole_cost.figures[1]))
    {
        const uint32_t next = size + 1;
        uint64_t combinations = 1;
        for (uint32_t k = 0; k < next; k += 1)
        {
            combinations = combinations * (active_count - k) / (k + 1);
        }
        affordable = combinations * count <= BUSTER_TIER0_CENSUS_PROBES;
        if (affordable)
        {
            uint32_t chosen[BUSTER_TIER0_CENSUS_EXHAUSTIVE + 1];
            for (uint32_t k = 0; k < next; k += 1)
            {
                chosen[k] = k;
            }
            bool taken[BUSTER_TIER0_CENSUS_FIGURES] = { false, false };
            for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
            {
                chains[f].sets[next] = chains[f].sets[size];
                chains[f].costs[next] = chains[f].costs[size];
                chains[f].rows[next] = chains[f].rows[size];
            }
            bool more = true;
            while (more)
            {
                uint64_t tried = 0;
                for (uint32_t k = 0; k < next; k += 1)
                {
                    tried |= 1ull << active[chosen[k]];
                }
                const Tier0CensusCost cost = tier0_census_evaluate(search, samples, count, tried);
                for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
                {
                    if (cost.figures[f] < chains[f].costs[next] || (cost.figures[f] == chains[f].costs[next] && taken[f] && cost.rows < chains[f].rows[next]))
                    {
                        chains[f].sets[next] = tried;
                        chains[f].costs[next] = cost.figures[f];
                        chains[f].rows[next] = cost.rows;
                        taken[f] = true;
                    }
                }
                // The next combination.
                int32_t k = (int32_t)next - 1;
                while (k >= 0 && chosen[k] == active_count - next + (uint32_t)k)
                {
                    k -= 1;
                }
                more = k >= 0;
                if (more)
                {
                    chosen[k] += 1;
                    for (uint32_t later = (uint32_t)k + 1; later < next; later += 1)
                    {
                        chosen[later] = chosen[later - 1] + 1;
                    }
                }
            }
            size = next;
        }
    }
    const uint32_t exhaustive = size;
    // Past the exhaustive sizes, each figure's own way: the set before with the input that takes the most away, or the
    // pair where none takes any.
    for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
    {
        Tier0CensusChain* chain = &chains[f];
        uint32_t at = exhaustive;
        bool moving = true;
        while (chain->costs[at] > whole_cost.figures[f] && at < active_count && moving)
        {
            const uint64_t set = chain->sets[at];
            uint64_t best = 0;
            uint64_t best_cost = chain->costs[at];
            uint32_t best_rows = UINT32_MAX;
            for (uint32_t a = 0; a < active_count; a += 1)
            {
                const uint64_t with = set | 1ull << active[a];
                if (with != set)
                {
                    const Tier0CensusCost cost = tier0_census_evaluate(search, samples, count, with);
                    if (cost.figures[f] < best_cost || (cost.figures[f] == best_cost && best != 0 && cost.rows < best_rows))
                    {
                        best = with;
                        best_cost = cost.figures[f];
                        best_rows = cost.rows;
                    }
                }
            }
            for (uint32_t a = 0; best == 0 && a < active_count; a += 1)
            {
                for (uint32_t b = a + 1; b < active_count; b += 1)
                {
                    const uint64_t with = set | 1ull << active[a] | 1ull << active[b];
                    if ((set >> active[a] & 1) == 0 && (set >> active[b] & 1) == 0)
                    {
                        const Tier0CensusCost cost = tier0_census_evaluate(search, samples, count, with);
                        if (cost.figures[f] < best_cost)
                        {
                            best = with;
                            best_cost = cost.figures[f];
                            best_rows = cost.rows;
                        }
                    }
                }
            }
            moving = best != 0;
            if (moving)
            {
                // A pair's first size has what the size before had.
                for (uint32_t added = (uint32_t)__builtin_popcountll(best & ~set); added > 1; added -= 1)
                {
                    at += 1;
                    chain->sets[at] = chain->sets[at - 1];
                    chain->costs[at] = chain->costs[at - 1];
                    chain->rows[at] = chain->rows[at - 1];
                }
                at += 1;
                chain->sets[at] = best;
                chain->costs[at] = best_cost;
                chain->rows[at] = best_rows;
            }
        }
        chain->size = at;
    }
    // The whole key: the conflicts' last set, or every input where neither an input nor a pair moved an entry though the
    // whole key decides more; then what it can drop, the inputs first to last.
    const Tier0CensusChain* strict = &chains[BUSTER_TIER0_CENSUS_CONFLICTS];
    const uint64_t baseline = whole_cost.figures[BUSTER_TIER0_CENSUS_CONFLICTS];
    uint64_t whole = strict->costs[strict->size] > baseline ? all : strict->sets[strict->size];
    for (uint32_t a = 0; a < active_count; a += 1)
    {
        const uint64_t without = whole & ~(1ull << active[a]);
        if (without != whole && tier0_census_evaluate(search, samples, count, without).figures[BUSTER_TIER0_CENSUS_CONFLICTS] <= baseline)
        {
            whole = without;
        }
    }
    const uint32_t whole_rows = tier0_census_evaluate(search, samples, count, whole).rows;
    const uint32_t whole_inputs = (uint32_t)__builtin_popcountll(whole);
    row->entries = entries;
    row->set = whole;
    row->inputs = whole_inputs;
    row->rows = whole_rows;
    row->undecided = baseline;
    row->exceptions = whole_cost.figures[BUSTER_TIER0_CENSUS_EXCEPTIONS];
    row->exhaustive = exhaustive;
    for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
    {
        const Tier0CensusChain* chain = &chains[f];
        uint64_t least = chain->costs[0];
        row->gate_sets[f] = 0;
        row->gate_rows[f] = chain->rows[0];
        for (uint32_t k = 0; k <= TIER0_CENSUS_INPUTS; k += 1)
        {
            const bool better = k <= chain->size && chain->costs[k] < least;
            least = better ? chain->costs[k] : least;
            if (better && k < BUSTER_TIER0_CENSUS_GATE_BYTES)
            {
                row->gate_sets[f] = chain->sets[k];
                row->gate_rows[f] = chain->rows[k];
            }
            // The whole key leaves the fewest conflicts there are, and no more exceptions than it has conflicts.
            const uint64_t whole_figure = f == BUSTER_TIER0_CENSUS_CONFLICTS ? baseline : tier0_census_evaluate(search, samples, count, whole).figures[f];
            if (k == whole_inputs && whole_figure < least)
            {
                least = whole_figure;
                if (k < BUSTER_TIER0_CENSUS_GATE_BYTES)
                {
                    row->gate_sets[f] = whole;
                    row->gate_rows[f] = whole_rows;
                }
            }
            row->within[f][k] = entries - least;
        }
    }
}

static void tier0_census_print_set(uint64_t set)
{
    for (uint64_t left = set; left != 0; left &= left - 1)
    {
        printf(",%s", tier0_census_input_names[__builtin_ctzll(left)]);
    }
}

// A kind's distinct keys at its set with their patterns (--list): each key's inputs, its entries, and each pattern's
// first head's forms.
static void tier0_census_list(const Tier0Census* census, const uint32_t* members, uint32_t count, uint64_t set, uint32_t top)
{
    // The kind's groups by key and pattern: a quadratic scan of the distinct ones, which a listing of one kind affords.
    typedef struct Tier0CensusListed Tier0CensusListed;
    struct Tier0CensusListed
    {
        uint64_t hash;
        uint64_t entries;
        uint32_t member;
        uint32_t pattern;
    };
    Tier0CensusListed* listed = calloc((size_t)count + 1, sizeof(Tier0CensusListed));
    uint32_t listed_count = 0;
    for (uint32_t m = 0; listed != 0 && m < count; m += 1)
    {
        const Tier0CensusSample* sample = &census->samples[members[m]];
        const uint64_t hash = tier0_census_key(sample, set);
        uint32_t at = 0;
        while (at < listed_count && !(listed[at].hash == hash && listed[at].pattern == sample->pattern))
        {
            at += 1;
        }
        if (at == listed_count)
        {
            listed[listed_count++] = (Tier0CensusListed){ .hash = hash, .member = members[m], .pattern = sample->pattern };
        }
        listed[at].entries += sample->weight;
    }
    for (uint32_t printed = 0; listed != 0 && printed < top && printed < listed_count; printed += 1)
    {
        // The largest left.
        uint32_t largest = printed;
        for (uint32_t at = printed + 1; at < listed_count; at += 1)
        {
            largest = listed[at].entries > listed[largest].entries ? at : largest;
        }
        const Tier0CensusListed row = listed[largest];
        listed[largest] = listed[printed];
        listed[printed] = row;
        const Tier0CensusSample* sample = &census->samples[row.member];
        const Tier0CensusPattern* pattern = &census->patterns[row.pattern];
        printf("tier0-census: list entries=%" PRIu64 " key=", row.entries);
        bool first = true;
        for (uint64_t left = set; left != 0; left &= left - 1)
        {
            const uint32_t input = (uint32_t)__builtin_ctzll(left);
            printf("%s%s:%u", first ? "" : ",", tier0_census_input_names[input], sample->inputs[input]);
            first = false;
        }
        printf("%s pattern=%u by=\"%s\" records=%u forms=", first ? "-" : "", row.pattern, tier0_census_state_names[pattern->state], pattern->records);
        for (uint32_t r = 0; r < pattern->records && r < BUSTER_TIER0_CENSUS_FORMS; r += 1)
        {
            printf("%s%s", r == 0 ? "" : ";", x86_form_description(pattern->forms[r])->mnemonic);
        }
        printf("%s\n", pattern->records == 0 ? "-" : pattern->records > BUSTER_TIER0_CENSUS_FORMS ? ";..." : "");
    }
    free(listed);
}

// --list's kind for every kind.
#define BUSTER_TIER0_CENSUS_LIST_ALL (BUSTER_TYPED_KINDS + 1)

// The two counts: over the raw inputs, and with the fusion's key offered.
#define BUSTER_TIER0_CENSUS_COUNTS 2
static const char* const tier0_census_count_names[BUSTER_TIER0_CENSUS_COUNTS] = { "raw", "fused" };
static const uint64_t tier0_census_count_inputs[BUSTER_TIER0_CENSUS_COUNTS] = { BUSTER_TIER0_CENSUS_RAW, BUSTER_TIER0_CENSUS_ALL };

// What tier0_census_report found, for the tests: each count's entries a key of the gate's bytes decides, those whose
// pattern is such a key's commonest, those no key decides, and the rows of the gate's keys.
typedef struct Tier0CensusResult Tier0CensusResult;
struct Tier0CensusResult
{
    uint64_t decided[BUSTER_TIER0_CENSUS_COUNTS];
    uint64_t common[BUSTER_TIER0_CENSUS_COUNTS];
    uint64_t undecided[BUSTER_TIER0_CENSUS_COUNTS];
    uint64_t rows[BUSTER_TIER0_CENSUS_COUNTS];
};

// The kind's distinct samples, into `distinct` (room for `count`): the groups with the same inputs and pattern as one,
// with their entries. Returns how many.
static uint32_t tier0_census_distinct(Tier0CensusSearch* search, const Tier0CensusSample* samples, const uint32_t* members, uint32_t count, Tier0CensusSample* distinct)
{
    search->epoch += 1;
    const uint32_t epoch = search->epoch;
    const uint64_t mask = search->capacity - 1;
    Tier0CensusCell* cells = search->cells;
    uint32_t made = 0;
    for (uint32_t m = 0; m < count; m += 1)
    {
        const Tier0CensusSample* sample = &samples[members[m]];
        const uint64_t hash = tier0_census_mix(tier0_census_key(sample, BUSTER_TIER0_CENSUS_ALL), sample->pattern);
        uint64_t at = hash & mask;
        while (cells[at].epoch == epoch && cells[at].hash != hash)
        {
            at = (at + 1) & mask;
        }
        if (cells[at].epoch != epoch)
        {
            cells[at].epoch = epoch;
            cells[at].hash = hash;
            cells[at].index = made;
            distinct[made] = *sample;
            distinct[made].weight = 0;
            made += 1;
        }
        distinct[cells[at].index].weight += sample->weight;
    }
    return made;
}

static void tier0_census_print_shares(const uint64_t* within, uint64_t entries)
{
    for (uint32_t k = 0; k < BUSTER_TIER0_CENSUS_GATE_BYTES; k += 1)
    {
        printf("%s%.2f", k == 0 ? "" : "/", 100.0 * (double)within[k] / (double)entries);
    }
}

// The report: the corpus's totals, a row a kind and a count (with `rows`), each count's totals against #2874's gate,
// what its keys decide without each family of inputs, and what no key decides; with `list` (a kind, BUSTER_TYPED_KINDS
// for none, BUSTER_TIER0_CENSUS_LIST_ALL for each) that kind's keys at the raw count's gate key. False without memory.
static bool tier0_census_report(const Tier0Census* census, bool rows, uint32_t list, uint32_t top, Tier0CensusResult* result)
{
    const uint64_t count = census->sample_count;
    uint32_t* members = calloc((size_t)count + 1, sizeof(uint32_t));
    Tier0CensusSample* distinct = calloc((size_t)count + 1, sizeof(Tier0CensusSample));
    uint64_t capacity = 1024;
    while (capacity < 2 * count)
    {
        capacity *= 2;
    }
    Tier0CensusSearch search = { .cells = calloc((size_t)capacity, sizeof(Tier0CensusCell)), .pairs = calloc((size_t)capacity, sizeof(Tier0CensusPair)), .capacity = capacity };
    uint8_t* seen = calloc((size_t)census->pattern_count + 1, 1);
    Tier0CensusRow* results = calloc((size_t)BUSTER_TYPED_KINDS * BUSTER_TIER0_CENSUS_COUNTS, sizeof(Tier0CensusRow));
    const bool room = members != 0 && distinct != 0 && search.cells != 0 && search.pairs != 0 && seen != 0 && results != 0;
    Tier0CensusKind kinds[BUSTER_TYPED_KINDS];
    memset(kinds, 0, sizeof(kinds));
    // without[c][f]: the entries the count's gate keys decide without family f's inputs.
    uint64_t without[BUSTER_TIER0_CENSUS_COUNTS][TIER0_CENSUS_FAMILIES];
    memset(without, 0, sizeof(without));
    memset(result, 0, sizeof(*result));
    const double entries = census->entries != 0 ? (double)census->entries : 1.0;
    if (room)
    {
        printf("tier0-census: %" PRIu64 " functions, %" PRIu64 " typed entries, %" PRIu64 " records (%.3f an entry), %" PRIu64 " of them the prologues' and epilogues'; %" PRIu64
               " branches, %" PRIu64 " references\n",
               census->functions, census->entries, census->records, (double)census->records / entries, census->frame_records, census->branches, census->references);
        printf("tier0-census: %" PRIu64 " groups (an entry that makes records, with the %" PRIu64 " entries they absorb), %u patterns\n", census->heads,
               census->entries - census->heads, census->pattern_count);
        for (uint32_t s = 0; s < TIER0_CENSUS_STATES; s += 1)
        {
            printf("tier0-census: pattern by %s: %" PRIu64 " groups, %" PRIu64 " entries (%.2f%%), %" PRIu64 " records\n", tier0_census_state_names[s], census->state_heads[s],
                   census->state_entries[s], 100.0 * (double)census->state_entries[s] / entries, census->state_records[s]);
        }
        printf("tier0-census: of the templates' entries, %" PRIu64 " (%.2f%%) take a template by their homes' case, %" PRIu64 " (%.2f%%) have an immediate the choice reads, and %"
               PRIu64 " (%.2f%%) leave it with another form\n",
               census->home_entries, 100.0 * (double)census->home_entries / entries, census->choice_entries, 100.0 * (double)census->choice_entries / entries,
               census->chosen_entries, 100.0 * (double)census->chosen_entries / entries);
        printf("tier0-census: %" PRIu64 " functions with deep values, %" PRIu64 " with segment overrides (left out), %" PRIu64 " refused\n", census->deep_functions,
               census->special_functions, census->refused);
        // The conditions (#2877).
        uint64_t conditions = 0;
        uint64_t again = 0;
        for (uint32_t c = 0; c < TIER0_CENSUS_CONDITIONS; c += 1)
        {
            conditions += census->conditions[c];
        }
        for (uint32_t a = 0; a < TIER0_CENSUS_AGAINS; a += 1)
        {
            again += census->tested_again[a];
        }
        printf("tier0-census: conditions: %" PRIu64 " conditional branches", conditions);
        for (uint32_t c = 0; c < TIER0_CENSUS_CONDITIONS; c += 1)
        {
            printf("%s %" PRIu64 " take %s", c == 0 ? ";" : ",", census->conditions[c], tier0_census_condition_names[c]);
        }
        printf("\ntier0-census: tested again: %" PRIu64 " sites make a compare's value and test it before a branch (setcc; movzx; test; je or jne)", again);
        for (uint32_t a = 0; a < TIER0_CENSUS_AGAINS; a += 1)
        {
            printf("%s %" PRIu64 " %s", a == 0 ? ":" : ";", census->tested_again[a], tier0_census_again_names[a]);
        }
        printf("\n");
        // What the fusion makes of the stream: the groups by their entries, those that make no record, and the groups by
        // the fusion's variant (BUSTER_TIER0_VARIANT_*: a key's kind has its own).
        uint64_t sizes[5] = { 0 };
        uint64_t silent_groups = 0;
        uint64_t silent_entries = 0;
        uint64_t variant_groups[BUSTER_TIER0_VARIANTS] = { 0 };
        uint64_t variant_entries[BUSTER_TIER0_VARIANTS] = { 0 };
        for (uint64_t s = 0; s < count; s += 1)
        {
            const Tier0CensusSample* sample = &census->samples[s];
            sizes[sample->weight < 4 ? sample->weight : 4] += 1;
            silent_groups += sample->records == 0 ? 1 : 0;
            silent_entries += sample->records == 0 ? sample->weight : 0;
            const uint32_t variant = sample->inputs[TIER0_CENSUS_VARIANT] < BUSTER_TIER0_VARIANTS ? sample->inputs[TIER0_CENSUS_VARIANT] : BUSTER_TIER0_VARIANTS - 1;
            variant_groups[variant] += 1;
            variant_entries[variant] += sample->weight;
        }
        printf("tier0-census: groups of 1 entry %" PRIu64 ", of 2 %" PRIu64 ", of 3 %" PRIu64 ", of 4 or more %" PRIu64 "; %" PRIu64 " groups (%" PRIu64 " entries, %.2f%%) make no record\n",
               sizes[1], sizes[2], sizes[3], sizes[4], silent_groups, silent_entries, 100.0 * (double)silent_entries / entries);
        for (uint32_t variant = 0; variant < BUSTER_TIER0_VARIANTS; variant += 1)
        {
            if (variant_groups[variant] != 0)
            {
                printf("tier0-census: variant=%u groups=%" PRIu64 " entries=%" PRIu64 " (%.2f%%)\n", variant, variant_groups[variant], variant_entries[variant],
                       100.0 * (double)variant_entries[variant] / entries);
            }
        }
    }
    for (uint32_t kind = 0; room && kind < BUSTER_TYPED_KINDS; kind += 1)
    {
        Tier0CensusKind* totals = &kinds[kind];
        uint32_t member_count = 0;
        memset(seen, 0, (size_t)census->pattern_count + 1);
        for (uint64_t s = 0; s < count; s += 1)
        {
            const Tier0CensusSample* sample = &census->samples[s];
            if (sample->kind == kind)
            {
                members[member_count++] = (uint32_t)s;
                totals->entries += sample->weight;
                totals->heads += 1;
                totals->records += sample->records;
                totals->state_entries[sample->state] += sample->weight;
                totals->patterns += seen[sample->pattern] == 0 ? 1 : 0;
                seen[sample->pattern] = 1;
            }
        }
        totals->distinct = member_count != 0 ? tier0_census_distinct(&search, census->samples, members, member_count, distinct) : 0;
        for (uint32_t c = 0; member_count != 0 && c < BUSTER_TIER0_CENSUS_COUNTS; c += 1)
        {
            Tier0CensusRow* row = &results[kind * BUSTER_TIER0_CENSUS_COUNTS + c];
            tier0_census_search(&search, distinct, totals->distinct, tier0_census_count_inputs[c], totals->entries, row);
            const uint64_t gate_set = row->gate_sets[BUSTER_TIER0_CENSUS_CONFLICTS];
            const uint64_t gate = totals->entries - row->within[BUSTER_TIER0_CENSUS_CONFLICTS][BUSTER_TIER0_CENSUS_GATE_BYTES - 1];
            for (uint32_t f = 0; f < TIER0_CENSUS_FAMILIES; f += 1)
            {
                uint64_t kept = gate_set;
                for (uint32_t input = 0; input < TIER0_CENSUS_INPUTS; input += 1)
                {
                    kept &= tier0_census_input_families[input] == f ? ~(1ull << input) : ~0ull;
                }
                const uint64_t conflicts = kept != gate_set ? tier0_census_evaluate(&search, distinct, totals->distinct, kept).figures[BUSTER_TIER0_CENSUS_CONFLICTS] : gate;
                without[c][f] += totals->entries - (conflicts > gate ? conflicts : gate);
            }
            if (rows)
            {
                // The kind is a key's first byte.
                printf("tier0-census: keys=%s kind=%s entries=%" PRIu64 " (%.2f%%) groups=%" PRIu64 " distinct=%u records=%" PRIu64 " templated=%.2f%% patterns=%u key-bytes=%u key=kind",
                       tier0_census_count_names[c], tier0_census_kind_names[kind], totals->entries, 100.0 * (double)totals->entries / entries, totals->heads, totals->distinct,
                       totals->records, 100.0 * (double)totals->state_entries[TIER0_CENSUS_TEMPLATED] / (double)totals->entries, totals->patterns, row->inputs + 1);
                tier0_census_print_set(row->set);
                printf(" rows=%u undecided=%" PRIu64 " exceptions=%" PRIu64 " exhaustive=%u decided=", row->rows, row->undecided, row->exceptions, row->exhaustive + 1);
                tier0_census_print_shares(row->within[BUSTER_TIER0_CENSUS_CONFLICTS], totals->entries);
                printf(" gate-key=kind");
                tier0_census_print_set(gate_set);
                printf(" gate-rows=%u commonest=", row->gate_rows[BUSTER_TIER0_CENSUS_CONFLICTS]);
                tier0_census_print_shares(row->within[BUSTER_TIER0_CENSUS_EXCEPTIONS], totals->entries);
                printf(" commonest-key=kind");
                tier0_census_print_set(row->gate_sets[BUSTER_TIER0_CENSUS_EXCEPTIONS]);
                printf(" commonest-rows=%u\n", row->gate_rows[BUSTER_TIER0_CENSUS_EXCEPTIONS]);
            }
        }
        if (member_count != 0 && (kind == list || list == BUSTER_TIER0_CENSUS_LIST_ALL))
        {
            printf("tier0-census: list kind=%s\n", tier0_census_kind_names[kind]);
            tier0_census_list(census, members, member_count, results[kind * BUSTER_TIER0_CENSUS_COUNTS].gate_sets[BUSTER_TIER0_CENSUS_CONFLICTS], top);
        }
    }
    static const char* const figure_names[BUSTER_TIER0_CENSUS_FIGURES] = { "decides", "has as its commonest the pattern of" };
    for (uint32_t c = 0; room && c < BUSTER_TIER0_CENSUS_COUNTS; c += 1)
    {
        const char* name = tier0_census_count_names[c];
        uint64_t whole_rows = 0;
        uint64_t gate_rows[BUSTER_TIER0_CENSUS_FIGURES] = { 0, 0 };
        uint64_t undecided = 0;
        uint64_t exceptions = 0;
        for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
        {
            uint64_t last = 0;
            for (uint32_t bytes = 1; bytes <= TIER0_CENSUS_INPUTS + 1; bytes += 1)
            {
                uint64_t left = 0;
                for (uint32_t kind = 0; kind < BUSTER_TYPED_KINDS; kind += 1)
                {
                    left += results[kind * BUSTER_TIER0_CENSUS_COUNTS + c].within[f][bytes - 1];
                }
                if (left != last || bytes == BUSTER_TIER0_CENSUS_GATE_BYTES)
                {
                    printf("tier0-census: keys=%s: a key of %u bytes or fewer %s %" PRIu64 " entries (%.2f%%)\n", name, bytes, figure_names[f], left, 100.0 * (double)left / entries);
                }
                if (bytes == BUSTER_TIER0_CENSUS_GATE_BYTES)
                {
                    result->decided[c] = f == BUSTER_TIER0_CENSUS_CONFLICTS ? left : result->decided[c];
                    result->common[c] = f == BUSTER_TIER0_CENSUS_EXCEPTIONS ? left : result->common[c];
                }
                last = left;
            }
        }
        for (uint32_t kind = 0; kind < BUSTER_TYPED_KINDS; kind += 1)
        {
            const Tier0CensusRow* row = &results[kind * BUSTER_TIER0_CENSUS_COUNTS + c];
            undecided += row->undecided;
            exceptions += row->exceptions;
            whole_rows += row->rows;
            for (uint32_t f = 0; f < BUSTER_TIER0_CENSUS_FIGURES; f += 1)
            {
                gate_rows[f] += kinds[kind].entries != 0 ? row->gate_rows[f] : 0;
            }
        }
        result->undecided[c] = undecided;
        result->rows[c] = gate_rows[BUSTER_TIER0_CENSUS_CONFLICTS];
        printf("tier0-census: keys=%s: gate: a key of %u bytes or fewer decides %" PRIu64 " of %" PRIu64 " entries (%.2f%%) with no conflict, its tables %" PRIu64
               " rows; no key of these bytes decides %" PRIu64 " (%.2f%%); the kinds' whole keys have %" PRIu64 " rows; %u patterns\n",
               name, BUSTER_TIER0_CENSUS_GATE_BYTES, result->decided[c], census->entries, 100.0 * (double)result->decided[c] / entries, gate_rows[BUSTER_TIER0_CENSUS_CONFLICTS],
               undecided, 100.0 * (double)undecided / entries, whole_rows, census->pattern_count);
        printf("tier0-census: keys=%s: a key of %u bytes or fewer has as its commonest the pattern of %" PRIu64 " entries (%.2f%%), its tables %" PRIu64
               " rows; every input leaves %" PRIu64 " entries (%.2f%%) with another pattern than their key's commonest\n",
               name, BUSTER_TIER0_CENSUS_GATE_BYTES, result->common[c], 100.0 * (double)result->common[c] / entries, gate_rows[BUSTER_TIER0_CENSUS_EXCEPTIONS], exceptions,
               100.0 * (double)exceptions / entries);
        for (uint32_t f = 0; f < TIER0_CENSUS_FAMILIES; f += 1)
        {
            if (without[c][f] != result->decided[c])
            {
                printf("tier0-census: keys=%s: without %s the gate's keys decide %" PRIu64 " entries (%.2f%%): %" PRIu64 " (%.2f%%) need it\n", name, tier0_census_family_names[f],
                       without[c][f], 100.0 * (double)without[c][f] / entries, result->decided[c] - without[c][f],
                       100.0 * (double)(result->decided[c] - without[c][f]) / entries);
            }
        }
        for (uint32_t kind = 0; kind < BUSTER_TYPED_KINDS; kind += 1)
        {
            const Tier0CensusRow* row = &results[kind * BUSTER_TIER0_CENSUS_COUNTS + c];
            if (row->undecided != 0)
            {
                printf("tier0-census: keys=%s: undecided kind=%s entries=%" PRIu64 " (%.2f%% of the entries, %.2f%% of the kind's)\n", name, tier0_census_kind_names[kind],
                       row->undecided, 100.0 * (double)row->undecided / entries, 100.0 * (double)row->undecided / (double)row->entries);
            }
        }
    }
    free(results);
    free(seen);
    free(search.pairs);
    free(search.cells);
    free(distinct);
    free(members);
    return room;
}

// ---------------------------------------------------------------------------------------------------------------------
// The files

// The file's functions, added to the census, or what kept them out of it, printed (not with `quiet`): whether the
// options could be read. It is prepared, lowered and emitted as benchmark-backend-source prepares its files
// (backend_source_file).
static bool tier0_census_file(Tier0Census* census, const char* file, char** options, uint32_t option_count, Arena* lowered, Arena* scratch, bool quiet)
{
    CheckSource prepared;
    const bool chosen = check_source_prepare(&prepared, file, options, option_count, (CheckSourceRequest){ .select = true, .emits = true });
    const bool valid = prepared.stage != CHECK_SOURCE_OPTIONS || prepared.refusal.error != CHECK_REQUEST_ACCEPTED;
    const CheckProgramSelection* selected = &prepared.selected;
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    const bool reserved = bytes.base != 0;
    const uint64_t lowered_mark = lowered->length;
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    const bool emitted = chosen && reserved && check_program_emit(&code, &selected->program, prepared.parser, prepared.pp->table, selected->data.capacity,
                                                                  BUSTER_SCALAR_PATH(prepared.scalar), lowered, &bytes, &prepared.path.scratch, NULL, NULL, false, false);
    const bool refused = chosen && reserved && !emitted && code.status == CHECK_PROGRAM_UNSUPPORTED;
    const uint32_t count = selected->program.body_count;
    uint8_t* defined = 0;
    bool added = emitted && check_program_access(&code, &selected->program, lowered, &prepared.path.scratch, &defined);
    const uint64_t functions_before = census->functions;
    const uint64_t entries_before = census->entries;
    for (uint32_t b = 0; added && b < count; b += 1)
    {
        added = tier0_census_function(census, &code.lowered.functions[b], scratch);
    }
    const bool failed = valid && (check_source_failed(&prepared) || (chosen && !reserved) || (chosen && reserved && !emitted && !refused) || (emitted && !added));
    census->failures += failed ? 1 : 0;
    census->left_out += valid && !failed && !added ? 1 : 0;
    census->files += added ? 1 : 0;
    if (quiet)
    {
        // The tests read the census itself.
    }
    else if (valid && !chosen)
    {
        check_source_report(stdout, "tier0-census", file, &prepared);
    }
    else if (valid && !reserved)
    {
        printf("tier0-census: file=%s failed=\"its arenas couldn't be reserved\"\n", file);
    }
    else if (valid && !emitted)
    {
        printf("tier0-census: file=%s %s=\"%s\"\n", file, refused ? "unsupported" : "failed", code.reason != NULL ? code.reason : "this C type or operation");
    }
    else if (valid && !added)
    {
        printf("tier0-census: file=%s failed=\"memory ran out\"\n", file);
    }
    else if (valid)
    {
        printf("tier0-census: file=%s functions=%" PRIu64 " entries=%" PRIu64 "\n", file, census->functions - functions_before, census->entries - entries_before);
    }
    // The functions' streams are read: the next file's take their place.
    arena_pop_to(lowered, lowered_mark);
    arena_release(&bytes);
    check_source_release(&prepared);
    return valid;
}

// A set of files and the options they are read with: the command's arguments between two --next.
typedef struct Tier0CensusSources Tier0CensusSources;
struct Tier0CensusSources
{
    char** files;
    uint32_t file_count;
    char** options;
    uint32_t option_count;
};

// The census of the sets' files, each read with its set's options, and its report with what it found in `result`
// (`quiet`, the tests': no report, the census's samples left for the caller to read): whether the options could be read
// and the arenas reserved.
static bool tier0_census_files(Tier0Census* census, const Tier0CensusSources* sources, uint32_t source_count, bool rows, uint32_t list, uint32_t top, bool quiet,
                               Tier0CensusResult* result)
{
    Arena scratch = arena_reserve(UINT64_C(1) << 32);
    Arena lowered = arena_reserve(UINT64_C(1) << 36);
    bool valid = scratch.base != 0 && lowered.base != 0 && tier0_census_begin(census);
    for (uint32_t s = 0; valid && s < source_count; s += 1)
    {
        for (uint32_t i = 0; valid && i < sources[s].file_count; i += 1)
        {
            valid = tier0_census_file(census, sources[s].files[i], sources[s].options, sources[s].option_count, &lowered, &scratch, quiet);
            fflush(stdout);
        }
    }
    const bool reported = valid && !quiet && census->entries != 0 && tier0_census_report(census, rows, list, top, result);
    if (valid && !quiet)
    {
        printf("tier0-census: totals files=%" PRIu64 " left_out=%" PRIu64 " failures=%" PRIu64 " census=%s\n", census->files, census->left_out, census->failures,
               reported ? "reported" : census->entries == 0 ? "none" : "failed");
    }
    fflush(stdout);
    arena_release(&lowered);
    arena_release(&scratch);
    return valid && (quiet || reported);
}

static int run_tier0_entries_measure(int count, char** arguments)
{
    char** options = calloc((size_t)count + 1, sizeof(char*));
    char** files = calloc((size_t)count + 1, sizeof(char*));
    Tier0CensusSources* sources = calloc((size_t)count + 1, sizeof(Tier0CensusSources));
    uint32_t option_count = 0;
    uint32_t file_count = 0;
    uint32_t source_count = 0;
    bool rows = false;
    uint32_t list = BUSTER_TYPED_KINDS;
    uint64_t top = 40;
    bool valid = options != 0 && files != 0 && sources != 0;
    if (valid)
    {
        sources[0] = (Tier0CensusSources){ .files = files, .options = options };
    }
    for (int i = 0; valid && i < count; i += 1)
    {
        Tier0CensusSources* current = &sources[source_count];
        if (strcmp(arguments[i], "--rows") == 0)
        {
            rows = true;
        }
        else if (strcmp(arguments[i], "--list") == 0)
        {
            valid = i + 1 < count;
            for (uint32_t kind = 0; valid && kind < BUSTER_TYPED_KINDS; kind += 1)
            {
                list = tier0_census_kind_names[kind] != 0 && strcmp(arguments[i + 1], tier0_census_kind_names[kind]) == 0 ? kind : list;
            }
            list = valid && strcmp(arguments[i + 1], "all") == 0 ? BUSTER_TIER0_CENSUS_LIST_ALL : list;
            valid = valid && list != BUSTER_TYPED_KINDS;
            i += 1;
        }
        else if (strcmp(arguments[i], "--top") == 0)
        {
            valid = i + 1 < count && check_corpus_number(arguments[++i], &top) && top <= 100000;
        }
        else if (strcmp(arguments[i], "--next") == 0)
        {
            // The set before must have a file: its options are no other set's.
            valid = current->file_count != 0;
            source_count += 1;
            sources[source_count] = (Tier0CensusSources){ .files = files + file_count, .options = options + option_count };
        }
        else if (arguments[i][0] != '-')
        {
            files[file_count++] = arguments[i];
            current->file_count += 1;
        }
        else if (strcmp(arguments[i], "--no-default-config") != 0)
        {
            options[option_count++] = arguments[i];
            current->option_count += 1;
            if (pp_option_takes_value(arguments[i]))
            {
                valid = i + 1 < count;
                options[option_count++] = valid ? arguments[++i] : 0;
                current->option_count += 1;
            }
        }
    }
    valid = valid && sources[source_count].file_count != 0;
    Tier0Census census;
    memset(&census, 0, sizeof(census));
    Tier0CensusResult result;
    memset(&result, 0, sizeof(result));
    const bool ran = valid && tier0_census_files(&census, sources, source_count + 1, rows, list, (uint32_t)top, false, &result);
    if (!valid)
    {
        fprintf(stderr, "usage: compiler measure-tier0-entries [--rows] [--list KIND|all] [--top N] [preprocessor options] file... [--next [preprocessor options] file...]...\n");
    }
    const int status = ran && census.failures == 0 && census.refused == 0 && census.entries != 0 ? 0 : 1;
    if (valid)
    {
        tier0_census_end(&census);
    }
    free(sources);
    free(files);
    free(options);
    return status;
}

#endif
