// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value numbering with memory (#2718): the optimizer's BUSTER_IR_OPTIMIZE_MEMORY, which ir_optimize.c includes and runs
// after SCCP and the identities and before dead-code removal, on the analyses it already has (CFG, uses, dominators,
// SCCP's constants). A plain load takes the value of an earlier access of the same bytes, the same type and the same
// memory: an earlier load's value, or the value an earlier store stored, where no write of that memory may come between
// on any path from the access to the load. The load forwards that value as an identity does (IrOptimizeWork.forward):
// its users read the value, and the load is gone.
//
// An access's address is a root value, an optional index value and its scale, and a constant offset, read through the
// copies, conversions between pointers and 64-bit integers, constant additions and IR_ADDRESS nodes that make it
// (ir_memory_step). Its family is (root, index, scale) and its key the family, the offset and the type. A slot's
// addresses take the slot's first IR_STACK_ADDR as their root. Two accesses of one key read and write the same bytes.
// Two keys of one family whose bytes don't meet are apart: a store to one doesn't change the other. Every other pair may
// meet, under -fno-strict-aliasing, but for a private slot: one whose address is used only as loads' and stores'
// addresses and to make those addresses (no call, return, phi, comparison, stored value or other use reaches it), which
// only its own stores write. Such a slot's accesses are of the slot's class; every other access is of general memory,
// which a call, a fence, an atomic or volatile access, a masked, compressing or scattering store, the dynamic stack's
// allocation and release, and a lifetime marker of a slot that isn't private all write. A volatile slot's access (a
// volatile or _Atomic declaration's local, BUSTER_IR_SLOT_VOLATILE) is such a write itself: it happens, and is no load's
// value.
//
// The decision walks the dominator tree in preorder, with each key's last access, entered by its value and its class's
// and its family's write counts there, and an undo log that restores the entries and the counts as the walk leaves a
// subtree (ir_memory_walk). Across a block boundary a value survives where no path from the block's immediate dominator
// to the block may write its class's memory (ir_memory_clear). The data-parallel phases before the walk (classification,
// the slots' derivation and escapes, the event lists and the families' and keys' hashes) have vector versions and exact
// scalar twins, which the caller's explicit request chooses; the addresses' chains, the grouping, the overlaps, the
// blocks' summaries and the walk are shared.

#define BUSTER_IR_MEMORY_NONE 0u
#define BUSTER_IR_MEMORY_LOAD 1u
#define BUSTER_IR_MEMORY_STORE 2u
// A write of general memory: a call, a fence, an atomic read/modify/write, a masked, compressing or scattering store,
// the dynamic stack's allocation and release, and an observable (volatile, atomic or segment) load or store.
#define BUSTER_IR_MEMORY_WRITE 3u
#define BUSTER_IR_MEMORY_LIFETIME 4u
// Bits for bits between pointers and 64-bit integers: a copy, a bitcast, inttoptr of an i64 and ptrtoint to an i64.
#define BUSTER_IR_MEMORY_THROUGH 5u
#define BUSTER_IR_MEMORY_ADDRESS 6u
// An i64 addition of a constant, or a subtraction of one.
#define BUSTER_IR_MEMORY_ADD 7u
#define BUSTER_IR_MEMORY_SUB 8u
#define BUSTER_IR_MEMORY_SLOT 9u

// The kinds by opcode, before the flags and types that refine them (ir_memory_classify_*).
static const uint8_t ir_memory_kinds[BUSTER_IR_OP_TABLE] = {
    [IR_LOAD] = BUSTER_IR_MEMORY_LOAD,
    [IR_STORE] = BUSTER_IR_MEMORY_STORE,
    [IR_CALL] = BUSTER_IR_MEMORY_WRITE,
    [IR_CALL_INDIRECT] = BUSTER_IR_MEMORY_WRITE,
    [IR_FENCE] = BUSTER_IR_MEMORY_WRITE,
    [IR_ATOMIC_RMW] = BUSTER_IR_MEMORY_WRITE,
    [IR_MASKED_STORE] = BUSTER_IR_MEMORY_WRITE,
    [IR_COMPRESS_STORE] = BUSTER_IR_MEMORY_WRITE,
    [IR_SCATTER] = BUSTER_IR_MEMORY_WRITE,
    [IR_ALLOCA] = BUSTER_IR_MEMORY_WRITE,
    [IR_STACK_RESTORE] = BUSTER_IR_MEMORY_WRITE,
    [IR_LIFETIME] = BUSTER_IR_MEMORY_LIFETIME,
    [IR_COPY] = BUSTER_IR_MEMORY_THROUGH,
    [IR_BITCAST] = BUSTER_IR_MEMORY_THROUGH,
    [IR_INTTOPTR] = BUSTER_IR_MEMORY_THROUGH,
    [IR_PTRTOINT] = BUSTER_IR_MEMORY_THROUGH,
    [IR_ADDRESS] = BUSTER_IR_MEMORY_ADDRESS,
    [IR_ADD] = BUSTER_IR_MEMORY_ADD,
    [IR_SUB] = BUSTER_IR_MEMORY_SUB,
    [IR_STACK_ADDR] = BUSTER_IR_MEMORY_SLOT,
};

// An instruction's info word: its kind, its type, whether SCCP knows it as an i64 constant, and whether its block is
// reachable.
#define BUSTER_IR_MEMORY_KIND 0xffu
#define BUSTER_IR_MEMORY_TYPE_SHIFT 8u
#define BUSTER_IR_MEMORY_CONSTANT (1u << 16)
#define BUSTER_IR_MEMORY_REACHABLE (1u << 17)
#define BUSTER_IR_MEMORY_HASH_BASIS UINT64_C(0xcbf29ce484222325)
#define BUSTER_IR_MEMORY_HASH_MULTIPLIER UINT64_C(0x9e3779b97f4a7c15)

typedef struct IrMemoryAddress IrMemoryAddress;
struct IrMemoryAddress
{
    uint32_t root;
    uint32_t index;
    uint32_t scale;
    int64_t offset;
};

// A key's last access in the walk: its value, its class's and its family's write counts after it, and its class
// kind's epoch then; `epoch` UINT32_MAX where no load takes the value (none yet, or an undef's).
typedef struct IrMemoryEntry IrMemoryEntry;
struct IrMemoryEntry
{
    uint32_t value;
    uint32_t class_count;
    uint32_t family_count;
    uint32_t epoch;
};

typedef struct IrMemoryUndo IrMemoryUndo;
struct IrMemoryUndo
{
    // The key whose entry it restores, or BUSTER_IR_NONE for the counter.
    uint32_t key;
    uint32_t* counter;
    IrMemoryEntry entry;
};

typedef struct IrMemoryFrame IrMemoryFrame;
struct IrMemoryFrame
{
    uint32_t block;
    uint32_t child;
    uint32_t mark;
    bool entered;
};

