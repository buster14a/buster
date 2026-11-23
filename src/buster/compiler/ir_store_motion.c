// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Loop store motion (#3309). Include after ir.c, dominators.c and ir_optimize_unroll.c.
//
// A loop that updates a global keeps it in memory: each iteration loads what the last one stored. Where the processor
// doesn't forward that store for nothing, the loop runs at the forwarding's latency whatever its arithmetic costs: a
// double's on the 9700X (`g_x = g_x * c + d` took 15.6 cycles an iteration, and 6.6 with the pass), and an integer's
// on the Xeon the issue was found on, though not on the 9700X. This pass runs on the lowered function, before SSA
// promotion, and gives each such global's bytes (a key: a symbol, a constant offset and a scalar type) a private stack
// slot of the function for the loop:
// - the loop's preheader loads the key and stores the value in the slot;
// - each of the loop's loads of the key reads the slot instead;
// - each of the loop's stores of the key stays as it is, and the value is stored in the slot too.
// Promotion (ssa_ir.c) then makes the slot values and phis, as it does a local's: the loop's loads are gone and its
// stores write a register's value. Memory holds the key's value after every store, as before: no store is added,
// delayed or removed, so no other thread, signal handler or load through another address sees a difference, and the
// function leaves no store that the source didn't make (a loop of no iteration stores nothing). The one access added
// is the preheader's load, of bytes the options say are an object's of the unit (IrStoreMotionOptions.symbol_bytes).
//
// A loop takes a key where all of these hold:
// - the loop is reducible, has a preheader (one predecessor outside it, which ends in `br` to its header) and no
//   barrier: a call of any kind (a row call and an asm body are calls), a fence, an atomic read/modify/write, an
//   observable (volatile, atomic or segment) load or store, a masked, compressing or scattering store, or the dynamic
//   stack's allocation or release;
// - every store in the loop is to a stack slot, to a symbol's bytes at a constant offset or at an index
//   (ir_store_motion_resolve reads an address back through the copies, the conversions between pointers and i64s, the
//   IR_ADDRESS nodes and the constant additions that make it), or to a symbol the options don't list; a store through
//   any other address may write the key, and the loop takes no key;
// - the key's symbol is one the options list (a distinct object: no other symbol names its bytes, and an access based
//   on another object's address doesn't reach them), and the key's bytes are inside it;
// - no other store to the symbol in the loop meets the key's bytes: one at an index, or one whose bytes overlap the
//   key's at another offset or type (a load that meets them reads memory, which is current);
// - the loop both loads and stores the key, and its type is an integer of 1 to 8 bytes, a pointer, a float or a double.
// The outermost such loop takes the key, with every access of it in the loops inside; at most
// IrStoreMotionOptions.key_limit keys a loop, since each holds a register across it (#2719's lesson of LICM).
//
// Loads through other addresses need no rule: memory is always current. That is why the stores stay. Sinking them to
// the loop's exits would also need every load in the loop to be of a known object, an exit block for each edge and a
// flag for the paths that stored nothing (a store the source didn't make is a data race the compiler added). It is a
// follow-up for the loops whose every access is known: docs/tier1-store-motion.md's MEASUREMENTS has what the stores
// cost on the 9700X, and why the pass is off by default.
//
// The scans over every instruction are vector code: the symbols' filter, which lets a function that names no listed
// symbol go after one pass over its opcodes, and the classification, which compresses the loads, stores and barriers
// into lists 64 instructions at a time. A function with no store to a listed symbol builds no CFG. What follows reads
// the lists: the addresses' chains (dependent walks of a few steps, as ir_optimize_memory.c's, whose guide has the
// measurement) and the loops' keys (a loop's accesses of listed symbols, few). The rewrite of a function that changed
// copies its instructions and remaps their operands 16 at a step, by the unroller's remap (ir_unroll_remap), in the
// runs between the few instructions an added one comes beside.
//
// Entry point: ir_store_motion. The input is never mutated. A function nothing moves in is returned as it was given
// (IrStoreMotionResult.changed false, its function the input's, nothing written to the output). A changed result borrows
// the input's immutable tables, as ir_if_convert's does: the input's and the output's arenas stay alive until promotion
// copies the function. Output and scratch must be distinct arenas; scratch is restored on every path, and the output on
// failure.

#ifndef BUSTER_IR_STORE_MOTION_C
#define BUSTER_IR_STORE_MOTION_C

// The keys and indexed symbols a loop's analysis follows; a loop with more is left as it is.
#define BUSTER_IR_STORE_MOTION_KEYS 64u
// The keys of one loop kept in slots, by default.
#define BUSTER_IR_STORE_MOTION_LIMIT 4u
// The steps of an address's chain; one past them is an unknown address.
#define BUSTER_IR_STORE_MOTION_STEPS 24u
// The instructions a chain's walk visits at most: each step's, and an address's or an addition's constant.
#define BUSTER_IR_STORE_MOTION_VISITS (2u * BUSTER_IR_STORE_MOTION_STEPS)

#define BUSTER_IR_STORE_MOTION_NONE 0u
#define BUSTER_IR_STORE_MOTION_LOAD 1u
#define BUSTER_IR_STORE_MOTION_STORE 2u
#define BUSTER_IR_STORE_MOTION_BARRIER 3u

// The kinds by opcode, before the flags that make an observable load or store a barrier.
static const uint8_t ir_store_motion_kinds[BUSTER_IR_OP_TABLE] = {
    [IR_LOAD] = BUSTER_IR_STORE_MOTION_LOAD,
    [IR_STORE] = BUSTER_IR_STORE_MOTION_STORE,
    [IR_CALL] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_CALL_INDIRECT] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_FENCE] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_ATOMIC_RMW] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_MASKED_STORE] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_COMPRESS_STORE] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_SCATTER] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_ALLOCA] = BUSTER_IR_STORE_MOTION_BARRIER,
    [IR_STACK_RESTORE] = BUSTER_IR_STORE_MOTION_BARRIER,
};

// What an address is based on: a symbol's object, a stack slot, or anything else (a loaded pointer, a parameter, a phi).
#define BUSTER_IR_STORE_MOTION_ROOT_UNKNOWN 0u
#define BUSTER_IR_STORE_MOTION_ROOT_SYMBOL 1u
#define BUSTER_IR_STORE_MOTION_ROOT_SLOT 2u
#define BUSTER_IR_STORE_MOTION_ROOT 3u
// An access's state past its root's kind: its address has an index that isn't a constant, and it has been resolved.
#define BUSTER_IR_STORE_MOTION_INDEXED 4u
#define BUSTER_IR_STORE_MOTION_RESOLVED 8u

