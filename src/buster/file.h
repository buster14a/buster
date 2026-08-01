#pragma once
#include <buster/os.h>

typedef struct FileReadOptions FileReadOptions;
struct FileReadOptions
{
    u32 start_padding;
    u32 start_alignment;
    u32 end_padding;
    u32 end_alignment;
};

BUSTER_F_DECL ByteSlice file_read(Arena* arena, String8 path, FileReadOptions options);
BUSTER_F_DECL bool file_write(String8 path, ByteSlice content);

typedef struct CopyFileArguments CopyFileArguments;
struct CopyFileArguments
{
    String8 original_path;
    String8 new_path;
};
BUSTER_F_DECL bool file_copy(CopyFileArguments arguments);

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult file_tests(UnitTestArguments* arguments);
#endif

#if BUSTER_ANDROID
struct AAssetManager;
extern struct AAssetManager* buster_android_asset_manager;
extern String8 buster_android_internal_data_path;
#endif
