// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's bottom-up rewrite selection (#141's design: Pelegrí-Llopart and Graham, 1988; iburg, Fraser, Hanson and
// Proebsting, 1992): both host paths' cover of the plan's integer core. Included by tier1_select.c, before its plan.
//
// - The grammar is a C table of patterns (tier1_burs_patterns), each a nonterminal's derivation from an operator and its
//   children's nonterminals, or from another nonterminal (a chain), with a cost: the candidates' structural bounds and
//   explicit latency penalties (docs/tier1-selection.md). A pattern stands for one candidate kind of
//   tier1_select_candidates: an inner one (an address's piece, an immediate, a truth value's link) folds into its root's.
// - tier1_select_initialize compiles it into state tables once (burg's algorithm, as Proebsting's "BURS automata
//   generation", 1995, describes it): a state is the cheapest rule and its normalized cost for every nonterminal; each
//   operator's child position projects a child's state onto the nonterminals the operator's patterns read there (its
//   representer); and each operator's table gives the state of each pair of representers. The tables are in the baked
//   section (baked.c), so a compile starts with them built, as with the encoder's templates.
// - A node's operator is its class (tier1_burs_classify): its IR operation, its type, and what the candidates' rules read
//   of its operands, as a constant's value (its class bits) or whether an operand is a load. A child the node's pattern
//   may fold is visible (a tree child, a constant, or a frame object's address in its block or the entry block); any other is
//   a value computed elsewhere, the REG leaf's state.
// - Labeling is bottom-up, a depth level at a time, 16 nodes a vector (tier1_burs_label): each node's state is a table
//   lookup on its operator and its children's representers, gathered. Choosing each node's rule is a lookup too
//   (tier1_burs_rules): the rule of its goal, a statement's or a register's.
// - The plan builds the pattern of each node its code computes from that rule: tier1_select_candidates' candidate of the
//   rule's kind (tier1_burs_pick), whose walks fill its fields. A node whose operator the grammar doesn't cover, or whose
//   rule names a candidate the walks didn't make (a walk past BUSTER_TIER1_SELECT_WALK), takes maximal munch's
//   (tier1_select_munch): the common fallback for what the tables don't cover. An address whose
//   displacement doesn't fit 32 bits is no fold's: its class is the REG leaf's (the tests' tier1_burs_displacement), whose pattern is
//   munch's, the one candidate it has (its constant index in a register, #141) or none, so that both paths take or refuse
//   it alike. Classification, labeling and rule lookup are vector kernels; the tests check them against their definitions
//   (tier1_select_burs_test.c's equations, tier1_burs_displacement and tier1_select_stack_object). The cover is
//   the cheapest the grammar has for each tree, and the grammar has every candidate the generator makes for the
//   operators it covers, so it is never costlier than munch's in the grammar's cost: each pattern's structural bound on
//   its records, not the records the consumer writes (docs/tier1-selection.md has an addition of two loads, which is
//   cheaper but a record longer as a lea of their registers than as an add from memory).

#include <stdio.h>
#include <sys/mman.h>

// The nonterminals.
typedef enum Tier1BursNonterminal
{
    TIER1_BURS_STMT,
    TIER1_BURS_REG,
    // A constant the consumer takes as its immediate, a lea's shift count (0 to 3), a power of two, 3, 5 or 9, a shift's
    // count, zero, and a 32-bit displacement.
    TIER1_BURS_IMM,
    TIER1_BURS_SCALE,
    TIER1_BURS_POW2,
    TIER1_BURS_M359,
    TIER1_BURS_SHAMT,
    TIER1_BURS_ZERO,
    TIER1_BURS_DISP,
    // A load a consumer takes as its memory operand; a shift a lea takes as its index's scale.
    TIER1_BURS_LOADF,
    TIER1_BURS_SHL3,
    // An address without an index (a base register or a frame object, and a displacement), and with at most one; an
    // index, through its constant additions.
    TIER1_BURS_A0,
    TIER1_BURS_A1,
    TIER1_BURS_INDEX,
    // An integer comparison a branch, a select (#2676) or a truth value takes, a floating one, and a truth value's zero
    // extension of each.
    TIER1_BURS_COND,
    TIER1_BURS_FCOND,
    TIER1_BURS_ZCOND,
    TIER1_BURS_ZFCOND,
    // A floating comparison by <, <=, > or >= itself, which its I1 copy takes (#2791); a floating == or != a select takes,
    // and one itself (#2922).
    TIER1_BURS_FCMP,
    TIER1_BURS_ECOND,
    TIER1_BURS_ECMP,
    TIER1_BURS_NONTERMINALS,
} Tier1BursNonterminal;

// A constant's class bits (tier1_burs_constant_bits_vector): which of the constant nonterminals it derives: an immediate
// that fits, a lea's shift count, a power of two, 3, 5 or 9, a shift's count, a zero and a 32-bit displacement.
#define BUSTER_TIER1_BURS_FIT 1u
#define BUSTER_TIER1_BURS_SCALE 2u
#define BUSTER_TIER1_BURS_POW2 4u
#define BUSTER_TIER1_BURS_M359 8u
#define BUSTER_TIER1_BURS_SHAMT 16u
#define BUSTER_TIER1_BURS_ZERO 32u
#define BUSTER_TIER1_BURS_DISP 64u
#define BUSTER_TIER1_BURS_CONSTANT_CLASSES 128u

// An ALU operation's kinds by its right operand, which decide what its candidates read: a constant (C), a load (L), a
// shift left (S, an addition's alone) or another value (O).
#define BUSTER_TIER1_BURS_KIND_C 0u
#define BUSTER_TIER1_BURS_KIND_L 1u
#define BUSTER_TIER1_BURS_KIND_S 2u
#define BUSTER_TIER1_BURS_KIND_O 3u
#define BUSTER_TIER1_BURS_KINDS 4u
#define BUSTER_TIER1_BURS_ALUS 9u

// The operators. REG is a value the pattern doesn't look into: an operator the grammar doesn't cover, or a child that
// isn't visible. The ALU operators are an operation's (ADD32 to NARROW) times its kinds.
typedef enum Tier1BursOperator
{
    TIER1_BURS_OP_REG,
    TIER1_BURS_OP_SLOT,
    TIER1_BURS_OP_SLOT_X,
    // The node has one machine pattern, with all its operand and ABI details filled by the candidate generator.
    TIER1_BURS_OP_DIRECT,
    TIER1_BURS_OP_CALL,
    TIER1_BURS_OP_CALL_ASM,
    TIER1_BURS_OP_CALL_ROW,
    TIER1_BURS_OP_CONST,
    TIER1_BURS_OP_ALU = TIER1_BURS_OP_CONST + BUSTER_TIER1_BURS_CONSTANT_CLASSES,
    TIER1_BURS_OP_SHL = TIER1_BURS_OP_ALU + BUSTER_TIER1_BURS_ALUS * BUSTER_TIER1_BURS_KINDS,
    TIER1_BURS_OP_LSHR,
    TIER1_BURS_OP_ASHR,
    TIER1_BURS_OP_LOAD_F,
    TIER1_BURS_OP_LOAD_N,
    TIER1_BURS_OP_STORE,
    TIER1_BURS_OP_ADDR_NOIDX,
    TIER1_BURS_OP_ADDR_CIDX,
    TIER1_BURS_OP_ADDR_IDX,
    TIER1_BURS_OP_CONV_PTR,
    TIER1_BURS_OP_ICMP,
    TIER1_BURS_OP_ICMP_LINK,
    TIER1_BURS_OP_ZEXT_TRUTH,
    TIER1_BURS_OP_ZEXT_PLAIN,
    TIER1_BURS_OP_FCMP_ORD,
    // A floating comparison by == or != (#2922), whose truth value reads the parity flag too, but one that a select whose
    // values recur through it reads (tier1_burs_equality).
    TIER1_BURS_OP_FCMP_EQ,
    TIER1_BURS_OP_CBR_ADJ,
    // An integer select, which reads its condition (#2676): the select's values are its own root's registers.
    TIER1_BURS_OP_SELECT,
    // An I1 copy of the floating comparison right before it, a truth value's link (tier1_select_truth, #2791).
    TIER1_BURS_OP_COPY_TRUTH,
    TIER1_BURS_OP_SIGN_LOAD,
    TIER1_BURS_OP_FLOAT_LOAD_F,
    TIER1_BURS_OP_FLOAT_LOAD_N,
    TIER1_BURS_OP_FLOAT_STORE,
    TIER1_BURS_OP_FLOAT_BINARY_RIGHT,
    TIER1_BURS_OP_FLOAT_BINARY_LEFT,
    TIER1_BURS_OP_X87,
    TIER1_BURS_OP_NARROW_BINARY,
    TIER1_BURS_OP_NARROW_UNARY,
    TIER1_BURS_OP_SIGNED_PRODUCT,
    TIER1_BURS_OP_FLOAT_CONVERT,
    TIER1_BURS_OP_FLOAT_CONVERT_LOAD,
    TIER1_BURS_OP_FLOAT_CONVERT_CONSTANT,
    TIER1_BURS_OPERATORS,
} Tier1BursOperator;

#ifndef BUSTER_TIER1_BURS_STORAGE_ONLY
// The ALU operations, the first index of an ALU operator: the address walks take a 64-bit addition alone, and the index's
// a 64-bit subtraction too.
#define BUSTER_TIER1_BURS_ADD32 0u
#define BUSTER_TIER1_BURS_ADD64 1u
#define BUSTER_TIER1_BURS_SUB32 2u
#define BUSTER_TIER1_BURS_SUB64 3u
#define BUSTER_TIER1_BURS_MUL 4u
#define BUSTER_TIER1_BURS_AND 5u
#define BUSTER_TIER1_BURS_OR 6u
#define BUSTER_TIER1_BURS_XOR 7u
// An I1's, I8's or I16's and, or or xor (#2677): its 32-bit operation's patterns but a load's, since no 32-bit form reads
// a byte or a word as its memory operand.
#define BUSTER_TIER1_BURS_NARROW 8u

// The families a pattern names: a set of operators, an ALU family's restricted to `kinds` (a bit per kind).
typedef enum Tier1BursFamily
{
    TIER1_BURS_FAMILY_NONE,
    TIER1_BURS_FAMILY_CHAIN,
    TIER1_BURS_FAMILY_REG,
    TIER1_BURS_FAMILY_DIRECT,
    TIER1_BURS_FAMILY_CALL,
    TIER1_BURS_FAMILY_SLOT,
    TIER1_BURS_FAMILY_CONST,
    TIER1_BURS_FAMILY_ALU,
    TIER1_BURS_FAMILY_ADD,
    TIER1_BURS_FAMILY_ADD64,
    TIER1_BURS_FAMILY_SUB,
    TIER1_BURS_FAMILY_SUB64,
    TIER1_BURS_FAMILY_MUL,
    TIER1_BURS_FAMILY_COMMUTATIVE,
    TIER1_BURS_FAMILY_IMMEDIATE,
    TIER1_BURS_FAMILY_COMMUTATIVE_PLAIN,
    // A commutative operation but a multiplication or a narrow one: which takes a load as its memory operand.
    TIER1_BURS_FAMILY_COMMUTATIVE_MEMORY,
    TIER1_BURS_FAMILY_SHIFT,
    TIER1_BURS_FAMILY_SHL,
    TIER1_BURS_FAMILY_LOAD,
    TIER1_BURS_FAMILY_LOAD_F,
    TIER1_BURS_FAMILY_STORE,
    TIER1_BURS_FAMILY_ADDR_NOIDX,
    TIER1_BURS_FAMILY_ADDR_CIDX,
    TIER1_BURS_FAMILY_ADDR_IDX,
    TIER1_BURS_FAMILY_CONV_PTR,
    TIER1_BURS_FAMILY_ICMP,
    TIER1_BURS_FAMILY_ICMP_LINK,
    TIER1_BURS_FAMILY_ZEXT_TRUTH,
    TIER1_BURS_FAMILY_ZEXT,
    TIER1_BURS_FAMILY_FCMP_ORD,
    TIER1_BURS_FAMILY_FCMP_EQ,
    TIER1_BURS_FAMILY_CBR_ADJ,
    TIER1_BURS_FAMILY_SELECT,
    TIER1_BURS_FAMILY_X87,
    TIER1_BURS_FAMILY_NARROW,
    TIER1_BURS_FAMILY_SIGNED_PRODUCT,
    TIER1_BURS_FAMILY_FLOAT_CONVERT,
    TIER1_BURS_FAMILY_SIGN_LOAD,
    TIER1_BURS_FAMILY_FLOAT_LOAD,
    TIER1_BURS_FAMILY_FLOAT_STORE,
    TIER1_BURS_FAMILY_FLOAT_BINARY,
    TIER1_BURS_FAMILY_COPY_TRUTH,
} Tier1BursFamily;

// What the plan takes as a root's pattern (tier1_burs_pick): the candidate of `kind`, with LEA_ADD's of a register or of
// a displacement apart; a load's or a store's walked address where it has one, a store's of a register and of its constant
// as the immediate (#2554) apart; a conversion's walked copy; a truth
// value's folded comparison; a node's own first candidate; or munch's choice.
typedef enum Tier1BursAction
{
    TIER1_BURS_ACTION_NONE,
    TIER1_BURS_ACTION_MUNCH,
    TIER1_BURS_ACTION_KIND,
    TIER1_BURS_ACTION_LEA_REGISTER,
    TIER1_BURS_ACTION_LEA_IMMEDIATE,
    TIER1_BURS_ACTION_LOAD,
    TIER1_BURS_ACTION_STORE,
    TIER1_BURS_ACTION_STORE_IMMEDIATE,
    TIER1_BURS_ACTION_WALKED_COPY,
    TIER1_BURS_ACTION_FOLDED_COMPARE,
    TIER1_BURS_ACTION_FLOAT_ADDRESS,
    TIER1_BURS_ACTION_NARROW,
    TIER1_BURS_ACTION_SIGNED_PRODUCT,
    TIER1_BURS_ACTION_PLAIN,
} Tier1BursAction;

typedef struct Tier1BursPattern Tier1BursPattern;
struct Tier1BursPattern
{
    uint8_t lhs;
    uint8_t family;
    // An ALU family's kinds (a bit per BUSTER_TIER1_BURS_KIND_*), all when zero; a constant's required class bits.
    uint8_t kinds;
    uint8_t kids[2];
    uint8_t cost;
    uint8_t action;
    uint8_t kind;
};

