#include <buster/tests/compiler/gpu/gpu_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL bool gpu_test_step_has_argument(GpuPipelinePlan plan, u32 step_index, String8 argument)
{
    bool result = false;
    if (step_index < plan.step_count && plan.steps[step_index].kind == GPU_PIPELINE_STEP_PROCESS)
    {
        SliceString8 arguments = plan.steps[step_index].arguments;
        for (u64 index = 0; index < arguments.length && !result; index += 1)
        {
            result = string_equal(arguments.pointer[index], argument);
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_test_plan_has_tool(GpuPipelinePlan plan, String8 tool)
{
    bool result = false;
    for (u32 step_index = 0; step_index < plan.step_count && !result; step_index += 1)
    {
        GpuPipelineStep step = plan.steps[step_index];
        result = step.kind == GPU_PIPELINE_STEP_PROCESS && step.arguments.length && string_equal(step.arguments.pointer[0], tool);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL GpuTarget gpu_test_target(String8 triple)
{
    GpuTargetParseResult parsed = gpu_target_parse(triple);
    return parsed.error == GPU_TARGET_PARSE_ERROR_NONE ? parsed.target : (GpuTarget){0};
}

BUSTER_GLOBAL_LOCAL GpuPipelineOptions gpu_test_options(String8* inputs, u32 input_count, GpuTarget target, GpuPipelineAction action)
{
    return (GpuPipelineOptions){
        .input_paths = inputs,
        .target = target,
        .input_count = input_count,
        .language = GPU_SOURCE_LANGUAGE_AUTOMATIC,
        .action = action,
        .optimization_level = 2,
    };
}

UnitTestResult gpu_pipeline_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    {
        GpuTargetParseResult parsed = gpu_target_parse(S8("spirv"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_NONE);
        BUSTER_TEST(arguments, parsed.target.kind == GPU_TARGET_SPIRV && parsed.target.address_bits == 0);
        BUSTER_STRING_TEST(arguments, parsed.target.backend_triple, S8("spirv"));
        BUSTER_TEST(arguments, gpu_target_is_valid(parsed.target));
        BUSTER_STRING_TEST(arguments, gpu_target_to_string(arena, parsed.target), S8("spirv"));
    }
    {
        GpuTargetParseResult parsed = gpu_target_parse(S8("spirv64v1.6-unknown-vulkan1.3"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_NONE);
        BUSTER_TEST(arguments, parsed.target.kind == GPU_TARGET_SPIRV64 && parsed.target.address_bits == 64);
        BUSTER_STRING_TEST(arguments, gpu_target_to_string(arena, parsed.target), S8("spirv64v1.6-unknown-vulkan1.3"));
    }
    {
        GpuTargetParseResult parsed = gpu_target_parse(S8("spirv64v1.7-unknown-vulkan1.3"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE);
        parsed = gpu_target_parse(S8("spirv64-unknown-opengl"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_INVALID_TRIPLE);
    }
    {
        GpuTargetParseResult ptx = gpu_target_parse(S8("ptx"));
        GpuTargetParseResult amd = gpu_target_parse(S8("amdgcn-amd-amdhsa"));
        GpuTargetParseResult metal = gpu_target_parse(S8("air64-apple-ios"));
        BUSTER_TEST(arguments, ptx.error == GPU_TARGET_PARSE_ERROR_NONE && ptx.target.kind == GPU_TARGET_NVPTX64 && ptx.target.address_bits == 64);
        BUSTER_TEST(arguments, amd.error == GPU_TARGET_PARSE_ERROR_NONE && amd.target.kind == GPU_TARGET_AMDGCN && !gpu_target_is_valid(amd.target));
        amd.target.architecture = S8("gfx1201");
        BUSTER_TEST(arguments, gpu_target_is_valid(amd.target));
        BUSTER_TEST(arguments, metal.error == GPU_TARGET_PARSE_ERROR_NONE && metal.target.kind == GPU_TARGET_METAL_AIR64);
        BUSTER_STRING_TEST(arguments, metal.target.metal_sdk, S8("iphoneos"));
    }
    {
        GpuTargetParseResult parsed = gpu_target_parse(S8("dxil-pc-shadermodel6.9-mesh"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_NONE);
        BUSTER_TEST(arguments, parsed.target.kind == GPU_TARGET_DXIL && parsed.target.stage == GPU_SHADER_STAGE_MESH);
        BUSTER_TEST(arguments, parsed.target.shader_model_major == 6 && parsed.target.shader_model_minor == 9);
        BUSTER_TEST(arguments, gpu_target_is_valid(parsed.target));
        BUSTER_STRING_TEST(arguments, gpu_target_to_string(arena, parsed.target), S8("dxil-pc-shadermodel6.9-mesh"));
    }
    {
        GpuTargetParseResult parsed = gpu_target_parse(S8("dxil-pc-shadermodel7.0-compute"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_SHADER_MODEL);
        parsed = gpu_target_parse(S8("dxil-pc-shadermodel6.4-mesh"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_SHADER_MODEL);
        parsed = gpu_target_parse(S8("dxil-pc-shadermodel6.0-library"));
        BUSTER_TEST(arguments, parsed.error == GPU_TARGET_PARSE_ERROR_SHADER_MODEL);
    }
    {
        GpuTarget mismatched = gpu_test_target(S8("spirv64"));
        mismatched.kind = GPU_TARGET_SPIRV32;
        BUSTER_TEST(arguments, !gpu_target_is_valid(mismatched));
        GpuTarget invalid_metal = gpu_test_target(S8("metal"));
        invalid_metal.metal_sdk = S8("not-an-sdk");
        BUSTER_TEST(arguments, !gpu_target_is_valid(invalid_metal));
    }
    BUSTER_TEST(arguments, gpu_shader_stage_from_string(S8("pixel")) == GPU_SHADER_STAGE_FRAGMENT);
    BUSTER_TEST(arguments, gpu_shader_stage_from_string(S8("raygen")) == GPU_SHADER_STAGE_RAY_GENERATION);
    {
        u16 major = 0;
        u16 minor = 0;
        BUSTER_TEST(arguments, gpu_shader_model_parse(S8("6_10"), &major, &minor) && major == 6 && minor == 10);
        BUSTER_TEST(arguments, !gpu_shader_model_parse(S8("6"), &major, &minor));
    }
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("kernel.cl")) == GPU_SOURCE_LANGUAGE_OPENCL);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("kernel.cu")) == GPU_SOURCE_LANGUAGE_CUDA);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("kernel.hip")) == GPU_SOURCE_LANGUAGE_HIP);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("shader.metal")) == GPU_SOURCE_LANGUAGE_METAL);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("shader.hlsl")) == GPU_SOURCE_LANGUAGE_HLSL);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("module.bc")) == GPU_SOURCE_LANGUAGE_LLVM_IR);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("module.spv")) == GPU_SOURCE_LANGUAGE_SPIRV_BINARY);
    BUSTER_TEST(arguments, gpu_source_language_from_path(S8("module.air")) == GPU_SOURCE_LANGUAGE_METAL_AIR);

    {
        String8 inputs[] = {S8("kernel.cl")};
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, gpu_test_target(S8("spirv64v1.6-unknown-vulkan1.3")),
                                                                         GPU_PIPELINE_ACTION_OBJECT));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
        BUSTER_TEST(arguments, plan.output_format == GPU_OUTPUT_SPIRV_BINARY);
        BUSTER_STRING_TEST(arguments, plan.output_path, S8("kernel.spv"));
        BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("clang")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("--target=spirv64v1.6-unknown-vulkan1.3")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-c")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("cl")));
    }
    {
        String8 inputs[] = {S8("kernel.cl")};
        GpuTarget target = gpu_test_target(S8("spirv-unknown-vulkan1.3"));
        target.stage = GPU_SHADER_STAGE_VERTEX;
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_INVALID_INPUT);
    }
    {
        String8 inputs[] = {S8("shader.hlsl")};
        GpuTarget target = gpu_test_target(S8("spirv-unknown-vulkan1.3"));
        target.stage = GPU_SHADER_STAGE_VERTEX;
        target.shader_model_minor = 9;
        target.entry_point = S8("vertex_main");
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
        BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("dxc")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-spirv")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-fspv-target-env=vulkan1.3")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("vs_6_9")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("vertex_main")));
    }
    {
        String8 inputs[] = {S8("a.spv")};
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
        BUSTER_TEST(arguments, plan.steps[0].kind == GPU_PIPELINE_STEP_COPY);
        BUSTER_STRING_TEST(arguments, plan.output_path, S8("a.out.spv"));
    }
    {
        String8 inputs[] = {S8("a.spv"), S8("b.cl")};
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 2, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_ASSEMBLY));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 3);
        BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("clang")));
        BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("spirv-link")));
        BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("spirv-dis")));
        BUSTER_TEST(arguments, plan.output_format == GPU_OUTPUT_SPIRV_ASSEMBLY);
    }
    {
        String8 inputs[] = {S8("kernel.cu")};
        GpuTarget target = gpu_test_target(S8("nvptx64-nvidia-cuda"));
        target.architecture = S8("sm_90a");
        GpuPipelineOptions options = gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_LINK);
        options.cuda_path = S8("/cuda");
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.output_format == GPU_OUTPUT_CUDA_PTX);
        BUSTER_STRING_TEST(arguments, plan.output_path, S8("kernel.ptx"));
        BUSTER_TEST(arguments, plan.output_path.pointer[plan.output_path.length] == 0);
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("--cuda-device-only")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("--cuda-gpu-arch=sm_90a")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("--cuda-path=/cuda")));
    }
    {
        String8 inputs[] = {S8("kernel.ll")};
        GpuTarget target = gpu_test_target(S8("nvptx64"));
        target.architecture = S8("sm_100");
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && gpu_test_plan_has_tool(plan, S8("llc")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-mtriple=nvptx64-nvidia-cuda")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-filetype=asm")));
    }
    {
        String8 inputs[] = {S8("kernel.cu")};
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, gpu_test_target(S8("nvptx")), GPU_PIPELINE_ACTION_OBJECT));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_INVALID_INPUT);
    }
    {
        String8 inputs[] = {S8("kernel.hip")};
        GpuTarget target = gpu_test_target(S8("amdgcn-amd-amdhsa"));
        target.architecture = S8("gfx1201");
        GpuPipelinePlan object = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT));
        GpuPipelinePlan linked = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, object.error == GPU_PIPELINE_ERROR_NONE && object.output_format == GPU_OUTPUT_AMDGCN_OBJECT);
        BUSTER_STRING_TEST(arguments, object.output_path, S8("kernel.o"));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(object, 0, S8("--offload-device-only")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(object, 0, S8("--offload-arch=gfx1201")));
        BUSTER_TEST(arguments, linked.error == GPU_PIPELINE_ERROR_NONE && linked.output_format == GPU_OUTPUT_AMDGCN_CODE_OBJECT);
        BUSTER_STRING_TEST(arguments, linked.output_path, S8("kernel.hsaco"));
    }
    {
        String8 inputs[] = {S8("kernel.bc")};
        GpuTarget target = gpu_test_target(S8("amdgcn"));
        target.architecture = S8("gfx1100");
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 2);
        BUSTER_TEST(arguments, string_equal(plan.steps[0].arguments.pointer[0], S8("llc")));
        BUSTER_TEST(arguments, string_equal(plan.steps[1].arguments.pointer[0], S8("clang")));
        BUSTER_TEST(arguments, plan.output_format == GPU_OUTPUT_AMDGCN_CODE_OBJECT);
    }
    {
        String8 inputs[] = {S8("shader.metal")};
        GpuTarget target = gpu_test_target(S8("air64-apple-macos"));
        GpuPipelinePlan air = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT));
        GpuPipelinePlan library = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, air.error == GPU_PIPELINE_ERROR_NONE && air.output_format == GPU_OUTPUT_METAL_AIR);
        BUSTER_TEST(arguments, gpu_test_step_has_argument(air, 0, S8("metal")));
        BUSTER_STRING_TEST(arguments, air.output_path, S8("shader.air"));
        BUSTER_TEST(arguments, library.error == GPU_PIPELINE_ERROR_NONE && library.step_count == 2);
        BUSTER_TEST(arguments, gpu_test_step_has_argument(library, 1, S8("metallib")));
        BUSTER_STRING_TEST(arguments, library.output_path, S8("shader.metallib"));
    }
    {
        String8 inputs[] = {S8("precompiled.air")};
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, gpu_test_options(inputs, 1, gpu_test_target(S8("metal")), GPU_PIPELINE_ACTION_OBJECT));
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
        BUSTER_TEST(arguments, plan.steps[0].kind == GPU_PIPELINE_STEP_COPY);
        BUSTER_STRING_TEST(arguments, plan.output_path, S8("precompiled.out.air"));
    }
    {
        String8 inputs[] = {S8("shader.hlsl")};
        GpuTarget target = gpu_test_target(S8("dxil-pc-shadermodel6.9-compute"));
        target.entry_point = S8("compute_main");
        GpuPipelineOptions options = gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_ASSEMBLY);
        options.capture_text_output = true;
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
        BUSTER_TEST(arguments, plan.output_format == GPU_OUTPUT_DXIL_ASSEMBLY && plan.output_is_temporary);
        BUSTER_TEST(arguments, plan.output_path.pointer[plan.output_path.length] == 0);
        BUSTER_TEST(arguments, plan.temporary_path_count >= 2 && plan.temporary_paths[0].pointer[plan.temporary_paths[0].length] == 0 &&
                                   plan.temporary_paths[1].pointer[plan.temporary_paths[1].length] == 0);
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("cs_6_9")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("compute_main")));
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-Fo")) && gpu_test_step_has_argument(plan, 0, S8("-Fc")));
    }
    {
        String8 ptx_inputs[] = {S8("a.cu"), S8("b.cu")};
        GpuPipelinePlan ptx = gpu_pipeline_plan(arena, gpu_test_options(ptx_inputs, 2, gpu_test_target(S8("nvptx64")), GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, ptx.error == GPU_PIPELINE_ERROR_INVALID_INPUT);
        String8 metal_inputs[] = {S8("shader.metal")};
        GpuPipelinePlan metal = gpu_pipeline_plan(arena, gpu_test_options(metal_inputs, 1, gpu_test_target(S8("metal")), GPU_PIPELINE_ACTION_ASSEMBLY));
        BUSTER_TEST(arguments, metal.error == GPU_PIPELINE_ERROR_UNSUPPORTED_ACTION);
        String8 hlsl_inputs[] = {S8("shader.hlsl")};
        GpuPipelinePlan fixed_spirv = gpu_pipeline_plan(arena, gpu_test_options(hlsl_inputs, 1, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_LINK));
        BUSTER_TEST(arguments, fixed_spirv.error == GPU_PIPELINE_ERROR_INVALID_INPUT);
    }

    {
        u8 spirv[] = {0x03, 0x02, 0x23, 0x07};
        u8 ptx[] = ".version 8.0\n.target sm_90\n";
        u8 air[] = {'B', 'C', 0xc0, 0xde};
        u8 metallib[] = {'M', 'T', 'L', 'B'};
        u8 dxil[] = {'D', 'X', 'B', 'C'};
        u8 elf_rel[20] = {0x7f, 'E', 'L', 'F', 2, 1};
        u8 elf_dyn[20] = {0x7f, 'E', 'L', 'F', 2, 1};
        elf_rel[16] = 1;
        elf_rel[18] = 224;
        elf_dyn[16] = 3;
        elf_dyn[18] = 224;
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_SPIRV_BINARY, (ByteSlice)BUSTER_ARRAY_TO_SLICE(spirv)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_CUDA_PTX, (ByteSlice)BUSTER_ARRAY_TO_SLICE(ptx)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_AMDGCN_OBJECT, (ByteSlice)BUSTER_ARRAY_TO_SLICE(elf_rel)));
        BUSTER_TEST(arguments, !gpu_artifact_has_expected_magic(GPU_OUTPUT_AMDGCN_CODE_OBJECT, (ByteSlice)BUSTER_ARRAY_TO_SLICE(elf_rel)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_AMDGCN_CODE_OBJECT, (ByteSlice)BUSTER_ARRAY_TO_SLICE(elf_dyn)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_METAL_AIR, (ByteSlice)BUSTER_ARRAY_TO_SLICE(air)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_METAL_LIBRARY, (ByteSlice)BUSTER_ARRAY_TO_SLICE(metallib)));
        BUSTER_TEST(arguments, gpu_artifact_has_expected_magic(GPU_OUTPUT_DXIL_CONTAINER, (ByteSlice)BUSTER_ARRAY_TO_SLICE(dxil)));
    }

    {
        String8 command_line[] = {
            S8("-target"), S8("amdgcn-amd-amdhsa"), S8("--gpu-arch=gfx1201"), S8("-x"), S8("hip"), S8("-c"),
            S8("--rocm-path=/opt/rocm"), S8("--gpu-clang=/tool/clang"), S8("-Xgpu=-mwavefrontsize64"), S8("kernel.hip"),
        };
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.has_gpu_target);
        BUSTER_TEST(arguments, invocation.gpu_target.kind == GPU_TARGET_AMDGCN);
        BUSTER_STRING_TEST(arguments, invocation.gpu_target.architecture, S8("gfx1201"));
        BUSTER_TEST(arguments, invocation.language == COMPILER_DRIVER_LANGUAGE_HIP && invocation.action == COMPILER_DRIVER_ACTION_OBJECT);
        BUSTER_STRING_TEST(arguments, invocation.rocm_path, S8("/opt/rocm"));
        BUSTER_STRING_TEST(arguments, invocation.gpu_tools.clang_path, S8("/tool/clang"));
        BUSTER_TEST(arguments, invocation.gpu_argument_count == 1);
        BUSTER_STRING_TEST(arguments, invocation.gpu_arguments[0], S8("-mwavefrontsize64"));
        BUSTER_TEST(arguments, invocation.system_include_path_count == 0 && invocation.library_count == 0 && invocation.linker_argument_count == 0);
    }
    {
        String8 command_line[] = {
            S8("-target=dxil-pc-shadermodel6.9-mesh"), S8("--gpu-entry=mesh_main"), S8("-S"), S8("shader.hlsl"),
        };
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.has_gpu_target);
        BUSTER_TEST(arguments, invocation.gpu_target.stage == GPU_SHADER_STAGE_MESH && invocation.gpu_target.shader_model_minor == 9);
        BUSTER_STRING_TEST(arguments, invocation.gpu_target.entry_point, S8("mesh_main"));
        BUSTER_TEST(arguments, invocation.language == COMPILER_DRIVER_LANGUAGE_AUTOMATIC);
    }
    {
        String8 command_line[] = {S8("-target=nvptx64"), S8("-lfoo"), S8("kernel.cu")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    {
        String8 command_line[] = {S8("-target=spirv"), S8("--shader-model=6.9"), S8("kernel.cl")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    {
        String8 command_line[] = {S8("-target=dxil"), S8("--sysroot=/sdk"), S8("shader.hlsl")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    {
        String8 command_line[] = {S8("-target=nvptx64"), S8("--cuda-path=/cuda"), S8("kernel.cl")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    }
    {
        String8 command_line[] = {S8("-target=nvptx64"), S8("-Ofast"), S8("kernel.cu")};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && invocation.optimization_level == 2);
    }

    return result;
}

#endif
