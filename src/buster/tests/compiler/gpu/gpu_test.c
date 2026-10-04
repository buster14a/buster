#include <buster/tests/compiler/gpu/gpu_test.h>

#if BUSTER_INCLUDE_TESTS

#include <buster/lib/arena.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/compiler/gpu/gpu_internal.h>
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
    bool result = directory.length && path.length > directory.length && string_starts_with_sequence(path, directory);
    if (result && directory.pointer[directory.length - 1] != '/' && directory.pointer[directory.length - 1] != '\\')
    {
        char8 separator = path.pointer[directory.length];
        result = separator == '/' || separator == '\\';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool gpu_test_workspace_is_child(String8 path, String8 root)
{
    bool result = gpu_test_path_is_within(path, root);
    if (result)
    {
        u64 begin = root.length;
        while (begin < path.length && (path.pointer[begin] == '/' || path.pointer[begin] == '\\'))
        {
            begin += 1;
        }
        String8 child = string_slice(path, begin, path.length);
        String8 prefix = S8(".buster-gpu-");
        String8 suffix = S8(".temps");
        result = child.length > prefix.length + suffix.length && string_starts_with_sequence(child, prefix) &&
                 memory_compare(child.pointer + child.length - suffix.length, suffix.pointer, suffix.length);
        for (u64 index = 0; result && index < child.length; index += 1)
        {
            result = child.pointer[index] != '/' && child.pointer[index] != '\\';
        }
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
    GpuPipelineOptions options[2];
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
            executions->results[index] = gpu_pipeline_execute(arena, executions->options[index]);
        }
    }
}

