// FFmpeg qualification intake (#3177): pristine inputs and independent builds.
// ffmpeg_main owns the opt-in driver action; ffmpeg_cc records and forwards
// every candidate compiler action without replacement objects or source edits.
// ffmpeg_run retains argv/cwd/stdout/stderr/status with bounded process cleanup.
// This initial portable census does not qualify optimized code or sample FATE.

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

BUSTER_GLOBAL_LOCAL bool ffmpeg_configure(Arena* arena, String8 checkout, String8 directory, String8 cc, String8 clang)
{
    String8 arguments[] = {
        S8("sh"), path_join(arena, checkout, S8("configure")),
        S8("--arch=x86_64"), S8("--cpu=generic"), S8("--target-os=linux"),
        S8("--disable-autodetect"), S8("--disable-asm"), S8("--disable-inline-asm"),
        S8("--disable-x86asm"), S8("--disable-hwaccels"), S8("--disable-network"),
        S8("--disable-doc"), S8("--disable-debug"), S8("--disable-stripping"),
        S8("--disable-gpl"), S8("--disable-nonfree"), S8("--disable-version3"),
        S8("--disable-everything"), S8("--disable-avdevice"), S8("--disable-postproc"),
        S8("--enable-ffmpeg"), S8("--enable-ffprobe"), S8("--enable-static"), S8("--disable-shared"),
        S8("--enable-protocol=file,pipe"),
        S8("--enable-decoder=h264,hevc,flac,mp3,aac,pcm_s16le,rawvideo"),
        S8("--enable-parser=h264,hevc,flac,mpegaudio,aac"),
        S8("--enable-demuxer=matroska,mov,flac,mp3,aac,wav,image2,rawvideo,pcm_s16le"),
        S8("--enable-encoder=flac,pcm_s16le,rawvideo"),
        S8("--enable-muxer=framecrc,framemd5,crc,null,wav,flac,matroska,mp4,image2,rawvideo,pcm_s16le"),
        S8("--enable-filter=aresample,aformat,format,scale,anull,null,abuffer,buffer,abuffersink,buffersink"),
        string_format(arena, S8("--cc={S8}"), cc),
        string_format(arena, S8("--ld={S8}"), clang),
        string_format(arena, S8("--host-cc={S8}"), cc),
        string_format(arena, S8("--host-ld={S8}"), clang),
        string_format(arena, S8("--dep-cc={S8}"), clang),
    };
    FfmpegCommand command = ffmpeg_run(arena, directory, path_join(arena, directory, S8("configure")), (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), BUSTER_FFMPEG_COMMAND_SECONDS);
    return ffmpeg_ok(command);
}