// The tests' faults (IrStoreMotionOptions.mutate), each of which some fixture must reject: a barrier or an unknown store
// passed over, a key taken beside an indexed or an overlapping access of its symbol, a store whose value the slot
// doesn't get, and a key past its symbol's bytes.
#define BUSTER_IR_STORE_MOTION_MUTATE_BARRIER 1u
#define BUSTER_IR_STORE_MOTION_MUTATE_UNKNOWN 2u
#define BUSTER_IR_STORE_MOTION_MUTATE_INDEXED 4u
#define BUSTER_IR_STORE_MOTION_MUTATE_OVERLAP 8u
#define BUSTER_IR_STORE_MOTION_MUTATE_STORE 16u
#define BUSTER_IR_STORE_MOTION_MUTATE_BOUNDS 32u

// What became of a loop that stores to a listed symbol (the others aren't looked at), by its first failing rule.
typedef enum IrStoreMotionOutcome
{
    IR_STORE_MOTION_MOVED,
    IR_STORE_MOTION_IRREDUCIBLE,
    IR_STORE_MOTION_BARRIER,
    IR_STORE_MOTION_PREHEADER,
    IR_STORE_MOTION_UNKNOWN_STORE,
    IR_STORE_MOTION_MANY,
    // Every key of it was refused, or an outer loop took them.
    IR_STORE_MOTION_NO_KEY,
    IR_STORE_MOTION_OUTCOME_COUNT,
} IrStoreMotionOutcome;

static const char* const ir_store_motion_outcome_names[IR_STORE_MOTION_OUTCOME_COUNT] = {
    [IR_STORE_MOTION_MOVED] = "moved",
    [IR_STORE_MOTION_IRREDUCIBLE] = "irreducible",
    [IR_STORE_MOTION_BARRIER] = "barrier",
    [IR_STORE_MOTION_PREHEADER] = "preheader",
    [IR_STORE_MOTION_UNKNOWN_STORE] = "unknown-store",
    [IR_STORE_MOTION_MANY] = "many",
    [IR_STORE_MOTION_NO_KEY] = "no-key",
};

// Why a stored key of a loop that passed the loop's rules stayed in memory, by its first failing rule.
typedef enum IrStoreMotionKeyRefusal
{
    IR_STORE_MOTION_KEY_MOVED,
    IR_STORE_MOTION_KEY_MET,
    IR_STORE_MOTION_KEY_BOUNDS,
    IR_STORE_MOTION_KEY_TYPE,
    IR_STORE_MOTION_KEY_UNREAD,
    IR_STORE_MOTION_KEY_LIMIT,
    IR_STORE_MOTION_KEY_CHAIN,
    IR_STORE_MOTION_KEY_COUNT,
} IrStoreMotionKeyRefusal;

static const char* const ir_store_motion_key_names[IR_STORE_MOTION_KEY_COUNT] = {
    [IR_STORE_MOTION_KEY_MOVED] = "moved",
    [IR_STORE_MOTION_KEY_MET] = "met",
    [IR_STORE_MOTION_KEY_BOUNDS] = "bounds",
    [IR_STORE_MOTION_KEY_TYPE] = "type",
    [IR_STORE_MOTION_KEY_UNREAD] = "unread",
    [IR_STORE_MOTION_KEY_LIMIT] = "limit",
    [IR_STORE_MOTION_KEY_CHAIN] = "chain",
};

typedef struct IrStoreMotionOptions IrStoreMotionOptions;
struct IrStoreMotionOptions
{
    // By SymbolId, below `symbol_count`: the bytes of the object the symbol names, where the unit knows it for a distinct
    // object (its own definition, which no other symbol names and no other module replaces), and 0 for any other symbol.
    // The producer's fact, trusted as IrPassed's locations are. Null lists none: nothing moves.
    const uint32_t* symbol_bytes;
    uint32_t symbol_count;
    uint32_t key_limit;
    uint32_t mutate;
};

typedef struct IrStoreMotionResult IrStoreMotionResult;
struct IrStoreMotionResult
{
    IrFunction function;
    bool changed;
    // The loops that store to a listed symbol, by outcome, and their stored keys that passed the loop's rules, by refusal.
    uint32_t outcomes[IR_STORE_MOTION_OUTCOME_COUNT];
    uint32_t keys[IR_STORE_MOTION_KEY_COUNT];
    // The loads that read a slot, the stores whose value a slot takes too, and the instructions added.
    uint32_t loads;
    uint32_t stores;
    uint32_t added;
    uint64_t scratch_bytes;
};

// A loop's key while the loop is analyzed: a listed symbol's bytes at a constant offset, of one type; or, `indexed`,
// every byte of the symbol, for its accesses at an index.
typedef struct IrStoreMotionKey IrStoreMotionKey;
struct IrStoreMotionKey
{
    uint32_t symbol;
    int64_t offset;
    uint8_t type;
    // The least alignment its accesses claim: the preheader's load's.
    uint8_t alignment_log2;
    bool indexed;
    bool met;
    uint32_t loads;
    uint32_t stores;
    // Its first access in the loop (an index into the accesses), whose address the preheader's load repeats.
    uint32_t first;
    // The move it became, plus one, or 0.
    uint32_t move;
};

// A key a loop takes: its slot, and the preheader's instructions for it. The chain is the part of the first access's
// address inside the loop (`chains[chain_first..+chain_count)`, from the root's side on), which the preheader repeats
// before its terminator; `position` is the new index of the first of them, the load's after them, then the slot's
// address's and the slot's store's.
typedef struct IrStoreMotionMove IrStoreMotionMove;
struct IrStoreMotionMove
{
    uint32_t terminator;
    uint32_t address;
    uint32_t slot;
    uint32_t chain_first;
    uint32_t chain_count;
    uint32_t position;
    uint8_t type;
    uint8_t alignment_log2;
};

