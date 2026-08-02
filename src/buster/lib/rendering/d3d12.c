#include <buster/lib/rendering/internal.h>

#ifndef CINTERFACE
#define CINTERFACE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIDL_C_INLINE_WRAPPERS
#define WIDL_C_INLINE_WRAPPERS
#endif

#include <buster/lib/system_headers.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#if defined(__has_include)
#if __has_include(<d3d12sdklayers.h>)
#include <d3d12sdklayers.h>
#define BUSTER_D3D12_HAS_SDK_LAYERS 1
#else
#define BUSTER_D3D12_HAS_SDK_LAYERS 0
#endif
#else
#include <d3d12sdklayers.h>
#define BUSTER_D3D12_HAS_SDK_LAYERS 1
#endif
#include <d3dcompiler.h>

#define BUSTER_D3D12_FRAME_COUNT (2)
#define MAX_D3D12_TEXTURE_COUNT (16)
#define MAX_D3D12_SRV_DESCRIPTOR_COUNT (128)
#define BUSTER_D3D12_RECT_TEXTURE_SLOT_COUNT (2)
#define BUSTER_D3D12_STRINGIFY_HELPER(x) #x
#define BUSTER_D3D12_STRINGIFY(x) BUSTER_D3D12_STRINGIFY_HELPER(x)

BUSTER_GLOBAL_LOCAL void d3d12_release_unknown(IUnknown* object)
{
    object->lpVtbl->Release(object);
}

#define BUSTER_D3D12_RELEASE(object)                                                                                                                           \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (object)                                                                                                                                            \
        {                                                                                                                                                      \
            d3d12_release_unknown((IUnknown*)(object));                                                                                                        \
            (object) = 0;                                                                                                                                      \
        }                                                                                                                                                      \
    } while (0)

BUSTER_CT_CHECK(BUSTER_D3D12_RECT_TEXTURE_SLOT_COUNT == RECT_TEXTURE_SLOT_COUNT);

typedef HRESULT(WINAPI BusterD3D12CreateDeviceFunction)(IUnknown* adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID riid, void** device);
typedef HRESULT(WINAPI BusterD3D12GetDebugInterfaceFunction)(REFIID riid, void** debug);
typedef HRESULT(WINAPI BusterD3D12SerializeRootSignatureFunction)(const D3D12_ROOT_SIGNATURE_DESC* root_signature, D3D_ROOT_SIGNATURE_VERSION version,
                                                                  ID3DBlob** blob, ID3DBlob** error_blob);
typedef HRESULT(WINAPI BusterCreateDXGIFactory2Function)(UINT flags, REFIID riid, void** factory);
typedef HRESULT(WINAPI BusterD3DCompileFunction)(LPCVOID source_data, SIZE_T source_data_size, LPCSTR source_name, const D3D_SHADER_MACRO* defines,
                                                 ID3DInclude* include, LPCSTR entrypoint, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code,
                                                 ID3DBlob** error_messages);

typedef enum BufferType
{
    BUFFER_TYPE_VERTEX,
    BUFFER_TYPE_INDEX,
    BUFFER_TYPE_COUNT,
} BufferType;

typedef struct D3D12Buffer D3D12Buffer;
struct D3D12Buffer
{
    ID3D12Resource* resource;
    u8* mapped;
    u64 size;
    BufferType type;
    u8 reserved[4];
};

typedef struct VertexBuffer VertexBuffer;
struct VertexBuffer
{
    D3D12Buffer gpu;
    Arena* cpu;
    u32 count;
    u8 reserved[4];
};

typedef struct IndexBuffer IndexBuffer;
struct IndexBuffer
{
    D3D12Buffer gpu;
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
    ID3D12CommandAllocator* command_allocator;
    ID3D12GraphicsCommandList* command_list;
    FramePipelineInstantiation pipeline_instantiations[(u64)BUSTER_PIPELINE_COUNT];
    u64 fence_value;
};

typedef struct D3D12Texture D3D12Texture;
struct D3D12Texture
{
    ID3D12Resource* resource;
    u32 descriptor_index;
    u32 width;
    u32 height;
    u32 reserved;
};

typedef struct RenderingHandle RenderingHandle;
struct RenderingHandle
{
    OsModuleHandle* d3d12_library;
    OsModuleHandle* dxgi_library;
    OsModuleHandle* d3dcompiler_library;
    BusterD3D12CreateDeviceFunction* d3d12_create_device;
    BusterD3D12GetDebugInterfaceFunction* d3d12_get_debug_interface;
    BusterD3D12SerializeRootSignatureFunction* d3d12_serialize_root_signature;
    BusterCreateDXGIFactory2Function* create_dxgi_factory2;
    BusterD3DCompileFunction* d3d_compile;
#if BUSTER_D3D12_HAS_SDK_LAYERS
    ID3D12Debug* debug_controller;
    ID3D12Debug1* debug_controller1;
#endif
    IDXGIFactory4* factory;
    IDXGIAdapter1* adapter;
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    ID3D12RootSignature* root_signature;
    ID3D12PipelineState* rect_pipeline;
    ID3D12DescriptorHeap* srv_heap;
    u32 srv_descriptor_count;
    u32 texture_count;
    UINT srv_descriptor_size;
    u32 reserved;
    ID3D12Fence* fence;
    HANDLE fence_event;
    u64 next_fence_value;
    ID3D12CommandAllocator* upload_command_allocator;
    ID3D12GraphicsCommandList* upload_command_list;
    FontTextureAtlas fonts[RENDER_FONT_TYPE_COUNT];
    D3D12Texture textures[MAX_D3D12_TEXTURE_COUNT];
};

typedef struct RenderingWindowHandle RenderingWindowHandle;
struct RenderingWindowHandle
{
    HWND hwnd;
    IDXGISwapChain3* swapchain;
    ID3D12DescriptorHeap* rtv_heap;
    ID3D12Resource* render_targets[BUSTER_D3D12_FRAME_COUNT];
    UINT rtv_descriptor_size;
    u32 width;
    u32 height;
    u32 frame_index;
    u32 frame_count;
    u32 rect_descriptor_base;
    WindowFrame frames[BUSTER_D3D12_FRAME_COUNT];
};

BUSTER_GLOBAL_LOCAL RenderingHandle rendering_handle = {0};
BUSTER_GLOBAL_LOCAL u32 d3d12_frame_begin_log_count = 0;
BUSTER_GLOBAL_LOCAL u32 d3d12_frame_end_log_count = 0;

BUSTER_GLOBAL_LOCAL bool d3d12_ok(HRESULT result)
{
    return SUCCEEDED(result);
}

