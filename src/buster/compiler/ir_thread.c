// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Truth values' blocks threaded into their predecessors (#2554). Include after ir.c. C's `&&`, `||`, `!` and `?:` leave a
// 0 or 1 that a later block tests: after promotion, a block of phis whose inputs are constants and comparisons' extended
// results, a short pure chain that tests them, and a conditional branch. Tier 1 then made the constant or the setcc in
// each predecessor, jumped to the block and tested it again (#138's record: 51.5 M and 22.5 M of speedtest1's dynamic
// instructions, 69.6 M and 65.7 M of Lua's). Here each such block J is threaded:
// - a predecessor P whose terminator is `br J` takes a copy of J's chain and branch, its phis replaced by P's inputs:
//   P then branches where J would have from P, and ir_truth_simplify (which the C driver runs again after this pass)
//   turns a copied `icmp ne (zext c), 0` into c itself, so the comparison is the branch;
// - a predecessor whose terminator is a conditional branch with one edge to J goes on that edge straight to J's successor
//   that J's chain chooses for it, where P's phi inputs are constants (jump threading proper);
// - every other predecessor (a switch's, an indirect jump's) keeps its edge, and J its phis' inputs from those.
// J's successors' phis take, for each predecessor that now reaches them, the value they took from J along its path: a J
// phi's input from P, a copied chain value, or the same value. J's own values are used only in J and by its successors'
// phis on its edges (otherwise J's dominance of their users could end), so every use stays dominated: a new path is an
// old one without J. A J that no reachable predecessor reaches any more is left an `unreachable` alone, its successors'
// phis without its inputs; the optimizer that follows removes it, the constant branches the copies made and the
// extensions and comparisons nothing reads.
//
// One exception (#2608, Lua's `tonumberns`, a `?:` whose join's phis are the truth value and the number it set): a phi
// of J may be read past J where each such reader (an instruction's block, or a phi entry's predecessor) is dominated by
// a successor S other than the entry that has J as its only predecessor. Threading gives S the predecessors that now
// reach it, so the rewrite puts a phi at S's start that merges the phi again, each predecessor's input to it (and the
// phi itself from J where J stays), and every reader in S's subtree reads that merge: local SSA repair over S's
// dominance subtree, which a new path still enters through S. Both successors may merge one phi, each for its own
// readers (`lua_geti`'s slot, set by `luaV_fastgeti`'s `?:`, is copied on its true successor and passed to
// `luaV_finishget` on its false one). J's chain values still may not be read past it. The repair is a truth value's:
// J's test reads none of the phis read past J, and the phi it reads first is a constant on some predecessor's edge
// that decides the test, or an extended comparison on one (`op_orderI`'s `cond != GETARG_k(i)`, whose `cond` is a
// setcc that the copy's branch then tests in its own block), so a data select's join
// (`y = x < lo ? lo : x; return y > hi ? hi : y;`) is left to if-conversion (ir_thread_decides). A J that looks like a
// loop's header in block order (a predecessor past it) isn't repaired: threading it would rotate the loop. These tests
// are cheap, and run before any walk: only then does a second pass of the escape scan, over the repairable
// candidates' values, find the successor that dominates each reader, each by a bounded walk back from the reader
// (ir_thread_cover), which the rewrite walks again for a phi both successors merge. No dominator tree is built.
//
// Bounds: J is not the entry or a label's block, has at least one phi, then at most BUSTER_IR_THREAD_BLOCK_LIMIT pure
// operations (constants, copies, extensions, truncations, integer comparisons, bitwise operations and shifts by a
// constant below the width, of integers and pointers, without flags), and branches on a value it defines, to two other
// blocks. A copy takes what the branch and the successors' phis read of them (IrThreadWork.needed), at most
// BUSTER_IR_THREAD_CHAIN_LIMIT besides constants: the rest is dead, the lowering's, which the optimizer removes. At most BUSTER_IR_THREAD_COPY_LIMIT predecessors take copies. A successor
// that only J enters and that has phis or merges one of J's keeps a predecessor: a J every predecessor of which would
// be redirected to its other successor is left alone (its test is one constant on every edge). Within a round, no block
// is both threaded and another's threaded predecessor or successor: a round's rewrite reads one CFG.
// BUSTER_IR_THREAD_ROUNDS rounds at most take the chains `a && b && c` makes, a next one only where a candidate was
// left to it: by the roles, or a repairable one whose readers no successor dominated yet (#2608: the first `?:` join of
// `tonumberns(a, n) && tonumberns(b, m)`, whose number is read past the `&&` join that the first round threads; the
// second round also threads the second `?:` join, which took that join's test). A block that a copy alone gives a
// candidate's shape asks for no round of its own: in #2608's first form such rounds were 82 of the 92 it added on
// sqlite3.c (each a CFG and an escape scan of the whole function, and its rewrite where it changed it), and Lua's 33
// repairs are the same without them. A function whose candidate blocks' phis all take a value from a later instruction
// (loops' headers) builds no CFG. A 64-byte opcode scan finds the conditional branches; the rest runs only for blocks
// that pass it. A successful result borrows the input's immutable tables, as ir_if_convert's does: the input and output
// arenas stay alive until the optimizer copies the function. The input is never mutated.

#ifndef BUSTER_IR_THREAD_C
#define BUSTER_IR_THREAD_C

#define BUSTER_IR_THREAD_BATCH 64u
#define BUSTER_IR_THREAD_CHAIN_LIMIT 4u
// A candidate's operations, its chain's and the dead ones beside it that the lowering left for the optimizer.
#define BUSTER_IR_THREAD_BLOCK_LIMIT 32u
#define BUSTER_IR_THREAD_COPY_LIMIT 16u
// The blocks a repair's walk from a reader back to the successor that merges it may visit (#2608), a multiple of 16.
#define BUSTER_IR_THREAD_WALK_LIMIT 64u
#define BUSTER_IR_THREAD_WIDE_PHI 32u
// A shape candidate whose wide phis have been indexed, after the escape scan.
#define BUSTER_IR_THREAD_INDEXED 2u
// A shape candidate whose values used past it are all phis, which a successor of which it is the only predecessor may
// merge again (#2608): what the second escape scan tracks. Every candidate is what the first one tracks.
#define BUSTER_IR_THREAD_REPAIRABLE 4u
#define BUSTER_IR_THREAD_TRACK_CANDIDATES UINT32_MAX
#define BUSTER_IR_THREAD_ROUNDS 4u

// A value's escapes byte (IrThreadWork.escapes): whether it is used where threading couldn't follow it, and for a phi of
// a repairable candidate, the successors (the true one, the false one) its uses' walks may stop at, those J alone
// enters; the successors that dominate one of its uses, which merge it again; and whether a use had none (#2608).
#define BUSTER_IR_THREAD_ESCAPE 1u
#define BUSTER_IR_THREAD_COVER_YES 2u
#define BUSTER_IR_THREAD_COVER_NO 4u
#define BUSTER_IR_THREAD_COVERS 6u
#define BUSTER_IR_THREAD_MERGE_SHIFT 2u
#define BUSTER_IR_THREAD_MERGES 24u
#define BUSTER_IR_THREAD_UNCOVERED 32u

// A block's roles in a round (IrThreadWork.roles).
#define BUSTER_IR_THREAD_THREADED 1u
#define BUSTER_IR_THREAD_PREDECESSOR 2u
#define BUSTER_IR_THREAD_SUCCESSOR 4u

// What a predecessor's terminator becomes (IrThreadWork.actions).
#define BUSTER_IR_THREAD_KEEP 0u
#define BUSTER_IR_THREAD_COPY 1u
#define BUSTER_IR_THREAD_REDIRECT 2u
#define BUSTER_IR_THREAD_COLLAPSE 3u

typedef struct IrThreadResult IrThreadResult;
struct IrThreadResult
{
    IrFunction function;
    // Over every round: the blocks threaded, the predecessors that took a copy of a block's chain and branch, and the
    // conditional predecessors' edges that go past a block (a collapse among them, where both edges reach one block).
    uint32_t blocks;
    uint32_t copies;
    uint32_t redirects;
    uint32_t rounds;
    // The phis read past a threaded block that a successor merges again (#2608).
    uint32_t merges;
    // The work: the rounds run, changed or not (a round without a candidate is a byte scan alone), and the blocks the
    // repairs' walks from their readers visited (ir_thread_cover).
    uint32_t attempts;
    uint32_t walked;
};

typedef struct IrThreadWork IrThreadWork;
struct IrThreadWork
{
    const IrFunction* function;
    IrCfg cfg;
    uint8_t* reachable;
    uint8_t* labeled;
    uint8_t* roles;
    // Per block: whether it is a candidate (its structure passed), a word each for the vector escape scan's gathers, and
    // per instruction whether a candidate's value is used where threading couldn't follow it (ir_thread_escapes), and
    // a repairable phi's covering successors (BUSTER_IR_THREAD_ESCAPE and _COVERS).
    uint32_t* candidates;
    uint8_t* escapes;
    // Per instruction of a threaded block: whether its branch or its successors' phis read it, through its chain: what a
    // copy takes. The block's other operations are pure and read by nothing else, which ir_thread_escapes checked.
    uint8_t* needed;
    // Per predecessor block: what its terminator becomes, the block it was threaded past, and the successor a redirect
    // or a collapse goes to.
    uint8_t* actions;
    uint32_t* threaded;
    uint32_t* targets;
    // Per threaded block: how many predecessors still reach it.
    uint32_t* remaining;
    // After reachability, reuse the CFG successor slots for the first phi's pair positions on indexed edges.
    uint32_t* input_positions;
    // Only joins with several indexed phis need values in addition to those positions.
    uint32_t** other_inputs;
    uint32_t block_count;
    uint32_t copy_count;
    uint32_t redirect_count;
    uint32_t cloned;
    // The threaded blocks' phis their successors merge again: the rewrite's new instructions besides the copies.
    uint32_t merged;
    // The blocks the repairs' walks visited (ir_thread_cover).
    uint32_t walked;
    // Candidates or their predecessors left for another round by the roles, and repairable candidates whose readers no
    // successor dominated: what a next round may still thread.
    uint32_t deferred;
};

