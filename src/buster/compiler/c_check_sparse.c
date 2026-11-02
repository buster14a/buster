// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse token columns (#628): a value a token, BUSTER_CHECK_NONE at most tokens, for the reader's output columns that
// few tokens have an entry in (CheckInput's label_targets, control_owners, declaration_bodies and the rest). A column is
// a directory, an entry for each 16 tokens, of blocks of 16 values in a pool that the unit's columns share. Block 0 holds
// 16 NONEs and is never written, so that a directory's zero entry, as a fresh page holds it, reads NONE for its 16
// tokens: a column takes memory only where it has entries, and a unit's columns need no fill. On sqlite3.c, 12 such
// columns hold 57,349 entries, which took 34.3 MB as columns of a value a token.
//
// A read is two loads (check_sparse_get), and 16 tokens' values two blocks' (check_sparse_vector). A write at a token
// whose 16 have no block takes the pool's next (check_sparse_set); a write of NONE there takes none, as the token reads
// NONE already. The pool has room for a block for each directory entry of each column (check_sparse_reserve), which a
// write takes once at most, so it never runs out.
//
// The lanes that read a unit's deferred bodies (c_check_deferred.c) write their own tokens of the same columns at once,
// and two lanes' tokens can share 16. Where the pool is shared (CheckSparsePool.shared), a write claims the directory
// entry it finds empty (BUSTER_CHECK_SPARSE_CLAIMED), takes a block, fills it and publishes it, and a write that finds the
// entry claimed waits for the block. A read of a claimed entry reads block 0: a lane reads its own tokens alone, which it
// hasn't written while the entry is claimed, since its write would have waited for the block.

// The tokens of a directory entry's block, as a shift.
#define BUSTER_CHECK_SPARSE_SHIFT 4u
#define BUSTER_CHECK_SPARSE_TOKENS (1u << BUSTER_CHECK_SPARSE_SHIFT)
// A directory entry a write claimed, whose block it is taking and filling: without its block's bits, which are 0, so that
// a read of it reads block 0.
#define BUSTER_CHECK_SPARSE_CLAIMED 0x80000000u

typedef struct CheckSparsePool CheckSparsePool;
struct CheckSparsePool
{
    // `capacity` blocks of BUSTER_CHECK_SPARSE_TOKENS values each, 64-byte aligned; block 0 the NONEs.
    uint32_t* blocks;
    // The blocks taken, block 0 among them.
    uint32_t count;
    uint32_t capacity;
    // Whether lanes write the pool's columns at once (the deferred reader's), so that its writes take blocks atomically.
    bool shared;
};

typedef struct CheckSparseColumn CheckSparseColumn;
struct CheckSparseColumn
{
    // An entry for each 16 tokens, and one past the last 16 with an entry, which check_sparse_vector may read: its block
    // in `blocks`, 0 for none.
    uint32_t* directory;
    // The pool's blocks (CheckSparsePool.blocks).
    uint32_t* blocks;
};

// The entries of a column's directory for `count` tokens and the unit's count, a token of its own (the reader writes and
// reads every column at `count`), and the entry past them that check_sparse_vector reads.
static __attribute__((unused)) uint64_t check_sparse_slots(uint32_t count)
{
    return ((uint64_t)count + 1 + BUSTER_CHECK_SPARSE_TOKENS - 1) / BUSTER_CHECK_SPARSE_TOKENS + 1;
}

// Whether the column has storage: a null column is one a CheckInput that the reader didn't fill leaves.
static __attribute__((unused)) bool check_sparse_present(CheckSparseColumn column)
{
    return column.directory != NULL;
}

