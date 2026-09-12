#pragma once
// Private archive-extraction state. Names borrow the invocation's ObjectFiles;
// the caller destroys arena after its last archive (also on driver failure).
#include <buster/lib/compiler/object/object.h>

typedef struct CompilerDriverArchiveSymbol CompilerDriverArchiveSymbol;
struct CompilerDriverArchiveSymbol
{
    String8 name;
    u64 providers;
    u32 flags;
};

typedef struct CompilerDriverArchiveState CompilerDriverArchiveState;
struct CompilerDriverArchiveState
{
    Arena* arena;
    CompilerDriverArchiveSymbol* symbols;
    u64 capacity;
    u64 count;
    u32 processed_objects;
};

BUSTER_F_DECL void compiler_driver_archive_extract(Arena* arena, CompilerDriverArchiveState* state, ObjectArchive* archive,
                                                   ObjectFile* objects, u32* object_count);
