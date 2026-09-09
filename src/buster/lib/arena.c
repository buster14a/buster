#include <buster/lib/arena.h>
#include <buster/lib/os.h>
#include <buster/lib/integer.h>



BUSTER_GLOBAL_LOCAL u64 default_granularity = BUSTER_KB(64);

BUSTER_GLOBAL_LOCAL u64 default_reserve_size = BUSTER_MB(256);
BUSTER_GLOBAL_LOCAL u64 initial_size_granularity_factor = 4;
#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool arena_fail_next_commit;

void arena_test_fail_next_commit(void)
{
    arena_fail_next_commit = true;
}
#endif

BUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit(u64 requested_end, u64 reserved_size)
{
    u64 page_size = os_get_page_size();
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(page_size));
    // Commit APIs round the end of a page-aligned request up to a native
    // page. Track that usable boundary so a later incremental commit also
    // starts page-aligned. A sub-page logical reservation ends at its exact
    // bound and cannot grow again, so clamping it is both accurate and safe.
    return BUSTER_MIN(align_forward(requested_end, page_size), reserved_size);
}

void arena_allocation_overflow(void)
{
    os_fail_message(S8("arena allocation size overflowed"));
}

// Arenas created with count > 1 share one reservation, so committing past
// reserved_size would land on the next arena's pages and corrupt it silently;
// fail loudly instead. Callers never check for null, so allocation must not
// return one.
//
// Bounding the operand in the inline bump is what makes that bound hold for
// every `size` rather than only for the ones that happen not to wrap: with
// `size` and `position` both under ARENA_MAX_RESERVATION (the second enforced
// in arena_create), the sum cannot carry past 2^64, so the comparison against
// `reserved_size` is exact instead of bypassable by a large enough `size`.
// The remaining-space form `size <= reserved_size - aligned_offset` needs one
// compare fewer, but only if reservations are alignment-granular, and they are
// not — the rendering boundary tests reserve 256 bytes on purpose. Rounding
// up to the commit granularity and clamping the final partial granule live
// here, in the branch that needs them; the bump never loads `granularity`.
void arena_allocate_commit(Arena* arena, u64 aligned_size_after)
{
    BUSTER_VALIDATE(aligned_size_after <= arena->reserved_size);
    u64 os_position = arena->os_position;
    u64 target_committed_size = aligned_size_after;
    u64 remainder = target_committed_size & (arena->granularity - 1);
    if (remainder)
    {
        u64 increment = arena->granularity - remainder;
        u64 remaining = arena->reserved_size - target_committed_size;
        target_committed_size = increment <= remaining ? target_committed_size + increment : arena->reserved_size;
    }
    u64 size_to_commit = target_committed_size - os_position;
    u8* commit_pointer = (u8*)arena + os_position;

    bool commit_succeeded;
#if BUSTER_INCLUDE_TESTS
    if (arena_fail_next_commit)
    {
        arena_fail_next_commit = false;
        commit_succeeded = false;
    }
    else
#endif
    {
        commit_succeeded = os_commit(commit_pointer, size_to_commit,
                                     (ProtectionFlags){.read = 1, .write = 1, .execute = arena->flags.execute}, arena->flags.lock_pages);
    }
    if (!commit_succeeded)
    {
        os_fail_message(S8("arena commit failed"));
    }
    arena->os_position = arena_os_position_after_commit(target_committed_size, arena->reserved_size);
}

u8* arena_get_byte_pointer_at_position(Arena* arena, u64 position)
{
    return (u8*)arena + position;
}

u8* arena_get_byte_pointer_at_position_check_aligned(Arena* arena, u64 position, u64 alignment)
{
    u8* result = arena_get_byte_pointer_at_position(arena, position);
    BUSTER_CHECK(is_aligned((u64)result, alignment));
    return result;
}

u64 arena_dirty_position(Arena* arena)
{
    // The live cursor is necessarily dirty without paying a high-water store
    // on every allocation. Rewinds fold that cursor into `dirty_position`.
    u64 result = arena ? BUSTER_MAX(arena->dirty_position, arena->position) : 0;
    return result;
}

u8* arena_get_byte_pointer_align(Arena* arena, u64 position, u64 alignment)
{
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(alignment));
    u8* result = arena_get_byte_pointer_at_position(arena, align_forward(position, alignment));
    return result;
}

void arena_reset_to_start(Arena* arena)
{
    arena_set_position(arena, arena_minimum_position);
}

