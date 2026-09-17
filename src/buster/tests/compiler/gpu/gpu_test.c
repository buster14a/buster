#include <buster/tests/compiler/gpu/gpu_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/arena.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

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

BUSTER_GLOBAL_LOCAL bool gpu_test_step_has_argument_sequence(GpuPipelinePlan plan, u32 step_index, String8* sequence, u32 sequence_count)
{
    bool result = false;
    if (step_index < plan.step_count && plan.steps[step_index].kind == GPU_PIPELINE_STEP_PROCESS)
    {
        SliceString8 arguments = plan.steps[step_index].arguments;
        for (u64 start = 0; start + sequence_count <= arguments.length && !result; start += 1)
        {
            bool matches = true;
            for (u32 index = 0; index < sequence_count; index += 1)
            {
                matches = matches && string_equal(arguments.pointer[start + index], sequence[index]);
            }
            result = matches;
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
        .temporary_directory = S8("owned-gpu-temporaries"),
        .language = GPU_SOURCE_LANGUAGE_AUTOMATIC,
        .action = action,
        .optimization_level = 2,
    };
}

BUSTER_GLOBAL_LOCAL bool gpu_test_path_is_within(String8 path, String8 directory)
{
    bool result = path.length > directory.length && string_starts_with_sequence(path, directory);
    if (result)
    {
        char8 separator = path.pointer[directory.length];
        result = separator == '/' || separator == '\\';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_test_path_has_kind(String8 path, OsFileKind kind)
{
    FileStats stats = os_file_replacement_target_stats(path);
    return stats.valid && stats.kind == kind;
}

BUSTER_GLOBAL_LOCAL bool gpu_test_file_equals(Arena* arena, String8 path, ByteSlice expected)
{
    ByteSlice actual = file_read(arena, path, (FileReadOptions){0});
    return actual.pointer && actual.length == expected.length && memory_compare(actual.pointer, expected.pointer, expected.length);
}

typedef struct GpuTestConcurrentExecutions GpuTestConcurrentExecutions;
struct GpuTestConcurrentExecutions
{
    GpuPipelineOptions options;
    GpuPipelineResult results[2];
    Arena* arenas[2];
    u64 worker_count;
};

BUSTER_GLOBAL_LOCAL ThreadReturnType gpu_test_concurrent_execute(void* argument)
{
    GpuTestConcurrentExecutions* executions = (GpuTestConcurrentExecutions*)argument;
    u64 index = lane_index();
    if (index == 0)
    {
        executions->worker_count = lane_count();
    }
    if (index < BUSTER_ARRAY_LENGTH(executions->results))
    {
        Arena* arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
        executions->arenas[index] = arena;
        if (arena)
        {
            executions->results[index] = gpu_pipeline_execute(arena, executions->options);
        }
    }
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
        CPreprocessorOperation operations[] = {
            {S8("ORDERED"), C_PREPROCESSOR_OPERATION_UNDEFINE},
            {S8("ORDERED=1"), C_PREPROCESSOR_OPERATION_DEFINE},
            {S8("ORDERED"), C_PREPROCESSOR_OPERATION_UNDEFINE},
            {S8("FUNCTION(x)=x + x"), C_PREPROCESSOR_OPERATION_DEFINE},
        };
        String8 expected[] = {S8("-UORDERED"), S8("-DORDERED=1"), S8("-UORDERED"), S8("-DFUNCTION(x)=x + x")};
        String8 inputs[][1] = {{S8("kernel.cl")}, {S8("shader.metal")}, {S8("shader.hlsl")}};
        String8 targets[] = {S8("spirv64"), S8("air64-apple-macos"), S8("dxil-pc-shadermodel6.9-compute")};
        for (u32 consumer = 0; consumer < BUSTER_ARRAY_LENGTH(targets); consumer += 1)
        {
            GpuPipelineOptions options = gpu_test_options(inputs[consumer], 1, gpu_test_target(targets[consumer]), GPU_PIPELINE_ACTION_OBJECT);
            options.macro_operations = operations;
            options.macro_operation_count = BUSTER_ARRAY_LENGTH(operations);
            GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
            BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count != 0);
            BUSTER_TEST(arguments, gpu_test_step_has_argument_sequence(plan, 0, expected, BUSTER_ARRAY_LENGTH(expected)));
        }

        String8 legacy_definitions[] = {S8("FIRST=1"), S8("SECOND(x)=x")};
        String8 legacy_undefinitions[] = {S8("FIRST")};
        String8 legacy_expected[] = {S8("-DFIRST=1"), S8("-DSECOND(x)=x"), S8("-UFIRST")};
        GpuPipelineOptions legacy = gpu_test_options(inputs[0], 1, gpu_test_target(targets[0]), GPU_PIPELINE_ACTION_OBJECT);
        legacy.definitions = legacy_definitions;
        legacy.undefinitions = legacy_undefinitions;
        legacy.definition_count = BUSTER_ARRAY_LENGTH(legacy_definitions);
        legacy.undefinition_count = BUSTER_ARRAY_LENGTH(legacy_undefinitions);
        GpuPipelinePlan legacy_plan = gpu_pipeline_plan(arena, legacy);
        BUSTER_TEST(arguments, legacy_plan.error == GPU_PIPELINE_ERROR_NONE && legacy_plan.step_count != 0);
        BUSTER_TEST(arguments, gpu_test_step_has_argument_sequence(legacy_plan, 0, legacy_expected, BUSTER_ARRAY_LENGTH(legacy_expected)));
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
        // Real LLVM 18 llc rejects -g. Debug-enabled IR inputs must remain
        // usable in every llc pipeline; metadata is carried by the input IR.
        String8 inputs[] = {S8("kernel.ll")};
        String8 triples[] = {S8("spirv64"), S8("nvptx64"), S8("amdgcn")};
        String8 architectures[] = {{0}, S8("sm_70"), S8("gfx900")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(triples); index += 1)
        {
            GpuTarget target = gpu_test_target(triples[index]);
            target.architecture = architectures[index];
            GpuPipelineOptions options = gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT);
            options.debug_info = true;
            GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
            BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE && plan.step_count == 1);
            BUSTER_TEST(arguments, gpu_test_plan_has_tool(plan, S8("llc")));
            BUSTER_TEST(arguments, !gpu_test_step_has_argument(plan, 0, S8("-g")));
        }
        // Source frontends still receive the requested debug generation flag.
        inputs[0] = S8("kernel.cl");
        GpuTarget target = gpu_test_target(S8("amdgcn"));
        target.architecture = S8("gfx900");
        GpuPipelineOptions options = gpu_test_options(inputs, 1, target, GPU_PIPELINE_ACTION_OBJECT);
        options.debug_info = true;
        GpuPipelinePlan plan = gpu_pipeline_plan(arena, options);
        BUSTER_TEST(arguments, plan.error == GPU_PIPELINE_ERROR_NONE);
        BUSTER_TEST(arguments, gpu_test_step_has_argument(plan, 0, S8("-g")));
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
        BUSTER_TEST(arguments, gpu_test_path_is_within(plan.temporary_paths[0], options.temporary_directory) &&
                                   gpu_test_path_is_within(plan.temporary_paths[1], options.temporary_directory));
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


    {
        u8 payload_data[] = {'o', 'w', 'n', 'e', 'd'};
        ByteSlice payload = (ByteSlice)BUSTER_ARRAY_TO_SLICE(payload_data);
        String8 claimed = buster_test_temporary_path(arena, S8("gpu-exclusive-claim"), S8(""));
        String8 referent = buster_test_temporary_path(arena, S8("gpu-exclusive-referent"), S8(".bin"));
        BUSTER_TEST(arguments, os_file_delete(claimed));
        BUSTER_TEST(arguments, os_file_delete(referent));

        BUSTER_TEST(arguments, file_write(claimed, payload));
        OsDirectoryCreateResult file_collision = os_make_directory_exclusive(claimed);
        BUSTER_TEST(arguments, !file_collision.error.v && file_collision.already_exists);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, claimed, payload));
        BUSTER_TEST(arguments, os_file_delete(claimed));

        BUSTER_TEST(arguments, os_make_directory_attempt(claimed));
        OsDirectoryCreateResult directory_collision = os_make_directory_exclusive(claimed);
        BUSTER_TEST(arguments, !directory_collision.error.v && directory_collision.already_exists);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(claimed, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, os_directory_delete(claimed));

