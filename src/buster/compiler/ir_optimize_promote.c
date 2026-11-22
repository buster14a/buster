// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's promotion of a loop's global scalars (#3309, --tier1-passes=promote). Include after ir_optimize_unroll.c,
// whose column remap (ir_unroll_remap, ir_unroll_extras) it shares, and dominators.c.
//
// ir_promote_globals takes a function before SSA promotion, its locals still frame slots, and gives a loop's plain
// accesses of a global scalar (one symbol, one constant offset, one type: a key) a frame slot of their own, which SSA
// promotion then keeps in a register for the whole loop. For a loop with a dedicated preheader P (its header's one
// outside predecessor, ending in `br`) and dedicated exits (blocks outside it whose every predecessor is inside):
//
//   P:  v0 = the key's bytes (one load); T = v0
//   loop: a load of the key reads T; a store of it writes T
//   each exit E, first: store T to (T != v0 ? the key : D)       D a frame slot no one reads; for a key the loop stores
//
// The write-back's address is a select, not a branch, and it writes the global only where its bits changed (compared
// as integers: a NaN is its own bits), so a loop that runs no iteration, or none that stores, writes D: no store is
// introduced on another thread's object (C11 5.1.2.4: a compiler may not invent a write). A loop that stored and left
// the bytes as they were writes nothing either, which is the same memory: from the preheader to the exit nothing in the
// loop synchronizes (no call, fence or atomic access), so another thread's write of the object in that time would race
// with the loop's own store. The preheader's load may be a load the loop never made; the caller keeps the pass off in a
// unit where a symbol can be weak and so have no object (an undefined weak symbol's address is 0).
//
// A key is promoted only where nothing else in the loop can read or write its bytes:
// - no call (a row call and an asm body too), fence, atomic read/modify/write, observable (volatile, atomic, segment)
//   access, masked, compressing, gathering or scattering access or indirect branch is in the loop;
// - every other plain access's address is a frame slot's, or another symbol's (distinct objects don't overlap: the
//   caller keeps the pass off in a unit with an alias, ifunc or weakref, whose two names are one object), or this
//   symbol's at a constant offset whose bytes don't meet the key's. Any other address (a pointer the loop loaded, a
//   parameter, an indexed access of the symbol) may be the key's, and refuses the loop.
// The caller also keeps the pass off for a body with a volatile access, whatever the IR's flags say (#3310), and for a
// unit with an asm statement, which may be a barrier the IR doesn't carry (an empty one with a memory clobber).
//
// Loops are taken outermost first; a key an enclosing loop promoted is a slot's in the loops inside it. A loop promotes
// at most BUSTER_IR_PROMOTE_LOOP_KEYS keys, each a value live across the whole loop.
//
// Data-parallel parts: the opcode classes and the observable accesses (a byte permute of a 128-entry table and a flag
// test per 16 instructions) and the remap of every original instruction's operands (16 at a time, ir_unroll_remap). A
// loop's key table, its exits and the address chains are the small ordered part: a loop's own accesses, each followed
// through at most BUSTER_IR_PROMOTE_CHAIN definitions.
//
// Contract: as ir_optimize_unroll's. The input is never mutated. The result is unverified IR, which promotion verifies;
// it borrows the input's tables but the slots, addresses, constants, calls and extra words, which it owns in `output`.
// Original blocks and slots keep their identities, so labels, debug regions and slot sidecars stay valid; a new
// instruction has no origin. Nothing promoted gives the input itself (result->promoted is 0). All scratch is returned; on
// failure, so is `output`'s growth.

#define BUSTER_IR_PROMOTE_LOOP_KEYS 4u
#define BUSTER_IR_PROMOTE_TABLE_KEYS 16u
#define BUSTER_IR_PROMOTE_CHAIN 16u
#define BUSTER_IR_PROMOTE_NEUTRAL 0u
#define BUSTER_IR_PROMOTE_LOAD 1u
#define BUSTER_IR_PROMOTE_STORE 2u
#define BUSTER_IR_PROMOTE_BARRIER 3u
#define BUSTER_IR_PROMOTE_SYMBOL 4u

// An address's root (ir_promote_root).
#define BUSTER_IR_PROMOTE_ROOT_UNKNOWN 0u
#define BUSTER_IR_PROMOTE_ROOT_SLOT 1u
#define BUSTER_IR_PROMOTE_ROOT_SYMBOL 2u

