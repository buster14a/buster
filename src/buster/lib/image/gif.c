// GIF87a/89a decoding into the image module's canonical RGBA8 canvas.
//
// image_gif_process validates the logical screen and walks the complete block
// stream so probes can report the exact image-descriptor count. The first
// image is composited over a transparent-black logical-screen canvas and clipped
// to it for image_decode. Later images and probes validate LZW through a
// no-output raster sink because the public API owns one image, not an animation
// timeline. gif_lzw_decode expands directly into the canvas, including the four
// GIF interlace passes, without an input-sized allocation. The frame ceiling
// counts image descriptors before their palette or raster is decoded. The
// block ceiling counts extension records and each nonempty size-prefixed data
// subblock; image descriptors are not charged again as blocks.
//
// Layout map:
//   GifSubblockReader, gif_subblock_read_byte  bounded data-block stream
//   GifLzwState, gif_lzw_decode                exact LZW dictionary machine
//   GifRasterWriter, gif_raster_write          interlace/canvas composition
//   gif_image_read                              descriptor and active palette
//   image_gif_process                          complete stream/public seam

#include <buster/lib/image/internal.h>

#define GIF_COLOR_CAPACITY 256u
#define GIF_LZW_CAPACITY 4096u

typedef struct GifColor GifColor;
struct GifColor
{
    u8 red;
    u8 green;
    u8 blue;
};

typedef struct GifPalette GifPalette;
struct GifPalette
{
    GifColor colors[GIF_COLOR_CAPACITY];
    u16 count;
    u8 bits;
    bool present;
};

typedef struct GifGraphicControl GifGraphicControl;
struct GifGraphicControl
{
    u8 transparent_index;
    bool transparency;
};

typedef struct GifSubblockReader GifSubblockReader;
struct GifSubblockReader
{
    ImageDecodeContext* context;
    u64 position;
    u64 block_end;
    u32 bit_buffer;
    u32 bit_count;
    bool terminated;
};

typedef struct GifRasterWriter GifRasterWriter;
struct GifRasterWriter
{
    ImageDecodeContext* context;
    GifPalette const* palette;
    u64 pixel_count;
    u64 produced;
    u32 left;
    u32 top;
    u32 width;
    u32 height;
    u32 x;
    u32 row;
    u32 pass;
    u8 transparent_index;
    bool transparency;
    bool transparent_pixel_in_canvas;
    bool interlaced;
    bool write_pixels;
};

typedef struct GifLzwState GifLzwState;
struct GifLzwState
{
    u16 prefixes[GIF_LZW_CAPACITY];
    u8 suffixes[GIF_LZW_CAPACITY];
    u8 stack[GIF_LZW_CAPACITY];
};

