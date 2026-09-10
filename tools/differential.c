// Native differential runner, included by build.c. No shell, Python or testing
// framework is required. d_matrix owns option discovery, d_observe owns bounded
// children/evidence, d_case_run compares observations, and d_reduce performs a
// bounded iterative line reduction against independent host-compiler oracles.
#include <buster/lib/compiler/driver/codegen_configurations.h>
#include <signal.h>

#define D_ALLOCATOR(name, value) S8_INITIALIZER(name),
BUSTER_GLOBAL_LOCAL String8 const d_allocators[] = {BUSTER_CODEGEN_ALLOCATORS(D_ALLOCATOR) S8_INITIALIZER("alias-none"), S8_INITIALIZER("default")};
#undef D_ALLOCATOR
#define D_OPTIMIZATION(name, value) S8_INITIALIZER(name),
BUSTER_GLOBAL_LOCAL String8 const d_optimizations[] = {{0}, BUSTER_CODEGEN_OPTIMIZATIONS(D_OPTIMIZATION)};
#undef D_OPTIMIZATION

typedef struct DConfig DConfig;
struct DConfig { String8 name; u32 allocator; u32 optimization; u32 promotion; };
typedef struct DCase DCase;
struct DCase { String8 name; String8 source; String8 host; bool reject; String8 include; bool require_zero; };
typedef enum DKind { D_EXIT, D_SIGNAL, D_TIMEOUT, D_SPAWN, D_WAIT } DKind;
typedef struct DObservation DObservation;
struct DObservation
{
    DKind kind;
    u32 status;
    u32 raw_status;
    bool sanitizer;
    String8 output;
    String8 error;
};
typedef struct DResult DResult;
struct DResult { DObservation compile; DObservation link; DObservation run; bool linked; bool ran; bool verified; };
typedef struct DSettings DSettings;
struct DSettings
{
    Arena* arena;
    String8 ide;
    String8 cc;
    String8 out;
    String8 include;
    FILE* report;
    u32 timeout_seconds;
    u32 reduce_limit;
    bool verify;
    bool sanitize_oracle;
    bool io_failed;
};

BUSTER_GLOBAL_LOCAL bool d_contains(String8 text, String8 part)
{
    bool found = false;
    for (u64 index = 0; !found && index + part.length <= text.length; index += 1)
    {
        found = memcmp(text.pointer + index, part.pointer, (size_t)part.length) == 0;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool d_sanitizer(String8 text)
{
    String8 names[] = {S8("AddressSanitizer"), S8("UndefinedBehaviorSanitizer"), S8("MemorySanitizer"),
                       S8("ThreadSanitizer"), S8("LeakSanitizer"), S8("runtime error:")};
    bool found = false;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1) { found |= d_contains(text, names[index]); }
    return found;
}

BUSTER_GLOBAL_LOCAL void d_write(DSettings* settings, String8 path, String8 text)
{
    String8 path_z = string_duplicate_arena(settings->arena, path, true);
    FILE* file = fopen((char*)path_z.pointer, "wb");
    if (!file) { settings->io_failed = true; }
    else
    {
        if (text.length) { settings->io_failed |= fwrite(text.pointer, 1, (size_t)text.length, file) != text.length; }
        settings->io_failed |= fclose(file) != 0;
    }
}

BUSTER_GLOBAL_LOCAL bool d_normal(DObservation observation)
{
    return observation.kind == D_EXIT && !observation.sanitizer;
}

BUSTER_GLOBAL_LOCAL bool d_success(DObservation observation)
{
    return d_normal(observation) && observation.status == 0;
}

BUSTER_GLOBAL_LOCAL u32 d_difference(DObservation left, DObservation right)
{
    u32 mask = 0;
    if (left.kind != right.kind) { mask |= 1; }
    if (left.status != right.status) { mask |= 2; }
    if (!string_equal(left.output, right.output)) { mask |= 4; }
    if (!string_equal(left.error, right.error)) { mask |= 8; }
    if (left.sanitizer != right.sanitizer) { mask |= 16; }
    return mask;
}

// A missing native status must not look like a successful exit. The portable
// result also represents ordinary nonzero exits, so consult it only for raw 0.
BUSTER_GLOBAL_LOCAL DObservation d_wait_observation(ProcessWaitResult wait)
{
    DObservation observation = {.kind = D_WAIT, .raw_status = wait.platform_status, .status = (u32)wait.result};
    observation.output = (String8){.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
    observation.error = (String8){.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
    if (wait.timed_out) { observation.kind = D_TIMEOUT; }
    else if (wait.platform_status || wait.result == PROCESS_RESULT_SUCCESS)
    {
#if BUSTER_WINDOWS
        observation.kind = wait.platform_status >= 0xC0000000U ? D_SIGNAL : D_EXIT;
        observation.status = wait.platform_status;
#else
        int status = (int)wait.platform_status;
        if (WIFEXITED(status)) { observation.kind = D_EXIT; observation.status = (u32)WEXITSTATUS(status); }
        else if (WIFSIGNALED(status)) { observation.kind = D_SIGNAL; observation.status = (u32)WTERMSIG(status); }
#endif
    }
    observation.sanitizer = d_sanitizer(observation.output) || d_sanitizer(observation.error);
    return observation;
}

BUSTER_GLOBAL_LOCAL DObservation d_observe(DSettings* settings, SliceString8 command, String8 prefix)
{
    Arena* arena = settings->arena;
    // NUL-delimited argv is lossless, including whitespace, quotes and newlines.
    u64 size = 0;
    for (u64 index = 0; index < command.length; index += 1) { size += command.pointer[index].length + 1; }
    char8* bytes = arena_allocate(arena, char8, size);
    u64 at = 0;
    for (u64 index = 0; index < command.length; index += 1)
    {
        String8 arg = command.pointer[index];
        if (arg.length) { memcpy(bytes + at, arg.pointer, (size_t)arg.length); }
        at += arg.length;
        bytes[at++] = 0;
    }
    d_write(settings, string_format(arena, S8("{S8}.argv"), prefix), (String8){.pointer = bytes, .length = size});
    u64 start = os_now_microseconds();
    ProcessSpawnResult spawn = os_process_spawn(command, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR), .use_process_environment = 1});
    DObservation observation = {.kind = D_SPAWN};
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, (u64)settings->timeout_seconds * 1000000);
        observation = d_wait_observation(wait);
    }
    d_write(settings, string_format(arena, S8("{S8}.stdout"), prefix), observation.output);
    d_write(settings, string_format(arena, S8("{S8}.stderr"), prefix), observation.error);
    u64 elapsed = os_now_microseconds() - start;
    if (settings->report)
    {
        int written = fprintf(settings->report, "%.*s\t%u\t%u\t%u\t%u\t%llu\n", (int)prefix.length, prefix.pointer,
            (unsigned)observation.kind, observation.status, observation.raw_status, (unsigned)observation.sanitizer, (unsigned long long)elapsed);
        settings->io_failed |= written < 0 || fflush(settings->report) != 0;
    }
    return observation;
}