static const uint8_t ir_promote_kinds[BUSTER_IR_OP_TABLE] = {
    [IR_LOAD] = BUSTER_IR_PROMOTE_LOAD,
    [IR_STORE] = BUSTER_IR_PROMOTE_STORE,
    [IR_CALL] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_CALL_INDIRECT] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_FENCE] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_ATOMIC_RMW] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_MASKED_LOAD] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_MASKED_STORE] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_COMPRESS_STORE] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_GATHER] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_SCATTER] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_BR_INDIRECT] = BUSTER_IR_PROMOTE_BARRIER,
    [IR_SYMBOL_ADDR] = BUSTER_IR_PROMOTE_SYMBOL,
};

// The bytes of a type a key may have: the scalars SSA promotion keeps in a register.
static const uint8_t ir_promote_bytes[IR_TYPE_COUNT] = {
    [IR_I8] = 1,
    [IR_I16] = 2,
    [IR_I32] = 4,
    [IR_I64] = 8,
    [IR_F32] = 4,
    [IR_F64] = 8,
    [IR_PTR] = 8,
};

typedef struct IrPromoteResult IrPromoteResult;
struct IrPromoteResult
{
    IrFunction function;
    // The keys promoted, the loops they were promoted in, and the accesses that now read or write a slot.
    uint32_t promoted;
    uint32_t loops;
    uint32_t accesses;
};

// A promoted key: its loop's preheader and exits (exits[exit_first..+exit_count)), its slots (T, and F where the loop
// stores it), and the new instructions the rewrite places in the preheader, which the loop and the exits read.
typedef struct IrPromoteKey IrPromoteKey;
struct IrPromoteKey
{
    int64_t offset;
    uint32_t symbol;
    uint32_t preheader;
    uint32_t exit_first;
    uint32_t exit_count;
    uint32_t value_slot;
    uint32_t value_address;
    uint32_t initial;
    uint8_t type;
    uint8_t alignment_log2;
    bool stored;
};

// A loop's access table entry (ir_promote_loop): a candidate key, or a symbol whose bytes the loop reaches where it can't
// tell (`poisoned`: an indexed or untyped access, or two keys whose bytes meet).
typedef struct IrPromoteEntry IrPromoteEntry;
struct IrPromoteEntry
{
    int64_t offset;
    uint32_t symbol;
    uint8_t type;
    uint8_t alignment_log2;
    bool stored;
    bool poisoned;
};

typedef struct IrPromoteWork IrPromoteWork;
struct IrPromoteWork
{
    const IrFunction* function;
    IrCfg cfg;
    Dominators loops;
    uint8_t* classes;
    // Per instruction, the promoted key index plus one of an access the rewrite redirects to a slot, else 0.
    uint32_t* taken;
    // Per block, the loop index plus one that last listed it as an exit.
    uint32_t* exit_marks;
    uint32_t* exits;
    uint32_t exit_count;
    uint32_t exit_capacity;
    IrPromoteKey* keys;
    uint32_t key_count;
    uint32_t key_capacity;
    uint32_t loops_promoted;
};

// Every instruction's class (BUSTER_IR_PROMOTE_*), an observable load or store a barrier: whether the function has a
// symbol's address, without which nothing is promoted.
static bool ir_promote_classify(const IrFunction* f, uint8_t* classes)
{
    uint32_t symbols = 0;
    for (uint64_t first = 0; first < f->instruction_count; first += 16)
    {
        const uint32_t count = f->instruction_count - first < 16 ? (uint32_t)(f->instruction_count - first) : 16u;
        const __mmask16 valid = (__mmask16)((1u << count) - 1u);
        const __m128i operations = _mm_maskz_loadu_epi8(valid, f->op + first);
        const __m512i kinds = ir_lookup_bytes(ir_promote_kinds, operations);
        const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, f->flags + first));
        const __mmask16 access = _mm512_cmpge_epu32_mask(kinds, _mm512_set1_epi32(BUSTER_IR_PROMOTE_LOAD)) & _mm512_cmple_epu32_mask(kinds, _mm512_set1_epi32(BUSTER_IR_PROMOTE_STORE));
        const __mmask16 observable = access & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_OBSERVABLE));
        const __m512i classified = _mm512_mask_mov_epi32(kinds, observable, _mm512_set1_epi32(BUSTER_IR_PROMOTE_BARRIER));
        _mm_mask_storeu_epi8(classes + first, valid, _mm512_cvtepi32_epi8(classified));
        symbols |= _mm512_mask_cmpeq_epi32_mask(valid, kinds, _mm512_set1_epi32(BUSTER_IR_PROMOTE_SYMBOL));
    }
    return symbols != 0;
}

