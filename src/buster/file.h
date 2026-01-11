#pragma once
#include <buster/os.h>

STRUCT(FileReadOptions)
{
    u32 start_padding;
    u32 start_alignment;
    u32 end_padding;
    u32 end_alignment;
};

BUSTER_DECL ByteSlice file_read(Arena* arena, StringOs path, FileReadOptions options);
BUSTER_DECL bool file_write(StringOs path, ByteSlice content);


STRUCT(CopyFileArguments)
{
    StringOs original_path;
    StringOs new_path;
};
BUSTER_DECL bool file_copy(CopyFileArguments arguments);