BUSTER_GLOBAL_LOCAL bool d_number(String8 text, u32* value)
{
    bool valid = text.length > 0;
    u32 parsed = 0;
    for (u64 index = 0; valid && index < text.length; index += 1)
    {
        u32 digit = (u32)(u8)text.pointer[index] - '0';
        if (digit > 9 || parsed > (UINT32_MAX - digit) / 10) { valid = false; }
        else { parsed = parsed * 10 + digit; }
    }
    if (valid) { *value = parsed; }
    return valid;
}

// Remove exactly one explicit telemetry line, never arbitrary diagnostics.
// Verification counts and allocator identity are a fail-closed protocol.
BUSTER_GLOBAL_LOCAL bool d_verification(DSettings* settings, DObservation* observation, DConfig config)
{
    bool valid = false;
    String8 text = observation->output;
    u64 offset = 0, remove_from = 0, remove_to = 0;
    u32 matches = 0;
    String8 expected = d_allocators[config.allocator];
    if (string_equal(expected, S8("alias-none"))) { expected = S8("none"); }
    if (string_equal(expected, S8("default"))) { expected = S8("fast"); }
    while (offset < text.length)
    {
        u64 end = offset;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        String8 line = string_slice(text, offset, end);
        if (string_starts_with_sequence(line, S8("CODEGEN_VERIFY ")))
        {
            matches += 1;
            String8 fields[] = {S8("version="), S8("ir="), S8("mir="), S8("scheduled=")};
            u32 values[4] = {0};
            u64 cursor = S8("CODEGEN_VERIFY ").length;
            valid = true;
            for (u32 field = 0; valid && field < BUSTER_ARRAY_LENGTH(fields); field += 1)
            {
                valid = string_starts_with_sequence(string_slice(line, cursor, line.length), fields[field]);
                if (valid)
                {
                    cursor += fields[field].length;
                    u64 value_end = cursor;
                    while (value_end < line.length && line.pointer[value_end] != ' ') { value_end += 1; }
                    valid = value_end < line.length && d_number(string_slice(line, cursor, value_end), values + field);
                    cursor = value_end < line.length ? value_end + 1 : value_end;
                }
            }
            String8 allocator = string_format(settings->arena, S8("allocator={S8}"), expected);
            valid &= string_equal(string_slice(line, cursor, line.length), allocator) && values[0] == 1 && values[1] > 0 &&
                     values[3] <= values[2] && (!string_equal(expected, S8("none")) || values[2] == 0);
            // Fallback-only functions may legitimately produce zero selected MIR.
            remove_from = offset;
            remove_to = end < text.length ? end + 1 : end;
        }
        offset = end < text.length ? end + 1 : end;
    }
    valid &= matches == 1;
    if (valid)
    {
        char8* normalized = arena_allocate(settings->arena, char8, text.length);
        if (remove_from) { memcpy(normalized, text.pointer, (size_t)remove_from); }
        if (text.length > remove_to) { memcpy(normalized + remove_from, text.pointer + remove_to, (size_t)(text.length - remove_to)); }
        observation->output = (String8){.pointer = normalized, .length = remove_from + text.length - remove_to};
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 d_matrix(DConfig* configs, u32 capacity)
{
    u32 count = 0;
    for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(d_allocators); allocator += 1)
    {
        for (u32 optimization = 0; optimization < BUSTER_ARRAY_LENGTH(d_optimizations); optimization += 1)
        {
            for (u32 promotion = 0; promotion < 8; promotion += 1)
            {
                if (count < capacity) { configs[count] = (DConfig){.allocator = allocator, .optimization = optimization, .promotion = promotion}; }
                count += 1;
            }
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL DResult d_execute(DSettings* settings, DCase test, DConfig config, bool host, bool optimize, String8 directory)
{
    Arena* arena = settings->arena;
    make_directory_recursive(arena, directory);
    String8 object = path_join(arena, directory, S8("subject.o"));
#if BUSTER_WINDOWS
    String8 executable = path_join(arena, directory, S8("program.exe"));
#else
    String8 executable = path_join(arena, directory, S8("program"));
#endif
    // Output existence is never allowed to turn a failed compile into a pass.
    os_file_delete(object);
    os_file_delete(executable);
    String8 argv[40];
    u64 count = 0;
    argv[count++] = host ? settings->cc : settings->ide;
    if (host)
    {
        argv[count++] = optimize ? S8("-O2") : S8("-O0");
        if (settings->sanitize_oracle)
        {
            argv[count++] = S8("-fsanitize=address,undefined");
            argv[count++] = S8("-fno-sanitize-recover=all");
        }
    }
    else
    {
        argv[count++] = S8("cc");
        if (d_optimizations[config.optimization].length) { argv[count++] = d_optimizations[config.optimization]; }
        String8 allocator = d_allocators[config.allocator];
        if (string_equal(allocator, S8("alias-none"))) { argv[count++] = S8("-fno-register-allocator"); }
        else if (!string_equal(allocator, S8("default"))) { argv[count++] = string_format(arena, S8("-fregister-allocator={S8}"), allocator); }
        argv[count++] = (config.promotion & 1) ? S8("-fcanonical-local-promotion") : S8("-fno-canonical-local-promotion");
        argv[count++] = (config.promotion & 2) ? S8("-ftarget-local-promotion") : S8("-fno-target-local-promotion");
        argv[count++] = (config.promotion & 4) ? S8("-ffrontend-ssa") : S8("-fno-frontend-ssa");
        if (settings->verify) { argv[count++] = S8("-fverify-codegen"); }
    }
    argv[count++] = S8("-fwrapv");
    argv[count++] = S8("-fno-strict-aliasing");
    argv[count++] = S8("-funsigned-char");
    if (test.include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), test.include); }
    if (settings->include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), settings->include); }
    argv[count++] = test.source;
    if (test.host.length && host) { argv[count++] = test.host; }
    bool object_only = (!host && test.host.length) || test.reject;
    if (object_only) { argv[count++] = S8("-c"); }
#if BUSTER_LINUX
    if (host && !object_only) { argv[count++] = S8("-no-pie"); }
#endif
    argv[count++] = S8("-o");
    argv[count++] = object_only ? object : executable;
    DResult result = {0};
    result.compile = d_observe(settings, (SliceString8){.pointer = argv, .length = count}, path_join(arena, directory, S8("compile")));
    result.verified = host || !settings->verify;
    if (!host && settings->verify && d_success(result.compile) && !test.reject)
    {
        result.verified = d_verification(settings, &result.compile, config);
    }
    if (!test.reject && d_success(result.compile))
    {
        bool executable_ready = path_exists(arena, executable);
        if (!host && test.host.length && path_exists(arena, object))
        {
            count = 0;
            argv[count++] = settings->cc;
            argv[count++] = S8("-O0");
            argv[count++] = S8("-fwrapv");
            argv[count++] = S8("-fno-strict-aliasing");
            argv[count++] = S8("-funsigned-char");
            if (settings->sanitize_oracle)
            {
                argv[count++] = S8("-fsanitize=address,undefined");
                argv[count++] = S8("-fno-sanitize-recover=all");
            }
            if (test.include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), test.include); }
            if (settings->include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), settings->include); }
            argv[count++] = test.host;
            argv[count++] = object;