// The root of address `value`: a frame slot's, symbol *symbol's at *offset (*exact where no index or overflow made it
// unknown), or unknown. Followed through copies, conversions between pointers and 64-bit integers, IR_ADDRESS nodes and
// 64-bit additions, whose constant indices and addends move the offset.
static uint32_t ir_promote_root(const IrFunction* f, uint32_t value, uint32_t* symbol, int64_t* offset, bool* exact)
{
    uint32_t root = BUSTER_IR_PROMOTE_ROOT_UNKNOWN;
    uint32_t v = value;
    bool open = true;
    *offset = 0;
    *exact = true;
    *symbol = 0;
    for (uint32_t step = 0; open && step < BUSTER_IR_PROMOTE_CHAIN && v < f->instruction_count; step += 1)
    {
        const uint32_t op = f->op[v];
        if (op == IR_STACK_ADDR)
        {
            root = BUSTER_IR_PROMOTE_ROOT_SLOT;
            open = false;
        }
        else if (op == IR_SYMBOL_ADDR)
        {
            root = BUSTER_IR_PROMOTE_ROOT_SYMBOL;
            *symbol = f->a[v];
            open = false;
        }
        else if (op == IR_COPY || ((op == IR_BITCAST || op == IR_INTTOPTR || op == IR_PTRTOINT) && (f->type[f->a[v]] == IR_PTR || f->type[f->a[v]] == IR_I64)))
        {
            v = f->a[v];
        }
        else if (op == IR_ADDRESS || (op == IR_ADD && f->type[v] == IR_I64))
        {
            // An index or an addend that is a constant moves the offset; any other makes it unknown.
            const uint32_t index = f->b[v];
            const bool constant = index == BUSTER_IR_NONE || (index < f->instruction_count && f->op[index] == IR_CONST);
            const int64_t scale = op == IR_ADDRESS ? (int64_t)f->addresses[f->c[v]].scale : 1;
            const int64_t step = index != BUSTER_IR_NONE && constant ? (int64_t)f->constants[f->a[index]].low : 0;
            int64_t scaled = 0;
            int64_t sum = 0;
            int64_t total = 0;
            *exact = *exact && constant && !__builtin_mul_overflow(step, scale, &scaled) && !__builtin_add_overflow(*offset, scaled, &sum) &&
                     !__builtin_add_overflow(sum, op == IR_ADDRESS ? f->addresses[f->c[v]].offset : 0, &total);
            *offset = total;
            // An integer addition of a variable may leave the object (an integer is any address): unknown. An index
            // stays in its object, as C's pointer arithmetic must.
            open = op == IR_ADDRESS || constant;
            v = open ? f->a[v] : v;
        }
        else
        {
            open = false;
        }
    }
    return root;
}

// The entry of key (symbol, offset, type) in a loop's table, added where it isn't there: its index, or `limit` where
// the table is full.
static uint32_t ir_promote_entry(IrPromoteEntry* table, uint32_t* count, uint32_t limit, uint32_t symbol, int64_t offset, uint8_t type)
{
    uint32_t found = limit;
    for (uint32_t e = 0; e < *count; e += 1)
    {
        found = found == limit && table[e].symbol == symbol && table[e].offset == offset && table[e].type == type ? e : found;
    }
    if (found == limit && *count < limit)
    {
        found = *count;
        table[found] = (IrPromoteEntry){ .offset = offset, .symbol = symbol, .type = type, .alignment_log2 = UINT8_MAX };
        *count += 1;
    }
    return found;
}

static bool ir_promote_in_loop(const IrPromoteWork* work, uint32_t loop, uint32_t block)
{
    const uint64_t* body = work->loops.loop_bodies + (uint64_t)loop * work->loops.row_words;
    return block < work->cfg.block_count && ((body[block >> 6] >> (block & 63)) & 1) != 0;
}