typedef struct IrMemoryWork IrMemoryWork;
struct IrMemoryWork
{
    IrOptimizeWork* ir;
    uint32_t* info;
    // The operand an address is read through (toward its root), or BUSTER_IR_NONE.
    uint32_t* follow;
    // The slot a reachable value's follow chain starts at, or BUSTER_IR_NONE.
    uint32_t* derived;
    // A slot's first IR_STACK_ADDR, its addresses' root, 1 for a private slot, and 1 for a volatile one (a volatile or
    // _Atomic declaration's, BUSTER_IR_SLOT_VOLATILE), whose accesses are observable writes.
    uint32_t* slot_first;
    uint32_t* slot_private;
    uint32_t* slot_volatile;
    // The events (the reachable blocks' accesses, writes and lifetime markers), with each block's first, and the accesses
    // (plain loads and stores) among them, in instruction order; the walk's undo records they need at most.
    uint32_t* events;
    uint32_t event_count;
    uint32_t* event_first;
    uint32_t* accesses;
    uint32_t access_count;
    uint64_t undo_bound;
    // By access: its address, class (0 general memory, 1 + s private slot s), family, key and hash.
    uint32_t* roots;
    uint32_t* indices;
    uint32_t* scales;
    int64_t* offsets;
    uint32_t* classes;
    uint32_t* families;
    uint32_t* keys;
    uint64_t* hashes;
    uint32_t family_count;
    uint32_t key_count;
    // By family: its root, index and scale; by key: its family, offset and type, whether another key of its family
    // meets its bytes, and its order by (family, offset, end).
    uint32_t* family_roots;
    uint32_t* family_indices;
    uint32_t* family_scales;
    uint32_t* key_families;
    int64_t* key_offsets;
    uint32_t* key_types;
    uint8_t* key_overlapped;
    // By block: its writes (bit 0 general memory, bit 1 a private slot's) and its clear entries (bit 0 general, bit 1
    // private: no path from its immediate dominator's end to its start writes that memory).
    uint8_t* dirty;
    uint8_t* clear;
};

// Scratch columns that are written whole before they're read: unlike ir_allocate, not zeroed.
static void* ir_memory_push(Arena* arena, uint64_t count, uint64_t bytes, bool* ok)
{
    void* result = 0;
    if (*ok && count != 0)
    {
        result = arena_push_array(arena, count, bytes, 64);
        *ok = result != 0;
    }
    return result;
}

// The bytes an access of `type` reads or writes.
static uint32_t ir_memory_bytes(uint32_t type)
{
    return ((uint32_t)ir_type_bits[type] + 7) / 8;
}

// Instruction i's kind, refined by its flags (an observable load or store is a write) and types (a conversion of
// another width, an addition of another type, is nothing here).
static uint32_t ir_memory_kind(const IrOptimizeWork* work, uint32_t i)
{
    const IrFunction* f = work->function;
    const uint32_t type = f->type[i];
    uint32_t kind = ir_memory_kinds[f->op[i]];
    kind = (kind == BUSTER_IR_MEMORY_LOAD || kind == BUSTER_IR_MEMORY_STORE) && (f->flags[i] & BUSTER_IR_OBSERVABLE) != 0 ? BUSTER_IR_MEMORY_WRITE : kind;
    kind = kind == BUSTER_IR_MEMORY_THROUGH && type != IR_PTR && type != IR_I64 ? BUSTER_IR_MEMORY_NONE : kind;
    kind = (kind == BUSTER_IR_MEMORY_ADD || kind == BUSTER_IR_MEMORY_SUB) && type != IR_I64 ? BUSTER_IR_MEMORY_NONE : kind;
    return kind;
}

// The instructions' info words, and the operands their addresses are read through: a conversion's (inttoptr's of an
// i64 alone), an IR_ADDRESS's base, an addition's other operand than its constant one and a subtraction's of a constant.
// Scalar twin.
static void ir_memory_classify_scalar(IrMemoryWork* m)
{
    const IrOptimizeWork* work = m->ir;
    const IrFunction* f = work->function;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        const uint32_t type = f->type[i];
        const bool reachable = work->dominators.rpo_index[work->cfg.instruction_blocks[i]] != BUSTER_DOMINATOR_NONE;
        const bool constant = work->states[i] == IROPT_CONSTANT && type == IR_I64;
        m->info[i] = ir_memory_kind(work, i) | type << BUSTER_IR_MEMORY_TYPE_SHIFT | (constant ? BUSTER_IR_MEMORY_CONSTANT : 0) | (reachable ? BUSTER_IR_MEMORY_REACHABLE : 0);
    }
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        const uint32_t kind = m->info[i] & BUSTER_IR_MEMORY_KIND;
        uint32_t follow = BUSTER_IR_NONE;
        if (kind == BUSTER_IR_MEMORY_THROUGH)
        {
            const bool source = f->op[i] != IR_INTTOPTR || ((m->info[f->a[i]] >> BUSTER_IR_MEMORY_TYPE_SHIFT) & 0xffu) == IR_I64;
            follow = source ? f->a[i] : BUSTER_IR_NONE;
        }
        else if (kind == BUSTER_IR_MEMORY_ADDRESS)
        {
            follow = f->a[i];
        }
        else if (kind == BUSTER_IR_MEMORY_ADD)
        {
            follow = (m->info[f->b[i]] & BUSTER_IR_MEMORY_CONSTANT) != 0 ? f->a[i] : (m->info[f->a[i]] & BUSTER_IR_MEMORY_CONSTANT) != 0 ? f->b[i] : BUSTER_IR_NONE;
        }
        else if (kind == BUSTER_IR_MEMORY_SUB)
        {
            follow = (m->info[f->b[i]] & BUSTER_IR_MEMORY_CONSTANT) != 0 ? f->a[i] : BUSTER_IR_NONE;
        }
        m->follow[i] = follow;
    }
}

#if BUSTER_HOST_VECTOR
// The vector classification, 16 instructions at a time: the kinds by a 128-byte table's lookup, refined by masks, and
// the follow operands by gathers of the operands' info words.
static void ir_memory_classify_vector(IrMemoryWork* m)
{
    const IrOptimizeWork* work = m->ir;
    const IrFunction* f = work->function;
    const uint32_t n = f->instruction_count;
    const __m512i low = _mm512_loadu_si512(ir_memory_kinds);
    const __m512i high = _mm512_loadu_si512(ir_memory_kinds + 64);
    for (uint64_t first = 0; first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        const __m128i op = _mm_maskz_loadu_epi8(valid, f->op + first);
        __m512i kind = _mm512_cvtepu8_epi32(_mm512_castsi512_si128(_mm512_permutex2var_epi8(low, _mm512_zextsi128_si512(op), high)));
        const __m512i type = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, f->type + first));
        const __m512i flags = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, f->flags + first));
        const __m512i state = _mm512_cvtepu8_epi32(_mm_maskz_loadu_epi8(valid, work->states + first));
        const __m512i block = _mm512_maskz_loadu_epi32(valid, work->cfg.instruction_blocks + first);
        const __m512i order = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), valid, block, work->dominators.rpo_index, 4);
        const __mmask16 reachable = valid & _mm512_cmpneq_epi32_mask(order, _mm512_set1_epi32((int)BUSTER_DOMINATOR_NONE));
        const __mmask16 i64 = _mm512_cmpeq_epi32_mask(type, _mm512_set1_epi32(IR_I64));
        const __mmask16 wide = i64 | _mm512_cmpeq_epi32_mask(type, _mm512_set1_epi32(IR_PTR));
        const __mmask16 constant = i64 & _mm512_cmpeq_epi32_mask(state, _mm512_set1_epi32(IROPT_CONSTANT));
        const __mmask16 access = _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_LOAD)) | _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_STORE));
        const __mmask16 observable = access & _mm512_test_epi32_mask(flags, _mm512_set1_epi32(BUSTER_IR_OBSERVABLE));
        const __mmask16 through = _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_THROUGH));
        const __mmask16 arithmetic = _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_ADD)) | _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_SUB));
        kind = _mm512_mask_mov_epi32(kind, observable, _mm512_set1_epi32(BUSTER_IR_MEMORY_WRITE));
        kind = _mm512_mask_mov_epi32(kind, (through & ~wide) | (arithmetic & ~i64), _mm512_setzero_si512());
        __m512i info = _mm512_or_si512(kind, _mm512_slli_epi32(type, BUSTER_IR_MEMORY_TYPE_SHIFT));
        info = _mm512_mask_or_epi32(info, constant, info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_CONSTANT));
        info = _mm512_mask_or_epi32(info, reachable, info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_REACHABLE));
        _mm512_mask_storeu_epi32(m->info + first, valid, info);
    }
    for (uint64_t first = 0; first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        const __m512i kind = _mm512_and_si512(_mm512_maskz_loadu_epi32(valid, m->info + first), _mm512_set1_epi32(BUSTER_IR_MEMORY_KIND));
        const __mmask16 through = valid & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_THROUGH));
        const __mmask16 address = valid & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_ADDRESS));
        const __mmask16 add = valid & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_ADD));
        const __mmask16 sub = valid & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_SUB));
        const __m512i a = _mm512_maskz_loadu_epi32(through | address | add | sub, f->a + first);
        const __m512i b = _mm512_maskz_loadu_epi32(add | sub, f->b + first);
        const __m512i a_info = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), through | add, a, m->info, 4);
        const __m512i b_info = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), add | sub, b, m->info, 4);
        const __mmask16 inttoptr = _mm_cmpeq_epi8_mask(_mm_maskz_loadu_epi8(valid, f->op + first), _mm_set1_epi8(IR_INTTOPTR));
        const __m512i a_type = _mm512_and_si512(_mm512_srli_epi32(a_info, BUSTER_IR_MEMORY_TYPE_SHIFT), _mm512_set1_epi32(0xff));
        const __mmask16 source = ~inttoptr | _mm512_cmpeq_epi32_mask(a_type, _mm512_set1_epi32(IR_I64));
        const __mmask16 b_constant = _mm512_test_epi32_mask(b_info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_CONSTANT));
        const __mmask16 a_constant = _mm512_test_epi32_mask(a_info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_CONSTANT));
        __m512i follow = _mm512_set1_epi32((int)BUSTER_IR_NONE);
        follow = _mm512_mask_mov_epi32(follow, (through & source) | address | ((add | sub) & b_constant), a);
        follow = _mm512_mask_mov_epi32(follow, add & ~b_constant & a_constant, b);
        _mm512_mask_storeu_epi32(m->follow + first, valid, follow);
    }
}
#endif

