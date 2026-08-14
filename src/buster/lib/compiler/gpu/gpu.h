#pragma once

#include <buster/lib/base.h>

typedef enum GpuTargetKind
{
    GPU_TARGET_NONE,
    GPU_TARGET_SPIRV,
    GPU_TARGET_SPIRV32,
    GPU_TARGET_SPIRV64,
    GPU_TARGET_NVPTX32,
    GPU_TARGET_NVPTX64,
    GPU_TARGET_AMDGCN,
    GPU_TARGET_METAL_AIR64,
    GPU_TARGET_DXIL,
    GPU_TARGET_COUNT,
} GpuTargetKind;

typedef enum GpuShaderStage
{
    GPU_SHADER_STAGE_NONE,
    GPU_SHADER_STAGE_COMPUTE,
    GPU_SHADER_STAGE_VERTEX,
    GPU_SHADER_STAGE_FRAGMENT,
    GPU_SHADER_STAGE_GEOMETRY,
    GPU_SHADER_STAGE_HULL,
    GPU_SHADER_STAGE_DOMAIN,
    GPU_SHADER_STAGE_MESH,
    GPU_SHADER_STAGE_AMPLIFICATION,
    GPU_SHADER_STAGE_LIBRARY,
    GPU_SHADER_STAGE_RAY_GENERATION,
    GPU_SHADER_STAGE_INTERSECTION,
    GPU_SHADER_STAGE_ANY_HIT,
    GPU_SHADER_STAGE_CLOSEST_HIT,
    GPU_SHADER_STAGE_MISS,
    GPU_SHADER_STAGE_CALLABLE,
    GPU_SHADER_STAGE_COUNT,
} GpuShaderStage;

typedef struct GpuTarget GpuTarget;
struct GpuTarget
{
    String8 architecture;
    String8 entry_point;
    String8 metal_sdk;
    // The LLVM SPIR-V target spelling, including an optional SPIR-V version,
    // vendor and runtime (for example spirv64v1.6-unknown-vulkan1.3).
    // Other backends derive their triples from `kind`.
    String8 backend_triple;
    GpuTargetKind kind;
    GpuShaderStage stage;
    u16 shader_model_major;
    u16 shader_model_minor;
    u8 address_bits;
    u8 reserved[3];
};

typedef enum GpuTargetParseError
{
    GPU_TARGET_PARSE_ERROR_NONE,
    GPU_TARGET_PARSE_ERROR_NOT_GPU,
    GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE,
    GPU_TARGET_PARSE_ERROR_SHADER_MODEL,
    GPU_TARGET_PARSE_ERROR_STAGE,
    GPU_TARGET_PARSE_ERROR_COUNT,
} GpuTargetParseError;

typedef struct GpuTargetParseResult GpuTargetParseResult;
struct GpuTargetParseResult
{
    GpuTarget target;
    String8 invalid_component;
    GpuTargetParseError error;
};

typedef enum GpuSourceLanguage
{
    GPU_SOURCE_LANGUAGE_AUTOMATIC,
    GPU_SOURCE_LANGUAGE_OPENCL,
    GPU_SOURCE_LANGUAGE_CUDA,
    GPU_SOURCE_LANGUAGE_HIP,
    GPU_SOURCE_LANGUAGE_METAL,
    GPU_SOURCE_LANGUAGE_HLSL,
    GPU_SOURCE_LANGUAGE_LLVM_IR,
    GPU_SOURCE_LANGUAGE_SPIRV_BINARY,
    GPU_SOURCE_LANGUAGE_METAL_AIR,
    GPU_SOURCE_LANGUAGE_COUNT,
} GpuSourceLanguage;

typedef enum GpuPipelineAction
{
    GPU_PIPELINE_ACTION_LINK,
    GPU_PIPELINE_ACTION_PREPROCESS,
    GPU_PIPELINE_ACTION_ASSEMBLY,
    GPU_PIPELINE_ACTION_OBJECT,
    GPU_PIPELINE_ACTION_SYNTAX_ONLY,
    GPU_PIPELINE_ACTION_COUNT,
} GpuPipelineAction;

typedef enum GpuOutputFormat
{
    GPU_OUTPUT_NONE,
    GPU_OUTPUT_PREPROCESSED_SOURCE,
    GPU_OUTPUT_SPIRV_BINARY,
    GPU_OUTPUT_SPIRV_ASSEMBLY,
    GPU_OUTPUT_CUDA_PTX,
    GPU_OUTPUT_AMDGCN_OBJECT,
    GPU_OUTPUT_AMDGCN_CODE_OBJECT,
    GPU_OUTPUT_AMDGCN_ASSEMBLY,
    GPU_OUTPUT_METAL_AIR,
    GPU_OUTPUT_METAL_LIBRARY,
    GPU_OUTPUT_DXIL_CONTAINER,
    GPU_OUTPUT_DXIL_ASSEMBLY,
    GPU_OUTPUT_COUNT,
} GpuOutputFormat;