BUSTER_GLOBAL_LOCAL bool d3d12_load_libraries(RenderingHandle* rendering)
{
    rendering->d3d12_library = os_dynamic_library_load(S8("d3d12.dll"));
    rendering->dxgi_library = os_dynamic_library_load(S8("dxgi.dll"));
    rendering->d3dcompiler_library = os_dynamic_library_load(S8("d3dcompiler_47.dll"));
    if (!rendering->d3dcompiler_library)
    {
        rendering->d3dcompiler_library = os_dynamic_library_load(S8("d3dcompiler_43.dll"));
    }

    if (rendering->d3d12_library && rendering->dxgi_library && rendering->d3dcompiler_library)
    {
        rendering->d3d12_create_device = (BusterD3D12CreateDeviceFunction*)os_dynamic_library_function_load(rendering->d3d12_library, S8("D3D12CreateDevice"));
        rendering->d3d12_get_debug_interface =
            (BusterD3D12GetDebugInterfaceFunction*)os_dynamic_library_function_load(rendering->d3d12_library, S8("D3D12GetDebugInterface"));
        rendering->d3d12_serialize_root_signature =
            (BusterD3D12SerializeRootSignatureFunction*)os_dynamic_library_function_load(rendering->d3d12_library, S8("D3D12SerializeRootSignature"));
        rendering->create_dxgi_factory2 =
            (BusterCreateDXGIFactory2Function*)os_dynamic_library_function_load(rendering->dxgi_library, S8("CreateDXGIFactory2"));
        rendering->d3d_compile = (BusterD3DCompileFunction*)os_dynamic_library_function_load(rendering->d3dcompiler_library, S8("D3DCompile"));
    }

    return rendering->d3d12_create_device && rendering->d3d12_serialize_root_signature && rendering->create_dxgi_factory2 && rendering->d3d_compile;
}

BUSTER_GLOBAL_LOCAL D3D12_CPU_DESCRIPTOR_HANDLE d3d12_cpu_descriptor(ID3D12DescriptorHeap* heap, UINT descriptor_size, u32 index)
{
#if defined(_MSC_VER)
    D3D12_CPU_DESCRIPTOR_HANDLE result;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &result);
#else
    D3D12_CPU_DESCRIPTOR_HANDLE result = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
#endif
    result.ptr += (SIZE_T)descriptor_size * index;
    return result;
}

BUSTER_GLOBAL_LOCAL D3D12_GPU_DESCRIPTOR_HANDLE d3d12_gpu_descriptor(ID3D12DescriptorHeap* heap, UINT descriptor_size, u32 index)
{
#if defined(_MSC_VER)
    D3D12_GPU_DESCRIPTOR_HANDLE result;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap, &result);
#else
    D3D12_GPU_DESCRIPTOR_HANDLE result = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
#endif
    result.ptr += (UINT64)descriptor_size * index;
    return result;
}

BUSTER_GLOBAL_LOCAL void d3d12_wait_for_fence(RenderingHandle* rendering, u64 fence_value)
{
    if (fence_value && ID3D12Fence_GetCompletedValue(rendering->fence) < fence_value)
    {
        HRESULT event_result = ID3D12Fence_SetEventOnCompletion(rendering->fence, fence_value, rendering->fence_event);
        if (d3d12_ok(event_result))
        {
            WaitForSingleObject(rendering->fence_event, INFINITE);
        }
        else
        {
            os_fail();
        }
    }
}

BUSTER_GLOBAL_LOCAL u64 d3d12_signal(RenderingHandle* rendering)
{
    u64 fence_value = rendering->next_fence_value;
    rendering->next_fence_value += 1;
    if (!d3d12_ok(ID3D12CommandQueue_Signal(rendering->queue, rendering->fence, fence_value)))
    {
        os_fail();
    }
    return fence_value;
}

BUSTER_GLOBAL_LOCAL void d3d12_flush(RenderingHandle* rendering)
{
    d3d12_wait_for_fence(rendering, d3d12_signal(rendering));
}

