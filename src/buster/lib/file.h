#pragma once
#include <buster/lib/os.h>

typedef struct FileReadOptions FileReadOptions;
struct FileReadOptions
{
    u32 start_padding;
    u32 start_alignment;
    u32 end_padding;
    u32 end_alignment;
};

typedef struct FileMapRead FileMapRead;
struct FileMapRead
{
    ByteSlice bytes;
    void* mapped_pointer;
    u64 mapped_size;
    void* mapped_handle;
};

BUSTER_F_DECL ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL FileMapRead file_map_read(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL void file_map_unmap(FileMapRead map);
BUSTER_F_DECL bool file_write(String8 path, ByteSlice content);

typedef struct CopyFileArguments CopyFileArguments;
struct CopyFileArguments
{
    String8 original_path;
    String8 new_path;
};
BUSTER_F_DECL bool file_copy(CopyFileArguments arguments);


#if BUSTER_ANDROID
struct AAssetManager;
extern struct AAssetManager* buster_android_asset_manager;
extern String8 buster_android_internal_data_path;
#endif
