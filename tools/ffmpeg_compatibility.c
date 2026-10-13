// FFmpeg qualification intake (#3177, #4078): bounded matched profiles.
// ffmpeg_main owns opt-in execution; ffmpeg_cc records candidate actions;
// ffmpeg_configuration_arguments pairs portable and assembly-enabled flags.
// ffmpeg_profile_manifest verifies the optimized feature/tool floor;
// ffmpeg_checkasm requires nonempty summaries and selected family records.
// ffmpeg_run retains argv/cwd/stdout/stderr/status with bounded process cleanup.
// Neither profile claims production dispatch, full samples or performance.

#define BUSTER_FFMPEG_COMMIT "f46e514491172d15bd74b4abb1814cd2f05a763e"
#define BUSTER_FFMPEG_COMMAND_SECONDS 900
#define BUSTER_FFMPEG_CC_SECONDS 60
#define BUSTER_FFMPEG_PCM_SAMPLES 8000

typedef struct FfmpegCommand FfmpegCommand;
struct FfmpegCommand
{
    ProcessWaitResult wait;
    String8 output;
    String8 error;
    bool recorded;
};

BUSTER_GLOBAL_LOCAL FfmpegCommand ffmpeg_run(Arena* arena, String8 directory, String8 prefix, SliceString8 arguments, u64 seconds)
{
    String8List command = {0};
    string8_list_push(arena, &command, string_format(arena, S8("cwd={S8}\n"), directory));
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        string8_list_push(arena, &command, string_format(arena, S8("{u64}:{S8}\n"), arguments.pointer[i].length, arguments.pointer[i]));
    }
    bool recorded = clang_suite_write(arena, string_format(arena, S8("{S8}.argv"), prefix), string_join_arena(arena, string8_list_to_slice(arena, command), false));
    ProcessRun run = {
        .arguments = arguments,
        .working_directory = directory,
        .spawn_options = {
            .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
            .use_process_environment = 1,
            .new_process_group = 1,
            .search_path = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
        },
    };
    u64 start = os_now_microseconds();
    FfmpegCommand result = {.wait = {.result = PROCESS_RESULT_FAILED}};
    if (recorded)
    {
        run.spawn = process_run_spawn(arena, &run);
        if (run.spawn.handle)
        {
            result.wait = os_process_wait_deadline(arena, run.spawn, seconds * 1000000);
        }
    }
    result.output = (String8){.pointer = (char8*)result.wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = result.wait.streams[STANDARD_STREAM_OUTPUT].length};
    result.error = (String8){.pointer = (char8*)result.wait.streams[STANDARD_STREAM_ERROR].pointer, .length = result.wait.streams[STANDARD_STREAM_ERROR].length};
    String8 status = string_format(arena, S8("result={u32}\nplatform_status={u32}\nlaunch_failed={u32}\n"
                                            "timed_out={u32}\noutput_truncated={u32}\ncapture_failed={u32}\n"
                                            "cleanup_failed={u32}\nelapsed_us={u64}\nperformance_qualified=0\n"),
                                   (u32)result.wait.result, result.wait.platform_status, (u32)!run.spawn.handle,
                                   result.wait.timed_out, result.wait.output_truncated,
                                   (u32)(result.wait.capture_failed || result.wait.capture_limit_exceeded),
                                   (u32)(result.wait.process_tree_cleanup_failed || result.wait.process_group_reservation_retained || result.wait.process_group_ownership_lost),
                                   os_now_microseconds() - start);
    result.recorded = clang_suite_write(arena, string_format(arena, S8("{S8}.stdout"), prefix), result.output) &&
                      clang_suite_write(arena, string_format(arena, S8("{S8}.stderr"), prefix), result.error) &&
                      clang_suite_write(arena, string_format(arena, S8("{S8}.status"), prefix), status) && recorded;
    if (!string_starts_with_sequence(quickjs_basename(prefix), S8("cc-")) && result.wait.result != PROCESS_RESULT_SUCCESS)
    {
        string_print(S8("FFMPEG_COMMAND_FAILURE artifacts={S8}\n{S8}\n{S8}\n"), prefix,
                     string_slice(result.output, result.output.length > 8192 ? result.output.length - 8192 : 0, result.output.length),
                     string_slice(result.error, result.error.length > 8192 ? result.error.length - 8192 : 0, result.error.length));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_ok(FfmpegCommand command)
{
    bool result = command.recorded && command.wait.result == PROCESS_RESULT_SUCCESS && !command.wait.timed_out &&
                  !command.wait.output_truncated && !command.wait.capture_failed && !command.wait.capture_limit_exceeded &&
                  !command.wait.process_tree_cleanup_failed && !command.wait.process_group_reservation_retained && !command.wait.process_group_ownership_lost;
    return result;
}

// Upstream configure expands its compiler command as words, not a shell eval.
// Fail explicitly for paths requiring quoting instead of changing their meaning.
BUSTER_GLOBAL_LOCAL bool ffmpeg_command_path(String8 path)
{
    bool result = path.length != 0 && path.pointer[0] == '/';
    for (u64 i = 0; result && i < path.length; i += 1)
    {
        char8 c = path.pointer[i];
        result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ffmpeg_cc(Arena* arena, SliceString8 arguments)
{
    bool valid = arguments.length >= 3;
    FfmpegCommand command = {.wait = {.result = PROCESS_RESULT_FAILED}};
    if (valid)
    {
        String8 compiler = os_path_absolute(arena, arguments.pointer[1], true);
        String8 ledger = os_path_absolute(arena, arguments.pointer[2], true);
        String8 directory = os_path_absolute(arena, S8("."), true);
        valid = compiler.length && ledger.length && directory.length;
        if (valid)
        {
            String8 prefix = path_join(arena, ledger, string_format(arena, S8("cc-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds()));
            OsArgumentBuilder builder = os_argument_builder_start(arena);
            os_argument_builder_append(&builder, compiler);
            os_argument_builder_append(&builder, S8("cc"));
            os_argument_builder_append(&builder, S8("-target"));
            os_argument_builder_append(&builder, S8("x86_64-linux"));
            os_argument_builder_append(&builder, S8("-mcpu=baseline"));
            os_argument_builder_append(&builder, S8("-g0"));
            for (u64 i = 3; i < arguments.length; i += 1)
            {
                os_argument_builder_append(&builder, arguments.pointer[i]);
            }
            command = ffmpeg_run(arena, directory, prefix, os_argument_builder_flush(&builder), BUSTER_FFMPEG_CC_SECONDS);
            // Preserve preprocessor/compiler output exactly for upstream probes.
            os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(command.output));
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(command.error));
        }
    }
    ProcessResult result = valid && ffmpeg_ok(command) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_pristine(Arena* arena, String8 checkout)
{
    String8 git = executable_resolve_in_path(arena, S8("git"));
    bool stopped = false;
    bool valid = git.length && raddebugger_git_verify(arena, git, checkout, S8(BUSTER_FFMPEG_COMMIT), &stopped);
    if (valid)
    {
        // Compare the worktree with the immutable pin, not just the index.
        String8 arguments[] = {git, S8("--no-replace-objects"), S8("-c"), S8("core.fsmonitor=false"), S8("diff"),
                               S8("--no-ext-diff"), S8("--exit-code"), S8(BUSTER_FFMPEG_COMMIT), S8("--")};
        RaddebuggerCommandResult diff = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), checkout, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
        valid = raddebugger_command_ok(diff);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_arguments_contain(SliceString8 arguments, String8 expected)
{
    bool found = false;
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        found = string_equal(arguments.pointer[i], expected) || found;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL SliceString8 ffmpeg_configuration_arguments(Arena* arena, String8 checkout, String8 cc, String8 clang, bool optimized, String8 nasm)
{
    String8List arguments = {0};
    string8_list_push(arena, &arguments, S8("sh"));
    string8_list_push(arena, &arguments, path_join(arena, checkout, S8("configure")));
    string8_list_push(arena, &arguments, S8("--arch=x86_64"));
    string8_list_push(arena, &arguments, S8("--cpu=generic"));
    string8_list_push(arena, &arguments, S8("--target-os=linux"));
    string8_list_push(arena, &arguments, S8("--enable-pic"));
    string8_list_push(arena, &arguments, S8("--disable-autodetect"));
    if (!optimized)
    {
        string8_list_push(arena, &arguments, S8("--disable-asm"));
        string8_list_push(arena, &arguments, S8("--disable-inline-asm"));
        string8_list_push(arena, &arguments, S8("--disable-x86asm"));
    }
    string8_list_push(arena, &arguments, S8("--disable-hwaccels"));
    string8_list_push(arena, &arguments, S8("--disable-network"));
    string8_list_push(arena, &arguments, S8("--disable-doc"));
    string8_list_push(arena, &arguments, S8("--disable-debug"));
    string8_list_push(arena, &arguments, S8("--disable-stripping"));
    string8_list_push(arena, &arguments, S8("--disable-gpl"));
    string8_list_push(arena, &arguments, S8("--disable-nonfree"));
    string8_list_push(arena, &arguments, S8("--disable-version3"));
    string8_list_push(arena, &arguments, S8("--disable-everything"));
    if (optimized)
    {
        string8_list_push(arena, &arguments, S8("--enable-asm"));
        string8_list_push(arena, &arguments, S8("--enable-inline-asm"));
        string8_list_push(arena, &arguments, S8("--enable-x86asm"));
        string8_list_push(arena, &arguments, string_format(arena, S8("--x86asmexe={S8}"), nasm));
    }
    string8_list_push(arena, &arguments, S8("--disable-avdevice"));
    string8_list_push(arena, &arguments, S8("--disable-postproc"));
    string8_list_push(arena, &arguments, S8("--enable-ffmpeg"));
    string8_list_push(arena, &arguments, S8("--enable-ffprobe"));
    string8_list_push(arena, &arguments, S8("--enable-static"));
    string8_list_push(arena, &arguments, S8("--disable-shared"));
    string8_list_push(arena, &arguments, S8("--enable-protocol=file,pipe"));
    string8_list_push(arena, &arguments, S8("--enable-decoder=h264,hevc,flac,mp3,aac,pcm_s16le,rawvideo,pgmyuv"));
    string8_list_push(arena, &arguments, S8("--enable-parser=h264,hevc,flac,mpegaudio,aac"));
    string8_list_push(arena, &arguments, S8("--enable-demuxer=matroska,mov,flac,mp3,aac,wav,image2,rawvideo,pcm_s16le"));
    string8_list_push(arena, &arguments, S8("--enable-encoder=flac,pcm_s16le,rawvideo"));
    string8_list_push(arena, &arguments, S8("--enable-muxer=framecrc,framemd5,crc,null,wav,flac,matroska,mp4,ismv,image2,rawvideo,pcm_s16le"));
    string8_list_push(arena, &arguments, S8("--enable-filter=aresample,aformat,channelmap,format,scale,anull,null,abuffer,buffer,abuffersink,buffersink"));
    string8_list_push(arena, &arguments, string_format(arena, S8("--cc={S8}"), cc));
    string8_list_push(arena, &arguments, string_format(arena, S8("--ld={S8}"), clang));
    string8_list_push(arena, &arguments, string_format(arena, S8("--host-cc={S8}"), cc));
    string8_list_push(arena, &arguments, string_format(arena, S8("--host-ld={S8}"), clang));
    string8_list_push(arena, &arguments, string_format(arena, S8("--dep-cc={S8}"), clang));
    SliceString8 result = string8_list_to_slice(arena, arguments);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_has_line(String8 text, String8 expected)
{
    bool found = false;
    for (u64 start = 0; start < text.length;)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        String8 line = string_slice(text, start, end);
        if (line.length && line.pointer[line.length - 1] == '\r') { line.length -= 1; }
        found = string_equal(line, expected) || found;
        start = end < text.length ? end + 1 : end;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL void ffmpeg_profile_required_features(Arena* arena, String8List* required)
{
    String8 names[] = {
        S8("ARCH_X86_64"), S8("HAVE_INLINE_ASM"), S8("HAVE_X86ASM"), S8("HAVE_INTRINSICS_SSE2"), S8("CONFIG_RUNTIME_CPUDETECT"),
        S8("HAVE_AESNI_EXTERNAL"), S8("HAVE_AMD3DNOW_EXTERNAL"), S8("HAVE_AMD3DNOWEXT_EXTERNAL"),
        S8("HAVE_AVX_EXTERNAL"), S8("HAVE_AVX2_EXTERNAL"), S8("HAVE_AVX512_EXTERNAL"), S8("HAVE_AVX512ICL_EXTERNAL"),
        S8("HAVE_FMA3_EXTERNAL"), S8("HAVE_FMA4_EXTERNAL"), S8("HAVE_MMX_EXTERNAL"), S8("HAVE_MMXEXT_EXTERNAL"),
        S8("HAVE_SSE_EXTERNAL"), S8("HAVE_SSE2_EXTERNAL"), S8("HAVE_SSE3_EXTERNAL"), S8("HAVE_SSE4_EXTERNAL"),
        S8("HAVE_SSE42_EXTERNAL"), S8("HAVE_SSSE3_EXTERNAL"), S8("HAVE_XOP_EXTERNAL"),
        S8("HAVE_MMXEXT_INLINE"), S8("HAVE_SSSE3_INLINE")
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        string8_list_push(arena, required, names[i]);
    }
}

BUSTER_GLOBAL_LOCAL String8 ffmpeg_profile_test_header(Arena* arena, String8 omitted)
{
    String8List required = {0};
    String8List rows = {0};
    ffmpeg_profile_required_features(arena, &required);
    SliceString8 features = string8_list_to_slice(arena, required);
    for (u64 i = 0; i < features.length; i += 1)
    {
        if (!omitted.length || !string_equal(features.pointer[i], omitted))
        {
            string8_list_push(arena, &rows, string_format(arena, S8("#define {S8} 1\n"), features.pointer[i]));
        }
    }
    return string_join_arena(arena, string8_list_to_slice(arena, rows), false);
}

BUSTER_GLOBAL_LOCAL String8 ffmpeg_profile_manifest(Arena* arena, String8 config_h, String8 config_mak, String8 nasm, bool* valid)
{
    String8List rows = {0};
    String8List required = {0};
    string8_list_push(arena, &rows, S8("feature\tstatus\texpected\n"));
    ffmpeg_profile_required_features(arena, &required);
    SliceString8 names = string8_list_to_slice(arena, required);
    bool result = true;
    for (u64 i = 0; i < names.length; i += 1)
    {
        bool present = ffmpeg_has_line(config_h, string_format(arena, S8("#define {S8} 1"), names.pointer[i]));
        result = present && result;
        string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\t1\n"), names.pointer[i], present ? S8("present") : S8("missing")));
    }
    String8 x86asm = string_format(arena, S8("X86ASM={S8}"), nasm);
    String8 depx86asm = string_format(arena, S8("DEPX86ASM={S8}"), nasm);
    bool tool_matches = ffmpeg_has_line(config_mak, x86asm) && ffmpeg_has_line(config_mak, depx86asm);
    result = tool_matches && result;
    string8_list_push(arena, &rows, string_format(arena, S8("X86ASM\t{S8}\t{S8}\n"), ffmpeg_has_line(config_mak, x86asm) ? S8("matched") : S8("mismatch"), nasm));
    string8_list_push(arena, &rows, string_format(arena, S8("DEPX86ASM\t{S8}\t{S8}\n"), ffmpeg_has_line(config_mak, depx86asm) ? S8("matched") : S8("mismatch"), nasm));
    *valid = result;
    String8 manifest = string_join_arena(arena, string8_list_to_slice(arena, rows), false);
    return manifest;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_optimized_profile(Arena* arena, String8 directory, String8 nasm)
{
    String8 config_h = clang_suite_read(arena, path_join(arena, directory, S8("config.h")));
    String8 config_mak = clang_suite_read(arena, path_join(arena, directory, S8("ffbuild/config.mak")));
    bool valid = false;
    String8 manifest = ffmpeg_profile_manifest(arena, config_h, config_mak, nasm, &valid);
    bool written = clang_suite_write(arena, path_join(arena, directory, S8("feature-manifest.tsv")), manifest);
    valid = written && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_contains(String8 text, String8 needle)
{
    bool found = false;
    if (needle.length && text.length >= needle.length)
    {
        for (u64 i = 0; i <= text.length - needle.length; i += 1)
        {
            found = string_equal(string_slice(text, i, i + needle.length), needle) || found;
        }
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm_summary(String8 text, u64* functions)
{
    String8 prefix = S8("checkasm: all ");
    String8 suffix = S8(" tests passed");
    u64 parsed = 0;
    u32 summaries = 0;
    bool valid = !ffmpeg_contains(text, S8("checkasm: no tests to perform")) && !ffmpeg_contains(text, S8("[FAILED]"));
    for (u64 start = 0; start < text.length;)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        String8 line = string_slice(text, start, end);
        if (line.length && line.pointer[line.length - 1] == '\r') { line.length -= 1; }
        if (string_starts_with_sequence(line, prefix))
        {
            summaries += 1;
            u64 index = prefix.length;
            u64 value = 0;
            u32 digits = 0;
            while (index < line.length && line.pointer[index] >= '0' && line.pointer[index] <= '9')
            {
                u32 digit = (u32)(line.pointer[index] - '0');
                bool safe = value <= (((u64)-1 - digit) / 10);
                valid = safe && valid;
                if (safe) { value = value * 10 + digit; }
                index += 1;
                digits += 1;
            }
            bool exact = digits != 0 && string_equal(string_slice(line, index, line.length), suffix);
            valid = exact && value != 0 && valid;
            parsed = value;
        }
        start = end < text.length ? end + 1 : end;
    }
    valid = summaries == 1 && valid;
    *functions = parsed;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm_family_record(Arena* arena, String8 text, String8 family)
{
    String8 marker = string_format(arena, S8(" - {S8}."), family);
    bool found = false;
    for (u64 start = 0; start < text.length;)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        String8 line = string_slice(text, start, end);
        found = (ffmpeg_contains(line, marker) && ffmpeg_contains(line, S8("[OK]"))) || found;
        start = end < text.length ? end + 1 : end;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL void ffmpeg_checkasm_required_families(Arena* arena, String8List* families)
{
    String8 names[] = {S8("h264chroma"), S8("h264dsp"), S8("h264pred"), S8("h264qpel"),
                       S8("hevc_add_res"), S8("hevc_deblock"), S8("hevc_idct"), S8("hevc_pel"), S8("hevc_sao"),
                       S8("flacdsp"), S8("sw_scale")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        string8_list_push(arena, families, names[i]);
    }
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm_not_run_ledger(Arena* arena, String8 output, String8 compiler)
{
    String8List rows = {0};
    String8List families = {0};
    string8_list_push(arena, &rows, S8("selection\tstatus\toptimized_function_pointers\n"));
    string8_list_push(arena, &rows, S8("x86_checkasm_object\tnot-run\tunknown\n"));
    string8_list_push(arena, &rows, S8("all\tnot-run\tunknown\n"));
    ffmpeg_checkasm_required_families(arena, &families);
    SliceString8 names = string8_list_to_slice(arena, families);
    for (u64 i = 0; i < names.length; i += 1)
    {
        string8_list_push(arena, &rows, string_format(arena, S8("{S8}\tnot-run\tunknown\n"), names.pointer[i]));
    }
    return clang_suite_write(arena, path_join(arena, output, string_format(arena, S8("checkasm-{S8}-counts.tsv"), compiler)),
                             string_join_arena(arena, string8_list_to_slice(arena, rows), false));
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm_counts_match(u64 reference, u64 candidate)
{
    return reference == candidate;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm(Arena* arena, String8 directory, String8 output, String8 compiler, u64* total_functions)
{
    String8 executable = path_join(arena, directory, S8("tests/checkasm/checkasm"));
    String8List rows = {0};
    string8_list_push(arena, &rows, S8("selection\tstatus\toptimized_function_pointers\n"));
    String8 all_arguments[] = {executable};
    FfmpegCommand all = ffmpeg_run(arena, directory, path_join(arena, output, string_format(arena, S8("checkasm-{S8}-all"), compiler)),
                                   (SliceString8)BUSTER_ARRAY_TO_SLICE(all_arguments), BUSTER_FFMPEG_CC_SECONDS);
    u64 all_count = 0;
    String8 abi_object = clang_suite_read(arena, path_join(arena, directory, S8("tests/checkasm/x86/checkasm.o")));
    bool abi_valid = abi_object.length != 0;
    bool valid = abi_valid && ffmpeg_ok(all) && ffmpeg_checkasm_summary(all.error, &all_count);
    string8_list_push(arena, &rows, string_format(arena, S8("x86_checkasm_object\t{S8}\t{S8}\n"), abi_valid ? S8("built") : S8("missing"), S8("required")));
    string8_list_push(arena, &rows, string_format(arena, S8("all\t{S8}\t{u64}\n"), valid ? S8("pass") : S8("fail"), all_count));
    String8List families = {0};
    ffmpeg_checkasm_required_families(arena, &families);
    SliceString8 names = string8_list_to_slice(arena, families);
    for (u64 i = 0; i < names.length; i += 1)
    {
        bool family_valid = ffmpeg_checkasm_family_record(arena, all.error, names.pointer[i]);
        valid = family_valid && valid;
        string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\tunknown\n"), names.pointer[i],
                                                       family_valid ? S8("passed-record") : S8("missing-passed-record")));
    }
    bool written = clang_suite_write(arena, path_join(arena, output, string_format(arena, S8("checkasm-{S8}-counts.tsv"), compiler)),
                                     string_join_arena(arena, string8_list_to_slice(arena, rows), false));
    *total_functions = all_count;
    valid = written && valid;
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_checkasm_controls_self_test(void)
{
    u64 count = 0;
    bool positive = ffmpeg_checkasm_summary(S8("checkasm: all 17 tests passed\n"), &count) && count == 17;
    bool zero = !ffmpeg_checkasm_summary(S8("checkasm: all 0 tests passed\n"), &count);
    bool empty = !ffmpeg_checkasm_summary(S8(""), &count);
    bool no_tests = !ffmpeg_checkasm_summary(S8("checkasm: no tests to perform\n"), &count);
    bool failed = !ffmpeg_checkasm_summary(S8("checkasm: all 2 tests passed\nfoo [FAILED]\n"), &count);
    bool malformed = !ffmpeg_checkasm_summary(S8("checkasm: all seventeen tests passed\n"), &count);
    bool unknown = !ffmpeg_checkasm_summary(S8("unrecognized checkasm output\n"), &count);
    bool duplicate = !ffmpeg_checkasm_summary(S8("checkasm: all 1 tests passed\ncheckasm: all 1 tests passed\n"), &count);
    bool equal_pointer_counts = ffmpeg_checkasm_counts_match(7, 7);
    bool differing_pointer_counts = !ffmpeg_checkasm_counts_match(7, 6);
    bool result = positive && zero && empty && no_tests && failed && malformed && unknown && duplicate &&
                  equal_pointer_counts && differing_pointer_counts;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_config_controls_self_test(Arena* arena)
{
    String8 checkout = S8("/tmp/ffmpeg");
    String8 cc = S8("/tmp/clang");
    String8 clang = S8("/tmp/clang");
    String8 nasm = S8("/usr/bin/nasm");
    SliceString8 portable = ffmpeg_configuration_arguments(arena, checkout, cc, clang, false, (String8){0});
    SliceString8 optimized = ffmpeg_configuration_arguments(arena, checkout, cc, clang, true, nasm);
    bool portable_controls = ffmpeg_arguments_contain(portable, S8("--disable-asm")) &&
                             ffmpeg_arguments_contain(portable, S8("--disable-inline-asm")) &&
                             ffmpeg_arguments_contain(portable, S8("--disable-x86asm")) &&
                             !ffmpeg_arguments_contain(portable, S8("--enable-asm"));
    bool optimized_controls = ffmpeg_arguments_contain(optimized, S8("--enable-asm")) &&
                              ffmpeg_arguments_contain(optimized, S8("--enable-inline-asm")) &&
                              ffmpeg_arguments_contain(optimized, S8("--enable-x86asm")) &&
                              ffmpeg_arguments_contain(optimized, S8("--x86asmexe=/usr/bin/nasm")) &&
                              !ffmpeg_arguments_contain(optimized, S8("--disable-asm")) &&
                              !ffmpeg_arguments_contain(optimized, S8("--disable-inline-asm")) &&
                              !ffmpeg_arguments_contain(optimized, S8("--disable-x86asm"));
    bool common_flags_match = true;
    for (u64 i = 0; i < portable.length; i += 1)
    {
        String8 argument = portable.pointer[i];
        bool profile_flag = string_equal(argument, S8("--disable-asm")) || string_equal(argument, S8("--disable-inline-asm")) ||
                            string_equal(argument, S8("--disable-x86asm"));
        common_flags_match = (profile_flag || ffmpeg_arguments_contain(optimized, argument)) && common_flags_match;
    }
    for (u64 i = 0; i < optimized.length; i += 1)
    {
        String8 argument = optimized.pointer[i];
        bool profile_flag = string_equal(argument, S8("--enable-asm")) || string_equal(argument, S8("--enable-inline-asm")) ||
                            string_equal(argument, S8("--enable-x86asm")) || string_starts_with_sequence(argument, S8("--x86asmexe="));
        common_flags_match = (profile_flag || ffmpeg_arguments_contain(portable, argument)) && common_flags_match;
    }
    bool result = portable_controls && optimized_controls && common_flags_match;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_profile_self_test(Arena* arena)
{
    String8 config_h = ffmpeg_profile_test_header(arena, (String8){0});
    String8 config_mak = S8("X86ASM=/usr/bin/nasm\nDEPX86ASM=/usr/bin/nasm\n");
    bool valid = false;
    String8 manifest = ffmpeg_profile_manifest(arena, config_h, config_mak, S8("/usr/bin/nasm"), &valid);
    bool positive = valid && ffmpeg_contains(manifest, S8("HAVE_X86ASM\tpresent\t1")) &&
                    ffmpeg_contains(manifest, S8("HAVE_AVX2_EXTERNAL\tpresent\t1"));
    String8 missing_inline = ffmpeg_profile_test_header(arena, S8("HAVE_INLINE_ASM"));
    String8 missing_x86asm = ffmpeg_profile_test_header(arena, S8("HAVE_X86ASM"));
    String8 missing_sse2 = ffmpeg_profile_test_header(arena, S8("HAVE_INTRINSICS_SSE2"));
    String8 missing_runtime = ffmpeg_profile_test_header(arena, S8("CONFIG_RUNTIME_CPUDETECT"));
    String8 missing_external_tier = ffmpeg_profile_test_header(arena, S8("HAVE_AVX2_EXTERNAL"));
    String8 missing_inline_tier = ffmpeg_profile_test_header(arena, S8("HAVE_SSSE3_INLINE"));
    bool control_valid = false;
    ffmpeg_profile_manifest(arena, missing_inline, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_inline_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, missing_x86asm, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_x86asm_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, missing_sse2, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_sse2_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, missing_runtime, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_runtime_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, missing_external_tier, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_external_tier_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, missing_inline_tier, config_mak, S8("/usr/bin/nasm"), &control_valid);
    bool missing_inline_tier_rejected = !control_valid;
    ffmpeg_profile_manifest(arena, config_h, config_mak, S8("/usr/local/bin/nasm"), &control_valid);
    bool tool_mismatch_rejected = !control_valid;
    String8 mismatched_dependency = S8("X86ASM=/usr/bin/nasm\nDEPX86ASM=/usr/local/bin/nasm\n");
    ffmpeg_profile_manifest(arena, config_h, mismatched_dependency, S8("/usr/bin/nasm"), &control_valid);
    bool dependency_mismatch_rejected = !control_valid;
    bool configs = ffmpeg_config_controls_self_test(arena);
    bool checkasm = ffmpeg_checkasm_controls_self_test();
    bool feature_floor = positive && missing_inline_rejected && missing_x86asm_rejected && missing_sse2_rejected &&
                         missing_runtime_rejected && missing_external_tier_rejected && missing_inline_tier_rejected;
    bool result = feature_floor && tool_mismatch_rejected && dependency_mismatch_rejected && configs && checkasm;
    string_print(S8("FFMPEG_HARNESS_CONTROLS status={S8} config_profiles={S8} feature_manifest={S8} nasm_identity={S8} checkasm_controls={S8}\n"),
                 result ? S8("pass") : S8("fail"), configs ? S8("pass") : S8("fail"),
                 feature_floor ? S8("pass") : S8("fail"),
                 (positive && tool_mismatch_rejected && dependency_mismatch_rejected) ? S8("pass") : S8("fail"), checkasm ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_configure(Arena* arena, String8 checkout, String8 directory, String8 cc, String8 clang, bool optimized, String8 nasm)
{
    SliceString8 arguments = ffmpeg_configuration_arguments(arena, checkout, cc, clang, optimized, nasm);
    FfmpegCommand command = ffmpeg_run(arena, directory, path_join(arena, directory, S8("configure")), arguments, BUSTER_FFMPEG_COMMAND_SECONDS);
    return ffmpeg_ok(command);
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_make(Arena* arena, String8 directory, String8 target, String8 prefix)
{
    String8 arguments[] = {S8("make"), S8("-k"), S8("-j2"), S8("V=1"), S8("SAMPLES="), S8("IGNORE_TESTS="), S8("GEN=no"), S8("TARGET_EXEC="), S8("THREADS=1"), target, S8("fate-hw")};
    // This pin appends FATE_HW after the default fate dependencies, but includes
    // it in fate-list. Run that upstream target too; never drop its denominator.
    SliceString8 selected = BUSTER_ARRAY_TO_SLICE(arguments);
    selected.length -= string_equal(target, S8("fate")) ? 0 : 1;
    FfmpegCommand command = ffmpeg_run(arena, directory, path_join(arena, directory, prefix), selected, BUSTER_FFMPEG_COMMAND_SECONDS);
    return ffmpeg_ok(command);
}


BUSTER_GLOBAL_LOCAL bool ffmpeg_fate_counts(Arena* arena, String8 directory, String8 list, bool fate_passed)
{
    u64 declared = 0, attempted = 0, passed = 0, failed = 0, missing = 0;
    bool valid = list.length != 0;
    String8List rows = {0};
    string8_list_push(arena, &rows, S8("target\tstatus\n"));
    for (u64 start = 0; start < list.length;)
    {
        u64 end = start;
        while (end < list.length && list.pointer[end] != '\n') { end += 1; }
        String8 target = string_slice(list, start, end);
        start = end < list.length ? end + 1 : end;
        if (target.length)
        {
            bool target_valid = string_starts_with_sequence(target, S8("fate-")) && clang_suite_safe_path(target);
            valid = target_valid && valid;
            if (target_valid)
            {
                declared += 1;
                String8 name = string_slice(target, 5, target.length);
                String8 report = clang_suite_read(arena, path_join(arena, directory, string_format(arena, S8("tests/data/fate/{S8}.rep"), name)));
                String8 prefix = string_format(arena, S8("{S8}:"), name);
                bool report_valid = string_starts_with_sequence(report, prefix);
                bool success = report_valid && report.length >= prefix.length + 2 &&
                               report.pointer[prefix.length] == '0' && report.pointer[prefix.length + 1] == ':';
                String8 status;
                if (!report.length) { missing += 1; status = S8("not-run"); }
                else
                {
                    attempted += 1;
                    if (success) { passed += 1; status = S8("pass"); }
                    else { failed += 1; status = S8("fail"); }
                    valid = report_valid && valid;
                }
                string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{S8}\n"), target, status));
            }
        }
    }
    string8_list_push(arena, &rows, string_format(arena, S8("declared\t{u64}\nattempted\t{u64}\npass\t{u64}\nfail\t{u64}\n"
                                                          "not-run\t{u64}\nskip\t0\nsample-tests\tunqualified\n"), declared, attempted, passed, failed, missing));
    bool written = clang_suite_write(arena, path_join(arena, directory, S8("fate-counts.tsv")), string_join_arena(arena, string8_list_to_slice(arena, rows), false));
    valid = written && declared != 0 && attempted == declared && passed == declared && !failed && !missing && fate_passed && valid;
    if (!valid) { string_print(S8("FFMPEG_FATE_COUNTS_INVALID list-begin\n{S8}list-end\n"), list); }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_pcm_fixture(Arena* arena, String8 path)
{
    u64 length = BUSTER_FFMPEG_PCM_SAMPLES * 2;
    char8* bytes = arena_allocate(arena, char8, length);
    for (u64 i = 0; i < BUSTER_FFMPEG_PCM_SAMPLES; i += 1)
    {
        // Defined unsigned arithmetic and little-endian serialization.
        u32 sample = (u32)((i * 257 + 12345) & 65535);
        bytes[2 * i] = (char8)(sample & 255);
        bytes[2 * i + 1] = (char8)(sample >> 8);
    }
    bool result = clang_suite_write(arena, path, (String8){.pointer = bytes, .length = length});
    return result;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_media(Arena* arena, String8 directory, String8 pcm)
{
    String8 ffmpeg = path_join(arena, directory, S8("ffmpeg"));
    String8 flac = path_join(arena, directory, S8("fixture.flac"));
    String8 decoded = path_join(arena, directory, S8("decoded.pcm"));
    String8 convert_arguments[] = {ffmpeg, S8("-nostdin"), S8("-v"), S8("error"), S8("-f"), S8("s16le"),
                                   S8("-ar"), S8("8000"), S8("-ac"), S8("1"), S8("-i"), pcm,
                                   S8("-c:a"), S8("flac"), flac};
    bool valid = ffmpeg_ok(ffmpeg_run(arena, directory, path_join(arena, directory, S8("media-convert")), (SliceString8)BUSTER_ARRAY_TO_SLICE(convert_arguments), BUSTER_FFMPEG_CC_SECONDS));
    if (valid)
    {
        String8 decode_arguments[] = {ffmpeg, S8("-nostdin"), S8("-v"), S8("error"), S8("-i"), flac,
                                      S8("-c:a"), S8("pcm_s16le"), S8("-f"), S8("s16le"), decoded};
        valid = ffmpeg_ok(ffmpeg_run(arena, directory, path_join(arena, directory, S8("media-decode")), (SliceString8)BUSTER_ARRAY_TO_SLICE(decode_arguments), BUSTER_FFMPEG_CC_SECONDS));
        if (valid)
        {
            String8 expected = clang_suite_read(arena, pcm);
            String8 actual = clang_suite_read(arena, decoded);
            valid = expected.length == BUSTER_FFMPEG_PCM_SAMPLES * 2 && string_equal(expected, actual);
        }
    }
    if (valid)
    {
        String8 probe_arguments[] = {path_join(arena, directory, S8("ffprobe")), S8("-v"), S8("error"),
                                     S8("-select_streams"), S8("a:0"), S8("-show_entries"), S8("stream=codec_name,sample_rate,channels"),
                                     S8("-of"), S8("default=noprint_wrappers=1"), flac};
        FfmpegCommand probe = ffmpeg_run(arena, directory, path_join(arena, directory, S8("media-probe")), (SliceString8)BUSTER_ARRAY_TO_SLICE(probe_arguments), BUSTER_FFMPEG_CC_SECONDS);
        valid = ffmpeg_ok(probe) && string_equal(probe.output, S8("codec_name=flac\nsample_rate=8000\nchannels=1\n"));
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_self_test(Arena* arena, String8 checkout)
{
    bool valid = ffmpeg_command_path(S8("/tmp/driver-1.2")) && !ffmpeg_command_path(S8("/tmp/with space")) &&
                 !ffmpeg_command_path(S8("/tmp/x;touch")) && !ffmpeg_command_path(S8("relative"));
    String8 git = executable_resolve_in_path(arena, S8("git"));
    bool stopped = false;
    valid = checkout.length && ffmpeg_pristine(arena, checkout) && valid;
    if (valid)
    {
        // Every control operates on a disposable clone; never edit the input.
        String8 directory = string_format(arena, S8("build/ffmpeg-control-{u64}-{u64}"), os_get_current_process_id(), os_now_microseconds());
        String8 arguments[] = {git, S8("--no-replace-objects"), S8("clone"), S8("--no-hardlinks"), S8("--no-checkout"), checkout, directory};
        RaddebuggerCommandResult clone = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), S8("."), (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
        valid = raddebugger_command_ok(clone);
        if (valid)
        {
            directory = os_path_absolute(arena, directory, true);
            String8 checkout_arguments[] = {git, S8("--no-replace-objects"), S8("checkout"), S8("--detach"), S8(BUSTER_FFMPEG_COMMIT)};
            RaddebuggerCommandResult checked_out = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout_arguments), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            valid = raddebugger_command_ok(checked_out) && ffmpeg_pristine(arena, directory);
            String8 marker = path_join(arena, directory, S8("buster-untracked-control.h"));
            bool written = clang_suite_write(arena, marker, S8("changed input\n"));
            valid = written && !ffmpeg_pristine(arena, directory) && valid;
            bool removed = remove_path_recursive(arena, marker);
            valid = removed && valid;
            String8 source = path_join(arena, directory, S8("libavutil/avutil.h"));
            String8 original = clang_suite_read(arena, source);
            bool modified = original.length && clang_suite_write(arena, source, S8("changed tracked input\n"));
            valid = modified && !ffmpeg_pristine(arena, directory) && valid;
            bool restored = clang_suite_write(arena, source, original);
            valid = restored && ffmpeg_pristine(arena, directory) && valid;
            String8 wrong_arguments[] = {git, S8("--no-replace-objects"), S8("checkout"), S8("--detach"), S8("HEAD^")};
            RaddebuggerCommandResult wrong = raddebugger_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(wrong_arguments), directory, (String8){0}, RADDEBUGGER_ENVIRONMENT_GIT, &stopped);
            valid = raddebugger_command_ok(wrong) && !ffmpeg_pristine(arena, directory) && valid;
        }
        bool cleaned = remove_path_recursive(arena, directory);
        valid = cleaned && valid;
    }
    string_print(S8("FFMPEG_INPUT_CONTROLS status={S8} sample_set_controls=not-run\n"), valid ? S8("pass") : S8("fail"));
    return valid;
}


BUSTER_GLOBAL_LOCAL String8 ffmpeg_feature_config(Arena* arena, String8 text)
{
    String8List rows = {0};
    for (u64 start = 0; start < text.length;)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        u64 next = end < text.length ? end + 1 : end;
        String8 row = string_slice(text, start, next);
        // These record command/compiler identity, not feature selection. Keep
        // their originals and the full diff; compare every other config line.
        bool identity = string_starts_with_sequence(row, S8("#define FFMPEG_CONFIGURATION ")) ||
                        string_starts_with_sequence(row, S8("#define CC_IDENT "));
        if (!identity) { string8_list_push(arena, &rows, row); }
        start = next;
    }
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, rows), false);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ffmpeg_main(Arena* arena, SliceString8 arguments, String8 driver_argument)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool cc = arguments.length >= 1 && string_equal(arguments.pointer[0], S8("--cc"));
    bool self_test = arguments.length == 2 && string_equal(arguments.pointer[0], S8("--self-test"));
    bool harness_self_test = arguments.length == 1 && string_equal(arguments.pointer[0], S8("--harness-self-test"));
    bool portable = arguments.length == 4 && string_equal(arguments.pointer[0], S8("--portable"));
    bool optimized = arguments.length == 5 && string_equal(arguments.pointer[0], S8("--optimized"));
    bool profile = portable || optimized;
    if (cc)
    {
        result = ffmpeg_cc(arena, arguments);
    }
    else if (self_test)
    {
        String8 checkout = os_path_absolute(arena, arguments.pointer[1], true);
        result = ffmpeg_self_test(arena, checkout) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (harness_self_test)
    {
        result = ffmpeg_profile_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (profile)
    {
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        String8 checkout = os_path_absolute(arena, arguments.pointer[1], true);
        String8 requested = os_path_absolute_lexical(arena, arguments.pointer[2], true);
        String8 parent = os_path_absolute(arena, path_parent(arena, requested), true);
        String8 name = quickjs_basename(requested);
        String8 output = path_join(arena, parent, name);
        String8 ide = os_path_absolute(arena, arguments.pointer[3], true);
        String8 nasm = optimized ? os_path_absolute(arena, arguments.pointer[4], true) : (String8){0};
        String8 driver = os_path_absolute(arena, driver_argument, true);
        String8 clang = executable_resolve_in_path(arena, S8("clang"));
        bool valid = checkout.length && parent.length && ide.length && driver.length && clang.length &&
                     clang_suite_safe_path(name) && !string_equal(parent, checkout) &&
                     !string_starts_with_sequence(parent, string_format(arena, S8("{S8}/"), checkout)) &&
                     ffmpeg_command_path(driver) && ffmpeg_command_path(ide) && ffmpeg_command_path(output) &&
                     ffmpeg_command_path(clang) && (!optimized || ffmpeg_command_path(nasm)) && ffmpeg_pristine(arena, checkout);
        if (valid)
        {
            valid = os_make_directory_exclusive(output).created;
        }
        bool claimed = valid;
        bool configured[2] = {0};
        bool profile_valid[2] = {0};
        bool built[2] = {0};
        bool checkasm[2] = {0};
        bool checkasm_attempted[2] = {0};
        u64 checkasm_functions[2] = {0};
        bool media[2] = {0};
        bool fate[2] = {0};
        String8 directories[2] = {path_join(arena, output, S8("reference")), path_join(arena, output, S8("buster"))};
        String8 compiler_names[2] = {S8("clang"), S8("buster")};
        String8 pcm = path_join(arena, output, S8("fixture.pcm"));
        if (valid && optimized)
        {
            for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(compiler_names); i += 1)
            {
                valid = ffmpeg_checkasm_not_run_ledger(arena, output, compiler_names[i]) && valid;
            }
        }
        if (valid)
        {
            String8 git_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("--full-tree"), S8(BUSTER_FFMPEG_COMMIT)};
            FfmpegCommand inventory = ffmpeg_run(arena, checkout, path_join(arena, output, S8("source-inventory")), (SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments), BUSTER_FFMPEG_CC_SECONDS);
            valid = ffmpeg_ok(inventory) && ffmpeg_pcm_fixture(arena, pcm);
            String8 compiler_arguments[] = {S8("sha256sum"), driver, ide, clang};
            valid = ffmpeg_ok(ffmpeg_run(arena, output, path_join(arena, output, S8("compiler-identities")), (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_arguments), BUSTER_FFMPEG_CC_SECONDS)) && valid;
            if (optimized)
            {
                String8 version_arguments[] = {nasm, S8("-v")};
                String8 checksum_arguments[] = {S8("sha256sum"), nasm};
                FfmpegCommand version = ffmpeg_run(arena, output, path_join(arena, output, S8("nasm-version")), (SliceString8)BUSTER_ARRAY_TO_SLICE(version_arguments), BUSTER_FFMPEG_CC_SECONDS);
                FfmpegCommand checksum = ffmpeg_run(arena, output, path_join(arena, output, S8("nasm-sha256")), (SliceString8)BUSTER_ARRAY_TO_SLICE(checksum_arguments), BUSTER_FFMPEG_CC_SECONDS);
                String8 provenance = string_format(arena, S8("tool=external-NASM-not-Buster-native\npath={S8}\nversion_status={u32}\nversion_stdout={S8}\nversion_stderr={S8}\nchecksum_status={u32}\nchecksum={S8}"),
                                                   nasm, (u32)ffmpeg_ok(version), version.output, version.error, (u32)ffmpeg_ok(checksum), checksum.output);
                bool written = clang_suite_write(arena, path_join(arena, output, S8("nasm-provenance.txt")), provenance);
                valid = written && ffmpeg_ok(version) && (version.output.length || version.error.length) && ffmpeg_ok(checksum) && checksum.output.length && valid;
            }
        }
        // Preserve the independent reference even if the candidate configure
        // fails. Each side uses normal unmodified upstream generation.
        for (u32 i = 0; valid && i < BUSTER_ARRAY_LENGTH(directories); i += 1)
        {
            bool ready = os_make_directory_exclusive(directories[i]).created;
            String8 ledger = path_join(arena, directories[i], S8("compiler-actions"));
            ready = os_make_directory_exclusive(ledger).created && ready;
            String8 cc_command = i == 0 ? clang : string_format(arena, S8("{S8} test_ffmpeg --cc {S8} {S8}"), driver, ide, ledger);
            configured[i] = ready && ffmpeg_configure(arena, checkout, directories[i], cc_command, clang, optimized, nasm);
            profile_valid[i] = configured[i] && (!optimized || ffmpeg_optimized_profile(arena, directories[i], nasm));
            string_print(S8("FFMPEG_CONFIGURE compiler={S8} profile={S8} status={S8}\n"), compiler_names[i], optimized ? S8("assembly-enabled") : S8("portable"),
                         profile_valid[i] ? S8("pass") : S8("fail"));
            if (profile_valid[i])
            {
                built[i] = ffmpeg_make(arena, directories[i], S8("all"), S8("build"));
                if (built[i])
                {
                    if (optimized)
                    {
                        bool checkasm_built = ffmpeg_make(arena, directories[i], S8("checkasm"), S8("checkasm-build"));
                        if (checkasm_built)
                        {
                            checkasm_attempted[i] = true;
                            checkasm[i] = ffmpeg_checkasm(arena, directories[i], output, compiler_names[i], &checkasm_functions[i]);
                        }
                    }
                    media[i] = ffmpeg_media(arena, directories[i], pcm);
                    // The denominator is only upstream's no-external-sample tests for this configuration.
                    // Sample FATE and complete implementation-level evidence remain pending.
                    String8 list_arguments[] = {S8("make"), S8("SAMPLES="), S8("IGNORE_TESTS="), S8("GEN=no"), S8("TARGET_EXEC="), S8("fate-list")};
                    bool prepared = ffmpeg_make(arena, directories[i], S8("tests/pixfmts.mak"), S8("fate-list-prerequisites"));
                    FfmpegCommand list = ffmpeg_run(arena, directories[i], path_join(arena, directories[i], S8("fate-list")), (SliceString8)BUSTER_ARRAY_TO_SLICE(list_arguments), BUSTER_FFMPEG_CC_SECONDS);
                    bool ran = prepared && ffmpeg_ok(list) && ffmpeg_make(arena, directories[i], S8("fate"), S8("fate"));
                    fate[i] = ffmpeg_fate_counts(arena, directories[i], list.output, ran);
                }
            }
            string_print(S8("FFMPEG_CENSUS compiler={S8} configure={u32} profile={u32} build={u32} checkasm_attempted={u32} checkasm={u32} functions={u64} media={u32} no_sample_fate={u32}\n"),
                         compiler_names[i], (u32)configured[i], (u32)profile_valid[i], (u32)built[i], (u32)checkasm_attempted[i], (u32)checkasm[i],
                         checkasm_functions[i], (u32)media[i], (u32)fate[i]);
        }
        String8 checkasm_count_comparison = S8("not-run");
        if (optimized && checkasm_attempted[0] && checkasm_attempted[1])
        {
            checkasm_count_comparison = ffmpeg_checkasm_counts_match(checkasm_functions[0], checkasm_functions[1]) ?
                                       S8("equal-diagnostic-only") : S8("different-unqualified");
        }
        bool matching = false;
        if (configured[0] && configured[1])
        {
            String8 reference = clang_suite_read(arena, path_join(arena, directories[0], S8("config_components.h")));
            String8 candidate = clang_suite_read(arena, path_join(arena, directories[1], S8("config_components.h")));
            matching = reference.length && string_equal(reference, candidate) && (!optimized || (profile_valid[0] && profile_valid[1]));
            String8 diff_arguments[] = {S8("diff"), S8("-u"), path_join(arena, directories[0], S8("config.h")), path_join(arena, directories[1], S8("config.h"))};
            FfmpegCommand diff = ffmpeg_run(arena, output, path_join(arena, output, S8("configuration-diff")), (SliceString8)BUSTER_ARRAY_TO_SLICE(diff_arguments), BUSTER_FFMPEG_CC_SECONDS);
            String8 reference_features = ffmpeg_feature_config(arena, clang_suite_read(arena, path_join(arena, directories[0], S8("config.h"))));
            String8 candidate_features = ffmpeg_feature_config(arena, clang_suite_read(arena, path_join(arena, directories[1], S8("config.h"))));
            matching = diff.recorded && reference_features.length && string_equal(reference_features, candidate_features) && matching;
        }
        if (claimed)
        {
            bool pristine = ffmpeg_pristine(arena, checkout);
            String8 receipt = string_format(arena, S8("BUSTER_FFMPEG_CENSUS_V1\nupstream={S8}\nconfiguration={S8}\n"
                                                     "nasm_path={S8}\nreference_configure={u32}\nreference_profile_manifest={u32}\nreference_build={u32}\n"
                                                     "reference_checkasm_status={S8}\nreference_checkasm_attempted={u32}\nreference_checkasm_function_pointers={u64}\nreference_media={u32}\nreference_no_sample_fate={u32}\n"
                                                     "buster_configure={u32}\nbuster_profile_manifest={u32}\nbuster_build={u32}\n"
                                                     "buster_checkasm_status={S8}\nbuster_checkasm_attempted={u32}\nbuster_checkasm_function_pointers={u64}\nbuster_media={u32}\nbuster_no_sample_fate={u32}\n"
                                                     "cross_compiler_checkasm_count_comparison={S8}\nmatching_configurations={u32}\npristine_after={u32}\n"
                                                     "sample_fate=not-run\nproduction_dispatch=not-run\nper_function_denominator=unknown\n"
                                                     "video_media=not-run\nperformance_qualified=0\nwhole_issue_qualified=0\n"),
                                            S8(BUSTER_FFMPEG_COMMIT), optimized ? S8("assembly-enabled-software") : S8("portable-software"), nasm,
                                            (u32)configured[0], (u32)profile_valid[0], (u32)built[0],
                                            (checkasm_attempted[0] ? (checkasm[0] ? S8("pass") : S8("fail")) : S8("not-run")),
                                            (u32)checkasm_attempted[0], checkasm_functions[0], (u32)media[0], (u32)fate[0],
                                            (u32)configured[1], (u32)profile_valid[1], (u32)built[1],
                                            (checkasm_attempted[1] ? (checkasm[1] ? S8("pass") : S8("fail")) : S8("not-run")),
                                            (u32)checkasm_attempted[1], checkasm_functions[1],
                                            (u32)media[1], (u32)fate[1], checkasm_count_comparison, (u32)matching, (u32)pristine);
            bool written = clang_suite_write(arena, path_join(arena, output, S8("receipt.txt")), receipt);
            bool checkasm_valid = !optimized || (checkasm[0] && checkasm[1]);
            valid = written && pristine && configured[0] && configured[1] && profile_valid[0] && profile_valid[1] &&
                    built[0] && built[1] && checkasm_valid && media[0] && media[1] && fate[0] && fate[1] && matching && valid;
            string_print(S8("FFMPEG_RESULT profile={S8} census={S8} production_dispatch=not-run full_qualification=pending artifacts={S8}\n"),
                         optimized ? S8("assembly-enabled") : S8("portable"), valid ? S8("pass") : S8("fail"), output);
        }
        result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
#else
        string_print(S8("error: FFmpeg profiles currently require Linux x86-64\n"));
#endif
    }
    else
    {
        string_print(S8("usage: test_ffmpeg --harness-self-test | --self-test CHECKOUT | --portable CHECKOUT FRESH_RESULTS ABSOLUTE_IDE | --optimized CHECKOUT FRESH_RESULTS ABSOLUTE_IDE ABSOLUTE_NASM\n"));
    }
    return result;
}