typedef enum GpuPipelineError
{
    GPU_PIPELINE_ERROR_NONE,
    GPU_PIPELINE_ERROR_INVALID_TARGET,
    GPU_PIPELINE_ERROR_INVALID_INPUT,
    GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION,
    GPU_PIPELINE_ERROR_TOOL_NOT_FOUND,
    GPU_PIPELINE_ERROR_TOOL_FAILED,
    GPU_PIPELINE_ERROR_FILE_READ,
    GPU_PIPELINE_ERROR_FILE_WRITE,
    GPU_PIPELINE_ERROR_INVALID_ARTIFACT,
    GPU_PIPELINE_ERROR_COUNT,
} GpuPipelineError;

typedef struct GpuToolchain GpuToolchain;
struct GpuToolchain
{
    String8 clang_path;
    String8 llc_path;
    String8 spirv_link_path;
    String8 spirv_dis_path;
    String8 xcrun_path;
    String8 dxc_path;
};

typedef struct GpuPipelineOptions GpuPipelineOptions;
struct GpuPipelineOptions
{
    String8* input_paths;
    String8* include_paths;
    String8* system_include_paths;
    String8* definitions;
    String8* undefinitions;
    String8* extra_arguments;
    String8 output_path;
    String8 sysroot;
    String8 cuda_path;
    String8 rocm_path;
    GpuToolchain tools;
    GpuTarget target;
    u32 input_count;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 definition_count;
    u32 undefinition_count;
    u32 extra_argument_count;
    GpuSourceLanguage language;
    GpuPipelineAction action;
    u8 optimization_level;
    bool debug_info;
    bool no_standard_includes;
    bool save_temporaries;
    // Match compiler-driver behaviour for -E/-S without -o: materialize the
    // textual artifact through a private temporary and return its bytes rather
    // than leaving a default-named file behind.
    bool capture_text_output;
    u8 reserved[3];
};

typedef enum GpuPipelineStepKind
{
    GPU_PIPELINE_STEP_PROCESS,
    GPU_PIPELINE_STEP_COPY,
    GPU_PIPELINE_STEP_COUNT,
} GpuPipelineStepKind;

typedef struct GpuPipelineStep GpuPipelineStep;
struct GpuPipelineStep
{
    SliceString8 arguments;
    String8 copy_source;
    String8 output_path;
    GpuPipelineStepKind kind;
    u32 reserved;
};

typedef struct GpuPipelinePlan GpuPipelinePlan;
struct GpuPipelinePlan
{
    GpuPipelineStep* steps;
    String8* temporary_paths;
    String8 diagnostic;
    String8 output_path;
    GpuOutputFormat output_format;
    GpuPipelineError error;
    u32 step_count;
    u32 temporary_path_count;
    bool output_is_temporary;
    u8 reserved[3];
};

typedef struct GpuArtifact GpuArtifact;
struct GpuArtifact
{
    ByteSlice bytes;
    String8 path;
    GpuOutputFormat format;
    u32 reserved;
};

typedef struct GpuPipelineResult GpuPipelineResult;
struct GpuPipelineResult
{
    GpuArtifact artifact;
    String8 diagnostic;
    String8 command;
    String8 log;
    ProcessResult process_result;
    GpuPipelineError error;
    u32 failed_step;
};

BUSTER_F_DECL GpuTargetParseResult gpu_target_parse(String8 triple);
BUSTER_F_DECL bool gpu_target_is_valid(GpuTarget target);
BUSTER_F_DECL String8 gpu_target_to_string(Arena* arena, GpuTarget target);
BUSTER_F_DECL GpuShaderStage gpu_shader_stage_from_string(String8 value);
BUSTER_F_DECL bool gpu_shader_model_parse(String8 value, u16* major, u16* minor);
BUSTER_F_DECL String8 gpu_shader_stage_to_string(GpuShaderStage stage);
BUSTER_F_DECL GpuSourceLanguage gpu_source_language_from_path(String8 path);
BUSTER_F_DECL String8 gpu_source_language_to_string(GpuSourceLanguage language);
BUSTER_F_DECL String8 gpu_output_format_to_string(GpuOutputFormat format);
BUSTER_F_DECL GpuPipelinePlan gpu_pipeline_plan(Arena* arena, GpuPipelineOptions options);
BUSTER_F_DECL GpuPipelineResult gpu_pipeline_execute(Arena* arena, GpuPipelineOptions options);
BUSTER_F_DECL bool gpu_artifact_has_expected_magic(GpuOutputFormat format, ByteSlice bytes);
