#include <buster/rendering/internal.h>

extern id MTLCreateSystemDefaultDevice(void);
extern int setenv(const char* name, const char* value, int overwrite);

typedef struct BusterMTLOrigin BusterMTLOrigin;
struct BusterMTLOrigin
{
    BusterNSUInteger x;
    BusterNSUInteger y;
    BusterNSUInteger z;
};
typedef struct BusterMTLSize BusterMTLSize;
struct BusterMTLSize
{
    BusterNSUInteger width;
    BusterNSUInteger height;
    BusterNSUInteger depth;
};
typedef struct BusterMTLRegion BusterMTLRegion;
struct BusterMTLRegion
{
    BusterMTLOrigin origin;
    BusterMTLSize size;
};
typedef struct BusterMTLViewport BusterMTLViewport;
struct BusterMTLViewport
{
    double originX;
    double originY;
    double width;
    double height;
    double znear;
    double zfar;
};
typedef struct BusterMTLScissorRect BusterMTLScissorRect;
struct BusterMTLScissorRect
{
    BusterNSUInteger x;
    BusterNSUInteger y;
    BusterNSUInteger width;
    BusterNSUInteger height;
};
typedef struct BusterMTLClearColor BusterMTLClearColor;
struct BusterMTLClearColor
{
    double red;
    double green;
    double blue;
    double alpha;
};

enum
{
    BUSTER_MTL_PIXEL_FORMAT_R8_UNORM = 10,
    BUSTER_MTL_PIXEL_FORMAT_RGBA8_UNORM = 70,
    BUSTER_MTL_PIXEL_FORMAT_BGRA8_UNORM = 80,
    BUSTER_MTL_LOAD_ACTION_CLEAR = 2,
    BUSTER_MTL_STORE_ACTION_STORE = 1,
    BUSTER_MTL_PRIMITIVE_TYPE_TRIANGLE = 3,
    BUSTER_MTL_INDEX_TYPE_UINT32 = 1,
    BUSTER_MTL_BLEND_OPERATION_ADD = 0,
    BUSTER_MTL_BLEND_FACTOR_ZERO = 0,
    BUSTER_MTL_BLEND_FACTOR_ONE = 1,
    BUSTER_MTL_BLEND_FACTOR_SOURCE_ALPHA = 4,
    BUSTER_MTL_BLEND_FACTOR_ONE_MINUS_SOURCE_ALPHA = 5,
    BUSTER_MTL_SAMPLER_MIN_MAG_FILTER_LINEAR = 1,
    BUSTER_MTL_SAMPLER_MIP_FILTER_NOT_MIPMAPPED = 0,
    BUSTER_MTL_SAMPLER_ADDRESS_MODE_REPEAT = 2,
    BUSTER_MTL_TEXTURE_USAGE_SHADER_READ = 1,
    BUSTER_MTL_STORAGE_MODE_SHARED = 0,
};

#define BUSTER_METAL_FRAME_COUNT (2)
#define MAX_METAL_TEXTURE_COUNT (16)
#define BUSTER_METAL_RECT_TEXTURE_SLOT_COUNT (2)
#define BUSTER_METAL_STRINGIFY_HELPER(x) #x
#define BUSTER_METAL_STRINGIFY(x) BUSTER_METAL_STRINGIFY_HELPER(x)

BUSTER_CT_CHECK(BUSTER_METAL_RECT_TEXTURE_SLOT_COUNT == RECT_TEXTURE_SLOT_COUNT);

typedef enum BufferType
{
    BUFFER_TYPE_VERTEX,
    BUFFER_TYPE_INDEX,
    BUFFER_TYPE_COUNT,
} BufferType;

typedef struct MetalBuffer MetalBuffer;
struct MetalBuffer
{
    id resource;
    u8* mapped;
    u64 size;
    BufferType type;
    u8 reserved[4];
};

typedef struct VertexBuffer VertexBuffer;
struct VertexBuffer
{
    MetalBuffer gpu;
    Arena* cpu;
    u32 count;
    u8 reserved[4];
};

typedef struct IndexBuffer IndexBuffer;
struct IndexBuffer
{
    MetalBuffer gpu;
    Arena* cpu;
};

typedef struct FramePipelineInstantiation FramePipelineInstantiation;
struct FramePipelineInstantiation
{
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
};

typedef struct WindowFrame WindowFrame;
struct WindowFrame
{
    id command_buffer;
    FramePipelineInstantiation pipeline_instantiations[(u64)BUSTER_PIPELINE_COUNT];
};

typedef struct MetalTexture MetalTexture;
struct MetalTexture
{
    id resource;
    u32 width;
    u32 height;
};

typedef struct RenderingHandle RenderingHandle;
struct RenderingHandle
{
    id device;
    id command_queue;
    id library;
    id rect_pipeline;
    id sampler_state;
    u32 texture_count;
    u32 reserved;
    FontTextureAtlas fonts[RENDER_FONT_TYPE_COUNT];
    MetalTexture textures[MAX_METAL_TEXTURE_COUNT];
};