BUSTER_GLOBAL_LOCAL bool gif_palette_read(ImageByteReader* reader, GifPalette* palette, u32 count)
{
    bool result = reader && reader->context && palette && count >= 2 && count <= GIF_COLOR_CAPACITY;
    u64 offset = reader ? reader->position : 0;
    if (result)
    {
        result = image_decode_add_work(reader->context, (u64)count * 3, offset);
    }
    ByteSlice bytes = {0};
    if (result)
    {
        bytes = image_reader_slice(reader, (u64)count * 3);
        result = bytes.pointer != 0;
    }
    if (result)
    {
        for (u32 index = 0; index < count; index += 1)
        {
            palette->colors[index] = (GifColor){
                .red = bytes.pointer[index * 3],
                .green = bytes.pointer[index * 3 + 1],
                .blue = bytes.pointer[index * 3 + 2],
            };
        }
        palette->count = (u16)count;
        while ((1u << palette->bits) < count)
        {
            palette->bits += 1;
        }
        palette->present = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_subblock_read_byte(GifSubblockReader* reader, u8* value)
{
    bool result = reader && reader->context && value && reader->context->status == IMAGE_DECODE_SUCCESS && !reader->terminated;
    while (result && reader->position == reader->block_end)
    {
        u64 size_offset = reader->position;
        if (image_decode_range(reader->context->encoded, reader->position, 1))
        {
            u8 size = reader->context->encoded.pointer[reader->position];
            reader->position += 1;
            result = image_decode_add_work(reader->context, 1, size_offset);
            if (result && size)
            {
                result = image_decode_count_limit(reader->context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, size_offset);
            }
            if (result && size)
            {
                if (image_decode_range(reader->context->encoded, reader->position, size))
                {
                    reader->block_end = reader->position + size;
                }
                else
                {
                    image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
                    result = false;
                }
            }
            else if (result)
            {
                reader->terminated = true;
                result = false;
            }
        }
        else
        {
            image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
            result = false;
        }
    }
    if (result)
    {
        u64 byte_offset = reader->position;
        *value = reader->context->encoded.pointer[reader->position];
        reader->position += 1;
        result = image_decode_add_work(reader->context, 1, byte_offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_subblocks_finish(GifSubblockReader* reader)
{
    bool result = reader && reader->context && reader->context->status == IMAGE_DECODE_SUCCESS;
    if (result && !reader->terminated && reader->position < reader->block_end)
    {
        u64 remaining = reader->block_end - reader->position;
        result = image_decode_add_work(reader->context, remaining, reader->position);
        if (result)
        {
            reader->position = reader->block_end;
        }
    }
    while (result && !reader->terminated)
    {
        u64 size_offset = reader->position;
        if (image_decode_range(reader->context->encoded, reader->position, 1))
        {
            u8 size = reader->context->encoded.pointer[reader->position];
            reader->position += 1;
            result = image_decode_add_work(reader->context, 1, size_offset);
            if (result && size)
            {
                result = image_decode_count_limit(reader->context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, size_offset);
            }
            if (result && size)
            {
                if (image_decode_range(reader->context->encoded, reader->position, size))
                {
                    result = image_decode_add_work(reader->context, size, reader->position);
                    if (result)
                    {
                        reader->position += size;
                        reader->block_end = reader->position;
                    }
                }
                else
                {
                    image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
                    result = false;
                }
            }
            else if (result)
            {
                reader->terminated = true;
            }
        }
        else
        {
            image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
            result = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_skip_subblocks(ImageByteReader* reader)
{
    GifSubblockReader blocks = {
        .context = reader ? reader->context : 0,
        .position = reader ? reader->position : 0,
        .block_end = reader ? reader->position : 0,
    };
    bool result = reader && gif_subblocks_finish(&blocks);
    if (result)
    {
        reader->position = blocks.position;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_lzw_finish(GifSubblockReader* reader)
{
    bool result = reader && reader->context && reader->context->status == IMAGE_DECODE_SUCCESS;
    if (result && (reader->bit_count >= 8 || reader->position != reader->block_end))
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, reader->position);
        result = false;
    }
    u64 terminator_offset = reader ? reader->position : 0;
    u8 terminator = 0xff;
    if (result)
    {
        if (image_decode_range(reader->context->encoded, reader->position, 1))
        {
            terminator = reader->context->encoded.pointer[reader->position];
            reader->position += 1;
            result = image_decode_add_work(reader->context, 1, terminator_offset);
        }
        else
        {
            image_decode_error(reader->context, IMAGE_DECODE_TRUNCATED, reader->position);
            result = false;
        }
    }
    if (result && terminator != 0)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, terminator_offset);
        result = false;
    }
    if (result)
    {
        reader->terminated = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_lzw_read_code(GifSubblockReader* reader, u32 code_size, u16* code)
{
    bool result = reader && code && code_size >= 2 && code_size <= 12;
    while (result && reader->bit_count < code_size)
    {
        u8 byte = 0;
        if (gif_subblock_read_byte(reader, &byte))
        {
            reader->bit_buffer |= (u32)byte << reader->bit_count;
            reader->bit_count += 8;
        }
        else
        {
            result = false;
        }
    }
    if (result)
    {
        u32 mask = (1u << code_size) - 1u;
        *code = (u16)(reader->bit_buffer & mask);
        reader->bit_buffer >>= code_size;
        reader->bit_count -= code_size;
        result = image_decode_add_work(reader->context, 1, reader->position);
    }
    else if (reader && reader->context && reader->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, reader->position);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void gif_raster_advance_row(GifRasterWriter* writer)
{
    if (writer->interlaced)
    {
        static u8 const starts[4] = {0, 4, 2, 1};
        static u8 const steps[4] = {8, 8, 4, 2};
        writer->row += steps[writer->pass];
        while (writer->row >= writer->height && writer->pass < 3)
        {
            writer->pass += 1;
            writer->row = starts[writer->pass];
        }
    }
    else
    {
        writer->row += 1;
    }
}

BUSTER_GLOBAL_LOCAL bool gif_raster_write(GifRasterWriter* writer, u8 palette_index, u64 offset)
{
    bool result = writer && writer->context && writer->palette && writer->produced < writer->pixel_count;
    if (!result && writer && writer->context)
    {
        image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, offset);
    }
    if (result)
    {
        result = image_decode_add_work(writer->context, 1, offset);
    }
    if (result)
    {
        bool transparent = writer->transparency && palette_index == writer->transparent_index;
        if (writer->row < writer->height && (transparent || palette_index < writer->palette->count))
        {
            u64 destination_x = (u64)writer->left + writer->x;
            u64 destination_y = (u64)writer->top + writer->row;
            if (transparent && destination_x < writer->context->information.width &&
                destination_y < writer->context->information.height)
            {
                writer->transparent_pixel_in_canvas = true;
            }
            if (writer->write_pixels && !transparent &&
                destination_x < writer->context->information.width && destination_y < writer->context->information.height)
            {
                GifColor color = writer->palette->colors[palette_index];
                u64 destination = destination_y * writer->context->image.stride + destination_x * 4;
                u8* pixel = writer->context->image.pixels.pointer + destination;
                pixel[0] = color.red;
                pixel[1] = color.green;
                pixel[2] = color.blue;
                pixel[3] = 255;
            }
        }
        else
        {
            image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, offset);
            result = false;
        }
    }
    if (result)
    {
        writer->produced += 1;
        writer->x += 1;
        if (writer->x == writer->width)
        {
            writer->x = 0;
            gif_raster_advance_row(writer);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_lzw_emit_sequence(GifLzwState* state, GifRasterWriter* writer, u16 code, u16 available,
                                                u16 clear_code, u8* first, u64 offset)
{
    u32 stack_count = 0;
    bool result = state && writer && first && code < available;
    while (result && code >= clear_code)
    {
        if (code < available && stack_count < GIF_LZW_CAPACITY)
        {
            state->stack[stack_count] = state->suffixes[code];
            stack_count += 1;
            code = state->prefixes[code];
            result = image_decode_add_work(writer->context, 1, offset);
        }
        else
        {
            image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, offset);
            result = false;
        }
    }
    if (result)
    {
        *first = (u8)code;
        if (stack_count < GIF_LZW_CAPACITY)
        {
            state->stack[stack_count] = *first;
            stack_count += 1;
        }
        else
        {
            image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, offset);
            result = false;
        }
    }
    while (result && stack_count)
    {
        stack_count -= 1;
        result = gif_raster_write(writer, state->stack[stack_count], offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_lzw_decode(ImageByteReader* input, u8 minimum_code_size, GifRasterWriter* writer)
{
    GifLzwState state;
    GifSubblockReader blocks = {
        .context = input ? input->context : 0,
        .position = input ? input->position : 0,
        .block_end = input ? input->position : 0,
    };
    u16 clear_code = 0;
    u16 end_code = 0;
    u16 available = 0;
    u16 old_code = 0;
    u8 first = 0;
    u32 code_size = minimum_code_size + 1;
    bool old_code_valid = false;
    bool end_seen = false;
    bool result = input && writer && minimum_code_size >= 2 && minimum_code_size <= 8;
    if (result)
    {
        clear_code = (u16)(1u << minimum_code_size);
        end_code = (u16)(clear_code + 1);
        available = (u16)(end_code + 1);
    }

    while (result && !end_seen)
    {
        u16 code = 0;
        u64 code_offset = blocks.position;
        result = gif_lzw_read_code(&blocks, code_size, &code);
        if (result && code == clear_code)
        {
            available = (u16)(end_code + 1);
            code_size = minimum_code_size + 1;
            old_code_valid = false;
        }
        else if (result && code == end_code)
        {
            end_seen = true;
        }
        else if (result && !old_code_valid)
        {
            if (code < clear_code)
            {
                first = (u8)code;
                result = gif_raster_write(writer, first, code_offset);
                old_code = code;
                old_code_valid = result;
            }
            else
            {
                image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, code_offset);
                result = false;
            }
        }
        else if (result)
        {
            u16 input_code = code;
            if (code == available)
            {
                u8 sequence_first = 0;
                result = gif_lzw_emit_sequence(&state, writer, old_code, available, clear_code, &sequence_first, code_offset);
                if (result)
                {
                    result = gif_raster_write(writer, first, code_offset);
                    first = sequence_first;
                }
            }
            else if (code < available)
            {
                result = gif_lzw_emit_sequence(&state, writer, code, available, clear_code, &first, code_offset);
            }
            else
            {
                image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, code_offset);
                result = false;
            }

            if (result && available < GIF_LZW_CAPACITY)
            {
                state.prefixes[available] = old_code;
                state.suffixes[available] = first;
                available += 1;
                if (available == (1u << code_size) && code_size < 12)
                {
                    code_size += 1;
                }
            }
            if (result)
            {
                old_code = input_code;
            }
        }
    }

    if (result && (!end_seen || writer->produced != writer->pixel_count))
    {
        image_decode_error(writer->context, IMAGE_DECODE_MALFORMED, blocks.position);
        result = false;
    }
    if (result)
    {
        result = gif_lzw_finish(&blocks);
    }
    if (result)
    {
        input->position = blocks.position;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_canvas_initialize(ImageDecodeContext* context, u64 offset)
{
    bool result = image_decode_allocate_pixels(context, context->information.width, context->information.height, offset);
    u64 pixels = (u64)context->information.width * context->information.height;
    if (result)
    {
        result = image_decode_add_work(context, pixels, offset);
    }
    if (result)
    {
        memset(context->image.pixels.pointer, 0, pixels * 4);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_graphic_control_read(ImageByteReader* reader, GifGraphicControl* control)
{
    u64 offset = reader ? reader->position : 0;
    u8 block_size = 0;
    bool result = reader && control && image_reader_u8(reader, &block_size);
    if (result && block_size != 4)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, offset);
        result = false;
    }
    ByteSlice fields = {0};
    if (result)
    {
        result = image_decode_add_work(reader->context, 6, offset);
    }
    if (result)
    {
        fields = image_reader_slice(reader, 4);
        result = fields.pointer != 0;
    }
    u8 terminator = 0xff;
    if (result)
    {
        result = image_reader_u8(reader, &terminator);
    }
    if (result && terminator != 0)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, reader->position - 1);
        result = false;
    }
    u8 disposal_method = result ? (fields.pointer[0] >> 2u) & 7u : 0;
    if (result && ((fields.pointer[0] & 0xe0u) || disposal_method > 3))
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, offset + 1);
        result = false;
    }
    if (result)
    {
        control->transparency = (fields.pointer[0] & 1u) != 0;
        control->transparent_index = fields.pointer[3];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_extension_read(ImageByteReader* reader, GifGraphicControl* control)
{
    u64 offset = reader ? reader->position : 0;
    u8 label = 0;
    bool result = reader && control && image_reader_u8(reader, &label);
    if (result && label == 0xf9)
    {
        result = gif_graphic_control_read(reader, control);
    }
    else if (result && (label == 0x01 || label == 0xff))
    {
        u64 header_offset = reader->position;
        u8 expected_size = label == 0x01 ? 12 : 11;
        u8 header_size = 0;
        result = image_reader_u8(reader, &header_size);
        if (result && header_size != expected_size)
        {
            image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, header_offset);
            result = false;
        }
        if (result)
        {
            result = image_decode_add_work(reader->context, (u64)header_size + 1, header_offset) &&
                     image_reader_skip(reader, header_size) && gif_skip_subblocks(reader);
        }
        if (result && label == 0x01)
        {
            *control = (GifGraphicControl){0};
        }
    }
    else if (result)
    {
        result = gif_skip_subblocks(reader);
    }
    if (!result && reader && reader->context && reader->context->status == IMAGE_DECODE_SUCCESS)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, offset);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gif_image_read(ImageByteReader* reader, GifPalette const* global_palette,
                                         GifGraphicControl control, u32* frame_count, u8* source_bits, bool* has_alpha)
{
    u64 descriptor_offset = reader ? reader->position - 1 : 0;
    ByteSlice descriptor = {0};
    bool result = reader && global_palette && frame_count && source_bits && has_alpha;
    if (result)
    {
        result = image_decode_count_limit(reader->context, IMAGE_EXCEEDED_LIMIT_FRAMES, 1, descriptor_offset);
    }
    if (result && reader->context->limit_counters.frames > UINT32_MAX)
    {
        image_decode_set_exceeded_limit(reader->context, IMAGE_EXCEEDED_LIMIT_FRAMES,
                                        reader->context->limit_counters.frames, UINT32_MAX, descriptor_offset);
        result = false;
    }
    if (result)
    {
        descriptor = image_reader_slice(reader, 9);
        result = descriptor.pointer != 0;
    }

    u32 left = 0;
    u32 top = 0;
    u32 width = 0;
    u32 height = 0;
    bool local_table_present = false;
    bool interlaced = false;
    u32 local_count = 0;
    if (result)
    {
        left = image_decode_u16_le(descriptor.pointer);
        top = image_decode_u16_le(descriptor.pointer + 2);
        width = image_decode_u16_le(descriptor.pointer + 4);
        height = image_decode_u16_le(descriptor.pointer + 6);
        u8 packed = descriptor.pointer[8];
        local_table_present = (packed & 0x80u) != 0;
        interlaced = (packed & 0x40u) != 0;
        local_count = 1u << ((packed & 7u) + 1u);
        if (packed & 0x18u)
        {
            image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, descriptor_offset + 9);
            result = false;
        }
        else if (!width || !height)
        {
            image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, descriptor_offset);
            result = false;
        }
    }

    GifPalette local_palette = {0};
    if (result && local_table_present)
    {
        result = gif_palette_read(reader, &local_palette, local_count);
    }
    GifPalette const* palette = local_table_present ? &local_palette : global_palette;
    if (result && !palette->present)
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, descriptor_offset);
        result = false;
    }
    u8 minimum_code_size = 0;
    if (result)
    {
        result = image_reader_u8(reader, &minimum_code_size);
    }
    if (result && (minimum_code_size < 2 || minimum_code_size > 8))
    {
        image_decode_error(reader->context, IMAGE_DECODE_MALFORMED, reader->position - 1);
        result = false;
    }

    bool first_frame = *frame_count == 0;
    bool write_pixels = first_frame && reader->context->decode_pixels;
    bool transparent_pixel_in_canvas = false;
    if (result && write_pixels)
    {
        result = gif_canvas_initialize(reader->context, descriptor_offset);
    }
    if (result)
    {
        if (first_frame)
        {
            *source_bits = palette->bits;
        }
        GifRasterWriter writer = {
            .context = reader->context,
            .palette = palette,
            .pixel_count = (u64)width * height,
            .left = left,
            .top = top,
            .width = width,
            .height = height,
            .transparent_index = control.transparent_index,
            .transparency = control.transparency,
            .interlaced = interlaced,
            .write_pixels = write_pixels,
        };
        result = gif_lzw_decode(reader, minimum_code_size, &writer);
        transparent_pixel_in_canvas = writer.transparent_pixel_in_canvas;
    }

    if (result)
    {
        *frame_count += 1;
        bool covers_canvas = left == 0 && top == 0 &&
                             width >= reader->context->information.width && height >= reader->context->information.height;
        if (first_frame)
        {
            // Metadata describes the returned first-frame canvas. Alpha used
            // only by later animation frames does not affect it.
            *has_alpha = transparent_pixel_in_canvas || !covers_canvas;
        }
    }
    return result;
}

void image_gif_process(ImageDecodeContext* context)
{
    ImageByteReader reader = {.context = context};
    ByteSlice header = image_reader_slice(&reader, 6);
    bool result = header.pointer != 0;
    if (result && memcmp(header.pointer, "GIF87a", 6) != 0 && memcmp(header.pointer, "GIF89a", 6) != 0)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, 0);
        result = false;
    }

    ByteSlice screen = {0};
    if (result)
    {
        screen = image_reader_slice(&reader, 7);
        result = screen.pointer != 0;
    }

    u32 width = 0;
    u32 height = 0;
    bool global_table_present = false;
    u32 global_count = 0;
    if (result)
    {
        width = image_decode_u16_le(screen.pointer);
        height = image_decode_u16_le(screen.pointer + 2);
        u8 packed = screen.pointer[4];
        global_table_present = (packed & 0x80u) != 0;
        global_count = 1u << ((packed & 7u) + 1u);
        result = image_decode_set_information(context, width, height, 0, 1, 1, false,
                                              IMAGE_ORIENTATION_TOP_LEFT, 6);
        if (result)
        {
            context->information.source_color_model = IMAGE_COLOR_MODEL_INDEXED;
        }
    }

    GifPalette global_palette = {0};
    if (result && global_table_present)
    {
        result = gif_palette_read(&reader, &global_palette, global_count);
    }

    GifGraphicControl control = {0};
    u32 frame_count = 0;
    u8 source_bits = 1;
    bool has_alpha = false;
    bool trailer_seen = false;
    while (result && !trailer_seen)
    {
        u8 marker = 0;
        u64 marker_offset = reader.position;
        result = image_reader_u8(&reader, &marker);
        if (result && marker == 0x2c)
        {
            result = gif_image_read(&reader, &global_palette, control, &frame_count, &source_bits, &has_alpha);
            control = (GifGraphicControl){0};
        }
        else if (result && marker == 0x21)
        {
            result = image_decode_count_limit(context, IMAGE_EXCEEDED_LIMIT_BLOCKS, 1, marker_offset);
            if (result)
            {
                result = gif_extension_read(&reader, &control);
            }
        }
        else if (result && marker == 0x3b)
        {
            trailer_seen = true;
        }
        else if (result)
        {
            image_decode_error(context, IMAGE_DECODE_MALFORMED, marker_offset);
            result = false;
        }
    }

    if (result && !frame_count)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, reader.position);
        result = false;
    }
    if (result && reader.position != context->encoded.length)
    {
        image_decode_error(context, IMAGE_DECODE_MALFORMED, reader.position);
        result = false;
    }
    if (result)
    {
        result = image_decode_set_information(context, width, height, frame_count, 1, source_bits, has_alpha,
                                              IMAGE_ORIENTATION_TOP_LEFT, 6);
        if (result)
        {
            context->information.source_color_model = IMAGE_COLOR_MODEL_INDEXED;
            context->information.is_animated = frame_count > 1;
            context->information.has_more_images = frame_count > 1;
        }
    }
}