// The conditional branches among `count` (at most 64) operations, a bit each.
static uint64_t ir_thread_candidates(const uint8_t* operations, uint32_t count)
{
    uint64_t candidates = 0;
    if (count != 0 && count <= BUSTER_IR_THREAD_BATCH)
    {
        const __mmask64 valid = UINT64_MAX >> (BUSTER_IR_THREAD_BATCH - count);
        const __m512i op = _mm512_maskz_loadu_epi8(valid, operations);
        candidates = valid & _mm512_cmpeq_epi8_mask(op, _mm512_set1_epi8(IR_CBR));
    }
    return candidates;
}

// Whether block J's phis (from `first`, before `end`) take a value of an instruction at or past J's start: a loop's back
// edge, whose header isn't a truth value's test (its phis are read in the loop, which the escape scan finds). A function
// whose candidates are all such needs no CFG: the cheap scan looks for one that isn't. Once the CFG is built, every
// candidate is checked, since a block order needn't be topological (luaV_execute's isn't).
static bool ir_thread_backward(const IrFunction* f, uint32_t first, uint32_t end)
{
    bool backward = false;
    for (uint32_t i = first; !backward && i < end && f->op[i] == IR_PHI; i += 1)
    {
        for (uint32_t p = 0; p < f->b[i]; p += 1)
        {
            backward = backward || f->extra[f->a[i] + p * 2 + 1] >= first;
        }
    }
    return backward;
}

static bool ir_thread_scalar_type(uint32_t type)
{
    return (type >= IR_I1 && type <= IR_I64) || type == IR_PTR;
}

// `type`'s width in bits, a pointer's 64.
static uint32_t ir_thread_width(uint32_t type)
{
    return type == IR_PTR ? 64 : ir_type_bits[type];
}

// Whether instruction i is a shift by a constant amount below its width, which is total (#2608: Lua's `GETARG_k(i)`).
static bool ir_thread_constant_shift(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    const uint32_t amount = op == IR_SHL || op == IR_LSHR || op == IR_ASHR ? f->b[i] : BUSTER_IR_NONE;
    const bool constant = amount < f->instruction_count && f->op[amount] == IR_CONST;
    return constant && f->constants[f->a[amount]].high == 0 && f->constants[f->a[amount]].low < ir_thread_width(f->type[i]);
}

// Whether instruction i may be in a threaded block's chain: pure, total and cheap, of scalar integers or pointers.
static bool ir_thread_chain_operation(const IrFunction* f, uint32_t i)
{
    const uint32_t op = f->op[i];
    const bool kind = op == IR_CONST || op == IR_COPY || op == IR_ZEXT || op == IR_SEXT || op == IR_TRUNC || op == IR_ICMP || op == IR_AND || op == IR_OR || op == IR_XOR || op == IR_NOT ||
                      ir_thread_constant_shift(f, i);
    const uint32_t mask = ir_value_mask(f, i);
    bool safe = kind && f->flags[i] == 0 && ir_thread_scalar_type(f->type[i]);
    safe = safe && ((mask & 1) == 0 || ir_thread_scalar_type(f->type[f->a[i]]));
    safe = safe && ((mask & 2) == 0 || ir_thread_scalar_type(f->type[f->b[i]]));
    return safe;
}

// Whether block J's shape is a candidate's: not the entry nor a label's, reachable, its phis, then its chain, then a
// conditional branch on a value of its own to two other blocks.
static bool ir_thread_shape(const IrThreadWork* work, uint32_t block)
{
    const IrFunction* f = work->function;
    const uint32_t first = f->block_offsets[block];
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    bool safe = block != 0 && work->reachable[block] != 0 && work->labeled[block] == 0 && f->op[last] == IR_CBR && f->op[first] == IR_PHI;
    uint32_t chain = first;
    while (safe && chain < last && f->op[chain] == IR_PHI)
    {
        chain += 1;
    }
    safe = safe && last - chain <= BUSTER_IR_THREAD_BLOCK_LIMIT;
    for (uint32_t i = chain; safe && i < last; i += 1)
    {
        safe = ir_thread_chain_operation(f, i);
    }
    const uint32_t condition = f->a[last];
    const uint32_t yes = f->b[last];
    const uint32_t no = f->c[last];
    safe = safe && condition < f->instruction_count && work->cfg.instruction_blocks[condition] == block && yes != no && yes != block && no != block;
    return safe;
}

// Whether `block` is entered only by the edge from its one predecessor: not the entry, which the function's start
// enters too, dominating every block (J among them), and which has no phis to merge in (#2608).
static bool ir_thread_alone(const IrCfg* cfg, uint32_t block)
{
    return block != 0 && cfg->predecessor_offsets[(uint64_t)block + 1] - cfg->predecessor_offsets[block] == 1;
}

// Which of repairable block J's successors that `possible` names (BUSTER_IR_THREAD_COVER_YES and _NO, of those J alone
// enters) dominates block `at` (#2608): a backward walk from `at` over predecessors that stops at them. J's phi
// dominates its reader, so J dominates `at` and the walk stays in J's region until it reaches J itself. Where it never
// does, every path from J to `at` passes a stop it met, and so does every path from the entry, which passes J: the one
// it met dominates `at`. None does where it reached J (through J's other successor: a path from the entry reaches J
// without passing a stop, J being their only predecessor), met both (successors J alone enters dominate subtrees
// apart), or would visit more than BUSTER_IR_THREAD_WALK_LIMIT blocks, which refuses the repair. The answer is the full
// one's within `possible`, so a use's walk takes only what the value's earlier uses left. A repair's readers are a few
// blocks past its successor (`tonumberns`'s number is read in the arithmetic past the test), so this bounded walk
// replaces the whole function's dominator tree that #2608's first form built in each round with a repairable
// candidate (1,153 of them on sqlite3.c, 160,053 blocks). Its queue is its visited set, padded with BUSTER_IR_NONE:
// whether it holds a block is one compare of each 16 entries.
static uint32_t ir_thread_cover(const IrThreadWork* work, uint32_t block, uint32_t at, uint32_t possible, uint32_t* walked)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    const uint32_t yes = (possible & BUSTER_IR_THREAD_COVER_YES) != 0 ? f->b[last] : BUSTER_IR_NONE;
    const uint32_t no = (possible & BUSTER_IR_THREAD_COVER_NO) != 0 ? f->c[last] : BUSTER_IR_NONE;
    _Alignas(64) uint32_t queue[BUSTER_IR_THREAD_WALK_LIMIT];
    for (uint32_t k = 0; k < BUSTER_IR_THREAD_WALK_LIMIT; k += 16)
    {
        _mm512_store_si512(queue + k, _mm512_set1_epi32(-1));
    }
    uint32_t met = at == yes ? BUSTER_IR_THREAD_COVER_YES : at == no ? BUSTER_IR_THREAD_COVER_NO : 0;
    queue[0] = at;
    uint32_t count = met == 0;
    bool reached = false;
    bool full = false;
    for (uint32_t next = 0; !reached && !full && next < count; next += 1)
    {
        const uint32_t from = queue[next];
        reached = from == block;
        for (uint32_t e = cfg->predecessor_offsets[from]; !reached && !full && e < cfg->predecessor_offsets[(uint64_t)from + 1]; e += 1)
        {
            const uint32_t predecessor = cfg->predecessors[e];
            const __m512i key = _mm512_set1_epi32((int)predecessor);
            uint32_t seen = 0;
            for (uint32_t k = 0; k < BUSTER_IR_THREAD_WALK_LIMIT; k += 16)
            {
                seen |= _mm512_cmpeq_epi32_mask(_mm512_load_si512(queue + k), key);
            }
            met |= predecessor == yes ? BUSTER_IR_THREAD_COVER_YES : predecessor == no ? BUSTER_IR_THREAD_COVER_NO : 0;
            const bool visit = predecessor != yes && predecessor != no && seen == 0;
            full = visit && count == BUSTER_IR_THREAD_WALK_LIMIT;
            if (visit && !full)
            {
                queue[count] = predecessor;
                count += 1;
            }
        }
    }
    *walked += count;
    return !reached && !full && (met == BUSTER_IR_THREAD_COVER_YES || met == BUSTER_IR_THREAD_COVER_NO) ? met : 0;
}

// A tracked block's value used at block `at`, outside it: the first scan's escape, or for the second (a repairable
// candidate's phi), the successor that dominates `at`, which then merges the phi again, or none (#2608). Each use may
// have its own: Lua's `lua_geti` reads the slot that `luaV_fastgeti`'s `?:` set in both successors. A value one of
// whose uses none dominates needs no further walk.
static void ir_thread_escape_use(IrThreadWork* work, uint32_t value, uint32_t at, uint32_t tracked)
{
    if (tracked == BUSTER_IR_THREAD_REPAIRABLE)
    {
        const uint32_t bits = work->escapes[value];
        const uint32_t possible = (bits & BUSTER_IR_THREAD_UNCOVERED) == 0 ? bits & BUSTER_IR_THREAD_COVERS : 0;
        const uint32_t cover = possible != 0 ? ir_thread_cover(work, work->cfg.instruction_blocks[value], at, possible, &work->walked) : 0;
        work->escapes[value] = (uint8_t)(bits | (cover != 0 ? cover << BUSTER_IR_THREAD_MERGE_SHIFT : BUSTER_IR_THREAD_UNCOVERED));
    }
    else
    {
        work->escapes[value] = BUSTER_IR_THREAD_ESCAPE;
    }
}

// The successors (as BUSTER_IR_THREAD_COVER_YES and _NO) that merge again a phi whose escapes byte is `bits`: those that
// dominate one of its uses, where each use had one; none for any other value (#2608).
static uint32_t ir_thread_merging(uint32_t bits)
{
    return (bits & BUSTER_IR_THREAD_UNCOVERED) == 0 ? (bits & BUSTER_IR_THREAD_MERGES) >> BUSTER_IR_THREAD_MERGE_SHIFT : 0;
}

