// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent storage-contract tests for #105. Include after c_fragments.c. Expected streams are written from the
// fixture's construction order, not recovered from production headers. The ownership audit independently walks
// live roots and the free list; every allocated record must occur exactly once. All walks have explicit bounds.
// The fixtures exercise each payload size, both capacity dimensions, moved/virtual/frozen handles and poisoned
// stores. They do not test the parser reduction hooks or claim semantic-consumer equivalence.

#include <stdio.h>
#include <string.h>

#define BUSTER_C_FRAGMENT_STORE_TEST_LIMIT 16384u
#define BUSTER_C_FRAGMENT_STORE_TEST_UNARY 8192u
#define BUSTER_C_FRAGMENT_STORE_TEST_LEAVES 4096u

typedef struct CFragmentStoreTest CFragmentStoreTest;
struct CFragmentStoreTest
{
    Arena arena;
    uint8_t leaf_kinds[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT];
    uint8_t expected_kinds[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT];
    uint32_t expected_ids[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT];
    uint32_t expected_count;
    uint32_t checks;
    uint32_t failures;
    uint32_t payload;
    const char* phase;
};

static void c_fragment_store_test_check(CFragmentStoreTest* test, bool condition, const char* description)
{
    test->checks += 1;
    test->failures += !condition;
    if (!condition && test->failures <= 24)
    {
        fprintf(stderr, "fragment-store: %s, payload %u: %s\n", test->phase, test->payload, description);
    }
}

static bool c_fragment_store_test_empty(const CFragmentHandle* handle)
{
    return handle->head == BUSTER_C_FRAGMENT_NONE && handle->tail == BUSTER_C_FRAGMENT_NONE && handle->positions == 0 &&
           handle->leaf == BUSTER_C_FRAGMENT_NONE && !handle->frozen;
}

static bool c_fragment_store_test_begin(CFragmentStoreTest* test, CFragmentStore* store, uint32_t capacity, const char* phase)
{
    test->phase = phase;
    test->expected_count = 0;
    arena_pop_to(&test->arena, 0);
    uint8_t* prefix = arena_push_array(&test->arena, 1, 1, 1);
    if (prefix != 0)
    {
        *prefix = 0x7d;
    }
    const bool valid = prefix != 0 && c_fragment_store_initialize(store, &test->arena, capacity, test->payload, test->leaf_kinds, BUSTER_C_FRAGMENT_STORE_TEST_LIMIT);
    c_fragment_store_test_check(test, valid, "store setup failed");
    if (valid)
    {
        c_fragment_store_test_check(test, *prefix == 0x7d, "allocation overwrote the preceding arena byte");
        c_fragment_store_test_check(test, (uintptr_t)store->records % _Alignof(CFragmentHeader) == 0, "headers are unaligned after an odd arena offset");
        c_fragment_store_test_check(test, store->stride == test->payload + sizeof(CFragmentHeader), "physical record stride differs");
    }
    return valid;
}

static void c_fragment_store_test_expect(CFragmentStoreTest* test, uint8_t kind, uint32_t identity)
{
    const bool fits = test->expected_count < BUSTER_C_FRAGMENT_STORE_TEST_LIMIT;
    c_fragment_store_test_check(test, fits, "fixture expectation overflow");
    if (fits)
    {
        test->expected_kinds[test->expected_count] = kind;
        test->expected_ids[test->expected_count] = identity;
        test->expected_count += 1;
    }
}

static void c_fragment_store_test_expect_leaf(CFragmentStoreTest* test, uint32_t identity)
{
    c_fragment_store_test_expect(test, test->leaf_kinds[identity], identity);
}

static uint8_t c_fragment_store_test_unary_kind(uint32_t identity)
{
    return identity & 1 ? C_FRAGMENT_NEGATIVE : C_FRAGMENT_NOT;
}

