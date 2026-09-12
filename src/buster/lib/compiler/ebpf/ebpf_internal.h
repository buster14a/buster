#pragma once

#include <buster/lib/compiler/ebpf/ebpf.h>

#if BUSTER_INCLUDE_TESTS
// Private semantic probe through the production index. Work is counted only
// in the existing BUSTER_BENCH_ALLOCATIONS build; ordinary lookups have no
// counter storage or updates. No ELF or public-backend interface is extended.
typedef struct EbpfSymbolIndexProbe EbpfSymbolIndexProbe;
struct EbpfSymbolIndexProbe
{
    u64 lookup_steps;
    u32 symbol_count;
    bool valid;
    bool invalid_key_rejected;
    bool full_count_rejected;
};

BUSTER_F_DECL EbpfSymbolIndexProbe ebpf_test_symbol_index(Arena* arena, u32 count);
#endif
