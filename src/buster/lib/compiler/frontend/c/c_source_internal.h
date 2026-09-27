#pragma once

// Private source-map and once-file table contracts. Test hooks are absent
// from tests-disabled builds; file identity remains separate from spelling.
#include <buster/lib/compiler/frontend/c/c.h>

typedef struct CIncludeFileIdentity CIncludeFileIdentity;
struct CIncludeFileIdentity
{
    // Path key for builtins and other non-filesystem namespaces. Filesystem
    // bytes use the identity captured from their supplying descriptor instead;
    // physical identities compare their device/index pair and ignore this path.
    String8 path;
    u64 device;
    u64 index;
    bool physical;
};

// One identity record drives all three ways a file can suppress a later
// inclusion: #import, #pragma once, and a proven whole-file include guard.
// `spelling` remains the first resolved path for diagnostics/source maps while
// `identity` is the comparison key. Keeping those roles separate prevents a
// canonical identity from degrading a useful diagnostic spelling.
typedef struct CIncludeFileEntry CIncludeFileEntry;
struct CIncludeFileEntry
{
    // With a path key this is both the key and diagnostic spelling. With a
    // physical key it remains only the spelling users should see.
    String8 spelling;
    u64 hash;
    u64 device;
    u64 index;
    u32 guard_symbol;
    bool physical;
    bool once;
};

typedef struct CIncludeFileTable CIncludeFileTable;
struct CIncludeFileTable
{
    Arena* arena;
    CIncludeFileEntry* entries;
    u32 count;
    u32 capacity;
#if BUSTER_INCLUDE_TESTS
    // Actual slot examinations, including reinsertion while growing.
    u64 probe_count;
#endif
};

typedef enum CIncludeFileStatus
{
    C_INCLUDE_FILE_OK,
    C_INCLUDE_FILE_INVALID_IDENTITY,
    C_INCLUDE_FILE_ALLOCATION_FAILED,
} CIncludeFileStatus;

#define C_INCLUDE_FILE_INITIAL_CAPACITY 64

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CIncludeFileStatus c_test_include_file_entry(CIncludeFileTable* table, CIncludeFileIdentity identity, String8 spelling, CIncludeFileEntry** entry_out);
BUSTER_F_DECL bool c_test_include_file_table_grow(CIncludeFileTable* table);
BUSTER_F_DECL void c_test_source_map_sort(Arena* arena, IrSourceRegion* regions, u32 count);
// Test the private append-only finalization boundary, not arbitrary map edits.
BUSTER_F_DECL void c_test_source_map_publish_appended(Arena* arena, CSourceMapRecovery* recovery, IrSourceRegion* regions, u32 count, u32 capacity);
#endif