#define BUSTER_TIER1_BURS_ANY 0
#define BUSTER_TIER1_BURS_K(k) (uint8_t)(1u << BUSTER_TIER1_BURS_KIND_##k)
#define BUSTER_TIER1_BURS_NOT_C (uint8_t)(BUSTER_TIER1_BURS_K(L) | BUSTER_TIER1_BURS_K(S) | BUSTER_TIER1_BURS_K(O))
#define BUSTER_TIER1_BURS_NOT_L (uint8_t)(BUSTER_TIER1_BURS_K(C) | BUSTER_TIER1_BURS_K(S) | BUSTER_TIER1_BURS_K(O))
#define BUSTER_TIER1_BURS_NOT_S (uint8_t)(BUSTER_TIER1_BURS_K(C) | BUSTER_TIER1_BURS_K(L) | BUSTER_TIER1_BURS_K(O))
#define BUSTER_TIER1_BURS_NO_KID TIER1_BURS_NONTERMINALS

// The grammar. Each pattern's cost is its candidate's intrinsic cost (tier1_select_candidates); an inner pattern costs
// nothing, its root's candidate paying for what it folds.
static const Tier1BursPattern tier1_burs_patterns[] = {
    // A value computed elsewhere: its own root's.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_REG, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_MUNCH, TIER1_RULE_NONE },
    // An operand of this leaf stays a separate root. Its only candidate supplies its own cost and machine operands.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_DIRECT, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_NONE },
    // The candidate generator uses the selected call kind to build ABI-specific operands and costs.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_CALL, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_CALL },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_CALL, 1, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_ASM },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_CALL, 2, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_ROW },
    // A constant: its move, or what a consumer folds of it by its class.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_CONST, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_CONSTANT },
    { TIER1_BURS_IMM, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_FIT, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_SCALE, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_SCALE, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_POW2, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_POW2, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_M359, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_M359, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_SHAMT, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_SHAMT, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ZERO, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_ZERO, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_DISP, TIER1_BURS_FAMILY_CONST, BUSTER_TIER1_BURS_DISP, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // A frame object: its lea, or a memory operand's base.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SLOT, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_STACK_ADDRESS },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_SLOT, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // Chains: a register is an address's base, and an index.
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_CHAIN, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_CHAIN, 0, { TIER1_BURS_A0, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_INDEX, TIER1_BURS_FAMILY_CHAIN, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // The ALU operations of registers, immediates (on the left where commutative and the right isn't a constant),
    // multiplications by constants, lea's additions of a constant or a shift left as its index, loads as memory operands
    // (on the left where commutative and the right isn't a load), and lea's additions of registers.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ALU, BUSTER_TIER1_BURS_ANY, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_REGISTER },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_IMMEDIATE, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_COMMUTATIVE_PLAIN, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 1, TIER1_BURS_ACTION_LEA_IMMEDIATE, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_LEA_IMMEDIATE, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_POW2 }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_POW2, TIER1_BURS_REG }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_M359 }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_LEA },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_M359, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_LEA },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_K(S), { TIER1_BURS_REG, TIER1_BURS_SHL3 }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_LEA_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_NOT_S, { TIER1_BURS_SHL3, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_LEA_SHIFT },
    // A memory form is two records at most, a copy of the other operand or the load into the destination, then the
    // operation (#2602): an addition's comes before the lea of two registers, the load's and the other's, whose cost it ties,
    // since it never writes more records and holds one register fewer. A subtraction's needs the scratch register where
    // the destination is in the address, and a multiplication's keeps its immediate form's penalty (#1217). A narrow
    // operation has none (#2677).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_COMMUTATIVE_MEMORY, BUSTER_TIER1_BURS_K(L), { TIER1_BURS_REG, TIER1_BURS_LOADF }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_COMMUTATIVE_MEMORY, BUSTER_TIER1_BURS_NOT_L, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SUB, BUSTER_TIER1_BURS_K(L), { TIER1_BURS_REG, TIER1_BURS_LOADF }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(L), { TIER1_BURS_REG, TIER1_BURS_LOADF }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_L, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_ANY, { TIER1_BURS_REG, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_LEA_REGISTER, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_SHL3, TIER1_BURS_FAMILY_SHL, 0, { TIER1_BURS_REG, TIER1_BURS_SCALE }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // Shifts, by a constant count below their width, or by a register's (cl).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SHIFT, 0, { TIER1_BURS_REG, TIER1_BURS_SHAMT }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SHIFT, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 4, TIER1_BURS_ACTION_KIND, TIER1_RULE_SHIFT },
    // A single-use integer constant converts at compile time. A shared constant keeps the register
    // conversion. Only the conversions with a memory encoding consume a foldable load.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_CONVERT, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 2, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_CONVERT, 1, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 2, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_CONVERT, 1, { TIER1_BURS_LOADF, BUSTER_TIER1_BURS_NO_KID }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_CONVERT_LOAD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_CONVERT, 2, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_CONSTANT },
    // The widening multiply has a register extension and, when range and use proofs allow it,
    // a signed 64-bit multiply with an immediate or register operand.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SIGNED_PRODUCT, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_SIGNED_PRODUCT, TIER1_RULE_NONE },
    // A single-use I32 arithmetic producer can write the requested narrow result directly.
    // The walk emits that alternative only when known bits and canonical storage permit it.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_NARROW, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_NARROW, TIER1_RULE_REGISTER },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_NARROW, 1, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_NARROW, TIER1_RULE_UNARY },
    // The x87 operations read and write their own frame objects. Their data operands are materialized by the pattern.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_X87, 0, { BUSTER_TIER1_BURS_NO_KID, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_X87 },
    // A signed widening reads a scalar load directly when its single use can fold
    // without crossing a memory barrier. Otherwise only the register extension remains.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SIGN_LOAD, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SIGN_LOAD, 0, { TIER1_BURS_LOADF, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_ADDRESS },
    // A floating binary operation reads a register or a foldable scalar load. When the right operand is a
    // load it always has priority. Addition and multiplication can otherwise take the left memory operand.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_BINARY, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_BINARY },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_BINARY, 1, { TIER1_BURS_REG, TIER1_BURS_LOADF }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_BINARY, 2, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FLOAT_LOAD, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_FLOAT_ADDRESS, TIER1_RULE_FLOAT_LOAD },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_FLOAT_STORE, 0, { TIER1_BURS_A1, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_FLOAT_ADDRESS, TIER1_RULE_FLOAT_STORE },
    // The address walk (tier1_select_address): 64-bit additions of a displacement, conversions between a pointer and its
    // bits, addresses without an index or with a constant one, and one address with an index, whose own walk peels the
    // index's constant additions and subtractions (tier1_select_index).
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_A0, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_DISP, TIER1_BURS_A0 }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_A1, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_DISP, TIER1_BURS_A1 }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_ANY, { TIER1_BURS_A0, TIER1_BURS_INDEX }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_INDEX, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_INDEX, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_INDEX, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_DISP, TIER1_BURS_INDEX }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_INDEX, TIER1_BURS_FAMILY_SUB64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_INDEX, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_CONV_PTR, 0, { TIER1_BURS_A0, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_CONV_PTR, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADDR_NOIDX, 0, { TIER1_BURS_A0, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADDR_NOIDX, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADDR_CIDX, 0, { TIER1_BURS_A0, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADDR_CIDX, 0, { TIER1_BURS_A1, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADDR_IDX, 0, { TIER1_BURS_A0, TIER1_BURS_INDEX }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // The roots that walk an address: an address's lea, a conversion's copy (of what its walk reached), a load and a
    // store, and a load as a consumer's memory operand.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADDR_NOIDX, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_ADDRESS },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADDR_CIDX, 0, { TIER1_BURS_A1, TIER1_BURS_DISP }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_ADDRESS },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADDR_IDX, 0, { TIER1_BURS_A0, TIER1_BURS_INDEX }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_ADDRESS },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_CONV_PTR, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_WALKED_COPY, TIER1_RULE_COPY },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_LOAD, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_LOAD, TIER1_RULE_LOAD },
    { TIER1_BURS_LOADF, TIER1_BURS_FAMILY_LOAD_F, 0, { TIER1_BURS_A1, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_STORE, 0, { TIER1_BURS_A1, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_STORE, TIER1_RULE_STORE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_STORE, 0, { TIER1_BURS_A1, TIER1_BURS_IMM }, 1, TIER1_BURS_ACTION_STORE_IMMEDIATE, TIER1_RULE_STORE },
    // Comparisons, their truth values' chains through C's `!= 0` and `== 0` (tier1_select_truth), and the branches that
    // fuse with them; and a zero extension's own move.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_IMM }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_LOADF }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_LOADF, TIER1_BURS_IMM }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE_IMMEDIATE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_IMM }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_LOADF }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_LOADF, TIER1_BURS_IMM }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP_LINK, 0, { TIER1_BURS_ZCOND, TIER1_BURS_ZERO }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_FCOND, TIER1_BURS_FAMILY_ICMP_LINK, 0, { TIER1_BURS_ZFCOND, TIER1_BURS_ZERO }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ZCOND, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ZFCOND, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_FCOND, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_FOLDED_COMPARE, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ZEXT, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_ZERO_EXTEND },
    { TIER1_BURS_FCOND, TIER1_BURS_FAMILY_FCMP_ORD, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_FCMP, TIER1_BURS_FAMILY_FCMP_ORD, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // A floating == or != (#2922): its truth value's own sequence, or a select's condition directly or through its copy,
    // through no truth value's chain, whose patterns would take the tables past BUSTER_TIER1_BURS_MAX_STATES (and which
    // tier1_select_candidates admits none of). No branch takes one: a single jump can't read both ZF and PF.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FCMP_EQ, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 5, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_COMPARE },
    { TIER1_BURS_ECOND, TIER1_BURS_FAMILY_FCMP_EQ, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ECMP, TIER1_BURS_FAMILY_FCMP_EQ, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FCMP_ORD, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_BRANCH_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_FCOND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_BRANCH_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_BRANCH },
    // A select of an integer comparison's condition code, cmp and cmovCC (#2676), and one that tests its truth value.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SELECT, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_SELECT_COMPARE },
    // A select of a floating comparison's flags, ucomis and cmovCC (#2791).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SELECT, 0, { TIER1_BURS_FCOND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_SELECT_COMPARE },
    // A select of a floating == or !='s flags, ucomis, cmovne and cmovp (#2922).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SELECT, 0, { TIER1_BURS_ECOND, BUSTER_TIER1_BURS_NO_KID }, 4, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_SELECT_COMPARE },
    // A floating comparison's I1 copy (#2791), which if-conversion makes of a test of it: a link of its chain (a select's
    // or a branch's), or its own move. It reads the comparison itself, not every state's FCOND, which would take the
    // tables past BUSTER_TIER1_BURS_MAX_STATES.
    { TIER1_BURS_FCOND, TIER1_BURS_FAMILY_COPY_TRUTH, 0, { TIER1_BURS_FCMP, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ECOND, TIER1_BURS_FAMILY_COPY_TRUTH, 0, { TIER1_BURS_ECMP, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_COPY_TRUTH, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_COPY },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SELECT, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_SELECT },
};

#define BUSTER_TIER1_BURS_PATTERNS (sizeof(tier1_burs_patterns) / sizeof(tier1_burs_patterns[0]))
#endif
// A state's costs are normalized (its cheapest nonterminal's is zero) and saturate at BUSTER_TIER1_BURS_BOUND: this grammar
// isn't finite-state without it, since folding an address's or a truth value's chain costs nothing while computing it
// into a register costs more for each node of it, so that a state of each chain length would be a state of its own
// (burg's generator fails on such a grammar; Proebsting, 1995). Within one node's choice, the alternatives that read a
// saturated cost read it at the same child, so they differ by what they fold of their other children, which the bound
// keeps. The generated counts depend on the grammar and test-tier1-select prints them. Going past a table's bound makes
// tier1_select_initialize fail and build.sh's bake with it. The tests compare every cover's cost with munch's.
#define BUSTER_TIER1_BURS_BOUND 8u
// The tables' bounds: the generator fails past them, which tier1_select_initialize reports.
#define BUSTER_TIER1_BURS_MAX_STATES 1088u
#define BUSTER_TIER1_BURS_MAX_REPRESENTERS 128u
#define BUSTER_TIER1_BURS_MAX_RULES 512u
// The pool of the operators' maps (state to representer) and transition tables, 16-bit entries, past its end the 16-bit
// pad that a 32-bit gather of its last entry reads.
#define BUSTER_TIER1_BURS_POOL 65536u
#define BUSTER_TIER1_BURS_INFINITE 255u
#define BUSTER_TIER1_BURS_SATURATED 254u
// The state of a value the patterns don't look into: the first state the generator makes, the REG leaf's.
#define BUSTER_TIER1_BURS_REG_STATE 0u

// A rule, the grammar's patterns expanded to their operators: `op` is BUSTER_TIER1_BURS_CHAIN for a chain, and
// TIER1_BURS_OP_CONST for each constant class whose bits have all of `requires`.
typedef struct Tier1BursRule Tier1BursRule;
struct Tier1BursRule
{
    uint16_t op;
    uint8_t requires;
    uint8_t lhs;
    uint8_t kids[2];
    uint8_t cost;
    uint8_t action;
    uint8_t kind;
};
#define BUSTER_TIER1_BURS_CHAIN 0xffffu

// The tables' linkage: main.c's units share them, the driver's unit's, as they share x86_64_encoder.c's templates
// (BUSTER_X86_SHARED), since a command's code runs in more than one unit (main.c): benchmark-tier1-select-source selects
// in the backend's unit and prepares its files through the checker's, whose emission selects with --tier1. A unit of its
// own, a benchmark's, has its own.
#if defined(BUSTER_UNIT) && BUSTER_UNIT != BUSTER_UNIT_DRIVER
#define BUSTER_TIER1_SHARED extern __attribute__((visibility("hidden")))
#elif defined(BUSTER_UNIT)
#define BUSTER_TIER1_SHARED __attribute__((visibility("hidden")))
#else
#define BUSTER_TIER1_SHARED static
#endif

// The tables: the rules, each state's rule per nonterminal (0 for none: rule 0 is never a derivation), and per operator
// its arity, its maps' and its table's places in the pool and its table's row width (its second position's
// representers). An operator of arity 0 or 1 has the zero map at its missing positions.
BUSTER_TIER1_SHARED BUSTER_BAKED Tier1BursRule tier1_burs_rules[BUSTER_TIER1_BURS_MAX_RULES];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_rule_count;
BUSTER_TIER1_SHARED BUSTER_BAKED uint8_t tier1_burs_state_rules[BUSTER_TIER1_BURS_MAX_STATES * TIER1_BURS_NONTERMINALS + 4];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_state_count;
BUSTER_TIER1_SHARED BUSTER_BAKED uint8_t tier1_burs_arity[TIER1_BURS_OPERATORS + 4];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_map_base[2][TIER1_BURS_OPERATORS];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_table_base[TIER1_BURS_OPERATORS];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_table_width[TIER1_BURS_OPERATORS];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_register_state[TIER1_BURS_OPERATORS];
BUSTER_TIER1_SHARED BUSTER_BAKED uint16_t tier1_burs_pool[BUSTER_TIER1_BURS_POOL + 2];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_pool_length;
// Each operator's goal at a root: a statement's (a store and a branch) or a register's.
BUSTER_TIER1_SHARED BUSTER_BAKED uint8_t tier1_burs_goal[TIER1_BURS_OPERATORS + 4];
// 0 before tier1_select_initialize, 1 while one thread builds, 2 built and 3 failed.
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_status;

#ifndef BUSTER_TIER1_BURS_STORAGE_ONLY
// Whether a pattern of `family` (with `kinds`) derives from operator `op`.
static bool tier1_burs_member(uint32_t family, uint32_t kinds, uint32_t op)
{
    const bool alu = op >= TIER1_BURS_OP_ALU && op < TIER1_BURS_OP_SHL;
    const uint32_t operation = alu ? (op - TIER1_BURS_OP_ALU) / BUSTER_TIER1_BURS_KINDS : UINT32_MAX;
    const uint32_t kind = alu ? (op - TIER1_BURS_OP_ALU) % BUSTER_TIER1_BURS_KINDS : UINT32_MAX;
    const bool kinded = alu && (kinds == 0 || ((kinds >> kind) & 1) != 0);
    const bool add = operation == BUSTER_TIER1_BURS_ADD32 || operation == BUSTER_TIER1_BURS_ADD64;
    const bool commutative = add || operation == BUSTER_TIER1_BURS_MUL || operation == BUSTER_TIER1_BURS_AND || operation == BUSTER_TIER1_BURS_OR || operation == BUSTER_TIER1_BURS_XOR ||
                             operation == BUSTER_TIER1_BURS_NARROW;
    // An addition alone is of kind S: another operation whose right operand shifts is of kind O.
    const bool real = alu && (kind != BUSTER_TIER1_BURS_KIND_S || add);
    bool member = false;
    switch (family)
    {
    case TIER1_BURS_FAMILY_CALL:
        member = op == TIER1_BURS_OP_CALL + kinds;
        break;
    case TIER1_BURS_FAMILY_DIRECT:
        member = op == TIER1_BURS_OP_DIRECT;
        break;
    case TIER1_BURS_FAMILY_REG:
        member = op == TIER1_BURS_OP_REG;
        break;
    case TIER1_BURS_FAMILY_SLOT:
        member = op == TIER1_BURS_OP_SLOT || op == TIER1_BURS_OP_SLOT_X;
        break;
    case TIER1_BURS_FAMILY_CONST:
        member = op == TIER1_BURS_OP_CONST;
        break;
    case TIER1_BURS_FAMILY_ALU:
        member = real && kinded;
        break;
    case TIER1_BURS_FAMILY_ADD:
        member = real && kinded && add;
        break;
    case TIER1_BURS_FAMILY_ADD64:
        member = real && kinded && operation == BUSTER_TIER1_BURS_ADD64;
        break;
    case TIER1_BURS_FAMILY_SUB:
        member = real && kinded && (operation == BUSTER_TIER1_BURS_SUB32 || operation == BUSTER_TIER1_BURS_SUB64);
        break;
    case TIER1_BURS_FAMILY_SUB64:
        member = real && kinded && operation == BUSTER_TIER1_BURS_SUB64;
        break;
    case TIER1_BURS_FAMILY_MUL:
        member = real && kinded && operation == BUSTER_TIER1_BURS_MUL;
        break;
    case TIER1_BURS_FAMILY_COMMUTATIVE:
        member = real && kinded && commutative;
        break;
    case TIER1_BURS_FAMILY_IMMEDIATE:
        member = real && kinded && operation != BUSTER_TIER1_BURS_MUL;
        break;
    case TIER1_BURS_FAMILY_COMMUTATIVE_PLAIN:
        member = real && kinded && commutative && operation != BUSTER_TIER1_BURS_MUL;
        break;
    case TIER1_BURS_FAMILY_COMMUTATIVE_MEMORY:
        member = real && kinded && commutative && operation != BUSTER_TIER1_BURS_MUL && operation != BUSTER_TIER1_BURS_NARROW;
        break;
    case TIER1_BURS_FAMILY_SHIFT:
        member = op == TIER1_BURS_OP_SHL || op == TIER1_BURS_OP_LSHR || op == TIER1_BURS_OP_ASHR;
        break;
    case TIER1_BURS_FAMILY_SHL:
        member = op == TIER1_BURS_OP_SHL;
        break;
    case TIER1_BURS_FAMILY_FLOAT_CONVERT:
        member = op == TIER1_BURS_OP_FLOAT_CONVERT + kinds;
        break;
    case TIER1_BURS_FAMILY_SIGNED_PRODUCT:
        member = op == TIER1_BURS_OP_SIGNED_PRODUCT;
        break;
    case TIER1_BURS_FAMILY_NARROW:
        member = op == TIER1_BURS_OP_NARROW_BINARY + kinds;
        break;
    case TIER1_BURS_FAMILY_X87:
        member = op == TIER1_BURS_OP_X87;
        break;
    case TIER1_BURS_FAMILY_SIGN_LOAD:
        member = op == TIER1_BURS_OP_SIGN_LOAD;
        break;
    case TIER1_BURS_FAMILY_FLOAT_BINARY:
        member = (op == TIER1_BURS_OP_FLOAT_BINARY_RIGHT && kinds != 2) || (op == TIER1_BURS_OP_FLOAT_BINARY_LEFT && kinds != 1);
        break;
    case TIER1_BURS_FAMILY_FLOAT_LOAD:
        member = op == TIER1_BURS_OP_FLOAT_LOAD_F || op == TIER1_BURS_OP_FLOAT_LOAD_N;
        break;
    case TIER1_BURS_FAMILY_FLOAT_STORE:
        member = op == TIER1_BURS_OP_FLOAT_STORE;
        break;
    case TIER1_BURS_FAMILY_LOAD:
        member = op == TIER1_BURS_OP_LOAD_F || op == TIER1_BURS_OP_LOAD_N;
        break;
    case TIER1_BURS_FAMILY_LOAD_F:
        member = op == TIER1_BURS_OP_LOAD_F || op == TIER1_BURS_OP_FLOAT_LOAD_F;
        break;
    case TIER1_BURS_FAMILY_STORE:
        member = op == TIER1_BURS_OP_STORE;
        break;
    case TIER1_BURS_FAMILY_ADDR_NOIDX:
        member = op == TIER1_BURS_OP_ADDR_NOIDX;
        break;
    case TIER1_BURS_FAMILY_ADDR_CIDX:
        member = op == TIER1_BURS_OP_ADDR_CIDX;
        break;
    case TIER1_BURS_FAMILY_ADDR_IDX:
        member = op == TIER1_BURS_OP_ADDR_IDX;
        break;
    case TIER1_BURS_FAMILY_CONV_PTR:
        member = op == TIER1_BURS_OP_CONV_PTR;
        break;
    case TIER1_BURS_FAMILY_ICMP:
        member = op == TIER1_BURS_OP_ICMP || op == TIER1_BURS_OP_ICMP_LINK;
        break;
    case TIER1_BURS_FAMILY_ICMP_LINK:
        member = op == TIER1_BURS_OP_ICMP_LINK;
        break;
    case TIER1_BURS_FAMILY_ZEXT_TRUTH:
        member = op == TIER1_BURS_OP_ZEXT_TRUTH;
        break;
    case TIER1_BURS_FAMILY_ZEXT:
        member = op == TIER1_BURS_OP_ZEXT_TRUTH || op == TIER1_BURS_OP_ZEXT_PLAIN;
        break;
    case TIER1_BURS_FAMILY_FCMP_ORD:
        member = op == TIER1_BURS_OP_FCMP_ORD;
        break;
    case TIER1_BURS_FAMILY_FCMP_EQ:
        member = op == TIER1_BURS_OP_FCMP_EQ;
        break;
    case TIER1_BURS_FAMILY_CBR_ADJ:
        member = op == TIER1_BURS_OP_CBR_ADJ;
        break;
    case TIER1_BURS_FAMILY_SELECT:
        member = op == TIER1_BURS_OP_SELECT;
        break;
    case TIER1_BURS_FAMILY_COPY_TRUTH:
        member = op == TIER1_BURS_OP_COPY_TRUTH;
        break;
    default:
        break;
    }
    return member;
}

// The generator's working state, in an arena of its own: its states' costs (normalized, BUSTER_TIER1_BURS_INFINITE for
// none) and rules; each operator position's representers' costs and the map of every state to one; and each operator's
// transitions, a representer pair's state, before they are compacted into the pool.
typedef struct Tier1BursBuilder Tier1BursBuilder;
struct Tier1BursBuilder
{
    uint8_t (*costs)[TIER1_BURS_NONTERMINALS];
    uint16_t (*rules)[TIER1_BURS_NONTERMINALS];
    uint32_t state_count;
    // Per operator and position: the nonterminals its patterns read there (a bit each), its representers and the map.
    uint32_t relevant[2][TIER1_BURS_OPERATORS];
    uint8_t (*representers)[BUSTER_TIER1_BURS_MAX_REPRESENTERS][TIER1_BURS_NONTERMINALS];
    uint32_t* representer_counts;
    uint16_t* maps;
    uint16_t* transitions;
    bool valid;
};

static uint32_t tier1_burs_position(uint32_t op, uint32_t position)
{
    return position * TIER1_BURS_OPERATORS + op;
}

// The state of `costs` and `rules` after the chain rules' closure, normalized: an existing state's index, or a new one's.
static uint32_t tier1_burs_intern(Tier1BursBuilder* builder, uint8_t* costs, uint16_t* rules)
{
    bool changed = true;
    for (uint32_t round = 0; changed && round < TIER1_BURS_NONTERMINALS; round += 1)
    {
        changed = false;
        for (uint32_t r = 1; r < tier1_burs_rule_count; r += 1)
        {
            const Tier1BursRule* rule = &tier1_burs_rules[r];
            const uint32_t through = rule->op == BUSTER_TIER1_BURS_CHAIN && costs[rule->kids[0]] != BUSTER_TIER1_BURS_INFINITE ? (uint32_t)costs[rule->kids[0]] + rule->cost : UINT32_MAX;
            if (through < costs[rule->lhs])
            {
                costs[rule->lhs] = (uint8_t)(through > BUSTER_TIER1_BURS_SATURATED ? BUSTER_TIER1_BURS_SATURATED : through);
                rules[rule->lhs] = (uint16_t)r;
                changed = true;
            }
        }
    }
    uint32_t minimum = BUSTER_TIER1_BURS_INFINITE;
    for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
    {
        minimum = costs[k] < minimum ? costs[k] : minimum;
    }
    for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
    {
        const uint32_t delta = costs[k] - minimum;
        costs[k] = costs[k] == BUSTER_TIER1_BURS_INFINITE ? BUSTER_TIER1_BURS_INFINITE : (uint8_t)(delta > BUSTER_TIER1_BURS_BOUND ? BUSTER_TIER1_BURS_BOUND : delta);
        rules[k] = costs[k] == BUSTER_TIER1_BURS_INFINITE ? 0 : rules[k];
    }
    uint32_t found = UINT32_MAX;
    for (uint32_t s = 0; found == UINT32_MAX && s < builder->state_count; s += 1)
    {
        found = memcmp(builder->costs[s], costs, TIER1_BURS_NONTERMINALS) == 0 && memcmp(builder->rules[s], rules, TIER1_BURS_NONTERMINALS * sizeof(uint16_t)) == 0 ? s : UINT32_MAX;
    }
    if (found == UINT32_MAX && builder->state_count < BUSTER_TIER1_BURS_MAX_STATES)
    {
        found = builder->state_count;
        memcpy(builder->costs[found], costs, TIER1_BURS_NONTERMINALS);
        memcpy(builder->rules[found], rules, TIER1_BURS_NONTERMINALS * sizeof(uint16_t));
        builder->state_count += 1;
    }
    builder->valid &= found != UINT32_MAX;
    return found != UINT32_MAX ? found : 0;
}

// The state an operator's patterns derive from its children's representers `left` and `right` (each a representer's
// costs, or null at a position the operator doesn't have).
static uint32_t tier1_burs_derive(Tier1BursBuilder* builder, uint32_t op, const uint8_t* left, const uint8_t* right)
{
    uint8_t costs[TIER1_BURS_NONTERMINALS];
    uint16_t rules[TIER1_BURS_NONTERMINALS];
    memset(costs, BUSTER_TIER1_BURS_INFINITE, sizeof(costs));
    memset(rules, 0, sizeof(rules));
    const bool constant = op >= TIER1_BURS_OP_CONST && op < TIER1_BURS_OP_ALU;
    for (uint32_t r = 1; r < tier1_burs_rule_count; r += 1)
    {
        const Tier1BursRule* rule = &tier1_burs_rules[r];
        const bool applies = constant ? rule->op == TIER1_BURS_OP_CONST && ((op - TIER1_BURS_OP_CONST) & rule->requires) == rule->requires : rule->op == op;
        uint32_t cost = applies ? rule->cost : UINT32_MAX;
        for (uint32_t k = 0; cost != UINT32_MAX && k < tier1_burs_arity[op]; k += 1)
        {
            const uint8_t* child = k == 0 ? left : right;
            cost = child[rule->kids[k]] == BUSTER_TIER1_BURS_INFINITE ? UINT32_MAX : cost + child[rule->kids[k]];
        }
        if (cost < costs[rule->lhs])
        {
            costs[rule->lhs] = (uint8_t)(cost > BUSTER_TIER1_BURS_SATURATED ? BUSTER_TIER1_BURS_SATURATED : cost);
            rules[rule->lhs] = (uint16_t)r;
        }
    }
    return tier1_burs_intern(builder, costs, rules);
}

// State `s` projected onto what operator `op` reads at `position`, normalized: its representer there, an existing one or
// a new one (and then *fresh).
static uint32_t tier1_burs_represent(Tier1BursBuilder* builder, uint32_t op, uint32_t position, uint32_t s, bool* fresh)
{
    const uint32_t slot = tier1_burs_position(op, position);
    uint8_t projected[TIER1_BURS_NONTERMINALS];
    uint32_t minimum = BUSTER_TIER1_BURS_INFINITE;
    for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
    {
        projected[k] = ((builder->relevant[position][op] >> k) & 1) != 0 ? builder->costs[s][k] : BUSTER_TIER1_BURS_INFINITE;
        minimum = projected[k] < minimum ? projected[k] : minimum;
    }
    for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
    {
        projected[k] = projected[k] == BUSTER_TIER1_BURS_INFINITE ? BUSTER_TIER1_BURS_INFINITE : (uint8_t)(projected[k] - minimum);
    }
    uint32_t found = UINT32_MAX;
    for (uint32_t r = 0; found == UINT32_MAX && r < builder->representer_counts[slot]; r += 1)
    {
        found = memcmp(builder->representers[slot][r], projected, TIER1_BURS_NONTERMINALS) == 0 ? r : UINT32_MAX;
    }
    *fresh = found == UINT32_MAX;
    if (found == UINT32_MAX && builder->representer_counts[slot] < BUSTER_TIER1_BURS_MAX_REPRESENTERS)
    {
        found = builder->representer_counts[slot];
        memcpy(builder->representers[slot][found], projected, TIER1_BURS_NONTERMINALS);
        builder->representer_counts[slot] += 1;
    }
    builder->valid &= found != UINT32_MAX;
    return found != UINT32_MAX ? found : 0;
}

static uint16_t* tier1_burs_transition(Tier1BursBuilder* builder, uint32_t op, uint32_t left, uint32_t right)
{
    return &builder->transitions[((uint64_t)op * BUSTER_TIER1_BURS_MAX_REPRESENTERS + left) * BUSTER_TIER1_BURS_MAX_REPRESENTERS + right];
}

// The generator's working memory: its columns' bytes, each 64-byte aligned, in one mapping.
#define BUSTER_TIER1_BURS_COSTS_BYTES ((uint64_t)BUSTER_TIER1_BURS_MAX_STATES * TIER1_BURS_NONTERMINALS)
#define BUSTER_TIER1_BURS_RULES_BYTES ((uint64_t)BUSTER_TIER1_BURS_MAX_STATES * TIER1_BURS_NONTERMINALS * sizeof(uint16_t))
#define BUSTER_TIER1_BURS_REPRESENTERS_BYTES ((uint64_t)2 * TIER1_BURS_OPERATORS * BUSTER_TIER1_BURS_MAX_REPRESENTERS * TIER1_BURS_NONTERMINALS)
#define BUSTER_TIER1_BURS_COUNTS_BYTES (((uint64_t)2 * TIER1_BURS_OPERATORS * sizeof(uint32_t) + 63) & ~UINT64_C(63))
#define BUSTER_TIER1_BURS_MAPS_BYTES ((uint64_t)2 * TIER1_BURS_OPERATORS * BUSTER_TIER1_BURS_MAX_STATES * sizeof(uint16_t))
#define BUSTER_TIER1_BURS_TRANSITIONS_BYTES ((uint64_t)TIER1_BURS_OPERATORS * BUSTER_TIER1_BURS_MAX_REPRESENTERS * BUSTER_TIER1_BURS_MAX_REPRESENTERS * sizeof(uint16_t))
#define BUSTER_TIER1_BURS_WORKING_BYTES                                                                                                                                     \
    (BUSTER_TIER1_BURS_COSTS_BYTES + BUSTER_TIER1_BURS_RULES_BYTES + BUSTER_TIER1_BURS_REPRESENTERS_BYTES + BUSTER_TIER1_BURS_COUNTS_BYTES + BUSTER_TIER1_BURS_MAPS_BYTES + \
     BUSTER_TIER1_BURS_TRANSITIONS_BYTES)

// The rules from the patterns, the states by burg's fixpoint, then the pool: each operator's maps (identical ones
// shared) and its table of exactly its representers' pairs. `working` is BUSTER_TIER1_BURS_WORKING_BYTES of zeros.
static bool tier1_burs_build(uint8_t* working)
{
    Tier1BursBuilder builder = { .valid = true };
    uint8_t* next = working;
    builder.costs = (uint8_t(*)[TIER1_BURS_NONTERMINALS])next;
    next += BUSTER_TIER1_BURS_COSTS_BYTES;
    builder.rules = (uint16_t(*)[TIER1_BURS_NONTERMINALS])next;
    next += BUSTER_TIER1_BURS_RULES_BYTES;
    builder.representers = (uint8_t(*)[BUSTER_TIER1_BURS_MAX_REPRESENTERS][TIER1_BURS_NONTERMINALS])next;
    next += BUSTER_TIER1_BURS_REPRESENTERS_BYTES;
    builder.representer_counts = (uint32_t*)next;
    next += BUSTER_TIER1_BURS_COUNTS_BYTES;
    builder.maps = (uint16_t*)next;
    next += BUSTER_TIER1_BURS_MAPS_BYTES;
    builder.transitions = (uint16_t*)next;
    // The rules: rule 0 a placeholder no derivation names, then each pattern for each operator of its family.
    tier1_burs_rule_count = 1;
    memset(&tier1_burs_rules[0], 0, sizeof(tier1_burs_rules[0]));
    for (uint32_t op = 0; op < TIER1_BURS_OPERATORS; op += 1)
    {
        tier1_burs_arity[op] = 0;
        tier1_burs_goal[op] = op == TIER1_BURS_OP_STORE || op == TIER1_BURS_OP_FLOAT_STORE || op == TIER1_BURS_OP_CBR_ADJ ? TIER1_BURS_STMT : TIER1_BURS_REG;
    }
    for (uint32_t p = 0; builder.valid && p < BUSTER_TIER1_BURS_PATTERNS; p += 1)
    {
        const Tier1BursPattern* pattern = &tier1_burs_patterns[p];
        for (uint32_t op = 0; builder.valid && op < (pattern->family == TIER1_BURS_FAMILY_CHAIN ? 1u : TIER1_BURS_OPERATORS); op += 1)
        {
            if (pattern->family == TIER1_BURS_FAMILY_CHAIN || tier1_burs_member(pattern->family, pattern->kinds, op))
            {
                builder.valid = tier1_burs_rule_count < BUSTER_TIER1_BURS_MAX_RULES;
                Tier1BursRule* rule = &tier1_burs_rules[builder.valid ? tier1_burs_rule_count : 0];
                *rule = (Tier1BursRule){
                    .op = pattern->family == TIER1_BURS_FAMILY_CHAIN ? BUSTER_TIER1_BURS_CHAIN : (uint16_t)op,
                    .requires = pattern->family == TIER1_BURS_FAMILY_CONST ? pattern->kinds : 0,
                    .lhs = pattern->lhs,
                    .kids = { pattern->kids[0], pattern->kids[1] },
                    .cost = pattern->cost,
                    .action = pattern->action,
                    .kind = pattern->kind,
                };
                tier1_burs_rule_count += builder.valid ? 1 : 0;
                // A production names exactly the children it reads. The operator's arity is the
                // maximum across its productions, including those with no node operands.
                for (uint32_t k = 0; pattern->family != TIER1_BURS_FAMILY_CHAIN && k < 2; k += 1)
                {
                    if (pattern->kids[k] < TIER1_BURS_NONTERMINALS)
                    {
                        builder.relevant[k][op] |= 1u << pattern->kids[k];
                        if (tier1_burs_arity[op] <= k)
                        {
                            tier1_burs_arity[op] = (uint8_t)(k + 1);
                        }
                    }
                }
            }
        }
    }
    // The leaves' states, the REG leaf's first; then burg's fixpoint: each state's representers at every operator's
    // positions, and for each new representer, its table's pairs with the other position's representers.
    for (uint32_t op = 0; builder.valid && op < TIER1_BURS_OPERATORS; op += 1)
    {
        if (tier1_burs_arity[op] == 0)
        {
            tier1_burs_table_base[op] = tier1_burs_derive(&builder, op, 0, 0);
        }
    }
    builder.valid &= builder.valid && tier1_burs_table_base[TIER1_BURS_OP_REG] == BUSTER_TIER1_BURS_REG_STATE;
    for (uint32_t s = 0; builder.valid && s < builder.state_count; s += 1)
    {
        for (uint32_t op = 0; builder.valid && op < TIER1_BURS_OPERATORS; op += 1)
        {
            for (uint32_t position = 0; builder.valid && position < tier1_burs_arity[op]; position += 1)
            {
                bool fresh = false;
                const uint32_t r = tier1_burs_represent(&builder, op, position, s, &fresh);
                builder.maps[(uint64_t)tier1_burs_position(op, position) * BUSTER_TIER1_BURS_MAX_STATES + s] = (uint16_t)r;
                const uint32_t other = tier1_burs_arity[op] == 2 ? builder.representer_counts[tier1_burs_position(op, position ^ 1)] : 1;
                for (uint32_t o = 0; fresh && builder.valid && o < other; o += 1)
                {
                    const uint32_t left = position == 0 ? r : o;
                    const uint32_t right = position == 0 ? o : r;
                    const uint8_t* left_costs = builder.representers[tier1_burs_position(op, 0)][left];
                    const uint8_t* right_costs = tier1_burs_arity[op] == 2 ? builder.representers[tier1_burs_position(op, 1)][right] : 0;
                    *tier1_burs_transition(&builder, op, left, right) = (uint16_t)tier1_burs_derive(&builder, op, left_costs, right_costs);
                }
            }
        }
    }
    // The pool: the zero map, then each operator position's map (an identical earlier one's place where there is one),
    // then each operator's table. A leaf's table is its state alone.
    uint32_t length = BUSTER_TIER1_BURS_MAX_STATES;
    builder.valid &= length <= BUSTER_TIER1_BURS_POOL;
    if (builder.valid)
    {
        memset(tier1_burs_pool, 0, sizeof(tier1_burs_pool));
    }
    for (uint32_t op = 0; builder.valid && op < TIER1_BURS_OPERATORS; op += 1)
    {
        for (uint32_t position = 0; builder.valid && position < 2; position += 1)
        {
            uint32_t place = 0;
            if (position < tier1_burs_arity[op])
            {
                const uint16_t* map = &builder.maps[(uint64_t)tier1_burs_position(op, position) * BUSTER_TIER1_BURS_MAX_STATES];
                place = UINT32_MAX;
                for (uint32_t earlier = 0; place == UINT32_MAX && earlier < op * 2 + position; earlier += 1)
                {
                    const uint32_t earlier_op = earlier / 2;
                    const uint32_t earlier_position = earlier % 2;
                    const uint32_t base = tier1_burs_map_base[earlier_position][earlier_op];
                    place = base != 0 && memcmp(&tier1_burs_pool[base], map, (uint64_t)builder.state_count * sizeof(uint16_t)) == 0 ? base : UINT32_MAX;
                }
                if (place == UINT32_MAX)
                {
                    builder.valid = length + builder.state_count <= BUSTER_TIER1_BURS_POOL;
                    place = length;
                    if (builder.valid)
                    {
                        memcpy(&tier1_burs_pool[place], map, (uint64_t)builder.state_count * sizeof(uint16_t));
                        length += builder.state_count;
                    }
                }
            }
            tier1_burs_map_base[position][op] = place;
        }
        const uint32_t rows = tier1_burs_arity[op] >= 1 ? builder.representer_counts[tier1_burs_position(op, 0)] : 1;
        const uint32_t width = tier1_burs_arity[op] == 2 ? builder.representer_counts[tier1_burs_position(op, 1)] : 1;
        builder.valid &= length + rows * width <= BUSTER_TIER1_BURS_POOL;
        for (uint32_t row = 0; builder.valid && row < rows; row += 1)
        {
            for (uint32_t column = 0; column < width; column += 1)
            {
                tier1_burs_pool[length + row * width + column] = tier1_burs_arity[op] == 0 ? (uint16_t)tier1_burs_table_base[op] : *tier1_burs_transition(&builder, op, row, column);
            }
        }
        if (builder.valid)
        {
            const uint32_t left = tier1_burs_pool[tier1_burs_map_base[0][op] + BUSTER_TIER1_BURS_REG_STATE];
            const uint32_t right = tier1_burs_pool[tier1_burs_map_base[1][op] + BUSTER_TIER1_BURS_REG_STATE];
            tier1_burs_register_state[op] = tier1_burs_pool[length + left * width + right];
        }
        tier1_burs_table_base[op] = length;
        tier1_burs_table_width[op] = width;
        length += builder.valid ? rows * width : 0;
    }
    tier1_burs_pool_length = length;
    // Each state's rule per nonterminal, 8 bits: the rules are fewer than 256 (the bound checks it).
    builder.valid &= tier1_burs_rule_count <= 256;
    for (uint32_t s = 0; builder.valid && s < builder.state_count; s += 1)
    {
        for (uint32_t k = 0; k < TIER1_BURS_NONTERMINALS; k += 1)
        {
            tier1_burs_state_rules[s * TIER1_BURS_NONTERMINALS + k] = (uint8_t)builder.rules[s][k];
        }
    }
    tier1_burs_state_count = builder.state_count;
    if (!builder.valid)
    {
        uint32_t max_representers = 0;
        for (uint32_t slot = 0; slot < 2 * TIER1_BURS_OPERATORS; slot += 1)
        {
            const uint32_t count = builder.representer_counts[slot];
            max_representers = count > max_representers ? count : max_representers;
        }
        fprintf(stderr, "tier1 BURS table generation failed: states %u/%u, representers %u/%u, pool %u/%u, rules %u/256\n",
            builder.state_count, BUSTER_TIER1_BURS_MAX_STATES, max_representers, BUSTER_TIER1_BURS_MAX_REPRESENTERS,
            length, BUSTER_TIER1_BURS_POOL, tier1_burs_rule_count);
    }
    return builder.valid;
}

// Builds the selector's state tables once (#141): both host paths' plans need them, and a baked executable has them
// (baked.c). Any thread may call it; one builds while the others wait for it.
static __attribute__((unused)) bool tier1_select_initialize(void)
{
    uint32_t status = __atomic_load_n(&tier1_burs_status, __ATOMIC_ACQUIRE);
    uint32_t expected = 0;
    if (status == 0 && __atomic_compare_exchange_n(&tier1_burs_status, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        // A mapping of its own: the bake (baked.c) runs this in a process whose image may be written only in the
        // buster_baked section.
        void* working = mmap(0, BUSTER_TIER1_BURS_WORKING_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool built = working != MAP_FAILED && tier1_burs_build(working);
        if (working != MAP_FAILED)
        {
            munmap(working, BUSTER_TIER1_BURS_WORKING_BYTES);
        }
        status = built ? 2 : 3;
        __atomic_store_n(&tier1_burs_status, status, __ATOMIC_RELEASE);
    }
    while (status <= 1)
    {
        _mm_pause();
        status = __atomic_load_n(&tier1_burs_status, __ATOMIC_ACQUIRE);
    }
    return status == 2;
}

// Whether the state tables are built: a baked executable starts with them (test-baked-tables).
static __attribute__((unused)) bool tier1_select_initialized(void)
{
    return __atomic_load_n(&tier1_burs_status, __ATOMIC_ACQUIRE) == 2;
}

// The vector classifier gathers an address's offset and its scale from its 16-byte record.
_Static_assert(sizeof(IrAddress) == 16 && offsetof(IrAddress, offset) == 0 && offsetof(IrAddress, scale) == 8, "IrAddress's layout, which tier1_burs_classify_vector gathers");

// The vector classifier gathers a frame object's size and its record's second qword (the alignment, the type and the
// flags) from its 16-byte slot record.
_Static_assert(sizeof(IrSlot) == 16 && offsetof(IrSlot, size) == 0 && offsetof(IrSlot, alignment) == 8 && offsetof(IrSlot, type) == 12 && offsetof(IrSlot, flags) == 13,
    "IrSlot's layout, which tier1_burs_classify_vector gathers");

// A floating == or !='s operator (#2922): FCMP_EQ, which a select takes by two cmovs where it is the select's condition
// directly or through its I1 copy right after it, but the REG leaf's, as on #2791, where that select's values recur through
// it (tier1_select_recurrent): the select tests its truth value, munch's TIER1_RULE_FLOAT_COMPARE, as
// tier1_select_candidates makes no other candidate of it. Such a comparison's last user is the select or the copy, its
// only one where the select may fold it (a tree child). The classifier's one case a lane at a time: the walk is candidate
// generation's too. `users` is each single-use node's user.
static __attribute__((noinline)) uint32_t tier1_burs_equality(const Tier1SelectForest* forest, const uint32_t* users, uint32_t node)
{
    const IrFunction* function = forest->function;
    const uint32_t count = function->instruction_count;
    const uint32_t first = users[node];
    const bool copied = first < count && function->op[first] == IR_COPY && function->type[first] == IR_I1 && function->a[first] == node && node + 1 == first;
    const uint32_t user = copied ? users[first] : first;
    const bool select = user < count && function->op[user] == IR_SELECT && function->a[user] == (copied ? first : node);
    return select && tier1_select_recurrent(function, user) ? TIER1_BURS_OP_REG : TIER1_BURS_OP_FCMP_EQ;
}

// Initial class by IR opcode. The low values encode ALU indices plus one; zero leaves an operation
// for classification from its operands. Direct leaves have a sole candidate even when its concrete
// rule depends on its parameter, phi or return type.
#define BUSTER_TIER1_BURS_DIRECT 0x80u
#define BUSTER_TIER1_BURS_FLOAT_CONVERSION 0x81u
static const uint8_t tier1_burs_opcode_class[BUSTER_IR_OP_TABLE] = {
    [IR_ADD] = 1 + BUSTER_TIER1_BURS_ADD32, [IR_SUB] = 1 + BUSTER_TIER1_BURS_SUB32,
    [IR_MUL] = 1 + BUSTER_TIER1_BURS_MUL, [IR_AND] = 1 + BUSTER_TIER1_BURS_AND,
    [IR_OR] = 1 + BUSTER_TIER1_BURS_OR, [IR_XOR] = 1 + BUSTER_TIER1_BURS_XOR,
    [IR_BR] = BUSTER_TIER1_BURS_DIRECT, [IR_BR_INDIRECT] = BUSTER_TIER1_BURS_DIRECT,
    [IR_SYMBOL_ADDR] = BUSTER_TIER1_BURS_DIRECT, [IR_LIFETIME] = BUSTER_TIER1_BURS_DIRECT,
    [IR_UNREACHABLE] = BUSTER_TIER1_BURS_DIRECT, [IR_FENCE] = BUSTER_TIER1_BURS_DIRECT,
    [IR_STACK_SAVE] = BUSTER_TIER1_BURS_DIRECT,
    [IR_FTRUNC] = BUSTER_TIER1_BURS_FLOAT_CONVERSION, [IR_FEXT] = BUSTER_TIER1_BURS_FLOAT_CONVERSION,
    [IR_SITOFP] = BUSTER_TIER1_BURS_FLOAT_CONVERSION, [IR_UITOFP] = BUSTER_TIER1_BURS_FLOAT_CONVERSION,
    [IR_FPTOSI] = BUSTER_TIER1_BURS_FLOAT_CONVERSION, [IR_FPTOUI] = BUSTER_TIER1_BURS_FLOAT_CONVERSION,
    [IR_BITCAST] = BUSTER_TIER1_BURS_FLOAT_CONVERSION,
};

// Whether each lane's type (32-bit lanes) is in the set `types`, a bit per IrType.
static __mmask16 tier1_burs_types(__mmask16 active, __m512i type, uint32_t types)
{
    const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), type);
    return _mm512_mask_test_epi32_mask(active, bit, _mm512_set1_epi32((int)types));
}

// The constant class bits of 8 values at once, each with its width in bytes.
static __m256i tier1_burs_constant_bits_vector(__m512i value, __m512i width)
{
    const __m512i zero = _mm512_setzero_si512();
    const __m512i bits_wide = _mm512_slli_epi64(width, 3);
    const __mmask8 extended = _mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(value, 32), 32), value);
    const __mmask8 narrow = _mm512_cmpeq_epi64_mask(width, _mm512_set1_epi64(4));
    const __mmask8 nonzero = _mm512_cmpneq_epi64_mask(value, zero);
    const __mmask8 single = _mm512_mask_cmpeq_epi64_mask(nonzero, _mm512_and_si512(value, _mm512_sub_epi64(value, _mm512_set1_epi64(1))), zero);
    // A power of two's log2 is its predecessor's population count.
    const __mmask8 power = _mm512_mask_cmplt_epu64_mask(single, _mm512_popcnt_epi64(_mm512_sub_epi64(value, _mm512_set1_epi64(1))), bits_wide);
    const __mmask8 small = _mm512_cmple_epu64_mask(value, _mm512_set1_epi64(3));
    const __mmask8 m359 = _mm512_cmpeq_epi64_mask(value, _mm512_set1_epi64(3)) | _mm512_cmpeq_epi64_mask(value, _mm512_set1_epi64(5)) | _mm512_cmpeq_epi64_mask(value, _mm512_set1_epi64(9));
    const __mmask8 shamt = _mm512_cmplt_epu64_mask(value, bits_wide);
    __m512i bits = _mm512_maskz_set1_epi64(narrow | extended, BUSTER_TIER1_BURS_FIT);
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64(small, BUSTER_TIER1_BURS_SCALE));
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64(power, BUSTER_TIER1_BURS_POW2));
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64(m359, BUSTER_TIER1_BURS_M359));
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64(shamt, BUSTER_TIER1_BURS_SHAMT));
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64((__mmask8)~nonzero, BUSTER_TIER1_BURS_ZERO));
    bits = _mm512_or_si512(bits, _mm512_maskz_set1_epi64(extended, BUSTER_TIER1_BURS_DISP));
    return _mm512_cvtepi64_epi32(bits);
}

