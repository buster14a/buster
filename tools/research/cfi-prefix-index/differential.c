/* Source-extracted helper differential, separate from production-writer runs. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct ByteSlice { u8 *pointer; u64 length; } ByteSlice;
typedef struct Arena { void *allocations[128]; size_t count; } Arena;
static void *test_alloc(Arena *a, size_t size, u64 n) {
    assert(n <= SIZE_MAX / size && a->count < 128);
    void *p = calloc((size_t)n, size); assert(p); a->allocations[a->count++] = p; return p;
}
static void release(Arena *a) { for (size_t i = 0; i < a->count; ++i) free(a->allocations[i]); a->count = 0; }
#define arena_allocate(a, T, n) ((T *)test_alloc((a), sizeof(T), (n)))
#define BUSTER_GLOBAL_LOCAL static inline
#include "generated-probe.h"
static u64 checks;
static void check_query(Arena *a, CfiProbeCache *c, unsigned mode, ByteSlice b, u64 off, u64 width, bool null_out) {
    u64 x = UINT64_C(0x123456789abcdef0), y = x;
    bool p = object_mach_eh_frame_record_start(b, off, width, null_out ? NULL : &x);
    bool q = cfi_query(a, c, mode, b, off, width, null_out ? NULL : &y);
    if (p != q || x != y) {
        fprintf(stderr, "CFI_MISMATCH mode=%u len=%llu offset=%llu size=%llu\n", mode,
            (unsigned long long)b.length, (unsigned long long)off, (unsigned long long)width); abort();
    }
    ++checks;
}
static void put32(u8 *p, u32 v) { memcpy(p, &v, sizeof(v)); }
static void put64(u8 *p, u64 v) { memcpy(p, &v, sizeof(v)); }
static ByteSlice family(unsigned f) {
    ByteSlice b = {calloc((size_t)f + 1, 24), 24 * ((u64)f + 1)}; assert(b.pointer);
    /* A zR CIE: x86-64, pcrel sdata4, CFA=rsp+8, return address at CFA-8. */
    static const u8 cie[] = {20,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,16,1,0x1b,0x0c,7,8,0x90,1,0,0};
    memcpy(b.pointer, cie, sizeof(cie));
    for (unsigned i = 0; i < f; ++i) {
        u8 *p = b.pointer + 24 * ((size_t)i + 1);
        put32(p, 20); put32(p + 4, 24 * (i + 1) + 4); put32(p + 8, 0); put32(p + 12, 4);
        /* zero augmentation length and DW_CFA_nop padding */
    }
    return b;
}
static u64 random_step(u64 *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }
static void grid(void) {
    static const unsigned fs[] = {1,2,4,16,64,256,1024,4096};
    static const unsigned rs[] = {0,1,4,16,64,256,1024};
    for (unsigned fi = 0; fi < sizeof(fs)/sizeof(*fs); ++fi) {
        unsigned f = fs[fi]; ByteSlice b = family(f);
        for (unsigned ri = 0; ri < sizeof(rs)/sizeof(*rs); ++ri) {
            unsigned r = rs[ri]; u64 *offsets = calloc(r ? r : 1, sizeof(*offsets)); assert(offsets);
            for (unsigned order = 0; order < 5; ++order) {
                u64 expected = 0, max_rank = 0;
                for (unsigned i = 0; i < r; ++i) {
                    u64 index = (u64)i * f / r;
                    if (order == 1) index = (u64)(r - 1 - i) * f / r;
                    if (order == 3) index = f - 1;
                    if (order == 4) index = 0;
                    offsets[i] = 24 * (index + 1) + 8; expected += index + 2;
                    if (index + 2 > max_rank) max_rank = index + 2;
                }
                if (order == 2) {
                    u64 seed = UINT64_C(0xd139f871de034caf);
                    for (unsigned i = r; i > 1; --i) { unsigned j = (unsigned)(random_step(&seed) % i); u64 t = offsets[i-1]; offsets[i-1] = offsets[j]; offsets[j] = t; }
                }
                for (unsigned mode = 0; mode < 3; ++mode) {
                    Arena a = {0}; CfiProbeCache c = {0};
                    for (unsigned i = 0; i < r; ++i) check_query(&a, &c, mode, b, offsets[i], 4, false);
                    assert(!c.overflow && c.calls == r);
                    if (mode == 0) assert(c.headers == expected);
                    if (mode == 2) assert(c.headers == max_rank);
                    printf("CFI_HELPER fdes=%u queries=%u order=%u mode=%u input_bytes=%llu query_bytes=%llu headers=%llu comparisons=%llu copied_entries=%llu allocated_entries=%llu\n",
                        f,r,order,mode,(unsigned long long)b.length,(unsigned long long)r * 8,
                        (unsigned long long)c.headers,(unsigned long long)c.comparisons,
                        (unsigned long long)c.copied,(unsigned long long)c.allocated);
                    release(&a);
                }
            }
            free(offsets);
        }
        free(b.pointer);
    }
}
static void boundaries(ByteSlice b) {
    static const u64 widths[] = {0,1,4,8,UINT64_MAX};
    for (unsigned m = 0; m < 3; ++m) {
        Arena a = {0}; CfiProbeCache c = {0};
        /* Descending then ascending queries keep the same cache. */
        for (unsigned direction = 0; direction < 2; ++direction)
            for (u64 i = 0; i <= b.length + 1; ++i)
                for (unsigned w = 0; w < sizeof(widths)/sizeof(*widths); ++w) {
                    u64 off = direction ? i : b.length + 1 - i;
                    check_query(&a, &c, m, b, off, widths[w], false);
                    check_query(&a, &c, m, b, off, widths[w], true);
                }
        release(&a);
    }
}
int main(void) {
    grid();
    ByteSlice b = family(3);
    for (u64 n = 0; n <= b.length; ++n) boundaries((ByteSlice){b.pointer,n});
    /* Held-out extended-length and malformed suffix cases. Cache is never
       reused after mutating the bytes. */
    u8 extra[48] = {0}; put32(extra, UINT32_MAX); put64(extra + 4, 4);
    put32(extra + 16, 4); put32(extra + 24, UINT32_MAX); put64(extra + 28, UINT64_MAX);
    boundaries((ByteSlice){extra,sizeof(extra)});
    for (unsigned n = 0; n < 12; ++n) boundaries((ByteSlice){extra,n});
    for (unsigned m = 0; m < 3; ++m) {
        Arena a = {0}; CfiProbeCache c = {0};
        check_query(&a,&c,m,(ByteSlice){NULL,0},0,0,false);
        check_query(&a,&c,m,(ByteSlice){NULL,4},0,4,false);
        release(&a);
    }
    /* Saturation is explicit, never wrapped and mistaken for a small count. */
    Arena a = {0}; CfiProbeCache c = {0}; c.headers = UINT64_MAX;
    u64 out = 0; assert(cfi_query(&a,&c,0,b,32,4,&out)); assert(c.overflow && c.headers == UINT64_MAX); release(&a);
    free(b.pointer);
    printf("CFI_HELPER_PASS checks=%llu\n", (unsigned long long)checks);
    return 0;
}
