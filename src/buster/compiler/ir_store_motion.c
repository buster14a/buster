// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Loop store motion (#3309, #3346). Include after ir.c, dominators.c and ir_optimize_unroll.c.
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
// stores write a register's value. Where the stores stay, memory holds the key's value after every store, as before:
// no store is added, delayed or removed, so no other thread, signal handler or load through another address sees a
// difference. The one access added is the preheader's load, of bytes the options say are an object's of the unit
// (IrStoreMotionOptions.symbol_bytes). Where they sink, below, memory holds the key's value at every exit.
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
// Loads through other addresses need no rule while the stores stay: memory is always current. #3346 sinks a key's
// stores to the loop's exits where that rule isn't needed: every read of memory in the loop is a plain load the pass
// places (a stack slot's, or a symbol's), and none but the key's own meets its bytes. Then the loop's stores of the
// key write the slot alone, and each exit writes the slot's value back once (IrStoreMotionMove.sink): through the
// key's address where its bits differ from the preheader's load, and through a dummy frame slot where they don't, so
// that a loop of no iteration, or one that changed nothing, stores nothing to the object (a store the source didn't
// make would be a data race the compiler added; skipping one of the bits memory holds isn't, since nothing in the loop
// synchronizes). An exit block whose every predecessor is in the loop gets the write-back first; an exit edge by a
// `cbr` to a block that other paths reach too is split through a block of its own, after the function's last. A loop
// with another exit to such a block (a switch's, an indirect branch's) keeps its stores. docs/tier1-store-motion.md's
// MEASUREMENTS has what the stores cost on the 9700X, and why the pass is off by default.
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
// The exits of a loop whose keys sink (#3346); a loop with more keeps its stores.
#define BUSTER_IR_STORE_MOTION_EXITS 16u
// A sunk move's exit that is a split edge's block: the bit, and the split's index below it.
#define BUSTER_IR_STORE_MOTION_SPLIT 0x80000000u

#define BUSTER_IR_STORE_MOTION_NONE 0u
#define BUSTER_IR_STORE_MOTION_LOAD 1u
#define BUSTER_IR_STORE_MOTION_STORE 2u
#define BUSTER_IR_STORE_MOTION_BARRIER 3u
// A read of memory that isn't a plain load (a masked load, a gather): the loop's memory must be current for it (#3346).
#define BUSTER_IR_STORE_MOTION_OPAQUE 4u

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
    [IR_MASKED_LOAD] = BUSTER_IR_STORE_MOTION_OPAQUE,
    [IR_GATHER] = BUSTER_IR_STORE_MOTION_OPAQUE,
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
// #3346's: a key sunk past a load the pass can't place, an exit given no write-back, and a write-back to the key on a
// path that didn't change it (the store the source didn't make).
#define BUSTER_IR_STORE_MOTION_MUTATE_SINK_LOAD 64u
#define BUSTER_IR_STORE_MOTION_MUTATE_SINK_EXIT 128u
#define BUSTER_IR_STORE_MOTION_MUTATE_SINK_ALWAYS 256u

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

// Whether a moved key's stores sank to the loop's exits (#3346), or why they stayed, by the first failing rule: the loop
// reads memory it can't place (a load through an unknown address, a masked load or a gather), a load in it meets the
// key's bytes, or an exit block has a predecessor outside the loop (or the loop has more than
// BUSTER_IR_STORE_MOTION_EXITS exit blocks).
typedef enum IrStoreMotionSink
{
    IR_STORE_MOTION_SINK_SUNK,
    IR_STORE_MOTION_SINK_LOAD,
    IR_STORE_MOTION_SINK_MET,
    IR_STORE_MOTION_SINK_EXIT,
    IR_STORE_MOTION_SINK_COUNT,
} IrStoreMotionSink;