// Which of a function's first 64 slots are frame objects (tier1_select_stack_object), a bit each: eight 16-byte records
// a step, their sizes and second qwords split by two permutes. The classifier looks a stack address's bit up in a
// register, where a gather of its record would cost four gathers a block for the one or two lanes most blocks have.
static uint64_t tier1_burs_slot_objects(const IrFunction* function)
{
    const uint32_t count = function->slot_count < 64 ? function->slot_count : 64;
    const __m512i even = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
    const __m512i odd = _mm512_setr_epi64(1, 3, 5, 7, 9, 11, 13, 15);
    uint64_t objects = 0;
    for (uint32_t first = 0; first < count; first += 8)
    {
        const uint32_t remaining = count - first;
        const uint32_t live = remaining >= 8 ? 0xffu : (1u << remaining) - 1;
        // Each record's two qwords: record r's are qwords 2r and 2r + 1 of the step's 128 bytes.
        const uint32_t qwords = _pdep_u32(live, 0x5555u) * 3u;
        const IrSlot* low_records = function->slots + first;
        const IrSlot* high_records = remaining > 4 ? function->slots + first + 4 : low_records;
        const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)qwords, low_records);
        const __m512i high = _mm512_maskz_loadu_epi64((__mmask8)(qwords >> 8), high_records);
        const __m512i size = _mm512_permutex2var_epi64(low, even, high);
        const __m512i second = _mm512_permutex2var_epi64(low, odd, high);
        const __mmask8 sized = _mm512_cmple_epu64_mask(size, _mm512_set1_epi64((long long)BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES));
        const __mmask8 aligned = _mm512_cmple_epu64_mask(_mm512_and_si512(second, _mm512_set1_epi64(0xffffffff)), _mm512_set1_epi64(BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT));
        const __mmask8 ordinary = _mm512_testn_epi64_mask(_mm512_srli_epi64(second, 40), _mm512_set1_epi64(BUSTER_IR_SLOT_OVERFLOW_AREA));
        objects |= (uint64_t)(live & sized & aligned & ordinary) << first;
    }
    return objects;
}

