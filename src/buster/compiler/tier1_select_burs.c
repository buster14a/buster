// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's bottom-up rewrite selection (#141's design: Pelegrí-Llopart and Graham, 1988; iburg, Fraser, Hanson and
// Proebsting, 1992): the vector path's cover of the plan's integer core. Included by tier1_select.c, before its plan.
//
// - The grammar is a C table of patterns (tier1_burs_patterns), each a nonterminal's derivation from an operator and its
//   children's nonterminals, or from another nonterminal (a chain), with a cost: the records' structural bound that the
//   candidates' intrinsic costs give (docs/tier1-selection.md). A pattern stands for one candidate kind of
//   tier1_select_candidates: an inner one (an address's piece, an immediate, a truth value's link) folds into its root's.
// - tier1_select_initialize compiles it into state tables once (burg's algorithm, as Proebsting's "BURS automata
//   generation", 1995, describes it): a state is the cheapest rule and its normalized cost for every nonterminal; each
//   operator's child position projects a child's state onto the nonterminals the operator's patterns read there (its
//   representer); and each operator's table gives the state of each pair of representers. The tables are in the baked
//   section (baked.c), so a compile starts with them built, as with the encoder's templates.
// - A node's operator is its class (tier1_burs_classify): its IR operation, its type, and what the candidates' rules read
//   of its operands, as a constant's value (its class bits) or whether an operand is a load. A child the node's pattern
//   may fold is visible (a tree child, a constant, or a frame object's single address in the entry block); any other is
//   a value computed elsewhere, the REG leaf's state.
// - Labeling is bottom-up, a depth level at a time, 16 nodes a vector (tier1_burs_label): each node's state is a table
//   lookup on its operator and its children's representers, gathered. Choosing each node's rule is a lookup too
//   (tier1_burs_rules): the rule of its goal, a statement's or a register's.
// - The plan builds the pattern of each node its code computes from that rule: tier1_select_candidates' candidate of the
//   rule's kind (tier1_burs_pick), whose walks fill its fields. A node whose operator the grammar doesn't cover, or whose
//   rule names a candidate the walks didn't make (a walk past BUSTER_TIER1_SELECT_WALK), takes maximal munch's
//   (tier1_select_munch), the scalar twin's choice: it is the fallback for what the tables don't cover. An address whose
//   displacement doesn't fit 32 bits has no candidate at all: its class is the REG leaf's (tier1_burs_displacement),
//   whose pattern the plan builds for every node, so that the vector path refuses it as the scalar twin does. The cover is
//   the cheapest the grammar has for each tree, and the grammar has every candidate the generator makes for the
//   operators it covers, so it is never costlier than munch's.

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
    // An integer comparison a branch or a truth value takes, a floating one, and a truth value's zero extension of each.
    TIER1_BURS_COND,
    TIER1_BURS_FCOND,
    TIER1_BURS_ZCOND,
    TIER1_BURS_ZFCOND,
    TIER1_BURS_NONTERMINALS,
} Tier1BursNonterminal;

// A constant's class bits (tier1_burs_constant_bits): which of the constant nonterminals it derives.
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
#define BUSTER_TIER1_BURS_ALUS 8u

// The operators. REG is a value the pattern doesn't look into: an operator the grammar doesn't cover, or a child that
// isn't visible. The ALU operators are an operation's (ADD32 to XOR) times its kinds.
typedef enum Tier1BursOperator
{
    TIER1_BURS_OP_REG,
    TIER1_BURS_OP_SLOT,
    TIER1_BURS_OP_SLOT_X,
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
    TIER1_BURS_OP_CBR_ADJ,
    TIER1_BURS_OPERATORS,
} Tier1BursOperator;

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

// The families a pattern names: a set of operators, an ALU family's restricted to `kinds` (a bit per kind).
typedef enum Tier1BursFamily
{
    TIER1_BURS_FAMILY_NONE,
    TIER1_BURS_FAMILY_CHAIN,
    TIER1_BURS_FAMILY_REG,
    TIER1_BURS_FAMILY_SLOT,
    TIER1_BURS_FAMILY_CONST,
    TIER1_BURS_FAMILY_ALU,
    TIER1_BURS_FAMILY_ADD,
    TIER1_BURS_FAMILY_ADD64,
    TIER1_BURS_FAMILY_SUB64,
    TIER1_BURS_FAMILY_MUL,
    TIER1_BURS_FAMILY_COMMUTATIVE,
    TIER1_BURS_FAMILY_IMMEDIATE,
    TIER1_BURS_FAMILY_LEFT_IMMEDIATE,
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
    TIER1_BURS_FAMILY_CBR_ADJ,
} Tier1BursFamily;