// The slot each reachable value's follow chain starts at: in reverse postorder, each one's followed operand first (it
// dominates the value). Scalar twin.
static void ir_memory_derive_scalar(IrMemoryWork* m)
{
    const IrOptimizeWork* work = m->ir;
    const IrFunction* f = work->function;
    const Dominators* d = &work->dominators;
    memset(m->derived, 0xff, (uint64_t)f->instruction_count * sizeof(uint32_t));
    for (uint32_t r = 0; r < d->reachable_count; r += 1)
    {
        const uint32_t block = d->rpo[r];
        for (uint32_t i = f->block_offsets[block]; i < f->block_offsets[(uint64_t)block + 1]; i += 1)
        {
            const uint32_t kind = m->info[i] & BUSTER_IR_MEMORY_KIND;
            m->derived[i] = kind == BUSTER_IR_MEMORY_SLOT ? f->a[i] : m->follow[i] != BUSTER_IR_NONE ? m->derived[m->follow[i]] : BUSTER_IR_NONE;
        }
    }
}

#if BUSTER_HOST_VECTOR
// The vector derivation: pointer jumping along the reachable values' follow operands until no jump moves (a reachable
// value's operand is reachable and dominates it, so no chain is a cycle), then each chain's start's slot.
static bool ir_memory_derive_vector(IrMemoryWork* m, Arena* scratch)
{
    const IrFunction* f = m->ir->function;
    const uint32_t n = f->instruction_count;
    bool ok = true;
    uint32_t* jump = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; ok && first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        const __m512i info = _mm512_maskz_loadu_epi32(valid, m->info + first);
        const __m512i follow = _mm512_maskz_loadu_epi32(valid, m->follow + first);
        const __mmask16 linked = _mm512_test_epi32_mask(info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_REACHABLE)) &
                                 _mm512_cmpneq_epi32_mask(follow, _mm512_set1_epi32((int)BUSTER_IR_NONE));
        _mm512_mask_storeu_epi32(jump + first, valid, _mm512_mask_mov_epi32(_mm512_add_epi32(_mm512_set1_epi32((int)first), lanes), linked, follow));
    }
    bool changed = ok;
    while (changed)
    {
        changed = false;
        for (uint64_t first = 0; first < n; first += 16)
        {
            const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
            const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
            const __m512i to = _mm512_maskz_loadu_epi32(valid, jump + first);
            const __m512i further = _mm512_mask_i32gather_epi32(to, valid, to, jump, 4);
            const __mmask16 moved = valid & _mm512_cmpneq_epi32_mask(further, to);
            _mm512_mask_storeu_epi32(jump + first, moved, further);
            changed = changed || moved != 0;
        }
    }
    for (uint64_t first = 0; ok && first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        const __mmask16 reachable = valid & _mm512_test_epi32_mask(_mm512_maskz_loadu_epi32(valid, m->info + first), _mm512_set1_epi32((int)BUSTER_IR_MEMORY_REACHABLE));
        const __m512i start = _mm512_maskz_loadu_epi32(valid, jump + first);
        const __m512i start_info = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), reachable, start, m->info, 4);
        const __mmask16 slot = reachable & _mm512_cmpeq_epi32_mask(_mm512_and_si512(start_info, _mm512_set1_epi32(BUSTER_IR_MEMORY_KIND)), _mm512_set1_epi32(BUSTER_IR_MEMORY_SLOT));
        _mm512_mask_storeu_epi32(m->derived + first, valid, _mm512_mask_i32gather_epi32(_mm512_set1_epi32((int)BUSTER_IR_NONE), slot, start, f->a, 4));
    }
    return ok;
}
#endif

// Whether instruction i keeps a slot's address at operand `position` (0 to 2: a, b, c) private: a plain load's or
// store's address, or the operand its own address is read through.
static bool ir_memory_kept(const IrMemoryWork* m, uint32_t i, uint32_t position)
{
    const IrFunction* f = m->ir->function;
    const uint32_t kind = m->info[i] & BUSTER_IR_MEMORY_KIND;
    const uint32_t followed = m->follow[i] == BUSTER_IR_NONE ? 3u : m->follow[i] == f->a[i] ? 0u : 1u;
    return (position == 0 && (kind == BUSTER_IR_MEMORY_LOAD || kind == BUSTER_IR_MEMORY_STORE)) || position == followed;
}

// The uses of a derived value `v` that escape its slot: a reachable user that doesn't keep it at its operand's position
// (ir_memory_kept); a phi's incoming value, a call's argument and the extra array's value (positions 3 and past) keep
// nothing.
static void ir_memory_escape_uses(IrMemoryWork* m, uint32_t v)
{
    const IrUses* uses = &m->ir->uses;
    bool escaped = false;
    for (uint32_t use = uses->offsets[v]; use < uses->offsets[(uint64_t)v + 1]; use += 1)
    {
        const uint32_t user = uses->uses[use].instruction;
        const uint32_t position = uses->uses[use].operand;
        escaped = escaped || ((m->info[user] & BUSTER_IR_MEMORY_REACHABLE) != 0 && !(position < 3 && ir_memory_kept(m, user, position)));
    }
    if (escaped)
    {
        m->slot_private[m->derived[v]] = 0;
    }
}