// The nodes' operators and visible children, 16 at a time: what tier1_select_candidates would make of each. Each node's operation,
// type and flags go first into one 32-bit word (`words`, `count` of them), whose gathers then stay in the columns; the
// children's columns are gathered by their indices where those are nodes, a frame object's slot record and an address's
// displacement too.
static void tier1_burs_classify_vector(const Tier1SelectForest* forest, const uint32_t* users, uint32_t* words, uint32_t* classes, uint32_t* kids)
{
    const IrFunction* function = forest->function;
    const uint32_t count = function->instruction_count;
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i op = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->op + first));
        const __m512i type = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->type + first));
        const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(active, function->flags + first));
        const __m512i word = _mm512_or_si512(op, _mm512_or_si512(_mm512_slli_epi32(type, 8), _mm512_slli_epi32(flags, 16)));
        _mm512_mask_storeu_epi32(words + first, active, word);
    }
    const __m512i class_low = _mm512_loadu_si512(tier1_burs_opcode_class);
    const __m512i class_high = _mm512_loadu_si512(tier1_burs_opcode_class + 64);
    const __m512i none = _mm512_set1_epi32((int)BUSTER_IR_NONE);
    const __m512i byte = _mm512_set1_epi32(0xff);
    const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const uint32_t width4 = (1u << IR_I1) | (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32);
    const uint32_t width8 = (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t memory = (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32) | (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t integers = (1u << IR_I32) | (1u << IR_I64);
    const uint32_t narrows = (1u << IR_I1) | (1u << IR_I8) | (1u << IR_I16);
    const uint32_t compared = (1u << IR_I32) | (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t floats = (1u << IR_F32) | (1u << IR_F64);
    // Frame objects among the first 64 slots, as two dwords' bits: a stack address's lane shifts both by its slot, and a
    // shift of 32 or more (the other dword's slots) gives 0.
    const bool few_slots = function->slot_count <= 64;
    const uint64_t slot_objects = tier1_burs_slot_objects(function);
    const __m512i objects_low = _mm512_set1_epi32((int)(uint32_t)slot_objects);
    const __m512i objects_high = _mm512_set1_epi32((int)(uint32_t)(slot_objects >> 32));
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i node = _mm512_add_epi32(_mm512_set1_epi32((int)first), positions);
        const __m512i word = _mm512_maskz_loadu_epi32(active, words + first);
        const __m512i op = _mm512_and_si512(word, byte);
        const __m512i index = _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(op));
        const __m512i initial = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(class_low, index, class_high)));
        const __mmask16 direct = _mm512_mask_cmpeq_epi32_mask(active, initial, _mm512_set1_epi32(BUSTER_TIER1_BURS_DIRECT));
        if (direct == active)
        {
            _mm512_mask_storeu_epi32(classes + first, active, _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
            continue;
        }
        const __m512i type = _mm512_and_si512(_mm512_srli_epi32(word, 8), byte);
        const __m512i flags = _mm512_srli_epi32(word, 16);
        const __m512i a = _mm512_maskz_loadu_epi32(active, function->a + first);
        const __m512i b = _mm512_maskz_loadu_epi32(active, function->b + first);
        const __m512i c = _mm512_maskz_loadu_epi32(active, function->c + first);
        const __m512i uses = _mm512_maskz_loadu_epi32(active, forest->uses + first);
        const __m512i block = _mm512_maskz_loadu_epi32(active, forest->blocks + first);
        const __m512i user = _mm512_maskz_loadu_epi32(active, users + first);
        const __mmask16 a_valid = _mm512_mask_cmplt_epu32_mask(active, a, _mm512_set1_epi32((int)count));
        const __mmask16 b_valid = _mm512_mask_cmplt_epu32_mask(active, b, _mm512_set1_epi32((int)count));
        const __m512i a_word = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), a_valid, a, words, 4);
        const __m512i b_word = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), b_valid, b, words, 4);
        const __m512i a_type = _mm512_and_si512(_mm512_srli_epi32(a_word, 8), byte);
        const __m512i b_op = _mm512_and_si512(b_word, byte);
        const __m512i b_type = _mm512_and_si512(_mm512_srli_epi32(b_word, 8), byte);
        const __mmask16 integer = tier1_burs_types(active, type, integers);
        const __mmask16 is_i64 = _mm512_mask_cmpeq_epi32_mask(active, type, _mm512_set1_epi32(IR_I64));
        const uint32_t simple_types = width4 | width8 | floats;
        const __mmask16 simple = tier1_burs_types(active, type, simple_types);
        const __mmask16 parameters = _mm512_mask_cmpeq_epi32_mask(simple & _mm512_cmplt_epu32_mask(a, _mm512_set1_epi32(BUSTER_TIER1_SELECT_MAX_ARGUMENTS)), op, _mm512_set1_epi32(IR_PARAM));
        const __mmask16 phis = _mm512_mask_cmpeq_epi32_mask(simple, op, _mm512_set1_epi32(IR_PHI));
        const __mmask16 result_types = tier1_burs_types(a_valid, a_type, simple_types | (1u << IR_F80)) | _mm512_cmpeq_epi32_mask(a, none);
        const __mmask16 returns = _mm512_mask_cmpeq_epi32_mask(result_types & active, op, _mm512_set1_epi32(IR_RET));
        const __mmask16 copies = _mm512_mask_cmpeq_epi32_mask(simple, op, _mm512_set1_epi32(IR_COPY));
        const __mmask16 unary = integer & (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_NEG)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_NOT)));
        const __mmask16 truncs = _mm512_mask_cmpeq_epi32_mask(a_valid, op, _mm512_set1_epi32(IR_TRUNC));
        const __mmask16 sexts = _mm512_mask_cmpeq_epi32_mask(a_valid, op, _mm512_set1_epi32(IR_SEXT));
        __mmask16 conversions = 0;
        __mmask16 sign_loads = 0;
        __mmask16 narrow_binary = 0;
        __mmask16 narrow_unary = 0;
        __mmask16 signed_products = 0;
        if ((truncs | sexts) != 0)
        {
            const __m512i a_op = _mm512_and_si512(a_word, byte);
            const uint32_t narrow_sources = (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32);
            const __mmask16 narrow_results = tier1_burs_types(truncs, type, narrow_sources);
            const __mmask16 trunc_inputs = tier1_burs_types(narrow_results, a_type, narrow_sources | (1u << IR_I64));
            // A narrow write can absorb its I32 producer only for these four operations.
            const __mmask16 changes_width = _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I32)) &
                                            _mm512_cmpneq_epi32_mask(type, _mm512_set1_epi32(IR_I32));
            const __mmask16 can_fold = _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_ADD)) | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_SUB)) |
                                       _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_NEG)) | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_NOT));
            conversions = trunc_inputs & ~(changes_width & can_fold);
            const __mmask16 arithmetic = _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_ADD)) | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_SUB));
            narrow_binary = trunc_inputs & changes_width & arithmetic;
            narrow_unary = trunc_inputs & changes_width & can_fold & ~arithmetic;
            const __mmask16 signed_results = tier1_burs_types(sexts, type, integers | (1u << IR_I16));
            const __mmask16 signed_inputs = tier1_burs_types(signed_results, a_type, narrow_sources);
            const __mmask16 from_load = _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_LOAD));
            const __mmask16 wide_result = _mm512_mask_cmpeq_epi32_mask(signed_inputs, type, _mm512_set1_epi32(IR_I64));
            const __mmask16 signed_arithmetic = _mm512_mask_cmpeq_epi32_mask(wide_result, a_type, _mm512_set1_epi32(IR_I32));
            const __mmask16 chained = _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_SEXT)) | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_TRUNC));
            // These I32 producers have additional algebraic patterns when widened to I64.
            const __mmask16 multiply = _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_MUL));
            const __mmask16 algebra = can_fold | multiply | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_AND)) |
                                      _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_OR)) | _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_XOR)) |
                                      _mm512_cmpeq_epi32_mask(a_op, _mm512_set1_epi32(IR_ASHR));
            conversions |= signed_inputs & ~(chained | from_load | (signed_arithmetic & algebra));
            signed_products = signed_arithmetic & multiply;
            sign_loads = signed_inputs & from_load & integer;
            conversions |= signed_inputs & from_load & ~integer;
        }
        __m512i result = _mm512_mask_mov_epi32(_mm512_set1_epi32(TIER1_BURS_OP_REG), direct | parameters | phis | returns | copies | unary | conversions, _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
        result = _mm512_mask_mov_epi32(result, sign_loads, _mm512_set1_epi32(TIER1_BURS_OP_SIGN_LOAD));
        result = _mm512_mask_mov_epi32(result, signed_products, _mm512_set1_epi32(TIER1_BURS_OP_SIGNED_PRODUCT));
        result = _mm512_mask_mov_epi32(result, narrow_binary, _mm512_set1_epi32(TIER1_BURS_OP_NARROW_BINARY));
        result = _mm512_mask_mov_epi32(result, narrow_unary, _mm512_set1_epi32(TIER1_BURS_OP_NARROW_UNARY));
        const __mmask16 divides = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SDIV)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_UDIV)) |
                                  _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SREM)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_UREM));
        result = _mm512_mask_mov_epi32(result, integer & b_valid & divides & _mm512_cmpneq_epi32_mask(b_op, _mm512_set1_epi32(IR_CONST)), _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
        // Constants: their class bits, 8 values at a time.
        const __mmask16 constant = _mm512_mask_cmpeq_epi32_mask(active & tier1_burs_types(active, type, width4 | width8), op, _mm512_set1_epi32(IR_CONST));
        if (constant != 0)
        {
            const __m512i width = _mm512_mask_blend_epi32(tier1_burs_types(active, type, width8), _mm512_set1_epi32(4), _mm512_set1_epi32(8));
            const __m512i index = _mm512_slli_epi32(a, 1);
            const __m512i low = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), (__mmask8)constant, _mm512_castsi512_si256(index), function->constants, 8);
            const __m512i high = _mm512_mask_i32gather_epi64(_mm512_setzero_si512(), (__mmask8)(constant >> 8), _mm512_extracti64x4_epi64(index, 1), function->constants, 8);
            const __m256i bits_low = tier1_burs_constant_bits_vector(low, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(width)));
            const __m256i bits_high = tier1_burs_constant_bits_vector(high, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(width, 1)));
            const __m512i bits = _mm512_inserti64x4(_mm512_castsi256_si512(bits_low), bits_high, 1);
            result = _mm512_mask_add_epi32(result, constant, bits, _mm512_set1_epi32(TIER1_BURS_OP_CONST));
        }
        // ALU operations: the operation (a 64-bit addition or subtraction apart, and a narrow and, or or xor one of its
        // own), and the kind of the right operand.
        const __m512i alu = _mm512_sub_epi32(initial, _mm512_set1_epi32(1));
        const __mmask16 bitwise = _mm512_cmpeq_epi32_mask(alu, _mm512_set1_epi32(BUSTER_TIER1_BURS_AND)) | _mm512_cmpeq_epi32_mask(alu, _mm512_set1_epi32(BUSTER_TIER1_BURS_OR)) |
                                  _mm512_cmpeq_epi32_mask(alu, _mm512_set1_epi32(BUSTER_TIER1_BURS_XOR));
        const __mmask16 narrow = tier1_burs_types(active, type, narrows) & bitwise;
        const __mmask16 is_alu = _mm512_mask_cmple_epu32_mask((integer | narrow) & b_valid, alu, _mm512_set1_epi32(BUSTER_TIER1_BURS_XOR));
        if (is_alu != 0)
        {
            const __mmask16 wide = is_i64 & (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SUB)));
            __m512i operation = _mm512_mask_add_epi32(alu, wide, alu, _mm512_set1_epi32(1));
            operation = _mm512_mask_mov_epi32(operation, narrow, _mm512_set1_epi32(BUSTER_TIER1_BURS_NARROW));
            const __mmask16 right_constant = _mm512_cmpeq_epi32_mask(b_op, _mm512_set1_epi32(IR_CONST));
            const __mmask16 right_load = _mm512_cmpeq_epi32_mask(b_op, _mm512_set1_epi32(IR_LOAD));
            const __mmask16 right_shift = _mm512_cmpeq_epi32_mask(b_op, _mm512_set1_epi32(IR_SHL)) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADD));
            __m512i kind = _mm512_set1_epi32(BUSTER_TIER1_BURS_KIND_O);
            kind = _mm512_mask_mov_epi32(kind, right_shift, _mm512_set1_epi32(BUSTER_TIER1_BURS_KIND_S));
            kind = _mm512_mask_mov_epi32(kind, right_load, _mm512_set1_epi32(BUSTER_TIER1_BURS_KIND_L));
            kind = _mm512_mask_mov_epi32(kind, right_constant, _mm512_set1_epi32(BUSTER_TIER1_BURS_KIND_C));
            const __m512i alu_op = _mm512_add_epi32(_mm512_set1_epi32(TIER1_BURS_OP_ALU), _mm512_add_epi32(_mm512_slli_epi32(operation, 2), kind));
            result = _mm512_mask_mov_epi32(result, is_alu, alu_op);
        }
        const __mmask16 shl = integer & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SHL));
        const __mmask16 lshr = integer & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LSHR));
        const __mmask16 ashr = integer & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ASHR));
        result = _mm512_mask_mov_epi32(result, shl, _mm512_set1_epi32(TIER1_BURS_OP_SHL));
        result = _mm512_mask_mov_epi32(result, lshr, _mm512_set1_epi32(TIER1_BURS_OP_LSHR));
        result = _mm512_mask_mov_epi32(result, ashr, _mm512_set1_epi32(TIER1_BURS_OP_ASHR));
        // Loads, foldable into their single user past no barrier but themselves (every load one: tier1_select_barrier);
        // stores.
        const __mmask16 floating = tier1_burs_types(active, type, floats);
        const __mmask16 float_leaf = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CONST)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_UNDEF)) |
                                     _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FNEG));
        result = _mm512_mask_mov_epi32(result, floating & float_leaf, _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
        const __mmask16 binary = floating & _mm512_cmpge_epu32_mask(op, _mm512_set1_epi32(IR_FADD)) & _mm512_cmple_epu32_mask(op, _mm512_set1_epi32(IR_FDIV));
        if (binary != 0)
        {
            const __mmask16 commute = _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FADD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FMUL));
            const __mmask16 left = commute & _mm512_cmpneq_epi32_mask(b_op, _mm512_set1_epi32(IR_LOAD));
            result = _mm512_mask_mov_epi32(result, binary, _mm512_mask_blend_epi32(left, _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_BINARY_RIGHT), _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_BINARY_LEFT)));
        }
        const __mmask16 load = tier1_burs_types(active, type, memory | floats) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD));
        if (load != 0)
        {
            const __mmask16 placed = _mm512_mask_cmplt_epu32_mask(load, user, _mm512_set1_epi32((int)count)) & _mm512_cmplt_epu32_mask(node, user);
            const __m512i user_block = _mm512_mask_i32gather_epi32(none, placed, user, forest->blocks, 4);
            const __m512i barrier = _mm512_mask_i32gather_epi32(none, placed, user, forest->barriers, 4);
            const __mmask16 foldable = placed & _mm512_testn_epi32_mask(flags, _mm512_set1_epi32(0xffu & ~BUSTER_IR_COPY_PIECE)) & _mm512_cmpeq_epi32_mask(uses, _mm512_set1_epi32(1)) &
                                       _mm512_cmpeq_epi32_mask(user_block, block) &
                                       _mm512_cmple_epu32_mask(barrier, _mm512_add_epi32(node, _mm512_set1_epi32(1)));
            result = _mm512_mask_mov_epi32(result, load, _mm512_mask_blend_epi32(floating, _mm512_set1_epi32(TIER1_BURS_OP_LOAD_N), _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_LOAD_N)));
            result = _mm512_mask_mov_epi32(result, foldable, _mm512_mask_blend_epi32(floating, _mm512_set1_epi32(TIER1_BURS_OP_LOAD_F), _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_LOAD_F)));
        }
        const __mmask16 store = b_valid & tier1_burs_types(active, b_type, memory) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE));
        result = _mm512_mask_mov_epi32(result, store, _mm512_set1_epi32(TIER1_BURS_OP_STORE));
        const __mmask16 float_store = tier1_burs_types(b_valid, b_type, floats) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE));
        result = _mm512_mask_mov_epi32(result, float_store, _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_STORE));
        // Addresses by their index, where their displacement fits (tier1_burs_displacement): the offset, which an index
        // that isn't a constant leaves alone, and a constant index's scaled value added. Where the constant or the offset
        // doesn't fit 32 bits, which may still sum to a displacement that does, the exact 64-bit arithmetic below decides.
        const __mmask16 address = active & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADDRESS));
        if (address != 0)
        {
            const __m512i zero = _mm512_setzero_si512();
            const __m512i record = _mm512_slli_epi32(c, 1);
            const __m512i offset_low = _mm512_mask_i32gather_epi64(zero, (__mmask8)address, _mm512_castsi512_si256(record), function->addresses, 8);
            const __m512i offset_high = _mm512_mask_i32gather_epi64(zero, (__mmask8)(address >> 8), _mm512_extracti64x4_epi64(record, 1), function->addresses, 8);
            const __mmask16 offset_fits = (__mmask16)(_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(offset_low, 32), 32), offset_low) |
                                                      ((uint32_t)_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(offset_high, 32), 32), offset_high) << 8));
            const __mmask16 constant_index = address & b_valid & _mm512_cmpeq_epi32_mask(b_op, _mm512_set1_epi32(IR_CONST));
            __mmask16 covered = address & ~constant_index & offset_fits;
            if (constant_index != 0)
            {
                const __m512i value = _mm512_slli_epi32(_mm512_mask_i32gather_epi32(zero, constant_index, b, function->a, 4), 1);
                const __m512i value_low = _mm512_mask_i32gather_epi64(zero, (__mmask8)constant_index, _mm512_castsi512_si256(value), function->constants, 8);
                const __m512i value_high = _mm512_mask_i32gather_epi64(zero, (__mmask8)(constant_index >> 8), _mm512_extracti64x4_epi64(value, 1), function->constants, 8);
                const __mmask16 value_fits = (__mmask16)(_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(value_low, 32), 32), value_low) |
                                                         ((uint32_t)_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(value_high, 32), 32), value_high) << 8));
                // The scale, the record's ninth byte: a 32-bit gather of its bytes 8 to 11, inside the 16-byte record.
                const __m512i scale = _mm512_and_si512(_mm512_mask_i32gather_epi32(zero, constant_index, _mm512_slli_epi32(c, 4), (const uint8_t*)function->addresses + 8, 1), byte);
                // Both factors and the offset within 32 bits: the sum is exact in 64, and fits a disp32 or doesn't.
                const __mmask16 small = constant_index & value_fits & offset_fits;
                const __m512i sum_low = _mm512_add_epi64(_mm512_mullo_epi64(value_low, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(scale))), offset_low);
                const __m512i sum_high = _mm512_add_epi64(_mm512_mullo_epi64(value_high, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(scale, 1))), offset_high);
                const __mmask16 sum_fits = (__mmask16)(_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(sum_low, 32), 32), sum_low) |
                                                       ((uint32_t)_mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(sum_high, 32), 32), sum_high) << 8));
                covered |= small & sum_fits;
                // A constant or an offset past 32 bits, which may still sum to a disp32 (tier1_burs_displacement):
                // the product of the constant and the scale (at most 255) exactly, by 32-bit limbs of the constant's
                // magnitude, refused where it overflows 64 bits; then the sum, refused where it overflows; then the
                // disp32's range.
                const __mmask16 large = constant_index & ~small;
                if (large != 0)
                {
                    const __m512i scales[2] = {
                        _mm512_cvtepu32_epi64(_mm512_castsi512_si256(scale)),
                        _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(scale, 1)),
                    };
                    const __m512i values[2] = { value_low, value_high };
                    const __m512i offsets[2] = { offset_low, offset_high };
                    uint32_t fits = 0;
                    for (uint32_t half = 0; half < 2; half += 1)
                    {
                        const __mmask8 negative = _mm512_movepi64_mask(values[half]);
                        const __m512i magnitude = _mm512_mask_sub_epi64(values[half], negative, _mm512_setzero_si512(), values[half]);
                        const __m512i low = _mm512_mul_epu32(magnitude, scales[half]);
                        const __m512i high = _mm512_mul_epu32(_mm512_srli_epi64(magnitude, 32), scales[half]);
                        // The magnitude's bits from 32 up, and those below: it fits an int64 under 2^63, or at exactly
                        // 2^63 when negative.
                        const __m512i top = _mm512_add_epi64(high, _mm512_srli_epi64(low, 32));
                        const __m512i limit = _mm512_set1_epi64(INT64_C(1) << 31);
                        const __mmask8 product_fits = _mm512_cmplt_epu64_mask(top, limit) |
                                                      (negative & _mm512_cmpeq_epi64_mask(top, limit) & _mm512_testn_epi64_mask(low, _mm512_set1_epi64(0xffffffff)));
                        const __m512i product = _mm512_mullo_epi64(values[half], scales[half]);
                        const __m512i sum = _mm512_add_epi64(product, offsets[half]);
                        // A signed addition overflows where both terms have one sign and the sum the other.
                        const __mmask8 sum_overflow = _mm512_movepi64_mask(_mm512_andnot_si512(_mm512_xor_si512(product, offsets[half]), _mm512_xor_si512(sum, product)));
                        const __mmask8 disp32 = _mm512_cmpeq_epi64_mask(_mm512_srai_epi64(_mm512_slli_epi64(sum, 32), 32), sum);
                        fits |= (uint32_t)(product_fits & ~sum_overflow & disp32) << (half * 8);
                    }
                    covered |= large & (__mmask16)fits;
                }
            }
            const __mmask16 no_index = covered & _mm512_cmpeq_epi32_mask(b, none);
            result = _mm512_mask_mov_epi32(result, covered, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_IDX));
            result = _mm512_mask_mov_epi32(result, no_index, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_NOIDX));
            result = _mm512_mask_mov_epi32(result, covered & constant_index, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_CIDX));
        }
        const __mmask16 conversion = (a_valid & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_INTTOPTR)) & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I64))) |
                                     (is_i64 & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_PTRTOINT)));
        result = _mm512_mask_mov_epi32(result, conversion, _mm512_set1_epi32(TIER1_BURS_OP_CONV_PTR));
        // Integer comparisons, and among them a truth value's links (tier1_select_truth's chains): ne or eq, of the zero
        // extension right before the zero right before it, of the I1 right before the extension, the zero only the
        // comparison's, in its block.
        const __mmask16 compare = b_valid & tier1_burs_types(a_valid, a_type, compared) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ICMP));
        if (compare != 0)
        {
            const __mmask16 shaped = compare & _mm512_cmple_epu32_mask(c, _mm512_set1_epi32(IR_NE)) & _mm512_cmpeq_epi32_mask(_mm512_add_epi32(a, _mm512_set1_epi32(2)), node) &
                                     _mm512_cmpeq_epi32_mask(_mm512_add_epi32(b, _mm512_set1_epi32(1)), node) & _mm512_cmpeq_epi32_mask(b_op, _mm512_set1_epi32(IR_CONST)) &
                                     _mm512_cmpeq_epi32_mask(_mm512_and_si512(a_word, byte), _mm512_set1_epi32(IR_ZEXT));
            const __m512i b_constant = _mm512_mask_i32gather_epi32(none, shaped, b, function->a, 4);
            const __m512i inner = _mm512_mask_i32gather_epi32(none, shaped, a, function->a, 4);
            const __mmask16 placed = shaped & _mm512_cmpeq_epi32_mask(_mm512_add_epi32(inner, _mm512_set1_epi32(1)), a) & _mm512_cmplt_epu32_mask(inner, _mm512_set1_epi32((int)count));
            const __m512i inner_word = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), placed, inner, words, 4);
            const __m512i b_uses = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), placed, b, forest->uses, 4);
            const __m512i b_block = _mm512_mask_i32gather_epi32(none, placed, b, forest->blocks, 4);
            const __m512i index = _mm512_slli_epi32(b_constant, 1);
            const __m512i value_low = _mm512_mask_i32gather_epi64(_mm512_set1_epi64(1), (__mmask8)placed, _mm512_castsi512_si256(index), function->constants, 8);
            const __m512i value_high = _mm512_mask_i32gather_epi64(_mm512_set1_epi64(1), (__mmask8)(placed >> 8), _mm512_extracti64x4_epi64(index, 1), function->constants, 8);
            const __mmask16 zero = (__mmask16)(_mm512_cmpeq_epi64_mask(value_low, _mm512_setzero_si512()) | ((uint32_t)_mm512_cmpeq_epi64_mask(value_high, _mm512_setzero_si512()) << 8));
            const __mmask16 link = placed & zero & _mm512_cmpeq_epi32_mask(_mm512_and_si512(_mm512_srli_epi32(inner_word, 8), byte), _mm512_set1_epi32(IR_I1)) &
                                   _mm512_cmpeq_epi32_mask(b_uses, _mm512_set1_epi32(1)) & _mm512_cmpeq_epi32_mask(b_block, block);
            result = _mm512_mask_mov_epi32(result, compare, _mm512_set1_epi32(TIER1_BURS_OP_ICMP));
            result = _mm512_mask_mov_epi32(result, link, _mm512_set1_epi32(TIER1_BURS_OP_ICMP_LINK));
        }
        // Zero extensions, a truth value's right after its I1; floating comparisons by <, <=, > and >=; branches right
        // after their conditions.
        const __mmask16 extension = tier1_burs_types(active, type, (1u << IR_I8) | (1u << IR_I16) | integers) & tier1_burs_types(a_valid, a_type, width4) &
                                    _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ZEXT));
        const __mmask16 adjacent = _mm512_cmpeq_epi32_mask(_mm512_add_epi32(a, _mm512_set1_epi32(1)), node);
        const __mmask16 truth = extension & adjacent & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I1));
        result = _mm512_mask_mov_epi32(result, extension, _mm512_set1_epi32(TIER1_BURS_OP_ZEXT_PLAIN));
        result = _mm512_mask_mov_epi32(result, truth, _mm512_set1_epi32(TIER1_BURS_OP_ZEXT_TRUTH));
        const __mmask16 ordered = b_valid & tier1_burs_types(a_valid, a_type, floats) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FCMP)) &
                                  _mm512_cmpge_epu32_mask(c, _mm512_set1_epi32(IR_LT)) & _mm512_cmple_epu32_mask(c, _mm512_set1_epi32(IR_GE));
        result = _mm512_mask_mov_epi32(result, ordered, _mm512_set1_epi32(TIER1_BURS_OP_FCMP_ORD));
        // Floating == and != (#2922): the scalar classifier's, which looks for the select that reads one.
        const __mmask16 equal = b_valid & tier1_burs_types(a_valid, a_type, floats) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FCMP)) &
                                _mm512_cmple_epu32_mask(c, _mm512_set1_epi32(IR_NE));
        const __mmask16 conditional_branches = _mm512_mask_cmpeq_epi32_mask(active, op, _mm512_set1_epi32(IR_CBR));
        const __mmask16 branch = conditional_branches & adjacent;
        result = _mm512_mask_mov_epi32(result, conditional_branches & ~adjacent, _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
        result = _mm512_mask_mov_epi32(result, branch, _mm512_set1_epi32(TIER1_BURS_OP_CBR_ADJ));
        // Integer selects (#2676), of any condition: the kids' pass decides whether it is visible.
        const __mmask16 select = tier1_burs_types(active, type, width4 | width8) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SELECT));
        result = _mm512_mask_mov_epi32(result, select, _mm512_set1_epi32(TIER1_BURS_OP_SELECT));
        // A truth value's I1 copy of the floating comparison right before it (#2791).
        const __mmask16 copy = adjacent & _mm512_mask_cmpeq_epi32_mask(active, type, _mm512_set1_epi32(IR_I1)) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_COPY)) &
                               _mm512_cmpeq_epi32_mask(_mm512_and_si512(a_word, byte), _mm512_set1_epi32(IR_FCMP));
        result = _mm512_mask_mov_epi32(result, copy, _mm512_set1_epi32(TIER1_BURS_OP_COPY_TRUTH));
        // A frame object's address (tier1_select_stack_object): a slot of the function's, not the variadic overflow
        // area, within the planner's object bounds; SLOT_X in the entry block. With up to 64 slots its bit is
        // tier1_burs_slot_objects'; past them its record's size, alignment and flags are gathered from the slot's 16 bytes.
        const __mmask16 slot_address = _mm512_mask_cmplt_epu32_mask(active & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STACK_ADDR)), a,
            _mm512_set1_epi32((int)function->slot_count));
        if (few_slots)
        {
            const __m512i bits = _mm512_or_si512(_mm512_srlv_epi32(objects_low, a), _mm512_srlv_epi32(objects_high, _mm512_sub_epi32(a, _mm512_set1_epi32(32))));
            const __mmask16 object = _mm512_mask_test_epi32_mask(slot_address, bits, _mm512_set1_epi32(1));
            const __mmask16 entry = object & _mm512_cmpeq_epi32_mask(block, _mm512_setzero_si512());
            result = _mm512_mask_mov_epi32(result, object, _mm512_set1_epi32(TIER1_BURS_OP_SLOT));
            result = _mm512_mask_mov_epi32(result, entry, _mm512_set1_epi32(TIER1_BURS_OP_SLOT_X));
        }
        else if (slot_address != 0)
        {
            const __m512i zero = _mm512_setzero_si512();
            const __m512i record = _mm512_slli_epi32(a, 1);
            const __m512i size_low = _mm512_mask_i32gather_epi64(zero, (__mmask8)slot_address, _mm512_castsi512_si256(record), function->slots, 8);
            const __m512i size_high = _mm512_mask_i32gather_epi64(zero, (__mmask8)(slot_address >> 8), _mm512_extracti64x4_epi64(record, 1), function->slots, 8);
            // The record's second qword: the alignment, then the type and flags bytes and the padding. Indices of qwords
            // (a slot's 2a and 2a + 1, scale 8) stay in a dword for every slot below 2^30.
            const __m512i second_low = _mm512_mask_i32gather_epi64(zero, (__mmask8)slot_address, _mm512_castsi512_si256(_mm512_add_epi32(record, _mm512_set1_epi32(1))), function->slots, 8);
            const __m512i second_high = _mm512_mask_i32gather_epi64(zero, (__mmask8)(slot_address >> 8), _mm512_extracti64x4_epi64(_mm512_add_epi32(record, _mm512_set1_epi32(1)), 1), function->slots, 8);
            const __m512i tail = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(second_low)), _mm512_cvtepi64_epi32(second_high), 1);
            const __m512i flags = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi64_epi32(_mm512_srli_epi64(second_low, 40))),
                                                     _mm512_cvtepi64_epi32(_mm512_srli_epi64(second_high, 40)), 1);
            const __m512i bytes = _mm512_set1_epi64((long long)BUSTER_TIER1_SELECT_MAX_OBJECT_BYTES);
            const __mmask16 sized = (__mmask16)(_mm512_cmple_epu64_mask(size_low, bytes) | ((uint32_t)_mm512_cmple_epu64_mask(size_high, bytes) << 8));
            const __mmask16 object = slot_address & sized & _mm512_cmple_epu32_mask(tail, _mm512_set1_epi32((int)BUSTER_TIER1_SELECT_MAX_OBJECT_ALIGNMENT)) &
                                     _mm512_testn_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_SLOT_OVERFLOW_AREA));
            const __mmask16 entry = object & _mm512_cmpeq_epi32_mask(block, zero);
            result = _mm512_mask_mov_epi32(result, object, _mm512_set1_epi32(TIER1_BURS_OP_SLOT));
            result = _mm512_mask_mov_epi32(result, entry, _mm512_set1_epi32(TIER1_BURS_OP_SLOT_X));
        }
        const __mmask16 float_conversion = _mm512_mask_cmpeq_epi32_mask(a_valid, initial, _mm512_set1_epi32(BUSTER_TIER1_BURS_FLOAT_CONVERSION));
        if (float_conversion != 0)
        {
            const __mmask16 float_input = tier1_burs_types(float_conversion, a_type, floats);
            const __mmask16 signed_to_float = _mm512_mask_cmpeq_epi32_mask(floating, op, _mm512_set1_epi32(IR_SITOFP));
            const __mmask16 unsigned_to_float = _mm512_mask_cmpeq_epi32_mask(floating, op, _mm512_set1_epi32(IR_UITOFP));
            const __mmask16 float_to_signed = _mm512_mask_cmpeq_epi32_mask(float_input, op, _mm512_set1_epi32(IR_FPTOSI));
            const __mmask16 float_to_unsigned = _mm512_mask_cmpeq_epi32_mask(float_input, op, _mm512_set1_epi32(IR_FPTOUI));
            const uint32_t signed_types = memory & ~(1u << IR_PTR);
            const __mmask16 from_int = tier1_burs_types(a_valid & signed_to_float, a_type, signed_types) |
                                       tier1_burs_types(a_valid & unsigned_to_float, a_type, signed_types | (1u << IR_I1));
            const __mmask16 to_int = tier1_burs_types(float_to_signed | float_to_unsigned, type, signed_types);
            const __mmask16 resize = floating & float_input &
                                     (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FEXT)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FTRUNC)));
            const __mmask16 bits = _mm512_mask_cmpeq_epi32_mask(a_valid, op, _mm512_set1_epi32(IR_BITCAST));
            const __mmask16 same32 = (_mm512_cmpeq_epi32_mask(type, _mm512_set1_epi32(IR_F32)) & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I32))) |
                                     (_mm512_cmpeq_epi32_mask(type, _mm512_set1_epi32(IR_I32)) & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_F32)));
            const __mmask16 same64 = (_mm512_cmpeq_epi32_mask(type, _mm512_set1_epi32(IR_F64)) & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I64))) |
                                     (is_i64 & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_F64)));
            result = _mm512_mask_mov_epi32(result, from_int | to_int | resize | (bits & (same32 | same64)), _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_CONVERT));

            const __mmask16 memory_form = tier1_burs_types(signed_to_float, a_type, integers) | tier1_burs_types(float_to_signed, type, integers) |
                                          _mm512_mask_cmpeq_epi32_mask(unsigned_to_float, a_type, _mm512_set1_epi32(IR_I32)) |
                                          _mm512_mask_cmpeq_epi32_mask(float_to_unsigned, type, _mm512_set1_epi32(IR_I32));
            const __m512i source_op = _mm512_and_si512(a_word, byte);
            const __mmask16 memory_input = _mm512_mask_cmpeq_epi32_mask(a_valid & memory_form, source_op, _mm512_set1_epi32(IR_LOAD));
            result = _mm512_mask_mov_epi32(result, memory_input, _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_CONVERT_LOAD));
            const __mmask16 literals = _mm512_mask_cmpeq_epi32_mask(from_int & _mm512_cmplt_epu32_mask(a, node), source_op, _mm512_set1_epi32(IR_CONST));
            if (literals != 0)
            {
                const __m512i source_uses = _mm512_mask_i32gather_epi32(none, literals, a, forest->uses, 4);
                const __m512i source_block = _mm512_mask_i32gather_epi32(none, literals, a, forest->blocks, 4);
                const __mmask16 owned = literals & _mm512_cmpeq_epi32_mask(source_uses, _mm512_set1_epi32(1)) & _mm512_cmpeq_epi32_mask(source_block, block);
                result = _mm512_mask_mov_epi32(result, owned, _mm512_set1_epi32(TIER1_BURS_OP_FLOAT_CONVERT_CONSTANT));
            }
        }
        const __mmask16 f80 = _mm512_mask_cmpeq_epi32_mask(active, type, _mm512_set1_epi32(IR_F80));
        const __mmask16 a80 = _mm512_mask_cmpeq_epi32_mask(a_valid, a_type, _mm512_set1_epi32(IR_F80));
        const __mmask16 b80 = _mm512_mask_cmpeq_epi32_mask(b_valid, b_type, _mm512_set1_epi32(IR_F80));
        if ((f80 | a80 | b80) != 0)
        {
            const uint32_t values = (1u << IR_CONST) | (1u << IR_UNDEF) | (1u << IR_COPY) | (1u << IR_FNEG) |
                                    (1u << IR_FADD) | (1u << IR_FSUB) | (1u << IR_FMUL) | (1u << IR_FDIV);
            __mmask16 x87 = tier1_burs_types(f80, op, values) | _mm512_mask_cmpeq_epi32_mask(f80, op, _mm512_set1_epi32(IR_LOAD)) |
                            _mm512_mask_cmpeq_epi32_mask(b80, op, _mm512_set1_epi32(IR_STORE)) | _mm512_mask_cmpeq_epi32_mask(a80, op, _mm512_set1_epi32(IR_FCMP));
            x87 |= _mm512_mask_cmpeq_epi32_mask(f80 & tier1_burs_types(a_valid, a_type, floats), op, _mm512_set1_epi32(IR_FEXT));
            x87 |= _mm512_mask_cmpeq_epi32_mask(a80 & floating, op, _mm512_set1_epi32(IR_FTRUNC));
            x87 |= f80 & tier1_burs_types(a_valid, a_type, memory) &
                   (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SITOFP)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_UITOFP)));
            x87 |= a80 & tier1_burs_types(active, type, memory) &
                   (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FPTOSI)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_FPTOUI)));
            result = _mm512_mask_mov_epi32(result, x87, _mm512_set1_epi32(TIER1_BURS_OP_X87));
            result = _mm512_mask_mov_epi32(result, _mm512_mask_cmpeq_epi32_mask(f80, op, _mm512_set1_epi32(IR_PHI)), _mm512_set1_epi32(TIER1_BURS_OP_DIRECT));
        }
        _mm512_mask_storeu_epi32(classes + first, active, result);
        // A floating == or != (#2922): tier1_burs_equality's, whose select's recurrence is tier1_select_recurrent's
        // bounded walk, which candidate generation shares (#3450's record has its census).
        uint32_t equalities = equal;
        uint32_t calls = _mm512_mask_cmpeq_epi32_mask(active, op, _mm512_set1_epi32(IR_CALL)) |
                         _mm512_mask_cmpeq_epi32_mask(active, op, _mm512_set1_epi32(IR_CALL_INDIRECT));
        while (calls != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(calls);
            const Tier1SelectRule kind = tier1_select_call_rule(function, first + lane);
            if (kind != TIER1_RULE_NONE)
            {
                classes[first + lane] = kind == TIER1_RULE_ASM ? TIER1_BURS_OP_CALL_ASM : kind == TIER1_RULE_ROW ? TIER1_BURS_OP_CALL_ROW : TIER1_BURS_OP_CALL;
            }
            calls &= calls - 1;
        }
        while (equalities != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(equalities);
            classes[first + lane] = tier1_burs_equality(forest, users, first + lane);
            equalities &= equalities - 1;
        }
    }
    // The visible children, from the classes: a tree child, a constant or a frame object's address in this block or the
    // entry, before the node, at a position its operator reads. A shared frame leaf is visible to memory operands too.
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i node = _mm512_add_epi32(_mm512_set1_epi32((int)first), positions);
        const __m512i class = _mm512_maskz_loadu_epi32(active, classes + first);
        const __m512i block = _mm512_maskz_loadu_epi32(active, forest->blocks + first);
        const __m512i arity = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, class, tier1_burs_arity, 1), byte);
        for (uint32_t position = 0; position < 2; position += 1)
        {
            const __m512i child = _mm512_maskz_loadu_epi32(active, (position == 0 ? function->a : function->b) + first);
            const __mmask16 read = _mm512_mask_cmpgt_epu32_mask(active, arity, _mm512_set1_epi32((int)position)) & _mm512_cmplt_epu32_mask(child, node);
            const __m512i child_class = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), read, child, classes, 4);
            const __m512i child_uses = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), read, child, forest->uses, 4);
            const __m512i child_block = _mm512_mask_i32gather_epi32(none, read, child, forest->blocks, 4);
            const __mmask16 tree = _mm512_cmpeq_epi32_mask(child_uses, _mm512_set1_epi32(1)) & _mm512_cmpeq_epi32_mask(child_block, block);
            const __mmask16 constant = _mm512_cmpge_epu32_mask(child_class, _mm512_set1_epi32(TIER1_BURS_OP_CONST)) & _mm512_cmplt_epu32_mask(child_class, _mm512_set1_epi32(TIER1_BURS_OP_ALU));
            const __mmask16 slot = _mm512_cmpeq_epi32_mask(child_class, _mm512_set1_epi32(TIER1_BURS_OP_SLOT_X)) |
                                   (_mm512_cmpeq_epi32_mask(child_class, _mm512_set1_epi32(TIER1_BURS_OP_SLOT)) & _mm512_cmpeq_epi32_mask(child_block, block));
            const __mmask16 visible = read & (tree | constant | slot);
            _mm512_mask_storeu_epi32(kids + (uint64_t)position * count + first, active, _mm512_mask_blend_epi32(visible, none, child));
        }
    }
}

