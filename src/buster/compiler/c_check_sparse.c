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
// A wide column (CheckSparseWide) holds 64-bit values the same way, BUSTER_CHECK_SPARSE_WIDE_NONE at most tokens: each of
// its directory entries names two consecutive blocks of the same pool, 16 values of 8 bytes, and its zero entry blocks 0
// and 1, both NONEs. CheckInput.attributes is one (#1167), which keeps its pointers complemented, so that a null pointer
// is its NONE.
//
// The lanes that read a unit's deferred bodies (c_check_deferred.c) write their own tokens of the same columns at once,
// and two lanes' tokens can share 16. Where the pool is shared (CheckSparsePool.shared):
// - A value's write claims the directory entry it finds empty (BUSTER_CHECK_SPARSE_CLAIMED), takes a block, fills it and
//   publishes it with a release store; another value's write that finds the entry claimed waits for the block. A write of
//   NONE waits for nothing, as the entry's block, whichever, reads NONE there until a write of that token.
// - A read of a directory entry is an acquire load, so that a block it finds published reads its fill. A read of a claimed
//   entry reads block 0: a lane reads its own tokens alone, which it hasn't written while the entry is claimed, since its
//   write would have waited for the block.
// - The lanes take blocks one after another from the pool, so that two lanes' first writes can fall in one 2 MB region of
//   the blocks, which no write has given a page yet. As with the rare columns (check_input_rare_claim, #764, #847), the
//   first lane to fill a block in a region claims the region and faults it in, and every other lane that fills a block
//   there waits for that (check_sparse_region), so that no two lanes clear a huge page for it at once. The directories
//   are cleared on the reading's own thread before the lanes start (check_sparse_reserve), so their pages are in place.
// - A lane's scan of the entries in its range (check_sparse_entries, the deferred merge's) loads a block's values in its
//   range alone, as another lane writes the block's others.

// The tokens of a directory entry's block, as a shift.
#define BUSTER_CHECK_SPARSE_SHIFT 4u
#define BUSTER_CHECK_SPARSE_TOKENS (1u << BUSTER_CHECK_SPARSE_SHIFT)
// A directory entry a write claimed, whose block it is taking and filling: without its block's bits, which are 0, so that
// a read of it reads block 0.
#define BUSTER_CHECK_SPARSE_CLAIMED 0x80000000u

