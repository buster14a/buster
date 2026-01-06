#pragma once

#include <buster/base.h>
#include <buster/string_os.h>

STRUCT(LinkArguments)
{
    StringOs* objects;
    u64 object_count;
    String8* section_contents;
    String8* section_names;
    u16 section_count;
    u8 reserved[6];
};