// A 16-bit entry of the pool at each lane's index, by 32-bit gathers (the pool's pad covers the last one's high half).
static __m512i tier1_burs_gather_pool(__mmask16 active, __m512i index)
{
    return _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, index, tier1_burs_pool, 2), _mm512_set1_epi32(0xffff));
}

// The labeling, 16 nodes of a depth level at a time: each one's children's states (the REG leaf's where none is
// visible), their representers by its operator's maps, and its table's entry, gathered; nodes of one level read only
// lower levels' states.
static void tier1_burs_label_vector(uint32_t maximum_depth, const uint32_t* level_offsets, const uint32_t* level_nodes, uint32_t count, const uint32_t* classes,
                                    const uint32_t* kids, uint32_t* states)
{
    const __m512i none = _mm512_set1_epi32((int)BUSTER_IR_NONE);
    const __m512i reg = _mm512_set1_epi32((int)BUSTER_TIER1_BURS_REG_STATE);
    for (uint32_t level = 0; level <= maximum_depth; level += 1)
    {
        for (uint32_t first = level_offsets[level]; first < level_offsets[level + 1]; first += 16)
        {
            const uint32_t remaining = level_offsets[level + 1] - first;
            const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
            const __m512i nodes = _mm512_maskz_loadu_epi32(active, level_nodes + first);
            const __m512i op = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, nodes, classes, 4);
            const __m512i left_kid = _mm512_mask_i32gather_epi32(none, active, nodes, kids, 4);
            const __m512i right_kid = _mm512_mask_i32gather_epi32(none, active, nodes, kids + count, 4);
            const __mmask16 left_visible = _mm512_mask_cmpneq_epi32_mask(active, left_kid, none);
            const __mmask16 right_visible = _mm512_mask_cmpneq_epi32_mask(active, right_kid, none);
            __m512i state;
            if ((left_visible | right_visible) == 0)
            {
                state = _mm512_mask_i32gather_epi32(reg, active, op, tier1_burs_register_state, 4);
            }
            else
            {
                const __m512i left = _mm512_mask_i32gather_epi32(reg, left_visible, left_kid, states, 4);
                const __m512i right = _mm512_mask_i32gather_epi32(reg, right_visible, right_kid, states, 4);
                const __m512i left_map = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_map_base[0], 4);
                const __m512i right_map = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_map_base[1], 4);
                const __m512i base = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_table_base, 4);
                const __m512i width = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_table_width, 4);
                const __m512i row = tier1_burs_gather_pool(active, _mm512_add_epi32(left_map, left));
                const __m512i column = tier1_burs_gather_pool(active, _mm512_add_epi32(right_map, right));
                const __m512i entry = _mm512_add_epi32(_mm512_add_epi32(base, _mm512_mullo_epi32(row, width)), column);
                state = tier1_burs_gather_pool(active, entry);
            }
