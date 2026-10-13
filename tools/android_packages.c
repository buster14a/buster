// Concurrent Android package batches, included only by the native build driver.
// android/test_ci.sh's runtime mode calls android_packages_main through
// BUSTER_ANDROID_HOST_DRIVER (else build.sh) when the required AArch64 package
// gate is on. The x86_64 and arm64-v8a `test_ci.sh --package-only` batches then
// build at the same time, beside the booting emulator, instead of one after the
// other (#3912). Each batch keeps its configure/build/verify glue in shell; the
// schedule, status aggregation and replay live here (#2467).
//
//   android_packages_parse        argument contract: script, configurations and
//                                 at most one batch per supported ABI
//   android_packages_environment  inherited environment plus batch overrides
//   android_packages_run          spawns every batch, drains every capture from
//                                 its own lane (android_packages_wait_lane), then
//                                 replays them in argument order and prints the
//                                 ANDROID_PACKAGES_BATCH records; a
//                                 BUSTER_SINGLE_THREADED driver streams the
//                                 batches one after another instead
//   android_packages_memory       ANDROID_PACKAGES_MEMORY: physical memory, the
//                                 batches' wait4 peaks and the emulator's VmHWM
//   android_packages_self_test    re-invokes the driver as fake batches
//                                 (android_packages_fake_batch)
//   android_packages_main         command entry
// ci_unit_clean and ci_unit_replay come from tools/ci_unit_tests.c.
#define ANDROID_PACKAGES_MAX 2
#define ANDROID_PACKAGES_MAX_CONFIGS 2
// One hosted batch logs about 0.44 MB (run 38312435360). The bound only stops a
// runaway batch; the FAIL overflow policy keeps a truncated replay, which could
// hide the batch's records, from passing.
#define ANDROID_PACKAGES_CAPTURE_LIMIT BUSTER_MB(64)
#define ANDROID_PACKAGES_SELF_TEST_CAPTURE_LIMIT BUSTER_KB(64)
// Larger than the default Linux, macOS and Windows pipe buffers: a fake batch
// whose capture is not drained while its sibling runs blocks before it can
// reach the rendezvous.
#define ANDROID_PACKAGES_FLOOD_BYTES BUSTER_MB(1)
#define ANDROID_PACKAGES_RENDEZVOUS_US (60ull * 1000000ull)
#define ANDROID_PACKAGES_SELF_TEST_TIMEOUT_US (120ull * 1000000ull)

typedef struct AndroidPackage AndroidPackage;
struct AndroidPackage
{
    String8 abi;
    String8 build_directory;
};

typedef struct AndroidPackagesRequest AndroidPackagesRequest;
struct AndroidPackagesRequest
{
    String8 script;
    String8 emulator_marker;
    String8 configs[ANDROID_PACKAGES_MAX_CONFIGS];
    AndroidPackage packages[ANDROID_PACKAGES_MAX];
    u64 config_count;
    u64 package_count;
};

typedef struct AndroidPackagesBatch AndroidPackagesBatch;
struct AndroidPackagesBatch
{
    Arena* arena;
    ProcessSpawnResult spawn;
    ProcessWaitResult wait;
    u64 start;
    u64 finish;
    bool clean;
};

typedef struct AndroidPackagesWork AndroidPackagesWork;
struct AndroidPackagesWork
{
    AndroidPackagesBatch batches[ANDROID_PACKAGES_MAX];
    u64 count;
    u64 timeout;
};

BUSTER_GLOBAL_LOCAL bool android_packages_abi_valid(String8 abi)
{
    bool result = string_equal(abi, S8("x86_64")) || string_equal(abi, S8("arm64-v8a"));
    return result;
}