// Audit all ownership, not just the chain selected for a read. This detects orphaned records, duplicate roots,
// live links into free records, missing free-list entries and cycles without using record_valid or finish.
static void c_fragment_store_test_ownership(CFragmentStoreTest* test, const CFragmentStore* store, const CFragmentHandle* roots, uint32_t root_count)
{
    uint8_t seen[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT] = { 0 };
    bool valid = store->valid && store->allocated <= store->capacity && store->allocated <= sizeof(seen);
    uint32_t live_count = 0;
    for (uint32_t root = 0; valid && root < root_count; root += 1)
    {
        const CFragmentHandle* handle = roots + root;
        uint32_t index = handle->head;
        uint32_t tail = BUSTER_C_FRAGMENT_NONE;
        uint64_t positions = 0;
        uint32_t walked = 0;
        if (index == BUSTER_C_FRAGMENT_NONE)
        {
            valid = handle->tail == BUSTER_C_FRAGMENT_NONE && handle->positions <= 1 &&
                    (handle->positions == 0 ? handle->leaf == BUSTER_C_FRAGMENT_NONE : handle->leaf < store->leaf_count);
        }
        while (valid && index != BUSTER_C_FRAGMENT_NONE)
        {
            valid = index < store->allocated && walked < store->allocated;
            if (valid)
            {
                valid = seen[index] == 0 && store->live[index] == 1;
            }
            if (valid)
            {
                const CFragmentHeader* fragment = c_fragment_header(store, index);
                uint32_t operators = 0;
                valid = fragment->used > 0 && fragment->used <= BUSTER_C_FRAGMENT_POSITIONS;
                for (uint32_t bit = 0; bit < BUSTER_C_FRAGMENT_POSITIONS; bit += 1)
                {
                    const uint32_t set = (uint32_t)((fragment->operator_mask >> bit) & 1);
                    operators += set;
                    valid &= bit < fragment->used || set == 0;
                }
                valid &= operators == fragment->operator_count && operators <= store->payload;
                seen[index] = 1;
                live_count += 1;
                positions += fragment->used;
                walked += 1;
                tail = index;
                index = fragment->next;
            }
        }
        if (handle->head != BUSTER_C_FRAGMENT_NONE)
        {
            valid &= tail == handle->tail && positions == handle->positions && handle->leaf == BUSTER_C_FRAGMENT_NONE;
        }
    }
    uint32_t free_index = store->free_head;
    uint32_t free_count = 0;
    while (valid && free_index != BUSTER_C_FRAGMENT_NONE)
    {
        valid = free_index < store->allocated && free_count < store->allocated;
        if (valid)
        {
            valid = seen[free_index] == 0 && store->live[free_index] == 0;
        }
        if (valid)
        {
            seen[free_index] = 2;
            free_index = store->free_next[free_index];
            free_count += 1;
        }
    }
    valid &= live_count == store->live_count && (uint64_t)live_count + free_count == store->allocated;
    for (uint32_t i = 0; valid && i < store->allocated; i += 1)
    {
        valid = seen[i] != 0;
    }
    c_fragment_store_test_check(test, valid, "live chains and free list do not partition allocated records");
}

static void c_fragment_store_test_windows(CFragmentStoreTest* test, const CFragmentStore* store, const CFragmentHandle* handle, bool vector)
{
    CFragmentCursor cursor = c_fragment_cursor(handle);
    uint32_t written = 0;
    uint32_t calls = 0;
    bool valid = true;
    do
    {
        uint8_t kinds[BUSTER_C_FRAGMENT_POSITIONS + 2];
        uint32_t identities[BUSTER_C_FRAGMENT_POSITIONS + 2];
        memset(kinds, 0xa5, sizeof(kinds));
        memset(identities, 0xa5, sizeof(identities));
        uint32_t count = UINT32_MAX;
        valid = c_fragment_window(store, &cursor, kinds + 1, identities + 1, &count, vector);
        valid &= count <= BUSTER_C_FRAGMENT_POSITIONS && count <= test->expected_count - written;
        if (valid)
        {
            valid = memcmp(kinds + 1, test->expected_kinds + written, count) == 0 &&
                    memcmp(identities + 1, test->expected_ids + written, (uint64_t)count * sizeof(uint32_t)) == 0;
            valid &= kinds[0] == 0xa5 && identities[0] == UINT32_C(0xa5a5a5a5);
            for (uint32_t i = count + 1; i < BUSTER_C_FRAGMENT_POSITIONS + 2; i += 1)
            {
                valid &= kinds[i] == 0xa5 && identities[i] == UINT32_C(0xa5a5a5a5);
            }
            written += count;
        }
        calls += 1;
        valid &= calls <= store->allocated + 1;
    } while (valid && cursor.remaining != 0);
    valid &= written == test->expected_count;
    c_fragment_store_test_check(test, valid, vector ? "direct vector windows differ or overwrite tails" : "direct scalar windows differ or overwrite tails");
}

static void c_fragment_store_test_verify(CFragmentStoreTest* test, CFragmentStore* store, CFragmentHandle* handle)
{
    c_fragment_store_test_ownership(test, store, handle, 1);
    c_fragment_finish(store, handle);
    const bool ready = store->valid && handle->frozen && handle->positions == test->expected_count;
    c_fragment_store_test_check(test, ready, "finished stream has wrong length or status");
    if (ready)
    {
        uint8_t kinds[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT + 2];
        uint32_t identities[BUSTER_C_FRAGMENT_STORE_TEST_LIMIT + 2];
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            memset(kinds, 0xa5, sizeof(kinds));
            memset(identities, 0xa5, sizeof(identities));
            const bool read = c_fragment_read(store, handle, kinds + 1, identities + 1, test->expected_count, vector != 0);
            c_fragment_store_test_check(test, read, "flat reader rejected valid stream");
            c_fragment_store_test_check(test, memcmp(kinds + 1, test->expected_kinds, test->expected_count) == 0, "flat kinds differ from fixture");
            c_fragment_store_test_check(test, memcmp(identities + 1, test->expected_ids, (uint64_t)test->expected_count * sizeof(uint32_t)) == 0, "flat identities differ from fixture");
            c_fragment_store_test_check(test, kinds[0] == 0xa5 && identities[0] == UINT32_C(0xa5a5a5a5) &&
                                       kinds[test->expected_count + 1] == 0xa5 && identities[test->expected_count + 1] == UINT32_C(0xa5a5a5a5), "flat reader overwrote output boundaries");
            if (test->expected_count != 0)
            {
                memset(kinds, 0xa5, sizeof(kinds));
                memset(identities, 0xa5, sizeof(identities));
                const bool short_read = c_fragment_read(store, handle, kinds + 1, identities + 1, test->expected_count - 1, vector != 0);
                bool untouched = true;
                for (uint32_t i = 0; i < test->expected_count + 2; i += 1)
                {
                    untouched &= kinds[i] == 0xa5 && identities[i] == UINT32_C(0xa5a5a5a5);
                }
                c_fragment_store_test_check(test, !short_read && untouched, "short output capacity was accepted or written");
            }
            c_fragment_store_test_windows(test, store, handle, vector != 0);
        }
    }
}