#if __has_feature(memory_sanitizer)
            // MSan checks a masked scatter's operands' shadow but writes none at its destinations: the elements it
            // writes keep their old poison, and a later read reports them uninitialized. Store the lanes scalar so
            // each written state's shadow lands with it.
            uint32_t scattered_nodes[16];
            uint32_t scattered_states[16];
            _mm512_storeu_si512(scattered_nodes, nodes);
            _mm512_storeu_si512(scattered_states, state);
            for (uint32_t lane = 0; lane < 16; lane += 1)
            {
                if ((uint32_t)active >> lane & 1)
                {
                    states[scattered_nodes[lane]] = scattered_states[lane];
                }
            }
#else
            _mm512_mask_i32scatter_epi32(states, active, nodes, state, 4);
#endif
        }
    }
}

// The rules, 16 nodes at a time: each one's goal by its operator, and its state's rule of the goal, gathered (the tables'
// pads cover a 32-bit gather of their last byte).
static void tier1_burs_rules_vector(uint32_t count, const uint32_t* classes, const uint32_t* states, uint32_t* rules)
{
    const __m512i low = _mm512_set1_epi32(0xff);
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i op = _mm512_maskz_loadu_epi32(active, classes + first);
        const __m512i state = _mm512_maskz_loadu_epi32(active, states + first);
        const __m512i goal = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_goal, 1), low);
        const __m512i index = _mm512_add_epi32(_mm512_mullo_epi32(state, _mm512_set1_epi32(TIER1_BURS_NONTERMINALS)), goal);
        const __m512i rule = _mm512_and_si512(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, index, tier1_burs_state_rules, 1), low);
        _mm512_mask_storeu_epi32(rules + first, active, rule);
    }
}