// What the plan takes as a root's pattern (tier1_burs_pick): the candidate of `kind`, with LEA_ADD's of a register or of
// a displacement apart; a load's or a store's walked address where it has one; a conversion's walked copy; a truth
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
    TIER1_BURS_ACTION_WALKED_COPY,
    TIER1_BURS_ACTION_FOLDED_COMPARE,
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
    // The ALU operations of registers, immediates (on the left where commutative and the right isn't a constant), lea's
    // additions, multiplications by constants, a shift left as a lea's index, and loads as memory operands (on the left
    // where commutative and the right isn't a load).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ALU, BUSTER_TIER1_BURS_ANY, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_REGISTER },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_ANY, { TIER1_BURS_REG, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_LEA_REGISTER, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_IMMEDIATE, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_LEFT_IMMEDIATE, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_IMM }, 1, TIER1_BURS_ACTION_LEA_IMMEDIATE, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_IMM, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_LEA_IMMEDIATE, TIER1_RULE_LEA_ADD },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_POW2 }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_POW2, TIER1_BURS_REG }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_REG, TIER1_BURS_M359 }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_LEA },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_MUL, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_M359, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_MULTIPLY_LEA },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_K(S), { TIER1_BURS_REG, TIER1_BURS_SHL3 }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_LEA_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ADD, BUSTER_TIER1_BURS_NOT_S, { TIER1_BURS_SHL3, TIER1_BURS_REG }, 1, TIER1_BURS_ACTION_KIND, TIER1_RULE_LEA_SHIFT },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ALU, BUSTER_TIER1_BURS_K(L), { TIER1_BURS_REG, TIER1_BURS_LOADF }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_COMMUTATIVE, BUSTER_TIER1_BURS_NOT_L, { TIER1_BURS_LOADF, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_LOAD_OPERAND },
    { TIER1_BURS_SHL3, TIER1_BURS_FAMILY_SHL, 0, { TIER1_BURS_REG, TIER1_BURS_SCALE }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    // Shifts, by a constant count below their width, or by a register's (cl).
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SHIFT, 0, { TIER1_BURS_REG, TIER1_BURS_SHAMT }, 2, TIER1_BURS_ACTION_KIND, TIER1_RULE_IMMEDIATE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_SHIFT, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 4, TIER1_BURS_ACTION_KIND, TIER1_RULE_SHIFT },
    // The address walk (tier1_select_address): 64-bit additions of a displacement, conversions between a pointer and its
    // bits, addresses without an index or with a constant one, and one address with an index, whose own walk peels the
    // index's constant additions and subtractions (tier1_select_index).
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_A0, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A0, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_DISP, TIER1_BURS_A0 }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_K(C), { TIER1_BURS_A1, TIER1_BURS_DISP }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_A1, TIER1_BURS_FAMILY_ADD64, BUSTER_TIER1_BURS_NOT_C, { TIER1_BURS_DISP, TIER1_BURS_A1 }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
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
    // Comparisons, their truth values' chains through C's `!= 0` and `== 0` (tier1_select_truth), and the branches that
    // fuse with them; and a zero extension's own move.
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_IMM }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_COMPARE_IMMEDIATE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP, 0, { TIER1_BURS_REG, TIER1_BURS_IMM }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_COND, TIER1_BURS_FAMILY_ICMP_LINK, 0, { TIER1_BURS_ZCOND, TIER1_BURS_ZERO }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_FCOND, TIER1_BURS_FAMILY_ICMP_LINK, 0, { TIER1_BURS_ZFCOND, TIER1_BURS_ZERO }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ZCOND, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_ZFCOND, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_FCOND, BUSTER_TIER1_BURS_NO_KID }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ZEXT_TRUTH, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_FOLDED_COMPARE, TIER1_RULE_COMPARE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_ZEXT, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 1, TIER1_BURS_ACTION_PLAIN, TIER1_RULE_ZERO_EXTEND },
    { TIER1_BURS_FCOND, TIER1_BURS_FAMILY_FCMP_ORD, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 0, TIER1_BURS_ACTION_NONE, TIER1_RULE_NONE },
    { TIER1_BURS_REG, TIER1_BURS_FAMILY_FCMP_ORD, 0, { TIER1_BURS_REG, TIER1_BURS_REG }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_COND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_BRANCH_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_FCOND, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_FLOAT_BRANCH_COMPARE },
    { TIER1_BURS_STMT, TIER1_BURS_FAMILY_CBR_ADJ, 0, { TIER1_BURS_REG, BUSTER_TIER1_BURS_NO_KID }, 3, TIER1_BURS_ACTION_KIND, TIER1_RULE_BRANCH },
};