typedef struct IrStoreMotionWork IrStoreMotionWork;
struct IrStoreMotionWork
{
    const IrFunction* function;
    const IrStoreMotionOptions* options;
    IrCfg cfg;
    Dominators loops;
    // The plain loads and stores, and the barriers, in instruction order; each block's first access.
    uint32_t* accesses;
    uint32_t access_count;
    uint32_t* barriers;
    uint32_t barrier_count;
    uint32_t* access_first;
    // By access: its address's root, offset and state (BUSTER_IR_STORE_MOTION_ROOT's kind, _INDEXED, _RESOLVED), the
    // move that owns it plus one (0 for none), and its key in the loop being analyzed.
    uint32_t* roots;
    int64_t* offsets;
    uint8_t* states;
    uint32_t* owners;
    uint8_t* keys;
    // The blocks that hold a barrier, and those that hold a store to a listed symbol's constant offset: a bit each, in
    // rows of the loop bodies' width.
    uint64_t* barrier_blocks;
    uint64_t* candidate_blocks;
    IrStoreMotionMove* moves;
    uint32_t move_count;
    uint32_t move_capacity;
    uint32_t* chains;
    uint32_t chain_count;
    IrStoreMotionResult statistics;
};

static __attribute__((unused)) IrStoreMotionOptions ir_store_motion_options(void)
{
    const IrStoreMotionOptions options = { .key_limit = BUSTER_IR_STORE_MOTION_LIMIT };
    return options;
}

// Whether `symbol` is one the options list, and its object's bytes.
static uint32_t ir_store_motion_symbol_bytes(const IrStoreMotionOptions* options, uint32_t symbol)
{
    return options->symbol_bytes != 0 && symbol < options->symbol_count ? options->symbol_bytes[symbol] : 0;
}

// Whether `f` names a listed symbol at all: its IR_SYMBOL_ADDRs found 64 opcodes at a time, each one's symbol looked up.
// Most functions name none (the strings and functions they name aren't listed), and are done after this pass.
static bool ir_store_motion_symbols(const IrFunction* f, const IrStoreMotionOptions* options)
{
    const uint32_t n = f->instruction_count;
    bool found = false;
    for (uint64_t first = 0; !found && first < n; first += 64)
    {
        const uint64_t count = n - first < 64 ? n - first : 64;
        const __mmask64 valid = count == 64 ? ~UINT64_C(0) : (UINT64_C(1) << count) - 1;
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        uint64_t symbols = _mm512_mask_cmpeq_epi8_mask(valid, op, _mm512_set1_epi8(IR_SYMBOL_ADDR));
        while (!found && symbols != 0)
        {
            const uint32_t i = (uint32_t)first + (uint32_t)__builtin_ctzll(symbols);
            symbols &= symbols - 1;
            found = ir_store_motion_symbol_bytes(options, f->a[i]) != 0;
        }
    }
    return found;
}

// The plain loads and stores and the barriers, compressed into their lists 64 instructions at a time: the kinds by a
// 128-byte table's lookup, an observable load or store a barrier by its flags.
static void ir_store_motion_classify(IrStoreMotionWork* work)
{
    const IrFunction* f = work->function;
    const uint32_t n = f->instruction_count;
    const __m512i low = _mm512_loadu_si512(ir_store_motion_kinds);
    const __m512i high = _mm512_loadu_si512(ir_store_motion_kinds + 64);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; first < n; first += 64)
    {
        const uint64_t count = n - first < 64 ? n - first : 64;
        const __mmask64 valid = count == 64 ? ~UINT64_C(0) : (UINT64_C(1) << count) - 1;
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, f->flags + first);
        const __m512i kinds = _mm512_permutex2var_epi8(low, op, high);
        const __mmask64 access = valid & (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_IR_STORE_MOTION_LOAD)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_IR_STORE_MOTION_STORE)));
        const __mmask64 observable = access & _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_IR_OBSERVABLE));
        const __mmask64 barrier = (valid & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_IR_STORE_MOTION_BARRIER))) | observable;
        const __mmask64 plain = access & ~observable;
        for (uint32_t group = 0; group < 4; group += 1)
        {
            const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int)(first + group * 16)), lanes);
            const __mmask16 accessed = (__mmask16)(plain >> (group * 16));
            const __mmask16 stopped = (__mmask16)(barrier >> (group * 16));
            _mm512_mask_compressstoreu_epi32(work->accesses + work->access_count, accessed, index);
            work->access_count += (uint32_t)__builtin_popcount(accessed);
            _mm512_mask_compressstoreu_epi32(work->barriers + work->barrier_count, stopped, index);
            work->barrier_count += (uint32_t)__builtin_popcount(stopped);
        }
    }
}

// `v`'s value where it is an i64 constant's instruction.
static bool ir_store_motion_constant(const IrFunction* f, uint32_t v, int64_t* value)
{
    const bool constant = f->op[v] == IR_CONST && f->type[v] == IR_I64 && f->a[v] < f->constant_count;
    *value = constant ? (int64_t)f->constants[f->a[v]].low : 0;
    return constant;
}