BUSTER_GLOBAL_LOCAL DXGI_FORMAT d3d12_texture_format(TextureFormat format)
{
    DXGI_FORMAT result = DXGI_FORMAT_UNKNOWN;
    switch (format)
    {
        break;
    case TEXTURE_FORMAT_R8_UNORM:
        result = DXGI_FORMAT_R8_UNORM;
        break;
    case TEXTURE_FORMAT_R8G8B8A8_SRGB:
        result = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case TEXTURE_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 d3d12_format_channel_count(TextureFormat format)
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
    return 0;
}

BUSTER_GLOBAL_LOCAL const char* d3d12_rect_inline_shader_source(void)
{
    return "struct RectVertex { float2 p0; float2 uv0; float2 extent; float corner_radius; float softness; float4 colors[4]; uint texture_index; uint3 "
           "reserved; };\n"
           "struct VertexOut { float4 position : SV_Position; nointerpolation uint texture_index : TEXCOORD0; float4 color : TEXCOORD1; float2 uv : TEXCOORD2; "
           "float2 pixel_position : TEXCOORD3; float2 center : TEXCOORD4; float2 half_size : TEXCOORD5; float corner_radius : TEXCOORD6; float softness : "
           "TEXCOORD7; };\n"
           "StructuredBuffer<RectVertex> vertices_buffer : register(t0);\n"
           "cbuffer DrawConstants : register(b0) { float width; float height; };\n"
           "Texture2D textures[" BUSTER_D3D12_STRINGIFY(
               BUSTER_D3D12_RECT_TEXTURE_SLOT_COUNT) "] : register(t1);\n"
                                                     "SamplerState texture_sampler : register(s0);\n"
                                                     "static const float2 quad_vertices[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };\n"
                                                     "VertexOut vs_main(uint vertex_id : SV_VertexID) {\n"
                                                     "    VertexOut output; RectVertex v = vertices_buffer[vertex_id]; uint quad_vertex_id = vertex_id % 4; "
                                                     "float2 extent = v.extent; float2 p0 = v.p0; float2 p1 = p0 + extent; float2 center = (p1 + p0) * 0.5; "
                                                     "float2 half_size = (p1 - p0) * 0.5; float2 position = quad_vertices[quad_vertex_id] * half_size + "
                                                     "center;\n"
                                                     "    output.position = float4(2.0 * position.x / width - 1.0, 1.0 - 2.0 * position.y / height, 0.0, "
                                                     "1.0);\n"
                                                     "    float2 uv0 = v.uv0; float2 uv1 = uv0 + extent; float2 texture_center = (uv1 + uv0) * 0.5; output.uv "
                                                     "= quad_vertices[quad_vertex_id] * half_size + texture_center;\n"
                                                     "    output.texture_index = v.texture_index; output.color = v.colors[quad_vertex_id]; "
                                                     "output.pixel_position = position; output.center = center; output.half_size = half_size; "
                                                     "output.corner_radius = v.corner_radius; output.softness = v.softness; return output; }\n"
                                                     "float rounded_rect_sdf(float2 position, float2 center, float2 half_size, float radius) { float2 r2 = "
                                                     "float2(radius, radius); float2 d2_no_r2 = abs(center - position) - half_size; float2 d2 = d2_no_r2 + r2; "
                                                     "float negative_distance = min(max(d2.x, d2.y), 0.0); float positive_distance = length(max(d2, 0.0)); "
                                                     "return negative_distance + positive_distance - radius; }\n"
                                                     "float4 ps_main(VertexOut input) : SV_Target { uint texture_index = "
                                                     "NonUniformResourceIndex(input.texture_index); uint width_tex; uint height_tex; "
                                                     "textures[texture_index].GetDimensions(width_tex, height_tex); float2 uv = float2(input.uv.x / "
                                                     "(float)width_tex, input.uv.y / (float)height_tex); float4 sampled = "
                                                     "textures[texture_index].Sample(texture_sampler, uv); float softness = input.softness; float "
                                                     "softness_padding_scalar = max(0.0, softness * 2.0 - 1.0); float distance = "
                                                     "rounded_rect_sdf(input.pixel_position, input.center, input.half_size - float2(softness_padding_scalar, "
                                                     "softness_padding_scalar), input.corner_radius); float sdf_factor = 1.0 - smoothstep(0.0, 2.0 * softness, "
                                                     "distance); return input.color * sampled * sdf_factor; }\n";
}

BUSTER_GLOBAL_LOCAL const char* d3d12_rect_vertex_shader_source(void)
{
#if BUSTER_USE_SLANG_SHADERS
    return BUSTER_SHADER_RECT_D3D12_VERTEX_SOURCE;
#else
    return d3d12_rect_inline_shader_source();
#endif
}

BUSTER_GLOBAL_LOCAL const char* d3d12_rect_pixel_shader_source(void)
{
#if BUSTER_USE_SLANG_SHADERS
    return BUSTER_SHADER_RECT_D3D12_PIXEL_SOURCE;
#else
    return d3d12_rect_inline_shader_source();
#endif
}

BUSTER_GLOBAL_LOCAL ID3DBlob* d3d12_compile_shader(const char* source, const char* entry_point, const char* target)
{
    ID3DBlob* result = 0;
    ID3DBlob* errors = 0;
    UINT compile_flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if BUSTER_GPU_VALIDATION_ENABLED
    compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT compile_result =
        rendering_handle.d3d_compile(source, strlen(source), "buster_rect.hlsl", 0, 0, entry_point, target, compile_flags, 0, &result, &errors);
    if (!d3d12_ok(compile_result))
    {
        if (errors)
        {
            String8 message = {
                .pointer = (char8*)ID3D10Blob_GetBufferPointer(errors),
                .length = ID3D10Blob_GetBufferSize(errors),
            };
            string_print(S8("D3DCompile failed: {S8}\n"), message);
        }
        BUSTER_D3D12_RELEASE(result);
    }
    BUSTER_D3D12_RELEASE(errors);
    return result;
}

BUSTER_GLOBAL_LOCAL bool d3d12_create_rect_pipeline(RenderingHandle* rendering)
{
    bool result = false;
    D3D12_DESCRIPTOR_RANGE texture_range = {
        .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        .NumDescriptors = (UINT)RECT_TEXTURE_SLOT_COUNT,
        .BaseShaderRegister = 1,
        .RegisterSpace = 0,
        .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
    };
    D3D12_ROOT_PARAMETER root_parameters[] = {
        {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
            .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0},
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
        {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants = {.ShaderRegister = 0, .RegisterSpace = 0, .Num32BitValues = 2},
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        },
        {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {.NumDescriptorRanges = 1, .pDescriptorRanges = &texture_range},
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
        },
    };
    D3D12_STATIC_SAMPLER_DESC sampler = {
        .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MipLODBias = 0,
        .MaxAnisotropy = 1,
        .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
        .BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
        .MinLOD = 0,
        .MaxLOD = D3D12_FLOAT32_MAX,
        .ShaderRegister = 0,
        .RegisterSpace = 0,
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
    };
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {
        .NumParameters = (UINT)BUSTER_ARRAY_LENGTH(root_parameters),
        .pParameters = root_parameters,
        .NumStaticSamplers = 1,
        .pStaticSamplers = &sampler,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };
    ID3DBlob* signature = 0;
    ID3DBlob* errors = 0;
    if (d3d12_ok(rendering->d3d12_serialize_root_signature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)))
    {
        if (d3d12_ok(ID3D12Device_CreateRootSignature(rendering->device, 0, ID3D10Blob_GetBufferPointer(signature), ID3D10Blob_GetBufferSize(signature),
                                                      &IID_ID3D12RootSignature, (void**)&rendering->root_signature)))
        {
            ID3DBlob* vertex_shader = d3d12_compile_shader(d3d12_rect_vertex_shader_source(), "vs_main", "vs_5_1");
            ID3DBlob* pixel_shader = d3d12_compile_shader(d3d12_rect_pixel_shader_source(), "ps_main", "ps_5_1");
            if (vertex_shader && pixel_shader)
            {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {0};
                pipeline_desc.pRootSignature = rendering->root_signature;
                pipeline_desc.VS = (D3D12_SHADER_BYTECODE){.pShaderBytecode = ID3D10Blob_GetBufferPointer(vertex_shader),
                                                           .BytecodeLength = ID3D10Blob_GetBufferSize(vertex_shader)};
                pipeline_desc.PS = (D3D12_SHADER_BYTECODE){.pShaderBytecode = ID3D10Blob_GetBufferPointer(pixel_shader),
                                                           .BytecodeLength = ID3D10Blob_GetBufferSize(pixel_shader)};
                pipeline_desc.BlendState.RenderTarget[0] = (D3D12_RENDER_TARGET_BLEND_DESC){
                    .BlendEnable = TRUE,
                    .LogicOpEnable = FALSE,
                    .SrcBlend = D3D12_BLEND_SRC_ALPHA,
                    .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
                    .BlendOp = D3D12_BLEND_OP_ADD,
                    .SrcBlendAlpha = D3D12_BLEND_ONE,
                    .DestBlendAlpha = D3D12_BLEND_ZERO,
                    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                    .LogicOp = D3D12_LOGIC_OP_NOOP,
                    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
                };
                pipeline_desc.SampleMask = UINT_MAX;
                pipeline_desc.RasterizerState = (D3D12_RASTERIZER_DESC){
                    .FillMode = D3D12_FILL_MODE_SOLID,
                    .CullMode = D3D12_CULL_MODE_NONE,
                    .FrontCounterClockwise = FALSE,
                    .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
                    .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                    .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                    .DepthClipEnable = TRUE,
                    .MultisampleEnable = FALSE,
                    .AntialiasedLineEnable = FALSE,
                    .ForcedSampleCount = 0,
                    .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
                };
                pipeline_desc.DepthStencilState.DepthEnable = FALSE;
                pipeline_desc.DepthStencilState.StencilEnable = FALSE;
                pipeline_desc.InputLayout = (D3D12_INPUT_LAYOUT_DESC){0};
                pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pipeline_desc.NumRenderTargets = 1;
                pipeline_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
                pipeline_desc.SampleDesc.Count = 1;
                result = d3d12_ok(
                    ID3D12Device_CreateGraphicsPipelineState(rendering->device, &pipeline_desc, &IID_ID3D12PipelineState, (void**)&rendering->rect_pipeline));
            }
            BUSTER_D3D12_RELEASE(vertex_shader);
            BUSTER_D3D12_RELEASE(pixel_shader);
        }
    }
    else if (errors)
    {
        String8 message = {.pointer = (char8*)ID3D10Blob_GetBufferPointer(errors), .length = ID3D10Blob_GetBufferSize(errors)};
        string_print(S8("D3D12SerializeRootSignature failed: {S8}\n"), message);
    }
    BUSTER_D3D12_RELEASE(signature);
    BUSTER_D3D12_RELEASE(errors);
    return result;
}