#if !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
        BUSTER_TEST(arguments, file_write(referent, payload));
        BUSTER_TEST(arguments, symlink((const char*)referent.pointer, (const char*)claimed.pointer) == 0);
        OsDirectoryCreateResult link_collision = os_make_directory_exclusive(claimed);
        BUSTER_TEST(arguments, !link_collision.error.v && link_collision.already_exists);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(claimed, OS_FILE_KIND_LINK));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, referent, payload));
        BUSTER_TEST(arguments, os_file_delete(claimed));
        BUSTER_TEST(arguments, os_file_delete(referent));
#endif

        OsDirectoryCreateResult created = os_make_directory_exclusive(claimed);
        BUSTER_TEST(arguments, !created.error.v && !created.already_exists);
#if !BUSTER_WINDOWS
        struct stat claimed_stats = {0};
        BUSTER_TEST(arguments, stat((const char*)claimed.pointer, &claimed_stats) == 0 && (claimed_stats.st_mode & 0777) == 0700);
#endif
        BUSTER_TEST(arguments, os_directory_delete(claimed));
    }

    {
        u8 valid_spirv_data[] = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x00, 0x00};
        u8 invalid_spirv_data[] = {'n', 'o', 'p', 'e'};
        u8 sentinel_data[] = {'o', 'l', 'd', '-', 'o', 'u', 't', 'p', 'u', 't'};
        ByteSlice valid_spirv = (ByteSlice)BUSTER_ARRAY_TO_SLICE(valid_spirv_data);
        ByteSlice invalid_spirv = (ByteSlice)BUSTER_ARRAY_TO_SLICE(invalid_spirv_data);
        ByteSlice sentinel = (ByteSlice)BUSTER_ARRAY_TO_SLICE(sentinel_data);
        String8 input = buster_test_temporary_path(arena, S8("gpu-private-input"), S8(".spv"));
        String8 output = buster_test_temporary_path(arena, S8("gpu-private-output"), S8(".spv"));
        String8 referent = buster_test_temporary_path(arena, S8("gpu-private-referent"), S8(".spv"));
        String8 input_base = string_slice(input, 0, input.length - S8(".spv").length);
        String8 legacy_temporary = string_format_z(arena, S8("{S8}.buster-gpu-0.spv"), input_base);
        String8 inputs[] = {input};
        GpuPipelineOptions options = gpu_test_options(inputs, 1, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_LINK);
        options.output_path = output;
        options.temporary_directory = (String8){0};

        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_file_delete(referent));
        BUSTER_TEST(arguments, os_file_delete(legacy_temporary));
        BUSTER_TEST(arguments, file_write(input, valid_spirv));
        BUSTER_TEST(arguments, file_write(output, sentinel));
        BUSTER_TEST(arguments, file_write(legacy_temporary, sentinel));

        GpuPipelineResult published = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, published.error == GPU_PIPELINE_ERROR_NONE && published.artifact.format == GPU_OUTPUT_SPIRV_BINARY);
        BUSTER_STRING_TEST(arguments, published.artifact.path, output);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, valid_spirv));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, legacy_temporary, sentinel));
        BUSTER_TEST(arguments, published.temporary_directory.length != 0 &&
                                   gpu_test_path_has_kind(published.temporary_directory, OS_FILE_KIND_MISSING));

        BUSTER_TEST(arguments, file_write(input, invalid_spirv));
        BUSTER_TEST(arguments, file_write(output, sentinel));
        GpuPipelineResult invalid = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, invalid.error == GPU_PIPELINE_ERROR_INVALID_ARTIFACT);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, sentinel));
        BUSTER_TEST(arguments, invalid.temporary_directory.length != 0 &&
                                   gpu_test_path_has_kind(invalid.temporary_directory, OS_FILE_KIND_MISSING));

        BUSTER_TEST(arguments, file_write(input, valid_spirv));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_make_directory_attempt(output));
        GpuPipelineResult directory_target = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, directory_target.error == GPU_PIPELINE_ERROR_FILE_WRITE);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(output, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, gpu_test_path_has_kind(directory_target.temporary_directory, OS_FILE_KIND_MISSING));
        BUSTER_TEST(arguments, os_directory_delete(output));