#define BUSTER_TIER1_BURS_PATTERNS (sizeof(tier1_burs_patterns) / sizeof(tier1_burs_patterns[0]))
// A state's costs are normalized (its cheapest nonterminal's is zero) and saturate at BUSTER_TIER1_BURS_BOUND: this grammar
// isn't finite-state without it, since folding an address's or a truth value's chain costs nothing while computing it
// into a register costs more for each node of it, so that a state of each chain length would be a state of its own
// (burg's generator fails on such a grammar; Proebsting, 1995). Within one node's choice, the alternatives that read a
// saturated cost read it at the same child, so they differ by what they fold of their other children, which the bound
// keeps. 8 makes 582 states, at most 57 representers in an operator's position, and a pool of 21,777 entries; 16 makes
// 1,430 states and 47,735 entries, and 4, 346 and 14,490. The tests compare every cover's cost with munch's.
#define BUSTER_TIER1_BURS_BOUND 8u
// The tables' bounds: the generator fails past them, which tier1_select_initialize reports.
#define BUSTER_TIER1_BURS_MAX_STATES 1024u
#define BUSTER_TIER1_BURS_MAX_REPRESENTERS 128u
#define BUSTER_TIER1_BURS_MAX_RULES 512u
// The pool of the operators' maps (state to representer) and transition tables, 16-bit entries, past its end the 16-bit
// pad that a 32-bit gather of its last entry reads.
#define BUSTER_TIER1_BURS_POOL 32768u
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
BUSTER_TIER1_SHARED BUSTER_BAKED uint16_t tier1_burs_pool[BUSTER_TIER1_BURS_POOL + 2];
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_pool_length;
// Each operator's goal at a root: a statement's (a store and a branch) or a register's.
BUSTER_TIER1_SHARED BUSTER_BAKED uint8_t tier1_burs_goal[TIER1_BURS_OPERATORS + 4];
// 0 before tier1_select_initialize, 1 while one thread builds, 2 built and 3 failed.
BUSTER_TIER1_SHARED BUSTER_BAKED uint32_t tier1_burs_status;

static uint32_t tier1_burs_alu(uint32_t operation, uint32_t kind)
{
    return TIER1_BURS_OP_ALU + operation * BUSTER_TIER1_BURS_KINDS + kind;
}

static uint8_t tier1_burs_operator_arity(uint32_t op)
{
    uint8_t arity = 1;
    if (op < TIER1_BURS_OP_ALU)
    {
        arity = 0;
    }
    else if (op < TIER1_BURS_OP_SHL + 3 || op == TIER1_BURS_OP_STORE || op == TIER1_BURS_OP_ADDR_CIDX || op == TIER1_BURS_OP_ADDR_IDX || op == TIER1_BURS_OP_ICMP ||
             op == TIER1_BURS_OP_ICMP_LINK || op == TIER1_BURS_OP_FCMP_ORD)
    {
        arity = 2;
    }
    return arity;
}

// Whether a pattern of `family` (with `kinds`) derives from operator `op`.
static bool tier1_burs_member(uint32_t family, uint32_t kinds, uint32_t op)
{
    const bool alu = op >= TIER1_BURS_OP_ALU && op < TIER1_BURS_OP_SHL;
    const uint32_t operation = alu ? (op - TIER1_BURS_OP_ALU) / BUSTER_TIER1_BURS_KINDS : UINT32_MAX;
    const uint32_t kind = alu ? (op - TIER1_BURS_OP_ALU) % BUSTER_TIER1_BURS_KINDS : UINT32_MAX;
    const bool kinded = alu && (kinds == 0 || ((kinds >> kind) & 1) != 0);
    const bool add = operation == BUSTER_TIER1_BURS_ADD32 || operation == BUSTER_TIER1_BURS_ADD64;
    const bool commutative = add || operation == BUSTER_TIER1_BURS_MUL || operation == BUSTER_TIER1_BURS_AND || operation == BUSTER_TIER1_BURS_OR || operation == BUSTER_TIER1_BURS_XOR;
    // An addition alone is of kind S: another operation whose right operand shifts is of kind O.
    const bool real = alu && (kind != BUSTER_TIER1_BURS_KIND_S || add);
    bool member = false;
    switch (family)
    {
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
    case TIER1_BURS_FAMILY_LEFT_IMMEDIATE:
        member = real && kinded && commutative && operation != BUSTER_TIER1_BURS_MUL;
        break;
    case TIER1_BURS_FAMILY_SHIFT:
        member = op == TIER1_BURS_OP_SHL || op == TIER1_BURS_OP_LSHR || op == TIER1_BURS_OP_ASHR;
        break;
    case TIER1_BURS_FAMILY_SHL:
        member = op == TIER1_BURS_OP_SHL;
        break;
    case TIER1_BURS_FAMILY_LOAD:
        member = op == TIER1_BURS_OP_LOAD_F || op == TIER1_BURS_OP_LOAD_N;
        break;
    case TIER1_BURS_FAMILY_LOAD_F:
        member = op == TIER1_BURS_OP_LOAD_F;
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
    case TIER1_BURS_FAMILY_CBR_ADJ:
        member = op == TIER1_BURS_OP_CBR_ADJ;
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
        tier1_burs_arity[op] = tier1_burs_operator_arity(op);
        tier1_burs_goal[op] = op == TIER1_BURS_OP_STORE || op == TIER1_BURS_OP_CBR_ADJ ? TIER1_BURS_STMT : TIER1_BURS_REG;
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
                for (uint32_t k = 0; pattern->family != TIER1_BURS_FAMILY_CHAIN && k < tier1_burs_arity[op]; k += 1)
                {
                    builder.relevant[k][op] |= pattern->kids[k] < TIER1_BURS_NONTERMINALS ? 1u << pattern->kids[k] : 0u;
                }
            }
        }
    }
    // The leaves' states, the REG leaf's first; then burg's fixpoint: each state's representers at every operator's
    // positions, and for each new representer, its table's pairs with the other position's representers.
    for (uint32_t op = 0; builder.valid && op < TIER1_BURS_OP_ALU; op += 1)
    {
        const uint32_t s = tier1_burs_derive(&builder, op, 0, 0);
        tier1_burs_table_base[op] = s;
    }
    builder.valid &= builder.valid && tier1_burs_table_base[TIER1_BURS_OP_REG] == BUSTER_TIER1_BURS_REG_STATE;
    for (uint32_t s = 0; builder.valid && s < builder.state_count; s += 1)
    {
        for (uint32_t op = TIER1_BURS_OP_ALU; builder.valid && op < TIER1_BURS_OPERATORS; op += 1)
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
    return builder.valid;
}