// The candidate of `node` that its rule names (Tier1BursAction): its index in `candidates`, or munch's choice where the
// rule names none or the walks didn't make the one it names, which *munched says.
static uint32_t tier1_burs_pick(const Tier1SelectCandidates* candidates, uint32_t rule_index, bool* munched)
{
    const Tier1BursRule* rule = &tier1_burs_rules[rule_index < tier1_burs_rule_count ? rule_index : 0];
    if (rule->action == TIER1_BURS_ACTION_PLAIN)
    {
        *munched = candidates->count == 0;
        return candidates->count != 0 ? 0 : UINT32_MAX;
    }
    uint32_t chosen = UINT32_MAX;
    for (uint32_t p = 0; p < candidates->count; p += 1)
    {
        const Tier1SelectPattern* pattern = &candidates->patterns[p];
        bool match = false;
        switch (rule->action)
        {
        case TIER1_BURS_ACTION_KIND:
            match = pattern->rule == rule->kind;
            break;
        case TIER1_BURS_ACTION_LEA_REGISTER:
            match = pattern->rule == TIER1_RULE_LEA_ADD && pattern->values[1] != BUSTER_IR_NONE;
            break;
        case TIER1_BURS_ACTION_LEA_IMMEDIATE:
            match = pattern->rule == TIER1_RULE_LEA_ADD && pattern->values[1] == BUSTER_IR_NONE;
            break;
        case TIER1_BURS_ACTION_SIGNED_PRODUCT:
            match = (p == 0 && (pattern->rule == TIER1_RULE_COPY || pattern->rule == TIER1_RULE_SIGN_EXTEND)) ||
                    (pattern->operation == X86_OPERATION_IMUL && pattern->fold_count != 0 && (pattern->rule == TIER1_RULE_IMMEDIATE || pattern->rule == TIER1_RULE_REGISTER));
            break;
        case TIER1_BURS_ACTION_NARROW:
            match = pattern->rule == rule->kind || (p == 0 && (pattern->rule == TIER1_RULE_COPY || pattern->rule == TIER1_RULE_TRUNCATE));
            break;
        case TIER1_BURS_ACTION_FLOAT_ADDRESS:
            match = pattern->rule == (rule->kind == TIER1_RULE_FLOAT_LOAD ? TIER1_RULE_FLOAT_LOAD_ADDRESS : TIER1_RULE_FLOAT_STORE_ADDRESS) ||
                    (pattern->rule == rule->kind && chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_LOAD:
            match = pattern->rule == TIER1_RULE_LOAD_ADDRESS || (pattern->rule == TIER1_RULE_LOAD && chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_STORE:
            match = !pattern->compare_immediate && (pattern->rule == TIER1_RULE_STORE_ADDRESS || (pattern->rule == TIER1_RULE_STORE && chosen == UINT32_MAX));
            break;
        case TIER1_BURS_ACTION_STORE_IMMEDIATE:
            match = pattern->compare_immediate && (pattern->rule == TIER1_RULE_STORE_ADDRESS || (pattern->rule == TIER1_RULE_STORE && chosen == UINT32_MAX));
            break;
        case TIER1_BURS_ACTION_WALKED_COPY:
            match = (pattern->rule == TIER1_RULE_COPY || pattern->rule == TIER1_RULE_ADDRESS) && (pattern->fold_count != 0 || chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_FOLDED_COMPARE:
            match = pattern->rule == TIER1_RULE_COMPARE || pattern->rule == TIER1_RULE_COMPARE_IMMEDIATE;
            break;
        default:
            break;
        }
        // The first match, or for a load, a store and a walked copy, a later walked one over an earlier plain one.
        const bool walked = rule->action == TIER1_BURS_ACTION_SIGNED_PRODUCT || rule->action == TIER1_BURS_ACTION_NARROW || rule->action == TIER1_BURS_ACTION_FLOAT_ADDRESS || rule->action == TIER1_BURS_ACTION_LOAD || rule->action == TIER1_BURS_ACTION_STORE || rule->action == TIER1_BURS_ACTION_STORE_IMMEDIATE ||
                            rule->action == TIER1_BURS_ACTION_WALKED_COPY;
        chosen = match && (chosen == UINT32_MAX || walked) ? p : chosen;
    }
    *munched = chosen == UINT32_MAX;
    return chosen != UINT32_MAX ? chosen : candidates->count != 0 ? tier1_select_munch(candidates) : UINT32_MAX;
}
#endif