typedef struct CheckSparsePool CheckSparsePool;
struct CheckSparsePool
{
    // `capacity` blocks of BUSTER_CHECK_SPARSE_TOKENS values each, 64-byte aligned; block 0 the NONEs, and block 1 too
    // where the pool has wide columns.
    uint32_t* blocks;
    // The blocks taken, the NONEs' among them.
    uint32_t count;
    // At most BUSTER_CHECK_SPARSE_CLAIMED, so that no block's index has the claim's bit.
    uint32_t capacity;
    // Where the pool is shared, a word per 2 MB region of `blocks`, from the region of block 0's: 0 unclaimed, 1 being
    // faulted in, 2 in place (check_sparse_region). Null otherwise.
    uint32_t* regions;
    // Whether lanes write the pool's columns at once (the deferred reader's), so that its writes take blocks atomically.
    bool shared;
    // Whether a write found no block to take, which the reservation's capacity rules out: the reader reports it as a
    // resource failure (check_input_run) rather than lose the value.
    bool exhausted;
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

// A wide column's value where it has none.
#define BUSTER_CHECK_SPARSE_WIDE_NONE UINT64_MAX

typedef struct CheckSparseWide CheckSparseWide;
struct CheckSparseWide
{
    // As a CheckSparseColumn's, but each entry names the first of the two blocks that hold its 16 tokens' values.
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
    const uint32_t block = __atomic_load_n(column.directory + (token >> BUSTER_CHECK_SPARSE_SHIFT), __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    return column.blocks[((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT) | (token & (BUSTER_CHECK_SPARSE_TOKENS - 1))];
}

// Whether the wide column has storage, as check_sparse_present.
static __attribute__((unused)) bool check_sparse_wide_present(CheckSparseWide column)
{
    return column.directory != NULL;
}

// The wide column's value at `token`, which is at most the unit's count.
static __attribute__((unused)) uint64_t check_sparse_wide_get(CheckSparseWide column, uint32_t token)
{
    const uint32_t block = __atomic_load_n(column.directory + (token >> BUSTER_CHECK_SPARSE_SHIFT), __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    const uint64_t* values = (const uint64_t*)(column.blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT));
    return values[token & (BUSTER_CHECK_SPARSE_TOKENS - 1)];
}

// The values of tokens `first` to `first` + 15, where `first` is at most the unit's count: the two blocks' of the directory
// entries that hold them, one where `first` starts a block, which the permute then takes alone.
static __attribute__((unused)) __m512i check_sparse_vector(CheckSparseColumn column, uint32_t first)
{
    const uint32_t* entry = column.directory + (first >> BUSTER_CHECK_SPARSE_SHIFT);
    const uint32_t low = __atomic_load_n(entry, __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
    const uint32_t high = __atomic_load_n(entry + 1, __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
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

// Before a shared pool's first fill of `block`: the first lane to fill a block in the block's 2 MB region claims the
// region (CheckSparsePool.regions), faults it in with a store into its own block, which no other lane writes, and every
// other lane that fills a block there waits for that. A region holds a page once a fill wrote it, and its claim then costs
// a load.
static __attribute__((unused)) void check_sparse_region(CheckSparsePool* pool, uint32_t* block)
{
    uint32_t* const claim = pool->regions + (((uintptr_t)block / BUSTER_ARENA_HUGE_PAGE_BYTES) - ((uintptr_t)pool->blocks / BUSTER_ARENA_HUGE_PAGE_BYTES));
    uint32_t state = __atomic_load_n(claim, __ATOMIC_ACQUIRE);
    if (state == 0 && __atomic_compare_exchange_n(claim, &state, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
    {
        *(volatile uint32_t*)block = BUSTER_CHECK_NONE;
        __atomic_store_n(claim, 2, __ATOMIC_RELEASE);
        state = 2;
    }
    while (state != 2)
    {
        _mm_pause();
        state = __atomic_load_n(claim, __ATOMIC_ACQUIRE);
    }
}

// `width` new blocks at `entry` (1, or a wide column's 2), filled with NONEs, or the blocks another write published there;
// 0 where the pool has no room, which the reservation rules out (check_sparse_reserve_wide), with the pool marked
// exhausted.
static __attribute__((unused)) uint32_t check_sparse_take(CheckSparsePool* pool, uint32_t* entry, uint32_t width)
{
    const bool shared = pool->shared;
    uint32_t found = 0;
    uint32_t block = 0;
    if (!shared || __atomic_compare_exchange_n(entry, &found, BUSTER_CHECK_SPARSE_CLAIMED, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
    {
        const uint32_t taken = shared ? __atomic_fetch_add(&pool->count, width, __ATOMIC_RELAXED) : (pool->count += width) - width;
        block = taken <= pool->capacity - width ? taken : 0;
        for (uint32_t b = 0; block != 0 && b < width; b += 1)
        {
            uint32_t* const values = pool->blocks + ((uint64_t)(block + b) << BUSTER_CHECK_SPARSE_SHIFT);
            if (shared)
            {
                check_sparse_region(pool, values);
            }
            _mm512_store_si512(values, _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
        }
        if (block == 0)
        {
            __atomic_store_n(&pool->exhausted, true, __ATOMIC_RELAXED);
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
        block = check_sparse_take(pool, entry, 1);
    }
    if (block != 0 && block != BUSTER_CHECK_SPARSE_CLAIMED)
    {
        pool->blocks[((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT) | (token & (BUSTER_CHECK_SPARSE_TOKENS - 1))] = value;
    }
}

// Writes `value` at `token` of the wide `column`, whose blocks are `pool`'s, as check_sparse_set.
static __attribute__((unused)) void check_sparse_wide_set(CheckSparsePool* pool, CheckSparseWide column, uint32_t token, uint64_t value)
{
    uint32_t* entry = column.directory + (token >> BUSTER_CHECK_SPARSE_SHIFT);
    uint32_t block = pool->shared ? __atomic_load_n(entry, __ATOMIC_ACQUIRE) : *entry;
    if ((block == 0 || block == BUSTER_CHECK_SPARSE_CLAIMED) && value != BUSTER_CHECK_SPARSE_WIDE_NONE)
    {
        block = check_sparse_take(pool, entry, 2);
    }
    if (block != 0 && block != BUSTER_CHECK_SPARSE_CLAIMED)
    {
        uint64_t* values = (uint64_t*)(pool->blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT));
        values[token & (BUSTER_CHECK_SPARSE_TOKENS - 1)] = value;
    }
}

// Room in `arena` for `columns` columns and `wides` wide columns of `count` tokens and their pool, into `pool`, `column`s
// and `wide`s: the directories, and the pool, with its NONEs' blocks, and with `shared` its regions' claims. The
// directories are cleared but where `zero` says the arena's pages past its length are as the kernel mapped them
// (arena_untouched) and the pool isn't `shared`: a shared pool's are cleared on this thread, so that the lanes find their
// pages in place. The pool has blocks for each directory entry of each column, which a write takes once at most, so it
// never runs out. Returns whether the arena had room.
static __attribute__((unused)) bool check_sparse_reserve_wide(Arena* arena, CheckSparsePool** pool, CheckSparseColumn* column, uint32_t columns, CheckSparseWide* wide, uint32_t wides, uint32_t count, bool zero, bool shared)
{
    const uint64_t slots = check_sparse_slots(count);
    const uint64_t entries = slots * ((uint64_t)columns + wides);
    // A wide column's zero entry reads blocks 0 and 1.
    const uint32_t nones = wides != 0 ? 2 : 1;
    const uint64_t capacity = slots * ((uint64_t)columns + 2 * (uint64_t)wides) + nones;
    CheckSparsePool* result = capacity <= BUSTER_CHECK_SPARSE_CLAIMED ? arena_push(arena, sizeof(CheckSparsePool), _Alignof(CheckSparsePool)) : NULL;
    uint32_t* directories = result != NULL ? arena_push_array(arena, entries, sizeof(uint32_t), 64) : NULL;
    uint32_t* blocks = directories != NULL ? arena_push_array(arena, capacity * BUSTER_CHECK_SPARSE_TOKENS, sizeof(uint32_t), 64) : NULL;
    const uint64_t block_bytes = capacity * BUSTER_CHECK_SPARSE_TOKENS * sizeof(uint32_t);
    const uint64_t region_count = blocks != NULL ? ((uintptr_t)blocks + block_bytes - 1) / BUSTER_ARENA_HUGE_PAGE_BYTES - (uintptr_t)blocks / BUSTER_ARENA_HUGE_PAGE_BYTES + 1 : 0;
    uint32_t* regions = blocks != NULL && shared ? arena_push_array(arena, region_count, sizeof(uint32_t), 64) : NULL;
    const bool valid = blocks != NULL && (!shared || regions != NULL);
    if (valid)
    {
        if (!zero || shared)
        {
            memset(directories, 0, entries * sizeof(uint32_t));
        }
        if (shared)
        {
            memset(regions, 0, region_count * sizeof(uint32_t));
        }
        for (uint32_t b = 0; b < nones; b += 1)
        {
            _mm512_store_si512(blocks + b * BUSTER_CHECK_SPARSE_TOKENS, _mm512_set1_epi32((int)BUSTER_CHECK_NONE));
        }
        *result = (CheckSparsePool){ .blocks = blocks, .count = nones, .capacity = (uint32_t)capacity, .regions = regions, .shared = shared };
        for (uint32_t c = 0; c < columns; c += 1)
        {
            column[c] = (CheckSparseColumn){ .directory = directories + slots * c, .blocks = blocks };
        }
        for (uint32_t w = 0; w < wides; w += 1)
        {
            wide[w] = (CheckSparseWide){ .directory = directories + slots * ((uint64_t)columns + w), .blocks = blocks };
        }
        *pool = result;
    }
    return valid;
}

// Room for `columns` columns alone, as check_sparse_reserve_wide.
static __attribute__((unused)) bool check_sparse_reserve(Arena* arena, CheckSparsePool** pool, CheckSparseColumn* column, uint32_t columns, uint32_t count, bool zero, bool shared)
{
    return check_sparse_reserve_wide(arena, pool, column, columns, NULL, 0, count, zero, shared);
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

// A wide column of `count` tokens with `values`' entries, as check_sparse_from_values.
static __attribute__((unused)) CheckSparseWide check_sparse_wide_from_values(Arena* arena, const uint64_t* values, uint32_t count)
{
    CheckSparsePool* pool = NULL;
    CheckSparseWide column = { 0 };
    if (values != NULL && check_sparse_reserve_wide(arena, &pool, NULL, 0, &column, 1, count, false, false))
    {
        for (uint32_t token = 0; token < count; token += 1)
        {
            check_sparse_wide_set(pool, column, token, values[token]);
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
            const uint32_t block = __atomic_load_n(column.directory + slot, __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
            if (block != 0)
            {
                const uint32_t start = slot << BUSTER_CHECK_SPARSE_SHIFT;
                const uint32_t stop = end - start < BUSTER_CHECK_SPARSE_TOKENS ? end - start : BUSTER_CHECK_SPARSE_TOKENS;
                const uint32_t lanes = ((stop == BUSTER_CHECK_SPARSE_TOKENS ? 0x10000u : 1u << stop) - 1) & ~((1u << (first - start)) - 1);
                // The block's values in the range alone: a lane of the deferred merge writes the others of a block that
                // straddles its range's ends.
                const __m512i values = _mm512_maskz_load_epi32((__mmask16)lanes, column.blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT));
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

// The first token from `first`, a multiple of 16, at which 16 tokens below `end` have a block in the wide `column`, or
// a token at or past `end` where none has: the vector path tests 16 directory entries, 256 tokens, at a time. The
// scalar path, the twin of check_sparse_wide_group's, returns `first`, so that its caller visits every 16. For the scans
// on the reading's own thread: a lane may be writing the directory at once.
static __attribute__((unused)) uint32_t check_sparse_wide_next(CheckSparseWide column, uint32_t first, uint32_t end, bool vector)
{
    uint32_t slot = first >> BUSTER_CHECK_SPARSE_SHIFT;
    const uint32_t last = (uint32_t)(((uint64_t)end + BUSTER_CHECK_SPARSE_TOKENS - 1) >> BUSTER_CHECK_SPARSE_SHIFT);
    bool found = !vector;
    while (!found && slot < last)
    {
        const uint32_t entries = last - slot < 16 ? last - slot : 16;
        const __mmask16 lanes = (__mmask16)(entries == 16 ? 0xffffu : (1u << entries) - 1);
        const __m512i blocks = _mm512_maskz_loadu_epi32(lanes, column.directory + slot);
        const uint32_t taken = _mm512_mask_test_epi32_mask(lanes, blocks, _mm512_set1_epi32((int)~BUSTER_CHECK_SPARSE_CLAIMED));
        found = taken != 0;
        slot += found ? (uint32_t)__builtin_ctz(taken) : entries;
    }
    return slot << BUSTER_CHECK_SPARSE_SHIFT;
}

// Of the 16 tokens from `first`, a multiple of 16, below `end`, those whose values in the wide `column` aren't NONE, a
// bit each: the vector path tests their block pair's values 8 at a time, block 0's where they have none; the scalar
// path, its twin, reads each token's value.
static __attribute__((unused)) uint32_t check_sparse_wide_group(CheckSparseWide column, uint32_t first, uint32_t end, bool vector)
{
    const uint32_t stop = end - first < BUSTER_CHECK_SPARSE_TOKENS ? end - first : BUSTER_CHECK_SPARSE_TOKENS;
    const uint32_t lanes = (stop == BUSTER_CHECK_SPARSE_TOKENS ? 0x10000u : 1u << stop) - 1;
    uint32_t set = 0;
    if (vector)
    {
        const __m512i none = _mm512_set1_epi64((long long)BUSTER_CHECK_SPARSE_WIDE_NONE);
        const uint32_t block = __atomic_load_n(column.directory + (first >> BUSTER_CHECK_SPARSE_SHIFT), __ATOMIC_ACQUIRE) & ~BUSTER_CHECK_SPARSE_CLAIMED;
        const uint64_t* values = (const uint64_t*)(column.blocks + ((uint64_t)block << BUSTER_CHECK_SPARSE_SHIFT));
        const __m512i low = _mm512_maskz_load_epi64((__mmask8)lanes, values);
        const __m512i high = _mm512_maskz_load_epi64((__mmask8)(lanes >> 8), values + 8);
        set = (uint32_t)_mm512_mask_cmpneq_epu64_mask((__mmask8)lanes, low, none) | (uint32_t)_mm512_mask_cmpneq_epu64_mask((__mmask8)(lanes >> 8), high, none) << 8;
    }
    else
    {
        for (uint32_t t = 0; t < stop; t += 1)
        {
            set |= (uint32_t)(check_sparse_wide_get(column, first + t) != BUSTER_CHECK_SPARSE_WIDE_NONE) << t;
        }
    }
    return set;
}

// The tokens from `begin` to `end` - 1 whose values in the wide `column` aren't NONE, in order into `positions`, which
// has room for `end` - `begin`: how many, by check_sparse_wide_next's and check_sparse_wide_group's paths.
static __attribute__((unused)) uint32_t check_sparse_wide_entries(CheckSparseWide column, uint32_t begin, uint32_t end, bool vector, uint32_t* positions)
{
    uint32_t total = 0;
    const uint32_t start = begin & ~(BUSTER_CHECK_SPARSE_TOKENS - 1);
    for (uint32_t first = check_sparse_wide_next(column, start, end, vector); first < end; first = check_sparse_wide_next(column, first + BUSTER_CHECK_SPARSE_TOKENS, end, vector))
    {
        const uint32_t below = begin > first ? begin - first : 0;
        for (uint32_t set = check_sparse_wide_group(column, first, end, vector) & ~((1u << below) - 1); set != 0; set &= set - 1)
        {
            positions[total] = first + (uint32_t)__builtin_ctz(set);
            total += 1;
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