static CFragmentHandle c_fragment_store_test_leaves(CFragmentStoreTest* test, CFragmentStore* store, uint32_t first, uint32_t count)
{
    CFragmentHandle result = c_fragment_empty();
    for (uint32_t i = 0; store->valid && i < count; i += 1)
    {
        CFragmentHandle leaf = c_fragment_leaf(first + i);
        CFragmentHandle joined = c_fragment_join(store, &result, &leaf);
        c_fragment_store_test_check(test, c_fragment_store_test_empty(&result) && c_fragment_store_test_empty(&leaf), "join retained an input handle");
        result = joined;
    }
    return result;
}

static CFragmentHandle c_fragment_store_test_operators(CFragmentStore* store, uint32_t first, uint32_t count)
{
    CFragmentHandle result = c_fragment_empty();
    for (uint32_t i = 0; store->valid && i < count; i += 1)
    {
        c_fragment_prepend(store, &result, c_fragment_store_test_unary_kind(first + i), first + i);
    }
    return result;
}

static void c_fragment_store_test_handles(CFragmentStoreTest* test)
{
    CFragmentStore store;
    if (c_fragment_store_test_begin(test, &store, 8, "empty and virtual handles"))
    {
        CFragmentHandle left = c_fragment_empty();
        CFragmentHandle right = c_fragment_empty();
        CFragmentHandle empty = c_fragment_join(&store, &left, &right);
        c_fragment_store_test_check(test, c_fragment_store_test_empty(&left) && c_fragment_store_test_empty(&right) && c_fragment_store_test_empty(&empty), "empty join is not the identity");
        c_fragment_store_test_verify(test, &store, &empty);
        left = c_fragment_empty();
        right = c_fragment_leaf(3);
        CFragmentHandle joined = c_fragment_join(&store, &left, &right);
        c_fragment_store_test_check(test, c_fragment_store_test_empty(&left) && c_fragment_store_test_empty(&right), "virtual join did not consume both inputs");
        c_fragment_store_test_check(test, joined.head == BUSTER_C_FRAGMENT_NONE && joined.leaf == 3 && joined.positions == 1 && store.allocated == 0, "empty join materialized or lost the virtual leaf");
        right = c_fragment_empty();
        left = c_fragment_join(&store, &joined, &right);
        c_fragment_store_test_check(test, c_fragment_store_test_empty(&joined) && c_fragment_store_test_empty(&right), "right-empty join did not consume both inputs");
        c_fragment_store_test_check(test, c_fragment_materialize(&store, &left) && left.head == left.tail && left.leaf == BUSTER_C_FRAGMENT_NONE && store.live_count == 1, "virtual materialization failed");
        c_fragment_store_test_expect_leaf(test, 3);
        c_fragment_store_test_verify(test, &store, &left);
    }
    if (c_fragment_store_test_begin(test, &store, 0, "virtual leaf with no record capacity"))
    {
        CFragmentHandle leaf = c_fragment_leaf(5);
        c_fragment_store_test_expect_leaf(test, 5);
        c_fragment_store_test_verify(test, &store, &leaf);
        c_fragment_store_test_check(test, store.allocated == 0 && store.live_count == 0, "reading a virtual leaf allocated a record");
    }
    if (c_fragment_store_test_begin(test, &store, 8, "free-list reuse and unique ownership"))
    {
        CFragmentHandle left = c_fragment_leaf(0);
        CFragmentHandle right = c_fragment_leaf(1);
        const bool materialized = c_fragment_materialize(&store, &left) && c_fragment_materialize(&store, &right);
        c_fragment_store_test_check(test, materialized, "two independent leaves did not materialize");
        if (materialized)
        {
            const CFragmentHandle roots[] =
            {
                left,
                right,
            };
            c_fragment_store_test_ownership(test, &store, roots, 2);
            const uint32_t freed = right.head;
            CFragmentHandle joined = c_fragment_join(&store, &left, &right);
            c_fragment_store_test_check(test, c_fragment_store_test_empty(&left) && c_fragment_store_test_empty(&right) && store.free_head == freed && !store.live[freed], "boundary copy did not release exactly the consumed right record");
            CFragmentHandle third = c_fragment_leaf(2);
            const bool reused = c_fragment_materialize(&store, &third);
            c_fragment_store_test_check(test, reused && third.head == freed && store.allocated == 2 && store.statistics.reused == 1, "allocation did not reuse the free record");
            if (reused)
            {
                const CFragmentHandle reused_roots[] =
                {
                    joined,
                    third,
                };
                c_fragment_store_test_ownership(test, &store, reused_roots, 2);
                left = c_fragment_join(&store, &joined, &third);
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    c_fragment_store_test_expect_leaf(test, i);
                }
                c_fragment_store_test_verify(test, &store, &left);
            }
        }
    }
}