typedef struct RenderingWindowHandle RenderingWindowHandle;
struct RenderingWindowHandle
{
    id ns_window;
    id content_view;
    id layer;
    u32 width;
    u32 height;
    u32 frame_index;
    u32 frame_count;
    TextureIndex rect_textures[RECT_TEXTURE_SLOT_COUNT];
    WindowFrame frames[BUSTER_METAL_FRAME_COUNT];
};

BUSTER_GLOBAL_LOCAL RenderingHandle rendering_handle = {0};
BUSTER_GLOBAL_LOCAL u32 metal_drawable_size_log_count = 0;
BUSTER_GLOBAL_LOCAL u32 metal_frame_begin_log_count = 0;
BUSTER_GLOBAL_LOCAL u32 metal_frame_end_log_count = 0;

#if defined(__x86_64__)
#define metal_msg_send_stret objc_msgSend_stret
#else
#define metal_msg_send_stret objc_msgSend
#endif

BUSTER_GLOBAL_LOCAL SEL metal_sel(const char* name)
{
    return sel_registerName(name);
}

BUSTER_GLOBAL_LOCAL id metal_msg_id(id receiver, const char* selector)
{
    return ((id (*)(id, SEL))objc_msgSend)(receiver, metal_sel(selector));
}

BUSTER_GLOBAL_LOCAL void metal_msg_void(id receiver, const char* selector)
{
    ((void (*)(id, SEL))objc_msgSend)(receiver, metal_sel(selector));
}

BUSTER_GLOBAL_LOCAL void metal_msg_void_id(id receiver, const char* selector, id argument)
{
    ((void (*)(id, SEL, id))objc_msgSend)(receiver, metal_sel(selector), argument);
}

BUSTER_GLOBAL_LOCAL void metal_msg_void_bool(id receiver, const char* selector, bool argument)
{
    ((void (*)(id, SEL, bool))objc_msgSend)(receiver, metal_sel(selector), argument);
}

BUSTER_GLOBAL_LOCAL void metal_msg_void_ulong(id receiver, const char* selector, BusterNSUInteger argument)
{
    ((void (*)(id, SEL, BusterNSUInteger))objc_msgSend)(receiver, metal_sel(selector), argument);
}

BUSTER_GLOBAL_LOCAL id metal_nsstring_from_cstring(const char* string)
{
    id ns_string_class = (id)objc_getClass("NSString");
    id allocated = metal_msg_id(ns_string_class, "alloc");
    return ((id (*)(id, SEL, const char*))objc_msgSend)(allocated, metal_sel("initWithUTF8String:"), string);
}

BUSTER_GLOBAL_LOCAL const char* metal_utf8_string(id ns_string)
{
    return ((const char* (*)(id, SEL))objc_msgSend)(ns_string, metal_sel("UTF8String"));
}

BUSTER_GLOBAL_LOCAL void metal_release(id object)
{
    if (object)
    {
        metal_msg_void(object, "release");
    }
}

BUSTER_GLOBAL_LOCAL const char* metal_rect_inline_shader_source(void)
{
    return "#include <metal_stdlib>\n"
           "using namespace metal;\n"
           "struct RectVertex { float2 p0; float2 uv0; float2 extent; float corner_radius; float softness; float4 colors[4]; uint texture_index; uint "
           "reserved0; uint reserved1; uint reserved2; };\n"
           "struct DrawConstants { float width; float height; };\n"
           "struct VertexOut { float4 position [[position]]; uint texture_index [[flat]]; float4 color; float2 uv; float2 pixel_position; float2 center; "
           "float2 half_size; float corner_radius; float softness; };\n"
           "constant float2 quad_vertices[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };\n"
           "vertex VertexOut rect_vs(uint vertex_id [[vertex_id]], const device RectVertex* vertices_buffer [[buffer(0)]], constant DrawConstants& constants "
           "[[buffer(1)]]) {\n"
           "    VertexOut output; RectVertex v = vertices_buffer[vertex_id]; uint quad_vertex_id = vertex_id % 4; float2 extent = v.extent; float2 p0 = v.p0; "
           "float2 p1 = p0 + extent; float2 center = (p1 + p0) * 0.5; float2 half_size = (p1 - p0) * 0.5; float2 position = quad_vertices[quad_vertex_id] * "
           "half_size + center;\n"
           "    output.position = float4(2.0 * position.x / constants.width - 1.0, 1.0 - 2.0 * position.y / constants.height, 0.0, 1.0);\n"
           "    float2 uv0 = v.uv0; float2 uv1 = uv0 + extent; float2 texture_center = (uv1 + uv0) * 0.5; output.uv = quad_vertices[quad_vertex_id] * "
           "half_size + texture_center;\n"
           "    output.texture_index = v.texture_index; output.color = v.colors[quad_vertex_id]; output.pixel_position = position; output.center = center; "
           "output.half_size = half_size; output.corner_radius = v.corner_radius; output.softness = v.softness; return output; }\n"
           "float rounded_rect_sdf(float2 position, float2 center, float2 half_size, float radius) { float2 r2 = float2(radius, radius); float2 d2_no_r2 = "
           "abs(center - position) - half_size; float2 d2 = d2_no_r2 + r2; float negative_distance = min(max(d2.x, d2.y), 0.0); float positive_distance = "
           "length(max(d2, 0.0)); return negative_distance + positive_distance - radius; }\n"
           "fragment float4 rect_fs(VertexOut input [[stage_in]], array<texture2d<float>, " BUSTER_METAL_STRINGIFY(
               BUSTER_METAL_RECT_TEXTURE_SLOT_COUNT) "> textures [[texture(0)]], sampler texture_sampler [[sampler(0)]]) { uint texture_index = "
                                                     "input.texture_index; float2 texture_size = float2(textures[texture_index].get_width(), "
                                                     "textures[texture_index].get_height()); float2 uv = float2(input.uv.x / texture_size.x, input.uv.y / "
                                                     "texture_size.y); float4 sampled = textures[texture_index].sample(texture_sampler, uv); float softness = "
                                                     "input.softness; float softness_padding_scalar = max(0.0, softness * 2.0 - 1.0); float distance = "
                                                     "rounded_rect_sdf(input.pixel_position, input.center, input.half_size - float2(softness_padding_scalar, "
                                                     "softness_padding_scalar), input.corner_radius); float sdf_factor = 1.0 - smoothstep(0.0, 2.0 * softness, "
                                                     "distance); return input.color * sampled * sdf_factor; }\n";
}