#if BUSTER_LINUX
            argv[count++] = S8("-no-pie");
#endif
            argv[count++] = S8("-o");
            argv[count++] = executable;
            result.link = d_observe(settings, (SliceString8){.pointer = argv, .length = count}, path_join(arena, directory, S8("link")));
            result.linked = true;
            executable_ready = d_success(result.link) && path_exists(arena, executable);
        }
        if (executable_ready)
        {
            argv[0] = executable;
            result.run = d_observe(settings, (SliceString8){.pointer = argv, .length = 1}, path_join(arena, directory, S8("run")));
            result.ran = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 d_classify(DResult candidate, DResult reference, bool reject)
{
    u32 failure = 0;
    if (!d_normal(candidate.compile)) { failure = 100 + (u32)candidate.compile.kind; }
    else if (reject)
    {
        if (candidate.compile.status == 0) { failure = 110; }
    }
    else if (!d_success(candidate.compile)) { failure = 120; }
    else if (!candidate.verified) { failure = 130; }
    else if (candidate.linked && !d_success(candidate.link)) { failure = 140; }
    else if (!candidate.ran) { failure = 150; }
    else if (!d_normal(candidate.run)) { failure = 160 + (u32)candidate.run.kind; }
    else { failure = d_difference(candidate.run, reference.run); }
    return failure;
}

BUSTER_GLOBAL_LOCAL bool d_oracle_valid(DResult o0, DResult o2, DCase test)
{
    bool trusted = test.reject ?
        d_normal(o0.compile) && o0.compile.status != 0 && d_normal(o2.compile) && o2.compile.status != 0 :
        o0.ran && o2.ran && d_normal(o0.run) && d_normal(o2.run) && d_difference(o0.run, o2.run) == 0;
    if (test.require_zero && !test.reject) { trusted &= o0.run.status == 0 && o2.run.status == 0; }
    return trusted;
}

// Source reduction never rewrites fixed ABI callers. Preserve the exact runtime
// mismatch mask; reject compile/link failures, sanitizer reports and crashes.
// Host O0/O2 agreement is checked for every candidate (not just the original).
BUSTER_GLOBAL_LOCAL void d_reduce(DSettings* settings, DCase test, DConfig config, u32 signature, String8 directory)
{
    Arena* arena = settings->arena;
    bool saved_sanitizer = settings->sanitize_oracle;
    settings->sanitize_oracle = true;
    ByteSlice input = file_read(arena, test.source, (FileReadOptions){0});
    String8 best = {.pointer = (char8*)input.pointer, .length = input.length};
    u64* boundaries = arena_allocate(arena, u64, input.length + 2);
    char8* candidate_bytes = arena_allocate(arena, char8, input.length + 1);
    make_directory_recursive(arena, directory);
    String8 source = path_join(arena, directory, S8("candidate.c"));
    DCase reduced = test;
    reduced.source = source;
    u32 trials = 0;
    u64 granularity = 2;
    bool exhausted = false;
    while (!exhausted && trials < settings->reduce_limit && best.length)
    {
        u64 lines = 0;
        boundaries[lines++] = 0;
        for (u64 index = 0; index < best.length; index += 1)
        {
            if (best.pointer[index] == '\n' && index + 1 < best.length) { boundaries[lines++] = index + 1; }
        }
        boundaries[lines] = best.length;
        if (granularity > lines) { granularity = lines; }
        u64 chunk = (lines + granularity - 1) / granularity;
        bool changed = false;
        for (u64 line = 0; !changed && line < lines && trials < settings->reduce_limit; line += chunk)
        {
            u64 from = boundaries[line];
            u64 to = boundaries[BUSTER_MIN(line + chunk, lines)];
            String8 candidate = {.pointer = candidate_bytes, .length = best.length - (to - from)};
            if (from) { memcpy(candidate_bytes, best.pointer, (size_t)from); }
            if (best.length > to) { memcpy(candidate_bytes + from, best.pointer + to, (size_t)(best.length - to)); }
            d_write(settings, source, candidate);
            String8 trial = path_join(arena, directory, string_format(arena, S8("trial-{u32}"), trials++));
            DResult o0 = d_execute(settings, reduced, config, true, false, path_join(arena, trial, S8("host-o0")));
            DResult o2 = d_execute(settings, reduced, config, true, true, path_join(arena, trial, S8("host-o2")));
            if (d_oracle_valid(o0, o2, reduced))
            {
                DResult actual = d_execute(settings, reduced, config, false, false, path_join(arena, trial, S8("buster")));
                if (d_classify(actual, o0, false) == signature)
                {
                    best = string_duplicate_arena(arena, candidate, false);
                    changed = true;
                }
            }
        }
        if (changed) { granularity = granularity > 2 ? granularity - 1 : 2; }
        else if (granularity >= lines) { exhausted = true; }
        else { granularity = BUSTER_MIN(lines, granularity * 2); }
    }
    String8 final_source = path_join(arena, directory, S8("minimized.c"));
    d_write(settings, final_source, best);
    reduced.source = final_source;
    DResult o0 = d_execute(settings, reduced, config, true, false, path_join(arena, directory, S8("final-host-o0")));
    DResult o2 = d_execute(settings, reduced, config, true, true, path_join(arena, directory, S8("final-host-o2")));
    DResult actual = d_execute(settings, reduced, config, false, false, path_join(arena, directory, S8("final-buster")));
    bool confirmed = d_oracle_valid(o0, o2, reduced) && d_classify(actual, o0, false) == signature;
    d_write(settings, path_join(arena, directory, S8("reduction.txt")),
        string_format(arena, S8("version=1 original_bytes={u64} reduced_bytes={u64} trials={u32} signature={u32} confirmed={u32}\n"),
            input.length, best.length, trials, signature, (u32)confirmed));
    settings->io_failed |= !confirmed;
    settings->sanitize_oracle = saved_sanitizer;
    string_print(S8("DIFFERENTIAL_REDUCE case={S8} bytes={u64}->{u64} trials={u32} confirmed={u32}\n"), test.name, input.length, best.length, trials, (u32)confirmed);
}

BUSTER_GLOBAL_LOCAL u32 d_case_run(DSettings* settings, DCase test, DConfig* configs, u32 config_count)
{
    Arena* arena = settings->arena;
    u64 slash = 0;
    for (u64 index = 0; index < test.source.length; index += 1)
    {
        if (test.source.pointer[index] == '/' || test.source.pointer[index] == '\\') { slash = index + 1; }
    }
    test.include = slash ? string_slice(test.source, 0, slash) : S8(".");
    test.include = os_path_absolute(arena, string_duplicate_arena(arena, test.include, true), true);
    String8 directory = path_join(arena, settings->out, test.name);
    make_directory_recursive(arena, directory);
    ByteSlice input = file_read(arena, test.source, (FileReadOptions){0});
    d_write(settings, path_join(arena, directory, S8("input.c")), (String8){.pointer = (char8*)input.pointer, .length = input.length});
    if (test.host.length)
    {
        ByteSlice fixed = file_read(arena, test.host, (FileReadOptions){0});
        d_write(settings, path_join(arena, directory, S8("host.c")), (String8){.pointer = (char8*)fixed.pointer, .length = fixed.length});
    }
    DResult o0 = d_execute(settings, test, configs[0], true, false, path_join(arena, directory, S8("host-o0")));
    DResult o2 = d_execute(settings, test, configs[0], true, true, path_join(arena, directory, S8("host-o2")));
    bool trusted = d_oracle_valid(o0, o2, test);
    u32 failures = trusted ? 0 : 1;
    DObservation diagnostics = {0};
    bool reduced = false;
    if (!trusted) { string_print(S8("DIFFERENTIAL_FAIL case={S8} independent_oracle=invalid\n"), test.name); }
    for (u32 index = 0; trusted && index < config_count; index += 1)
    {
        u64 scratch = arena->position;
        DConfig config = configs[index];
        DResult actual = d_execute(settings, test, config, false, false, path_join(arena, directory, config.name));
        u32 failure = d_classify(actual, o0, test.reject);
        // Source paths are identical across the matrix; only the explicitly
        // validated CODEGEN_VERIFY line is removed from successful diagnostics.
        if (index == 0) { diagnostics = actual.compile; }
        else if (!failure && d_difference(actual.compile, diagnostics)) { failure = 170; }
        if (failure)
        {
            failures += 1;
            d_write(settings, path_join(arena, path_join(arena, directory, config.name), S8("failure.txt")),
                string_format(arena, S8("version=1 signature={u32} oracle_exit={u32} candidate_exit={u32}\n"), failure, o0.run.status, actual.run.status));
            string_print(S8("DIFFERENTIAL_FAIL case={S8} config={S8} signature={u32}\n"), test.name, config.name, failure);
            if (!reduced && !test.reject && failure < 32 && settings->reduce_limit)
            {
                d_reduce(settings, test, config, failure, path_join(arena, directory, S8("reduction")));
                reduced = true;
            }
        }
        // First observation's diagnostics survive; all later child buffers are
        // reclaimed after writing their byte-exact files and status records.
        if (index) { arena_set_position(arena, scratch); }
    }
    string_print(S8("DIFFERENTIAL_CASE name={S8} configurations={u32} failures={u32}\n"), test.name, trusted ? config_count : 0, failures);
    return failures;
}

// Explicit test children exercise the real capture/status/deadline path. They
// never compile arbitrary source and run only on the private self-test command.
BUSTER_GLOBAL_LOCAL void d_self_test_child(String8 mode)
{
    if (string_equal(mode, S8("exit")))
    {
        os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){.pointer = (u8*)"a\0b", .length = 3});
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){.pointer = (u8*)"child stderr\n", .length = 13});
        exit(7);
    }
    else if (string_equal(mode, S8("sanitizer")))
    {
        fputs("runtime error: simulated recovering sanitizer\n", stderr);
        exit(0);
    }
    else if (string_equal(mode, S8("timeout")))
    {
#if BUSTER_WINDOWS
        Sleep(3000);
#else
        struct timespec delay = {.tv_sec = 3};
        nanosleep(&delay, 0);
#endif
        exit(0);
    }
    else if (string_equal(mode, S8("crash")))
    {
#if BUSTER_WINDOWS
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        RaiseException(0xC0000409U, 0, 0, 0);
#else
        struct rlimit limit = {0};
        setrlimit(RLIMIT_CORE, &limit);
        raise(SIGABRT);
#endif
        exit(2);
    }
}