// The slots whose addresses escape, from the uses of the values derived from them, which are few. Scalar twin.
static void ir_memory_escapes_scalar(IrMemoryWork* m)
{
    for (uint32_t v = 0; v < m->ir->function->instruction_count; v += 1)
    {
        if (m->derived[v] != BUSTER_IR_NONE)
        {
            ir_memory_escape_uses(m, v);
        }
    }
}

#if BUSTER_HOST_VECTOR
// The vector escapes: the derived values found 16 at a time, by a mask of their slots.
static void ir_memory_escapes_vector(IrMemoryWork* m)
{
    const uint32_t n = m->ir->function->instruction_count;
    for (uint64_t first = 0; first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        uint32_t derived = valid & _mm512_cmpneq_epi32_mask(_mm512_maskz_loadu_epi32(valid, m->derived + first), _mm512_set1_epi32((int)BUSTER_IR_NONE));
        while (derived != 0)
        {
            ir_memory_escape_uses(m, (uint32_t)first + (uint32_t)__builtin_ctz(derived));
            derived &= derived - 1;
        }
    }
}
#endif

// An event's undo records in the walk: three for a store (its class's and family's counts and its key's entry), one for
// any other.
static uint32_t ir_memory_undos(uint32_t kind)
{
    return kind == BUSTER_IR_MEMORY_STORE ? 3u : 1u;
}

// The events and accesses, each block's first event, every slot's first IR_STACK_ADDR and the undo bound; a volatile
// slot's access becomes a write. Scalar twin.
static void ir_memory_events_scalar(IrMemoryWork* m)
{
    const IrFunction* f = m->ir->function;
    for (uint32_t i = 0; i < f->instruction_count; i += 1)
    {
        uint32_t info = m->info[i];
        uint32_t kind = info & BUSTER_IR_MEMORY_KIND;
        const bool reachable = (info & BUSTER_IR_MEMORY_REACHABLE) != 0;
        // A volatile slot's access is an observable write.
        const uint32_t slot = reachable && (kind == BUSTER_IR_MEMORY_LOAD || kind == BUSTER_IR_MEMORY_STORE) ? m->derived[f->a[i]] : BUSTER_IR_NONE;
        if (slot != BUSTER_IR_NONE && m->slot_volatile[slot] != 0)
        {
            kind = BUSTER_IR_MEMORY_WRITE;
            info = (info & ~BUSTER_IR_MEMORY_KIND) | kind;
            m->info[i] = info;
        }
        if (kind == BUSTER_IR_MEMORY_SLOT && m->slot_first[f->a[i]] == BUSTER_IR_NONE)
        {
            m->slot_first[f->a[i]] = i;
        }
        if (reachable && kind >= BUSTER_IR_MEMORY_LOAD && kind <= BUSTER_IR_MEMORY_LIFETIME)
        {
            m->events[m->event_count] = i;
            m->event_count += 1;
            m->undo_bound += ir_memory_undos(kind);
        }
        if (reachable && (kind == BUSTER_IR_MEMORY_LOAD || kind == BUSTER_IR_MEMORY_STORE))
        {
            m->accesses[m->access_count] = i;
            m->access_count += 1;
        }
    }
}

#if BUSTER_HOST_VECTOR
// The vector events: 16 instructions at a time, a volatile slot's accesses made writes by masks, the events' and the
// accesses' indices compressed into their lists; the slots' addresses, few, set one by one.
static void ir_memory_events_vector(IrMemoryWork* m)
{
    const IrFunction* f = m->ir->function;
    const uint32_t n = f->instruction_count;
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    for (uint64_t first = 0; first < n; first += 16)
    {
        const uint32_t count = n - first < 16 ? (uint32_t)(n - first) : 16;
        const __mmask16 valid = (__mmask16)((UINT32_C(1) << count) - 1);
        __m512i info = _mm512_maskz_loadu_epi32(valid, m->info + first);
        __m512i kind = _mm512_and_si512(info, _mm512_set1_epi32(BUSTER_IR_MEMORY_KIND));
        const __mmask16 reachable = valid & _mm512_test_epi32_mask(info, _mm512_set1_epi32((int)BUSTER_IR_MEMORY_REACHABLE));
        // A volatile slot's access is an observable write: its address's slot and that slot's flag, gathered.
        const __mmask16 plain = reachable & _mm512_cmple_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_STORE)) & _mm512_cmpge_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_LOAD));
        const __m512i address = _mm512_maskz_loadu_epi32(plain, f->a + first);
        const __m512i slot = _mm512_mask_i32gather_epi32(_mm512_set1_epi32((int)BUSTER_IR_NONE), plain, address, m->derived, 4);
        const __mmask16 slotted = plain & _mm512_cmpneq_epi32_mask(slot, _mm512_set1_epi32((int)BUSTER_IR_NONE));
        const __m512i flagged = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), slotted, slot, m->slot_volatile, 4);
        const __mmask16 observable = slotted & _mm512_test_epi32_mask(flagged, flagged);
        kind = _mm512_mask_mov_epi32(kind, observable, _mm512_set1_epi32(BUSTER_IR_MEMORY_WRITE));
        info = _mm512_mask_or_epi32(info, observable, _mm512_andnot_si512(_mm512_set1_epi32(BUSTER_IR_MEMORY_KIND), info), _mm512_set1_epi32(BUSTER_IR_MEMORY_WRITE));
        _mm512_mask_storeu_epi32(m->info + first, observable, info);
        const __mmask16 event = reachable & _mm512_cmpge_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_LOAD)) &
                                _mm512_cmple_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_LIFETIME));
        const __mmask16 access = reachable & _mm512_cmple_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_STORE)) & _mm512_cmpge_epu32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_LOAD));
        const __mmask16 store = access & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_STORE));
        const __m512i index = _mm512_add_epi32(_mm512_set1_epi32((int)first), lanes);
        _mm512_mask_compressstoreu_epi32(m->events + m->event_count, event, index);
        _mm512_mask_compressstoreu_epi32(m->accesses + m->access_count, access, index);
        m->event_count += (uint32_t)__builtin_popcount(event);
        m->access_count += (uint32_t)__builtin_popcount(access);
        m->undo_bound += (uint64_t)__builtin_popcount(event) + 2u * (uint64_t)__builtin_popcount(store);
        uint32_t slots = valid & _mm512_cmpeq_epi32_mask(kind, _mm512_set1_epi32(BUSTER_IR_MEMORY_SLOT));
        while (slots != 0)
        {
            const uint32_t i = (uint32_t)first + (uint32_t)__builtin_ctz(slots);
            slots &= slots - 1;
            m->slot_first[f->a[i]] = m->slot_first[f->a[i]] == BUSTER_IR_NONE ? i : m->slot_first[f->a[i]];
        }
    }
}
#endif