// The address `value` read back to what it is based on: through the copies and the conversions between pointers and
// i64s, the IR_ADDRESS nodes (a displacement, and an index: a constant's product with the scale joins the offset, any
// other makes the address indexed) and the i64 additions and subtractions of a constant, to an IR_SYMBOL_ADDR or an
// IR_STACK_ADDR. Any other instruction on the way, an offset past int64_t's range or more than
// BUSTER_IR_STORE_MOTION_STEPS steps leaves the address unknown. An index is C's: it stays inside the object the address
// is based on, so an indexed address still names its root's bytes alone; a sum of two values that aren't constants is
// unknown, since either may be the base. With `visited`, the instructions on the way go there in the walk's order, the
// value's first: each step's, and an address's or an addition's constant after it (at most
// BUSTER_IR_STORE_MOTION_VISITS).
static uint8_t ir_store_motion_resolve(const IrFunction* f, uint32_t value, uint32_t* root, int64_t* offset, uint32_t* visited, uint32_t* visited_count)
{
    uint32_t v = value;
    int64_t sum = 0;
    uint8_t state = BUSTER_IR_STORE_MOTION_ROOT_UNKNOWN;
    uint32_t seen = 0;
    bool walking = true;
    for (uint32_t step = 0; walking && step < BUSTER_IR_STORE_MOTION_STEPS; step += 1)
    {
        const uint32_t op = f->op[v];
        const uint32_t type = f->type[v];
        uint32_t next = BUSTER_IR_NONE;
        uint32_t constant_at = BUSTER_IR_NONE;
        int64_t constant = 0;
        int64_t product = 0;
        int64_t reached = sum;
        bool moved = false;
        bool rooted = false;
        if (op == IR_SYMBOL_ADDR || op == IR_STACK_ADDR)
        {
            state = (uint8_t)(state | (op == IR_SYMBOL_ADDR ? BUSTER_IR_STORE_MOTION_ROOT_SYMBOL : BUSTER_IR_STORE_MOTION_ROOT_SLOT));
            *root = f->a[v];
            rooted = true;
        }
        else if (op == IR_COPY || op == IR_BITCAST || op == IR_INTTOPTR || op == IR_PTRTOINT)
        {
            const uint32_t source = f->type[f->a[v]];
            moved = (type == IR_PTR || type == IR_I64) && (source == IR_PTR || source == IR_I64);
            next = f->a[v];
        }
        else if (op == IR_ADDRESS)
        {
            const IrAddress entry = f->addresses[f->c[v]];
            const uint32_t index = f->b[v];
            moved = !__builtin_add_overflow(reached, entry.offset, &reached);
            if (moved && index != BUSTER_IR_NONE && ir_store_motion_constant(f, index, &constant))
            {
                moved = !__builtin_mul_overflow(constant, (int64_t)entry.scale, &product) && !__builtin_add_overflow(reached, product, &reached);
                constant_at = index;
            }
            else if (moved && index != BUSTER_IR_NONE)
            {
                state = (uint8_t)(state | BUSTER_IR_STORE_MOTION_INDEXED);
            }
            next = f->a[v];
        }
        else if ((op == IR_ADD || op == IR_SUB) && type == IR_I64)
        {
            if (ir_store_motion_constant(f, f->b[v], &constant))
            {
                moved = op == IR_ADD ? !__builtin_add_overflow(reached, constant, &reached) : !__builtin_sub_overflow(reached, constant, &reached);
                next = f->a[v];
                constant_at = f->b[v];
            }
            else if (op == IR_ADD && ir_store_motion_constant(f, f->a[v], &constant))
            {
                moved = !__builtin_add_overflow(reached, constant, &reached);
                next = f->b[v];
                constant_at = f->a[v];
            }
        }
        if (visited != 0 && (moved || rooted))
        {
            visited[seen] = v;
            seen += 1;
            visited[seen] = constant_at;
            seen += constant_at != BUSTER_IR_NONE;
        }
        walking = moved;
        sum = moved ? reached : sum;
        v = moved ? next : v;
    }
    *offset = sum;
    if (visited_count != 0)
    {
        *visited_count = seen;
    }
    return state;
}

// Access k's address, resolved once.
static uint8_t ir_store_motion_access(IrStoreMotionWork* work, uint32_t k)
{
    if ((work->states[k] & BUSTER_IR_STORE_MOTION_RESOLVED) == 0)
    {
        const IrFunction* f = work->function;
        const uint8_t state = ir_store_motion_resolve(f, f->a[work->accesses[k]], &work->roots[k], &work->offsets[k], 0, 0);
        // A root is its kind: an index or an offset on the way to no root says nothing.
        work->states[k] = (uint8_t)(((state & BUSTER_IR_STORE_MOTION_ROOT) != 0 ? state : 0) | BUSTER_IR_STORE_MOTION_RESOLVED);
    }
    return work->states[k];
}

// The type an access reads or writes: a load's, or a store's stored value's.
static uint32_t ir_store_motion_type(const IrFunction* f, uint32_t i)
{
    return f->op[i] == IR_STORE ? f->type[f->b[i]] : f->type[i];
}

static uint32_t ir_store_motion_bytes(uint32_t type)
{
    return ((uint32_t)ir_type_bits[type] + 7) / 8;
}

// The types a slot holds for a loop: what promotion makes a value of and a general or an xmm register carries.
static bool ir_store_motion_scalar(uint32_t type)
{
    return type == IR_I8 || type == IR_I16 || type == IR_I32 || type == IR_I64 || type == IR_PTR || type == IR_F32 || type == IR_F64;
}

// Whether access k is one of a listed symbol's: a key's, or an indexed one's.
static bool ir_store_motion_listed(const IrStoreMotionWork* work, uint32_t k)
{
    return (work->states[k] & BUSTER_IR_STORE_MOTION_ROOT) == BUSTER_IR_STORE_MOTION_ROOT_SYMBOL && ir_store_motion_symbol_bytes(work->options, work->roots[k]) != 0;
}

// The loop's preheader: its header's only reachable predecessor outside it, whose only successor the header is, by a
// `br`; or BUSTER_IR_NONE.
static uint32_t ir_store_motion_preheader(const IrStoreMotionWork* work, uint32_t loop)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const uint32_t header = work->loops.loop_header[loop];
    uint32_t preheader = BUSTER_IR_NONE;
    uint32_t entries = 0;
    for (uint32_t edge = cfg->predecessor_offsets[header]; edge < cfg->predecessor_offsets[(uint64_t)header + 1]; edge += 1)
    {
        const uint32_t predecessor = cfg->predecessors[edge];
        if (work->loops.rpo_index[predecessor] != BUSTER_DOMINATOR_NONE && !dominators_loop_contains(&work->loops, loop, predecessor))
        {
            entries += 1;
            preheader = predecessor;
        }
    }
    const bool alone = entries == 1 && cfg->successor_offsets[(uint64_t)preheader + 1] - cfg->successor_offsets[preheader] == 1 &&
                       f->op[f->block_offsets[(uint64_t)preheader + 1] - 1] == IR_BR;
    return alone ? preheader : BUSTER_IR_NONE;
}

// Whether two keys of one symbol meet: one of them is every byte of it, or their byte ranges do.
static bool ir_store_motion_meet(const IrStoreMotionKey* a, const IrStoreMotionKey* b)
{
    const int64_t end_a = a->offset + (int64_t)ir_store_motion_bytes(a->type);
    const int64_t end_b = b->offset + (int64_t)ir_store_motion_bytes(b->type);
    return a->indexed || b->indexed || (a->offset < end_b && b->offset < end_a);
}