BUSTER_GLOBAL_LOCAL bool d_create_output(Arena* arena, String8 path)
{
    // Claim the final directory atomically. A pre-existing directory (including
    // one another runner just claimed) is not writable evidence for this run.
    make_directory_recursive(arena, path_parent(arena, path));
    String8 terminated = string_duplicate_arena(arena, path, true);
    bool created;
#if BUSTER_WINDOWS
    String16 wide = string16_from_string8(arena, terminated, true);
    created = CreateDirectoryW(wide.pointer, 0) != 0;
#else
    created = mkdir((char*)terminated.pointer, 0700) == 0;
#endif
    return created;
}

BUSTER_GLOBAL_LOCAL u32 d_self_test(Arena* arena)
{
    DObservation normal = {.kind = D_EXIT};
    DObservation other = normal;
    u32 errors = d_difference(normal, other) != 0;
    other.status = 7; errors += d_difference(normal, other) != 2;
    other = normal; other.kind = D_TIMEOUT; errors += d_difference(normal, other) != 1;
    other.kind = D_SIGNAL; other.status = 11; errors += d_normal(other);
    other = normal; other.sanitizer = true; errors += d_normal(other);
    errors += !d_sanitizer(S8("x.c:42: runtime error: bad value"));
    errors += d_sanitizer(S8("ordinary warning"));
    normal.output = S8("a\0b"); other = normal; other.output = S8("a\0c");
    errors += d_difference(normal, other) != 4;
    u32 number = 1;
    errors += !d_number(S8("4294967295"), &number) || number != UINT32_MAX;
    errors += d_number(S8("4294967296"), &number);
    errors += d_number(S8("-1"), &number);
    errors += d_number(S8(""), &number);
    DResult result = {.compile = {.kind = D_EXIT}, .ran = true, .verified = true, .run = {.kind = D_SIGNAL, .status = 11}};
    errors += d_classify(result, result, false) == 0; // Two equal crashes still fail.
    result.run.kind = D_EXIT; result.run.status = 0; result.run.sanitizer = true;
    errors += d_classify(result, result, false) == 0; // Recovering sanitizers too.
    result.run.sanitizer = false; result.compile.status = 1;
    errors += d_classify(result, result, false) == 0; // Shared rejection is not a pass.
    result.compile.status = 0; result.run.status = 37;
    errors += d_classify(result, result, false) != 0; // An agreed nonzero program exit is valid.
    errors += d_oracle_valid(result, result, (DCase){.require_zero = true});
    errors += !d_oracle_valid(result, result, (DCase){0});
    result.ran = false;
    errors += d_classify(result, result, false) != 150; // No artifact/run cannot pass.
    result.linked = true; result.link.status = 1;
    errors += d_classify(result, result, false) != 140;
    result.linked = false; result.verified = false;
    errors += d_classify(result, result, false) != 130;
    result.compile.kind = D_SPAWN;
    errors += d_classify(result, result, true) != 103;
    DObservation wait_failure = d_wait_observation((ProcessWaitResult){.result = PROCESS_RESULT_FAILED});
    errors += wait_failure.kind != D_WAIT || d_normal(wait_failure);
    DObservation wait_success = d_wait_observation((ProcessWaitResult){.result = PROCESS_RESULT_SUCCESS});
    errors += !d_success(wait_success);
    DConfig matrix[512];
    u32 count = d_matrix(matrix, BUSTER_ARRAY_LENGTH(matrix));
    errors += count != BUSTER_ARRAY_LENGTH(d_allocators) * BUSTER_ARRAY_LENGTH(d_optimizations) * 8;
    errors += count > BUSTER_ARRAY_LENGTH(matrix);
    for (u32 left = 0; left < BUSTER_MIN(count, BUSTER_ARRAY_LENGTH(matrix)); left += 1)
    {
        for (u32 right = left + 1; right < BUSTER_MIN(count, BUSTER_ARRAY_LENGTH(matrix)); right += 1)
        {
            errors += matrix[left].allocator == matrix[right].allocator && matrix[left].optimization == matrix[right].optimization &&
                      matrix[left].promotion == matrix[right].promotion;
        }
    }
    DSettings settings = {.arena = arena, .timeout_seconds = 1};
    String8 directory = string_format_z(arena, S8("build/differential-self-test-{u64}"), os_now_microseconds());
    if (!d_create_output(arena, directory)) { errors += 1; }
    else
    {
        String8 modes[] = {S8("exit"), S8("sanitizer"), S8("timeout"), S8("crash")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"), S8("--self-test-child"), modes[index]};
            DObservation child = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv), path_join(arena, directory, modes[index]));
            if (index == 0) { errors += child.kind != D_EXIT || child.status != 7 || !string_equal(child.output, S8("a\0b")) || !string_equal(child.error, S8("child stderr\n")); }
            if (index == 1) { errors += child.kind != D_EXIT || child.status != 0 || !child.sanitizer || d_success(child); }
            if (index == 2) { errors += child.kind != D_TIMEOUT; }
            if (index == 3) { errors += child.kind != D_SIGNAL; }
        }
        DConfig config = {.allocator = 0};
        DObservation telemetry = {.output = S8("warning\nCODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=none\n")};
        errors += !d_verification(&settings, &telemetry, config) || !string_equal(telemetry.output, S8("warning\n"));
        telemetry.output = S8("CODEGEN_VERIFY version=1 ir=0 mir=0 scheduled=0 allocator=none\n");
        errors += d_verification(&settings, &telemetry, config);
        telemetry.output = S8("CODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=fast\n");
        errors += d_verification(&settings, &telemetry, config);
        telemetry.output = S8("CODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=none\nCODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=none\n");
        errors += d_verification(&settings, &telemetry, config);
        String8 invalid_markers[] = {
            S8("CODEGEN_VERIFY version=1 ir=-1 mir=0 scheduled=0 allocator=none\n"),
            S8("CODEGEN_VERIFY version=1 ir=4294967297 mir=0 scheduled=0 allocator=none\n"),
            S8("CODEGEN_VERIFY version=1 ir=+1 mir=0 scheduled=0 allocator=none\n"),
            S8("CODEGEN_VERIFY version=1 ir=1 mir=1 scheduled=0 allocator=none\n"),
            S8("CODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=1 allocator=none\n"),
            S8("CODEGEN_VERIFY version=2 ir=1 mir=0 scheduled=0 allocator=none\n"),
            S8("CODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=none trailing\n"),
            S8("CODEGEN_VERIFY version=1 ir=1 mir=0 allocator=none\n"),
            S8("ordinary warning\n"),
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_markers); index += 1)
        {
            telemetry.output = invalid_markers[index];
            errors += d_verification(&settings, &telemetry, config);
            errors += !string_equal(telemetry.output, invalid_markers[index]);
        }
        config.allocator = 3; // QUALITY may move selected functions but cannot add functions.
        telemetry.output = S8("CODEGEN_VERIFY version=1 ir=1 mir=1 scheduled=2 allocator=quality\n");
        errors += d_verification(&settings, &telemetry, config);
        telemetry.output = S8("CODEGEN_VERIFY version=1 ir=1 mir=2 scheduled=1 allocator=quality\n");
        errors += !d_verification(&settings, &telemetry, config) || telemetry.output.length != 0;
        errors += d_create_output(arena, directory); // Never reuse existing output.
    }
    errors += settings.io_failed;
    string_print(S8("DIFFERENTIAL_SELF_TEST failures={u32} configurations={u32}\n"), errors, count);
    return errors;
}

