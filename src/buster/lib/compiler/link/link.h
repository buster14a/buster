#pragma once

#include <buster/lib/compiler/object/object.h>

typedef enum LinkError
{
    LINK_ERROR_NONE,
    LINK_ERROR_INVALID_INPUT,
    LINK_ERROR_TARGET_MISMATCH,
    LINK_ERROR_DUPLICATE_SYMBOL,
    LINK_ERROR_UNRESOLVED_SYMBOL,
    LINK_ERROR_OBJECT_WRITE,
    LINK_ERROR_FILE_WRITE,
    LINK_ERROR_PROCESS_SPAWN,
    LINK_ERROR_PROCESS_FAILED,
    LINK_ERROR_UNSUPPORTED_HOST,
    LINK_ERROR_UNSUPPORTED_FEATURE,
    LINK_ERROR_ENTRY_SYMBOL,
    LINK_ERROR_RELOCATION,
    LINK_ERROR_COUNT,
} LinkError;

typedef struct LinkOptions LinkOptions;
struct LinkOptions
{
    bool allow_undefined_symbols;
    u8 reserved[7];
};

typedef struct LinkObjectResult LinkObjectResult;
struct LinkObjectResult
{
    ObjectFile object;
    String8 symbol;
    LinkError error;
};

typedef struct NativeExecutableLinkOptions NativeExecutableLinkOptions;
typedef struct NativeDynamicLibrary NativeDynamicLibrary;
struct NativeDynamicLibrary
{
    String8 name;
    String8* exported_symbols;
    u32 exported_symbol_count;
    bool exports_known;
    u8 reserved[3];
};

struct NativeExecutableLinkOptions
{
    String8 output_path;
    String8 entry_symbol;
    String8 sysroot;
    String8* library_paths;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    NativeDynamicLibrary* dynamic_libraries;
    String8* runtime_exported_symbols;
    u32 library_path_count;
    u32 framework_path_count;
    u32 framework_count;
    u32 linker_argument_count;
    u32 dynamic_library_count;
    u32 runtime_exported_symbol_count;
    bool runtime_exports_known;
    bool debug_info;
    u8 reserved[6];
};

typedef struct NativeExecutableLinkResult NativeExecutableLinkResult;
struct NativeExecutableLinkResult
{
    ByteSlice executable;
    ByteSlice pdb;
    String8 pdb_path;
    String8 symbol;
    LinkError error;
};

BUSTER_F_DECL LinkObjectResult link_objects(Arena* arena, ObjectFile* objects, u32 object_count, LinkOptions options);
BUSTER_F_DECL NativeExecutableLinkResult link_native_executable(Arena* arena, ObjectFile* object, NativeExecutableLinkOptions options);
BUSTER_F_DECL ByteSlice link_pe_resolved_codeview(Arena* arena, ObjectFile* object, ObjectDebugModule* debug_module,
                                                       u32 const* object_output_sections, u64 const* object_section_offsets,
                                                       u32 output_section_count);
