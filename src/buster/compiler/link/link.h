#pragma once

#include <buster/base.h>

typedef struct LinkArguments LinkArguments;
struct LinkArguments
{
    String8* objects;
    u64 object_count;
    String8* section_contents;
    String8* section_names;
    u16 section_count;
    u8 reserved[6];
};