BUSTER_GLOBAL_LOCAL String8 gpu_test_default_scratch_root(Arena* arena)
{
#if BUSTER_WINDOWS
    BUSTER_UNUSED(arena);
    String8 result = os_get_environment_variable(S8("TEMP"));
    if (!result.length)
    {
        result = os_get_environment_variable(S8("TMP"));
    }
#else
    String8 result = os_get_environment_variable(S8("TMPDIR"));
    if (!result.length)
    {
#if BUSTER_ANDROID
        BUSTER_UNUSED(arena);
        result = buster_android_internal_data_path;
#elif BUSTER_IOS
        String8 home = os_get_environment_variable(S8("HOME"));
        if (home.length)
        {
            result = string_format_z(arena, S8("{S8}/tmp"), home);
        }
#else
        BUSTER_UNUSED(arena);
        result = S8("/tmp");
#endif
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult gpu_test_scratch_roots(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 root = buster_test_temporary_path(arena, S8("gpu-scratch-root"), S8(""));
    String8 input = buster_test_temporary_path(arena, S8("gpu-scratch-input"), S8(".spv"));
    String8 output = buster_test_temporary_path(arena, S8("gpu-scratch-output"), S8(".spv"));
    String8 foreign = string_format_z(arena, S8("{S8}/foreign.bin"), root);
    u8 good_data[] = {0x03, 0x02, 0x23, 0x07, 0, 0, 0, 0};
    ByteSlice good = (ByteSlice)BUSTER_ARRAY_TO_SLICE(good_data);
    String8 sentinel = S8("preserve-parent-and-output");
    ByteSlice sentinel_bytes = {(u8*)sentinel.pointer, sentinel.length};
    OsDirectoryCreateResult created = os_make_directory_exclusive(root);
    BUSTER_TEST(arguments, created.created && !created.error.v);
    if (created.created)
    {
        BUSTER_TEST(arguments, file_write(input, good));
        BUSTER_TEST(arguments, file_write(output, sentinel_bytes));
        BUSTER_TEST(arguments, file_write(foreign, sentinel_bytes));
        String8 inputs[] = {input};
        GpuPipelineOptions options = gpu_test_options(inputs, 1, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_OBJECT);
        options.output_path = output;
        options.temporary_directory = root;
        GpuPipelineResult explicit_root = gpu_pipeline_execute(arena, options);
        BUSTER_TEST_RAW(arguments, explicit_root.error == GPU_PIPELINE_ERROR_NONE, explicit_root.diagnostic);
        BUSTER_TEST(arguments, explicit_root.published && !explicit_root.cleanup_failed);
        BUSTER_TEST(arguments, gpu_test_path_is_within(explicit_root.temporary_directory, root));
        BUSTER_TEST(arguments, gpu_test_path_has_kind(explicit_root.temporary_directory, OS_FILE_KIND_MISSING));
        BUSTER_TEST(arguments, gpu_test_path_has_kind(root, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, foreign, sentinel_bytes));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, good));

        options.temporary_directory = string_format_z(arena, S8("{S8}/"), root);
        options.save_temporaries = true;
        GpuPipelineResult saved = gpu_pipeline_execute(arena, options);
        BUSTER_TEST_RAW(arguments, saved.error == GPU_PIPELINE_ERROR_NONE, saved.diagnostic);
        bool saved_owned = gpu_test_workspace_is_child(saved.temporary_directory, root);
        BUSTER_TEST(arguments, saved_owned);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(saved.temporary_directory, OS_FILE_KIND_DIRECTORY));
        if (saved_owned)
        {
            BUSTER_TEST(arguments, os_directory_delete(saved.temporary_directory));
        }
        BUSTER_TEST(arguments, gpu_test_path_has_kind(root, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, foreign, sentinel_bytes));

        // Do not replace the real platform default with the fixture root.
        // This COPY path runs on mobile too, where subprocesses are unavailable.
        options.temporary_directory = (String8){0};
        GpuPipelineResult default_root = gpu_pipeline_execute(arena, options);
        String8 expected_root = gpu_test_default_scratch_root(arena);
        BUSTER_TEST_RAW(arguments, default_root.error == GPU_PIPELINE_ERROR_NONE, default_root.diagnostic);
        bool default_owned = gpu_test_workspace_is_child(default_root.temporary_directory, expected_root);
        BUSTER_TEST(arguments, default_owned);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(default_root.temporary_directory, OS_FILE_KIND_DIRECTORY));
        if (default_owned)
        {
            BUSTER_TEST(arguments, os_directory_delete(default_root.temporary_directory));
        }
        arguments->show(arguments, S8("GPU_SCRATCH_DEFAULT root={S8} owned_child={u32} error={u32}\n"), expected_root,
                        (u32)default_owned, (u32)default_root.error);

        options.temporary_directory = foreign;
        options.save_temporaries = false;
        BUSTER_TEST(arguments, file_write(output, sentinel_bytes));
        GpuPipelineResult unavailable = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, unavailable.error == GPU_PIPELINE_ERROR_FILE_WRITE && unavailable.process_result == PROCESS_RESULT_FAILED);
        BUSTER_TEST(arguments, !unavailable.temporary_directory.length && !unavailable.published);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, sentinel_bytes));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, foreign, sentinel_bytes));

        options.temporary_directory = (String8){.length = 1};
        GpuPipelineResult malformed_root = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, malformed_root.error == GPU_PIPELINE_ERROR_INVALID_INPUT && malformed_root.process_result == PROCESS_RESULT_FAILED);
        BUSTER_TEST(arguments, !malformed_root.temporary_directory.length && !malformed_root.published);
        BUSTER_STRING_TEST(arguments, malformed_root.diagnostic, S8("GPU scratch root requires a valid path"));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, sentinel_bytes));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, foreign, sentinel_bytes));

        String8 invalid_inputs[] = {S8("gpu-invalid-input.unknown")};
        options.input_paths = invalid_inputs;
        options.temporary_directory = foreign;
        GpuPipelineResult invalid = gpu_pipeline_execute(arena, options);
        BUSTER_TEST(arguments, invalid.error == GPU_PIPELINE_ERROR_INVALID_INPUT && invalid.process_result == PROCESS_RESULT_FAILED);
        BUSTER_TEST(arguments, !invalid.temporary_directory.length && !invalid.published);
        // Reusing arena storage after an invalid plan must not corrupt its
        // allocated diagnostic, even when the scratch root is unusable.
        u8* overwrite = arena_allocate(arena, u8, BUSTER_KB(64));
        for (u64 index = 0; index < BUSTER_KB(64); index += 1)
        {
            overwrite[index] = 0x5a;
        }
        BUSTER_STRING_TEST(arguments, invalid.diagnostic, S8("cannot infer GPU source language from gpu-invalid-input.unknown; use -x"));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, sentinel_bytes));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, foreign, sentinel_bytes));
        BUSTER_TEST(arguments, os_directory_delete(root));
        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
    }
    return result;
}

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool gpu_test_environment_key_equal(String8 left, String8 right)
{
    bool result = left.length == right.length;
    for (u64 index = 0; result && index < left.length; index += 1)
    {
        char8 a = left.pointer[index];
        char8 b = right.pointer[index];
#if BUSTER_WINDOWS
        if (a >= 'a' && a <= 'z')
        {
            a = (char8)(a - ('a' - 'A'));
        }
        if (b >= 'a' && b <= 'z')
        {
            b = (char8)(b - ('a' - 'A'));
        }
#endif
        result = a == b;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessWaitResult gpu_test_scratch_child(Arena* arena, SliceString8 arguments, String8* override_keys, String8* override_values,
                                                            u32 override_count)
{
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    String8* keys = arena_allocate(arena, String8, inherited_keys.length + override_count);
    String8* values = arena_allocate(arena, String8, inherited_keys.length + override_count);
    u64 count = override_count;
    for (u32 index = 0; index < override_count; index += 1)
    {
        keys[index] = override_keys[index];
        values[index] = override_values[index];
    }
    for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
    {
        bool overridden = false;
        for (u32 index = 0; index < override_count && !overridden; index += 1)
        {
            overridden = gpu_test_environment_key_equal(inherited_keys.pointer[inherited], override_keys[index]);
        }
        if (!overridden)
        {
            keys[count] = inherited_keys.pointer[inherited];
            values[count] = inherited_values.pointer[inherited];
            count += 1;
        }
    }
    ProcessSpawnResult spawn = os_process_spawn(arguments, (SliceString8){keys, count}, (SliceString8){values, count},
                                                (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                                                      .new_process_group = true});
    ProcessWaitResult result;
    if (spawn.error.v || spawn.failure != PROCESS_SPAWN_FAILURE_NONE)
    {
        result = (ProcessWaitResult){.result = PROCESS_RESULT_FAILED};
    }
    else
    {
        result = os_process_wait_deadline(arena, spawn, UINT64_C(30000000));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 gpu_test_child_stream(ProcessWaitResult child, StandardStream stream)
{
    ByteSlice bytes = child.streams[stream];
    return (String8){(char8*)bytes.pointer, bytes.length};
}

BUSTER_GLOBAL_LOCAL String8 gpu_test_saved_workspace(Arena* arena, ProcessWaitResult child)
{
    String8 result = {0};
    String8 marker = S8("GPU temporary files: ");
    for (u32 stream = STANDARD_STREAM_OUTPUT; stream <= STANDARD_STREAM_ERROR && !result.length; stream += 1)
    {
        String8 text = gpu_test_child_stream(child, (StandardStream)stream);
        u64 index = string_first_sequence(text, marker);
        if (index != BUSTER_STRING_NO_MATCH)
        {
            u64 begin = index + marker.length;
            u64 end = begin;
            while (end < text.length && text.pointer[end] != '\n' && text.pointer[end] != '\r')
            {
                end += 1;
            }
            result = string_format_z(arena, S8("{S8}"), string_slice(text, begin, end));
        }
    }
    return result;
}

#if BUSTER_WINDOWS
BUSTER_GLOBAL_LOCAL UnitTestResult gpu_test_windows_scratch_environment(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    String8 first = buster_test_temporary_path(arena, S8("gpu-env-temp"), S8(""));
    String8 second = buster_test_temporary_path(arena, S8("gpu-env-tmp"), S8(""));
    String8 input = buster_test_temporary_path(arena, S8("gpu-env-input"), S8(".spv"));
    String8 output = buster_test_temporary_path(arena, S8("gpu-env-output"), S8(".spv"));
    String8 unavailable = buster_test_temporary_path(arena, S8("gpu-env-unavailable"), S8(".bin"));
    u8 good_data[] = {0x03, 0x02, 0x23, 0x07, 0, 0, 0, 0};
    ByteSlice good = (ByteSlice)BUSTER_ARRAY_TO_SLICE(good_data);
    String8 sentinel = S8("keep-existing-output");
    ByteSlice sentinel_bytes = {(u8*)sentinel.pointer, sentinel.length};
    OsDirectoryCreateResult first_created = os_make_directory_exclusive(first);
    OsDirectoryCreateResult second_created = os_make_directory_exclusive(second);
    BUSTER_TEST(arguments, first_created.created && second_created.created);
    if (first_created.created && second_created.created)
    {
        first = os_path_absolute_lexical(arena, first, true);
        second = os_path_absolute_lexical(arena, second, true);
        BUSTER_TEST(arguments, file_write(input, good));
        BUSTER_TEST(arguments, file_write(unavailable, sentinel_bytes));
        String8 executable = os_path_absolute_lexical(arena, program_state->input.arguments.pointer[0], true);
        String8 command[] = {executable, S8("cc"), S8("-target=spirv64"), S8("-c"), input, S8("-o"), output, S8("--save-temps")};
        String8 keys[] = {S8("TEMP"), S8("TMP")};
        String8 values[] = {first, second};
        for (u32 control = 0; control < 4; control += 1)
        {
            BUSTER_TEST(arguments, file_write(output, sentinel_bytes));
            values[0] = control == 0 ? first : control == 3 ? unavailable : (String8){0};
            values[1] = control == 2 ? (String8){0} : second;
            ProcessWaitResult child = gpu_test_scratch_child(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values,
                                                           BUSTER_ARRAY_LENGTH(keys));
            BUSTER_TEST(arguments, !child.timed_out && !child.capture_failed && !child.process_tree_cleanup_failed);
            if (control < 2)
            {
                String8 workspace = gpu_test_saved_workspace(arena, child);
                String8 selected = control == 0 ? first : second;
                bool owned = gpu_test_workspace_is_child(workspace, selected);
                BUSTER_TEST_RAW(arguments, child.result == PROCESS_RESULT_SUCCESS, gpu_test_child_stream(child, STANDARD_STREAM_ERROR));
                BUSTER_TEST(arguments, owned && gpu_test_path_has_kind(workspace, OS_FILE_KIND_DIRECTORY));
                BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, good));
                if (owned)
                {
                    BUSTER_TEST(arguments, os_directory_delete(workspace));
                }
            }
            else
            {
                BUSTER_TEST(arguments, child.result == PROCESS_RESULT_FAILED);
                BUSTER_TEST(arguments, !gpu_test_saved_workspace(arena, child).length);
                BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, sentinel_bytes));
                String8 diagnostic = gpu_test_child_stream(child, STANDARD_STREAM_ERROR);
                BUSTER_TEST(arguments, string_first_sequence(diagnostic, control == 2 ? S8("no GPU scratch root is configured")
                                                                                     : S8("could not create a private GPU temporary directory under")) != BUSTER_STRING_NO_MATCH);
            }
        }
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, unavailable, sentinel_bytes));
        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_file_delete(unavailable));
    }
    if (first_created.created)
    {
        BUSTER_TEST(arguments, os_directory_delete(first));
    }
    if (second_created.created)
    {
        BUSTER_TEST(arguments, os_directory_delete(second));
    }
    return result;
}
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool gpu_test_directory_entry_count(String8 path, u64* count)
{
    *count = 0;
    DIR* directory = opendir((const char*)path.pointer);
    bool result = directory != 0;
    if (directory)
    {
        errno = 0;
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            String8 name = string_from_pointer((const char8*)entry->d_name);
            if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
            {
                *count += 1;
            }
        }
        result = errno == 0;
        if (closedir(directory) != 0)
        {
            result = false;
        }
    }
    return result;
}
#endif
#endif

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL UnitTestResult gpu_test_readonly_source_scratch(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 effective_uid = (u64)geteuid();
    BUSTER_TEST(arguments, effective_uid != 0);
    String8 fixture = buster_test_temporary_path(arena, S8("gpu-readonly-fixture"), S8(""));
    OsDirectoryCreateResult fixture_created = os_make_directory_exclusive(fixture);
    BUSTER_TEST(arguments, fixture_created.created && !fixture_created.error.v);
    if (fixture_created.created)
    {
        fixture = os_path_absolute_lexical(arena, fixture, true);
        String8 source = string_format_z(arena, S8("{S8}/source"), fixture);
        String8 scratch = string_format_z(arena, S8("{S8}/scratch"), fixture);
        String8 writable = string_format_z(arena, S8("{S8}/writable"), fixture);
        String8 input = string_format_z(arena, S8("{S8}/kernel.cu"), source);
        String8 writable_input = string_format_z(arena, S8("{S8}/kernel.cu"), writable);
        String8 default_output = string_format_z(arena, S8("{S8}/kernel.ptx"), writable);
        String8 output = string_format_z(arena, S8("{S8}/named.ptx"), writable);
        String8 tool = string_format_z(arena, S8("{S8}/tool.sh"), fixture);
        String8 marker = string_format_z(arena, S8("{S8}/tool-invoked"), fixture);
        String8 unavailable = string_format_z(arena, S8("{S8}/not-a-directory"), fixture);
        String8 source_bytes = S8("//GPU_PERMISSION_INPUT\n__global__ void kernel(void) {}\n");
        String8 preprocessed = S8("int gpu_permission_preprocessed;\n");
        String8 assembly = S8(".version 7.0\n.target sm_70\n.address_size 64\n");
        String8 script = S8("#!/bin/sh\nset -eu\nmode=assembly\ninput=\noutput=\nwhile [ \"$#\" -gt 0 ]; do\n    argument=$1\n    shift\n    case \"$argument\" in\n        -fsyntax-only) mode=syntax ;;\n        -E) mode=preprocess ;;\n        -o) output=$1; shift ;;\n        *.cu) input=$argument ;;\n    esac\ndone\nIFS= read -r first < \"$input\"\n[ \"$first\" = '//GPU_PERMISSION_INPUT' ]\nprintf '%s\\n' \"$mode\" >> \"${0%/*}/tool-invoked\"\nif [ \"$mode\" != syntax ]; then\n    [ -n \"$output\" ]\n    if [ \"$mode\" = preprocess ]; then\n        printf 'int gpu_permission_preprocessed;\\n' > \"$output\"\n    else\n        printf '.version 7.0\\n.target sm_70\\n.address_size 64\\n' > \"$output\"\n    fi\nfi\n");
        OsDirectoryCreateResult source_created = os_make_directory_exclusive(source);
        OsDirectoryCreateResult scratch_created = os_make_directory_exclusive(scratch);
        OsDirectoryCreateResult writable_created = os_make_directory_exclusive(writable);
        BUSTER_TEST(arguments, source_created.created && scratch_created.created && writable_created.created);
        bool ready = source_created.created && scratch_created.created && writable_created.created;
        if (ready)
        {
            ready = file_write(input, (ByteSlice){(u8*)source_bytes.pointer, source_bytes.length}) &&
                    file_write(writable_input, (ByteSlice){(u8*)source_bytes.pointer, source_bytes.length}) &&
                    file_write(tool, (ByteSlice){(u8*)script.pointer, script.length}) &&
                    file_write(marker, (ByteSlice){(u8*)S8("").pointer, 0}) &&
                    file_write(unavailable, (ByteSlice){(u8*)source_bytes.pointer, source_bytes.length}) &&
                    chmod((const char*)tool.pointer, 0700) == 0;
            BUSTER_TEST(arguments, ready);
        }
        bool read_only = ready && chmod((const char*)source.pointer, 0555) == 0;
        BUSTER_TEST(arguments, read_only);
        if (read_only && effective_uid != 0)
        {
            String8 denied_path = string_format_z(arena, S8("{S8}/denied-probe"), source);
            OsDirectoryCreateResult denied = os_make_directory_exclusive(denied_path);
            BUSTER_TEST(arguments, denied.error.v && !denied.created && !denied.already_exists);
            if (denied.created)
            {
                BUSTER_TEST(arguments, os_directory_delete(denied_path));
            }
            u64 entries = 0;
            BUSTER_TEST(arguments, gpu_test_directory_entry_count(source, &entries) && entries == 1);
            arguments->show(arguments, S8("GPU_READONLY_SOURCE_PERMISSION euid={u64} denied_error={u32} source_entries={u64}\n"),
                            effective_uid, denied.error.v, entries);
            String8 executable = os_path_absolute_lexical(arena, program_state->input.arguments.pointer[0], true);
            BUSTER_TEST(arguments, executable.length != 0);
            String8 keys[] = {S8("TMPDIR"), S8("BUSTER_GPU_CLANG")};
            String8 values[] = {scratch, tool};
            String8 expected_calls = S8("");
            for (u32 control = 0; control < 10; control += 1)
            {
                String8 action = control == 1 || control == 6 ? S8("-E") : control == 2 || control == 3 ? S8("-S") :
                                 control == 4 || control == 5 ? S8("-c") : S8("-fsyntax-only");
                String8 command[8] = {executable, S8("cc"), S8("-target=nvptx64"), action, control == 5 ? writable_input : input};
                u64 command_count = 5;
                if (control == 3)
                {
                    command[command_count++] = S8("-o");
                    command[command_count++] = output;
                }
                if (control == 6 || control == 9)
                {
                    command[command_count++] = S8("--save-temps");
                }
                values[0] = control == 7 ? source : control == 8 ? unavailable : control == 9 ? (String8){0} : scratch;
                ProcessWaitResult child = gpu_test_scratch_child(arena, (SliceString8){command, command_count}, keys, values,
                                                               BUSTER_ARRAY_LENGTH(keys));
                BUSTER_TEST(arguments, !child.timed_out && !child.capture_failed && !child.process_tree_cleanup_failed);
                bool succeeds = control != 4 && control != 7 && control != 8;
                BUSTER_TEST_RAW(arguments, child.result == (succeeds ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED),
                                gpu_test_child_stream(child, STANDARD_STREAM_ERROR));
                if (control < 7 || control == 9)
                {
                    String8 mode = control == 0 || control == 9 ? S8("syntax") : control == 1 || control == 6 ? S8("preprocess") : S8("assembly");
                    expected_calls = string_format_z(arena, S8("{S8}{S8}\n"), expected_calls, mode);
                }
                BUSTER_TEST(arguments, gpu_test_file_equals(arena, marker, (ByteSlice){(u8*)expected_calls.pointer, expected_calls.length}));
                BUSTER_TEST(arguments, gpu_test_directory_entry_count(source, &entries) && entries == 1);
                u64 source_entries = entries;
                BUSTER_TEST(arguments, gpu_test_file_equals(arena, input, (ByteSlice){(u8*)source_bytes.pointer, source_bytes.length}));
                if (control == 1 || control == 2 || control == 6)
                {
                    BUSTER_STRING_TEST(arguments, gpu_test_child_stream(child, STANDARD_STREAM_OUTPUT), control == 2 ? assembly : preprocessed);
                }
                if (control == 3 || control == 5)
                {
                    BUSTER_TEST(arguments, gpu_test_file_equals(arena, control == 3 ? output : default_output,
                                                               (ByteSlice){(u8*)assembly.pointer, assembly.length}));
                }
                if (control == 6 || control == 9)
                {
                    String8 workspace = gpu_test_saved_workspace(arena, child);
                    String8 selected = control == 9 ? S8("/tmp") : scratch;
                    bool owned = gpu_test_workspace_is_child(workspace, selected);
                    BUSTER_TEST(arguments, owned && gpu_test_path_has_kind(workspace, OS_FILE_KIND_DIRECTORY));
                    if (owned)
                    {
                        BUSTER_TEST(arguments, os_directory_delete(workspace));
                    }
                }
                else
                {
                    BUSTER_TEST(arguments, !gpu_test_saved_workspace(arena, child).length);
                }
                BUSTER_TEST(arguments, gpu_test_directory_entry_count(scratch, &entries) && entries == 0);
                if (control == 7 || control == 8)
                {
                    BUSTER_TEST(arguments, string_first_sequence(gpu_test_child_stream(child, STANDARD_STREAM_ERROR),
                                                               S8("could not create a private GPU temporary directory under")) != BUSTER_STRING_NO_MATCH);
                }
                arguments->show(arguments, S8("GPU_READONLY_SOURCE_CONTROL euid={u64} control={u32} result={u32} source_entries={u64}\n"),
                                effective_uid, control, (u32)child.result, source_entries);
            }
            BUSTER_TEST(arguments, gpu_test_file_equals(arena, unavailable, (ByteSlice){(u8*)source_bytes.pointer, source_bytes.length}));
        }
        // Restore permissions before owned-fixture cleanup on every path.
        if (source_created.created)
        {
            BUSTER_TEST(arguments, chmod((const char*)source.pointer, 0700) == 0);
        }
        BUSTER_TEST(arguments, os_directory_delete(fixture));
    }
    arguments->show(arguments, S8("GPU_READONLY_SOURCE_SUMMARY euid={u64} passed={u64} failed={u64}\n"), effective_uid,
                    result.succeeded_test_count, result.test_count - result.succeeded_test_count);
    return result;
}
#endif