BUSTER_GLOBAL_LOCAL ProcessResult differential_main(Arena* arena, SliceString8 arguments)
{
    DSettings settings = {.arena = arena, .ide = S8("build/Release/ide"), .cc = S8("clang"),
        .out = S8("build/differential"), .timeout_seconds = 10, .reduce_limit = 64, .verify = true};
    DCase custom = {.name = S8("custom")};
    bool list = false, self_test = false, valid = true;
    String8 child_mode = {0};
    u32 generated = 4, seed = 1;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        String8 arg = arguments.pointer[index];
        if (string_equal(arg, S8("--no-verify"))) { settings.verify = false; }
        else if (string_equal(arg, S8("--sanitize-oracle"))) { settings.sanitize_oracle = true; }
        else if (string_equal(arg, S8("--list-configurations"))) { list = true; }
        else if (string_equal(arg, S8("--self-test"))) { self_test = true; }
        else if (string_equal(arg, S8("--reject"))) { custom.reject = true; }
        else if (index + 1 == arguments.length) { valid = false; }
        else
        {
            String8 value = arguments.pointer[++index];
            if (string_equal(arg, S8("--self-test-child"))) { child_mode = value; }
            else if (string_equal(arg, S8("--ide"))) { settings.ide = value; }
            else if (string_equal(arg, S8("--cc"))) { settings.cc = value; }
            else if (string_equal(arg, S8("--out"))) { settings.out = value; }
            else if (string_equal(arg, S8("--source"))) { custom.source = value; }
            else if (string_equal(arg, S8("--host"))) { custom.host = value; }
            else if (string_equal(arg, S8("--include"))) { settings.include = value; }
            else if (string_equal(arg, S8("--generated"))) { valid &= d_number(value, &generated) && generated <= 10000; }
            else if (string_equal(arg, S8("--seed"))) { valid &= d_number(value, &seed); }
            else if (string_equal(arg, S8("--minimize"))) { valid &= d_number(value, &settings.reduce_limit) && settings.reduce_limit <= 10000; }
            else if (string_equal(arg, S8("--timeout"))) { valid &= d_number(value, &settings.timeout_seconds) && settings.timeout_seconds > 0 && settings.timeout_seconds <= 3600; }
            else { valid = false; }
        }
    }
    if (child_mode.length) { d_self_test_child(child_mode); valid = false; }
    valid &= !(custom.host.length && custom.reject) && (!(custom.host.length || custom.reject) || custom.source.length);
    u32 count = d_matrix(0, 0);
    DConfig* configs = arena_allocate(arena, DConfig, count);
    d_matrix(configs, count);
    u32 failures = 0;
    if (!valid)
    {
        string_print(S8("usage: test_differential [--ide path] [--cc clang-or-gcc] [--out new-directory] [--source C-file [--host fixed-C-file] [--reject]] [--include dir] [--generated N] [--seed N] [--minimize N] [--timeout seconds] [--sanitize-oracle] [--no-verify] [--list-configurations] [--self-test]\n"));
        failures = 1;
    }
    else if (self_test) { failures = d_self_test(arena); }
    else
    {
        for (u32 index = 0; index < count; index += 1)
        {
            DConfig* config = configs + index;
            String8 optimization = config->optimization ? d_optimizations[config->optimization] : S8("default");
            config->name = string_format(arena, S8("{S8}_{S8}_p{u32}"), d_allocators[config->allocator], optimization, config->promotion);
            if (list) { string_print(S8("{S8}\n"), config->name); }
        }
        if (!list)
        {
            settings.ide = os_path_absolute(arena, settings.ide, true);
            settings.cc = executable_resolve_in_path(arena, settings.cc);
            if (!path_exists(arena, settings.ide) || !settings.cc.length || !d_create_output(arena, settings.out))
            {
                string_print(S8("error: compiler missing or output directory already exists (refusing stale evidence)\n"));
                failures = 1;
            }
            else
            {
                settings.out = os_path_absolute(arena, settings.out, true);
                String8 report = string_format_z(arena, S8("{S8}/processes.tsv"), settings.out);
                settings.report = fopen((char*)report.pointer, "wb");
                if (!settings.report) { failures = 1; }
                else
                {
                    fprintf(settings.report, "prefix\tkind_0exit_1signal_2timeout_3spawn_4wait\tstatus\traw_status\tsanitizer\telapsed_us\n");
                    String8 manifest = string_format(arena, S8("version=1\nide={S8}\ncc={S8}\nconfigurations={u32}\nseed={u32}\ngenerated={u32}\nverify={u32}\nsanitize_oracle={u32}\n"),
                        settings.ide, settings.cc, count, seed, custom.source.length ? 0 : generated, (u32)settings.verify, (u32)settings.sanitize_oracle);
                    u64 ide_hash = 0, ide_size = 0, cc_hash = 0, cc_size = 0;
                    settings.io_failed |= !build_artifact_fanout_hash_file(arena, settings.ide, &ide_hash, &ide_size);
                    settings.io_failed |= !build_artifact_fanout_hash_file(arena, settings.cc, &cc_hash, &cc_size);
                    manifest = string_format(arena, S8("{S8}hash_algorithm=buster_hash_64\nide_hash={u64} ide_bytes={u64}\ncc_hash={u64} cc_bytes={u64}\n"),
                        manifest, ide_hash, ide_size, cc_hash, cc_size);
                    d_write(&settings, path_join(arena, settings.out, S8("manifest.txt")), manifest);
                    String8* config_names = arena_allocate(arena, String8, count * 2);
                    for (u32 index = 0; index < count; index += 1)
                    {
                        config_names[index * 2] = configs[index].name;
                        config_names[index * 2 + 1] = S8("\n");
                    }
                    d_write(&settings, path_join(arena, settings.out, S8("configurations.txt")),
                        string_join_arena(arena, (SliceString8){.pointer = config_names, .length = count * 2}, false));
                    if (custom.source.length)
                    {
                        custom.source = os_path_absolute(arena, custom.source, true);
                        if (custom.host.length) { custom.host = os_path_absolute(arena, custom.host, true); }
                        failures += d_case_run(&settings, custom, configs, count);
                    }
                    else
                    {
                        DCase tests[] = {
                            {S8("warning"), S8("tests/differential/warning.c"), {0}, false},
                            {S8("observables"), S8("tests/differential/observables.c"), {0}, false},
                            {S8("qualified-aggregate"), S8("tests/differential/qualified_aggregate.c"), S8("tests/differential/qualified_aggregate_host.c"), false, {0}, true},
                            {S8("abi"), S8("tests/differential/abi.c"), S8("tests/differential/abi_host.c"), false, {0}, true},
                            {S8("aligned-parameters"), S8("tests/differential/aligned.c"), S8("tests/differential/aligned_host.c"), false, {0}, true},
                            {S8("unsigned-switch"), S8("tests/differential/switch_unsigned.c"), S8("tests/differential/switch_unsigned_host.c"), false, {0}, true},
                            {S8("clear-cache"), S8("tests/differential/clear_cache.c"), S8("tests/differential/clear_cache_host.c"), false, {0}, true},
                            {S8("cpu-queries"), S8("tests/differential/cpu_queries.c"), S8("tests/differential/cpu_queries_host.c"), false, {0}, true},
                            {S8("native-variadic"), S8("tests/differential/native_variadic.c"), S8("tests/differential/native_variadic_host.c"), false, {0}, true},
                            {S8("reject-type"), S8("tests/differential/reject_type.c"), {0}, true},
                            {S8("reject-syntax"), S8("tests/differential/reject_syntax.c"), {0}, true},
                        };
                        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(tests); index += 1)
                        {
                            u64 scratch = arena->position;
                            failures += d_case_run(&settings, tests[index], configs, count);
                            arena_set_position(arena, scratch);
                        }
                        for (u32 index = 0; index < generated; index += 1)
                        {
                            u64 scratch = arena->position;
                            seed = seed * 1664525U + 1013904223U;
                            String8 name = string_format(arena, S8("seed-{u32}"), seed);
                            String8 source = path_join(arena, settings.out, string_format(arena, S8("{S8}.c"), name));
                            // Defined unsigned wrap and masked shifts; bounded loops. Keep
                            // generation deterministic and preserve the exact source artifact.
                            String8 prefix = S8("extern int printf(const char *, ...);\nunsigned mix(unsigned x, unsigned n) {\n unsigned a=x, b=x^1234567u;\n for(unsigned i=0;i<n;i++){ unsigned t=a; a=(b+(i*17u))^(a>>3); b=(t<<5)|(t>>27); if(a&1u) b^=a; else a+=b; }\n return a^b;\n}\nint main(void) { unsigned x=mix(");
                            String8 suffix = S8("u,31u); printf(\"%u\\n\",x); return (int)(x&127u); }\n");
                            d_write(&settings, source, string_format(arena, S8("{S8}{u32}{S8}"), prefix, seed, suffix));
                            failures += d_case_run(&settings, (DCase){.name = name, .source = source}, configs, count);
                            arena_set_position(arena, scratch);
                        }
                    }
                    settings.io_failed |= fclose(settings.report) != 0;
                    d_write(&settings, path_join(arena, settings.out, S8("summary.txt")),
                        string_format(arena, S8("version=1 failures={u32} io_failed={u32} configurations={u32}\n"), failures, (u32)settings.io_failed, count));
                }
            }
        }
    }
    failures += settings.io_failed;
    string_print(S8("DIFFERENTIAL_SUMMARY failures={u32} configurations={u32}\n"), failures, count);
    return failures ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
