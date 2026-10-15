// Trusted native build policy for the small AST-retirement RSS observer.
// Included by build.c; retirement_rss_build_main owns qualification, fixed
// flags, exclusive output, bounded compiler lifetime and identity receipt.
BUSTER_GLOBAL_LOCAL ProcessResult retirement_rss_build_main(Arena* arena, SliceString8 arguments)
{
    bool success = arguments.length == 1;
#if BUSTER_LINUX
    MatrixCoverageCapability compiler = {0};
    if (success)
        success = matrix_coverage_probe(arena, matrix_coverage_target_current(), BUILD_COMPILER_CLANG,
                                       cmake_cc(arena, BUILD_COMPILER_CLANG), &compiler);
    String8 directory = success ? os_path_absolute_lexical(arena, arguments.pointer[0], true) : (String8){0};
    if (success)
    {
        OsDirectoryCreateResult created = os_make_directory_exclusive(directory);
        success = directory.length && created.created;
    }
    String8 source = os_path_absolute(arena, S8("tools/bench_direct/retirement_rss.c"), true);
    String8 executable = success ? path_join(arena, directory, S8("retirement-rss")) : (String8){0};
    String8 source_hash = {0}, compiler_hash = {0}, binary_hash = {0};
    success = success && stage_object_sha256_file(arena, source, &source_hash) &&
              stage_object_sha256_file(arena, compiler.path, &compiler_hash);
    String8 flags[] = {S8("-std=c11"), S8("-O2"), S8("-Wall"), S8("-Wextra"), S8("-Werror"), S8("-Wpedantic"),
                      S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char")};
    SliceString8 compile_arguments = {0};
    if (success)
    {
        OsArgumentBuilder builder = os_argument_builder_start(arena);
        os_argument_builder_append(&builder, compiler.path);
        for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(flags); i += 1) os_argument_builder_append(&builder, flags[i]);
        os_argument_builder_append(&builder, source);
        os_argument_builder_append(&builder, S8("-o"));
        os_argument_builder_append(&builder, executable);
        compile_arguments = os_argument_builder_flush(&builder);
        ProcessSpawnResult spawn = os_process_spawn(compile_arguments, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){
            .use_process_environment = 1, .new_process_group = 1,
            .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
            .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = 65536, [STANDARD_STREAM_ERROR] = 65536}, .total = 131072},
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_TRUNCATE,
        });
        ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
        if (spawn.handle) wait = os_process_wait_deadline(arena, spawn, 60000000);
        string_print(S8("{S8}{S8}"), BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]),
                     BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]));
        success = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out &&
                  !wait.output_truncated && !wait.capture_failed && !build_compiler_query_cleanup_failed(wait);
    }
    String8 source_after = {0}, compiler_after = {0};
    success = success && stage_object_sha256_file(arena, source, &source_after) && string_equal(source_hash, source_after) &&
              stage_object_sha256_file(arena, compiler.path, &compiler_after) && string_equal(compiler_hash, compiler_after) &&
              stage_object_sha256_file(arena, executable, &binary_hash);
    if (success)
    {
        String8List argv_json = {0};
        for (u64 i = 0; i < compile_arguments.length; i += 1)
        {
            if (i) string8_list_push(arena, &argv_json, S8(","));
            string8_list_push(arena, &argv_json, matrix_coverage_json_escape(arena, compile_arguments.pointer[i]));
        }
        String8 argv = string_join_arena(arena, string8_list_to_slice(arena, argv_json), true);
        String8 receipt = string_format(arena, S8("{{\"schema\":\"buster-retirement-rss-build-v1\","
            "\"source\":{{\"path\":{S8},\"sha256\":\"{S8}\",\"size_bytes\":{u64}}},"
            "\"compiler\":{{\"path\":{S8},\"sha256\":\"{S8}\",\"size_bytes\":{u64},\"version\":{S8}}},"
            "\"binary\":{{\"path\":{S8},\"sha256\":\"{S8}\",\"size_bytes\":{u64}}},\"argv\":[{S8}]}}\n"),
            matrix_coverage_json_escape(arena, source), source_hash, os_path_followed_stats(source).size,
            matrix_coverage_json_escape(arena, compiler.path), compiler_hash, os_path_followed_stats(compiler.path).size,
            matrix_coverage_json_escape(arena, compiler.version), matrix_coverage_json_escape(arena, executable), binary_hash,
            os_path_followed_stats(executable).size, argv);
        String8 path = path_join(arena, directory, S8("build.json"));
        success = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(receipt));
        if (success) string_print(S8("RETIREMENT_RSS_HELPER {S8}\n"), executable);
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    success = false;
#endif
    if (!success) string_print(S8("error: retirement_rss_build requires qualified Linux Clang and one fresh output directory\n"));
    ProcessResult result = success ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}
