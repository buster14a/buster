#pragma once

#include <buster/lib.h>

STRUCT(LinkArguments)
{
    OsString* objects;
    u64 object_count;
    String8* section_contents;
    String8* section_names;
    u16 section_count;
};