BUSTER_GLOBAL_LOCAL const char* metal_rect_vertex_shader_source(void)
{
#if BUSTER_USE_SLANG_SHADERS
    return BUSTER_SHADER_RECT_METAL_VERTEX_SOURCE;
#else
    return metal_rect_inline_shader_source();
#endif
}

BUSTER_GLOBAL_LOCAL const char* metal_rect_fragment_shader_source(void)
{
#if BUSTER_USE_SLANG_SHADERS
    return BUSTER_SHADER_RECT_METAL_FRAGMENT_SOURCE;
#else
    return metal_rect_inline_shader_source();
#endif
}

BUSTER_GLOBAL_LOCAL void metal_log_error(String8 prefix, id error)
{
    if (error)
    {
        id description = metal_msg_id(error, "localizedDescription");
        const char* c_string = metal_utf8_string(description);
        string_print(S8("{S8}: {S8}\n"), prefix, string_from_pointer((char8*)c_string));
    }
}

BUSTER_GLOBAL_LOCAL bool metal_create_rect_pipeline(RenderingHandle* rendering)
{
    bool result = false;
    id vertex_source = metal_nsstring_from_cstring(metal_rect_vertex_shader_source());
    id error = 0;
    rendering->library =
        ((id (*)(id, SEL, id, id, id*))objc_msgSend)(rendering->device, metal_sel("newLibraryWithSource:options:error:"), vertex_source, 0, &error);
    metal_release(vertex_source);
    string_print(S8("Metal vertex shader library creation: library={u64:x}\n"), (u64)rendering->library);
    if (!rendering->library)
    {
        metal_log_error(S8("Metal vertex shader compilation failed"), error);
    }

#if BUSTER_USE_SLANG_SHADERS
    id fragment_library = 0;
    id fragment_source = metal_nsstring_from_cstring(metal_rect_fragment_shader_source());
    error = 0;
    fragment_library =
        ((id (*)(id, SEL, id, id, id*))objc_msgSend)(rendering->device, metal_sel("newLibraryWithSource:options:error:"), fragment_source, 0, &error);
    metal_release(fragment_source);
    string_print(S8("Metal fragment shader library creation: library={u64:x}\n"), (u64)fragment_library);
    if (!fragment_library)
    {
        metal_log_error(S8("Metal fragment shader compilation failed"), error);
    }
#else
    id fragment_library = rendering->library;
#endif

    if (rendering->library && fragment_library)
    {
        id vertex_name = metal_nsstring_from_cstring("rect_vs");
        id fragment_name = metal_nsstring_from_cstring("rect_fs");
        id vertex_function = ((id (*)(id, SEL, id))objc_msgSend)(rendering->library, metal_sel("newFunctionWithName:"), vertex_name);
        id fragment_function = ((id (*)(id, SEL, id))objc_msgSend)(fragment_library, metal_sel("newFunctionWithName:"), fragment_name);
        metal_release(vertex_name);
        metal_release(fragment_name);
        string_print(S8("Metal shader functions: vertex={u64:x}, fragment={u64:x}\n"), (u64)vertex_function, (u64)fragment_function);

        if (vertex_function && fragment_function)
        {
            id descriptor = metal_msg_id(metal_msg_id((id)objc_getClass("MTLRenderPipelineDescriptor"), "alloc"), "init");
            metal_msg_void_id(descriptor, "setVertexFunction:", vertex_function);
            metal_msg_void_id(descriptor, "setFragmentFunction:", fragment_function);
            id color_attachments = metal_msg_id(descriptor, "colorAttachments");
            id color_attachment = ((id (*)(id, SEL, BusterNSUInteger))objc_msgSend)(color_attachments, metal_sel("objectAtIndexedSubscript:"), 0);
            metal_msg_void_ulong(color_attachment, "setPixelFormat:", BUSTER_MTL_PIXEL_FORMAT_BGRA8_UNORM);
            metal_msg_void_bool(color_attachment, "setBlendingEnabled:", true);
            metal_msg_void_ulong(color_attachment, "setSourceRGBBlendFactor:", BUSTER_MTL_BLEND_FACTOR_SOURCE_ALPHA);
            metal_msg_void_ulong(color_attachment, "setDestinationRGBBlendFactor:", BUSTER_MTL_BLEND_FACTOR_ONE_MINUS_SOURCE_ALPHA);
            metal_msg_void_ulong(color_attachment, "setRgbBlendOperation:", BUSTER_MTL_BLEND_OPERATION_ADD);
            metal_msg_void_ulong(color_attachment, "setSourceAlphaBlendFactor:", BUSTER_MTL_BLEND_FACTOR_ONE);
            metal_msg_void_ulong(color_attachment, "setDestinationAlphaBlendFactor:", BUSTER_MTL_BLEND_FACTOR_ZERO);
            metal_msg_void_ulong(color_attachment, "setAlphaBlendOperation:", BUSTER_MTL_BLEND_OPERATION_ADD);

            error = 0;
            rendering->rect_pipeline =
                ((id (*)(id, SEL, id, id*))objc_msgSend)(rendering->device, metal_sel("newRenderPipelineStateWithDescriptor:error:"), descriptor, &error);
            result = rendering->rect_pipeline != 0;
            string_print(S8("Metal rect pipeline creation: pipeline={u64:x}\n"), (u64)rendering->rect_pipeline);
            if (!result)
            {
                metal_log_error(S8("Metal pipeline creation failed"), error);
            }
            metal_release(descriptor);
        }
        else
        {
            if (!vertex_function)
            {
                string_print(S8("Metal vertex function creation failed\n"));
            }
            if (!fragment_function)
            {
                string_print(S8("Metal fragment function creation failed\n"));
            }
        }

        metal_release(vertex_function);
        metal_release(fragment_function);
    }

#if BUSTER_USE_SLANG_SHADERS
    metal_release(fragment_library);
#endif

    return result;
}