UnitTestResult gpu_pipeline_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    BUSTER_TEST_FIXTURE(arguments, gpu_test_scratch_roots);
#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, gpu_test_readonly_source_scratch);
#endif
#if BUSTER_WINDOWS && !BUSTER_ANDROID && !BUSTER_IOS
    BUSTER_TEST_FIXTURE(arguments, gpu_test_windows_scratch_environment);
#endif

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
        // Exercise workspace ownership with a shared input and directory.
        // Each invocation owns its final output too: concurrent replacement of
        // one Windows destination can legitimately fail with access denied.
        GpuTestConcurrentExecutions concurrent = {.options = {options, options}};
        concurrent.options[1].output_path = buster_test_temporary_path(arena, S8("gpu-private-concurrent-output"), S8(".spv"));
        lane_run(BUSTER_ARRAY_LENGTH(concurrent.results), &gpu_test_concurrent_execute, &concurrent);
        if (concurrent.worker_count < BUSTER_ARRAY_LENGTH(concurrent.results))
        {
            concurrent.arenas[1] = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
            if (concurrent.arenas[1])
            {
                concurrent.results[1] = gpu_pipeline_execute(concurrent.arenas[1], concurrent.options[1]);
            }
        }
        BUSTER_TEST(arguments, concurrent.arenas[0] != 0 && concurrent.arenas[1] != 0);
        for (u32 execution = 0; execution < BUSTER_ARRAY_LENGTH(concurrent.results); execution += 1)
        {
            BUSTER_TEST_RAW(arguments, concurrent.results[execution].error == GPU_PIPELINE_ERROR_NONE,
                            concurrent.results[execution].diagnostic);
            BUSTER_STRING_TEST(arguments, concurrent.results[execution].artifact.path, concurrent.options[execution].output_path);
            BUSTER_TEST(arguments, gpu_test_file_equals(arena, concurrent.options[execution].output_path, valid_spirv));
        }
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
            // Workspace cleanup must preserve both published artifacts.
            BUSTER_TEST(arguments, gpu_test_file_equals(arena, concurrent.options[0].output_path, valid_spirv));
            BUSTER_TEST(arguments, gpu_test_file_equals(arena, concurrent.options[1].output_path, valid_spirv));
            if (concurrent.arenas[execution])
            {
                BUSTER_TEST(arguments, arena_destroy(concurrent.arenas[execution], 1));
            }
        }

        BUSTER_TEST(arguments, os_file_delete(concurrent.options[1].output_path));
        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_file_delete(referent));
        BUSTER_TEST(arguments, os_file_delete(legacy_temporary));
    }

    // Fault only the final owned-workspace deletion, after real COPY,
    // validation and atomic publication. No external tool is required.
    {
        u8 good_data[] = {0x03, 0x02, 0x23, 0x07, 0, 0, 0, 0};
        u8 bad_data[] = {'n', 'o', 'p', 'e'};
        u8 old_data[] = {'o', 'l', 'd'};
        ByteSlice good = (ByteSlice)BUSTER_ARRAY_TO_SLICE(good_data);
        ByteSlice bad = (ByteSlice)BUSTER_ARRAY_TO_SLICE(bad_data);
        ByteSlice old = (ByteSlice)BUSTER_ARRAY_TO_SLICE(old_data);
        String8 input = buster_test_temporary_path(arena, S8("gpu-cleanup-input"), S8(".spv"));
        String8 output = buster_test_temporary_path(arena, S8("gpu-cleanup-output"), S8(".spv"));
        String8 inputs[] = {input};
        GpuPipelineOptions options = gpu_test_options(inputs, 1, gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_LINK);
        options.output_path = output;
        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, file_write(input, good));
        options.temporary_directory = (String8){0};
        for (u32 existing = 0; existing < 2; existing += 1)
        {
            BUSTER_TEST(arguments, existing ? file_write(output, old) : os_file_delete(output));
            gpu_test_fail_next_cleanup(true);
            GpuPipelineResult committed = gpu_pipeline_execute(arena, options);
            gpu_test_fail_next_cleanup(false);
            BUSTER_TEST(arguments, committed.published && committed.cleanup_failed);
            BUSTER_TEST(arguments, committed.error == GPU_PIPELINE_ERROR_FILE_WRITE && committed.process_result == PROCESS_RESULT_FAILED);
            BUSTER_TEST(arguments, committed.artifact.format == GPU_OUTPUT_SPIRV_BINARY);
            BUSTER_STRING_TEST(arguments, committed.artifact.path, output);
            BUSTER_TEST(arguments, committed.artifact.bytes.length == good.length &&
                                       memory_compare(committed.artifact.bytes.pointer, good.pointer, good.length));
            BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, good));
            BUSTER_TEST(arguments, string_first_sequence(committed.diagnostic, S8("GPU artifact published to")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, gpu_test_path_has_kind(committed.temporary_directory, OS_FILE_KIND_DIRECTORY));
            BUSTER_TEST(arguments, os_directory_delete(committed.temporary_directory));
        }

        BUSTER_TEST(arguments, file_write(input, bad));
        BUSTER_TEST(arguments, file_write(output, old));
        gpu_test_fail_next_cleanup(true);
        GpuPipelineResult invalid = gpu_pipeline_execute(arena, options);
        gpu_test_fail_next_cleanup(false);
        BUSTER_TEST(arguments, !invalid.published && invalid.cleanup_failed && invalid.error == GPU_PIPELINE_ERROR_INVALID_ARTIFACT);
        BUSTER_TEST(arguments, !invalid.artifact.bytes.length && !invalid.artifact.path.length);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, old));
        BUSTER_TEST(arguments, string_first_sequence(invalid.diagnostic, S8("does not contain a valid")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, os_directory_delete(invalid.temporary_directory));

        BUSTER_TEST(arguments, file_write(input, good));
        BUSTER_TEST(arguments, os_file_delete(output));
        BUSTER_TEST(arguments, os_make_directory_attempt(output));
        gpu_test_fail_next_cleanup(true);
        GpuPipelineResult refused = gpu_pipeline_execute(arena, options);
        gpu_test_fail_next_cleanup(false);
        BUSTER_TEST(arguments, !refused.published && refused.cleanup_failed && refused.error == GPU_PIPELINE_ERROR_FILE_WRITE);
        BUSTER_TEST(arguments, !refused.artifact.bytes.length && !refused.artifact.path.length);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(output, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, os_directory_delete(refused.temporary_directory));
        BUSTER_TEST(arguments, os_directory_delete(output));

        options.save_temporaries = true;
        gpu_test_fail_next_cleanup(true);
        GpuPipelineResult saved = gpu_pipeline_execute(arena, options);
        gpu_test_fail_next_cleanup(false);
        BUSTER_TEST(arguments, saved.published && !saved.cleanup_failed && saved.error == GPU_PIPELINE_ERROR_NONE);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(saved.temporary_directory, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, good));
        BUSTER_TEST(arguments, os_directory_delete(saved.temporary_directory));

        String8 argv[] = {S8("-target=spirv64"), input, S8("-o"), output};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv));
        BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
        gpu_test_fail_next_cleanup(true);
        CompilerDriverResult driver = compiler_driver_execute_invocation(arena, invocation);
        gpu_test_fail_next_cleanup(false);
        BUSTER_TEST(arguments, driver.error == COMPILER_DRIVER_ERROR_GPU && driver.has_gpu);
        BUSTER_STRING_TEST(arguments, driver.gpu.path, output);
        BUSTER_TEST(arguments, driver.gpu.bytes.length == good.length && memory_compare(driver.gpu.bytes.pointer, good.pointer, good.length));
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, good));
        BUSTER_TEST(arguments, string_first_sequence(driver.diagnostic, S8("GPU artifact published to")) != BUSTER_STRING_NO_MATCH);
        // The public artifact path is preserved; the owned workspace path is
        // included in the error text for remediation. Remove only this test's
        // workspace, found through its uniquely reported scratch path below.
        String8 marker = S8("could not remove owned GPU temporary directory ");
        u64 marker_index = string_first_sequence(driver.diagnostic, marker);
        BUSTER_TEST(arguments, marker_index != BUSTER_STRING_NO_MATCH);
        if (marker_index != BUSTER_STRING_NO_MATCH)
        {
            String8 workspace = string_format_z(arena, S8("{S8}"), string_slice(driver.diagnostic, marker_index + marker.length, driver.diagnostic.length));
            BUSTER_TEST(arguments, gpu_test_path_has_kind(workspace, OS_FILE_KIND_DIRECTORY));
            BUSTER_TEST(arguments, os_directory_delete(workspace));
        }
        BUSTER_TEST(arguments, os_file_delete(input));
        BUSTER_TEST(arguments, os_file_delete(output));
    }