// Builds the selector's state tables once (#141): the vector path's plan needs them, and a baked executable has them
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

// A constant's class bits, for its own width: what the candidates' consumers fold of its value (tier1_select_candidates:
// an immediate that fits, a lea's shift count, a multiplication's power of two or its 3, 5 or 9, a shift's count, a truth
// value's zero, a 32-bit displacement).
static uint32_t tier1_burs_constant_bits(uint32_t width, uint64_t value)
{
    const uint32_t shift = tier1_select_log2(value);
    uint32_t bits = tier1_select_immediate_fits(width, value) ? BUSTER_TIER1_BURS_FIT : 0;
    bits |= value <= 3 ? BUSTER_TIER1_BURS_SCALE : 0;
    bits |= shift < width * 8 ? BUSTER_TIER1_BURS_POW2 : 0;
    bits |= value == 3 || value == 5 || value == 9 ? BUSTER_TIER1_BURS_M359 : 0;
    bits |= value < width * 8 ? BUSTER_TIER1_BURS_SHAMT : 0;
    bits |= value == 0 ? BUSTER_TIER1_BURS_ZERO : 0;
    bits |= (uint64_t)(int64_t)(int32_t)value == value ? BUSTER_TIER1_BURS_DISP : 0;
    return bits;
}

// Whether `node` is a link of a truth value's chain (tier1_select_truth): `icmp ne/eq (zext x), 0` in consecutive nodes,
// x an I1 right before its extension, the zero only the comparison's.
static bool tier1_burs_link(const Tier1SelectForest* forest, uint32_t node)
{
    const IrFunction* function = forest->function;
    const uint32_t extended = function->a[node];
    const uint32_t zero = function->b[node];
    uint64_t value = 1;
    return function->c[node] <= IR_NE && extended + 2 == node && zero + 1 == node && tier1_select_constant(function, zero, &value) && value == 0 &&
           function->op[extended] == IR_ZEXT && function->type[function->a[extended]] == IR_I1 && function->a[extended] + 1 == extended && forest->uses[zero] == 1 &&
           forest->blocks[zero] == forest->blocks[node];
}

// Whether an address's displacement, its constant index scaled and its offset, fits a disp32: the condition of its lea's
// candidate (tier1_select_candidates). An address past it has no candidate, and its class is the munch's, which refuses
// it wherever the walks would.
// The vector classifier gathers an address's offset and its scale from its 16-byte record.
_Static_assert(sizeof(IrAddress) == 16 && offsetof(IrAddress, offset) == 0 && offsetof(IrAddress, scale) == 8, "IrAddress's layout, which tier1_burs_classify_vector gathers");

static bool tier1_burs_displacement(const IrFunction* function, uint32_t node)
{
    const IrAddress operand = function->addresses[function->c[node]];
    const uint32_t b = function->b[node];
    uint64_t constant = 0;
    const bool constant_index = b != BUSTER_IR_NONE && tier1_select_constant(function, b, &constant);
    int64_t displacement = 0;
    return !__builtin_mul_overflow((int64_t)(constant_index ? constant : 0), (int64_t)operand.scale, &displacement) && !__builtin_add_overflow(displacement, operand.offset, &displacement) &&
           displacement == (int64_t)(int32_t)displacement;
}