// The value at `token`, which is at most the unit's count.
static __attribute__((unused)) uint32_t check_sparse_get(CheckSparseColumn column, uint32_t token)
{
    const uint32_t block = __atomic_load_n(column.directory + (token >> BUSTER_CHECK_SPARSE_SHIFT), __ATOMIC_RELAXED) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    return column.blocks[((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT) | (token & (BUSTER_CHECK_SPARSE_TOKENS - 1))];
}

// The values of tokens `first` to `first` + 15, where `first` is at most the unit's count: the two blocks' of the directory
// entries that hold them, one where `first` starts a block, which the permute then takes alone.
static __attribute__((unused)) __m512i check_sparse_vector(CheckSparseColumn column, uint32_t first)
{
    const uint32_t* entry = column.directory + (first >> BUSTER_CHECK_SPARSE_SHIFT);
    const uint32_t low = __atomic_load_n(entry, __ATOMIC_RELAXED) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    const uint32_t high = __atomic_load_n(entry + 1, __ATOMIC_RELAXED) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    const __m512i lows = _mm512_load_si512(column.blocks + ((uint64_t)low << BUSTER_CHECK_SPARSE_SHIFT));
    const __m512i highs = _mm512_load_si512(column.blocks + ((uint64_t)high << BUSTER_CHECK_SPARSE_SHIFT));
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i indices = _mm512_add_epi32(lanes, _mm512_set1_epi32((int)(first & (BUSTER_CHECK_SPARSE_TOKENS - 1))));
    return _mm512_permutex2var_epi32(lows, indices, highs);
}

// Of the tokens `first` to `first` + 15 below `count`, which `first` is, those whose values are below `bound`, a bit each.
static __attribute__((unused)) uint32_t check_sparse_below(CheckSparseColumn column, uint32_t first, uint32_t count, uint32_t bound)
{
    const __mmask16 lanes = (__mmask16)(count - first >= BUSTER_CHECK_SPARSE_TOKENS ? 0xffffu : (1u << (count - first)) - 1);
    return _mm512_mask_cmplt_epu32_mask(lanes, check_sparse_vector(column, first), _mm512_set1_epi32((int)bound));
}

// A new block at `entry`, filled with NONEs, or the block another write published there; 0 where the pool has no room,
// which it always has (check_sparse_reserve).
static __attribute__((unused)) uint32_t check_sparse_take(CheckSparsePool* pool, uint32_t* entry)
{
    const bool shared = pool->shared;
    uint32_t found = 0;
    uint32_t block = 0;
    if (!shared || __atomic_compare_exchange_n(entry, &found, BUSTER_CHECK_SPARSE_CLAIMED, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
    {
        const uint32_t taken = shared ? __atomic_fetch_add(&pool->count, 1, __ATOMIC_RELAXED) : pool->count++;
        block = taken < pool->capacity ? taken : 0;
        if (block != 0)
        {
            _mm512_store_si512(pool->blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT), _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
        }
        __atomic_store_n(entry, block, __ATOMIC_RELEASE);
    }
    else
    {
        block = found;
        while (block == BUSTER_CHECK_SPARSE_CLAIMED)
        {
            _mm_pause();
            block = __atomic_load_n(entry, __ATOMIC_ACQUIRE);
        }
    }
    return block;
}

// Writes `value` at `token` of `column`, whose blocks are `pool`'s.
static __attribute__((unused)) void check_sparse_set(CheckSparsePool* pool, CheckSparseColumn column, uint32_t token, uint32_t value)
{
    uint32_t* entry = column.directory + (token >> BUSTER_CHECK_SPARSE_SHIFT);
    uint32_t block = pool->shared ? __atomic_load_n(entry, __ATOMIC_ACQUIRE) : *entry;
    if ((block == 0 || block == BUSTER_CHECK_SPARSE_CLAIMED) && value != BUSTER_CHECK_NONE)
    {
        block = check_sparse_take(pool, entry);
    }
    if (block != 0 && block != BUSTER_CHECK_SPARSE_CLAIMED)
    {
        pool->blocks[((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT) | (token & (BUSTER_CHECK_SPARSE_TOKENS - 1))] = value;
    }
}

// Room in `arena` for `columns` columns of `count` tokens and their pool, into `pool` and `column`s: the directories, zero
// where `zero` says the arena's pages past its length are as the kernel mapped them (arena_untouched), and the pool,
// with block 0's NONEs. Returns whether the arena had room.
static __attribute__((unused)) bool check_sparse_reserve(Arena* arena, CheckSparsePool** pool, CheckSparseColumn* column, uint32_t columns, uint32_t count, bool zero, bool shared)
{
    const uint64_t slots = check_sparse_slots(count);
    const uint64_t capacity = slots * columns + 1;
    CheckSparsePool* result = capacity <= UINT32_MAX ? arena_push(arena, sizeof(CheckSparsePool), _Alignof(CheckSparsePool)) : NULL;
    uint32_t* directories = result != NULL ? arena_push_array(arena, slots * columns, sizeof(uint32_t), 64) : NULL;
    uint32_t* blocks = directories != NULL ? arena_push_array(arena, capacity * BUSTER_CHECK_SPARSE_TOKENS, sizeof(uint32_t), 64) : NULL;
    if (blocks != NULL)
    {
        if (!zero)
        {
            memset(directories, 0, slots * columns * sizeof(uint32_t));
        }
        _mm512_store_si512(blocks, _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
        *result = (CheckSparsePool){ .blocks = blocks, .count = 1, .capacity = (uint32_t)capacity, .shared = shared };
        for (uint32_t c = 0; c < columns; c += 1)
        {
            column[c] = (CheckSparseColumn){ .directory = directories + slots * c, .blocks = blocks };
        }
        *pool = result;
    }
    return blocks != NULL;
}

// A column of `count` tokens with `values`' entries, for tokens 0 to `count` - 1, and NONE at the unit's count, in its own
// pool in `arena`: for the tests that build a CheckInput's columns from columns of a value a token. Returns a null column
// where the arena has no room.
static __attribute__((unused)) CheckSparseColumn check_sparse_from_values(Arena* arena, const uint32_t* values, uint32_t count)
{
    CheckSparsePool* pool = NULL;
    CheckSparseColumn column = { 0 };
    if (values != NULL && check_sparse_reserve(arena, &pool, &column, 1, count, false, false))
    {
        for (uint32_t token = 0; token < count; token += 1)
        {
            check_sparse_set(pool, column, token, values[token]);
        }
    }
    return column;
}

// The tokens from `begin` to `end` - 1 whose values in `column` aren't NONE, in order into `positions`, which has room
// for `end` - `begin`: how many. The vector path skips each 16 tokens without a block with one load, and tests the
// others' values 16 at a time; the scalar path reads every token's value.
static __attribute__((unused)) uint32_t check_sparse_entries(CheckSparseColumn column, uint32_t begin, uint32_t end, bool vector, uint32_t* positions)
{
    uint32_t total = 0;
    if (vector)
    {
        for (uint32_t first = begin; first < end; first = (first | (BUSTER_CHECK_SPARSE_TOKENS - 1)) + 1)
        {
            const uint32_t slot = first >> BUSTER_CHECK_SPARSE_SHIFT;
            const uint32_t block = __atomic_load_n(column.directory + slot, __ATOMIC_RELAXED) & ~BUSTER_CHECK_SPARSE_CLAIMED;
            if (block != 0)
            {
                const uint32_t start = slot << BUSTER_CHECK_SPARSE_SHIFT;
                const uint32_t stop = end - start < BUSTER_CHECK_SPARSE_TOKENS ? end - start : BUSTER_CHECK_SPARSE_TOKENS;
                const uint32_t lanes = ((stop == BUSTER_CHECK_SPARSE_TOKENS ? 0x10000u : 1u << stop) - 1) & ~((1u << (first - start)) - 1);
                const __m512i values = _mm512_load_si512(column.blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT));
                for (uint32_t set = _mm512_mask_cmpneq_epu32_mask((__mmask16)lanes, values, _mm512_set1_epi32((int)BUSTER_CHECK_NONE)); set != 0; set &= set - 1)
                {
                    positions[total] = start + (uint32_t)__builtin_ctz(set);
                    total += 1;
                }
            }
        }
    }
    else
    {
        for (uint32_t token = begin; token < end; token += 1)
        {
            positions[total] = token;
            total += check_sparse_get(column, token) != BUSTER_CHECK_NONE;
        }
    }
    return total;
}

// The values of tokens 0 to `count` - 1 of `column` into `values`: for the tests that compare a column with one of a
// value a token.
static __attribute__((unused)) void check_sparse_values(CheckSparseColumn column, uint32_t count, uint32_t* values)
{
    for (uint32_t token = 0; token < count; token += 1)
    {
        values[token] = check_sparse_get(column, token);
    }
}