RenderingHandle* rendering_initialize(Arena* arena)
{
    BUSTER_UNUSED(arena);
    RenderingHandle* result = 0;
    memset(&rendering_handle, 0, sizeof(rendering_handle));
    rendering_handle.next_fence_value = 1;
    d3d12_frame_begin_log_count = 0;
    d3d12_frame_end_log_count = 0;

    bool enable_validation = BUSTER_GPU_VALIDATION_ENABLED;
    bool debug_layer_enabled = false;
    UINT factory_flags = 0;
    bool libraries_loaded = d3d12_load_libraries(&rendering_handle);
    string_print(S8("DirectX 12 rendering initialization: libraries_loaded={u32}, validation={u32}\n"), (u32)libraries_loaded, (u32)enable_validation);
#if BUSTER_D3D12_HAS_SDK_LAYERS
    if (libraries_loaded && enable_validation && rendering_handle.d3d12_get_debug_interface)
    {
        HRESULT debug1_interface_result = rendering_handle.d3d12_get_debug_interface(&IID_ID3D12Debug1, (void**)&rendering_handle.debug_controller1);
        HRESULT debug_interface_result = debug1_interface_result;
        bool gpu_based_validation_enabled = false;
        bool synchronized_queue_validation_enabled = false;
        if (d3d12_ok(debug1_interface_result) && rendering_handle.debug_controller1)
        {
            ID3D12Debug1_EnableDebugLayer(rendering_handle.debug_controller1);
            ID3D12Debug1_SetEnableGPUBasedValidation(rendering_handle.debug_controller1, TRUE);
            ID3D12Debug1_SetEnableSynchronizedCommandQueueValidation(rendering_handle.debug_controller1, TRUE);
            debug_layer_enabled = true;
            gpu_based_validation_enabled = true;
            synchronized_queue_validation_enabled = true;
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        else
        {
            debug_interface_result = rendering_handle.d3d12_get_debug_interface(&IID_ID3D12Debug, (void**)&rendering_handle.debug_controller);
        }

        if (!debug_layer_enabled && d3d12_ok(debug_interface_result) && rendering_handle.debug_controller)
        {
            ID3D12Debug_EnableDebugLayer(rendering_handle.debug_controller);
            debug_layer_enabled = true;
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        string_print(S8("DirectX 12 debug layer: D3D12GetDebugInterface={u64:x}, enabled={u32}, gpu_validation={u32}, queue_validation={u32}\n"),
                     (u64)(u32)debug_interface_result, (u32)debug_layer_enabled, (u32)gpu_based_validation_enabled, (u32)synchronized_queue_validation_enabled);
    }
    else if (libraries_loaded && enable_validation)
    {
        string_print(S8("DirectX 12 debug layer unavailable; continuing without validation\n"));
    }
#else
    if (libraries_loaded && enable_validation)
    {
        string_print(S8("DirectX 12 SDK layer headers unavailable; continuing without validation\n"));
    }
#endif

    HRESULT create_factory_result = E_FAIL;
    if (libraries_loaded)
    {
        create_factory_result = rendering_handle.create_dxgi_factory2(factory_flags, &IID_IDXGIFactory4, (void**)&rendering_handle.factory);
        string_print(S8("DirectX 12 rendering initialization: CreateDXGIFactory2={u64:x}\n"), (u64)(u32)create_factory_result);
    }

    if (libraries_loaded && d3d12_ok(create_factory_result))
    {
        for (UINT adapter_index = 0; !rendering_handle.adapter; adapter_index += 1)
        {
            IDXGIAdapter1* adapter = 0;
            if (IDXGIFactory4_EnumAdapters1(rendering_handle.factory, adapter_index, &adapter) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 adapter_desc;
            IDXGIAdapter1_GetDesc1(adapter, &adapter_desc);
            bool hardware_adapter = (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
            HRESULT create_device_result = hardware_adapter ? rendering_handle.d3d12_create_device((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0,
                                                                                                   &IID_ID3D12Device, (void**)&rendering_handle.device)
                                                            : E_FAIL;
            string_print(S8("DirectX 12 adapter {u32}: flags={u32}, hardware={u32}, D3D12CreateDevice={u64:x}\n"), adapter_index, (u32)adapter_desc.Flags,
                         (u32)hardware_adapter, (u64)(u32)create_device_result);
            if (hardware_adapter && d3d12_ok(create_device_result))
            {
                rendering_handle.adapter = adapter;
                string_print(S8("DirectX 12 adapter {u32}: selected\n"), adapter_index);
            }
            else
            {
                BUSTER_D3D12_RELEASE(adapter);
            }
        }

        if (!rendering_handle.device)
        {
            HRESULT default_device_result =
                rendering_handle.d3d12_create_device(0, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&rendering_handle.device);
            string_print(S8("DirectX 12 default adapter D3D12CreateDevice={u64:x}\n"), (u64)(u32)default_device_result);
        }

        if (rendering_handle.device)
        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {
                .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
                .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                .NodeMask = 0,
            };
            D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {
                .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                .NumDescriptors = MAX_D3D12_SRV_DESCRIPTOR_COUNT,
                .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                .NodeMask = 0,
            };

            HRESULT create_queue_result =
                ID3D12Device_CreateCommandQueue(rendering_handle.device, &queue_desc, &IID_ID3D12CommandQueue, (void**)&rendering_handle.queue);
            HRESULT create_srv_heap_result =
                ID3D12Device_CreateDescriptorHeap(rendering_handle.device, &srv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&rendering_handle.srv_heap);
            HRESULT create_fence_result =
                ID3D12Device_CreateFence(rendering_handle.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&rendering_handle.fence);
            HRESULT create_upload_allocator_result = ID3D12Device_CreateCommandAllocator(
                rendering_handle.device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&rendering_handle.upload_command_allocator);
            HRESULT create_upload_command_list_result = E_FAIL;
            if (d3d12_ok(create_upload_allocator_result))
            {
                create_upload_command_list_result =
                    ID3D12Device_CreateCommandList(rendering_handle.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, rendering_handle.upload_command_allocator, 0,
                                                   &IID_ID3D12GraphicsCommandList, (void**)&rendering_handle.upload_command_list);
            }
            string_print(
                S8("DirectX 12 device objects: queue={u64:x}, srv_heap={u64:x}, fence={u64:x}, upload_allocator={u64:x}, upload_command_list={u64:x}\n"),
                (u64)(u32)create_queue_result, (u64)(u32)create_srv_heap_result, (u64)(u32)create_fence_result, (u64)(u32)create_upload_allocator_result,
                (u64)(u32)create_upload_command_list_result);

            if (d3d12_ok(create_queue_result) && d3d12_ok(create_srv_heap_result) && d3d12_ok(create_fence_result) &&
                d3d12_ok(create_upload_allocator_result) && d3d12_ok(create_upload_command_list_result))
            {
                rendering_handle.srv_descriptor_size =
                    ID3D12Device_GetDescriptorHandleIncrementSize(rendering_handle.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                rendering_handle.srv_descriptor_count = MAX_D3D12_TEXTURE_COUNT;
                rendering_handle.fence_event = CreateEventW(0, FALSE, FALSE, 0);
                ID3D12GraphicsCommandList_Close(rendering_handle.upload_command_list);
                bool rect_pipeline_created = d3d12_create_rect_pipeline(&rendering_handle);
                string_print(S8("DirectX 12 device objects: fence_event={u64:x}, srv_descriptor_size={u32}, rect_pipeline_created={u32}\n"),
                             (u64)(UINT_PTR)rendering_handle.fence_event, rendering_handle.srv_descriptor_size, (u32)rect_pipeline_created);
                if (rendering_handle.fence_event && rect_pipeline_created)
                {
                    result = &rendering_handle;
                }
            }
        }
    }

    if (!result)
    {
        string_print(S8("DirectX 12 rendering initialization failed\n"));
    }
    else
    {
        string_print(S8("DirectX 12 rendering initialization succeeded: device={u64:x}, queue={u64:x}, factory={u64:x}\n"),
                     (u64)(UINT_PTR)rendering_handle.device, (u64)(UINT_PTR)rendering_handle.queue, (u64)(UINT_PTR)rendering_handle.factory);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void d3d12_buffer_destroy(D3D12Buffer* buffer)
{
    if (buffer->resource)
    {
        if (buffer->mapped)
        {
            ID3D12Resource_Unmap(buffer->resource, 0, 0);
            buffer->mapped = 0;
        }
        BUSTER_D3D12_RELEASE(buffer->resource);
    }
    buffer->size = 0;
}

BUSTER_GLOBAL_LOCAL D3D12Buffer d3d12_buffer_create(RenderingHandle* rendering, u64 size, BufferType type)
{
    D3D12Buffer result = {.type = type};
    D3D12_HEAP_PROPERTIES heap_properties = {
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 1,
        .VisibleNodeMask = 1,
    };
    D3D12_RESOURCE_DESC resource_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    if (!d3d12_ok(ID3D12Device_CreateCommittedResource(rendering->device, &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, 0, &IID_ID3D12Resource, (void**)&result.resource)))
    {
        os_fail();
    }
    D3D12_RANGE read_range = {0};
    if (!d3d12_ok(ID3D12Resource_Map(result.resource, 0, &read_range, (void**)&result.mapped)))
    {
        d3d12_buffer_destroy(&result);
        os_fail();
    }
    result.size = size;
    return result;
}

BUSTER_GLOBAL_LOCAL void d3d12_buffer_ensure_capacity(RenderingHandle* rendering, D3D12Buffer* buffer, u64 needed_size)
{
    if (needed_size > buffer->size)
    {
        BufferType type = buffer->type;
        d3d12_buffer_destroy(buffer);
        *buffer = d3d12_buffer_create(rendering, needed_size, type);
    }
}

BUSTER_GLOBAL_LOCAL void d3d12_swapchain_recreate(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    RECT client_rect;
    GetClientRect(window->hwnd, &client_rect);
    u32 width = (u32)(client_rect.right - client_rect.left);
    u32 height = (u32)(client_rect.bottom - client_rect.top);
    string_print(S8("DirectX 12 render target recreate: hwnd={u64:x}, client={u32}x{u32}, presentable={u32}\n"), (u64)(UINT_PTR)window->hwnd, width, height,
                 (u32)(window->swapchain != 0));
    if (!width || !height)
    {
        string_print(S8("DirectX 12 render target recreate skipped: zero-sized client\n"));
        return;
    }
    d3d12_flush(rendering);
    for (u32 i = 0; i < window->frame_count; i += 1)
    {
        BUSTER_D3D12_RELEASE(window->render_targets[i]);
    }

    if (window->swapchain)
    {
        HRESULT resize_result = IDXGISwapChain3_ResizeBuffers(window->swapchain, window->frame_count, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        string_print(S8("DirectX 12 swapchain resize: ResizeBuffers={u64:x}, client={u32}x{u32}, buffers={u32}\n"), (u64)(u32)resize_result, width, height,
                     window->frame_count);
        if (!d3d12_ok(resize_result))
        {
            os_fail();
        }
    }

    window->width = width;
    window->height = height;

    if (window->swapchain)
    {
        window->frame_index = IDXGISwapChain3_GetCurrentBackBufferIndex(window->swapchain);

        for (u32 i = 0; i < window->frame_count; i += 1)
        {
            if (d3d12_ok(IDXGISwapChain3_GetBuffer(window->swapchain, i, &IID_ID3D12Resource, (void**)&window->render_targets[i])))
            {
                D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_cpu_descriptor(window->rtv_heap, window->rtv_descriptor_size, i);
                ID3D12Device_CreateRenderTargetView(rendering->device, window->render_targets[i], 0, rtv);
            }
            else
            {
                string_print(S8("DirectX 12 swapchain back buffer lookup failed: buffer={u32}\n"), i);
            }
        }
    }
    else
    {
        D3D12_HEAP_PROPERTIES default_heap = {.Type = D3D12_HEAP_TYPE_DEFAULT, .CreationNodeMask = 1, .VisibleNodeMask = 1};
        D3D12_RESOURCE_DESC render_target_desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Alignment = 0,
            .Width = width,
            .Height = height,
            .DepthOrArraySize = 1,
            .MipLevels = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        };
        D3D12_CLEAR_VALUE clear_value = {
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .Color = {1.0f, 0.0f, 1.0f, 1.0f},
        };
        for (u32 i = 0; i < window->frame_count; i += 1)
        {
            HRESULT create_render_target_result =
                ID3D12Device_CreateCommittedResource(rendering->device, &default_heap, D3D12_HEAP_FLAG_NONE, &render_target_desc, D3D12_RESOURCE_STATE_PRESENT,
                                                     &clear_value, &IID_ID3D12Resource, (void**)&window->render_targets[i]);
            if (!d3d12_ok(create_render_target_result))
            {
                string_print(S8("DirectX 12 offscreen render target creation failed: CreateCommittedResource={u64:x}, client={u32}x{u32}, target={u32}\n"),
                             (u64)(u32)create_render_target_result, width, height, i);
                os_fail();
            }
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_cpu_descriptor(window->rtv_heap, window->rtv_descriptor_size, i);
            ID3D12Device_CreateRenderTargetView(rendering->device, window->render_targets[i], 0, rtv);
        }
    }
}

RenderingWindowHandle* rendering_window_initialize(Arena* arena, WmHandle* windowing, RenderingHandle* rendering, WmWindowHandle* window)
{
    RenderingWindowHandle* result = arena_allocate(arena, RenderingWindowHandle, 1);
    WmNativeSurface native_surface = wm_window_get_native_surface(windowing, window);
    BUSTER_CHECK(native_surface.kind == WM_NATIVE_SURFACE_WIN32);
    result->hwnd = (HWND)native_surface.window;
    result->frame_count = BUSTER_D3D12_FRAME_COUNT;

    RECT client_rect;
    GetClientRect(result->hwnd, &client_rect);
    result->width = (u32)(client_rect.right - client_rect.left);
    result->height = (u32)(client_rect.bottom - client_rect.top);
    string_print(S8("DirectX 12 render window initialization: hwnd={u64:x}, client={u32}x{u32}, buffers={u32}\n"), (u64)(UINT_PTR)result->hwnd, result->width,
                 result->height, result->frame_count);

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {
        .Width = result->width,
        .Height = result->height,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = result->frame_count,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
        .Flags = 0,
    };
    IDXGISwapChain1* swapchain1 = 0;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = result->frame_count,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    HRESULT create_swapchain_result =
        IDXGIFactory4_CreateSwapChainForHwnd(rendering->factory, (IUnknown*)rendering->queue, result->hwnd, &swapchain_desc, 0, 0, &swapchain1);
    HRESULT query_swapchain_result = E_FAIL;
    HRESULT create_rtv_heap_result = ID3D12Device_CreateDescriptorHeap(rendering->device, &rtv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&result->rtv_heap);
    if (d3d12_ok(create_swapchain_result))
    {
        query_swapchain_result = IDXGISwapChain1_QueryInterface(swapchain1, &IID_IDXGISwapChain3, (void**)&result->swapchain);
    }

    bool presentable = d3d12_ok(create_swapchain_result) && d3d12_ok(query_swapchain_result);
    bool presentation_unavailable = create_swapchain_result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    string_print(S8("DirectX 12 swapchain attempt: CreateSwapChainForHwnd={u64:x}, QueryInterface(IDXGISwapChain3)={u64:x}, CreateDescriptorHeap(RTV)={u64:x}, "
                    "presentable={u32}, presentation_unavailable={u32}\n"),
                 (u64)(u32)create_swapchain_result, (u64)(u32)query_swapchain_result, (u64)(u32)create_rtv_heap_result, (u32)presentable,
                 (u32)presentation_unavailable);
    if ((presentable || presentation_unavailable) && d3d12_ok(create_rtv_heap_result))
    {
        if (presentable)
        {
            IDXGIFactory4_MakeWindowAssociation(rendering->factory, result->hwnd, DXGI_MWA_NO_ALT_ENTER);
        }
        else
        {
            string_print(
                S8("DirectX 12 presentation swapchain unavailable: CreateSwapChainForHwnd={u64:x}; using offscreen render targets for CI/window smoke test\n"),
                (u64)(u32)create_swapchain_result);
        }
        result->rtv_descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(rendering->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        result->rect_descriptor_base = rendering->srv_descriptor_count;
        rendering->srv_descriptor_count += RECT_TEXTURE_SLOT_COUNT;
        BUSTER_CHECK(rendering->srv_descriptor_count <= MAX_D3D12_SRV_DESCRIPTOR_COUNT);
        d3d12_swapchain_recreate(rendering, result);
    }
    BUSTER_D3D12_RELEASE(swapchain1);

    if ((!presentable && !presentation_unavailable) || !d3d12_ok(create_rtv_heap_result))
    {
        string_print(S8("DirectX 12 swapchain creation failed: CreateSwapChainForHwnd={u64:x}, QueryInterface(IDXGISwapChain3)={u64:x}, "
                        "CreateDescriptorHeap(RTV)={u64:x}, hwnd={u64:x}, client={u32}x{u32}, buffers={u32}\n"),
                     (u64)(u32)create_swapchain_result, (u64)(u32)query_swapchain_result, (u64)(u32)create_rtv_heap_result, (u64)(UINT_PTR)result->hwnd,
                     result->width, result->height, result->frame_count);
        BUSTER_D3D12_RELEASE(result->swapchain);
        BUSTER_D3D12_RELEASE(result->rtv_heap);
        return 0;
    }

    for (u32 frame_index = 0; frame_index < result->frame_count; frame_index += 1)
    {
        WindowFrame* frame = &result->frames[frame_index];
        HRESULT create_command_allocator_result = ID3D12Device_CreateCommandAllocator(rendering->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                                      &IID_ID3D12CommandAllocator, (void**)&frame->command_allocator);
        HRESULT create_command_list_result = E_FAIL;
        if (d3d12_ok(create_command_allocator_result))
        {
            create_command_list_result = ID3D12Device_CreateCommandList(rendering->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame->command_allocator,
                                                                        rendering->rect_pipeline, &IID_ID3D12GraphicsCommandList, (void**)&frame->command_list);
        }
        string_print(S8("DirectX 12 frame resources {u32}: command_allocator={u64:x}, command_list={u64:x}\n"), frame_index,
                     (u64)(u32)create_command_allocator_result, (u64)(u32)create_command_list_result);
        if (!d3d12_ok(create_command_allocator_result) || !d3d12_ok(create_command_list_result))
        {
            os_fail();
        }
        ID3D12GraphicsCommandList_Close(frame->command_list);

        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[pipeline_index];
            pipeline->vertex_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->index_buffer.cpu = arena_create((ArenaCreation){0});
            pipeline->vertex_buffer.gpu.type = BUFFER_TYPE_VERTEX;
            pipeline->index_buffer.gpu.type = BUFFER_TYPE_INDEX;
        }
    }
    string_print(S8("DirectX 12 render window initialization succeeded: hwnd={u64:x}, presentable={u32}, frame_count={u32}, rtv_descriptor_size={u32}\n"),
                 (u64)(UINT_PTR)result->hwnd, (u32)(result->swapchain != 0), result->frame_count, result->rtv_descriptor_size);
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
    D3D12_CPU_DESCRIPTOR_HANDLE source =
        d3d12_cpu_descriptor(rendering->srv_heap, rendering->srv_descriptor_size, rendering->textures[texture_index.value].descriptor_index);
    D3D12_CPU_DESCRIPTOR_HANDLE destination =
        d3d12_cpu_descriptor(rendering->srv_heap, rendering->srv_descriptor_size, window->rect_descriptor_base + (u32)slot);
    ID3D12Device_CopyDescriptorsSimple(rendering->device, 1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void rendering_window_rect_texture_update_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    BUSTER_UNUSED(rendering);
    BUSTER_UNUSED(window);
}

TextureIndex rendering_texture_create(RenderingHandle* rendering, TextureMemory texture_memory)
{
    BUSTER_CHECK(texture_memory.depth == 1);
    BUSTER_CHECK(rendering->texture_count < MAX_D3D12_TEXTURE_COUNT);
    u32 texture_index = rendering->texture_count;
    rendering->texture_count += 1;
    D3D12Texture* texture = &rendering->textures[texture_index];
    texture->descriptor_index = texture_index;
    texture->width = texture_memory.width;
    texture->height = texture_memory.height;

    DXGI_FORMAT format = d3d12_texture_format(texture_memory.format);
    D3D12_HEAP_PROPERTIES default_heap = {.Type = D3D12_HEAP_TYPE_DEFAULT, .CreationNodeMask = 1, .VisibleNodeMask = 1};
    D3D12_RESOURCE_DESC texture_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = texture_memory.width,
        .Height = texture_memory.height,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = format,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    if (!d3d12_ok(ID3D12Device_CreateCommittedResource(rendering->device, &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, 0,
                                                       &IID_ID3D12Resource, (void**)&texture->resource)))
    {
        os_fail();
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT num_rows = 0;
    UINT64 row_size_in_bytes = 0;
    UINT64 upload_size = 0;
    ID3D12Device_GetCopyableFootprints(rendering->device, &texture_desc, 0, 1, 0, &footprint, &num_rows, &row_size_in_bytes, &upload_size);
    BUSTER_CHECK(num_rows == texture_memory.height);
    BUSTER_UNUSED(row_size_in_bytes);

    D3D12_HEAP_PROPERTIES upload_heap = {.Type = D3D12_HEAP_TYPE_UPLOAD, .CreationNodeMask = 1, .VisibleNodeMask = 1};
    D3D12_RESOURCE_DESC upload_desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = upload_size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    ID3D12Resource* upload_resource = 0;
    if (!d3d12_ok(ID3D12Device_CreateCommittedResource(rendering->device, &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                       0, &IID_ID3D12Resource, (void**)&upload_resource)))
    {
        os_fail();
    }

    u8* upload_pointer = 0;
    D3D12_RANGE read_range = {0};
    if (d3d12_ok(ID3D12Resource_Map(upload_resource, 0, &read_range, (void**)&upload_pointer)))
    {
        u32 source_row_pitch = texture_memory.width * d3d12_format_channel_count(texture_memory.format);
        for (u32 row = 0; row < texture_memory.height; row += 1)
        {
            memcpy(upload_pointer + footprint.Offset + (u64)row * footprint.Footprint.RowPitch, (u8*)texture_memory.pointer + (u64)row * source_row_pitch,
                   source_row_pitch);
        }
        D3D12_RANGE written_range = {.Begin = 0, .End = (SIZE_T)upload_size};
        ID3D12Resource_Unmap(upload_resource, 0, &written_range);
    }

    ID3D12CommandAllocator_Reset(rendering->upload_command_allocator);
    ID3D12GraphicsCommandList_Reset(rendering->upload_command_list, rendering->upload_command_allocator, 0);
    D3D12_TEXTURE_COPY_LOCATION destination = {.pResource = texture->resource, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0};
    D3D12_TEXTURE_COPY_LOCATION source = {.pResource = upload_resource, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = footprint};
    ID3D12GraphicsCommandList_CopyTextureRegion(rendering->upload_command_list, &destination, 0, 0, 0, &source, 0);
    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {.pResource = texture->resource,
                       .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
                       .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
    };
    ID3D12GraphicsCommandList_ResourceBarrier(rendering->upload_command_list, 1, &barrier);
    ID3D12GraphicsCommandList_Close(rendering->upload_command_list);
    ID3D12CommandList* command_lists[] = {(ID3D12CommandList*)rendering->upload_command_list};
    ID3D12CommandQueue_ExecuteCommandLists(rendering->queue, (UINT)BUSTER_ARRAY_LENGTH(command_lists), command_lists);
    d3d12_flush(rendering);
    BUSTER_D3D12_RELEASE(upload_resource);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
        .Format = format,
        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0, .ResourceMinLODClamp = 0},
    };
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = d3d12_cpu_descriptor(rendering->srv_heap, rendering->srv_descriptor_size, texture->descriptor_index);
    ID3D12Device_CreateShaderResourceView(rendering->device, texture->resource, &srv_desc, descriptor);
    return (TextureIndex){.value = texture_index};
}

BUSTER_GLOBAL_LOCAL WindowFrame* rendering_window_frame(RenderingWindowHandle* window)
{
    return &window->frames[window->frame_index % window->frame_count];
}

void rendering_window_frame_begin(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    RECT client_rect;
    GetClientRect(window->hwnd, &client_rect);
    u32 width = (u32)(client_rect.right - client_rect.left);
    u32 height = (u32)(client_rect.bottom - client_rect.top);
    if (d3d12_frame_begin_log_count < 3)
    {
        string_print(S8("DirectX 12 frame begin {u32}: hwnd={u64:x}, client={u32}x{u32}, stored={u32}x{u32}, presentable={u32}, frame_index={u32}\n"),
                     d3d12_frame_begin_log_count, (u64)(UINT_PTR)window->hwnd, width, height, window->width, window->height, (u32)(window->swapchain != 0),
                     window->frame_index);
        d3d12_frame_begin_log_count += 1;
    }
    if (width && height && (width != window->width || height != window->height))
    {
        d3d12_swapchain_recreate(rendering, window);
    }

    if (window->swapchain)
    {
        window->frame_index = IDXGISwapChain3_GetCurrentBackBufferIndex(window->swapchain);
    }
    WindowFrame* frame = rendering_window_frame(window);
    d3d12_wait_for_fence(rendering, frame->fence_value);
    ID3D12CommandAllocator_Reset(frame->command_allocator);
    ID3D12GraphicsCommandList_Reset(frame->command_list, frame->command_allocator, rendering->rect_pipeline);

    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(frame->pipeline_instantiations); i += 1)
    {
        FramePipelineInstantiation* pipeline_instantiation = &frame->pipeline_instantiations[i];
        arena_reset_to_start(pipeline_instantiation->vertex_buffer.cpu);
        pipeline_instantiation->vertex_buffer.count = 0;
        arena_reset_to_start(pipeline_instantiation->index_buffer.cpu);
    }
}

void rendering_window_frame_end(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    WindowFrame* frame = rendering_window_frame(window);
    ID3D12GraphicsCommandList* command_list = frame->command_list;
    ID3D12Resource* render_target = window->render_targets[window->frame_index];
    if (d3d12_frame_end_log_count < 3)
    {
        string_print(S8("DirectX 12 frame end {u32}: presentable={u32}, frame_index={u32}, render_target={u64:x}, vertex0={u32}, fence={u64}\n"),
                     d3d12_frame_end_log_count, (u32)(window->swapchain != 0), window->frame_index, (u64)(UINT_PTR)render_target,
                     frame->pipeline_instantiations[0].vertex_buffer.count, frame->fence_value);
        d3d12_frame_end_log_count += 1;
    }
    D3D12_RESOURCE_BARRIER begin_barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = render_target,
                       .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                       .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET},
    };
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &begin_barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_cpu_descriptor(window->rtv_heap, window->rtv_descriptor_size, window->frame_index);
    FLOAT clear_color[] = {1.0f, 0.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv, FALSE, 0);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv, clear_color, 0, 0);

    ID3D12DescriptorHeap* descriptor_heaps[] = {rendering->srv_heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, (UINT)BUSTER_ARRAY_LENGTH(descriptor_heaps), descriptor_heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, rendering->root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, rendering->rect_pipeline);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VIEWPORT viewport = {.TopLeftX = 0, .TopLeftY = 0, .Width = (FLOAT)window->width, .Height = (FLOAT)window->height, .MinDepth = 0, .MaxDepth = 1};
    D3D12_RECT scissor = {.left = 0, .top = 0, .right = (LONG)window->width, .bottom = (LONG)window->height};
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &scissor);
    D3D12_GPU_DESCRIPTOR_HANDLE textures = d3d12_gpu_descriptor(rendering->srv_heap, rendering->srv_descriptor_size, window->rect_descriptor_base);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 2, textures);

    for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
    {
        FramePipelineInstantiation* pipeline_instantiation = &frame->pipeline_instantiations[pipeline_index];
        u64 vertex_size = arena_buffer_size(pipeline_instantiation->vertex_buffer.cpu);
        u64 index_size = arena_buffer_size(pipeline_instantiation->index_buffer.cpu);
        if (vertex_size && index_size)
        {
            d3d12_buffer_ensure_capacity(rendering, &pipeline_instantiation->vertex_buffer.gpu, vertex_size);
            d3d12_buffer_ensure_capacity(rendering, &pipeline_instantiation->index_buffer.gpu, index_size);
            u8* vertex_destination = pipeline_instantiation->vertex_buffer.gpu.mapped;
            u8* index_destination = pipeline_instantiation->index_buffer.gpu.mapped;
            if (!vertex_destination || !index_destination)
            {
                os_fail();
            }
            memcpy(vertex_destination, arena_buffer_start(pipeline_instantiation->vertex_buffer.cpu), vertex_size);
            memcpy(index_destination, arena_buffer_start(pipeline_instantiation->index_buffer.cpu), index_size);

            D3D12_GPU_VIRTUAL_ADDRESS vertex_address = ID3D12Resource_GetGPUVirtualAddress(pipeline_instantiation->vertex_buffer.gpu.resource);
            ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list, 0, vertex_address);
            f32 constants[] = {(f32)window->width, (f32)window->height};
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, (UINT)BUSTER_ARRAY_LENGTH(constants), constants, 0);
            D3D12_INDEX_BUFFER_VIEW index_buffer_view = {
                .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(pipeline_instantiation->index_buffer.gpu.resource),
                .SizeInBytes = (UINT)index_size,
                .Format = DXGI_FORMAT_R32_UINT,
            };
            ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &index_buffer_view);
            ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, (UINT)(index_size / sizeof(u32)), 1, 0, 0, 0);
        }
    }

    D3D12_RESOURCE_BARRIER end_barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = render_target,
                       .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                       .StateAfter = D3D12_RESOURCE_STATE_PRESENT},
    };
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &end_barrier);
    ID3D12GraphicsCommandList_Close(command_list);
    ID3D12CommandList* command_lists[] = {(ID3D12CommandList*)command_list};
    ID3D12CommandQueue_ExecuteCommandLists(rendering->queue, (UINT)BUSTER_ARRAY_LENGTH(command_lists), command_lists);
    if (window->swapchain)
    {
        IDXGISwapChain3_Present(window->swapchain, 0, 0);
    }
    frame->fence_value = d3d12_signal(rendering);
    if (!window->swapchain)
    {
        window->frame_index = (window->frame_index + 1) % window->frame_count;
    }
}

