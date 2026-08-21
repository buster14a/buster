// External GPU shader-pipeline orchestration. The driver hands this module
// a shader compile request; gpu_target_parse classifies the target string
// (SPIR-V, NVPTX/PTX, AMDGCN/HSA, Metal AIR/metallib, DXIL), and the
// gpu_plan_* family builds a deterministic command plan — discovered
// external tools, temporary files this module owns and deletes, and
// expected artifacts — that execution then runs and validates. Everything
// external stays external: the plans invoke installed toolchains
// (clang/llc, spirv tools, metal, dxc) as subprocesses, adding no linked or
// vendored dependency to the executable, and a missing tool or malformed
// artifact is a structured error, not a crash.

#include <buster/lib/compiler/gpu/gpu.h>

#include <buster/lib/arena.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

typedef struct GpuStringComponents GpuStringComponents;
struct GpuStringComponents
{
    String8 values[8];
    u32 count;
    bool valid;
};

BUSTER_GLOBAL_LOCAL GpuStringComponents gpu_split_components(String8 value)
{
    GpuStringComponents result = {
        .valid = value.length != 0,
    };
    u64 start = 0;
    while (result.valid && start < value.length)
    {
        if (result.count >= BUSTER_ARRAY_LENGTH(result.values))
        {
            result.valid = false;
            break;
        }
        u64 end = start;
        while (end < value.length && value.pointer[end] != '-')
        {
            end += 1;
        }
        if (end == start)
        {
            result.valid = false;
            break;
        }
        result.values[result.count++] = string_slice(value, start, end);
        if (end == value.length)
        {
            break;
        }
        start = end + 1;
        if (start == value.length)
        {
            result.valid = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_string_is_one_of(String8 value, const String8* candidates, u32 candidate_count)
{
    bool result = false;
    for (u32 candidate_index = 0; candidate_index < candidate_count && !result; candidate_index += 1)
    {
        result = string_equal(value, candidates[candidate_index]);
    }

    return result;
}

GpuShaderStage gpu_shader_stage_from_string(String8 value)
{
    if (string_equal(value, S8("compute")) || string_equal(value, S8("cs"))) return GPU_SHADER_STAGE_COMPUTE;
    if (string_equal(value, S8("vertex")) || string_equal(value, S8("vs"))) return GPU_SHADER_STAGE_VERTEX;
    if (string_equal(value, S8("fragment")) || string_equal(value, S8("pixel")) || string_equal(value, S8("ps"))) return GPU_SHADER_STAGE_FRAGMENT;
    if (string_equal(value, S8("geometry")) || string_equal(value, S8("gs"))) return GPU_SHADER_STAGE_GEOMETRY;
    if (string_equal(value, S8("hull")) || string_equal(value, S8("hs"))) return GPU_SHADER_STAGE_HULL;
    if (string_equal(value, S8("domain")) || string_equal(value, S8("ds"))) return GPU_SHADER_STAGE_DOMAIN;
    if (string_equal(value, S8("mesh")) || string_equal(value, S8("ms"))) return GPU_SHADER_STAGE_MESH;
    if (string_equal(value, S8("amplification")) || string_equal(value, S8("task")) || string_equal(value, S8("as")))
        return GPU_SHADER_STAGE_AMPLIFICATION;
    if (string_equal(value, S8("library")) || string_equal(value, S8("lib"))) return GPU_SHADER_STAGE_LIBRARY;
    if (string_equal(value, S8("raygeneration")) || string_equal(value, S8("ray-generation")) || string_equal(value, S8("raygen")))
        return GPU_SHADER_STAGE_RAY_GENERATION;
    if (string_equal(value, S8("intersection"))) return GPU_SHADER_STAGE_INTERSECTION;
    if (string_equal(value, S8("anyhit")) || string_equal(value, S8("any-hit"))) return GPU_SHADER_STAGE_ANY_HIT;
    if (string_equal(value, S8("closesthit")) || string_equal(value, S8("closest-hit"))) return GPU_SHADER_STAGE_CLOSEST_HIT;
    if (string_equal(value, S8("miss"))) return GPU_SHADER_STAGE_MISS;
    if (string_equal(value, S8("callable"))) return GPU_SHADER_STAGE_CALLABLE;
    return GPU_SHADER_STAGE_NONE;
}

String8 gpu_shader_stage_to_string(GpuShaderStage stage)
{
    switch (stage)
    {
    case GPU_SHADER_STAGE_NONE: return S8("none");
    case GPU_SHADER_STAGE_COMPUTE: return S8("compute");
    case GPU_SHADER_STAGE_VERTEX: return S8("vertex");
    case GPU_SHADER_STAGE_FRAGMENT: return S8("pixel");
    case GPU_SHADER_STAGE_GEOMETRY: return S8("geometry");
    case GPU_SHADER_STAGE_HULL: return S8("hull");
    case GPU_SHADER_STAGE_DOMAIN: return S8("domain");
    case GPU_SHADER_STAGE_MESH: return S8("mesh");
    case GPU_SHADER_STAGE_AMPLIFICATION: return S8("amplification");
    case GPU_SHADER_STAGE_LIBRARY: return S8("library");
    case GPU_SHADER_STAGE_RAY_GENERATION: return S8("raygeneration");
    case GPU_SHADER_STAGE_INTERSECTION: return S8("intersection");
    case GPU_SHADER_STAGE_ANY_HIT: return S8("anyhit");
    case GPU_SHADER_STAGE_CLOSEST_HIT: return S8("closesthit");
    case GPU_SHADER_STAGE_MISS: return S8("miss");
    case GPU_SHADER_STAGE_CALLABLE: return S8("callable");
    case GPU_SHADER_STAGE_COUNT: break;
    }
    return S8("invalid");
}

BUSTER_GLOBAL_LOCAL bool gpu_dxc_stage_uses_entry_point(GpuShaderStage stage)
{
    return stage >= GPU_SHADER_STAGE_COMPUTE && stage <= GPU_SHADER_STAGE_AMPLIFICATION;
}

BUSTER_GLOBAL_LOCAL bool gpu_dxc_target_fields_are_valid(GpuTarget target)
{
    if (target.shader_model_major != 6 || target.shader_model_minor > 10 ||
        target.stage <= GPU_SHADER_STAGE_NONE || target.stage >= GPU_SHADER_STAGE_COUNT)
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
    return !gpu_dxc_stage_uses_entry_point(target.stage) || target.entry_point.length != 0;
}

BUSTER_GLOBAL_LOCAL bool gpu_parse_decimal_component(String8 value, u16* parsed)
{
    if (!value.length || value.length > 3)
    {
        return false;
    }
    u32 result = 0;
    for (u64 index = 0; index < value.length; index += 1)
    {
        if (!code_unit_is_decimal(value.pointer[index]))
        {
            return false;
        }
        result = result * 10 + (u32)(value.pointer[index] - '0');
    }
    if (result > UINT16_MAX)
    {
        return false;
    }
    *parsed = (u16)result;
    return true;
}

BUSTER_GLOBAL_LOCAL bool gpu_parse_shader_model(String8 value, u16* major, u16* minor)
{
    String8 prefix = S8("shadermodel");
    if (!string_starts_with_sequence(value, prefix))
    {
        return false;
    }
    String8 version = string_slice(value, prefix.length, value.length);
    u64 separator = string_first_code_unit(version, '.');
    if (separator >= version.length || separator == 0 || separator + 1 >= version.length)
    {
        return false;
    }
    return gpu_parse_decimal_component(string_slice(version, 0, separator), major) &&
           gpu_parse_decimal_component(string_slice(version, separator + 1, version.length), minor);
}

bool gpu_shader_model_parse(String8 value, u16* major, u16* minor)
{
    if (!major || !minor || !value.length)
    {
        return false;
    }
    String8 version = value;
    if (string_starts_with_sequence(version, S8("shadermodel")))
    {
        version = string_slice(version, S8("shadermodel").length, version.length);
    }
    u64 separator = string_first_code_unit(version, '.');
    if (separator >= version.length)
    {
        separator = string_first_code_unit(version, '_');
    }
    if (separator >= version.length || separator == 0 || separator + 1 >= version.length)
    {
        return false;
    }
    return gpu_parse_decimal_component(string_slice(version, 0, separator), major) &&
           gpu_parse_decimal_component(string_slice(version, separator + 1, version.length), minor);
}

BUSTER_GLOBAL_LOCAL String8 gpu_metal_sdk_from_os(String8 os)
{
    String8 result;
    if (string_equal(os, S8("macos")) || string_equal(os, S8("macosx")))
    {
        result = S8("macosx");
    }
    else if (string_equal(os, S8("ios")) || string_equal(os, S8("iphoneos")))
    {
        result = S8("iphoneos");
    }
    else if (string_equal(os, S8("iossimulator")) || string_equal(os, S8("iphonesimulator")))
    {
        result = S8("iphonesimulator");
    }
    else if (string_equal(os, S8("tvos")) || string_equal(os, S8("appletvos")))
    {
        result = S8("appletvos");
    }
    else if (string_equal(os, S8("tvossimulator")) || string_equal(os, S8("appletvsimulator")))
    {
        result = S8("appletvsimulator");
    }
    else if (string_equal(os, S8("visionos")) || string_equal(os, S8("xros")))
    {
        result = S8("xros");
    }
    else if (string_equal(os, S8("visionossimulator")) || string_equal(os, S8("xrsimulator")))
    {
        result = S8("xrsimulator");
    }
    else
    {
        result = (String8){0};
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_metal_sdk_is_valid(String8 sdk)
{
    String8 sdks[] = {
        S8("macosx"), S8("iphoneos"), S8("iphonesimulator"), S8("appletvos"), S8("appletvsimulator"), S8("xros"), S8("xrsimulator"),
    };
    return gpu_string_is_one_of(sdk, sdks, BUSTER_ARRAY_LENGTH(sdks));
}

BUSTER_GLOBAL_LOCAL bool gpu_spirv_architecture_parse(String8 architecture, GpuTargetKind* kind, u8* address_bits)
{
    String8 base = {0};
    GpuTargetKind parsed_kind = GPU_TARGET_NONE;
    u8 parsed_address_bits = 0;
    if (string_starts_with_sequence(architecture, S8("spirv64")))
    {
        base = S8("spirv64");
        parsed_kind = GPU_TARGET_SPIRV64;
        parsed_address_bits = 64;
    }
    else if (string_starts_with_sequence(architecture, S8("spirv32")))
    {
        base = S8("spirv32");
        parsed_kind = GPU_TARGET_SPIRV32;
        parsed_address_bits = 32;
    }
    else if (string_starts_with_sequence(architecture, S8("spirv")))
    {
        base = S8("spirv");
        parsed_kind = GPU_TARGET_SPIRV;
    }
    else
    {
        return false;
    }

    String8 version = string_slice(architecture, base.length, architecture.length);
    if (version.length)
    {
        // LLVM spells SPIR-V subarchitectures directly after the architecture,
        // for example spirv64v1.6.
        if (version.length != 4 || version.pointer[0] != 'v' || version.pointer[1] != '1' || version.pointer[2] != '.' ||
            version.pointer[3] < '0' || version.pointer[3] > '6')
        {
            return false;
        }
    }
    *kind = parsed_kind;
    *address_bits = parsed_address_bits;
    return true;
}

BUSTER_GLOBAL_LOCAL bool gpu_spirv_runtime_is_valid(String8 runtime)
{
    String8 runtimes[] = {S8("unknown"), S8("vulkan"), S8("vulkan1.2"), S8("vulkan1.3"), S8("amdhsa")};
    return gpu_string_is_one_of(runtime, runtimes, BUSTER_ARRAY_LENGTH(runtimes));
}

GpuTargetParseResult gpu_target_parse(String8 triple)
{
    GpuTargetParseResult result = {
        .error = GPU_TARGET_PARSE_ERROR_NOT_GPU,
    };
    if (triple.length)
    {
        if (string_equal(triple, S8("ptx"))) triple = S8("nvptx64");
        if (string_equal(triple, S8("amdgpu"))) triple = S8("amdgcn");
        if (string_equal(triple, S8("metal"))) triple = S8("air64");
        if (string_equal(triple, S8("dxil")))
        {
            result.target = (GpuTarget){
                .entry_point = S8("main"),
                .kind = GPU_TARGET_DXIL,
                .stage = GPU_SHADER_STAGE_COMPUTE,
                .shader_model_major = 6,
                .shader_model_minor = 6,
                .address_bits = 32,
            };
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }

        GpuStringComponents components = gpu_split_components(triple);
        if (!components.valid || !components.count)
        {
            return result;
        }
        String8 architecture = components.values[0];
        GpuTargetKind spirv_kind = GPU_TARGET_NONE;
        u8 spirv_address_bits = 0;
        bool spirv_prefix = string_starts_with_sequence(architecture, S8("spirv"));
        bool spirv_architecture = gpu_spirv_architecture_parse(architecture, &spirv_kind, &spirv_address_bits);
        bool recognized = spirv_prefix || string_equal(architecture, S8("nvptx")) || string_equal(architecture, S8("nvptx64")) ||
                          string_equal(architecture, S8("amdgcn")) || string_equal(architecture, S8("amdgpu")) ||
                          string_equal(architecture, S8("air64")) || string_equal(architecture, S8("metal")) ||
                          string_equal(architecture, S8("dxil"));
        if (!recognized)
        {
            return result;
        }
        result.error = GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE;
        result.invalid_component = triple;

        if (spirv_prefix)
        {
            if (!spirv_architecture || (components.count != 1 && components.count != 3 && components.count != 4))
            {
                result.invalid_component = architecture;
                return result;
            }
            if (components.count >= 3)
            {
                String8 vendors[] = {S8("unknown"), S8("amd")};
                if (!gpu_string_is_one_of(components.values[1], vendors, BUSTER_ARRAY_LENGTH(vendors)))
                {
                    result.invalid_component = components.values[1];
                    return result;
                }
                if (!gpu_spirv_runtime_is_valid(components.values[2]))
                {
                    result.invalid_component = components.values[2];
                    return result;
                }
                if (components.count == 4 && !string_equal(components.values[3], S8("unknown")))
                {
                    result.invalid_component = components.values[3];
                    return result;
                }
            }
            result.target = (GpuTarget){
                .entry_point = S8("main"),
                .backend_triple = triple,
                .kind = spirv_kind,
                .stage = GPU_SHADER_STAGE_COMPUTE,
                .shader_model_major = 6,
                .shader_model_minor = 6,
                .address_bits = spirv_address_bits,
            };
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }

        if (string_equal(architecture, S8("nvptx")) || string_equal(architecture, S8("nvptx64")))
        {
            if (components.count != 1 && components.count != 3)
            {
                return result;
            }
            if (components.count == 3 &&
                (!string_equal(components.values[1], S8("nvidia")) ||
                 (!string_equal(components.values[2], S8("cuda")) && !string_equal(components.values[2], S8("nvcl")))))
            {
                result.invalid_component = !string_equal(components.values[1], S8("nvidia")) ? components.values[1] : components.values[2];
                return result;
            }
            result.target = (GpuTarget){
                .kind = string_equal(architecture, S8("nvptx")) ? GPU_TARGET_NVPTX32 : GPU_TARGET_NVPTX64,
                .stage = GPU_SHADER_STAGE_COMPUTE,
                .address_bits = string_equal(architecture, S8("nvptx")) ? 32 : 64,
            };
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }

        if (string_equal(architecture, S8("amdgcn")) || string_equal(architecture, S8("amdgpu")))
        {
            if (components.count != 1 && components.count != 3)
            {
                return result;
            }
            if (components.count == 3 &&
                (!string_equal(components.values[1], S8("amd")) || !string_equal(components.values[2], S8("amdhsa"))))
            {
                result.invalid_component = !string_equal(components.values[1], S8("amd")) ? components.values[1] : components.values[2];
                return result;
            }
            result.target = (GpuTarget){
                .kind = GPU_TARGET_AMDGCN,
                .stage = GPU_SHADER_STAGE_COMPUTE,
                .address_bits = 64,
            };
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }

        if (string_equal(architecture, S8("air64")) || string_equal(architecture, S8("metal")))
        {
            if (components.count != 1 && components.count != 3)
            {
                return result;
            }
            String8 sdk = S8("macosx");
            if (components.count == 3)
            {
                sdk = gpu_metal_sdk_from_os(components.values[2]);
                if (!string_equal(components.values[1], S8("apple")) || !sdk.length)
                {
                    result.invalid_component = !string_equal(components.values[1], S8("apple")) ? components.values[1] : components.values[2];
                    return result;
                }
            }
            result.target = (GpuTarget){
                .metal_sdk = sdk,
                .kind = GPU_TARGET_METAL_AIR64,
                .address_bits = 64,
            };
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }

        if (string_equal(architecture, S8("dxil")))
        {
            if (components.count == 2)
            {
                GpuShaderStage stage = gpu_shader_stage_from_string(components.values[1]);
                if (stage == GPU_SHADER_STAGE_NONE)
                {
                    result.error = GPU_TARGET_PARSE_ERROR_STAGE;
                    result.invalid_component = components.values[1];
                    return result;
                }
                result.target = (GpuTarget){
                    .entry_point = stage >= GPU_SHADER_STAGE_COMPUTE && stage <= GPU_SHADER_STAGE_AMPLIFICATION ? S8("main") : (String8){0},
                    .kind = GPU_TARGET_DXIL,
                    .stage = stage,
                    .shader_model_major = 6,
                    .shader_model_minor = 6,
                    .address_bits = 32,
                };
                result.error = GPU_TARGET_PARSE_ERROR_NONE;
                return result;
            }
            if (components.count != 4 ||
                (!string_equal(components.values[1], S8("pc")) && !string_equal(components.values[1], S8("unknown"))))
            {
                if (components.count >= 2) result.invalid_component = components.values[1];
                return result;
            }
            u16 major = 0;
            u16 minor = 0;
            if (!gpu_parse_shader_model(components.values[2], &major, &minor))
            {
                result.error = GPU_TARGET_PARSE_ERROR_SHADER_MODEL;
                result.invalid_component = components.values[2];
                return result;
            }
            GpuShaderStage stage = gpu_shader_stage_from_string(components.values[3]);
            if (stage == GPU_SHADER_STAGE_NONE)
            {
                result.error = GPU_TARGET_PARSE_ERROR_STAGE;
                result.invalid_component = components.values[3];
                return result;
            }
            result.target = (GpuTarget){
                .entry_point = gpu_dxc_stage_uses_entry_point(stage) ? S8("main") : (String8){0},
                .kind = GPU_TARGET_DXIL,
                .stage = stage,
                .shader_model_major = major,
                .shader_model_minor = minor,
                .address_bits = 32,
            };
            if (!gpu_dxc_target_fields_are_valid(result.target))
            {
                result.error = GPU_TARGET_PARSE_ERROR_SHADER_MODEL;
                result.invalid_component = components.values[2];
                return result;
            }
            result.error = GPU_TARGET_PARSE_ERROR_NONE;
            return result;
        }
    }

    return result;
}

bool gpu_target_is_valid(GpuTarget target)
{
    if (target.kind > GPU_TARGET_NONE && target.kind < GPU_TARGET_COUNT)
    {
        switch (target.kind)
        {
        case GPU_TARGET_SPIRV:
        case GPU_TARGET_SPIRV32:
        case GPU_TARGET_SPIRV64:
        {
            if (!target.backend_triple.length)
            {
                return false;
            }
            GpuTargetParseResult parsed = gpu_target_parse(target.backend_triple);
            if (parsed.error != GPU_TARGET_PARSE_ERROR_NONE || parsed.target.kind != target.kind || parsed.target.address_bits != target.address_bits)
            {
                return false;
            }
            if (target.kind != GPU_TARGET_SPIRV && target.stage != GPU_SHADER_STAGE_COMPUTE)
            {
                return false;
            }
            return gpu_dxc_target_fields_are_valid(target);
        }
        case GPU_TARGET_NVPTX32: return target.address_bits == 32 && target.stage == GPU_SHADER_STAGE_COMPUTE;
        case GPU_TARGET_NVPTX64: return target.address_bits == 64 && target.stage == GPU_SHADER_STAGE_COMPUTE;
        case GPU_TARGET_AMDGCN:
            return target.address_bits == 64 && target.stage == GPU_SHADER_STAGE_COMPUTE && target.architecture.length != 0;
        case GPU_TARGET_METAL_AIR64:
            return target.address_bits == 64 && target.stage == GPU_SHADER_STAGE_NONE && gpu_metal_sdk_is_valid(target.metal_sdk);
        case GPU_TARGET_DXIL: return target.address_bits == 32 && gpu_dxc_target_fields_are_valid(target);
        case GPU_TARGET_NONE:
        case GPU_TARGET_COUNT: break;
        }
    }

    return false;
}

String8 gpu_target_to_string(Arena* arena, GpuTarget target)
{
    switch (target.kind)
    {
    case GPU_TARGET_SPIRV: return target.backend_triple.length ? target.backend_triple : S8("spirv");
    case GPU_TARGET_SPIRV32: return target.backend_triple.length ? target.backend_triple : S8("spirv32");
    case GPU_TARGET_SPIRV64: return target.backend_triple.length ? target.backend_triple : S8("spirv64");
    case GPU_TARGET_NVPTX32: return S8("nvptx-nvidia-cuda");
    case GPU_TARGET_NVPTX64: return S8("nvptx64-nvidia-cuda");
    case GPU_TARGET_AMDGCN: return S8("amdgcn-amd-amdhsa");
    case GPU_TARGET_METAL_AIR64:
    {
        String8 os = S8("macos");
        if (string_equal(target.metal_sdk, S8("iphoneos"))) os = S8("ios");
        else if (string_equal(target.metal_sdk, S8("iphonesimulator"))) os = S8("iossimulator");
        else if (string_equal(target.metal_sdk, S8("appletvos"))) os = S8("tvos");
        else if (string_equal(target.metal_sdk, S8("appletvsimulator"))) os = S8("tvossimulator");
        else if (string_equal(target.metal_sdk, S8("xros"))) os = S8("visionos");
        else if (string_equal(target.metal_sdk, S8("xrsimulator"))) os = S8("visionossimulator");
        return string_format(arena, S8("air64-apple-{S8}"), os);
    }
    case GPU_TARGET_DXIL:
        return string_format(arena, S8("dxil-pc-shadermodel{u32}.{u32}-{S8}"), (u32)target.shader_model_major,
                             (u32)target.shader_model_minor, gpu_shader_stage_to_string(target.stage));
    case GPU_TARGET_NONE:
    case GPU_TARGET_COUNT: break;
    }
    return S8("invalid-gpu-target");
}

GpuSourceLanguage gpu_source_language_from_path(String8 path)
{
    GpuSourceLanguage result;
    if (string_ends_with_sequence(path, S8(".cl")) || string_ends_with_sequence(path, S8(".ocl")))
    {
        result = GPU_SOURCE_LANGUAGE_OPENCL;
    }
    else if (string_ends_with_sequence(path, S8(".cu")))
    {
        result = GPU_SOURCE_LANGUAGE_CUDA;
    }
    else if (string_ends_with_sequence(path, S8(".hip")))
    {
        result = GPU_SOURCE_LANGUAGE_HIP;
    }
    else if (string_ends_with_sequence(path, S8(".metal")))
    {
        result = GPU_SOURCE_LANGUAGE_METAL;
    }
    else if (string_ends_with_sequence(path, S8(".hlsl")) || string_ends_with_sequence(path, S8(".fx")))
    {
        result = GPU_SOURCE_LANGUAGE_HLSL;
    }
    else if (string_ends_with_sequence(path, S8(".ll")) || string_ends_with_sequence(path, S8(".bc")))
    {
        result = GPU_SOURCE_LANGUAGE_LLVM_IR;
    }
    else if (string_ends_with_sequence(path, S8(".spv")))
    {
        result = GPU_SOURCE_LANGUAGE_SPIRV_BINARY;
    }
    else if (string_ends_with_sequence(path, S8(".air")))
    {
        result = GPU_SOURCE_LANGUAGE_METAL_AIR;
    }
    else
    {
        result = GPU_SOURCE_LANGUAGE_AUTOMATIC;
    }

    return result;
}

String8 gpu_source_language_to_string(GpuSourceLanguage language)
{
    switch (language)
    {
    case GPU_SOURCE_LANGUAGE_AUTOMATIC: return S8("automatic");
    case GPU_SOURCE_LANGUAGE_OPENCL: return S8("opencl");
    case GPU_SOURCE_LANGUAGE_CUDA: return S8("cuda");
    case GPU_SOURCE_LANGUAGE_HIP: return S8("hip");
    case GPU_SOURCE_LANGUAGE_METAL: return S8("metal");
    case GPU_SOURCE_LANGUAGE_HLSL: return S8("hlsl");
    case GPU_SOURCE_LANGUAGE_LLVM_IR: return S8("llvm-ir");
    case GPU_SOURCE_LANGUAGE_SPIRV_BINARY: return S8("spirv-binary");
    case GPU_SOURCE_LANGUAGE_METAL_AIR: return S8("metal-air");
    case GPU_SOURCE_LANGUAGE_COUNT: break;
    }
    return S8("invalid");
}

String8 gpu_output_format_to_string(GpuOutputFormat format)
{
    switch (format)
    {
    case GPU_OUTPUT_NONE: return S8("none");
    case GPU_OUTPUT_PREPROCESSED_SOURCE: return S8("preprocessed-source");
    case GPU_OUTPUT_SPIRV_BINARY: return S8("spirv");
    case GPU_OUTPUT_SPIRV_ASSEMBLY: return S8("spirv-assembly");
    case GPU_OUTPUT_CUDA_PTX: return S8("ptx");
    case GPU_OUTPUT_AMDGCN_OBJECT: return S8("amdgcn-object");
    case GPU_OUTPUT_AMDGCN_CODE_OBJECT: return S8("amdgcn-code-object");
    case GPU_OUTPUT_AMDGCN_ASSEMBLY: return S8("amdgcn-assembly");
    case GPU_OUTPUT_METAL_AIR: return S8("metal-air");
    case GPU_OUTPUT_METAL_LIBRARY: return S8("metallib");
    case GPU_OUTPUT_DXIL_CONTAINER: return S8("dxil");
    case GPU_OUTPUT_DXIL_ASSEMBLY: return S8("dxil-assembly");
    case GPU_OUTPUT_COUNT: break;
    }
    return S8("invalid");
}

typedef struct GpuArgumentBuilder GpuArgumentBuilder;
struct GpuArgumentBuilder
{
    String8* arguments;
    u32 count;
    u32 capacity;
    bool overflow;
};

typedef struct GpuPlanBuilder GpuPlanBuilder;
struct GpuPlanBuilder
{
    Arena* arena;
    GpuPipelineOptions options;
    GpuPipelinePlan plan;
    u32 step_capacity;
    u32 temporary_capacity;
    u32 temporary_serial;
};

BUSTER_GLOBAL_LOCAL String8 gpu_path_without_extension(String8 path);

BUSTER_GLOBAL_LOCAL GpuArgumentBuilder gpu_arguments_begin(Arena* arena, GpuPipelineOptions options, u32 additional_capacity)
{
    u64 capacity = 48 + (u64)options.include_path_count * 2 + (u64)options.system_include_path_count * 2 + options.definition_count +
                   options.undefinition_count + options.extra_argument_count + additional_capacity;
    if (capacity > UINT32_MAX)
    {
        return (GpuArgumentBuilder){.overflow = true};
    }
    return (GpuArgumentBuilder){
        .arguments = arena_allocate(arena, String8, capacity),
        .capacity = (u32)capacity,
    };
}

BUSTER_GLOBAL_LOCAL void gpu_argument_append(GpuArgumentBuilder* builder, String8 argument)
{
    if (builder->count >= builder->capacity)
    {
        builder->overflow = true;
        return;
    }
    builder->arguments[builder->count++] = argument;
}

BUSTER_GLOBAL_LOCAL void gpu_plan_error(GpuPlanBuilder* builder, GpuPipelineError error, String8 diagnostic)
{
    if (builder->plan.error == GPU_PIPELINE_ERROR_NONE)
    {
        builder->plan.error = error;
        builder->plan.diagnostic = diagnostic;
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_add_temporary(GpuPlanBuilder* builder, String8 path)
{
    if (builder->plan.temporary_path_count >= builder->temporary_capacity)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU pipeline generated too many temporary paths"));
        return;
    }
    builder->plan.temporary_paths[builder->plan.temporary_path_count++] = path;
}

BUSTER_GLOBAL_LOCAL String8 gpu_plan_temporary_path(GpuPlanBuilder* builder, String8 suffix)
{
    String8 base = builder->plan.output_path;
    if (!base.length && builder->options.input_paths && builder->options.input_count)
    {
        base = gpu_path_without_extension(builder->options.input_paths[0]);
    }
    if (!base.length)
    {
        base = S8("buster-gpu");
    }
    String8 path = string_format_z(builder->arena, S8("{S8}.buster-gpu-{u32}{S8}"), base, builder->temporary_serial++, suffix);
    gpu_plan_add_temporary(builder, path);
    return path;
}

BUSTER_GLOBAL_LOCAL void gpu_plan_process(GpuPlanBuilder* builder, GpuArgumentBuilder arguments, String8 output_path)
{
    if (arguments.overflow || !arguments.count)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU command argument capacity was exceeded"));
        return;
    }
    if (builder->plan.step_count >= builder->step_capacity)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU pipeline generated too many process steps"));
        return;
    }
    builder->plan.steps[builder->plan.step_count++] = (GpuPipelineStep){
        .arguments = {
            .pointer = arguments.arguments,
            .length = arguments.count,
        },
        .output_path = output_path,
        .kind = GPU_PIPELINE_STEP_PROCESS,
    };
}

BUSTER_GLOBAL_LOCAL void gpu_plan_copy(GpuPlanBuilder* builder, String8 source, String8 output)
{
    if (builder->plan.step_count >= builder->step_capacity)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU pipeline generated too many copy steps"));
        return;
    }
    builder->plan.steps[builder->plan.step_count++] = (GpuPipelineStep){
        .copy_source = source,
        .output_path = output,
        .kind = GPU_PIPELINE_STEP_COPY,
    };
}

BUSTER_GLOBAL_LOCAL String8 gpu_tool_path(String8 explicit_path, String8 environment_name, String8 fallback)
{
    String8 result;
    if (explicit_path.length)
    {
        result = explicit_path;
    }
    else
    {
        String8 environment = os_get_environment_variable(environment_name);
        result = environment.length ? environment : fallback;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL String8 gpu_clang_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.clang_path, S8("BUSTER_GPU_CLANG"), S8("clang"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_llc_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.llc_path, S8("BUSTER_GPU_LLC"), S8("llc"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_spirv_link_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.spirv_link_path, S8("BUSTER_SPIRV_LINK"), S8("spirv-link"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_spirv_dis_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.spirv_dis_path, S8("BUSTER_SPIRV_DIS"), S8("spirv-dis"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_xcrun_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.xcrun_path, S8("BUSTER_XCRUN"), S8("xcrun"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_dxc_path(GpuPipelineOptions options)
{
    return gpu_tool_path(options.tools.dxc_path, S8("BUSTER_DXC"), S8("dxc"));
}

BUSTER_GLOBAL_LOCAL String8 gpu_path_without_extension(String8 path)
{
    u64 component_start = 0;
    for (u64 index = path.length; index > 0; index -= 1)
    {
        char8 code_unit = path.pointer[index - 1];
        if (code_unit == '/' || code_unit == '\\')
        {
            component_start = index;
            break;
        }
    }
    for (u64 index = path.length; index > component_start; index -= 1)
    {
        if (path.pointer[index - 1] == '.' && index - 1 > component_start)
        {
            return string_slice(path, 0, index - 1);
        }
    }
    return path;
}

BUSTER_GLOBAL_LOCAL String8 gpu_output_suffix(GpuOutputFormat format)
{
    switch (format)
    {
    case GPU_OUTPUT_PREPROCESSED_SOURCE: return S8(".i");
    case GPU_OUTPUT_SPIRV_BINARY: return S8(".spv");
    case GPU_OUTPUT_SPIRV_ASSEMBLY: return S8(".spvasm");
    case GPU_OUTPUT_CUDA_PTX: return S8(".ptx");
    case GPU_OUTPUT_AMDGCN_OBJECT: return S8(".o");
    case GPU_OUTPUT_AMDGCN_CODE_OBJECT: return S8(".hsaco");
    case GPU_OUTPUT_AMDGCN_ASSEMBLY: return S8(".s");
    case GPU_OUTPUT_METAL_AIR: return S8(".air");
    case GPU_OUTPUT_METAL_LIBRARY: return S8(".metallib");
    case GPU_OUTPUT_DXIL_CONTAINER: return S8(".dxil");
    case GPU_OUTPUT_DXIL_ASSEMBLY: return S8(".dxil.txt");
    case GPU_OUTPUT_NONE:
    case GPU_OUTPUT_COUNT: break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 gpu_default_output_path(Arena* arena, GpuPipelineOptions options, GpuOutputFormat format)
{
    String8 result;
    if (options.output_path.length || format == GPU_OUTPUT_NONE)
    {
        result = options.output_path;
    }
    else
    {
        String8 suffix = gpu_output_suffix(format);
        String8 base = gpu_path_without_extension(options.input_paths[0]);
        String8 output = string_format_z(arena, S8("{S8}{S8}"), base, suffix);
        if (string_equal(output, options.input_paths[0]))
        {
            output = string_format_z(arena, S8("{S8}.out{S8}"), base, suffix);
        }
        result = output;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_output_format_is_text(GpuOutputFormat format)
{
    return format == GPU_OUTPUT_PREPROCESSED_SOURCE || format == GPU_OUTPUT_SPIRV_ASSEMBLY || format == GPU_OUTPUT_CUDA_PTX ||
           format == GPU_OUTPUT_AMDGCN_ASSEMBLY || format == GPU_OUTPUT_DXIL_ASSEMBLY;
}

BUSTER_GLOBAL_LOCAL String8 gpu_plan_output_path(GpuPlanBuilder* builder, GpuOutputFormat format)
{
    if (builder->options.capture_text_output && !builder->options.output_path.length && gpu_output_format_is_text(format))
    {
        String8 base = gpu_path_without_extension(builder->options.input_paths[0]);
        String8 path = string_format_z(builder->arena, S8("{S8}.buster-gpu-output-{u32}{S8}"), base, builder->temporary_serial++,
                                       gpu_output_suffix(format));
        gpu_plan_add_temporary(builder, path);
        builder->plan.output_is_temporary = true;
        return path;
    }
    return gpu_default_output_path(builder->arena, builder->options, format);
}

BUSTER_GLOBAL_LOCAL void gpu_append_optimization(GpuArgumentBuilder* arguments, Arena* arena, u8 level)
{
    gpu_argument_append(arguments, string_format(arena, S8("-O{u32}"), (u32)BUSTER_MIN(level, (u8)3)));
}

BUSTER_GLOBAL_LOCAL void gpu_append_clang_common(GpuArgumentBuilder* arguments, Arena* arena, GpuPipelineOptions options)
{
    gpu_append_optimization(arguments, arena, options.optimization_level);
    if (options.debug_info)
    {
        gpu_argument_append(arguments, S8("-g"));
    }
    if (options.no_standard_includes)
    {
        gpu_argument_append(arguments, S8("-nostdinc"));
    }
    if (options.sysroot.length)
    {
        gpu_argument_append(arguments, string_format(arena, S8("--sysroot={S8}"), options.sysroot));
    }
    for (u32 include_index = 0; include_index < options.include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-I"));
        gpu_argument_append(arguments, options.include_paths[include_index]);
    }
    for (u32 include_index = 0; include_index < options.system_include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-isystem"));
        gpu_argument_append(arguments, options.system_include_paths[include_index]);
    }
    for (u32 definition_index = 0; definition_index < options.definition_count; definition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-D{S8}"), options.definitions[definition_index]));
    }
    for (u32 undefinition_index = 0; undefinition_index < options.undefinition_count; undefinition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-U{S8}"), options.undefinitions[undefinition_index]));
    }
    for (u32 argument_index = 0; argument_index < options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(arguments, options.extra_arguments[argument_index]);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_append_clang_language(GpuArgumentBuilder* arguments, GpuSourceLanguage language)
{
    switch (language)
    {
    case GPU_SOURCE_LANGUAGE_OPENCL:
        gpu_argument_append(arguments, S8("-x"));
        gpu_argument_append(arguments, S8("cl"));
        break;
    case GPU_SOURCE_LANGUAGE_CUDA:
        gpu_argument_append(arguments, S8("-x"));
        gpu_argument_append(arguments, S8("cuda"));
        break;
    case GPU_SOURCE_LANGUAGE_HIP:
        gpu_argument_append(arguments, S8("-x"));
        gpu_argument_append(arguments, S8("hip"));
        break;
    case GPU_SOURCE_LANGUAGE_LLVM_IR:
        gpu_argument_append(arguments, S8("-x"));
        gpu_argument_append(arguments, S8("ir"));
        break;
    case GPU_SOURCE_LANGUAGE_AUTOMATIC:
    case GPU_SOURCE_LANGUAGE_METAL:
    case GPU_SOURCE_LANGUAGE_HLSL:
    case GPU_SOURCE_LANGUAGE_SPIRV_BINARY:
    case GPU_SOURCE_LANGUAGE_METAL_AIR:
    case GPU_SOURCE_LANGUAGE_COUNT: break;
    }
}

BUSTER_GLOBAL_LOCAL GpuSourceLanguage gpu_effective_language(GpuPipelineOptions options, String8 path)
{
    return options.language == GPU_SOURCE_LANGUAGE_AUTOMATIC ? gpu_source_language_from_path(path) : options.language;
}

BUSTER_GLOBAL_LOCAL bool gpu_target_kind_is_spirv(GpuTargetKind target)
{
    return target == GPU_TARGET_SPIRV || target == GPU_TARGET_SPIRV32 || target == GPU_TARGET_SPIRV64;
}

BUSTER_GLOBAL_LOCAL bool gpu_target_accepts_language(GpuTargetKind target, GpuSourceLanguage language)
{
    switch (target)
    {
    case GPU_TARGET_SPIRV:
        return language == GPU_SOURCE_LANGUAGE_OPENCL || language == GPU_SOURCE_LANGUAGE_HLSL || language == GPU_SOURCE_LANGUAGE_LLVM_IR ||
               language == GPU_SOURCE_LANGUAGE_SPIRV_BINARY;
    case GPU_TARGET_SPIRV32:
    case GPU_TARGET_SPIRV64:
        return language == GPU_SOURCE_LANGUAGE_OPENCL || language == GPU_SOURCE_LANGUAGE_LLVM_IR || language == GPU_SOURCE_LANGUAGE_SPIRV_BINARY;
    case GPU_TARGET_NVPTX32:
    case GPU_TARGET_NVPTX64:
        return language == GPU_SOURCE_LANGUAGE_CUDA || language == GPU_SOURCE_LANGUAGE_OPENCL || language == GPU_SOURCE_LANGUAGE_LLVM_IR;
    case GPU_TARGET_AMDGCN:
        return language == GPU_SOURCE_LANGUAGE_HIP || language == GPU_SOURCE_LANGUAGE_OPENCL || language == GPU_SOURCE_LANGUAGE_LLVM_IR;
    case GPU_TARGET_METAL_AIR64:
        return language == GPU_SOURCE_LANGUAGE_METAL || language == GPU_SOURCE_LANGUAGE_METAL_AIR;
    case GPU_TARGET_DXIL: return language == GPU_SOURCE_LANGUAGE_HLSL;
    case GPU_TARGET_NONE:
    case GPU_TARGET_COUNT: break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 gpu_spirv_backend_triple(GpuTarget target)
{
    if (target.backend_triple.length)
    {
        return target.backend_triple;
    }
    switch (target.kind)
    {
    case GPU_TARGET_SPIRV: return S8("spirv");
    case GPU_TARGET_SPIRV32: return S8("spirv32");
    case GPU_TARGET_SPIRV64: return S8("spirv64");
    case GPU_TARGET_NONE:
    case GPU_TARGET_NVPTX32:
    case GPU_TARGET_NVPTX64:
    case GPU_TARGET_AMDGCN:
    case GPU_TARGET_METAL_AIR64:
    case GPU_TARGET_DXIL:
    case GPU_TARGET_COUNT: break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 gpu_spirv_dxc_environment(GpuTarget target, bool* supported)
{
    *supported = true;
    GpuStringComponents components = gpu_split_components(gpu_spirv_backend_triple(target));
    if (components.valid && components.count >= 3 && !string_equal(components.values[2], S8("unknown")))
    {
        if (string_starts_with_sequence(components.values[2], S8("vulkan")))
        {
            return components.values[2];
        }
        *supported = false;
    }

    return (String8){0};
}

BUSTER_GLOBAL_LOCAL String8 gpu_nvptx_triple(GpuTargetKind target)
{
    return target == GPU_TARGET_NVPTX32 ? S8("nvptx-nvidia-cuda") : S8("nvptx64-nvidia-cuda");
}

// Shared by the DXIL and HLSL-to-SPIR-V paths. Definitions live next to the
// DXIL planner below.
BUSTER_GLOBAL_LOCAL String8 gpu_dxc_profile(Arena* arena, GpuTarget target);
BUSTER_GLOBAL_LOCAL void gpu_append_dxc_common(GpuArgumentBuilder* arguments, Arena* arena, GpuPipelineOptions options);
BUSTER_GLOBAL_LOCAL void gpu_append_dxc_target(GpuArgumentBuilder* arguments, Arena* arena, GpuTarget target);

BUSTER_GLOBAL_LOCAL void gpu_plan_spirv_opencl(GpuPlanBuilder* builder, String8 input, String8 output, bool syntax_only, bool preprocess)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
    gpu_argument_append(&arguments, gpu_clang_path(builder->options));
    gpu_argument_append(&arguments, string_format(builder->arena, S8("--target={S8}"), gpu_spirv_backend_triple(builder->options.target)));
    gpu_append_clang_common(&arguments, builder->arena, builder->options);
    gpu_append_clang_language(&arguments, GPU_SOURCE_LANGUAGE_OPENCL);
    if (syntax_only)
    {
        gpu_argument_append(&arguments, S8("-fsyntax-only"));
    }
    else if (preprocess)
    {
        gpu_argument_append(&arguments, S8("-E"));
    }
    else
    {
        gpu_argument_append(&arguments, S8("-c"));
    }
    gpu_argument_append(&arguments, input);
    if (output.length)
    {
        gpu_argument_append(&arguments, S8("-o"));
        gpu_argument_append(&arguments, output);
    }
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_spirv_llvm_ir(GpuPlanBuilder* builder, String8 input, String8 output)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
    gpu_argument_append(&arguments, gpu_llc_path(builder->options));
    gpu_argument_append(&arguments, string_format(builder->arena, S8("-mtriple={S8}"), gpu_spirv_backend_triple(builder->options.target)));
    gpu_argument_append(&arguments, S8("-filetype=obj"));
    gpu_append_optimization(&arguments, builder->arena, builder->options.optimization_level);
    if (builder->options.debug_info)
    {
        gpu_argument_append(&arguments, S8("-g"));
    }
    for (u32 argument_index = 0; argument_index < builder->options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(&arguments, builder->options.extra_arguments[argument_index]);
    }
    gpu_argument_append(&arguments, input);
    gpu_argument_append(&arguments, S8("-o"));
    gpu_argument_append(&arguments, output);
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_spirv_hlsl(GpuPlanBuilder* builder, String8 input, String8 output, bool preprocess)
{
    bool environment_supported = false;
    String8 environment = gpu_spirv_dxc_environment(builder->options.target, &environment_supported);
    if (!environment_supported)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_TARGET, S8("DXC HLSL-to-SPIR-V supports generic or Vulkan SPIR-V targets"));
        return;
    }
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 24);
    gpu_argument_append(&arguments, gpu_dxc_path(builder->options));
    gpu_append_dxc_common(&arguments, builder->arena, builder->options);
    gpu_argument_append(&arguments, S8("-spirv"));
    if (environment.length)
    {
        gpu_argument_append(&arguments, string_format(builder->arena, S8("-fspv-target-env={S8}"), environment));
    }
    gpu_append_dxc_target(&arguments, builder->arena, builder->options.target);
    if (preprocess)
    {
        gpu_argument_append(&arguments, S8("-P"));
        gpu_argument_append(&arguments, output);
    }
    else
    {
        gpu_argument_append(&arguments, S8("-Fo"));
        gpu_argument_append(&arguments, output);
    }
    gpu_argument_append(&arguments, input);
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_spirv_compile(GpuPlanBuilder* builder, String8 input, GpuSourceLanguage language, String8 output)
{
    switch (language)
    {
    case GPU_SOURCE_LANGUAGE_OPENCL: gpu_plan_spirv_opencl(builder, input, output, false, false); break;
    case GPU_SOURCE_LANGUAGE_HLSL: gpu_plan_spirv_hlsl(builder, input, output, false); break;
    case GPU_SOURCE_LANGUAGE_LLVM_IR: gpu_plan_spirv_llvm_ir(builder, input, output); break;
    case GPU_SOURCE_LANGUAGE_AUTOMATIC:
    case GPU_SOURCE_LANGUAGE_CUDA:
    case GPU_SOURCE_LANGUAGE_HIP:
    case GPU_SOURCE_LANGUAGE_METAL:
    case GPU_SOURCE_LANGUAGE_SPIRV_BINARY:
    case GPU_SOURCE_LANGUAGE_METAL_AIR:
    case GPU_SOURCE_LANGUAGE_COUNT:
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("invalid source language in SPIR-V compile step"));
        break;
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_spirv(GpuPlanBuilder* builder, GpuSourceLanguage* languages)
{
    if (builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS)
    {
        if (builder->options.input_count != 1 ||
            (languages[0] != GPU_SOURCE_LANGUAGE_OPENCL && languages[0] != GPU_SOURCE_LANGUAGE_HLSL))
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("SPIR-V preprocessing requires exactly one OpenCL or HLSL source"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_PREPROCESSED_SOURCE;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        if (languages[0] == GPU_SOURCE_LANGUAGE_HLSL)
        {
            gpu_plan_spirv_hlsl(builder, builder->options.input_paths[0], builder->plan.output_path, true);
        }
        else
        {
            gpu_plan_spirv_opencl(builder, builder->options.input_paths[0], builder->plan.output_path, false, true);
        }
        return;
    }
    if (builder->options.action == GPU_PIPELINE_ACTION_SYNTAX_ONLY)
    {
        builder->plan.output_format = GPU_OUTPUT_NONE;
        for (u32 input_index = 0; input_index < builder->options.input_count; input_index += 1)
        {
            if (languages[input_index] == GPU_SOURCE_LANGUAGE_SPIRV_BINARY)
            {
                gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("-fsyntax-only does not accept precompiled SPIR-V binaries"));
                return;
            }
            if (languages[input_index] == GPU_SOURCE_LANGUAGE_LLVM_IR)
            {
                gpu_plan_error(builder, GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION,
                               S8("SPIR-V LLVM IR validation requires llvm-as, which is not part of this pipeline"));
                return;
            }
            if (languages[input_index] == GPU_SOURCE_LANGUAGE_HLSL)
            {
                String8 temporary = gpu_plan_temporary_path(builder, S8(".spv"));
                gpu_plan_spirv_hlsl(builder, builder->options.input_paths[input_index], temporary, false);
            }
            else
            {
                gpu_plan_spirv_opencl(builder, builder->options.input_paths[input_index], (String8){0}, true, false);
            }
        }
        return;
    }

    bool assembly = builder->options.action == GPU_PIPELINE_ACTION_ASSEMBLY;
    builder->plan.output_format = assembly ? GPU_OUTPUT_SPIRV_ASSEMBLY : GPU_OUTPUT_SPIRV_BINARY;
    builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
    String8* modules = arena_allocate(builder->arena, String8, builder->options.input_count);
    for (u32 input_index = 0; input_index < builder->options.input_count; input_index += 1)
    {
        if (languages[input_index] == GPU_SOURCE_LANGUAGE_SPIRV_BINARY)
        {
            modules[input_index] = builder->options.input_paths[input_index];
            continue;
        }
        bool direct_binary = builder->options.input_count == 1 && !assembly;
        modules[input_index] = direct_binary ? builder->plan.output_path : gpu_plan_temporary_path(builder, S8(".spv"));
        gpu_plan_spirv_compile(builder, builder->options.input_paths[input_index], languages[input_index], modules[input_index]);
    }
    if (builder->plan.error != GPU_PIPELINE_ERROR_NONE)
    {
        return;
    }

    String8 binary = modules[0];
    if (builder->options.input_count > 1)
    {
        binary = assembly ? gpu_plan_temporary_path(builder, S8(".spv")) : builder->plan.output_path;
        GpuArgumentBuilder link = gpu_arguments_begin(builder->arena, builder->options, builder->options.input_count + 8);
        gpu_argument_append(&link, gpu_spirv_link_path(builder->options));
        for (u32 input_index = 0; input_index < builder->options.input_count; input_index += 1)
        {
            gpu_argument_append(&link, modules[input_index]);
        }
        gpu_argument_append(&link, S8("-o"));
        gpu_argument_append(&link, binary);
        gpu_plan_process(builder, link, binary);
    }
    else if (!assembly && languages[0] == GPU_SOURCE_LANGUAGE_SPIRV_BINARY)
    {
        gpu_plan_copy(builder, modules[0], builder->plan.output_path);
        binary = builder->plan.output_path;
    }

    if (assembly)
    {
        GpuArgumentBuilder disassemble = gpu_arguments_begin(builder->arena, builder->options, 8);
        gpu_argument_append(&disassemble, gpu_spirv_dis_path(builder->options));
        gpu_argument_append(&disassemble, binary);
        gpu_argument_append(&disassemble, S8("-o"));
        gpu_argument_append(&disassemble, builder->plan.output_path);
        gpu_plan_process(builder, disassemble, builder->plan.output_path);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_nvptx_clang(GpuPlanBuilder* builder, String8 input, GpuSourceLanguage language, String8 output,
                                               bool syntax_only, bool preprocess)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 20);
    gpu_argument_append(&arguments, gpu_clang_path(builder->options));
    if (language == GPU_SOURCE_LANGUAGE_CUDA)
    {
        gpu_argument_append(&arguments, S8("--cuda-device-only"));
        if (builder->options.target.architecture.length)
        {
            gpu_argument_append(&arguments, string_format(builder->arena, S8("--cuda-gpu-arch={S8}"), builder->options.target.architecture));
        }
        if (builder->options.cuda_path.length)
        {
            gpu_argument_append(&arguments, string_format(builder->arena, S8("--cuda-path={S8}"), builder->options.cuda_path));
        }
    }
    else
    {
        gpu_argument_append(&arguments, string_format(builder->arena, S8("--target={S8}"), gpu_nvptx_triple(builder->options.target.kind)));
        if (builder->options.target.architecture.length)
        {
            gpu_argument_append(&arguments, string_format(builder->arena, S8("-mcpu={S8}"), builder->options.target.architecture));
        }
    }
    gpu_append_clang_common(&arguments, builder->arena, builder->options);
    gpu_append_clang_language(&arguments, language);
    if (syntax_only)
    {
        gpu_argument_append(&arguments, S8("-fsyntax-only"));
    }
    else if (preprocess)
    {
        gpu_argument_append(&arguments, S8("-E"));
    }
    else
    {
        gpu_argument_append(&arguments, S8("-S"));
    }
    gpu_argument_append(&arguments, input);
    if (output.length)
    {
        gpu_argument_append(&arguments, S8("-o"));
        gpu_argument_append(&arguments, output);
    }
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_nvptx(GpuPlanBuilder* builder, GpuSourceLanguage language)
{
    String8 input = builder->options.input_paths[0];
    if (builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS)
    {
        if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("PTX preprocessing requires CUDA or OpenCL source"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_PREPROCESSED_SOURCE;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        gpu_plan_nvptx_clang(builder, input, language, builder->plan.output_path, false, true);
        return;
    }
    if (builder->options.action == GPU_PIPELINE_ACTION_SYNTAX_ONLY)
    {
        if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION, S8("PTX LLVM IR validation requires llvm-as, which is not part of this pipeline"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_NONE;
        gpu_plan_nvptx_clang(builder, input, language, (String8){0}, true, false);
        return;
    }

    builder->plan.output_format = GPU_OUTPUT_CUDA_PTX;
    builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
    if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
    {
        GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
        gpu_argument_append(&arguments, gpu_llc_path(builder->options));
        gpu_argument_append(&arguments, string_format(builder->arena, S8("-mtriple={S8}"), gpu_nvptx_triple(builder->options.target.kind)));
        if (builder->options.target.architecture.length)
        {
            gpu_argument_append(&arguments, string_format(builder->arena, S8("-mcpu={S8}"), builder->options.target.architecture));
        }
        gpu_argument_append(&arguments, S8("-filetype=asm"));
        gpu_append_optimization(&arguments, builder->arena, builder->options.optimization_level);
        if (builder->options.debug_info)
        {
            gpu_argument_append(&arguments, S8("-g"));
        }
        for (u32 argument_index = 0; argument_index < builder->options.extra_argument_count; argument_index += 1)
        {
            gpu_argument_append(&arguments, builder->options.extra_arguments[argument_index]);
        }
        gpu_argument_append(&arguments, S8("-o"));
        gpu_argument_append(&arguments, builder->plan.output_path);
        gpu_argument_append(&arguments, input);
        gpu_plan_process(builder, arguments, builder->plan.output_path);
    }
    else
    {
        gpu_plan_nvptx_clang(builder, input, language, builder->plan.output_path, false, false);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_append_amdgcn_target(GpuArgumentBuilder* arguments, Arena* arena, GpuPipelineOptions options, GpuSourceLanguage language)
{
    if (language == GPU_SOURCE_LANGUAGE_HIP)
    {
        gpu_argument_append(arguments, S8("--offload-device-only"));
        gpu_argument_append(arguments, string_format(arena, S8("--offload-arch={S8}"), options.target.architecture));
    }
    else
    {
        gpu_argument_append(arguments, S8("--target=amdgcn-amd-amdhsa"));
        gpu_argument_append(arguments, string_format(arena, S8("-mcpu={S8}"), options.target.architecture));
    }
    if (options.rocm_path.length)
    {
        gpu_argument_append(arguments, string_format(arena, S8("--rocm-path={S8}"), options.rocm_path));
    }
    else if (language != GPU_SOURCE_LANGUAGE_HIP)
    {
        gpu_argument_append(arguments, S8("-nogpulib"));
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_amdgcn_source(GpuPlanBuilder* builder, String8 input, GpuSourceLanguage language, String8 output,
                                                 GpuPipelineAction action)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 24);
    gpu_argument_append(&arguments, gpu_clang_path(builder->options));
    gpu_append_amdgcn_target(&arguments, builder->arena, builder->options, language);
    gpu_append_clang_common(&arguments, builder->arena, builder->options);
    gpu_append_clang_language(&arguments, language);
    switch (action)
    {
    case GPU_PIPELINE_ACTION_PREPROCESS: gpu_argument_append(&arguments, S8("-E")); break;
    case GPU_PIPELINE_ACTION_ASSEMBLY: gpu_argument_append(&arguments, S8("-S")); break;
    case GPU_PIPELINE_ACTION_OBJECT: gpu_argument_append(&arguments, S8("-c")); break;
    case GPU_PIPELINE_ACTION_SYNTAX_ONLY: gpu_argument_append(&arguments, S8("-fsyntax-only")); break;
    case GPU_PIPELINE_ACTION_LINK:
    case GPU_PIPELINE_ACTION_COUNT: break;
    }
    gpu_argument_append(&arguments, input);
    if (output.length)
    {
        gpu_argument_append(&arguments, S8("-o"));
        gpu_argument_append(&arguments, output);
    }
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_amdgcn_llc(GpuPlanBuilder* builder, String8 input, String8 output, bool assembly)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
    gpu_argument_append(&arguments, gpu_llc_path(builder->options));
    gpu_argument_append(&arguments, S8("-mtriple=amdgcn-amd-amdhsa"));
    gpu_argument_append(&arguments, string_format(builder->arena, S8("-mcpu={S8}"), builder->options.target.architecture));
    gpu_argument_append(&arguments, assembly ? S8("-filetype=asm") : S8("-filetype=obj"));
    gpu_append_optimization(&arguments, builder->arena, builder->options.optimization_level);
    if (builder->options.debug_info)
    {
        gpu_argument_append(&arguments, S8("-g"));
    }
    for (u32 argument_index = 0; argument_index < builder->options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(&arguments, builder->options.extra_arguments[argument_index]);
    }
    gpu_argument_append(&arguments, S8("-o"));
    gpu_argument_append(&arguments, output);
    gpu_argument_append(&arguments, input);
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_amdgcn_link_object(GpuPlanBuilder* builder, String8 object, String8 output)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
    gpu_argument_append(&arguments, gpu_clang_path(builder->options));
    gpu_argument_append(&arguments, S8("--target=amdgcn-amd-amdhsa"));
    gpu_argument_append(&arguments, string_format(builder->arena, S8("-mcpu={S8}"), builder->options.target.architecture));
    if (builder->options.rocm_path.length)
    {
        gpu_argument_append(&arguments, string_format(builder->arena, S8("--rocm-path={S8}"), builder->options.rocm_path));
    }
    else
    {
        gpu_argument_append(&arguments, S8("-nogpulib"));
    }
    for (u32 argument_index = 0; argument_index < builder->options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(&arguments, builder->options.extra_arguments[argument_index]);
    }
    gpu_argument_append(&arguments, object);
    gpu_argument_append(&arguments, S8("-o"));
    gpu_argument_append(&arguments, output);
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_amdgcn(GpuPlanBuilder* builder, GpuSourceLanguage language)
{
    String8 input = builder->options.input_paths[0];
    if (builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS)
    {
        if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("AMDGCN preprocessing requires HIP or OpenCL source"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_PREPROCESSED_SOURCE;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        gpu_plan_amdgcn_source(builder, input, language, builder->plan.output_path, GPU_PIPELINE_ACTION_PREPROCESS);
        return;
    }
    if (builder->options.action == GPU_PIPELINE_ACTION_SYNTAX_ONLY)
    {
        if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION, S8("AMDGCN LLVM IR validation requires llvm-as, which is not part of this pipeline"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_NONE;
        gpu_plan_amdgcn_source(builder, input, language, (String8){0}, GPU_PIPELINE_ACTION_SYNTAX_ONLY);
        return;
    }

    bool assembly = builder->options.action == GPU_PIPELINE_ACTION_ASSEMBLY;
    bool object = builder->options.action == GPU_PIPELINE_ACTION_OBJECT;
    builder->plan.output_format = assembly ? GPU_OUTPUT_AMDGCN_ASSEMBLY : object ? GPU_OUTPUT_AMDGCN_OBJECT : GPU_OUTPUT_AMDGCN_CODE_OBJECT;
    builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
    if (language == GPU_SOURCE_LANGUAGE_LLVM_IR)
    {
        if (assembly || object)
        {
            gpu_plan_amdgcn_llc(builder, input, builder->plan.output_path, assembly);
        }
        else
        {
            String8 temporary_object = gpu_plan_temporary_path(builder, S8(".o"));
            gpu_plan_amdgcn_llc(builder, input, temporary_object, false);
            gpu_plan_amdgcn_link_object(builder, temporary_object, builder->plan.output_path);
        }
    }
    else
    {
        gpu_plan_amdgcn_source(builder, input, language, builder->plan.output_path, builder->options.action);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_append_metal_common(GpuArgumentBuilder* arguments, Arena* arena, GpuPipelineOptions options)
{
    gpu_append_optimization(arguments, arena, options.optimization_level);
    if (options.debug_info)
    {
        gpu_argument_append(arguments, S8("-gline-tables-only"));
    }
    if (options.no_standard_includes)
    {
        gpu_argument_append(arguments, S8("-nostdinc"));
    }
    if (options.sysroot.length)
    {
        gpu_argument_append(arguments, S8("-isysroot"));
        gpu_argument_append(arguments, options.sysroot);
    }
    for (u32 include_index = 0; include_index < options.include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-I"));
        gpu_argument_append(arguments, options.include_paths[include_index]);
    }
    for (u32 include_index = 0; include_index < options.system_include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-isystem"));
        gpu_argument_append(arguments, options.system_include_paths[include_index]);
    }
    for (u32 definition_index = 0; definition_index < options.definition_count; definition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-D{S8}"), options.definitions[definition_index]));
    }
    for (u32 undefinition_index = 0; undefinition_index < options.undefinition_count; undefinition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-U{S8}"), options.undefinitions[undefinition_index]));
    }
    for (u32 argument_index = 0; argument_index < options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(arguments, options.extra_arguments[argument_index]);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_metal_compile(GpuPlanBuilder* builder, String8 input, String8 output, GpuPipelineAction action)
{
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
    gpu_argument_append(&arguments, gpu_xcrun_path(builder->options));
    gpu_argument_append(&arguments, S8("-sdk"));
    gpu_argument_append(&arguments, builder->options.target.metal_sdk);
    gpu_argument_append(&arguments, S8("metal"));
    gpu_append_metal_common(&arguments, builder->arena, builder->options);
    switch (action)
    {
    case GPU_PIPELINE_ACTION_PREPROCESS: gpu_argument_append(&arguments, S8("-E")); break;
    case GPU_PIPELINE_ACTION_OBJECT: gpu_argument_append(&arguments, S8("-c")); break;
    case GPU_PIPELINE_ACTION_SYNTAX_ONLY: gpu_argument_append(&arguments, S8("-fsyntax-only")); break;
    case GPU_PIPELINE_ACTION_LINK:
    case GPU_PIPELINE_ACTION_ASSEMBLY:
    case GPU_PIPELINE_ACTION_COUNT: break;
    }
    gpu_argument_append(&arguments, input);
    if (output.length)
    {
        gpu_argument_append(&arguments, S8("-o"));
        gpu_argument_append(&arguments, output);
    }
    gpu_plan_process(builder, arguments, output);
}

BUSTER_GLOBAL_LOCAL void gpu_plan_metal(GpuPlanBuilder* builder, GpuSourceLanguage* languages)
{
    if (builder->options.action == GPU_PIPELINE_ACTION_ASSEMBLY)
    {
        gpu_plan_error(builder, GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION,
                       S8("Metal has no stable textual assembly output; use -c for AIR or link for metallib"));
        return;
    }
    if (builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS || builder->options.action == GPU_PIPELINE_ACTION_SYNTAX_ONLY)
    {
        if (builder->options.input_count != 1 || languages[0] != GPU_SOURCE_LANGUAGE_METAL)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("Metal preprocessing and syntax checking require exactly one .metal source"));
            return;
        }
        builder->plan.output_format = builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS ? GPU_OUTPUT_PREPROCESSED_SOURCE : GPU_OUTPUT_NONE;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        gpu_plan_metal_compile(builder, builder->options.input_paths[0], builder->plan.output_path, builder->options.action);
        return;
    }
    if (builder->options.action == GPU_PIPELINE_ACTION_OBJECT)
    {
        if (builder->options.input_count != 1)
        {
            gpu_plan_error(builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("Metal AIR output requires exactly one .metal or .air input"));
            return;
        }
        builder->plan.output_format = GPU_OUTPUT_METAL_AIR;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        if (languages[0] == GPU_SOURCE_LANGUAGE_METAL_AIR)
        {
            gpu_plan_copy(builder, builder->options.input_paths[0], builder->plan.output_path);
        }
        else
        {
            gpu_plan_metal_compile(builder, builder->options.input_paths[0], builder->plan.output_path, GPU_PIPELINE_ACTION_OBJECT);
        }
        return;
    }

    builder->plan.output_format = GPU_OUTPUT_METAL_LIBRARY;
    builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
    String8* air_inputs = arena_allocate(builder->arena, String8, builder->options.input_count);
    for (u32 input_index = 0; input_index < builder->options.input_count; input_index += 1)
    {
        if (languages[input_index] == GPU_SOURCE_LANGUAGE_METAL_AIR)
        {
            air_inputs[input_index] = builder->options.input_paths[input_index];
        }
        else
        {
            air_inputs[input_index] = gpu_plan_temporary_path(builder, S8(".air"));
            gpu_plan_metal_compile(builder, builder->options.input_paths[input_index], air_inputs[input_index], GPU_PIPELINE_ACTION_OBJECT);
        }
    }
    GpuArgumentBuilder link = gpu_arguments_begin(builder->arena, builder->options, builder->options.input_count + 12);
    gpu_argument_append(&link, gpu_xcrun_path(builder->options));
    gpu_argument_append(&link, S8("-sdk"));
    gpu_argument_append(&link, builder->options.target.metal_sdk);
    gpu_argument_append(&link, S8("metallib"));
    for (u32 input_index = 0; input_index < builder->options.input_count; input_index += 1)
    {
        gpu_argument_append(&link, air_inputs[input_index]);
    }
    gpu_argument_append(&link, S8("-o"));
    gpu_argument_append(&link, builder->plan.output_path);
    gpu_plan_process(builder, link, builder->plan.output_path);
}

BUSTER_GLOBAL_LOCAL String8 gpu_dxc_profile_prefix(GpuShaderStage stage)
{
    switch (stage)
    {
    case GPU_SHADER_STAGE_COMPUTE: return S8("cs");
    case GPU_SHADER_STAGE_VERTEX: return S8("vs");
    case GPU_SHADER_STAGE_FRAGMENT: return S8("ps");
    case GPU_SHADER_STAGE_GEOMETRY: return S8("gs");
    case GPU_SHADER_STAGE_HULL: return S8("hs");
    case GPU_SHADER_STAGE_DOMAIN: return S8("ds");
    case GPU_SHADER_STAGE_MESH: return S8("ms");
    case GPU_SHADER_STAGE_AMPLIFICATION: return S8("as");
    case GPU_SHADER_STAGE_LIBRARY:
    case GPU_SHADER_STAGE_RAY_GENERATION:
    case GPU_SHADER_STAGE_INTERSECTION:
    case GPU_SHADER_STAGE_ANY_HIT:
    case GPU_SHADER_STAGE_CLOSEST_HIT:
    case GPU_SHADER_STAGE_MISS:
    case GPU_SHADER_STAGE_CALLABLE: return S8("lib");
    case GPU_SHADER_STAGE_NONE:
    case GPU_SHADER_STAGE_COUNT: break;
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL bool gpu_dxc_target_is_valid(GpuTarget target)
{
    return gpu_dxc_target_fields_are_valid(target);
}

BUSTER_GLOBAL_LOCAL String8 gpu_dxc_profile(Arena* arena, GpuTarget target)
{
    return string_format(arena, S8("{S8}_{u32}_{u32}"), gpu_dxc_profile_prefix(target.stage), (u32)target.shader_model_major,
                         (u32)target.shader_model_minor);
}

BUSTER_GLOBAL_LOCAL void gpu_append_dxc_common(GpuArgumentBuilder* arguments, Arena* arena, GpuPipelineOptions options)
{
    gpu_append_optimization(arguments, arena, options.optimization_level);
    if (options.debug_info)
    {
        gpu_argument_append(arguments, S8("-Zi"));
        gpu_argument_append(arguments, S8("-Qembed_debug"));
    }
    for (u32 include_index = 0; include_index < options.include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-I"));
        gpu_argument_append(arguments, options.include_paths[include_index]);
    }
    for (u32 include_index = 0; include_index < options.system_include_path_count; include_index += 1)
    {
        gpu_argument_append(arguments, S8("-I"));
        gpu_argument_append(arguments, options.system_include_paths[include_index]);
    }
    for (u32 definition_index = 0; definition_index < options.definition_count; definition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-D{S8}"), options.definitions[definition_index]));
    }
    for (u32 undefinition_index = 0; undefinition_index < options.undefinition_count; undefinition_index += 1)
    {
        gpu_argument_append(arguments, string_format(arena, S8("-U{S8}"), options.undefinitions[undefinition_index]));
    }
    for (u32 argument_index = 0; argument_index < options.extra_argument_count; argument_index += 1)
    {
        gpu_argument_append(arguments, options.extra_arguments[argument_index]);
    }
}

BUSTER_GLOBAL_LOCAL void gpu_append_dxc_target(GpuArgumentBuilder* arguments, Arena* arena, GpuTarget target)
{
    gpu_argument_append(arguments, S8("-T"));
    gpu_argument_append(arguments, gpu_dxc_profile(arena, target));
    if (gpu_dxc_stage_uses_entry_point(target.stage))
    {
        gpu_argument_append(arguments, S8("-E"));
        gpu_argument_append(arguments, target.entry_point.length ? target.entry_point : S8("main"));
    }
}

BUSTER_GLOBAL_LOCAL void gpu_plan_dxil(GpuPlanBuilder* builder)
{
    String8 input = builder->options.input_paths[0];
    if (builder->options.action == GPU_PIPELINE_ACTION_PREPROCESS)
    {
        builder->plan.output_format = GPU_OUTPUT_PREPROCESSED_SOURCE;
        builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
        GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 16);
        gpu_argument_append(&arguments, gpu_dxc_path(builder->options));
        gpu_append_dxc_common(&arguments, builder->arena, builder->options);
        gpu_argument_append(&arguments, S8("-P"));
        gpu_argument_append(&arguments, builder->plan.output_path);
        gpu_argument_append(&arguments, input);
        gpu_plan_process(builder, arguments, builder->plan.output_path);
        return;
    }

    bool syntax_only = builder->options.action == GPU_PIPELINE_ACTION_SYNTAX_ONLY;
    bool assembly = builder->options.action == GPU_PIPELINE_ACTION_ASSEMBLY;
    builder->plan.output_format = syntax_only ? GPU_OUTPUT_NONE : assembly ? GPU_OUTPUT_DXIL_ASSEMBLY : GPU_OUTPUT_DXIL_CONTAINER;
    builder->plan.output_path = gpu_plan_output_path(builder, builder->plan.output_format);
    String8 binary_output = syntax_only || assembly ? gpu_plan_temporary_path(builder, S8(".dxil")) : builder->plan.output_path;
    GpuArgumentBuilder arguments = gpu_arguments_begin(builder->arena, builder->options, 24);
    gpu_argument_append(&arguments, gpu_dxc_path(builder->options));
    gpu_append_dxc_common(&arguments, builder->arena, builder->options);
    gpu_append_dxc_target(&arguments, builder->arena, builder->options.target);
    gpu_argument_append(&arguments, S8("-Fo"));
    gpu_argument_append(&arguments, binary_output);
    if (assembly)
    {
        gpu_argument_append(&arguments, S8("-Fc"));
        gpu_argument_append(&arguments, builder->plan.output_path);
    }
    gpu_argument_append(&arguments, input);
    gpu_plan_process(builder, arguments, assembly ? builder->plan.output_path : binary_output);
}

GpuPipelinePlan gpu_pipeline_plan(Arena* arena, GpuPipelineOptions options)
{
    GpuPlanBuilder builder = {
        .arena = arena,
        .options = options,
    };
    if (!arena)
    {
        builder.plan.error = GPU_PIPELINE_ERROR_INVALID_INPUT;
        builder.plan.diagnostic = S8("GPU pipeline requires an arena");
        return builder.plan;
    }
    u64 step_capacity = (u64)options.input_count * 2 + 8;
    u64 temporary_capacity = (u64)options.input_count * 2 + 8;
    if (step_capacity > UINT32_MAX || temporary_capacity > UINT32_MAX)
    {
        builder.plan.error = GPU_PIPELINE_ERROR_INVALID_INPUT;
        builder.plan.diagnostic = S8("GPU input count is too large");
        return builder.plan;
    }
    builder.step_capacity = (u32)step_capacity;
    builder.temporary_capacity = (u32)temporary_capacity;
    builder.plan.steps = arena_allocate(arena, GpuPipelineStep, builder.step_capacity);
    builder.plan.temporary_paths = arena_allocate(arena, String8, builder.temporary_capacity);

    if (!options.input_paths || !options.input_count)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU compilation requires at least one input"));
        return builder.plan;
    }
    if (options.target.kind <= GPU_TARGET_NONE || options.target.kind >= GPU_TARGET_COUNT)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_TARGET, S8("invalid GPU target"));
        return builder.plan;
    }
    if (!gpu_target_is_valid(options.target))
    {
        if (options.target.kind == GPU_TARGET_AMDGCN && !options.target.architecture.length)
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_TARGET, S8("AMDGCN requires -mcpu=gfx... or --gpu-arch=gfx..."));
        else
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_TARGET, S8("incomplete GPU target configuration"));
        return builder.plan;
    }
    if (options.action >= GPU_PIPELINE_ACTION_COUNT)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION, S8("invalid GPU pipeline action"));
        return builder.plan;
    }
    if (options.optimization_level > 3)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU optimization level must be between 0 and 3"));
        return builder.plan;
    }
    GpuSourceLanguage* languages = arena_allocate(arena, GpuSourceLanguage, options.input_count);
    for (u32 input_index = 0; input_index < options.input_count; input_index += 1)
    {
        languages[input_index] = gpu_effective_language(options, options.input_paths[input_index]);
        if (languages[input_index] == GPU_SOURCE_LANGUAGE_AUTOMATIC)
        {
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT,
                           string_format(arena, S8("cannot infer GPU source language from {S8}; use -x"), options.input_paths[input_index]));
            return builder.plan;
        }
        if (!gpu_target_accepts_language(options.target.kind, languages[input_index]))
        {
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT,
                           string_format(arena, S8("{S8} input {S8} is incompatible with target {S8}"),
                                         gpu_source_language_to_string(languages[input_index]), options.input_paths[input_index],
                                         gpu_target_to_string(arena, options.target)));
            return builder.plan;
        }
        if (options.target.kind == GPU_TARGET_NVPTX32 && languages[input_index] == GPU_SOURCE_LANGUAGE_CUDA)
        {
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT,
                           S8("CUDA source requires nvptx64; use OpenCL or LLVM IR when explicitly targeting 32-bit NVPTX"));
            return builder.plan;
        }
    }
    if (options.target.kind == GPU_TARGET_SPIRV && options.target.stage != GPU_SHADER_STAGE_COMPUTE)
    {
        for (u32 input_index = 0; input_index < options.input_count; input_index += 1)
        {
            if (languages[input_index] != GPU_SOURCE_LANGUAGE_HLSL)
            {
                gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT,
                               S8("non-HLSL SPIR-V inputs require the compute stage; graphics and library stages require HLSL/DXC"));
                return builder.plan;
            }
        }
    }

    bool uses_dxc = options.target.kind == GPU_TARGET_DXIL;
    for (u32 input_index = 0; input_index < options.input_count && !uses_dxc; input_index += 1)
    {
        uses_dxc = options.target.kind == GPU_TARGET_SPIRV && languages[input_index] == GPU_SOURCE_LANGUAGE_HLSL;
    }
    if (uses_dxc)
    {
        if (gpu_dxc_stage_uses_entry_point(builder.options.target.stage) && !builder.options.target.entry_point.length)
        {
            builder.options.target.entry_point = S8("main");
        }
        if (!gpu_dxc_target_is_valid(builder.options.target))
        {
            gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_TARGET,
                           S8("DXC targets require a valid shader model 6.0 through 6.10 profile (mesh/amplification require 6.5+)"));
            return builder.plan;
        }
    }

    bool multi_input_target = gpu_target_kind_is_spirv(options.target.kind) || options.target.kind == GPU_TARGET_METAL_AIR64;
    if (!multi_input_target && options.input_count != 1)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT,
                       string_format(arena, S8("target {S8} currently accepts exactly one translation unit"), gpu_target_to_string(arena, options.target)));
        return builder.plan;
    }

    switch (options.target.kind)
    {
    case GPU_TARGET_SPIRV:
    case GPU_TARGET_SPIRV32:
    case GPU_TARGET_SPIRV64: gpu_plan_spirv(&builder, languages); break;
    case GPU_TARGET_NVPTX32:
    case GPU_TARGET_NVPTX64: gpu_plan_nvptx(&builder, languages[0]); break;
    case GPU_TARGET_AMDGCN: gpu_plan_amdgcn(&builder, languages[0]); break;
    case GPU_TARGET_METAL_AIR64: gpu_plan_metal(&builder, languages); break;
    case GPU_TARGET_DXIL: gpu_plan_dxil(&builder); break;
    case GPU_TARGET_NONE:
    case GPU_TARGET_COUNT: break;
    }
    if (builder.plan.error == GPU_PIPELINE_ERROR_NONE && builder.plan.output_format != GPU_OUTPUT_NONE && !builder.plan.output_path.length)
    {
        gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU pipeline did not produce an output path"));
    }
    if (builder.plan.error == GPU_PIPELINE_ERROR_NONE && builder.plan.output_path.length)
    {
        for (u32 input_index = 0; input_index < options.input_count; input_index += 1)
        {
            if (string_equal(builder.plan.output_path, options.input_paths[input_index]))
            {
                gpu_plan_error(&builder, GPU_PIPELINE_ERROR_INVALID_INPUT, S8("GPU output path must not overwrite an input"));
                break;
            }
        }
    }
    return builder.plan;
}

BUSTER_GLOBAL_LOCAL bool gpu_argument_needs_quotes(String8 argument)
{
    if (!argument.length)
    {
        return true;
    }
    for (u64 index = 0; index < argument.length; index += 1)
    {
        char8 code_unit = argument.pointer[index];
        if (code_unit == ' ' || code_unit == '\t' || code_unit == '\n' || code_unit == '"')
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL String8 gpu_command_to_string(Arena* arena, SliceString8 arguments)
{
    u64 length = arguments.length ? arguments.length - 1 : 0;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        String8 argument = arguments.pointer[argument_index];
        bool quote = gpu_argument_needs_quotes(argument);
        length += argument.length + (quote ? 2 : 0);
        if (quote)
        {
            for (u64 index = 0; index < argument.length; index += 1)
            {
                length += argument.pointer[index] == '"';
            }
        }
    }
    char8* pointer = arena_allocate(arena, char8, length + 1);
    u64 position = 0;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        if (argument_index)
        {
            pointer[position++] = ' ';
        }
        String8 argument = arguments.pointer[argument_index];
        bool quote = gpu_argument_needs_quotes(argument);
        if (quote) pointer[position++] = '"';
        for (u64 index = 0; index < argument.length; index += 1)
        {
            if (quote && argument.pointer[index] == '"') pointer[position++] = '\\';
            pointer[position++] = argument.pointer[index];
        }
        if (quote) pointer[position++] = '"';
    }
    pointer[position] = 0;
    return (String8){.pointer = pointer, .length = position};
}

BUSTER_GLOBAL_LOCAL void gpu_result_append_log(Arena* arena, GpuPipelineResult* result, ByteSlice bytes)
{
    if (!bytes.length)
    {
        return;
    }
    String8 text = BYTE_SLICE_TO_STRING(8, bytes);
    result->log = result->log.length ? string_format(arena, S8("{S8}{S8}"), result->log, text) : text;
}

BUSTER_GLOBAL_LOCAL void gpu_pipeline_cleanup(GpuPipelinePlan plan, bool save_temporaries)
{
    if (save_temporaries)
    {
        return;
    }
    for (u32 temporary_index = 0; temporary_index < plan.temporary_path_count; temporary_index += 1)
    {
        os_file_delete(plan.temporary_paths[temporary_index]);
    }
}

BUSTER_GLOBAL_LOCAL bool gpu_bytes_contains(ByteSlice bytes, String8 needle)
{
    if (needle.length && needle.length <= bytes.length)
    {
        u64 limit = bytes.length - needle.length;
        for (u64 offset = 0; offset <= limit; offset += 1)
        {
            if (memcmp(bytes.pointer + offset, needle.pointer, needle.length) == 0)
            {
                return true;
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool gpu_elf_u16(ByteSlice bytes, u64 offset, u16* value)
{
    if (bytes.length >= offset + 2 && value)
    {
        if (bytes.pointer[5] == 1)
        {
            *value = (u16)(bytes.pointer[offset] | (u16)bytes.pointer[offset + 1] << 8);
            return true;
        }
        if (bytes.pointer[5] == 2)
        {
            *value = (u16)((u16)bytes.pointer[offset] << 8 | bytes.pointer[offset + 1]);
            return true;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool gpu_amdgcn_elf(ByteSlice bytes, u16 expected_type)
{
    // AMDGCN code objects are ELF64 and use EM_AMDGPU (224). LLVM emits
    // ET_REL before device linking and ET_DYN for an HSA-loadable code object.
    if (bytes.length < 20 || bytes.pointer[0] != 0x7f || bytes.pointer[1] != 'E' || bytes.pointer[2] != 'L' || bytes.pointer[3] != 'F' ||
        bytes.pointer[4] != 2)
    {
        return false;
    }
    u16 type = 0;
    u16 machine = 0;
    return gpu_elf_u16(bytes, 16, &type) && gpu_elf_u16(bytes, 18, &machine) && type == expected_type && machine == 224;
}

bool gpu_artifact_has_expected_magic(GpuOutputFormat format, ByteSlice bytes)
{
    switch (format)
    {
    case GPU_OUTPUT_NONE: return bytes.length == 0;
    case GPU_OUTPUT_PREPROCESSED_SOURCE: return bytes.length != 0;
    case GPU_OUTPUT_SPIRV_BINARY:
        return bytes.length >= 4 &&
               ((bytes.pointer[0] == 0x03 && bytes.pointer[1] == 0x02 && bytes.pointer[2] == 0x23 && bytes.pointer[3] == 0x07) ||
                (bytes.pointer[0] == 0x07 && bytes.pointer[1] == 0x23 && bytes.pointer[2] == 0x02 && bytes.pointer[3] == 0x03));
    case GPU_OUTPUT_SPIRV_ASSEMBLY:
        return gpu_bytes_contains(bytes, S8("OpCapability")) || gpu_bytes_contains(bytes, S8("; SPIR-V"));
    case GPU_OUTPUT_CUDA_PTX: return gpu_bytes_contains(bytes, S8(".version")) && gpu_bytes_contains(bytes, S8(".target"));
    case GPU_OUTPUT_AMDGCN_OBJECT: return gpu_amdgcn_elf(bytes, 1);
    case GPU_OUTPUT_AMDGCN_CODE_OBJECT: return gpu_amdgcn_elf(bytes, 3);
    case GPU_OUTPUT_AMDGCN_ASSEMBLY: return bytes.length != 0;
    case GPU_OUTPUT_METAL_AIR:
        return bytes.length >= 4 &&
               ((bytes.pointer[0] == 'B' && bytes.pointer[1] == 'C' && bytes.pointer[2] == 0xc0 && bytes.pointer[3] == 0xde) ||
                (bytes.pointer[0] == 0xde && bytes.pointer[1] == 0xc0 && bytes.pointer[2] == 0x17 && bytes.pointer[3] == 0x0b) ||
                (bytes.pointer[0] == 'A' && bytes.pointer[1] == 'I' && bytes.pointer[2] == 'R'));
    case GPU_OUTPUT_METAL_LIBRARY:
        return bytes.length >= 4 && bytes.pointer[0] == 'M' && bytes.pointer[1] == 'T' && bytes.pointer[2] == 'L' && bytes.pointer[3] == 'B';
    case GPU_OUTPUT_DXIL_CONTAINER:
        return bytes.length >= 4 && bytes.pointer[0] == 'D' && bytes.pointer[1] == 'X' && bytes.pointer[2] == 'B' && bytes.pointer[3] == 'C';
    case GPU_OUTPUT_DXIL_ASSEMBLY: return bytes.length != 0;
    case GPU_OUTPUT_COUNT: break;
    }
    return false;
}

GpuPipelineResult gpu_pipeline_execute(Arena* arena, GpuPipelineOptions options)
{
    GpuPipelineResult result = {
        .process_result = PROCESS_RESULT_SUCCESS,
        .failed_step = UINT32_MAX,
    };
    GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
    if (plan.error != GPU_PIPELINE_ERROR_NONE)
    {
        result.error = plan.error;
        result.diagnostic = plan.diagnostic;
        result.process_result = PROCESS_RESULT_FAILED;
        return result;
    }

    for (u32 temporary_index = 0; temporary_index < plan.temporary_path_count; temporary_index += 1)
    {
        os_file_delete(plan.temporary_paths[temporary_index]);
    }
    if (plan.output_path.length)
    {
        os_file_delete(plan.output_path);
    }

    for (u32 step_index = 0; step_index < plan.step_count; step_index += 1)
    {
        GpuPipelineStep step = plan.steps[step_index];
        if (step.kind == GPU_PIPELINE_STEP_COPY)
        {
            if (!string_equal(step.copy_source, step.output_path) &&
                !file_copy((CopyFileArguments){.original_path = step.copy_source, .new_path = step.output_path}))
            {
                result.error = GPU_PIPELINE_ERROR_FILE_WRITE;
                result.process_result = PROCESS_RESULT_FAILED;
                result.failed_step = step_index;
                result.diagnostic = string_format(arena, S8("could not copy GPU artifact {S8} to {S8}"), step.copy_source, step.output_path);
                break;
            }
            continue;
        }
        if (step.kind != GPU_PIPELINE_STEP_PROCESS)
        {
            result.error = GPU_PIPELINE_ERROR_INVALID_INPUT;
            result.process_result = PROCESS_RESULT_FAILED;
            result.failed_step = step_index;
            result.diagnostic = S8("invalid GPU pipeline step");
            break;
        }
        result.command = gpu_command_to_string(arena, step.arguments);
        ProcessSpawnResult spawn = os_process_spawn(step.arguments, (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                                                        .use_process_environment = true,
                                                    });
        if (!spawn.handle)
        {
            result.error = GPU_PIPELINE_ERROR_TOOL_NOT_FOUND;
            result.process_result = PROCESS_RESULT_NOT_EXISTENT;
            result.failed_step = step_index;
            result.diagnostic = string_format(arena, S8("could not launch GPU tool: {S8}"), result.command);
            break;
        }
        ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
        gpu_result_append_log(arena, &result, wait.streams[STANDARD_STREAM_OUTPUT]);
        gpu_result_append_log(arena, &result, wait.streams[STANDARD_STREAM_ERROR]);
        result.process_result = wait.result;
        if (wait.result != PROCESS_RESULT_SUCCESS)
        {
            result.error = GPU_PIPELINE_ERROR_TOOL_FAILED;
            result.failed_step = step_index;
            result.diagnostic = result.log.length ? string_format(arena, S8("GPU tool failed: {S8}\n{S8}"), result.command, result.log)
                                                  : string_format(arena, S8("GPU tool failed: {S8}"), result.command);
            break;
        }
    }

    if (result.error == GPU_PIPELINE_ERROR_NONE && plan.output_format != GPU_OUTPUT_NONE)
    {
        ByteSlice bytes = file_read(arena, plan.output_path, (FileReadOptions){0});
        if (!bytes.pointer || !bytes.length)
        {
            result.error = GPU_PIPELINE_ERROR_FILE_READ;
            result.process_result = PROCESS_RESULT_FAILED;
            result.diagnostic = string_format(arena, S8("GPU pipeline did not produce {S8}"), plan.output_path);
        }
        else if (!gpu_artifact_has_expected_magic(plan.output_format, bytes))
        {
            result.error = GPU_PIPELINE_ERROR_INVALID_ARTIFACT;
            result.process_result = PROCESS_RESULT_FAILED;
            result.diagnostic = string_format(arena, S8("{S8} does not contain a valid {S8} artifact"), plan.output_path,
                                              gpu_output_format_to_string(plan.output_format));
        }
        else
        {
            result.artifact = (GpuArtifact){
                .bytes = bytes,
                .path = plan.output_is_temporary && !options.save_temporaries ? (String8){0} : plan.output_path,
                .format = plan.output_format,
            };
        }
    }
    if (result.error != GPU_PIPELINE_ERROR_NONE && plan.output_path.length)
    {
        os_file_delete(plan.output_path);
    }
    gpu_pipeline_cleanup(plan, options.save_temporaries);
    return result;
}