// Key `key`'s move: its slot, and the part of its first access's address inside the loop, which the preheader repeats.
// Returns false where the chain has no room.
static bool ir_store_motion_take(IrStoreMotionWork* work, uint32_t loop, uint32_t preheader, const IrStoreMotionKey* key)
{
    const IrFunction* f = work->function;
    const uint32_t address = f->a[work->accesses[key->first]];
    uint32_t visited[BUSTER_IR_STORE_MOTION_VISITS];
    uint32_t count = 0;
    uint32_t root = 0;
    int64_t offset = 0;
    (void)ir_store_motion_resolve(f, address, &root, &offset, visited, &count);
    IrStoreMotionMove move = {
        .terminator = f->block_offsets[(uint64_t)preheader + 1] - 1,
        .address = address,
        .slot = f->slot_count + work->move_count,
        .chain_first = work->chain_count,
        .type = key->type,
        .alignment_log2 = key->alignment_log2,
    };
    // The walk went from the address to its root; the preheader's copy is made from the root's side.
    for (uint32_t k = count; k != 0; k -= 1)
    {
        const uint32_t v = visited[k - 1];
        if (dominators_loop_contains(&work->loops, loop, work->cfg.instruction_blocks[v]))
        {
            work->chains[work->chain_count + move.chain_count] = v;
            move.chain_count += 1;
        }
    }
    const bool room = work->move_count < work->move_capacity;
    if (room)
    {
        work->chain_count += move.chain_count;
        work->moves[work->move_count] = move;
        work->move_count += 1;
    }
    return room;
}

// One loop that stores to a listed symbol: its outcome by the header's rules, and its keys' moves.
static IrStoreMotionOutcome ir_store_motion_loop(IrStoreMotionWork* work, uint32_t loop)
{
    const IrFunction* f = work->function;
    const IrStoreMotionOptions* options = work->options;
    const Dominators* loops = &work->loops;
    const uint64_t words = loops->row_words;
    const uint64_t* body = loops->loop_bodies + (uint64_t)loop * words;
    bool barrier = false;
    for (uint64_t word = 0; word < words; word += 1)
    {
        barrier = barrier || (body[word] & work->barrier_blocks[word]) != 0;
    }
    const uint32_t preheader = ir_store_motion_preheader(work, loop);
    IrStoreMotionOutcome outcome = loops->loop_irreducible[loop] != 0                                   ? IR_STORE_MOTION_IRREDUCIBLE :
                                   barrier && (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_BARRIER) == 0 ? IR_STORE_MOTION_BARRIER :
                                   preheader == BUSTER_IR_NONE                                          ? IR_STORE_MOTION_PREHEADER :
                                                                                                          IR_STORE_MOTION_MOVED;
    IrStoreMotionKey keys[BUSTER_IR_STORE_MOTION_KEYS];
    uint32_t key_count = 0;
    // The loop's keys: its accesses of listed symbols that no outer loop took, each one's key found among the few so far.
    for (uint64_t word = 0; outcome == IR_STORE_MOTION_MOVED && word < words; word += 1)
    {
        for (uint64_t bits = body[word]; outcome == IR_STORE_MOTION_MOVED && bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(bits);
            for (uint32_t k = work->access_first[block]; outcome == IR_STORE_MOTION_MOVED && k < work->access_first[(uint64_t)block + 1]; k += 1)
            {
                const uint32_t i = work->accesses[k];
                const bool store = f->op[i] == IR_STORE;
                const uint8_t state = work->owners[k] == 0 ? ir_store_motion_access(work, k) : 0;
                if (work->owners[k] != 0)
                {
                    // An outer loop's: the slot's access now, and its store's bytes no key of this loop meets.
                }
                else if ((state & BUSTER_IR_STORE_MOTION_ROOT) == BUSTER_IR_STORE_MOTION_ROOT_UNKNOWN && store && (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_UNKNOWN) == 0)
                {
                    outcome = IR_STORE_MOTION_UNKNOWN_STORE;
                }
                else if (ir_store_motion_listed(work, k))
                {
                    const bool indexed = (state & BUSTER_IR_STORE_MOTION_INDEXED) != 0;
                    const uint8_t type = (uint8_t)ir_store_motion_type(f, i);
                    uint32_t at = 0;
                    while (at < key_count && !(keys[at].symbol == work->roots[k] && keys[at].indexed == indexed &&
                                               (indexed || (keys[at].offset == work->offsets[k] && keys[at].type == type))))
                    {
                        at += 1;
                    }
                    if (at == key_count && key_count == BUSTER_IR_STORE_MOTION_KEYS)
                    {
                        outcome = IR_STORE_MOTION_MANY;
                    }
                    else
                    {
                        if (at == key_count)
                        {
                            keys[at] = (IrStoreMotionKey){
                                .symbol = work->roots[k],
                                .offset = indexed ? 0 : work->offsets[k],
                                .type = type,
                                .alignment_log2 = f->alignment_log2[i],
                                .indexed = indexed,
                                .first = k,
                            };
                            key_count += 1;
                        }
                        keys[at].alignment_log2 = f->alignment_log2[i] < keys[at].alignment_log2 ? f->alignment_log2[i] : keys[at].alignment_log2;
                        keys[at].loads += !store;
                        keys[at].stores += store;
                        work->keys[k] = (uint8_t)at;
                    }
                }
            }
        }
    }
    // The keys another key's stores meet: a store to other bytes of the symbol that overlap theirs, or at an index.
    for (uint32_t a = 0; outcome == IR_STORE_MOTION_MOVED && a < key_count; a += 1)
    {
        for (uint32_t b = a + 1; b < key_count; b += 1)
        {
            const bool skipped = (keys[a].indexed || keys[b].indexed) ? (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_INDEXED) != 0 : (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_OVERLAP) != 0;
            if (keys[a].symbol == keys[b].symbol && !skipped && ir_store_motion_meet(&keys[a], &keys[b]))
            {
                keys[a].met = keys[a].met || keys[b].stores != 0;
                keys[b].met = keys[b].met || keys[a].stores != 0;
            }
        }
    }
    uint32_t taken = 0;
    for (uint32_t a = 0; outcome == IR_STORE_MOTION_MOVED && a < key_count; a += 1)
    {
        IrStoreMotionKey* key = &keys[a];
        if (!key->indexed && key->stores != 0)
        {
            const uint32_t bytes = ir_store_motion_bytes(key->type);
            const uint32_t object = ir_store_motion_symbol_bytes(options, key->symbol);
            const bool inside = (key->offset >= 0 && (uint64_t)key->offset <= object && bytes <= object - (uint64_t)key->offset) || (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_BOUNDS) != 0;
            IrStoreMotionKeyRefusal refusal = key->met                                ? IR_STORE_MOTION_KEY_MET :
                                              !inside                                 ? IR_STORE_MOTION_KEY_BOUNDS :
                                              !ir_store_motion_scalar(key->type)      ? IR_STORE_MOTION_KEY_TYPE :
                                              key->loads == 0                         ? IR_STORE_MOTION_KEY_UNREAD :
                                              taken >= options->key_limit             ? IR_STORE_MOTION_KEY_LIMIT :
                                                                                        IR_STORE_MOTION_KEY_MOVED;
            if (refusal == IR_STORE_MOTION_KEY_MOVED && !ir_store_motion_take(work, loop, preheader, key))
            {
                refusal = IR_STORE_MOTION_KEY_CHAIN;
            }
            if (refusal == IR_STORE_MOTION_KEY_MOVED)
            {
                key->move = work->move_count;
                taken += 1;
            }
            work->statistics.keys[refusal] += 1;
        }
    }
    // The taken keys' accesses, the loops' inside too: theirs from here on.
    for (uint64_t word = 0; taken != 0 && word < words; word += 1)
    {
        for (uint64_t bits = body[word]; bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(bits);
            for (uint32_t k = work->access_first[block]; k < work->access_first[(uint64_t)block + 1]; k += 1)
            {
                if (work->owners[k] == 0 && ir_store_motion_listed(work, k) && keys[work->keys[k]].move != 0)
                {
                    const bool store = f->op[work->accesses[k]] == IR_STORE;
                    work->owners[k] = keys[work->keys[k]].move;
                    work->statistics.loads += !store;
                    work->statistics.stores += store;
                }
            }
        }
    }
    outcome = outcome == IR_STORE_MOTION_MOVED && taken == 0 ? IR_STORE_MOTION_NO_KEY : outcome;
    return outcome;
}