RenderingHandle* rendering_initialize(Arena* arena)
{
    BUSTER_UNUSED(arena);
    RenderingHandle* result = 0;
    memset(&rendering_handle, 0, sizeof(rendering_handle));
    metal_drawable_size_log_count = 0;
    metal_frame_begin_log_count = 0;
    metal_frame_end_log_count = 0;
    bool enable_validation = BUSTER_GPU_VALIDATION_ENABLED;
    if (enable_validation)
    {
        setenv("MTL_DEBUG_LAYER", "1", 1);
        setenv("MTL_SHADER_VALIDATION", "1", 1);
    }
    string_print(S8("Metal rendering initialization: validation={u32}\n"), (u32)enable_validation);
    rendering_handle.device = MTLCreateSystemDefaultDevice();
    string_print(S8("Metal rendering initialization: device={u64:x}\n"), (u64)rendering_handle.device);
    if (rendering_handle.device)
    {
        rendering_handle.command_queue = metal_msg_id(rendering_handle.device, "newCommandQueue");
        id sampler_descriptor = metal_msg_id(metal_msg_id((id)objc_getClass("MTLSamplerDescriptor"), "alloc"), "init");
        metal_msg_void_ulong(sampler_descriptor, "setMinFilter:", BUSTER_MTL_SAMPLER_MIN_MAG_FILTER_LINEAR);
        metal_msg_void_ulong(sampler_descriptor, "setMagFilter:", BUSTER_MTL_SAMPLER_MIN_MAG_FILTER_LINEAR);
        metal_msg_void_ulong(sampler_descriptor, "setMipFilter:", BUSTER_MTL_SAMPLER_MIP_FILTER_NOT_MIPMAPPED);
        metal_msg_void_ulong(sampler_descriptor, "setSAddressMode:", BUSTER_MTL_SAMPLER_ADDRESS_MODE_REPEAT);
        metal_msg_void_ulong(sampler_descriptor, "setTAddressMode:", BUSTER_MTL_SAMPLER_ADDRESS_MODE_REPEAT);
        rendering_handle.sampler_state =
            ((id (*)(id, SEL, id))objc_msgSend)(rendering_handle.device, metal_sel("newSamplerStateWithDescriptor:"), sampler_descriptor);
        metal_release(sampler_descriptor);
        string_print(S8("Metal device objects: command_queue={u64:x}, sampler_state={u64:x}\n"), (u64)rendering_handle.command_queue,
                     (u64)rendering_handle.sampler_state);

        if (!rendering_handle.command_queue)
        {
            string_print(S8("Metal command queue creation failed\n"));
        }
        if (!rendering_handle.sampler_state)
        {
            string_print(S8("Metal sampler state creation failed\n"));
        }
        if (rendering_handle.command_queue && rendering_handle.sampler_state && metal_create_rect_pipeline(&rendering_handle))
        {
            result = &rendering_handle;
        }
    }
    else
    {
        string_print(S8("Metal device creation failed\n"));
    }

    if (!result)
    {
        string_print(S8("Metal rendering initialization failed\n"));
    }
    else
    {
        string_print(S8("Metal rendering initialization succeeded: rendering={u64:x}, device={u64:x}, command_queue={u64:x}, rect_pipeline={u64:x}\n"),
                     (u64)result, (u64)result->device, (u64)result->command_queue, (u64)result->rect_pipeline);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BusterCGSize metal_window_backing_size(RenderingWindowHandle* window)
{
#if BUSTER_IOS
    // The Metal layer is the UIView's backing layer; backing pixels are its
    // bounds (points) scaled by contentsScale (the screen's native scale).
    BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))metal_msg_send_stret)(window->layer, metal_sel("bounds"));
    BusterCGFloat scale = ((BusterCGFloat (*)(id, SEL))objc_msgSend)(window->layer, metal_sel("contentsScale"));
    return (BusterCGSize){.width = bounds.size.width * scale, .height = bounds.size.height * scale};