void rendering_window_deinitialize(RenderingHandle* rendering, RenderingWindowHandle* window)
{
    string_print(S8("DirectX 12 render window deinitialize: hwnd={u64:x}, presentable={u32}, frame_count={u32}\n"), (u64)(UINT_PTR)window->hwnd,
                 (u32)(window->swapchain != 0), window->frame_count);
    d3d12_flush(rendering);
    for (u32 frame_index = 0; frame_index < window->frame_count; frame_index += 1)
    {
        WindowFrame* frame = &window->frames[frame_index];
        for (BusterPipeline pipeline_index = 0; pipeline_index < BUSTER_PIPELINE_COUNT; pipeline_index += 1)
        {
            FramePipelineInstantiation* pipeline = &frame->pipeline_instantiations[pipeline_index];
            if (pipeline->vertex_buffer.cpu)
                arena_destroy(pipeline->vertex_buffer.cpu, 1);
            if (pipeline->index_buffer.cpu)
                arena_destroy(pipeline->index_buffer.cpu, 1);
            d3d12_buffer_destroy(&pipeline->vertex_buffer.gpu);
            d3d12_buffer_destroy(&pipeline->index_buffer.gpu);
        }
        BUSTER_D3D12_RELEASE(frame->command_list);
        BUSTER_D3D12_RELEASE(frame->command_allocator);
        BUSTER_D3D12_RELEASE(window->render_targets[frame_index]);
    }
    BUSTER_D3D12_RELEASE(window->rtv_heap);
    BUSTER_D3D12_RELEASE(window->swapchain);
}

