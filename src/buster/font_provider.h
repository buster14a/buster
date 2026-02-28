#pragma once

#include <buster/base.h>

ENUM(FontIndex,
    FONT_INDEX_MONO,
    FONT_INDEX_COUNT,
);

BUSTER_DECL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create);
BUSTER_DECL uint2 texture_atlas_compute_string_rect(String8 string, const FontTextureAtlasDescription* atlas);
BUSTER_IMPL StringOs font_file_get_path(Arena* arena, FontIndex index);