#else
    BusterCGRect bounds = ((BusterCGRect (*)(id, SEL))metal_msg_send_stret)(window->content_view, metal_sel("bounds"));
    return ((BusterCGSize (*)(id, SEL, BusterCGSize))objc_msgSend)(window->content_view, metal_sel("convertSizeToBacking:"), bounds.size);
#endif
}

BUSTER_GLOBAL_LOCAL void metal_window_update_drawable_size(RenderingWindowHandle* window)
{
    BusterCGSize backing_size = metal_window_backing_size(window);
    window->width = (u32)backing_size.width;
    window->height = (u32)backing_size.height;
    ((void (*)(id, SEL, BusterCGSize))objc_msgSend)(window->layer, metal_sel("setDrawableSize:"), backing_size);
    if (metal_drawable_size_log_count < 3)
    {
        string_print(S8("Metal drawable size update {u32}: layer={u64:x}, size={u32}x{u32}\n"), metal_drawable_size_log_count, (u64)window->layer,
                     window->width, window->height);
        metal_drawable_size_log_count += 1;
    }
}

RenderingWindowHandle* rendering_window_initialize(Arena* arena, WmHandle* windowing, RenderingHandle* rendering, WmWindowHandle* window)
{
    RenderingWindowHandle* result = arena_allocate(arena, RenderingWindowHandle, 1);
    WmNativeSurface native_surface = wm_window_get_native_surface(windowing, window);
    result->frame_index = 0;
    result->frame_count = BUSTER_METAL_FRAME_COUNT;

#if BUSTER_IOS
    // On iOS the window handle already exposes a CAMetalLayer (the UIView's
    // backing layer); use it directly instead of creating and attaching one.
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_METAL_LAYER);
    result->ns_window = 0;
    result->content_view = 0;
    result->layer = (id)native_surface.window;
    ((void (*)(id, SEL))objc_msgSend)(result->layer, metal_sel("retain"));
    metal_msg_void_id(result->layer, "setDevice:", rendering->device);
    metal_msg_void_ulong(result->layer, "setPixelFormat:", BUSTER_MTL_PIXEL_FORMAT_BGRA8_UNORM);
    metal_msg_void_bool(result->layer, "setFramebufferOnly:", true);
    string_print(S8("Metal render window initialization: platform=ios, layer={u64:x}, frame_count={u32}\n"), (u64)result->layer, result->frame_count);
#else
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_APPKIT);
    result->ns_window = (id)native_surface.window;
    result->content_view = metal_msg_id(result->ns_window, "contentView");
    string_print(S8("Metal render window initialization: ns_window={u64:x}, content_view={u64:x}, frame_count={u32}\n"), (u64)result->ns_window,
                 (u64)result->content_view, result->frame_count);

    result->layer = metal_msg_id((id)objc_getClass("CAMetalLayer"), "layer");
    ((void (*)(id, SEL))objc_msgSend)(result->layer, metal_sel("retain"));
    metal_msg_void_id(result->layer, "setDevice:", rendering->device);
    metal_msg_void_ulong(result->layer, "setPixelFormat:", BUSTER_MTL_PIXEL_FORMAT_BGRA8_UNORM);
    metal_msg_void_bool(result->layer, "setFramebufferOnly:", true);
    // Headless CI runners have no WindowServer session actively cycling
    // drawables, so nextDrawable's default ~1s internal wait (looking for a
    // free one) turns every frame in `ide test`'s 3-frame smoke test into a
    // multi-second stall. frame_end already logs and skips a null drawable
    // (see below), so failing fast here instead of blocking is safe.
    metal_msg_void_bool(result->layer, "setAllowsNextDrawableTimeout:", false);
    metal_msg_void_bool(result->content_view, "setWantsLayer:", true);
    metal_msg_void_id(result->content_view, "setLayer:", result->layer);