// One step of an address toward its root, from its current value (state->root): through a conversion, an IR_ADDRESS
// (its displacement and its constant index's product into the offset, or its index as the address's first), or an
// addition or subtraction of a constant. Returns false where the current value is the root: no such step, a second
// index, or an offset past int64_t's range. Both versions take an address's steps by this definition.
static bool ir_memory_step(const IrMemoryWork* m, IrMemoryAddress* state)
{
    const IrOptimizeWork* work = m->ir;
    const IrFunction* f = work->function;
    const uint32_t v = state->root;
    const uint32_t kind = m->info[v] & BUSTER_IR_MEMORY_KIND;
    const uint32_t follow = m->follow[v];
    bool moved = false;
    int64_t sum = state->offset;
    if (kind == BUSTER_IR_MEMORY_THROUGH && follow != BUSTER_IR_NONE)
    {
        moved = true;
    }
    else if (kind == BUSTER_IR_MEMORY_ADDRESS)
    {
        const IrAddress address = f->addresses[f->c[v]];
        const uint32_t b = f->b[v];
        int64_t product = 0;
        if (b == BUSTER_IR_NONE)
        {
            moved = !__builtin_add_overflow(state->offset, address.offset, &sum);
        }
        else if ((m->info[b] & BUSTER_IR_MEMORY_CONSTANT) != 0)
        {
            moved = !__builtin_mul_overflow((int64_t)work->values[b].low, (int64_t)address.scale, &product) && !__builtin_add_overflow(state->offset, address.offset, &sum) &&
                    !__builtin_add_overflow(sum, product, &sum);
        }
        else if (state->index == BUSTER_IR_NONE)
        {
            moved = !__builtin_add_overflow(state->offset, address.offset, &sum);
            state->index = moved ? ir_optimize_forwarded(work, b) : state->index;
            state->scale = moved ? address.scale : state->scale;
        }
    }
    else if ((kind == BUSTER_IR_MEMORY_ADD || kind == BUSTER_IR_MEMORY_SUB) && follow != BUSTER_IR_NONE)
    {
        const uint32_t other = follow == f->a[v] ? f->b[v] : f->a[v];
        const int64_t constant = (int64_t)work->values[other].low;
        moved = kind == BUSTER_IR_MEMORY_ADD ? !__builtin_add_overflow(state->offset, constant, &sum) : !__builtin_sub_overflow(state->offset, constant, &sum);
    }
    if (moved)
    {
        state->offset = sum;
        state->root = ir_optimize_forwarded(work, kind == BUSTER_IR_MEMORY_ADDRESS ? f->a[v] : follow);
    }
    return moved;
}

// The accesses' addresses: the steps from each one's address operand (its forwarded value) to its root. A reachable
// value's operands dominate it and an identity's operand dominates it, so a chain of steps ends. Shared by both versions:
// a chain is a dependent walk of a few steps, and on the 9700X the gathers of a round of eight chains' next steps took 3.5
// times the scalar walk's time (the guide's "SIMD: what pays").
static void ir_memory_addresses(IrMemoryWork* m)
{
    const IrFunction* f = m->ir->function;
    for (uint32_t k = 0; k < m->access_count; k += 1)
    {
        IrMemoryAddress state = { .root = ir_optimize_forwarded(m->ir, f->a[m->accesses[k]]), .index = BUSTER_IR_NONE };
        while (ir_memory_step(m, &state))
        {
        }
        m->roots[k] = state.root;
        m->indices[k] = state.index;
        m->scales[k] = state.scale;
        m->offsets[k] = state.offset;
    }
}

static uint64_t ir_memory_mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * BUSTER_IR_MEMORY_HASH_MULTIPLIER;
}

static uint64_t ir_memory_family_hash(uint32_t root, uint32_t index, uint32_t scale)
{
    const uint64_t hash = ir_memory_mix(ir_memory_mix(ir_memory_mix(BUSTER_IR_MEMORY_HASH_BASIS, root), index), index == BUSTER_IR_NONE ? 0 : scale);
    return hash ^ (hash >> 32);
}

static uint64_t ir_memory_key_hash(uint32_t family, int64_t offset, uint32_t type)
{
    const uint64_t hash = ir_memory_mix(ir_memory_mix(ir_memory_mix(BUSTER_IR_MEMORY_HASH_BASIS, family), (uint64_t)offset), type);
    return hash ^ (hash >> 32);
}

// The type an access reads or writes: a load's, or a store's stored value's.
static uint32_t ir_memory_type(const IrFunction* f, uint32_t i)
{
    return f->op[i] == IR_STORE ? f->type[f->b[i]] : f->type[i];
}

// The accesses' family hashes (`keys` false) or key hashes (true, from their families). Scalar twin.
static void ir_memory_hash_scalar(IrMemoryWork* m, bool keys)
{
    const IrFunction* f = m->ir->function;
    for (uint32_t k = 0; k < m->access_count; k += 1)
    {
        m->hashes[k] = keys ? ir_memory_key_hash(m->families[k], m->offsets[k], ir_memory_type(f, m->accesses[k])) : ir_memory_family_hash(m->roots[k], m->indices[k], m->scales[k]);
    }
}

#if BUSTER_HOST_VECTOR
static __m512i ir_memory_mix8(__m512i hash, __m512i value)
{
    return _mm512_mullo_epi64(_mm512_xor_si512(hash, value), _mm512_set1_epi64((long long)BUSTER_IR_MEMORY_HASH_MULTIPLIER));
}

// The vector hashes, 8 accesses at a time, by the same mixing.
static void ir_memory_hash_vector(IrMemoryWork* m, bool keys)
{
    const IrFunction* f = m->ir->function;
    for (uint32_t first = 0; first < m->access_count; first += 8)
    {
        const uint32_t count = m->access_count - first < 8 ? m->access_count - first : 8;
        const __mmask8 lanes = (__mmask8)((UINT32_C(1) << count) - 1);
        __m512i words[3];
        if (keys)
        {
            const __m256i access = _mm256_maskz_loadu_epi32(lanes, m->accesses + first);
            const __m256i stored = _mm256_mmask_i32gather_epi32(_mm256_setzero_si256(), lanes, access, f->b, 4);
            const __m128i op = _mm256_cvtepi32_epi8(_mm256_mmask_i32gather_epi32(_mm256_setzero_si256(), lanes, access, (const int*)m->info, 4));
            const __mmask8 store = _mm_cmpeq_epi8_mask(op, _mm_set1_epi8(BUSTER_IR_MEMORY_STORE));
            const __m256i typed = _mm256_mask_blend_epi32(store, access, stored);
            const __m256i info = _mm256_mmask_i32gather_epi32(_mm256_setzero_si256(), lanes, typed, m->info, 4);
            words[0] = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, m->families + first));
            words[1] = _mm512_maskz_loadu_epi64(lanes, m->offsets + first);
            words[2] = _mm512_cvtepu32_epi64(_mm256_and_si256(_mm256_srli_epi32(info, BUSTER_IR_MEMORY_TYPE_SHIFT), _mm256_set1_epi32(0xff)));
        }
        else
        {
            const __m256i index = _mm256_maskz_loadu_epi32(lanes, m->indices + first);
            const __mmask8 indexed = _mm256_cmpneq_epi32_mask(index, _mm256_set1_epi32((int)BUSTER_IR_NONE));
            words[0] = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, m->roots + first));
            words[1] = _mm512_cvtepu32_epi64(index);
            words[2] = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes & indexed, m->scales + first));
        }
        __m512i hash = _mm512_set1_epi64((long long)BUSTER_IR_MEMORY_HASH_BASIS);
        hash = ir_memory_mix8(hash, words[0]);
        hash = ir_memory_mix8(hash, words[1]);
        hash = ir_memory_mix8(hash, words[2]);
        hash = _mm512_xor_si512(hash, _mm512_srli_epi64(hash, 32));
        _mm512_mask_storeu_epi64(m->hashes + first, lanes, hash);
    }
}
#endif