// Each tracked block's values that are used where threading couldn't follow them: outside their block, but by a phi of
// a successor on the edge from it. One pass over every operand, read from its column directly. `tracked` selects the
// blocks by their candidates word: every candidate, or the repairable ones.
static void ir_thread_escape(IrThreadWork* work, uint32_t value, uint32_t at, uint32_t tracked)
{
    const uint32_t defined = work->cfg.instruction_blocks[value];
    if ((work->candidates[defined] & tracked) != 0 && defined != at)
    {
        ir_thread_escape_use(work, value, at, tracked);
    }
}

// Instruction i's operands outside its a, b and c columns: a phi's inputs, used at its predecessors' ends, a call's
// arguments and a ternary intrinsic's third operand.
static void ir_thread_escapes_extra(IrThreadWork* work, uint32_t i, uint32_t tracked)
{
    const IrFunction* f = work->function;
    const uint32_t block = work->cfg.instruction_blocks[i];
    const uint32_t op = f->op[i];
    if (op == IR_PHI)
    {
        for (uint32_t p = 0; p < f->b[i]; p += 1)
        {
            ir_thread_escape(work, f->extra[f->a[i] + p * 2 + 1], f->extra[f->a[i] + p * 2], tracked);
        }
    }
    else if (ir_extra_value_count(f, i) != 0)
    {
        for (uint32_t k = 0, extra_values = ir_extra_value_count(f, i); k < extra_values; k += 1)
        {
            ir_thread_escape(work, ir_extra_value(f, i, k), block, tracked);
        }
    }
    else if (op == IR_CALL || op == IR_CALL_INDIRECT)
    {
        const IrCall call = f->calls[f->b[i]];
        for (uint32_t k = 0; k < call.argument_count; k += 1)
        {
            ir_thread_escape(work, f->extra[call.argument_first + k], block, tracked);
        }
    }
}

// The escapes of 16 instructions at a time (#2554's measured cost: this scan was a third of the pass on speedtest1):
// each one's value mask is its opcode's operand bits, the optional ones where present (ir_value_mask's), by two 128-byte
// tables' lookups; each used operand's block is gathered, then whether that block is a tracked candidate's, and such a
// value used in another block is an escape, which ir_thread_escape_use records lane by lane (a few per function).
// Phis, calls, ternary intrinsics and scatters, whose other operands are in the extra array, take
// ir_thread_escapes_extra.
static void ir_thread_escapes(IrThreadWork* work, uint32_t tracked)
{
    const IrFunction* f = work->function;
    const uint32_t count = f->instruction_count;
    const uint32_t* const blocks = work->cfg.instruction_blocks;
    const uint32_t* const columns[3] = { f->a, f->b, f->c };
    const __m512i fixed_table_low = _mm512_loadu_si512(ir_operand_table);
    const __m512i fixed_table_high = _mm512_loadu_si512(ir_operand_table + 64);
    const __m512i optional_table_low = _mm512_loadu_si512(ir_optional_table);
    const __m512i optional_table_high = _mm512_loadu_si512(ir_optional_table + 64);
    for (uint64_t first = 0; first < count; first += 16)
    {
        const uint32_t lanes = count - first < 16 ? (uint32_t)(count - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << lanes) - 1);
        const __m128i op = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m512i fixed = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(fixed_table_low, _mm512_zextsi128_si512(op), fixed_table_high)));
        const __m512i optional = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(optional_table_low, _mm512_zextsi128_si512(op), optional_table_high)));
        const __m512i own = _mm512_maskz_loadu_epi32(valid, blocks + first);
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const __m512i bit = _mm512_set1_epi32((int)(1u << k));
            const __m512i operand = _mm512_maskz_loadu_epi32(valid, columns[k] + first);
            const __mmask16 present = _mm512_cmpneq_epi32_mask(operand, _mm512_set1_epi32(-1));
            const __mmask16 used = valid & (_mm512_test_epi32_mask(fixed, bit) | (_mm512_test_epi32_mask(optional, bit) & present));
            const __m512i defined = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), used, operand, blocks, 4);
            const __m512i candidate = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), used, defined, work->candidates, 4);
            uint32_t escape = _mm512_mask_cmpneq_epi32_mask(used, defined, own) & _mm512_test_epi32_mask(candidate, _mm512_set1_epi32((int)tracked));
            if (escape != 0)
            {
                uint32_t values[16];
                uint32_t at[16];
                _mm512_storeu_si512(values, operand);
                _mm512_storeu_si512(at, own);
                while (escape != 0)
                {
                    const uint32_t lane = (uint32_t)__builtin_ctz(escape);
                    ir_thread_escape_use(work, values[lane], at[lane], tracked);
                    escape &= escape - 1;
                }
            }
        }
        uint32_t extra = _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_PHI)) | _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_CALL)) |
                         _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_CALL_INDIRECT)) | _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_INTRINSIC)) |
                         _mm_mask_cmpeq_epi8_mask(valid, op, _mm_set1_epi8(IR_SCATTER));
        while (extra != 0)
        {
            ir_thread_escapes_extra(work, (uint32_t)first + (uint32_t)__builtin_ctz(extra), tracked);
            extra &= extra - 1;
        }
    }
}

// A br's only edge, or a cbr's true then false edge, as ir_cfg_validated writes them. Only these terminators
// request phi inputs during planning and rewriting. A switch or an indirect branch keeps its edge.
static uint32_t ir_thread_input_edge(const IrThreadWork* work, uint32_t predecessor, uint32_t block)
{
    const IrFunction* f = work->function;
    const uint32_t term = f->block_offsets[(uint64_t)predecessor + 1] - 1;
    const uint32_t edge = work->cfg.successor_offsets[predecessor];
    uint32_t result = BUSTER_IR_NONE;
    if ((f->op[term] == IR_BR && f->a[term] == block) || (f->op[term] == IR_CBR && f->b[term] == block))
    {
        result = edge;
    }
    else if (f->op[term] == IR_CBR && f->c[term] == block)
    {
        result = edge + 1;
    }
    return result;
}

// The first phi numbers a join's incoming edges in its own order. This costs no extra scratch: once reachability is
// known the pass only needs the CFG's predecessors and offsets. Each further phi gets a column in the same order.
static bool ir_thread_index(IrThreadWork* work, uint32_t block, Arena* scratch)
{
    const IrFunction* f = work->function;
    const uint32_t first = f->block_offsets[block];
    const uint32_t width = f->b[first];
    uint32_t phis = 1;
    while (f->op[first + phis] == IR_PHI)
    {
        phis += 1;
    }
    bool ok = true;
    if (phis > 1)
    {
        if (work->other_inputs == NULL)
        {
            work->other_inputs = ir_allocate(scratch, f->block_count, sizeof(*work->other_inputs), &ok);
        }
        if (ok)
        {
            work->other_inputs[block] = ir_allocate(scratch, (uint64_t)(phis - 1) * width, sizeof(uint32_t), &ok);
        }
    }
    if (ok)
    {
        if (work->input_positions == NULL)
        {
            work->input_positions = work->cfg.successors;
            work->cfg.successors = NULL;
        }
        for (uint32_t i = 0; i < phis; i += 1)
        {
            const uint32_t* pairs = f->extra + f->a[first + i];
            for (uint32_t k = 0; k < width; k += 1)
            {
                const uint32_t edge = ir_thread_input_edge(work, pairs[2 * k], block);
                if (edge != BUSTER_IR_NONE)
                {
                    if (i == 0)
                    {
                        work->input_positions[edge] = k;
                    }
                    else
                    {
                        work->other_inputs[block][(uint64_t)(i - 1) * width + work->input_positions[edge]] = pairs[2 * k + 1];
                    }
                }
            }
        }
        work->candidates[block] = BUSTER_IR_THREAD_INDEXED;
    }
    return ok;
}

// The phi's input from `predecessor`, or BUSTER_IR_NONE: its last entry for it, found from the end. The search stops
// there, a scalar walk of a phi's few entries. The select-every-entry loop it replaces, vectorized where it was inlined
// in ir_thread_block, failed MemorySanitizer's native row with an uninitialized value of no allocation's (an origin the
// compiler's vector code made) while every entry it read was set (#2554).
static uint32_t ir_thread_scan_incoming(const IrFunction* f, uint32_t phi, uint32_t predecessor)
{
    uint32_t value = BUSTER_IR_NONE;
    const uint32_t* entries = f->extra + f->a[phi];
    for (uint32_t p = f->b[phi]; p != 0 && value == BUSTER_IR_NONE; p -= 1)
    {
        if (entries[(p - 1) * 2] == predecessor)
        {
            value = entries[(p - 1) * 2 + 1];
        }
    }
    return value;
}

static uint32_t ir_thread_incoming(const IrThreadWork* work, uint32_t phi, uint32_t predecessor)
{
    const IrFunction* f = work->function;
    uint32_t input = BUSTER_IR_NONE;
    if (work->input_positions != NULL && f->b[phi] >= BUSTER_IR_THREAD_WIDE_PHI)
    {
        const uint32_t block = work->cfg.instruction_blocks[phi];
        if (work->candidates[block] == BUSTER_IR_THREAD_INDEXED)
        {
            const uint32_t edge = ir_thread_input_edge(work, predecessor, block);
            if (edge != BUSTER_IR_NONE)
            {
                const uint32_t position = work->input_positions[edge];
                const uint32_t column = phi - f->block_offsets[block];
                input = column == 0 ? f->extra[f->a[phi] + 2 * position + 1] : work->other_inputs[block][(uint64_t)(column - 1) * f->b[phi] + position];
            }
        }
    }
    return input != BUSTER_IR_NONE ? input : ir_thread_scan_incoming(f, phi, predecessor);
}

// `value`'s bits at `type`'s width.
static uint64_t ir_thread_bits(uint32_t type, uint64_t value)
{
    const uint32_t bits = type == IR_PTR ? 64 : ir_type_bits[type];
    return bits >= 64 ? value : value & ((UINT64_C(1) << bits) - 1);
}