#endif
    metal_window_update_drawable_size(result);
    string_print(S8("Metal layer attached: layer={u64:x}, device={u64:x}, drawable_size={u32}x{u32}\n"), (u64)result->layer, (u64)rendering->device,
                 result->width, result->height);

    for (u64 frame_index = 0; frame_index < result->frame_count; frame_index += 1)
    {
        WindowFrame* frame = &result->frames[frame_index];
        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[pipeline_index];
            pipeline->vertex_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->index_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->vertex_buffer.gpu.type = BUFFER_TYPE_VERTEX;
            pipeline->index_buffer.gpu.type = BUFFER_TYPE_INDEX;
        }
        string_print(S8("Metal frame resources {u32}: command_buffer={u64:x}\n"), (u32)frame_index, (u64)frame->command_buffer);
    }
    string_print(S8("Metal render window initialization succeeded: ns_window={u64:x}, layer={u64:x}, frame_count={u32}\n"), (u64)result->ns_window,
                 (u64)result->layer, result->frame_count);
    return result;
}

void rendering_window_rect_texture_update_begin(RenderingWindowHandle* window)
{
    BUSTER_UNUSED(window);
}

void rendering_window_queue_rect_texture_update(RenderingHandle* rendering, RenderingWindowHandle* window, RectTextureSlot slot, TextureIndex texture_index)
{
    BUSTER_CHECK(slot < RECT_TEXTURE_SLOT_COUNT);
    BUSTER_CHECK(texture_index.value < rendering->texture_count);
    window->rect_textures[(u32)slot] = texture_index;
}

void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

BUSTER_GLOBAL_LOCAL BusterNSUInteger metal_texture_format(TextureFormat format)
{
    BusterNSUInteger result = 0;
    switch (format)
    {
        break;
    case TEXTURE_FORMAT_R8_UNORM:
        result = BUSTER_MTL_PIXEL_FORMAT_R8_UNORM;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        result = BUSTER_MTL_PIXEL_FORMAT_RGBA8_UNORM;
        break;
    case TEXTURE_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 metal_format_channel_count(TextureFormat format)
{
    switch (format)
    {
        break;
    case TEXTURE_FORMAT_R8_UNORM:
        return 1;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        return 4;
        break;
    case TEXTURE_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }
    BUSTER_UNREACHABLE();
}

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_CHECK(texture_memory.depth == 1);
    BUSTER_CHECK(rendering->texture_count < MAX_METAL_TEXTURE_COUNT);
    u32 texture_index = rendering->texture_count;
    rendering->texture_count += 1;
    MetalTexture* texture = &rendering->textures[texture_index];
    texture->width = texture_memory.width;
    texture->height = texture_memory.height;

    BusterNSUInteger format = metal_texture_format(texture_memory.format);
    id texture_descriptor_class = (id)objc_getClass("MTLTextureDescriptor");
    id descriptor = ((id (*)(id, SEL, BusterNSUInteger, BusterNSUInteger, BusterNSUInteger, bool))objc_msgSend)(
        texture_descriptor_class, metal_sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), format, (BusterNSUInteger)texture_memory.width,
        (BusterNSUInteger)texture_memory.height, false);
    metal_msg_void_ulong(descriptor, "setUsage:", BUSTER_MTL_TEXTURE_USAGE_SHADER_READ);
    metal_msg_void_ulong(descriptor, "setStorageMode:", BUSTER_MTL_STORAGE_MODE_SHARED);
    texture->resource = ((id (*)(id, SEL, id))objc_msgSend)(rendering->device, metal_sel("newTextureWithDescriptor:"), descriptor);
    if (!texture->resource)
    {
        os_fail();
    }

    BusterMTLRegion region = {
        .origin = {0, 0, 0},
        .size = {texture_memory.width, texture_memory.height, 1},
    };
    BusterNSUInteger bytes_per_row = texture_memory.width * metal_format_channel_count(texture_memory.format);
    ((void (*)(id, SEL, BusterMTLRegion, BusterNSUInteger, const void*, BusterNSUInteger))objc_msgSend)(
        texture->resource, metal_sel("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0, texture_memory.pointer, bytes_per_row);
    return (TextureIndex){.value = texture_index};
}

BUSTER_GLOBAL_LOCAL WindowFrame* rendering_window_frame(RenderingWindowHandle* window)
{
    return &window->frames[window->frame_index % window->frame_count];
}

void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    metal_window_update_drawable_size(window);
    WindowFrame* frame = rendering_window_frame(window);
    if (metal_frame_begin_log_count < 3)
    {
        string_print(S8("Metal frame begin {u32}: frame_index={u32}, drawable_size={u32}x{u32}, old_command_buffer={u64:x}\n"), metal_frame_begin_log_count,
                     window->frame_index, window->width, window->height, (u64)frame->command_buffer);
        metal_frame_begin_log_count += 1;
    }
    if (frame->command_buffer)
    {
        metal_msg_void(frame->command_buffer, "waitUntilCompleted");
        metal_release(frame->command_buffer);
        frame->command_buffer = 0;
    }

    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(frame->pipeline_instantiations); i += 1)
    {
        FramePipelineInstantiation* pipeline_instantiation = &frame->pipeline_instantiations[i];
        arena_reset_to_start(pipeline_instantiation->vertex_buffer.cpu);
        pipeline_instantiation->vertex_buffer.count = 0;
        arena_reset_to_start(pipeline_instantiation->index_buffer.cpu);
    }
}

