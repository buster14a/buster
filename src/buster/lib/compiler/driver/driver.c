// The Clang-like `ide cc` driver: two public calls (driver.h) split
// argument parsing from execution. compiler_driver_parse_arguments turns
// argv into a CompilerDriverInvocation — dialect, target/CPU/features,
// inputs classified as C source, objects, or archives, and every output
// mode — rejecting unknown languages and retired options explicitly
// rather than guessing. compiler_driver_execute_invocation then runs the
// selected pipeline: compiler_driver_execute_c_single carries a C input
// through preprocess, parse, lowering, codegen, and object/executable
// output (with -emit-llvm, Wasm64, and eBPF as alternate emissions), the
// dynamic-library plumbing around compiler_driver_dynamic_libraries
// resolves import libraries for hosted links — reading a library's own
// export table when one is needed, through
// compiler_driver_pe_library_exports on Windows and
// compiler_driver_elf_library_exports on Linux — and
// compiler_driver_execute_gpu isolates the external shader-toolchain
// orchestration in gpu.h. compiler_prewarm at the top fills every lazily
// built compile-path table on one thread before parallel lanes run
// (AGENTS.md).

#include <buster/lib/compiler/driver/driver.h>

#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>

void compiler_prewarm(void)
{
    c_prewarm();
    codegen_prewarm();
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_windows_runtime_object_target(Target target)
{
    return target.os == OPERATING_SYSTEM_WINDOWS && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
}

// The ELF runtime object carries the glibc stubs that live in
// libc_nonshared.a rather than in the shared object the ELF writers import
// from; see link_elf_libc_runtime_object.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_runtime_object_target(Target target)
{
    return target.os == OPERATING_SYSTEM_LINUX && (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64);
}

// How many synthetic runtime objects a hosted link for this target can add:
// Windows takes the unconditional `_fltused` marker plus the UCRT exit-handler
// stubs, ELF only the glibc ones.  Both stub objects are selected the way an
// archive member is, so this is the capacity to reserve, not the count that
// will be used.
BUSTER_GLOBAL_LOCAL u32 compiler_driver_runtime_object_capacity(Target target)
{
    return compiler_driver_windows_runtime_object_target(target) ? 2 : compiler_driver_elf_runtime_object_target(target) ? 1 : 0;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_option_value(String8 argument, String8 prefix)
{
    if (!string_starts_with_sequence(argument, prefix))
    {
        return (String8){0};
    }
    return (String8){
        .pointer = argument.pointer + prefix.length,
        .length = argument.length - prefix.length,
    };
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_dialect(CompilerDriverInvocation* invocation, String8 dialect)
{
    if (string_equal(dialect, S8("gnu99")) || string_equal(dialect, S8("gnu9x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU99;
    }
    else if (string_equal(dialect, S8("gnu11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU11;
    }
    else if (string_equal(dialect, S8("gnu17")) || string_equal(dialect, S8("gnu18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17;
    }
    else if (string_equal(dialect, S8("gnu23")) || string_equal(dialect, S8("gnu2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU23;
    }
    else if (string_equal(dialect, S8("c99")) || string_equal(dialect, S8("c9x")) || string_equal(dialect, S8("iso9899:1999")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C99;
    }
    else if (string_equal(dialect, S8("c11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C11;
    }
    else if (string_equal(dialect, S8("c17")) || string_equal(dialect, S8("c18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C17;
    }
    else if (string_equal(dialect, S8("c23")) || string_equal(dialect, S8("c2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C23;
    }
    else
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CPreprocessDialect compiler_driver_preprocess_dialect(CompilerDriverCDialect dialect)
{
    switch (dialect)
    {
    case COMPILER_DRIVER_C_DIALECT_GNU99:
        return C_PREPROCESS_DIALECT_GNU99;
    case COMPILER_DRIVER_C_DIALECT_GNU11:
        return C_PREPROCESS_DIALECT_GNU11;
    case COMPILER_DRIVER_C_DIALECT_GNU17:
        return C_PREPROCESS_DIALECT_GNU17;
    case COMPILER_DRIVER_C_DIALECT_GNU23:
        return C_PREPROCESS_DIALECT_GNU23;
    case COMPILER_DRIVER_C_DIALECT_C99:
        return C_PREPROCESS_DIALECT_C99;
    case COMPILER_DRIVER_C_DIALECT_C11:
        return C_PREPROCESS_DIALECT_C11;
    case COMPILER_DRIVER_C_DIALECT_C17:
        return C_PREPROCESS_DIALECT_C17;
    case COMPILER_DRIVER_C_DIALECT_C23:
        return C_PREPROCESS_DIALECT_C23;
    case COMPILER_DRIVER_C_DIALECT_COUNT:
        return C_PREPROCESS_DIALECT_COUNT;
    }
    return C_PREPROCESS_DIALECT_COUNT;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_argument_error(Arena* arena, CompilerDriverInvocation* invocation, String8 format, String8 argument)
{
    invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
    invocation->diagnostic = string_format(arena, format, argument);
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_cpu_model(Arena* arena, CompilerDriverInvocation* invocation, String8 model_string)
{
    CpuModel model = cpu_model_from_string(model_string);
    if (model == CPU_MODEL_ERROR)
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported CPU model: {S8}"), model_string);
        return false;
    }
    invocation->target.cpu_model = model;
    invocation->target.cpu_features_explicit = false;
    invocation->target.cpu_features = target_cpu_features_empty();
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_target(Arena* arena, CompilerDriverInvocation* invocation, String8 target_string)
{
    GpuTargetParseResult gpu = gpu_target_parse(target_string);
    switch (gpu.error)
    {
    case GPU_TARGET_PARSE_ERROR_NONE:
        invocation->gpu_target = gpu.target;
        invocation->has_gpu_target = true;
        return true;
    case GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE:
        compiler_driver_argument_error(arena, invocation, S8("unsupported GPU target component: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_SHADER_MODEL:
        compiler_driver_argument_error(arena, invocation, S8("unsupported DXIL shader model: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_STAGE:
        compiler_driver_argument_error(arena, invocation, S8("unsupported GPU shader stage: {S8}"), gpu.invalid_component);
        return false;
    case GPU_TARGET_PARSE_ERROR_NOT_GPU:
    case GPU_TARGET_PARSE_ERROR_COUNT:
        break;
    }

    TargetParseResult parsed = target_parse_triple(target_string);
    switch (parsed.error)
    {
    case TARGET_PARSE_ERROR_NONE:
        invocation->target = parsed.target;
        invocation->gpu_target = (GpuTarget){0};
        invocation->has_gpu_target = false;
        return true;
    case TARGET_PARSE_ERROR_CPU_MODEL:
        compiler_driver_argument_error(arena, invocation, S8("CPU model must be selected with -march=: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_EXCESS_COMPONENT:
        compiler_driver_argument_error(arena, invocation, S8("unsupported target component: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_EMPTY:
    case TARGET_PARSE_ERROR_ARCHITECTURE:
    case TARGET_PARSE_ERROR_OPERATING_SYSTEM:
    case TARGET_PARSE_ERROR_COUNT:
        break;
    }
    compiler_driver_argument_error(arena, invocation, S8("unsupported target: {S8}"), target_string);
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_kind_is_spirv(GpuTargetKind kind)
{
    return kind == GPU_TARGET_SPIRV || kind == GPU_TARGET_SPIRV32 || kind == GPU_TARGET_SPIRV64;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_target_uses_dxc(GpuTargetKind kind)
{
    return kind == GPU_TARGET_DXIL || kind == GPU_TARGET_SPIRV;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_gpu_stage_uses_entry_point(GpuShaderStage stage)
{
    return stage >= GPU_SHADER_STAGE_COMPUTE && stage <= GPU_SHADER_STAGE_AMPLIFICATION;
}

BUSTER_GLOBAL_LOCAL GpuSourceLanguage compiler_driver_gpu_language(CompilerDriverLanguage language)
{
    switch (language)
    {
    case COMPILER_DRIVER_LANGUAGE_AUTOMATIC: return GPU_SOURCE_LANGUAGE_AUTOMATIC;
    case COMPILER_DRIVER_LANGUAGE_OPENCL: return GPU_SOURCE_LANGUAGE_OPENCL;
    case COMPILER_DRIVER_LANGUAGE_CUDA: return GPU_SOURCE_LANGUAGE_CUDA;
    case COMPILER_DRIVER_LANGUAGE_HIP: return GPU_SOURCE_LANGUAGE_HIP;
    case COMPILER_DRIVER_LANGUAGE_METAL: return GPU_SOURCE_LANGUAGE_METAL;
    case COMPILER_DRIVER_LANGUAGE_HLSL: return GPU_SOURCE_LANGUAGE_HLSL;
    case COMPILER_DRIVER_LANGUAGE_LLVM_IR: return GPU_SOURCE_LANGUAGE_LLVM_IR;
    case COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY: return GPU_SOURCE_LANGUAGE_SPIRV_BINARY;
    case COMPILER_DRIVER_LANGUAGE_METAL_AIR: return GPU_SOURCE_LANGUAGE_METAL_AIR;
    case COMPILER_DRIVER_LANGUAGE_C:
    case COMPILER_DRIVER_LANGUAGE_ASSEMBLY:
    case COMPILER_DRIVER_LANGUAGE_COUNT: break;
    }
    return GPU_SOURCE_LANGUAGE_COUNT;
}

BUSTER_GLOBAL_LOCAL GpuSourceLanguage compiler_driver_gpu_effective_language(CompilerDriverInvocation invocation, String8 path)
{
    GpuSourceLanguage language = compiler_driver_gpu_language(invocation.language);
    return language == GPU_SOURCE_LANGUAGE_AUTOMATIC ? gpu_source_language_from_path(path) : language;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_metal_sdk_is_valid(String8 sdk)
{
    String8 values[] = {
        S8("macosx"), S8("iphoneos"), S8("iphonesimulator"), S8("appletvos"),
        S8("appletvsimulator"), S8("xros"), S8("xrsimulator"),
    };
    bool result = false;
    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values) && !result; value_index += 1)
    {
        result = string_equal(sdk, values[value_index]);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_dxc_shader_model_is_valid(GpuTarget target)
{
    bool result;
    if (target.shader_model_major != 6 || target.shader_model_minor > 10)
    {
        result = false;
    }
    else if ((target.stage == GPU_SHADER_STAGE_MESH || target.stage == GPU_SHADER_STAGE_AMPLIFICATION) && target.shader_model_minor < 5)
    {
        result = false;
    }
    else if (target.stage >= GPU_SHADER_STAGE_LIBRARY && target.shader_model_minor == 0)
    {
        result = false;
    }
    else
    {
        result = true;
    }

    return result;
}

typedef struct CompilerDriverFeatureOverride CompilerDriverFeatureOverride;
struct CompilerDriverFeatureOverride
{
    String8 name;
    bool enable;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_parse_feature_overrides(Arena* arena, CompilerDriverInvocation* invocation, String8 value,
                                                                  CompilerDriverFeatureOverride* overrides, u64 override_capacity, u64* override_count)
{
    u64 start = 0;
    while (start < value.length)
    {
        u64 end = start;
        while (end < value.length && value.pointer[end] != ',')
        {
            end += 1;
        }
        String8 item = string_slice(value, start, end);
        if (item.length < 2 || (item.pointer[0] != '+' && item.pointer[0] != '-'))
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), item);
            return false;
        }
        if (*override_count >= override_capacity)
        {
            compiler_driver_argument_error(arena, invocation, S8("too many target feature overrides: {S8}"), value);
            return false;
        }
        overrides[*override_count] = (CompilerDriverFeatureOverride){
            .name = string_slice(item, 1, item.length),
            .enable = item.pointer[0] == '+',
        };
        *override_count += 1;
        if (end == value.length)
        {
            return true;
        }
        start = end + 1;
        if (start == value.length)
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
            return false;
        }
    }
    compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_assembly_syntax(Arena* arena, CompilerDriverInvocation* invocation, String8 value)
{
    if (string_equal(value, S8("att")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_ATT;
        return true;
    }
    if (string_equal(value, S8("intel")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_INTEL;
        return true;
    }
    compiler_driver_argument_error(arena, invocation, S8("unsupported assembly syntax: {S8}"), value);
    return false;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_entry_symbol(Target target)
{
    return target.os == OPERATING_SYSTEM_UEFI ? S8("UefiMain") : S8("main");
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_executable_path(Target target)
{
    return target.os == OPERATING_SYSTEM_UEFI      ? S8("a.efi")
           : target.os == OPERATING_SYSTEM_WINDOWS ? S8("a.exe")
                                                   : S8("a.out");
}

// A GPU pipeline runs entirely in an external toolchain, so every option that
// only the native backend understands is a mistake rather than a no-op.
BUSTER_GLOBAL_LOCAL void compiler_driver_reject_gpu_native_options(Arena* arena, CompilerDriverInvocation* invocation, u64 feature_override_count)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool spirv_target = compiler_driver_gpu_kind_is_spirv(gpu_kind);
    bool dxc_target = compiler_driver_gpu_target_uses_dxc(gpu_kind);
    bool llvm_gpu_target = spirv_target || gpu_kind == GPU_TARGET_NVPTX32 || gpu_kind == GPU_TARGET_NVPTX64 || gpu_kind == GPU_TARGET_AMDGCN;
    if (invocation->emit_llvm_bitcode)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("-emit-llvm is not supported for external GPU target pipelines");
    }
    else if (feature_override_count)
    {
        compiler_driver_argument_error(arena, invocation, S8("-mattr is not supported for GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        compiler_driver_argument_error(arena, invocation, S8("assembly syntax is not supported for GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->register_allocator_explicit)
    {
        compiler_driver_argument_error(arena, invocation, S8("the native register allocator is not used by GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->c_dialect_explicit)
    {
        compiler_driver_argument_error(arena, invocation, S8("the native C dialect option is not used by GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->source_metrics_path.length)
    {
        compiler_driver_argument_error(arena, invocation, S8("source metrics are not supported for GPU target: {S8}"), invocation->source_metrics_path);
    }
    else if (invocation->language == COMPILER_DRIVER_LANGUAGE_C)
    {
        compiler_driver_argument_error(arena, invocation, S8("native source language is incompatible with GPU target: {S8}"),
                                       S8("c"));
    }
    else if (invocation->library_path_count || invocation->library_count || invocation->framework_path_count || invocation->framework_count ||
        invocation->linker_argument_count)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("native libraries, frameworks, and linker arguments cannot be used in a GPU pipeline; pass backend options with -Xgpu");
    }
    else if (invocation->action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY && invocation->output_path.length)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("cannot specify -o with -fsyntax-only for a GPU target");
    }
    else if (invocation->cuda_path.length && gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64)
    {
        compiler_driver_argument_error(arena, invocation, S8("CUDA toolkit path is incompatible with GPU target: {S8}"), invocation->cuda_path);
    }
    else if (invocation->rocm_path.length && gpu_kind != GPU_TARGET_AMDGCN)
    {
        compiler_driver_argument_error(arena, invocation, S8("ROCm path is incompatible with GPU target: {S8}"), invocation->rocm_path);
    }
    else if ((invocation->gpu_tools.spirv_link_path.length || invocation->gpu_tools.spirv_dis_path.length) && !spirv_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("SPIR-V tool override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.xcrun_path.length && gpu_kind != GPU_TARGET_METAL_AIR64)
    {
        compiler_driver_argument_error(arena, invocation, S8("xcrun override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.dxc_path.length && !dxc_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("DXC override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.clang_path.length && !llvm_gpu_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU Clang override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
    else if (invocation->gpu_tools.llc_path.length && !llvm_gpu_target)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU llc override is incompatible with GPU target: {S8}"),
                                       gpu_target_to_string(arena, invocation->gpu_target));
    }
}

// Folds the GPU architecture, shader stage, shader model, entry point and Metal
// SDK the command line asked for into the target itself. Each step runs only
// while the ones before it agreed.
BUSTER_GLOBAL_LOCAL void compiler_driver_resolve_gpu_target(Arena* arena, CompilerDriverInvocation* invocation, String8 architecture_option)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool dxc_target = compiler_driver_gpu_target_uses_dxc(gpu_kind);
    if (invocation->gpu_architecture.length && architecture_option.length && !string_equal(invocation->gpu_architecture, architecture_option))
    {
        compiler_driver_argument_error(arena, invocation, S8("conflicting GPU architectures: {S8}"), invocation->gpu_architecture);
    }
    String8 gpu_architecture = invocation->gpu_architecture.length ? invocation->gpu_architecture : architecture_option;
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && gpu_architecture.length)
    {
        if (gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64 && gpu_kind != GPU_TARGET_AMDGCN)
        {
            compiler_driver_argument_error(arena, invocation, S8("GPU architecture is incompatible with target: {S8}"), gpu_architecture);
        }
        else
        {
            invocation->gpu_target.architecture = gpu_architecture;
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->gpu_stage.length)
    {
        GpuShaderStage stage = gpu_shader_stage_from_string(invocation->gpu_stage);
        if (stage == GPU_SHADER_STAGE_NONE)
        {
            compiler_driver_argument_error(arena, invocation, S8("unsupported GPU shader stage: {S8}"), invocation->gpu_stage);
        }
        else if (gpu_kind == GPU_TARGET_METAL_AIR64 || (!dxc_target && stage != GPU_SHADER_STAGE_COMPUTE))
        {
            compiler_driver_argument_error(arena, invocation, S8("shader stage is incompatible with GPU target: {S8}"), invocation->gpu_stage);
        }
        else
        {
            invocation->gpu_target.stage = stage;
            if (dxc_target && !compiler_driver_gpu_stage_uses_entry_point(stage))
            {
                invocation->gpu_target.entry_point = (String8){0};
            }
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->gpu_shader_model.length &&
        (!dxc_target || !gpu_shader_model_parse(invocation->gpu_shader_model, &invocation->gpu_target.shader_model_major,
                                                &invocation->gpu_target.shader_model_minor)))
    {
        compiler_driver_argument_error(arena, invocation, S8("shader model is incompatible with GPU target: {S8}"), invocation->gpu_shader_model);
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && dxc_target && !compiler_driver_dxc_shader_model_is_valid(invocation->gpu_target))
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported shader model for target stage: {S8}"),
                                       invocation->gpu_shader_model.length ? invocation->gpu_shader_model
                                                                           : gpu_target_to_string(arena, invocation->gpu_target));
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation->gpu_entry_point.length)
        {
            if (!dxc_target || !compiler_driver_gpu_stage_uses_entry_point(invocation->gpu_target.stage))
            {
                compiler_driver_argument_error(arena, invocation, S8("GPU entry point is incompatible with target or stage: {S8}"),
                                               invocation->gpu_entry_point);
            }
            else
            {
                invocation->gpu_target.entry_point = invocation->gpu_entry_point;
            }
        }
        else if (dxc_target && compiler_driver_gpu_stage_uses_entry_point(invocation->gpu_target.stage) && !invocation->gpu_target.entry_point.length)
        {
            invocation->gpu_target.entry_point = S8("main");
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->metal_sdk.length)
    {
        if (gpu_kind != GPU_TARGET_METAL_AIR64 || !compiler_driver_metal_sdk_is_valid(invocation->metal_sdk))
        {
            compiler_driver_argument_error(arena, invocation, S8("unsupported Metal SDK for GPU target: {S8}"), invocation->metal_sdk);
        }
        else
        {
            invocation->gpu_target.metal_sdk = invocation->metal_sdk;
        }
    }
}

// The options a GPU pipeline accepts depend on what the inputs actually are:
// HLSL sources reach DXC, CUDA sources need the toolkit path.
BUSTER_GLOBAL_LOCAL void compiler_driver_check_gpu_inputs(Arena* arena, CompilerDriverInvocation* invocation)
{
    GpuTargetKind gpu_kind = invocation->gpu_target.kind;
    bool has_hlsl_input = gpu_kind == GPU_TARGET_DXIL;
    bool has_cuda_input = false;
    for (u32 input_index = 0; input_index < invocation->input_count; input_index += 1)
    {
        GpuSourceLanguage input_language = compiler_driver_gpu_effective_language(*invocation, invocation->input_paths[input_index]);
        has_hlsl_input = has_hlsl_input || input_language == GPU_SOURCE_LANGUAGE_HLSL;
        has_cuda_input = has_cuda_input || input_language == GPU_SOURCE_LANGUAGE_CUDA;
    }
    if (gpu_kind == GPU_TARGET_SPIRV && !has_hlsl_input &&
        (invocation->gpu_entry_point.length || invocation->gpu_shader_model.length || invocation->gpu_tools.dxc_path.length ||
         invocation->gpu_target.stage != GPU_SHADER_STAGE_COMPUTE))
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("SPIR-V shader stage, entry point, shader model, and DXC options require HLSL input");
    }
    else if (invocation->cuda_path.length && !has_cuda_input)
    {
        compiler_driver_argument_error(arena, invocation, S8("CUDA toolkit path requires CUDA input: {S8}"), invocation->cuda_path);
    }
    else if (has_hlsl_input && (invocation->sysroot.length || invocation->no_standard_includes))
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("DXC HLSL pipelines do not support -isysroot/--sysroot or -nostdinc");
    }
    else if (!gpu_target_is_valid(invocation->gpu_target))
    {
        String8 target = gpu_target_to_string(arena, invocation->gpu_target);
        compiler_driver_argument_error(arena, invocation, S8("incomplete GPU target configuration: {S8}"), target);
    }
}

// Resolves the native target's CPU model and feature set, and rejects the GPU
// options that have no target to apply to.
BUSTER_GLOBAL_LOCAL void compiler_driver_resolve_native_target(Arena* arena, CompilerDriverInvocation* invocation, String8 architecture_option,
                                                               CompilerDriverFeatureOverride* feature_overrides, u64 feature_override_count)
{
    bool gpu_option = invocation->gpu_architecture.length || invocation->gpu_entry_point.length || invocation->gpu_stage.length ||
                      invocation->gpu_shader_model.length || invocation->metal_sdk.length || invocation->cuda_path.length || invocation->rocm_path.length ||
                      invocation->gpu_tools.clang_path.length || invocation->gpu_tools.llc_path.length || invocation->gpu_tools.spirv_link_path.length ||
                      invocation->gpu_tools.spirv_dis_path.length || invocation->gpu_tools.xcrun_path.length || invocation->gpu_tools.dxc_path.length ||
                      invocation->gpu_argument_count || invocation->save_gpu_temporaries || invocation->language > COMPILER_DRIVER_LANGUAGE_C;
    if (gpu_option)
    {
        compiler_driver_argument_error(arena, invocation, S8("GPU option requires a GPU target: {S8}"),
                                       S8("use --target=spirv64, nvptx64-nvidia-cuda, amdgcn-amd-amdhsa, air64-apple-macos, or dxil"));
    }
    else if (architecture_option.length)
    {
        compiler_driver_set_cpu_model(arena, invocation, architecture_option);
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && feature_override_count)
    {
        invocation->target.cpu_features = target_cpu_features_effective(invocation->target);
        invocation->target.cpu_features_explicit = true;
        for (u64 override_index = 0; override_index < feature_override_count && invocation->error == COMPILER_DRIVER_ERROR_NONE; override_index += 1)
        {
            CompilerDriverFeatureOverride override = feature_overrides[override_index];
            TargetCpuFeature feature = target_cpu_feature_from_string(invocation->target.cpu_arch, override.name);
            if (feature == TARGET_CPU_FEATURE_NONE)
            {
                compiler_driver_argument_error(arena, invocation, S8("unsupported target feature: {S8}"), override.name);
            }
            else if (override.enable)
            {
                invocation->target.cpu_features = target_cpu_features_add(invocation->target.cpu_features, feature);
            }
            else
            {
                invocation->target.cpu_features = target_cpu_features_remove(invocation->target.cpu_features, feature);
            }
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && !target_cpu_features_are_valid(invocation->target))
    {
        if (feature_override_count)
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature combination: {S8}"),
                                           target_cpu_features_to_string(arena, invocation->target));
        }
        else
        {
            compiler_driver_argument_error(arena, invocation, S8("CPU model is incompatible with target: {S8}"),
                                           cpu_model_to_string_os(invocation->target.cpu_model));
        }
    }
    if (invocation->error == COMPILER_DRIVER_ERROR_NONE && invocation->target.cpu_arch != CPU_ARCH_X86_64 &&
        invocation->assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        compiler_driver_argument_error(arena, invocation, S8("assembly syntax is incompatible with target: {S8}"),
                                       invocation->assembly_syntax == ASSEMBLY_SYNTAX_ATT ? S8("att") : S8("intel"));
    }
}

// The builtin resource headers plus whatever the sysroot, the target triple, or
// the host environment says the system headers are.
#if BUSTER_WINDOWS || BUSTER_INCLUDE_TESTS
// Reserve from the actual INCLUDE population, not from a fixed allowance.
// The prefix already contains -isystem and resource paths in search order.
BUSTER_GLOBAL_LOCAL void compiler_driver_append_environment_includes(Arena* arena, CompilerDriverInvocation* invocation, String8 includes)
{
    u64 extra_count = 0;
    for (u64 i = 0; i < includes.length; ++i)
        extra_count += includes.pointer[i] != ';' && (i == 0 || includes.pointer[i - 1] == ';');
    u64 count = (u64)invocation->system_include_path_count + extra_count;
    if (count > UINT32_MAX)
    {
        invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation->diagnostic = S8("too many system include paths");
    }
    else if (extra_count)
    {
        String8* paths = arena_allocate(arena, String8, count);
        if (invocation->system_include_path_count)
            memcpy(paths, invocation->system_include_paths, (u64)invocation->system_include_path_count * sizeof(*paths));
        // Keep the environment snapshot alive for the invocation, independently
        // of later environment queries or the caller's temporary storage.
        includes = string_duplicate_arena(arena, includes, false);
        for (u64 start = 0; start < includes.length;)
        {
            u64 end = start;
            while (end < includes.length && includes.pointer[end] != ';')
                ++end;
            if (end != start)
                paths[invocation->system_include_path_count++] = string_slice(includes, start, end);
            start = end + (end < includes.length);
        }
        invocation->system_include_paths = paths;
    }
}
#endif

#if BUSTER_INCLUDE_TESTS
void compiler_driver_test_append_environment_includes(Arena* arena, CompilerDriverInvocation* invocation, String8 includes)
{
    compiler_driver_append_environment_includes(arena, invocation, includes);
}
#endif

BUSTER_GLOBAL_LOCAL void compiler_driver_append_system_includes(Arena* arena, CompilerDriverInvocation* invocation)
{
#if defined(BUSTER_HOST_C_RESOURCE_INCLUDE)
    if (sizeof(BUSTER_HOST_C_RESOURCE_INCLUDE) > 1)
    {
        invocation->system_include_paths[invocation->system_include_path_count++] = S8(BUSTER_HOST_C_RESOURCE_INCLUDE);
    }
#endif
    if (invocation->sysroot.length)
    {
        invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/usr/local/include"), invocation->sysroot);
        if (invocation->target.os == OPERATING_SYSTEM_LINUX || invocation->target.os == OPERATING_SYSTEM_ANDROID)
        {
            String8 multiarch = invocation->target.cpu_arch == CPU_ARCH_AARCH64
                                    ? (invocation->target.os == OPERATING_SYSTEM_ANDROID ? S8("aarch64-linux-android") : S8("aarch64-linux-gnu"))
                                    : (invocation->target.os == OPERATING_SYSTEM_ANDROID ? S8("x86_64-linux-android") : S8("x86_64-linux-gnu"));
            invocation->system_include_paths[invocation->system_include_path_count++] =
                string_format(arena, S8("{S8}/usr/include/{S8}"), invocation->sysroot, multiarch);
        }
        else if (invocation->target.os == OPERATING_SYSTEM_WINDOWS)
        {
            invocation->system_include_paths[invocation->system_include_path_count++] =
                string_format(arena, S8("{S8}/x86_64-w64-mingw32/include"), invocation->sysroot);
            invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/include"), invocation->sysroot);
        }
        invocation->system_include_paths[invocation->system_include_path_count++] = string_format(arena, S8("{S8}/usr/include"), invocation->sysroot);
    }
    else if (invocation->target.cpu_arch == target_native.cpu_arch && invocation->target.os == target_native.os)
    {
#if BUSTER_LINUX
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/local/include");
#if BUSTER_CPU_ARCH_X86_64
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include/x86_64-linux-gnu");
#else
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include/aarch64-linux-gnu");
#endif
        invocation->system_include_paths[invocation->system_include_path_count++] = S8("/usr/include");
#endif
#if BUSTER_WINDOWS
        // Capture once: counting and appending must see the same snapshot.
        compiler_driver_append_environment_includes(arena, invocation, os_get_environment_variable(S8("INCLUDE")));
#endif
    }
}


CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments)
{
    CompilerDriverInvocation invocation = {
        .target = target_native,
        .language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
        .action = COMPILER_DRIVER_ACTION_LINK,
        .c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17,
        .debug_info = true,
        // Native machine code needs register placement even when source-level
        // optimization is disabled. Match LLVM's -O0 policy by using the
        // low-latency allocator unless the caller explicitly opts out.
        .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
    };
    if (!arena)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        return invocation;
    }
    // At most one resource directory plus four sysroot directories. Native
    // Windows INCLUDE entries reserve their own exact-sized array below.
    u64 default_include_capacity = 5;
    if (arguments.length > UINT32_MAX - default_include_capacity)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("too many compiler arguments");
        return invocation;
    }
    invocation.input_paths = arena_allocate(arena, String8, arguments.length);
    invocation.include_paths = arena_allocate(arena, String8, arguments.length);
    invocation.system_include_paths = arena_allocate(arena, String8, arguments.length + default_include_capacity);
    invocation.definitions = arena_allocate(arena, String8, arguments.length);
    invocation.undefinitions = arena_allocate(arena, String8, arguments.length);
    invocation.library_paths = arena_allocate(arena, String8, arguments.length);
    invocation.libraries = arena_allocate(arena, String8, arguments.length);
    invocation.framework_paths = arena_allocate(arena, String8, arguments.length);
    invocation.frameworks = arena_allocate(arena, String8, arguments.length);
    invocation.linker_arguments = arena_allocate(arena, String8, arguments.length);
    invocation.gpu_arguments = arena_allocate(arena, String8, arguments.length);
    u64 feature_override_capacity = 0;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        feature_override_capacity += arguments.pointer[argument_index].length / 2 + 1;
    }
    CompilerDriverFeatureOverride* feature_overrides = arena_allocate(arena, CompilerDriverFeatureOverride, feature_override_capacity);
    u64 feature_override_count = 0;
    String8 architecture_option = {0};
    bool options_ended = false;
    bool action_seen = false;
    for (u64 argument_index = 0; argument_index < arguments.length && invocation.error == COMPILER_DRIVER_ERROR_NONE; argument_index += 1)
    {
        String8 argument = arguments.pointer[argument_index];
        if (options_ended || !argument.length || argument.pointer[0] != '-')
        {
            invocation.input_paths[invocation.input_count++] = argument;
            continue;
        }
        if (string_equal(argument, S8("--")))
        {
            options_ended = true;
            continue;
        }
        if (string_equal(argument, S8("-E")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_PREPROCESS)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_PREPROCESS;
            continue;
        }
        if (string_equal(argument, S8("-S")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_ASSEMBLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_ASSEMBLY;
            continue;
        }
        if (string_equal(argument, S8("-c")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_OBJECT)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_OBJECT;
            continue;
        }
        if (string_equal(argument, S8("-fsyntax-only")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                break;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_SYNTAX_ONLY;
            continue;
        }
        if (string_equal(argument, S8("-emit-llvm")))
        {
            invocation.emit_llvm_bitcode = true;
            continue;
        }
        if (string_equal(argument, S8("-v")) || string_equal(argument, S8("--verbose")))
        {
            invocation.verbose = true;
            continue;
        }
        if (string_equal(argument, S8("-g")) || string_equal(argument, S8("-g0")))
        {
            invocation.debug_info = !string_equal(argument, S8("-g0"));
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-g")))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported debug option: {S8}"), argument);
            break;
        }
        if (string_equal(argument, S8("-nostdinc")))
        {
            invocation.no_standard_includes = true;
            continue;
        }
        if (string_equal(argument, S8("--save-temps")) || string_equal(argument, S8("-save-temps")))
        {
            invocation.save_gpu_temporaries = true;
            continue;
        }
        if (string_equal(argument, S8("-o")) || string_equal(argument, S8("-e")) || string_equal(argument, S8("--entry")) ||
            string_equal(argument, S8("-I")) || string_equal(argument, S8("-isystem")) ||
            string_equal(argument, S8("-D")) || string_equal(argument, S8("-U")) || string_equal(argument, S8("-L")) || string_equal(argument, S8("-l")) ||
            string_equal(argument, S8("-F")) || string_equal(argument, S8("-framework")) || string_equal(argument, S8("-x")) ||
            string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")) || string_equal(argument, S8("-march")) ||
            string_equal(argument, S8("-mcpu")) || string_equal(argument, S8("-mattr")) || string_equal(argument, S8("-masm")) ||
            string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")) ||
            string_equal(argument, S8("-Xlinker")) ||
            string_equal(argument, S8("--gpu-arch")) || string_equal(argument, S8("--gpu-stage")) || string_equal(argument, S8("--gpu-entry")) ||
            string_equal(argument, S8("--shader-model")) || string_equal(argument, S8("--metal-sdk")) || string_equal(argument, S8("--cuda-path")) ||
            string_equal(argument, S8("--rocm-path")) || string_equal(argument, S8("--gpu-clang")) || string_equal(argument, S8("--gpu-llc")) ||
            string_equal(argument, S8("--spirv-link")) || string_equal(argument, S8("--spirv-dis")) || string_equal(argument, S8("--xcrun")) ||
            string_equal(argument, S8("--dxc")) || string_equal(argument, S8("-Xgpu")))
        {
            if (argument_index + 1 >= arguments.length)
            {
                compiler_driver_argument_error(arena, &invocation, S8("missing argument after {S8}"), argument);
                break;
            }
            String8 value = arguments.pointer[++argument_index];
            if (string_equal(argument, S8("-o")))
            {
                invocation.output_path = value;
            }
            else if (string_equal(argument, S8("-e")) || string_equal(argument, S8("--entry")))
            {
                invocation.entry_symbol = value;
            }
            else if (string_equal(argument, S8("-I")))
            {
                invocation.include_paths[invocation.include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-isystem")))
            {
                invocation.system_include_paths[invocation.system_include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-D")))
            {
                invocation.definitions[invocation.definition_count++] = value;
            }
            else if (string_equal(argument, S8("-U")))
            {
                invocation.undefinitions[invocation.undefinition_count++] = value;
            }
            else if (string_equal(argument, S8("-L")))
            {
                invocation.library_paths[invocation.library_path_count++] = value;
            }
            else if (string_equal(argument, S8("-l")))
            {
                invocation.libraries[invocation.library_count++] = value;
            }
            else if (string_equal(argument, S8("-F")))
            {
                invocation.framework_paths[invocation.framework_path_count++] = value;
            }
            else if (string_equal(argument, S8("-framework")))
            {
                invocation.frameworks[invocation.framework_count++] = value;
            }
            else if (string_equal(argument, S8("-x")))
            {
                if (string_equal(value, S8("c")) || string_equal(value, S8("cpp-output")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_C;
                }
                else if (string_equal(value, S8("cl")) || string_equal(value, S8("opencl")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_OPENCL;
                }
                else if (string_equal(value, S8("cuda")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_CUDA;
                }
                else if (string_equal(value, S8("hip")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_HIP;
                }
                else if (string_equal(value, S8("metal")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_METAL;
                }
                else if (string_equal(value, S8("hlsl")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_HLSL;
                }
                else if (string_equal(value, S8("ir")) || string_equal(value, S8("llvm-ir")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_LLVM_IR;
                }
                else if (string_equal(value, S8("spirv")) || string_equal(value, S8("spirv-binary")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_SPIRV_BINARY;
                }
                else if (string_equal(value, S8("air")) || string_equal(value, S8("metal-air")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_METAL_AIR;
                }
                else if (string_equal(value, S8("assembler")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_ASSEMBLY;
                }
                else if (string_equal(value, S8("none")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC;
                }
                else
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported language: {S8}"), value);
                    break;
                }
            }
            else if (string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")))
            {
                if (!compiler_driver_set_target(arena, &invocation, value))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-march")) || string_equal(argument, S8("-mcpu")))
            {
                architecture_option = value;
            }
            else if (string_equal(argument, S8("-mattr")))
            {
                if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity,
                                                             &feature_override_count))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-masm")))
            {
                if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
                {
                    break;
                }
            }
            else if (string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")))
            {
                invocation.sysroot = value;
            }
            else if (string_equal(argument, S8("--gpu-arch")))
            {
                invocation.gpu_architecture = value;
            }
            else if (string_equal(argument, S8("--gpu-stage")))
            {
                invocation.gpu_stage = value;
            }
            else if (string_equal(argument, S8("--gpu-entry")))
            {
                invocation.gpu_entry_point = value;
            }
            else if (string_equal(argument, S8("--shader-model")))
            {
                invocation.gpu_shader_model = value;
            }
            else if (string_equal(argument, S8("--metal-sdk")))
            {
                invocation.metal_sdk = value;
            }
            else if (string_equal(argument, S8("--cuda-path")))
            {
                invocation.cuda_path = value;
            }
            else if (string_equal(argument, S8("--rocm-path")))
            {
                invocation.rocm_path = value;
            }
            else if (string_equal(argument, S8("--gpu-clang")))
            {
                invocation.gpu_tools.clang_path = value;
            }
            else if (string_equal(argument, S8("--gpu-llc")))
            {
                invocation.gpu_tools.llc_path = value;
            }
            else if (string_equal(argument, S8("--spirv-link")))
            {
                invocation.gpu_tools.spirv_link_path = value;
            }
            else if (string_equal(argument, S8("--spirv-dis")))
            {
                invocation.gpu_tools.spirv_dis_path = value;
            }
            else if (string_equal(argument, S8("--xcrun")))
            {
                invocation.gpu_tools.xcrun_path = value;
            }
            else if (string_equal(argument, S8("--dxc")))
            {
                invocation.gpu_tools.dxc_path = value;
            }
            else if (string_equal(argument, S8("-Xgpu")))
            {
                invocation.gpu_arguments[invocation.gpu_argument_count++] = value;
            }
            else
            {
                invocation.linker_arguments[invocation.linker_argument_count++] = value;
            }
            continue;
        }
        String8 value = compiler_driver_option_value(argument, S8("--target="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-target="));
        }
        if (value.length)
        {
            if (!compiler_driver_set_target(arena, &invocation, value))
            {
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--sysroot="));
        if (value.length)
        {
            invocation.sysroot = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-arch="));
        if (value.length)
        {
            invocation.gpu_architecture = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-stage="));
        if (value.length)
        {
            invocation.gpu_stage = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-entry="));
        if (value.length)
        {
            invocation.gpu_entry_point = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--shader-model="));
        if (value.length)
        {
            invocation.gpu_shader_model = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--metal-sdk="));
        if (value.length)
        {
            invocation.metal_sdk = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--cuda-path="));
        if (value.length)
        {
            invocation.cuda_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--rocm-path="));
        if (value.length)
        {
            invocation.rocm_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-clang="));
        if (value.length)
        {
            invocation.gpu_tools.clang_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-llc="));
        if (value.length)
        {
            invocation.gpu_tools.llc_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--spirv-link="));
        if (value.length)
        {
            invocation.gpu_tools.spirv_link_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--spirv-dis="));
        if (value.length)
        {
            invocation.gpu_tools.spirv_dis_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--xcrun="));
        if (value.length)
        {
            invocation.gpu_tools.xcrun_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--dxc="));
        if (value.length)
        {
            invocation.gpu_tools.dxc_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--gpu-arg="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-Xgpu="));
        }
        if (value.length)
        {
            invocation.gpu_arguments[invocation.gpu_argument_count++] = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--entry="));
        if (value.length)
        {
            invocation.entry_symbol = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fsource-metrics="));
        if (value.length)
        {
            invocation.source_metrics_path = value;
            continue;
        }
        // Register allocation is independent of source-level optimization:
        // like LLVM, -O0 still uses the low-latency allocator. QUALITY stays
        // out of the optimization-level mapping because it does not yet beat
        // FAST on a measured corpus; callers can still name it explicitly.
        if (string_starts_with_sequence(argument, S8("-O")))
        {
            String8 level = string_slice(argument, 2, argument.length);
            if (!level.length || string_equal(level, S8("0")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST;
                invocation.optimization_level = 0;
                continue;
            }
            if (string_equal(level, S8("1")) || string_equal(level, S8("2")) || string_equal(level, S8("3")) || string_equal(level, S8("s")) ||
                string_equal(level, S8("z")) || string_equal(level, S8("fast")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST;
                invocation.optimization_level = string_equal(level, S8("1")) ? 1 : string_equal(level, S8("3")) ? 3 : 2;
                continue;
            }
            compiler_driver_argument_error(arena, &invocation, S8("unsupported optimization level: {S8}"), argument);
            break;
        }
        if (string_equal(argument, S8("-fno-register-allocator")))
        {
            invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_NONE;
            invocation.register_allocator_explicit = true;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fregister-allocator="));
        if (value.length)
        {
            invocation.register_allocator_explicit = true;
            if (string_equal(value, S8("none")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_NONE;
            }
            else if (string_equal(value, S8("mir-stack")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK;
            }
            else if (string_equal(value, S8("fast")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST;
            }
            else if (string_equal(value, S8("quality")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_QUALITY;
            }
            else
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported register allocator: {S8}"), value);
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-march="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-mcpu="));
        }
        if (value.length)
        {
            architecture_option = value;
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-mattr=")))
        {
            value = string_slice(argument, S8("-mattr=").length, argument.length);
            if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity, &feature_override_count))
            {
                break;
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-masm=")))
        {
            value = string_slice(argument, S8("-masm=").length, argument.length);
            if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
            {
                break;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-std="));
        if (value.length)
        {
            if (!compiler_driver_set_dialect(&invocation, value))
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported C dialect: {S8}"), value);
                break;
            }
            invocation.c_dialect_explicit = true;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-Wl,"));
        if (value.length)
        {
            invocation.linker_arguments[invocation.linker_argument_count++] = value;
            continue;
        }
        String8 prefix = {
            .pointer = argument.pointer,
            .length = BUSTER_MIN(argument.length, (u64)2),
        };
        value = argument.length > 2 ?
            (String8){
                .pointer = argument.pointer + 2,
                .length = argument.length - 2,
            } :
            (String8){0};
        if (string_equal(prefix, S8("-I")) && value.length)
        {
            invocation.include_paths[invocation.include_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-D")) && value.length)
        {
            invocation.definitions[invocation.definition_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-U")) && value.length)
        {
            invocation.undefinitions[invocation.undefinition_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-L")) && value.length)
        {
            invocation.library_paths[invocation.library_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-l")) && value.length)
        {
            invocation.libraries[invocation.library_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-F")) && value.length)
        {
            invocation.framework_paths[invocation.framework_path_count++] = value;
            continue;
        }
        bool optimization_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'O';
        bool debug_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'g';
        bool warning_option =
            argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'W' && !string_starts_with_sequence(argument, S8("-Wl,"));
        // The code model, which is a fact about the emitted references
        // rather than a flag to absorb. It decides the thread-local model,
        // because an object that may end up in a shared library cannot fold
        // an offset from the thread pointer, and it decides how every other
        // reference to an interposable symbol is spelled: through the GOT for
        // an address and the PLT for a direct call. -fno-pic asks for the
        // rip-relative forms back. -fPIE/-fpie stay accepted and inert on
        // purpose -- a position-independent executable's own thread-local
        // block is still the initial one, its own definitions are not
        // interposable, its references to another image's data are what the
        // linker's copy relocation is for, and every reference this compiler
        // emits is already rip-relative, so that model asks for no code this
        // one does not already produce.
        if (string_equal(argument, S8("-fPIC")) || string_equal(argument, S8("-fpic")))
        {
            invocation.position_independent = true;
            continue;
        }
        if (string_equal(argument, S8("-fno-pic")))
        {
            invocation.position_independent = false;
            continue;
        }
        // clang's spelling of the flag configure scripts pass the linker as
        // `-Xlinker -export-dynamic`; both routes land in linker_arguments
        // and the hosted ELF writer reads it there.
        if (string_equal(argument, S8("-rdynamic")))
        {
            invocation.linker_arguments[invocation.linker_argument_count++] = S8("-export-dynamic");
            continue;
        }
        bool compatible_codegen_option =
            string_equal(argument, S8("-pipe")) || string_equal(argument, S8("-pthread")) ||
            string_equal(argument, S8("-fPIE")) || string_equal(argument, S8("-fpie")) ||
            string_equal(argument, S8("-fno-pie")) || string_equal(argument, S8("-fno-builtin")) ||
            string_equal(argument, S8("-fwrapv")) || string_equal(argument, S8("-fno-strict-aliasing")) || string_equal(argument, S8("-funsigned-char")) ||
            string_equal(argument, S8("-fsigned-char")) || string_equal(argument, S8("-fcommon")) || string_equal(argument, S8("-fno-common")) ||
            // Buster emits no stack-protector prologue, so the disabling
            // spelling is already what it does. A libc asks for it on the
            // translation units that run before thread-local storage exists,
            // where the canary's own load would fault; accepting the flag lets
            // one flag set drive this compiler and the reference one.
            string_equal(argument, S8("-fno-stack-protector"));
        if (optimization_option || debug_option || warning_option || compatible_codegen_option)
        {
            continue;
        }
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), argument);
        break;
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (invocation.has_gpu_target)
        {
            compiler_driver_reject_gpu_native_options(arena, &invocation, feature_override_count);
            if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
            {
                compiler_driver_resolve_gpu_target(arena, &invocation, architecture_option);
            }
            if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
            {
                compiler_driver_check_gpu_inputs(arena, &invocation);
            }
        }
        else
        {
            compiler_driver_resolve_native_target(arena, &invocation, architecture_option, feature_overrides, feature_override_count);
        }
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.emit_llvm_bitcode &&
        (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
         invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY))
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("-emit-llvm emits binary bitcode and cannot be combined with -E, -S, or -fsyntax-only");
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE && !invocation.no_standard_includes && !invocation.has_gpu_target &&
        invocation.target.os != OPERATING_SYSTEM_UEFI)
    {
        compiler_driver_append_system_includes(arena, &invocation);
    }
    if (invocation.error == COMPILER_DRIVER_ERROR_NONE)
    {
        if (!invocation.has_gpu_target && invocation.target.os == OPERATING_SYSTEM_UEFI &&
            invocation.target.cpu_arch != CPU_ARCH_X86_64 && invocation.target.cpu_arch != CPU_ARCH_AARCH64)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("UEFI output is supported only for x86_64 and aarch64 targets");
        }
        else if (!invocation.has_gpu_target && invocation.target.os == OPERATING_SYSTEM_UEFI && invocation.linker_argument_count)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("raw linker arguments are not supported for UEFI targets");
        }
        else if (!invocation.has_gpu_target && invocation.framework_count && invocation.target.os != OPERATING_SYSTEM_MACOS &&
                 invocation.target.os != OPERATING_SYSTEM_IOS)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("-framework is only supported for Apple targets");
        }
        else if (!invocation.input_count)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("no input files");
        }
    }
    return invocation;

}

BUSTER_GLOBAL_LOCAL bool compiler_driver_c_input(CompilerDriverInvocation invocation, String8 path)
{
    bool result;
    if (invocation.language == COMPILER_DRIVER_LANGUAGE_C)
    {
        result = true;
    }
    else if (invocation.language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 2)
    {
        result = false;
    }
    else
    {
        result = path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'c' || path.pointer[path.length - 1] == 'i');
    }

    return result;
}


// A `.s` input, or any input under `-x assembler`.
BUSTER_GLOBAL_LOCAL bool compiler_driver_assembly_input(CompilerDriverInvocation invocation, String8 path)
{
    if (invocation.language == COMPILER_DRIVER_LANGUAGE_ASSEMBLY)
    {
        return true;
    }
    if (invocation.language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 2)
    {
        return false;
    }
    return path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 's';
}

// GNU's `.S` runs the C preprocessor over assembly text before assembling
// it; compiler_driver_execute_preprocessed_assembly_single is that route.
BUSTER_GLOBAL_LOCAL bool compiler_driver_preprocessed_assembly_input(String8 path)
{
    return path.length >= 2 && path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 'S';
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_object_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 'o')
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'o' || path.pointer[path.length - 3] == 'O') &&
           (path.pointer[path.length - 2] == 'b' || path.pointer[path.length - 2] == 'B') &&
           (path.pointer[path.length - 1] == 'j' || path.pointer[path.length - 1] == 'J');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'a' || path.pointer[path.length - 1] == 'A'))
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'l' || path.pointer[path.length - 3] == 'L') &&
           (path.pointer[path.length - 2] == 'i' || path.pointer[path.length - 2] == 'I') &&
           (path.pointer[path.length - 1] == 'b' || path.pointer[path.length - 1] == 'B');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_member_needed(ObjectFile* member, ObjectFile* selected, u32 selected_count)
{
    for (u32 member_symbol_index = 0; member_symbol_index < member->symbol_count; member_symbol_index += 1)
    {
        ObjectSymbol* member_symbol = &member->symbols[member_symbol_index];
        if (!member_symbol->global || member_symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        bool unresolved = false;
        bool defined = false;
        for (u32 object_index = 0; object_index < selected_count; object_index += 1)
        {
            ObjectFile* object = &selected[object_index];
            for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
            {
                ObjectSymbol* symbol = &object->symbols[symbol_index];
                if (!symbol->global || !string_equal(symbol->name, member_symbol->name))
                {
                    continue;
                }
                if (symbol->section == OBJECT_SECTION_UNDEFINED)
                {
                    unresolved = true;
                }
                else
                {
                    defined = true;
                }
            }
        }
        if (unresolved && !defined)
        {
            return true;
        }
    }
    return false;
}

typedef struct CompilerDriverDynamicLibraries CompilerDriverDynamicLibraries;
struct CompilerDriverDynamicLibraries
{
    NativeDynamicLibrary* pointer;
    NativeDynamicLibrary runtime;
    FileMapRead* export_maps;
    u32 count;
    u32 export_map_count;
    // The first `-l` request the export scan found no usable file for, in the
    // caller's own spelling.  A hosted ELF link must refuse such a request the
    // way ld does ("cannot find -lX"): recording a DT_NEEDED for a library
    // that exists nowhere on the search path defers the failure to the
    // loader, and a configure script reads the successful link as the
    // library existing.  Empty when every requested library was found.
    String8 missing_request;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u16(ByteSlice bytes, u64 offset, u16* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u32(ByteSlice bytes, u64 offset, u32* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u64(ByteSlice bytes, u64 offset, u64* value)
{
    bool result;
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        result = false;
    }
    else
    {
        memcpy(value, bytes.pointer + offset, sizeof(*value));
        result = true;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_pe_rva_offset(ByteSlice bytes, u64 section_table, u16 section_count, u32 rva, u64* offset_out)
{
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        u32 virtual_size = 0;
        u32 virtual_address = 0;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        if (!compiler_driver_read_u32(bytes, section + 8, &virtual_size) || !compiler_driver_read_u32(bytes, section + 12, &virtual_address) ||
            !compiler_driver_read_u32(bytes, section + 16, &raw_size) || !compiler_driver_read_u32(bytes, section + 20, &raw_offset))
        {
            return false;
        }
        u64 span = BUSTER_MAX(virtual_size, raw_size);
        if (rva < virtual_address || (u64)rva - virtual_address >= span)
        {
            continue;
        }
        u64 offset = raw_offset + ((u64)rva - virtual_address);
        if (offset >= bytes.length)
        {
            return false;
        }
        *offset_out = offset;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_pe_library_exports(Arena* arena, CompilerDriverInvocation invocation, NativeDynamicLibrary* library,
                                                            FileMapRead* export_map)
{
    String8 path = {0};
    FileMapRead file = {0};
    ByteSlice bytes = {0};
    *export_map = (FileMapRead){0};

    for (u32 path_index = 0; path_index < invocation.library_path_count && !bytes.pointer; path_index += 1)
    {
        if (file.bytes.pointer)
        {
            file_map_unmap(file);
        }
        path = string_format_z(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
    if (!bytes.pointer && invocation.sysroot.length)
    {
        path = string_format_z(arena, S8("{S8}/Windows/System32/{S8}"), invocation.sysroot, library->name);
        file_map_unmap(file);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
#if BUSTER_WINDOWS
    if (!bytes.pointer && invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        String8 system_root = os_get_environment_variable(S8("SystemRoot"));
        if (system_root.length)
        {
            path = string_format_z(arena, S8("{S8}/System32/{S8}"), system_root, library->name);
            file_map_unmap(file);
            file = file_map_read(arena, path, (FileReadOptions){0});
            bytes = file.bytes;
        }
    }
#endif
    if (!bytes.pointer)
    {
        file_map_unmap(file);
        file = file_map_read(arena, string_duplicate_arena(arena, library->name, true), (FileReadOptions){0});
        bytes = file.bytes;
    }
    u32 pe_offset = 0;
    u16 section_count = 0;
    u16 optional_size = 0;
    if (!bytes.pointer || bytes.length < 0x40 || bytes.pointer[0] != 'M' || bytes.pointer[1] != 'Z' || !compiler_driver_read_u32(bytes, 0x3c, &pe_offset) ||
        pe_offset > bytes.length || bytes.length - pe_offset < 24 || memcmp(bytes.pointer + pe_offset, "PE\0\0", 4) != 0 ||
        !compiler_driver_read_u16(bytes, pe_offset + 6, &section_count) || !compiler_driver_read_u16(bytes, pe_offset + 20, &optional_size))
    {
        file_map_unmap(file);
        return;
    }
    u64 optional = pe_offset + 24;
    u16 magic = 0;
    if (!compiler_driver_read_u16(bytes, optional, &magic))
    {
        file_map_unmap(file);
        return;
    }
    u64 directory = optional + (magic == 0x20b ? 112 : 96);
    u32 export_rva = 0;
    if ((magic != 0x20b && magic != 0x10b) || directory + 8 > optional + optional_size || !compiler_driver_read_u32(bytes, directory, &export_rva))
    {
        file_map_unmap(file);
        return;
    }
    library->exports_known = true;
    if (!export_rva)
    {
        *export_map = file;
        return;
    }
    u64 section_table = optional + optional_size;
    u64 export_offset = 0;
    u32 name_count = 0;
    u32 names_rva = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, export_rva, &export_offset) ||
        !compiler_driver_read_u32(bytes, export_offset + 24, &name_count) || !compiler_driver_read_u32(bytes, export_offset + 32, &names_rva) ||
        name_count > (bytes.length / sizeof(u32)))
    {
        file_map_unmap(file);
        return;
    }
    u64 names_offset = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, names_rva, &names_offset) || names_offset > bytes.length ||
        (u64)name_count * sizeof(u32) > bytes.length - names_offset)
    {
        file_map_unmap(file);
        return;
    }
    library->exported_symbols = arena_allocate(arena, String8, name_count);
    for (u32 name_index = 0; name_index < name_count; name_index += 1)
    {
        u32 name_rva = 0;
        u64 name_offset = 0;
        if (!compiler_driver_read_u32(bytes, names_offset + (u64)name_index * sizeof(u32), &name_rva) ||
            !compiler_driver_pe_rva_offset(bytes, section_table, section_count, name_rva, &name_offset))
        {
            continue;
        }
        u64 length = 0;
        while (name_offset + length < bytes.length && bytes.pointer[name_offset + length])
        {
            length += 1;
        }
        if (name_offset + length >= bytes.length)
        {
            continue;
        }
        library->exported_symbols[library->exported_symbol_count++] = (String8){
            .pointer = (char8*)bytes.pointer + name_offset,
            .length = length,
        };
    }
    *export_map = file;
}

// What one ELF shared library defines, read from its own dynamic symbol table.
//
// Two things the referencing name alone does not carry.  The data objects, for
// copy relocations: the address tells the linker which exported names are one
// object, so the executable can define every one of them at the slot it
// reserves, and the size tells it how large that slot has to be.  Only data is
// collected there; imported functions go through the PLT.
//
// And the symbol version of every definition, functions included.  A reference
// GNU ld resolves records the version it bound to, so the image keeps that
// answer for its whole life; an unversioned reference to a name whose
// definitions are all `name@VER` has no default to bind to at all, which is
// what makes GNU ld refuse `sys_errlist`.
BUSTER_GLOBAL_LOCAL bool compiler_driver_elf_dynamic_symbols(Arena* arena, ByteSlice bytes, u16 machine, bool collect_data, NativeDynamicLibrary* library)
{
    enum
    {
        DRIVER_ELF_SECTION_HEADER_SIZE = 64,
        DRIVER_ELF_SYMBOL_SIZE = 24,
        DRIVER_ELF_SECTION_TYPE_DYNAMIC_SYMBOLS = 11,
        DRIVER_ELF_SECTION_TYPE_VERSION_DEFINITIONS = 0x6ffffffd,
        DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS = 0x6fffffff,
        DRIVER_ELF_VERSION_DEFINITION_SIZE = 20,
        DRIVER_ELF_VERSION_AUXILIARY_SIZE = 8,
        DRIVER_ELF_VERSION_HIDDEN = 0x8000,
        DRIVER_ELF_VERSION_INDEX_MAX = 0x7fff,
        DRIVER_ELF_TYPE_SHARED = 3,
        DRIVER_ELF_SYMBOL_TYPE_OBJECT = 1,
        DRIVER_ELF_SECTION_ABSOLUTE = 0xfff1,
    };
    bool result = false;
    u16 type = 0;
    u16 file_machine = 0;
    u64 section_table = 0;
    u16 section_entry_size = 0;
    u16 section_count = 0;
    bool header_valid = bytes.length >= DRIVER_ELF_SECTION_HEADER_SIZE && memcmp(bytes.pointer, "\x7f" "ELF", 4) == 0 && bytes.pointer[4] == 2 &&
                        bytes.pointer[5] == 1 && compiler_driver_read_u16(bytes, 16, &type) && compiler_driver_read_u16(bytes, 18, &file_machine) &&
                        compiler_driver_read_u64(bytes, 40, &section_table) && compiler_driver_read_u16(bytes, 58, &section_entry_size) &&
                        compiler_driver_read_u16(bytes, 60, &section_count) && type == DRIVER_ELF_TYPE_SHARED && file_machine == machine &&
                        section_entry_size == DRIVER_ELF_SECTION_HEADER_SIZE && section_count && section_table <= bytes.length &&
                        (u64)section_count * DRIVER_ELF_SECTION_HEADER_SIZE <= bytes.length - section_table;
    u64 symbol_offset = 0;
    u64 symbol_size = 0;
    u64 string_offset = 0;
    u64 string_size = 0;
    u64 version_symbol_offset = 0;
    u64 version_symbol_size = 0;
    u64 version_definition_offset = 0;
    u64 version_definition_size = 0;
    u32 version_definition_count = 0;
    for (u16 section_index = 0; header_valid && section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * DRIVER_ELF_SECTION_HEADER_SIZE;
        u32 section_type = 0;
        u32 string_section = 0;
        u32 definition_count = 0;
        u64 offset = 0;
        u64 size = 0;
        u64 entry_size = 0;
        u64 strings_offset = 0;
        u64 strings_size = 0;
        if (!compiler_driver_read_u32(bytes, section + 4, &section_type) || !compiler_driver_read_u64(bytes, section + 24, &offset) ||
            !compiler_driver_read_u64(bytes, section + 32, &size) || offset > bytes.length || size > bytes.length - offset)
        {
            continue;
        }
        if (section_type == DRIVER_ELF_SECTION_TYPE_DYNAMIC_SYMBOLS && !symbol_size)
        {
            if (compiler_driver_read_u32(bytes, section + 40, &string_section) && compiler_driver_read_u64(bytes, section + 56, &entry_size) &&
                entry_size == DRIVER_ELF_SYMBOL_SIZE && size && !(size % DRIVER_ELF_SYMBOL_SIZE) && string_section < section_count)
            {
                u64 strings = section_table + (u64)string_section * DRIVER_ELF_SECTION_HEADER_SIZE;
                if (compiler_driver_read_u64(bytes, strings + 24, &strings_offset) && compiler_driver_read_u64(bytes, strings + 32, &strings_size) &&
                    strings_offset <= bytes.length && strings_size <= bytes.length - strings_offset)
                {
                    symbol_offset = offset;
                    symbol_size = size;
                    string_offset = strings_offset;
                    string_size = strings_size;
                }
            }
        }
        else if (section_type == DRIVER_ELF_SECTION_TYPE_VERSION_SYMBOLS && !version_symbol_size)
        {
            version_symbol_offset = offset;
            version_symbol_size = size;
        }
        else if (section_type == DRIVER_ELF_SECTION_TYPE_VERSION_DEFINITIONS && !version_definition_size &&
                 compiler_driver_read_u32(bytes, section + 44, &definition_count) && definition_count)
        {
            version_definition_offset = offset;
            version_definition_size = size;
            version_definition_count = definition_count;
        }
    }
    // .gnu.version_d as an index-keyed table.  The definitions form a linked
    // list whose entries carry their own index, so the highest index decides
    // how large the table has to be; both walks are over the version count,
    // which is a few dozen entries even for glibc.  The names live in the
    // string table .dynsym already named: an ELF section table links both to
    // the one .dynstr.
    String8* version_names = 0;
    u32 version_name_count = 0;
    for (u32 pass = 0; pass < 2 && version_definition_count; pass += 1)
    {
        u64 cursor = version_definition_offset;
        u64 end = version_definition_offset + version_definition_size;
        u32 highest = 0;
        for (u32 definition = 0; definition < version_definition_count && cursor && cursor + DRIVER_ELF_VERSION_DEFINITION_SIZE <= end; definition += 1)
        {
            u16 index = 0;
            u16 flags = 0;
            u32 auxiliary = 0;
            u32 next = 0;
            if (!compiler_driver_read_u16(bytes, cursor + 2, &flags) || !compiler_driver_read_u16(bytes, cursor + 4, &index) ||
                !compiler_driver_read_u32(bytes, cursor + 12, &auxiliary) || !compiler_driver_read_u32(bytes, cursor + 16, &next))
            {
                break;
            }
            index = (u16)(index & DRIVER_ELF_VERSION_INDEX_MAX);
            // VER_FLG_BASE names the library itself rather than a version any
            // symbol is published under.
            bool base = (flags & 1) != 0;
            highest = !base && index > highest ? index : highest;
            u32 name = 0;
            u64 auxiliary_offset = cursor + auxiliary;
            if (pass && !base && index < version_name_count && auxiliary >= DRIVER_ELF_VERSION_DEFINITION_SIZE &&
                auxiliary_offset + DRIVER_ELF_VERSION_AUXILIARY_SIZE <= end && compiler_driver_read_u32(bytes, auxiliary_offset, &name) && name &&
                name < string_size)
            {
                u64 length = 0;
                while ((u64)name + length < string_size && bytes.pointer[string_offset + name + length])
                {
                    length += 1;
                }
                if (length && (u64)name + length < string_size)
                {
                    version_names[index] = (String8){.pointer = (char8*)bytes.pointer + string_offset + name, .length = length};
                }
            }
            cursor = next ? cursor + next : 0;
        }
        if (!pass)
        {
            version_name_count = highest + 1;
            version_names = arena_allocate(arena, String8, version_name_count);
            memset(version_names, 0, (u64)version_name_count * sizeof(*version_names));
        }
    }
    if (symbol_size)
    {
        u64 symbol_count = symbol_size / DRIVER_ELF_SYMBOL_SIZE;
        bool versioned = version_symbol_size / sizeof(u16) >= symbol_count;
        library->exported_data_symbols = collect_data ? arena_allocate(arena, NativeDynamicDataSymbol, symbol_count) : 0;
        library->versioned_symbols = arena_allocate(arena, NativeDynamicVersionedSymbol, symbol_count);
        for (u64 symbol_index = 0; symbol_index < symbol_count; symbol_index += 1)
        {
            u64 symbol = symbol_offset + symbol_index * DRIVER_ELF_SYMBOL_SIZE;
            u32 name = 0;
            u16 section = 0;
            u64 value = 0;
            u64 size = 0;
            if (!compiler_driver_read_u32(bytes, symbol, &name) || !compiler_driver_read_u16(bytes, symbol + 6, &section) ||
                !compiler_driver_read_u64(bytes, symbol + 8, &value) || !compiler_driver_read_u64(bytes, symbol + 16, &size))
            {
                continue;
            }
            u8 info = bytes.pointer[symbol + 4];
            u8 binding = (u8)(info >> 4);
            // Defined global or weak entries only: an undefined or local one
            // names nothing this executable could bind to.
            if ((binding != 1 && binding != 2) || !section || !name || name >= string_size)
            {
                continue;
            }
            u64 length = 0;
            while ((u64)name + length < string_size && bytes.pointer[string_offset + name + length])
            {
                length += 1;
            }
            if (!length || (u64)name + length >= string_size)
            {
                continue;
            }
            String8 spelling = {.pointer = (char8*)bytes.pointer + string_offset + name, .length = length};
            u16 version = 0;
            if (versioned)
            {
                compiler_driver_read_u16(bytes, version_symbol_offset + symbol_index * sizeof(u16), &version);
            }
            u32 version_index = version & DRIVER_ELF_VERSION_INDEX_MAX;
            library->versioned_symbols[library->versioned_symbol_count++] = (NativeDynamicVersionedSymbol){
                .name = spelling,
                // VER_NDX_LOCAL and VER_NDX_GLOBAL name no version, so a
                // reference to such a definition records none either.
                .version = version_index > 1 && version_index < version_name_count ? version_names[version_index] : (String8){0},
                .has_default = (version & DRIVER_ELF_VERSION_HIDDEN) == 0,
            };
            // STT_OBJECT is what a copy relocation applies to, and an absolute
            // or address-less entry names no storage to copy.
            if (collect_data && (info & 0xf) == DRIVER_ELF_SYMBOL_TYPE_OBJECT && section != DRIVER_ELF_SECTION_ABSOLUTE && value)
            {
                library->exported_data_symbols[library->exported_data_symbol_count++] = (NativeDynamicDataSymbol){
                    .name = spelling,
                    .address = value,
                    .size = size,
                };
            }
        }
        // Every name this library defines is now recorded, so the linker may
        // read the absence of a name from `versioned_symbols` as the library
        // not defining it -- which is what makes an undefined weak reference
        // to a name nothing exports resolve to zero rather than becoming an
        // import.  The ELF export list is `versioned_symbols` itself rather
        // than a second copy of the same names in `exported_symbols`, which
        // stays the PE side's.
        library->exports_known = true;
        result = true;
    }

    return result;
}

// The ELF counterpart of compiler_driver_pe_library_exports.  A shared library
// is looked up where the loader would look for it, and a file whose machine
// disagrees with the target is skipped rather than believed, so a cross link
// does not read the host's own libc.
BUSTER_GLOBAL_LOCAL void compiler_driver_elf_library_exports(Arena* arena, CompilerDriverInvocation invocation, bool collect_data,
                                                             NativeDynamicLibrary* library, FileMapRead* export_map)
{
    String8 multiarch = invocation.target.cpu_arch == CPU_ARCH_AARCH64 ? S8("aarch64-linux-gnu") : S8("x86_64-linux-gnu");
    u16 machine = invocation.target.cpu_arch == CPU_ARCH_AARCH64 ? 183 : 62;
    String8 roots[6] = {0};
    u32 root_count = 0;
    if (invocation.sysroot.length)
    {
        roots[root_count++] = string_format(arena, S8("{S8}/lib/{S8}"), invocation.sysroot, multiarch);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib/{S8}"), invocation.sysroot, multiarch);
        roots[root_count++] = string_format(arena, S8("{S8}/lib64"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib64"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/lib"), invocation.sysroot);
        roots[root_count++] = string_format(arena, S8("{S8}/usr/lib"), invocation.sysroot);
    }
    else
    {
        roots[root_count++] = string_format(arena, S8("/lib/{S8}"), multiarch);
        roots[root_count++] = string_format(arena, S8("/usr/lib/{S8}"), multiarch);
        roots[root_count++] = S8("/lib64");
        roots[root_count++] = S8("/usr/lib64");
        roots[root_count++] = S8("/lib");
        roots[root_count++] = S8("/usr/lib");
    }
    *export_map = (FileMapRead){0};
    bool found = false;
    u32 candidate_count = invocation.library_path_count + root_count + 1;
    for (u32 path_index = 0; !found && path_index < candidate_count; path_index += 1)
    {
        // Every candidate is zero-terminated: os_file_open takes the path as a
        // C string, and the bare library name is one this driver built with a
        // length and no terminator of its own.
        String8 path = path_index < invocation.library_path_count
                           ? string_format_z(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name)
                       : path_index < invocation.library_path_count + root_count
                           ? string_format_z(arena, S8("{S8}/{S8}"), roots[path_index - invocation.library_path_count], library->name)
                           : string_duplicate_arena(arena, library->name, true);
        FileMapRead file = file_map_read(arena, path, (FileReadOptions){0});
        found = file.bytes.pointer && compiler_driver_elf_dynamic_symbols(arena, file.bytes, machine, collect_data, library);
        if (found)
        {
            *export_map = file;
        }
        else
        {
            library->exports_known = false;
            library->exported_data_symbols = 0;
            library->exported_data_symbol_count = 0;
            library->versioned_symbols = 0;
            library->versioned_symbol_count = 0;
            file_map_unmap(file);
        }
    }
}

BUSTER_GLOBAL_LOCAL void compiler_driver_dynamic_libraries_release(CompilerDriverDynamicLibraries* libraries)
{
    for (u32 index = 0; index < libraries->export_map_count; index += 1)
    {
        file_map_unmap(libraries->export_maps[index]);
    }
}

// Whether this link has any undefined data symbol at all.  Only such a link
// needs a shared library's symbol table read, and that read is the one part of
// building the dynamic library list that touches the file system.
BUSTER_GLOBAL_LOCAL bool compiler_driver_object_imports_data(ObjectFile* object)
{
    bool result = false;
    for (u32 index = 0; !result && index < object->symbol_count; index += 1)
    {
        ObjectSymbol* symbol = object->symbols + index;
        result = symbol->global && symbol->section == OBJECT_SECTION_UNDEFINED && symbol->kind == OBJECT_SYMBOL_DATA;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation, bool* static_libraries,
                                                                                    bool imports_data)
{
    CompilerDriverDynamicLibraries result = {0};
    static String8 const windows_system_libraries[] = {
        S8_INITIALIZER("kernel32.dll"),
        S8_INITIALIZER("user32.dll"),
        S8_INITIALIZER("gdi32.dll"),
        S8_INITIALIZER("ws2_32.dll"),
        S8_INITIALIZER("dwmapi.dll"),
        S8_INITIALIZER("shell32.dll"),
        S8_INITIALIZER("vcruntime140.dll"),
    };
    u32 default_library_count = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? BUSTER_ARRAY_LENGTH(windows_system_libraries) : 0;
    NativeDynamicLibrary* libraries =
        arena_allocate(arena, NativeDynamicLibrary, invocation.library_count + invocation.framework_count + default_library_count);
    // The `-l` spelling that produced each entry, kept beside the mapped file
    // name so a library the search below never finds is reported as the
    // request the caller wrote rather than as the soname it was mapped to.
    // Entries the caller did not request -- the Windows defaults and the
    // Apple frameworks -- keep an empty request and are never reported.
    String8* requests = arena_allocate(arena, String8, invocation.library_count + invocation.framework_count + default_library_count);
    memset(requests, 0, sizeof(*requests) * (invocation.library_count + invocation.framework_count + default_library_count));
    u32 count = 0;
    for (u32 index = 0; index < default_library_count; index += 1)
    {
        libraries[count++] = (NativeDynamicLibrary){
            .name = windows_system_libraries[index],
        };
    }
    for (u32 index = 0; index < invocation.library_count; index += 1)
    {
        if (static_libraries && static_libraries[index])
        {
            continue;
        }
        String8 requested = invocation.libraries[index];
        if (!requested.length || string_equal(requested, S8("c")))
        {
            continue;
        }
        String8 name = {0};
        if (requested.pointer[0] == ':')
        {
            name = (String8){
                .pointer = requested.pointer + 1,
                .length = requested.length - 1,
            };
        }
        else if (invocation.target.os == OPERATING_SYSTEM_ANDROID)
        {
            name = string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_LINUX)
        {
            name = string_equal(requested, S8("m"))         ? S8("libm.so.6")
                   : string_equal(requested, S8("pthread")) ? S8("libpthread.so.0")
                   : string_equal(requested, S8("dl"))      ? S8("libdl.so.2")
                   : string_equal(requested, S8("rt"))      ? S8("librt.so.1")
                                                            : string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
        {
            if (string_equal(requested, S8("m")))
            {
                continue;
            }
            name = string_format(arena, S8("/usr/lib/lib{S8}.dylib"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
        {
            bool has_dll_suffix = requested.length >= 4 && string_equal(
                                                               (String8){
                                                                   .pointer = requested.pointer + requested.length - 4,
                                                                   .length = 4,
                                                               },
                                                               S8(".dll"));
            name = has_dll_suffix ? requested : string_format(arena, S8("{S8}.dll"), requested);
        }
        else
        {
            name = requested;
        }
        if (!name.length)
        {
            continue;
        }
        bool duplicate = false;
        for (u32 previous = 0; previous < count; previous += 1)
        {
            duplicate |= string_equal(libraries[previous].name, name);
        }
        if (!duplicate)
        {
            requests[count] = requested;
            libraries[count++] = (NativeDynamicLibrary){
                .name = name,
            };
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
    {
        for (u32 index = 0; index < invocation.framework_count; index += 1)
        {
            String8 framework = invocation.frameworks[index];
            if (!framework.length)
            {
                continue;
            }
            String8 root = invocation.framework_path_count ? invocation.framework_paths[0] : S8("/System/Library/Frameworks");
            String8 name = string_format(arena, S8("{S8}/{S8}.framework/{S8}"), root, framework, framework);
            bool duplicate = false;
            for (u32 previous = 0; previous < count; previous += 1)
            {
                duplicate |= string_equal(libraries[previous].name, name);
            }
            if (!duplicate)
            {
                libraries[count++] = (NativeDynamicLibrary){
                    .name = name,
                };
            }
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        result.export_maps = arena_allocate(arena, FileMapRead, count + 1);
        result.runtime.name = S8("ucrtbase.dll");
        FileMapRead* export_map = result.export_maps + result.export_map_count;
        compiler_driver_pe_library_exports(arena, invocation, &result.runtime, export_map);
        result.export_map_count += export_map->bytes.pointer != 0;
        for (u32 index = 0; index < count; index += 1)
        {
            export_map = result.export_maps + result.export_map_count;
            compiler_driver_pe_library_exports(arena, invocation, &libraries[index], export_map);
            result.export_map_count += export_map->bytes.pointer != 0;
        }
    }
    else if (invocation.target.os == OPERATING_SYSTEM_LINUX)
    {
        // libc.so.6 is the library the ELF writers name themselves, so it is
        // read as the runtime rather than as one of the requested ones.
        //
        // Every hosted ELF link reads these, not only one that imports data:
        // symbol versions apply to functions too, and a reference that binds
        // to a version has to record it.  The data objects -- the half that
        // needs an address and a size -- stay behind imports_data, since a
        // link with no undefined data symbol has nothing to copy.  Reading
        // libc.so.6 where nothing did before costs about 0,65 M instructions
        // on this host, a tenth of a percent of the smallest hosted compile.
        result.export_maps = arena_allocate(arena, FileMapRead, count + 1);
        result.runtime.name = S8("libc.so.6");
        FileMapRead* export_map = result.export_maps + result.export_map_count;
        compiler_driver_elf_library_exports(arena, invocation, imports_data, &result.runtime, export_map);
        result.export_map_count += export_map->bytes.pointer != 0;
        for (u32 index = 0; index < count; index += 1)
        {
            export_map = result.export_maps + result.export_map_count;
            compiler_driver_elf_library_exports(arena, invocation, imports_data, &libraries[index], export_map);
            result.export_map_count += export_map->bytes.pointer != 0;
            // The scan walked every search directory the loader would, so a
            // library it did not find is one the produced executable could
            // never load -- and one the archive search above did not satisfy
            // statically either, or the entry would not be here.  Record the
            // first such request for the caller to refuse the link with.
            if (!libraries[index].exports_known && requests[index].length && !result.missing_request.length)
            {
                result.missing_request = requests[index];
            }
        }
    }
    result.pointer = libraries;
    result.count = count;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_target_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation,
                                                                                              bool* static_libraries, ObjectFile* linked)
{
    CompilerDriverDynamicLibraries result;
    if (invocation.target.os == OPERATING_SYSTEM_UEFI)
    {
        result = (CompilerDriverDynamicLibraries){0};
    }
    else
    {
        result = compiler_driver_dynamic_libraries(arena, invocation, static_libraries, compiler_driver_object_imports_data(linked));
    }

    return result;
}

// Splits a `-D` operand at its first `=`. The `=` decides the value, not the
// text after it: `-DNAME=` leaves an empty replacement list, and only the form
// with no `=` at all takes the `1` default -- the split clang and GCC make.
BUSTER_GLOBAL_LOCAL CPreprocessorDefinition compiler_driver_c_definition(String8 definition)
{
    for (u64 index = 0; index < definition.length; index += 1)
    {
        if (definition.pointer[index] == '=')
        {
            return (CPreprocessorDefinition){
                .name =
                    {
                        .pointer = definition.pointer,
                        .length = index,
                    },
                .value =
                    {
                        .pointer = definition.pointer + index + 1,
                        .length = definition.length - index - 1,
                    },
            };
        }
    }
    return (CPreprocessorDefinition){
        .name = definition,
        .value = S8("1"),
    };
}

BUSTER_GLOBAL_LOCAL ObjectArchive compiler_driver_library_archive(Arena* arena, CompilerDriverInvocation invocation, String8 requested, bool* found,
                                                                  String8* path_out)
{
    ObjectArchive result = {0};
    bool exact = requested.length && requested.pointer[0] == ':';
    String8 exact_name = exact ?
        (String8){
            .pointer = requested.pointer + 1,
            .length = requested.length - 1,
        } :
        (String8){0};
    bool exact_archive = exact && compiler_driver_archive_input(exact_name);
    for (u32 path_index = 0; path_index < invocation.library_path_count; path_index += 1)
    {
        String8 root = invocation.library_paths[path_index];
        if (!exact_archive && invocation.target.os != OPERATING_SYSTEM_UEFI)
        {
            String8 shared_name = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.dll"), requested)
                                  : invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS
                                      ? string_format(arena, S8("lib{S8}.dylib"), requested)
                                      : string_format(arena, S8("lib{S8}.so"), requested);
            String8 shared_path = string_format_z(arena, S8("{S8}/{S8}"), root, shared_name);
            FileMapRead shared_map = file_map_read(arena, shared_path, (FileReadOptions){0});
            ByteSlice shared = shared_map.bytes;
            if (shared.pointer)
            {
                file_map_unmap(shared_map);
                return result;
            }
            file_map_unmap(shared_map);
        }
        String8 archive_name = exact_archive                                      ? exact_name
                               : invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.lib"), requested)
                                                                                  : string_format(arena, S8("lib{S8}.a"), requested);
        String8 archive_path = string_format_z(arena, S8("{S8}/{S8}"), root, archive_name);
        FileMapRead archive_map = file_map_read(arena, archive_path, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (!archive_bytes.pointer)
        {
            file_map_unmap(archive_map);
            continue;
        }
        *found = true;
        *path_out = archive_path;
        ObjectArchive archive = object_archive_read(arena, archive_bytes, invocation.target);
        file_map_unmap(archive_map);
        return archive;
    }
    if (exact_archive)
    {
        FileMapRead archive_map = file_map_read(arena, exact_name, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (archive_bytes.pointer)
        {
            *found = true;
            *path_out = exact_name;
            ObjectArchive archive = object_archive_read(arena, archive_bytes, invocation.target);
            file_map_unmap(archive_map);
            return archive;
        }
        file_map_unmap(archive_map);
    }
    return result;
}

// The -E text is read by other programs, not only by people: autoconf's
// established idiom preprocesses a file of literal lines and greps the output
// for one of them -- CPython's Misc/platform_triplet.c is
// `grep '^PLATFORM_TRIPLET='` -- so tokens that shared a source line must
// share an output line, and tokens that touched in the source must touch in
// the output.  A printer that space-joined the whole stream onto one line
// made that grep read the triplet as empty.  Both facts are recovered from
// the source map: a line gap becomes a newline (capped, since without line
// markers a skipped conditional's size is not information), and adjacency is
// the previous token's column plus its width reaching the next token's
// column.  Tokens a macro expansion synthesized share the use site's
// location, where the column arithmetic does not hold; they keep the single
// space, which is also what a hand-built result with no recovery map
// degrades to for every token.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_preprocess_text(Arena* arena, CPreprocessResult preprocess)
{
    enum { compiler_driver_preprocess_line_gap_cap = 8 };
    u64 capacity = 2;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        if (preprocess.tokens[index].kind != C_TOKEN_END_OF_FILE)
        {
            capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[index]) + compiler_driver_preprocess_line_gap_cap + 1;
        }
    }
    char8* text = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    u32 previous_line = 1;
    u32 previous_file = 0;
    u32 previous_end_column = 0;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            break;
        }
        CSourceLocation location = c_preprocess_token_location(&preprocess, token);
        if (length)
        {
            if (location.file != previous_file || location.line < previous_line)
            {
                text[length++] = '\n';
            }
            else if (location.line > previous_line)
            {
                u32 gap = location.line - previous_line;
                gap = gap > compiler_driver_preprocess_line_gap_cap ? compiler_driver_preprocess_line_gap_cap : gap;
                for (u32 newline = 0; newline < gap; newline += 1)
                {
                    text[length++] = '\n';
                }
            }
            else if (location.column != previous_end_column)
            {
                text[length++] = ' ';
            }
        }
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        memcpy(text + length, spelling.pointer, spelling.length);
        length += spelling.length;
        previous_line = location.line;
        previous_file = location.file;
        previous_end_column = location.column + (u32)spelling.length;
    }
    text[length++] = '\n';
    text[length] = 0;
    return (String8){
        .pointer = text,
        .length = length,
    };
}

typedef struct CompilerDriverWarningChunk CompilerDriverWarningChunk;
struct CompilerDriverWarningChunk
{
    CompilerDriverWarningChunk* next;
    String8 text;
};

typedef struct CompilerDriverWarningCollector CompilerDriverWarningCollector;
struct CompilerDriverWarningCollector
{
    Arena* arena;
    CompilerDriverWarningChunk* first;
    CompilerDriverWarningChunk* last;
    u64 length;
};

BUSTER_GLOBAL_LOCAL void compiler_driver_warning_append_text(CompilerDriverWarningCollector* collector, String8 text)
{
    if (!collector || !collector->arena || !text.length)
    {
        return;
    }
    CompilerDriverWarningChunk* chunk = arena_allocate(collector->arena, CompilerDriverWarningChunk, 1);
    *chunk = (CompilerDriverWarningChunk){
        .text = text,
    };
    if (collector->last)
    {
        collector->last->next = chunk;
    }
    else
    {
        collector->first = chunk;
    }
    collector->last = chunk;
    collector->length += text.length;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_append_warning(CompilerDriverWarningCollector* collector, String8 path, CDiagnostic diagnostic)
{
    if (!collector || !collector->arena || diagnostic.severity != C_DIAGNOSTIC_WARNING)
    {
        return;
    }
    String8 formatted = string_format(collector->arena, S8("{S8}:{u32}:{u32}: warning: {S8}\n"), path, diagnostic.location.line, diagnostic.location.column,
                                      diagnostic.message);
    compiler_driver_warning_append_text(collector, formatted);
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_warning_flatten(CompilerDriverWarningCollector collector)
{
    if (!collector.arena || !collector.length)
    {
        return (String8){0};
    }
    char8* text = arena_allocate(collector.arena, char8, collector.length + 1);
    u64 offset = 0;
    for (CompilerDriverWarningChunk* chunk = collector.first; chunk; chunk = chunk->next)
    {
        memcpy(text + offset, chunk->text.pointer, chunk->text.length);
        offset += chunk->text.length;
    }
    text[offset] = 0;
    return (String8){
        .pointer = text,
        .length = offset,
    };
}

BUSTER_GLOBAL_LOCAL CDiagnostic* compiler_driver_first_preprocess_error(CPreprocessResult preprocess)
{
    for (u64 diagnostic_index = 0; diagnostic_index < preprocess.diagnostic_count; diagnostic_index += 1)
    {
        CDiagnostic* diagnostic = preprocess.diagnostics + diagnostic_index;
        if (diagnostic->severity != C_DIAGNOSTIC_WARNING)
        {
            return diagnostic;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input);

BUSTER_GLOBAL_LOCAL String8 compiler_driver_llvm_target_triple(Target target)
{
    if (target.cpu_arch == CPU_ARCH_WASM64)
    {
        return S8("wasm64-unknown-unknown");
    }
    if (target.cpu_arch == CPU_ARCH_BPFEL)
    {
        return S8("bpfel-unknown-linux");
    }
    bool aarch64 = target.cpu_arch == CPU_ARCH_AARCH64;
    if (aarch64 || target.cpu_arch == CPU_ARCH_X86_64)
    {
        switch (target.os)
        {
        case OPERATING_SYSTEM_LINUX:
            return aarch64 ? S8("aarch64-unknown-linux-gnu") : S8("x86_64-unknown-linux-gnu");
        case OPERATING_SYSTEM_ANDROID:
            return aarch64 ? S8("aarch64-unknown-linux-android") : S8("x86_64-unknown-linux-android");
        case OPERATING_SYSTEM_MACOS:
            return aarch64 ? S8("arm64-apple-macosx") : S8("x86_64-apple-macosx");
        case OPERATING_SYSTEM_IOS:
            return aarch64 ? S8("arm64-apple-ios") : S8("x86_64-apple-ios-simulator");
        case OPERATING_SYSTEM_WINDOWS:
            return aarch64 ? S8("aarch64-pc-windows-msvc") : S8("x86_64-pc-windows-msvc");
        case OPERATING_SYSTEM_UEFI:
            return aarch64 ? S8("aarch64-unknown-windows") : S8("x86_64-unknown-windows");
        case OPERATING_SYSTEM_FREESTANDING:
            return aarch64 ? S8("aarch64-unknown-none") : S8("x86_64-unknown-none");
        case OPERATING_SYSTEM_COUNT:
            break;
        }
    }

    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_llvm_data_layout(Target target)
{
    switch (target.cpu_arch)
    {
    case CPU_ARCH_X86_64:
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return S8("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI)
        {
            return S8("e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
        }
        return S8("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128");
    case CPU_ARCH_AARCH64:
        if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
        {
            return S8("e-m:o-i64:64-i128:128-n32:64-S128-Fn32");
        }
        if (target.os == OPERATING_SYSTEM_WINDOWS || target.os == OPERATING_SYSTEM_UEFI)
        {
            return S8("e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32");
        }
        return S8("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32");
    case CPU_ARCH_WASM64:
        return S8("e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
    case CPU_ARCH_BPFEL:
        return S8("e-m:e-p:64:64-i64:64-i128:128-n32:64-S128");
    case CPU_ARCH_COUNT:
        break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL LlvmBitcodeOptions compiler_driver_llvm_bitcode_options(Target target, String8 source_filename)
{
    return (LlvmBitcodeOptions){
        .target_triple = compiler_driver_llvm_target_triple(target),
        .data_layout = compiler_driver_llvm_data_layout(target),
        .source_filename = source_filename,
        .deterministic = true,
        // Both driver pipelines validate immediately before reaching the
        // emitter, so do not repeat a whole-module walk here.
        .validate_ir = false,
    };
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_llvm_bitcode_path(Arena* arena, String8 input)
{
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.')
        {
            extension = index - 1;
            break;
        }
        if (byte == '/' || byte == '\\')
        {
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.bc"), (String8){
                                                        .pointer = input.pointer,
                                                        .length = extension,
                                                    });
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_llvm_bitcode(Arena* arena, CompilerDriverInvocation invocation, LlvmBitcodeArtifact artifact,
                                                              CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->llvm_bitcode = artifact;
    if (!llvm_bitcode_artifact_is_valid(artifact))
    {
        result->error = COMPILER_DRIVER_ERROR_LLVM_BITCODE;
        result->diagnostic = artifact.error.diagnostic.length ? artifact.error.diagnostic
                             : artifact.error.message.length  ? artifact.error.message
                                                               : S8("LLVM bitcode emission failed");
        return false;
    }
    result->has_llvm_bitcode = true;
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_llvm_bitcode_path(arena, invocation.input_paths[0]);
    if (!file_write(output, artifact.bytes))
    {
        result->error = COMPILER_DRIVER_ERROR_FILE_READ;
        result->diagnostic = string_format(arena, S8("could not write {S8}"), output);
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_wasm64_path(Arena* arena, String8 input)
{
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.')
        {
            extension = index - 1;
            break;
        }
        if (byte == '/' || byte == '\\')
        {
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.wasm"), (String8){
                                                          .pointer = input.pointer,
                                                          .length = extension,
                                                      });
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_wasm64(Arena* arena, CompilerDriverInvocation invocation, Wasm64Artifact artifact,
                                                        CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->wasm64 = artifact;
    if (artifact.error.code != WASM64_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_WASM64;
        result->diagnostic = artifact.error.diagnostic.length ? artifact.error.diagnostic
                             : artifact.error.message.length  ? artifact.error.message
                                                               : S8("Wasm64 code generation failed");
        return false;
    }
    result->has_wasm64 = true;
    String8 output = invocation.output_path.length ? invocation.output_path
                     : invocation.action == COMPILER_DRIVER_ACTION_OBJECT
                         ? compiler_driver_default_wasm64_path(arena, invocation.input_paths[0])
                         : S8("a.wasm");
    if (!file_write(output, artifact.bytes))
    {
        result->error = COMPILER_DRIVER_ERROR_FILE_READ;
        result->diagnostic = string_format(arena, S8("could not write {S8}"), output);
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_write_ebpf(Arena* arena, CompilerDriverInvocation invocation, EbpfArtifact artifact,
                                                     CompilerDriverResult* result)
{
    if (!result)
    {
        return false;
    }
    result->ebpf = artifact;
    if (artifact.error.code != EBPF_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_EBPF;
        result->diagnostic = artifact.error.diagnostic.length ? artifact.error.diagnostic
                             : artifact.error.message.length  ? artifact.error.message
                                                               : S8("eBPF code generation failed");
        return false;
    }
    result->has_ebpf = true;
    String8 output = invocation.output_path.length ? invocation.output_path
                     : invocation.action == COMPILER_DRIVER_ACTION_OBJECT
                         ? compiler_driver_default_object_path(arena, invocation.input_paths[0])
                         : S8("a.o");
    if (!file_write(output, artifact.bytes))
    {
        result->error = COMPILER_DRIVER_ERROR_FILE_READ;
        result->diagnostic = string_format(arena, S8("could not write {S8}"), output);
        return false;
    }
    return true;
}

// What a finished object becomes: textual assembly for -S, a written object
// file for -c, or a linked executable. It is shared by the C pipeline above
// and by the assembly front door below, which reach the same three outputs
// through completely different producers.
BUSTER_GLOBAL_LOCAL void compiler_driver_emit_object_output(Arena* arena, CompilerDriverInvocation invocation, ObjectFile object,
                                                             bool suppress_object_write, CompilerDriverResult* result)
{
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result->output = object_print_assembly(arena, &object);
        if (!result->output.length)
        {
            result->error = COMPILER_DRIVER_ERROR_OBJECT;
            result->diagnostic = S8("could not format native object as textual assembly");
            return;
        }
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result->output)))
        {
            result->error = COMPILER_DRIVER_ERROR_FILE_READ;
            result->diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        return;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
    {
        if (suppress_object_write)
        {
            return;
        }
        ObjectArtifact artifact = object_write(arena, &object, object_format_for_target(invocation.target));
        if (artifact.error != OBJECT_ERROR_NONE)
        {
            result->error = COMPILER_DRIVER_ERROR_OBJECT;
            result->object_error = artifact.error;
            return;
        }
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        if (!file_write(output, artifact.bytes))
        {
            result->error = COMPILER_DRIVER_ERROR_FILE_READ;
            result->diagnostic = string_format(arena, S8("could not write {S8}"), output);
        }
        return;
    }
    ObjectFile link_inputs[3] = {object};
    u32 link_input_count = 1;
    if (compiler_driver_windows_runtime_object_target(invocation.target))
    {
        link_inputs[link_input_count++] = link_windows_runtime_object(arena, invocation.target);
        ObjectFile runtime = link_windows_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, link_inputs, link_input_count))
        {
            link_inputs[link_input_count++] = runtime;
        }
    }
    if (compiler_driver_elf_runtime_object_target(invocation.target))
    {
        ObjectFile runtime = link_elf_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, link_inputs, link_input_count))
        {
            link_inputs[link_input_count++] = runtime;
        }
    }
    LinkObjectResult linked = link_objects(arena, link_inputs, link_input_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->diagnostic = linked.symbol.length ? string_format(arena, S8("C object linking failed with error {u32} on symbol '{S8}'"), (u32)linked.error, linked.symbol)
                                                 : string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        return;
    }
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, 0, &linked.object);
    if (dynamic_libraries.missing_request.length)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->diagnostic = string_format(arena, S8("cannot find -l{S8}"), dynamic_libraries.missing_request);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        return;
    }
    result->native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = invocation.entry_symbol.length ? invocation.entry_symbol
                                                                                                   : compiler_driver_default_entry_symbol(invocation.target),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_data_symbols = dynamic_libraries.runtime.exported_data_symbols,
                                                    .runtime_versioned_symbols = dynamic_libraries.runtime.versioned_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_data_symbol_count = dynamic_libraries.runtime.exported_data_symbol_count,
                                                    .runtime_versioned_symbol_count = dynamic_libraries.runtime.versioned_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result->native_link.error != LINK_ERROR_NONE)
    {
        result->error = COMPILER_DRIVER_ERROR_LINK;
        result->diagnostic =
            string_format(arena, S8("native C link failed with {S8}: {S8}"), link_error_name(result->native_link.error), result->native_link.symbol);
    }
}

// The section an assembled unit's kind becomes. The unit keeps the section's
// own name -- `.init` and `.fini` are neither `.text` nor absent -- and the
// kind only decides the flags the object writer stamps on it.
BUSTER_GLOBAL_LOCAL ObjectSectionKind compiler_driver_assembly_section_kind(AssemblyUnitSectionKind kind)
{
    switch (kind)
    {
    case ASSEMBLY_UNIT_SECTION_TEXT: return OBJECT_SECTION_TEXT;
    case ASSEMBLY_UNIT_SECTION_READ_ONLY_DATA: return OBJECT_SECTION_READ_ONLY_DATA;
    case ASSEMBLY_UNIT_SECTION_DATA: return OBJECT_SECTION_DATA;
    case ASSEMBLY_UNIT_SECTION_ZERO:
    case ASSEMBLY_UNIT_SECTION_KIND_COUNT: break;
    }
    return OBJECT_SECTION_ZERO;
}

// The assembler reports a relocation in its own vocabulary, which is wider
// than the object model's. A family the object cannot express is refused
// rather than written without its relocation.
BUSTER_GLOBAL_LOCAL bool compiler_driver_assembly_relocation_kind(AssemblyRelocationKind kind, ObjectRelocationKind* object_kind)
{
    switch (kind)
    {
    case ASSEMBLY_RELOCATION_X86_PC32: *object_kind = OBJECT_RELOCATION_X86_64_PC32; return true;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32: *object_kind = OBJECT_RELOCATION_ABSOLUTE32; return true;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE64: *object_kind = OBJECT_RELOCATION_ABSOLUTE64; return true;
    case ASSEMBLY_RELOCATION_X86_ABSOLUTE32_SIGN_EXTENDED: *object_kind = OBJECT_RELOCATION_X86_64_ABSOLUTE32S; return true;
    case ASSEMBLY_RELOCATION_AARCH64_BRANCH26: *object_kind = OBJECT_RELOCATION_AARCH64_JUMP26; return true;
    case ASSEMBLY_RELOCATION_AARCH64_CALL26: *object_kind = OBJECT_RELOCATION_AARCH64_CALL26; return true;
    default: break;
    }
    return false;
}

// One assembly input, from source text to the same three outputs a C input
// reaches. The assembler is the whole front end here: there is no
// preprocessor, no IR, and no code generation between the file and the object.
// The assemble-and-emit half shared by a `.s` input and a preprocessed `.S`
// one: the caller owns the source text's lifetime, and the encoder keeps
// nothing that points into it.
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_assembly_source(Arena* arena, CompilerDriverInvocation invocation, String8 source,
                                                                                  String8 path, bool suppress_object_write)
{
    CompilerDriverResult result = {0};
    if (invocation.emit_llvm_bitcode || invocation.target.cpu_arch == CPU_ARCH_WASM64 || invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("assembly input has no LLVM bitcode, Wasm64, or eBPF emission");
        return result;
    }
    AssemblyUnitResult unit = assembly_unit_encode(arena, source,
                                                   (AssemblyEncodeOptions){
                                                       .target = invocation.target,
                                                       .syntax = invocation.target.cpu_arch == CPU_ARCH_X86_64 ? invocation.assembly_syntax
                                                                                                              : ASSEMBLY_SYNTAX_DEFAULT,
                                                   });
    if (unit.diagnostic_count)
    {
        AssemblyDiagnostic diagnostic = unit.diagnostics[0];
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), path, diagnostic.line, diagnostic.column, diagnostic.message);
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        return result;
    }
    ObjectFile object = {
        .target = invocation.target,
        .sections = arena_allocate(arena, ObjectSection, unit.section_count ? unit.section_count : 1),
        .symbols = arena_allocate(arena, ObjectSymbol, unit.symbol_count ? unit.symbol_count : 1),
        .relocations = arena_allocate(arena, ObjectRelocation, unit.relocation_count ? unit.relocation_count : 1),
        .section_count = unit.section_count,
        .symbol_count = unit.symbol_count,
    };
    for (u32 index = 0; index < unit.section_count; index += 1)
    {
        AssemblyUnitSection section = unit.sections[index];
        object.sections[index] = (ObjectSection){
            .name = section.name,
            .data = section.data,
            .virtual_size = section.zero_size,
            .kind = compiler_driver_assembly_section_kind(section.kind),
            .alignment = section.alignment,
        };
    }
    for (u32 index = 0; index < unit.symbol_count; index += 1)
    {
        AssemblyUnitSymbol symbol = unit.symbols[index];
        object.symbols[index] = (ObjectSymbol){
            .name = symbol.name,
            .value = symbol.value,
            .size = symbol.size,
            .section = symbol.defined ? symbol.section : OBJECT_SECTION_UNDEFINED,
            .kind = symbol.function ? OBJECT_SYMBOL_FUNCTION : OBJECT_SYMBOL_DATA,
            .global = symbol.global,
            .weak = symbol.weak,
            .hidden = symbol.hidden,
        };
    }
    for (u32 index = 0; index < unit.relocation_count; index += 1)
    {
        AssemblyUnitRelocation relocation = unit.relocations[index];
        ObjectRelocationKind kind = OBJECT_RELOCATION_X86_64_PC32;
        if (!compiler_driver_assembly_relocation_kind(relocation.kind, &kind))
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = string_format(arena, S8("{S8}: relocation family {u32} has no object representation"), path, (u32)relocation.kind);
            return result;
        }
        object.relocations[object.relocation_count++] = (ObjectRelocation){
            // The assembler's addend already carries the distance from the
            // relocated field to the end of its instruction, which is what an
            // ELF PC-relative addend is; nothing is added or removed here.
            .addend = relocation.addend,
            .offset = relocation.offset,
            .section = relocation.section,
            .symbol = relocation.symbol,
            .kind = kind,
        };
    }
    result.object = object;
    result.has_object = true;
    compiler_driver_emit_object_output(arena, invocation, object, suppress_object_write, &result);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_assembly_single(Arena* arena, CompilerDriverInvocation invocation,
                                                                                  bool suppress_object_write)
{
    CompilerDriverResult result = {0};
    String8 path = invocation.input_paths[0];
    FileMapRead source_file = file_map_read(arena, path, (FileReadOptions){0});
    if (!source_file.bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), path);
        file_map_unmap(source_file);
        return result;
    }
    String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        // An assembly unit is already what the preprocessor would have
        // produced, so -E hands the text back unchanged.
        result.output = string_duplicate_arena(arena, source, false);
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        file_map_unmap(source_file);
        return result;
    }
    result = compiler_driver_execute_assembly_source(arena, invocation, source, path, suppress_object_write);
    file_map_unmap(source_file);
    return result;
}

// GNU's `.S` runs the C preprocessor over the assembly text before it is
// assembled.  The -E printer reproduces line structure and adjacency from
// the source map, so `%rax`, `1f` and `.globl` survive the round trip, and
// the preprocess itself runs with assembly_comment_lines: a `#` line whose
// word is no directive is GNU-as commentary, not an error.  CPython's
// Python/asm_trampoline.S is the load-bearing case -- one trampoline body
// selected by #ifdef per architecture.
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_preprocessed_assembly_single(Arena* arena, CompilerDriverInvocation invocation,
                                                                                               bool suppress_object_write)
{
    CompilerDriverResult result = {0};
    String8 path = invocation.input_paths[0];
    FileMapRead source_file = file_map_read(arena, path, (FileReadOptions){0});
    if (!source_file.bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), path);
        file_map_unmap(source_file);
        return result;
    }
    CPreprocessorDefinition* definitions = arena_allocate(arena, CPreprocessorDefinition, invocation.definition_count);
    for (u32 index = 0; index < invocation.definition_count; index += 1)
    {
        definitions[index] = compiler_driver_c_definition(invocation.definitions[index]);
    }
    // GNU-as spells an immediate `$NAME`, and the C lexer reads `$` as an
    // identifier character, which would glue the prefix onto a macro name
    // and keep it from expanding.  A space after every `$` splits the two
    // the way GNU cpp's assembler mode tokenizes them, and the assembler
    // reads `$ 0` and `$0` alike; quoted regions keep their bytes.
    String8 raw = BYTE_SLICE_TO_STRING(8, source_file.bytes);
    char8* split = arena_allocate(arena, char8, raw.length * 2 + 1);
    u64 split_length = 0;
    bool in_string = false;
    char8 quote = 0;
    for (u64 byte_index = 0; byte_index < raw.length; byte_index += 1)
    {
        char8 byte = raw.pointer[byte_index];
        split[split_length++] = byte;
        if (in_string)
        {
            if (byte == '\\' && byte_index + 1 < raw.length)
            {
                split[split_length++] = raw.pointer[++byte_index];
            }
            else if (byte == quote)
            {
                in_string = false;
            }
        }
        else if (byte == '"' || byte == '\'')
        {
            in_string = true;
            quote = byte;
        }
        else if (byte == '$')
        {
            split[split_length++] = ' ';
        }
    }
    CPreprocessResult preprocess = c_preprocess(arena, (String8){.pointer = split, .length = split_length},
                                                (CPreprocessOptions){
                                                    .definitions = definitions,
                                                    .undefinitions = invocation.undefinitions,
                                                    .include_paths = invocation.include_paths,
                                                    .system_include_paths = invocation.system_include_paths,
                                                    .source_path = path,
                                                    .target = invocation.target,
                                                    .data_layout = target_data_layout(invocation.target),
                                                    .dialect = compiler_driver_preprocess_dialect(invocation.c_dialect),
                                                    .definition_count = invocation.definition_count,
                                                    .undefinition_count = invocation.undefinition_count,
                                                    .include_path_count = invocation.include_path_count,
                                                    .system_include_path_count = invocation.system_include_path_count,
                                                    .assembly_comment_lines = true,
                                                });
    file_map_unmap(source_file);
    if (preprocess.error_count)
    {
        CDiagnostic* diagnostic = compiler_driver_first_preprocess_error(preprocess);
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.error_count;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), path, diagnostic->location.line, diagnostic->location.column,
                                          diagnostic->message);
        return result;
    }
    String8 source = compiler_driver_preprocess_text(arena, preprocess);
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = source;
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        return result;
    }
    return compiler_driver_execute_assembly_source(arena, invocation, source, path, suppress_object_write);
}

static CompilerDriverResult compiler_driver_execute_c_single(Arena* arena, CompilerDriverInvocation invocation, bool suppress_object_write,
                                                             CompilerDriverWarningCollector* warnings)
{
    CompilerDriverResult result = {
        .error = invocation.error,
        .diagnostic = invocation.diagnostic,
    };
    FileMapRead source_file = {0};
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return result;
    }
    if (invocation.input_count == 1 && compiler_driver_preprocessed_assembly_input(invocation.input_paths[0]))
    {
        return compiler_driver_execute_preprocessed_assembly_single(arena, invocation, suppress_object_write);
    }
    if (invocation.input_count == 1 && compiler_driver_assembly_input(invocation, invocation.input_paths[0]))
    {
        return compiler_driver_execute_assembly_single(arena, invocation, suppress_object_write);
    }
    if (invocation.input_count != 1 || !compiler_driver_c_input(invocation, invocation.input_paths[0]))
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the C frontend currently requires exactly one C input");
        goto end;
    }
    source_file = file_map_read(arena, invocation.input_paths[0], (FileReadOptions){0});
    ByteSlice bytes = source_file.bytes;
    if (!bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), invocation.input_paths[0]);
        goto end;
    }
    CPreprocessorDefinition* definitions = arena_allocate(arena, CPreprocessorDefinition, invocation.definition_count);
    for (u32 index = 0; index < invocation.definition_count; index += 1)
    {
        definitions[index] = compiler_driver_c_definition(invocation.definitions[index]);
    }
    CPreprocessResult preprocess = c_preprocess(arena, BYTE_SLICE_TO_STRING(8, bytes),
                                                (CPreprocessOptions){
                                                    .definitions = definitions,
                                                    .undefinitions = invocation.undefinitions,
                                                    .include_paths = invocation.include_paths,
                                                    .system_include_paths = invocation.system_include_paths,
                                                    .source_path = invocation.input_paths[0],
                                                    .target = invocation.target,
                                                    .data_layout = target_data_layout(invocation.target),
                                                    .dialect = compiler_driver_preprocess_dialect(invocation.c_dialect),
                                                    .definition_count = invocation.definition_count,
                                                    .undefinition_count = invocation.undefinition_count,
                                                    .include_path_count = invocation.include_path_count,
                                                    .system_include_path_count = invocation.system_include_path_count,
                                                });
    // Reported even when a later stage fails: the units the frontend read are
    // measured by then, and a failing compile is exactly when the size of
    // what it read is worth knowing.
    CPreprocessDetail const* preprocess_detail = c_preprocess_detail(preprocess);
    result.source_lexed = preprocess_detail->source_lexed;
    result.source_unique = preprocess_detail->source_unique;
    result.lexed_files = preprocess_detail->lexed_files;
    result.lexed_file_count = preprocess_detail->lexed_file_count;
    result.preprocessed = preprocess_detail->preprocessed;
    for (u64 diagnostic_index = 0; diagnostic_index < preprocess.diagnostic_count; diagnostic_index += 1)
    {
        compiler_driver_append_warning(warnings, invocation.input_paths[0], preprocess.diagnostics[diagnostic_index]);
    }
    result.tokenizer_warning_count = (u32)preprocess.warning_count;
    if (preprocess.error_count)
    {
        CDiagnostic* diagnostic = compiler_driver_first_preprocess_error(preprocess);
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.error_count;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic->location.line, diagnostic->location.column,
                                          diagnostic->message);
        goto end;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = compiler_driver_preprocess_text(arena, preprocess);
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        goto end;
    }
    CParserResult syntax = c_parse_ast(arena, preprocess);
    result.parser_diagnostic_count = syntax.diagnostic_count;
    if (syntax.diagnostic_count)
    {
        CDiagnostic diagnostic = syntax.diagnostics[0];
        result.error = COMPILER_DRIVER_ERROR_PARSE;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line, diagnostic.location.column,
                                          diagnostic.message);
        goto end;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        CIRLowerResult semantic = c_analyze(arena, invocation.input_paths[0], preprocess, syntax, invocation.target);
        result.analysis_diagnostic_count = semantic.diagnostic_count;
        if (semantic.diagnostic_count || !semantic.program)
        {
            result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
            if (semantic.diagnostic_count)
            {
                CDiagnostic diagnostic = semantic.diagnostics[0];
                result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line,
                                                  diagnostic.location.column, diagnostic.message);
            }
            else
            {
                result.diagnostic = string_format(arena, S8("{S8}: semantic validation failed"), invocation.input_paths[0]);
            }
        }
        goto end;
    }
    CIRLowerResult lowered = c_analyze(arena, invocation.input_paths[0], preprocess, syntax, invocation.target);
    result.analysis_diagnostic_count = lowered.diagnostic_count;
    if (!lowered.program || lowered.diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
        if (lowered.diagnostic_count)
        {
            CDiagnostic diagnostic = lowered.diagnostics[0];
            result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line,
                                              diagnostic.location.column, diagnostic.message);
        }
        goto end;
    }
    IrModule* module = &lowered.program->modules[0];
    IrValidationResult validation = lowered.canonical_ir_certified
                                        ? (IrValidationResult){
                                              .function = IR_FUNCTION_ID_INVALID,
                                              .block = IR_BLOCK_ID_INVALID,
                                              .instruction = IR_INSTRUCTION_ID_INVALID,
                                          }
                                        : ir_validate_canonical_module(lowered.program, module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        String8 function_name = validation.function.value < module->function_count ? module->functions[validation.function.value].name : S8("<invalid>");
        u32 opcode = IR_OPCODE_COUNT;
        if (validation.function.value < module->function_count)
        {
            IrFunction* failed_function = &module->functions[validation.function.value];
            if (validation.instruction.value < failed_function->instruction_count)
            {
                opcode = (u32)failed_function->instructions[validation.instruction.value].opcode;
            }
        }
        result.error = COMPILER_DRIVER_ERROR_IR;
        result.diagnostic =
            string_format(arena, S8("canonical C IR validation failed: error {u32}, function {u32} ('{S8}'), block {u32}, instruction {u32}, opcode {u32}"),
                          (u32)validation.error, validation.function.value, function_name, validation.block.value, validation.instruction.value, opcode);
        goto end;
    }
    if (invocation.emit_llvm_bitcode)
    {
        LlvmBitcodeArtifact artifact =
            llvm_bitcode_emit_with_options(arena, lowered.program, module, 1,
                                           compiler_driver_llvm_bitcode_options(invocation.target, invocation.input_paths[0]));
        compiler_driver_write_llvm_bitcode(arena, invocation, artifact, &result);
        goto end;
    }
    if (invocation.target.cpu_arch == CPU_ARCH_WASM64)
    {
        Wasm64Artifact artifact = wasm64_emit(arena, lowered.program, module, 1);
        compiler_driver_write_wasm64(arena, invocation, artifact, &result);
        goto end;
    }
    if (invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        EbpfArtifact artifact = ebpf_emit(arena, lowered.program, module, 1);
        compiler_driver_write_ebpf(arena, invocation, artifact, &result);
        goto end;
    }
    CodegenModule code = codegen_generate_canonical_module(arena, lowered.program, module, invocation.target,
                                                           (CodegenModuleOptions){
                                                               .debug_info = invocation.debug_info,
                                                               .assume_validated = true,
                                                               .position_independent = invocation.position_independent,
                                                               .register_allocator = invocation.register_allocator,
                                                               .assembly_syntax = (u8)invocation.assembly_syntax,
                                                           });
    result.codegen_statistics = code.statistics;
    result.codegen_error = code.error;
    if (code.error != CODEGEN_ERROR_NONE && code.failed_in_assembly)
    {
        // A module-level assembly block has no function and no IR
        // instruction, so the function-shaped report above it would name
        // whichever C function happens to come next in the file. Name the
        // block instead: where the `__asm__` was written, which line of it
        // stopped, and what that line says.
        IrModuleAssembly assembly = code.failed_assembly < module->assembly_count ? module->assemblies[code.failed_assembly] : (IrModuleAssembly){0};
        IrSourcePosition position = ir_source_position(lowered.program, assembly.source_range);
        String8 line = {0};
        u32 line_number = 0;
        u64 line_start = 0;
        while (line_start < assembly.source.length && line_number < code.failed_assembly_line)
        {
            u64 line_end = line_start;
            while (line_end < assembly.source.length && assembly.source.pointer[line_end] != '\n')
            {
                line_end += 1;
            }
            line_number += 1;
            line = (String8){
                .pointer = assembly.source.pointer + line_start,
                .length = line_end - line_start,
            };
            line_start = line_end < assembly.source.length ? line_end + 1 : assembly.source.length;
        }
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        result.diagnostic =
            string_format(arena, S8("C module assembly generation failed with error {u32}, block {u32}, block line {u32} ('{S8}'), source {u32}:{u32}"),
                          (u32)code.error, code.failed_assembly, code.failed_assembly_line, line, position.line, position.column);
        goto end;
    }
    if (code.error != CODEGEN_ERROR_NONE)
    {
        String8 function_name = code.failed_function.value < module->function_count ? module->functions[code.failed_function.value].name : S8("<none>");
        u32 operation = IR_BINARY_COUNT;
        u32 source_line = 0;
        u32 source_column = 0;
        u32 function_state = IR_FUNCTION_STATE_COUNT;
        u32 function_block_count = 0;
        u32 function_instruction_count = 0;
        String8 referenced_symbol = S8("<none>");
        if (code.failed_function.value < module->function_count)
        {
            IrFunction* failed_function = &module->functions[code.failed_function.value];
            function_state = (u32)failed_function->state;
            function_block_count = failed_function->block_count;
            function_instruction_count = failed_function->instruction_count;
            if (code.failed_instruction.value < failed_function->instruction_count)
            {
                IrInstruction* failed_instruction = &failed_function->instructions[code.failed_instruction.value];
                IrSourceRange failed_source = failed_function->instruction_canonical_sources[code.failed_instruction.value];
                IrSourcePosition failed_position = ir_source_position(lowered.program, failed_source);
                source_line = failed_position.line;
                source_column = failed_position.column;
                operation = failed_instruction->opcode == IR_OPCODE_BINARY  ? (u32)failed_instruction->binary_operation
                            : failed_instruction->opcode == IR_OPCODE_UNARY ? (u32)failed_instruction->unary_operation
                                                                            : (u32)IR_BINARY_COUNT;
                if (failed_instruction->opcode == IR_OPCODE_CALL && failed_instruction->operand_count &&
                    failed_instruction->operands[0].value < failed_function->value_count)
                {
                    IrInstructionId definition = failed_function->values[failed_instruction->operands[0].value].definition;
                    if (definition.value < failed_function->instruction_count)
                    {
                        IrInstruction* reference = &failed_function->instructions[definition.value];
                        IrSymbol* symbol = ir_symbol_from_id(&lowered.program->symbols, reference->symbol);
                        if (reference->opcode == IR_OPCODE_FUNCTION && symbol)
                        {
                            referenced_symbol = symbol->link_name;
                        }
                    }
                }
            }
        }
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        // A refusal that has a rule behind it says so in the rule's own words
        // and blames the source position, the way the frontend's refusals do;
        // the instruction identity below is what is left for a shape that has
        // no lowering yet, where there is no rule to name (#831).
        result.diagnostic =
            code.failure_reason.length
                ? string_format(arena, S8("{S8} (in function '{S8}', source {u32}:{u32})"), code.failure_reason, function_name, source_line, source_column)
                : string_format(arena,
                                S8("C code generation failed with error {u32}, function {u32} ('{S8}', state {u32}, blocks {u32}, instructions {u32}), "
                                   "instruction {u32}, opcode {u32}, operation {u32}, source {u32}:{u32}, referenced symbol '{S8}'"),
                                (u32)code.error, code.failed_function.value, function_name, function_state, function_block_count, function_instruction_count,
                                code.failed_instruction.value, (u32)code.failed_opcode, operation, source_line, source_column, referenced_symbol);
        goto end;
    }
    ObjectFile object = object_from_canonical_codegen_module(arena, lowered.program, &code, invocation.target);
    result.object_error = object.error;
    if (object.error != OBJECT_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_OBJECT;
        result.diagnostic = string_format(arena, S8("C object generation failed with error {u32}"), (u32)object.error);
        goto end;
    }
    result.object = object;
    result.has_object = true;
    compiler_driver_emit_object_output(arena, invocation, object, suppress_object_write, &result);
end:
    file_map_unmap(source_file);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input)
{
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.')
        {
            extension = index - 1;
            break;
        }
        if (byte == '/' || byte == '\\')
        {
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.o"), (String8){
                                                           .pointer = input.pointer,
                                                           .length = extension,
                                                       });
}


BUSTER_GLOBAL_LOCAL GpuPipelineAction compiler_driver_gpu_action(CompilerDriverAction action)
{
    switch (action)
    {
    case COMPILER_DRIVER_ACTION_LINK: return GPU_PIPELINE_ACTION_LINK;
    case COMPILER_DRIVER_ACTION_PREPROCESS: return GPU_PIPELINE_ACTION_PREPROCESS;
    case COMPILER_DRIVER_ACTION_ASSEMBLY: return GPU_PIPELINE_ACTION_ASSEMBLY;
    case COMPILER_DRIVER_ACTION_OBJECT: return GPU_PIPELINE_ACTION_OBJECT;
    case COMPILER_DRIVER_ACTION_SYNTAX_ONLY: return GPU_PIPELINE_ACTION_SYNTAX_ONLY;
    case COMPILER_DRIVER_ACTION_COUNT: break;
    }
    return GPU_PIPELINE_ACTION_COUNT;
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_execute_gpu(Arena* arena, CompilerDriverInvocation invocation,
                                                                      CompilerDriverWarningCollector* warnings)
{
    CompilerDriverResult result = {0};
    GpuSourceLanguage language = compiler_driver_gpu_language(invocation.language);
    GpuPipelineAction action = compiler_driver_gpu_action(invocation.action);
    if (language == GPU_SOURCE_LANGUAGE_COUNT || action == GPU_PIPELINE_ACTION_COUNT)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("invalid language or action for GPU compilation");
        return result;
    }
    bool capture_text_output = !invocation.output_path.length &&
                               (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY);
    GpuPipelineResult pipeline = gpu_pipeline_execute(arena,
                                                      (GpuPipelineOptions){
                                                          .input_paths = invocation.input_paths,
                                                          .include_paths = invocation.include_paths,
                                                          .system_include_paths = invocation.system_include_paths,
                                                          .definitions = invocation.definitions,
                                                          .undefinitions = invocation.undefinitions,
                                                          .extra_arguments = invocation.gpu_arguments,
                                                          .output_path = invocation.output_path,
                                                          .sysroot = invocation.sysroot,
                                                          .cuda_path = invocation.cuda_path,
                                                          .rocm_path = invocation.rocm_path,
                                                          .tools = invocation.gpu_tools,
                                                          .target = invocation.gpu_target,
                                                          .input_count = invocation.input_count,
                                                          .include_path_count = invocation.include_path_count,
                                                          .system_include_path_count = invocation.system_include_path_count,
                                                          .definition_count = invocation.definition_count,
                                                          .undefinition_count = invocation.undefinition_count,
                                                          .extra_argument_count = invocation.gpu_argument_count,
                                                          .language = language,
                                                          .action = action,
                                                          .optimization_level = invocation.optimization_level,
                                                          .debug_info = invocation.debug_info,
                                                          .no_standard_includes = invocation.no_standard_includes,
                                                          .save_temporaries = invocation.save_gpu_temporaries,
                                                          .capture_text_output = capture_text_output,
                                                      });
    if (pipeline.error != GPU_PIPELINE_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_GPU;
        result.diagnostic = pipeline.diagnostic.length ? pipeline.diagnostic : S8("GPU pipeline failed without a diagnostic");
        return result;
    }
    if (pipeline.log.length)
    {
        compiler_driver_warning_append_text(warnings, pipeline.log);
    }
    if (pipeline.artifact.format != GPU_OUTPUT_NONE)
    {
        result.gpu = pipeline.artifact;
        result.has_gpu = true;
        if (capture_text_output)
        {
            result.output = BYTE_SLICE_TO_STRING(8, pipeline.artifact.bytes);
        }
    }
    return result;
}

CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation)
{
    CompilerDriverWarningCollector warnings = {
        .arena = arena,
    };
    CompilerDriverResult result = {0};
    if (!arena)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("compiler driver requires an arena");
        return result;
    }
    if (invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        result.error = invocation.error;
        result.diagnostic = invocation.diagnostic.length ? invocation.diagnostic : S8("invalid compiler invocation");
        goto finish;
    }
    if (invocation.has_gpu_target)
    {
        result = compiler_driver_execute_gpu(arena, invocation, &warnings);
        goto finish;
    }
    if (invocation.emit_llvm_bitcode)
    {
        if (invocation.library_count || invocation.library_path_count || invocation.framework_count || invocation.framework_path_count ||
            invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("LLVM bitcode output does not accept native libraries, frameworks, or linker arguments");
            goto finish;
        }
        for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
        {
            if (compiler_driver_object_input(invocation.input_paths[input_index]) || compiler_driver_archive_input(invocation.input_paths[input_index]))
            {
                result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
                result.diagnostic = S8("native objects and archives cannot be included in LLVM bitcode output");
                goto finish;
            }
        }
    }
    if (invocation.target.cpu_arch == CPU_ARCH_WASM64)
    {
        if (invocation.target.os != OPERATING_SYSTEM_FREESTANDING)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("Wasm64 currently requires the wasm64-unknown-freestanding target");
            goto finish;
        }
        if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("-S is not supported for direct Wasm64 module output");
            goto finish;
        }
        if (invocation.input_count > 1 || invocation.library_count || invocation.library_path_count || invocation.framework_count ||
            invocation.framework_path_count || invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("Wasm64 accepts one source program and no native objects, archives, libraries, frameworks, or linker arguments");
            goto finish;
        }
        if (invocation.input_count &&
            (compiler_driver_object_input(invocation.input_paths[0]) || compiler_driver_archive_input(invocation.input_paths[0])))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("native objects and archives cannot be linked into a Wasm64 module");
            goto finish;
        }
    }
    if (invocation.target.cpu_arch == CPU_ARCH_BPFEL)
    {
        if (invocation.target.os != OPERATING_SYSTEM_LINUX)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("eBPF currently requires the bpfel-unknown-linux target");
            goto finish;
        }
        if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
        {
            result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            result.diagnostic = S8("-S is not supported for direct eBPF object output");
            goto finish;
        }
        if (invocation.input_count > 1 || invocation.library_count || invocation.library_path_count || invocation.framework_count ||
            invocation.framework_path_count || invocation.linker_argument_count)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("eBPF accepts one source program and no native objects, archives, libraries, frameworks, or linker arguments");
            goto finish;
        }
        if (invocation.input_count &&
            (compiler_driver_object_input(invocation.input_paths[0]) || compiler_driver_archive_input(invocation.input_paths[0])))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("native objects and archives cannot be linked into an eBPF object");
            goto finish;
        }
    }
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 path = invocation.input_paths[input_index];
        bool object_input = compiler_driver_object_input(path);
        bool archive_input = compiler_driver_archive_input(path);
        if ((object_input || archive_input) && invocation.action != COMPILER_DRIVER_ACTION_LINK)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("prebuilt input {S8} is only valid while linking"), path);
            goto finish;
        }
        if (!object_input && !archive_input && !compiler_driver_c_input(invocation, path) && !compiler_driver_assembly_input(invocation, path) &&
            !compiler_driver_preprocessed_assembly_input(path))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("unsupported C input {S8}"), path);
            goto finish;
        }
    }
    if (invocation.input_count <= 1 && !invocation.library_count &&
        (!invocation.input_count || (!compiler_driver_object_input(invocation.input_paths[0]) && !compiler_driver_archive_input(invocation.input_paths[0]))))
    {
        result = compiler_driver_execute_c_single(arena, invocation, false, &warnings);
        goto finish;
    }
    if ((invocation.emit_llvm_bitcode || invocation.action == COMPILER_DRIVER_ACTION_OBJECT ||
         invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY || invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY) &&
        invocation.output_path.length)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = invocation.emit_llvm_bitcode                         ? S8("cannot specify -o with -emit-llvm and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_OBJECT ? S8("cannot specify -o with -c and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ? S8("cannot specify -o with -S and multiple input files")
                                                                                     : S8("cannot specify -o with -fsyntax-only and multiple input files");
        goto finish;
    }
    ObjectArchive* input_archives = arena_allocate(arena, ObjectArchive, invocation.input_count);
    u32 object_capacity = invocation.input_count;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 input_path = invocation.input_paths[input_index];
        if (!compiler_driver_archive_input(input_path))
        {
            continue;
        }
        FileMapRead archive_file = file_map_read(arena, input_path, (FileReadOptions){0});
        if (!archive_file.bytes.pointer)
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not read {S8}"), input_path);
            file_map_unmap(archive_file);
            goto finish;
        }
        input_archives[input_index] = object_archive_read(arena, archive_file.bytes, invocation.target);
        file_map_unmap(archive_file);
        if (input_archives[input_index].error != OBJECT_ERROR_NONE || input_archives[input_index].object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = input_archives[input_index].error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), input_path, (u32)input_archives[input_index].error);
            goto finish;
        }
        object_capacity += input_archives[input_index].object_count;
    }
    ObjectArchive* library_archives = arena_allocate(arena, ObjectArchive, invocation.library_count);
    bool* static_libraries = arena_allocate(arena, bool, invocation.library_count);
    memset(static_libraries, 0, sizeof(*static_libraries) * invocation.library_count);
    for (u32 library_index = 0; library_index < invocation.library_count; library_index += 1)
    {
        bool found = false;
        String8 archive_path = {0};
        ObjectArchive archive = compiler_driver_library_archive(arena, invocation, invocation.libraries[library_index], &found, &archive_path);
        if (!found)
        {
            if (invocation.target.os == OPERATING_SYSTEM_UEFI)
            {
                result.error = COMPILER_DRIVER_ERROR_FILE_READ;
                result.diagnostic = string_format(arena, S8("could not find static UEFI library {S8}"), invocation.libraries[library_index]);
                goto finish;
            }
            continue;
        }
        static_libraries[library_index] = true;
        library_archives[library_index] = archive;
        if (archive.error != OBJECT_ERROR_NONE || archive.object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = archive.error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), archive_path, (u32)archive.error);
            goto finish;
        }
        object_capacity += archive.object_count;
    }
    u32 runtime_object_capacity =
        !invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK ? compiler_driver_runtime_object_capacity(invocation.target) : 0;
    if (runtime_object_capacity)
    {
        if (object_capacity > UINT32_MAX - runtime_object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("too many link inputs");
            goto finish;
        }
        object_capacity += runtime_object_capacity;
    }
    ObjectFile* objects = arena_allocate(arena, ObjectFile, object_capacity);
    String8* preprocessed = arena_allocate(arena, String8, invocation.input_count);
    u32 object_count = 0;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 input_path = invocation.input_paths[input_index];
        if (compiler_driver_archive_input(input_path))
        {
            ObjectArchive* archive = &input_archives[input_index];
            bool* selected = arena_allocate(arena, bool, archive->object_count);
            memset(selected, 0, sizeof(*selected) * archive->object_count);
            bool added = false;
            do
            {
                added = false;
                for (u32 member_index = 0; member_index < archive->object_count; member_index += 1)
                {
                    if (selected[member_index] || !compiler_driver_archive_member_needed(&archive->objects[member_index], objects, object_count))
                    {
                        continue;
                    }
                    selected[member_index] = true;
                    objects[object_count++] = archive->objects[member_index];
                    added = true;
                }
            } while (added);
            continue;
        }
        if (compiler_driver_object_input(input_path))
        {
            FileMapRead object_file = file_map_read(arena, input_path, (FileReadOptions){0});
            if (!object_file.bytes.pointer)
            {
                result.error = COMPILER_DRIVER_ERROR_FILE_READ;
                result.diagnostic = string_format(arena, S8("could not read {S8}"), input_path);
                file_map_unmap(object_file);
                goto finish;
            }
            ObjectFile object = object_read(arena, object_file.bytes, invocation.target);
            file_map_unmap(object_file);
            if (object.error != OBJECT_ERROR_NONE)
            {
                result.error = COMPILER_DRIVER_ERROR_OBJECT;
                result.object_error = object.error;
                result.diagnostic = object.diagnostic.length
                                        ? string_format(arena, S8("could not read object {S8}: {S8}"), input_path, object.diagnostic)
                                        : string_format(arena, S8("could not read object {S8}: error {u32}"), input_path, (u32)object.error);
                goto finish;
            }
            objects[object_count++] = object;
            continue;
        }
        CompilerDriverInvocation single = invocation;
        single.input_paths = invocation.input_paths + input_index;
        single.input_count = 1;
        single.output_path = (String8){0};
        bool suppress_object_write = !invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK;
        if (!invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
        {
            String8 input = invocation.input_paths[input_index];
            u64 extension = input.length;
            for (u64 index = input.length; index != 0; index -= 1)
            {
                char8 byte = input.pointer[index - 1];
                if (byte == '.')
                {
                    extension = index - 1;
                    break;
                }
                if (byte == '/' || byte == '\\')
                {
                    break;
                }
            }
            single.output_path = string_format(arena, S8("{S8}.o"),
                                               (String8){
                                                   .pointer = input.pointer,
                                                   .length = extension,
                                               });
        }
        else if (!invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK)
        {
            single.action = COMPILER_DRIVER_ACTION_OBJECT;
        }
        Arena* unit_arena = arena_create((ArenaCreation){
            .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
            // Per-unit arenas churn once per translation unit (and once per
            // driver test fixture); reusing the parked mapping keeps its
            // already-faulted pages instead of paying mmap + first-touch
            // zeroing + munmap every time. The C pipeline never assumes
            // zeroed arena memory.
            .flags = {.pool_reuse = 1},
        });
        if (!unit_arena)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("could not allocate C translation-unit arena");
            goto finish;
        }
        CompilerDriverResult unit = compiler_driver_execute_c_single(unit_arena, single, suppress_object_write, &warnings);
        result.tokenizer_error_count += unit.tokenizer_error_count;
        result.tokenizer_warning_count += unit.tokenizer_warning_count;
        result.parser_diagnostic_count += unit.parser_diagnostic_count;
        result.analysis_diagnostic_count += unit.analysis_diagnostic_count;
        // Each unit dedups its own include closure, so across several inputs
        // the unique aggregate is a sum of per-unit uniques and still counts
        // a shared header once per unit that included it.
        c_source_metrics_add(&result.source_lexed, &unit.source_lexed);
        c_source_metrics_add(&result.source_unique, &unit.source_unique);
        // Only the amplification rows survive the unit arena: a row lexed
        // once is not amplification, and a header shared between units is
        // legitimately lexed once per unit, so per-unit rows are appended
        // rather than merged by path.
        for (u32 row_index = 0; row_index < unit.lexed_file_count; row_index += 1)
        {
            CSourceFileMetrics row = unit.lexed_files[row_index];
            if (row.lex_count > 1)
            {
                if (result.lexed_file_count == result.lexed_files_reserved)
                {
                    u32 row_capacity = result.lexed_files_reserved ? result.lexed_files_reserved * 2 : 64;
                    CSourceFileMetrics* rows = arena_allocate(arena, CSourceFileMetrics, row_capacity);
                    if (result.lexed_file_count)
                    {
                        memcpy(rows, result.lexed_files, result.lexed_file_count * sizeof(*rows));
                    }
                    result.lexed_files = rows;
                    result.lexed_files_reserved = row_capacity;
                }
                row.path = string_duplicate_arena(arena, row.path, false);
                result.lexed_files[result.lexed_file_count] = row;
                result.lexed_file_count += 1;
            }
        }
        result.preprocessed.tokens += unit.preprocessed.tokens;
        result.preprocessed.bytes += unit.preprocessed.bytes;
        result.preprocessed.spelling_bytes += unit.preprocessed.spelling_bytes;
        result.preprocessed.expansions += unit.preprocessed.expansions;
        result.preprocessed.definitions += unit.preprocessed.definitions;
        result.codegen_statistics.instruction_count += unit.codegen_statistics.instruction_count;
        result.codegen_statistics.value_count += unit.codegen_statistics.value_count;
        result.codegen_statistics.stack_value_bytes += unit.codegen_statistics.stack_value_bytes;
        result.codegen_statistics.stack_frame_bytes += unit.codegen_statistics.stack_frame_bytes;
        result.codegen_statistics.code_bytes += unit.codegen_statistics.code_bytes;
        result.codegen_statistics.native_vector_operation_count += unit.codegen_statistics.native_vector_operation_count;
        result.codegen_statistics.split_vector_operation_count += unit.codegen_statistics.split_vector_operation_count;
        result.codegen_statistics.vzeroupper_count += unit.codegen_statistics.vzeroupper_count;
        result.codegen_statistics.forwarded_wide_vector_load_count += unit.codegen_statistics.forwarded_wide_vector_load_count;
        result.codegen_statistics.simd_operation_count += unit.codegen_statistics.simd_operation_count;
        result.codegen_statistics.function_count += unit.codegen_statistics.function_count;
        result.codegen_statistics.maximum_stack_frame_bytes =
            BUSTER_MAX(result.codegen_statistics.maximum_stack_frame_bytes, unit.codegen_statistics.maximum_stack_frame_bytes);
        result.codegen_statistics.exact_attempts += unit.codegen_statistics.exact_attempts;
        result.codegen_statistics.exact_successes += unit.codegen_statistics.exact_successes;
        result.codegen_statistics.exact_failures += unit.codegen_statistics.exact_failures;
        if (unit.error != COMPILER_DRIVER_ERROR_NONE)
        {
            if (unit.diagnostic.length)
            {
                result.diagnostic = string_duplicate_arena(arena, unit.diagnostic, false);
            }
            result.error = unit.error;
            result.codegen_error = unit.codegen_error;
            result.object_error = unit.object_error;
            arena_destroy(unit_arena, 1);
            goto finish;
        }
        if (unit.has_object)
        {
            ObjectFile object = {
                .target = unit.object.target,
                .error = unit.object.error,
                .section_count = unit.object.section_count,
                .symbol_count = unit.object.symbol_count,
                .relocation_count = unit.object.relocation_count,
                .debug_module_count = unit.object.debug_module_count,
            };
            object.sections = arena_allocate(arena, ObjectSection, object.section_count);
            for (u32 section_index = 0; section_index < object.section_count; section_index += 1)
            {
                ObjectSection source = unit.object.sections[section_index];
                ObjectSection* destination = &object.sections[section_index];
                *destination = source;
                destination->name = string_duplicate_arena(arena, source.name, false);
                destination->data.pointer = arena_allocate(arena, u8, source.data.length);
                if (source.data.length)
                {
                    memcpy(destination->data.pointer, source.data.pointer, source.data.length);
                }
            }
            object.symbols = arena_allocate(arena, ObjectSymbol, object.symbol_count);
            for (u32 symbol_index = 0; symbol_index < object.symbol_count; symbol_index += 1)
            {
                object.symbols[symbol_index] = unit.object.symbols[symbol_index];
                object.symbols[symbol_index].name = string_duplicate_arena(arena, unit.object.symbols[symbol_index].name, false);
            }
            object.relocations = arena_allocate(arena, ObjectRelocation, object.relocation_count);
            if (object.relocation_count)
            {
                memcpy(object.relocations, unit.object.relocations, sizeof(ObjectRelocation) * object.relocation_count);
            }
            object.debug_modules = arena_allocate(arena, ObjectDebugModule, object.debug_module_count);
            for (u32 module_index = 0; module_index < object.debug_module_count; module_index += 1)
            {
                object.debug_modules[module_index] = unit.object.debug_modules[module_index];
                object.debug_modules[module_index].name = string_duplicate_arena(arena, unit.object.debug_modules[module_index].name, false);
            }
            // The initializer priorities are as much a part of the array
            // sections as their bytes are: the linker orders the whole
            // program's constructors by them, and a copy that dropped them
            // would leave a `constructor(101)` in this unit running after an
            // unprioritized one in another.
            for (u32 slot = 0; slot < 2; slot += 1)
            {
                u32 kind = slot ? OBJECT_SECTION_FINI_ARRAY : OBJECT_SECTION_INIT_ARRAY;
                u64 entries = unit.object.initializer_priorities[slot] && kind < unit.object.section_count
                                  ? unit.object.sections[kind].data.length / OBJECT_INITIALIZER_ENTRY_SIZE
                                  : 0;
                if (entries)
                {
                    object.initializer_priorities[slot] = arena_allocate(arena, u32, entries);
                    memcpy(object.initializer_priorities[slot], unit.object.initializer_priorities[slot], entries * sizeof(u32));
                }
            }
            objects[object_count++] = object;
        }
        if (unit.output.length)
        {
            preprocessed[input_index] = string_duplicate_arena(arena, unit.output, false);
        }
        arena_destroy(unit_arena, 1);
    }
    for (u32 library_index = 0; library_index < invocation.library_count; library_index += 1)
    {
        if (!static_libraries[library_index])
        {
            continue;
        }
        ObjectArchive* archive = &library_archives[library_index];
        bool* selected = arena_allocate(arena, bool, archive->object_count);
        memset(selected, 0, sizeof(*selected) * archive->object_count);
        bool added = false;
        do
        {
            added = false;
            for (u32 member_index = 0; member_index < archive->object_count; member_index += 1)
            {
                if (selected[member_index] || !compiler_driver_archive_member_needed(&archive->objects[member_index], objects, object_count))
                {
                    continue;
                }
                selected[member_index] = true;
                objects[object_count++] = archive->objects[member_index];
                added = true;
            }
        } while (added);
    }
    if (!invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK && compiler_driver_windows_runtime_object_target(invocation.target))
    {
        objects[object_count++] = link_windows_runtime_object(arena, invocation.target);
        // The UCRT exit-handler stubs are selected the way an archive member
        // is, unlike the `_fltused` marker above them: their undefined `_crt_`
        // reference would otherwise import into every image, and fail the link
        // where no ucrtbase.dll can be read.
        ObjectFile runtime = link_windows_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, objects, object_count))
        {
            objects[object_count++] = runtime;
        }
    }
    if (!invocation.emit_llvm_bitcode && invocation.action == COMPILER_DRIVER_ACTION_LINK && compiler_driver_elf_runtime_object_target(invocation.target))
    {
        // Selected the way an archive member is: only a program that
        // references one of its stubs and defines none of them pulls it in.
        ObjectFile runtime = link_elf_libc_runtime_object(arena, invocation.target);
        if (runtime.error == OBJECT_ERROR_NONE && compiler_driver_archive_member_needed(&runtime, objects, object_count))
        {
            objects[object_count++] = runtime;
        }
    }
    if (invocation.emit_llvm_bitcode)
    {
        goto finish;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        goto finish;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        goto finish;
    }
    if (invocation.action != COMPILER_DRIVER_ACTION_LINK)
    {
        goto finish;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = linked.symbol.length ? string_format(arena, S8("C object linking failed with error {u32} on symbol '{S8}'"), (u32)linked.error, linked.symbol)
                                                 : string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        goto finish;
    }
    result.object = linked.object;
    result.has_object = true;
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, static_libraries, &linked.object);
    if (dynamic_libraries.missing_request.length)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("cannot find -l{S8}"), dynamic_libraries.missing_request);
        compiler_driver_dynamic_libraries_release(&dynamic_libraries);
        goto finish;
    }
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = invocation.entry_symbol.length ? invocation.entry_symbol
                                                                                                   : compiler_driver_default_entry_symbol(invocation.target),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_data_symbols = dynamic_libraries.runtime.exported_data_symbols,
                                                    .runtime_versioned_symbols = dynamic_libraries.runtime.versioned_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_data_symbol_count = dynamic_libraries.runtime.exported_data_symbol_count,
                                                    .runtime_versioned_symbol_count = dynamic_libraries.runtime.versioned_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic =
            string_format(arena, S8("native C link failed with {S8}: {S8}"), link_error_name(result.native_link.error), result.native_link.symbol);
    }
finish:
    result.warning = compiler_driver_warning_flatten(warnings);
    return result;
}
