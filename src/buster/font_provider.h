#pragma once

#include <buster/base.h>

typedef enum FontIndex
{
    FONT_INDEX_MONO,
    FONT_INDEX_COUNT,
} FontIndex;

BUSTER_F_DECL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create);
BUSTER_F_DECL uint2 texture_atlas_compute_string_rect(String8 string, const FontTextureAtlasDescription* atlas);
BUSTER_F_DECL StringOs font_file_get_path(Arena* arena, FontIndex index);