// Loop `loop`'s decision: its preheader, its exits (appended to work->exits) and its keys (appended to work->keys, their
// accesses marked in work->taken). A loop it refuses adds nothing.
static void ir_promote_loop(IrPromoteWork* work, uint32_t loop)
{
    const IrFunction* f = work->function;
    const IrCfg* cfg = &work->cfg;
    const uint32_t header = work->loops.loop_header[loop];
    const uint64_t* body = work->loops.loop_bodies + (uint64_t)loop * work->loops.row_words;
    uint32_t preheader = BUSTER_IR_NONE;
    uint32_t outside = 0;
    for (uint32_t e = cfg->predecessor_offsets[header]; e < cfg->predecessor_offsets[header + 1]; e += 1)
    {
        const uint32_t predecessor = cfg->predecessors[e];
        const bool in = ir_promote_in_loop(work, loop, predecessor);
        outside += in ? 0u : 1u;
        preheader = in ? preheader : predecessor;
    }
    bool admitted = !work->loops.loop_irreducible[loop] && header != 0 && outside == 1 && f->op[f->block_offsets[preheader + 1] - 1] == IR_BR;
    // The exits, each once, every predecessor of each in the loop.
    const uint32_t exit_first = work->exit_count;
    for (uint64_t w = 0; admitted && w < work->loops.row_words; w += 1)
    {
        for (uint64_t bits = body[w]; admitted && bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits));
            for (uint32_t e = cfg->successor_offsets[block]; admitted && e < cfg->successor_offsets[block + 1]; e += 1)
            {
                const uint32_t exit = cfg->successors[e];
                if (!ir_promote_in_loop(work, loop, exit) && work->exit_marks[exit] != loop + 1)
                {
                    // The entry block, which no preheader dominates, can't take a write-back.
                    admitted = exit != 0 && work->exit_count < work->exit_capacity;
                    work->exit_marks[exit] = loop + 1;
                    for (uint32_t p = cfg->predecessor_offsets[exit]; admitted && p < cfg->predecessor_offsets[exit + 1]; p += 1)
                    {
                        admitted = ir_promote_in_loop(work, loop, cfg->predecessors[p]);
                    }
                    if (admitted)
                    {
                        work->exits[work->exit_count] = exit;
                        work->exit_count += 1;
                    }
                }
            }
        }
    }
    // The loop's accesses: every one a slot's, a key's or a symbol's whose bytes the table poisons.
    IrPromoteEntry table[BUSTER_IR_PROMOTE_TABLE_KEYS];
    uint32_t entries = 0;
    for (uint64_t w = 0; admitted && w < work->loops.row_words; w += 1)
    {
        for (uint64_t bits = body[w]; admitted && bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits));
            for (uint32_t i = f->block_offsets[block]; admitted && i < f->block_offsets[block + 1]; i += 1)
            {
                const uint32_t class = work->classes[i];
                admitted = class != BUSTER_IR_PROMOTE_BARRIER;
                if (admitted && (class == BUSTER_IR_PROMOTE_LOAD || class == BUSTER_IR_PROMOTE_STORE) && work->taken[i] == 0)
                {
                    uint32_t symbol = 0;
                    int64_t offset = 0;
                    bool exact = true;
                    const uint32_t root = ir_promote_root(f, f->a[i], &symbol, &offset, &exact);
                    const uint8_t type = class == BUSTER_IR_PROMOTE_LOAD ? f->type[i] : f->type[f->b[i]];
                    admitted = root != BUSTER_IR_PROMOTE_ROOT_UNKNOWN;
                    if (admitted && root == BUSTER_IR_PROMOTE_ROOT_SYMBOL)
                    {
                        const bool keyed = exact && type < IR_TYPE_COUNT && ir_promote_bytes[type] != 0;
                        // An access no key can be: its symbol's entry of type VOID, poisoned.
                        const uint32_t e = ir_promote_entry(table, &entries, BUSTER_IR_PROMOTE_TABLE_KEYS, symbol, keyed ? offset : 0, keyed ? type : (uint8_t)IR_VOID);
                        admitted = e < BUSTER_IR_PROMOTE_TABLE_KEYS;
                        if (admitted)
                        {
                            table[e].poisoned |= !keyed;
                            table[e].stored |= class == BUSTER_IR_PROMOTE_STORE;
                            table[e].alignment_log2 = f->alignment_log2[i] < table[e].alignment_log2 ? f->alignment_log2[i] : table[e].alignment_log2;
                        }
                    }
                }
            }
        }
    }
    // Two entries of one symbol whose bytes meet poison each other; a poisoned entry, its whole symbol.
    for (uint32_t e = 0; admitted && e < entries; e += 1)
    {
        for (uint32_t o = 0; o < entries; o += 1)
        {
            const bool same_symbol = o != e && table[o].symbol == table[e].symbol;
            const int64_t e_end = table[e].offset + ir_promote_bytes[table[e].type];
            const int64_t o_end = table[o].offset + ir_promote_bytes[table[o].type];
            const bool meet = table[e].offset < o_end && table[o].offset < e_end;
            table[e].poisoned |= same_symbol && (table[o].type == IR_VOID || table[e].type == IR_VOID || meet);
        }
    }
    for (uint32_t e = 0; admitted && e < entries; e += 1)
    {
        for (uint32_t o = 0; o < entries; o += 1)
        {
            table[e].poisoned |= table[o].symbol == table[e].symbol && table[o].poisoned;
        }
    }
    uint32_t chosen = 0;
    const uint32_t key_first = work->key_count;
    for (uint32_t e = 0; admitted && e < entries && chosen < BUSTER_IR_PROMOTE_LOOP_KEYS && work->key_count < work->key_capacity; e += 1)
    {
        if (!table[e].poisoned)
        {
            work->keys[work->key_count] = (IrPromoteKey){
                .offset = table[e].offset,
                .symbol = table[e].symbol,
                .preheader = preheader,
                .exit_first = exit_first,
                .exit_count = work->exit_count - exit_first,
                .type = table[e].type,
                .alignment_log2 = table[e].alignment_log2,
                .stored = table[e].stored,
            };
            work->key_count += 1;
            chosen += 1;
        }
    }
    // The chosen keys' accesses, taken.
    for (uint64_t w = 0; chosen != 0 && w < work->loops.row_words; w += 1)
    {
        for (uint64_t bits = body[w]; bits != 0; bits &= bits - 1)
        {
            const uint32_t block = (uint32_t)(w * 64 + (uint64_t)__builtin_ctzll(bits));
            for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[block + 1]; i += 1)
            {
                const uint32_t class = work->classes[i];
                if ((class == BUSTER_IR_PROMOTE_LOAD || class == BUSTER_IR_PROMOTE_STORE) && work->taken[i] == 0)
                {
                    uint32_t symbol = 0;
                    int64_t offset = 0;
                    bool exact = true;
                    const uint32_t root = ir_promote_root(f, f->a[i], &symbol, &offset, &exact);
                    const uint8_t type = class == BUSTER_IR_PROMOTE_LOAD ? f->type[i] : f->type[f->b[i]];
                    for (uint32_t k = key_first; root == BUSTER_IR_PROMOTE_ROOT_SYMBOL && exact && k < work->key_count; k += 1)
                    {
                        const IrPromoteKey* key = &work->keys[k];
                        work->taken[i] = key->symbol == symbol && key->offset == offset && key->type == type ? k + 1 : work->taken[i];
                    }
                }
            }
        }
    }
    work->loops_promoted += chosen != 0 ? 1u : 0u;
    work->exit_count = chosen != 0 ? work->exit_count : exit_first;
}

