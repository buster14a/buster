#pragma once

#include <buster/lib/compiler/frontend/c/c.h>

// Private source-metrics attribution table. Rows retain borrowed paths for the
// lifetime of preprocessing; slot_rows holds indices, not pointers into the
// growable row array. Hash matches require equal path lengths and bytes.
// c_source_metrics_file_row owns ordinary hashing, probing and table growth.
// The test-only entry point supplies a hash to the same lookup implementation.
typedef struct CSourceMetricsFileSet CSourceMetricsFileSet;
struct CSourceMetricsFileSet
{
    u64* hashes;
    u32* slot_rows;
    CSourceFileMetrics* rows;
    u32 capacity;
    u32 count;
    u32 row_capacity;
};

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL u32 c_test_source_metrics_file_row(Arena* arena, CSourceMetricsFileSet* set, String8 path, u64 hash);
#endif
