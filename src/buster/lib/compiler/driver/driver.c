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
// resolves import libraries for hosted links, and
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
    if (string_equal(dialect, S8("gnu11")))
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
    case COMPILER_DRIVER_C_DIALECT_GNU11:
        return C_PREPROCESS_DIALECT_GNU11;
    case COMPILER_DRIVER_C_DIALECT_GNU17:
        return C_PREPROCESS_DIALECT_GNU17;
    case COMPILER_DRIVER_C_DIALECT_GNU23:
        return C_PREPROCESS_DIALECT_GNU23;
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
    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1)
    {
        if (string_equal(sdk, values[value_index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_dxc_shader_model_is_valid(GpuTarget target)
{
    if (target.shader_model_major != 6 || target.shader_model_minor > 10)
    {
        return false;
    }
    if ((target.stage == GPU_SHADER_STAGE_MESH || target.stage == GPU_SHADER_STAGE_AMPLIFICATION) && target.shader_model_minor < 5)
    {
        return false;
    }
    if (target.stage >= GPU_SHADER_STAGE_LIBRARY && target.shader_model_minor == 0)
    {
        return false;
    }
    return true;
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
    invocation.input_paths = arena_allocate(arena, String8, arguments.length);
    invocation.include_paths = arena_allocate(arena, String8, arguments.length);
    invocation.system_include_paths = arena_allocate(arena, String8, arguments.length + 64);
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
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
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
                return invocation;
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
                return invocation;
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
                return invocation;
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
                return invocation;
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
            return invocation;
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
                return invocation;
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
                else if (string_equal(value, S8("none")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC;
                }
                else
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported language: {S8}"), value);
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")))
            {
                if (!compiler_driver_set_target(arena, &invocation, value))
                {
                    return invocation;
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
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-masm")))
            {
                if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
                {
                    return invocation;
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
                return invocation;
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
            return invocation;
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
                return invocation;
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
                return invocation;
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-masm=")))
        {
            value = string_slice(argument, S8("-masm=").length, argument.length);
            if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
            {
                return invocation;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-std="));
        if (value.length)
        {
            if (!compiler_driver_set_dialect(&invocation, value))
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported C dialect: {S8}"), value);
                return invocation;
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
        bool compatible_codegen_option =
            string_equal(argument, S8("-pipe")) || string_equal(argument, S8("-pthread")) || string_equal(argument, S8("-fPIC")) ||
            string_equal(argument, S8("-fpic")) || string_equal(argument, S8("-fPIE")) || string_equal(argument, S8("-fpie")) ||
            string_equal(argument, S8("-fno-pic")) || string_equal(argument, S8("-fno-pie")) || string_equal(argument, S8("-fno-builtin")) ||
            string_equal(argument, S8("-fwrapv")) || string_equal(argument, S8("-fno-strict-aliasing")) || string_equal(argument, S8("-funsigned-char")) ||
            string_equal(argument, S8("-fsigned-char")) || string_equal(argument, S8("-fcommon")) || string_equal(argument, S8("-fno-common"));
        if (optimization_option || debug_option || warning_option || compatible_codegen_option)
        {
            continue;
        }
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), argument);
        return invocation;
    }
    if (invocation.has_gpu_target)
    {
        if (invocation.emit_llvm_bitcode)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("-emit-llvm is not supported for external GPU target pipelines");
            return invocation;
        }
        GpuTargetKind gpu_kind = invocation.gpu_target.kind;
        bool spirv_target = compiler_driver_gpu_kind_is_spirv(gpu_kind);
        bool dxc_target = compiler_driver_gpu_target_uses_dxc(gpu_kind);
        bool llvm_gpu_target = spirv_target || gpu_kind == GPU_TARGET_NVPTX32 || gpu_kind == GPU_TARGET_NVPTX64 || gpu_kind == GPU_TARGET_AMDGCN;
        if (feature_override_count)
        {
            compiler_driver_argument_error(arena, &invocation, S8("-mattr is not supported for GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
        {
            compiler_driver_argument_error(arena, &invocation, S8("assembly syntax is not supported for GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.register_allocator_explicit)
        {
            compiler_driver_argument_error(arena, &invocation, S8("the native register allocator is not used by GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.c_dialect_explicit)
        {
            compiler_driver_argument_error(arena, &invocation, S8("the native C dialect option is not used by GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.source_metrics_path.length)
        {
            compiler_driver_argument_error(arena, &invocation, S8("source metrics are not supported for GPU target: {S8}"), invocation.source_metrics_path);
            return invocation;
        }
        if (invocation.language == COMPILER_DRIVER_LANGUAGE_C)
        {
            compiler_driver_argument_error(arena, &invocation, S8("native source language is incompatible with GPU target: {S8}"),
                                           S8("c"));
            return invocation;
        }
        if (invocation.library_path_count || invocation.library_count || invocation.framework_path_count || invocation.framework_count ||
            invocation.linker_argument_count)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("native libraries, frameworks, and linker arguments cannot be used in a GPU pipeline; pass backend options with -Xgpu");
            return invocation;
        }
        if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY && invocation.output_path.length)
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("cannot specify -o with -fsyntax-only for a GPU target");
            return invocation;
        }
        if (invocation.cuda_path.length && gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64)
        {
            compiler_driver_argument_error(arena, &invocation, S8("CUDA toolkit path is incompatible with GPU target: {S8}"), invocation.cuda_path);
            return invocation;
        }
        if (invocation.rocm_path.length && gpu_kind != GPU_TARGET_AMDGCN)
        {
            compiler_driver_argument_error(arena, &invocation, S8("ROCm path is incompatible with GPU target: {S8}"), invocation.rocm_path);
            return invocation;
        }
        if ((invocation.gpu_tools.spirv_link_path.length || invocation.gpu_tools.spirv_dis_path.length) && !spirv_target)
        {
            compiler_driver_argument_error(arena, &invocation, S8("SPIR-V tool override is incompatible with GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.gpu_tools.xcrun_path.length && gpu_kind != GPU_TARGET_METAL_AIR64)
        {
            compiler_driver_argument_error(arena, &invocation, S8("xcrun override is incompatible with GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.gpu_tools.dxc_path.length && !dxc_target)
        {
            compiler_driver_argument_error(arena, &invocation, S8("DXC override is incompatible with GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.gpu_tools.clang_path.length && !llvm_gpu_target)
        {
            compiler_driver_argument_error(arena, &invocation, S8("GPU Clang override is incompatible with GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.gpu_tools.llc_path.length && !llvm_gpu_target)
        {
            compiler_driver_argument_error(arena, &invocation, S8("GPU llc override is incompatible with GPU target: {S8}"),
                                           gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }

        if (invocation.gpu_architecture.length && architecture_option.length &&
            !string_equal(invocation.gpu_architecture, architecture_option))
        {
            compiler_driver_argument_error(arena, &invocation, S8("conflicting GPU architectures: {S8}"), invocation.gpu_architecture);
            return invocation;
        }
        String8 gpu_architecture = invocation.gpu_architecture.length ? invocation.gpu_architecture : architecture_option;
        if (gpu_architecture.length)
        {
            if (gpu_kind != GPU_TARGET_NVPTX32 && gpu_kind != GPU_TARGET_NVPTX64 && gpu_kind != GPU_TARGET_AMDGCN)
            {
                compiler_driver_argument_error(arena, &invocation, S8("GPU architecture is incompatible with target: {S8}"), gpu_architecture);
                return invocation;
            }
            invocation.gpu_target.architecture = gpu_architecture;
        }
        if (invocation.gpu_stage.length)
        {
            GpuShaderStage stage = gpu_shader_stage_from_string(invocation.gpu_stage);
            if (stage == GPU_SHADER_STAGE_NONE)
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported GPU shader stage: {S8}"), invocation.gpu_stage);
                return invocation;
            }
            if (gpu_kind == GPU_TARGET_METAL_AIR64 || (!dxc_target && stage != GPU_SHADER_STAGE_COMPUTE))
            {
                compiler_driver_argument_error(arena, &invocation, S8("shader stage is incompatible with GPU target: {S8}"), invocation.gpu_stage);
                return invocation;
            }
            invocation.gpu_target.stage = stage;
            if (dxc_target && !compiler_driver_gpu_stage_uses_entry_point(stage))
            {
                invocation.gpu_target.entry_point = (String8){0};
            }
        }
        if (invocation.gpu_shader_model.length)
        {
            if (!dxc_target ||
                !gpu_shader_model_parse(invocation.gpu_shader_model, &invocation.gpu_target.shader_model_major, &invocation.gpu_target.shader_model_minor))
            {
                compiler_driver_argument_error(arena, &invocation, S8("shader model is incompatible with GPU target: {S8}"), invocation.gpu_shader_model);
                return invocation;
            }
        }
        if (dxc_target && !compiler_driver_dxc_shader_model_is_valid(invocation.gpu_target))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported shader model for target stage: {S8}"),
                                           invocation.gpu_shader_model.length ? invocation.gpu_shader_model : gpu_target_to_string(arena, invocation.gpu_target));
            return invocation;
        }
        if (invocation.gpu_entry_point.length)
        {
            if (!dxc_target || !compiler_driver_gpu_stage_uses_entry_point(invocation.gpu_target.stage))
            {
                compiler_driver_argument_error(arena, &invocation, S8("GPU entry point is incompatible with target or stage: {S8}"),
                                               invocation.gpu_entry_point);
                return invocation;
            }
            invocation.gpu_target.entry_point = invocation.gpu_entry_point;
        }
        else if (dxc_target && compiler_driver_gpu_stage_uses_entry_point(invocation.gpu_target.stage) && !invocation.gpu_target.entry_point.length)
        {
            invocation.gpu_target.entry_point = S8("main");
        }
        if (invocation.metal_sdk.length)
        {
            if (gpu_kind != GPU_TARGET_METAL_AIR64 || !compiler_driver_metal_sdk_is_valid(invocation.metal_sdk))
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported Metal SDK for GPU target: {S8}"), invocation.metal_sdk);
                return invocation;
            }
            invocation.gpu_target.metal_sdk = invocation.metal_sdk;
        }
        bool has_hlsl_input = gpu_kind == GPU_TARGET_DXIL;
        bool has_cuda_input = false;
        for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
        {
            GpuSourceLanguage input_language = compiler_driver_gpu_effective_language(invocation, invocation.input_paths[input_index]);
            has_hlsl_input = has_hlsl_input || input_language == GPU_SOURCE_LANGUAGE_HLSL;
            has_cuda_input = has_cuda_input || input_language == GPU_SOURCE_LANGUAGE_CUDA;
        }
        if (gpu_kind == GPU_TARGET_SPIRV && !has_hlsl_input &&
            (invocation.gpu_entry_point.length || invocation.gpu_shader_model.length || invocation.gpu_tools.dxc_path.length ||
             invocation.gpu_target.stage != GPU_SHADER_STAGE_COMPUTE))
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("SPIR-V shader stage, entry point, shader model, and DXC options require HLSL input");
            return invocation;
        }
        if (invocation.cuda_path.length && !has_cuda_input)
        {
            compiler_driver_argument_error(arena, &invocation, S8("CUDA toolkit path requires CUDA input: {S8}"), invocation.cuda_path);
            return invocation;
        }
        if (has_hlsl_input && (invocation.sysroot.length || invocation.no_standard_includes))
        {
            invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
            invocation.diagnostic = S8("DXC HLSL pipelines do not support -isysroot/--sysroot or -nostdinc");
            return invocation;
        }
        if (!gpu_target_is_valid(invocation.gpu_target))
        {
            String8 target = gpu_target_to_string(arena, invocation.gpu_target);
            compiler_driver_argument_error(arena, &invocation, S8("incomplete GPU target configuration: {S8}"), target);
            return invocation;
        }
    }
    else
    {
        bool gpu_option = invocation.gpu_architecture.length || invocation.gpu_entry_point.length || invocation.gpu_stage.length ||
                          invocation.gpu_shader_model.length || invocation.metal_sdk.length || invocation.cuda_path.length || invocation.rocm_path.length ||
                          invocation.gpu_tools.clang_path.length || invocation.gpu_tools.llc_path.length || invocation.gpu_tools.spirv_link_path.length ||
                          invocation.gpu_tools.spirv_dis_path.length || invocation.gpu_tools.xcrun_path.length || invocation.gpu_tools.dxc_path.length ||
                          invocation.gpu_argument_count || invocation.save_gpu_temporaries || invocation.language > COMPILER_DRIVER_LANGUAGE_C;
        if (gpu_option)
        {
            compiler_driver_argument_error(arena, &invocation, S8("GPU option requires a GPU target: {S8}"), S8("use --target=spirv64, nvptx64-nvidia-cuda, amdgcn-amd-amdhsa, air64-apple-macos, or dxil"));
            return invocation;
        }
        if (architecture_option.length && !compiler_driver_set_cpu_model(arena, &invocation, architecture_option))
        {
            return invocation;
        }
        if (feature_override_count)
        {
            invocation.target.cpu_features = target_cpu_features_effective(invocation.target);
            invocation.target.cpu_features_explicit = true;
            for (u64 override_index = 0; override_index < feature_override_count; override_index += 1)
            {
                CompilerDriverFeatureOverride override = feature_overrides[override_index];
                TargetCpuFeature feature = target_cpu_feature_from_string(invocation.target.cpu_arch, override.name);
                if (feature == TARGET_CPU_FEATURE_NONE)
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported target feature: {S8}"), override.name);
                    return invocation;
                }
                if (override.enable)
                {
                    invocation.target.cpu_features = target_cpu_features_add(invocation.target.cpu_features, feature);
                }
                else
                {
                    invocation.target.cpu_features = target_cpu_features_remove(invocation.target.cpu_features, feature);
                }
            }
        }
        if (!target_cpu_features_are_valid(invocation.target))
        {
            if (feature_override_count)
            {
                compiler_driver_argument_error(arena, &invocation, S8("invalid target feature combination: {S8}"),
                                               target_cpu_features_to_string(arena, invocation.target));
            }
            else
            {
                compiler_driver_argument_error(arena, &invocation, S8("CPU model is incompatible with target: {S8}"),
                                               cpu_model_to_string_os(invocation.target.cpu_model));
            }
            return invocation;
        }
        if (invocation.target.cpu_arch != CPU_ARCH_X86_64 && invocation.assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
        {
            compiler_driver_argument_error(arena, &invocation, S8("assembly syntax is incompatible with target: {S8}"),
                                           invocation.assembly_syntax == ASSEMBLY_SYNTAX_ATT ? S8("att") : S8("intel"));
            return invocation;
        }
    }
    if (invocation.emit_llvm_bitcode &&
        (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
         invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY))
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("-emit-llvm emits binary bitcode and cannot be combined with -E, -S, or -fsyntax-only");
        return invocation;
    }
    if (!invocation.no_standard_includes && !invocation.has_gpu_target && invocation.target.os != OPERATING_SYSTEM_UEFI)
    {
#if defined(BUSTER_HOST_C_RESOURCE_INCLUDE)
        if (sizeof(BUSTER_HOST_C_RESOURCE_INCLUDE) > 1)
        {
            invocation.system_include_paths[invocation.system_include_path_count++] = S8(BUSTER_HOST_C_RESOURCE_INCLUDE);
        }
#endif
        if (invocation.sysroot.length)
        {
            invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/usr/local/include"), invocation.sysroot);
            if (invocation.target.os == OPERATING_SYSTEM_LINUX || invocation.target.os == OPERATING_SYSTEM_ANDROID)
            {
                String8 multiarch = invocation.target.cpu_arch == CPU_ARCH_AARCH64
                                        ? (invocation.target.os == OPERATING_SYSTEM_ANDROID ? S8("aarch64-linux-android") : S8("aarch64-linux-gnu"))
                                        : (invocation.target.os == OPERATING_SYSTEM_ANDROID ? S8("x86_64-linux-android") : S8("x86_64-linux-gnu"));
                invocation.system_include_paths[invocation.system_include_path_count++] =
                    string_format(arena, S8("{S8}/usr/include/{S8}"), invocation.sysroot, multiarch);
            }
            else if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
            {
                invocation.system_include_paths[invocation.system_include_path_count++] =
                    string_format(arena, S8("{S8}/x86_64-w64-mingw32/include"), invocation.sysroot);
                invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/include"), invocation.sysroot);
            }
            invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/usr/include"), invocation.sysroot);
        }
        else if (invocation.target.cpu_arch == target_native.cpu_arch && invocation.target.os == target_native.os)
        {
#if BUSTER_LINUX
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/local/include");
#if BUSTER_CPU_ARCH_X86_64
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include/x86_64-linux-gnu");
#else
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include/aarch64-linux-gnu");
#endif
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include");
#endif
#if BUSTER_WINDOWS
            String8 system_includes = os_get_environment_variable(S8("INCLUDE"));
            for (u64 start = 0; start < system_includes.length;)
            {
                u64 end = start;
                while (end < system_includes.length && system_includes.pointer[end] != ';')
                {
                    end += 1;
                }
                if (end != start)
                {
                    invocation.system_include_paths[invocation.system_include_path_count++] = string_slice(system_includes, start, end);
                }
                start = end + 1;
            }
#endif
        }
    }
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
    return invocation;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_c_input(CompilerDriverInvocation invocation, String8 path)
{
    if (invocation.language == COMPILER_DRIVER_LANGUAGE_C)
    {
        return true;
    }
    if (invocation.language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 2)
    {
        return false;
    }
    return path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'c' || path.pointer[path.length - 1] == 'i');
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
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u16(ByteSlice bytes, u64 offset, u16* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u32(ByteSlice bytes, u64 offset, u32* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
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
        path = string_format(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
    if (!bytes.pointer && invocation.sysroot.length)
    {
        path = string_format(arena, S8("{S8}/Windows/System32/{S8}"), invocation.sysroot, library->name);
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
            path = string_format(arena, S8("{S8}/System32/{S8}"), system_root, library->name);
            file_map_unmap(file);
            file = file_map_read(arena, path, (FileReadOptions){0});
            bytes = file.bytes;
        }
    }
#endif
    if (!bytes.pointer)
    {
        file_map_unmap(file);
        file = file_map_read(arena, library->name, (FileReadOptions){0});
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

BUSTER_GLOBAL_LOCAL void compiler_driver_dynamic_libraries_release(CompilerDriverDynamicLibraries* libraries)
{
    for (u32 index = 0; index < libraries->export_map_count; index += 1)
    {
        file_map_unmap(libraries->export_maps[index]);
    }
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation, bool* static_libraries)
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
    result.pointer = libraries;
    result.count = count;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_target_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation,
                                                                                              bool* static_libraries)
{
    if (invocation.target.os == OPERATING_SYSTEM_UEFI)
    {
        return (CompilerDriverDynamicLibraries){0};
    }
    return compiler_driver_dynamic_libraries(arena, invocation, static_libraries);
}

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

BUSTER_GLOBAL_LOCAL String8 compiler_driver_preprocess_text(Arena* arena, CPreprocessResult preprocess)
{
    u64 capacity = 1;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        if (preprocess.tokens[index].kind != C_TOKEN_END_OF_FILE)
        {
            capacity += c_token_length(preprocess.spelling_base, preprocess.tokens[index]) + 1;
        }
    }
    char8* text = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            break;
        }
        if (length)
        {
            text[length++] = ' ';
        }
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        memcpy(text + length, spelling.pointer, spelling.length);
        length += spelling.length;
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
    if (!aarch64 && target.cpu_arch != CPU_ARCH_X86_64)
    {
        return (String8){0};
    }
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
    result.source_lexed = preprocess.source_lexed;
    result.source_unique = preprocess.source_unique;
    result.preprocessed = preprocess.preprocessed;
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
                                                               .register_allocator = invocation.register_allocator,
                                                               .assembly_syntax = (u8)invocation.assembly_syntax,
                                                           });
    result.codegen_statistics = code.statistics;
    result.codegen_error = code.error;
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
        result.diagnostic =
            string_format(arena,
                          S8("C code generation failed with error {u32}, function {u32} ('{S8}', state {u32}, blocks {u32}, instructions {u32}), instruction "
                             "{u32}, opcode {u32}, operation {u32}, source {u32}:{u32}, referenced symbol '{S8}'"),
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
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = object_print_assembly(arena, &object);
        if (!result.output.length)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = S8("could not format native object as textual assembly");
            return result;
        }
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
    {
        if (suppress_object_write)
        {
            goto end;
        }
        ObjectArtifact artifact = object_write(arena, &object, object_format_for_target(invocation.target));
        if (artifact.error != OBJECT_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = artifact.error;
            goto end;
        }
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        if (!file_write(output, artifact.bytes))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), output);
        }
        goto end;
    }
    LinkObjectResult linked = link_objects(arena, &object, 1,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        goto end;
    }
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, 0);
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
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("native C link failed with error {u32}: {S8}"), (u32)result.native_link.error, result.native_link.symbol);
    }
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
        if (!object_input && !archive_input && !compiler_driver_c_input(invocation, path))
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
                result.diagnostic = string_format(arena, S8("could not read object {S8}: error {u32}"), input_path, (u32)object.error);
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
        result.diagnostic = string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        goto finish;
    }
    result.object = linked.object;
    result.has_object = true;
    String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_executable_path(invocation.target);
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_target_dynamic_libraries(arena, invocation, static_libraries);
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
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("native C link failed with error {u32}: {S8}"), (u32)result.native_link.error, result.native_link.symbol);
    }
finish:
    result.warning = compiler_driver_warning_flatten(warnings);
    return result;
}