// The integer type a key's write-back compares its value as (ir_promote_rewrite): its bits, as a 32-bit integer at least.
static uint8_t ir_promote_compared(uint8_t type)
{
    return type == IR_F64 ? (uint8_t)IR_I64 : type == IR_F32 || type == IR_I8 || type == IR_I16 ? (uint8_t)IR_I32 : type;
}

// The instructions the rewrite adds: a key's in its preheader, and a stored key's write-back at each of its exits.
static uint32_t ir_promote_preheader_size(const IrPromoteKey* key)
{
    return 4u + (key->offset != 0 ? 1u : 0u);
}

static uint32_t ir_promote_exit_size(const IrPromoteKey* key)
{
    return key->stored ? 6u + (key->offset != 0 ? 1u : 0u) + (ir_promote_compared(key->type) != key->type ? 2u : 0u) : 0u;
}

// The key's address at `at` (its symbol's, and an IR_ADDRESS of its offset): the index of the address value.
static uint32_t ir_promote_put_address(IrUnrollEmit* emit, uint32_t at, const IrPromoteKey* key)
{
    ir_unroll_put(emit, at, IR_SYMBOL_ADDR, IR_PTR, key->symbol, BUSTER_IR_NONE, BUSTER_IR_NONE);
    uint32_t address = at;
    if (key->offset != 0)
    {
        emit->view->addresses[emit->address] = (IrAddress){ .offset = key->offset, .scale = 1 };
        ir_unroll_put(emit, at + 1, IR_ADDRESS, IR_PTR, at, BUSTER_IR_NONE, emit->address);
        emit->address += 1;
        address = at + 1;
    }
    return address;
}

static void ir_promote_put_access(IrUnrollEmit* emit, uint32_t at, uint32_t op, uint32_t type, uint32_t address, uint32_t value, uint8_t alignment_log2)
{
    ir_unroll_put(emit, at, op, type, address, value, BUSTER_IR_NONE);
    emit->view->alignment_log2[at] = alignment_log2;
}

static uint8_t ir_promote_log2(uint32_t bytes)
{
    return (uint8_t)(31 - __builtin_clz(bytes));
}