// `value`, of `type`'s width, sign-extended to 64 bits.
static int64_t ir_thread_signed(uint32_t type, uint64_t value)
{
    const uint32_t bits = type == IR_PTR ? 64 : ir_type_bits[type];
    const uint64_t sign = bits >= 64 ? 0 : UINT64_C(1) << (bits - 1);
    return bits >= 64 ? (int64_t)value : (int64_t)((ir_thread_bits(type, value) ^ sign) - sign);
}

// Block J's branch along its predecessor's edge where the predecessor's phi inputs make it constant: its successor then,
// or BUSTER_IR_NONE. `values` and `known` have a slot per instruction of J.
static uint32_t ir_thread_evaluate(const IrThreadWork* work, uint32_t block, uint32_t predecessor, uint64_t* values, uint8_t* known)
{
    const IrFunction* f = work->function;
    const uint32_t first = f->block_offsets[block];
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    for (uint32_t i = first; i < last; i += 1)
    {
        const uint32_t op = f->op[i];
        const uint32_t type = f->type[i];
        uint64_t operands[2] = { 0 };
        bool all = true;
        uint32_t source = op == IR_PHI ? ir_thread_incoming(work, i, predecessor) : BUSTER_IR_NONE;
        const uint32_t mask = op == IR_PHI ? 0 : ir_value_mask(f, i);
        for (uint32_t k = 0; k < 2; k += 1)
        {
            const uint32_t operand = op == IR_PHI ? (k == 0 ? source : BUSTER_IR_NONE) : ((mask >> k) & 1) != 0 ? (k == 0 ? f->a[i] : f->b[i]) : BUSTER_IR_NONE;
            if (operand != BUSTER_IR_NONE)
            {
                const bool inside = operand >= first && operand < last;
                const bool constant = !inside && f->op[operand] == IR_CONST;
                all = all && ((inside && known[operand - first] != 0) || constant);
                operands[k] = inside && known[operand - first] != 0 ? values[operand - first] : constant ? f->constants[f->a[operand]].low : 0;
            }
            else if (op == IR_PHI && k == 0)
            {
                all = false;
            }
        }
        uint64_t result = 0;
        switch (op)
        {
        case IR_PHI:
        case IR_COPY:
            result = operands[0];
            break;
        case IR_CONST:
            result = f->constants[f->a[i]].low;
            break;
        case IR_ZEXT:
        case IR_TRUNC:
            result = ir_thread_bits(f->type[f->a[i]], operands[0]);
            break;
        case IR_SEXT:
            result = (uint64_t)ir_thread_signed(f->type[f->a[i]], operands[0]);
            break;
        case IR_AND:
            result = operands[0] & operands[1];
            break;
        case IR_OR:
            result = operands[0] | operands[1];
            break;
        case IR_XOR:
            result = operands[0] ^ operands[1];
            break;
        case IR_NOT:
            result = ~operands[0];
            break;
        case IR_SHL:
        case IR_LSHR:
        case IR_ASHR:
        {
            // ir_thread_chain_operation admitted only a constant amount below the width.
            const uint32_t amount = (uint32_t)operands[1] & 63u;
            result = op == IR_SHL ? operands[0] << amount : op == IR_LSHR ? ir_thread_bits(type, operands[0]) >> amount : (uint64_t)(ir_thread_signed(type, operands[0]) >> amount);
        } break;
        case IR_ICMP:
        {
            const uint32_t operand_type = f->type[f->a[i]];
            const uint64_t left = ir_thread_bits(operand_type, operands[0]);
            const uint64_t right = ir_thread_bits(operand_type, operands[1]);
            const int64_t signed_left = ir_thread_signed(operand_type, operands[0]);
            const int64_t signed_right = ir_thread_signed(operand_type, operands[1]);
            switch (f->c[i])
            {
            case IR_EQ: result = left == right; break;
            case IR_NE: result = left != right; break;
            case IR_LT: result = signed_left < signed_right; break;
            case IR_LE: result = signed_left <= signed_right; break;
            case IR_GT: result = signed_left > signed_right; break;
            case IR_GE: result = signed_left >= signed_right; break;
            case IR_ULT: result = left < right; break;
            case IR_ULE: result = left <= right; break;
            case IR_UGT: result = left > right; break;
            case IR_UGE: result = left >= right; break;
            default: all = false; break;
            }
        } break;
        default:
            all = false;
            break;
        }
        known[i - first] = all;
        values[i - first] = all ? ir_thread_bits(type, result) : 0;
    }
    const uint32_t condition = f->a[last];
    const bool decided = known[condition - first] != 0;
    const uint32_t result = !decided ? BUSTER_IR_NONE : (values[condition - first] & 1) != 0 ? f->b[last] : f->c[last];
    return result;
}

// The value J's successors' phis take along `predecessor`'s edge for `value`, which they took from J: a J phi's input
// from it, or `value` itself where J doesn't define it. BUSTER_IR_NONE for a value of J's chain (only a copy has it).
static uint32_t ir_thread_passed(const IrThreadWork* work, uint32_t block, uint32_t predecessor, uint32_t value)
{
    const IrFunction* f = work->function;
    const uint32_t first = f->block_offsets[block];
    const uint32_t end = f->block_offsets[(uint64_t)block + 1];
    const bool inside = value >= first && value < end;
    const uint32_t result = !inside ? value : f->op[value] == IR_PHI ? ir_thread_incoming(work, value, predecessor) : BUSTER_IR_NONE;
    return result;
}

// Whether every phi of `target` takes the same value from `predecessor` directly as through `block` (a collapse's two
// edges): a phi's inputs from the predecessor and through J must be one value.
static bool ir_thread_agrees(const IrThreadWork* work, uint32_t target, uint32_t block, uint32_t predecessor)
{
    const IrFunction* f = work->function;
    bool agrees = true;
    for (uint32_t i = f->block_offsets[target]; agrees && f->op[i] == IR_PHI; i += 1)
    {
        const uint32_t direct = ir_thread_incoming(work, i, predecessor);
        const uint32_t through = ir_thread_passed(work, block, predecessor, ir_thread_incoming(work, i, block));
        agrees = direct != BUSTER_IR_NONE && direct == through;
    }
    return agrees;
}

// Whether every phi of `target` can take a value from `predecessor` through `block` without a copy of J's chain.
static bool ir_thread_redirectable(const IrThreadWork* work, uint32_t target, uint32_t block, uint32_t predecessor)
{
    const IrFunction* f = work->function;
    bool redirectable = true;
    for (uint32_t i = f->block_offsets[target]; redirectable && f->op[i] == IR_PHI; i += 1)
    {
        redirectable = ir_thread_passed(work, block, predecessor, ir_thread_incoming(work, i, block)) != BUSTER_IR_NONE;
    }
    return redirectable;
}

// Whether a repair of candidate J pays (#2608): its test reads none of the phis read past J, and is a constant on the
// edge of the first constant input of the first phi it reads, or that phi takes an extended comparison on some edge: a
// truth value, whose copy's test reads the setcc in its own block (Lua's `op_orderI` compares `cond` with the
// instruction's k bit, which no edge decides). Lua's `tonumberns` is a `?:` whose join's truth value is
// 1 or 0 on every edge and whose number is read past the test, which reads the truth value alone. A data select's join
// is no truth value's: `y = x < lo ? lo : x; y > hi ? hi : y` is decided on no edge and takes no comparison, and a
// clamp to a constant (`y > 255`), decided on the edge of y's 0, reads y. If-conversion makes either a select, which threading would undo
// for a test on every other path. The test's operands are marked in `known` back to J's phis (only phis of J escape
// here, ir_thread_repairable having refused a chain value read past J), a walk of at most
// BUSTER_IR_THREAD_BLOCK_LIMIT operations. Whether J's evaluation knows the test along an edge depends only on which of
// the phis it reads are constants there (an operation is known where its operands are), so J is evaluated along one
// edge, found by a scan of one phi's inputs: it decides the test wherever the test reads that phi alone.
static bool ir_thread_decides(const IrThreadWork* work, uint32_t block, uint64_t* values, uint8_t* known)
{
    const IrFunction* f = work->function;
    const uint32_t first = f->block_offsets[block];
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    memset(known, 0, last - first);
    known[f->a[last] - first] = 1;
    bool escaping = false;
    uint32_t tested = BUSTER_IR_NONE;
    for (uint32_t i = last; i > first; i -= 1)
    {
        const uint32_t at = i - 1;
        const bool read = known[at - first] != 0;
        const uint32_t mask = read && f->op[at] != IR_PHI ? ir_value_mask(f, at) : 0;
        const uint32_t operands[2] = { f->a[at], f->b[at] };
        for (uint32_t k = 0; k < 2; k += 1)
        {
            if (((mask >> k) & 1) != 0 && operands[k] >= first && operands[k] < last)
            {
                known[operands[k] - first] = 1;
            }
        }
        escaping = escaping || (read && work->escapes[at] != 0);
        tested = read && f->op[at] == IR_PHI ? at : tested;
    }
    uint32_t source = BUSTER_IR_NONE;
    bool extended = false;
    const uint32_t* entries = tested != BUSTER_IR_NONE ? f->extra + f->a[tested] : NULL;
    for (uint32_t p = 0; entries != NULL && p < f->b[tested]; p += 1)
    {
        const uint32_t input = entries[2 * p + 1];
        source = source == BUSTER_IR_NONE && f->op[input] == IR_CONST ? entries[2 * p] : source;
        extended = extended || (f->op[input] == IR_ZEXT && f->type[f->a[input]] == IR_I1);
    }
    return !escaping && (extended || (source != BUSTER_IR_NONE && ir_thread_evaluate(work, block, source, values, known) != BUSTER_IR_NONE));
}