static const char* const ir_store_motion_sink_names[IR_STORE_MOTION_SINK_COUNT] = {
    [IR_STORE_MOTION_SINK_SUNK] = "sunk",
    [IR_STORE_MOTION_SINK_LOAD] = "load",
    [IR_STORE_MOTION_SINK_MET] = "met",
    [IR_STORE_MOTION_SINK_EXIT] = "exit",
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
    // The moved keys by whether their stores sank, the write-backs on the exits of those that did, and the blocks added
    // for the exit edges they split.
    uint32_t sinks[IR_STORE_MOTION_SINK_COUNT];
    uint32_t write_backs;
    uint32_t blocks;
    // The loads that read a slot, the stores whose value a slot takes (too, where the key's stores stay), the
    // instructions added and the slots.
    uint32_t loads;
    uint32_t stores;
    uint32_t added;
    uint32_t slots;
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
    // A load of the loop that isn't the key's meets its bytes: its stores can't sink.
    bool read;
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
    // The stores sink (#3346): the loop's stores of the key write the slot alone, and each of its exit blocks writes the
    // slot's value back first, before its first instruction that isn't a phi (`exits[exit_first..+exit_count)`, those
    // instructions), through a selected address: the key's where the value differs from the preheader's load, the dummy
    // slot's where it doesn't. `address` is the key's address in the new function.
    bool sink;
    uint32_t exit_first;
    uint32_t exit_count;
    uint32_t key_address;
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
    // The reads that aren't plain loads, in instruction order, and their blocks' bits (#3346).
    uint32_t* opaque;
    uint32_t opaque_count;
    uint64_t* opaque_blocks;
    // The sunk moves' exits (an exit block's first instruction that isn't a phi, or BUSTER_IR_STORE_MOTION_SPLIT and a
    // split edge's index), and the new index of each one's write-back.
    uint32_t* exits;
    uint32_t* exit_positions;
    uint32_t exit_count;
    // The edges the write-backs split, each one's loop block and exit block: block `block_count + k` is edge k's.
    uint32_t* split_from;
    uint32_t* split_to;
    uint32_t split_count;
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
        const __mmask64 opaque = valid & _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_IR_STORE_MOTION_OPAQUE));
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
            const __mmask16 read = (__mmask16)(opaque >> (group * 16));
            _mm512_mask_compressstoreu_epi32(work->opaque + work->opaque_count, read, index);
            work->opaque_count += (uint32_t)__builtin_popcount(read);
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

// Block `block`'s first instruction that isn't a phi: where a write-back goes.
static uint32_t ir_store_motion_entry(const IrFunction* f, uint32_t block)
{
    uint32_t i = f->block_offsets[block];
    while (i + 1 < f->block_offsets[(uint64_t)block + 1] && f->op[i] == IR_PHI)
    {
        i += 1;
    }
    return i;
}

// The loop's exits, where its sunk keys' write-backs go: an exit block whose every predecessor is in the loop gets them
// before its first instruction that isn't a phi (`from` BUSTER_IR_NONE, `to` the block); an exit edge to a block that
// other paths reach too (the block after a loop that a `break`'s block also branches to) gets a block of its own, which
// the edge is split through (`from` the loop's block, `to` the exit block). A return, a break and a goto out of the
// loop are exits alike: each leaves by an edge to a block outside it. Returns false where an edge to split leaves by a
// terminator other than a `cbr` to two blocks (a switch's or an indirect branch's: its other cases may go to the same
// block), or where there are more than BUSTER_IR_STORE_MOTION_EXITS: then the loop's stores stay.
static bool ir_store_motion_exits(const IrStoreMotionWork* work, uint32_t loop, uint32_t* from, uint32_t* to, uint32_t* exit_count)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const Dominators* loops = &work->loops;
    const uint64_t words = loops->row_words;
    const uint64_t* body = loops->loop_bodies + (uint64_t)loop * words;
    uint32_t count = 0;
    bool taken = true;
    for (uint64_t word = 0; taken && word < words; word += 1)
    {
        for (uint64_t bits = body[word]; taken && bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(word * 64) + (uint32_t)__builtin_ctzll(bits);
            for (uint32_t edge = cfg->successor_offsets[block]; taken && edge < cfg->successor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                const uint32_t exit = cfg->successors[edge];
                bool dedicated = !dominators_loop_contains(loops, loop, exit);
                for (uint32_t back = cfg->predecessor_offsets[exit]; dedicated && back < cfg->predecessor_offsets[(uint64_t)exit + 1]; back += 1)
                {
                    dedicated = dominators_loop_contains(loops, loop, cfg->predecessors[back]);
                }
                const uint32_t terminator = f->block_offsets[(uint64_t)block + 1] - 1;
                const uint32_t source = dedicated ? BUSTER_IR_NONE : block;
                uint32_t at = 0;
                while (at < count && !(to[at] == exit && from[at] == source))
                {
                    at += 1;
                }
                if (at == count && !dominators_loop_contains(loops, loop, exit))
                {
                    taken = count < BUSTER_IR_STORE_MOTION_EXITS && (dedicated || (f->op[terminator] == IR_CBR && f->b[terminator] != f->c[terminator]));
                    from[count] = source;
                    to[count] = exit;
                    count += taken;
                }
            }
        }
    }
    *exit_count = count;
    return taken;
}