// The accesses' families (keys false) or keys (true), numbered in access order by their hashes' open-addressed table
// (`heads`, of `buckets`, a power of two), comparing whole keys. Returns the number made.
static uint32_t ir_memory_group(IrMemoryWork* m, bool keys, uint32_t* heads, uint64_t buckets)
{
    const IrFunction* f = m->ir->function;
    uint32_t made = 0;
    memset(heads, 0xff, buckets * sizeof(uint32_t));
    for (uint32_t k = 0; k < m->access_count; k += 1)
    {
        const uint32_t type = ir_memory_type(f, m->accesses[k]);
        const uint32_t scale = m->indices[k] == BUSTER_IR_NONE ? 0 : m->scales[k];
        uint64_t bucket = m->hashes[k] & (buckets - 1);
        bool found = false;
        while (!found && heads[bucket] != BUSTER_IR_NONE)
        {
            const uint32_t at = heads[bucket];
            found = keys ? m->key_families[at] == m->families[k] && m->key_offsets[at] == m->offsets[k] && m->key_types[at] == type
                         : m->family_roots[at] == m->roots[k] && m->family_indices[at] == m->indices[k] && m->family_scales[at] == scale;
            bucket = found ? bucket : (bucket + 1) & (buckets - 1);
        }
        if (!found)
        {
            heads[bucket] = made;
            if (keys)
            {
                m->key_families[made] = m->families[k];
                m->key_offsets[made] = m->offsets[k];
                m->key_types[made] = type;
            }
            else
            {
                m->family_roots[made] = m->roots[k];
                m->family_indices[made] = m->indices[k];
                m->family_scales[made] = scale;
            }
            made += 1;
        }
        if (keys)
        {
            m->keys[k] = heads[bucket];
        }
        else
        {
            m->families[k] = heads[bucket];
        }
    }
    return made;
}

// Where key k's bytes end, at most INT64_MAX.
static int64_t ir_memory_key_end(const IrMemoryWork* m, uint32_t k)
{
    int64_t end = 0;
    return __builtin_add_overflow(m->key_offsets[k], (int64_t)ir_memory_bytes(m->key_types[k]), &end) ? INT64_MAX : end;
}

// Whether key a orders before key b of the same family: by offset, then end.
static bool ir_memory_key_before(const IrMemoryWork* m, uint32_t a, uint32_t b)
{
    const int64_t end_a = ir_memory_key_end(m, a);
    const int64_t end_b = ir_memory_key_end(m, b);
    return m->key_offsets[a] != m->key_offsets[b] ? m->key_offsets[a] < m->key_offsets[b] : end_a < end_b;
}

// One family's keys `keys` (of `count`) by offset and end: an insertion sort of a few, a bottom-up merge sort through
// `spare` of more. The sorted keys end in `keys`.
static void ir_memory_sort_family(const IrMemoryWork* m, uint32_t* keys, uint32_t* spare, uint32_t count)
{
    if (count <= 16)
    {
        for (uint32_t k = 1; k < count; k += 1)
        {
            const uint32_t key = keys[k];
            uint32_t at = k;
            while (at != 0 && ir_memory_key_before(m, key, keys[at - 1]))
            {
                keys[at] = keys[at - 1];
                at -= 1;
            }
            keys[at] = key;
        }
    }
    else
    {
        uint32_t* from = keys;
        uint32_t* to = spare;
        for (uint32_t width = 1; width < count; width *= 2)
        {
            for (uint32_t left = 0; left < count; left += 2 * width)
            {
                const uint32_t middle = count - left < width ? count : left + width;
                const uint32_t end = count - left < 2 * width ? count : left + 2 * width;
                uint32_t x = left;
                uint32_t y = middle;
                for (uint32_t out = left; out < end; out += 1)
                {
                    const bool take_y = y < end && (x >= middle || ir_memory_key_before(m, from[y], from[x]));
                    to[out] = take_y ? from[y++] : from[x++];
                }
            }
            uint32_t* swap = from;
            from = to;
            to = swap;
        }
        if (from != keys)
        {
            memcpy(keys, from, (uint64_t)count * sizeof(uint32_t));
        }
    }
}

// The keys another key of their family meets: none where every family has one key; else the keys by family (a counting
// sort, in key order), each family's by offset and end, then a sweep of each with the furthest end so far and its key.
// A key that starts before that end meets that key, and so does every earlier key whose end is past its start, which
// then meets that key too and is marked already.
static bool ir_memory_overlaps(IrMemoryWork* m, Arena* scratch)
{
    bool ok = true;
    const uint32_t count = m->key_count;
    if (count != m->family_count)
    {
        uint32_t* starts = ir_allocate(scratch, (uint64_t)m->family_count + 1, sizeof(uint32_t), &ok);
        uint32_t* order = ir_memory_push(scratch, count, sizeof(uint32_t), &ok);
        uint32_t* spare = ir_memory_push(scratch, count, sizeof(uint32_t), &ok);
        for (uint32_t key = 0; ok && key < count; key += 1)
        {
            starts[m->key_families[key] + 1] += 1;
        }
        for (uint32_t family = 0; ok && family < m->family_count; family += 1)
        {
            starts[family + 1] += starts[family];
        }
        for (uint32_t key = 0; ok && key < count; key += 1)
        {
            order[starts[m->key_families[key]]] = key;
            starts[m->key_families[key]] += 1;
        }
        uint32_t first = 0;
        for (uint32_t family = 0; ok && family < m->family_count; family += 1)
        {
            const uint32_t end = starts[family];
            ir_memory_sort_family(m, order + first, spare + first, end - first);
            uint32_t furthest = BUSTER_IR_NONE;
            int64_t reach = INT64_MIN;
            for (uint32_t k = first; k < end; k += 1)
            {
                const uint32_t key = order[k];
                const int64_t key_end = ir_memory_key_end(m, key);
                if (furthest != BUSTER_IR_NONE && m->key_offsets[key] < reach)
                {
                    m->key_overlapped[key] = 1;
                    m->key_overlapped[furthest] = 1;
                }
                furthest = furthest == BUSTER_IR_NONE || key_end > reach ? key : furthest;
                reach = key_end > reach ? key_end : reach;
            }
            first = end;
        }
    }
    return ok;
}

// An access's class (0 general memory, 1 + s private slot s).
static uint32_t ir_memory_class(const IrMemoryWork* m, uint32_t i)
{
    const uint32_t slot = m->derived[m->ir->function->a[i]];
    return slot != BUSTER_IR_NONE && m->slot_private[slot] != 0 ? 1 + slot : 0;
}

// Each reachable block's writes: bit 0 for general memory (a write, a store of general memory, a marker of a slot that
// isn't private), bit 1 for a private slot's (a store to it, its marker), from its events. Shared by both versions: a
// store's class is its address's derivation (ir_memory_class).
static void ir_memory_dirty(IrMemoryWork* m)
{
    const IrFunction* f = m->ir->function;
    for (uint32_t e = 0; e < m->event_count; e += 1)
    {
        const uint32_t i = m->events[e];
        const uint32_t kind = m->info[i] & BUSTER_IR_MEMORY_KIND;
        const bool lifetime = kind == BUSTER_IR_MEMORY_LIFETIME;
        const bool store = kind == BUSTER_IR_MEMORY_STORE;
        const bool private_slot = (lifetime && m->slot_private[f->a[i]] != 0) || (store && ir_memory_class(m, i) != 0);
        const uint8_t bits = (uint8_t)((kind == BUSTER_IR_MEMORY_WRITE || ((lifetime || store) && !private_slot) ? 1u : 0u) | (private_slot ? 2u : 0u));
        m->dirty[m->ir->cfg.instruction_blocks[i]] |= bits;
    }
}