// The candidates a repair may follow (#2608): those whose values used past them are all phis, with a successor of which
// each is the only predecessor (else no repair could follow it, and the walks would be for nothing), that don't look
// like a loop's header, and that a repair pays for (ir_thread_decides). A loop's header is left as it was: threading it
// would rotate the loop, which this pass doesn't set out to do. Its test is the cheap one of block order: a
// predecessor past J. A header laid out after its back edges' blocks isn't seen, and threading it is still exact (the
// successor's merge takes the back edges' values, the rotated loop's). A phi input from a block laid out past J
// doesn't make J a header: each of luaV_execute's handlers takes `trap` from its dispatch, laid out last, which
// ir_thread_backward's test refused (#2608's setcc form). A candidate's escapes and phis, then its
// predecessors, are read 64 and 16 at a time as masks, and only a candidate they pass is evaluated. Whether any
// candidate is repairable.
static bool ir_thread_repairable(IrThreadWork* work, uint64_t* values, uint8_t* known)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    bool any = false;
    for (uint32_t block = 1; block < f->block_count; block += 1)
    {
        if (work->candidates[block] != 0)
        {
            const uint32_t first = f->block_offsets[block];
            const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
            const uint32_t yes = f->b[last];
            const uint32_t no = f->c[last];
            const bool alone = ir_thread_alone(cfg, yes) || ir_thread_alone(cfg, no);
            bool escapes = false;
            bool chain = false;
            for (uint32_t at = first; at < last; at += 64)
            {
                const uint32_t lanes = last - at < 64 ? last - at : 64;
                const __mmask64 valid = UINT64_MAX >> (64 - lanes);
                const __mmask64 escaping = _mm512_mask_test_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, work->escapes + at), _mm512_set1_epi8(BUSTER_IR_THREAD_ESCAPE));
                const __mmask64 phi = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, f->op + at), _mm512_set1_epi8(IR_PHI));
                escapes = escapes || escaping != 0;
                chain = chain || (escaping & ~phi) != 0;
            }
            bool later = false;
            const uint32_t begin = cfg->predecessor_offsets[block];
            const uint32_t end = cfg->predecessor_offsets[(uint64_t)block + 1];
            for (uint32_t e = begin; escapes && !chain && alone && e < end; e += 16)
            {
                const __mmask16 valid = (__mmask16)((UINT32_C(1) << (end - e < 16 ? end - e : 16)) - 1);
                const __m512i predecessors = _mm512_maskz_loadu_epi32(valid, cfg->predecessors + e);
                later = later || _mm512_mask_cmpgt_epu32_mask(valid, predecessors, _mm512_set1_epi32((int)block)) != 0;
            }
            const bool header = later;
            const bool repairable = escapes && !chain && alone && !header && ir_thread_decides(work, block, values, known);
            work->candidates[block] |= repairable ? BUSTER_IR_THREAD_REPAIRABLE : 0;
            any = any || repairable;
        }
    }
    return any;
}

// The second scan over the repairable candidates' phis' uses (#2608): each one's escaping phis take the cover bits of
// its successors of which it is the only predecessor, 64 escapes bytes at a time, which the scan's walks narrow to the
// one that dominates every use.
static void ir_thread_repairs(IrThreadWork* work)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    for (uint32_t block = 1; block < f->block_count; block += 1)
    {
        const bool repairable = (work->candidates[block] & BUSTER_IR_THREAD_REPAIRABLE) != 0;
        const uint32_t first = f->block_offsets[block];
        const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
        const uint32_t yes = f->b[last];
        const uint32_t no = f->c[last];
        const bool yes_alone = repairable && ir_thread_alone(cfg, yes);
        const bool no_alone = repairable && ir_thread_alone(cfg, no);
        const uint32_t covers = (yes_alone ? BUSTER_IR_THREAD_COVER_YES : 0) | (no_alone ? BUSTER_IR_THREAD_COVER_NO : 0);
        for (uint32_t at = first; repairable && at < last; at += 64)
        {
            const uint32_t lanes = last - at < 64 ? last - at : 64;
            const __mmask64 valid = UINT64_MAX >> (64 - lanes);
            const __mmask64 escaping = _mm512_mask_test_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, work->escapes + at), _mm512_set1_epi8(BUSTER_IR_THREAD_ESCAPE));
            _mm512_mask_storeu_epi8(work->escapes + at, escaping, _mm512_set1_epi8((char)(BUSTER_IR_THREAD_ESCAPE | covers)));
        }
    }
    ir_thread_escapes(work, BUSTER_IR_THREAD_REPAIRABLE);
}

// Thread block J where its predecessors allow it: their actions, and the roles that keep a round's blocks apart.
static bool ir_thread_block(IrThreadWork* work, uint32_t block, uint64_t* values, uint8_t* known, Arena* scratch)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const uint32_t first = f->block_offsets[block];
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    const uint32_t yes = f->b[last];
    const uint32_t no = f->c[last];
    const bool apart = work->roles[block] == 0 && (work->roles[yes] & BUSTER_IR_THREAD_THREADED) == 0 && (work->roles[no] & BUSTER_IR_THREAD_THREADED) == 0;
    work->deferred += !apart;
    bool safe = apart;
    // A value used past J must be a phi that the successors dominating its uses merge again (#2608): every use had one in
    // the second scan, a merge each.
    uint32_t merged = 0;
    uint32_t merging = 0;
    for (uint32_t i = first; safe && i < last; i += 1)
    {
        const uint32_t sides = ir_thread_merging(work->escapes[i]);
        safe = work->escapes[i] == 0 || sides != 0;
        merged += (uint32_t)__builtin_popcount(sides);
        merging |= sides;
    }
    // A repairable J some of whose phis' readers no successor dominates yet: another block's threading in this round
    // may leave its successor their only way in (the `&&` join of Lua's two `tonumberns`, threaded first).
    work->deferred += apart && !safe && (work->candidates[block] & BUSTER_IR_THREAD_REPAIRABLE) != 0;
    // What a copy needs: the branch's condition and the successors' phis' inputs from J, and their operands in J, back to
    // its phis; at most BUSTER_IR_THREAD_CHAIN_LIMIT operations besides constants, which are immediates (Lua's
    // `cond != GETARG_k(i)` is a shift, a mask and a comparison, with two constants).
    const uint32_t condition = f->a[last];
    work->needed[condition] = condition >= first && condition < last;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        const uint32_t successor = k == 0 ? yes : no;
        for (uint32_t i = f->block_offsets[successor]; f->op[i] == IR_PHI; i += 1)
        {
            const uint32_t value = ir_thread_incoming(work, i, block);
            if (value >= first && value < last)
            {
                work->needed[value] = 1;
            }
        }
    }
    uint32_t chain = 0;
    uint32_t operations = 0;
    for (uint32_t i = last; i > first; i -= 1)
    {
        const uint32_t at = i - 1;
        if (work->needed[at] != 0 && f->op[at] != IR_PHI)
        {
            chain += 1;
            operations += f->op[at] != IR_CONST;
            const uint32_t mask = ir_value_mask(f, at);
            const uint32_t operands[2] = { f->a[at], f->b[at] };
            for (uint32_t k = 0; k < 2; k += 1)
            {
                if (((mask >> k) & 1) != 0 && operands[k] >= first && operands[k] < last)
                {
                    work->needed[operands[k]] = 1;
                }
            }
        }
    }
    safe = safe && operations <= BUSTER_IR_THREAD_CHAIN_LIMIT;
    uint32_t threaded = 0;
    uint32_t copies = 0;
    // The reachable predecessors, all of which may be threaded: one that no edge from the entry reaches keeps its edge.
    uint32_t predecessors = 0;
    uint32_t conditionals = 0;
    const bool wide = safe && f->b[first] >= BUSTER_IR_THREAD_WIDE_PHI;
    for (uint32_t e = cfg->predecessor_offsets[block]; e < cfg->predecessor_offsets[(uint64_t)block + 1]; e += 1)
    {
        const uint32_t p = cfg->predecessors[e];
        predecessors += work->reachable[p] != 0;
        if (wide && conditionals < 2 && work->reachable[p] != 0 && p != block && (work->roles[p] & (BUSTER_IR_THREAD_THREADED | BUSTER_IR_THREAD_PREDECESSOR)) == 0)
        {
            const uint32_t term = f->block_offsets[(uint64_t)p + 1] - 1;
            conditionals += f->op[term] == IR_CBR && f->b[term] != f->c[term];
        }
    }
    const bool ok = conditionals < 2 || ir_thread_index(work, block, scratch);
    // The successors (as BUSTER_IR_THREAD_COVER_YES and _NO) that a threaded predecessor reaches.
    uint32_t reaching = 0;
    for (uint32_t e = cfg->predecessor_offsets[block]; ok && safe && e < cfg->predecessor_offsets[(uint64_t)block + 1]; e += 1)
    {
        const uint32_t predecessor = cfg->predecessors[e];
        const uint32_t terminator = f->block_offsets[(uint64_t)predecessor + 1] - 1;
        const uint32_t op = f->op[terminator];
        const bool taken = (work->roles[predecessor] & (BUSTER_IR_THREAD_THREADED | BUSTER_IR_THREAD_PREDECESSOR)) != 0;
        const bool free = work->reachable[predecessor] != 0 && predecessor != block && !taken;
        work->deferred += work->reachable[predecessor] != 0 && taken;
        if (free && op == IR_BR && copies < BUSTER_IR_THREAD_COPY_LIMIT)
        {
            work->actions[predecessor] = BUSTER_IR_THREAD_COPY;
            work->threaded[predecessor] = block;
            work->targets[predecessor] = BUSTER_IR_NONE;
            copies += 1;
            threaded += 1;
            reaching |= BUSTER_IR_THREAD_COVERS;
        }
        else if (free && op == IR_CBR && (f->b[terminator] == block) != (f->c[terminator] == block))
        {
            const uint32_t target = ir_thread_evaluate(work, block, predecessor, values, known);
            const uint32_t other = f->b[terminator] == block ? f->c[terminator] : f->b[terminator];
            const bool collapse = target == other;
            const bool valid = target != BUSTER_IR_NONE && (collapse ? ir_thread_agrees(work, target, block, predecessor) : ir_thread_redirectable(work, target, block, predecessor));
            if (valid)
            {
                work->actions[predecessor] = collapse ? BUSTER_IR_THREAD_COLLAPSE : BUSTER_IR_THREAD_REDIRECT;
                work->threaded[predecessor] = block;
                work->targets[predecessor] = target;
                threaded += 1;
                reaching |= target == yes ? BUSTER_IR_THREAD_COVER_YES : BUSTER_IR_THREAD_COVER_NO;
            }
        }
    }
    // A successor that only J enters keeps a predecessor where it has phis or merges one of J's again: where every
    // predecessor is threaded, J is left an `unreachable`, and redirects that all chose its other successor would leave
    // it none, an empty phi. J's test is then the same constant on every edge, SCCP's to fold (Lua's checktab).
    const bool yes_alone = ir_thread_alone(cfg, yes);
    const bool no_alone = ir_thread_alone(cfg, no);
    const uint32_t needing = merging | (yes_alone && f->op[f->block_offsets[yes]] == IR_PHI ? BUSTER_IR_THREAD_COVER_YES : 0) |
                             (no_alone && f->op[f->block_offsets[no]] == IR_PHI ? BUSTER_IR_THREAD_COVER_NO : 0);
    safe = safe && (threaded != predecessors || (needing & ~reaching) == 0);
    if (safe && threaded != 0)
    {
        // A copy is the needed chain's operations, the phis read as the predecessor's inputs, and the branch in its
        // terminator's place.
        work->roles[block] |= BUSTER_IR_THREAD_THREADED;
        work->roles[yes] |= BUSTER_IR_THREAD_SUCCESSOR;
        work->roles[no] |= BUSTER_IR_THREAD_SUCCESSOR;
        work->remaining[block] = predecessors - threaded;
        work->block_count += 1;
        work->merged += merged;
        for (uint32_t e = cfg->predecessor_offsets[block]; e < cfg->predecessor_offsets[(uint64_t)block + 1]; e += 1)
        {
            const uint32_t predecessor = cfg->predecessors[e];
            if (work->actions[predecessor] != BUSTER_IR_THREAD_KEEP && work->threaded[predecessor] == block)
            {
                work->roles[predecessor] |= BUSTER_IR_THREAD_PREDECESSOR;
                work->copy_count += work->actions[predecessor] == BUSTER_IR_THREAD_COPY;
                work->redirect_count += work->actions[predecessor] != BUSTER_IR_THREAD_COPY;
                work->cloned += work->actions[predecessor] == BUSTER_IR_THREAD_COPY ? chain : 0;
            }
        }
    }
    else
    {
        // Undo this block's provisional actions and what its copies would have needed.
        memset(work->needed + first, 0, last - first);
        for (uint32_t e = cfg->predecessor_offsets[block]; e < cfg->predecessor_offsets[(uint64_t)block + 1]; e += 1)
        {
            const uint32_t predecessor = cfg->predecessors[e];
            if (work->actions[predecessor] != BUSTER_IR_THREAD_KEEP && work->threaded[predecessor] == block && (work->roles[predecessor] & BUSTER_IR_THREAD_PREDECESSOR) == 0)
            {
                work->actions[predecessor] = BUSTER_IR_THREAD_KEEP;
                work->threaded[predecessor] = BUSTER_IR_NONE;
            }
        }
    }
    return ok;
}