static void c_fragment_store_test_boundaries(CFragmentStoreTest* test)
{
    CFragmentStore store;
    if (c_fragment_store_test_begin(test, &store, 8, "join position capacity"))
    {
        CFragmentHandle left = c_fragment_store_test_leaves(test, &store, 0, 63);
        CFragmentHandle right = c_fragment_leaf(63);
        CFragmentHandle full = c_fragment_join(&store, &left, &right);
        c_fragment_store_test_check(test, store.valid && store.live_count == 1 && c_fragment_header(&store, full.head)->used == 64, "exact position fit linked instead of copying");
        right = c_fragment_leaf(64);
        left = c_fragment_join(&store, &full, &right);
        c_fragment_store_test_check(test, store.valid && store.live_count == 2 && left.head != left.tail, "one extra position did not link a second record");
        for (uint32_t i = 0; i < 65; i += 1)
        {
            c_fragment_store_test_expect_leaf(test, i);
        }
        c_fragment_store_test_verify(test, &store, &left);
    }
    if (c_fragment_store_test_begin(test, &store, 8, "prepend position capacity"))
    {
        CFragmentHandle handle = c_fragment_store_test_leaves(test, &store, 0, 63);
        c_fragment_prepend(&store, &handle, C_FRAGMENT_NOT, 100);
        c_fragment_store_test_check(test, store.valid && store.live_count == 1 && c_fragment_header(&store, handle.head)->used == 64, "prepend missed the exact position fit");
        c_fragment_prepend(&store, &handle, C_FRAGMENT_NEGATIVE, 101);
        c_fragment_store_test_check(test, store.valid && store.live_count == 2 && handle.head != handle.tail, "prepend overflowed the position mask");
        c_fragment_store_test_expect(test, C_FRAGMENT_NEGATIVE, 101);
        c_fragment_store_test_expect(test, C_FRAGMENT_NOT, 100);
        for (uint32_t i = 0; i < 63; i += 1)
        {
            c_fragment_store_test_expect_leaf(test, i);
        }
        c_fragment_store_test_verify(test, &store, &handle);
    }
    if (c_fragment_store_test_begin(test, &store, 8, "prepend operator capacity"))
    {
        CFragmentHandle handle = c_fragment_store_test_operators(&store, 0, test->payload);
        c_fragment_store_test_check(test, store.valid && store.live_count == 1 && c_fragment_header(&store, handle.head)->operator_count == test->payload, "exact operator fit did not stay in one record");
        c_fragment_prepend(&store, &handle, c_fragment_store_test_unary_kind(test->payload), test->payload);
        c_fragment_store_test_check(test, store.valid && store.live_count == 2 && handle.head != handle.tail, "one extra operator did not start a record");
        for (uint32_t i = test->payload + 1; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i - 1), i - 1);
        }
        c_fragment_store_test_verify(test, &store, &handle);
    }
    if (c_fragment_store_test_begin(test, &store, 8, "join operator capacity"))
    {
        CFragmentHandle left = c_fragment_store_test_operators(&store, 0, test->payload - 1);
        CFragmentHandle right = c_fragment_store_test_operators(&store, test->payload, 1);
        CFragmentHandle full = c_fragment_join(&store, &left, &right);
        c_fragment_store_test_check(test, store.valid && store.live_count == 1 && c_fragment_header(&store, full.head)->operator_count == test->payload, "exact operator join did not merge");
        right = c_fragment_store_test_operators(&store, test->payload + 1, 1);
        left = c_fragment_join(&store, &full, &right);
        c_fragment_store_test_check(test, store.valid && store.live_count == 2, "operator-only overflow did not link");
        for (uint32_t i = test->payload - 1; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i - 1), i - 1);
        }
        c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(test->payload), test->payload);
        c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(test->payload + 1), test->payload + 1);
        c_fragment_store_test_verify(test, &store, &left);
    }
    if (c_fragment_store_test_begin(test, &store, 8, "multi-record right-tail regression"))
    {
        CFragmentHandle left = c_fragment_leaf(0);
        CFragmentHandle right = c_fragment_store_test_operators(&store, 1, test->payload + 1);
        const uint32_t right_head = right.head;
        const uint32_t right_tail = right.tail;
        CFragmentHandle joined = c_fragment_join(&store, &left, &right);
        const bool retained_tail = store.valid && right_head != right_tail && joined.tail == right_tail && !store.live[right_head];
        c_fragment_store_test_check(test, retained_tail, "boundary copy replaced the remaining right-chain tail");
        if (retained_tail)
        {
            right = c_fragment_leaf(test->payload + 2);
            left = c_fragment_join(&store, &joined, &right);
            c_fragment_store_test_check(test, store.valid && left.tail == right_tail && store.live[right_tail], "next append used the freed right head as the tail");
            c_fragment_store_test_expect_leaf(test, 0);
            for (uint32_t i = test->payload + 1; i != 0; i -= 1)
            {
                c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i), i);
            }
            c_fragment_store_test_expect_leaf(test, test->payload + 2);
            c_fragment_store_test_verify(test, &store, &left);
        }
    }
}