BUSTER_GLOBAL_LOCAL void metal_buffer_destroy(MetalBuffer* buffer)
{
    metal_release(buffer->resource);
    buffer->resource = 0;
    buffer->mapped = 0;
    buffer->size = 0;
}

BUSTER_GLOBAL_LOCAL MetalBuffer metal_buffer_create(RenderingHandle* rendering, u64 size, BufferType type)
{
    MetalBuffer result = {.type = type};
    result.resource = ((id (*)(id, SEL, BusterNSUInteger, BusterNSUInteger))objc_msgSend)(
        rendering->device, metal_sel("newBufferWithLength:options:"), (BusterNSUInteger)size, (BusterNSUInteger)BUSTER_MTL_STORAGE_MODE_SHARED);
    if (!result.resource)
    {
        os_fail();
    }
    result.mapped = ((u8 * (*)(id, SEL)) objc_msgSend)(result.resource, metal_sel("contents"));
    if (!result.mapped)
    {
        metal_buffer_destroy(&result);
        os_fail();
    }
    result.size = size;
    return result;
}

BUSTER_GLOBAL_LOCAL void metal_buffer_ensure_capacity(RenderingHandle* rendering, MetalBuffer* buffer, u64 needed_size)
{
    if (needed_size > buffer->size)
    {
        BufferType type = buffer->type;
        metal_buffer_destroy(buffer);
        *buffer = metal_buffer_create(rendering, needed_size, type);
    }
}

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    bool log_frame_end = metal_frame_end_log_count < 3;
    u32 log_index = metal_frame_end_log_count;
    if (log_frame_end)
    {
        metal_frame_end_log_count += 1;
        string_print(S8("Metal frame end {u32}: frame_index={u32}, drawable_size={u32}x{u32}, layer={u64:x}\n"), log_index, window->frame_index, window->width,
                     window->height, (u64)window->layer);
    }
    if (!window->width || !window->height)
    {
        if (log_frame_end)
        {
            string_print(S8("Metal frame end {u32}: skipped zero-sized drawable\n"), log_index);
        }
        return;
    }

    id drawable = metal_msg_id(window->layer, "nextDrawable");
    if (!drawable)
    {
        if (log_frame_end)
        {
            string_print(S8("Metal frame end {u32}: nextDrawable returned null\n"), log_index);
        }
        return;
    }

    WindowFrame* frame = rendering_window_frame(window);
    id command_buffer = metal_msg_id(rendering->command_queue, "commandBuffer");
    id render_pass_descriptor = metal_msg_id((id)objc_getClass("MTLRenderPassDescriptor"), "renderPassDescriptor");
    id color_attachments = metal_msg_id(render_pass_descriptor, "colorAttachments");
    id color_attachment = ((id (*)(id, SEL, BusterNSUInteger))objc_msgSend)(color_attachments, metal_sel("objectAtIndexedSubscript:"), 0);
    id drawable_texture = metal_msg_id(drawable, "texture");
    if (log_frame_end)
    {
        string_print(S8("Metal frame end {u32}: drawable={u64:x}, texture={u64:x}, command_buffer={u64:x}\n"), log_index, (u64)drawable, (u64)drawable_texture,
                     (u64)command_buffer);
    }
    metal_msg_void_id(color_attachment, "setTexture:", drawable_texture);
    metal_msg_void_ulong(color_attachment, "setLoadAction:", BUSTER_MTL_LOAD_ACTION_CLEAR);
    metal_msg_void_ulong(color_attachment, "setStoreAction:", BUSTER_MTL_STORE_ACTION_STORE);
    BusterMTLClearColor clear_color = {1.0, 0.0, 1.0, 1.0};
    ((void (*)(id, SEL, BusterMTLClearColor))objc_msgSend)(color_attachment, metal_sel("setClearColor:"), clear_color);

    id encoder = ((id (*)(id, SEL, id))objc_msgSend)(command_buffer, metal_sel("renderCommandEncoderWithDescriptor:"), render_pass_descriptor);
    if (log_frame_end)
    {
        string_print(S8("Metal frame end {u32}: encoder={u64:x}, rect_pipeline={u64:x}\n"), log_index, (u64)encoder, (u64)rendering->rect_pipeline);
    }
    metal_msg_void_id(encoder, "setRenderPipelineState:", rendering->rect_pipeline);
    BusterMTLViewport viewport = {0, 0, (double)window->width, (double)window->height, 0, 1};
    ((void (*)(id, SEL, BusterMTLViewport))objc_msgSend)(encoder, metal_sel("setViewport:"), viewport);
    BusterMTLScissorRect scissor = {0, 0, window->width, window->height};
    ((void (*)(id, SEL, BusterMTLScissorRect))objc_msgSend)(encoder, metal_sel("setScissorRect:"), scissor);
    ((void (*)(id, SEL, id, BusterNSUInteger))objc_msgSend)(encoder, metal_sel("setFragmentSamplerState:atIndex:"), rendering->sampler_state, 0);
    for (u32 slot = 0; slot < RECT_TEXTURE_SLOT_COUNT; slot += 1)
    {
        TextureIndex texture_index = window->rect_textures[slot];
        if (texture_index.value < rendering->texture_count)
        {
            ((void (*)(id, SEL, id, BusterNSUInteger))objc_msgSend)(encoder, metal_sel("setFragmentTexture:atIndex:"),
                                                                    rendering->textures[texture_index.value].resource, (BusterNSUInteger)slot);
        }
    }

    for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
    {
        FramePipelineInstantiation* pipeline_instantiation = &frame->pipeline_instantiations[pipeline_index];
        u64 vertex_size = arena_buffer_size(pipeline_instantiation->vertex_buffer.cpu);
        u64 index_size = arena_buffer_size(pipeline_instantiation->index_buffer.cpu);
        if (vertex_size && index_size)
        {
            metal_buffer_ensure_capacity(rendering, &pipeline_instantiation->vertex_buffer.gpu, vertex_size);
            metal_buffer_ensure_capacity(rendering, &pipeline_instantiation->index_buffer.gpu, index_size);
            u8* vertex_destination = pipeline_instantiation->vertex_buffer.gpu.mapped;
            u8* index_destination = pipeline_instantiation->index_buffer.gpu.mapped;
            if (!vertex_destination || !index_destination)
            {
                os_fail();
            }
            memcpy(vertex_destination, arena_buffer_start(pipeline_instantiation->vertex_buffer.cpu), vertex_size);
            memcpy(index_destination, arena_buffer_start(pipeline_instantiation->index_buffer.cpu), index_size);

            ((void (*)(id, SEL, id, BusterNSUInteger, BusterNSUInteger))objc_msgSend)(encoder, metal_sel("setVertexBuffer:offset:atIndex:"),
                                                                                      pipeline_instantiation->vertex_buffer.gpu.resource, 0, 0);
            f32 constants[] = {(f32)window->width, (f32)window->height};
            ((void (*)(id, SEL, const void*, BusterNSUInteger, BusterNSUInteger))objc_msgSend)(encoder, metal_sel("setVertexBytes:length:atIndex:"), constants,
                                                                                               (BusterNSUInteger)sizeof(constants), 1);
            ((void (*)(id, SEL, BusterNSUInteger, BusterNSUInteger, BusterNSUInteger, id, BusterNSUInteger))objc_msgSend)(
                encoder, metal_sel("drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"), BUSTER_MTL_PRIMITIVE_TYPE_TRIANGLE,
                (BusterNSUInteger)(index_size / sizeof(u32)), BUSTER_MTL_INDEX_TYPE_UINT32, pipeline_instantiation->index_buffer.gpu.resource, 0);
        }
    }

    metal_msg_void(encoder, "endEncoding");
    metal_msg_void_id(command_buffer, "presentDrawable:", drawable);
    metal_msg_void(command_buffer, "commit");
    ((void (*)(id, SEL))objc_msgSend)(command_buffer, metal_sel("retain"));
    frame->command_buffer = command_buffer;
    window->frame_index = (window->frame_index + 1) % window->frame_count;
    if (log_frame_end)
    {
        string_print(S8("Metal frame end {u32}: present+commit complete, next_frame_index={u32}\n"), log_index, window->frame_index);
    }
}