// Each node's operator (tier1_burs_classify's scalar loop): what tier1_select_candidates would make of it. `users` is each
// single-use node's user.
static uint32_t tier1_burs_classify_node(const Tier1SelectForest* forest, const uint32_t* users, uint32_t node)
{
    const IrFunction* function = forest->function;
    const uint32_t count = function->instruction_count;
    const uint32_t op = function->op[node];
    const uint32_t type = function->type[node];
    const uint32_t a = function->a[node];
    const uint32_t b = function->b[node];
    const bool integer = type == IR_I32 || type == IR_I64;
    const uint32_t a_type = a < count ? function->type[a] : IR_VOID;
    uint32_t result = TIER1_BURS_OP_REG;
    if (op == IR_CONST && tier1_select_mask_width(type) == 0 && tier1_select_float_width(type) == 0 && tier1_select_width(type) != 0)
    {
        result = TIER1_BURS_OP_CONST + tier1_burs_constant_bits(tier1_select_width(type), function->constants[a].low);
    }
    else if ((op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_AND || op == IR_OR || op == IR_XOR) && integer && b < count)
    {
        const uint32_t operation = op == IR_ADD ? (type == IR_I64 ? BUSTER_TIER1_BURS_ADD64 : BUSTER_TIER1_BURS_ADD32) :
                                   op == IR_SUB ? (type == IR_I64 ? BUSTER_TIER1_BURS_SUB64 : BUSTER_TIER1_BURS_SUB32) :
                                   op == IR_MUL ? BUSTER_TIER1_BURS_MUL : op == IR_AND ? BUSTER_TIER1_BURS_AND : op == IR_OR ? BUSTER_TIER1_BURS_OR : BUSTER_TIER1_BURS_XOR;
        const uint32_t right = function->op[b];
        const uint32_t kind = right == IR_CONST ? BUSTER_TIER1_BURS_KIND_C : right == IR_LOAD ? BUSTER_TIER1_BURS_KIND_L :
                              right == IR_SHL && op == IR_ADD ? BUSTER_TIER1_BURS_KIND_S : BUSTER_TIER1_BURS_KIND_O;
        result = tier1_burs_alu(operation, kind);
    }
    else if ((op == IR_SHL || op == IR_LSHR || op == IR_ASHR) && integer)
    {
        result = op == IR_SHL ? TIER1_BURS_OP_SHL : op == IR_LSHR ? TIER1_BURS_OP_LSHR : TIER1_BURS_OP_ASHR;
    }
    else if (op == IR_LOAD && tier1_select_memory_width(type) != 0)
    {
        const uint32_t user = users[node];
        const bool foldable = function->flags[node] == 0 && forest->uses[node] == 1 && user < count && node < user && forest->blocks[user] == forest->blocks[node] &&
                              forest->barriers[user] <= node;
        result = foldable ? TIER1_BURS_OP_LOAD_F : TIER1_BURS_OP_LOAD_N;
    }
    else if (op == IR_STORE && b < count && tier1_select_memory_width(function->type[b]) != 0)
    {
        result = TIER1_BURS_OP_STORE;
    }
    else if (op == IR_ADDRESS && tier1_burs_displacement(function, node))
    {
        result = b == BUSTER_IR_NONE ? TIER1_BURS_OP_ADDR_NOIDX : b < count && function->op[b] == IR_CONST ? TIER1_BURS_OP_ADDR_CIDX : TIER1_BURS_OP_ADDR_IDX;
    }
    else if ((op == IR_INTTOPTR && a_type == IR_I64) || (op == IR_PTRTOINT && type == IR_I64))
    {
        result = TIER1_BURS_OP_CONV_PTR;
    }
    else if (op == IR_STACK_ADDR && tier1_select_stack_object(function, a))
    {
        const uint32_t user = users[node];
        result = forest->blocks[node] == 0 && forest->uses[node] == 1 && user < count && node < user ? TIER1_BURS_OP_SLOT_X : TIER1_BURS_OP_SLOT;
    }
    else if (op == IR_ICMP && b < count && (a_type == IR_I32 || a_type == IR_I64 || a_type == IR_PTR))
    {
        result = tier1_burs_link(forest, node) ? TIER1_BURS_OP_ICMP_LINK : TIER1_BURS_OP_ICMP;
    }
    else if (op == IR_ZEXT && (type == IR_I8 || type == IR_I16 || integer) && (a_type == IR_I1 || a_type == IR_I8 || a_type == IR_I16 || a_type == IR_I32))
    {
        result = a_type == IR_I1 && a + 1 == node ? TIER1_BURS_OP_ZEXT_TRUTH : TIER1_BURS_OP_ZEXT_PLAIN;
    }
    else if (op == IR_FCMP && b < count && tier1_select_float_width(a_type) != 0 && function->c[node] >= IR_LT && function->c[node] <= IR_GE)
    {
        result = TIER1_BURS_OP_FCMP_ORD;
    }
    else if (op == IR_CBR && a + 1 == node)
    {
        result = TIER1_BURS_OP_CBR_ADJ;
    }
    return result;
}