static void c_fragment_store_test_metric_bytes(CFragmentStoreTest* test, const CFragmentStore* store, uint64_t operators, uint64_t identities, uint64_t next_fields)
{
    c_fragment_store_test_check(test, store->valid, "metric fixture poisoned the store");
    c_fragment_store_test_check(test, store->statistics.copied_bytes == operators, "operator-copy bytes differ from hand count");
    c_fragment_store_test_check(test, store->statistics.identity_bytes_copied == identities, "identity-move bytes differ from hand count");
    c_fragment_store_test_check(test, store->statistics.linked_bytes == next_fields, "logical next-field bytes differ from hand count");
}

static void c_fragment_store_test_metrics(CFragmentStoreTest* test)
{
    CFragmentStore store;
    if (c_fragment_store_test_begin(test, &store, 0, "virtual singleton metrics"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        c_fragment_store_test_expect_leaf(test, 0);
        c_fragment_store_test_verify(test, &store, &handle);
        c_fragment_store_test_metric_bytes(test, &store, 0, 0, 0);
        c_fragment_store_test_check(test, store.statistics.fragments == 0 && store.statistics.positions == 0 && store.statistics.operators == 0 &&
                                   store.statistics.copies == 0 && store.statistics.links == 0 && store.statistics.peak_live == 0, "virtual singleton was charged as a physical fragment");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "leaf join metrics"))
    {
        CFragmentHandle left = c_fragment_leaf(0);
        CFragmentHandle right = c_fragment_leaf(1);
        CFragmentHandle joined = c_fragment_join(&store, &left, &right);
        // Two allocations, the boundary rewrite and the freed header each write one four-byte next field.
        // Materializing identities emits fresh values; only the right leaf's copied identity counts as a move.
        c_fragment_store_test_metric_bytes(test, &store, 0, 4, 16);
        c_fragment_store_test_check(test, store.statistics.copies == 1 && store.statistics.links == 0 &&
                                   store.statistics.peak_live == 2 && store.live_count == 1, "leaf join operation or live-record counts differ");
        right = c_fragment_leaf(2);
        left = c_fragment_join(&store, &joined, &right);
        // Reusing the free slot adds its initialization, the boundary rewrite and its free: twelve more bytes.
        c_fragment_store_test_metric_bytes(test, &store, 0, 8, 28);
        c_fragment_store_test_check(test, store.statistics.copies == 2 && store.statistics.links == 0 && store.statistics.reused == 1, "reused leaf join operation counts differ");
        for (uint32_t i = 0; i < 3; i += 1)
        {
            c_fragment_store_test_expect_leaf(test, i);
        }
        c_fragment_store_test_verify(test, &store, &left);
        c_fragment_store_test_metric_bytes(test, &store, 0, 8, 28);
        c_fragment_store_test_check(test, store.statistics.fragments == 1 && store.statistics.positions == 3 && store.statistics.operators == 0 &&
                                   store.statistics.position_occupancy[3] == 1 && store.statistics.operator_occupancy[0] == 1, "leaf join final occupancy differs");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "unary prepend metrics"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        for (uint32_t i = 1; i <= 3; i += 1)
        {
            c_fragment_prepend(&store, &handle, c_fragment_store_test_unary_kind(i), i);
        }
        // Existing operator moves are 0+1+2 bytes. Identity moves are (1+2+3)*4 bytes. Only allocation writes next.
        c_fragment_store_test_metric_bytes(test, &store, 3, 24, 4);
        c_fragment_store_test_check(test, store.statistics.copies == 3 && store.statistics.links == 0 && store.statistics.peak_live == 1, "unary operation counts differ");
        for (uint32_t i = 3; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i), i);
        }
        c_fragment_store_test_expect_leaf(test, 0);
        c_fragment_store_test_verify(test, &store, &handle);
        c_fragment_store_test_metric_bytes(test, &store, 3, 24, 4);
        c_fragment_store_test_check(test, store.statistics.fragments == 1 && store.statistics.positions == 4 && store.statistics.operators == 3 &&
                                   store.statistics.position_occupancy[4] == 1 && store.statistics.operator_occupancy[3] == 1, "unary final occupancy differs");
    }
    // With only operators, filling 16/32/48 slots moves 1+...+(payload-1) bytes, respectively 120/496/1128.
    const uint64_t full_moves = test->payload == 16 ? 120 : test->payload == 32 ? 496 : 1128;
    if (c_fragment_store_test_begin(test, &store, 4, "prepend capacity spill metrics"))
    {
        CFragmentHandle handle = c_fragment_store_test_operators(&store, 0, test->payload);
        // Creating the first operator record writes next twice, once at allocation and once at prepend.
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 8);
        c_fragment_prepend(&store, &handle, c_fragment_store_test_unary_kind(test->payload), test->payload);
        // The spill initializes and links a new head; it moves no old operator or identity bytes.
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 16);
        c_fragment_store_test_check(test, store.statistics.copies == test->payload - 1 && store.statistics.links == 1 &&
                                   store.statistics.peak_live == 2 && store.live_count == 2, "prepend spill operation or live-record counts differ");
        for (uint32_t i = test->payload + 1; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i - 1), i - 1);
        }
        c_fragment_store_test_verify(test, &store, &handle);
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 16);
        c_fragment_store_test_check(test, store.statistics.fragments == 2 && store.statistics.positions == test->payload + 1 && store.statistics.operators == test->payload + 1 &&
                                   store.statistics.position_occupancy[1] == 1 && store.statistics.position_occupancy[test->payload] == 1 &&
                                   store.statistics.operator_occupancy[1] == 1 && store.statistics.operator_occupancy[test->payload] == 1, "prepend spill final occupancy differs");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "join capacity spill metrics"))
    {
        CFragmentHandle left = c_fragment_store_test_operators(&store, 0, test->payload);
        CFragmentHandle right = c_fragment_store_test_operators(&store, test->payload, 1);
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 16);
        CFragmentHandle joined = c_fragment_join(&store, &left, &right);
        // Both records already exist: a linking join adds exactly one next-field assignment and no copies.
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 20);
        c_fragment_store_test_check(test, store.statistics.copies == test->payload - 1 && store.statistics.links == 1 &&
                                   store.statistics.peak_live == 2 && store.live_count == 2, "linking join operation or live-record counts differ");
        for (uint32_t i = test->payload; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i - 1), i - 1);
        }
        c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(test->payload), test->payload);
        c_fragment_store_test_verify(test, &store, &joined);
        c_fragment_store_test_metric_bytes(test, &store, full_moves, 4 * full_moves, 20);
    }
}