// Each reachable block's clear entries (bit 0 general memory, bit 1 the private slots'): no path from the end of its
// immediate dominator D to its start, without D again, writes that memory. Every such path ends at a predecessor P of the
// block, which D dominates, and passes through P and through each block between P and D on the dominator tree, entering
// each by a clear path: the greatest fixed point of these equations, from all clear, over the blocks with another
// predecessor than D (`joins`, in reverse postorder) until it holds; a block whose only predecessor is D is clear.
// Shared by both versions: the blocks are few.
static bool ir_memory_clear(IrMemoryWork* m, Arena* scratch)
{
    const IrOptimizeWork* work = m->ir;
    const Dominators* d = &work->dominators;
    bool ok = true;
    uint32_t* joins = ir_memory_push(scratch, d->reachable_count, sizeof(uint32_t), &ok);
    uint32_t join_count = 0;
    for (uint32_t b = 0; b < work->function->block_count; b += 1)
    {
        m->clear[b] = 3;
    }
    // Without a back edge into a join, reverse postorder has every block a join's equation reads before it: one pass.
    bool cyclic = false;
    for (uint32_t r = 1; ok && r < d->reachable_count; r += 1)
    {
        const uint32_t block = d->rpo[r];
        bool join = false;
        for (uint32_t edge = work->cfg.predecessor_offsets[block]; edge < work->cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
        {
            const uint32_t predecessor = work->cfg.predecessors[edge];
            const bool reachable = d->rpo_index[predecessor] != BUSTER_DOMINATOR_NONE;
            join = join || (predecessor != d->idom[block] && reachable);
            cyclic = cyclic || (reachable && d->rpo_index[predecessor] >= r);
        }
        joins[join_count] = block;
        join_count += join;
    }
    bool changed = ok;
    while (changed)
    {
        changed = false;
        for (uint32_t k = 0; k < join_count; k += 1)
        {
            const uint32_t block = joins[k];
            const uint32_t idom = d->idom[block];
            uint8_t value = 3;
            for (uint32_t edge = work->cfg.predecessor_offsets[block]; value != 0 && edge < work->cfg.predecessor_offsets[(uint64_t)block + 1]; edge += 1)
            {
                uint32_t y = work->cfg.predecessors[edge];
                while (value != 0 && d->rpo_index[y] != BUSTER_DOMINATOR_NONE && y != idom)
                {
                    value &= (uint8_t)(~m->dirty[y] & m->clear[y]);
                    y = d->idom[y];
                }
            }
            changed = changed || (cyclic && value != m->clear[block]);
            m->clear[block] = value;
        }
    }
    return ok;
}

static void ir_memory_count(IrMemoryUndo* undo, uint32_t* undo_count, uint32_t* counter, uint32_t value)
{
    undo[*undo_count] = (IrMemoryUndo){ .key = BUSTER_IR_NONE, .counter = counter, .entry = { .value = *counter } };
    *undo_count += 1;
    *counter = value;
}

// The decision: the dominator tree in preorder, each block's events in order (`access_first[b]` the first of block b's
// accesses among them). A write adds to general memory's
// count, a marker to its slot's class's (or general memory's), a store to its class's and its family's and enters its
// key's last access; a load takes its key's last access's value where its epoch is the current one of its class's kind
// and no write of its class came since but its own family's stores, which don't meet its bytes unless another key of
// its family does (then none came since); otherwise it enters itself. Entering a block that isn't clear for a kind
// takes a new epoch of that kind. Leaving a subtree undoes its entries and counts. A load of a block SCCP found
// unreachable takes nothing. Shared by both versions. Returns the loads forwarded.
static uint32_t ir_memory_walk(IrMemoryWork* m, IrMemoryEntry* entries, uint32_t* class_counts, uint32_t* family_counts, IrMemoryUndo* undo, IrMemoryFrame* frames,
                               const uint32_t* access_first)
{
    IrOptimizeWork* work = m->ir;
    const IrFunction* f = work->function;
    const Dominators* d = &work->dominators;
    uint32_t epochs[2] = { 0, 0 };
    uint32_t next_epoch = 1;
    uint32_t undo_count = 0;
    uint32_t forwarded = 0;
    uint32_t depth = f->block_count != 0 ? 1u : 0u;
    frames[0] = (IrMemoryFrame){ .block = 0 };
    while (depth != 0)
    {
        IrMemoryFrame* frame = &frames[depth - 1];
        if (!frame->entered)
        {
            const uint32_t block = frame->block;
            frame->entered = true;
            frame->mark = undo_count;
            frame->child = d->tree_offsets[block];
            for (uint32_t kind = 0; kind < 2; kind += 1)
            {
                if (((m->clear[block] >> kind) & 1u) == 0)
                {
                    ir_memory_count(undo, &undo_count, &epochs[kind], next_epoch);
                    next_epoch += 1;
                }
            }
            const bool executable = work->blocks[block] != 0;
            uint32_t k = access_first[block];
            for (uint32_t event = m->event_first[block]; event < m->event_first[(uint64_t)block + 1]; event += 1)
            {
                const uint32_t i = m->events[event];
                const uint32_t kind = m->info[i] & BUSTER_IR_MEMORY_KIND;
                if (kind == BUSTER_IR_MEMORY_WRITE)
                {
                    ir_memory_count(undo, &undo_count, &class_counts[0], class_counts[0] + 1);
                }
                else if (kind == BUSTER_IR_MEMORY_LIFETIME)
                {
                    const uint32_t class = m->slot_private[f->a[i]] != 0 ? 1 + f->a[i] : 0;
                    ir_memory_count(undo, &undo_count, &class_counts[class], class_counts[class] + 1);
                }
                else if (kind == BUSTER_IR_MEMORY_LOAD || kind == BUSTER_IR_MEMORY_STORE)
                {
                    const uint32_t key = m->keys[k];
                    const uint32_t family = m->families[k];
                    const uint32_t class = m->classes[k];
                    const uint32_t epoch = epochs[class != 0];
                    k += 1;
                    if (kind == BUSTER_IR_MEMORY_STORE)
                    {
                        ir_memory_count(undo, &undo_count, &class_counts[class], class_counts[class] + 1);
                        ir_memory_count(undo, &undo_count, &family_counts[family], family_counts[family] + 1);
                    }
                    const IrMemoryEntry entry = entries[key];
                    bool taken = kind == BUSTER_IR_MEMORY_LOAD && executable && entry.epoch == epoch;
                    if (taken)
                    {
                        const uint32_t writes = class_counts[class] - entry.class_count;
                        taken = m->key_overlapped[key] != 0 ? writes == 0 : writes == family_counts[family] - entry.family_count;
                    }
                    if (taken)
                    {
                        work->forward[i] = entry.value;
                        forwarded += 1;
                    }
                    else
                    {
                        const uint32_t value = kind == BUSTER_IR_MEMORY_STORE ? ir_optimize_forwarded(work, f->b[i]) : i;
                        undo[undo_count] = (IrMemoryUndo){ .key = key, .entry = entry };
                        undo_count += 1;
                        entries[key] = (IrMemoryEntry){
                            .value = value,
                            .class_count = class_counts[class],
                            .family_count = family_counts[family],
                            .epoch = f->op[value] == IR_UNDEF ? UINT32_MAX : epoch,
                        };
                    }
                }
            }
        }
        if (frame->child < d->tree_offsets[(uint64_t)frame->block + 1])
        {
            frames[depth] = (IrMemoryFrame){ .block = d->tree_children[frame->child] };
            frame->child += 1;
            depth += 1;
        }
        else
        {
            while (undo_count > frame->mark)
            {
                undo_count -= 1;
                const IrMemoryUndo record = undo[undo_count];
                if (record.key != BUSTER_IR_NONE)
                {
                    entries[record.key] = record.entry;
                }
                else
                {
                    *record.counter = record.entry.value;
                }
            }
            depth -= 1;
        }
    }
    return forwarded;
}

// Value numbering with memory on the optimizer's work, after SCCP and the identities: the loads it forwards go to
// work->forward (made here where the identities made none), their number to `forwarded`. Its other columns are scratch's
// and go before it returns. `scalar` is the caller's explicit request for the scalar twins, not the AUTO policy's size
// bound (work->scalar): this pass's vector phases have no set-up that small functions don't repay (the verifier's rule
// too, #786). Returns false where scratch has no room for them.
static bool ir_optimize_memory(IrOptimizeWork* work, Arena* scratch, bool requested_scalar, uint32_t* forwarded)
{
    const IrFunction* f = work->function;
    const uint32_t n = f->instruction_count;
    bool ok = true;
    // The identities' forwards (#2554), which may name a load this pass forwards: then the forwards are resolved again.
    const bool identities = work->forward != 0;
    if (work->forward == 0 && n != 0)
    {
        work->forward = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
        if (ok)
        {
            memset(work->forward, 0xff, (uint64_t)n * sizeof(uint32_t));
        }
    }
    const uint64_t mark = scratch->length;
    // AVX-512's gather indices are signed dwords, and the constants' and addresses' gathers double theirs: larger value
    // spaces take the scalar twin.
    const bool scalar = !BUSTER_VECTOR_PATH(!requested_scalar) || n >= (UINT32_C(1) << 30) || f->address_count >= (UINT32_C(1) << 30);
    IrMemoryWork m = { .ir = work, .undo_bound = (uint64_t)f->block_count * 2 };
    m.info = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    m.follow = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    m.derived = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    m.events = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    m.accesses = ir_memory_push(scratch, n, sizeof(uint32_t), &ok);
    m.event_first = ir_memory_push(scratch, (uint64_t)f->block_count + 1, sizeof(uint32_t), &ok);
    uint32_t* access_first = ir_memory_push(scratch, (uint64_t)f->block_count + 1, sizeof(uint32_t), &ok);
    m.slot_first = ir_memory_push(scratch, f->slot_count, sizeof(uint32_t), &ok);
    m.slot_private = ir_memory_push(scratch, f->slot_count, sizeof(uint32_t), &ok);
    m.slot_volatile = ir_memory_push(scratch, f->slot_count, sizeof(uint32_t), &ok);
    m.dirty = ir_allocate(scratch, f->block_count, sizeof(uint8_t), &ok);
    m.clear = ir_memory_push(scratch, f->block_count, sizeof(uint8_t), &ok);
    for (uint32_t s = 0; ok && s < f->slot_count; s += 1)
    {
        m.slot_first[s] = BUSTER_IR_NONE;
        m.slot_private[s] = (f->slots[s].flags & (BUSTER_IR_SLOT_VOLATILE | BUSTER_IR_SLOT_REGISTER_SAVE_AREA | BUSTER_IR_SLOT_OVERFLOW_AREA)) == 0;
        m.slot_volatile[s] = (f->slots[s].flags & BUSTER_IR_SLOT_VOLATILE) != 0;
    }
    if (ok)
    {
#if BUSTER_HOST_VECTOR
        if (!scalar)
        {
            ir_memory_classify_vector(&m);
            ok = ir_memory_derive_vector(&m, scratch);
            ir_memory_escapes_vector(&m);
            ir_memory_events_vector(&m);
        }
        else
#endif
        {
            ir_memory_classify_scalar(&m);
            ir_memory_derive_scalar(&m);
            ir_memory_escapes_scalar(&m);
            ir_memory_events_scalar(&m);
        }
    }
    // Each block's first event and first access: the lists are in instruction order, and a block's instructions are a range.
    uint32_t e = 0;
    uint32_t k = 0;
    for (uint32_t b = 0; ok && b <= f->block_count; b += 1)
    {
        const uint32_t start = f->block_offsets[b];
        while (e < m.event_count && m.events[e] < start)
        {
            e += 1;
        }
        while (k < m.access_count && m.accesses[k] < start)
        {
            k += 1;
        }
        m.event_first[b] = e;
        access_first[b] = k;
    }
    const uint32_t a = m.access_count;
    m.roots = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.indices = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.scales = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.offsets = ir_memory_push(scratch, a, sizeof(int64_t), &ok);
    m.classes = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.families = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.keys = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.hashes = ir_memory_push(scratch, a, sizeof(uint64_t), &ok);
    m.family_roots = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.family_indices = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.family_scales = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.key_families = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.key_offsets = ir_memory_push(scratch, a, sizeof(int64_t), &ok);
    m.key_types = ir_memory_push(scratch, a, sizeof(uint32_t), &ok);
    m.key_overlapped = ir_allocate(scratch, a, sizeof(uint8_t), &ok);
    uint64_t buckets = 16;
    while (buckets < (uint64_t)a * 2)
    {
        buckets *= 2;
    }
    uint32_t* heads = ir_memory_push(scratch, buckets, sizeof(uint32_t), &ok);
    if (ok)
    {
        for (uint32_t access = 0; access < a; access += 1)
        {
            m.classes[access] = ir_memory_class(&m, m.accesses[access]);
        }
        ir_memory_addresses(&m);
        for (uint32_t access = 0; access < a; access += 1)
        {
            const uint32_t root = m.roots[access];
            m.roots[access] = (m.info[root] & BUSTER_IR_MEMORY_KIND) == BUSTER_IR_MEMORY_SLOT ? m.slot_first[f->a[root]] : root;
        }
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
#if BUSTER_HOST_VECTOR
            if (!scalar)
            {
                ir_memory_hash_vector(&m, pass != 0);
            }
            else
#endif
            {
                ir_memory_hash_scalar(&m, pass != 0);
            }
            if (pass == 0)
            {
                m.family_count = ir_memory_group(&m, false, heads, buckets);
            }
            else
            {
                m.key_count = ir_memory_group(&m, true, heads, buckets);
            }
        }
        ok = ir_memory_overlaps(&m, scratch);
    }
    IrMemoryEntry* entries = ir_memory_push(scratch, m.key_count, sizeof(IrMemoryEntry), &ok);
    uint32_t* class_counts = ir_allocate(scratch, (uint64_t)f->slot_count + 1, sizeof(uint32_t), &ok);
    uint32_t* family_counts = ir_allocate(scratch, m.family_count, sizeof(uint32_t), &ok);
    IrMemoryUndo* undo = ir_memory_push(scratch, m.undo_bound, sizeof(IrMemoryUndo), &ok);
    IrMemoryFrame* frames = ir_memory_push(scratch, f->block_count, sizeof(IrMemoryFrame), &ok);
    if (ok)
    {
        for (uint32_t key = 0; key < m.key_count; key += 1)
        {
            entries[key].epoch = UINT32_MAX;
        }
        ir_memory_dirty(&m);
        ok = ir_memory_clear(&m, scratch);
    }
    if (ok)
    {
        *forwarded += ir_memory_walk(&m, entries, class_counts, family_counts, undo, frames, access_first);
        // An identity's operand may be a load forwarded since: each forward names a value that isn't forwarded.
        for (uint32_t i = 0; identities && i < n; i += 1)
        {
            uint32_t root = work->forward[i];
            while (root != BUSTER_IR_NONE && work->forward[root] != BUSTER_IR_NONE)
            {
                root = work->forward[root];
            }
            work->forward[i] = root;
        }
    }
    arena_pop_to(scratch, mark);
    return ok;
}