// Whether `child`, an operand of `node`, is visible to its patterns: a tree child (tier1_select_tree_child), a constant
// (whose value any consumer folds), or a frame object's single address in the entry block (tier1_select_address's walk
// takes it past its block); and labeled before it.
static bool tier1_burs_visible(const Tier1SelectForest* forest, const uint32_t* classes, uint32_t node, uint32_t child)
{
    const uint32_t class = child < node ? classes[child] : TIER1_BURS_OP_REG;
    return child < node && (tier1_select_tree_child(forest, node, child) || (class >= TIER1_BURS_OP_CONST && class < TIER1_BURS_OP_ALU) || class == TIER1_BURS_OP_SLOT_X);
}

// The nodes' operators and visible children: the scalar twin of tier1_burs_classify_vector, an exact one. `kids` is two
// columns, each node's first operand (a) and second (b) where its operator reads one and it is visible, else
// BUSTER_IR_NONE.
static void tier1_burs_classify_scalar(const Tier1SelectForest* forest, const uint32_t* users, uint32_t* classes, uint32_t* kids)
{
    const IrFunction* function = forest->function;
    const uint32_t count = function->instruction_count;
    for (uint32_t node = 0; node < count; node += 1)
    {
        classes[node] = tier1_burs_classify_node(forest, users, node);
    }
    for (uint32_t node = 0; node < count; node += 1)
    {
        const uint32_t arity = tier1_burs_arity[classes[node]];
        const uint32_t a = function->a[node];
        const uint32_t b = function->b[node];
        kids[node] = arity >= 1 && tier1_burs_visible(forest, classes, node, a) ? a : BUSTER_IR_NONE;
        kids[count + node] = arity >= 2 && tier1_burs_visible(forest, classes, node, b) ? b : BUSTER_IR_NONE;
    }
}

#if BUSTER_HOST_VECTOR
// Per IR operation: an ALU operation's index (BUSTER_TIER1_BURS_ADD32 to XOR, 0xff for none), and a shift's operator.
static uint32_t tier1_burs_alu_index(uint32_t op)
{
    return op == IR_ADD ? BUSTER_TIER1_BURS_ADD32 : op == IR_SUB ? BUSTER_TIER1_BURS_SUB32 : op == IR_MUL ? BUSTER_TIER1_BURS_MUL : op == IR_AND ? BUSTER_TIER1_BURS_AND :
           op == IR_OR ? BUSTER_TIER1_BURS_OR : op == IR_XOR ? BUSTER_TIER1_BURS_XOR : 0xffu;
}

// Whether each lane's type (32-bit lanes) is in the set `types`, a bit per IrType.
static __mmask16 tier1_burs_types(__mmask16 active, __m512i type, uint32_t types)
{
    const __m512i bit = _mm512_sllv_epi32(_mm512_set1_epi32(1), type);
    return _mm512_mask_test_epi32_mask(active, bit, _mm512_set1_epi32((int)types));
}

// The constant class bits (tier1_burs_constant_bits) of 8 values at once, each with its width in bytes.
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

