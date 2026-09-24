// Native differential runner, included by build.c. No shell, Python or testing
// framework is required. d_matrix owns option discovery, d_observe owns bounded
// children/evidence, d_case_run compares observations, and d_reduce performs a
// bounded iterative line reduction against independent host-compiler oracles.
// d_case_lane dynamically admits complete cases into the persistent lane gang;
// d_cases_run validates and publishes case-owned evidence in registry order.
// d_workers_self_test checks live admission, unequal cases and arena cleanup.
// d_reference_* owns explicit reference dialect capabilities and argv policy.
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
struct DCase { String8 name; String8 source; String8 host; bool reject; String8 include; bool require_zero; bool strict_mir; bool expect_sanitizer; };
BUSTER_GLOBAL_LOCAL DCase const d_builtin_cases[] = {
    {S8("warning"), S8("tests/differential/warning.c"), {0}, false},
    {S8("observables"), S8("tests/differential/observables.c"), {0}, false},
    {S8("qualified-aggregate"), S8("tests/differential/qualified_aggregate.c"), S8("tests/differential/qualified_aggregate_host.c"), false, {0}, true},
    {S8("abi"), S8("tests/differential/abi.c"), S8("tests/differential/abi_host.c"), false, {0}, true},
    {S8("aligned-parameters"), S8("tests/differential/aligned.c"), S8("tests/differential/aligned_host.c"), false, {0}, true},
    {S8("unsigned-switch"), S8("tests/differential/switch_unsigned.c"), S8("tests/differential/switch_unsigned_host.c"), false, {0}, true},
    {S8("clear-cache"), S8("tests/differential/clear_cache.c"), S8("tests/differential/clear_cache_host.c"), false, {0}, true},
    {S8("cpu-queries"), S8("tests/differential/cpu_queries.c"), S8("tests/differential/cpu_queries_host.c"), false, {0}, true},
    {S8("native-variadic"), S8("tests/differential/native_variadic.c"), S8("tests/differential/native_variadic_host.c"), false, {0}, true},
    {S8("va-list-places"), S8("tests/basic_c_va_list_places.c"), {0}, false, {0}, true},
    {S8("native-aggregate"), S8("tests/differential/native_aggregate.c"), S8("tests/differential/native_aggregate_host.c"), false, {0}, true},
#if BUSTER_CPU_ARCH_X86_64 && (BUSTER_LINUX || BUSTER_MACOS) && !BUSTER_ANDROID && !BUSTER_IOS
    {S8("sysv-va-list"), S8("tests/differential/sysv_va_list.c"), S8("tests/differential/sysv_va_list_host.c"), false, {0}, true, true},
    {S8("sysv-sseup"), S8("tests/basic_c_sysv_sseup.c"), S8("tests/host_sysv_sseup.c"), false, {0}, true, true},
#endif
    {S8("reject-type"), S8("tests/differential/reject_type.c"), {0}, true},
    {S8("reject-syntax"), S8("tests/differential/reject_syntax.c"), {0}, true},
};
typedef enum DKind { D_EXIT, D_SIGNAL, D_TIMEOUT, D_SPAWN, D_WAIT } DKind;
typedef struct DObservation DObservation;
struct DObservation
{
    DKind kind;
    u32 status;
    u32 raw_status;
    bool sanitizer;
    String8 sanitizer_report;
    String8 output;
    String8 error;
};
typedef struct DResult DResult;
struct DResult { DObservation compile; DObservation link; DObservation run; bool linked; bool ran; bool verified; };
typedef enum DReferenceDialect { D_REFERENCE_GNU, D_REFERENCE_MSVC } DReferenceDialect;
typedef struct DSettings DSettings;
struct DSettings
{
    Arena* arena;
    String8 ide;
    String8 cc;
    String8 cc_version;
    String8 cc_target;
    DReferenceDialect reference_dialect;
    String8 out;
    String8 include;
    SliceString8 library_paths;
    FILE* report;
    FILE* log;
    FILE* evidence;
    String8 evidence_directory;
    u64* evidence_path_hashes;
    u32 evidence_path_capacity;
    u32 evidence_count;
    OsMutexHandle* spawn_mutex;
    SliceString8 environment_keys;
    SliceString8 environment_values;
    u32 rows;
    u32 timeout_seconds;
    u32 reduce_limit;
    bool verify;
    bool sanitize_oracle;
    bool strict_mir;
    bool explicit_environment;
    bool io_failed;
    bool self_test_cancel_before_reap;
};

typedef ProcessControlAtomic DCancellationAtomic;
#if !BUSTER_SINGLE_THREADED && !BUSTER_COMPILER_MSVC
BUSTER_CT_CHECK(__atomic_always_lock_free(sizeof(DCancellationAtomic), 0));
#endif
BUSTER_GLOBAL_LOCAL DCancellationAtomic d_cancellation_signal;
BUSTER_GLOBAL_LOCAL DCancellationAtomic d_cancellation_escalated;
BUSTER_GLOBAL_LOCAL DCancellationAtomic d_spawn_admission_in_flight;

BUSTER_GLOBAL_LOCAL u64 d_atomic_load(DCancellationAtomic* value)
{
    u64 result = process_control_atomic_load(value);
    return result;
}

BUSTER_GLOBAL_LOCAL void d_atomic_store(DCancellationAtomic* value, u64 stored)
{
    process_control_atomic_store(value, stored);
}

BUSTER_GLOBAL_LOCAL bool d_atomic_set_if_zero(DCancellationAtomic* value, u64 stored)
{
    bool result = process_control_atomic_set_if_zero(value, stored);
    return result;
}

typedef struct DCancellationHandlers DCancellationHandlers;
struct DCancellationHandlers
{
#if BUSTER_LINUX || BUSTER_MACOS
    struct sigaction previous_interrupt;
    struct sigaction previous_terminate;
    struct sigaction previous_alarm;
#endif
    bool installed;
};

#if BUSTER_LINUX || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void d_cancellation_handler(int signal_number)
{
    if (signal_number == SIGALRM)
    {
        d_atomic_store(&d_cancellation_escalated, 1);
    }
    else
    {
        bool first_signal = d_atomic_set_if_zero(&d_cancellation_signal, (u64)signal_number);
        // Cooperative children get a bounded cleanup interval. SIGALRM only
        // publishes escalation; ordinary wait lanes own every group syscall.
        if (first_signal) { alarm(2); }
    }
}
#endif

BUSTER_GLOBAL_LOCAL bool d_cancellation_begin(DCancellationHandlers* handlers)
{
    bool result = true;
    d_atomic_store(&d_cancellation_signal, 0);
    d_atomic_store(&d_cancellation_escalated, 0);
    d_atomic_store(&d_spawn_admission_in_flight, 0);
#if BUSTER_LINUX || BUSTER_MACOS
    struct sigaction action = {0};
    action.sa_handler = d_cancellation_handler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGALRM);
    result = sigaction(SIGALRM, &action, &handlers->previous_alarm) == 0;
    if (result)
    {
        result = sigaction(SIGTERM, &action, &handlers->previous_terminate) == 0;
        if (result)
        {
            result = sigaction(SIGINT, &action, &handlers->previous_interrupt) == 0;
            if (!result) { sigaction(SIGTERM, &handlers->previous_terminate, 0); }
        }
        if (!result) { sigaction(SIGALRM, &handlers->previous_alarm, 0); }
    }
#endif
    handlers->installed = result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool d_spawn_admission_begin(void)
{
    // The caller holds spawn_mutex, so one slot describes the complete
    // check/spawn/control-publication transaction. A concurrent signal either
    // closes admission first or observes an already admitted transaction whose
    // child will be published to the ordinary wait lane before this clears.
    d_atomic_store(&d_spawn_admission_in_flight, 1);
    bool result = !d_atomic_load(&d_cancellation_signal);
    return result;
}

BUSTER_GLOBAL_LOCAL void d_spawn_admission_end(void)
{
    d_atomic_store(&d_spawn_admission_in_flight, 0);
}

#if BUSTER_LINUX || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL void d_cancel_admission(DSettings* settings)
{
    // Serialize the stop flag with the admission check. The ordinary wait
    // lane, never this helper or an async handler, owns group operations.
    if (settings->spawn_mutex) { os_mutex_lock(settings->spawn_mutex); }
    d_atomic_set_if_zero(&d_cancellation_signal, SIGTERM);
    d_atomic_store(&d_cancellation_escalated, 1);
    if (settings->spawn_mutex) { os_mutex_unlock(settings->spawn_mutex); }
}