BUSTER_GLOBAL_LOCAL bool android_packages_parse(Arena* arena, SliceString8 arguments, AndroidPackagesRequest* request)
{
    *request = (AndroidPackagesRequest){0};
    bool result = true;
    u64 i = 0;
    while (result && i < arguments.length)
    {
        String8 argument = arguments.pointer[i];
        String8* value = string_equal(argument, S8("--script")) ? &request->script : string_equal(argument, S8("--emulator-marker")) ? &request->emulator_marker : 0;
        if (value && i + 1 < arguments.length && !value->length)
        {
            *value = arguments.pointer[i + 1];
            result = value->length != 0;
            i += 2;
        }
        else if (string_equal(argument, S8("--configs")) && !request->config_count)
        {
            i += 1;
            while (result && i < arguments.length && !string_starts_with_sequence(arguments.pointer[i], S8("--")))
            {
                String8 config = arguments.pointer[i];
                result = (string_equal(config, S8("Debug")) || string_equal(config, S8("Release"))) && request->config_count < ANDROID_PACKAGES_MAX_CONFIGS;
                for (u64 j = 0; result && j < request->config_count; j += 1)
                {
                    result = !string_equal(request->configs[j], config);
                }
                if (result)
                {
                    request->configs[request->config_count] = config;
                    request->config_count += 1;
                    i += 1;
                }
            }
            result = result && request->config_count != 0;
        }
        else if (string_equal(argument, S8("--package")) && i + 2 < arguments.length && request->package_count < ANDROID_PACKAGES_MAX)
        {
            AndroidPackage package = {.abi = arguments.pointer[i + 1], .build_directory = arguments.pointer[i + 2]};
            result = android_packages_abi_valid(package.abi) && package.build_directory.length != 0;
            String8 directory = result ? os_path_absolute_lexical(arena, package.build_directory, false) : (String8){0};
            for (u64 j = 0; result && j < request->package_count; j += 1)
            {
                AndroidPackage other = request->packages[j];
                result = !string_equal(other.abi, package.abi) && !string_equal(os_path_absolute_lexical(arena, other.build_directory, false), directory);
            }
            if (result)
            {
                request->packages[request->package_count] = package;
                request->package_count += 1;
                i += 3;
            }
        }
        else
        {
            result = false;
        }
    }
    result = result && request->script.length && request->config_count && request->package_count;
    return result;
}

BUSTER_GLOBAL_LOCAL void android_packages_environment(Arena* arena, AndroidPackagesRequest* request, u64 index, SliceString8* keys, SliceString8* values)
{
    AndroidPackage package = request->packages[index];
    String8 override_keys[] = {
        S8("BUSTER_ANDROID_EXECUTION"), S8("BUSTER_ANDROID_ABI"), S8("BUSTER_ANDROID_BUILD_DIRECTORY"),
        S8("BUSTER_ANDROID_AARCH64_PACKAGE"), S8("BUSTER_ANDROID_EMULATOR_STARTED_MARKER"),
    };
    String8 override_values[] = {
        S8("package"), package.abi, package.build_directory, S8("0"),
        string_format(arena, S8("{S8}.{S8}-package"), request->emulator_marker, package.abi),
    };
    // Package batches never start, query or stop the emulator; without a
    // marker they keep the inherited one.
    u64 override_count = request->emulator_marker.length ? BUSTER_ARRAY_LENGTH(override_keys) : BUSTER_ARRAY_LENGTH(override_keys) - 1;
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    keys->pointer = arena_allocate(arena, String8, inherited_keys.length + override_count);
    values->pointer = arena_allocate(arena, String8, inherited_keys.length + override_count);
    keys->length = 0;
    values->length = 0;
    for (u64 i = 0; i < override_count; i += 1)
    {
        keys->pointer[keys->length++] = override_keys[i];
        values->pointer[values->length++] = override_values[i];
    }
    for (u64 i = 0; i < inherited_keys.length; i += 1)
    {
        bool replaced = false;
        for (u64 j = 0; j < override_count; j += 1)
        {
            replaced = replaced || string_equal(inherited_keys.pointer[i], override_keys[j]);
        }
        if (!replaced)
        {
            keys->pointer[keys->length++] = inherited_keys.pointer[i];
            values->pointer[values->length++] = inherited_values.pointer[i];
        }
    }
}

BUSTER_GLOBAL_LOCAL ProcessSpawnOptions android_packages_spawn_options(u64 capture_limit)
{
    ProcessSpawnOptions options = {.search_path = 1, .observe_resources = 1, .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL};
    if (capture_limit)
    {
        options.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR);
        options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = capture_limit;
        options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = capture_limit;
        options.capture_limits.total = 2 * capture_limit;
    }
    return options;
}

BUSTER_GLOBAL_LOCAL void android_packages_wait_lane(void* argument)
{
    AndroidPackagesWork* work = argument;
    LaneRange range = lane_range(work->count);
    for (u64 index = range.start; index < range.end; index += 1)
    {
        AndroidPackagesBatch* batch = work->batches + index;
        if (batch->spawn.handle)
        {
            batch->wait = os_process_wait_deadline(batch->arena, batch->spawn, work->timeout);
        }
        batch->finish = os_now_microseconds();
        batch->clean = ci_unit_clean(batch->spawn, batch->wait);
    }
}

