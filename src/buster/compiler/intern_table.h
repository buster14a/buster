#pragma once
#include <buster/base.h>

STRUCT(InternIndex)
{
    u32 v;
};

STRUCT(InternTable)
{
    Arena* string_arena;
    Arena* slot_arena;
    Arena* hash_table_arena;
    u32 hash_table_item_count;
    u32 reserved;
};

BUSTER_F_DECL InternTable intern_table_create();
BUSTER_F_IMPL InternIndex table_intern(InternTable* restrict table, String8 string);