// The loops that hold a store to a listed symbol's constant offset, the outermost first (by their sizes: an outer loop
// is larger than the loops inside it), each analyzed in turn.
static IrError ir_store_motion_loops(IrStoreMotionWork* work, Arena* scratch)
{
    const Dominators* loops = &work->loops;
    const uint64_t words = loops->row_words;
    bool ok = true;
    uint32_t* order = ir_allocate(scratch, loops->loop_count, sizeof(uint32_t), &ok);
    uint32_t count = 0;
    for (uint32_t loop = 0; ok && loop < loops->loop_count; loop += 1)
    {
        const uint64_t* body = loops->loop_bodies + (uint64_t)loop * words;
        bool held = false;
        for (uint64_t word = 0; word < words; word += 1)
        {
            held = held || (body[word] & work->candidate_blocks[word]) != 0;
        }
        if (held)
        {
            uint32_t at = count;
            while (at != 0 && loops->loop_size[order[at - 1]] < loops->loop_size[loop])
            {
                order[at] = order[at - 1];
                at -= 1;
            }
            order[at] = loop;
            count += 1;
        }
    }
    for (uint32_t at = 0; ok && at < count; at += 1)
    {
        const IrStoreMotionOutcome outcome = ir_store_motion_loop(work, order[at]);
        work->statistics.outcomes[outcome] += 1;
    }
    return ok ? IR_OK : IR_ERROR_MEMORY;
}

// map[first..end) = first + delta, ..., end - 1 + delta: a run of instructions that no added instruction parts moves by
// one distance, 16 indices at a time.
static void ir_store_motion_run(uint32_t* map, uint32_t first, uint32_t end, uint32_t delta)
{
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t at = first; at < end; at += 16)
    {
        const uint64_t count = end - at < 16 ? end - at : 16;
        const __mmask16 active = (__mmask16)((UINT32_C(1) << count) - 1);
        _mm512_mask_storeu_epi32(map + at, active, _mm512_add_epi32(_mm512_set1_epi32((int)((uint32_t)at + delta)), lanes));
    }
}

// How many of `f`'s instructions are calls: 64 opcodes at a time.
static uint64_t ir_store_motion_calls(const IrFunction* f)
{
    const uint32_t n = f->instruction_count;
    uint64_t calls = 0;
    for (uint64_t first = 0; first < n; first += 64)
    {
        const uint64_t count = n - first < 64 ? n - first : 64;
        const __mmask64 valid = count == 64 ? ~UINT64_C(0) : (UINT64_C(1) << count) - 1;
        const __m512i op = _mm512_maskz_loadu_epi8(valid, f->op + first);
        const __mmask64 direct = _mm512_mask_cmpeq_epi8_mask(valid, op, _mm512_set1_epi8(IR_CALL));
        const __mmask64 indirect = _mm512_mask_cmpeq_epi8_mask(valid, op, _mm512_set1_epi8(IR_CALL_INDIRECT));
        calls += (uint64_t)__builtin_popcountll(direct | indirect);
    }
    return calls;
}

// Instructions [first, end) of `f`, which keep their order and move by one distance, into the view at map[first]: their
// columns and fixed operands 16 at a step (ir_unroll_remap), and their phis', calls' and extra values' operands in new
// slices of the extra array (ir_unroll_extras).
static void ir_store_motion_copy(IrUnrollEmit* emit, const IrFunction* f, uint32_t first, uint32_t end, const uint32_t* map)
{
    if (end > first)
    {
        ir_unroll_remap(f, first, end - first, map, emit->view, map[first]);
        ir_unroll_extras(emit, f, first, end, map[first], map, NULL);
    }
}

