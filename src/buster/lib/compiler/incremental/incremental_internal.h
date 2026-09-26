#pragma once

// Private seam of incremental.c for its tests: the artifact and pack codecs
// and the container shapes they exchange. Production callers use
// incremental.h; nothing here is a stable interface.

#include <buster/lib/compiler/incremental/incremental.h>

// The three record sections a miss is attributed to, in comparison order.
typedef enum IncrementalSection
{
    INCREMENTAL_SECTION_BODY,
    INCREMENTAL_SECTION_TYPES,
    INCREMENTAL_SECTION_SYMBOLS,
    INCREMENTAL_SECTION_COUNT,
} IncrementalSection;

// One pack entry: a record and the artifact it names. The fingerprint indexes
// the entry table; the record bytes decide reuse.
typedef struct IncrementalPackEntry IncrementalPackEntry;
struct IncrementalPackEntry
{
    u64 fingerprint;
    ByteSlice record;
    ByteSlice artifact;
};

// One function of the compilation that published the pack, by name, with
// the fingerprints of its record sections. Used only to explain a miss.
typedef struct IncrementalManifestEntry IncrementalManifestEntry;
struct IncrementalManifestEntry
{
    String8 name;
    u64 fingerprint;
    u64 sections[INCREMENTAL_SECTION_COUNT];
    u32 entry_index;
    u8 reserved[4];
};

typedef struct IncrementalPack IncrementalPack;
struct IncrementalPack
{
    ByteSlice context;
    IncrementalPackEntry* entries;
    IncrementalManifestEntry* manifest;
    u32 entry_count;
    u32 manifest_count;
};

BUSTER_F_DECL ByteSlice incremental_artifact_encode(Arena* arena, IncrementalFunctionArtifact const* artifact);
// Validates every count against the remaining bytes and every index against
// `function`; a false result leaves nothing the caller may use.
BUSTER_F_DECL bool incremental_artifact_decode(Arena* arena, ByteSlice bytes, IrFunction const* function, u32 symbol_slot_count,
                                               IncrementalFunctionArtifact* artifact);
BUSTER_F_DECL ByteSlice incremental_pack_encode(Arena* arena, ByteSlice context, IncrementalPackEntry* entries, u32 entry_count,
                                                IncrementalManifestEntry* manifest, u32 manifest_count);
BUSTER_F_DECL IncrementalPackStatus incremental_pack_decode(Arena* arena, ByteSlice file, IncrementalPack* pack);
// The entry whose record equals `record` byte for byte, or UINT32_MAX.
BUSTER_F_DECL u32 incremental_pack_find_entry(IncrementalPack const* pack, u64 fingerprint, ByteSlice record);
// Where the pack of `input_path` lives inside `directory`, null-terminated.
BUSTER_F_DECL String8 incremental_pack_path(Arena* arena, String8 directory, String8 input_path);
