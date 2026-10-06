#pragma once

#include <buster/lib/base.h>

typedef enum FontIndex
{
    FONT_INDEX_MONO,
    FONT_INDEX_COUNT,
} FontIndex;

// Reads the font file and rasterizes its printable ASCII glyphs into an atlas
// (see truetype_font_atlas_build). An unusable font, an out-of-range
// text_height or a glyph that does not fit the atlas fails the process through
// os_fail_message_format in every build; the result is never a partial atlas.
BUSTER_F_DECL FontTextureAtlasDescription font_texture_atlas_create(Arena* arena, FontTextureAtlasCreate create);
BUSTER_F_DECL uint2 texture_atlas_compute_string_rect(String8 string, const FontTextureAtlasDescription* atlas);
BUSTER_F_DECL String8 font_file_get_path(FontIndex index);
// Runs system font discovery on the calling thread so its resolved paths are
// complete before any gang queries them; font_file_get_path() otherwise does
// the discovery at its first call, unsynchronized.
BUSTER_F_DECL void font_provider_prewarm(void);