// The nodes' operators and visible children, 16 at a time: tier1_burs_classify_scalar's, exactly. Each node's operation,
// type and flags go first into one 32-bit word (`words`, `count` of them), whose gathers then stay in the columns; the
// children's columns are gathered by their indices where those are nodes. A frame object's address, whose slot the
// class reads, takes the scalar classifier.
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
    uint32_t alu_table[IR_OP_COUNT];
    for (uint32_t op = 0; op < IR_OP_COUNT; op += 1)
    {
        alu_table[op] = tier1_burs_alu_index(op);
    }
    const __m512i none = _mm512_set1_epi32((int)BUSTER_IR_NONE);
    const __m512i byte = _mm512_set1_epi32(0xff);
    const __m512i positions = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const uint32_t width4 = (1u << IR_I1) | (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32);
    const uint32_t width8 = (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t memory = (1u << IR_I8) | (1u << IR_I16) | (1u << IR_I32) | (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t integers = (1u << IR_I32) | (1u << IR_I64);
    const uint32_t compared = (1u << IR_I32) | (1u << IR_I64) | (1u << IR_PTR);
    const uint32_t floats = (1u << IR_F32) | (1u << IR_F64);
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t remaining = count - first;
        const __mmask16 active = remaining >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << remaining) - 1);
        const __m512i node = _mm512_add_epi32(_mm512_set1_epi32((int)first), positions);
        const __m512i word = _mm512_maskz_loadu_epi32(active, words + first);
        const __m512i op = _mm512_and_si512(word, byte);
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
        __m512i result = _mm512_set1_epi32(TIER1_BURS_OP_REG);
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
        // ALU operations: the operation (a 64-bit addition or subtraction apart), and the kind of the right operand.
        const __m512i alu = _mm512_mask_i32gather_epi32(byte, active, op, alu_table, 4);
        const __mmask16 is_alu = _mm512_mask_cmpneq_epi32_mask(integer & b_valid, alu, byte);
        if (is_alu != 0)
        {
            const __mmask16 wide = is_i64 & (_mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADD)) | _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_SUB)));
            const __m512i operation = _mm512_mask_add_epi32(alu, wide, alu, _mm512_set1_epi32(1));
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
        // Loads, foldable into their single user past no barrier; stores.
        const __mmask16 load = tier1_burs_types(active, type, memory) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_LOAD));
        if (load != 0)
        {
            const __mmask16 placed = _mm512_mask_cmplt_epu32_mask(load, user, _mm512_set1_epi32((int)count)) & _mm512_cmplt_epu32_mask(node, user);
            const __m512i user_block = _mm512_mask_i32gather_epi32(none, placed, user, forest->blocks, 4);
            const __m512i barrier = _mm512_mask_i32gather_epi32(none, placed, user, forest->barriers, 4);
            const __mmask16 foldable = placed & _mm512_cmpeq_epi32_mask(flags, _mm512_setzero_si512()) & _mm512_cmpeq_epi32_mask(uses, _mm512_set1_epi32(1)) &
                                       _mm512_cmpeq_epi32_mask(user_block, block) & _mm512_cmple_epu32_mask(barrier, node);
            result = _mm512_mask_mov_epi32(result, load, _mm512_set1_epi32(TIER1_BURS_OP_LOAD_N));
            result = _mm512_mask_mov_epi32(result, foldable, _mm512_set1_epi32(TIER1_BURS_OP_LOAD_F));
        }
        const __mmask16 store = b_valid & tier1_burs_types(active, b_type, memory) & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STORE));
        result = _mm512_mask_mov_epi32(result, store, _mm512_set1_epi32(TIER1_BURS_OP_STORE));
        // Addresses by their index, where their displacement fits (tier1_burs_displacement): the offset, which an index
        // that isn't a constant leaves alone, and a constant index's scaled value added. Where the constant or the offset
        // doesn't fit 32 bits, which may still sum to a displacement that does, the scalar classifier decides.
        const __mmask16 address = active & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_ADDRESS));
        uint32_t exact = 0;
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
                exact = constant_index & ~small;
            }
            const __mmask16 no_index = covered & _mm512_cmpeq_epi32_mask(b, none);
            result = _mm512_mask_mov_epi32(result, covered, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_IDX));
            result = _mm512_mask_mov_epi32(result, no_index, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_NOIDX));
            result = _mm512_mask_mov_epi32(result, covered & constant_index, _mm512_set1_epi32(TIER1_BURS_OP_ADDR_CIDX));
        }
        const __mmask16 conversion = (a_valid & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_INTTOPTR)) & _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I64))) |
                                     (is_i64 & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_PTRTOINT)));
        result = _mm512_mask_mov_epi32(result, conversion, _mm512_set1_epi32(TIER1_BURS_OP_CONV_PTR));
        // Integer comparisons, and among them a truth value's links (tier1_burs_link): ne or eq, of the zero extension
        // right before the zero right before it, of the I1 right before the extension.
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
        const __mmask16 branch = active & adjacent & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_CBR));
        result = _mm512_mask_mov_epi32(result, branch, _mm512_set1_epi32(TIER1_BURS_OP_CBR_ADJ));
        _mm512_mask_storeu_epi32(classes + first, active, result);
        // A frame object's address, and an address the vector rule leaves: the scalar classifier's, which reads its slot
        // or its displacement.
        uint32_t slots = (active & _mm512_cmpeq_epi32_mask(op, _mm512_set1_epi32(IR_STACK_ADDR))) | exact;
        while (slots != 0)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(slots);
            classes[first + lane] = tier1_burs_classify_node(forest, users, first + lane);
            slots &= slots - 1;
        }
    }
    // The visible children, from the classes: a tree child, a constant or a frame object's single address in the entry
    // block, before the node, at a position its operator reads.
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
            const __mmask16 slot = _mm512_cmpeq_epi32_mask(child_class, _mm512_set1_epi32(TIER1_BURS_OP_SLOT_X));
            const __mmask16 visible = read & (tree | constant | slot);
            _mm512_mask_storeu_epi32(kids + (uint64_t)position * count + first, active, _mm512_mask_blend_epi32(visible, none, child));
        }
    }
}
#endif

