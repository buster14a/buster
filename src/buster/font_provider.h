#pragma once

#include <buster/base.h>

ENUM(FontIndex,
    FONT_INDEX_MONO);

BUSTER_F_DECL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create);
BUSTER_F_DECL uint2 texture_atlas_compute_string_rect(String8 string, const FontTextureAtlasDescription* atlas);
BUSTER_F_DECL StringOs font_file_get_path(Arena* arena, FontIndex index);