#if !BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
        BUSTER_TEST(arguments, file_write(referent, sentinel));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, symlink((const char*)referent.pointer, (const char*)output.pointer) == 0);
        GpuPipelineResult link_target = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, link_target.error == GPU_PIPELINE_ERROR_FILE_WRITE);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(output, OS_FILE_KIND_LINK));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, referent, sentinel));
        BUSTER_TEST(arguments, gpu_test_path_has_kind(link_target.temporary_directory, OS_FILE_KIND_MISSING));
        BUSTER_TEST(arguments, os_file_delete(output));
#endif

        options.save_temporaries = true;
        GpuTestConcurrentExecutions concurrent = {.options = options};
        lane_run(BUSTER_ARRAY_LENGTH(concurrent.results), &gpu_test_concurrent_execute, &concurrent);
        if (concurrent.worker_count < BUSTER_ARRAY_LENGTH(concurrent.results))
        {
            concurrent.arenas[1] = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
            if (concurrent.arenas[1])
            {
                concurrent.results[1] = gpu_pipeline_execute(concurrent.arenas[1], options);
            }
        }
        BUSTER_TEST(arguments, concurrent.arenas[0] != 0 && concurrent.arenas[1] != 0);
        BUSTER_TEST(arguments, concurrent.results[0].error == GPU_PIPELINE_ERROR_NONE &&
                                   concurrent.results[1].error == GPU_PIPELINE_ERROR_NONE);
        BUSTER_TEST(arguments, concurrent.results[0].temporary_directory.length && concurrent.results[1].temporary_directory.length &&
                                   !string_equal(concurrent.results[0].temporary_directory, concurrent.results[1].temporary_directory));
        BUSTER_TEST(arguments, gpu_test_path_has_kind(concurrent.results[0].temporary_directory, OS_FILE_KIND_DIRECTORY) &&
                                   gpu_test_path_has_kind(concurrent.results[1].temporary_directory, OS_FILE_KIND_DIRECTORY));
#if !BUSTER_WINDOWS
        struct stat saved_stats = {0};
        BUSTER_TEST(arguments, stat((const char*)concurrent.results[0].temporary_directory.pointer, &saved_stats) == 0 &&
                                   (saved_stats.st_mode & 0777) == 0700);
#endif
        for (u32 execution = 0; execution < BUSTER_ARRAY_LENGTH(concurrent.results); execution += 1)
        {
            if (concurrent.results[execution].temporary_directory.length)
            {
                BUSTER_TEST(arguments, os_directory_delete(concurrent.results[execution].temporary_directory));
            }
            if (concurrent.arenas[execution])
            {
                BUSTER_TEST(arguments, arena_destroy(concurrent.arenas[execution], 1));
            }
        }

        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_file_delete(referent));
        BUSTER_TEST(arguments, os_file_delete(legacy_temporary));
    }

    return result;
}

#endif
