#pragma once

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/compiler/wasm/wasm.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/ebpf/ebpf.h>

// A unity C translation unit retains preprocessing, semantic, typed IR, and
// object/debug data through the driver call. The reservation is virtual and
// uses demand-paged commits, so this headroom does not eagerly consume 8 GiB
// of physical memory.
#define COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE BUSTER_GB(32)

typedef enum CompilerDriverError
{
    COMPILER_DRIVER_ERROR_NONE,
    COMPILER_DRIVER_ERROR_ARGUMENT,
    COMPILER_DRIVER_ERROR_INVALID_INPUT,
    COMPILER_DRIVER_ERROR_FILE_READ,
    COMPILER_DRIVER_ERROR_TOKENIZE,
    COMPILER_DRIVER_ERROR_PARSE,
    COMPILER_DRIVER_ERROR_ANALYSIS,
    COMPILER_DRIVER_ERROR_IR,
    COMPILER_DRIVER_ERROR_LLVM_BITCODE,
    COMPILER_DRIVER_ERROR_CODEGEN,
    COMPILER_DRIVER_ERROR_WASM64,
    COMPILER_DRIVER_ERROR_GPU,
    COMPILER_DRIVER_ERROR_EBPF,
    COMPILER_DRIVER_ERROR_OBJECT,
    COMPILER_DRIVER_ERROR_LINK,
    COMPILER_DRIVER_ERROR_COUNT,
} CompilerDriverError;

typedef enum CompilerDriverLanguage
{
    COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
    COMPILER_DRIVER_LANGUAGE_C,
    COMPILER_DRIVER_LANGUAGE_OPENCL,
    COMPILER_DRIVER_LANGUAGE_CUDA,
    COMPILER_DRIVER_LANGUAGE_HIP,
    COMPILER_DRIVER_LANGUAGE_METAL,
    COMPILER_DRIVER_LANGUAGE_HLSL,
    COMPILER_DRIVER_LANGUAGE_LLVM_IR,
    COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY,
    COMPILER_DRIVER_LANGUAGE_METAL_AIR,
    COMPILER_DRIVER_LANGUAGE_COUNT,
} CompilerDriverLanguage;

typedef enum CompilerDriverAction
{
    COMPILER_DRIVER_ACTION_LINK,
    COMPILER_DRIVER_ACTION_PREPROCESS,
    COMPILER_DRIVER_ACTION_ASSEMBLY,
    COMPILER_DRIVER_ACTION_OBJECT,
    COMPILER_DRIVER_ACTION_SYNTAX_ONLY,
    COMPILER_DRIVER_ACTION_COUNT,
} CompilerDriverAction;

typedef enum CompilerDriverCDialect
{
    COMPILER_DRIVER_C_DIALECT_GNU11,
    COMPILER_DRIVER_C_DIALECT_GNU17,
    COMPILER_DRIVER_C_DIALECT_GNU23,
    COMPILER_DRIVER_C_DIALECT_C11,
    COMPILER_DRIVER_C_DIALECT_C17,
    COMPILER_DRIVER_C_DIALECT_C23,
    COMPILER_DRIVER_C_DIALECT_COUNT,
} CompilerDriverCDialect;

typedef struct CompilerDriverInvocation CompilerDriverInvocation;
struct CompilerDriverInvocation
{
    String8* input_paths;
    String8* include_paths;
    String8* system_include_paths;
    String8* definitions;
    String8* undefinitions;
    String8* library_paths;
    String8* libraries;
    String8* framework_paths;
    String8* frameworks;
    String8* linker_arguments;
    String8* gpu_arguments;
    String8 output_path;
    String8 entry_symbol;
    String8 sysroot;
    // Where to write the source measurement as key=value text. `-v` prints the
    // same numbers as a table for a human; this is the form another program
    // reads, so a build driver can divide its own instruction count by them.
    String8 source_metrics_path;
    String8 gpu_architecture;
    String8 gpu_entry_point;
    String8 gpu_stage;
    String8 gpu_shader_model;
    String8 metal_sdk;
    String8 cuda_path;
    String8 rocm_path;
    String8 diagnostic;
    GpuToolchain gpu_tools;
    GpuTarget gpu_target;
    Target target;
    u32 input_count;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 definition_count;
    u32 undefinition_count;
    u32 library_path_count;
    u32 library_count;
    u32 framework_path_count;
    u32 framework_count;
    u32 linker_argument_count;
    u32 gpu_argument_count;
    CompilerDriverLanguage language;
    CompilerDriverAction action;
    CompilerDriverCDialect c_dialect;
    CompilerDriverError error;
    AssemblySyntax assembly_syntax;
    bool emit_llvm_bitcode;
    bool verbose;
    bool no_standard_includes;
    bool debug_info;
    // A CodegenRegisterAllocatorMode value. FAST is the driver default;
    // -fregister-allocator= selects another mode and
    // -fno-register-allocator selects NONE.
    u8 register_allocator;
    u8 optimization_level;
    bool has_gpu_target;
    bool save_gpu_temporaries;
    bool register_allocator_explicit;
    bool c_dialect_explicit;
};

typedef struct CompilerDriverResult CompilerDriverResult;
struct CompilerDriverResult
{
    String8 diagnostic;
    String8 warning;
    String8 output;
    NativeExecutableLinkResult native_link;
    Wasm64Artifact wasm64;
    GpuArtifact gpu;
    LlvmBitcodeArtifact llvm_bitcode;
    EbpfArtifact ebpf;
    ObjectFile object;
    CodegenStatistics codegen_statistics;
    // What the C frontend consumed, per inclusion and per distinct file, and
    // what preprocessing made of it.
    CSourceMetrics source_lexed;
    CSourceMetrics source_unique;
    CPreprocessedMetrics preprocessed;
    CompilerDriverError error;
    CodegenError codegen_error;
    ObjectError object_error;
    u32 tokenizer_error_count;
    u32 tokenizer_warning_count;
    u32 parser_diagnostic_count;
    u32 analysis_diagnostic_count;
    bool has_object;
    bool has_wasm64;
    bool has_gpu;
    bool has_llvm_bitcode;
    bool has_ebpf;
    u8 reserved;
};

// Fills every table the compile pipeline builds on first use -- the C
// frontend and codegen -- on the calling thread. Call this before lane_run:
// those tables are written once and read afterwards through plain loads, so
// they must be complete before a second lane can reach them, and a gang that
// finds one unwarmed reports through BUSTER_CHECK_SERIAL_INITIALIZATION
// rather than racing.
BUSTER_F_DECL void compiler_prewarm(void);
BUSTER_F_DECL CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments);
BUSTER_F_DECL CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation);