// The function with the moves made: a private view that borrows the input's immutable tables, with new instruction
// columns, block offsets, extra words, calls and slots in `output`. Every instruction keeps its order; the preheaders'
// instructions come before their terminators, and a slot's store after the store whose value it takes. The instructions
// an added one comes before or after are few (a bit each in `marks`). Between two of them the input's instructions move
// by one distance: their new indices are filled a run at a time (ir_store_motion_run), and so are their columns copied
// and their operands remapped (ir_store_motion_copy). The input's extra words keep their places, where a switch's and an
// indirect branch's lists are read.
static IrError ir_store_motion_rewrite(IrStoreMotionWork* work, Arena* output, Arena* scratch, IrFunction* result)
{
    const IrFunction* f = work->function;
    const uint32_t n = f->instruction_count;
    const uint64_t mark = output->length;
    const uint64_t words = ((uint64_t)n + 63) / 64;
    bool ok = true;
    // By instruction: the instructions put before it and after it, how many of those before it are placed, its new
    // index, and (a chain's) its preheader's copy; and a bit where an instruction is put before it or after it.
    uint32_t* before = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint32_t* after = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint32_t* used = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint32_t* map = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint32_t* copies = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
    uint64_t* marks = ir_allocate(scratch, words, sizeof(uint64_t), &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    uint64_t extras = 0;
    uint64_t calls = 0;
    uint64_t total = n;
    if (error == IR_OK)
    {
        memset(copies, 0xff, (uint64_t)n * sizeof(uint32_t));
        for (uint32_t m = 0; m < work->move_count; m += 1)
        {
            const uint32_t i = work->moves[m].terminator;
            before[i] += work->moves[m].chain_count + 3;
            marks[i / 64] |= UINT64_C(1) << (i % 64);
        }
        for (uint32_t k = 0; k < work->access_count; k += 1)
        {
            const uint32_t i = work->accesses[k];
            if (work->owners[k] != 0 && f->op[i] == IR_STORE)
            {
                after[i] = 1;
                marks[i / 64] |= UINT64_C(1) << (i % 64);
            }
        }
        uint32_t first = 0;
        for (uint64_t word = 0; word < words; word += 1)
        {
            for (uint64_t bits = marks[word]; bits != 0; bits &= bits - 1)
            {
                const uint32_t i = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(bits);
                ir_store_motion_run(map, first, i, (uint32_t)(total - n));
                total += before[i];
                map[i] = (uint32_t)(total - n) + i;
                total += after[i];
                first = i + 1;
            }
        }
        ir_store_motion_run(map, first, n, (uint32_t)(total - n));
        extras = (uint64_t)f->extra_count + ir_unroll_extra_words(f, 0, n);
        calls = ir_store_motion_calls(f);
        if (total > UINT32_MAX || extras > UINT32_MAX || (uint64_t)f->slot_count + work->move_count > UINT32_MAX)
        {
            error = IR_ERROR_LIMIT;
        }
    }
    IrFunction view = *f;
    if (error == IR_OK)
    {
        view.instruction_count = (uint32_t)total;
        view.slot_count = f->slot_count + work->move_count;
        view.op = ir_allocate(output, total, 1, &ok);
        view.type = ir_allocate(output, total, 1, &ok);
        view.flags = ir_allocate(output, total, 1, &ok);
        view.alignment_log2 = ir_allocate(output, total, 1, &ok);
        view.a = ir_allocate(output, total, 4, &ok);
        view.b = ir_allocate(output, total, 4, &ok);
        view.c = ir_allocate(output, total, 4, &ok);
        view.block_offsets = ir_allocate(output, (uint64_t)f->block_count + 1, 4, &ok);
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, calls, sizeof(IrCall), &ok);
        view.slots = ir_allocate(output, view.slot_count, sizeof(IrSlot), &ok);
        view.extra_count = (uint32_t)extras;
        view.call_count = (uint32_t)calls;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        memset(view.a, 0xff, total * 4);
        memset(view.b, 0xff, total * 4);
        memset(view.c, 0xff, total * 4);
        if (f->slot_count != 0)
        {
            memcpy(view.slots, f->slots, (uint64_t)f->slot_count * sizeof(IrSlot));
        }
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * sizeof(uint32_t));
        }
        for (uint32_t b = 0; b < f->block_count; b += 1)
        {
            const uint32_t first = f->block_offsets[b];
            view.block_offsets[b] = map[first] - before[first];
        }
        view.block_offsets[f->block_count] = (uint32_t)total;
        // The input's instructions, a run at a time: one ends before an instruction that others are put before, and with
        // one that another is put after.
        IrUnrollEmit emit = { .view = &view, .extra = f->extra_count };
        uint32_t first = 0;
        for (uint64_t word = 0; word < words; word += 1)
        {
            for (uint64_t bits = marks[word]; bits != 0; bits &= bits - 1)
            {
                const uint32_t i = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(bits);
                if (before[i] != 0)
                {
                    ir_store_motion_copy(&emit, f, first, i, map);
                    first = i;
                }
                if (after[i] != 0)
                {
                    ir_store_motion_copy(&emit, f, first, i + 1, map);
                    first = i + 1;
                }
            }
        }
        ir_store_motion_copy(&emit, f, first, n, map);
        // Each move's preheader instructions: the chain's copies, the load, the slot's address and the slot's store.
        for (uint32_t m = 0; m < work->move_count; m += 1)
        {
            IrStoreMotionMove* move = &work->moves[m];
            const uint32_t bytes = ir_store_motion_bytes(move->type);
            const uint8_t slot_alignment = (uint8_t)__builtin_ctz(bytes);
            move->position = map[move->terminator] - before[move->terminator] + used[move->terminator];
            used[move->terminator] += move->chain_count + 3;
            view.slots[move->slot] = (IrSlot){ .size = bytes, .alignment = bytes, .type = move->type };
            for (uint32_t k = 0; k < move->chain_count; k += 1)
            {
                const uint32_t i = work->chains[move->chain_first + k];
                const uint32_t out = move->position + k;
                const uint32_t mask = ir_value_mask(f, i);
                const uint32_t a = (mask & 1) == 0 ? f->a[i] : copies[f->a[i]] != BUSTER_IR_NONE ? copies[f->a[i]] : map[f->a[i]];
                const uint32_t b = (mask & 2) == 0 ? f->b[i] : copies[f->b[i]] != BUSTER_IR_NONE ? copies[f->b[i]] : map[f->b[i]];
                view.op[out] = f->op[i];
                view.type[out] = f->type[i];
                view.flags[out] = f->flags[i];
                view.alignment_log2[out] = f->alignment_log2[i];
                view.a[out] = a;
                view.b[out] = b;
                view.c[out] = f->c[i];
                copies[i] = out;
            }
            const uint32_t address = copies[move->address] != BUSTER_IR_NONE ? copies[move->address] : map[move->address];
            for (uint32_t k = 0; k < move->chain_count; k += 1)
            {
                copies[work->chains[move->chain_first + k]] = BUSTER_IR_NONE;
            }
            const uint32_t load = move->position + move->chain_count;
            view.op[load] = IR_LOAD;
            view.type[load] = move->type;
            view.alignment_log2[load] = move->alignment_log2;
            view.a[load] = address;
            view.op[load + 1] = IR_STACK_ADDR;
            view.type[load + 1] = IR_PTR;
            view.a[load + 1] = move->slot;
            view.op[load + 2] = IR_STORE;
            view.type[load + 2] = IR_VOID;
            view.alignment_log2[load + 2] = slot_alignment;
            view.a[load + 2] = load + 1;
            view.b[load + 2] = load;
        }
        ir_debug_remap(f, &view, map, NULL, NULL);
        // The taken keys' accesses: a load reads the slot; a store's value goes to the slot too, after it.
        for (uint32_t k = 0; k < work->access_count; k += 1)
        {
            if (work->owners[k] != 0)
            {
                const uint32_t i = work->accesses[k];
                const uint32_t out = map[i];
                const IrStoreMotionMove* move = &work->moves[work->owners[k] - 1];
                const uint32_t slot_address = move->position + move->chain_count + 1;
                const uint8_t slot_alignment = (uint8_t)__builtin_ctz(ir_store_motion_bytes(move->type));
                if (f->op[i] == IR_LOAD)
                {
                    view.a[out] = slot_address;
                    view.alignment_log2[out] = slot_alignment;
                }
                else
                {
                    const bool dropped = (work->options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_STORE) != 0;
                    view.op[out + 1] = IR_STORE;
                    view.type[out + 1] = IR_VOID;
                    view.alignment_log2[out + 1] = slot_alignment;
                    view.a[out + 1] = slot_address;
                    view.b[out + 1] = dropped ? move->position + move->chain_count : view.b[out];
                }
            }
        }
        view.arena_bytes = output->length - mark;
        work->statistics.added = (uint32_t)(total - n);
        *result = view;
    }
    else
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_store_motion(const IrFunction* function, const IrStoreMotionOptions* options, Arena* output, Arena* scratch, IrStoreMotionResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    const uint32_t n = function->instruction_count;
    const uint32_t blocks = function->block_count;
    IrStoreMotionWork work = { .function = function, .options = options };
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    bool ok = true;
    const bool named = check.error == IR_OK && n != 0 && ir_store_motion_symbols(function, options);
    uint32_t candidates = 0;
    if (named)
    {
        work.accesses = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        work.barriers = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (named && check.error == IR_OK)
    {
        ir_store_motion_classify(&work);
        work.roots = ir_allocate(scratch, work.access_count, sizeof(uint32_t), &ok);
        work.offsets = ir_allocate(scratch, work.access_count, sizeof(int64_t), &ok);
        work.states = ir_allocate(scratch, work.access_count, sizeof(uint8_t), &ok);
        work.owners = ir_allocate(scratch, work.access_count, sizeof(uint32_t), &ok);
        work.keys = ir_allocate(scratch, work.access_count, sizeof(uint8_t), &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        // The stores to a listed symbol's constant offset: without one no loop has a key, and no CFG is built.
        for (uint32_t k = 0; check.error == IR_OK && k < work.access_count; k += 1)
        {
            if (function->op[work.accesses[k]] == IR_STORE)
            {
                const uint8_t state = ir_store_motion_access(&work, k);
                candidates += ir_store_motion_listed(&work, k) && (state & BUSTER_IR_STORE_MOTION_INDEXED) == 0;
            }
        }
    }
    const bool stored = named && check.error == IR_OK && candidates != 0;
    if (stored)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &work.cfg);
    }
    if (stored && check.error == IR_OK)
    {
        const DominatorCfg cfg = {
            .block_count = work.cfg.block_count,
            .edge_count = work.cfg.edge_count,
            .entry = blocks == 0 ? BUSTER_DOMINATOR_NONE : 0,
            .successor_offsets = work.cfg.successor_offsets,
            .successors = work.cfg.successors,
            .predecessor_offsets = work.cfg.predecessor_offsets,
            .predecessors = work.cfg.predecessors,
        };
        DominatorOptions dom = dominators_options();
        dom.outputs = BUSTER_DOMINATOR_LOOPS;
        const DominatorStatus status = dominators_analyze(scratch, output, &cfg, dom, &work.loops);
        // A function past the analysis' limits is left as it is.
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_UNSUPPORTED;
    }
    const bool looped = stored && check.error == IR_OK && work.loops.loop_count != 0;
    if (looped)
    {
        const uint64_t words = work.loops.row_words;
        work.access_first = ir_allocate(scratch, (uint64_t)blocks + 1, sizeof(uint32_t), &ok);
        work.barrier_blocks = ir_allocate(scratch, words, sizeof(uint64_t), &ok);
        work.candidate_blocks = ir_allocate(scratch, words, sizeof(uint64_t), &ok);
        work.move_capacity = candidates;
        work.moves = ir_allocate(scratch, work.move_capacity, sizeof(IrStoreMotionMove), &ok);
        work.chains = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_VISITS, sizeof(uint32_t), &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (looped && check.error == IR_OK)
    {
        // Each block's first access: the list is in instruction order, and a block's instructions are a range.
        uint32_t k = 0;
        for (uint32_t b = 0; b <= blocks; b += 1)
        {
            const uint32_t start = function->block_offsets[b];
            while (k < work.access_count && work.accesses[k] < start)
            {
                k += 1;
            }
            work.access_first[b] = k;
        }
        for (uint32_t e = 0; e < work.barrier_count; e += 1)
        {
            const uint32_t block = work.cfg.instruction_blocks[work.barriers[e]];
            work.barrier_blocks[block / 64] |= UINT64_C(1) << (block % 64);
        }
        for (k = 0; k < work.access_count; k += 1)
        {
            const uint32_t i = work.accesses[k];
            if (function->op[i] == IR_STORE && ir_store_motion_listed(&work, k) && (work.states[k] & BUSTER_IR_STORE_MOTION_INDEXED) == 0)
            {
                const uint32_t block = work.cfg.instruction_blocks[i];
                work.candidate_blocks[block / 64] |= UINT64_C(1) << (block % 64);
            }
        }
        check.error = ir_store_motion_loops(&work, scratch);
    }
    IrFunction rewritten = { 0 };
    const bool moved = looped && check.error == IR_OK && work.move_count != 0;
    if (moved)
    {
        check.error = ir_store_motion_rewrite(&work, output, scratch, &rewritten);
    }
    if (check.error == IR_OK || check.error == IR_ERROR_UNSUPPORTED)
    {
        const bool changed = moved && check.error == IR_OK;
        work.statistics.function = changed ? rewritten : *function;
        work.statistics.changed = changed;
        work.statistics.scratch_bytes = scratch->length - sm;
        *result = work.statistics;
    }
    if (check.error != IR_OK)
    {
        arena_pop_to(output, om);
    }
    arena_pop_to(scratch, sm);
    return check;
}

#endif