// The type a key of `type` is compared in at a write-back: its bits as an integer for a float's, widened to an i32 for an
// i8's or an i16's (Tier 1 selects compares of i32s, i64s and pointers); IR_VOID where it is compared as it is.
static uint8_t ir_store_motion_compared(uint32_t type)
{
    return type == IR_F64 ? IR_I64 : type == IR_F32 || type == IR_I8 || type == IR_I16 ? IR_I32 : IR_VOID;
}

// The instructions of one write-back of a key of `type`: the slot's load, the key's, the compare of the two (after their
// conversions where ir_store_motion_compared has one), the dummy slot's address, the select and the store.
static uint32_t ir_store_motion_write_back_size(uint32_t type)
{
    return ir_store_motion_compared(type) != IR_VOID ? 8u : 6u;
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
static bool ir_store_motion_take(IrStoreMotionWork* work, uint32_t loop, uint32_t preheader, const IrStoreMotionKey* key, bool sink, const uint32_t* from, const uint32_t* to, uint32_t exit_count)
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
        .sink = sink,
        .exit_first = work->exit_count,
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
    // The fault of an exit given no write-back drops the first.
    const uint32_t dropped = sink && exit_count != 0 && (work->options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_SINK_EXIT) != 0;
    for (uint32_t e = dropped; room && sink && e < exit_count; e += 1)
    {
        // An edge to split is one block of the function's however many loops' keys it writes back.
        uint32_t split = 0;
        while (from[e] != BUSTER_IR_NONE && split < work->split_count && !(work->split_from[split] == from[e] && work->split_to[split] == to[e]))
        {
            split += 1;
        }
        if (from[e] != BUSTER_IR_NONE && split == work->split_count)
        {
            work->split_from[split] = from[e];
            work->split_to[split] = to[e];
            work->split_count += 1;
        }
        work->exits[work->exit_count + move.exit_count] = from[e] == BUSTER_IR_NONE ? ir_store_motion_entry(work->function, to[e]) : BUSTER_IR_STORE_MOTION_SPLIT | split;
        move.exit_count += 1;
    }
    if (room)
    {
        work->exit_count += move.exit_count;
        work->statistics.write_backs += move.exit_count;
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
    // A load the pass can't place, in the loop: then no key's stores sink.
    bool unknown_load = false;
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
                else if ((state & BUSTER_IR_STORE_MOTION_ROOT) == BUSTER_IR_STORE_MOTION_ROOT_UNKNOWN && !store)
                {
                    unknown_load = true;
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
            if (keys[a].symbol == keys[b].symbol && ir_store_motion_meet(&keys[a], &keys[b]))
            {
                keys[a].read = keys[a].read || keys[b].loads != 0;
                keys[b].read = keys[b].read || keys[a].loads != 0;
            }
        }
    }
    // Whether the loop's stores can sink (#3346): every read of memory in it is a plain load the pass places (a stack
    // slot's, or a symbol's, which a listed key's bytes meet only where it is that symbol's), and each exit has a place
    // for the write-back (ir_store_motion_exits).
    bool opaque = false;
    for (uint64_t word = 0; word < words; word += 1)
    {
        opaque = opaque || (body[word] & work->opaque_blocks[word]) != 0;
    }
    const bool blind = (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_SINK_LOAD) != 0;
    uint32_t exit_from[BUSTER_IR_STORE_MOTION_EXITS];
    uint32_t exit_to[BUSTER_IR_STORE_MOTION_EXITS];
    uint32_t exit_count = 0;
    const IrStoreMotionSink sinking = outcome != IR_STORE_MOTION_MOVED || key_count == 0 ? IR_STORE_MOTION_SINK_COUNT :
                                      (unknown_load || opaque) && !blind                   ? IR_STORE_MOTION_SINK_LOAD :
                                      !ir_store_motion_exits(work, loop, exit_from, exit_to, &exit_count) ? IR_STORE_MOTION_SINK_EXIT :
                                                                                             IR_STORE_MOTION_SINK_SUNK;
    uint32_t taken = 0;
    for (uint32_t a = 0; outcome == IR_STORE_MOTION_MOVED && a < key_count; a += 1)
    {
        IrStoreMotionKey* key = &keys[a];
        if (!key->indexed && key->stores != 0)
        {
            const IrStoreMotionSink sink = sinking == IR_STORE_MOTION_SINK_SUNK && key->read && !blind ? IR_STORE_MOTION_SINK_MET : sinking;
            const uint32_t bytes = ir_store_motion_bytes(key->type);
            const uint32_t object = ir_store_motion_symbol_bytes(options, key->symbol);
            const bool inside = (key->offset >= 0 && (uint64_t)key->offset <= object && bytes <= object - (uint64_t)key->offset) || (options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_BOUNDS) != 0;
            IrStoreMotionKeyRefusal refusal = key->met                                ? IR_STORE_MOTION_KEY_MET :
                                              !inside                                 ? IR_STORE_MOTION_KEY_BOUNDS :
                                              !ir_store_motion_scalar(key->type)      ? IR_STORE_MOTION_KEY_TYPE :
                                              key->loads == 0 && sink != IR_STORE_MOTION_SINK_SUNK ? IR_STORE_MOTION_KEY_UNREAD :
                                              taken >= options->key_limit             ? IR_STORE_MOTION_KEY_LIMIT :
                                                                                        IR_STORE_MOTION_KEY_MOVED;
            if (refusal == IR_STORE_MOTION_KEY_MOVED && !ir_store_motion_take(work, loop, preheader, key, sink == IR_STORE_MOTION_SINK_SUNK, exit_from, exit_to, exit_count))
            {
                refusal = IR_STORE_MOTION_KEY_CHAIN;
            }
            if (refusal == IR_STORE_MOTION_KEY_MOVED)
            {
                key->move = work->move_count;
                taken += 1;
                work->statistics.sinks[sink] += 1;
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
    // By split edge: its block's write-backs' instructions, how many of them are placed, and its first new index.
    uint32_t* split_size = ir_allocate(scratch, work->split_count, sizeof(uint32_t), &ok);
    uint32_t* split_used = ir_allocate(scratch, work->split_count, sizeof(uint32_t), &ok);
    uint32_t* split_base = ir_allocate(scratch, work->split_count, sizeof(uint32_t), &ok);
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
        for (uint32_t m = 0; m < work->move_count; m += 1)
        {
            const IrStoreMotionMove* move = &work->moves[m];
            for (uint32_t e = move->exit_first; e < move->exit_first + move->exit_count; e += 1)
            {
                const uint32_t i = work->exits[e];
                if ((i & BUSTER_IR_STORE_MOTION_SPLIT) != 0)
                {
                    split_size[i & ~BUSTER_IR_STORE_MOTION_SPLIT] += ir_store_motion_write_back_size(move->type);
                }
                else
                {
                    before[i] += ir_store_motion_write_back_size(move->type);
                    marks[i / 64] |= UINT64_C(1) << (i % 64);
                }
            }
        }
        // A kept store's value goes to the slot too, after it; a sunk one's store writes the slot instead.
        for (uint32_t k = 0; k < work->access_count; k += 1)
        {
            const uint32_t i = work->accesses[k];
            if (work->owners[k] != 0 && f->op[i] == IR_STORE && !work->moves[work->owners[k] - 1].sink)
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
        // The split edges' blocks after the input's last: each one's write-backs, then its `br` to the exit block.
        for (uint32_t k = 0; k < work->split_count; k += 1)
        {
            split_base[k] = (uint32_t)total;
            total += (uint64_t)split_size[k] + 1;
        }
        extras = (uint64_t)f->extra_count + ir_unroll_extra_words(f, 0, n);
        calls = ir_store_motion_calls(f);
        if (total > UINT32_MAX || extras > UINT32_MAX || (uint64_t)f->slot_count + work->move_count + 1 > UINT32_MAX || (uint64_t)f->block_count + work->split_count > UINT32_MAX)
        {
            error = IR_ERROR_LIMIT;
        }
    }
    // The dummy slot the sunk moves' write-backs select where the key's value didn't change: the last.
    const uint32_t dummy = f->slot_count + work->move_count;
    const uint32_t slots = work->move_count + (work->exit_count != 0);
    IrFunction view = *f;
    if (error == IR_OK)
    {
        view.instruction_count = (uint32_t)total;
        view.slot_count = f->slot_count + slots;
        view.op = ir_allocate(output, total, 1, &ok);
        view.type = ir_allocate(output, total, 1, &ok);
        view.flags = ir_allocate(output, total, 1, &ok);
        view.alignment_log2 = ir_allocate(output, total, 1, &ok);
        view.a = ir_allocate(output, total, 4, &ok);
        view.b = ir_allocate(output, total, 4, &ok);
        view.c = ir_allocate(output, total, 4, &ok);
        view.block_count = f->block_count + work->split_count;
        view.block_offsets = ir_allocate(output, (uint64_t)view.block_count + 1, 4, &ok);
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
        for (uint32_t k = 0; k < work->split_count; k += 1)
        {
            view.block_offsets[f->block_count + k] = split_base[k];
        }
        view.block_offsets[view.block_count] = (uint32_t)total;
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
        if (work->exit_count != 0)
        {
            view.slots[dummy] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        }
        // The write-backs' places first: before an instruction that both a write-back and a preheader's instructions
        // come before (an exit block that is another loop's preheader), the write-back goes first.
        for (uint32_t m = 0; m < work->move_count; m += 1)
        {
            const IrStoreMotionMove* move = &work->moves[m];
            for (uint32_t e = move->exit_first; e < move->exit_first + move->exit_count; e += 1)
            {
                const uint32_t i = work->exits[e];
                const uint32_t split = i & ~BUSTER_IR_STORE_MOTION_SPLIT;
                const uint32_t size = ir_store_motion_write_back_size(move->type);
                if ((i & BUSTER_IR_STORE_MOTION_SPLIT) != 0)
                {
                    work->exit_positions[e] = split_base[split] + split_used[split];
                    split_used[split] += size;
                }
                else
                {
                    work->exit_positions[e] = map[i] - before[i] + used[i];
                    used[i] += size;
                }
            }
        }
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
            move->key_address = address;
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
        // Each sunk move's write-backs: the slot's value, stored through the key's address where its bits differ from
        // the key's own bytes, which the loop no longer wrote, and through the dummy slot's where they don't, so that a
        // path that changed nothing stores nothing to the key. (The bits: a float's -0.0 isn't 0.0 here, and a NaN is
        // itself.) The key is read again at the exit rather than the preheader's value held in a register across the
        // loop, which costs a spill in a loop that needs its registers; memory holds that value, since nothing in the
        // loop stores to the key and another thread's write in that time would race with the write-back itself.
        const bool always = (work->options->mutate & BUSTER_IR_STORE_MOTION_MUTATE_SINK_ALWAYS) != 0;
        for (uint32_t m = 0; m < work->move_count; m += 1)
        {
            const IrStoreMotionMove* move = &work->moves[m];
            const uint8_t compared = ir_store_motion_compared(move->type);
            const uint8_t conversion = move->type == IR_F32 || move->type == IR_F64 ? IR_BITCAST : IR_ZEXT;
            const uint32_t slot_address = move->position + move->chain_count + 1;
            for (uint32_t e = move->exit_first; e < move->exit_first + move->exit_count; e += 1)
            {
                const uint32_t at = work->exit_positions[e];
                view.op[at] = IR_LOAD;
                view.type[at] = move->type;
                view.alignment_log2[at] = (uint8_t)__builtin_ctz(ir_store_motion_bytes(move->type));
                view.a[at] = slot_address;
                view.op[at + 1] = IR_LOAD;
                view.type[at + 1] = move->type;
                view.alignment_log2[at + 1] = move->alignment_log2;
                view.a[at + 1] = move->key_address;
                uint32_t next = at + 2;
                uint32_t left = at;
                uint32_t right = at + 1;
                if (compared != IR_VOID)
                {
                    view.op[next] = conversion;
                    view.type[next] = compared;
                    view.a[next] = at;
                    view.op[next + 1] = conversion;
                    view.type[next + 1] = compared;
                    view.a[next + 1] = at + 1;
                    left = next;
                    right = next + 1;
                    next += 2;
                }
                view.op[next] = IR_ICMP;
                view.type[next] = IR_I1;
                view.a[next] = left;
                view.b[next] = right;
                view.c[next] = IR_NE;
                view.op[next + 1] = IR_STACK_ADDR;
                view.type[next + 1] = IR_PTR;
                view.a[next + 1] = dummy;
                view.op[next + 2] = IR_SELECT;
                view.type[next + 2] = IR_PTR;
                view.a[next + 2] = next;
                view.b[next + 2] = move->key_address;
                view.c[next + 2] = always ? move->key_address : next + 1;
                view.op[next + 3] = IR_STORE;
                view.type[next + 3] = IR_VOID;
                view.alignment_log2[next + 3] = move->alignment_log2;
                view.a[next + 3] = next + 2;
                view.b[next + 3] = at;
            }
        }
        // Each split edge's block ends in a `br` to the exit block; the loop block's `cbr` goes to it instead, and the exit
        // block's phis take the loop block's value from it.
        for (uint32_t k = 0; k < work->split_count; k += 1)
        {
            const uint32_t block = f->block_count + k;
            const uint32_t from = work->split_from[k];
            const uint32_t to = work->split_to[k];
            const uint32_t branch = split_base[k] + split_size[k];
            view.op[branch] = IR_BR;
            view.type[branch] = IR_VOID;
            view.a[branch] = to;
            const uint32_t terminator = map[f->block_offsets[(uint64_t)from + 1] - 1];
            view.b[terminator] = view.b[terminator] == to ? block : view.b[terminator];
            view.c[terminator] = view.c[terminator] == to ? block : view.c[terminator];
            for (uint32_t phi = view.block_offsets[to]; view.op[phi] == IR_PHI; phi += 1)
            {
                for (uint32_t p = 0; p < view.b[phi]; p += 1)
                {
                    const uint32_t at = view.a[phi] + p * 2;
                    view.extra[at] = view.extra[at] == from ? block : view.extra[at];
                }
            }
        }
        ir_debug_remap(f, &view, map, NULL, NULL);
        // The taken keys' accesses: a load reads the slot; a kept store's value goes to the slot too, after it, and a
        // sunk one's store writes the slot instead of the key.
        for (uint32_t k = 0; k < work->access_count; k += 1)
        {
            if (work->owners[k] != 0)
            {
                const uint32_t i = work->accesses[k];
                const uint32_t out = map[i];
                const IrStoreMotionMove* move = &work->moves[work->owners[k] - 1];
                const uint32_t slot_address = move->position + move->chain_count + 1;
                const uint8_t slot_alignment = (uint8_t)__builtin_ctz(ir_store_motion_bytes(move->type));
                if (f->op[i] == IR_LOAD || move->sink)
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
        work->statistics.slots = slots;
        work->statistics.blocks = work->split_count;
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
        work.opaque = ir_allocate(scratch, n, sizeof(uint32_t), &ok);
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
        work.opaque_blocks = ir_allocate(scratch, words, sizeof(uint64_t), &ok);
        work.move_capacity = candidates;
        work.moves = ir_allocate(scratch, work.move_capacity, sizeof(IrStoreMotionMove), &ok);
        work.chains = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_VISITS, sizeof(uint32_t), &ok);
        work.exits = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_EXITS, sizeof(uint32_t), &ok);
        work.exit_positions = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_EXITS, sizeof(uint32_t), &ok);
        work.split_from = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_EXITS, sizeof(uint32_t), &ok);
        work.split_to = ir_allocate(scratch, (uint64_t)work.move_capacity * BUSTER_IR_STORE_MOTION_EXITS, sizeof(uint32_t), &ok);
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
        for (uint32_t e = 0; e < work.opaque_count; e += 1)
        {
            const uint32_t block = work.cfg.instruction_blocks[work.opaque[e]];
            work.opaque_blocks[block / 64] |= UINT64_C(1) << (block % 64);
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