BUSTER_GLOBAL_LOCAL bool d_process_group_control_self_test(void)
{
    DSettings settings = {0};
    bool result = !d_atomic_load(&d_cancellation_signal) && !d_atomic_load(&d_cancellation_escalated);
    result = d_spawn_admission_begin() && d_atomic_load(&d_spawn_admission_in_flight) == 1 && result;
    d_cancellation_handler(SIGTERM);
    d_cancellation_handler(SIGINT);
    d_spawn_admission_end();
    result = d_atomic_load(&d_cancellation_signal) == SIGTERM && !d_atomic_load(&d_cancellation_escalated) &&
        !d_atomic_load(&d_spawn_admission_in_flight) && result;
    result = !d_spawn_admission_begin() && result;
    d_spawn_admission_end();
    alarm(0);
    d_cancellation_handler(SIGALRM);
    result = d_atomic_load(&d_cancellation_escalated) == 1 && result;
    d_atomic_store(&d_cancellation_signal, 0);
    d_atomic_store(&d_cancellation_escalated, 0);
    d_cancel_admission(&settings);
    result = d_atomic_load(&d_cancellation_signal) == SIGTERM && d_atomic_load(&d_cancellation_escalated) == 1 && result;
    d_atomic_store(&d_cancellation_signal, 0);
    d_atomic_store(&d_cancellation_escalated, 0);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL u32 d_cancellation_end(DCancellationHandlers* handlers)
{
#if BUSTER_LINUX || BUSTER_MACOS
    if (handlers->installed)
    {
        alarm(0);
        sigaction(SIGINT, &handlers->previous_interrupt, 0);
        sigaction(SIGTERM, &handlers->previous_terminate, 0);
        sigaction(SIGALRM, &handlers->previous_alarm, 0);
    }
#else
    BUSTER_UNUSED(handlers);
#endif
    u32 result = (u32)d_atomic_load(&d_cancellation_signal);
    return result;
}

BUSTER_GLOBAL_LOCAL bool d_mir_config(DConfig config)
{
    String8 allocator = d_allocators[config.allocator];
    return !string_equal(allocator, S8("none")) && !string_equal(allocator, S8("alias-none"));
}

BUSTER_GLOBAL_LOCAL bool d_contains(String8 text, String8 part)
{
    bool found = false;
    for (u64 index = 0; !found && index + part.length <= text.length; index += 1)
    {
        found = memcmp(text.pointer + index, part.pointer, (size_t)part.length) == 0;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL String8 d_reference_compiler(Arena* arena, String8 file)
{
    bool explicit_path = d_contains(file, S8("/")) || d_contains(file, S8("\\"));
    String8 result = explicit_path ? os_path_absolute(arena, file, true) : executable_resolve_in_path(arena, file);
    return result;
}

BUSTER_GLOBAL_LOCAL bool d_reference_capabilities(DSettings* settings, bool native_windows, bool custom_source)
{
    bool valid = settings->reference_dialect == D_REFERENCE_GNU ||
        (native_windows && custom_source && !settings->sanitize_oracle && settings->reduce_limit == 0);
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 const d_sanitizer_environment_names[] = {
    S8_INITIALIZER("ASAN_OPTIONS"), S8_INITIALIZER("UBSAN_OPTIONS"), S8_INITIALIZER("LSAN_OPTIONS"),
    S8_INITIALIZER("MSAN_OPTIONS"), S8_INITIALIZER("TSAN_OPTIONS")};

BUSTER_GLOBAL_LOCAL String8 d_sanitizer_name_text(void)
{
    return S8("AddressSanitizer UndefinedBehaviorSanitizer MemorySanitizer ThreadSanitizer LeakSanitizer runtime error:\n");
}

BUSTER_GLOBAL_LOCAL bool d_environment_key_equal(String8 left, String8 right)
{
    bool equal = left.length == right.length;
    for (u64 index = 0; equal && index < left.length; index += 1)
    {
        char8 left_character = left.pointer[index];
        char8 right_character = right.pointer[index];
#if BUSTER_WINDOWS
        if (left_character >= 'A' && left_character <= 'Z') { left_character += 'a' - 'A'; }
        if (right_character >= 'A' && right_character <= 'Z') { right_character += 'a' - 'A'; }
#endif
        equal = left_character == right_character;
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL u32 d_sanitizer_environment_index(String8 key)
{
    u32 result = BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names); index += 1)
    {
        if (d_environment_key_equal(key, d_sanitizer_environment_names[index]))
        {
            result = index;
            break;
        }
    }
    return result;
}

// compiler-rt's common flag parser accepts quoted values. Quote every
// absolute path so spaces and drive colons remain one flag value; percent
// characters are literal in log_path and must remain byte-for-byte unchanged.
BUSTER_GLOBAL_LOCAL String8 d_sanitizer_log_path(DSettings* settings, String8 path, String8* absolute, bool* valid)
{
    String8 terminated = string_duplicate_arena(settings->arena, path, true);
    *absolute = os_path_absolute_lexical(settings->arena, terminated, true);
    bool single_quote = false, double_quote = false;
    for (u64 index = 0; index < absolute->length; index += 1)
    {
        single_quote |= absolute->pointer[index] == '\'';
        double_quote |= absolute->pointer[index] == '"';
    }
    *valid = absolute->length && !(single_quote && double_quote) && absolute->length <= UINT64_MAX - 2;
    String8 result = {0};
    if (*valid)
    {
        char8 quote = single_quote ? '"' : '\'';
        char8* quoted = arena_allocate(settings->arena, char8, absolute->length + 2);
        u64 length = 0;
        quoted[length++] = quote;
        for (u64 index = 0; index < absolute->length; index += 1)
        {
            char8 character = absolute->pointer[index];
            quoted[length++] = character;
        }
        quoted[length++] = quote;
        result = (String8){.pointer = quoted, .length = length};
    }
    return result;
}

// Preserve the caller's complete environment and sanitizer policy, replacing
// only duplicate sanitizer option keys and appending a final, authoritative
// report destination. stdout and stderr remain ordinary observed streams.
BUSTER_GLOBAL_LOCAL bool d_sanitizer_environment(DSettings* settings, String8 requested_report_base,
                                                  String8* report_base, SliceString8* keys, SliceString8* values)
{
    SliceString8 source_keys = settings->explicit_environment ? settings->environment_keys : program_state->input.environment_keys;
    SliceString8 source_values = settings->explicit_environment ? settings->environment_values : program_state->input.environment_values;
    bool valid = source_keys.length == source_values.length;
    String8 quoted_path = valid ? d_sanitizer_log_path(settings, requested_report_base, report_base, &valid) : (String8){0};
    String8 directive = valid ? string_format(settings->arena,
        S8("log_path={S8}:log_exe_name=0:log_suffix='':log_to_syslog=0:log_fallback_to_stderr=0"), quoted_path) : (String8){0};
    u64 capacity = source_keys.length + BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names);
    String8* output_keys = valid ? arena_allocate(settings->arena, String8, capacity) : 0;
    String8* output_values = valid ? arena_allocate(settings->arena, String8, capacity) : 0;
    bool seen[BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names)] = {0};
    u64 count = 0;
    for (u64 index = 0; valid && index < source_keys.length; index += 1)
    {
        String8 key = source_keys.pointer[index];
        String8 value = source_values.pointer[index];
        u32 sanitizer = d_sanitizer_environment_index(key);
        if (sanitizer < BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names))
        {
            if (!seen[sanitizer])
            {
                String8 separator = value.length ? S8(":") : (String8){0};
                output_keys[count] = d_sanitizer_environment_names[sanitizer];
                output_values[count] = string_format(settings->arena, S8("{S8}{S8}{S8}"), value, separator, directive);
                seen[sanitizer] = true;
                count += 1;
            }
        }
        else
        {
            output_keys[count] = key;
            output_values[count] = value;
            count += 1;
        }
    }
    for (u32 index = 0; valid && index < BUSTER_ARRAY_LENGTH(d_sanitizer_environment_names); index += 1)
    {
        if (!seen[index])
        {
            output_keys[count] = d_sanitizer_environment_names[index];
            output_values[count] = directive;
            count += 1;
        }
    }
    if (valid)
    {
        *keys = (SliceString8){.pointer = output_keys, .length = count};
        *values = (SliceString8){.pointer = output_values, .length = count};
    }
    else
    {
        *keys = (SliceString8){0};
        *values = (SliceString8){0};
        *report_base = (String8){0};
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u64 d_process_identifier(ProcessSpawnResult spawn)
{
    u64 result = 0;
#if BUSTER_WINDOWS
    if (spawn.handle) { result = (u64)GetProcessId((HANDLE)spawn.handle); }
#else
    result = (u64)(size_t)spawn.handle;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL FILE* d_file_open(char const* path, bool write, bool update)
{
    // UCRT's N mode creates a non-inheritable descriptor atomically. A child
    // retaining another lane's evidence writer would prevent its read-only
    // hash/replay open on Windows even after the owning lane closed the file.
#if BUSTER_WINDOWS
    char const* mode = update ? "w+bN" : write ? "wbN" : "rbN";
#else
    char const* mode = update ? "w+b" : write ? "wb" : "rb";
#endif
    return fopen(path, mode);
}

BUSTER_GLOBAL_LOCAL void d_write(DSettings* settings, String8 path, String8 text)
{
    String8 path_z = string_duplicate_arena(settings->arena, path, true);
    FILE* file = d_file_open((char*)path_z.pointer, true, false);
    if (!file) { settings->io_failed = true; }
    else
    {
        if (text.length) { settings->io_failed |= fwrite(text.pointer, 1, (size_t)text.length, file) != text.length; }
        settings->io_failed |= fclose(file) != 0;
    }
}

BUSTER_GLOBAL_LOCAL String8 d_evidence_relative_path(String8 directory, String8 path, bool* valid)
{
    *valid = path.length > directory.length + 1 &&
        memcmp(path.pointer, directory.pointer, (size_t)directory.length) == 0 &&
        path_is_separator(path.pointer[directory.length]);
    String8 result = *valid ? string_slice(path, directory.length + 1, path.length) : (String8){0};
    u64 component = 0;
    for (u64 index = 0; *valid && index <= result.length; index += 1)
    {
        if (index == result.length || path_is_separator(result.pointer[index]))
        {
            u64 length = index - component;
            *valid = length > 0 && !(length == 1 && result.pointer[component] == '.') &&
                !(length == 2 && result.pointer[component] == '.' && result.pointer[component + 1] == '.');
            component = index + 1;
        }
        else if (result.pointer[index] == '\t' || result.pointer[index] == '\n' || result.pointer[index] == '\r')
        {
            *valid = false;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool d_evidence_path_insert(u64* slots, u32 capacity, String8 path)
{
    u64 hash = buster_hash_64(path.pointer ? (u8*)path.pointer : (u8*)"", path.length);
    bool result = slots && capacity && !(capacity & (capacity - 1));
    u32 index = result ? (u32)hash & (capacity - 1) : 0;
    u32 probes = 0;
    while (result && slots[index] && slots[index] != hash && probes < capacity)
    {
        index = (index + 1) & (capacity - 1);
        probes += 1;
    }
    result = result && probes < capacity && !slots[index];
    if (result) { slots[index] = hash; }
    return result;
}

BUSTER_GLOBAL_LOCAL void d_write_evidence(DSettings* settings, String8 path, String8 text)
{
    if (!settings->evidence) { d_write(settings, path, text); }
    else
    {
        bool valid = false;
        String8 relative = d_evidence_relative_path(settings->evidence_directory, path, &valid);
        valid = valid && d_evidence_path_insert(settings->evidence_path_hashes, settings->evidence_path_capacity, relative);
        if (!valid) { settings->io_failed = true; }
        else
        {
            d_write(settings, path, text);
            if (!settings->io_failed)
            {
                u64 hash = buster_hash_64(text.pointer ? (u8*)text.pointer : (u8*)"", text.length);
                int written = fprintf(settings->evidence, "%llu\t%llu\t%.*s\n", (unsigned long long)hash,
                    (unsigned long long)text.length, (int)relative.length, relative.pointer);
                settings->io_failed |= written < 0;
                settings->evidence_count += written >= 0;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void d_collect_sanitizer_report(DSettings* settings, String8 prefix, String8 report_base,
                                                              u64 process_identifier, DObservation* observation)
{
    if (process_identifier)
    {
        String8 runtime_path = string_format_z(settings->arena, S8("{S8}.{u64}"), report_base, process_identifier);
        if (path_exists(settings->arena, runtime_path))
        {
            FileReadResult read = file_read_checked(settings->arena, runtime_path, (FileReadOptions){0});
            bool valid = read.status == OS_FILE_READ_OK && read.bytes.pointer && read.bytes.length;
            if (valid)
            {
                observation->sanitizer = true;
                observation->sanitizer_report = (String8){.pointer = (char8*)read.bytes.pointer, .length = read.bytes.length};
                d_write_evidence(settings, string_format(settings->arena, S8("{S8}.sanitizer"), prefix),
                    observation->sanitizer_report);
            }
            else { settings->io_failed = true; }
            settings->io_failed |= !os_file_delete(runtime_path);
        }
    }
}

BUSTER_GLOBAL_LOCAL void d_log(DSettings* settings, String8 text)
{
    if (settings->log)
    {
        settings->io_failed |= fwrite(text.pointer, 1, (size_t)text.length, settings->log) != text.length;
    }
    else { string_print(S8("{S8}"), text); }
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
    d_write_evidence(settings, string_format(arena, S8("{S8}.argv"), prefix), (String8){.pointer = bytes, .length = size});
    String8 sanitizer_report_base = {0};
    SliceString8 environment_keys = {0}, environment_values = {0};
    bool environment_valid = d_sanitizer_environment(settings,
        string_format(arena, S8("{S8}.sanitizer-runtime"), prefix), &sanitizer_report_base,
        &environment_keys, &environment_values);
    settings->io_failed |= !environment_valid;
    u64 start = os_now_microseconds();
    // Serialize pipe creation through closing each child's pipe ends. Otherwise
    // a concurrent child can inherit another child's writer and delay its EOF.
    // Child execution and deadline waits remain concurrent.
    if (settings->spawn_mutex) { os_mutex_lock(settings->spawn_mutex); }
    ProcessSpawnResult spawn = {0};
#if BUSTER_LINUX || BUSTER_MACOS
    ProcessGroupControlState process_group_control = {
        .cancellation_signal = &d_cancellation_signal,
        .cancellation_escalated = &d_cancellation_escalated,
        .admission_mutex = settings->spawn_mutex,
        .test_cancel_before_reap = settings->self_test_cancel_before_reap,
    };
#endif
    bool admitted = d_spawn_admission_begin();
    if (admitted && environment_valid)
    {
        spawn = os_process_spawn(command, environment_keys, environment_values,
            (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                  .use_process_environment = false,
                                  .search_path = true,
                                  .new_process_group = !BUSTER_WINDOWS});
#if BUSTER_LINUX || BUSTER_MACOS
        if (spawn.handle)
        {
            spawn.process_group_control = &process_group_control;
        }
#endif
    }
    d_spawn_admission_end();
    u64 process_identifier = d_process_identifier(spawn);
    if (spawn.handle && !process_identifier) { settings->io_failed = true; }
    if (settings->spawn_mutex) { os_mutex_unlock(settings->spawn_mutex); }
    DObservation observation = {.kind = D_SPAWN};
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, (u64)settings->timeout_seconds * 1000000);
        observation = d_wait_observation(wait);
#if BUSTER_LINUX || BUSTER_MACOS
        if (wait.process_group_reservation_retained)
        {
            // The wait deliberately retained an uncertain leader/PGID. Stop
            // all later admission without publishing that raw identity. The
            // OS owner already issued this notification; repeat it only as an
            // idempotent fail-safe for an unclassified platform failure.
            if (!d_atomic_load(&d_cancellation_signal))
            {
                d_cancel_admission(settings);
            }
        }
#endif
        d_collect_sanitizer_report(settings, prefix, sanitizer_report_base, process_identifier, &observation);
    }
    d_write_evidence(settings, string_format(arena, S8("{S8}.stdout"), prefix), observation.output);
    d_write_evidence(settings, string_format(arena, S8("{S8}.stderr"), prefix), observation.error);
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

// The fixed observer is immutable within a case. Compile it once for Buster
// rows, but never share it with the independent O0/O2 or reduction controls.
// Source-only flags belong to compilation; sanitizer runtime flags also belong
// to the final link. Explicit library paths make the Visual Studio developer
// shell's LIB contract visible to child compilers that do not consume LIB.
BUSTER_GLOBAL_LOCAL u64 d_caller_arguments(DSettings* settings, DCase test, bool compile, bool optimize, String8* argv)
{
    Arena* arena = settings->arena;
    u64 count = 0;
    argv[count++] = settings->cc;
    if (settings->reference_dialect == D_REFERENCE_MSVC)
    {
        argv[count++] = S8("/nologo");
        if (compile)
        {
            argv[count++] = S8("/std:c11");
            argv[count++] = S8("/TC");
            argv[count++] = S8("/J");
            argv[count++] = optimize ? S8("/O2") : S8("/Od");
            if (test.include.length) { argv[count++] = string_format(arena, S8("/I{S8}"), test.include); }
            if (settings->include.length) { argv[count++] = string_format(arena, S8("/I{S8}"), settings->include); }
        }
    }
    else
    {
        if (compile)
        {
            argv[count++] = optimize ? S8("-O2") : S8("-O0");
            argv[count++] = S8("-fwrapv");
            argv[count++] = S8("-fno-strict-aliasing");
            argv[count++] = S8("-funsigned-char");
        }
        if (settings->sanitize_oracle)
        {
            argv[count++] = S8("-fsanitize=address,undefined");
            argv[count++] = S8("-fno-sanitize-recover=all");
        }
        if (compile && test.include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), test.include); }
        if (compile && settings->include.length) { argv[count++] = string_format(arena, S8("-I{S8}"), settings->include); }
        for (u64 index = 0; index < settings->library_paths.length; index += 1)
        {
            argv[count++] = string_format(arena, S8("-L{S8}"), settings->library_paths.pointer[index]);
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL u64 d_reference_compile_arguments(DSettings* settings, DCase test, String8 source, String8 object,
                                                       bool optimize, String8* argv)
{
    u64 count = d_caller_arguments(settings, test, true, optimize, argv);
    argv[count++] = source;
    argv[count++] = S8("/c");
    argv[count++] = string_format(settings->arena, S8("/Fo{S8}"), object);
    return count;
}

BUSTER_GLOBAL_LOCAL u64 d_reference_link_arguments(DSettings* settings, DCase test, String8 caller, String8 object,
                                                    String8 executable, bool compile_caller, String8* argv)
{
    u64 count = d_caller_arguments(settings, test, compile_caller, false, argv);
    if (caller.length) { argv[count++] = caller; }
    argv[count++] = object;
    if (settings->reference_dialect == D_REFERENCE_MSVC)
    {
        argv[count++] = string_format(settings->arena, S8("/Fe{S8}"), executable);
        argv[count++] = S8("/link");
        for (u64 index = 0; index < settings->library_paths.length; index += 1)
        {
            argv[count++] = string_format(settings->arena, S8("/LIBPATH:{S8}"), settings->library_paths.pointer[index]);
        }
        argv[count++] = S8("legacy_stdio_definitions.lib");
    }
    else
    {
#if BUSTER_LINUX
        argv[count++] = S8("-no-pie");
#endif
        argv[count++] = S8("-o");
        argv[count++] = executable;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL bool d_caller_ready(DObservation compile, bool artifact_exists, bool io_failed)
{
    return d_success(compile) && artifact_exists && !io_failed;
}

BUSTER_GLOBAL_LOCAL String8 d_prepare_caller(DSettings* settings, DCase test, String8 directory)
{
    Arena* arena = settings->arena;
    String8 caller_directory = path_join(arena, directory, S8("caller"));
    make_directory_recursive(arena, caller_directory);
    String8 object = path_join(arena, caller_directory,
        settings->reference_dialect == D_REFERENCE_MSVC ? S8("caller.obj") : S8("caller.o"));
    os_file_delete(object);
    bool ready = false;
    if (!path_exists(arena, object) && !settings->io_failed)
    {
        String8* argv = arena_allocate(arena, String8, 40 + settings->library_paths.length);
        u64 count = 0;
        if (settings->reference_dialect == D_REFERENCE_MSVC)
        {
            count = d_reference_compile_arguments(settings, test, test.host, object, false, argv);
        }
        else
        {
            count = d_caller_arguments(settings, test, true, false, argv);
            argv[count++] = test.host;
            argv[count++] = S8("-c");
            argv[count++] = S8("-o");
            argv[count++] = object;
        }
        DObservation compile = d_observe(settings, (SliceString8){.pointer = argv, .length = count},
            path_join(arena, caller_directory, S8("compile")));
        ready = d_caller_ready(compile, path_exists(arena, object), settings->io_failed);
        if (ready)
        {
            u64 hash = 0, size = 0;
            ready = build_artifact_fanout_hash_file(arena, object, &hash, &size) && size > 0;
            if (ready)
            {
                d_write_evidence(settings, path_join(arena, caller_directory, S8("manifest.txt")),
                    string_format(arena, S8("version=1\nsource={S8}\nhash_algorithm=buster_hash_64\nobject_hash={u64} object_bytes={u64}\nsanitize_oracle={u32}\n"),
                        test.host, hash, size, (u32)settings->sanitize_oracle));
                ready = !settings->io_failed;
            }
        }
    }
    return ready ? object : (String8){0};
}

BUSTER_GLOBAL_LOCAL DResult d_execute(DSettings* settings, DCase test, DConfig config, bool host, bool optimize, String8 directory, String8 caller_object)
{
    Arena* arena = settings->arena;
    make_directory_recursive(arena, directory);
    String8 object = path_join(arena, directory,
        settings->reference_dialect == D_REFERENCE_MSVC ? S8("subject.obj") : S8("subject.o"));
#if BUSTER_WINDOWS
    String8 executable = path_join(arena, directory, S8("program.exe"));
#else
    String8 executable = path_join(arena, directory, S8("program"));
#endif
    // Output existence is never allowed to turn a failed compile into a pass.
    os_file_delete(object);
    os_file_delete(executable);
    String8* argv = arena_allocate(arena, String8, 40 + settings->library_paths.length);
    bool msvc_host = host && settings->reference_dialect == D_REFERENCE_MSVC;
    u64 count = 0;
    if (msvc_host)
    {
        count = d_reference_compile_arguments(settings, test, test.source, object, optimize, argv);
    }
    else
    {
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
            if ((test.strict_mir || settings->strict_mir) && d_mir_config(config)) { argv[count++] = S8("-fno-machine-fallback"); }
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
        for (u64 index = 0; index < settings->library_paths.length; index += 1)
        {
            argv[count++] = string_format(arena, S8("-L{S8}"), settings->library_paths.pointer[index]);
        }
        argv[count++] = test.source;
        if (test.host.length && host) { argv[count++] = test.host; }
        bool object_only = (!host && test.host.length) || test.reject;
        if (object_only) { argv[count++] = S8("-c"); }
#if BUSTER_LINUX
        if (host && !object_only) { argv[count++] = S8("-no-pie"); }
#endif
#if BUSTER_WINDOWS
        // Headerless Buster printf needs the UCRT compatibility definitions.
        if (!object_only && !test.host.length) { argv[count++] = S8("-llegacy_stdio_definitions"); }
#endif
        argv[count++] = S8("-o");
        argv[count++] = object_only ? object : executable;
    }
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
        if ((msvc_host || (!host && test.host.length)) && path_exists(arena, object))
        {
            result.linked = true;
            String8 link_caller = caller_object;
            bool caller_ready = true;
            if (msvc_host && test.host.length)
            {
                link_caller = path_join(arena, directory, S8("oracle-caller.obj"));
                os_file_delete(link_caller);
                count = d_reference_compile_arguments(settings, test, test.host, link_caller, optimize, argv);
                result.link = d_observe(settings, (SliceString8){.pointer = argv, .length = count},
                    path_join(arena, directory, S8("caller-compile")));
                caller_ready = d_caller_ready(result.link, path_exists(arena, link_caller), settings->io_failed);
            }
            if (caller_ready)
            {
                if (settings->reference_dialect == D_REFERENCE_MSVC)
                {
                    count = d_reference_link_arguments(settings, test, link_caller, object, executable, false, argv);
                }
                else
                {
                    count = d_reference_link_arguments(settings, test, link_caller.length ? link_caller : test.host,
                        object, executable, !link_caller.length, argv);
                }
                result.link = d_observe(settings, (SliceString8){.pointer = argv, .length = count}, path_join(arena, directory, S8("link")));
            }
            executable_ready = caller_ready && d_success(result.link) && path_exists(arena, executable);
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
    while (!exhausted && trials < settings->reduce_limit && best.length && !d_atomic_load(&d_cancellation_signal))
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
        for (u64 line = 0; !changed && line < lines && trials < settings->reduce_limit && !d_atomic_load(&d_cancellation_signal); line += chunk)
        {
            u64 from = boundaries[line];
            u64 to = boundaries[BUSTER_MIN(line + chunk, lines)];
            String8 candidate = {.pointer = candidate_bytes, .length = best.length - (to - from)};
            if (from) { memcpy(candidate_bytes, best.pointer, (size_t)from); }
            if (best.length > to) { memcpy(candidate_bytes + from, best.pointer + to, (size_t)(best.length - to)); }
            d_write(settings, source, candidate);
            String8 trial = path_join(arena, directory, string_format(arena, S8("trial-{u32}"), trials++));
            DResult o0 = d_execute(settings, reduced, config, true, false, path_join(arena, trial, S8("host-o0")), (String8){0});
            DResult o2 = d_execute(settings, reduced, config, true, true, path_join(arena, trial, S8("host-o2")), (String8){0});
            if (d_oracle_valid(o0, o2, reduced))
            {
                DResult actual = d_execute(settings, reduced, config, false, false, path_join(arena, trial, S8("buster")), (String8){0});
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
    d_write_evidence(settings, final_source, best);
    reduced.source = final_source;
    DResult o0 = d_execute(settings, reduced, config, true, false, path_join(arena, directory, S8("final-host-o0")), (String8){0});
    DResult o2 = d_execute(settings, reduced, config, true, true, path_join(arena, directory, S8("final-host-o2")), (String8){0});
    DResult actual = d_execute(settings, reduced, config, false, false, path_join(arena, directory, S8("final-buster")), (String8){0});
    bool confirmed = d_oracle_valid(o0, o2, reduced) && d_classify(actual, o0, false) == signature;
    d_write_evidence(settings, path_join(arena, directory, S8("reduction.txt")),
        string_format(arena, S8("version=1 original_bytes={u64} reduced_bytes={u64} trials={u32} signature={u32} confirmed={u32}\n"),
            input.length, best.length, trials, signature, (u32)confirmed));
    settings->io_failed |= !confirmed;
    settings->sanitize_oracle = saved_sanitizer;
    d_log(settings, string_format(arena, S8("DIFFERENTIAL_REDUCE case={S8} bytes={u64}->{u64} trials={u32} confirmed={u32}\n"), test.name, input.length, best.length, trials, (u32)confirmed));
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
    d_write_evidence(settings, path_join(arena, directory, S8("input.c")), (String8){.pointer = (char8*)input.pointer, .length = input.length});
    if (test.host.length)
    {
        ByteSlice fixed = file_read(arena, test.host, (FileReadOptions){0});
        d_write_evidence(settings, path_join(arena, directory, S8("host.c")), (String8){.pointer = (char8*)fixed.pointer, .length = fixed.length});
    }
    DResult o0 = d_execute(settings, test, configs[0], true, false, path_join(arena, directory, S8("host-o0")), (String8){0});
    DResult o2 = d_execute(settings, test, configs[0], true, true, path_join(arena, directory, S8("host-o2")), (String8){0});
    bool trusted = d_oracle_valid(o0, o2, test);
    u32 failures = trusted ? 0 : 1;
    DObservation diagnostics = {0};
    bool reduced = false;
    if (!trusted) { d_log(settings, string_format(arena, S8("DIFFERENTIAL_FAIL case={S8} independent_oracle=invalid\n"), test.name)); }
    String8 caller_object = {0};
    bool ready = trusted;
    if (trusted && test.host.length && !test.reject)
    {
        // Allocate before row scratch checkpoints. The object is case-local,
        // freshly built, and never enters the independent reference/reducer.
        caller_object = d_prepare_caller(settings, test, directory);
        ready = caller_object.length > 0;
        if (!ready)
        {
            failures += 1;
            d_log(settings, string_format(arena, S8("DIFFERENTIAL_FAIL case={S8} caller_compile=invalid\n"), test.name));
        }
    }
    for (u32 index = 0; ready && index < config_count && !d_atomic_load(&d_cancellation_signal); index += 1)
    {
        u64 scratch = arena->position;
        DConfig config = configs[index];
        settings->rows += 1;
        DResult actual = d_execute(settings, test, config, false, false, path_join(arena, directory, config.name), caller_object);
        u32 failure = d_classify(actual, o0, test.reject);
        // Source paths are identical across the matrix; only the explicitly
        // validated CODEGEN_VERIFY line is removed from successful diagnostics.
        if (index == 0) { diagnostics = actual.compile; }
        else if (!failure && d_difference(actual.compile, diagnostics)) { failure = 170; }
        if (failure)
        {
            failures += 1;
            d_write_evidence(settings, path_join(arena, path_join(arena, directory, config.name), S8("failure.txt")),
                string_format(arena, S8("version=1 signature={u32} oracle_exit={u32} candidate_exit={u32}\n"), failure, o0.run.status, actual.run.status));
            d_log(settings, string_format(arena, S8("DIFFERENTIAL_FAIL case={S8} config={S8} signature={u32}\n"), test.name, config.name, failure));
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
    d_log(settings, string_format(arena, S8("DIFFERENTIAL_CASE name={S8} configurations={u32} failures={u32}\n"), test.name, ready ? config_count : 0, failures));
    return failures;
}

// Explicit test children exercise the real capture/status/deadline path. They
// never compile arbitrary source and run only on the private self-test command.
#if BUSTER_LINUX || BUSTER_MACOS
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t d_cancel_tree_stop;
BUSTER_GLOBAL_LOCAL void d_cancel_tree_handler(int signal_number)
{
    BUSTER_UNUSED(signal_number);
    d_cancel_tree_stop = 1;
}

BUSTER_GLOBAL_LOCAL bool d_cancel_tree_marker_create(String8 path)
{
    int descriptor;
    do
    {
        descriptor = open((char*)path.pointer, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    } while (descriptor < 0 && errno == EINTR);
    bool result = descriptor >= 0;
    if (result)
    {
        ssize_t written;
        do
        {
            written = write(descriptor, "1", 1);
        } while (written < 0 && errno == EINTR);
        result = written == 1 && close(descriptor) == 0;
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL void d_self_test_child(Arena* arena, String8 mode, String8 path, u32 child_index, u32 child_count)
{
    if (string_equal(mode, S8("exit")))
    {
        os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), (ByteSlice){.pointer = (u8*)"a\0b", .length = 3});
        os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), (ByteSlice){.pointer = (u8*)"child stderr\n", .length = 13});
        exit(7);
    }
    else if (string_equal(mode, S8("sanitizer-name-stdout")) || string_equal(mode, S8("sanitizer-name-stderr")))
    {
        String8 text = d_sanitizer_name_text();
        StandardStream stream = string_equal(mode, S8("sanitizer-name-stdout")) ? STANDARD_STREAM_OUTPUT : STANDARD_STREAM_ERROR;
        os_file_write(os_get_standard_stream(stream), (ByteSlice){.pointer = (u8*)text.pointer, .length = text.length});
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
        // Core-dumping signals can block behind user-space core handlers on
        // hosted Linux; SIGTERM still exercises the signal observation path.
        raise(SIGTERM);
#endif
        exit(2);
    }
#if BUSTER_LINUX || BUSTER_MACOS
    else if (string_equal(mode, S8("cancel-tree")))
    {
        bool detached_leaf = child_count && child_index + 1 == child_count;
        String8 leaf_ready = detached_leaf ? string_format_z(arena, S8("{S8}.{u32}.leaf-ready"), path, child_index) : (String8){0};
        String8 leaf_release = detached_leaf ? string_format_z(arena, S8("{S8}.{u32}.leaf-release"), path, child_index) : (String8){0};
        String8 leaf_escaped = detached_leaf ? string_format_z(arena, S8("{S8}.{u32}.leaf-escaped"), path, child_index) : (String8){0};
        pid_t leaf = fork();
        if (leaf == 0)
        {
            if (detached_leaf)
            {
                signal(SIGTERM, SIG_IGN);
                close(STDOUT_FILENO);
                close(STDERR_FILENO);
                if (d_cancel_tree_marker_create(leaf_ready))
                {
                    while (access((char*)leaf_release.pointer, F_OK) != 0)
                    {
                        struct timespec delay = {.tv_nsec = 1000000};
                        nanosleep(&delay, 0);
                    }
                    d_cancel_tree_marker_create(leaf_escaped);
                }
                exit(0);
            }
            for (;;) { pause(); }
        }
        if (leaf > 0 && path.length)
        {
            struct sigaction action = {0};
            action.sa_handler = d_cancel_tree_handler;
            sigemptyset(&action.sa_mask);
            bool handler_ready = sigaction(SIGTERM, &action, 0) == 0;
            String8 marker = string_format_z(arena, S8("{S8}.{u32}"), path, child_index);
            FILE* file = d_file_open((char*)marker.pointer, true, false);
            if (file)
            {
                bool written = fprintf(file, "%ld\n", (long)getpid()) > 0 && fclose(file) == 0;
                bool ready = handler_ready && written && child_count > 0;
                bool all_ready = false;
                u64 deadline = os_now_microseconds() + 5000000;
                while (ready && !all_ready && os_now_microseconds() < deadline)
                {
                    all_ready = !detached_leaf || access((char*)leaf_ready.pointer, F_OK) == 0;
                    for (u32 index = 0; all_ready && index < child_count; index += 1)
                    {
                        all_ready = path_exists(arena, string_format(arena, S8("{S8}.{u32}"), path, index));
                    }
                    if (!all_ready)
                    {
                        struct timespec delay = {.tv_nsec = 10000000};
                        nanosleep(&delay, 0);
                    }
                }
                ready &= all_ready;
                if (ready)
                {
                    if (detached_leaf) { exit(0); }
                    else
                    {
                        while (!d_cancel_tree_stop) { pause(); }
                        kill(leaf, SIGTERM);
                        while (waitpid(leaf, 0, 0) < 0 && errno == EINTR) {}
                        signal(SIGTERM, SIG_DFL);
                        raise(SIGTERM);
                    }
                }
            }
            kill(leaf, SIGKILL);
            waitpid(leaf, 0, 0);
        }
        exit(1);
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
#endif
}

typedef struct DCancellationSelfTestWork DCancellationSelfTestWork;
struct DCancellationSelfTestWork
{
    DSettings settings;
    String8 marker;
    DObservation observations[4];
    u32 jobs;
};

BUSTER_GLOBAL_LOCAL void d_cancellation_self_test_lane(void* argument)
{
    DCancellationSelfTestWork* work = argument;
    u32 index = lane_index();
    DSettings settings = work->settings;
    settings.arena = arena_create((ArenaCreation){0});
    settings.self_test_cancel_before_reap = index + 1 == work->jobs;
    String8 index_text = string_format(settings.arena, S8("{u32}"), index);
    String8 count_text = string_format(settings.arena, S8("{u32}"), work->jobs);
    String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"),
                     S8("--self-test-child"), S8("cancel-tree"), S8("--self-test-path"), work->marker,
                     S8("--self-test-index"), index_text, S8("--self-test-count"), count_text};
    work->observations[index] = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv),
        string_format(settings.arena, S8("{S8}.{u32}.observation"), work->marker, index));
    BUSTER_CHECK(arena_destroy(settings.arena, 1));
}

BUSTER_GLOBAL_LOCAL bool d_jobs(u32 requested, u32 cpus, String8 quota_text, u32* jobs);

// Run only as a nested self-test process: its child creates a grandchild and
// then cancels this runner. The production handlers must terminate the whole
// private process group, reap the direct child, and finally preserve SIGTERM
// as this process's externally visible status.
BUSTER_GLOBAL_LOCAL u32 d_cancellation_self_test(Arena* arena, String8 marker)
{
    u32 errors = 0;
#if BUSTER_LINUX || BUSTER_MACOS
    DCancellationHandlers handlers = {0};
    errors += !d_cancellation_begin(&handlers);
    errors += !d_process_group_control_self_test();
    if (!errors)
    {
        DCancellationSelfTestWork work = {.settings = {.arena = arena, .timeout_seconds = 10}, .marker = marker};
        errors += !d_jobs(BUSTER_ARRAY_LENGTH(work.observations), os_get_logical_thread_count(),
            os_get_environment_variable(S8("BUSTER_TEST_JOBS")), &work.jobs);
        work.settings.spawn_mutex = os_mutex_create();
        errors += !work.settings.spawn_mutex;
        if (work.settings.spawn_mutex && work.jobs) { lane_run(work.jobs, &d_cancellation_self_test_lane, &work); }
        if (work.jobs)
        {
            u32 detached_index = work.jobs - 1;
            String8 leaf_release = string_format_z(arena, S8("{S8}.{u32}.leaf-release"), marker, detached_index);
            errors += !d_cancel_tree_marker_create(leaf_release);
            struct timespec delay = {.tv_nsec = 300000000};
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
            {
            }
        }
        for (u32 index = 0; index < work.jobs; index += 1)
        {
            DObservation observation = work.observations[index];
            if (index + 1 == work.jobs) { errors += observation.kind != D_EXIT || observation.status != 0; }
            else { errors += observation.kind != D_SIGNAL || (observation.status != SIGTERM && observation.status != SIGKILL); }
            errors += !path_exists(arena, string_format(arena, S8("{S8}.{u32}"), marker, index));
            if (index + 1 == work.jobs)
            {
                String8 leaf_ready = string_format_z(arena, S8("{S8}.{u32}.leaf-ready"), marker, index);
                String8 leaf_release = string_format_z(arena, S8("{S8}.{u32}.leaf-release"), marker, index);
                String8 leaf_escaped = string_format_z(arena, S8("{S8}.{u32}.leaf-escaped"), marker, index);
                bool escaped = path_exists(arena, leaf_escaped);
                errors += !path_exists(arena, leaf_ready);
                errors += escaped;
                errors += !os_file_delete(leaf_ready);
                errors += !os_file_delete(leaf_release);
                if (escaped) { errors += !os_file_delete(leaf_escaped); }
            }
        }
        if (work.settings.spawn_mutex) { os_mutex_destroy(work.settings.spawn_mutex); }
    }
    u32 signal_number = d_cancellation_end(&handlers);
    errors += signal_number != SIGTERM;
    if (!errors)
    {
        fflush(0);
        raise((int)signal_number);
        errors = 1;
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(marker);
#endif
    return errors;
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

typedef struct DCaseRecord DCaseRecord;
struct DCaseRecord
{
    u32 failures;
    u32 rows;
    u32 completed;
    bool io_failed;
    u64 report_hash;
    u64 report_size;
    u64 log_hash;
    u64 log_size;
    u64 evidence_hash;
    u64 evidence_size;
    u32 evidence_count;
};
typedef struct DCaseWork DCaseWork;
struct DCaseWork
{
    DSettings settings;
    DCase* tests;
    DConfig* configs;
    DCaseRecord* records;
    u32 count;
    u32 config_count;
    AtomicU64 next;
    bool self_test;
    // Private self-test observations, protected by spawn_mutex while lanes run.
    // Corpus runs neither update these counters nor wait on the test gates.
    u32 test_jobs;
    u32 test_active;
    u32 test_peak;
    u32 test_started;
    u32 test_finished;
    u32 test_lanes;
    u32 test_arenas;
    u32* test_completions;
    bool test_skew;
};

BUSTER_GLOBAL_LOCAL bool d_jobs(u32 requested, u32 cpus, String8 quota_text, u32* jobs)
{
    u32 quota = UINT32_MAX;
    bool valid = requested > 0 && requested <= 64;
    if (quota_text.length) { valid &= d_number(quota_text, &quota) && quota > 0; }
    if (valid)
    {
        *jobs = BUSTER_MIN(requested, BUSTER_MAX(cpus, 1U));
        *jobs = BUSTER_MIN(*jobs, quota);
#if BUSTER_SINGLE_THREADED
        *jobs = 1;
#endif
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 d_case_record_text(Arena* arena, DCase test, DCaseRecord record, u32 count)
{
    return string_format(arena, S8("version=2 case={S8} configurations={u32} rows={u32} failures={u32} io_failed={u32} completed={u32} evidence_files={u32}\n"),
        test.name, count, record.rows, record.failures, (u32)record.io_failed, record.completed, record.evidence_count);
}

BUSTER_GLOBAL_LOCAL u32 d_evidence_hash_capacity(u64 entries)
{
    u64 capacity = 1;
    while (capacity < entries * 2 && capacity <= UINT32_MAX / 2) { capacity *= 2; }
    u32 result = capacity >= entries * 2 && capacity <= UINT32_MAX ? (u32)capacity : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL u32 d_evidence_path_capacity(u32 config_count, u32 reduce_limit)
{
    u64 entries = 128 + (u64)config_count * 16 + (u64)reduce_limit * 32;
    u32 result = d_evidence_hash_capacity(entries);
    return result;
}

// Admit an initial full cohort before releasing its short cases. Case zero
// then waits for every other case, proving that a free lane drains the dynamic
// queue while a longer case remains active. This is an ordering control, not a
// timing measurement. Its independent 30-second rendezvous deadline fails the
// control without stranding lanes; child deadlines remain unchanged.
BUSTER_GLOBAL_LOCAL bool d_worker_self_test_enter(DCaseWork* work, u64 index)
{
    os_mutex_lock(work->settings.spawn_mutex);
    work->test_started += 1;
    work->test_active += 1;
    work->test_peak = BUSTER_MAX(work->test_peak, work->test_active);
    os_mutex_unlock(work->settings.spawn_mutex);
    bool ready = false;
    u64 start = os_now_microseconds();
    while (!ready && os_now_microseconds() - start < 30000000)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        ready = work->test_started >= work->test_jobs;
        if (work->test_skew && work->test_jobs > 1 && index == 0)
        {
            ready &= work->test_finished + 1 == work->count;
        }
        os_mutex_unlock(work->settings.spawn_mutex);
        if (!ready)
        {
#if BUSTER_WINDOWS
            Sleep(1);
#else
            struct timespec delay = {.tv_nsec = 1000000};
            nanosleep(&delay, 0);
#endif
        }
    }
    return ready;
}

BUSTER_GLOBAL_LOCAL void d_case_lane(void* argument)
{
    DCaseWork* work = argument;
    Arena* arena = arena_create((ArenaCreation){0});
    u64 start = arena->position;
    if (work->self_test)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        work->test_lanes += 1;
        work->test_arenas += 1;
        os_mutex_unlock(work->settings.spawn_mutex);
    }
    for (;;)
    {
        if (d_atomic_load(&d_cancellation_signal)) { break; }
        u64 index = atomic_u64_increment(&work->next);
        if (index >= work->count) { break; }
        DCase test = work->tests[index];
        DCaseRecord* record = work->records + index;
        if (work->self_test) { record->failures += !d_worker_self_test_enter(work, index); }
        DSettings settings = work->settings;
        settings.arena = arena;
        settings.report = 0;
        settings.log = 0;
        settings.evidence = 0;
        settings.evidence_directory = (String8){0};
        settings.evidence_path_hashes = 0;
        settings.evidence_path_capacity = 0;
        settings.evidence_count = 0;
        settings.rows = 0;
        String8 directory = path_join(arena, settings.out, test.name);
        bool claimed = d_create_output(arena, directory);
        if (!claimed) { settings.io_failed = true; }
        else
        {
            String8 report_path = string_format_z(arena, S8("{S8}/processes.tsv"), directory);
            String8 log_path = string_format_z(arena, S8("{S8}/case.log"), directory);
            String8 evidence_path = string_format_z(arena, S8("{S8}/evidence.tsv"), directory);
            settings.evidence_directory = directory;
            settings.evidence_path_capacity = d_evidence_path_capacity(work->config_count, settings.reduce_limit);
            if (settings.evidence_path_capacity)
            {
                settings.evidence_path_hashes = arena_allocate_zeroed(arena, u64, settings.evidence_path_capacity);
            }
            settings.report = d_file_open((char*)report_path.pointer, true, false);
            settings.log = d_file_open((char*)log_path.pointer, true, false);
            settings.evidence = d_file_open((char*)evidence_path.pointer, true, true);
            if (!settings.report || !settings.log || !settings.evidence || !settings.evidence_path_capacity)
            {
                settings.io_failed = true;
            }
            if (settings.evidence)
            {
                settings.io_failed |= fprintf(settings.evidence, "version=1 files=0000000000\n") != 27;
            }
            if (!settings.io_failed)
            {
                if (work->self_test)
                {
                    String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"), S8("--self-test-child"), test.source};
                    if (test.host.length) { argv[0] = test.host; }
                    DObservation observed = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv), path_join(arena, directory, S8("child")));
                    bool normal = observed.kind == D_EXIT && observed.status == 7 &&
                        string_equal(observed.output, S8("a\0b")) && string_equal(observed.error, S8("child stderr\n"));
                    record->failures += !normal;
                    if (test.expect_sanitizer)
                    {
                        record->failures += observed.kind != D_EXIT || observed.status != 0 || !observed.sanitizer ||
                            !d_contains(observed.sanitizer_report, S8("runtime error:"));
                    }
                    settings.rows = 1;
                    d_log(&settings, string_format(arena, S8("case={S8}\n"), test.name));
                }
                else { record->failures = d_case_run(&settings, test, work->configs, work->config_count); }
            }
            if (settings.report) { settings.io_failed |= fclose(settings.report) != 0; }
            if (settings.log) { settings.io_failed |= fclose(settings.log) != 0; }
            if (settings.evidence)
            {
                settings.io_failed |= fseek(settings.evidence, 0, SEEK_SET) != 0;
                settings.io_failed |= fprintf(settings.evidence, "version=1 files=%010u\n", settings.evidence_count) != 27;
                settings.io_failed |= fclose(settings.evidence) != 0;
            }
            settings.io_failed |= !build_artifact_fanout_hash_file(arena, report_path, &record->report_hash, &record->report_size);
            settings.io_failed |= !build_artifact_fanout_hash_file(arena, log_path, &record->log_hash, &record->log_size);
            settings.io_failed |= !build_artifact_fanout_hash_file(arena, evidence_path, &record->evidence_hash, &record->evidence_size);
        }
        record->rows = settings.rows;
        record->evidence_count = settings.evidence_count;
        record->io_failed = settings.io_failed;
        record->completed += 1;
        if (claimed)
        {
            d_write(&settings, path_join(arena, directory, S8("result.txt")), d_case_record_text(arena, test, *record, work->config_count));
            record->io_failed |= settings.io_failed;
        }
        record->io_failed |= !arena_set_position_and_decommit(arena, start);
        if (work->self_test)
        {
            // A case is no longer active only after its children were waited,
            // streams closed, completion written and scratch cleanup attempted.
            os_mutex_lock(work->settings.spawn_mutex);
            work->test_active -= 1;
            work->test_finished += 1;
            work->test_completions[index] = work->test_finished;
            os_mutex_unlock(work->settings.spawn_mutex);
        }
    }
    BUSTER_CHECK(arena_destroy(arena, 1));
    if (work->self_test)
    {
        os_mutex_lock(work->settings.spawn_mutex);
        work->test_arenas -= 1;
        os_mutex_unlock(work->settings.spawn_mutex);
    }
}

// Reopen and hash each completed stream before ordered publication. Missing,
// truncated, duplicated or unwritten case evidence is never a passing slot.
BUSTER_GLOBAL_LOCAL bool d_case_stream(DSettings* settings, String8 path, u64 expected_hash, u64 expected_size, FILE* output)
{
    u64 hash = 0, size = 0;
    bool valid = build_artifact_fanout_hash_file(settings->arena, path, &hash, &size) && hash == expected_hash && size == expected_size;
    if (valid)
    {
        String8 path_z = string_duplicate_arena(settings->arena, path, true);
        FILE* input = d_file_open((char*)path_z.pointer, false, false);
        valid = input != 0;
        if (input)
        {
            u8 buffer[16384];
            u64 copied = 0;
            size_t length;
            while ((length = fread(buffer, 1, sizeof(buffer), input)) != 0)
            {
                copied += length;
                if (output) { valid &= fwrite(buffer, 1, length, output) == length; }
            }
            valid &= !ferror(input) && copied == expected_size;
            valid &= fclose(input) == 0;
        }
    }
    if (output) { valid &= fflush(output) == 0; }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool d_evidence_line_valid(DSettings* settings, String8 directory, String8 line,
                                                u64* path_hashes, u32 path_capacity)
{
    u64 first_tab = UINT64_MAX, second_tab = UINT64_MAX;
    bool valid = line.length > 0;
    for (u64 index = 0; valid && index < line.length; index += 1)
    {
        if (line.pointer[index] == '\t')
        {
            if (first_tab == UINT64_MAX) { first_tab = index; }
            else if (second_tab == UINT64_MAX) { second_tab = index; }
            else { valid = false; }
        }
    }
    valid = valid && first_tab != UINT64_MAX && second_tab != UINT64_MAX &&
        first_tab > 0 && second_tab > first_tab + 1 && second_tab + 1 < line.length;
    String8 hash_text = valid ? string_slice(line, 0, first_tab) : (String8){0};
    String8 size_text = valid ? string_slice(line, first_tab + 1, second_tab) : (String8){0};
    String8 relative = valid ? string_slice(line, second_tab + 1, line.length) : (String8){0};
    IntegerParsingU64 hash = string8_parse_u64_decimal(hash_text);
    IntegerParsingU64 size = string8_parse_u64_decimal(size_text);
    valid = valid && hash.status == INTEGER_PARSING_SUCCESS && hash.length == hash_text.length &&
        size.status == INTEGER_PARSING_SUCCESS && size.length == size_text.length;
    String8 path = valid ? path_join(settings->arena, directory, relative) : (String8){0};
    bool relative_valid = false;
    String8 checked = valid ? d_evidence_relative_path(directory, path, &relative_valid) : (String8){0};
    valid = valid && relative_valid && string_equal(relative, checked) &&
        d_evidence_path_insert(path_hashes, path_capacity, relative);
    if (valid)
    {
        FileReadResult read = file_read_checked(settings->arena, path, (FileReadOptions){0});
        valid = read.bytes.pointer && read.bytes.length == size.value &&
            buster_hash_64(read.bytes.pointer, read.bytes.length) == hash.value;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool d_evidence_manifest_valid(DSettings* settings, String8 directory, DCaseRecord record)
{
    String8 path = path_join(settings->arena, directory, S8("evidence.tsv"));
    FileReadResult read = file_read_checked(settings->arena, path, (FileReadOptions){0});
    bool valid = read.bytes.pointer && read.bytes.length == record.evidence_size &&
        buster_hash_64(read.bytes.pointer, read.bytes.length) == record.evidence_hash;
    String8 text = valid ? (String8){.pointer = (char8*)read.bytes.pointer, .length = read.bytes.length} : (String8){0};
    String8 prefix = S8("version=1 files=");
    u64 newline = 0;
    while (valid && newline < text.length && text.pointer[newline] != '\n') { newline += 1; }
    valid = valid && newline == prefix.length + 10 && newline < text.length &&
        memcmp(text.pointer, prefix.pointer, (size_t)prefix.length) == 0;
    String8 count_text = valid ? string_slice(text, prefix.length, newline) : (String8){0};
    IntegerParsingU64 count = string8_parse_u64_decimal(count_text);
    valid = valid && count.status == INTEGER_PARSING_SUCCESS && count.length == count_text.length &&
        count.value == record.evidence_count;
    u32 path_capacity = valid ? d_evidence_hash_capacity((u64)record.evidence_count + 1) : 0;
    valid = valid && path_capacity;
    u64* path_hashes = valid ? arena_allocate_zeroed(settings->arena, u64, path_capacity) : 0;
    u64 row_scratch = settings->arena->position;
    u64 at = newline + 1;
    for (u32 index = 0; valid && index < record.evidence_count; index += 1)
    {
        u64 end = at;
        while (end < text.length && text.pointer[end] != '\n') { end += 1; }
        valid = end < text.length && d_evidence_line_valid(settings, directory, string_slice(text, at, end),
            path_hashes, path_capacity);
        at = end + 1;
        arena_set_position(settings->arena, row_scratch);
    }
    valid = valid && at == text.length;
    return valid;
}

BUSTER_GLOBAL_LOCAL u32 d_cases_collect(DCaseWork* work)
{
    DSettings* settings = &work->settings;
    Arena* arena = settings->arena;
    u32 failures = 0;
    for (u32 index = 0; index < work->count; index += 1)
    {
        u64 scratch = arena->position;
        DCaseRecord record = work->records[index];
        String8 directory = path_join(arena, settings->out, work->tests[index].name);
        bool valid = record.completed == 1 && !record.io_failed && (record.failures || record.rows == work->config_count);
        String8 expected = d_case_record_text(arena, work->tests[index], record, work->config_count);
        u64 expected_hash = 0, expected_size = 0;
        String8 result_path = path_join(arena, directory, S8("result.txt"));
        valid &= build_artifact_fanout_hash_file(arena, result_path, &expected_hash, &expected_size) && expected_size == expected.length;
        if (valid)
        {
            ByteSlice bytes = file_read(arena, result_path, (FileReadOptions){0});
            valid = string_equal(expected, (String8){.pointer = (char8*)bytes.pointer, .length = bytes.length});
        }
        // Inspect both streams even when the case failed; preserve its evidence.
        valid &= d_case_stream(settings, path_join(arena, directory, S8("processes.tsv")), record.report_hash, record.report_size, settings->report);
        valid &= d_case_stream(settings, path_join(arena, directory, S8("case.log")), record.log_hash, record.log_size, settings->log ? settings->log : stdout);
        valid &= d_evidence_manifest_valid(settings, directory, record);
        failures += record.failures + !valid;
        arena_set_position(arena, scratch);
    }
    return failures;
}

BUSTER_GLOBAL_LOCAL u32 d_cases_run(DSettings* settings, DCase* tests, u32 count, DConfig* configs, u32 config_count, u32 jobs)
{
    DCaseWork work = {.settings = *settings, .tests = tests, .count = count, .configs = configs, .config_count = config_count};
    work.records = arena_allocate_zeroed(settings->arena, DCaseRecord, count);
    work.settings.spawn_mutex = os_mutex_create();
    u32 failures = 1;
    if (work.settings.spawn_mutex)
    {
        lane_run(BUSTER_MIN(jobs, count), &d_case_lane, &work);
        os_mutex_destroy(work.settings.spawn_mutex);
        failures = d_cases_collect(&work);
    }
    return failures;
}

BUSTER_GLOBAL_LOCAL u32 d_sanitizer_runtime_self_test(Arena* arena, DSettings* parent, String8 root,
                                                               String8* recovering_program)
{
    *recovering_program = (String8){0};
    String8 configured = os_get_environment_variable(S8("CC"));
    String8 compiler = configured.length ? d_reference_compiler(arena, configured) : (String8){0};
    if (!compiler.length) { compiler = d_reference_compiler(arena, S8("clang")); }
    if (!compiler.length) { compiler = d_reference_compiler(arena, S8("cc")); }
#if BUSTER_WINDOWS && BUSTER_CPU_ARCH_AARCH64
    // The hosted Windows Arm64 LLVM toolchain currently has no linkable
    // compiler-rt UBSan runtime. Report the real runtime control as
    // unavailable; never replace it with sanitizer-looking program text.
    bool available = false;
#else
    bool available = compiler.length > 0;
#endif
    bool recover_built = false, fatal_built = false, recover_ok = false, fatal_ok = false;
    u32 errors = 0;
    DSettings settings = *parent;
    settings.timeout_seconds = 30;
    if (available)
    {
        String8 source = path_join(arena, root, S8("sanitizer-control.c"));
#if BUSTER_WINDOWS
        String8 recover_path = path_join(arena, root, S8("sanitizer-recover.exe"));
        String8 fatal_path = path_join(arena, root, S8("sanitizer-fatal.exe"));
#else
        String8 recover_path = path_join(arena, root, S8("sanitizer-recover"));
        String8 fatal_path = path_join(arena, root, S8("sanitizer-fatal"));
#endif
        d_write(&settings, source, S8("#include <limits.h>\nint main(void)\n{\n    volatile int value = INT_MAX;\n    value += 1;\n    (void)value;\n    return 0;\n}\n"));
        String8 recover_argv[] = {compiler, S8("-O0"), S8("-fsanitize=undefined"), S8("-fsanitize-recover=all"),
                                  source, S8("-o"), recover_path};
        String8 fatal_argv[] = {compiler, S8("-O0"), S8("-fsanitize=undefined"), S8("-fno-sanitize-recover=all"),
                                source, S8("-o"), fatal_path};
        DObservation recover_compile = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(recover_argv),
            path_join(arena, root, S8("sanitizer-compile-recover")));
        DObservation fatal_compile = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(fatal_argv),
            path_join(arena, root, S8("sanitizer-compile-fatal")));
        recover_built = d_success(recover_compile) && path_exists(arena, recover_path);
        fatal_built = d_success(fatal_compile) && path_exists(arena, fatal_path);
        if (recover_built && fatal_built && !settings.io_failed)
        {
            String8 recover_run_argv[] = {recover_path};
            String8 fatal_run_argv[] = {fatal_path};
            DObservation recover = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(recover_run_argv),
                path_join(arena, root, S8("sanitizer-run-recover")));
            DObservation fatal = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(fatal_run_argv),
                path_join(arena, root, S8("sanitizer-run-fatal")));
            recover_ok = recover.kind == D_EXIT && recover.status == 0 && recover.sanitizer &&
                d_contains(recover.sanitizer_report, S8("runtime error:"));
            fatal_ok = fatal.sanitizer && d_contains(fatal.sanitizer_report, S8("runtime error:")) &&
                (fatal.kind == D_SIGNAL || (fatal.kind == D_EXIT && fatal.status != 0));
            if (recover_ok) { *recovering_program = recover_path; }
        }
        errors += !recover_built || !fatal_built;
        if (recover_built && fatal_built) { errors += !recover_ok; errors += !fatal_ok; }
        errors += settings.io_failed;
    }
    string_print(S8("DIFFERENTIAL_SANITIZER_CONTROL version=1 available={u32} recover_built={u32} fatal_built={u32} recover_ok={u32} fatal_ok={u32} errors={u32}\n"),
        (u32)available, (u32)recover_built, (u32)fatal_built, (u32)recover_ok, (u32)fatal_ok, errors);
    return errors;
}

BUSTER_GLOBAL_LOCAL u32 d_workers_self_test(Arena* arena, String8 root, String8 sanitizer_program)
{
    u32 errors = 0, jobs = 0;
    errors += !d_jobs(4, 2, S8("3"), &jobs) || jobs != (BUSTER_SINGLE_THREADED ? 1 : 2);
    errors += !d_jobs(4, 8, S8("1"), &jobs) || jobs != 1;
    errors += d_jobs(0, 8, S8(""), &jobs) || d_jobs(65, 8, S8(""), &jobs);
    errors += d_jobs(4, 8, S8("0"), &jobs) || d_jobs(4, 8, S8("2x"), &jobs);
    errors += d_jobs(4, 8, S8("4294967296"), &jobs);
    u32 requested[] = {1, 2, 4, 4};
    for (u32 pass = 0; pass < BUSTER_ARRAY_LENGTH(requested); pass += 1)
    {
        u32 pass_errors = errors, worker_jobs = 0;
        bool failure_control = pass + 1 == BUSTER_ARRAY_LENGTH(requested);
        DCase tests[] = {{.name = S8("first"), .source = S8("exit")},
                         {.name = S8("second"), .source = S8("exit")},
                         {.name = S8("third"), .source = S8("exit")},
                         {.name = S8("fourth"), .source = S8("exit")},
                         {.name = S8("fifth"), .source = S8("exit")},
                         {.name = S8("sixth"), .source = S8("exit")}};
        DCaseRecord records[BUSTER_ARRAY_LENGTH(tests)] = {0};
        u32 completions[BUSTER_ARRAY_LENGTH(tests)] = {0};
        DCaseWork work = {.settings = {.arena = arena, .timeout_seconds = 1}, .tests = tests, .records = records,
                         .count = BUSTER_ARRAY_LENGTH(tests), .config_count = 1, .self_test = true,
                         .test_completions = completions, .test_skew = !failure_control};
        work.settings.out = path_join(arena, root, string_format(arena, S8("workers-{u32}"), pass));
        errors += !d_create_output(arena, work.settings.out);
        String8 report = string_format_z(arena, S8("{S8}/processes.tsv"), work.settings.out);
        String8 log = string_format_z(arena, S8("{S8}/case.log"), work.settings.out);
        work.settings.report = d_file_open((char*)report.pointer, true, false);
        work.settings.log = d_file_open((char*)log.pointer, true, true);
        work.settings.spawn_mutex = os_mutex_create();
        if (!work.settings.report || !work.settings.log || !work.settings.spawn_mutex) { errors += 1; }
        else
        {
            if (failure_control)
            {
                tests[0].source = S8("timeout");
                tests[1].source = S8("crash");
                if (sanitizer_program.length)
                {
                    tests[2].host = sanitizer_program;
                    tests[2].expect_sanitizer = true;
                }
                else { tests[2].source = S8("crash"); }
                tests[3].host = path_join(arena, work.settings.out, S8("missing-program"));
                // Both duplicate directory claims must not count as success.
                tests[5].name = tests[4].name;
            }
            errors += !d_jobs(requested[pass], os_get_logical_thread_count(), os_get_environment_variable(S8("BUSTER_TEST_JOBS")), &worker_jobs);
            work.test_jobs = worker_jobs;
            lane_run(worker_jobs, &d_case_lane, &work);
            u32 failures = d_cases_collect(&work);
            if (failure_control && sanitizer_program.length)
            {
                errors += records[2].failures != 1;
                errors += records[2].evidence_count != 4;
            }
            errors += failure_control ? failures < 5 : failures != 0;
            errors += work.test_lanes != worker_jobs || work.test_peak != worker_jobs;
            errors += work.test_active != 0 || work.test_arenas != 0;
            errors += work.test_started != work.count || work.test_finished != work.count;
            if (!failure_control && failures)
            {
                for (u32 index = 0; index < work.count; index += 1)
                {
                    string_print(S8("DIFFERENTIAL_SELF_TEST_RECORD pass={u32} case={S8} rows={u32} failures={u32} io_failed={u32} report_bytes={u64} log_bytes={u64} evidence_files={u32} evidence_bytes={u64}\n"),
                        pass, tests[index].name, records[index].rows, records[index].failures, (u32)records[index].io_failed,
                        records[index].report_size, records[index].log_size, records[index].evidence_count, records[index].evidence_size);
                }
            }
            for (u32 index = 0; index < work.count; index += 1)
            {
                errors += records[index].completed != 1;
                errors += completions[index] == 0 || completions[index] > work.count;
                for (u32 other = 0; other < index; other += 1) { errors += completions[index] == completions[other]; }
                if (!failure_control && worker_jobs == 1) { errors += completions[index] != index + 1; }
            }
            if (!failure_control)
            {
                if (worker_jobs > 1) { errors += completions[0] != work.count; }
                // Publication remains byte-identical to the one-lane order,
                // even though the first case deliberately completed last.
                // Inspect through the owning stream: a second OS open with
                // read-only sharing conflicts with the live writer on Windows.
                errors += fseek(work.settings.log, 0, SEEK_SET) != 0;
                char8 output[256];
                size_t output_length = fread(output, 1, sizeof(output), work.settings.log);
                errors += ferror(work.settings.log) != 0;
                errors += !string_equal((String8){.pointer = output, .length = output_length},
                    S8("case=first\ncase=second\ncase=third\ncase=fourth\ncase=fifth\ncase=sixth\n"));
                errors += fseek(work.settings.log, 0, SEEK_END) != 0;
                records[0].completed = 0;
                errors += d_cases_collect(&work) == 0;
                records[0].completed = 2;
                errors += d_cases_collect(&work) == 0;
                records[0].completed = 1;
                records[0].rows = 0;
                errors += d_cases_collect(&work) == 0;
                records[0].rows = 1;
                String8 first = path_join(arena, work.settings.out, tests[0].name);
                String8 child_stdout = path_join(arena, first, S8("child.stdout"));
                d_write(&work.settings, child_stdout, S8("truncated"));
                errors += d_cases_collect(&work) == 0;
                d_write(&work.settings, child_stdout, S8("a\0b"));
                errors += d_cases_collect(&work) != 0;
                String8 child_argv = path_join(arena, first, S8("child.argv"));
                FileReadResult saved_argv = file_read_checked(arena, child_argv, (FileReadOptions){0});
                errors += !saved_argv.bytes.pointer;
                errors += !os_file_delete(child_argv);
                errors += d_cases_collect(&work) == 0;
                d_write(&work.settings, child_argv,
                    (String8){.pointer = (char8*)saved_argv.bytes.pointer, .length = saved_argv.bytes.length});
                errors += d_cases_collect(&work) != 0;
                d_write(&work.settings, path_join(arena, first, S8("case.log")), S8("truncated"));
                errors += d_cases_collect(&work) == 0;
                errors += !os_file_delete(path_join(arena, first, S8("result.txt")));
                errors += d_cases_collect(&work) == 0;
                // A real evidence write into a directory fails closed.
                DSettings broken = work.settings;
                d_write(&broken, first, S8("cannot write"));
                errors += !broken.io_failed;
            }
        }
        if (work.settings.report) { errors += fclose(work.settings.report) != 0; }
        if (work.settings.log) { errors += fclose(work.settings.log) != 0; }
        if (work.settings.spawn_mutex) { os_mutex_destroy(work.settings.spawn_mutex); }
        string_print(S8("DIFFERENTIAL_WORKER_CONTROL version=1 requested={u32} effective={u32} cases={u32} peak={u32} started={u32} finished={u32} active={u32} arenas={u32} failure_control={u32} errors={u32}\n"),
            requested[pass], worker_jobs, work.count, work.test_peak, work.test_started, work.test_finished,
            work.test_active, work.test_arenas, (u32)failure_control, errors - pass_errors);
        if (errors != pass_errors)
        {
            string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL workers_pass={u32} failures={u32}\n"), pass, errors - pass_errors);
        }
    }
    return errors;
}

BUSTER_GLOBAL_LOCAL u32 d_self_test(Arena* arena)
{
    String8 self = os_path_absolute(arena, program_state->input.arguments.pointer[0], true);
    u32 path_errors = !self.length || !string_equal(d_reference_compiler(arena, self), self);
    DObservation normal = {.kind = D_EXIT};
    DObservation other = normal;
    u32 errors = path_errors + (d_difference(normal, other) != 0);
    if (path_errors) { string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL explicit_compiler_path\n")); }
    other.status = 7; errors += d_difference(normal, other) != 2;
    other = normal; other.kind = D_TIMEOUT; errors += d_difference(normal, other) != 1;
    other.kind = D_SIGNAL; other.status = 11; errors += d_normal(other);
    other = normal; other.sanitizer = true; errors += d_normal(other);
    normal.output = S8("a\0b"); other = normal; other.output = S8("a\0c");
    errors += d_difference(normal, other) != 4;
    u32 number = 1;
    errors += !d_number(S8("4294967295"), &number) || number != UINT32_MAX;
    errors += d_number(S8("4294967296"), &number);
    errors += d_number(S8("-1"), &number);
    errors += d_number(S8(""), &number);
    u64 evidence_paths[8] = {0};
    errors += !d_evidence_path_insert(evidence_paths, BUSTER_ARRAY_LENGTH(evidence_paths), S8("child.stdout"));
    errors += d_evidence_path_insert(evidence_paths, BUSTER_ARRAY_LENGTH(evidence_paths), S8("child.stdout"));
    bool evidence_path_valid = false;
    d_evidence_relative_path(S8("case"), S8("case/../escape"), &evidence_path_valid);
    errors += evidence_path_valid;
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
    u32 strict_rows = 0;
    for (u32 index = 0; index < BUSTER_MIN(count, BUSTER_ARRAY_LENGTH(matrix)); index += 1) { strict_rows += d_mir_config(matrix[index]); }
    errors += strict_rows != (BUSTER_ARRAY_LENGTH(d_allocators) - 2) * BUSTER_ARRAY_LENGTH(d_optimizations) * 8;
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
    String8 caller_library_paths[] = {S8("first library"), S8("second-library")};
    DSettings caller_settings = {.arena = arena, .cc = S8("compiler with spaces"), .include = S8("global include"),
                                 .library_paths = (SliceString8)BUSTER_ARRAY_TO_SLICE(caller_library_paths)};
    DCase caller_case = {.host = S8("fixed caller.c"), .include = S8("source include")};
    for (u32 sanitize = 0; sanitize < 2; sanitize += 1)
    {
        caller_settings.sanitize_oracle = sanitize != 0;
        String8 argv[40];
        u64 argc = d_caller_arguments(&caller_settings, caller_case, true, false, argv);
        String8 expected[] = {S8("compiler with spaces"), S8("-O0"), S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char")};
        errors += argc != 9 + sanitize * 2;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1) { errors += !string_equal(argv[index], expected[index]); }
        if (sanitize)
        {
            errors += !string_equal(argv[5], S8("-fsanitize=address,undefined"));
            errors += !string_equal(argv[6], S8("-fno-sanitize-recover=all"));
        }
        errors += !string_equal(argv[argc - 4], S8("-Isource include"));
        errors += !string_equal(argv[argc - 3], S8("-Iglobal include"));
        errors += !string_equal(argv[argc - 2], S8("-Lfirst library"));
        errors += !string_equal(argv[argc - 1], S8("-Lsecond-library"));
        argc = d_caller_arguments(&caller_settings, caller_case, false, false, argv);
        errors += argc != 3 + sanitize * 2 || !string_equal(argv[0], caller_settings.cc);
        if (sanitize)
        {
            errors += !string_equal(argv[1], S8("-fsanitize=address,undefined"));
            errors += !string_equal(argv[2], S8("-fno-sanitize-recover=all"));
        }
        errors += !string_equal(argv[argc - 2], S8("-Lfirst library"));
        errors += !string_equal(argv[argc - 1], S8("-Lsecond-library"));
    }
    DSettings msvc_settings = caller_settings;
    msvc_settings.reference_dialect = D_REFERENCE_MSVC;
    msvc_settings.sanitize_oracle = false;
    errors += !d_reference_capabilities(&msvc_settings, true, true);
    errors += d_reference_capabilities(&msvc_settings, false, true);
    errors += d_reference_capabilities(&msvc_settings, true, false);
    msvc_settings.sanitize_oracle = true;
    errors += d_reference_capabilities(&msvc_settings, true, true);
    msvc_settings.sanitize_oracle = false;
    msvc_settings.reduce_limit = 1;
    errors += d_reference_capabilities(&msvc_settings, true, true);
    msvc_settings.reduce_limit = 0;
    {
        String8 argv[40];
        String8 compile_expected[] = {S8("compiler with spaces"), S8("/nologo"), S8("/std:c11"), S8("/TC"),
            S8("/J"), S8("/Od"), S8("/Isource include"), S8("/Iglobal include"), S8("fixed caller.c"),
            S8("/c"), S8("/Foobject path/caller.obj")};
        u64 argc = d_reference_compile_arguments(&msvc_settings, caller_case, caller_case.host,
            S8("object path/caller.obj"), false, argv);
        errors += argc != BUSTER_ARRAY_LENGTH(compile_expected);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(compile_expected); index += 1)
        {
            errors += !string_equal(argv[index], compile_expected[index]);
        }
        argc = d_reference_compile_arguments(&msvc_settings, caller_case, S8("subject path/source.c"),
            S8("object path/subject.obj"), true, argv);
        errors += argc != BUSTER_ARRAY_LENGTH(compile_expected) || !string_equal(argv[5], S8("/O2")) ||
            !string_equal(argv[8], S8("subject path/source.c")) ||
            !string_equal(argv[10], S8("/Foobject path/subject.obj"));
        String8 link_expected[] = {S8("compiler with spaces"), S8("/nologo"), S8("object path/caller.obj"),
            S8("object path/subject.obj"), S8("/Feoutput path/program.exe"), S8("/link"),
            S8("/LIBPATH:first library"), S8("/LIBPATH:second-library"), S8("legacy_stdio_definitions.lib")};
        argc = d_reference_link_arguments(&msvc_settings, caller_case, S8("object path/caller.obj"),
            S8("object path/subject.obj"), S8("output path/program.exe"), false, argv);
        errors += argc != BUSTER_ARRAY_LENGTH(link_expected);
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(link_expected); index += 1)
        {
            errors += !string_equal(argv[index], link_expected[index]);
        }
        argc = d_reference_link_arguments(&msvc_settings, caller_case, (String8){0},
            S8("object path/subject.obj"), S8("output path/program.exe"), false, argv);
        errors += argc != BUSTER_ARRAY_LENGTH(link_expected) - 1 ||
            !string_equal(argv[2], S8("object path/subject.obj"));
    }
    DObservation caller_compile = {.kind = D_EXIT};
    errors += !d_caller_ready(caller_compile, true, false);
    errors += d_caller_ready(caller_compile, false, false);
    errors += d_caller_ready(caller_compile, true, true);
    caller_compile.status = 1;
    errors += d_caller_ready(caller_compile, true, false);
    caller_compile.status = 0; caller_compile.sanitizer = true;
    errors += d_caller_ready(caller_compile, true, false);
    caller_compile.sanitizer = false;
    for (u32 kind = D_SIGNAL; kind <= D_WAIT; kind += 1)
    {
        caller_compile.kind = (DKind)kind;
        errors += d_caller_ready(caller_compile, true, false);
    }
    if (errors) { string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL pure_controls={u32}\n"), errors); }
    String8 directory = string_format_z(arena, S8("build/differential self-test%-{u64}"), os_now_microseconds());
    if (!d_create_output(arena, directory)) { errors += 1; }
    else
    {
        String8 recovering_sanitizer = {0};
        String8 modes[] = {S8("exit"), S8("sanitizer-name-stdout"), S8("sanitizer-name-stderr"),
                           S8("timeout"), S8("crash")};
        String8 sanitizer_names = d_sanitizer_name_text();
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            u32 before_child = errors;
            String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"), S8("--self-test-child"), modes[index]};
            DSettings observation_settings = settings;
            // A loaded CI host can delay even an immediately fatal child past
            // the one-second timeout control. Keep the real crash path, but do
            // not let scheduling latency misclassify it as the timeout case.
            if (string_equal(modes[index], S8("crash"))) { observation_settings.timeout_seconds = 5; }
            DObservation child = d_observe(&observation_settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv),
                                           path_join(arena, directory, modes[index]));
            if (index == 0) { errors += child.kind != D_EXIT || child.status != 7 || !string_equal(child.output, S8("a\0b")) || !string_equal(child.error, S8("child stderr\n")); }
            if (index == 1) { errors += child.kind != D_EXIT || child.status != 0 || child.sanitizer || !d_success(child) ||
                !string_equal(child.output, sanitizer_names) || child.error.length; }
            if (index == 2) { errors += child.kind != D_EXIT || child.status != 0 || child.sanitizer || !d_success(child) ||
                child.output.length || !string_equal(child.error, sanitizer_names); }
            if (index == 3) { errors += child.kind != D_TIMEOUT; }
            if (index == 4) { errors += child.kind != D_SIGNAL; }
            if (errors != before_child)
            {
                string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL child={S8} kind={u32} status={u32} raw={u32}\n"), modes[index], (u32)child.kind, child.status, child.raw_status);
            }
        }
        errors += d_sanitizer_runtime_self_test(arena, &settings, directory, &recovering_sanitizer);
#if BUSTER_LINUX || BUSTER_MACOS
        {
            u32 before_child = errors;
            String8 marker = path_join(arena, directory, S8("cancellation.pid"));
            String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"),
                             S8("--self-test-cancellation"), marker};
            DSettings cancellation_settings = settings;
            cancellation_settings.timeout_seconds = 10;
            DObservation child = d_observe(&cancellation_settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv),
                path_join(arena, directory, S8("cancellation")));
            settings.io_failed |= cancellation_settings.io_failed;
            errors += child.kind != D_SIGNAL || child.status != SIGTERM;
            if (errors != before_child)
            {
                string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL cancellation kind={u32} status={u32} raw={u32}\n"),
                    (u32)child.kind, child.status, child.raw_status);
            }
        }
#endif
        DConfig config = {.allocator = 0};
        DObservation telemetry = {.output = S8("CODEGEN_VERIFY version=1 ir=1 mir=0 scheduled=0 allocator=none\n"),
                                  .error = S8("warning\n")};
        errors += !d_verification(&settings, &telemetry, config) || telemetry.output.length ||
                  !string_equal(telemetry.error, S8("warning\n"));
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
        // A stale object plus a failed launch must never publish a caller.
        DSettings missing_caller = settings;
        missing_caller.cc = path_join(arena, directory, S8("missing-compiler"));
        String8 stale_directory = path_join(arena, directory, S8("caller"));
        make_directory_recursive(arena, stale_directory);
        String8 stale_object = path_join(arena, stale_directory, S8("caller.o"));
        d_write(&missing_caller, stale_object, S8("not an object"));
        errors += !path_exists(arena, stale_object);
        errors += d_prepare_caller(&missing_caller, caller_case, directory).length != 0;
        errors += path_exists(arena, stale_object) || missing_caller.io_failed;
        errors += d_create_output(arena, directory); // Never reuse existing output.
        if (errors) { string_print(S8("DIFFERENTIAL_SELF_TEST_FAIL before_workers={u32}\n"), errors); }
        errors += d_workers_self_test(arena, directory, recovering_sanitizer);
    }
    errors += settings.io_failed;
    string_print(S8("DIFFERENTIAL_SELF_TEST failures={u32} configurations={u32}\n"), errors, count);
    return errors;
}

BUSTER_GLOBAL_LOCAL ProcessResult differential_main(Arena* arena, SliceString8 arguments)
{
    DSettings settings = {.arena = arena, .ide = S8("build/Release/ide"), .cc = S8("clang"),
        .out = S8("build/differential"), .timeout_seconds = 10, .reduce_limit = 64, .verify = true};
    settings.library_paths.pointer = arena_allocate(arena, String8, arguments.length);
    DCase custom = {.name = S8("custom")};
    bool list = false, self_test = false, valid = true, generated_explicit = false;
    String8 child_mode = {0}, self_test_path = {0}, cancellation_test = {0};
    u32 cancellation_signal = 0;
    u32 generated = 4, seed = 1, requested_jobs = 1, jobs = 1, self_test_index = 0, self_test_count = 0;
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        String8 arg = arguments.pointer[index];
        if (string_equal(arg, S8("--no-verify"))) { settings.verify = false; }
        else if (string_equal(arg, S8("--sanitize-oracle"))) { settings.sanitize_oracle = true; }
        else if (string_equal(arg, S8("--strict-mir"))) { settings.strict_mir = true; }
        else if (string_equal(arg, S8("--list-configurations"))) { list = true; }
        else if (string_equal(arg, S8("--self-test"))) { self_test = true; }
        else if (string_equal(arg, S8("--reject"))) { custom.reject = true; }
        else if (index + 1 == arguments.length) { valid = false; }
        else
        {
            String8 value = arguments.pointer[++index];
            if (string_equal(arg, S8("--self-test-child"))) { child_mode = value; }
            else if (string_equal(arg, S8("--self-test-path"))) { self_test_path = value; }
            else if (string_equal(arg, S8("--self-test-cancellation"))) { cancellation_test = value; }
            else if (string_equal(arg, S8("--self-test-index"))) { valid &= d_number(value, &self_test_index); }
            else if (string_equal(arg, S8("--self-test-count"))) { valid &= d_number(value, &self_test_count); }
            else if (string_equal(arg, S8("--ide"))) { settings.ide = value; }
            else if (string_equal(arg, S8("--cc"))) { settings.cc = value; }
            else if (string_equal(arg, S8("--reference-dialect")))
            {
                if (string_equal(value, S8("gnu"))) { settings.reference_dialect = D_REFERENCE_GNU; }
                else if (string_equal(value, S8("msvc"))) { settings.reference_dialect = D_REFERENCE_MSVC; }
                else { valid = false; }
            }
            else if (string_equal(arg, S8("--out"))) { settings.out = value; }
            else if (string_equal(arg, S8("--source"))) { custom.source = value; }
            else if (string_equal(arg, S8("--host"))) { custom.host = value; }
            else if (string_equal(arg, S8("--include"))) { settings.include = value; }
            else if (string_equal(arg, S8("--library-path")))
            {
                valid &= value.length != 0;
                if (value.length) { settings.library_paths.pointer[settings.library_paths.length++] = value; }
            }
            else if (string_equal(arg, S8("--generated")))
            {
                generated_explicit = true;
                valid &= d_number(value, &generated) && generated <= 10000;
            }
            else if (string_equal(arg, S8("--jobs"))) { valid &= d_number(value, &requested_jobs); }
            else if (string_equal(arg, S8("--seed"))) { valid &= d_number(value, &seed); }
            else if (string_equal(arg, S8("--minimize"))) { valid &= d_number(value, &settings.reduce_limit) && settings.reduce_limit <= 10000; }
            else if (string_equal(arg, S8("--timeout"))) { valid &= d_number(value, &settings.timeout_seconds) && settings.timeout_seconds > 0 && settings.timeout_seconds <= 3600; }
            else { valid = false; }
        }
    }
    if (child_mode.length) { d_self_test_child(arena, child_mode, self_test_path, self_test_index, self_test_count); valid = false; }
    valid &= !(custom.host.length && custom.reject) && (!(custom.host.length || custom.reject) || custom.source.length);
    if (settings.reference_dialect == D_REFERENCE_MSVC && !custom.reject) { custom.require_zero = true; }
    if (settings.reference_dialect == D_REFERENCE_MSVC && generated_explicit && generated != 0)
    {
        string_print(S8("error: MSVC reference does not support requested generated cases; use --generated 0 with --source\n"));
        valid = false;
    }
    if (valid && !list && !self_test && !d_reference_capabilities(&settings, BUSTER_WINDOWS, custom.source.length != 0))
    {
        string_print(S8("error: MSVC reference requires native Windows, --source, --minimize 0, and no --sanitize-oracle; only reviewed standard-C/Windows-ABI sources are admitted\n"));
        valid = false;
    }
    valid &= d_jobs(requested_jobs, os_get_logical_thread_count(), os_get_environment_variable(S8("BUSTER_TEST_JOBS")), &jobs);
    u32 total_cases = custom.source.length ? 1 : (u32)BUSTER_ARRAY_LENGTH(d_builtin_cases) + generated;
    jobs = BUSTER_MIN(jobs, total_cases);
    u32 count = d_matrix(0, 0);
    DConfig* configs = arena_allocate(arena, DConfig, count);
    d_matrix(configs, count);
    u32 failures = 0;
    if (cancellation_test.length) { failures = d_cancellation_self_test(arena, cancellation_test); }
    else if (!valid)
    {
        string_print(S8("usage: test_differential [--ide path] [--cc compiler] [--reference-dialect gnu|msvc] [--out new-directory] [--source C-file [--host fixed-C-file] [--reject]] [--include dir] [--library-path dir]... [--generated N] [--seed N] [--minimize N] [--timeout seconds] [--jobs 1..64] [--sanitize-oracle] [--strict-mir] [--no-verify] [--list-configurations] [--self-test]\n"));
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
            settings.cc = d_reference_compiler(arena, settings.cc);
            bool compiler_ready = path_exists(arena, settings.ide) && settings.cc.length;
            if (compiler_ready && settings.reference_dialect == D_REFERENCE_MSVC)
            {
                MatrixCoverageCapability capability = {0};
                compiler_ready = matrix_coverage_probe(arena, matrix_coverage_target_current(), BUILD_COMPILER_CL,
                    settings.cc, &capability) && d_contains(capability.version, S8("Microsoft (R) C/C++ Optimizing Compiler Version"));
                if (compiler_ready)
                {
                    settings.cc_version = capability.version;
                    settings.cc_target = capability.target;
                }
                else { string_print(S8("error: MSVC reference probe failed (require cl.exe and a native-target Visual Studio developer environment)\n")); }
            }
            if (!compiler_ready || !d_create_output(arena, settings.out))
            {
                string_print(S8("error: compiler missing or output directory already exists (refusing stale evidence)\n"));
                failures = 1;
            }
            else
            {
                settings.out = os_path_absolute(arena, settings.out, true);
                String8 report = string_format_z(arena, S8("{S8}/processes.tsv"), settings.out);
                settings.report = d_file_open((char*)report.pointer, true, false);
                if (!settings.report) { failures = 1; }
                else
                {
                    DCancellationHandlers cancellation_handlers = {0};
                    bool cancellation_ready = d_cancellation_begin(&cancellation_handlers);
                    if (!cancellation_ready)
                    {
                        string_print(S8("error: failed to install differential cancellation handlers\n"));
                        failures += 1;
                    }
                    settings.io_failed |= fprintf(settings.report, "prefix\tkind_0exit_1signal_2timeout_3spawn_4wait\tstatus\traw_status\tsanitizer\telapsed_us\n") < 0;
                    String8 manifest = string_format(arena, S8("version=1\nide={S8}\ncc={S8}\nconfigurations={u32}\nseed={u32}\ngenerated={u32}\nverify={u32}\nsanitize_oracle={u32}\nstrict_mir={u32}\n"),
                        settings.ide, settings.cc, count, seed, custom.source.length ? 0 : generated,
                        (u32)settings.verify, (u32)settings.sanitize_oracle, (u32)settings.strict_mir);
                    String8 dialect = settings.reference_dialect == D_REFERENCE_MSVC ? S8("msvc") : S8("gnu");
                    String8 capabilities = settings.reference_dialect == D_REFERENCE_MSVC ?
                        S8("custom-standard-c-windows-abi;no-sanitizer;no-reduction") : S8("existing-corpus;asan-ubsan-reduction");
                    manifest = string_format(arena, S8("{S8}reference_dialect={S8}\nreference_version={S8}\nreference_target={S8}\nreference_capabilities={S8}\nenvironment_policy=inherited-with-per-child-sanitizer-report-options\ninclude={S8}\njobs_requested={u32} jobs_effective={u32} cases={u32}\n"),
                        manifest, dialect, settings.cc_version, settings.cc_target, capabilities, settings.include,
                        requested_jobs, jobs, total_cases);
                    manifest = string_format(arena, S8("{S8}library_path_count={u64}\n"), manifest, settings.library_paths.length);
                    for (u64 index = 0; index < settings.library_paths.length; index += 1)
                    {
                        manifest = string_format(arena, S8("{S8}library_path_{u64}={S8}\n"), manifest, index, settings.library_paths.pointer[index]);
                    }
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
                    if (cancellation_ready && custom.source.length)
                    {
                        custom.source = os_path_absolute(arena, custom.source, true);
                        if (custom.host.length) { custom.host = os_path_absolute(arena, custom.host, true); }
                        failures += d_cases_run(&settings, &custom, 1, configs, count, jobs);
                    }
                    else if (cancellation_ready)
                    {
                        u32 case_count = (u32)BUSTER_ARRAY_LENGTH(d_builtin_cases) + generated;
                        DCase* cases = arena_allocate(arena, DCase, case_count);
                        memcpy(cases, d_builtin_cases, sizeof(d_builtin_cases));
                        for (u32 index = 0; index < generated; index += 1)
                        {
                            seed = seed * 1664525U + 1013904223U;
                            String8 name = string_format(arena, S8("seed-{u32}"), seed);
                            String8 source = path_join(arena, settings.out, string_format(arena, S8("{S8}.c"), name));
                            // Defined unsigned wrap and masked shifts; bounded loops. Keep
                            // generation deterministic and preserve the exact source artifact.
                            String8 prefix = S8("extern int printf(const char *, ...);\nunsigned mix(unsigned x, unsigned n) {\n unsigned a=x, b=x^1234567u;\n for(unsigned i=0;i<n;i++){ unsigned t=a; a=(b+(i*17u))^(a>>3); b=(t<<5)|(t>>27); if(a&1u) b^=a; else a+=b; }\n return a^b;\n}\nint main(void) { unsigned x=mix(");
                            String8 suffix = S8("u,31u); printf(\"%u\\n\",x); return (int)(x&127u); }\n");
                            d_write(&settings, source, string_format(arena, S8("{S8}{u32}{S8}"), prefix, seed, suffix));
                            cases[BUSTER_ARRAY_LENGTH(d_builtin_cases) + index] = (DCase){.name = name, .source = source};
                        }
                        failures += d_cases_run(&settings, cases, case_count, configs, count, jobs);
                    }
                    settings.io_failed |= fclose(settings.report) != 0;
                    d_write(&settings, path_join(arena, settings.out, S8("summary.txt")),
                        string_format(arena, S8("version=1 failures={u32} io_failed={u32} configurations={u32}\n"), failures, (u32)settings.io_failed, count));
                    cancellation_signal = d_cancellation_end(&cancellation_handlers);
                }
            }
        }
    }
    failures += settings.io_failed;
    string_print(S8("DIFFERENTIAL_SUMMARY failures={u32} configurations={u32}\n"), failures, count);
    if (cancellation_signal)
    {
        fflush(0);
        raise((int)cancellation_signal);
        failures = 1;
    }
    return failures ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