BUSTER_GLOBAL_LOCAL bool ffmpeg_make(Arena* arena, String8 directory, String8 target, String8 prefix)
{
    String8 arguments[] = {S8("make"), S8("-k"), S8("-j2"), S8("V=1"), S8("SAMPLES="), S8("IGNORE_TESTS="), S8("GEN=no"), S8("TARGET_EXEC="), S8("THREADS=1"), target};
    FfmpegCommand command = ffmpeg_run(arena, directory, path_join(arena, directory, prefix), (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), BUSTER_FFMPEG_COMMAND_SECONDS);
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
    bool portable = arguments.length == 4 && string_equal(arguments.pointer[0], S8("--portable"));
    if (cc)
    {
        result = ffmpeg_cc(arena, arguments);
    }
    else if (self_test)
    {
        String8 checkout = os_path_absolute(arena, arguments.pointer[1], true);
        result = ffmpeg_self_test(arena, checkout) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (portable)
    {
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64
        String8 checkout = os_path_absolute(arena, arguments.pointer[1], true);
        String8 requested = os_path_absolute_lexical(arena, arguments.pointer[2], true);
        String8 parent = os_path_absolute(arena, path_parent(arena, requested), true);
        String8 name = quickjs_basename(requested);
        String8 output = path_join(arena, parent, name);
        String8 ide = os_path_absolute(arena, arguments.pointer[3], true);
        String8 driver = os_path_absolute(arena, driver_argument, true);
        String8 clang = executable_resolve_in_path(arena, S8("clang"));
        bool valid = checkout.length && parent.length && ide.length && driver.length && clang.length &&
                     clang_suite_safe_path(name) && !string_equal(parent, checkout) &&
                     !string_starts_with_sequence(parent, string_format(arena, S8("{S8}/"), checkout)) &&
                     ffmpeg_command_path(driver) && ffmpeg_command_path(ide) && ffmpeg_command_path(output) &&
                     ffmpeg_command_path(clang) && ffmpeg_pristine(arena, checkout);
        if (valid)
        {
            valid = os_make_directory_exclusive(output).created;
        }
        bool claimed = valid;
        bool configured[2] = {0};
        bool built[2] = {0};
        bool media[2] = {0};
        bool fate[2] = {0};
        String8 directories[2] = {path_join(arena, output, S8("reference")), path_join(arena, output, S8("buster"))};
        String8 pcm = path_join(arena, output, S8("fixture.pcm"));
        if (valid)
        {
            String8 git_arguments[] = {S8("git"), S8("--no-replace-objects"), S8("ls-tree"), S8("-r"), S8("--full-tree"), S8(BUSTER_FFMPEG_COMMIT)};
            FfmpegCommand inventory = ffmpeg_run(arena, checkout, path_join(arena, output, S8("source-inventory")), (SliceString8)BUSTER_ARRAY_TO_SLICE(git_arguments), BUSTER_FFMPEG_CC_SECONDS);
            valid = ffmpeg_ok(inventory) && ffmpeg_pcm_fixture(arena, pcm);
            String8 compiler_arguments[] = {S8("sha256sum"), driver, ide, clang};
            valid = ffmpeg_ok(ffmpeg_run(arena, output, path_join(arena, output, S8("compiler-identities")), (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_arguments), BUSTER_FFMPEG_CC_SECONDS)) && valid;
        }
        // Preserve the independent reference even if the candidate configure
        // fails. Each side uses normal unmodified upstream generation.
        for (u32 i = 0; valid && i < BUSTER_ARRAY_LENGTH(directories); i += 1)
        {
            bool ready = os_make_directory_exclusive(directories[i]).created;
            String8 ledger = path_join(arena, directories[i], S8("compiler-actions"));
            ready = os_make_directory_exclusive(ledger).created && ready;
            String8 cc_command = i == 0 ? clang : string_format(arena, S8("{S8} test_ffmpeg --cc {S8} {S8}"), driver, ide, ledger);
            configured[i] = ready && ffmpeg_configure(arena, checkout, directories[i], cc_command, clang);
            string_print(S8("FFMPEG_CONFIGURE compiler={S8} status={S8}\n"), i ? S8("buster") : S8("clang"), configured[i] ? S8("pass") : S8("fail"));
            if (configured[i])
            {
                built[i] = ffmpeg_make(arena, directories[i], S8("all"), S8("build"));
                if (built[i])
                {
                    media[i] = ffmpeg_media(arena, directories[i], pcm);
                    // The denominator is only upstream's no-external-sample
                    // tests for this declared configuration. Sample FATE stays pending.
                    String8 list_arguments[] = {S8("make"), S8("SAMPLES="), S8("IGNORE_TESTS="), S8("GEN=no"), S8("TARGET_EXEC="), S8("fate-list")};
                    FfmpegCommand list = ffmpeg_run(arena, directories[i], path_join(arena, directories[i], S8("fate-list")), (SliceString8)BUSTER_ARRAY_TO_SLICE(list_arguments), BUSTER_FFMPEG_CC_SECONDS);
                    bool ran = ffmpeg_ok(list) && ffmpeg_make(arena, directories[i], S8("fate"), S8("fate"));
                    fate[i] = ffmpeg_fate_counts(arena, directories[i], list.output, ran);
                }
            }
            string_print(S8("FFMPEG_CENSUS compiler={S8} configure={u32} build={u32} media={u32} no_sample_fate={u32}\n"),
                         i ? S8("buster") : S8("clang"), (u32)configured[i], (u32)built[i], (u32)media[i], (u32)fate[i]);
        }
        bool matching = false;
        if (configured[0] && configured[1])
        {
            String8 reference = clang_suite_read(arena, path_join(arena, directories[0], S8("config_components.h")));
            String8 candidate = clang_suite_read(arena, path_join(arena, directories[1], S8("config_components.h")));
            matching = reference.length && string_equal(reference, candidate);
            String8 diff_arguments[] = {S8("diff"), S8("-u"), path_join(arena, directories[0], S8("config.h")), path_join(arena, directories[1], S8("config.h"))};
            FfmpegCommand diff = ffmpeg_run(arena, output, path_join(arena, output, S8("configuration-diff")), (SliceString8)BUSTER_ARRAY_TO_SLICE(diff_arguments), BUSTER_FFMPEG_CC_SECONDS);
            String8 reference_features = ffmpeg_feature_config(arena, clang_suite_read(arena, path_join(arena, directories[0], S8("config.h"))));
            String8 candidate_features = ffmpeg_feature_config(arena, clang_suite_read(arena, path_join(arena, directories[1], S8("config.h"))));
            matching = diff.recorded && reference_features.length && string_equal(reference_features, candidate_features) && matching;
        }
        if (claimed)
        {
            bool pristine = ffmpeg_pristine(arena, checkout);
            String8 receipt = string_format(arena, S8("BUSTER_FFMPEG_CENSUS_V1\nupstream={S8}\nconfiguration=portable-software\n"
                                                     "reference_configure={u32}\nreference_build={u32}\nreference_media={u32}\nreference_no_sample_fate={u32}\n"
                                                     "buster_configure={u32}\nbuster_build={u32}\nbuster_media={u32}\nbuster_no_sample_fate={u32}\n"
                                                     "matching_configurations={u32}\npristine_after={u32}\n"
                                                     "sample_fate=not-run\noptimized=not-run\nvideo_media=not-run\nperformance_qualified=0\n"
                                                     "whole_issue_qualified=0\n"),
                                            S8(BUSTER_FFMPEG_COMMIT), (u32)configured[0], (u32)built[0], (u32)media[0], (u32)fate[0],
                                            (u32)configured[1], (u32)built[1], (u32)media[1], (u32)fate[1], (u32)matching, (u32)pristine);
            bool written = clang_suite_write(arena, path_join(arena, output, S8("receipt.txt")), receipt);
            valid = written && pristine && configured[0] && configured[1] && built[0] && built[1] && media[0] && media[1] && fate[0] && fate[1] && matching && valid;
            string_print(S8("FFMPEG_RESULT portable_census={S8} full_qualification=pending artifacts={S8}\n"), valid ? S8("pass") : S8("fail"), output);
        }
        result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
#else
        string_print(S8("error: FFmpeg portable census currently requires Linux x86-64\n"));
#endif
    }
    else
    {
        string_print(S8("usage: test_ffmpeg --self-test CHECKOUT | --portable CHECKOUT FRESH_RESULTS ABSOLUTE_IDE\n"));
    }
    return result;
}