// The index among a copy's operations of `value`, an operation of threaded block J (`first` its first instruction): the
// needed operations before it.
static uint32_t ir_thread_position(const IrThreadWork* work, uint32_t first, uint32_t value)
{
    const IrFunction* f = work->function;
    uint32_t position = 0;
    for (uint32_t j = first; j < value; j += 1)
    {
        position += f->op[j] != IR_PHI && work->needed[j] != 0;
    }
    return position;
}

// Whether `block` was threaded past by every predecessor that reaches it: the rewrite leaves it an `unreachable` alone,
// whose successors' phis drop its inputs, so that no unreachable block uses another's values (ir_verify_complete's rule).
static bool ir_thread_dead(const IrThreadWork* work, uint32_t block)
{
    return (work->roles[block] & BUSTER_IR_THREAD_THREADED) != 0 && work->remaining[block] == 0;
}

// Which of phi i's merges `successor` makes, where i is read past its candidate (#2608): 0 for the true successor's, 1 for
// the false one's, BUSTER_IR_NONE where it makes none. It does where the candidate was threaded (ir_thread_merged_from).
static uint32_t ir_thread_merger(const IrThreadWork* work, uint32_t i, uint32_t successor)
{
    const IrFunction* f = work->function;
    const uint32_t block = work->cfg.instruction_blocks[i];
    const uint32_t sides = ir_thread_merging(work->escapes[i]);
    const uint32_t last = f->block_offsets[(uint64_t)block + 1] - 1;
    const bool yes = (sides & BUSTER_IR_THREAD_COVER_YES) != 0 && f->b[last] == successor;
    const bool no = (sides & BUSTER_IR_THREAD_COVER_NO) != 0 && f->c[last] == successor;
    return yes ? 0 : no ? 1 : BUSTER_IR_NONE;
}

// The threaded block whose phis `block` may merge again: its only predecessor, where that was threaded; else
// BUSTER_IR_NONE.
static uint32_t ir_thread_merged_from(const IrThreadWork* work, uint32_t block)
{
    const IrCfg* cfg = &work->cfg;
    const uint32_t from = ir_thread_alone(cfg, block) ? cfg->predecessors[cfg->predecessor_offsets[block]] : BUSTER_IR_NONE;
    return from != BUSTER_IR_NONE && (work->roles[from] & BUSTER_IR_THREAD_THREADED) != 0 ? from : BUSTER_IR_NONE;
}

// `value`'s new index where it is read at block `at` (an instruction's own block, or a phi entry's predecessor): a
// repaired phi's merge outside the phi's own block, that of the successor that dominates `at` where both merge it,
// which a walk as the second escape scan's finds again; its own new index elsewhere (#2608). `merges` is NULL in a
// round without repairs, else two slots a value, the true successor's merge and the false one's.
static uint32_t ir_thread_read(const IrThreadWork* work, const uint32_t* values, const uint32_t* merges, uint32_t value, uint32_t at)
{
    const uint32_t block = merges != NULL ? work->cfg.instruction_blocks[value] : BUSTER_IR_NONE;
    const bool outside = merges != NULL && block != at;
    const uint32_t yes = outside ? merges[(uint64_t)value * 2] : BUSTER_IR_NONE;
    const uint32_t no = outside ? merges[(uint64_t)value * 2 + 1] : BUSTER_IR_NONE;
    uint32_t walked = 0;
    const uint32_t side = yes != BUSTER_IR_NONE && no != BUSTER_IR_NONE ? ir_thread_cover(work, block, at, BUSTER_IR_THREAD_COVERS, &walked) : 0;
    const uint32_t merge = side == BUSTER_IR_THREAD_COVER_NO || yes == BUSTER_IR_NONE ? no : yes;
    return merge != BUSTER_IR_NONE ? merge : values[value];
}

// The phis a successor of a threaded block gains: its threaded predecessors' additions, grouped by successor
// (`offsets`, a CSR of `blocks + 1` entries, and `entries`, the predecessors that now reach it).
static void ir_thread_additions(const IrThreadWork* work, uint32_t* offsets, uint32_t* entries)
{
    const IrFunction* f = work->function;
    const uint32_t blocks = f->block_count;
    memset(offsets, 0, ((uint64_t)blocks + 1) * sizeof(uint32_t));
    for (uint32_t pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t p = 0; p < blocks; p += 1)
        {
            const uint32_t action = work->actions[p];
            const uint32_t block = work->threaded[p];
            const uint32_t last = action != BUSTER_IR_THREAD_KEEP ? f->block_offsets[(uint64_t)block + 1] - 1 : 0;
            const uint32_t gains[2] = {
                action == BUSTER_IR_THREAD_COPY ? f->b[last] : action == BUSTER_IR_THREAD_REDIRECT ? work->targets[p] : BUSTER_IR_NONE,
                action == BUSTER_IR_THREAD_COPY ? f->c[last] : BUSTER_IR_NONE,
            };
            for (uint32_t g = 0; g < 2; g += 1)
            {
                if (gains[g] != BUSTER_IR_NONE && pass == 0)
                {
                    offsets[gains[g] + 1] += 1;
                }
                else if (gains[g] != BUSTER_IR_NONE)
                {
                    entries[offsets[gains[g]]++] = p;
                }
            }
        }
        for (uint32_t b = 0; pass == 0 && b < blocks; b += 1)
        {
            offsets[b + 1] += offsets[b];
        }
        // The second pass's increments moved each start to the next one's: shift them back.
        for (uint32_t b = blocks; pass == 1 && b > 0; b -= 1)
        {
            offsets[b] = offsets[b - 1];
        }
        if (pass == 1)
        {
            offsets[0] = 0;
        }
    }
}

