/* Research-only insertion into object.c. CFI_BASELINE is filled from the
   pinned production function by probe.c, not from a handwritten oracle. */
#include <stdio.h>
typedef struct CfiProbeCache {
    u64 calls, headers, comparisons, copied, allocated;
    u64 *ends, count, capacity, covered, last;
    u64 hint_start, hint_end;
    bool stopped, overflow;
} CfiProbeCache;
BUSTER_GLOBAL_LOCAL void cfi_bump(CfiProbeCache *c, u64 *v, u64 n)
{
    if (n > UINT64_MAX - *v) { *v = UINT64_MAX; c->overflow = true; }
    else { *v += n; }
}
/* CFI_BASELINE */
BUSTER_GLOBAL_LOCAL bool cfi_decode(CfiProbeCache *c, ByteSlice b, u64 off, u64 *end)
{
    if (off > b.length || b.length - off < sizeof(u32)) { return false; }
    u64 remaining = b.length - off;
    u32 n = 0;
    cfi_bump(c, &c->headers, 1);
    memcpy(&n, b.pointer + off, sizeof(n));
    if (!n) { return false; }
    u64 header = sizeof(u32), payload = n;
    if (n == UINT32_MAX) {
        header += sizeof(u64);
        if (remaining < header) { return false; }
        memcpy(&payload, b.pointer + off + sizeof(u32), sizeof(payload));
    }
    if (payload > remaining - header) { return false; }
    *end = off + header + payload;
    return true;
}
BUSTER_GLOBAL_LOCAL bool cfi_contains(u64 start, u64 end, u64 off, u64 size, u64 *out)
{
    if (off < start || off >= end || size > end - off) { return false; }
    *out = start;
    return true;
}
BUSTER_GLOBAL_LOCAL bool cfi_query(Arena *arena, CfiProbeCache *c, unsigned mode,
                                  ByteSlice b, u64 off, u64 size, u64 *out)
{
    cfi_bump(c, &c->calls, 1);
    if (mode == 0) { return cfi_scan(c, b, off, size, out); }
    if (!out || (b.length && !b.pointer) || off > b.length || size > b.length - off) { return false; }
    if (mode == 1) {
        if (cfi_contains(c->hint_start, c->hint_end, off, size, out)) { return true; }
        u64 pos = off >= c->hint_end ? c->hint_end : 0;
        while (pos < b.length) {
            u64 end = 0;
            if (!cfi_decode(c, b, pos, &end)) { return false; }
            c->hint_start = pos; c->hint_end = end;
            if (off < end) { return cfi_contains(pos, end, off, size, out); }
            pos = end;
        }
        return false;
    }
    /* A cache belongs to ONE immutable section during ONE writer call.
       No pointer-identity cache, globals, TLS, or cross-invocation reuse. */
    if (c->count) {
        u64 start = c->last ? c->ends[c->last - 1] : 0;
        if (cfi_contains(start, c->ends[c->last], off, size, out)) { return true; }
    }
    while (!c->stopped && c->covered <= off) {
        u64 end = 0;
        if (!cfi_decode(c, b, c->covered, &end)) { c->stopped = true; break; }
        if (c->count == c->capacity) {
            u64 cap = c->capacity ? c->capacity * 2 : 16;
            if (cap < c->capacity || cap > SIZE_MAX / sizeof(u64)) { return false; }
            u64 *next = arena_allocate(arena, u64, cap);
            if (!next) { return false; }
            if (c->count) { memcpy(next, c->ends, (size_t)(c->count * sizeof(u64))); }
            cfi_bump(c, &c->copied, c->count);
            cfi_bump(c, &c->allocated, cap);
            c->ends = next; c->capacity = cap;
        }
        u64 start = c->covered;
        c->ends[c->count++] = end; c->covered = end;
        if (off < end) {
            c->last = c->count - 1;
            return cfi_contains(start, end, off, size, out);
        }
    }
    if (off >= c->covered) { return false; }
    u64 lo = 0, hi = c->count;
    while (lo < hi) {
        u64 mid = lo + (hi - lo) / 2;
        cfi_bump(c, &c->comparisons, 1);
        if (c->ends[mid] <= off) { lo = mid + 1; }
        else { hi = mid; }
    }
    if (lo == c->count) { return false; }
    c->last = lo;
    return cfi_contains(lo ? c->ends[lo - 1] : 0, c->ends[lo], off, size, out);
}
BUSTER_GLOBAL_LOCAL bool cfi_writer_query(Arena *arena, CfiProbeCache **caches,
    u32 sections, u32 section, ByteSlice b, u64 off, u64 size, u64 *out)
{
    if (section >= sections) { return false; }
    if (!*caches) {
        *caches = arena_allocate(arena, CfiProbeCache, sections);
        if (!*caches) { return false; }
        memset(*caches, 0, (size_t)sections * sizeof(**caches));
    }
    return cfi_query(arena, *caches + section, CFI_PROBE_MODE, b, off, size, out);
}
BUSTER_GLOBAL_LOCAL void cfi_report(CfiProbeCache *caches, u32 sections)
{
    if (!caches) { return; }
    for (u32 i = 0; i < sections; ++i) {
        CfiProbeCache *c = caches + i;
        if (!c->calls) { continue; }
        fprintf(stderr, "CFI_COUNTS mode=%u section=%u calls=%llu headers=%llu comparisons=%llu copied_entries=%llu allocated_entries=%llu live_entries=%llu cache_bytes=%llu overflow=%u\n",
            (unsigned)CFI_PROBE_MODE, (unsigned)i, (unsigned long long)c->calls,
            (unsigned long long)c->headers, (unsigned long long)c->comparisons,
            (unsigned long long)c->copied, (unsigned long long)c->allocated,
            (unsigned long long)c->count, (unsigned long long)sections * sizeof(*caches), (unsigned)c->overflow);
    }
}