// Each node's state, a depth level at a time (`level_offsets` and `level_nodes`, the plan's): the scalar twin, node by
// node, of tier1_burs_label_vector.
static __attribute__((unused)) void tier1_burs_label_scalar(uint32_t count, const uint32_t* classes, const uint32_t* kids, uint32_t* states)
{
    for (uint32_t node = 0; node < count; node += 1)
    {
        const uint32_t op = classes[node];
        const uint32_t left = kids[node] != BUSTER_IR_NONE ? states[kids[node]] : BUSTER_TIER1_BURS_REG_STATE;
        const uint32_t right = kids[count + node] != BUSTER_IR_NONE ? states[kids[count + node]] : BUSTER_TIER1_BURS_REG_STATE;
        const uint32_t row = tier1_burs_pool[tier1_burs_map_base[0][op] + left];
        const uint32_t column = tier1_burs_pool[tier1_burs_map_base[1][op] + right];
        states[node] = tier1_burs_pool[tier1_burs_table_base[op] + row * tier1_burs_table_width[op] + column];
    }
}

// Each node's rule: its state's rule of its operator's goal. The scalar twin of tier1_burs_rules_vector.
static __attribute__((unused)) void tier1_burs_rules_scalar(uint32_t count, const uint32_t* classes, const uint32_t* states, uint32_t* rules)
{
    for (uint32_t node = 0; node < count; node += 1)
    {
        rules[node] = tier1_burs_state_rules[states[node] * TIER1_BURS_NONTERMINALS + tier1_burs_goal[classes[node]]];
    }
}

#if BUSTER_HOST_VECTOR
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
            const __m512i left = _mm512_mask_i32gather_epi32(reg, left_visible, left_kid, states, 4);
            const __m512i right = _mm512_mask_i32gather_epi32(reg, right_visible, right_kid, states, 4);
            const __m512i left_map = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_map_base[0], 4);
            const __m512i right_map = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_map_base[1], 4);
            const __m512i base = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_table_base, 4);
            const __m512i width = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, op, tier1_burs_table_width, 4);
            const __m512i row = tier1_burs_gather_pool(active, _mm512_add_epi32(left_map, left));
            const __m512i column = tier1_burs_gather_pool(active, _mm512_add_epi32(right_map, right));
            const __m512i entry = _mm512_add_epi32(_mm512_add_epi32(base, _mm512_mullo_epi32(row, width)), column);
            const __m512i state = tier1_burs_gather_pool(active, entry);
            _mm512_mask_i32scatter_epi32(states, active, nodes, state, 4);
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
#endif

// The candidate of `node` that its rule names (Tier1BursAction): its index in `candidates`, or munch's choice where the
// rule names none or the walks didn't make the one it names, which *munched says.
static uint32_t tier1_burs_pick(const Tier1SelectCandidates* candidates, uint32_t rule_index, bool* munched)
{
    const Tier1BursRule* rule = &tier1_burs_rules[rule_index < tier1_burs_rule_count ? rule_index : 0];
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
        case TIER1_BURS_ACTION_LOAD:
            match = pattern->rule == TIER1_RULE_LOAD_ADDRESS || (pattern->rule == TIER1_RULE_LOAD && chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_STORE:
            match = pattern->rule == TIER1_RULE_STORE_ADDRESS || (pattern->rule == TIER1_RULE_STORE && chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_WALKED_COPY:
            match = (pattern->rule == TIER1_RULE_COPY || pattern->rule == TIER1_RULE_ADDRESS) && (pattern->fold_count != 0 || chosen == UINT32_MAX);
            break;
        case TIER1_BURS_ACTION_FOLDED_COMPARE:
            match = pattern->rule == TIER1_RULE_COMPARE || pattern->rule == TIER1_RULE_COMPARE_IMMEDIATE;
            break;
        case TIER1_BURS_ACTION_PLAIN:
            match = p == 0;
            break;
        default:
            break;
        }
        // The first match, or for a load, a store and a walked copy, a later walked one over an earlier plain one.
        const bool walked = rule->action == TIER1_BURS_ACTION_LOAD || rule->action == TIER1_BURS_ACTION_STORE || rule->action == TIER1_BURS_ACTION_WALKED_COPY;
        chosen = match && (chosen == UINT32_MAX || walked) ? p : chosen;
    }
    *munched = chosen == UINT32_MAX;
    return chosen != UINT32_MAX ? chosen : candidates->count != 0 ? tier1_select_munch(candidates) : UINT32_MAX;
}