// The round's rewrite: copies before their predecessors' new branches, redirected edges, the successors' phis' new inputs
// and the threaded blocks' phis without the threaded predecessors' (undef where none is left).
static IrError ir_thread_rewrite(const IrThreadWork* work, Arena* output, Arena* scratch, IrFunction* result)
{
    const IrFunction* const f = work->function;
    const uint64_t mark = output->length;
    const uint32_t count = f->instruction_count;
    const uint32_t blocks = f->block_count;
    // A dead threaded block keeps its terminator's place alone (ir_thread_dead).
    uint64_t removed = 0;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        removed += ir_thread_dead(work, block) ? f->block_offsets[(uint64_t)block + 1] - f->block_offsets[block] - 1 : 0;
    }
    const uint64_t total = (uint64_t)count + work->cloned - removed + work->merged;
    bool ok = total <= UINT32_MAX - 1;
    uint32_t* values = ir_allocate(scratch, count, 4, &ok);
    // Each repaired phi's merge's new index (#2608), in a round that has one.
    uint32_t* merges = work->merged != 0 ? ir_allocate(scratch, (uint64_t)count * 2, 4, &ok) : NULL;
    // Each copying predecessor's first copied instruction's new index.
    uint32_t* bases = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* offsets = ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok);
    uint32_t* entries = ir_allocate(scratch, (uint64_t)work->copy_count * 2 + work->redirect_count + 1, 4, &ok);
    IrFunction view = *f;
    view.instruction_count = (uint32_t)total;
    view.op = ir_allocate(output, total, 1, &ok);
    view.type = ir_allocate(output, total, 1, &ok);
    view.flags = ir_allocate(output, total, 1, &ok);
    view.alignment_log2 = ir_allocate(output, total, 1, &ok);
    view.a = ir_allocate(output, total, 4, &ok);
    view.b = ir_allocate(output, total, 4, &ok);
    view.c = ir_allocate(output, total, 4, &ok);
    view.block_offsets = ir_allocate(output, (uint64_t)blocks + 1, 4, &ok);
    IrError error = ok ? IR_OK : total > UINT32_MAX - 1 ? IR_ERROR_LIMIT : IR_ERROR_MEMORY;
    if (error == IR_OK)
    {
        ir_thread_additions(work, offsets, entries);
    }
    uint64_t extras = f->extra_count;
    for (uint32_t i = 0; error == IR_OK && i < count; i += 1)
    {
        const uint32_t block = work->cfg.instruction_blocks[i];
        if (f->op[i] == IR_PHI)
        {
            extras += ((uint64_t)f->b[i] + offsets[block + 1] - offsets[block]) * 2;
        }
        else if (f->op[i] == IR_CALL || f->op[i] == IR_CALL_INDIRECT)
        {
            extras += f->calls[f->b[i]].argument_count;
        }
        else if (ir_extra_value_count(f, i) != 0)
        {
            extras += ir_extra_value_count(f, i);
        }
    }
    // A repaired phi's merge (#2608): an entry from each predecessor that now reaches its successor, and from its block
    // where that stays.
    for (uint32_t block = 0; error == IR_OK && merges != NULL && block < blocks; block += 1)
    {
        const uint32_t from = ir_thread_merged_from(work, block);
        for (uint32_t i = from != BUSTER_IR_NONE ? f->block_offsets[from] : 0; from != BUSTER_IR_NONE && f->op[i] == IR_PHI; i += 1)
        {
            extras += ir_thread_merger(work, i, block) != BUSTER_IR_NONE ? ((uint64_t)offsets[block + 1] - offsets[block] + !ir_thread_dead(work, from)) * 2 : 0;
        }
    }
    error = error == IR_OK && extras > UINT32_MAX ? IR_ERROR_LIMIT : error;
    if (error == IR_OK)
    {
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
        view.extra_count = (uint32_t)extras;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        // The new indices: each block's instructions, a copying predecessor's copy before its terminator, and a successor's
        // merges of its threaded predecessor's phis before its own (#2608).
        if (merges != NULL)
        {
            memset(merges, 0xff, (uint64_t)count * 8);
        }
        uint32_t next = 0;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            view.block_offsets[block] = next;
            const uint32_t from = merges != NULL ? ir_thread_merged_from(work, block) : BUSTER_IR_NONE;
            for (uint32_t i = from != BUSTER_IR_NONE ? f->block_offsets[from] : 0; from != BUSTER_IR_NONE && f->op[i] == IR_PHI; i += 1)
            {
                const uint32_t side = ir_thread_merger(work, i, block);
                if (side != BUSTER_IR_NONE)
                {
                    merges[(uint64_t)i * 2 + side] = next;
                    next += 1;
                }
            }
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            const bool dead = ir_thread_dead(work, block);
            for (uint32_t i = f->block_offsets[block]; i < end; i += 1)
            {
                if (dead && i + 1 != end)
                {
                    values[i] = BUSTER_IR_NONE;
                    continue;
                }
                if (i + 1 == end && work->actions[block] == BUSTER_IR_THREAD_COPY)
                {
                    const uint32_t threaded = work->threaded[block];
                    const uint32_t chain_end = f->block_offsets[(uint64_t)threaded + 1] - 1;
                    bases[block] = next;
                    next += ir_thread_position(work, f->block_offsets[threaded], chain_end);
                }
                values[i] = next;
                next += 1;
            }
        }
        view.block_offsets[blocks] = next;
        ir_debug_remap(f, &view, values, NULL, NULL);
        // Never remap extra in place (ir_if_convert's rule): keep its slices, and append each phi's, call's and ternary's.
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
        }
        if (f->call_count != 0)
        {
            memcpy(view.calls, f->calls, (uint64_t)f->call_count * sizeof(IrCall));
        }
        uint32_t extra = f->extra_count;
        for (uint32_t block = 0; block < blocks; block += 1)
        {
            const uint32_t end = f->block_offsets[(uint64_t)block + 1];
            const bool dead = ir_thread_dead(work, block);
            // A repaired phi's merge (#2608): the phi from its block where that stays, then each predecessor's input to it,
            // read at that predecessor's end.
            const uint32_t from = merges != NULL ? ir_thread_merged_from(work, block) : BUSTER_IR_NONE;
            for (uint32_t i = from != BUSTER_IR_NONE ? f->block_offsets[from] : 0; from != BUSTER_IR_NONE && f->op[i] == IR_PHI; i += 1)
            {
                const uint32_t side = ir_thread_merger(work, i, block);
                if (side != BUSTER_IR_NONE)
                {
                    const uint32_t out = merges[(uint64_t)i * 2 + side];
                    view.op[out] = IR_PHI;
                    view.type[out] = f->type[i];
                    view.flags[out] = f->flags[i];
                    view.alignment_log2[out] = f->alignment_log2[i];
                    view.a[out] = extra;
                    view.b[out] = 0;
                    view.c[out] = f->c[i];
                    if (!ir_thread_dead(work, from))
                    {
                        view.extra[extra++] = from;
                        view.extra[extra++] = values[i];
                        view.b[out] += 1;
                    }
                    for (uint32_t k = offsets[block]; k < offsets[block + 1]; k += 1)
                    {
                        const uint32_t predecessor = entries[k];
                        view.extra[extra++] = predecessor;
                        view.extra[extra++] = ir_thread_read(work, values, merges, ir_thread_incoming(work, i, predecessor), predecessor);
                        view.b[out] += 1;
                    }
                    if (view.debug != NULL)
                    {
                        view.debug->origins[out] = f->debug->origins[i];
                    }
                }
            }
            for (uint32_t i = dead ? end - 1 : f->block_offsets[block]; i < end; i += 1)
            {
                const uint32_t out = values[i];
                const uint32_t op = dead ? IR_UNREACHABLE : f->op[i];
                const uint32_t mask = dead ? 0 : ir_value_mask(f, i);
                view.op[out] = (uint8_t)op;
                view.type[out] = dead ? IR_VOID : f->type[i];
                view.flags[out] = dead ? 0 : f->flags[i];
                view.alignment_log2[out] = dead ? 0 : f->alignment_log2[i];
                view.a[out] = dead ? BUSTER_IR_NONE : (mask & 1) != 0 ? ir_thread_read(work, values, merges, f->a[i], block) : f->a[i];
                view.b[out] = dead ? BUSTER_IR_NONE : (mask & 2) != 0 ? ir_thread_read(work, values, merges, f->b[i], block) : f->b[i];
                view.c[out] = dead ? BUSTER_IR_NONE : (mask & 4) != 0 ? ir_thread_read(work, values, merges, f->c[i], block) : f->c[i];
                if (dead)
                {
                    // No predecessor reaches it any more (ir_thread_dead): the optimizer removes it.
                }
                else if (op == IR_PHI)
                {
                    const bool threaded = (work->roles[block] & BUSTER_IR_THREAD_THREADED) != 0;
                    view.a[out] = extra;
                    view.b[out] = 0;
                    for (uint32_t p = 0; p < f->b[i]; p += 1)
                    {
                        const uint32_t predecessor = f->extra[f->a[i] + p * 2];
                        const bool gone = (threaded && work->actions[predecessor] != BUSTER_IR_THREAD_KEEP && work->threaded[predecessor] == block) || ir_thread_dead(work, predecessor);
                        if (!gone)
                        {
                            view.extra[extra++] = predecessor;
                            view.extra[extra++] = ir_thread_read(work, values, merges, f->extra[f->a[i] + p * 2 + 1], predecessor);
                            view.b[out] += 1;
                        }
                    }
                    for (uint32_t k = offsets[block]; k < offsets[block + 1]; k += 1)
                    {
                        const uint32_t predecessor = entries[k];
                        const uint32_t through = work->threaded[predecessor];
                        const uint32_t value = ir_thread_incoming(work, i, through);
                        const uint32_t through_first = f->block_offsets[through];
                        const uint32_t through_end = f->block_offsets[(uint64_t)through + 1];
                        const bool chained = value >= through_first && value < through_end && f->op[value] != IR_PHI;
                        uint32_t mapped = BUSTER_IR_NONE;
                        if (chained)
                        {
                            // A copied chain value: its copy's index in the predecessor.
                            mapped = bases[predecessor] + ir_thread_position(work, through_first, value);
                        }
                        else
                        {
                            mapped = ir_thread_read(work, values, merges, ir_thread_passed(work, through, predecessor, value), predecessor);
                        }
                        view.extra[extra++] = predecessor;
                        view.extra[extra++] = mapped;
                        view.b[out] += 1;
                    }
                }
                else if (op == IR_CALL || op == IR_CALL_INDIRECT)
                {
                    const IrCall call = f->calls[f->b[i]];
                    view.calls[f->b[i]].argument_first = extra;
                    for (uint32_t k = 0; k < call.argument_count; k += 1)
                    {
                        view.extra[extra++] = ir_thread_read(work, values, merges, f->extra[call.argument_first + k], block);
                    }
                }
                else if (ir_extra_value_count(f, i) != 0)
                {
                    view.c[out] = ir_intrinsic_ternary_c(f->c[i], extra);
                    for (uint32_t k = 0, extra_values = ir_extra_value_count(f, i); k < extra_values; k += 1)
                    {
                        view.extra[extra++] = ir_thread_read(work, values, merges, ir_extra_value(f, i, k), block);
                    }
                }
                else if (i + 1 == end && work->actions[block] == BUSTER_IR_THREAD_COPY)
                {
                    // The copy of the threaded block's chain, its phis read as this predecessor's inputs, then its branch.
                    const uint32_t threaded = work->threaded[block];
                    const uint32_t chain_first = f->block_offsets[threaded];
                    const uint32_t chain_end = f->block_offsets[(uint64_t)threaded + 1] - 1;
                    uint32_t position = bases[block];
                    for (uint32_t j = chain_first; j <= chain_end; j += 1)
                    {
                        if (f->op[j] != IR_PHI && (j == chain_end || work->needed[j] != 0))
                        {
                            const uint32_t at = j == chain_end ? out : position;
                            const uint32_t j_mask = ir_value_mask(f, j);
                            const uint32_t operands[3] = { f->a[j], f->b[j], f->c[j] };
                            uint32_t mapped[3] = { f->a[j], f->b[j], f->c[j] };
                            for (uint32_t k = 0; k < 3; k += 1)
                            {
                                if (((j_mask >> k) & 1) != 0)
                                {
                                    const uint32_t operand = operands[k];
                                    const bool inside = operand >= chain_first && operand < chain_end;
                                    uint32_t copied = BUSTER_IR_NONE;
                                    if (inside && f->op[operand] == IR_PHI)
                                    {
                                        copied = ir_thread_read(work, values, merges, ir_thread_incoming(work, operand, block), block);
                                    }
                                    else if (inside)
                                    {
                                        copied = bases[block] + ir_thread_position(work, chain_first, operand);
                                    }
                                    else
                                    {
                                        copied = ir_thread_read(work, values, merges, operand, block);
                                    }
                                    mapped[k] = copied;
                                }
                            }
                            view.op[at] = f->op[j];
                            view.type[at] = f->type[j];
                            view.flags[at] = f->flags[j];
                            view.alignment_log2[at] = f->alignment_log2[j];
                            view.a[at] = mapped[0];
                            view.b[at] = mapped[1];
                            view.c[at] = mapped[2];
                            if (view.debug != NULL)
                            {
                                view.debug->origins[at] = f->debug->origins[j];
                            }
                            position += j == chain_end ? 0 : 1;
                        }
                    }
                }
                else if (i + 1 == end && work->actions[block] == BUSTER_IR_THREAD_REDIRECT)
                {
                    const uint32_t threaded = work->threaded[block];
                    view.b[out] = f->b[i] == threaded ? work->targets[block] : f->b[i];
                    view.c[out] = f->c[i] == threaded ? work->targets[block] : f->c[i];
                }
                else if (i + 1 == end && work->actions[block] == BUSTER_IR_THREAD_COLLAPSE)
                {
                    view.op[out] = IR_BR;
                    view.a[out] = work->targets[block];
                    view.b[out] = BUSTER_IR_NONE;
                    view.c[out] = BUSTER_IR_NONE;
                }
            }
        }
        view.extra_count = extra;
        view.arena_bytes = output->length - mark;
        *result = view;
    }
    if (error != IR_OK)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