void arena_set_position(Arena* arena, u64 position)
{
    arena->dirty_position = BUSTER_MAX(arena->dirty_position, BUSTER_MAX(arena->position, position));
    arena->position = position;
}

bool arena_set_position_and_decommit(Arena* arena, u64 position)
{
    BUSTER_CHECK(position >= arena_minimum_position && position <= arena->position);
    u64 page_size = os_get_page_size();
    BUSTER_CHECK(BUSTER_IS_POWER_OF_TWO(page_size));
    // Arena granularities may legally be smaller than a native page. Start at
    // the next boundary that satisfies both contracts, and stop before any
    // partial native page at the old high-water mark. If no complete page is
    // available, resetting the logical position is still useful and safe.
    u64 decommit_alignment = BUSTER_MAX(arena->granularity, page_size);
    u64 decommit_start = align_forward(position, decommit_alignment);
    u64 decommit_end = arena->os_position & ~(page_size - 1);
    bool result = true;
    if (decommit_start < decommit_end)
    {
        result = os_decommit((u8*)arena + decommit_start, decommit_end - decommit_start);
        if (result)
        {
            // A sub-page-granularity arena can have a committed partial page
            // above decommit_end. Treating it as uncommitted is conservative:
            // a later allocation recommits the full range from this boundary.
            arena->os_position = decommit_start;
        }
    }
    if (result)
    {
#if defined(__APPLE__)
        // Darwin's MADV_DONTNEED is a paging hint, not a zero-fill contract.
        // Recommit can expose the old contents, including earlier rewinds.
        arena_set_position(arena, position);
#else
        // Bytes beyond the native decommit boundary are freshly zeroed if
        // they are committed again; retain the prefix that can still carry
        // old contents, including a partial page below that boundary.
        arena->dirty_position = BUSTER_MIN(BUSTER_MAX(arena->dirty_position, arena->position), decommit_start);
        arena->position = position;
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ArenaCreation arena_creation_parameters(ArenaCreation original)
{
    ArenaCreation result = original;

    if (!result.reserved_size)
    {
        result.reserved_size = default_reserve_size;
    }

    if (!result.count)
    {
        result.count = 1;
    }

    if (!result.granularity)
    {
        result.granularity = default_granularity;
    }

    if (!result.initial_size)
    {
        result.initial_size = default_granularity * initial_size_granularity_factor;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool arena_destroy_extended(Arena* arena, u64 count, u64 reserved_size)
{
    u64 size = reserved_size * count;
    return os_unreserve(arena, size);
}

// Destroyed default-shaped reservations park here, per thread, for reuse:
// the mapping and its already-faulted pages survive, so the next
// arena_create skips the unmap/remap pair and the kernel's first-touch page
// zeroing. A reused arena hands out dirty bytes — the same contract
// arena_reset_to_start already imposes on every allocation site — and its
// dirty high-water mark survives the header rewrite below. The parked arena's
// first buffer bytes hold the pool link, so parking raises that watermark
// before writing the link. Non-default reservation sizes join only through
// the opt-in pool_reuse flag, because a consumer of a custom-size arena may
// still assume freshly zeroed pages; reuse always requires an exact
// reservation-size match, so differently shaped arenas never satisfy each
// other's requests. Multi-arena reservations, execute or locked pages, and
// entries past the cap unmap exactly as before.
#define ARENA_POOL_LIMIT 16
BUSTER_THREAD_LOCAL_DECL Arena* arena_pool_head;
BUSTER_THREAD_LOCAL_DECL u64 arena_pool_count;

u64 arena_pool_release_thread(void)
{
    u64 result = 0;
    Arena* pooled = arena_pool_head;
    arena_pool_head = 0;
    arena_pool_count = 0;
    while (pooled)
    {
        Arena* next = *(Arena**)((u8*)pooled + arena_minimum_position);
        u64 reserved_size = pooled->reserved_size;
        // This runs after an OS worker has cleared its ThreadContext, so the
        // raw reporter is the only failure path that does not need scratch.
        BUSTER_CHECK_RAW(arena_destroy_extended(pooled, 1, reserved_size));
        pooled = next;
        result += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool arena_pool_eligible(u64 reserved_size, u64 count, ArenaFlags flags)
{
    return count == 1 && !flags.execute && !flags.lock_pages && !flags.no_pool && (reserved_size == default_reserve_size || flags.pool_reuse);
}

bool arena_destroy(Arena* arena, u64 count)
{
    count = count == 0 ? 1 : count;
    u64 reserved_size = arena->reserved_size;
    bool result;
    if (arena_pool_eligible(reserved_size, count, arena->flags) && arena_pool_count < ARENA_POOL_LIMIT)
    {
        arena->dirty_position = BUSTER_MAX(arena_dirty_position(arena), arena_minimum_position + sizeof(Arena*));
        *(Arena**)((u8*)arena + arena_minimum_position) = arena_pool_head;
        arena_pool_head = arena;
        arena_pool_count += 1;
        result = true;
    }
    else
    {
        result = arena_destroy_extended(arena, count, reserved_size);
    }

    return result;
}

Arena* arena_create(ArenaCreation original_creation)
{
    ArenaCreation creation = arena_creation_parameters(original_creation);
    u64 count = creation.count;
    u64 individual_reserved_size = creation.reserved_size;
    // The ceiling is what lets every later allocation reason about `position`
    // and `reserved_size` as small numbers: with both under 2^48 no sum or
    // alignment round-up in arena_allocate_bytes can reach 2^64.
    BUSTER_VALIDATE(individual_reserved_size <= ARENA_MAX_RESERVATION);
    BUSTER_VALIDATE(count <= UINT64_MAX / individual_reserved_size);
    u64 total_reserved_size = individual_reserved_size * count;

    BUSTER_VALIDATE(BUSTER_IS_POWER_OF_TWO(creation.granularity));
    BUSTER_VALIDATE(creation.initial_size >= arena_minimum_position);
    BUSTER_VALIDATE(creation.initial_size <= individual_reserved_size);

    // A pooled arena short-circuits the fresh reservation below; `reused` is how
    // that decision reaches the single exit without a second return.
    Arena* reused = 0;
    if (arena_pool_eligible(individual_reserved_size, count, creation.flags))
    {
        Arena* previous = 0;
        for (Arena* pooled = arena_pool_head; pooled; previous = pooled, pooled = *(Arena**)((u8*)pooled + arena_minimum_position))
        {
            if (pooled->reserved_size != individual_reserved_size)
            {
                continue;
            }
            Arena* next = *(Arena**)((u8*)pooled + arena_minimum_position);
            if (previous)
            {
                *(Arena**)((u8*)previous + arena_minimum_position) = next;
            }
            else
            {
                arena_pool_head = next;
            }
            arena_pool_count -= 1;
            u64 committed = pooled->os_position;
            u64 dirty_position = BUSTER_MAX(pooled->dirty_position, arena_minimum_position + sizeof(Arena*));
            bool committed_enough = committed >= creation.initial_size;
            if (!committed_enough)
            {
                committed_enough = os_commit(pooled, creation.initial_size, (ProtectionFlags){.read = 1, .write = 1}, false);
                committed = arena_os_position_after_commit(creation.initial_size, individual_reserved_size);
            }
            if (committed_enough)
            {
                *pooled = (Arena){
                    .reserved_size = individual_reserved_size,
                    .position = arena_minimum_position,
                    .os_position = committed,
                    .granularity = creation.granularity,
                    .flags = creation.flags,
                    .dirty_position = dirty_position,
                };
                reused = pooled;
                break;
            }
            arena_destroy_extended(pooled, 1, individual_reserved_size);
            break;
        }
    }

    u8* result = (u8*)reused;
    if (!reused)
    {
        ProtectionFlags protection_flags = {.read = 1, .write = 1, .execute = creation.flags.execute};
        MapFlags map_flags = {.priv = 1, .anonymous = 1, .no_reserve = 1, .populate = 0};
        result = (u8*)os_reserve(0, total_reserved_size, protection_flags, map_flags);

        if (result)
        {
            for (u64 i = 0; i < count; i += 1)
            {
                Arena* arena = (Arena*)(result + (individual_reserved_size * i));

                bool commit_result = os_commit(arena, creation.initial_size, protection_flags, creation.flags.lock_pages);
                if (commit_result)
                {
                    *arena = (Arena){
                        .reserved_size = individual_reserved_size,
                        .position = arena_minimum_position,
                        .os_position = arena_os_position_after_commit(creation.initial_size, individual_reserved_size),
                        .granularity = creation.granularity,
                        .flags = creation.flags,
                        .dirty_position = arena_minimum_position,
                    };
                }
                else
                {
                    bool destroy_result = arena_destroy_extended((Arena*)result, count, individual_reserved_size);
                    result = 0;
                    BUSTER_VALIDATE(destroy_result);
                    break;
                }
            }
        }
    }

    return (Arena*)result;
}

TemporalArena arena_begin_temporal(Arena* arena)
{
    TemporalArena result = {.arena = arena, .position = arena->position};
    return result;
}

TemporalArena scratch_begin(Arena** conflicts, u64 count)
{
    Arena* arena = thread_context_get_scratch(conflicts, count);
    // Null means every scratch arena conflicted with the caller's arenas;
    // fail here instead of dereferencing null in arena_begin_temporal.
    BUSTER_VALIDATE(arena);
    return arena_begin_temporal(arena);
}

void scratch_end(TemporalArena temporal)
{
    Arena* arena = temporal.arena;
    arena_set_position(arena, temporal.position);
}

#if BUSTER_BENCH_ALLOCATIONS
#include <buster/lib/system_headers.h>

// One allocation-free TLS recorder supplies both the source-metrics snapshot
// and the optional exit census. No sampling or silent overflow is permitted.
#define ARENA_BENCHMARK_SITE_CAPACITY 8192

typedef struct ArenaBenchmarkSite ArenaBenchmarkSite;
struct ArenaBenchmarkSite
{
    String8 file;
    String8 function;
    u32 line;
    ArenaBenchmarkKind kind;
    ArenaBenchmarkCounters totals;
};
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL ArenaBenchmarkSite arena_benchmark_sites[ARENA_BENCHMARK_SITE_CAPACITY];
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL ArenaBenchmarkCounters arena_benchmark_totals[ARENA_BENCHMARK_KIND_COUNT];
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool arena_benchmark_reported;
BUSTER_GLOBAL_LOCAL bool arena_benchmark_report_enabled;
BUSTER_GLOBAL_LOCAL AtomicU64 arena_benchmark_report_sequence;
BUSTER_GLOBAL_LOCAL AtomicU64 arena_benchmark_report_turn;

BUSTER_GLOBAL_LOCAL void arena_benchmark_raw_write(ByteSlice bytes)
{
    u64 offset = 0;
    while (offset < bytes.length)
    {
#if defined(__linux__) || defined(__APPLE__)
        ssize_t written;
        do
        {
            written = write(STDERR_FILENO, bytes.pointer + offset, (size_t)(bytes.length - offset));
        } while (written < 0 && errno == EINTR);
        if (written <= 0)
        {
            os_exit(1);
        }
        offset += (u64)written;
#elif defined(_WIN32)
        DWORD written = 0;
        // Every bounded record is at most 4 KiB; the DWORD conversion is exact.
        if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), bytes.pointer + offset, (DWORD)(bytes.length - offset), &written, 0) || !written)
        {
            os_exit(1);
        }
        offset += written;
#else
        BUSTER_UNUSED(bytes);
        os_exit(1);
#endif
    }
}

BUSTER_NORETURN BUSTER_GLOBAL_LOCAL void arena_benchmark_fail(void)
{
    // Raw output remains usable after thread-context cleanup and needs no arena.
    String8 message = S8("BUSTER_ALLOC_ERROR capacity, counter, or lifecycle failure\n");
    arena_benchmark_raw_write(BUSTER_SLICE_TO_BYTE_SLICE(message));
    os_exit(1);
}

BUSTER_GLOBAL_LOCAL void arena_benchmark_add(ArenaBenchmarkCounters* totals, u64 size, u64 padding,
                                            u64 zero_requested, u64 zero_written, bool success)
{
    if (totals->calls == UINT64_MAX || size > UINT64_MAX - totals->requested_bytes ||
        padding > UINT64_MAX - totals->padding || zero_requested > UINT64_MAX - totals->zero_requested ||
        zero_written > UINT64_MAX - totals->zero_written)
    {
        arena_benchmark_fail();
    }
    totals->calls += 1;
    totals->requested_bytes += size;
    totals->padding += padding;
    totals->zero_requested += zero_requested;
    totals->zero_written += zero_written;
    // Subset counts/bytes cannot overflow before their checked supersets.
    totals->empty += size == 0;
    totals->small += size > 0 && size <= 64;
    totals->maximum = BUSTER_MAX(totals->maximum, size);
    totals->failures += !success;
    totals->failed_bytes += success ? 0 : size;
}

BUSTER_GLOBAL_LOCAL bool arena_benchmark_name_equal(String8 left, String8 right)
{
    bool result = left.length == right.length && !memcmp(left.pointer, right.pointer, (size_t)left.length);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 arena_benchmark_hash_name(u64 hash, String8 name)
{
    for (u64 index = 0; index < name.length; index += 1)
    {
        hash = (hash ^ name.pointer[index]) * 1099511628211ull;
    }
    return hash;
}

void arena_benchmark_event(ArenaBenchmarkKind kind, String8 file, String8 function, u32 line,
                           u64 size, u64 padding, u64 zero_requested, u64 zero_written, bool success)
{
    if (arena_benchmark_reported || (u32)kind >= ARENA_BENCHMARK_KIND_COUNT || !line || !file.length || !function.length ||
        zero_written > zero_requested || zero_requested > size)
    {
        arena_benchmark_fail();
    }
    // Textual identities coalesce equal spellings across non-unity TUs. Neither
    // addresses nor the hash-table emission order are part of the protocol.
    u64 hash = arena_benchmark_hash_name(14695981039346656037ull, file);
    hash = arena_benchmark_hash_name(hash, function) ^ ((u64)line << 3) ^ (u64)kind;
    u64 index = hash & (ARENA_BENCHMARK_SITE_CAPACITY - 1);
    u64 probes = 0;
    while (arena_benchmark_sites[index].line &&
           (arena_benchmark_sites[index].kind != kind || arena_benchmark_sites[index].line != line ||
            !arena_benchmark_name_equal(arena_benchmark_sites[index].file, file) ||
            !arena_benchmark_name_equal(arena_benchmark_sites[index].function, function)))
    {
        index = (index + 1) & (ARENA_BENCHMARK_SITE_CAPACITY - 1);
        probes += 1;
        if (probes == ARENA_BENCHMARK_SITE_CAPACITY)
        {
            arena_benchmark_fail();
        }
    }
    ArenaBenchmarkSite* site = arena_benchmark_sites + index;
    site->kind = kind;
    site->file = file;
    site->function = function;
    site->line = line;
    arena_benchmark_add(&site->totals, size, padding, zero_requested, zero_written, success);
    arena_benchmark_add(arena_benchmark_totals + kind, size, padding, zero_requested, zero_written, success);
}

ArenaBenchmarkCounters arena_benchmark_kind_counters(ArenaBenchmarkKind kind)
{
    BUSTER_CHECK_RAW((u32)kind < ARENA_BENCHMARK_KIND_COUNT);
    return arena_benchmark_totals[kind];
}

ArenaBenchmarkCounters arena_benchmark_counters(void)
{
    return arena_benchmark_kind_counters(ARENA_BENCHMARK_ARENA);
}

void* arena_benchmark_allocate(Arena* arena, u64 size, u64 alignment, bool zeroed, String8 file, String8 function, u32 line)
{
    u64 start = arena->position;
    u64 dirty = zeroed ? arena_dirty_position(arena) : 0;
    void* result = zeroed ? arena_benchmark_allocate_zeroed_bytes_raw(arena, size, alignment)
                          : arena_benchmark_allocate_bytes_raw(arena, size, alignment);
    u64 begin = arena->position - size;
    u64 clear_end = BUSTER_MIN(dirty, arena->position);
    u64 written = clear_end > begin ? clear_end - begin : 0;
    arena_benchmark_event(ARENA_BENCHMARK_ARENA, file, function, line, size, begin - start, zeroed ? size : 0, written, true);
    return result;
}

void arena_benchmark_report_enable(bool enabled)
{
    BUSTER_CHECK_RAW(os_is_only_live_thread());
    arena_benchmark_report_enabled = enabled;
}

BUSTER_GLOBAL_LOCAL u64 arena_benchmark_append(char* buffer, u64 length, u64 capacity, String8 text)
{
    for (u64 index = 0; index < text.length; index += 1)
    {
        char8 value = text.pointer[index];
        bool escaped = value == '\t' || value == '\n' || value == '\r' || value == '\\';
        if (length + 1 + (u64)escaped > capacity)
        {
            arena_benchmark_fail();
        }
        if (escaped)
        {
            buffer[length++] = '\\';
            value = value == '\t' ? 't' : value == '\n' ? 'n' : value == '\r' ? 'r' : '\\';
        }
        buffer[length++] = value;
    }
    return length;
}

BUSTER_GLOBAL_LOCAL u64 arena_benchmark_separator(char* buffer, u64 length, u64 capacity)
{
    if (length == capacity)
    {
        arena_benchmark_fail();
    }
    buffer[length++] = '\t';
    return length;
}

BUSTER_GLOBAL_LOCAL u64 arena_benchmark_number(char* buffer, u64 length, u64 capacity, u64 number)
{
    char digits[20];
    u64 count = 0;
    do
    {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    if (length + count + 1 > capacity)
    {
        arena_benchmark_fail();
    }
    buffer[length++] = '\t';
    while (count)
    {
        buffer[length++] = digits[--count];
    }
    return length;
}

BUSTER_GLOBAL_LOCAL u64 arena_benchmark_values(char* buffer, u64 length, u64 capacity, ArenaBenchmarkCounters totals)
{
    u64 values[] = {totals.calls, totals.requested_bytes, totals.padding, totals.zero_requested, totals.zero_written,
                    totals.empty, totals.small, totals.maximum, totals.failures, totals.failed_bytes};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        length = arena_benchmark_number(buffer, length, capacity, values[index]);
    }
    return length;
}

BUSTER_GLOBAL_LOCAL void arena_benchmark_write(char* buffer, u64 length)
{
    // Callers reserve the final byte. A ticket lock covers the entire epoch,
    // including partial writes, without depending on a platform's PIPE_BUF.
    buffer[length++] = '\n';
    arena_benchmark_raw_write((ByteSlice){.pointer = (u8*)buffer, .length = length});
}

void arena_benchmark_flush(bool final)
{
    if (arena_benchmark_report_enabled)
    {
        if (arena_benchmark_reported || (final && !os_is_only_live_thread()))
        {
            arena_benchmark_fail();
        }
        arena_benchmark_reported = true;
        u64 ticket = atomic_u64_increment(&arena_benchmark_report_sequence);
        if (ticket == UINT64_MAX)
        {
            arena_benchmark_fail();
        }
        while (atomic_u64_add(&arena_benchmark_report_turn, 0) != ticket)
        {
            // Reporting happens once per retiring thread, never on a hot path.
        }
        u64 epoch = ticket + 1;
        String8 names[] = {S8("arena"), S8("os_reserve"), S8("os_commit"), S8("os_decommit"), S8("os_unreserve")};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == ARENA_BENCHMARK_KIND_COUNT);
        u64 row_count = 0;
        for (u64 index = 0; index < ARENA_BENCHMARK_SITE_CAPACITY; index += 1)
        {
            ArenaBenchmarkSite* site = arena_benchmark_sites + index;
            if (site->line)
            {
                char buffer[4096];
                u64 capacity = sizeof(buffer) - 1;
                u64 length = arena_benchmark_append(buffer, 0, capacity, S8("BUSTER_ALLOC_V2"));
                length = arena_benchmark_number(buffer, length, capacity, epoch);
                length = arena_benchmark_separator(buffer, length, capacity);
                length = arena_benchmark_append(buffer, length, capacity, names[site->kind]);
                length = arena_benchmark_separator(buffer, length, capacity);
                length = arena_benchmark_append(buffer, length, capacity, site->file);
                length = arena_benchmark_number(buffer, length, capacity, site->line);
                length = arena_benchmark_separator(buffer, length, capacity);
                length = arena_benchmark_append(buffer, length, capacity, site->function);
                length = arena_benchmark_values(buffer, length, capacity, site->totals);
                arena_benchmark_write(buffer, length);
                row_count += 1;
            }
        }
        char footer[512];
        u64 capacity = sizeof(footer) - 1;
        for (u64 kind = 0; kind < ARENA_BENCHMARK_KIND_COUNT; kind += 1)
        {
            u64 length = arena_benchmark_append(footer, 0, capacity, S8("BUSTER_ALLOC_TOTAL_V2"));
            length = arena_benchmark_number(footer, length, capacity, epoch);
            length = arena_benchmark_separator(footer, length, capacity);
            length = arena_benchmark_append(footer, length, capacity, names[kind]);
            length = arena_benchmark_values(footer, length, capacity, arena_benchmark_totals[kind]);
            arena_benchmark_write(footer, length);
        }
        u64 length = arena_benchmark_append(footer, 0, capacity, S8("BUSTER_ALLOC_END_V2"));
        length = arena_benchmark_number(footer, length, capacity, epoch);
        length = arena_benchmark_number(footer, length, capacity, row_count);
        arena_benchmark_write(footer, length);
        if (final)
        {
            length = arena_benchmark_append(footer, 0, capacity, S8("BUSTER_ALLOC_DONE_V2"));
            length = arena_benchmark_number(footer, length, capacity, epoch);
            arena_benchmark_write(footer, length);
        }
        atomic_u64_increment(&arena_benchmark_report_turn);
    }
}
#endif