BUSTER_GLOBAL_LOCAL String8 android_packages_peak(Arena* arena, AndroidPackagesBatch* batch)
{
    bool observed = batch->wait.resources.memory_status == PROCESS_RESOURCE_OBSERVED;
    String8 result = observed ? string_format(arena, S8("{u64}"), batch->wait.resources.peak_memory_bytes) : S8("unavailable");
    return result;
}

// Runs each batch as `launcher --package-only --configs <configs>`. A failed
// batch never cancels its sibling: every batch finishes and is reported. The
// captures stay in `work` until android_packages_release.
BUSTER_GLOBAL_LOCAL bool android_packages_run(Arena* arena, AndroidPackagesRequest* request, SliceString8 launcher, u64 capture_limit,
                                              u64 timeout, bool replay, AndroidPackagesWork* work)
{
    // Waiting on one child at a time can block the other on a full pipe, so a
    // serial driver streams instead of capturing (as test_units_partitioned).
    bool concurrent = capture_limit != 0 && !BUSTER_SINGLE_THREADED;
    String8 mode = concurrent ? S8("concurrent") : S8("serial");
    *work = (AndroidPackagesWork){.count = request->package_count, .timeout = timeout};
    u64 argument_count = launcher.length + 2 + request->config_count;
    string_print(S8("ANDROID_PACKAGES_PLAN mode={S8} batches={u64} configs={u64}\n"), mode, request->package_count, request->config_count);
    bool admitted = true;
    for (u64 i = 0; i < request->package_count; i += 1)
    {
        AndroidPackagesBatch* batch = work->batches + i;
        String8* arguments = arena_allocate(arena, String8, argument_count);
        for (u64 j = 0; j < launcher.length; j += 1)
        {
            arguments[j] = launcher.pointer[j];
        }
        arguments[launcher.length] = S8("--package-only");
        arguments[launcher.length + 1] = S8("--configs");
        for (u64 j = 0; j < request->config_count; j += 1)
        {
            arguments[launcher.length + 2 + j] = request->configs[j];
        }
        SliceString8 command = {.pointer = arguments, .length = argument_count};
        SliceString8 keys = {0};
        SliceString8 values = {0};
        android_packages_environment(arena, request, i, &keys, &values);
        string_print(S8("Starting the Android {S8} package batch in {S8} ({S8}):\n"), request->packages[i].abi, request->packages[i].build_directory, mode);
        command_print(command);
        batch->start = os_now_microseconds();
        if (concurrent)
        {
            batch->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(16) + 4 * capture_limit, .flags = {.no_pool = 1}});
            if (batch->arena && admitted)
            {
                batch->spawn = os_process_spawn(command, keys, values, android_packages_spawn_options(capture_limit));
            }
            admitted = admitted && batch->spawn.handle != 0;
        }
        else
        {
            batch->spawn = os_process_spawn(command, keys, values, android_packages_spawn_options(0));
            if (batch->spawn.handle)
            {
                batch->wait = os_process_wait_deadline(arena, batch->spawn, timeout);
            }
            batch->finish = os_now_microseconds();
            batch->clean = ci_unit_clean(batch->spawn, batch->wait);
        }
    }
    if (concurrent)
    {
        // A batch that could not start fails the command; stop its siblings.
        if (!admitted)
        {
            work->timeout = 1;
        }
        lane_run(work->count, &android_packages_wait_lane, work);
    }
    bool result = admitted;
    for (u64 i = 0; i < work->count; i += 1)
    {
        AndroidPackagesBatch* batch = work->batches + i;
        bool replayed = true;
        if (concurrent && replay)
        {
            string_print(S8("Android {S8} package batch output, captured while the batches ran concurrently:\n"), request->packages[i].abi);
            replayed = ci_unit_replay(batch->wait);
        }
        bool clean = batch->clean && replayed;
        string_print(S8("ANDROID_PACKAGES_BATCH abi={S8} mode={S8} native_status={u32} timed_out={u32} capture_failed={u32} elapsed_seconds={u64} peak_rss_bytes={S8} status={S8}\n"),
                     request->packages[i].abi, mode, batch->wait.platform_status, (u32)batch->wait.timed_out,
                     (u32)(batch->wait.capture_failed || batch->wait.output_truncated || batch->wait.capture_limit_exceeded || !replayed),
                     (batch->finish - batch->start) / 1000000, android_packages_peak(arena, batch), clean ? S8("pass") : S8("fail"));
        result = result && clean;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool android_packages_release(AndroidPackagesWork* work)
{
    bool result = true;
    for (u64 i = 0; i < work->count; i += 1)
    {
        AndroidPackagesBatch* batch = work->batches + i;
        if (batch->arena)
        {
            result = arena_destroy(batch->arena, 1) && result;
            batch->arena = 0;
        }
    }
    return result;
}

// One `<key> <decimal> kB` line of /proc/<pid>/status, in bytes.
BUSTER_GLOBAL_LOCAL bool android_packages_status_bytes(String8 status, String8 key, u64* bytes)
{
    u64 offset = string_first_sequence(status, key);
    bool result = offset != BUSTER_STRING_NO_MATCH;
    if (result)
    {
        String8 rest = string_slice(status, offset + key.length, status.length);
        u64 skip = 0;
        while (skip < rest.length && (rest.pointer[skip] == ' ' || rest.pointer[skip] == '\t'))
        {
            skip += 1;
        }
        rest = string_slice(rest, skip, rest.length);
        IntegerParsingU64 value = string8_parse_u64_decimal(rest);
        result = value.status == INTEGER_PARSING_SUCCESS && value.length && value.value <= UINT64_MAX / 1024 &&
                 string_starts_with_sequence(string_slice(rest, value.length, rest.length), S8(" kB\n"));
        if (result)
        {
            *bytes = value.value * 1024;
        }
    }
    return result;
}

// The emulator's resident high water so far, read once after the batches.
// Its boot overlaps them, so this bounds its share of the concurrent peak.
// /proc is Linux-only; elsewhere the record says unavailable.
BUSTER_GLOBAL_LOCAL String8 android_packages_emulator(Arena* arena, String8 marker)
{
    String8 result = S8("emulator=unavailable");
#if BUSTER_LINUX
    String8 text = BYTE_SLICE_TO_STRING(8, file_read_checked(arena, marker, (FileReadOptions){0}).bytes);
    IntegerParsingU64 pid = string8_parse_u64_decimal(text);
    bool valid = text.pointer && pid.status == INTEGER_PARSING_SUCCESS && pid.length && pid.value &&
                 (pid.length == text.length || text.pointer[pid.length] == '\n');
    if (valid)
    {
        String8 status = BYTE_SLICE_TO_STRING(8, file_read_checked(arena, string_format(arena, S8("/proc/{u64}/status"), pid.value), (FileReadOptions){0}).bytes);
        String8 comm = BYTE_SLICE_TO_STRING(8, file_read_checked(arena, string_format(arena, S8("/proc/{u64}/comm"), pid.value), (FileReadOptions){0}).bytes);
        u64 peak = 0;
        u64 resident = 0;
        valid = status.pointer && comm.length > 1 && comm.pointer[comm.length - 1] == '\n' && android_packages_status_bytes(status, S8("\nVmHWM:"), &peak) &&
                android_packages_status_bytes(status, S8("\nVmRSS:"), &resident);
        if (valid)
        {
            result = string_format(arena, S8("emulator_pid={u64} emulator_comm={S8} emulator_peak_rss_bytes={u64} emulator_rss_bytes={u64}"), pid.value,
                                   string_slice(comm, 0, comm.length - 1), peak, resident);
        }
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(marker);
#endif
    return result;
}

// A batch peak is wait4's ru_maxrss: the largest process in its tree, and at
// least the driver's own high water when it spawned the batch, because Linux
// carries the pre-exec image's high water across exec. The sum of the batch
// peaks plus the emulator's peak therefore bounds what was resident at once.
BUSTER_GLOBAL_LOCAL void android_packages_memory(Arena* arena, AndroidPackagesRequest* request, AndroidPackagesWork* work)
{
    bool observed = true;
    u64 sum = 0;
    for (u64 i = 0; i < work->count; i += 1)
    {
        observed = observed && work->batches[i].wait.resources.memory_status == PROCESS_RESOURCE_OBSERVED;
        sum += work->batches[i].wait.resources.peak_memory_bytes;
    }
    String8 emulator = request->emulator_marker.length ? android_packages_emulator(arena, request->emulator_marker) : S8("emulator=not-requested");
    string_print(S8("ANDROID_PACKAGES_MEMORY physical_bytes={u64} batch_peak_sum_bytes={S8} {S8}\n"), os_get_physical_memory_size(),
                 observed ? string_format(arena, S8("{u64}"), sum) : S8("unavailable"), emulator);
}

// A fake package batch for android_packages_self_test. It checks what
// android_packages_run handed it; `overlap*` batches flood stdout past any
// pipe buffer and then wait for their sibling to start, `serial*` batches
// require the first batch to have finished, `flood` overruns the capture, and
// a `*-fail` arm64-v8a batch fails after its checks.
BUSTER_GLOBAL_LOCAL ProcessResult android_packages_fake_batch(Arena* arena, String8 scratch, String8 mode, SliceString8 suffix)
{
    String8 abi = os_get_environment_variable(S8("BUSTER_ANDROID_ABI"));
    bool first = string_equal(abi, S8("x86_64"));
    String8 sibling = first ? S8("arm64-v8a") : S8("x86_64");
    bool overlap = string_starts_with_sequence(mode, S8("overlap"));
    bool serial = string_starts_with_sequence(mode, S8("serial"));
    String8 expected[] = {S8("--package-only"), S8("--configs"), S8("Debug"), S8("Release")};
    String8 keys[] = {S8("BUSTER_ANDROID_EXECUTION"), S8("BUSTER_ANDROID_AARCH64_PACKAGE"), S8("BUSTER_ANDROID_BUILD_DIRECTORY"), S8("BUSTER_ANDROID_EMULATOR_STARTED_MARKER")};
    String8 values[] = {S8("package"), S8("0"), string_format(arena, S8("{S8}/{S8}"), scratch, abi), string_format(arena, S8("{S8}/emulator.started.{S8}-package"), scratch, abi)};
    bool valid = android_packages_abi_valid(abi) && suffix.length == BUSTER_ARRAY_LENGTH(expected);
    for (u64 i = 0; valid && i < suffix.length; i += 1)
    {
        valid = string_equal(suffix.pointer[i], expected[i]);
    }
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(keys); i += 1)
    {
        valid = string_equal(os_get_environment_variable(keys[i]), values[i]);
    }
    if (valid && !serial)
    {
        u8 filler[4096];
        memset(filler, 'f', sizeof(filler));
        filler[sizeof(filler) - 1] = '\n';
        for (u64 written = 0; valid && written < ANDROID_PACKAGES_FLOOD_BYTES; written += sizeof(filler))
        {
            OsFileTransferResult transfer = os_file_write_checked(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){filler, sizeof(filler)});
            valid = !transfer.error.v && transfer.transferred == sizeof(filler);
        }
    }
    String8 sibling_started = string_format(arena, S8("{S8}/{S8}.started"), scratch, sibling);
    valid = valid && file_write(string_format(arena, S8("{S8}/{S8}.started"), scratch, abi), BUSTER_SLICE_TO_BYTE_SLICE(S8("1\n")));
    if (valid && overlap)
    {
        bool met = false;
        u64 start = os_now_microseconds();
        while (!met && os_now_microseconds() - start < ANDROID_PACKAGES_RENDEZVOUS_US)
        {
            TemporalArena scratch_arena = scratch_begin(&arena, 1);
            met = path_exists(scratch_arena.arena, sibling_started);
            scratch_end(scratch_arena);
        }
        valid = met;
    }
    else if (valid && serial)
    {
        valid = first ? !path_exists(arena, sibling_started) : path_exists(arena, string_format(arena, S8("{S8}/{S8}.done"), scratch, sibling));
    }
    valid = valid && file_write(string_format(arena, S8("{S8}/{S8}.done"), scratch, abi), BUSTER_SLICE_TO_BYTE_SLICE(S8("1\n")));
    string_print(S8("FAKE_ANDROID_PACKAGE_BATCH abi={S8} mode={S8} status={S8}\n"), abi, mode, valid ? S8("ok") : S8("invalid"));
    string_print_error(S8("FAKE_ANDROID_PACKAGE_BATCH_STDERR abi={S8}\n"), abi);
    bool failed = !first && string_ends_with_sequence(mode, S8("-fail"));
    ProcessResult result = valid && !failed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool android_packages_captured(AndroidPackagesBatch* batch, String8 line)
{
    String8 output = BYTE_SLICE_TO_STRING(8, batch->wait.streams[STANDARD_STREAM_OUTPUT]);
    bool result = string_first_sequence(output, line) != BUSTER_STRING_NO_MATCH;
    return result;
}

BUSTER_GLOBAL_LOCAL bool android_packages_self_test(Arena* arena, String8 executable, String8 scratch)
{
    bool result = true;
    AndroidPackagesRequest request = {0};
    // Argument contract: a valid pair, then each rejected shape: a missing
    // script, configuration list or package; an unknown, repeated or empty
    // configuration; an unsupported ABI; a package without a directory; an
    // unknown option; a duplicate ABI.
    bool split = true;
    String8 valid = S8("--script s.sh --configs Debug Release --package x86_64 a --package arm64-v8a b --emulator-marker m");
    result = android_packages_parse(arena, shell_split(arena, valid, &split), &request) && request.package_count == 2 && request.config_count == 2 &&
             string_equal(request.packages[1].abi, S8("arm64-v8a")) && string_equal(request.emulator_marker, S8("m"));
    String8 invalid[] = {
        S8("--configs Debug --package x86_64 a"), S8("--script s.sh --package x86_64 a"), S8("--script s.sh --configs Debug"),
        S8("--script s.sh --configs Profile --package x86_64 a"), S8("--script s.sh --configs Debug Debug --package x86_64 a"),
        S8("--script s.sh --configs --package x86_64 a"), S8("--script s.sh --configs Debug --package armeabi-v7a a"),
        S8("--script s.sh --configs Debug --package x86_64"), S8("--script s.sh --configs Debug --package x86_64 a --jobs"),
        S8("--script s.sh --configs Debug --package x86_64 a --package x86_64 b"),
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        result = !android_packages_parse(arena, shell_split(arena, invalid[i], &split), &request) && result;
    }
    // One build directory spelled relative and absolute.
    String8 shared_directory[] = {S8("--script"), S8("s.sh"), S8("--configs"), S8("Debug"), S8("--package"), S8("x86_64"), S8("a"), S8("--package"), S8("arm64-v8a"),
                                  os_path_absolute_lexical(arena, S8("a"), false)};
    result = !android_packages_parse(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(shared_directory), &request) && split && result;
    string_print(S8("ANDROID_PACKAGES_SELF_TEST case=arguments status={S8}\n"), result ? S8("pass") : S8("fail"));

    // Real process cases: each one re-invokes this driver as both fake batches.
    typedef struct AndroidPackagesSelfTestCase AndroidPackagesSelfTestCase;
    struct AndroidPackagesSelfTestCase
    {
        String8 mode;
        u64 capture_limit;
        bool passes;
    };
    AndroidPackagesSelfTestCase cases[] = {
#if BUSTER_SINGLE_THREADED
        {.mode = S8("serial"), .capture_limit = 0, .passes = true},
        {.mode = S8("serial-fail"), .capture_limit = 0, .passes = false},
#else
        {.mode = S8("overlap"), .capture_limit = ANDROID_PACKAGES_CAPTURE_LIMIT, .passes = true},
        {.mode = S8("overlap-fail"), .capture_limit = ANDROID_PACKAGES_CAPTURE_LIMIT, .passes = false},
        {.mode = S8("flood"), .capture_limit = ANDROID_PACKAGES_SELF_TEST_CAPTURE_LIMIT, .passes = false},
#endif
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        AndroidPackagesSelfTestCase test = cases[i];
        bool prepared = remove_path_recursive(arena, scratch);
        if (prepared)
        {
            make_directory_recursive(arena, scratch);
        }
        String8 x86_64_directory = string_format(arena, S8("{S8}/x86_64"), scratch);
        String8 arm64_directory = string_format(arena, S8("{S8}/arm64-v8a"), scratch);
        String8 marker = string_format(arena, S8("{S8}/emulator.started"), scratch);
        String8 arguments[] = {S8("--script"), S8("unused"), S8("--configs"), S8("Debug"), S8("Release"), S8("--package"), S8("x86_64"), x86_64_directory,
                               S8("--package"), S8("arm64-v8a"), arm64_directory, S8("--emulator-marker"), marker};
        prepared = prepared && android_packages_parse(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), &request);
        String8 launcher[] = {executable, S8("android_packages"), S8("--self-test-batch"), scratch, test.mode};
        AndroidPackagesWork work = {0};
        bool passed = prepared && android_packages_run(arena, &request, (SliceString8)BUSTER_ARRAY_TO_SLICE(launcher), test.capture_limit,
                                                       ANDROID_PACKAGES_SELF_TEST_TIMEOUT_US, false, &work);
        // Every case lets the x86_64 batch finish, including the failing ones.
        bool expected = prepared && passed == test.passes && path_exists(arena, string_format(arena, S8("{S8}/x86_64.done"), scratch));
        if (string_equal(test.mode, S8("flood")))
        {
            expected = expected && work.batches[0].wait.capture_limit_exceeded && work.batches[1].wait.capture_limit_exceeded;
        }
        else
        {
            String8 ok = string_format(arena, S8("FAKE_ANDROID_PACKAGE_BATCH abi=x86_64 mode={S8} status=ok"), test.mode);
            expected = expected && work.batches[0].clean && work.batches[1].clean == test.passes;
            expected = expected && (test.capture_limit == 0 || (android_packages_captured(&work.batches[0], ok) &&
                                                                android_packages_captured(&work.batches[1], S8("FAKE_ANDROID_PACKAGE_BATCH abi=arm64-v8a"))));
        }
        expected = android_packages_release(&work) && expected;
        string_print(S8("ANDROID_PACKAGES_SELF_TEST case={S8} status={S8}\n"), test.mode, expected ? S8("pass") : S8("fail"));
        result = expected && result;
    }
#if BUSTER_LINUX
    // The emulator reader against this process's own /proc entry.
    String8 marker = string_format(arena, S8("{S8}/self.pid"), scratch);
    u64 pid = os_get_current_process_id();
    make_directory_recursive(arena, scratch);
    bool reader = file_write(marker, BUSTER_SLICE_TO_BYTE_SLICE(string_format(arena, S8("{u64}\n"), pid))) &&
                  string_starts_with_sequence(android_packages_emulator(arena, marker), string_format(arena, S8("emulator_pid={u64} emulator_comm="), pid));
    reader = reader && file_write(marker, BUSTER_SLICE_TO_BYTE_SLICE(S8("not-a-pid\n"))) &&
             string_equal(android_packages_emulator(arena, marker), S8("emulator=unavailable"));
    string_print(S8("ANDROID_PACKAGES_SELF_TEST case=emulator-memory status={S8}\n"), reader ? S8("pass") : S8("fail"));
    result = reader && result;
#endif
    result = remove_path_recursive(arena, scratch) && result;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult android_packages_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    AndroidPackagesRequest request = {0};
    if (arguments.length >= 3 && string_equal(arguments.pointer[0], S8("--self-test-batch")))
    {
        SliceString8 suffix = {.pointer = arguments.pointer + 3, .length = arguments.length - 3};
        result = android_packages_fake_batch(arena, arguments.pointer[1], arguments.pointer[2], suffix);
    }
    else if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("--self-test")) && arguments.pointer[1].length)
    {
        bool passed = android_packages_self_test(arena, build_running_driver(arena), arguments.pointer[1]);
        string_print(S8("ANDROID_PACKAGES_SELF_TEST status={S8}\n"), passed ? S8("pass") : S8("fail"));
        result = passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (android_packages_parse(arena, arguments, &request))
    {
        String8 launcher[] = {S8("bash"), request.script};
        AndroidPackagesWork work = {0};
        bool passed = android_packages_run(arena, &request, (SliceString8)BUSTER_ARRAY_TO_SLICE(launcher), ANDROID_PACKAGES_CAPTURE_LIMIT, 0, true, &work);
        android_packages_memory(arena, &request, &work);
        passed = android_packages_release(&work) && passed;
        string_print(S8("ANDROID_PACKAGES_RESULT batches={u64} status={S8}\n"), request.package_count, passed ? S8("pass") : S8("fail"));
        result = passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else
    {
        string_print(S8("usage: android_packages --script <android/test_ci.sh> --configs <Debug|Release>... --package <x86_64|arm64-v8a> <build-directory> "
                        "[--package <abi> <build-directory>] [--emulator-marker <path>] | --self-test <scratch-directory>\n"));
    }
    return result;
}