// One round over `function`: its candidates threaded where their predecessors allow it. `changed` says whether anything
// was; `result` is the rewritten function then, else `function` itself.
static IrCheck ir_thread_round(const IrFunction* function, Arena* output, Arena* scratch, IrThreadResult* totals, IrFunction* result, bool* changed, uint32_t* deferred)
{
    const uint64_t sm = scratch->length;
    IrCheck check = ir_check(IR_OK, BUSTER_IR_NONE, 0);
    IrThreadWork work = { .function = function };
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = function->block_count;
    *changed = false;
    *deferred = 0;
    *result = *function;
    // The usual function pays only a bounded byte scan: a block of phis that ends in a conditional branch is rare.
    bool candidate = false;
    for (uint64_t first = 0; first < count && !candidate; first += BUSTER_IR_THREAD_BATCH)
    {
        const uint32_t lanes = count - first < BUSTER_IR_THREAD_BATCH ? (uint32_t)(count - first) : BUSTER_IR_THREAD_BATCH;
        uint64_t branches = ir_thread_candidates(function->op + first, lanes);
        while (branches != 0 && !candidate)
        {
            const uint32_t branch = (uint32_t)first + (uint32_t)__builtin_ctzll(branches);
            branches &= branches - 1;
            // The branch's block starts at the last block offset at or below it: a phi there is the cheap test.
            uint32_t low = 0;
            uint32_t high = blocks;
            while (high - low > 1)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = function->block_offsets[middle] <= branch ? middle : low;
                high = function->block_offsets[middle] <= branch ? high : middle;
            }
            candidate = low != 0 && function->op[function->block_offsets[low]] == IR_PHI && !ir_thread_backward(function, function->block_offsets[low], branch + 1);
        }
    }
    if (candidate)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &work.cfg);
        bool ok = check.error == IR_OK;
        work.reachable = ir_allocate(scratch, blocks, 1, &ok);
        work.labeled = ir_allocate(scratch, blocks, 1, &ok);
        work.roles = ir_allocate(scratch, blocks, 1, &ok);
        work.candidates = ir_allocate(scratch, blocks, 4, &ok);
        work.escapes = ir_allocate(scratch, count, 1, &ok);
        work.needed = ir_allocate(scratch, count, 1, &ok);
        work.actions = ir_allocate(scratch, blocks, 1, &ok);
        work.threaded = ir_allocate(scratch, blocks, 4, &ok);
        work.targets = ir_allocate(scratch, blocks, 4, &ok);
        work.remaining = ir_allocate(scratch, blocks, 4, &ok);
        uint32_t* queue = ir_allocate(scratch, blocks, 4, &ok);
        check.error = check.error == IR_OK && !ok ? IR_ERROR_MEMORY : check.error;
        if (check.error == IR_OK)
        {
            memset(work.reachable, 0, blocks);
            memset(work.labeled, 0, blocks);
            memset(work.roles, 0, blocks);
            memset(work.candidates, 0, (uint64_t)blocks * 4);
            memset(work.escapes, 0, count);
            memset(work.needed, 0, count);
            memset(work.actions, 0, blocks);
            memset(work.threaded, 0xff, (uint64_t)blocks * 4);
            memset(work.targets, 0xff, (uint64_t)blocks * 4);
            memset(work.remaining, 0, (uint64_t)blocks * 4);
            for (uint32_t label = 0; label < function->label_count; label += 1)
            {
                if (function->labels[label] < blocks)
                {
                    work.labeled[function->labels[label]] = 1;
                }
            }
            uint32_t queued = blocks != 0 ? 1 : 0;
            queue[0] = 0;
            work.reachable[0] = blocks != 0;
            for (uint32_t at = 0; at < queued; at += 1)
            {
                const uint32_t block = queue[at];
                for (uint32_t e = work.cfg.successor_offsets[block]; e < work.cfg.successor_offsets[(uint64_t)block + 1]; e += 1)
                {
                    const uint32_t target = work.cfg.successors[e];
                    if (work.reachable[target] == 0)
                    {
                        work.reachable[target] = 1;
                        queue[queued++] = target;
                    }
                }
            }
            bool any = false;
            for (uint32_t block = 1; block < blocks; block += 1)
            {
                work.candidates[block] = ir_thread_shape(&work, block);
                any = any || work.candidates[block] != 0;
            }
            if (any)
            {
                ir_thread_escapes(&work, BUSTER_IR_THREAD_TRACK_CANDIDATES);
                // The chain's evaluation slots: a known flag and a value per instruction of the largest candidate.
                uint32_t largest = 0;
                for (uint32_t block = 1; block < blocks; block += 1)
                {
                    const uint32_t size = function->block_offsets[(uint64_t)block + 1] - function->block_offsets[block];
                    largest = work.candidates[block] != 0 && size > largest ? size : largest;
                }
                uint64_t* slots = ir_allocate(scratch, largest, 8, &ok);
                uint8_t* known = ir_allocate(scratch, largest, 1, &ok);
                check.error = ok ? IR_OK : IR_ERROR_MEMORY;
                // #2608's repairs, where a candidate's values used past it are phis a successor may merge again.
                if (check.error == IR_OK && ir_thread_repairable(&work, slots, known))
                {
                    ir_thread_repairs(&work);
                }
                for (uint32_t block = 1; check.error == IR_OK && block < blocks; block += 1)
                {
                    if (work.candidates[block] != 0)
                    {
                        check.error = ir_thread_block(&work, block, slots, known, scratch) ? IR_OK : IR_ERROR_MEMORY;
                    }
                }
            }
        }
        if (check.error == IR_OK && work.block_count != 0)
        {
            check.error = ir_thread_rewrite(&work, output, scratch, result);
            *changed = check.error == IR_OK;
        }
        *deferred = work.deferred;
        totals->walked += work.walked;
        if (check.error == IR_OK && *changed)
        {
            totals->blocks += work.block_count;
            totals->copies += work.copy_count;
            totals->redirects += work.redirect_count;
            totals->merges += work.merged;
        }
    }
    arena_pop_to(scratch, sm);
    return check;
}

// The function's truth values' blocks threaded (#2554), in up to BUSTER_IR_THREAD_ROUNDS rounds. The caller supplies
// verified SSA before use lists or a verified-analysis revision exist, and runs the optimizer afterwards, which verifies
// the result. `result->function` borrows the input's tables where nothing was threaded.
static IrCheck ir_thread(const IrFunction* function, Arena* output, Arena* scratch, IrThreadResult* result)
{
    const uint64_t om = output->length;
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    IrThreadResult totals = { .function = *function };
    bool changed = true;
    uint32_t deferred = 1;
    // Another round only where this one changed something and left a candidate to it.
    for (uint32_t round = 0; check.error == IR_OK && changed && deferred != 0 && round < BUSTER_IR_THREAD_ROUNDS; round += 1)
    {
        IrFunction next = { 0 };
        check = ir_thread_round(&totals.function, output, scratch, &totals, &next, &changed, &deferred);
        totals.attempts += 1;
        if (check.error == IR_OK && changed)
        {
            totals.function = next;
            totals.rounds += 1;
        }
    }
    if (check.error == IR_OK)
    {
        *result = totals;
    }
    else
    {
        arena_pop_to(output, om);
    }
    return check;
}

#endif