void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    string_print(S8("Metal render window deinitialize: ns_window={u64:x}, layer={u64:x}, frame_count={u32}\n"), (u64)window->ns_window, (u64)window->layer,
                 window->frame_count);
    for (u32 frame_index = 0; frame_index < window->frame_count; frame_index += 1)
    {
        WindowFrame* frame = &window->frames[frame_index];
        if (frame->command_buffer)
        {
            metal_msg_void(frame->command_buffer, "waitUntilCompleted");
            metal_release(frame->command_buffer);
            frame->command_buffer = 0;
        }
        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[pipeline_index];
            if (pipeline->vertex_buffer.cpu)
                arena_destroy(pipeline->vertex_buffer.cpu, 1);
            if (pipeline->index_buffer.cpu)
                arena_destroy(pipeline->index_buffer.cpu, 1);
            metal_buffer_destroy(&pipeline->vertex_buffer.gpu);
            metal_buffer_destroy(&pipeline->index_buffer.gpu);
        }
    }
    if (window->content_view)
    {
        metal_msg_void_id(window->content_view, "setLayer:", 0);
    }
    metal_release(window->layer);
    window->layer = 0;
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    string_print(S8("Metal rendering deinitialize: texture_count={u32}, device={u64:x}, command_queue={u64:x}\n"), rendering->texture_count,
                 (u64)rendering->device, (u64)rendering->command_queue);
    for (u32 i = 0; i < rendering->texture_count; i += 1)
    {
        metal_release(rendering->textures[i].resource);
        rendering->textures[i].resource = 0;
    }
    metal_release(rendering->sampler_state);
    metal_release(rendering->rect_pipeline);
    metal_release(rendering->library);
    metal_release(rendering->command_queue);
    metal_release(rendering->device);
    rendering->sampler_state = 0;
    rendering->rect_pipeline = 0;
    rendering->library = 0;
    rendering->command_queue = 0;
    rendering->device = 0;
}