static IrError ir_promote_rewrite(IrPromoteWork* work, Arena* output, Arena* scratch, IrFunction* result)
{
    const IrFunction* const f = work->function;
    const uint64_t mark = output->length;
    const uint32_t count = f->instruction_count;
    const uint32_t blocks = f->block_count;
    bool ok = true;
    // Each block's additions after its phis (write-backs) and before its terminator (a preheader's loads), and the new
    // slots and addresses.
    uint32_t* heads = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* tails = ir_allocate(scratch, blocks, 4, &ok);
    // Then where each block's additions go next: its head's, after its phis, and its tail's, before its terminator.
    uint32_t* head_at = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* tail_at = ir_allocate(scratch, blocks, 4, &ok);
    uint32_t* values = ir_allocate(scratch, count, 4, &ok);
    uint32_t* map = ir_allocate(scratch, count, 4, &ok);
    IrError error = ok ? IR_OK : IR_ERROR_MEMORY;
    uint64_t slots = f->slot_count;
    uint64_t addresses = f->address_count;
    uint64_t added = 0;
    bool stores = false;
    if (error == IR_OK)
    {
        memset(heads, 0, (uint64_t)blocks * 4);
        memset(tails, 0, (uint64_t)blocks * 4);
        for (uint32_t k = 0; k < work->key_count; k += 1)
        {
            IrPromoteKey* key = &work->keys[k];
            tails[key->preheader] += ir_promote_preheader_size(key);
            added += ir_promote_preheader_size(key);
            for (uint32_t e = 0; e < key->exit_count; e += 1)
            {
                heads[work->exits[key->exit_first + e]] += ir_promote_exit_size(key);
                added += ir_promote_exit_size(key);
            }
            key->value_slot = (uint32_t)slots;
            slots += 1u;
            addresses += key->offset != 0 ? 1u + key->exit_count : 0u;
            stores |= key->stored;
        }
        // The write-backs' unread slot, one for every key.
        slots += stores ? 1u : 0u;
        error = (uint64_t)count + added < UINT32_MAX && slots < UINT32_MAX && addresses <= UINT32_MAX ? IR_OK : IR_ERROR_LIMIT;
    }
    const uint64_t total = (uint64_t)count + added;
    const uint32_t discard_slot = (uint32_t)slots - 1u;
    // The original instructions' new places: after their block's earlier additions.
    uint32_t* offsets = ir_allocate(scratch, (uint64_t)blocks + 1, 4, &ok);
    error = error == IR_OK && !ok ? IR_ERROR_MEMORY : error;
    for (uint32_t b = 0; error == IR_OK && b < blocks; b += 1)
    {
        uint32_t out = b == 0 ? 0u : offsets[b];
        offsets[b] = out;
        const uint32_t first = f->block_offsets[b];
        const uint32_t end = f->block_offsets[b + 1];
        bool heading = true;
        for (uint32_t i = first; i < end; i += 1)
        {
            if (heading && f->op[i] != IR_PHI)
            {
                head_at[b] = out;
                out += heads[b];
                heading = false;
            }
            tail_at[b] = out;
            out += i + 1 == end ? tails[b] : 0u;
            values[i] = out;
            out += 1u;
        }
        offsets[b + 1] = out;
    }
    const uint64_t extras = (uint64_t)f->extra_count + (error == IR_OK ? ir_unroll_extra_words(f, 0, count) : 0);
    error = error == IR_OK && extras > UINT32_MAX ? IR_ERROR_LIMIT : error;
    IrFunction view = *f;
    if (error == IR_OK)
    {
        view.instruction_count = (uint32_t)total;
        view.op = ir_allocate(output, total, 1, &ok);
        view.type = ir_allocate(output, total, 1, &ok);
        view.flags = ir_allocate(output, total, 1, &ok);
        view.alignment_log2 = ir_allocate(output, total, 1, &ok);
        view.a = ir_allocate(output, total, 4, &ok);
        view.b = ir_allocate(output, total, 4, &ok);
        view.c = ir_allocate(output, total, 4, &ok);
        view.block_offsets = ir_allocate(output, (uint64_t)blocks + 1, 4, &ok);
        view.extra = ir_allocate(output, extras, 4, &ok);
        view.calls = ir_allocate(output, f->call_count, sizeof(IrCall), &ok);
        view.addresses = ir_allocate(output, addresses, sizeof(IrAddress), &ok);
        view.slots = ir_allocate(output, slots, sizeof(IrSlot), &ok);
        view.slot_count = (uint32_t)slots;
        error = ok ? IR_OK : IR_ERROR_MEMORY;
    }
    if (error == IR_OK && !ir_debug_create(output, &view, f->debug))
    {
        error = IR_ERROR_MEMORY;
    }
    if (error == IR_OK)
    {
        memcpy(view.block_offsets, offsets, ((uint64_t)blocks + 1) * 4);
        if (f->extra_count != 0)
        {
            memcpy(view.extra, f->extra, (uint64_t)f->extra_count * 4);
        }
        if (f->address_count != 0)
        {
            memcpy(view.addresses, f->addresses, (uint64_t)f->address_count * sizeof(IrAddress));
        }
        if (f->slot_count != 0)
        {
            memcpy(view.slots, f->slots, (uint64_t)f->slot_count * sizeof(IrSlot));
        }
        ir_debug_remap(f, &view, values, NULL, NULL);
        memcpy(map, values, (uint64_t)count * 4);
        IrUnrollEmit emit = {
            .view = &view,
            .extra = f->extra_count,
            .call = 0,
            .constant = f->constant_count,
            .address = f->address_count,
        };
        // The original instructions, in runs whose places are consecutive.
        for (uint32_t first = 0; first < count;)
        {
            uint32_t end = first + 1;
            while (end < count && values[end] == values[end - 1] + 1u)
            {
                end += 1;
            }
            ir_unroll_remap(f, first, end - first, map, &view, values[first]);
            ir_unroll_extras(&emit, f, first, end, values[first], map, NULL);
            first = end;
        }
        // The keys' slots and preheader instructions, at each preheader's terminator in key order.
        for (uint32_t k = 0; k < work->key_count; k += 1)
        {
            IrPromoteKey* key = &work->keys[k];
            const uint32_t bytes = ir_promote_bytes[key->type];
            view.slots[key->value_slot] = (IrSlot){ .size = bytes, .alignment = bytes, .type = key->type, .flags = 0 };
            const uint32_t at = tail_at[key->preheader];
            tail_at[key->preheader] += ir_promote_preheader_size(key);
            key->value_address = at;
            ir_unroll_put(&emit, at, IR_STACK_ADDR, IR_PTR, key->value_slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
            const uint32_t global = ir_promote_put_address(&emit, at + 1, key);
            const uint32_t load = global + 1;
            ir_promote_put_access(&emit, load, IR_LOAD, key->type, global, BUSTER_IR_NONE, key->alignment_log2);
            ir_promote_put_access(&emit, load + 1, IR_STORE, IR_VOID, at, load, ir_promote_log2(bytes));
            key->initial = load;
        }
        if (stores)
        {
            view.slots[discard_slot] = (IrSlot){ .size = 8, .alignment = 8, .type = IR_VOID, .flags = BUSTER_IR_SLOT_ADDRESS_TAKEN };
        }
        // The taken accesses read and write their key's slot.
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (work->taken[i] != 0)
            {
                const IrPromoteKey* key = &work->keys[work->taken[i] - 1];
                view.a[values[i]] = key->value_address;
                view.alignment_log2[values[i]] = ir_promote_log2(ir_promote_bytes[key->type]);
            }
        }
        // The write-backs, at each exit after its phis in key order: the slot's value, to the key where its bits aren't
        // the preheader's load's, else to the unread slot.
        for (uint32_t k = 0; k < work->key_count; k += 1)
        {
            const IrPromoteKey* key = &work->keys[k];
            const uint8_t compared = ir_promote_compared(key->type);
            const uint32_t convert = key->type == IR_F32 || key->type == IR_F64 ? IR_BITCAST : IR_ZEXT;
            for (uint32_t e = 0; key->stored && e < key->exit_count; e += 1)
            {
                const uint32_t exit = work->exits[key->exit_first + e];
                const uint32_t at = head_at[exit];
                head_at[exit] += ir_promote_exit_size(key);
                ir_promote_put_access(&emit, at, IR_LOAD, key->type, key->value_address, BUSTER_IR_NONE, ir_promote_log2(ir_promote_bytes[key->type]));
                uint32_t next = at + 1;
                uint32_t final = at;
                uint32_t initial = key->initial;
                if (compared != key->type)
                {
                    ir_unroll_put(&emit, next, convert, compared, at, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    ir_unroll_put(&emit, next + 1, convert, compared, key->initial, BUSTER_IR_NONE, BUSTER_IR_NONE);
                    final = next;
                    initial = next + 1;
                    next += 2;
                }
                ir_unroll_put(&emit, next, IR_ICMP, IR_I1, final, initial, IR_NE);
                const uint32_t global = ir_promote_put_address(&emit, next + 1, key);
                ir_unroll_put(&emit, global + 1, IR_STACK_ADDR, IR_PTR, discard_slot, BUSTER_IR_NONE, BUSTER_IR_NONE);
                ir_unroll_put(&emit, global + 2, IR_SELECT, IR_PTR, next, global, global + 1);
                ir_promote_put_access(&emit, global + 3, IR_STORE, IR_VOID, global + 2, at, key->alignment_log2);
            }
        }
        view.extra_count = emit.extra;
        view.call_count = emit.call;
        view.constant_count = emit.constant;
        view.address_count = emit.address;
        view.arena_bytes = output->length - mark;
        *result = view;
    }
    if (error != IR_OK)
    {
        arena_pop_to(output, mark);
    }
    return error;
}

static __attribute__((unused)) IrCheck ir_promote_globals(const IrFunction* function, Arena* output, Arena* scratch, IrPromoteResult* result)
{
    const uint64_t om = output->length;
    const uint64_t sm = scratch->length;
    IrPromoteWork work = { .function = function };
    *result = (IrPromoteResult){ .function = *function };
    IrCheck check = ir_check(output != scratch ? IR_OK : IR_ERROR_STRUCTURE, BUSTER_IR_NONE, 0);
    const uint32_t count = function->instruction_count;
    const uint32_t blocks = function->block_count;
    bool ok = check.error == IR_OK;
    work.classes = ir_allocate(scratch, count, 1, &ok);
    check.error = check.error == IR_OK && !ok ? IR_ERROR_MEMORY : check.error;
    // A function with labels is left as it is: an indirect branch's targets are its labels' blocks.
    const bool symbols = check.error == IR_OK && count != 0 && function->label_count == 0 && ir_promote_classify(function, work.classes);
    if (check.error == IR_OK && symbols)
    {
        check.error = ir_cfg_validated(function, scratch, scratch, &work.cfg);
    }
    if (check.error == IR_OK && symbols)
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
        // The analysis lives in `output` until the decisions are made, then goes back before the rewrite.
        const DominatorStatus status = dominators_analyze(output, scratch, &cfg, dom, &work.loops);
        check.error = status == DOMINATOR_OK ? IR_OK : status == DOMINATOR_NO_MEMORY ? IR_ERROR_MEMORY : IR_ERROR_UNSUPPORTED;
    }
    const uint32_t loops = check.error == IR_OK && symbols ? work.loops.loop_count : 0;
    if (loops != 0)
    {
        work.taken = ir_allocate(scratch, count, 4, &ok);
        work.exit_marks = ir_allocate(scratch, blocks, 4, &ok);
        work.exit_capacity = work.cfg.edge_count;
        work.exits = ir_allocate(scratch, work.exit_capacity, 4, &ok);
        work.key_capacity = loops * BUSTER_IR_PROMOTE_LOOP_KEYS;
        work.keys = ir_allocate(scratch, work.key_capacity, sizeof(IrPromoteKey), &ok);
        uint32_t* depth = ir_allocate(scratch, loops, 4, &ok);
        check.error = ok ? IR_OK : IR_ERROR_MEMORY;
        uint32_t deepest = 0;
        for (uint32_t l = 0; check.error == IR_OK && l < loops; l += 1)
        {
            uint32_t d = 0;
            for (uint32_t parent = work.loops.loop_parent[l]; parent != BUSTER_DOMINATOR_NONE && d < loops; parent = work.loops.loop_parent[parent])
            {
                d += 1;
            }
            depth[l] = d;
            deepest = d > deepest ? d : deepest;
        }
        if (check.error == IR_OK)
        {
            memset(work.taken, 0, (uint64_t)count * 4);
            memset(work.exit_marks, 0, (uint64_t)blocks * 4);
            // Outermost first: a key an enclosing loop takes is a slot's inside it.
            for (uint32_t d = 0; d <= deepest; d += 1)
            {
                for (uint32_t l = 0; l < loops; l += 1)
                {
                    if (depth[l] == d)
                    {
                        ir_promote_loop(&work, l);
                    }
                }
            }
        }
    }
    arena_pop_to(output, om);
    if (check.error == IR_OK && work.key_count != 0)
    {
        IrFunction rewritten = { 0 };
        check.error = ir_promote_rewrite(&work, output, scratch, &rewritten);
        if (check.error == IR_OK)
        {
            uint32_t accesses = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                accesses += work.taken[i] != 0 ? 1u : 0u;
            }
            *result = (IrPromoteResult){
                .function = rewritten,
                .promoted = work.key_count,
                .loops = work.loops_promoted,
                .accesses = accesses,
            };
        }
    }
    // A function past the loop analysis' limits is left as it is.
    check.error = check.error == IR_ERROR_UNSUPPORTED ? IR_OK : check.error;
    if (check.error != IR_OK)
    {
        arena_pop_to(output, om);
        *result = (IrPromoteResult){ .function = *function };
    }
    arena_pop_to(scratch, sm);
    return check;
}