static void c_fragment_store_test_long(CFragmentStoreTest* test)
{
    CFragmentStore store;
    if (c_fragment_store_test_begin(test, &store, BUSTER_C_FRAGMENT_STORE_TEST_UNARY + 2, "long unary chain"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        for (uint32_t i = 1; store.valid && i <= BUSTER_C_FRAGMENT_STORE_TEST_UNARY; i += 1)
        {
            c_fragment_prepend(&store, &handle, c_fragment_store_test_unary_kind(i), i);
        }
        for (uint32_t i = BUSTER_C_FRAGMENT_STORE_TEST_UNARY; i != 0; i -= 1)
        {
            c_fragment_store_test_expect(test, c_fragment_store_test_unary_kind(i), i);
        }
        c_fragment_store_test_expect_leaf(test, 0);
        c_fragment_store_test_verify(test, &store, &handle);
    }
    for (uint32_t direction = 0; direction < 2; direction += 1)
    {
        if (c_fragment_store_test_begin(test, &store, BUSTER_C_FRAGMENT_STORE_TEST_LEAVES + 2, direction == 0 ? "long left joins" : "long right joins"))
        {
            CFragmentHandle handle = c_fragment_leaf(0);
            for (uint32_t i = 1; store.valid && i < BUSTER_C_FRAGMENT_STORE_TEST_LEAVES; i += 1)
            {
                CFragmentHandle leaf = c_fragment_leaf(i);
                CFragmentHandle joined = direction == 0 ? c_fragment_join(&store, &handle, &leaf) : c_fragment_join(&store, &leaf, &handle);
                c_fragment_store_test_check(test, c_fragment_store_test_empty(&handle) && c_fragment_store_test_empty(&leaf), "long join retained an input handle");
                handle = joined;
                c_fragment_prepend(&store, &handle, C_FRAGMENT_ADD, BUSTER_C_FRAGMENT_STORE_TEST_LEAVES + i);
            }
            for (uint32_t i = BUSTER_C_FRAGMENT_STORE_TEST_LEAVES - 1; i != 0; i -= 1)
            {
                c_fragment_store_test_expect(test, C_FRAGMENT_ADD, BUSTER_C_FRAGMENT_STORE_TEST_LEAVES + i);
                if (direction != 0)
                {
                    c_fragment_store_test_expect_leaf(test, i);
                }
            }
            const uint32_t tail_leaves = direction == 0 ? BUSTER_C_FRAGMENT_STORE_TEST_LEAVES : 1;
            for (uint32_t i = 0; i < tail_leaves; i += 1)
            {
                c_fragment_store_test_expect_leaf(test, i);
            }
            c_fragment_store_test_verify(test, &store, &handle);
        }
    }
}

static void c_fragment_store_test_failures(CFragmentStoreTest* test)
{
    CFragmentStore store;
    if (c_fragment_store_test_begin(test, &store, 4, "unfinished cursor refusal"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            CFragmentCursor cursor = c_fragment_cursor(&handle);
            uint8_t kinds[64];
            uint32_t identities[64];
            memset(kinds, 0xa5, sizeof(kinds));
            memset(identities, 0xa5, sizeof(identities));
            uint32_t count = UINT32_MAX;
            const bool accepted = c_fragment_window(&store, &cursor, kinds, identities, &count, vector != 0);
            bool untouched = true;
            for (uint32_t i = 0; i < 64; i += 1)
            {
                untouched &= kinds[i] == 0xa5 && identities[i] == UINT32_C(0xa5a5a5a5);
            }
            c_fragment_store_test_check(test, !accepted && count == 0 && untouched && store.valid, "unfinished cursor was accepted or wrote output");
        }
    }
    if (c_fragment_store_test_begin(test, &store, 1, "record exhaustion and empty reads"))
    {
        CFragmentHandle empty = c_fragment_empty();
        c_fragment_finish(&store, &empty);
        CFragmentHandle left = c_fragment_leaf(0);
        CFragmentHandle right = c_fragment_leaf(1);
        CFragmentHandle failed = c_fragment_join(&store, &left, &right);
        c_fragment_store_test_check(test, !store.valid && c_fragment_store_test_empty(&failed) && c_fragment_store_test_empty(&left) && c_fragment_store_test_empty(&right), "exhausted join did not poison and consume its handles");
        c_fragment_store_test_check(test, !c_fragment_read(&store, &empty, 0, 0, 0, false) && !c_fragment_read(&store, &empty, 0, 0, 0, true), "empty read bypassed the poisoned store");
        c_fragment_store_test_check(test, c_fragment_allocate(&store) == BUSTER_C_FRAGMENT_NONE, "poisoned store allocated another record");
    }
    if (c_fragment_store_test_begin(test, &store, 1, "prepend exhaustion"))
    {
        CFragmentHandle handle = c_fragment_store_test_operators(&store, 0, test->payload);
        const uint32_t positions = handle.positions;
        c_fragment_prepend(&store, &handle, C_FRAGMENT_NOT, 100);
        c_fragment_store_test_check(test, !store.valid && handle.positions == positions && store.allocated == 1, "failed prepend published a partial extra position");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "self join"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        CFragmentHandle failed = c_fragment_join(&store, &handle, &handle);
        c_fragment_store_test_check(test, !store.valid && c_fragment_store_test_empty(&handle) && c_fragment_store_test_empty(&failed), "self join was accepted");
    }
    for (uint32_t operation = 0; operation < 2; operation += 1)
    {
        if (c_fragment_store_test_begin(test, &store, 4, "frozen mutation"))
        {
            CFragmentHandle handle = c_fragment_leaf(0);
            c_fragment_prepend(&store, &handle, C_FRAGMENT_NOT, 1);
            c_fragment_finish(&store, &handle);
            uint8_t record[sizeof(CFragmentHeader) + 48];
            uint32_t identities[2];
            const uint32_t head = handle.head;
            memcpy(record, c_fragment_header(&store, head), store.stride);
            memcpy(identities, c_fragment_identities(&store, head), sizeof(identities));
            if (operation == 0)
            {
                c_fragment_prepend(&store, &handle, C_FRAGMENT_NEGATIVE, 2);
            }
            else
            {
                CFragmentHandle leaf = c_fragment_leaf(2);
                CFragmentHandle failed = c_fragment_join(&store, &handle, &leaf);
                c_fragment_store_test_check(test, c_fragment_store_test_empty(&failed), "frozen join returned a live handle");
            }
            c_fragment_store_test_check(test, !store.valid && store.allocated == 1 && store.live_count == 1, "frozen mutation was accepted or changed allocation");
            c_fragment_store_test_check(test, memcmp(record, c_fragment_header(&store, head), store.stride) == 0 &&
                                       memcmp(identities, c_fragment_identities(&store, head), sizeof(identities)) == 0, "frozen record was modified");
        }
    }
    for (uint32_t operation = 0; operation < 2; operation += 1)
    {
        if (c_fragment_store_test_begin(test, &store, 4, "logical position overflow"))
        {
            CFragmentHandle handle = c_fragment_leaf(0);
            handle.positions = UINT32_MAX;
            if (operation == 0)
            {
                c_fragment_prepend(&store, &handle, C_FRAGMENT_NOT, 1);
            }
            else
            {
                CFragmentHandle leaf = c_fragment_leaf(1);
                CFragmentHandle failed = c_fragment_join(&store, &handle, &leaf);
                c_fragment_store_test_check(test, c_fragment_store_test_empty(&failed), "overflow join returned a live handle");
            }
            c_fragment_store_test_check(test, !store.valid && store.allocated == 0, "logical position count wrapped or allocated");
        }
    }
    test->phase = "arena exhaustion";
    Arena unavailable = { 0 };
    c_fragment_store_test_check(test, !c_fragment_store_initialize(&store, &unavailable, 4, test->payload, test->leaf_kinds, BUSTER_C_FRAGMENT_STORE_TEST_LIMIT), "unavailable arena was accepted");
}

static void c_fragment_store_test_invalid(CFragmentStoreTest* test)
{
    CFragmentStore store;
    for (uint32_t mutation = 0; mutation < 9; mutation += 1)
    {
        if (c_fragment_store_test_begin(test, &store, 4, "invalid record metadata"))
        {
            CFragmentHandle handle = c_fragment_leaf(0);
            c_fragment_prepend(&store, &handle, C_FRAGMENT_NOT, 1);
            CFragmentHeader* record = c_fragment_header(&store, handle.head);
            switch (mutation)
            {
                case 0: record->used = 0; break;
                case 1: record->used = 65; break;
                case 2: record->operator_count = (uint8_t)(test->payload + 1); break;
                case 3: record->operator_count = 2; break;
                case 4: record->operator_mask = UINT64_C(1) << 2; break;
                case 5: record->next = UINT32_MAX - 1; break;
                case 6: record->next = handle.head; break;
                case 7: handle.tail = BUSTER_C_FRAGMENT_NONE; break;
                case 8: handle.positions += 1; break;
            }
            c_fragment_finish(&store, &handle);
            c_fragment_store_test_check(test, !store.valid && !handle.frozen, "finish accepted malformed metadata or a cycle");
        }
    }
    if (c_fragment_store_test_begin(test, &store, 4, "live link into free list"))
    {
        CFragmentHandle left = c_fragment_leaf(0);
        CFragmentHandle right = c_fragment_leaf(1);
        c_fragment_materialize(&store, &left);
        c_fragment_materialize(&store, &right);
        const uint32_t freed = right.head;
        CFragmentHandle joined = c_fragment_join(&store, &left, &right);
        c_fragment_header(&store, joined.tail)->next = freed;
        c_fragment_finish(&store, &joined);
        c_fragment_store_test_check(test, !store.valid && !joined.frozen, "finish followed a freed record");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "invalid virtual identity"))
    {
        CFragmentHandle handle = c_fragment_leaf(store.leaf_count);
        c_fragment_finish(&store, &handle);
        c_fragment_store_test_check(test, !store.valid && !handle.frozen, "finish published an out-of-range virtual leaf");
    }
    if (c_fragment_store_test_begin(test, &store, 4, "invalid leaf identity at read"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        c_fragment_materialize(&store, &handle);
        c_fragment_finish(&store, &handle);
        c_fragment_identities(&store, handle.head)[0] = store.leaf_count;
        uint8_t kinds[64];
        uint32_t identities[64];
        for (uint32_t vector = 0; vector < 2; vector += 1)
        {
            CFragmentCursor cursor = c_fragment_cursor(&handle);
            uint32_t count = UINT32_MAX;
            c_fragment_store_test_check(test, !c_fragment_window(&store, &cursor, kinds, identities, &count, vector != 0) && count == 0, "reader accepted an out-of-range leaf identity");
        }
    }
    if (c_fragment_store_test_begin(test, &store, 4, "invalid operator kind"))
    {
        CFragmentHandle handle = c_fragment_leaf(0);
        c_fragment_prepend(&store, &handle, C_FRAGMENT_KIND_COUNT, 1);
        c_fragment_store_test_check(test, !store.valid && store.allocated == 0, "out-of-range operator kind was accepted");
    }
    test->phase = "invalid payload capacity";
    const uint32_t payloads[] =
    {
        0,
        15,
        17,
        31,
        33,
        47,
        49,
        UINT32_MAX,
    };
    for (uint32_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i += 1)
    {
        const uint64_t mark = test->arena.length;
        const bool accepted = c_fragment_store_initialize(&store, &test->arena, 4, payloads[i], test->leaf_kinds, BUSTER_C_FRAGMENT_STORE_TEST_LIMIT);
        c_fragment_store_test_check(test, !accepted && test->arena.length == mark, "invalid payload allocated or was accepted");
    }
}

static uint32_t c_fragment_store_tests(void)
{
    CFragmentStoreTest test = {
        .arena = arena_reserve(32ull << 20),
        .phase = "setup",
    };
    c_fragment_store_test_check(&test, test.arena.base != 0, "test arena reservation failed");
    if (test.arena.base != 0)
    {
        for (uint32_t i = 0; i < BUSTER_C_FRAGMENT_STORE_TEST_LIMIT; i += 1)
        {
            test.leaf_kinds[i] = (uint8_t)(C_FRAGMENT_IDENTIFIER + i % 4);
        }
        for (uint32_t payload = 16; payload <= 48; payload += 16)
        {
            test.payload = payload;
            c_fragment_store_test_handles(&test);
            c_fragment_store_test_boundaries(&test);
            c_fragment_store_test_metrics(&test);
            c_fragment_store_test_long(&test);
            c_fragment_store_test_failures(&test);
            c_fragment_store_test_invalid(&test);
        }
    }
    arena_release(&test.arena);
    printf("fragment-store: %u checks, %u failures\n", test.checks, test.failures);
    return test.failures;
}