void rendering_deinitialize(RenderingHandle* rendering)
{
    string_print(S8("DirectX 12 rendering deinitialize: texture_count={u32}, next_fence={u64}\n"), rendering->texture_count, rendering->next_fence_value);
    d3d12_flush(rendering);
    for (u32 i = 0; i < rendering->texture_count; i += 1)
    {
        BUSTER_D3D12_RELEASE(rendering->textures[i].resource);
    }
    BUSTER_D3D12_RELEASE(rendering->upload_command_list);
    BUSTER_D3D12_RELEASE(rendering->upload_command_allocator);
    BUSTER_D3D12_RELEASE(rendering->rect_pipeline);
    BUSTER_D3D12_RELEASE(rendering->root_signature);
    BUSTER_D3D12_RELEASE(rendering->srv_heap);
    BUSTER_D3D12_RELEASE(rendering->fence);
    if (rendering->fence_event)
    {
        CloseHandle(rendering->fence_event);
        rendering->fence_event = 0;
    }
    BUSTER_D3D12_RELEASE(rendering->queue);
    BUSTER_D3D12_RELEASE(rendering->device);
    BUSTER_D3D12_RELEASE(rendering->adapter);
    BUSTER_D3D12_RELEASE(rendering->factory);
#if BUSTER_D3D12_HAS_SDK_LAYERS
    BUSTER_D3D12_RELEASE(rendering->debug_controller1);
    BUSTER_D3D12_RELEASE(rendering->debug_controller);
#endif
    if (rendering->d3dcompiler_library)
    {
        os_dynamic_library_unload(rendering->d3dcompiler_library);
        rendering->d3dcompiler_library = 0;
    }
    if (rendering->dxgi_library)
    {
        os_dynamic_library_unload(rendering->dxgi_library);
        rendering->dxgi_library = 0;
    }
    if (rendering->d3d12_library)
    {
        os_dynamic_library_unload(rendering->d3d12_library);
        rendering->d3d12_library = 0;
    }
    rendering->d3d12_create_device = 0;
    rendering->d3d12_serialize_root_signature = 0;
    rendering->create_dxgi_factory2 = 0;
    rendering->d3d_compile = 0;
}