#if (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    // A private executable shell script is a deterministic fake external
    // tool. Its sleep child must be terminated with the complete process group.
    {
        String8 tool = buster_test_temporary_path(arena, S8("gpu-timeout-tool"), S8(".sh"));
        String8 first = buster_test_temporary_path(arena, S8("gpu-timeout-input-a"), S8(".spv"));
        String8 second = buster_test_temporary_path(arena, S8("gpu-timeout-input-b"), S8(".spv"));
        String8 output = buster_test_temporary_path(arena, S8("gpu-timeout-output"), S8(".spv"));
        String8 script_text = S8("#!/bin/sh\nwhile :; do sleep 1; done\n");
        u8 spirv_bytes[] = {0x03, 0x02, 0x23, 0x07, 0, 0, 0, 0};
        String8 inputs[] = {first, second};
        BUSTER_TEST(arguments, file_write(tool, (ByteSlice){(u8*)script_text.pointer, script_text.length}));
        BUSTER_TEST(arguments, chmod((const char*)tool.pointer, 0700) == 0);
        BUSTER_TEST(arguments, file_write(first, (ByteSlice)BUSTER_ARRAY_TO_SLICE(spirv_bytes)));
        BUSTER_TEST(arguments, file_write(second, (ByteSlice)BUSTER_ARRAY_TO_SLICE(spirv_bytes)));
        GpuPipelineOptions options = gpu_test_options(inputs, BUSTER_ARRAY_LENGTH(inputs), gpu_test_target(S8("spirv64")), GPU_PIPELINE_ACTION_LINK);
        options.temporary_directory = (String8){0};
        options.output_path = output;
        options.tools.spirv_link_path = tool;
        options.tool_timeout_microseconds = 100000;
        u64 start = os_now_microseconds();
        GpuPipelineResult timed = gpu_pipeline_execute(arena, options);
        u64 elapsed = os_now_microseconds() - start;
        BUSTER_TEST(arguments, timed.error == GPU_PIPELINE_ERROR_TOOL_TIMEOUT);
        BUSTER_TEST(arguments, timed.timed_out && timed.process_result == PROCESS_RESULT_FAILED);
        BUSTER_TEST(arguments, elapsed < 5000000);
        BUSTER_TEST(arguments, timed.temporary_directory.length && gpu_test_path_has_kind(timed.temporary_directory, OS_FILE_KIND_MISSING));
        String8 failure_script = S8("#!/bin/sh\nexit 7\n");
        u8 old_output[] = {'o', 'l', 'd'};
        ByteSlice old_bytes = (ByteSlice)BUSTER_ARRAY_TO_SLICE(old_output);
        BUSTER_TEST(arguments, file_write(tool, (ByteSlice){(u8*)failure_script.pointer, failure_script.length}));
        BUSTER_TEST(arguments, file_write(output, old_bytes));
        gpu_test_fail_next_cleanup(true);
        GpuPipelineResult failed = gpu_pipeline_execute(arena, options);
        gpu_test_fail_next_cleanup(false);
        BUSTER_TEST(arguments, failed.error == GPU_PIPELINE_ERROR_TOOL_FAILED && failed.cleanup_failed && !failed.published);
        BUSTER_TEST(arguments, failed.process_result != PROCESS_RESULT_SUCCESS && !failed.artifact.bytes.length);
        BUSTER_TEST(arguments, gpu_test_file_equals(arena, output, old_bytes));
        BUSTER_TEST(arguments, string_first_sequence(failed.diagnostic, S8("GPU tool failed:")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, gpu_test_path_has_kind(failed.temporary_directory, OS_FILE_KIND_DIRECTORY));
        BUSTER_TEST(arguments, os_directory_delete(failed.temporary_directory));
        BUSTER_TEST(arguments, os_file_delete(tool));
        BUSTER_TEST(arguments, os_file_delete(first));
        BUSTER_TEST(arguments, os_file_delete(second));
        BUSTER_TEST(arguments, os_file_delete(output));
    }
#endif

    return result;
}

#endif
