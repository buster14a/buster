#pragma once

#include <buster/lib/base.h>

typedef enum FontIndex
{
    FONT_INDEX_MONO,
    FONT_INDEX_COUNT,
} FontIndex;

BUSTER_F_DECL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create);
BUSTER_F_DECL uint2 texture_atlas_compute_string_rect(String8 string, const FontTextureAtlasDescription* atlas);
BUSTER_F_DECL String8 font_file_get_path(FontIndex index);
// Runs system font discovery on the calling thread so its resolved paths are
// complete before any gang queries them; font_file_get_path() otherwise does
// the discovery at its first call, unsynchronized.
BUSTER_F_DECL void font_provider_prewarm(void);
