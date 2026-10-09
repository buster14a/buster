// Build-driver-only Clang analysis, included by build.c after its compile-command
// parser; this is not a compiler module. clang_analyze_plan owns selected row
// identity and deterministic same-run representatives; clang_analyze_context_proof
// proves immutable input/context for eligible groups; clang_analyze_worker owns
// one shard and materializes every row's result/log; clang_analyze_aggregate
// reconstructs the plan and requires one terminal disposition per selected TU.
// clang_analyze_main exposes ordinary execution, prepare/worker/aggregate modes,
// and the opt-in two/four-worker qualification campaign. The ordinary scheduler
// uses clang_analyze_schedule_shard to admit expensive shards first. Results are
// fresh-run evidence, never a cross-run analysis cache. clang_analyze_self_test
// opens each negative control with clang_analyze_test_begin, which sets
// clang_analyze_expecting_rejection; clang_analyze_error_prefix and
// clang_analyze_status_qualifier mark diagnostics as expected until
// clang_analyze_test_check prints the verdict and closes the scope.

#define BUSTER_ANALYZE_DEFAULT_SHARDS 8
#define BUSTER_ANALYZE_DEFAULT_JOBS 2
#define BUSTER_ANALYZE_MAX_SHARDS 256
#define BUSTER_ANALYZE_TIMEOUT_SECONDS 600
#define BUSTER_ANALYZE_PLAN_VERSION "BUSTER_CLANG_ANALYZE_PLAN_V2\n"
#define BUSTER_ANALYZE_RESULT_VERSION "BUSTER_CLANG_ANALYZE_RESULT_V2\n"
#define BUSTER_ANALYZE_QUALIFICATION_SAMPLES 4
#define BUSTER_ANALYZE_PROCESS_TREE_MAX_PIDS 4096
#define BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS 8192
#define BUSTER_ANALYZE_MAX_SNAPSHOT_ENTRIES 1000000
#define BUSTER_ANALYZE_MAX_COMPILER_LINKS 64

#if BUSTER_LINUX
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
extern char** environ;
#endif

typedef struct ClangAnalyzeOptions ClangAnalyzeOptions;
struct ClangAnalyzeOptions
{
    String8 database;
    String8 config;
    String8 clang;
    String8 results;
    u64 shards;
    u64 jobs;
    u64 shard;
    u64 timeout;
    bool quiet;
    bool prepare;
    bool aggregate;
    bool worker;
    bool self_test;
    bool qualify_workers;
    bool expect_rejection;
    bool fixture_compiler;
    String8* run_record;
};

typedef struct ClangAnalyzeUnit ClangAnalyzeUnit;
struct ClangAnalyzeUnit
{
    CompileCommandEntry entry;
    SliceString8 command;
    String8 module;
    u64 shard;
    u64 representative;
    String8 ineligible_reason;
    String8 compiler_path;
    String8 compiler_path_fingerprint;
    String8 compiler_resolved_path;
    String8 runtime_ldd_path;
    String8 runtime_fingerprint;
    String8 context_proof;
    String8 environment_context;
    String8 preprocessing_fingerprint;
    String8 driver_fingerprint;
    String8* input_paths;
    String8* input_fingerprints;
    String8* input_metadata;
    bool* input_content_rechecked;
    u64 input_count;
    u64 input_capacity;
    String8* search_paths;
    String8* search_fingerprints;
    u64* search_entry_counts;
    u64 search_count;
    u64 search_capacity;
};

typedef struct ClangAnalyzePlan ClangAnalyzePlan;
struct ClangAnalyzePlan
{
    ClangAnalyzeUnit* units;
    u64 count;
    u64 excluded;
    u64 unique_executions;
    u64 aliased_rows;
    String8 manifest;
    String8 fingerprint;
    u64 candidate_groups;
    u64 proven_groups;
    u64 context_proof_us;
    bool fixture_compiler;
};

typedef struct ClangAnalyzeFileFingerprint ClangAnalyzeFileFingerprint;
struct ClangAnalyzeFileFingerprint
{
    String8 path;
    String8 fingerprint;
    String8 metadata;
};

typedef struct ClangAnalyzePlanContext ClangAnalyzePlanContext;
struct ClangAnalyzePlanContext
{
    ClangAnalyzeFileFingerprint* files;
    u64 file_count;
    u64 file_capacity;
    String8* search_paths;
    String8* search_fingerprints;
    u64* search_entry_counts;
    u64 search_count;
    u64 search_capacity;
};

typedef struct ClangAnalyzeDirectoryFingerprint ClangAnalyzeDirectoryFingerprint;
struct ClangAnalyzeDirectoryFingerprint
{
    String8 path;
    String8 fingerprint;
    u64 entry_count;
};

typedef struct ClangAnalyzeContextCheckCache ClangAnalyzeContextCheckCache;
struct ClangAnalyzeContextCheckCache
{
    ClangAnalyzeFileFingerprint* files;
    u64 file_count;
    u64 file_capacity;
    ClangAnalyzeDirectoryFingerprint* directories;
    u64 directory_count;
    u64 directory_capacity;
};

typedef enum ClangAnalyzeStatus
{
    CLANG_ANALYZE_PASS,
    CLANG_ANALYZE_WARNING,
    CLANG_ANALYZE_FAILURE,
    CLANG_ANALYZE_CRASH,
    CLANG_ANALYZE_TIMEOUT,
    CLANG_ANALYZE_LAUNCH,
    CLANG_ANALYZE_LOG_FAILURE,
    CLANG_ANALYZE_CONTEXT_FAILURE,
    CLANG_ANALYZE_STATUS_COUNT,
} ClangAnalyzeStatus;

BUSTER_GLOBAL_LOCAL ProcessSpawnResult clang_analyze_spawn(Arena* arena, SliceString8 command, String8 directory);

BUSTER_GLOBAL_LOCAL String8 clang_analyze_status_name(u64 status)
{
    String8 names[] = {S8("pass"), S8("warning"), S8("failure"), S8("crash"), S8("timeout"), S8("launch-failure"), S8("log-failure"), S8("context-failure")};
    String8 result = status < BUSTER_ARRAY_LENGTH(names) ? names[status] : S8("invalid-result");
    return result;
}

// Self-test scope. True only while a self-test check that expects rejection
// runs, or in a worker child launched by one (--expect-rejection). Its
// diagnostics then print as expected-error: and failed status records carry
// expected=1. Only the text changes: results, evidence and exit codes do not.
// Real runs never set it, so their failures keep plain error:/status=fail.
BUSTER_GLOBAL_LOCAL bool clang_analyze_expecting_rejection = false;

BUSTER_GLOBAL_LOCAL String8 clang_analyze_error_prefix(void)
{
    String8 result = clang_analyze_expecting_rejection ? S8("expected-error:") : S8("error:");
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_status_qualifier(bool failed)
{
    String8 result = failed && clang_analyze_expecting_rejection ? S8(" expected=1") : S8("");
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_read(Arena* arena, String8 path)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){.end_padding = 1});
    String8 result = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_write(Arena* arena, String8 path, String8 text)
{
    // Read back evidence before publishing success, including short writes/close
    // failures not distinguished by older bootstrap file helpers.
    bool result = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
    if (result)
    {
        result = string_equal(text, clang_analyze_read(arena, path));
    }
    if (!result)
    {
        string_print(S8("{S8} analyzer evidence write failed: {S8}\n"), clang_analyze_error_prefix(), path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_try_new_directory(Arena* arena, String8 path)
{
    bool result;
#if BUSTER_WINDOWS
    String16 wide = string16_from_string8(arena, path, true);
    result = CreateDirectoryW(wide.pointer, 0) != 0;
#else
    String8 terminated = string_duplicate_arena(arena, path, true);
    result = mkdir(terminated.pointer, 0700) == 0;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_new_directory(Arena* arena, String8 path)
{
    bool result = clang_analyze_try_new_directory(arena, path);
    if (!result)
    {
        string_print(S8("{S8} analyzer requires a fresh directory (or shard already claimed): {S8}\n"), clang_analyze_error_prefix(), path);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_claim_unique_directory(Arena* arena, String8 prefix, u64 seed, String8* claimed_path)
{
    bool claimed = false;
    for (u64 attempt = 0; !claimed && attempt < 64; attempt += 1)
    {
        String8 relative = string_format(arena, S8("{S8}-{u64}-{u64}"), prefix, seed, attempt);
        String8 candidate = os_path_absolute_lexical(arena, relative, true);
        claimed = candidate.length && clang_analyze_try_new_directory(arena, candidate);
        if (claimed) *claimed_path = candidate;
    }
    return claimed;
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_child_peak_rss(void)
{
    u64 result = 0;
#if BUSTER_LINUX || BUSTER_APPLE
    struct rusage usage = {0};
    if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
    {
        result = (u64)usage.ru_maxrss;
#if BUSTER_LINUX
        result *= 1024;
#endif
    }
#endif
    // Zero means unavailable (Windows), not zero memory consumption. POSIX
    // reports the largest child's high water, not simultaneous process-tree RSS.
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_sha256(Arena* arena, String8 text)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, text.pointer, text.length);
    char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, digits);
    String8 result = {.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_module(String8 file)
{
    // A source basename is the stable module key; its paired *_test TU follows
    // the same owner. No filesystem discovery or reconstructed compile flags.
    u64 start = 0;
    for (u64 i = 0; i < file.length; i += 1)
    {
        if (file.pointer[i] == '/' || file.pointer[i] == '\\')
        {
            start = i + 1;
        }
    }
    String8 result = string_slice(file, start, file.length - 2);
    if (string_ends_with_sequence(result, S8("_test")))
    {
        result.length -= 5;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_module_shard(String8 module, u64 shards)
{
    // Fixed byte-order-independent FNV-1a, unaffected by database enumeration.
    u64 hash = 14695981039346656037ull;
    for (u64 i = 0; i < module.length; i += 1)
    {
        hash = (hash ^ (u8)module.pointer[i]) * 1099511628211ull;
    }
    return hash % shards;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_command_accounts_for_source(Arena* arena, CompileCommandEntry entry, SliceString8 arguments)
{
    bool valid = true;
    bool found = false;
    String8 source = build_relative_path(arena, entry.directory, entry.file);
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        for (u64 c = 0; valid && c < argument.length; c += 1) valid = argument.pointer[c] != 0;
        // Response files could reintroduce -c/-o after the analyzer projection.
        // Refuse them explicitly instead of accepting an unaccounted action.
        valid = valid && !(argument.length && argument.pointer[0] == '@');
        if (argument.length && argument.pointer[0] != '-' && clang_analyze_is_c_source(argument))
        {
            String8 candidate = build_relative_path(arena, entry.directory, argument);
            found = found || build_artifact_fanout_path_equal(source, candidate);
        }
    }
    valid = valid && found;
    if (!valid)
    {
        string_print(S8("{S8} analyzer command must explicitly name its source and contain no response files or NUL bytes: {S8}\n"), clang_analyze_error_prefix(), entry.file);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_contains(String8 text, String8 needle)
{
    bool result = false;
    for (u64 i = 0; !result && i + needle.length <= text.length; i += 1)
    {
        result = memcmp(text.pointer + i, needle.pointer, needle.length) == 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_dynamic_time_input(String8 text)
{
    String8 builtins[] = {S8("__DATE__"), S8("__TIME__"), S8("__TIMESTAMP__")};
    bool result = false;
    for (u64 i = 0; !result && i < BUSTER_ARRAY_LENGTH(builtins); i += 1)
    {
        result = clang_analyze_contains(text, builtins[i]);
    }
    // Token pasting can synthesize a time builtin without spelling it in the
    // source (CAT(__DA, TE__) is one example). Translation-phase line splices,
    // trigraph backslashes, and the %:%: digraph can hide the same construct.
    // We conservatively keep any such input on the per-row path.
    result = result || clang_analyze_contains(text, S8("##")) || clang_analyze_contains(text, S8("%:%:")) ||
             clang_analyze_contains(text, S8("?" "?/"));
    for (u64 i = 0; !result && i < text.length; i += 1)
    {
        if (text.pointer[i] == '\\' && i + 1 < text.length)
        {
            char8 next = text.pointer[i + 1];
            result = next == '\n' || next == '\r' || next == 'u' || next == 'U';
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_plugin_or_mutable_input(SliceString8 arguments)
{
    String8 prefixes[] = {
        S8("-fplugin"), S8("-fpass-plugin"), S8("-Xclang"), S8("-Xanalyzer"), S8("-load"), S8("-plugin"), S8("-add-plugin"),
        S8("-analyzer-config"),
        S8("-fmodules"), S8("-fmodule"), S8("-fimplicit-modules"), S8("-fmodule-map-file"), S8("-fprebuilt-module-path"),
        S8("-ivfsoverlay"), S8("-vfsoverlay"),
        S8("-include-pch"), S8("-fpch"), S8("-fprofile"), S8("-fcoverage"), S8("--coverage"), S8("-coverage"),
        S8("--config"), S8("-specs="), S8("-save-temps"), S8("-ftime-trace"), S8("-serialize-diagnostics"),
    };
    bool result = false;
    for (u64 i = 1; !result && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        for (u64 p = 0; !result && p < BUSTER_ARRAY_LENGTH(prefixes); p += 1)
        {
            result = string_starts_with_sequence(argument, prefixes[p]);
        }
        if (!result) result = clang_analyze_contains(argument, S8("-analyzer-config"));
        if (!result && string_equal(argument, S8("-Xclang")) && i + 1 < arguments.length)
        {
            String8 next = arguments.pointer[i + 1];
            result = string_equal(next, S8("-load")) || string_equal(next, S8("-plugin")) || string_equal(next, S8("-add-plugin")) ||
                     string_starts_with_sequence(next, S8("-plugin-arg-"));
        }
        if (!result && string_equal(argument, S8("-mllvm")))
        {
            result = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_string_slice_equal(SliceString8 left, SliceString8 right)
{
    bool result = left.length == right.length;
    for (u64 i = 0; result && i < left.length; i += 1)
    {
        result = string_equal(left.pointer[i], right.pointer[i]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_same_invocation(ClangAnalyzeUnit left, ClangAnalyzeUnit right)
{
    bool result = string_equal(left.entry.directory, right.entry.directory) && string_equal(left.entry.file, right.entry.file) &&
                  left.shard == right.shard && clang_analyze_string_slice_equal(left.command, right.command);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_identity_less(ClangAnalyzeUnit left, ClangAnalyzeUnit right)
{
    bool result = false;
    if (!string_equal(left.entry.directory, right.entry.directory))
    {
        u64 limit = BUSTER_MIN(left.entry.directory.length, right.entry.directory.length);
        int order = limit ? memcmp(left.entry.directory.pointer, right.entry.directory.pointer, limit) : 0;
        result = order < 0 || (order == 0 && left.entry.directory.length < right.entry.directory.length);
    }
    else if (!string_equal(left.entry.file, right.entry.file))
    {
        u64 limit = BUSTER_MIN(left.entry.file.length, right.entry.file.length);
        int order = limit ? memcmp(left.entry.file.pointer, right.entry.file.pointer, limit) : 0;
        result = order < 0 || (order == 0 && left.entry.file.length < right.entry.file.length);
    }
    else
    {
        u64 limit = BUSTER_MIN(left.entry.output.length, right.entry.output.length);
        int order = limit ? memcmp(left.entry.output.pointer, right.entry.output.pointer, limit) : 0;
        result = order < 0 || (order == 0 && left.entry.output.length < right.entry.output.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_absolute_from(Arena* arena, String8 directory, String8 path)
{
    String8 combined = path_is_absolute(path) ? path : path_join(arena, directory, path);
    String8 result = os_path_absolute_lexical(arena, combined, true);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_file_metadata_fingerprint(Arena* arena, String8 path, String8* fingerprint)
{
    bool result = false;
#if BUSTER_LINUX
    TemporalArena temporary = scratch_begin(&arena, 1);
    Arena* scratch = temporary.arena;
    String8 path_z = string_duplicate_arena(scratch, path, true);
    struct stat info = {0};
    result = path.length && stat(path_z.pointer, &info) == 0 && S_ISREG(info.st_mode);
    if (result)
    {
        u64 values[] = {(u64)info.st_dev, (u64)info.st_ino, (u64)info.st_size, (u64)info.st_mode,
            (u64)info.st_mtim.tv_sec, (u64)info.st_mtim.tv_nsec, (u64)info.st_ctim.tv_sec, (u64)info.st_ctim.tv_nsec};
        String8 bytes = {.pointer = (char8*)values, .length = sizeof(values)};
        *fingerprint = clang_analyze_sha256(arena, bytes);
    }
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
    BUSTER_UNUSED(fingerprint);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_file_fingerprint(Arena* arena, String8 path, String8* fingerprint, String8* metadata)
{
    String8 before = {0};
    String8 after = {0};
    bool result = clang_analyze_file_metadata_fingerprint(arena, path, &before);
    if (result)
    {
#if BUSTER_LINUX
        String8 path_z = string_duplicate_arena(arena, path, true);
        int descriptor = open(path_z.pointer, O_RDONLY | O_CLOEXEC, 0);
        struct stat opened_before = {0};
        result = descriptor >= 0 && fstat(descriptor, &opened_before) == 0 && S_ISREG(opened_before.st_mode);
        u64 opened_values[] = {(u64)opened_before.st_dev, (u64)opened_before.st_ino, (u64)opened_before.st_size,
            (u64)opened_before.st_mode, (u64)opened_before.st_mtim.tv_sec, (u64)opened_before.st_mtim.tv_nsec,
            (u64)opened_before.st_ctim.tv_sec, (u64)opened_before.st_ctim.tv_nsec};
        String8 opened_metadata = {0};
        if (result)
        {
            opened_metadata = clang_analyze_sha256(arena, (String8){.pointer = (char8*)opened_values, .length = sizeof(opened_values)});
            result = string_equal(opened_metadata, before);
        }
        Sha256 content_hash;
        sha256_init(&content_hash);
        char8 buffer[64 * 1024];
        while (result)
        {
            ssize_t amount = read(descriptor, buffer, sizeof(buffer));
            if (amount > 0)
            {
                sha256_add(&content_hash, buffer, (u64)amount);
            }
            else if (amount == 0)
            {
                break;
            }
            else if (errno != EINTR)
            {
                result = false;
            }
        }
        struct stat opened_after = {0};
        result = result && fstat(descriptor, &opened_after) == 0;
        if (result)
        {
            u64 after_values[] = {(u64)opened_after.st_dev, (u64)opened_after.st_ino, (u64)opened_after.st_size,
                (u64)opened_after.st_mode, (u64)opened_after.st_mtim.tv_sec, (u64)opened_after.st_mtim.tv_nsec,
                (u64)opened_after.st_ctim.tv_sec, (u64)opened_after.st_ctim.tv_nsec};
            String8 opened_after_metadata = clang_analyze_sha256(arena,
                (String8){.pointer = (char8*)after_values, .length = sizeof(after_values)});
            result = string_equal(opened_metadata, opened_after_metadata);
        }
        if (descriptor >= 0) result = close(descriptor) == 0 && result;
        if (result)
        {
            char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
            sha256_finish_hex(&content_hash, digits);
            *fingerprint = (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
        }
#else
        result = false;
#endif
        result = result && clang_analyze_file_metadata_fingerprint(arena, path, &after) && string_equal(before, after);
        if (result) *metadata = after;
    }
    return result;
}

#if BUSTER_LINUX
typedef struct ClangAnalyzeVisitedDirectory ClangAnalyzeVisitedDirectory;
struct ClangAnalyzeVisitedDirectory
{
    u64 hash;
    dev_t device;
    ino_t inode;
    bool occupied;
};

BUSTER_GLOBAL_LOCAL u64 clang_analyze_directory_identity_hash(dev_t device, ino_t inode)
{
    u64 result = 14695981039346656037ull;
    result = (result ^ (u64)device) * 1099511628211ull;
    result = (result ^ (u64)inode) * 1099511628211ull;
    return result ? result : 1;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_hash_stat(Sha256* hash, struct stat info)
{
    u64 values[] = {(u64)info.st_dev, (u64)info.st_ino, (u64)info.st_size, (u64)info.st_mode,
        (u64)info.st_mtim.tv_sec, (u64)info.st_mtim.tv_nsec, (u64)info.st_ctim.tv_sec, (u64)info.st_ctim.tv_nsec};
    sha256_add(hash, values, sizeof(values));
}

BUSTER_GLOBAL_LOCAL void clang_analyze_hash_resolution_stat(Sha256* hash, struct stat info)
{
    if (S_ISDIR(info.st_mode))
    {
        // Ancestor directory membership is not part of compiler path identity:
        // creating a result beside the compiler must not invalidate a plan.
        // Keep the identity and access bits which govern traversal instead.
        u64 values[] = {(u64)info.st_dev, (u64)info.st_ino, (u64)info.st_mode,
            (u64)info.st_uid, (u64)info.st_gid};
        sha256_add(hash, values, sizeof(values));
    }
    else
    {
        clang_analyze_hash_stat(hash, info);
    }
}

BUSTER_GLOBAL_LOCAL void clang_analyze_hash_string(Sha256* hash, String8 text)
{
    sha256_add(hash, &text.length, sizeof(text.length));
    sha256_add(hash, text.pointer, text.length);
}

BUSTER_GLOBAL_LOCAL int clang_analyze_string8_compare(String8 left, String8 right)
{
    u64 limit = BUSTER_MIN(left.length, right.length);
    int result = limit ? memcmp(left.pointer, right.pointer, limit) : 0;
    if (!result && left.length != right.length) result = left.length < right.length ? -1 : 1;
    return result;
}

BUSTER_GLOBAL_LOCAL String8* clang_analyze_sort_directory_names(Arena* arena, String8* names, u64 count)
{
    String8* result = names;
    if (count > 1)
    {
        String8* temporary = arena_allocate(arena, String8, count);
        String8* source = names;
        String8* destination = temporary;
        for (u64 width = 1; width < count; width *= 2)
        {
            for (u64 start = 0; start < count; start += width * 2)
            {
                u64 middle = BUSTER_MIN(start + width, count);
                u64 end = BUSTER_MIN(start + width * 2, count);
                u64 left = start;
                u64 right = middle;
                for (u64 at = start; at < end; at += 1)
                {
                    if (left < middle && (right >= end || clang_analyze_string8_compare(source[left], source[right]) <= 0))
                    {
                        destination[at] = source[left++];
                    }
                    else
                    {
                        destination[at] = source[right++];
                    }
                }
            }
            String8* swap = source;
            source = destination;
            destination = swap;
        }
        result = source;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_visited_insert(ClangAnalyzeVisitedDirectory* slots, u64 capacity, u64 hash, dev_t device, ino_t inode)
{
    u64 slot = hash % capacity;
    while (slots[slot].occupied && (slots[slot].device != device || slots[slot].inode != inode))
    {
        slot = (slot + 1) % capacity;
    }
    slots[slot] = (ClangAnalyzeVisitedDirectory){.hash = hash, .device = device, .inode = inode, .occupied = true};
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_visited_contains(ClangAnalyzeVisitedDirectory* slots, u64 capacity, u64 hash, dev_t device, ino_t inode)
{
    u64 slot = hash % capacity;
    bool found = false;
    while (!found && slots[slot].occupied)
    {
        found = slots[slot].device == device && slots[slot].inode == inode;
        slot = (slot + 1) % capacity;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_snapshot_directory(Arena* arena, String8 path, String8* fingerprint, u64* entry_count)
{
    TemporalArena temporary = scratch_begin(&arena, 1);
    Arena* scratch = temporary.arena;
    Sha256 hash;
    sha256_init(&hash);
    clang_analyze_hash_string(&hash, path);
    u64 capacity = 128;
    String8* pending = arena_allocate(scratch, String8, capacity);
    u64 pending_count = 0;
    u64 visited_capacity = 2048;
    ClangAnalyzeVisitedDirectory* visited = arena_allocate(scratch, ClangAnalyzeVisitedDirectory, visited_capacity);
    memset(visited, 0, visited_capacity * sizeof(*visited));
    u64 visited_count = 0;
    u64 entries = 0;
    bool valid = true;
    struct stat root_info = {0};
    String8 root_z = string_duplicate_arena(scratch, path, true);
    errno = 0;
    struct stat root_link_info = {0};
    if (lstat(root_z.pointer, &root_link_info) == 0 && S_ISLNK(root_link_info.st_mode))
    {
        u64 kind = (u64)(root_link_info.st_mode & S_IFMT);
        sha256_add(&hash, &kind, sizeof(kind));
        char target[4096];
        ssize_t target_length = readlink(root_z.pointer, target, sizeof(target));
        valid = target_length >= 0 && (u64)target_length < sizeof(target);
        if (valid) clang_analyze_hash_string(&hash, (String8){.pointer = (char8*)target, .length = (u64)target_length});
    }
    else if (errno != 0 && errno != ENOENT && errno != ENOTDIR)
    {
        valid = false;
    }
    errno = 0;
    if (stat(root_z.pointer, &root_info) == 0 && S_ISDIR(root_info.st_mode))
    {
        clang_analyze_hash_stat(&hash, root_info);
        pending[pending_count++] = path;
        while (valid && pending_count)
        {
            String8 directory = pending[--pending_count];
            String8 directory_z = string_duplicate_arena(scratch, directory, true);
            struct stat directory_info = {0};
            if (stat(directory_z.pointer, &directory_info) != 0 || !S_ISDIR(directory_info.st_mode))
            {
                valid = false;
            }
            else
            {
                clang_analyze_hash_stat(&hash, directory_info);
                u64 identity = clang_analyze_directory_identity_hash(directory_info.st_dev, directory_info.st_ino);
                if (!clang_analyze_visited_contains(visited, visited_capacity, identity, directory_info.st_dev, directory_info.st_ino))
                {
                    if (visited_count * 2 >= visited_capacity)
                    {
                        u64 new_capacity = visited_capacity * 2;
                        ClangAnalyzeVisitedDirectory* grown = arena_allocate(scratch, ClangAnalyzeVisitedDirectory, new_capacity);
                        memset(grown, 0, new_capacity * sizeof(*grown));
                        for (u64 i = 0; i < visited_capacity; i += 1)
                        {
                            if (visited[i].occupied) clang_analyze_visited_insert(grown, new_capacity, visited[i].hash, visited[i].device, visited[i].inode);
                        }
                        visited = grown;
                        visited_capacity = new_capacity;
                    }
                    clang_analyze_visited_insert(visited, visited_capacity, identity, directory_info.st_dev, directory_info.st_ino);
                    visited_count += 1;
                    DIR* stream = opendir(directory_z.pointer);
                    if (!stream)
                    {
                        valid = false;
                    }
                    else
                    {
                        u64 names_capacity = 64;
                        u64 names_count = 0;
                        String8* names = arena_allocate(scratch, String8, names_capacity);
                        errno = 0;
                        struct dirent* item = readdir(stream);
                        while (item)
                        {
                            String8 name = {.pointer = (char8*)item->d_name, .length = strlen(item->d_name)};
                            if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
                            {
                                entries += 1;
                                valid = entries <= BUSTER_ANALYZE_MAX_SNAPSHOT_ENTRIES;
                                if (valid)
                                {
                                    if (names_count == names_capacity)
                                    {
                                        u64 new_capacity = names_capacity * 2;
                                        String8* grown = arena_allocate(scratch, String8, new_capacity);
                                        memcpy(grown, names, names_count * sizeof(*grown));
                                        names = grown;
                                        names_capacity = new_capacity;
                                    }
                                    names[names_count++] = string_duplicate_arena(scratch, name, true);
                                }
                            }
                            errno = 0;
                            item = readdir(stream);
                        }
                        valid = valid && errno == 0;
                        valid = closedir(stream) == 0 && valid;
                        names = clang_analyze_sort_directory_names(scratch, names, names_count);
                        for (u64 name_index = 0; valid && name_index < names_count; name_index += 1)
                        {
                            String8 name = names[name_index];
                            char8 item_path_bytes[4096];
                            bool separator = directory.length && directory.pointer[directory.length - 1] != '/' && directory.pointer[directory.length - 1] != '\\';
                            u64 item_path_length = directory.length + (u64)separator + name.length;
                            valid = item_path_length < sizeof(item_path_bytes);
                            String8 item_path = {0};
                            if (valid)
                            {
                                memcpy(item_path_bytes, directory.pointer, directory.length);
                                u64 at = directory.length;
                                if (separator) item_path_bytes[at++] = '/';
                                memcpy(item_path_bytes + at, name.pointer, name.length);
                                item_path_bytes[item_path_length] = 0;
                                item_path = (String8){.pointer = item_path_bytes, .length = item_path_length};
                            }
                            struct stat item_info = {0};
                            valid = valid && lstat(item_path_bytes, &item_info) == 0;
                            bool descend = false;
                            if (valid)
                            {
                                u64 kind = (u64)(item_info.st_mode & S_IFMT);
                                sha256_add(&hash, &kind, sizeof(kind));
                                clang_analyze_hash_string(&hash, item_path);
                                clang_analyze_hash_stat(&hash, item_info);
                                if (!S_ISDIR(item_info.st_mode) && !S_ISREG(item_info.st_mode) && !S_ISLNK(item_info.st_mode)) valid = false;
                                if (S_ISLNK(item_info.st_mode))
                                {
                                    char target[4096];
                                    ssize_t target_length = readlink(item_path_bytes, target, sizeof(target));
                                    valid = target_length >= 0 && (u64)target_length < sizeof(target);
                                    if (valid)
                                    {
                                        String8 target_string = {.pointer = (char8*)target, .length = (u64)target_length};
                                        clang_analyze_hash_string(&hash, target_string);
                                    }
                                }
                                struct stat target_info = {0};
                                descend = valid && !S_ISLNK(item_info.st_mode) && S_ISDIR(item_info.st_mode);
                                if (valid && S_ISLNK(item_info.st_mode))
                                {
                                    errno = 0;
                                    if (stat(item_path_bytes, &target_info) == 0)
                                    {
                                        u64 target_kind = (u64)(target_info.st_mode & S_IFMT);
                                        sha256_add(&hash, &target_kind, sizeof(target_kind));
                                        clang_analyze_hash_stat(&hash, target_info);
                                        if (S_ISDIR(target_info.st_mode))
                                        {
                                            descend = true;
                                        }
                                        else if (S_ISREG(target_info.st_mode))
                                        {
                                            String8 target_fingerprint = {0};
                                            String8 target_metadata = {0};
                                            valid = clang_analyze_file_fingerprint(scratch, item_path, &target_fingerprint, &target_metadata);
                                            BUSTER_UNUSED(target_metadata);
                                            if (valid)
                                            {
                                                clang_analyze_hash_string(&hash, target_fingerprint);
                                            }
                                        }
                                        else
                                        {
                                            valid = false;
                                        }
                                    }
                                    else if (errno == ENOENT || errno == ENOTDIR)
                                    {
                                        u64 missing = 0;
                                        sha256_add(&hash, &missing, sizeof(missing));
                                    }
                                    else
                                    {
                                        valid = false;
                                    }
                                }
                            }
                            if (valid && descend)
                            {
                                if (pending_count == capacity)
                                {
                                    u64 new_capacity = capacity * 2;
                                    String8* grown = arena_allocate(scratch, String8, new_capacity);
                                    memcpy(grown, pending, pending_count * sizeof(*grown));
                                    pending = grown;
                                    capacity = new_capacity;
                                }
                                pending[pending_count++] = string_duplicate_arena(scratch, item_path, true);
                            }
                        }
                    }
                }
            }
        }
        sha256_add(&hash, &entries, sizeof(entries));
    }
    else if (errno == ENOENT || errno == ENOTDIR)
    {
        u64 absent = 0;
        sha256_add(&hash, &absent, sizeof(absent));
    }
    else
    {
        valid = false;
    }
    if (valid)
    {
        char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digits);
        *fingerprint = (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
        *entry_count = entries;
    }
    scratch_end(temporary);
    return valid;
}
#endif

BUSTER_GLOBAL_LOCAL String8 clang_analyze_trim(String8 text)
{
    u64 start = 0;
    u64 end = text.length;
    while (start < end && (text.pointer[start] == ' ' || text.pointer[start] == '\t' || text.pointer[start] == '\r' || text.pointer[start] == '\n')) start += 1;
    while (end > start && (text.pointer[end - 1] == ' ' || text.pointer[end - 1] == '\t' || text.pointer[end - 1] == '\r' || text.pointer[end - 1] == '\n')) end -= 1;
    String8 result = string_slice(text, start, end);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_query(Arena* arena, SliceString8 command, String8 directory, String8* out, String8* err)
{
    ProcessSpawnResult spawn = clang_analyze_spawn(arena, command, directory);
    ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 30000000);
    bool result = spawn.handle && !wait.timed_out && wait.result == PROCESS_RESULT_SUCCESS;
    if (result)
    {
        *out = (String8){.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
        *err = (String8){.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_append_input(Arena* arena, ClangAnalyzeUnit* unit, String8 path, String8 fingerprint, String8 metadata,
                                                     bool content_rechecked)
{
    bool valid = true;
    u64 found = 0;
    for (u64 i = 0; i < unit->input_count; i += 1)
    {
        if (string_equal(unit->input_paths[i], path))
        {
            found = i + 1;
            valid = string_equal(unit->input_fingerprints[i], fingerprint) && string_equal(unit->input_metadata[i], metadata) &&
                    unit->input_content_rechecked[i] == content_rechecked;
        }
    }
    if (!found && valid)
    {
        if (unit->input_count == unit->input_capacity)
        {
            u64 capacity = unit->input_capacity ? unit->input_capacity * 2 : 16;
            String8* paths = arena_allocate(arena, String8, capacity);
            String8* fingerprints = arena_allocate(arena, String8, capacity);
            String8* metadata_fingerprints = arena_allocate(arena, String8, capacity);
            bool* content_rechecked_inputs = arena_allocate(arena, bool, capacity);
            if (unit->input_count)
            {
                memcpy(paths, unit->input_paths, unit->input_count * sizeof(*paths));
                memcpy(fingerprints, unit->input_fingerprints, unit->input_count * sizeof(*fingerprints));
                memcpy(metadata_fingerprints, unit->input_metadata, unit->input_count * sizeof(*metadata_fingerprints));
                memcpy(content_rechecked_inputs, unit->input_content_rechecked, unit->input_count * sizeof(*content_rechecked_inputs));
            }
            unit->input_paths = paths;
            unit->input_fingerprints = fingerprints;
            unit->input_metadata = metadata_fingerprints;
            unit->input_content_rechecked = content_rechecked_inputs;
            unit->input_capacity = capacity;
        }
        unit->input_paths[unit->input_count] = path;
        unit->input_fingerprints[unit->input_count] = fingerprint;
        unit->input_metadata[unit->input_count] = metadata;
        unit->input_content_rechecked[unit->input_count] = content_rechecked;
        unit->input_count += 1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_append_search(Arena* arena, ClangAnalyzeUnit* unit, String8 path, ClangAnalyzePlanContext* context)
{
    bool valid = true;
    bool found = false;
    for (u64 i = 0; i < unit->search_count; i += 1) found = found || string_equal(unit->search_paths[i], path);
    if (!found)
    {
        String8 fingerprint = {0};
        u64 entries = 0;
        u64 cached = BUSTER_STRING_NO_MATCH;
        for (u64 i = 0; i < context->search_count; i += 1)
        {
            if (string_equal(context->search_paths[i], path)) cached = i;
        }
        if (cached != BUSTER_STRING_NO_MATCH)
        {
            fingerprint = context->search_fingerprints[cached];
            entries = context->search_entry_counts[cached];
        }
        else
        {
#if BUSTER_LINUX
            valid = clang_analyze_snapshot_directory(arena, path, &fingerprint, &entries);
#else
            BUSTER_UNUSED(arena);
            BUSTER_UNUSED(path);
            valid = false;
#endif
            if (valid)
            {
                if (context->search_count == context->search_capacity)
                {
                    u64 capacity = context->search_capacity ? context->search_capacity * 2 : 16;
                    String8* paths = arena_allocate(arena, String8, capacity);
                    String8* fingerprints = arena_allocate(arena, String8, capacity);
                    u64* counts = arena_allocate(arena, u64, capacity);
                    if (context->search_count)
                    {
                        memcpy(paths, context->search_paths, context->search_count * sizeof(*paths));
                        memcpy(fingerprints, context->search_fingerprints, context->search_count * sizeof(*fingerprints));
                        memcpy(counts, context->search_entry_counts, context->search_count * sizeof(*counts));
                    }
                    context->search_paths = paths;
                    context->search_fingerprints = fingerprints;
                    context->search_entry_counts = counts;
                    context->search_capacity = capacity;
                }
                context->search_paths[context->search_count] = path;
                context->search_fingerprints[context->search_count] = fingerprint;
                context->search_entry_counts[context->search_count] = entries;
                context->search_count += 1;
            }
        }
        if (valid)
        {
            if (unit->search_count == unit->search_capacity)
            {
                u64 capacity = unit->search_capacity ? unit->search_capacity * 2 : 16;
                String8* paths = arena_allocate(arena, String8, capacity);
                String8* fingerprints = arena_allocate(arena, String8, capacity);
                u64* counts = arena_allocate(arena, u64, capacity);
                if (unit->search_count)
                {
                    memcpy(paths, unit->search_paths, unit->search_count * sizeof(*paths));
                    memcpy(fingerprints, unit->search_fingerprints, unit->search_count * sizeof(*fingerprints));
                    memcpy(counts, unit->search_entry_counts, unit->search_count * sizeof(*counts));
                }
                unit->search_paths = paths;
                unit->search_fingerprints = fingerprints;
                unit->search_entry_counts = counts;
                unit->search_capacity = capacity;
            }
            unit->search_paths[unit->search_count] = path;
            unit->search_fingerprints[unit->search_count] = fingerprint;
            unit->search_entry_counts[unit->search_count] = entries;
            unit->search_count += 1;
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_make_dependencies(Arena* arena, String8 output, bool* valid)
{
    String8List dependencies = {0};
    char8* word = arena_allocate(arena, char8, output.length + 1);
    u64 word_length = 0;
    bool after_target = false;
    bool escaped = false;
    for (u64 i = 0; *valid && i < output.length; i += 1)
    {
        char8 c = output.pointer[i];
        if (!after_target && c == ':')
        {
            after_target = true;
        }
        else if (after_target && escaped)
        {
            if (c != '\n' && c != '\r') word[word_length++] = c;
            escaped = false;
        }
        else if (after_target && c == '\\')
        {
            escaped = true;
        }
        else if (after_target && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
        {
            if (word_length)
            {
                word[word_length] = 0;
                string8_list_push(arena, &dependencies, string_duplicate_arena(arena, (String8){.pointer = word, .length = word_length}, true));
                word_length = 0;
            }
        }
        else if (after_target)
        {
            word[word_length++] = c;
        }
    }
    if (escaped) *valid = false;
    if (word_length)
    {
        word[word_length] = 0;
        string8_list_push(arena, &dependencies, string_duplicate_arena(arena, (String8){.pointer = word, .length = word_length}, true));
    }
    SliceString8 result = string8_list_to_slice(arena, dependencies);
    *valid = *valid && after_target && result.length > 0;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_search_line_path(Arena* arena, String8 line)
{
    u64 start = 0;
    u64 end = line.length;
    u64 quote_start = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; i < line.length; i += 1)
    {
        if (line.pointer[i] == '"')
        {
            if (quote_start == BUSTER_STRING_NO_MATCH) quote_start = i + 1;
            else
            {
                start = quote_start;
                end = i;
                break;
            }
        }
    }
    String8 result = start < end ? string_slice(line, start, end) : (String8){0};
    if (!result.length)
    {
        result = clang_analyze_trim(line);
        if (string_ends_with_sequence(result, S8(" (framework directory)"))) result.length -= sizeof(" (framework directory)") - 1;
    }
    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_parse_search_paths(Arena* arena, String8 directory, String8 error_output, ClangAnalyzeUnit* unit,
                                                            ClangAnalyzePlanContext* context)
{
    bool valid = true;
    bool in_search = false;
    bool saw_marker = false;
    bool ended = false;
    u64 cursor = 0;
    while (cursor < error_output.length)
    {
        u64 end = cursor;
        while (end < error_output.length && error_output.pointer[end] != '\n') end += 1;
        String8 line = clang_analyze_trim(string_slice(error_output, cursor, end));
        bool marker = string_starts_with_sequence(line, S8("#include \"...\" search starts here:")) ||
                      string_starts_with_sequence(line, S8("#include <...> search starts here:"));
        if (marker)
        {
            in_search = true;
            saw_marker = true;
        }
        else if (string_equal(line, S8("End of search list.")))
        {
            ended = true;
            in_search = false;
        }
        else if (in_search && line.length)
        {
            String8 path = clang_analyze_search_line_path(arena, line);
            path = clang_analyze_absolute_from(arena, directory, path);
            bool appended = path.length && clang_analyze_append_search(arena, unit, path, context);
            valid = appended && valid;
        }
        else if (string_starts_with_sequence(line, S8("ignoring nonexistent directory ")))
        {
            String8 path = clang_analyze_search_line_path(arena, line);
            path = clang_analyze_absolute_from(arena, directory, path);
            bool appended = path.length && clang_analyze_append_search(arena, unit, path, context);
            valid = appended && valid;
        }
        cursor = end < error_output.length ? end + 1 : end;
    }
    valid = valid && saw_marker && ended;
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_environment_context(Arena* arena, bool* valid)
{
    String8 names[] = {
        S8("PATH"), S8("HOME"), S8("USERPROFILE"), S8("XDG_CONFIG_HOME"), S8("PWD"), S8("TMPDIR"), S8("TEMP"), S8("TMP"),
        S8("LANG"), S8("LC_ALL"), S8("LC_MESSAGES"), S8("TZ"), S8("SOURCE_DATE_EPOCH"),
        S8("CPATH"), S8("C_INCLUDE_PATH"), S8("CPLUS_INCLUDE_PATH"), S8("OBJC_INCLUDE_PATH"),
        S8("SDKROOT"), S8("MACOSX_DEPLOYMENT_TARGET"), S8("INCLUDE"), S8("LIB"), S8("LIBPATH"),
        S8("COMPILER_PATH"), S8("GCC_EXEC_PREFIX"), S8("LIBRARY_PATH"),
        S8("CLANG_CONFIG_FILE"), S8("CLANG_CONFIG_FILE_USER_DIR"), S8("CLANG_CONFIG_FILE_SYSTEM_DIR"),
        S8("CLANG_RESOURCE_DIR"), S8("CLANG_MODULE_CACHE_PATH"), S8("CLANG_CACHE_PREFIX"),
        S8("CLANG_NO_DEFAULT_CONFIG"), S8("CLANG_USE_RESPONSE_FILE"), S8("CLANG_FORCE_COLOR_DIAGNOSTICS"),
        S8("CLANG_DIAGNOSTICS_SHOW_OPTION"), S8("CC"), S8("CFLAGS"), S8("CPPFLAGS"),
        S8("CCC_OVERRIDE_OPTIONS"), S8("LD_PRELOAD"), S8("LD_AUDIT"), S8("LD_LIBRARY_PATH"),
        S8("DYLD_INSERT_LIBRARIES"), S8("DYLD_LIBRARY_PATH"),
    };
    String8 rejected[] = {
        S8("CLANG_CONFIG_FILE"), S8("CLANG_CONFIG_FILE_USER_DIR"), S8("CLANG_CONFIG_FILE_SYSTEM_DIR"),
        S8("CLANG_MODULE_CACHE_PATH"), S8("CLANG_CACHE_PREFIX"),
        S8("CCC_OVERRIDE_OPTIONS"), S8("LD_PRELOAD"), S8("LD_AUDIT"), S8("LD_LIBRARY_PATH"),
        S8("DYLD_INSERT_LIBRARIES"), S8("DYLD_LIBRARY_PATH"),
    };
    String8List parts = {0};
    string8_list_push(arena, &parts, S8("BUSTER_CLANG_ANALYZE_ENVIRONMENT_V4\n"));
#if BUSTER_LINUX
    Sha256 full_environment_hash;
    sha256_init(&full_environment_hash);
    u64 environment_count = 0;
    for (char** item = environ; item && *item; item += 1)
    {
        if (!((*item)[0] == '_' && (*item)[1] == '=')) environment_count += 1;
    }
    String8* environment_items = arena_allocate(arena, String8, environment_count);
    u64 environment_index = 0;
    for (char** item = environ; item && *item; item += 1)
    {
        // Bash rewrites `_` to the last external command on each prepare,
        // worker and aggregate invocation. It has no supported Clang meaning
        // and cannot be a reproducible cross-process plan input.
        if (!((*item)[0] == '_' && (*item)[1] == '='))
        {
            environment_items[environment_index++] = (String8){.pointer = (char8*)*item, .length = strlen(*item)};
        }
    }
    environment_items = clang_analyze_sort_directory_names(arena, environment_items, environment_count);
    for (u64 i = 0; i < environment_count; i += 1)
    {
        sha256_add(&full_environment_hash, environment_items[i].pointer, environment_items[i].length + 1);
    }
    sha256_add(&full_environment_hash, &environment_count, sizeof(environment_count));
    char8* full_environment_digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&full_environment_hash, full_environment_digits);
    build_artifact_fanout_provenance_record_append_string(arena, &parts,
        (String8){.pointer = full_environment_digits, .length = SHA256_HEX_CAPACITY - 1});
    build_artifact_fanout_provenance_record_append_u64(arena, &parts, environment_count);
    for (u64 i = 0; i < environment_count; i += 1)
    {
        String8 item = environment_items[i];
        u64 equals = 0;
        while (equals < item.length && item.pointer[equals] != '=') equals += 1;
        if (equals < item.length)
        {
            String8 name = string_slice(item, 0, equals);
            String8 value = string_slice(item, equals + 1, item.length);
            build_artifact_fanout_provenance_record_append_string(arena, &parts, name);
            build_artifact_fanout_provenance_record_append_string(arena, &parts, clang_analyze_sha256(arena, value));
        }
        else
        {
            *valid = false;
        }
    }
#endif
    for (u64 i = 0; *valid && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        String8 value = os_get_environment_variable(names[i]);
        bool is_rejected = false;
        for (u64 j = 0; j < BUSTER_ARRAY_LENGTH(rejected); j += 1) is_rejected = is_rejected || string_equal(names[i], rejected[j]);
        if (is_rejected && value.length) *valid = false;
        build_artifact_fanout_provenance_record_append_string(arena, &parts, names[i]);
        build_artifact_fanout_provenance_record_append_u64(arena, &parts, value.pointer != 0);
        build_artifact_fanout_provenance_record_append_string(arena, &parts, clang_analyze_sha256(arena, value));
    }
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_native_clang_binary(Arena* arena, String8 path)
{
    bool result = false;
#if BUSTER_LINUX
    String8 path_z = string_duplicate_arena(arena, path, true);
    struct stat info = {0};
    bool regular = lstat(path_z.pointer, &info) == 0 && S_ISREG(info.st_mode);
    FILE* file = regular ? fopen(path_z.pointer, "rb") : 0;
    u8 magic[4] = {0};
    bool read = file && fread(magic, 1, sizeof(magic), file) == sizeof(magic);
    if (file) read = fclose(file) == 0 && read;
    result = regular && read && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(path);
#endif
    return result;
}

#if BUSTER_LINUX
typedef struct ClangAnalyzeCompilerLinkIdentity ClangAnalyzeCompilerLinkIdentity;
struct ClangAnalyzeCompilerLinkIdentity
{
    dev_t device;
    ino_t inode;
};

typedef struct ClangAnalyzeCompilerPathWalk ClangAnalyzeCompilerPathWalk;
struct ClangAnalyzeCompilerPathWalk
{
    Sha256* hash;
    ClangAnalyzeCompilerLinkIdentity links[BUSTER_ANALYZE_MAX_COMPILER_LINKS];
    u64 link_count;
};

BUSTER_GLOBAL_LOCAL bool clang_analyze_compiler_path_walk(Arena* arena, ClangAnalyzeCompilerPathWalk* walk, String8 path)
{
    bool valid = path_is_absolute(path) && path.length < PATH_MAX;
    BUSTER_UNUSED(arena);
    char8 pending[PATH_MAX] = {0};
    char8 prefix[PATH_MAX] = "/";
    u64 pending_length = valid ? path.length : 0;
    if (valid) memcpy(pending, path.pointer, path.length);
    u64 prefix_length = 1;
    u64 cursor = 1;
    while (valid && cursor < pending_length)
    {
        while (cursor < pending_length && pending[cursor] == '/') cursor += 1;
        u64 end = cursor;
        while (end < pending_length && pending[end] != '/') end += 1;
        bool replaced_path = false;
        if (end > cursor)
        {
            String8 component = {.pointer = pending + cursor, .length = end - cursor};
            if (string_equal(component, S8(".")))
            {
                // Ignore the current-directory component.
            }
            else if (string_equal(component, S8("..")))
            {
                while (prefix_length > 1 && prefix[prefix_length - 1] != '/') prefix_length -= 1;
                if (prefix_length > 1) prefix_length -= 1;
                prefix[prefix_length] = 0;
            }
            else
            {
                bool separator = prefix_length > 1;
                valid = prefix_length + (u64)separator + component.length < sizeof(prefix);
                if (valid)
                {
                    if (separator) prefix[prefix_length++] = '/';
                    memcpy(prefix + prefix_length, component.pointer, component.length);
                    prefix_length += component.length;
                    prefix[prefix_length] = 0;
                }
                struct stat info = {0};
                if (valid) valid = lstat(prefix, &info) == 0;
                if (valid)
                {
                    String8 prefix_path = {.pointer = prefix, .length = prefix_length};
                    clang_analyze_hash_string(walk->hash, prefix_path);
                    clang_analyze_hash_resolution_stat(walk->hash, info);
                    if (S_ISLNK(info.st_mode))
                    {
                        bool already_seen = false;
                        for (u64 i = 0; i < walk->link_count; i += 1)
                        {
                            already_seen = already_seen || (walk->links[i].device == info.st_dev && walk->links[i].inode == info.st_ino);
                        }
                        valid = !already_seen && walk->link_count < BUSTER_ANALYZE_MAX_COMPILER_LINKS;
                        char target[PATH_MAX] = {0};
                        ssize_t target_length = valid ? readlink(prefix, target, sizeof(target)) : -1;
                        valid = valid && target_length > 0 && (u64)target_length < sizeof(target);
                        if (valid)
                        {
                            walk->links[walk->link_count++] = (ClangAnalyzeCompilerLinkIdentity){.device = info.st_dev, .inode = info.st_ino};
                            String8 target_text = {.pointer = (char8*)target, .length = (u64)target_length};
                            clang_analyze_hash_string(walk->hash, target_text);
                            u64 suffix_start = end;
                            while (suffix_start < pending_length && pending[suffix_start] == '/') suffix_start += 1;
                            String8 suffix = {.pointer = pending + suffix_start, .length = pending_length - suffix_start};
                            bool absolute_target = path_is_absolute(target_text);
                            u64 parent_length = prefix_length;
                            while (parent_length > 1 && prefix[parent_length - 1] != '/') parent_length -= 1;
                            if (parent_length > 1) parent_length -= 1;
                            u64 target_prefix_length = absolute_target ? 0 : parent_length;
                            bool target_separator = target_prefix_length && prefix[target_prefix_length - 1] != '/' && target[0] != '/';
                            bool suffix_separator = suffix.length && target[target_length - 1] != '/';
                            u64 replacement_length = target_prefix_length + (u64)target_separator + (u64)target_length +
                                                     (u64)suffix_separator + suffix.length;
                            valid = replacement_length < sizeof(pending);
                            if (valid)
                            {
                                char8 replacement[PATH_MAX] = {0};
                                u64 at = 0;
                                if (target_prefix_length)
                                {
                                    memcpy(replacement, prefix, target_prefix_length);
                                    at = target_prefix_length;
                                }
                                if (target_separator) replacement[at++] = '/';
                                memcpy(replacement + at, target, target_length);
                                at += (u64)target_length;
                                if (suffix_separator) replacement[at++] = '/';
                                if (suffix.length) memcpy(replacement + at, suffix.pointer, suffix.length);
                                memcpy(pending, replacement, replacement_length);
                                pending_length = replacement_length;
                                prefix[0] = '/';
                                prefix[1] = 0;
                                prefix_length = 1;
                                cursor = 1;
                                replaced_path = true;
                            }
                        }
                    }
                    else
                    {
                        valid = S_ISDIR(info.st_mode) || S_ISREG(info.st_mode);
                    }
                }
            }
        }
        if (!replaced_path) cursor = end;
    }
    char canonical[PATH_MAX];
    char pending_z[PATH_MAX] = {0};
    if (valid)
    {
        memcpy(pending_z, pending, pending_length);
        valid = realpath(pending_z, canonical) != 0;
    }
    if (valid)
    {
        struct stat final_info = {0};
        valid = stat(canonical, &final_info) == 0 && (S_ISDIR(final_info.st_mode) || S_ISREG(final_info.st_mode));
        if (valid)
        {
            clang_analyze_hash_string(walk->hash, string_from_pointer(canonical));
            clang_analyze_hash_resolution_stat(walk->hash, final_info);
        }
    }
    return valid;
}
#endif

BUSTER_GLOBAL_LOCAL bool clang_analyze_path_lookup_is_stable(String8 compiler)
{
    bool result = true;
#if BUSTER_LINUX
    bool has_separator = path_is_absolute(compiler) || string_first_code_unit(compiler, '/') != BUSTER_STRING_NO_MATCH ||
                         string_first_code_unit(compiler, '\\') != BUSTER_STRING_NO_MATCH;
    if (!has_separator)
    {
        String8 path = os_get_environment_variable(S8("PATH"));
        result = path.length != 0;
        u64 start = 0;
        while (result && start <= path.length)
        {
            u64 end = start;
            while (end < path.length && path.pointer[end] != ':') end += 1;
            String8 entry = string_slice(path, start, end);
            result = entry.length && path_is_absolute(entry);
            start = end + 1;
        }
    }
#else
    BUSTER_UNUSED(compiler);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_plan_file_fingerprint(Arena* arena, ClangAnalyzePlanContext* context, String8 path,
                                                              String8* fingerprint, String8* metadata)
{
    bool result = true;
    u64 found = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; i < context->file_count; i += 1)
    {
        if (string_equal(context->files[i].path, path)) found = i;
    }
    if (found != BUSTER_STRING_NO_MATCH)
    {
        result = clang_analyze_file_metadata_fingerprint(arena, path, metadata) &&
                 string_equal(*metadata, context->files[found].metadata);
        if (result)
        {
            *fingerprint = context->files[found].fingerprint;
        }
    }
    else
    {
        result = clang_analyze_file_fingerprint(arena, path, fingerprint, metadata);
        if (result)
        {
            if (context->file_count == context->file_capacity)
            {
                u64 capacity = context->file_capacity ? context->file_capacity * 2 : 16;
                ClangAnalyzeFileFingerprint* files = arena_allocate(arena, ClangAnalyzeFileFingerprint, capacity);
                if (context->file_count) memcpy(files, context->files, context->file_count * sizeof(*files));
                context->files = files;
                context->file_capacity = capacity;
            }
            context->files[context->file_count++] = (ClangAnalyzeFileFingerprint){.path = path,
                .fingerprint = *fingerprint, .metadata = *metadata};
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_add_cached_file_input(Arena* arena, ClangAnalyzePlanContext* context,
                                                              ClangAnalyzeUnit* unit, String8 path)
{
    String8 fingerprint = {0};
    String8 metadata = {0};
    bool result = clang_analyze_plan_file_fingerprint(arena, context, path, &fingerprint, &metadata) &&
                  clang_analyze_append_input(arena, unit, path, fingerprint, metadata, true);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_snapshot_config_directory(Arena* arena, ClangAnalyzePlanContext* context,
                                                                  ClangAnalyzeUnit* unit, String8 directory)
{
    bool valid = directory.length && clang_analyze_append_search(arena, unit, directory, context);
#if BUSTER_LINUX
    if (valid)
    {
        String8 path_z = string_duplicate_arena(arena, directory, true);
        DIR* stream = opendir(path_z.pointer);
        if (!stream && errno != ENOENT && errno != ENOTDIR) valid = false;
        if (stream)
        {
            errno = 0;
            struct dirent* entry = readdir(stream);
            while (valid && entry)
            {
                String8 name = {.pointer = (char8*)entry->d_name, .length = strlen(entry->d_name)};
                if (string_ends_with_sequence(name, S8(".cfg")))
                {
                    String8 path = path_join(arena, directory, name);
                    struct stat info = {0};
                    String8 path_item = string_duplicate_arena(arena, path, true);
                    if (lstat(path_item.pointer, &info) != 0 || (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)))
                    {
                        valid = false;
                    }
                    else
                    {
                        valid = clang_analyze_add_cached_file_input(arena, context, unit, path);
                    }
                }
                errno = 0;
                entry = readdir(stream);
            }
            valid = valid && errno == 0;
            valid = closedir(stream) == 0 && valid;
        }
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(context);
    BUSTER_UNUSED(unit);
#endif
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_snapshot_default_config_inputs(Arena* arena, ClangAnalyzePlanContext* context,
                                                                       ClangAnalyzeUnit* unit, String8 compiler,
                                                                       String8 resource)
{
    bool valid = true;
#if BUSTER_LINUX
    String8List directories = {0};
    String8 executable_directory = path_parent(arena, compiler);
    String8 install_prefix = path_parent(arena, executable_directory);
    String8 home = os_get_environment_variable(S8("HOME"));
    String8 xdg = os_get_environment_variable(S8("XDG_CONFIG_HOME"));
    if (executable_directory.length) string8_list_push(arena, &directories, executable_directory);
    if (install_prefix.length) string8_list_push(arena, &directories, path_join(arena, install_prefix, S8("etc/clang")));
    string8_list_push(arena, &directories, S8("/etc/clang"));
    string8_list_push(arena, &directories, S8("/usr/local/etc/clang"));
    if (home.length) string8_list_push(arena, &directories, path_join(arena, home, S8(".config/clang")));
    if (xdg.length) string8_list_push(arena, &directories, path_join(arena, xdg, S8("clang")));
    if (resource.length) string8_list_push(arena, &directories, clang_analyze_absolute_from(arena, unit->entry.directory, resource));
    SliceString8 paths = string8_list_to_slice(arena, directories);
    for (u64 i = 0; valid && i < paths.length; i += 1)
    {
        String8 absolute = clang_analyze_absolute_from(arena, unit->entry.directory, paths.pointer[i]);
        valid = absolute.length && clang_analyze_snapshot_config_directory(arena, context, unit, absolute);
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(context);
    BUSTER_UNUSED(unit);
    BUSTER_UNUSED(compiler);
    BUSTER_UNUSED(resource);
    valid = false;
#endif
    return valid;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_make_dependency_command(Arena* arena, ClangAnalyzeUnit unit)
{
    String8List arguments = {0};
    string8_list_push(arena, &arguments, unit.command.pointer[0]);
    string8_list_push(arena, &arguments, S8("--analyze"));
    string8_list_push(arena, &arguments, S8("-M"));
    string8_list_push(arena, &arguments, S8("-v"));
    string8_list_push(arena, &arguments, S8("-Wno-error=unused-command-line-argument"));
    string8_list_push(arena, &arguments, S8("-MT"));
    string8_list_push(arena, &arguments, S8("buster_analyzer_snapshot"));
    for (u64 i = 1; i < unit.entry.arguments.length;)
    {
        u64 skip_count = 0;
        if (clang_analyze_skip_option(unit.entry.arguments, i, &skip_count))
        {
            i += skip_count;
        }
        else
        {
            string8_list_push(arena, &arguments, unit.entry.arguments.pointer[i]);
            i += 1;
        }
    }
    SliceString8 result = string8_list_to_slice(arena, arguments);
    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_make_dynamic_time_command(Arena* arena, ClangAnalyzeUnit unit)
{
    String8List arguments = {0};
    string8_list_push(arena, &arguments, unit.command.pointer[0]);
    string8_list_push(arena, &arguments, S8("--analyze"));
    for (u64 i = 1; i < unit.entry.arguments.length;)
    {
        u64 skip_count = 0;
        if (clang_analyze_skip_option(unit.entry.arguments, i, &skip_count))
        {
            i += skip_count;
        }
        else
        {
            string8_list_push(arena, &arguments, unit.entry.arguments.pointer[i]);
            i += 1;
        }
    }
    string8_list_push(arena, &arguments, S8("-E"));
    // A compile database can contain a broad -Werror. Keep unrelated warnings
    // from turning the proof query into a false exclusion, then make the
    // dynamic builtin warning the only warning promoted to an error.
    string8_list_push(arena, &arguments, S8("-Wno-error"));
    string8_list_push(arena, &arguments, S8("-Wdate-time"));
    string8_list_push(arena, &arguments, S8("-Werror=date-time"));
    string8_list_push(arena, &arguments, S8("-Wsystem-headers"));
    SliceString8 result = string8_list_to_slice(arena, arguments);
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_dynamic_time_warning_suppressed(String8 preprocessed)
{
    bool result = false;
    u64 cursor = 0;
    while (!result && cursor < preprocessed.length)
    {
        u64 end = cursor;
        while (end < preprocessed.length && preprocessed.pointer[end] != '\n') end += 1;
        String8 line = clang_analyze_trim(string_slice(preprocessed, cursor, end));
        bool pragma = string_starts_with_sequence(line, S8("#pragma"));
        bool diagnostic = clang_analyze_contains(line, S8("diagnostic"));
        bool date_time_warning = clang_analyze_contains(line, S8("-Wdate-time")) ||
                                 clang_analyze_contains(line, S8("-Weverything"));
        bool suppress_or_downgrade = clang_analyze_contains(line, S8("ignored")) || clang_analyze_contains(line, S8("warning"));
        bool system_header = clang_analyze_contains(line, S8("system_header"));
        result = pragma && ((diagnostic && date_time_warning && suppress_or_downgrade) || system_header);
        cursor = end < preprocessed.length ? end + 1 : end;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_dynamic_time_warning_emitted(String8 error_output)
{
    bool result = clang_analyze_contains(error_output, S8("[-Wdate-time]")) ||
                  clang_analyze_contains(error_output, S8("[-Werror,-Wdate-time]"));
    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_make_driver_expansion_command(Arena* arena, SliceString8 arguments)
{
    String8* result_arguments = arena_allocate(arena, String8, arguments.length + 1);
    if (arguments.length) memcpy(result_arguments, arguments.pointer, arguments.length * sizeof(*result_arguments));
    result_arguments[arguments.length] = S8("-###");
    return (SliceString8){.pointer = result_arguments, .length = arguments.length + 1};
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_driver_expansion_has_unsupported_option(String8 expansion)
{
    // Clang's -### prints the effective cc1 argv, including flags injected by
    // default config files. External analyzer configs, serialized ASTs,
    // profiles, modules and plugins need separate input-proof contracts.
    String8 needles[] = {S8("\"-load\""), S8("\"-plugin\""), S8("\"-add-plugin\""),
                         S8("-fplugin="), S8("-fpass-plugin="), S8("-analyzer-config"), S8("-ast-merge"),
                         S8("-include-pch"), S8("-fmodule"), S8("\"-module"), S8("-fprebuilt-module-path"),
                         S8("-fprofile"), S8("-fprofile-instr-use"), S8("-fprofile-use"), S8("-fprofile-sample-use"),
                         S8("-fprofile-remapping-file"), S8("-fprofile-instr-generate"), S8("-fprofile-generate"),
                         S8("-fcoverage-mapping"), S8("-fprofile-arcs"), S8("-ftest-coverage"), S8("-profile"), S8("-coverage"),
                         S8("-ivfsoverlay"), S8("-vfsoverlay")};
    bool found = false;
    for (u64 i = 0; !found && i < BUSTER_ARRAY_LENGTH(needles); i += 1)
    {
        found = clang_analyze_contains(expansion, needles[i]);
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_driver_expansion_disables_warnings(String8 expansion)
{
    return clang_analyze_contains(expansion, S8("\"-w\""));
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_capture_driver_expansion(Arena* arena, SliceString8 arguments, String8 directory,
                                                                  String8* expansion)
{
    String8 out = {0};
    String8 err = {0};
    SliceString8 command = clang_analyze_make_driver_expansion_command(arena, arguments);
    bool valid = clang_analyze_query(arena, command, directory, &out, &err);
    if (valid)
    {
        String8List parts = {0};
        string8_list_push(arena, &parts, S8("BUSTER_CLANG_ANALYZE_DRIVER_EXPANSION_V1\n"));
        string8_list_push(arena, &parts, out);
        string8_list_push(arena, &parts, S8("\nBUSTER_CLANG_ANALYZE_DRIVER_STDERR_V1\n"));
        string8_list_push(arena, &parts, err);
        *expansion = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_driver_expansion(Arena* arena, ClangAnalyzeUnit unit, String8* fingerprint)
{
    String8 analyzer_expansion = {0};
    String8 preprocessing_expansion = {0};
    bool valid = clang_analyze_capture_driver_expansion(arena, unit.command, unit.entry.directory, &analyzer_expansion);
    SliceString8 preprocessing_arguments = clang_analyze_make_dynamic_time_command(arena, unit);
    if (valid)
    {
        valid = clang_analyze_capture_driver_expansion(arena, preprocessing_arguments, unit.entry.directory, &preprocessing_expansion);
    }
    if (valid)
    {
        valid = !clang_analyze_driver_expansion_has_unsupported_option(analyzer_expansion) &&
                !clang_analyze_driver_expansion_has_unsupported_option(preprocessing_expansion) &&
                !clang_analyze_driver_expansion_disables_warnings(preprocessing_expansion);
    }
    if (valid)
    {
        String8List parts = {0};
        string8_list_push(arena, &parts, analyzer_expansion);
        string8_list_push(arena, &parts, S8("\nBUSTER_CLANG_ANALYZE_PREPROCESSOR_EXPANSION_V1\n"));
        string8_list_push(arena, &parts, preprocessing_expansion);
        *fingerprint = clang_analyze_sha256(arena, string_join_arena(arena, string8_list_to_slice(arena, parts), true));
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_warning_suppression_option(SliceString8 arguments)
{
    bool result = false;
    for (u64 i = 1; !result && i < arguments.length; i += 1)
    {
        result = string_equal(arguments.pointer[i], S8("-w"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_parse_ldd(Arena* arena, String8 output, ClangAnalyzeUnit* unit,
                                                 ClangAnalyzePlanContext* context, String8List* evidence)
{
    bool valid = output.length != 0;
    bool saw_dependency = false;
    bool saw_static = false;
    u64 cursor = 0;
    while (valid && cursor < output.length)
    {
        u64 end = cursor;
        while (end < output.length && output.pointer[end] != '\n') end += 1;
        String8 line = clang_analyze_trim(string_slice(output, cursor, end));
        if (string_equal(line, S8("statically linked")) || string_equal(line, S8("not a dynamic executable")))
        {
            saw_static = true;
        }
        else if (line.length && !string_starts_with_sequence(line, S8("linux-vdso")))
        {
            u64 path_start = BUSTER_STRING_NO_MATCH;
            u64 arrow = BUSTER_STRING_NO_MATCH;
            for (u64 i = 0; i + 1 < line.length; i += 1)
            {
                if (line.pointer[i] == '=' && line.pointer[i + 1] == '>')
                {
                    arrow = i;
                    break;
                }
            }
            if (arrow != BUSTER_STRING_NO_MATCH)
            {
                path_start = arrow + 2;
                while (path_start < line.length && (line.pointer[path_start] == ' ' || line.pointer[path_start] == '\t')) path_start += 1;
            }
            else if (line.pointer[0] == '/')
            {
                path_start = 0;
            }
            valid = path_start != BUSTER_STRING_NO_MATCH && path_start < line.length && line.pointer[path_start] == '/';
            if (valid)
            {
                u64 path_end = path_start;
                while (path_end < line.length && line.pointer[path_end] != ' ' && line.pointer[path_end] != '\t' && line.pointer[path_end] != '(') path_end += 1;
                String8 path = string_slice(line, path_start, path_end);
                valid = path.length && clang_analyze_add_cached_file_input(arena, context, unit, path);
                if (valid)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, evidence, path);
                    build_artifact_fanout_provenance_record_append_string(arena, evidence, unit->input_fingerprints[unit->input_count - 1]);
                    saw_dependency = true;
                }
            }
        }
        cursor = end < output.length ? end + 1 : end;
    }
    valid = valid && (saw_dependency || saw_static);
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_executable_path(Arena* arena, ClangAnalyzeUnit unit)
{
    String8 requested = unit.command.pointer[0];
    String8 result = {0};
    if (path_is_absolute(requested))
    {
        result = os_path_absolute_lexical(arena, requested, true);
    }
    else if (string_first_code_unit(requested, '/') != BUSTER_STRING_NO_MATCH ||
             string_first_code_unit(requested, '\\') != BUSTER_STRING_NO_MATCH)
    {
        result = clang_analyze_absolute_from(arena, unit.entry.directory, requested);
    }
    else
    {
        result = executable_resolve_in_path(arena, requested);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_clang_name(String8 path)
{
    u64 start = 0;
    for (u64 i = 0; i < path.length; i += 1) if (path.pointer[i] == '/' || path.pointer[i] == '\\') start = i + 1;
    String8 name = string_slice(path, start, path.length);
    if (string_ends_with_sequence_insensitive(name, S8(".exe"))) name.length -= 4;
    bool result = string_equal(name, S8("clang"));
    if (!result && string_starts_with_sequence(name, S8("clang-")))
    {
        result = name.length > sizeof("clang-") - 1;
        bool saw_digit = false;
        for (u64 i = sizeof("clang-") - 1; result && i < name.length; i += 1)
        {
            char8 c = name.pointer[i];
            bool digit = c >= '0' && c <= '9';
            result = digit || c == '.';
            saw_digit = saw_digit || digit;
        }
        result = result && saw_digit && name.pointer[name.length - 1] != '.';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_compiler_path_snapshot(Arena* arena, String8 path, String8* resolved_path,
                                                               String8* fingerprint)
{
    bool valid = path.length && path_is_absolute(path) && clang_analyze_clang_name(path);
#if BUSTER_LINUX
    Sha256 hash;
    sha256_init(&hash);
    clang_analyze_hash_string(&hash, S8("BUSTER_CLANG_ANALYZE_COMPILER_PATH_V1"));
    ClangAnalyzeCompilerPathWalk walk = {.hash = &hash};
    valid = valid && clang_analyze_compiler_path_walk(arena, &walk, path);
    String8 path_z = valid ? string_duplicate_arena(arena, path, true) : (String8){0};
    char canonical[PATH_MAX];
    if (valid) valid = realpath(path_z.pointer, canonical) != 0;
    String8 resolved = valid ? string_duplicate_arena(arena, string_from_pointer(canonical), true) : (String8){0};
    if (valid)
    {
        valid = clang_analyze_clang_name(resolved) && clang_analyze_native_clang_binary(arena, resolved);
    }
    if (valid)
    {
        char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digits);
        *resolved_path = resolved;
        *fingerprint = (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(resolved_path);
    BUSTER_UNUSED(fingerprint);
    valid = false;
#endif
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_context_proof(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzeUnit* unit,
                                                      ClangAnalyzePlanContext* context)
{
    u64 proof_timing_start = os_now_microseconds();
    bool proof_timing_enabled = string_equal(os_get_environment_variable(S8("BUSTER_CLANG_ANALYZE_PROOF_TIMING")), S8("1"));
    u64 toolchain_us = 0;
    u64 runtime_us = 0;
    u64 ldd_resolution_us = 0;
    u64 ldd_query_us = 0;
    u64 runtime_closure_us = 0;
    u64 dependency_scan_us = 0;
    u64 dependency_inputs_us = 0;
    u64 preprocessing_us = 0;
    u64 driver_expansion_us = 0;
    u64 include_search_us = 0;
    bool valid = true;
    String8 reason = S8("unprovable-context");
    for (u64 i = 0; options.fixture_compiler && valid && i < unit->command.length; i += 1)
    {
        valid = !clang_analyze_dynamic_time_input(unit->command.pointer[i]);
        if (!valid) reason = S8("dynamic-time-builtin");
    }
    if (valid && clang_analyze_plugin_or_mutable_input(unit->entry.arguments))
    {
        valid = false;
        reason = S8("plugin-or-mutable-input");
    }
    if (valid && !options.fixture_compiler && clang_analyze_warning_suppression_option(unit->entry.arguments))
    {
        valid = false;
        reason = S8("warning-suppression-option");
    }
    bool environment_valid = true;
    String8 environment = clang_analyze_environment_context(arena, &environment_valid);
    valid = valid && environment_valid;
    if (!environment_valid) reason = S8("unsupported-environment");
#if BUSTER_LINUX
    u64 toolchain_start = os_now_microseconds();
    String8 compiler = clang_analyze_executable_path(arena, *unit);
    String8 compiler_resolved_path = {0};
    String8 compiler_path_fingerprint = {0};
    bool compiler_path_valid = options.fixture_compiler ||
        (valid && clang_analyze_compiler_path_snapshot(arena, compiler, &compiler_resolved_path, &compiler_path_fingerprint));
    if (valid && !clang_analyze_path_lookup_is_stable(unit->command.pointer[0]))
    {
        valid = false;
        reason = S8("relative-path-search-unsupported");
    }
    if (valid && !options.fixture_compiler &&
        (!path_is_absolute(unit->command.pointer[0]) || !string_equal(unit->command.pointer[0], compiler) ||
         !compiler.length || !compiler_path_valid))
    {
        valid = false;
        reason = S8("unsupported-wrapper-or-toolchain");
    }
    String8 compiler_hash = {0};
    String8 compiler_metadata = {0};
    if (valid)
    {
        valid = clang_analyze_plan_file_fingerprint(arena, context, compiler, &compiler_hash, &compiler_metadata) &&
                    clang_analyze_append_input(arena, unit, compiler, compiler_hash, compiler_metadata, true);
        if (!valid) reason = S8("compiler-identity-unavailable");
    }
    if (valid && !options.fixture_compiler)
    {
        String8 resolved_after = {0};
        String8 fingerprint_after = {0};
        valid = clang_analyze_compiler_path_snapshot(arena, compiler, &resolved_after, &fingerprint_after) &&
                string_equal(compiler_resolved_path, resolved_after) && string_equal(compiler_path_fingerprint, fingerprint_after);
        if (!valid) reason = S8("compiler-path-changed-during-proof");
    }
    if (valid && !options.fixture_compiler)
    {
        valid = clang_analyze_snapshot_default_config_inputs(arena, context, unit, compiler, (String8){0});
        if (!valid) reason = S8("default-config-search-snapshot-failed");
    }
    String8 version = {0};
    String8 version_error = {0};
    String8 version_arguments[] = {compiler, S8("--version")};
    if (valid && options.fixture_compiler)
    {
        version = S8("BUSTER_ANALYZER_NATIVE_FIXTURE_V1");
    }
    else if (valid)
    {
        valid = clang_analyze_query(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(version_arguments), unit->entry.directory, &version, &version_error) &&
                clang_analyze_contains(version, S8("clang version"));
        if (!valid) reason = S8("toolchain-version-unavailable");
    }
    String8 resource = {0};
    String8 resource_error = {0};
    String8 resource_arguments[] = {compiler, S8("-print-resource-dir")};
    if (valid && options.fixture_compiler)
    {
        resource = S8("native-fixture-no-resource-directory");
    }
    else if (valid)
    {
        valid = clang_analyze_query(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(resource_arguments), unit->entry.directory, &resource, &resource_error);
        resource = clang_analyze_trim(resource);
        valid = valid && resource.length;
        if (!valid) reason = S8("resource-directory-unavailable");
    }
    if (valid && !options.fixture_compiler)
    {
        valid = clang_analyze_snapshot_default_config_inputs(arena, context, unit, compiler, resource);
        if (!valid) reason = S8("default-config-search-snapshot-failed");
    }
    toolchain_us = os_now_microseconds() - toolchain_start;
    String8List runtime_evidence = {0};
    u64 runtime_start = os_now_microseconds();
    u64 ldd_resolution_start = os_now_microseconds();
    String8 ldd = executable_resolve_in_path(arena, S8("ldd"));
    if (valid && !options.fixture_compiler && ldd.length)
    {
        valid = clang_analyze_add_cached_file_input(arena, context, unit, ldd);
        if (!valid) reason = S8("runtime-dependency-tool-unavailable");
    }
    ldd_resolution_us = os_now_microseconds() - ldd_resolution_start;
    if (valid && options.fixture_compiler)
    {
        string8_list_push(arena, &runtime_evidence, S8("native-fixture-no-runtime-dependencies\n"));
    }
    else if (valid && ldd.length)
    {
        unit->runtime_ldd_path = ldd;
        String8 ldd_arguments[] = {ldd, compiler};
        String8 ldd_out = {0};
        String8 ldd_err = {0};
        u64 ldd_query_start = os_now_microseconds();
        bool ldd_queried = clang_analyze_query(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ldd_arguments), unit->entry.directory, &ldd_out, &ldd_err);
        ldd_query_us = os_now_microseconds() - ldd_query_start;
        u64 runtime_closure_start = os_now_microseconds();
        valid = ldd_queried && clang_analyze_parse_ldd(arena, ldd_out, unit, context, &runtime_evidence);
        runtime_closure_us = os_now_microseconds() - runtime_closure_start;
        if (!valid) reason = S8("runtime-dependency-unavailable");
    }
    else if (valid)
    {
        valid = false;
        reason = S8("runtime-dependency-tool-unavailable");
    }
    runtime_us = os_now_microseconds() - runtime_start;
    String8 dependency_output = {0};
    String8 dependency_error = {0};
    SliceString8 dependency_command = clang_analyze_make_dependency_command(arena, *unit);
    u64 dependency_scan_start = os_now_microseconds();
    if (valid)
    {
        valid = clang_analyze_query(arena, dependency_command, unit->entry.directory, &dependency_output, &dependency_error);
        if (!valid) reason = S8("dependency-scan-failed");
    }
    dependency_scan_us = os_now_microseconds() - dependency_scan_start;
    String8 source = clang_analyze_absolute_from(arena, unit->entry.directory, unit->entry.file);
    if (valid)
    {
        String8 source_directory = path_parent(arena, source);
        valid = source_directory.length && clang_analyze_append_search(arena, unit, source_directory, context);
        if (!valid) reason = S8("source-directory-snapshot-failed");
    }
    bool dependency_list_valid = valid;
    SliceString8 dependencies = valid ? clang_analyze_make_dependencies(arena, dependency_output, &dependency_list_valid) : (SliceString8){0};
    valid = valid && dependency_list_valid;
    if (!valid && string_equal(reason, S8("unprovable-context"))) reason = S8("dependency-record-invalid");
    String8List input_evidence = {0};
    u64 dependency_inputs_start = os_now_microseconds();
    for (u64 i = 0; valid && i < dependencies.length; i += 1)
    {
        String8 path = clang_analyze_absolute_from(arena, unit->entry.directory, dependencies.pointer[i]);
        String8 parent = path_parent(arena, path);
        valid = parent.length && clang_analyze_append_search(arena, unit, parent, context);
        if (!valid) reason = S8("dependency-parent-snapshot-failed");
        String8 metadata_before = {0};
        bool stable_before = valid && clang_analyze_file_metadata_fingerprint(arena, path, &metadata_before);
        TemporalArena input_temporary = scratch_begin(&arena, 1);
        Arena* input_scratch = input_temporary.arena;
        ByteSlice bytes = file_read(input_scratch, path, (FileReadOptions){.end_padding = 1});
        valid = stable_before && bytes.pointer != 0;
        if (valid)
        {
            String8 contents = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
            valid = options.fixture_compiler ? !clang_analyze_dynamic_time_input(contents) : true;
            if (valid)
            {
                String8 fingerprint = clang_analyze_sha256(arena, contents);
                String8 metadata_after = {0};
                valid = clang_analyze_file_metadata_fingerprint(arena, path, &metadata_after) && string_equal(metadata_before, metadata_after);
                valid = valid && clang_analyze_append_input(arena, unit, path, fingerprint, metadata_after, true);
                if (valid)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, &input_evidence, path);
                    build_artifact_fanout_provenance_record_append_string(arena, &input_evidence, fingerprint);
                    build_artifact_fanout_provenance_record_append_string(arena, &input_evidence, metadata_after);
                }
            }
            else
            {
                reason = S8("dynamic-time-builtin");
            }
        }
        else
        {
            reason = S8("dependency-input-unavailable");
        }
        scratch_end(input_temporary);
    }
    dependency_inputs_us = os_now_microseconds() - dependency_inputs_start;
    if (valid)
    {
        if (!options.fixture_compiler)
        {
            String8 preprocessed = {0};
            String8 preprocessor_error = {0};
            SliceString8 dynamic_time_command = clang_analyze_make_dynamic_time_command(arena, *unit);
            u64 preprocessing_start = os_now_microseconds();
            valid = clang_analyze_query(arena, dynamic_time_command, unit->entry.directory, &preprocessed, &preprocessor_error);
            if (!valid)
            {
                reason = S8("dynamic-time-builtin-or-preprocess-error");
            }
            else if (clang_analyze_dynamic_time_warning_emitted(preprocessor_error))
            {
                valid = false;
                reason = S8("date-time-warning-downgraded");
            }
            else if (clang_analyze_dynamic_time_warning_suppressed(preprocessed))
            {
                valid = false;
                reason = S8("date-time-warning-suppressed");
            }
            else
            {
                unit->preprocessing_fingerprint = clang_analyze_sha256(arena, preprocessed);
            }
            preprocessing_us = os_now_microseconds() - preprocessing_start;

            // Snapshot the actual analyzer driver's expanded cc1 command.
            // This binds effective default-config arguments that do not alter
            // preprocessed text (for example checker and analyzer settings).
            // The query is repeated at both context checks, so changes to
            // auto-discovered config state invalidate this representative.
            if (valid)
            {
                u64 driver_start = os_now_microseconds();
                valid = clang_analyze_driver_expansion(arena, *unit, &unit->driver_fingerprint);
                driver_expansion_us = os_now_microseconds() - driver_start;
                if (!valid) reason = S8("driver-expansion-or-plugin-unsupported");
            }
        }
    }
    if (valid)
    {
        if (!options.fixture_compiler)
        {
            u64 include_search_start = os_now_microseconds();
            valid = clang_analyze_parse_search_paths(arena, unit->entry.directory, dependency_error, unit, context);
            include_search_us = os_now_microseconds() - include_search_start;
            if (!valid) reason = S8("include-search-snapshot-failed");
        }
    }
    if (valid)
    {
        String8List proof = {0};
        String8 runtime_evidence_text = string_join_arena(arena, string8_list_to_slice(arena, runtime_evidence), true);
        unit->compiler_path_fingerprint = compiler_path_fingerprint;
        unit->compiler_resolved_path = compiler_resolved_path;
        unit->runtime_fingerprint = options.fixture_compiler ? (String8){0} : clang_analyze_sha256(arena, runtime_evidence_text);
        string8_list_push(arena, &proof, S8("BUSTER_CLANG_ANALYZE_CONTEXT_V5\n"));
        build_artifact_fanout_provenance_record_append_string(arena, &proof, compiler);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, compiler_resolved_path);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, compiler_path_fingerprint);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, compiler_hash);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, version);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, resource);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, environment);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->preprocessing_fingerprint);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->driver_fingerprint);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->runtime_ldd_path);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->runtime_fingerprint);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, runtime_evidence_text);
        build_artifact_fanout_provenance_record_append_string(arena, &proof, string_join_arena(arena, string8_list_to_slice(arena, input_evidence), true));
        build_artifact_fanout_provenance_record_append_u64(arena, &proof, unit->search_count);
        for (u64 i = 0; i < unit->search_count; i += 1)
        {
            build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->search_paths[i]);
            build_artifact_fanout_provenance_record_append_u64(arena, &proof, unit->search_entry_counts[i]);
            build_artifact_fanout_provenance_record_append_string(arena, &proof, unit->search_fingerprints[i]);
        }
        unit->context_proof = string_join_arena(arena, string8_list_to_slice(arena, proof), true);
        unit->environment_context = environment;
        unit->compiler_path = compiler;
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(options);
    BUSTER_UNUSED(context);
    valid = false;
    reason = S8("platform-context-unavailable");
#endif
    unit->ineligible_reason = valid ? (String8){0} : reason;
    if (proof_timing_enabled)
    {
        string_print(S8("ANALYZE_PROOF_TIMING source={S8} total_us={u64} toolchain_us={u64} runtime_us={u64} ldd_resolution_us={u64} ldd_query_us={u64} runtime_closure_us={u64} dependency_scan_us={u64} dependency_inputs_us={u64} preprocessing_us={u64} driver_expansion_us={u64} include_search_us={u64} status={S8}{S8}\n"),
            unit->entry.file, os_now_microseconds() - proof_timing_start, toolchain_us, runtime_us, ldd_resolution_us, ldd_query_us,
            runtime_closure_us, dependency_scan_us,
            dependency_inputs_us, preprocessing_us, driver_expansion_us, include_search_us,
            valid ? S8("pass") : S8("fail"), clang_analyze_status_qualifier(!valid));
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_context_file_matches(Arena* arena, ClangAnalyzeContextCheckCache* cache,
                                                             String8 path, String8 expected_fingerprint,
                                                             String8 expected_metadata)
{
    bool valid = true;
    u64 found = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; i < cache->file_count; i += 1)
    {
        if (string_equal(cache->files[i].path, path)) found = i;
    }
    if (found == BUSTER_STRING_NO_MATCH)
    {
        String8 fingerprint = {0};
        String8 metadata = {0};
        bool hashed = clang_analyze_file_fingerprint(arena, path, &fingerprint, &metadata);
        valid = hashed;
        if (valid && cache->file_count == cache->file_capacity)
        {
            u64 capacity = cache->file_capacity ? cache->file_capacity * 2 : 16;
            ClangAnalyzeFileFingerprint* files = arena_allocate(arena, ClangAnalyzeFileFingerprint, capacity);
            if (cache->file_count) memcpy(files, cache->files, cache->file_count * sizeof(*files));
            cache->files = files;
            cache->file_capacity = capacity;
        }
        if (valid)
        {
            found = cache->file_count++;
            cache->files[found] = (ClangAnalyzeFileFingerprint){.path = path, .fingerprint = fingerprint, .metadata = metadata};
        }
    }
    if (valid)
    {
        valid = string_equal(cache->files[found].fingerprint, expected_fingerprint) &&
                string_equal(cache->files[found].metadata, expected_metadata);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_context_directory_matches(Arena* arena, ClangAnalyzeContextCheckCache* cache,
                                                                  String8 path, String8 expected_fingerprint,
                                                                  u64 expected_entry_count)
{
    bool valid = true;
    u64 found = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; i < cache->directory_count; i += 1)
    {
        if (string_equal(cache->directories[i].path, path)) found = i;
    }
    if (found == BUSTER_STRING_NO_MATCH)
    {
        String8 fingerprint = {0};
        u64 entry_count = 0;
#if BUSTER_LINUX
        bool snapped = clang_analyze_snapshot_directory(arena, path, &fingerprint, &entry_count);
#else
        bool snapped = false;
#endif
        valid = snapped;
        if (valid && cache->directory_count == cache->directory_capacity)
        {
            u64 capacity = cache->directory_capacity ? cache->directory_capacity * 2 : 16;
            ClangAnalyzeDirectoryFingerprint* directories = arena_allocate(arena, ClangAnalyzeDirectoryFingerprint, capacity);
            if (cache->directory_count) memcpy(directories, cache->directories, cache->directory_count * sizeof(*directories));
            cache->directories = directories;
            cache->directory_capacity = capacity;
        }
        if (valid)
        {
            found = cache->directory_count++;
            cache->directories[found] = (ClangAnalyzeDirectoryFingerprint){.path = path, .fingerprint = fingerprint, .entry_count = entry_count};
        }
    }
    if (valid)
    {
        valid = cache->directories[found].entry_count == expected_entry_count &&
                string_equal(cache->directories[found].fingerprint, expected_fingerprint);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_parse_ldd_recheck(Arena* arena, String8 output, ClangAnalyzeUnit unit,
                                                          ClangAnalyzeContextCheckCache* cache, String8List* evidence)
{
    bool valid = output.length != 0;
    bool saw_dependency = false;
    bool saw_static = false;
    u64 cursor = 0;
    while (valid && cursor < output.length)
    {
        u64 end = cursor;
        while (end < output.length && output.pointer[end] != '\n') end += 1;
        String8 line = clang_analyze_trim(string_slice(output, cursor, end));
        if (string_equal(line, S8("statically linked")) || string_equal(line, S8("not a dynamic executable")))
        {
            saw_static = true;
        }
        else if (line.length && !string_starts_with_sequence(line, S8("linux-vdso")))
        {
            u64 path_start = BUSTER_STRING_NO_MATCH;
            u64 arrow = BUSTER_STRING_NO_MATCH;
            for (u64 i = 0; i + 1 < line.length; i += 1)
            {
                if (line.pointer[i] == '=' && line.pointer[i + 1] == '>')
                {
                    arrow = i;
                    break;
                }
            }
            if (arrow != BUSTER_STRING_NO_MATCH)
            {
                path_start = arrow + 2;
                while (path_start < line.length && (line.pointer[path_start] == ' ' || line.pointer[path_start] == '\t')) path_start += 1;
            }
            else if (line.pointer[0] == '/')
            {
                path_start = 0;
            }
            valid = path_start != BUSTER_STRING_NO_MATCH && path_start < line.length && line.pointer[path_start] == '/';
            if (valid)
            {
                u64 path_end = path_start;
                while (path_end < line.length && line.pointer[path_end] != ' ' && line.pointer[path_end] != '\t' && line.pointer[path_end] != '(') path_end += 1;
                String8 path = string_slice(line, path_start, path_end);
                u64 input_index = BUSTER_STRING_NO_MATCH;
                for (u64 i = 0; i < unit.input_count; i += 1)
                {
                    if (string_equal(unit.input_paths[i], path)) input_index = i;
                }
                if (input_index != BUSTER_STRING_NO_MATCH)
                {
                    valid = clang_analyze_context_file_matches(arena, cache, path, unit.input_fingerprints[input_index], unit.input_metadata[input_index]);
                }
                else
                {
                    // Hash an unexpected newly resolved object once into the
                    // shared pass cache, then reject this changed loader closure.
                    bool cached = clang_analyze_context_file_matches(arena, cache, path, (String8){0}, (String8){0});
                    BUSTER_UNUSED(cached);
                    valid = false;
                }
                if (input_index != BUSTER_STRING_NO_MATCH)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, evidence, path);
                    build_artifact_fanout_provenance_record_append_string(arena, evidence, unit.input_fingerprints[input_index]);
                    saw_dependency = true;
                }
            }
        }
        cursor = end < output.length ? end + 1 : end;
    }
    valid = valid && (saw_dependency || saw_static);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_context_matches_cached(Arena* arena, ClangAnalyzeUnit unit,
                                                               ClangAnalyzeContextCheckCache* cache)
{
    bool environment_valid = true;
    String8 environment = clang_analyze_environment_context(arena, &environment_valid);
    bool valid = unit.context_proof.length && environment_valid && string_equal(unit.environment_context, environment);
    if (valid && unit.compiler_path_fingerprint.length)
    {
        String8 resolved_path = {0};
        String8 fingerprint = {0};
        valid = string_equal(unit.command.pointer[0], unit.compiler_path) &&
                clang_analyze_compiler_path_snapshot(arena, unit.compiler_path, &resolved_path, &fingerprint) &&
                string_equal(resolved_path, unit.compiler_resolved_path) &&
                string_equal(fingerprint, unit.compiler_path_fingerprint);
    }
    for (u64 i = 0; valid && i < unit.input_count; i += 1)
    {
        if (unit.input_content_rechecked[i])
        {
            valid = clang_analyze_context_file_matches(arena, cache, unit.input_paths[i], unit.input_fingerprints[i], unit.input_metadata[i]);
        }
        else
        {
            String8 metadata = {0};
            valid = clang_analyze_file_metadata_fingerprint(arena, unit.input_paths[i], &metadata) &&
                    string_equal(metadata, unit.input_metadata[i]);
        }
    }
    for (u64 i = 0; valid && i < unit.search_count; i += 1)
    {
        valid = clang_analyze_context_directory_matches(arena, cache, unit.search_paths[i], unit.search_fingerprints[i],
                                                         unit.search_entry_counts[i]);
    }
    if (valid && unit.runtime_fingerprint.length)
    {
        String8 ldd_arguments[] = {unit.runtime_ldd_path, unit.compiler_path};
        String8 ldd_output = {0};
        String8 ldd_error = {0};
        String8List runtime_evidence = {0};
        valid = unit.runtime_ldd_path.length &&
                clang_analyze_query(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ldd_arguments), unit.entry.directory,
                                    &ldd_output, &ldd_error) &&
                clang_analyze_parse_ldd_recheck(arena, ldd_output, unit, cache, &runtime_evidence);
        if (valid)
        {
            String8 evidence = string_join_arena(arena, string8_list_to_slice(arena, runtime_evidence), true);
            valid = string_equal(clang_analyze_sha256(arena, evidence), unit.runtime_fingerprint);
        }
    }
    if (valid && unit.preprocessing_fingerprint.length)
    {
        String8 preprocessed = {0};
        String8 preprocessor_error = {0};
        SliceString8 command = clang_analyze_make_dynamic_time_command(arena, unit);
        valid = clang_analyze_query(arena, command, unit.entry.directory, &preprocessed, &preprocessor_error) &&
                !clang_analyze_dynamic_time_warning_emitted(preprocessor_error) &&
                !clang_analyze_dynamic_time_warning_suppressed(preprocessed) &&
                string_equal(clang_analyze_sha256(arena, preprocessed), unit.preprocessing_fingerprint);
    }
    if (valid && unit.driver_fingerprint.length)
    {
        String8 fingerprint = {0};
        valid = clang_analyze_driver_expansion(arena, unit, &fingerprint) && string_equal(fingerprint, unit.driver_fingerprint);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_context_matches(Arena* arena, ClangAnalyzeUnit unit)
{
    ClangAnalyzeContextCheckCache cache = {0};
    return clang_analyze_context_matches_cached(arena, unit, &cache);
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_worker_manifest_matches(Arena* arena, ClangAnalyzeOptions options,
                                                                 String8 database, ClangAnalyzePlan* plan)
{
    String8 path = path_join(arena, options.results, S8("manifest.txt"));
    String8 stored = clang_analyze_read(arena, path);
    u64 cursor = 0;
    String8 magic = {0};
    String8 result_directory = {0};
    String8 config = {0};
    String8 clang = {0};
    String8 stored_database = {0};
    u64 shards = 0;
    u64 timeout = 0;
    u64 count = 0;
    u64 excluded = 0;
    u64 unique = 0;
    u64 aliases = 0;
    u64 fixture = 0;
    bool valid = stored.pointer &&
        build_artifact_fanout_provenance_record_read_line(stored, &cursor, &magic) &&
        string_equal(magic, S8("BUSTER_CLANG_ANALYZE_PLAN_V2")) &&
        build_artifact_fanout_provenance_record_read_string(stored, &cursor, &result_directory) &&
        build_artifact_fanout_provenance_record_read_string(stored, &cursor, &config) &&
        build_artifact_fanout_provenance_record_read_string(stored, &cursor, &clang) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &shards) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &timeout) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &count) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &excluded) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &unique) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &aliases) &&
        build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &fixture) &&
        build_artifact_fanout_provenance_record_read_string(stored, &cursor, &stored_database);
    valid = valid && string_equal(result_directory, os_path_absolute_lexical(arena, options.results, true)) &&
        string_equal(config, options.config) && string_equal(clang, options.clang) && shards == options.shards &&
        timeout == options.timeout && count == plan->count && excluded == plan->excluded && fixture == options.fixture_compiler &&
        string_equal(stored_database, database) && unique <= count && aliases == count - unique;
    u64* representatives = arena_allocate(arena, u64, plan->count);
    u64* proven = arena_allocate(arena, u64, plan->count);
    u64* input_counts = arena_allocate(arena, u64, plan->count);
    u64* search_counts = arena_allocate(arena, u64, plan->count);
    bool* reason_present = arena_allocate(arena, bool, plan->count);
    bool* context_present = arena_allocate(arena, bool, plan->count);
    for (u64 i = 0; valid && i < plan->count; i += 1)
    {
        ClangAnalyzeUnit unit = plan->units[i];
        u64 index = 0;
        u64 shard = 0;
        u64 representative = 0;
        String8 module = {0};
        String8 source = {0};
        String8 directory = {0};
        String8 output = {0};
        u64 argument_count = 0;
        u64 command_count = 0;
        u64 row_proven = 0;
        String8 reason = {0};
        String8 context_proof = {0};
        valid = build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &index) && index == i &&
            build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &shard) &&
            build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &representative) && representative < plan->count &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &module) &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &source) &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &directory) &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &output) &&
            build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &argument_count) && argument_count == unit.entry.arguments.length;
        for (u64 argument = 0; valid && argument < argument_count; argument += 1)
        {
            String8 value = {0};
            valid = build_artifact_fanout_provenance_record_read_string(stored, &cursor, &value) &&
                string_equal(value, unit.entry.arguments.pointer[argument]);
        }
        valid = valid && build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &command_count) && command_count == unit.command.length;
        for (u64 argument = 0; valid && argument < command_count; argument += 1)
        {
            String8 value = {0};
            valid = build_artifact_fanout_provenance_record_read_string(stored, &cursor, &value) &&
                string_equal(value, unit.command.pointer[argument]);
        }
        valid = valid && shard == unit.shard && string_equal(module, unit.module) && string_equal(source, unit.entry.file) &&
            string_equal(directory, unit.entry.directory) && string_equal(output, unit.entry.output) &&
            build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &row_proven) && row_proven <= 1 &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &reason) &&
            build_artifact_fanout_provenance_record_read_string(stored, &cursor, &context_proof);
        bool owned = unit.shard == options.shard;
        bool expected_proven = unit.representative != i || unit.context_proof.length != 0;
        if (valid && owned)
        {
            valid = representative == unit.representative && row_proven == expected_proven &&
                string_equal(reason, unit.ineligible_reason) && string_equal(context_proof, unit.representative == i ? unit.context_proof : (String8){0});
        }
        representatives[i] = representative;
        proven[i] = row_proven;
        reason_present[i] = reason.length != 0;
        context_present[i] = context_proof.length != 0;
        u64 input_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &input_count) && input_count <= stored.length / 3;
        if (valid && owned) valid = input_count == unit.input_count;
        input_counts[i] = input_count;
        for (u64 input = 0; valid && input < input_count; input += 1)
        {
            String8 input_path = {0};
            String8 fingerprint = {0};
            String8 metadata = {0};
            u64 content_rechecked = 0;
            valid = build_artifact_fanout_provenance_record_read_string(stored, &cursor, &input_path) &&
                build_artifact_fanout_provenance_record_read_string(stored, &cursor, &fingerprint) &&
                build_artifact_fanout_provenance_record_read_string(stored, &cursor, &metadata) &&
                build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &content_rechecked) && content_rechecked <= 1;
            if (valid && owned)
            {
                valid = string_equal(input_path, unit.input_paths[input]) && string_equal(fingerprint, unit.input_fingerprints[input]) &&
                    string_equal(metadata, unit.input_metadata[input]) && content_rechecked == unit.input_content_rechecked[input];
            }
        }
        u64 search_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &search_count) && search_count <= stored.length / 3;
        if (valid && owned) valid = search_count == unit.search_count;
        search_counts[i] = search_count;
        for (u64 search = 0; valid && search < search_count; search += 1)
        {
            String8 search_path = {0};
            String8 fingerprint = {0};
            u64 entry_count = 0;
            valid = build_artifact_fanout_provenance_record_read_string(stored, &cursor, &search_path) &&
                build_artifact_fanout_provenance_record_read_u64(stored, &cursor, &entry_count) &&
                build_artifact_fanout_provenance_record_read_string(stored, &cursor, &fingerprint);
            if (valid && owned)
            {
                valid = string_equal(search_path, unit.search_paths[search]) && entry_count == unit.search_entry_counts[search] &&
                    string_equal(fingerprint, unit.search_fingerprints[search]);
            }
        }
    }
    valid = valid && cursor == stored.length;
    bool* grouped = arena_allocate(arena, bool, plan->count);
    memset(grouped, 0, plan->count * sizeof(*grouped));
    u64 observed_unique = 0;
    for (u64 i = 0; valid && i < plan->count; i += 1)
    {
        observed_unique += representatives[i] == i;
        if (plan->units[i].shard != options.shard && !grouped[i])
        {
            ClangAnalyzeUnit key = plan->units[i];
            u64 canonical = i;
            u64 members = 0;
            bool all_self = true;
            bool all_canonical = true;
            for (u64 j = 0; j < plan->count; j += 1)
            {
                if (clang_analyze_same_invocation(key, plan->units[j]))
                {
                    grouped[j] = true;
                    members += 1;
                    if (clang_analyze_identity_less(plan->units[j], plan->units[canonical])) canonical = j;
                }
            }
            for (u64 j = 0; j < plan->count; j += 1)
            {
                if (clang_analyze_same_invocation(key, plan->units[j]))
                {
                    all_self = all_self && representatives[j] == j;
                    all_canonical = all_canonical && representatives[j] == canonical;
                }
            }
            valid = all_self || all_canonical;
            for (u64 j = 0; valid && j < plan->count; j += 1)
            {
                if (clang_analyze_same_invocation(key, plan->units[j]))
                {
                    if (all_canonical && members > 1)
                    {
                        valid = proven[j] == 1 && !reason_present[j] && (context_present[j] == (j == canonical)) &&
                            (j == canonical || (input_counts[j] == 0 && search_counts[j] == 0));
                    }
                    else if (members > 1)
                    {
                        valid = proven[j] == 0 && reason_present[j] && !context_present[j] &&
                            (j == canonical || (input_counts[j] == 0 && search_counts[j] == 0));
                    }
                    else
                    {
                        valid = representatives[j] == j && proven[j] == 0 && reason_present[j] && !context_present[j] &&
                            input_counts[j] == 0 && search_counts[j] == 0;
                    }
                    plan->units[j].representative = representatives[j];
                }
            }
        }
    }
    valid = valid && observed_unique == unique && plan->count - observed_unique == aliases;
    if (valid)
    {
        plan->unique_executions = unique;
        plan->aliased_rows = aliases;
        plan->manifest = stored;
        plan->fingerprint = clang_analyze_sha256(arena, stored);
    }
    else
    {
        string_print(S8("{S8} worker shard manifest is malformed, stale or inconsistent with its selected rows: {S8} shard={u64}\n"),
            clang_analyze_error_prefix(), path, options.shard);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_plan(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan* plan)
{
    String8 database = clang_analyze_read(arena, options.database);
    JsonParser parser = {.text = database};
    bool valid = database.pointer && json_consume(&parser, '[');
    bool done = valid && json_consume(&parser, ']');
    u64 capacity = 0;
    *plan = (ClangAnalyzePlan){0};
    plan->fixture_compiler = options.fixture_compiler;
    while (valid && !done)
    {
        CompileCommandEntry entry = json_parse_compile_command_entry(arena, &parser, &valid);
        valid = valid && entry.file.length && entry.directory.length;
        SliceString8 arguments = entry.arguments;
        if (valid && !arguments.length && entry.command.length)
        {
            arguments = shell_split(arena, entry.command, &valid);
        }
        valid = valid && arguments.length && arguments.pointer[0].length;
        if (valid && clang_analyze_is_c_source(entry.file) && clang_analyze_entry_matches_config(arena, entry, arguments, options.config))
        {
            valid = clang_analyze_command_accounts_for_source(arena, entry, arguments);
            if (!entry.output.length)
            {
                for (u64 i = 1; i + 1 < arguments.length; i += 1)
                {
                    if (string_equal(arguments.pointer[i], S8("-o"))) entry.output = arguments.pointer[i + 1];
                }
            }
            // A TU identity is its directory/file/output in this configuration.
            // Conflicting commands for the same output fail rather than silently
            // choosing one. Distinct outputs for the same source remain eligible.
            for (u64 i = 0; valid && i < plan->count; i += 1)
            {
                CompileCommandEntry previous = plan->units[i].entry;
                if (string_equal(previous.directory, entry.directory) && string_equal(previous.file, entry.file) &&
                    string_equal(previous.output, entry.output))
                {
                    string_print(S8("{S8} duplicate analyzer TU: {S8}\n"), clang_analyze_error_prefix(), entry.file);
                    valid = false;
                }
            }
            if (valid)
            {
                if (plan->count == capacity)
                {
                    u64 new_capacity = capacity ? capacity * 2 : 64;
                    ClangAnalyzeUnit* units = arena_allocate(arena, ClangAnalyzeUnit, new_capacity);
                    if (plan->count)
                    {
                        memcpy(units, plan->units, plan->count * sizeof(*units));
                    }
                    plan->units = units;
                    capacity = new_capacity;
                }
                entry.arguments = arguments;
                String8 module = clang_analyze_module(entry.file);
                plan->units[plan->count++] = (ClangAnalyzeUnit){.entry = entry, .module = module,
                    .shard = clang_analyze_module_shard(module, options.shards),
                    .command = clang_analyzer_command(arena, arguments, options.clang)};
            }
        }
        else if (valid)
        {
            plan->excluded += 1;
        }
        if (valid)
        {
            if (json_consume(&parser, ']'))
            {
                done = true;
            }
            else
            {
                valid = json_consume(&parser, ',');
                // A trailing comma cannot turn a truncated database into success.
                json_skip_whitespace(&parser);
                valid = valid && parser.index < parser.text.length && parser.text.pointer[parser.index] != ']';
            }
        }
    }
    json_skip_whitespace(&parser);
    valid = valid && done && parser.index == database.length && plan->count;
    if (valid)
    {
        ClangAnalyzePlanContext context = {0};
        bool* assigned = arena_allocate(arena, bool, plan->count);
        memset(assigned, 0, plan->count * sizeof(*assigned));
        for (u64 i = 0; i < plan->count; i += 1)
        {
            if (!assigned[i])
            {
                // Freeze the original argv before assigning representatives.
                // A representative may later receive separately resolved
                // execution metadata; grouping must always compare the source
                // database command, never that mutable execution state.
                ClangAnalyzeUnit invocation_key = plan->units[i];
                u64 members = 0;
                u64 representative = i;
                for (u64 j = i; j < plan->count; j += 1)
                {
                    if (clang_analyze_same_invocation(invocation_key, plan->units[j]))
                    {
                        members += 1;
                        if (clang_analyze_identity_less(plan->units[j], plan->units[representative])) representative = j;
                    }
                }
                bool prove_group = !options.worker || plan->units[representative].shard == options.shard;
                u64 proof_start = os_now_microseconds();
                if (members > 1) plan->candidate_groups += 1;
                bool eligible = members > 1 && prove_group && clang_analyze_context_proof(arena, options, &plan->units[representative], &context);
                if (members > 1 && prove_group)
                {
                    plan->context_proof_us += os_now_microseconds() - proof_start;
                    plan->proven_groups += eligible;
                }
                String8 reason = plan->units[representative].ineligible_reason;
                for (u64 j = i; j < plan->count; j += 1)
                {
                    if (clang_analyze_same_invocation(invocation_key, plan->units[j]))
                    {
                        assigned[j] = true;
                        plan->units[j].representative = eligible ? representative : j;
                        if (eligible)
                        {
                            plan->units[j].compiler_path = plan->units[representative].compiler_path;
                            plan->units[j].compiler_path_fingerprint = plan->units[representative].compiler_path_fingerprint;
                            plan->units[j].compiler_resolved_path = plan->units[representative].compiler_resolved_path;
                            plan->units[j].runtime_ldd_path = plan->units[representative].runtime_ldd_path;
                            plan->units[j].runtime_fingerprint = plan->units[representative].runtime_fingerprint;
                        }
                        if (!eligible) plan->units[j].ineligible_reason = members > 1 ? reason : S8("single-row");
                    }
                }
            }
        }
        if (options.worker)
        {
            valid = clang_analyze_worker_manifest_matches(arena, options, database, plan);
        }
        else
        {
            plan->unique_executions = plan->count;
            for (u64 i = 0; i < plan->count; i += 1)
            {
                if (plan->units[i].representative != i)
                {
                    plan->unique_executions -= 1;
                    plan->aliased_rows += 1;
                }
            }
            String8List parts = {0};
            string8_list_push(arena, &parts, S8(BUSTER_ANALYZE_PLAN_VERSION));
            build_artifact_fanout_provenance_record_append_string(arena, &parts, os_path_absolute_lexical(arena, options.results, true));
            build_artifact_fanout_provenance_record_append_string(arena, &parts, options.config);
            build_artifact_fanout_provenance_record_append_string(arena, &parts, options.clang);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, options.shards);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, options.timeout);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->count);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->excluded);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->unique_executions);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, plan->aliased_rows);
            build_artifact_fanout_provenance_record_append_u64(arena, &parts, options.fixture_compiler);
            // Retain the exact authority, including rows excluded by config/language.
            build_artifact_fanout_provenance_record_append_string(arena, &parts, database);
            for (u64 i = 0; i < plan->count; i += 1)
            {
                ClangAnalyzeUnit unit = plan->units[i];
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, i);
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.shard);
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.representative);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.module);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.entry.file);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.entry.directory);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.entry.output);
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.entry.arguments.length);
                for (u64 a = 0; a < unit.entry.arguments.length; a += 1)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.entry.arguments.pointer[a]);
                }
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.command.length);
                for (u64 a = 0; a < unit.command.length; a += 1)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.command.pointer[a]);
                }
                bool proven = unit.representative != i || unit.context_proof.length != 0;
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, proven);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.ineligible_reason);
                build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.representative == i ? unit.context_proof : (String8){0});
                u64 input_count = unit.representative == i ? unit.input_count : 0;
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, input_count);
                for (u64 input = 0; input < input_count; input += 1)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.input_paths[input]);
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.input_fingerprints[input]);
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.input_metadata[input]);
                    build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.input_content_rechecked[input]);
                }
                u64 search_count = unit.representative == i ? unit.search_count : 0;
                build_artifact_fanout_provenance_record_append_u64(arena, &parts, search_count);
                for (u64 search = 0; search < search_count; search += 1)
                {
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.search_paths[search]);
                    build_artifact_fanout_provenance_record_append_u64(arena, &parts, unit.search_entry_counts[search]);
                    build_artifact_fanout_provenance_record_append_string(arena, &parts, unit.search_fingerprints[search]);
                }
            }
            plan->manifest = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
            plan->fingerprint = clang_analyze_sha256(arena, plan->manifest);
        }
    }
    else
    {
        string_print(S8("{S8} invalid, empty or duplicate analyzer inventory: {S8} config={S8} offset={u64}\n"), clang_analyze_error_prefix(),
                     options.database, options.config, parser.index);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_shard_directory(Arena* arena, ClangAnalyzeOptions options, u64 shard)
{
    String8 result = path_join(arena, options.results, string_format(arena, S8("shard-{u64}"), shard));
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_same_plan(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    String8 stored = clang_analyze_read(arena, path_join(arena, options.results, S8("manifest.txt")));
    bool result = string_equal(plan.manifest, stored);
    if (!result)
    {
        u64 different = 0;
        u64 common = BUSTER_MIN(plan.manifest.length, stored.length);
        while (different < common && plan.manifest.pointer[different] == stored.pointer[different]) different += 1;
        String8 stored_environment = {0};
        String8 environment_marker = S8("BUSTER_CLANG_ANALYZE_ENVIRONMENT_V4\n");
        for (u64 i = 0; i + environment_marker.length <= stored.length && !stored_environment.length; i += 1)
        {
            if (memcmp(stored.pointer + i, environment_marker.pointer, environment_marker.length) == 0 && i && stored.pointer[i - 1] == '\n')
            {
                u64 length_line_start = i - 1;
                while (length_line_start && stored.pointer[length_line_start - 1] != '\n') length_line_start -= 1;
                String8 length_line = string_slice(stored, length_line_start, i - 1);
                IntegerParsingU64 parsed_length = string8_parse_u64_decimal(length_line);
                if (parsed_length.status == INTEGER_PARSING_SUCCESS && parsed_length.length == length_line.length &&
                    parsed_length.value <= stored.length - i && i + parsed_length.value < stored.length &&
                    stored.pointer[i + parsed_length.value] == '\n')
                {
                    stored_environment = string_slice(stored, i, i + parsed_length.value);
                }
            }
        }
        String8 environment_difference = S8("none");
        String8 old_environment_value_hash = {0};
        String8 new_environment_value_hash = {0};
        if (stored_environment.length && plan.units[0].environment_context.length)
        {
            u64 old_cursor = 0;
            u64 new_cursor = 0;
            String8 old_magic = {0};
            String8 new_magic = {0};
            String8 old_full_hash = {0};
            String8 new_full_hash = {0};
            bool parsed = build_artifact_fanout_provenance_record_read_line(stored_environment, &old_cursor, &old_magic) &&
                build_artifact_fanout_provenance_record_read_line(plan.units[0].environment_context, &new_cursor, &new_magic) &&
                build_artifact_fanout_provenance_record_read_string(stored_environment, &old_cursor, &old_full_hash) &&
                build_artifact_fanout_provenance_record_read_string(plan.units[0].environment_context, &new_cursor, &new_full_hash);
            if (parsed && !string_equal(old_full_hash, new_full_hash)) environment_difference = S8("unlisted-environment-entry");
            u64 old_environment_count = 0;
            u64 new_environment_count = 0;
            parsed = parsed && build_artifact_fanout_provenance_record_read_u64(stored_environment, &old_cursor, &old_environment_count) &&
                build_artifact_fanout_provenance_record_read_u64(plan.units[0].environment_context, &new_cursor, &new_environment_count);
            if (parsed && old_environment_count != new_environment_count) environment_difference = S8("environment-count");
            for (u64 i = 0; parsed && i < BUSTER_MIN(old_environment_count, new_environment_count); i += 1)
            {
                String8 old_name = {0};
                String8 new_name = {0};
                String8 old_value_hash = {0};
                String8 new_value_hash = {0};
                parsed = build_artifact_fanout_provenance_record_read_string(stored_environment, &old_cursor, &old_name) &&
                    build_artifact_fanout_provenance_record_read_string(stored_environment, &old_cursor, &old_value_hash) &&
                    build_artifact_fanout_provenance_record_read_string(plan.units[0].environment_context, &new_cursor, &new_name) &&
                    build_artifact_fanout_provenance_record_read_string(plan.units[0].environment_context, &new_cursor, &new_value_hash);
                if (parsed && (!string_equal(old_name, new_name) || !string_equal(old_value_hash, new_value_hash)))
                {
                    environment_difference = string_equal(old_name, new_name) ? old_name : new_name;
                    old_environment_value_hash = old_value_hash;
                    new_environment_value_hash = new_value_hash;
                    break;
                }
            }
            while (parsed && old_cursor < stored_environment.length && new_cursor < plan.units[0].environment_context.length)
            {
                String8 old_name = {0};
                String8 new_name = {0};
                String8 old_value_hash = {0};
                String8 new_value_hash = {0};
                u64 old_present = 0;
                u64 new_present = 0;
                parsed = build_artifact_fanout_provenance_record_read_string(stored_environment, &old_cursor, &old_name) &&
                    build_artifact_fanout_provenance_record_read_u64(stored_environment, &old_cursor, &old_present) &&
                    build_artifact_fanout_provenance_record_read_string(stored_environment, &old_cursor, &old_value_hash) &&
                    build_artifact_fanout_provenance_record_read_string(plan.units[0].environment_context, &new_cursor, &new_name) &&
                    build_artifact_fanout_provenance_record_read_u64(plan.units[0].environment_context, &new_cursor, &new_present) &&
                    build_artifact_fanout_provenance_record_read_string(plan.units[0].environment_context, &new_cursor, &new_value_hash);
                if (parsed && (!string_equal(old_name, new_name) || old_present != new_present || !string_equal(old_value_hash, new_value_hash)))
                {
                    environment_difference = old_name;
                    old_environment_value_hash = old_value_hash;
                    new_environment_value_hash = new_value_hash;
                    break;
                }
            }
        }
        string_print(S8("{S8} analyzer manifest differs from the selected database/options: {S8} stored_sha256={S8} selected_sha256={S8} first_diff_byte={u64} stored_bytes={u64} selected_bytes={u64} environment_change={S8} stored_environment_value_sha256={S8} selected_environment_value_sha256={S8}\n"),
            clang_analyze_error_prefix(), options.results, clang_analyze_sha256(arena, stored), clang_analyze_sha256(arena, plan.manifest),
            different, stored.length, plan.manifest.length, environment_difference,
            old_environment_value_hash.length ? old_environment_value_hash : S8("unavailable"),
            new_environment_value_hash.length ? new_environment_value_hash : S8("unavailable"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessSpawnResult clang_analyze_spawn(Arena* arena, SliceString8 command, String8 directory)
{
    bool changed;
#if BUSTER_WINDOWS
    char16 previous[BUSTER_KB(32) / sizeof(char16)];
    DWORD length = GetCurrentDirectoryW(BUSTER_ARRAY_LENGTH(previous), previous);
    String16 wide = string16_from_string8(arena, directory, true);
    changed = length && length < BUSTER_ARRAY_LENGTH(previous) && SetCurrentDirectoryW(wide.pointer);
#else
    char previous[BUSTER_KB(32)];
    String8 terminated = string_duplicate_arena(arena, directory, true);
    changed = getcwd(previous, sizeof(previous)) && chdir(terminated.pointer) == 0;
#endif
    ProcessSpawnResult result = {0};
    if (changed)
    {
        result = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1,
                                  .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR)});
#if BUSTER_WINDOWS
        BUSTER_CHECK(SetCurrentDirectoryW(previous));
#else
        BUSTER_CHECK(chdir(previous) == 0);
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_worker(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    String8 directory = clang_analyze_shard_directory(arena, options, options.shard);
    bool ready = clang_analyze_same_plan(arena, options, plan) && clang_analyze_new_directory(arena, directory);
    bool success = ready;
    u64* statuses = arena_allocate(arena, u64, plan.count);
    u64* durations = arena_allocate(arena, u64, plan.count);
    bool* executed = arena_allocate(arena, bool, plan.count);
    String8* fingerprints = arena_allocate(arena, String8, plan.count);
    memset(statuses, 0, plan.count * sizeof(*statuses));
    memset(durations, 0, plan.count * sizeof(*durations));
    memset(executed, 0, plan.count * sizeof(*executed));
    memset(fingerprints, 0, plan.count * sizeof(*fingerprints));
    String8List rows = {0};
    ClangAnalyzeContextCheckCache context_cache = {0};
    // Validate all proven representatives against one coherent preflight view.
    // Shared compiler/runtime files and include trees are hashed once per pass;
    // repeated rows still carry independently reconstructed proof records.
    u64 preflight_start = os_now_microseconds();
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard && unit.representative == i && unit.context_proof.length &&
            !clang_analyze_context_matches_cached(arena, unit, &context_cache))
        {
            statuses[i] = CLANG_ANALYZE_CONTEXT_FAILURE;
            success = false;
        }
    }
    u64 preflight_us = os_now_microseconds() - preflight_start;
    u64 selected = 0;
    u64 executions = 0;
    u64 aliases = 0;
    u64 start = os_now_microseconds();
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard) selected += 1;
        if (unit.shard == options.shard && unit.representative == i)
        {
            if (statuses[i] == CLANG_ANALYZE_CONTEXT_FAILURE) continue;
            TemporalArena temporary = scratch_begin(&arena, 1);
            Arena* scratch = temporary.arena;
            u64 unit_start = os_now_microseconds();
            u64 status = CLANG_ANALYZE_PASS;
            String8 log = {0};
            // Only the dedicated shard process changes its working directory.
            ProcessSpawnResult spawn = clang_analyze_spawn(scratch, unit.command, unit.entry.directory);
            executed[i] = spawn.handle != 0;
            ProcessWaitResult wait = os_process_wait_deadline(scratch, spawn, options.timeout * 1000000);
            String8 out = {.pointer = (char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, .length = wait.streams[STANDARD_STREAM_OUTPUT].length};
            String8 err = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
            bool warning = clang_analyze_output_has_warning(out) || clang_analyze_output_has_warning(err);
            status = !spawn.handle ? CLANG_ANALYZE_LAUNCH : wait.timed_out ? CLANG_ANALYZE_TIMEOUT :
                     wait.result == PROCESS_RESULT_CRASH ? CLANG_ANALYZE_CRASH : wait.result != PROCESS_RESULT_SUCCESS ? CLANG_ANALYZE_FAILURE : warning ? CLANG_ANALYZE_WARNING : CLANG_ANALYZE_PASS;
            String8 pieces[] = {out, err};
            log = string_join_arena(scratch, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true);
            String8 log_path = path_join(scratch, directory, string_format(scratch, S8("unit-{u64}.log"), i));
            if (!clang_analyze_write(scratch, log_path, log))
            {
                status = CLANG_ANALYZE_LOG_FAILURE;
            }
            u64 elapsed = os_now_microseconds() - unit_start;
            statuses[i] = status;
            durations[i] = elapsed;
            fingerprints[i] = string_duplicate_arena(arena, clang_analyze_sha256(scratch, log), true);
            executions += executed[i];
            success = success && status == CLANG_ANALYZE_PASS;
            scratch_end(temporary);
        }
    }
    // A later representative may have changed a source, header, search tree,
    // compiler, or runtime library used by an earlier one. Revalidate the
    // entire shard after all child processes, before any alias receives status.
    u64 postflight_start = os_now_microseconds();
    context_cache.file_count = 0;
    context_cache.directory_count = 0;
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard && unit.representative == i && unit.context_proof.length &&
            !clang_analyze_context_matches_cached(arena, unit, &context_cache))
        {
            statuses[i] = CLANG_ANALYZE_CONTEXT_FAILURE;
            success = false;
            String8 log_path = path_join(arena, directory, string_format(arena, S8("unit-{u64}.log"), i));
            String8 old_log = clang_analyze_read(arena, log_path);
            String8 pieces[] = {old_log, S8("\nAnalyzer input or toolchain context changed during the shard execution interval.\n")};
            String8 new_log = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true);
            bool wrote = clang_analyze_write(arena, log_path, new_log);
            if (wrote) fingerprints[i] = clang_analyze_sha256(arena, new_log);
            else statuses[i] = CLANG_ANALYZE_LOG_FAILURE;
        }
    }
    u64 postflight_us = os_now_microseconds() - postflight_start;
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard && unit.representative == i)
        {
            success = success && statuses[i] == CLANG_ANALYZE_PASS;
            String8 log_path = path_join(arena, directory, string_format(arena, S8("unit-{u64}.log"), i));
            if (statuses[i] == CLANG_ANALYZE_CONTEXT_FAILURE && fingerprints[i].length == 0)
            {
                String8 log = S8("Analyzer input or toolchain context was unavailable before execution.\n");
                bool wrote = clang_analyze_write(arena, log_path, log);
                if (wrote) fingerprints[i] = clang_analyze_sha256(arena, log);
                else statuses[i] = CLANG_ANALYZE_LOG_FAILURE;
            }
            if (!options.quiet || statuses[i] != CLANG_ANALYZE_PASS)
            {
                u64 executed_value = executed[i] ? (u64)1 : (u64)0;
                string_print(S8("ANALYZE_UNIT shard={u64} unit={u64} representative={u64} executed={u64} status={S8}{S8} elapsed_us={u64} file={S8} output={S8}\n"),
                             options.shard, i, i, executed_value, clang_analyze_status_name(statuses[i]),
                             clang_analyze_status_qualifier(statuses[i] != CLANG_ANALYZE_PASS), durations[i], unit.entry.file, unit.entry.output);
            }
            if (statuses[i] != CLANG_ANALYZE_PASS)
            {
                command_print(unit.command);
                string_print(S8("{S8}"), clang_analyze_read(arena, log_path));
            }
        }
    }
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard && unit.representative != i)
        {
            String8 representative_path = path_join(arena, directory, string_format(arena, S8("unit-{u64}.log"), unit.representative));
            String8 log = clang_analyze_read(arena, representative_path);
            String8 log_path = path_join(arena, directory, string_format(arena, S8("unit-{u64}.log"), i));
            bool written = log.pointer && clang_analyze_write(arena, log_path, log);
            statuses[i] = statuses[unit.representative];
            fingerprints[i] = log.pointer ? clang_analyze_sha256(arena, log) : S8("");
            if (!written) statuses[i] = CLANG_ANALYZE_LOG_FAILURE;
            aliases += 1;
            success = success && statuses[i] == CLANG_ANALYZE_PASS && written;
            if (!options.quiet || statuses[i] != CLANG_ANALYZE_PASS)
            {
                string_print(S8("ANALYZE_ALIAS shard={u64} unit={u64} representative={u64} executed=0 status={S8}{S8} file={S8} output={S8}\n"),
                             options.shard, i, unit.representative, clang_analyze_status_name(statuses[i]),
                             clang_analyze_status_qualifier(statuses[i] != CLANG_ANALYZE_PASS), unit.entry.file, unit.entry.output);
            }
        }
    }
    for (u64 i = 0; ready && i < plan.count; i += 1)
    {
        ClangAnalyzeUnit unit = plan.units[i];
        if (unit.shard == options.shard)
        {
            u64 executed_value = executed[i] ? (u64)1 : (u64)0;
            string8_list_push(arena, &rows, string_format(arena, S8("{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n{S8}\n"),
                i, unit.representative, executed_value, statuses[i], durations[i], fingerprints[i]));
        }
    }
    if (ready)
    {
        u64 elapsed = os_now_microseconds() - start;
        u64 rss = clang_analyze_child_peak_rss();
        String8 header = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n"),
                                       plan.fingerprint, options.shard, selected, executions, aliases, elapsed, rss);
        String8 pieces[] = {header, string_join_arena(arena, string8_list_to_slice(arena, rows), true)};
        String8 report = string_join_arena(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces), true);
        // The terminal record is written last. A killed worker has no complete
        // record, even if every earlier analyzer process happened to succeed.
        bool written = clang_analyze_write(arena, path_join(arena, directory, S8("result.txt")), report);
        success = success && written;
        string_print(S8("ANALYZE_SHARD shard={u64} selected_rows={u64} unique_executions={u64} aliased_rows={u64} elapsed_us={u64} context_preflight_us={u64} context_postflight_us={u64} peak_child_rss_bytes={u64} status={S8}{S8}\n"),
                     options.shard, selected, executions, aliases, elapsed, preflight_us, postflight_us, rss,
                     success ? S8("pass") : S8("fail"), clang_analyze_status_qualifier(!success));
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_aggregate(Arena* arena, ClangAnalyzeOptions options, ClangAnalyzePlan plan)
{
    bool success = clang_analyze_same_plan(arena, options, plan);
    bool* seen = arena_allocate(arena, bool, plan.count);
    u64* statuses = arena_allocate(arena, u64, plan.count);
    u64* representatives = arena_allocate(arena, u64, plan.count);
    String8* log_fingerprints = arena_allocate(arena, String8, plan.count);
    memset(seen, 0, plan.count * sizeof(*seen));
    memset(statuses, 0, plan.count * sizeof(*statuses));
    memset(representatives, 0, plan.count * sizeof(*representatives));
    memset(log_fingerprints, 0, plan.count * sizeof(*log_fingerprints));
    u64 checked = 0;
    u64 failures = 0;
    u64 max_rss = 0;
    u64 actual_executions = 0;
    u64 aliased_rows = 0;
    for (u64 shard = 0; shard < options.shards; shard += 1)
    {
        String8 path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("result.txt"));
        String8 text = clang_analyze_read(arena, path);
        u64 cursor = 0;
        String8 magic = {0};
        String8 fingerprint = {0};
        u64 recorded_shard = 0;
        u64 selected = 0;
        u64 executions = 0;
        u64 aliases = 0;
        u64 elapsed = 0;
        u64 rss = 0;
        bool valid = build_artifact_fanout_provenance_record_read_line(text, &cursor, &magic) &&
                     string_equal(magic, S8("BUSTER_CLANG_ANALYZE_RESULT_V2")) &&
                     build_artifact_fanout_provenance_record_read_line(text, &cursor, &fingerprint) && string_equal(fingerprint, plan.fingerprint) &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &recorded_shard) && recorded_shard == shard &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &selected) && selected <= plan.count &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &executions) && executions <= selected &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &aliases) && aliases <= selected &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &elapsed) &&
                     build_artifact_fanout_provenance_record_read_u64(text, &cursor, &rss);
        u64 expected_selected = 0;
        u64 expected_aliases = 0;
        for (u64 i = 0; i < plan.count; i += 1)
        {
            if (plan.units[i].shard == shard)
            {
                expected_selected += 1;
                expected_aliases += plan.units[i].representative != i;
            }
        }
        valid = valid && selected == expected_selected && aliases == expected_aliases && executions <= selected - aliases;
        u64 shard_executions = 0;
        u64 shard_aliases = 0;
        for (u64 row = 0; valid && row < selected; row += 1)
        {
            u64 index = 0;
            u64 representative = 0;
            u64 launched = 0;
            u64 status = 0;
            u64 duration = 0;
            String8 log_fingerprint = {0};
            valid = build_artifact_fanout_provenance_record_read_u64(text, &cursor, &index) && index < plan.count && !seen[index] &&
                    plan.units[index].shard == shard && build_artifact_fanout_provenance_record_read_u64(text, &cursor, &representative) &&
                    representative < plan.count && representative == plan.units[index].representative &&
                    plan.units[representative].representative == representative && plan.units[representative].shard == shard &&
                    build_artifact_fanout_provenance_record_read_u64(text, &cursor, &launched) && launched <= 1 &&
                    build_artifact_fanout_provenance_record_read_u64(text, &cursor, &status) && status < CLANG_ANALYZE_STATUS_COUNT &&
                    build_artifact_fanout_provenance_record_read_u64(text, &cursor, &duration) &&
                    build_artifact_fanout_provenance_record_read_line(text, &cursor, &log_fingerprint);
            if (valid)
            {
                seen[index] = true;
                statuses[index] = status;
                representatives[index] = representative;
                log_fingerprints[index] = log_fingerprint;
                shard_executions += launched;
                shard_aliases += representative != index;
                if (representative != index)
                {
                    valid = !launched && !duration;
                }
                else if (launched)
                {
                    valid = status != CLANG_ANALYZE_LAUNCH;
                }
                else
                {
                    valid = status == CLANG_ANALYZE_CONTEXT_FAILURE || status == CLANG_ANALYZE_LAUNCH;
                }
            }
        }
        valid = valid && shard_executions == executions && shard_aliases == aliases && cursor == text.length;
        if (!valid)
        {
            string_print(S8("{S8} missing, duplicate, stale or malformed analyzer result: shard={u64} path={S8}\n"), clang_analyze_error_prefix(), shard, path);
        }
        if (valid)
        {
            actual_executions += executions;
            aliased_rows += aliases;
            if (rss > max_rss) max_rss = rss;
        }
        success = success && valid;
    }
    for (u64 i = 0; i < plan.count; i += 1)
    {
        if (!seen[i])
        {
            string_print(S8("{S8} missing analyzer TU: shard={u64} file={S8}\n"), clang_analyze_error_prefix(), plan.units[i].shard, plan.units[i].entry.file);
            success = false;
        }
        else
        {
            u64 representative = representatives[i];
            String8 shard_directory = clang_analyze_shard_directory(arena, options, plan.units[i].shard);
            String8 path = path_join(arena, shard_directory, string_format(arena, S8("unit-{u64}.log"), i));
            String8 log = clang_analyze_read(arena, path);
            bool log_valid = log.pointer && string_equal(log_fingerprints[i], clang_analyze_sha256(arena, log));
            if (log_valid && representative != i)
            {
                String8 representative_path = path_join(arena, shard_directory, string_format(arena, S8("unit-{u64}.log"), representative));
                String8 representative_log = clang_analyze_read(arena, representative_path);
                log_valid = representative_log.pointer && string_equal(log_fingerprints[i], log_fingerprints[representative]) &&
                            string_equal(log, representative_log);
            }
            if (!log_valid)
            {
                string_print(S8("{S8} missing, changed or unbound analyzer log: shard={u64} file={S8} representative={u64}\n"),
                             clang_analyze_error_prefix(), plan.units[i].shard, plan.units[i].entry.file, representative);
                success = false;
            }
            checked += 1;
            if (statuses[i] != CLANG_ANALYZE_PASS)
            {
                failures += 1;
                string_print(S8("{S8} analyzer shard={u64} status={S8} representative={u64} file={S8} output={S8}\n"),
                             clang_analyze_error_prefix(), plan.units[i].shard, clang_analyze_status_name(statuses[i]), representative,
                             plan.units[i].entry.file, plan.units[i].entry.output);
            }
            success = success && log_valid;
        }
    }
    for (u64 i = 0; i < plan.count; i += 1)
    {
        if (seen[i] && representatives[i] != i)
        {
            u64 representative = representatives[i];
            success = success && (statuses[i] == statuses[representative] || statuses[i] == CLANG_ANALYZE_LOG_FAILURE);
        }
    }
    success = success && !failures && checked == plan.count && actual_executions == plan.unique_executions && aliased_rows == plan.aliased_rows;
    string_print(S8("ANALYZE_AGGREGATE selected_rows={u64} checked={u64} unique_executions={u64} aliased_rows={u64} excluded_config_or_language={u64} failures={u64} shards={u64} peak_child_rss_bytes={u64} status={S8}{S8}\n"),
                 plan.count, checked, actual_executions, aliased_rows, plan.excluded, failures, options.shards, max_rss,
                 success ? S8("pass") : S8("fail"), clang_analyze_status_qualifier(!success));
    return success;
}

BUSTER_GLOBAL_LOCAL SliceString8 clang_analyze_worker_command(Arena* arena, ClangAnalyzeOptions options, u64 shard)
{
    // The argument builder stores a contiguous array in this arena. Allocate
    // all string payloads before starting it, never between append operations.
    String8 self = os_path_absolute(arena, program_state->input.arguments.pointer[0], true);
    String8 shards_text = string_format(arena, S8("{u64}"), options.shards);
    String8 shard_text = string_format(arena, S8("{u64}"), shard);
    String8 timeout_text = string_format(arena, S8("{u64}"), options.timeout);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("clang_analyze"));
    os_argument_builder_append(&builder, options.database);
    os_argument_builder_append(&builder, S8("--results"));
    os_argument_builder_append(&builder, options.results);
    os_argument_builder_append(&builder, S8("--shards"));
    os_argument_builder_append(&builder, shards_text);
    os_argument_builder_append(&builder, S8("--shard"));
    os_argument_builder_append(&builder, shard_text);
    os_argument_builder_append(&builder, S8("--timeout"));
    os_argument_builder_append(&builder, timeout_text);
    if (options.config.length)
    {
        os_argument_builder_append(&builder, S8("--config"));
        os_argument_builder_append(&builder, options.config);
    }
    if (options.clang.length)
    {
        os_argument_builder_append(&builder, S8("--clang"));
        os_argument_builder_append(&builder, options.clang);
    }
    if (options.quiet)
    {
        os_argument_builder_append(&builder, S8("--quiet"));
    }
    if (clang_analyze_expecting_rejection)
    {
        // Carry the self-test scope into the child; it never changes its result.
        os_argument_builder_append(&builder, S8("--expect-rejection"));
    }
    SliceString8 result = os_argument_builder_flush(&builder);
    return result;
}

typedef enum ClangAnalyzeTreeStatus ClangAnalyzeTreeStatus;
enum ClangAnalyzeTreeStatus
{
    CLANG_ANALYZE_TREE_COMPLETE,
    CLANG_ANALYZE_TREE_INCOMPLETE,
    CLANG_ANALYZE_TREE_UNAVAILABLE,
};

typedef enum ClangAnalyzeTreeReason ClangAnalyzeTreeReason;
enum ClangAnalyzeTreeReason
{
    CLANG_ANALYZE_TREE_REASON_NONE,
    CLANG_ANALYZE_TREE_REASON_UNSUPPORTED_HOST,
    CLANG_ANALYZE_TREE_REASON_ROOT_PID_UNAVAILABLE,
    CLANG_ANALYZE_TREE_REASON_PROC_PATH_TOO_LONG,
    CLANG_ANALYZE_TREE_REASON_STATM_UNAVAILABLE,
    CLANG_ANALYZE_TREE_REASON_STATM_UNREADABLE,
    CLANG_ANALYZE_TREE_REASON_CHILDREN_UNAVAILABLE,
    CLANG_ANALYZE_TREE_REASON_CHILDREN_READ_FAILED,
    CLANG_ANALYZE_TREE_REASON_INVALID_CHILD_PID,
    CLANG_ANALYZE_TREE_REASON_PROCESS_TABLE_FULL,
    CLANG_ANALYZE_TREE_REASON_CHILD_LIST_READ_LIMIT,
    CLANG_ANALYZE_TREE_REASON_NO_RSS_SAMPLES,
    CLANG_ANALYZE_TREE_REASON_PARENTAGE_UNAVAILABLE,
};

typedef struct ClangAnalyzeResources ClangAnalyzeResources;
struct ClangAnalyzeResources
{
    u64 samples;
    u64 peak_processes;
    u64 peak_tree_rss;
    ClangAnalyzeTreeStatus tree_status;
    ClangAnalyzeTreeReason tree_reason;
};

typedef struct ClangAnalyzeChildListBudget ClangAnalyzeChildListBudget;
struct ClangAnalyzeChildListBudget
{
    u64 reads;
    bool exhausted;
};

BUSTER_GLOBAL_LOCAL String8 clang_analyze_tree_status_name(ClangAnalyzeTreeStatus status)
{
    String8 result = S8("invalid");
    switch (status)
    {
        case CLANG_ANALYZE_TREE_COMPLETE: result = S8("complete"); break;
        case CLANG_ANALYZE_TREE_INCOMPLETE: result = S8("incomplete"); break;
        case CLANG_ANALYZE_TREE_UNAVAILABLE: result = S8("unavailable"); break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_tree_reason_name(ClangAnalyzeTreeReason reason)
{
    String8 names[] = {S8("none"), S8("unsupported-host"), S8("root-pid-unavailable"), S8("proc-path-too-long"),
                       S8("statm-unavailable"), S8("statm-unreadable"), S8("children-unavailable"),
                       S8("children-read-failed"), S8("invalid-child-pid"), S8("process-table-full"),
                       S8("child-list-read-limit"), S8("no-rss-samples"), S8("parentage-unavailable")};
    u64 reason_index = (u64)reason;
    String8 result = reason_index < BUSTER_ARRAY_LENGTH(names) ? names[reason_index] : S8("invalid");
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_resources_begin(ClangAnalyzeResources* resources)
{
    memset(resources, 0, sizeof(*resources));
#if !BUSTER_LINUX
    resources->tree_status = CLANG_ANALYZE_TREE_UNAVAILABLE;
    resources->tree_reason = CLANG_ANALYZE_TREE_REASON_UNSUPPORTED_HOST;
#endif
}

BUSTER_GLOBAL_LOCAL void clang_analyze_resources_mark_incomplete(ClangAnalyzeResources* resources, ClangAnalyzeTreeReason reason)
{
    if (resources->tree_status == CLANG_ANALYZE_TREE_COMPLETE)
    {
        resources->tree_status = CLANG_ANALYZE_TREE_INCOMPLETE;
        resources->tree_reason = reason;
    }
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_proc_state(const char* proc_root, u32 pid, char* state, u32* parent_pid)
{
    char path[512];
    int path_length = snprintf(path, sizeof(path), "%s/%u/stat", proc_root, pid);
    FILE* file = path_length > 0 && (u64)path_length < sizeof(path) ? fopen(path, "r") : 0;
    bool result = false;
    if (file)
    {
        char line[4096];
        if (fgets(line, sizeof(line), file))
        {
            char* close = strrchr(line, ')');
            unsigned long long parent = 0;
            if (close && close[1] == ' ' && close[2] && sscanf(close + 3, "%llu", &parent) == 1 && parent <= UINT32_MAX)
            {
                *state = close[2];
                *parent_pid = (u32)parent;
                result = true;
            }
        }
        fclose(file);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int clang_analyze_proc_child_membership(const char* proc_root, u32 parent_pid, u32 child_pid,
                                                             ClangAnalyzeChildListBudget* budget)
{
    char path[512];
    int path_length = snprintf(path, sizeof(path), "%s/%u/task/%u/children", proc_root, parent_pid, parent_pid);
    FILE* file = path_length > 0 && (u64)path_length < sizeof(path) ? fopen(path, "r") : 0;
    int result = -1;
    if (file)
    {
        for (;;)
        {
            if (budget->reads >= BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS)
            {
                budget->exhausted = true;
                break;
            }
            budget->reads += 1;
            unsigned long long child = 0;
            int parsed = fscanf(file, "%llu", &child);
            if (parsed == 1)
            {
                if (!child || child > UINT32_MAX) break;
                if ((u32)child == child_pid)
                {
                    result = 1;
                    break;
                }
            }
            else
            {
                if (parsed == EOF && !ferror(file)) result = 0;
                break;
            }
        }
        fclose(file);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int clang_analyze_proc_process_status(const char* proc_root, u32 pid, u32 parent_pid,
                                                           ClangAnalyzeChildListBudget* budget)
{
    char state = 0;
    u32 actual_parent = 0;
    bool state_read = clang_analyze_proc_state(proc_root, pid, &state, &actual_parent);
    int result = -1;
    if (state_read)
    {
        result = state == 'Z' || state == 'X' || state == 'x' || (parent_pid && actual_parent != parent_pid);
    }
    else if (!parent_pid) result = 0;
    else if (!budget->exhausted)
    {
        int membership = clang_analyze_proc_child_membership(proc_root, parent_pid, pid, budget);
        if (membership >= 0) result = membership ? 0 : 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL int clang_analyze_proc_sample_process_status(ClangAnalyzeResources* resources, const char* proc_root,
                                                                  u32 pid, u32 parent_pid,
                                                                  ClangAnalyzeChildListBudget* budget)
{
    int result = clang_analyze_proc_process_status(proc_root, pid, parent_pid, budget);
    if (result < 0) clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_PARENTAGE_UNAVAILABLE);
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_resources_stop_child_scan_if_limited(ClangAnalyzeResources* resources,
                                                                             ClangAnalyzeChildListBudget* budget,
                                                                             bool* child_list_scan_stopped)
{
    if (budget->exhausted)
    {
        clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_CHILD_LIST_READ_LIMIT);
        *child_list_scan_stopped = true;
    }
}

BUSTER_GLOBAL_LOCAL void clang_analyze_sample_resources_from_proc(ClangAnalyzeResources* resources, u32 root_pid,
                                                                   const char* proc_root, u64 page_size)
{
    u32 pids[BUSTER_ANALYZE_PROCESS_TREE_MAX_PIDS];
    u32 parents[BUSTER_ARRAY_LENGTH(pids)];
    u64 count = root_pid ? 1 : 0;
    u64 rss = 0;
    u64 processes = 0;
    ClangAnalyzeChildListBudget child_list_budget = {0};
    bool child_list_scan_stopped = false;
    if (root_pid)
    {
        pids[0] = root_pid;
        parents[0] = 0;
    }
    else clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_ROOT_PID_UNAVAILABLE);
    for (u64 i = 0; i < count; i += 1)
    {
        int process_status = clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget);
        bool exited = process_status > 0;
        if (!exited)
        {
            char statm_path[512];
            int statm_path_length = snprintf(statm_path, sizeof(statm_path), "%s/%u/statm", proc_root, pids[i]);
            bool statm_path_valid = statm_path_length > 0 && (u64)statm_path_length < sizeof(statm_path);
            FILE* statm = statm_path_valid ? fopen(statm_path, "r") : 0;
            bool statm_read = false;
            unsigned long long size = 0;
            unsigned long long resident = 0;
            if (!statm_path_valid)
            {
                clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_PROC_PATH_TOO_LONG);
            }
            else if (statm)
            {
                int parsed = fscanf(statm, "%llu %llu", &size, &resident);
                statm_read = parsed == 2;
                fclose(statm);
                if (!statm_read)
                {
                    int process_status = clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget);
                    clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
                    if (process_status <= 0)
                    {
                        clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_STATM_UNREADABLE);
                    }
                }
            }
            else
            {
                int process_status = clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget);
                clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
                if (process_status <= 0)
                {
                    clang_analyze_resources_mark_incomplete(resources, statm_path_valid ? CLANG_ANALYZE_TREE_REASON_STATM_UNAVAILABLE :
                                                                                         CLANG_ANALYZE_TREE_REASON_PROC_PATH_TOO_LONG);
                }
            }
            if (!child_list_scan_stopped)
            {
                char children_path[512];
                int children_path_length = snprintf(children_path, sizeof(children_path), "%s/%u/task/%u/children", proc_root, pids[i], pids[i]);
                bool children_path_valid = children_path_length > 0 && (u64)children_path_length < sizeof(children_path);
                FILE* children = children_path_valid ? fopen(children_path, "r") : 0;
                if (!children_path_valid)
                {
                    clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_PROC_PATH_TOO_LONG);
                }
                else if (children)
                {
                    for (;;)
                    {
                        if (child_list_budget.reads >= BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS)
                        {
                            child_list_budget.exhausted = true;
                            clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
                            break;
                        }
                        child_list_budget.reads += 1;
                        unsigned long long child = 0;
                        int parsed = fscanf(children, "%llu", &child);
                        if (parsed == 1)
                        {
                            if (!child || child > UINT32_MAX)
                            {
                                clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_INVALID_CHILD_PID);
                                break;
                            }
                            bool seen = false;
                            for (u64 previous = 0; !seen && previous < count; previous += 1)
                            {
                                seen = pids[previous] == (u32)child;
                            }
                            if (!seen)
                            {
                                if (count < BUSTER_ARRAY_LENGTH(pids))
                                {
                                    pids[count] = (u32)child;
                                    parents[count] = pids[i];
                                    count += 1;
                                    if (count == BUSTER_ARRAY_LENGTH(pids))
                                    {
                                        clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_PROCESS_TABLE_FULL);
                                        child_list_scan_stopped = true;
                                        break;
                                    }
                                }
                                else
                                {
                                    clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_PROCESS_TABLE_FULL);
                                    child_list_scan_stopped = true;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            bool malformed = parsed != EOF;
                            bool read_failed = false;
                            if (ferror(children))
                            {
                                int process_status = clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget);
                                clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
                                read_failed = process_status <= 0;
                            }
                            if (malformed || read_failed)
                            {
                                clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_CHILDREN_READ_FAILED);
                            }
                            break;
                        }
                    }
                    fclose(children);
                }
                else
                {
                    int process_status = clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget);
                    clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
                    if (process_status <= 0)
                    {
                        clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_CHILDREN_UNAVAILABLE);
                    }
                }
            }
            int final_process_status = statm_read && resident ?
                clang_analyze_proc_sample_process_status(resources, proc_root, pids[i], parents[i], &child_list_budget) : 0;
            clang_analyze_resources_stop_child_scan_if_limited(resources, &child_list_budget, &child_list_scan_stopped);
            if (statm_read && resident && final_process_status == 0)
            {
                rss += (u64)resident * page_size;
                processes += 1;
            }
        }
    }
    if (processes)
    {
        resources->samples += 1;
        if (processes > resources->peak_processes) resources->peak_processes = processes;
        if (rss > resources->peak_tree_rss) resources->peak_tree_rss = rss;
    }
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_proc_self_pid(u32* pid)
{
#if BUSTER_LINUX
    char target[64];
    ssize_t length = readlink("/proc/self", target, sizeof(target));
    u64 value = 0;
    bool valid = length > 0 && (u64)length < sizeof(target);
    for (ssize_t i = 0; valid && i < length; i += 1)
    {
        char digit = target[i];
        valid = digit >= '0' && digit <= '9' && value <= (UINT32_MAX - (u64)(digit - '0')) / 10;
        if (valid) value = value * 10 + (u64)(digit - '0');
    }
    valid = valid && value != 0;
    if (valid) *pid = (u32)value;
#else
    BUSTER_UNUSED(pid);
    bool valid = false;
#endif
    return valid;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_sample_resources(ClangAnalyzeResources* resources)
{
#if BUSTER_LINUX
    u32 root_pid = 0;
    if (clang_analyze_proc_self_pid(&root_pid))
    {
        // /proc/self resolves the process ID in the mounted procfs namespace;
        // getpid() can name a different PID in nested namespace arrangements.
        clang_analyze_sample_resources_from_proc(resources, root_pid, "/proc", os_get_page_size());
    }
    else
    {
        clang_analyze_resources_mark_incomplete(resources, CLANG_ANALYZE_TREE_REASON_ROOT_PID_UNAVAILABLE);
    }
#else
    BUSTER_UNUSED(resources);
#endif
}

BUSTER_GLOBAL_LOCAL void clang_analyze_resources_finish(ClangAnalyzeResources* resources)
{
    if (!resources->samples && resources->tree_status != CLANG_ANALYZE_TREE_UNAVAILABLE)
    {
        resources->tree_status = CLANG_ANALYZE_TREE_UNAVAILABLE;
        if (resources->tree_reason == CLANG_ANALYZE_TREE_REASON_NONE)
        {
            resources->tree_reason = CLANG_ANALYZE_TREE_REASON_NO_RSS_SAMPLES;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_missing_children_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-missing-children"));
    String8 process = path_join(arena, proc_root, S8("100"));
    make_directory_recursive(arena, path_join(arena, process, S8("task/100")));
    bool written = clang_analyze_write(arena, path_join(arena, process, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, process, S8("stat")), S8("100 (coordinator) S 1 0 0\n"));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    result = written && resources.samples == 1 && resources.peak_processes == 1 &&
             resources.peak_tree_rss == 10 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_INCOMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_CHILDREN_UNAVAILABLE;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_complete_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-complete"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    String8 child = path_join(arena, proc_root, S8("101"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    make_directory_recursive(arena, path_join(arena, child, S8("task/101")));
    bool written = clang_analyze_write(arena, path_join(arena, coordinator, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("stat")), S8("100 (coordinator) S 1 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("task/100/children")), S8("101\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("statm")), S8("20 5 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("stat")), S8("101 (worker) S 100 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("task/101/children")), S8(""));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    result = written && resources.samples == 1 && resources.peak_processes == 2 &&
             resources.peak_tree_rss == 15 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_COMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_NONE;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_exit_race_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-exit-race"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    String8 child = path_join(arena, proc_root, S8("101"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    make_directory_recursive(arena, child);
    bool written = clang_analyze_write(arena, path_join(arena, coordinator, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("stat")), S8("100 (coordinator) S 1 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("task/100/children")), S8("101\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("statm")), S8("20 5 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("stat")), S8("101 (finished) Z 100 0 0\n"));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    result = written && resources.samples == 1 && resources.peak_processes == 1 &&
             resources.peak_tree_rss == 10 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_COMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_NONE;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_vanished_child_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-vanished-child"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    String8 children_path = path_join(arena, coordinator, S8("task/100/children"));
    bool written = clang_analyze_write(arena, children_path, S8("101\n"));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeChildListBudget budget = {0};
    bool child_was_listed = written && clang_analyze_proc_child_membership(terminated_root.pointer, 100, 101, &budget) == 1;
    bool removed = clang_analyze_write(arena, children_path, S8(""));
    result = child_was_listed && removed && clang_analyze_proc_child_membership(terminated_root.pointer, 100, 101, &budget) == 0 &&
             clang_analyze_proc_process_status(terminated_root.pointer, 101, 100, &budget) == 1;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_child_list_budget_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-child-list-read-limit"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    String8 child = path_join(arena, proc_root, S8("101"));
    String8 later_child = path_join(arena, proc_root, S8("102"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    make_directory_recursive(arena, path_join(arena, child, S8("task/101")));
    make_directory_recursive(arena, path_join(arena, later_child, S8("task/102")));
    u64 child_list_length = (BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS + 1) * 4;
    char8* child_list_bytes = arena_allocate(arena, char8, child_list_length);
    for (u64 i = 0; i < BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS; i += 1)
    {
        memcpy(child_list_bytes + i * 4, "101\n", 4);
    }
    memcpy(child_list_bytes + BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS * 4, "102\n", 4);
    String8 child_list = {.pointer = child_list_bytes, .length = child_list_length};
    bool written = clang_analyze_write(arena, path_join(arena, coordinator, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("stat")), S8("100 (coordinator) S 1 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("task/100/children")), child_list) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("statm")), S8("20 5 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("task/101/children")), S8("")) &&
                   clang_analyze_write(arena, path_join(arena, later_child, S8("statm")), S8("30 7 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, later_child, S8("stat")), S8("102 (later-worker) S 100 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, later_child, S8("task/102/children")), S8(""));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeChildListBudget found_budget = {0};
    bool found_early = clang_analyze_proc_child_membership(terminated_root.pointer, 100, 101, &found_budget) == 1 &&
                       found_budget.reads == 1 && !found_budget.exhausted;
    ClangAnalyzeChildListBudget unknown_budget = {.reads = BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS - 1};
    bool absence_unknown = clang_analyze_proc_child_membership(terminated_root.pointer, 100, 102, &unknown_budget) == -1 &&
                           unknown_budget.exhausted && unknown_budget.reads == BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS;
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    ClangAnalyzeChildListBudget parentage_budget = {.reads = BUSTER_ANALYZE_PROCESS_TREE_MAX_CHILD_LIST_READS};
    int parentage_status = clang_analyze_proc_process_status(terminated_root.pointer, 101, 100, &parentage_budget);
    ClangAnalyzeResources unknown_parentage;
    clang_analyze_resources_begin(&unknown_parentage);
    ClangAnalyzeChildListBudget unknown_parentage_budget = {0};
    int unknown_parentage_status = clang_analyze_proc_sample_process_status(&unknown_parentage, terminated_root.pointer, 101, 103,
                                                                            &unknown_parentage_budget);
    result = written && found_early && absence_unknown && parentage_status < 0 && unknown_parentage_status < 0 &&
             unknown_parentage.tree_status == CLANG_ANALYZE_TREE_INCOMPLETE &&
             unknown_parentage.tree_reason == CLANG_ANALYZE_TREE_REASON_PARENTAGE_UNAVAILABLE &&
             resources.samples == 1 && resources.peak_processes == 1 &&
             resources.peak_tree_rss == 10 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_INCOMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_CHILD_LIST_READ_LIMIT;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_child_statm_failure_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-child-statm-failure"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    String8 child = path_join(arena, proc_root, S8("101"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    make_directory_recursive(arena, path_join(arena, child, S8("task/101")));
    bool written = clang_analyze_write(arena, path_join(arena, coordinator, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("stat")), S8("100 (coordinator) S 1 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("task/100/children")), S8("101\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("stat")), S8("101 (worker) S 100 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("task/101/children")), S8(""));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    result = written && resources.samples == 1 && resources.peak_processes == 1 &&
             resources.peak_tree_rss == 10 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_INCOMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_STATM_UNAVAILABLE;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_tree_child_children_failure_control(Arena* arena, String8 root)
{
    bool result = false;
#if BUSTER_LINUX
    String8 proc_root = path_join(arena, root, S8("proc-child-children-failure"));
    String8 coordinator = path_join(arena, proc_root, S8("100"));
    String8 child = path_join(arena, proc_root, S8("101"));
    make_directory_recursive(arena, path_join(arena, coordinator, S8("task/100")));
    make_directory_recursive(arena, path_join(arena, child, S8("task/101")));
    bool written = clang_analyze_write(arena, path_join(arena, coordinator, S8("statm")), S8("100 10 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("stat")), S8("100 (coordinator) S 1 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, coordinator, S8("task/100/children")), S8("101\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("statm")), S8("20 5 0 0 0 0 0\n")) &&
                   clang_analyze_write(arena, path_join(arena, child, S8("stat")), S8("101 (worker) S 100 0 0\n"));
    String8 terminated_root = string_duplicate_arena(arena, proc_root, true);
    ClangAnalyzeResources resources;
    clang_analyze_resources_begin(&resources);
    clang_analyze_sample_resources_from_proc(&resources, 100, terminated_root.pointer, os_get_page_size());
    result = written && resources.samples == 1 && resources.peak_processes == 2 &&
             resources.peak_tree_rss == 15 * os_get_page_size() && resources.tree_status == CLANG_ANALYZE_TREE_INCOMPLETE &&
             resources.tree_reason == CLANG_ANALYZE_TREE_REASON_CHILDREN_UNAVAILABLE;
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(root);
    result = true;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_proc_self_control(void)
{
    bool result = true;
#if BUSTER_LINUX
    u32 pid = 0;
    bool valid = clang_analyze_proc_self_pid(&pid);
    char path[64];
    int path_length = valid ? snprintf(path, sizeof(path), "/proc/%u/statm", pid) : -1;
    FILE* file = path_length > 0 && (u64)path_length < sizeof(path) ? fopen(path, "r") : 0;
    result = file != 0;
    if (file) fclose(file);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_finished(ProcessSpawnResult spawn)
{
    bool result = !spawn.handle;
    if (spawn.handle)
    {
#if BUSTER_WINDOWS
        result = WaitForSingleObject(spawn.handle, 0) != WAIT_TIMEOUT;
#else
        siginfo_t information = {0};
        int status = waitid(P_PID, (id_t)(uintptr_t)spawn.handle, &information, WEXITED | WNOHANG | WNOWAIT);
        result = information.si_pid != 0 || (status < 0 && errno != EINTR);
#endif
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_sample_pause(void)
{
#if BUSTER_WINDOWS
    Sleep(25);
#else
    poll(0, 0, 25);
#endif
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_schedule_shard(u64 shards, u64 ordinal)
{
    // Two complete 182-TU Ubuntu inventories have this duration ranking (#3103).
    // This is launch priority only: FNV ownership and the manifest stay unchanged.
    // Other cardinalities retain numeric order; no prior result skips any work.
    const u64 priority[] = {2, 7, 3, 0, 6, 5, 1, 4};
    BUSTER_CHECK(ordinal < shards);
    u64 result = ordinal;
    if (shards == BUSTER_ARRAY_LENGTH(priority))
    {
        result = priority[ordinal];
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_run(Arena* arena, ClangAnalyzeOptions options)
{
    u64 setup_start = os_now_microseconds();
    ClangAnalyzePlan plan;
    bool success = clang_analyze_plan(arena, options, &plan);
    u64 planning_us = os_now_microseconds() - setup_start;
    if (success && options.worker)
    {
        string_print(S8("ANALYZE_PLAN mode=worker shard={u64} selected_rows={u64} unique_executions={u64} aliases={u64} planning_us={u64} context_proof_us={u64}\n"),
            options.shard, plan.count, plan.unique_executions, plan.aliased_rows, planning_us, plan.context_proof_us);
    }
    else if (success && options.aggregate)
    {
        string_print(S8("ANALYZE_PLAN mode=aggregate selected_rows={u64} unique_executions={u64} aliases={u64} planning_us={u64} context_proof_us={u64}\n"),
            plan.count, plan.unique_executions, plan.aliased_rows, planning_us, plan.context_proof_us);
    }
    else if (success && !options.prepare)
    {
        string_print(S8("ANALYZE_PLAN mode=run selected_rows={u64} unique_executions={u64} aliases={u64} planning_us={u64} context_proof_us={u64}\n"),
            plan.count, plan.unique_executions, plan.aliased_rows, planning_us, plan.context_proof_us);
    }
    if (success && options.worker)
    {
        success = clang_analyze_worker(arena, options, plan);
    }
    else if (success && options.aggregate)
    {
        success = clang_analyze_aggregate(arena, options, plan);
    }
    else if (success)
    {
        success = clang_analyze_new_directory(arena, options.results) &&
                  clang_analyze_write(arena, path_join(arena, options.results, S8("manifest.txt")), plan.manifest);
        if (success && options.prepare)
        {
            string_print(S8("ANALYZE_PREPARE selected_rows={u64} unique_executions={u64} aliases={u64} candidate_groups={u64} proven_groups={u64} excluded_config_or_language={u64} planning_us={u64} context_proof_us={u64} status={S8}\n"),
                plan.count, plan.unique_executions, plan.aliased_rows, plan.candidate_groups, plan.proven_groups, plan.excluded,
                planning_us, plan.context_proof_us, success ? S8("pass") : S8("fail"));
        }
        if (success && !options.prepare)
        {
            u64 setup_us = os_now_microseconds() - setup_start;
            u64 start = os_now_microseconds();
            u64 peak_pending = 0;
            u64 next_ordinal = 0;
            u64 completed = 0;
            u64 pending = 0;
            ClangAnalyzeResources resources;
            clang_analyze_resources_begin(&resources);
            ProcessSpawnResult* spawns = arena_allocate(arena, ProcessSpawnResult, options.shards);
            bool* active = arena_allocate(arena, bool, options.shards);
            memset(active, 0, options.shards * sizeof(*active));
            while (completed < options.shards)
            {
                while (next_ordinal < options.shards && pending < options.jobs)
                {
                    u64 shard = clang_analyze_schedule_shard(options.shards, next_ordinal);
                    string_print(S8("ANALYZE_DISPATCH ordinal={u64} shard={u64}\n"), next_ordinal, shard);
                    SliceString8 command = clang_analyze_worker_command(arena, options, shard);
                    spawns[shard] = os_process_spawn(command, (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
                    active[shard] = true;
                    next_ordinal += 1;
                    pending += 1;
                }
                if (pending > peak_pending) peak_pending = pending;
                clang_analyze_sample_resources(&resources);
                for (u64 shard = 0; shard < options.shards; shard += 1)
                {
                    if (active[shard] && clang_analyze_finished(spawns[shard]))
                    {
                        // No captured coordinator pipes: a finished worker can
                        // be reaped immediately. Its own TU waits drain both streams.
                        ProcessWaitResult wait = os_process_wait_sync(arena, spawns[shard]);
                        if (wait.result != PROCESS_RESULT_SUCCESS)
                        {
                            string_print(S8("{S8} analyzer worker failed: shard={u64}\n"), clang_analyze_error_prefix(), shard);
                            success = false;
                        }
                        active[shard] = false;
                        pending -= 1;
                        completed += 1;
                    }
                }
                if (pending) clang_analyze_sample_pause();
            }
            // Always aggregate, even when a worker failed or never launched.
            bool aggregate = clang_analyze_aggregate(arena, options, plan);
            success = success && aggregate;
            clang_analyze_resources_finish(&resources);
            String8 record = string_format(arena, S8("ANALYZE_RUN elapsed_us={u64} peak_pending_workers={u64} jobs={u64} samples={u64} peak_live_processes={u64} sampled_peak_tree_rss_bytes={u64} process_tree_status={S8} process_tree_reason={S8} results={S8} status={S8}{S8}\n"),
                         os_now_microseconds() - start + setup_us, peak_pending, options.jobs, resources.samples, resources.peak_processes, resources.peak_tree_rss,
                         clang_analyze_tree_status_name(resources.tree_status), clang_analyze_tree_reason_name(resources.tree_reason), options.results,
                         success ? S8("pass") : S8("fail"), clang_analyze_status_qualifier(!success));
            string_print(S8("{S8}"), record);
            if (options.run_record) *options.run_record = record;
        }
    }
    return success;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_self_test(Arena* arena);

BUSTER_GLOBAL_LOCAL u64 clang_analyze_children_cpu_microseconds(void)
{
    u64 result = 0;
#if BUSTER_LINUX || BUSTER_APPLE
    struct rusage usage = {0};
    if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
    {
        result = (u64)(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000 +
                 (u64)(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec);
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_qualify_workers(Arena* arena, ClangAnalyzeOptions options)
{
    // Counterbalance order without running competing arms on the same host.
    // Each arm still uses the ordinary scheduler, deadlines and aggregate.
    u64 jobs[BUSTER_ANALYZE_QUALIFICATION_SAMPLES] = {2, 4, 4, 2};
    u64 cpus = os_get_logical_thread_count();
    ClangAnalyzePlan frozen;
    bool success = cpus >= 4 && options.shards >= 4 && clang_analyze_plan(arena, options, &frozen);
    if (!success) string_print(S8("{S8} worker qualification requires at least four host logical CPUs, four shards and a valid inventory\n"), clang_analyze_error_prefix());
    success = success && clang_analyze_new_directory(arena, options.results);
    if (success)
    {
        String8List records = {0};
        string8_list_push(arena, &records, S8("BUSTER_ANALYZE_WORKER_QUALIFICATION_V1\n"));
        for (u64 sample = 0; sample < BUSTER_ARRAY_LENGTH(jobs); sample += 1)
        {
            TemporalArena temporary = scratch_begin(&arena, 1);
            Arena* scratch = temporary.arena;
            ClangAnalyzeOptions arm = options;
            arm.qualify_workers = false;
            arm.jobs = jobs[sample];
            arm.results = path_join(scratch, options.results, string_format(scratch, S8("sample-{u64}-jobs-{u64}"), sample, arm.jobs));
            String8 run_record = {0};
            arm.run_record = &run_record;
            u64 cpu_start = clang_analyze_children_cpu_microseconds();
            u64 start = os_now_microseconds();
            bool passed = clang_analyze_run(scratch, arm);
            u64 elapsed = os_now_microseconds() - start;
            u64 cpu_end = clang_analyze_children_cpu_microseconds();
            ClangAnalyzePlan current;
            bool unchanged = clang_analyze_plan(scratch, options, &current) && string_equal(frozen.fingerprint, current.fingerprint);
            passed = passed && unchanged && run_record.length;
            String8 record = string_format(arena, S8("ANALYZE_WORKER_SAMPLE sample={u64} jobs={u64} elapsed_us={u64} children_cpu_us={u64} host_logical_cpus={u64} eligible={u64} inventory_sha256={S8} status={S8}{S8}\n"),
                sample, arm.jobs, elapsed, cpu_end >= cpu_start ? cpu_end - cpu_start : 0, cpus, frozen.count, frozen.fingerprint, passed ? S8("pass") : S8("fail"),
                clang_analyze_status_qualifier(!passed));
            bool written = clang_analyze_write(scratch, path_join(scratch, arm.results, S8("run.txt")), run_record);
            string8_list_push(arena, &records, record);
            string_print(S8("{S8}"), record);
            success = success && passed && written;
            scratch_end(temporary);
        }
        String8 summary = string_join_arena(arena, string8_list_to_slice(arena, records), true);
        bool written = clang_analyze_write(arena, path_join(arena, options.results, S8("qualification.txt")), summary);
        success = success && written;
    }
    return success;
}

BUSTER_GLOBAL_LOCAL ProcessResult clang_analyze_main(Arena* arena, SliceString8 arguments)
{
    ClangAnalyzeOptions options = {.database = S8("build"), .shards = BUSTER_ANALYZE_DEFAULT_SHARDS,
        .jobs = BUSTER_ANALYZE_DEFAULT_JOBS, .timeout = BUSTER_ANALYZE_TIMEOUT_SECONDS};
    bool valid = true;
    bool database_set = false;
    bool jobs_set = false;
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        String8 inline_value = {0};
        bool has_value = build_argument_split_value(argument, &argument, &inline_value);
        if (string_equal(argument, S8("--quiet")) && !has_value) options.quiet = true;
        else if (string_equal(argument, S8("--prepare")) && !has_value) options.prepare = true;
        else if (string_equal(argument, S8("--aggregate")) && !has_value) options.aggregate = true;
        else if (string_equal(argument, S8("--self-test")) && !has_value) options.self_test = true;
        else if (string_equal(argument, S8("--qualify-workers")) && !has_value) options.qualify_workers = true;
        else if (string_equal(argument, S8("--expect-rejection")) && !has_value) options.expect_rejection = true;
        else if (!string_starts_with_sequence(argument, S8("--")) && !database_set)
        {
            options.database = argument;
            database_set = true;
        }
        else if (has_value || i + 1 < arguments.length)
        {
            String8 value = has_value ? inline_value : arguments.pointer[++i];
            if (string_equal(argument, S8("--config"))) options.config = value;
            else if (string_equal(argument, S8("--clang"))) options.clang = value;
            else if (string_equal(argument, S8("--results"))) options.results = value;
            else if (string_equal(argument, S8("--build-directory"))) options.database = value;
            else
            {
                u64 number = 0;
                valid = text_parse_u64(value, &number);
                if (string_equal(argument, S8("--shards"))) options.shards = number;
                else if (string_equal(argument, S8("--jobs")))
                {
                    options.jobs = number;
                    jobs_set = true;
                }
                else if (string_equal(argument, S8("--timeout"))) options.timeout = number;
                else if (string_equal(argument, S8("--shard")))
                {
                    options.shard = number;
                    options.worker = true;
                }
                else valid = false;
            }
        }
        else valid = false;
    }
    valid = valid && options.shards && options.shards <= BUSTER_ANALYZE_MAX_SHARDS && options.jobs && options.jobs <= BUSTER_ANALYZE_MAX_SHARDS &&
            options.timeout && options.timeout <= 86400 && (!options.worker || options.shard < options.shards) &&
            ((u32)options.prepare + (u32)options.aggregate + (u32)options.worker <= 1) &&
            (!(options.prepare || options.aggregate || options.worker) || options.results.length) &&
            (!options.qualify_workers || !(options.prepare || options.aggregate || options.worker || options.self_test || jobs_set)) &&
            (!options.expect_rejection || options.worker);
    if (valid && options.self_test)
    {
        valid = arguments.length == 1 && clang_analyze_self_test(arena);
    }
    else if (valid)
    {
        options.database = os_path_absolute(arena, clang_analyze_compile_commands_path(arena, options.database), true);
        if (!options.results.length)
        {
            options.results = path_join(arena, path_parent(arena, options.database), string_format(arena, S8("analyze-{u64}"), os_now_microseconds()));
        }
        options.results = os_path_absolute_lexical(arena, options.results, true);
        if (options.jobs > options.shards) options.jobs = options.shards;
        u64 cpus = os_get_logical_thread_count();
        if (cpus && options.jobs > cpus) options.jobs = cpus;
        // Only a self-test-launched shard worker may qualify its diagnostics.
        if (options.expect_rejection) clang_analyze_expecting_rejection = true;
        valid = options.qualify_workers ? clang_analyze_qualify_workers(arena, options) : clang_analyze_run(arena, options);
    }
    else
    {
        string_print(S8("{S8} clang_analyze [database] [--config Release] [--shards N] [--jobs N] [--timeout seconds] [--results fresh-directory] "
                        "[--prepare | --shard zero-based-index | --aggregate | --qualify-workers] [--clang path] [--quiet]; or --self-test\n"), clang_analyze_error_prefix());
    }
    return valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_json(Arena* arena, String8 value)
{
    u64 start = arena->position;
    arena_append_json_string(arena, value);
    String8 result = {.pointer = (char8*)arena_get_byte_pointer_at_position(arena, start), .length = arena->position - start};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_fixture_database_row(Arena* arena, String8 directory, String8 file, String8 output,
                                                                     String8 compiler, SliceString8 extra, String8 count_path)
{
    String8List arguments = {0};
    string8_list_push(arena, &arguments, compiler);
    string8_list_push(arena, &arguments, S8("-c"));
    string8_list_push(arena, &arguments, S8("-DFIXTURE_OK"));
    for (u64 i = 0; i < extra.length; i += 1) string8_list_push(arena, &arguments, extra.pointer[i]);
    string8_list_push(arena, &arguments, string_format(arena, S8("-DFIXTURE_COUNT_PATH={S8}"), count_path));
    string8_list_push(arena, &arguments, file);
    string8_list_push(arena, &arguments, S8("-o"));
    string8_list_push(arena, &arguments, output);
    String8List encoded = {0};
    SliceString8 argument_slice = string8_list_to_slice(arena, arguments);
    for (u64 i = 0; i < argument_slice.length; i += 1)
    {
        if (i) string8_list_push(arena, &encoded, S8(","));
        string8_list_push(arena, &encoded, clang_analyze_test_json(arena, argument_slice.pointer[i]));
    }
    String8 result = string_format(arena, S8("{{\"directory\":{S8},\"file\":{S8},\"output\":{S8},\"arguments\":[{S8}]}}"),
        clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, file), clang_analyze_test_json(arena, output),
        string_join_arena(arena, string8_list_to_slice(arena, encoded), true));
    return result;
}

BUSTER_GLOBAL_LOCAL u64 clang_analyze_test_line_count(String8 text)
{
    u64 result = 0;
    for (u64 i = 0; i < text.length; i += 1) result += text.pointer[i] == '\n';
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_rewrite_result_row(Arena* arena, String8 report, u64 target_ordinal,
                                                                   u64 replacement_index, u64 replacement_representative)
{
    u64 cursor = 0;
    String8 magic = {0};
    String8 fingerprint = {0};
    u64 shard = 0;
    u64 selected = 0;
    u64 executions = 0;
    u64 aliases = 0;
    u64 elapsed = 0;
    u64 rss = 0;
    bool valid = build_artifact_fanout_provenance_record_read_line(report, &cursor, &magic) &&
                 build_artifact_fanout_provenance_record_read_line(report, &cursor, &fingerprint) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &shard) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &selected) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &executions) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &aliases) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &elapsed) &&
                 build_artifact_fanout_provenance_record_read_u64(report, &cursor, &rss);
    String8List parts = {0};
    if (valid)
    {
        string8_list_push(arena, &parts, string_format(arena, S8("{S8}\n{S8}\n{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n"),
            magic, fingerprint, shard, selected, executions, aliases, elapsed, rss));
    }
    for (u64 ordinal = 0; valid && ordinal < selected; ordinal += 1)
    {
        u64 index = 0;
        u64 representative = 0;
        u64 launched = 0;
        u64 status = 0;
        u64 duration = 0;
        String8 log_fingerprint = {0};
        valid = build_artifact_fanout_provenance_record_read_u64(report, &cursor, &index) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &representative) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &launched) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &status) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &duration) &&
                build_artifact_fanout_provenance_record_read_line(report, &cursor, &log_fingerprint);
        if (valid && ordinal == target_ordinal)
        {
            index = replacement_index;
            representative = replacement_representative;
        }
        if (valid)
        {
            string8_list_push(arena, &parts, string_format(arena, S8("{u64}\n{u64}\n{u64}\n{u64}\n{u64}\n{S8}\n"),
                index, representative, launched, status, duration, log_fingerprint));
        }
    }
    valid = valid && cursor == report.length;
    String8 result = valid ? string_join_arena(arena, string8_list_to_slice(arena, parts), true) : (String8){0};
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_remove_last_result_row(Arena* arena, String8 report)
{
    u64 cursor = 0;
    String8 discarded = {0};
    u64 selected = 0;
    bool valid = build_artifact_fanout_provenance_record_read_line(report, &cursor, &discarded) &&
                 build_artifact_fanout_provenance_record_read_line(report, &cursor, &discarded);
    for (u64 i = 0; valid && i < 6; i += 1)
    {
        u64 value = 0;
        valid = build_artifact_fanout_provenance_record_read_u64(report, &cursor, &value);
        if (i == 1) selected = value;
    }
    u64 last_row = cursor;
    for (u64 i = 0; valid && i < selected; i += 1)
    {
        last_row = cursor;
        u64 ignored = 0;
        String8 fingerprint = {0};
        valid = build_artifact_fanout_provenance_record_read_u64(report, &cursor, &ignored) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &ignored) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &ignored) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &ignored) &&
                build_artifact_fanout_provenance_record_read_u64(report, &cursor, &ignored) &&
                build_artifact_fanout_provenance_record_read_line(report, &cursor, &fingerprint);
    }
    valid = valid && selected > 0;
    String8 result = valid ? string_slice(report, 0, last_row) : (String8){0};
    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_rewrite_plan_row_u64(Arena* arena, String8 manifest, u64 target_row,
                                                                    u64 field, u64 replacement)
{
    // field 1 rewrites the shard and field 2 rewrites the representative.
    u64 cursor = 0;
    String8 discarded_string = {0};
    u64 discarded_u64 = 0;
    bool valid = build_artifact_fanout_provenance_record_read_line(manifest, &cursor, &discarded_string) &&
        build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
        build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
        build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
    for (u64 i = 0; valid && i < 7; i += 1) valid = build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &discarded_u64);
    valid = valid && build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
    u64 field_start = 0;
    u64 field_end = 0;
    for (u64 row = 0; valid && row <= target_row; row += 1)
    {
        u64 index = 0;
        valid = build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &index) && index == row;
        for (u64 current_field = 1; valid && current_field <= 2; current_field += 1)
        {
            if (row == target_row && current_field == field) field_start = cursor;
            valid = build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &discarded_u64);
            if (row == target_row && current_field == field) field_end = cursor;
        }
        for (u64 i = 0; valid && i < 4; i += 1) valid = build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
        u64 argument_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &argument_count) && argument_count <= manifest.length / 3;
        for (u64 i = 0; valid && i < argument_count; i += 1) valid = build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
        u64 command_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &command_count) && command_count <= manifest.length / 3;
        for (u64 i = 0; valid && i < command_count; i += 1) valid = build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
        valid = valid && build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &discarded_u64) &&
            build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
            build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
        u64 input_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &input_count) && input_count <= manifest.length / 3;
        for (u64 i = 0; valid && i < input_count; i += 1)
        {
            valid = build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
                build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
                build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
                build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &discarded_u64);
        }
        u64 search_count = 0;
        valid = valid && build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &search_count) && search_count <= manifest.length / 3;
        for (u64 i = 0; valid && i < search_count; i += 1)
        {
            valid = build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string) &&
                build_artifact_fanout_provenance_record_read_u64(manifest, &cursor, &discarded_u64) &&
                build_artifact_fanout_provenance_record_read_string(manifest, &cursor, &discarded_string);
        }
    }
    valid = valid && field >= 1 && field <= 2 && field_start < field_end && field_end <= manifest.length;
    String8 result = {0};
    if (valid)
    {
        String8List parts = {0};
        string8_list_push(arena, &parts, string_slice(manifest, 0, field_start));
        string8_list_push(arena, &parts, string_format(arena, S8("{u64}\n"), replacement));
        string8_list_push(arena, &parts, string_slice(manifest, field_end, manifest.length));
        result = string_join_arena(arena, string8_list_to_slice(arena, parts), true);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 clang_analyze_test_database(Arena* arena, String8 directory, String8 compiler, String8 mode)
{
    String8 files[] = {S8("alpha.c"), S8("alpha_test.c"), S8("beta.c"), S8("gamma.c")};
    String8List rows = {0};
    string8_list_push(arena, &rows, S8("["));
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
    {
        String8 output = string_format(arena, S8("obj/Release/{S8}.o"), files[i]);
        String8 row = string_format(arena, S8("{S8}{{\"directory\":{S8},\"file\":{S8},\"output\":{S8},\"arguments\":[{S8},\"-c\",{S8},\"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",{S8},\"-o\",{S8}]}"),
            i ? S8(",") : S8(""), clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, files[i]),
            clang_analyze_test_json(arena, output), clang_analyze_test_json(arena, compiler),
            clang_analyze_test_json(arena, i ? S8("-DFIXTURE_OK") : mode), clang_analyze_test_json(arena, files[i]), clang_analyze_test_json(arena, output));
        string8_list_push(arena, &rows, row);
    }
    // Excluded rows are still inventoried; they must never become shard work.
    string8_list_push(arena, &rows, string_format(arena, S8(",{{\"directory\":{S8},\"file\":\"debug.c\",\"output\":\"obj/Debug/debug.o\",\"arguments\":[{S8},\"-c\",\"debug.c\"]},"
        "{{\"directory\":{S8},\"file\":\"excluded.cpp\",\"arguments\":[{S8},\"-c\",\"excluded.cpp\"]}]"),
        clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, compiler),
        clang_analyze_test_json(arena, directory), clang_analyze_test_json(arena, compiler)));
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, rows), true);
    return result;
}

typedef struct ClangAnalyzeTestState ClangAnalyzeTestState;
struct ClangAnalyzeTestState
{
    u64 checks;
    u64 failures;
    u64 expected_rejections;
};

BUSTER_GLOBAL_LOCAL void clang_analyze_test_begin(String8 name, bool reject)
{
    // Announce the scope before its operation prints anything. The matching
    // ANALYZE_SELF_TEST verdict closes it.
    string_print(S8("ANALYZE_SELF_TEST_BEGIN name={S8} expect={S8}\n"), name, reject ? S8("reject") : S8("accept"));
    clang_analyze_expecting_rejection = reject;
}

BUSTER_GLOBAL_LOCAL void clang_analyze_test_check(bool condition, String8 name, ClangAnalyzeTestState* state)
{
    bool reject = clang_analyze_expecting_rejection;
    clang_analyze_expecting_rejection = false;
    string_print(S8("ANALYZE_SELF_TEST name={S8} status={S8}\n"), name, condition ? S8("pass") : S8("fail"));
    if (!condition)
    {
        // An unexpected outcome is a real failure, so it is never qualified.
        string_print(S8("error: analyzer self-test check failed: name={S8}\n"), name);
    }
    state->checks += 1;
    state->failures += !condition;
    state->expected_rejections += reject && condition;
}

BUSTER_GLOBAL_LOCAL bool clang_analyze_self_test(Arena* arena)
{
    ClangAnalyzeTestState state = {0};
    // Every supported cardinality must dispatch every real shard ID once.
    // In particular, a high first ID cannot be mistaken for a launch count.
    bool complete_order = clang_analyze_schedule_shard(BUSTER_ANALYZE_DEFAULT_SHARDS, 0) != 0;
    for (u64 shards = 1; complete_order && shards <= BUSTER_ANALYZE_MAX_SHARDS; shards += 1)
    {
        bool seen[BUSTER_ANALYZE_MAX_SHARDS] = {0};
        for (u64 ordinal = 0; complete_order && ordinal < shards; ordinal += 1)
        {
            u64 shard = clang_analyze_schedule_shard(shards, ordinal);
            complete_order = shard < shards && !seen[shard] &&
                             (shards == BUSTER_ANALYZE_DEFAULT_SHARDS || shard == ordinal);
            if (complete_order) seen[shard] = true;
        }
    }
    clang_analyze_test_check(complete_order, S8("dispatch-covers-every-shard-once"), &state);
    // Retired comparison options must fail before preparing an inventory or
    // launching a child, including both accepted option-value spellings.
    String8 retired[] = {S8("--baseline-driver"), S8("unused-reference")};
    String8 retired_inline[] = {S8("--baseline-driver=unused-reference")};
    clang_analyze_test_begin(S8("reference-driver-option-refused"), true);
    clang_analyze_test_check(clang_analyze_main(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(retired)) == PROCESS_RESULT_FAILED,
                             S8("reference-driver-option-refused"), &state);
    clang_analyze_test_begin(S8("reference-driver-inline-option-refused"), true);
    clang_analyze_test_check(clang_analyze_main(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(retired_inline)) == PROCESS_RESULT_FAILED,
                             S8("reference-driver-inline-option-refused"), &state);
    // The qualifier is text-only and scoped: real runs print plain diagnostics,
    // a rejection scope qualifies failures but never a pass, and the scope
    // reaches shard workers only through their explicit command argument.
    bool plain = string_equal(clang_analyze_error_prefix(), S8("error:")) && !clang_analyze_status_qualifier(true).length;
    clang_analyze_expecting_rejection = true;
    bool qualified = string_equal(clang_analyze_error_prefix(), S8("expected-error:")) &&
                     string_equal(clang_analyze_status_qualifier(true), S8(" expected=1")) && !clang_analyze_status_qualifier(false).length;
    ClangAnalyzeOptions scoped_options = {.database = S8("compile_commands.json"), .results = S8("results"), .shards = 1, .timeout = 1};
    SliceString8 scoped_worker = clang_analyze_worker_command(arena, scoped_options, 0);
    clang_analyze_expecting_rejection = false;
    SliceString8 plain_worker = clang_analyze_worker_command(arena, scoped_options, 0);
    bool propagated = scoped_worker.length == plain_worker.length + 1 &&
                      string_equal(scoped_worker.pointer[scoped_worker.length - 1], S8("--expect-rejection"));
    for (u64 i = 0; propagated && i < plain_worker.length; i += 1)
    {
        propagated = !string_equal(plain_worker.pointer[i], S8("--expect-rejection"));
    }
    clang_analyze_test_check(plain && qualified && propagated, S8("expected-diagnostics-are-scoped"), &state);
    // Outside a shard worker the qualifier is refused before any work starts.
    String8 unscoped_arguments[] = {S8("compile_commands.json"), S8("--expect-rejection")};
    clang_analyze_test_begin(S8("expect-rejection-requires-worker"), true);
    bool unscoped = clang_analyze_main(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unscoped_arguments)) == PROCESS_RESULT_FAILED;
    clang_analyze_test_check(unscoped, S8("expect-rejection-requires-worker"), &state);
    bool split_valid = true;
    SliceString8 split = shell_split(arena, S8("\"clang tool\" \"-DBUSTER_HOST_C_COMPILER=\\\"C:/Program Files/clang.exe\\\"\" -I\"dir with spaces\" -c \"source file.c\" -o output.o"), &split_valid);
    String8 expected_macro = S8("-DBUSTER_HOST_C_COMPILER=\"C:/Program Files/clang.exe\"");
    bool quoted = split_valid && split.length == 7 && string_equal(split.pointer[0], S8("clang tool")) &&
                  string_equal(split.pointer[1], expected_macro) && string_equal(split.pointer[2], S8("-Idir with spaces")) &&
                  string_equal(split.pointer[4], S8("source file.c"));
    clang_analyze_test_check(quoted, S8("compile-command-quoting"), &state);
    SliceString8 projected = quoted ? clang_analyzer_command(arena, split, (String8){0}) : (SliceString8){0};
    clang_analyze_test_check(projected.length == 9 && string_equal(projected.pointer[6], expected_macro) &&
                             string_equal(projected.pointer[7], S8("-Idir with spaces")) && string_equal(projected.pointer[8], S8("source file.c")),
                             S8("preserve-semantic-compile-arguments"), &state);
    u64 root_seed = os_now_microseconds();
    String8 root = {0};
    bool ready = clang_analyze_claim_unique_directory(arena, S8("build/analyzer-self-test"), root_seed, &root);
    if (!ready)
    {
        root = S8("unavailable");
        string_print(S8("error: analyzer self-test could not claim a private evidence directory after bounded retries\n"));
    }
    clang_analyze_test_check(ready, S8("self-test-private-root-claimed"), &state);
    if (ready)
    {
        String8 collision_prefix = string_format(arena, S8("{S8}-claim-control"), root);
        String8 occupied_candidate = os_path_absolute_lexical(arena,
            string_format(arena, S8("{S8}-{u64}-0"), collision_prefix, root_seed), true);
        bool occupied_created = clang_analyze_try_new_directory(arena, occupied_candidate);
        String8 occupied_sentinel = path_join(arena, occupied_candidate, S8("sentinel"));
        bool sentinel_written = occupied_created && clang_analyze_write(arena, occupied_sentinel, S8("owner-a\n"));
        String8 retried_claim = {0};
        bool collision_retried = sentinel_written &&
            clang_analyze_claim_unique_directory(arena, collision_prefix, root_seed, &retried_claim);
        bool retried_to_next = collision_retried && string_equal(retried_claim,
            os_path_absolute_lexical(arena, string_format(arena, S8("{S8}-{u64}-1"), collision_prefix, root_seed), true));
        bool collision_untouched = sentinel_written && string_equal(clang_analyze_read(arena, occupied_sentinel), S8("owner-a\n"));
        clang_analyze_test_check(retried_to_next && collision_untouched, S8("self-test-root-claim-retries-collision-without-writing-owner"), &state);
        if (collision_retried) remove_path_recursive(arena, retried_claim);
        if (occupied_created) remove_path_recursive(arena, occupied_candidate);
        String8 exhausted_prefix = string_format(arena, S8("{S8}-claim-exhausted"), root);
        String8 first_exhausted_candidate = {0};
        bool all_candidates_occupied = true;
        for (u64 attempt = 0; attempt < 64; attempt += 1)
        {
            String8 candidate = os_path_absolute_lexical(arena,
                string_format(arena, S8("{S8}-{u64}-{u64}"), exhausted_prefix, root_seed, attempt), true);
            bool candidate_created = candidate.length && clang_analyze_try_new_directory(arena, candidate);
            all_candidates_occupied = all_candidates_occupied && candidate_created;
            if (!attempt)
            {
                first_exhausted_candidate = candidate;
                String8 owner_sentinel = path_join(arena, candidate, S8("sentinel"));
                all_candidates_occupied = all_candidates_occupied && clang_analyze_write(arena, owner_sentinel, S8("owner-b\n"));
            }
        }
        String8 exhausted_claim = {0};
        bool rejected_exhausted_claim = all_candidates_occupied &&
            !clang_analyze_claim_unique_directory(arena, exhausted_prefix, root_seed, &exhausted_claim);
        bool no_fixture_writes = rejected_exhausted_claim && !exhausted_claim.length;
        for (u64 attempt = 0; no_fixture_writes && attempt < 64; attempt += 1)
        {
            String8 candidate = os_path_absolute_lexical(arena,
                string_format(arena, S8("{S8}-{u64}-{u64}"), exhausted_prefix, root_seed, attempt), true);
            no_fixture_writes = !path_exists(arena, path_join(arena, candidate, S8("fixture-output.txt")));
        }
        bool owner_b_untouched = all_candidates_occupied &&
            string_equal(clang_analyze_read(arena, path_join(arena, first_exhausted_candidate, S8("sentinel"))), S8("owner-b\n"));
        clang_analyze_test_check(rejected_exhausted_claim && no_fixture_writes && owner_b_untouched,
            S8("self-test-root-claim-fails-without-fixture-writes"), &state);
        for (u64 attempt = 0; attempt < 64; attempt += 1)
        {
            String8 candidate = os_path_absolute_lexical(arena,
                string_format(arena, S8("{S8}-{u64}-{u64}"), exhausted_prefix, root_seed, attempt), true);
            if (path_exists(arena, candidate)) remove_path_recursive(arena, candidate);
        }
#if BUSTER_LINUX
        bool environment_valid = true;
        String8 environment_before = clang_analyze_environment_context(arena, &environment_valid);
        char* old_underscore_value = getenv("_");
        String8 old_underscore = old_underscore_value ? string_duplicate_arena(arena, string_from_pointer(old_underscore_value), true) : (String8){0};
        bool underscore_changed = setenv("_", "buster-analyzer-test-command", 1) == 0;
        bool underscore_ignored = underscore_changed && string_equal(environment_before,
            clang_analyze_environment_context(arena, &environment_valid));
        bool underscore_restored = old_underscore.length ? setenv("_", string_duplicate_arena(arena, old_underscore, true).pointer, 1) == 0 : unsetenv("_") == 0;
        bool unrelated_environment_changed = setenv("BUSTER_CLANG_ANALYZE_TEST_ENV", "changed", 1) == 0;
        String8 changed_environment = clang_analyze_environment_context(arena, &environment_valid);
        bool unrelated_environment_bound = unrelated_environment_changed && !string_equal(environment_before, changed_environment);
        bool unrelated_environment_restored = unsetenv("BUSTER_CLANG_ANALYZE_TEST_ENV") == 0;
        clang_analyze_test_check(environment_valid && underscore_ignored && underscore_restored && unrelated_environment_bound &&
                                 unrelated_environment_restored, S8("shell-command-marker-excluded-other-environment-remains-bound"), &state);
#endif
        String8 fixture = path_join(arena, root, S8("fixture.exe"));
        String8 clang = get_resolved_path(arena, &clang_path, S8("clang"));
        String8 stream_probe_path = path_join(arena, root, S8("stream-probe.bin"));
        String8 empty_probe_path = path_join(arena, root, S8("empty-probe.bin"));
        String8 stream_probe = S8("bounded-hash\0bytes");
        String8 stream_fingerprint = {0};
        String8 stream_metadata = {0};
        String8 empty_fingerprint = {0};
        String8 empty_metadata = {0};
        bool stream_written = ready && clang_analyze_write(arena, stream_probe_path, stream_probe) &&
                              clang_analyze_write(arena, empty_probe_path, (String8){0});
#if BUSTER_LINUX
        bool stream_digest_matches = stream_written &&
            clang_analyze_file_fingerprint(arena, stream_probe_path, &stream_fingerprint, &stream_metadata) &&
            clang_analyze_file_fingerprint(arena, empty_probe_path, &empty_fingerprint, &empty_metadata) &&
            string_equal(stream_fingerprint, clang_analyze_sha256(arena, stream_probe)) &&
            string_equal(empty_fingerprint, clang_analyze_sha256(arena, (String8){0}));
        clang_analyze_test_check(stream_digest_matches, S8("streamed-file-digest-preserves-content-hash"), &state);
#else
        bool stream_digest_unavailable = stream_written &&
            !clang_analyze_file_fingerprint(arena, stream_probe_path, &stream_fingerprint, &stream_metadata) &&
            !stream_fingerprint.length && !stream_metadata.length;
        clang_analyze_test_check(stream_digest_unavailable,
            S8("streamed-file-digest-unavailable-platform-falls-back"), &state);
#endif
#if BUSTER_LINUX
        String8 link_chain_root = path_join(arena, root, S8("bounded-link-chain"));
        bool link_chain_ready = ready && stream_written && clang_analyze_new_directory(arena, link_chain_root);
        for (u64 i = 0; link_chain_ready && i <= BUSTER_ANALYZE_MAX_COMPILER_LINKS; i += 1)
        {
            String8 link_path = path_join(arena, link_chain_root, string_format(arena, S8("link-{u64}"), i));
            String8 target = i == BUSTER_ANALYZE_MAX_COMPILER_LINKS ? stream_probe_path : string_format(arena, S8("link-{u64}"), i + 1);
            String8 link_path_z = string_duplicate_arena(arena, link_path, true);
            String8 target_z = string_duplicate_arena(arena, target, true);
            link_chain_ready = symlink(target_z.pointer, link_path_z.pointer) == 0;
        }
        String8 link64 = path_join(arena, link_chain_root, S8("link-1"));
        String8 link65 = path_join(arena, link_chain_root, S8("link-0"));
        Sha256 link64_hash = {0};
        Sha256 link65_hash = {0};
        sha256_init(&link64_hash);
        sha256_init(&link65_hash);
        ClangAnalyzeCompilerPathWalk link64_walk = {.hash = &link64_hash};
        ClangAnalyzeCompilerPathWalk link65_walk = {.hash = &link65_hash};
        bool link64_accepted = link_chain_ready && clang_analyze_compiler_path_walk(arena, &link64_walk, link64) &&
                               link64_walk.link_count == BUSTER_ANALYZE_MAX_COMPILER_LINKS;
        bool link65_rejected = link_chain_ready && !clang_analyze_compiler_path_walk(arena, &link65_walk, link65);
        clang_analyze_test_check(link64_accepted && link65_rejected, S8("compiler-symlink-walk-has-bounded-64-link-limit"), &state);
#endif
        String8 compile[] = {clang, S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char"), S8("tools/clang_analyze_fixture.c"), S8("-o"), fixture};
        ProcessSpawnResult spawn = {0};
        if (ready)
        {
            spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(compile), (SliceString8){0}, (SliceString8){0},
                                     (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
        }
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, 60000000);
        ready = ready && wait.result == PROCESS_RESULT_SUCCESS;
        clang_analyze_test_check(ready, S8("native-process-oracle"), &state);
#if BUSTER_LINUX
        clang_analyze_test_begin(S8("missing-children-preserves-lower-bound"), true);
        clang_analyze_test_check(ready && clang_analyze_tree_missing_children_control(arena, root), S8("missing-children-preserves-lower-bound"), &state);
        clang_analyze_test_begin(S8("complete-descendant-sampling"), false);
        clang_analyze_test_check(ready && clang_analyze_tree_complete_control(arena, root), S8("complete-descendant-sampling"), &state);
        clang_analyze_test_begin(S8("exiting-child-race-is-tolerated"), false);
        clang_analyze_test_check(ready && clang_analyze_tree_exit_race_control(arena, root), S8("exiting-child-race-is-tolerated"), &state);
        clang_analyze_test_begin(S8("vanished-child-removed-from-parent-list"), false);
        clang_analyze_test_check(ready && clang_analyze_tree_vanished_child_control(arena, root), S8("vanished-child-removed-from-parent-list"), &state);
        clang_analyze_test_begin(S8("unknown-parentage-is-incomplete-and-excluded-from-rss"), true);
        clang_analyze_test_check(ready && clang_analyze_tree_child_list_budget_control(arena, root), S8("unknown-parentage-is-incomplete-and-excluded-from-rss"), &state);
        clang_analyze_test_begin(S8("live-child-missing-statm-is-incomplete"), true);
        clang_analyze_test_check(ready && clang_analyze_tree_child_statm_failure_control(arena, root), S8("live-child-missing-statm-is-incomplete"), &state);
        clang_analyze_test_begin(S8("live-child-missing-children-is-incomplete"), true);
        clang_analyze_test_check(ready && clang_analyze_tree_child_children_failure_control(arena, root), S8("live-child-missing-children-is-incomplete"), &state);
        clang_analyze_test_begin(S8("proc-self-resolves-mounted-pid"), false);
        clang_analyze_test_check(ready && clang_analyze_proc_self_control(), S8("proc-self-resolves-mounted-pid"), &state);
#endif
        String8 database = path_join(arena, root, S8("compile_commands.json"));
        String8 modes[] = {S8("-DFIXTURE_OK"), S8("-DFIXTURE_WARNING"), S8("-DFIXTURE_STDOUT"), S8("-DFIXTURE_FAILURE"),
                          S8("-DFIXTURE_CRASH"), S8("-DFIXTURE_TIMEOUT"), S8("-DFIXTURE_LARGE_OUTPUT")};
        for (u64 mode = 0; ready && mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            String8 contents = clang_analyze_test_database(arena, root, fixture, modes[mode]);
            bool written = clang_analyze_write(arena, database, contents);
            // Only the sleeping oracle needs the short deadline. Draining both
            // pipes can take several seconds on hosted Windows AArch64 runners.
            ClangAnalyzeOptions options = {.database = database, .config = S8("Release"), .shards = 4, .jobs = 2, .timeout = mode == 5 ? 1 : 30, .quiet = true,
                .results = path_join(arena, root, string_format(arena, S8("case-{u64}"), mode))};
            bool accept = mode == 0 || mode == 6;
            clang_analyze_test_begin(modes[mode], !accept);
            bool passed = written && clang_analyze_run(arena, options);
            clang_analyze_test_check(passed == accept, modes[mode], &state);
            if (mode <= 1)
            {
                ClangAnalyzeOptions reordered = options;
                reordered.shards = BUSTER_ANALYZE_DEFAULT_SHARDS;
                reordered.results = path_join(arena, root, string_format(arena, S8("priority-order-{u64}"), mode));
                String8 name = mode ? S8("reordered-worker-failure-propagates") : S8("reordered-workers-complete-coverage");
                clang_analyze_test_begin(name, mode != 0);
                bool reordered_pass = written && clang_analyze_run(arena, reordered);
                clang_analyze_test_check(reordered_pass == (mode == 0), name, &state);
                ClangAnalyzePlan reordered_plan;
                bool accounted = clang_analyze_plan(arena, reordered, &reordered_plan);
                for (u64 shard = 0; accounted && shard < reordered.shards; shard += 1)
                {
                    accounted = path_exists(arena, path_join(arena, clang_analyze_shard_directory(arena, reordered, shard), S8("result.txt")));
                }
                for (u64 i = 0; accounted && i < reordered_plan.count; i += 1)
                {
                    String8 log = path_join(arena, clang_analyze_shard_directory(arena, reordered, reordered_plan.units[i].shard),
                                           string_format(arena, S8("unit-{u64}.log"), i));
                    accounted = path_exists(arena, log);
                }
                clang_analyze_test_check(accounted, S8("reordered-workers-retain-every-result"), &state);
                // An independent reread must reach the same coverage/failure verdict.
                reordered.aggregate = true;
                name = mode ? S8("reordered-failure-aggregate") : S8("reordered-independent-aggregate");
                clang_analyze_test_begin(name, mode != 0);
                bool replay = written && clang_analyze_run(arena, reordered);
                clang_analyze_test_check(replay == (mode == 0), name, &state);
            }
            if (mode <= 1 && os_get_logical_thread_count() >= 4)
            {
                ClangAnalyzeOptions qualification = options;
                qualification.results = path_join(arena, root, string_format(arena, S8("worker-budget-{u64}"), mode));
                String8 name = mode ? S8("worker-budget-failure-propagates") : S8("worker-budget-complete-inventories");
                clang_analyze_test_begin(name, mode != 0);
                bool qualified_workers = written && clang_analyze_qualify_workers(arena, qualification);
                clang_analyze_test_check(qualified_workers == (mode == 0), name, &state);
            }
            ClangAnalyzePlan plan;
            bool planned = clang_analyze_plan(arena, options, &plan);
            clang_analyze_test_check(planned && plan.count == 4 && plan.excluded == 2 && plan.units[0].shard == plan.units[1].shard,
                                     S8("complete-inventory-and-module-pair"), &state);
            // Every case, including an early failure, must publish all TU logs.
            for (u64 i = 0; planned && i < plan.count; i += 1)
            {
                String8 log = path_join(arena, clang_analyze_shard_directory(arena, options, plan.units[i].shard), string_format(arena, S8("unit-{u64}.log"), i));
                clang_analyze_test_check(path_exists(arena, log), S8("continue-after-failure"), &state);
            }
            if (mode == 0 && planned && passed)
            {
                // Each tampered report is written before its rejection scope opens,
                // so a failed write stays a plain, real error.
                options.aggregate = true;
                clang_analyze_test_begin(S8("independent-aggregate"), false);
                clang_analyze_test_check(clang_analyze_run(arena, options), S8("independent-aggregate"), &state);
                u64 shard = plan.units[0].shard;
                String8 path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("result.txt"));
                String8 original = clang_analyze_read(arena, path);
                remove_path_recursive(arena, path);
                clang_analyze_test_begin(S8("missing-shard"), true);
                clang_analyze_test_check(!clang_analyze_aggregate(arena, options, plan), S8("missing-shard"), &state);
                String8 truncated = string_slice(original, 0, original.length - 1);
                written = clang_analyze_write(arena, path, truncated);
                clang_analyze_test_begin(S8("truncated-result"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("truncated-result"), &state);
                String8 extra = string_format(arena, S8("{S8}0\n0\n0\n"), original);
                written = clang_analyze_write(arena, path, extra);
                clang_analyze_test_begin(S8("unaccounted-result"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("unaccounted-result"), &state);
                // Keep the valid V2 header and all row records, but duplicate a
                // selected index so coverage fails for the intended reason.
                u64 first_index = BUSTER_STRING_NO_MATCH;
                u64 second_index = BUSTER_STRING_NO_MATCH;
                u64 second_ordinal = 0;
                u64 row_ordinal = 0;
                for (u64 i = 0; i < plan.count; i += 1)
                {
                    if (plan.units[i].shard == shard)
                    {
                        if (first_index == BUSTER_STRING_NO_MATCH) first_index = i;
                        else if (second_index == BUSTER_STRING_NO_MATCH)
                        {
                            second_index = i;
                            second_ordinal = row_ordinal;
                        }
                        row_ordinal += 1;
                    }
                }
                String8 duplicate = second_index != BUSTER_STRING_NO_MATCH ?
                    clang_analyze_test_rewrite_result_row(arena, original, second_ordinal, first_index, first_index) : (String8){0};
                written = clang_analyze_write(arena, path, duplicate);
                clang_analyze_test_begin(S8("duplicate-TU"), true);
                clang_analyze_test_check(second_index != BUSTER_STRING_NO_MATCH && written && !clang_analyze_aggregate(arena, options, plan), S8("duplicate-TU"), &state);
                String8 omitted = clang_analyze_test_remove_last_result_row(arena, original);
                written = clang_analyze_write(arena, path, omitted);
                clang_analyze_test_begin(S8("omitted-TU"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("omitted-TU"), &state);
                String8 wrong_shard = string_format(arena, S8(BUSTER_ANALYZE_RESULT_VERSION "{S8}\n{u64}\n0\n1\n1\n"), plan.fingerprint, options.shards);
                written = clang_analyze_write(arena, path, wrong_shard);
                clang_analyze_test_begin(S8("wrong-shard"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("wrong-shard"), &state);
                String8 stale = string_duplicate_arena(arena, original, true);
                stale.pointer[sizeof(BUSTER_ANALYZE_RESULT_VERSION) - 1] = 'z';
                written = clang_analyze_write(arena, path, stale);
                clang_analyze_test_begin(S8("stale-result"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("stale-result"), &state);
                clang_analyze_test_begin(S8("restored-coverage"), false);
                clang_analyze_test_check(clang_analyze_write(arena, path, original) && clang_analyze_aggregate(arena, options, plan), S8("restored-coverage"), &state);
                String8 log_path = path_join(arena, clang_analyze_shard_directory(arena, options, shard), S8("unit-0.log"));
                remove_path_recursive(arena, log_path);
                clang_analyze_test_begin(S8("missing-log"), true);
                clang_analyze_test_check(!clang_analyze_aggregate(arena, options, plan), S8("missing-log"), &state);
                written = clang_analyze_write(arena, log_path, S8("changed"));
                clang_analyze_test_begin(S8("changed-log"), true);
                clang_analyze_test_check(written && !clang_analyze_aggregate(arena, options, plan), S8("changed-log"), &state);
                clang_analyze_test_begin(S8("restored-log"), false);
                clang_analyze_test_check(clang_analyze_write(arena, log_path, S8("")) && clang_analyze_aggregate(arena, options, plan), S8("restored-log"), &state);
                options.shard = shard;
                clang_analyze_test_begin(S8("duplicate-worker-refused"), true);
                clang_analyze_test_check(!clang_analyze_worker(arena, options, plan), S8("duplicate-worker-refused"), &state);
                options.config = S8("Debug");
                clang_analyze_test_begin(S8("changed-config-refused"), true);
                clang_analyze_test_check(!clang_analyze_run(arena, options), S8("changed-config-refused"), &state);
            }
        }
        if (ready)
        {
            String8 dedup_root = path_join(arena, root, S8("dedup"));
            String8 source_directory = path_join(arena, dedup_root, S8("src"));
            String8 alternate_directory = path_join(arena, dedup_root, S8("alternate"));
            make_directory_recursive(arena, source_directory);
            make_directory_recursive(arena, alternate_directory);
            String8 count_path = path_join(arena, dedup_root, S8("launches.txt"));
            String8 fixture_alt = path_join(arena, root, S8("fixture-alt.exe"));
            String8 fixture_relative = S8("../../fixture.exe");
            String8 alt_compile[] = {clang, S8("-DFIXTURE_ALT"), S8("-fwrapv"), S8("-fno-strict-aliasing"), S8("-funsigned-char"),
                S8("tools/clang_analyze_fixture.c"), S8("-o"), fixture_alt};
            ProcessSpawnResult alt_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(alt_compile), (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1});
            ProcessWaitResult alt_wait = os_process_wait_deadline(arena, alt_spawn, 60000000);
            bool alt_ready = alt_wait.result == PROCESS_RESULT_SUCCESS;
            String8 source_text = S8("int alpha(void) { return 7; }\n");
            String8 beta_text = S8("int beta(void) { return 8; }\n");
            String8 time_text = S8("int time_value(void) { return __TIME__[0]; }\n");
            bool source_files = alt_ready && clang_analyze_write(arena, path_join(arena, source_directory, S8("alpha.c")), source_text) &&
                clang_analyze_write(arena, path_join(arena, source_directory, S8("beta.c")), beta_text) &&
                clang_analyze_write(arena, path_join(arena, source_directory, S8("time.c")), time_text) &&
                clang_analyze_write(arena, path_join(arena, alternate_directory, S8("alpha.c")), source_text);
            String8 variant[] = {S8("-DFIXTURE_VARIANT")};
            String8 include_forward[] = {S8("-Ileft"), S8("-Iright")};
            String8 include_reverse[] = {S8("-Iright"), S8("-Ileft")};
            String8 target[] = {S8("-target"), S8("x86_64-unknown-linux-gnu")};
            String8 plugin[] = {S8("-Xclang"), S8("-load"), S8("-Xclang"), S8("unused-plugin.so")};
            String8 overlay[] = {S8("-ivfsoverlay"), S8("filesystem-overlay.yaml")};
            String8 rows[15] = {
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/z-alpha.o"), fixture_relative, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/a-alpha.o"), fixture_relative, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("beta.c"), S8("obj/beta.o"), fixture, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/variant.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(variant), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/include-forward.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(include_forward), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/include-reverse.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(include_reverse), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/target.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(target), count_path),
                clang_analyze_test_fixture_database_row(arena, alternate_directory, S8("alpha.c"), S8("obj/alternate-cwd.o"), fixture,
                    (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/alternate-compiler.o"), fixture_alt,
                    (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("time.c"), S8("obj/time-a.o"), fixture, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("time.c"), S8("obj/time-b.o"), fixture, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/plugin-a.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(plugin), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/plugin-b.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(plugin), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/overlay-a.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(overlay), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/overlay-b.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(overlay), count_path),
            };
            String8List database_parts = {0};
            string8_list_push(arena, &database_parts, S8("["));
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(rows); i += 1)
            {
                if (i) string8_list_push(arena, &database_parts, S8(","));
                string8_list_push(arena, &database_parts, rows[i]);
            }
            string8_list_push(arena, &database_parts, S8("]"));
            String8 dedup_database = string_join_arena(arena, string8_list_to_slice(arena, database_parts), true);
            String8 dedup_database_path = path_join(arena, dedup_root, S8("compile_commands.json"));
            ClangAnalyzeOptions dedup_options = {.database = dedup_database_path, .shards = 1, .jobs = 1, .timeout = 30, .quiet = true,
                .fixture_compiler = true, .results = path_join(arena, dedup_root, S8("results"))};
            ClangAnalyzePlan dedup_plan = {0};
            bool dedup_planned = source_files && clang_analyze_write(arena, dedup_database_path, dedup_database) &&
                clang_analyze_plan(arena, dedup_options, &dedup_plan);
            u64 expected_aliases = BUSTER_LINUX ? 1 : 0;
            bool deterministic = dedup_planned && dedup_plan.count == BUSTER_ARRAY_LENGTH(rows) &&
                dedup_plan.aliased_rows == expected_aliases && dedup_plan.unique_executions == dedup_plan.count - expected_aliases;
            String8 expected_time_reason = BUSTER_LINUX ? S8("dynamic-time-builtin") : S8("platform-context-unavailable");
            String8 expected_mutable_reason = BUSTER_LINUX ? S8("plugin-or-mutable-input") : S8("platform-context-unavailable");
            u64 alpha_z = BUSTER_STRING_NO_MATCH;
            u64 alpha_a = BUSTER_STRING_NO_MATCH;
            u64 timed = 0;
            u64 plugin_rows = 0;
            u64 overlay_rows = 0;
            for (u64 i = 0; deterministic && i < dedup_plan.count; i += 1)
            {
                if (string_equal(dedup_plan.units[i].entry.output, S8("obj/z-alpha.o"))) alpha_z = i;
                if (string_equal(dedup_plan.units[i].entry.output, S8("obj/a-alpha.o"))) alpha_a = i;
                if (string_starts_with_sequence(dedup_plan.units[i].entry.output, S8("obj/time-")))
                {
                    timed += dedup_plan.units[i].representative == i;
                    deterministic = deterministic && dedup_plan.units[i].ineligible_reason.length &&
                        string_equal(dedup_plan.units[i].ineligible_reason, expected_time_reason);
                }
                if (string_starts_with_sequence(dedup_plan.units[i].entry.output, S8("obj/plugin-")))
                {
                    plugin_rows += dedup_plan.units[i].representative == i;
                    deterministic = deterministic && string_equal(dedup_plan.units[i].ineligible_reason, expected_mutable_reason);
                }
                if (string_starts_with_sequence(dedup_plan.units[i].entry.output, S8("obj/overlay-")))
                {
                    overlay_rows += dedup_plan.units[i].representative == i;
                    deterministic = deterministic && string_equal(dedup_plan.units[i].ineligible_reason, expected_mutable_reason);
                }
            }
            deterministic = deterministic && timed == 2 && plugin_rows == 2 && overlay_rows == 2 && alpha_z != BUSTER_STRING_NO_MATCH && alpha_a != BUSTER_STRING_NO_MATCH &&
                (expected_aliases ? dedup_plan.units[alpha_z].representative == alpha_a && dedup_plan.units[alpha_a].representative == alpha_a :
                                    dedup_plan.units[alpha_z].representative == alpha_z && dedup_plan.units[alpha_a].representative == alpha_a);
            clang_analyze_test_check(deterministic, S8("same-run-dedup-eligibility-and-stable-representative"), &state);
#if !BUSTER_LINUX
            bool platform_fallback = dedup_planned && dedup_plan.unique_executions == dedup_plan.count && dedup_plan.aliased_rows == 0;
            for (u64 i = 0; platform_fallback && i < dedup_plan.count; i += 1)
            {
                platform_fallback = dedup_plan.units[i].representative == i;
            }
            platform_fallback = platform_fallback && alpha_z != BUSTER_STRING_NO_MATCH && alpha_a != BUSTER_STRING_NO_MATCH &&
                string_equal(dedup_plan.units[alpha_z].ineligible_reason, S8("platform-context-unavailable")) &&
                string_equal(dedup_plan.units[alpha_a].ineligible_reason, S8("platform-context-unavailable"));
            clang_analyze_test_check(platform_fallback, S8("unsupported-platform-dedup-falls-back-to-independent-rows"), &state);
#endif
            String8List reversed_parts = {0};
            string8_list_push(arena, &reversed_parts, S8("["));
            for (u64 i = BUSTER_ARRAY_LENGTH(rows); i > 0; i -= 1)
            {
                if (i != BUSTER_ARRAY_LENGTH(rows)) string8_list_push(arena, &reversed_parts, S8(","));
                string8_list_push(arena, &reversed_parts, rows[i - 1]);
            }
            string8_list_push(arena, &reversed_parts, S8("]"));
            String8 reversed_database = string_join_arena(arena, string8_list_to_slice(arena, reversed_parts), true);
            String8 reversed_path = path_join(arena, dedup_root, S8("compile_commands-reversed.json"));
            ClangAnalyzePlan reversed_plan = {0};
            bool reversed_planned = deterministic && clang_analyze_write(arena, reversed_path, reversed_database);
            dedup_options.database = reversed_path;
            reversed_planned = reversed_planned && clang_analyze_plan(arena, dedup_options, &reversed_plan);
            bool reordered_representative = reversed_planned && reversed_plan.count == dedup_plan.count &&
                reversed_plan.unique_executions == dedup_plan.unique_executions && reversed_plan.aliased_rows == dedup_plan.aliased_rows;
            for (u64 i = 0; reordered_representative && i < reversed_plan.count; i += 1)
            {
                if (string_equal(reversed_plan.units[i].entry.output, S8("obj/a-alpha.o"))) reordered_representative = reversed_plan.units[i].representative == i;
                if (string_equal(reversed_plan.units[i].entry.output, S8("obj/z-alpha.o")))
                {
                    if (expected_aliases)
                    {
                        reordered_representative = reordered_representative && reversed_plan.units[i].representative != i &&
                            string_equal(reversed_plan.units[reversed_plan.units[i].representative].entry.output, S8("obj/a-alpha.o"));
                    }
                    else
                    {
                        reordered_representative = reordered_representative && reversed_plan.units[i].representative == i;
                    }
                }
            }
            clang_analyze_test_check(reordered_representative, S8("same-run-dedup-db-order-independent"), &state);
            dedup_options.database = dedup_database_path;
            String8 worker_probe_database = path_join(arena, dedup_root, S8("worker-proof-compile_commands.json"));
            String8 worker_probe_results = path_join(arena, dedup_root, S8("worker-proof-results"));
            String8 worker_probe_rows[] = {
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/worker-z.o"), fixture, (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/worker-a.o"), fixture, (SliceString8){0}, count_path),
            };
            String8 worker_probe_text = string_format(arena, S8("[{S8},{S8}]"), worker_probe_rows[0], worker_probe_rows[1]);
            ClangAnalyzeOptions worker_probe_options = dedup_options;
            worker_probe_options.database = worker_probe_database;
            worker_probe_options.results = worker_probe_results;
            worker_probe_options.shards = 17;
            ClangAnalyzePlan worker_probe_full = {0};
            bool worker_probe_ready = clang_analyze_write(arena, worker_probe_database, worker_probe_text) &&
                clang_analyze_plan(arena, worker_probe_options, &worker_probe_full) && worker_probe_full.count == 2 &&
                clang_analyze_new_directory(arena, worker_probe_results) &&
                clang_analyze_write(arena, path_join(arena, worker_probe_results, S8("manifest.txt")), worker_probe_full.manifest);
            u64 empty_shard = BUSTER_STRING_NO_MATCH;
            for (u64 shard = 0; worker_probe_ready && shard < worker_probe_options.shards; shard += 1)
            {
                if (shard != worker_probe_full.units[0].shard && shard != worker_probe_full.units[1].shard)
                {
                    empty_shard = shard;
                    break;
                }
            }
            worker_probe_options.worker = true;
            worker_probe_options.shard = empty_shard;
            ClangAnalyzePlan worker_probe_empty = {0};
            bool empty_shard_checked = worker_probe_ready && empty_shard != BUSTER_STRING_NO_MATCH &&
                clang_analyze_plan(arena, worker_probe_options, &worker_probe_empty) && worker_probe_empty.context_proof_us == 0 &&
                worker_probe_empty.unique_executions == worker_probe_full.unique_executions &&
                worker_probe_empty.aliased_rows == worker_probe_full.aliased_rows;
            clang_analyze_test_check(empty_shard_checked, S8("empty-worker-shard-skips-proof-reconstruction"), &state);
            worker_probe_options.shard = worker_probe_full.units[0].shard;
            ClangAnalyzePlan worker_probe_owned = {0};
            bool owned_shard_checked = worker_probe_ready && clang_analyze_plan(arena, worker_probe_options, &worker_probe_owned) &&
                worker_probe_owned.proven_groups == worker_probe_full.proven_groups && worker_probe_owned.manifest.length == worker_probe_full.manifest.length &&
                string_equal(worker_probe_owned.manifest, worker_probe_full.manifest);
            clang_analyze_test_check(owned_shard_checked, S8("worker-owned-proof-matches-complete-manifest"), &state);
            String8 worker_manifest_path = path_join(arena, worker_probe_results, S8("manifest.txt"));
            String8 worker_manifest = worker_probe_full.manifest;
            String8 truncated_manifest = string_slice(worker_manifest, 0, worker_manifest.length - 1);
            clang_analyze_test_begin(S8("worker-truncated-manifest-refused-before-launch"), true);
            ClangAnalyzePlan rejected_worker_plan = {0};
            bool truncated_refused = worker_probe_ready && clang_analyze_write(arena, worker_manifest_path, truncated_manifest) &&
                !clang_analyze_plan(arena, worker_probe_options, &rejected_worker_plan) &&
                !path_exists(arena, path_join(arena, clang_analyze_shard_directory(arena, worker_probe_options, worker_probe_options.shard), S8("result.txt")));
            clang_analyze_test_check(truncated_refused, S8("worker-truncated-manifest-refused-before-launch"), &state);
            u64 foreign_row_shard = (worker_probe_full.units[0].shard + 1) % worker_probe_options.shards;
            String8 foreign_shard_manifest = clang_analyze_test_rewrite_plan_row_u64(arena, worker_manifest, 0, 1, foreign_row_shard);
            clang_analyze_test_begin(S8("worker-foreign-row-shard-refused-before-launch"), true);
            bool foreign_shard_refused = foreign_shard_manifest.length &&
                clang_analyze_write(arena, worker_manifest_path, foreign_shard_manifest) &&
                !clang_analyze_plan(arena, worker_probe_options, &rejected_worker_plan);
            clang_analyze_test_check(foreign_shard_refused, S8("worker-foreign-row-shard-refused-before-launch"), &state);
            u64 alias_row = BUSTER_STRING_NO_MATCH;
            for (u64 i = 0; i < worker_probe_full.count; i += 1)
            {
                if (worker_probe_full.units[i].representative != i) alias_row = i;
            }
            String8 canonical_map_manifest = alias_row != BUSTER_STRING_NO_MATCH ?
                clang_analyze_test_rewrite_plan_row_u64(arena, worker_manifest, alias_row, 2, alias_row) : (String8){0};
            clang_analyze_test_begin(S8("worker-canonical-map-refused-before-launch"), true);
            bool canonical_map_refused = alias_row != BUSTER_STRING_NO_MATCH && canonical_map_manifest.length &&
                clang_analyze_write(arena, worker_manifest_path, canonical_map_manifest) &&
                !clang_analyze_plan(arena, worker_probe_options, &rejected_worker_plan);
            clang_analyze_test_check(!BUSTER_LINUX || canonical_map_refused, S8("worker-canonical-map-refused-before-launch"), &state);
            worker_probe_options.worker = false;
            bool restored_worker_manifest = clang_analyze_write(arena, worker_manifest_path, worker_manifest);
            clang_analyze_test_check(restored_worker_manifest && (!BUSTER_LINUX || worker_probe_full.aliased_rows == 1),
                S8("worker-proof-control-has-native-alias"), &state);
            bool prepared = dedup_planned && clang_analyze_new_directory(arena, dedup_options.results) &&
                clang_analyze_write(arena, path_join(arena, dedup_options.results, S8("manifest.txt")), dedup_plan.manifest);
            dedup_options.worker = true;
            bool worker_passed = prepared && clang_analyze_worker(arena, dedup_options, dedup_plan);
            dedup_options.worker = false;
            dedup_options.aggregate = true;
            bool aggregate_passed = worker_passed && clang_analyze_aggregate(arena, dedup_options, dedup_plan);
            String8 launch_lines = clang_analyze_read(arena, count_path);
            bool launch_counted = aggregate_passed && clang_analyze_test_line_count(launch_lines) == dedup_plan.unique_executions;
            clang_analyze_test_check(launch_counted, S8("native-launch-count-matches-unique-executions"), &state);
            if (BUSTER_LINUX)
            {
            String8 mutation_database = path_join(arena, dedup_root, S8("mutation-compile_commands.json"));
            String8 mutation_path = path_join(arena, source_directory, S8("created-during-shard.h"));
            String8 mutation[] = {string_format(arena, S8("-DFIXTURE_MUTATE_PATH={S8}"), mutation_path)};
            String8 mutation_rows[] = {
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/mutation-z.o"), fixture_relative,
                    (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/mutation-a.o"), fixture_relative,
                    (SliceString8){0}, count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("beta.c"), S8("obj/mutation-writer.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(mutation), count_path),
            };
            String8 mutation_text = string_format(arena, S8("[{S8},{S8},{S8}]"), mutation_rows[0], mutation_rows[1], mutation_rows[2]);
            ClangAnalyzeOptions mutation_options = dedup_options;
            mutation_options.database = mutation_database;
            mutation_options.results = path_join(arena, dedup_root, S8("mutation-results"));
            ClangAnalyzePlan mutation_plan = {0};
            bool mutation_planned = clang_analyze_write(arena, count_path, S8("")) &&
                clang_analyze_write(arena, mutation_database, mutation_text) && clang_analyze_plan(arena, mutation_options, &mutation_plan) &&
                mutation_plan.count == BUSTER_ARRAY_LENGTH(mutation_rows) && mutation_plan.aliased_rows == expected_aliases &&
                !path_exists(arena, mutation_path) && clang_analyze_new_directory(arena, mutation_options.results) &&
                clang_analyze_write(arena, path_join(arena, mutation_options.results, S8("manifest.txt")), mutation_plan.manifest);
            mutation_options.worker = true;
            clang_analyze_test_begin(S8("later-execution-invalidates-earlier-alias"), true);
            bool mutation_worker = mutation_planned && !clang_analyze_worker(arena, mutation_options, mutation_plan);
            mutation_options.worker = false;
            mutation_options.aggregate = true;
            bool mutation_aggregate = mutation_planned && !clang_analyze_aggregate(arena, mutation_options, mutation_plan);
            String8 mutation_report = clang_analyze_read(arena, path_join(arena,
                clang_analyze_shard_directory(arena, mutation_options, 0), S8("result.txt")));
            u64 mutation_cursor = 0;
            String8 mutation_magic = {0};
            String8 mutation_fingerprint = {0};
            u64 mutation_shard = 0;
            u64 mutation_selected = 0;
            u64 mutation_executions = 0;
            u64 mutation_aliases = 0;
            u64 mutation_elapsed = 0;
            u64 mutation_rss = 0;
            bool mutation_rows_valid = build_artifact_fanout_provenance_record_read_line(mutation_report, &mutation_cursor, &mutation_magic) &&
                string_equal(mutation_magic, S8("BUSTER_CLANG_ANALYZE_RESULT_V2")) &&
                build_artifact_fanout_provenance_record_read_line(mutation_report, &mutation_cursor, &mutation_fingerprint) &&
                string_equal(mutation_fingerprint, mutation_plan.fingerprint) &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_shard) && mutation_shard == 0 &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_selected) && mutation_selected == 3 &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_executions) && mutation_executions == 2 &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_aliases) && mutation_aliases == 1 &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_elapsed) &&
                build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_rss);
            u64 mutation_statuses[3] = {0};
            u64 mutation_representatives[3] = {0};
            u64 mutation_launches[3] = {0};
            bool mutation_seen[3] = {0};
            for (u64 row = 0; mutation_rows_valid && row < 3; row += 1)
            {
                u64 index = 0;
                u64 duration = 0;
                String8 log_fingerprint = {0};
                mutation_rows_valid = build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &index) && index < 3 &&
                    !mutation_seen[index] &&
                    build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_representatives[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_launches[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &mutation_statuses[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(mutation_report, &mutation_cursor, &duration) &&
                    build_artifact_fanout_provenance_record_read_line(mutation_report, &mutation_cursor, &log_fingerprint);
                if (mutation_rows_valid) mutation_seen[index] = true;
            }
            bool mutation_accounted = mutation_rows_valid && mutation_worker && mutation_aggregate && path_exists(arena, mutation_path) &&
                mutation_seen[0] && mutation_seen[1] && mutation_seen[2] &&
                mutation_representatives[0] == 1 && mutation_representatives[1] == 1 && mutation_representatives[2] == 2 &&
                mutation_launches[0] == 0 && mutation_launches[1] == 1 && mutation_launches[2] == 1 &&
                mutation_statuses[0] == CLANG_ANALYZE_CONTEXT_FAILURE && mutation_statuses[1] == CLANG_ANALYZE_CONTEXT_FAILURE &&
                mutation_statuses[2] == CLANG_ANALYZE_PASS && clang_analyze_test_line_count(clang_analyze_read(arena, count_path)) == 2;
            clang_analyze_test_check(mutation_accounted, S8("later-execution-invalidates-earlier-alias"), &state);
            remove_path_recursive(arena, mutation_path);
            }
            if (launch_counted && dedup_plan.aliased_rows)
            {
                u64 alias = 0;
                u64 representative = 0;
                for (u64 i = 0; i < dedup_plan.count; i += 1)
                {
                    if (dedup_plan.units[i].representative != i)
                    {
                        alias = i;
                        representative = dedup_plan.units[i].representative;
                        break;
                    }
                }
                String8 result_path = path_join(arena, clang_analyze_shard_directory(arena, dedup_options, 0), S8("result.txt"));
                String8 original = clang_analyze_read(arena, result_path);
                String8 changed = clang_analyze_test_rewrite_result_row(arena, original, alias, alias, alias);
                clang_analyze_test_begin(S8("alias-representative-is-independently-reconstructed"), true);
                bool rejected_mapping = changed.length && clang_analyze_write(arena, result_path, changed) &&
                    !clang_analyze_aggregate(arena, dedup_options, dedup_plan);
                clang_analyze_test_check(rejected_mapping, S8("alias-representative-is-independently-reconstructed"), &state);
                clang_analyze_test_begin(S8("restored-alias-result"), false);
                bool restored = clang_analyze_write(arena, result_path, original) && clang_analyze_aggregate(arena, dedup_options, dedup_plan);
                clang_analyze_test_check(restored, S8("restored-alias-result"), &state);
                String8 alias_path = path_join(arena, clang_analyze_shard_directory(arena, dedup_options, 0),
                    string_format(arena, S8("unit-{u64}.log"), alias));
                String8 representative_path = path_join(arena, clang_analyze_shard_directory(arena, dedup_options, 0),
                    string_format(arena, S8("unit-{u64}.log"), representative));
                String8 representative_log = clang_analyze_read(arena, representative_path);
                clang_analyze_test_begin(S8("alias-diagnostic-copy-is-verified"), true);
                bool rejected_log = clang_analyze_write(arena, alias_path, S8("tampered alias log\n")) &&
                    !clang_analyze_aggregate(arena, dedup_options, dedup_plan);
                clang_analyze_test_check(rejected_log, S8("alias-diagnostic-copy-is-verified"), &state);
                clang_analyze_test_begin(S8("restored-alias-diagnostic"), false);
                bool restored_log = clang_analyze_write(arena, alias_path, representative_log) &&
                    clang_analyze_aggregate(arena, dedup_options, dedup_plan);
                clang_analyze_test_check(restored_log, S8("restored-alias-diagnostic"), &state);
            }
            String8 failure_database = path_join(arena, dedup_root, S8("failure-compile_commands.json"));
            String8 warning[] = {S8("-DFIXTURE_WARNING")};
            String8 failure_rows[] = {
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/warning-z.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(warning), count_path),
                clang_analyze_test_fixture_database_row(arena, source_directory, S8("alpha.c"), S8("obj/warning-a.o"), fixture,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(warning), count_path),
            };
            String8 failure_database_text = string_format(arena, S8("[{S8},{S8}]"), failure_rows[0], failure_rows[1]);
            ClangAnalyzeOptions failure_options = dedup_options;
            failure_options.database = failure_database;
            failure_options.results = path_join(arena, dedup_root, S8("warning-results"));
            failure_options.worker = false;
            failure_options.aggregate = false;
            ClangAnalyzePlan failure_plan = {0};
            u64 expected_failure_aliases = BUSTER_LINUX ? 1 : 0;
            bool failure_planned = clang_analyze_write(arena, count_path, S8("")) &&
                clang_analyze_write(arena, failure_database, failure_database_text) && clang_analyze_plan(arena, failure_options, &failure_plan) &&
                failure_plan.unique_executions == failure_plan.count - expected_failure_aliases && failure_plan.aliased_rows == expected_failure_aliases &&
                clang_analyze_new_directory(arena, failure_options.results) &&
                clang_analyze_write(arena, path_join(arena, failure_options.results, S8("manifest.txt")), failure_plan.manifest);
            failure_options.worker = true;
            clang_analyze_test_begin(S8("failure-status-and-log-propagate-to-alias"), true);
            bool warning_worker = failure_planned && clang_analyze_worker(arena, failure_options, failure_plan);
            failure_options.worker = false;
            failure_options.aggregate = true;
            bool warning_aggregate = failure_planned && clang_analyze_aggregate(arena, failure_options, failure_plan);
            String8 warning_report = clang_analyze_read(arena, path_join(arena,
                clang_analyze_shard_directory(arena, failure_options, 0), S8("result.txt")));
            u64 warning_cursor = 0;
            String8 warning_magic = {0};
            String8 warning_fingerprint = {0};
            u64 warning_shard = 0;
            u64 warning_selected = 0;
            u64 warning_executions = 0;
            u64 warning_aliases = 0;
            u64 warning_elapsed = 0;
            u64 warning_rss = 0;
            bool warning_rows_valid = build_artifact_fanout_provenance_record_read_line(warning_report, &warning_cursor, &warning_magic) &&
                string_equal(warning_magic, S8("BUSTER_CLANG_ANALYZE_RESULT_V2")) &&
                build_artifact_fanout_provenance_record_read_line(warning_report, &warning_cursor, &warning_fingerprint) &&
                string_equal(warning_fingerprint, failure_plan.fingerprint) &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_shard) && warning_shard == 0 &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_selected) && warning_selected == failure_plan.count &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_executions) && warning_executions == failure_plan.unique_executions &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_aliases) && warning_aliases == failure_plan.aliased_rows &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_elapsed) &&
                build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_rss);
            u64 warning_statuses[2] = {0};
            u64 warning_representatives[2] = {0};
            u64 warning_launches[2] = {0};
            u64 warning_durations[2] = {0};
            String8 warning_logs[2] = {0};
            bool warning_seen[2] = {0};
            for (u64 row = 0; warning_rows_valid && row < warning_selected; row += 1)
            {
                u64 index = 0;
                String8 log_fingerprint = {0};
                warning_rows_valid = build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &index) && index < 2 && !warning_seen[index] &&
                    build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_representatives[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_launches[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_statuses[index]) &&
                    build_artifact_fanout_provenance_record_read_u64(warning_report, &warning_cursor, &warning_durations[index]) &&
                    build_artifact_fanout_provenance_record_read_line(warning_report, &warning_cursor, &log_fingerprint);
                if (warning_rows_valid)
                {
                    warning_seen[index] = true;
                    warning_logs[index] = clang_analyze_read(arena, path_join(arena,
                        clang_analyze_shard_directory(arena, failure_options, 0), string_format(arena, S8("unit-{u64}.log"), index)));
                    warning_rows_valid = string_equal(log_fingerprint, clang_analyze_sha256(arena, warning_logs[index]));
                }
            }
            warning_rows_valid = warning_rows_valid && warning_cursor == warning_report.length && warning_seen[0] && warning_seen[1];
            for (u64 i = 0; warning_rows_valid && i < failure_plan.count; i += 1)
            {
                u64 representative = failure_plan.units[i].representative;
                warning_rows_valid = warning_rows_valid && warning_statuses[i] == CLANG_ANALYZE_WARNING &&
                    warning_representatives[i] == representative && warning_launches[i] == (representative == i) &&
                    (representative == i || warning_durations[i] == 0);
            }
            warning_rows_valid = warning_rows_valid && string_equal(warning_logs[0], warning_logs[1]);
            bool failure_accounted = failure_planned && !warning_worker && !warning_aggregate && warning_rows_valid &&
                clang_analyze_test_line_count(clang_analyze_read(arena, count_path)) == failure_plan.unique_executions;
            clang_analyze_test_check(failure_accounted, S8("failure-status-and-log-propagate-to-alias"), &state);
        }
        if (ready)
        {
            ClangAnalyzeOptions options = {.database = database, .config = S8("Release"), .shards = 1, .jobs = 1, .timeout = 1, .quiet = true,
                .results = path_join(arena, root, S8("single"))};
            String8 contents = clang_analyze_test_database(arena, root, fixture, modes[0]);
            clang_analyze_test_begin(S8("one-shard-one-job"), false);
            clang_analyze_test_check(clang_analyze_write(arena, database, contents) && clang_analyze_run(arena, options), S8("one-shard-one-job"), &state);
            clang_analyze_test_begin(S8("no-result-cache"), true);
            clang_analyze_test_check(!clang_analyze_run(arena, options), S8("no-result-cache"), &state);
            options.results = path_join(arena, root, S8("independent"));
            options.prepare = true;
            clang_analyze_test_begin(S8("prepare-worker-aggregate"), false);
            bool independent = clang_analyze_run(arena, options);
            options.prepare = false;
            options.worker = true;
            independent = independent && clang_analyze_run(arena, options);
            options.worker = false;
            options.aggregate = true;
            independent = independent && clang_analyze_run(arena, options);
            clang_analyze_test_check(independent, S8("prepare-worker-aggregate"), &state);
            options.aggregate = false;
            options.results = path_join(arena, root, S8("missing-executable"));
            options.clang = path_join(arena, root, S8("not-a-compiler"));
            clang_analyze_test_begin(S8("launch-failure"), true);
            clang_analyze_test_check(!clang_analyze_run(arena, options), S8("launch-failure"), &state);
            options.clang = (String8){0};
            String8 invalid[] = {S8("[]"), S8("[{}]"), S8("["), S8("[{\"file\":\"missing.c\"}]"),
                                string_format(arena, S8("{S8}garbage"), contents),
                                string_format(arena, S8("{S8},]"), string_slice(contents, 0, contents.length - 1))};
            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
            {
                ClangAnalyzePlan plan;
                bool written = clang_analyze_write(arena, database, invalid[i]);
                clang_analyze_test_begin(S8("malformed-or-empty-database"), true);
                clang_analyze_test_check(written && !clang_analyze_plan(arena, options, &plan), S8("malformed-or-empty-database"), &state);
            }
            // Repeat the complete array contents to prove duplicate inventory is an
            // error independently of result coverage checks.
            String8 entries = string_slice(contents, 1, contents.length - 1);
            String8 duplicate = string_format(arena, S8("[{S8},{S8}]"), entries, entries);
            ClangAnalyzePlan duplicate_plan;
            bool duplicate_written = clang_analyze_write(arena, database, duplicate);
            clang_analyze_test_begin(S8("duplicate-database-entry"), true);
            clang_analyze_test_check(duplicate_written && !clang_analyze_plan(arena, options, &duplicate_plan), S8("duplicate-database-entry"), &state);
            // Real Clang validates compile-command projection, context snapshots,
            // same-run grouping and cwd handling.
            String8 source = S8("#if __has_include(\"buster-analyzer-negative-lookup.h\")\n"
                                "#include \"buster-analyzer-negative-lookup.h\"\n#endif\n"
                                "#include \"input.h\"\nint value(void) { return VALUE; }\n");
            String8 header = S8("#define VALUE 42\n");
            String8 context_directory = path_join(arena, root, S8("real-context"));
            String8 forced_context_directory = path_join(arena, root, S8("forced-context"));
            String8 missing_search_directory = path_join(arena, root, S8("missing-search"));
            String8 absolute_lookup_header = path_join(arena, root, S8("absolute-negative-lookup.h"));
            String8 absolute_lookup_source = string_format(arena,
                S8("#ifdef __clang_analyzer__\n#if __has_include(\"{S8}\")\n#define BUSTER_ABSOLUTE_LOOKUP 1\n#endif\n#endif\n"
                   "int value(void) {{\n#ifdef BUSTER_ABSOLUTE_LOOKUP\n return 1;\n#else\n return 0;\n#endif\n}}\n"),
                absolute_lookup_header);
            make_directory_recursive(arena, context_directory);
            make_directory_recursive(arena, forced_context_directory);
            bool files = clang_analyze_write(arena, path_join(arena, context_directory, S8("real.c")), source) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("input.h")), header) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-paste.c")),
                             S8("#define JOIN(a,b) a ## b\nconst char *value = JOIN(__DA,TE__);\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-suppressed.c")),
                             S8("#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wdate-time\"\n"
                                "#define JOIN(a,b) a ## b\nconst char *value = JOIN(__DA,TE__);\n#pragma clang diagnostic pop\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-pragma-macro.c")),
                             S8("#define PRAGMA(x) _Pragma(#x)\n#define IGNORE_TIME PRAGMA(clang diagnostic ignored \"-Wdate-time\")\n"
                                "IGNORE_TIME\n#define JOIN(a,b) a ## b\nconst char *value = JOIN(__DA,TE__);\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-pragma-warning.c")),
                             S8("#pragma clang diagnostic warning \"-Wdate-time\"\n#define JOIN(a,b) a ## b\n"
                                "const char *value = JOIN(__DA,TE__);\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-everything.c")),
                             S8("#pragma clang diagnostic ignored \"-Weverything\"\n#define JOIN(a,b) a ## b\n"
                                "const char *value = JOIN(__DA,TE__);\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("driver-w.cfg")), S8("-w\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("driver-analyzer-config.cfg")),
                             S8("-Xanalyzer\n-analyzer-config\n-Xanalyzer\noptin.taint.TaintPropagation:Config=/tmp/taint-rules.yml\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("driver-ast-merge.cfg")),
                             S8("-Xclang\n-ast-merge\n-Xclang\n/tmp/external.ast\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("driver-pch.cfg")),
                             S8("-include-pch\n/tmp/external.pch\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("driver-module.cfg")),
                             S8("-fmodules\n-fmodule-map-file=/tmp/external.modulemap\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("analyzer-config.c")),
                             S8("int analyzer_config_control(void) { return 1; }\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("time-w.c")),
                             S8("#define JOIN(a,b) a ## b\nconst char *value = JOIN(__DA,TE__);\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("analyzer-conditional-time.c")),
                             S8("#ifdef __clang_analyzer__\nconst char *value = __TIME__;\n#endif\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("analyzer-conditional-include.c")),
                             S8("#ifdef __clang_analyzer__\n#include \"analyzer-only.h\"\n#endif\n"
                                "int analyzer_only(void) { return ANALYZER_ONLY_VALUE; }\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("analyzer-only.h")),
                             S8("#define ANALYZER_ONLY_VALUE 42\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("macro-paste.c")),
                             S8("#pragma clang diagnostic ignored \"-Wunused-variable\"\n"
                                "#define JOIN(a,b) a ## b\nint value(void) { return JOIN(1,2); }\n")) &&
                         clang_analyze_write(arena, path_join(arena, context_directory, S8("absolute-lookup.c")), absolute_lookup_source) &&
                         clang_analyze_write(arena, path_join(arena, forced_context_directory, S8("forced.h")),
                             S8("#if __has_include(\"forced-next.h\")\n#define FORCED_VALUE 1\n#endif\n"));
            String8 real = string_format(arena,
                S8("[{{\"directory\":{S8},\"file\":\"real.c\",\"output\":\"Release/a-real.o\",\"arguments\":[{S8},\"-c\",\"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",\"-I\",\"../missing-search\",\"-include\",\"../forced-context/forced.h\",\"real.c\",\"-o\",\"Release/a-real.o\"]}},"
                   "{{\"directory\":{S8},\"file\":\"real.c\",\"output\":\"Release/z-real.o\",\"arguments\":[{S8},\"-c\",\"-fwrapv\",\"-fno-strict-aliasing\",\"-funsigned-char\",\"-I\",\"../missing-search\",\"-include\",\"../forced-context/forced.h\",\"real.c\",\"-o\",\"Release/z-real.o\"]}}]"),
                clang_analyze_test_json(arena, context_directory), clang_analyze_test_json(arena, clang),
                clang_analyze_test_json(arena, context_directory), clang_analyze_test_json(arena, clang));
            options.results = path_join(arena, root, S8("real-clang"));
            options.timeout = 30;
            ClangAnalyzePlan real_plan = {0};
            bool real_planned = files && clang_analyze_write(arena, database, real) && clang_analyze_plan(arena, options, &real_plan);
            u64 real_aliases = BUSTER_LINUX ? 1 : 0;
            bool real_context = real_planned && real_plan.count == 2 && real_plan.aliased_rows == real_aliases &&
                real_plan.unique_executions == real_plan.count - real_aliases;
            if (real_planned && !real_context)
            {
                String8 compiler_path = clang_analyze_executable_path(arena, real_plan.units[0]);
                string_print(S8("ANALYZE_SELF_TEST_CONTEXT rows={u64} unique={u64} aliases={u64} reason={S8} compiler={S8} clang_name={S8} native={S8}\n"),
                    real_plan.count, real_plan.unique_executions, real_plan.aliased_rows, real_plan.units[0].ineligible_reason,
                    compiler_path, clang_analyze_clang_name(compiler_path) ? S8("yes") : S8("no"),
                    clang_analyze_native_clang_binary(arena, compiler_path) ? S8("yes") : S8("no"));
            }
            if (real_context && BUSTER_LINUX)
            {
                u64 representative = real_plan.units[0].representative;
                real_context = real_plan.units[representative].context_proof.length &&
                               clang_analyze_context_matches(arena, real_plan.units[representative]);
                if (!real_context) string_print(S8("ANALYZE_SELF_TEST_CONTEXT reason={S8}\n"), real_plan.units[representative].ineligible_reason);
            }
            clang_analyze_test_check(real_context, S8("real-clang-context-proof-and-dedup"), &state);
#if BUSTER_LINUX
            if (ready)
            {
                String8 compiler_root = path_join(arena, root, S8("private-compiler"));
                bool compiler_root_ready = clang_analyze_new_directory(arena, compiler_root);
                String8 compiler_directory = path_join(arena, compiler_root, S8("bin"));
                bool compiler_directory_ready = compiler_root_ready && clang_analyze_new_directory(arena, compiler_directory);
                String8 compiler_through_directory = path_join(arena, compiler_root, S8("through-bin"));
                String8 compiler_target = path_join(arena, compiler_directory, S8("clang-target"));
                String8 compiler_alias = path_join(arena, compiler_through_directory, S8("clang"));
                String8 compiler_target_moved = path_join(arena, compiler_directory, S8("clang-target.moved"));
                String8 clang_z = string_duplicate_arena(arena, clang, true);
                String8 compiler_through_directory_z = string_duplicate_arena(arena, compiler_through_directory, true);
                String8 compiler_target_z = string_duplicate_arena(arena, compiler_target, true);
                String8 compiler_alias_z = string_duplicate_arena(arena, compiler_alias, true);
                String8 compiler_target_moved_z = string_duplicate_arena(arena, compiler_target_moved, true);
                bool target_linked = compiler_directory_ready && symlink(clang_z.pointer, compiler_target_z.pointer) == 0;
                bool trailing_directory_linked = target_linked && symlink("bin/", compiler_through_directory_z.pointer) == 0;
                bool alias_linked = trailing_directory_linked && symlink("clang-target", compiler_alias_z.pointer) == 0;
                String8 compiler_link_database = path_join(arena, root, S8("compiler-symlink-compile_commands.json"));
                String8 compiler_link_count = path_join(arena, root, S8("compiler-symlink-count.txt"));
                String8 compiler_link_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/compiler-z.o"),
                        compiler_alias, (SliceString8){0}, compiler_link_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/compiler-a.o"),
                        compiler_alias, (SliceString8){0}, compiler_link_count),
                };
                String8 compiler_link_text = string_format(arena, S8("[{S8},{S8}]"), compiler_link_rows[0], compiler_link_rows[1]);
                ClangAnalyzeOptions compiler_link_options = options;
                compiler_link_options.database = compiler_link_database;
                ClangAnalyzePlan compiler_link_plan = {0};
                bool compiler_link_planned = alias_linked && clang_analyze_write(arena, compiler_link_database, compiler_link_text) &&
                    clang_analyze_plan(arena, compiler_link_options, &compiler_link_plan);
                u64 compiler_link_representative = compiler_link_planned ? compiler_link_plan.units[0].representative : 0;
                bool compiler_link_proven = compiler_link_planned && compiler_link_plan.count == 2 &&
                    compiler_link_plan.aliased_rows == 1 && compiler_link_plan.units[compiler_link_representative].context_proof.length &&
                    compiler_link_plan.units[compiler_link_representative].compiler_resolved_path.length &&
                    clang_analyze_context_matches(arena, compiler_link_plan.units[compiler_link_representative]);
                clang_analyze_test_check(compiler_link_proven, S8("absolute-clang-symlink-chain-trailing-directory-target-preserves-argv0"), &state);

                String8 compiler_sibling = path_join(arena, compiler_root, S8("result-sibling.txt"));
                bool compiler_sibling_stable = compiler_link_proven && clang_analyze_write(arena, compiler_sibling, S8("unrelated result sibling\n")) &&
                    clang_analyze_context_matches(arena, compiler_link_plan.units[compiler_link_representative]);
                clang_analyze_test_check(compiler_sibling_stable, S8("compiler-ancestor-sibling-does-not-invalidate-path-proof"), &state);
                unlink(string_duplicate_arena(arena, compiler_sibling, true).pointer);

                bool alias_retargeted = unlink(compiler_alias_z.pointer) == 0 && symlink(clang_z.pointer, compiler_alias_z.pointer) == 0;
                bool compiler_link_retarget_rejected = compiler_link_proven && alias_retargeted &&
                    !clang_analyze_context_matches(arena, compiler_link_plan.units[compiler_link_representative]);
                clang_analyze_test_begin(S8("compiler-symlink-retarget-invalidates-context"), true);
                clang_analyze_test_check(compiler_link_retarget_rejected, S8("compiler-symlink-retarget-invalidates-context"), &state);

                bool alias_restored = unlink(compiler_alias_z.pointer) == 0 && symlink("clang-target", compiler_alias_z.pointer) == 0;
                bool target_renamed = alias_restored && rename(compiler_target_z.pointer, compiler_target_moved_z.pointer) == 0;
                bool compiler_link_target_rename_rejected = compiler_link_proven && target_renamed &&
                    !clang_analyze_context_matches(arena, compiler_link_plan.units[compiler_link_representative]);
                clang_analyze_test_begin(S8("compiler-symlink-target-rename-invalidates-context"), true);
                clang_analyze_test_check(compiler_link_target_rename_rejected,
                    S8("compiler-symlink-target-rename-invalidates-context"), &state);
                if (target_renamed) rename(compiler_target_moved_z.pointer, compiler_target_z.pointer);
                unlink(compiler_alias_z.pointer);
                unlink(compiler_target_z.pointer);
            }

            if (ready)
            {
                String8 wrapper_directory = path_join(arena, root, S8("ldd-wrapper"));
                String8 wrapper_path = path_join(arena, wrapper_directory, S8("ldd"));
                String8 marker_path = path_join(arena, root, S8("ldd-static-mode"));
                String8 system_ldd = executable_resolve_in_path(arena, S8("ldd"));
                String8 wrapper_path_z = string_duplicate_arena(arena, wrapper_path, true);
                String8 marker_path_z = string_duplicate_arena(arena, marker_path, true);
                String8 wrapper_text = string_format(arena,
                    S8("#!/bin/sh\nif [ -e '{S8}' ]; then\n  printf 'statically linked\\n'\nelse\n  exec '{S8}' \"$@\"\nfi\n"),
                    marker_path, system_ldd);
                make_directory_recursive(arena, wrapper_directory);
                bool wrapper_ready = system_ldd.length && clang_analyze_write(arena, wrapper_path, wrapper_text) &&
                    chmod(wrapper_path_z.pointer, 0700) == 0;
                char* old_path_value = getenv("PATH");
                char* old_path_copy = old_path_value ? strdup(old_path_value) : 0;
                String8 isolated_path = string_format(arena, S8("{S8}:/usr/lib/llvm-21/bin:/usr/bin:/bin"), wrapper_directory);
                String8 isolated_path_z = string_duplicate_arena(arena, isolated_path, true);
                SliceString8 saved_process_environment = program_state->input.environment_values;
                String8* isolated_environment = arena_allocate(arena, String8, saved_process_environment.length);
                if (saved_process_environment.length)
                {
                    memcpy(isolated_environment, saved_process_environment.pointer,
                        saved_process_environment.length * sizeof(*isolated_environment));
                }
                bool path_in_process_environment = false;
                for (u64 i = 0; i < program_state->input.environment_keys.length; i += 1)
                {
                    if (string_equal(program_state->input.environment_keys.pointer[i], S8("PATH")))
                    {
                        isolated_environment[i] = isolated_path;
                        path_in_process_environment = true;
                    }
                }
                program_state->input.environment_values =
                    (SliceString8){.pointer = isolated_environment, .length = saved_process_environment.length};
                bool path_changed = wrapper_ready && old_path_copy && path_in_process_environment &&
                    setenv("PATH", isolated_path_z.pointer, 1) == 0;
                String8 runtime_database = path_join(arena, root, S8("runtime-closure-compile_commands.json"));
                String8 runtime_count = path_join(arena, root, S8("runtime-closure-count.txt"));
                String8 runtime_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/runtime-z.o"),
                        clang, (SliceString8){0}, runtime_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/runtime-a.o"),
                        clang, (SliceString8){0}, runtime_count),
                };
                String8 runtime_text = string_format(arena, S8("[{S8},{S8}]"), runtime_rows[0], runtime_rows[1]);
                ClangAnalyzeOptions runtime_options = options;
                runtime_options.database = runtime_database;
                ClangAnalyzePlan runtime_plan = {0};
                bool runtime_planned = path_changed && clang_analyze_write(arena, runtime_database, runtime_text) &&
                    clang_analyze_plan(arena, runtime_options, &runtime_plan);
                u64 runtime_representative = runtime_planned ? runtime_plan.units[0].representative : 0;
                bool runtime_closure_stable = runtime_planned && runtime_plan.aliased_rows == 1 &&
                    runtime_plan.units[runtime_representative].runtime_fingerprint.length &&
                    clang_analyze_context_matches(arena, runtime_plan.units[runtime_representative]);
                bool marker_written = runtime_closure_stable && clang_analyze_write(arena, marker_path, S8("changed loader lookup state\n"));
                bool runtime_closure_invalidated = marker_written &&
                    !clang_analyze_context_matches(arena, runtime_plan.units[runtime_representative]);
                if (marker_written && !runtime_closure_invalidated)
                {
                    String8 debug_output = {0};
                    String8 debug_error = {0};
                    String8 debug_arguments[] = {runtime_plan.units[runtime_representative].runtime_ldd_path,
                                                 runtime_plan.units[runtime_representative].compiler_path};
                    clang_analyze_query(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(debug_arguments), context_directory,
                                        &debug_output, &debug_error);
                    string_print(S8("ANALYZE_SELF_TEST_RUNTIME_DEBUG ldd={S8} marker={S8} output={S8} stderr={S8}\n"),
                        runtime_plan.units[runtime_representative].runtime_ldd_path, marker_path, debug_output, debug_error);
                }
                if (old_path_copy) setenv("PATH", old_path_copy, 1);
                else unsetenv("PATH");
                program_state->input.environment_values = saved_process_environment;
                free(old_path_copy);
                clang_analyze_test_begin(S8("runtime-dependency-resolution-is-replayed"), false);
                clang_analyze_test_check(runtime_closure_stable, S8("runtime-dependency-resolution-is-replayed"), &state);
                clang_analyze_test_begin(S8("runtime-dependency-resolution-change-invalidates"), true);
                clang_analyze_test_check(runtime_closure_invalidated,
                    S8("runtime-dependency-resolution-change-invalidates"), &state);
                remove_path_recursive(arena, marker_path);
                remove_path_recursive(arena, wrapper_directory);
            }

            if (ready)
            {
                String8 basename_database = path_join(arena, root, S8("basename-clang-compile_commands.json"));
                String8 basename_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/z-basename.o"),
                        S8("clang"), (SliceString8){0}, path_join(arena, root, S8("basename-count.txt"))),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("real.c"), S8("Release/a-basename.o"),
                        S8("clang"), (SliceString8){0}, path_join(arena, root, S8("basename-count.txt"))),
                };
                String8 basename_text = string_format(arena, S8("[{S8},{S8}]"), basename_rows[0], basename_rows[1]);
                ClangAnalyzePlan basename_plan = {0};
                ClangAnalyzeOptions basename_options = options;
                basename_options.database = basename_database;
                String8 clang_directory = path_parent(arena, clang);
                char* old_path_value = getenv("PATH");
                char* old_path_copy = old_path_value ? strdup(old_path_value) : 0;
                String8 clang_directory_z = string_duplicate_arena(arena, clang_directory, true);
                bool path_changed = clang_directory.length && setenv("PATH", clang_directory_z.pointer, 1) == 0;
                bool basename_planned = path_changed && clang_analyze_write(arena, basename_database, basename_text) &&
                    clang_analyze_plan(arena, basename_options, &basename_plan);
                bool path_restored = old_path_copy ? setenv("PATH", old_path_copy, 1) == 0 : unsetenv("PATH") == 0;
                free(old_path_copy);
                bool basename_uses_original_argv = basename_planned && path_restored && basename_plan.count == 2 &&
                    basename_plan.unique_executions == 2 && basename_plan.aliased_rows == 0 &&
                    basename_plan.units[0].representative == 0 && basename_plan.units[1].representative == 1 &&
                    string_equal(basename_plan.units[0].command.pointer[0], S8("clang")) &&
                    string_equal(basename_plan.units[1].command.pointer[0], S8("clang")) &&
                    basename_plan.units[0].ineligible_reason.length && basename_plan.units[1].ineligible_reason.length;
                clang_analyze_test_check(basename_uses_original_argv, S8("basename-clang-keeps-argv-and-falls-back"), &state);
            }
            if (ready)
            {
                String8 dynamic_database = path_join(arena, root, S8("dynamic-time-compile_commands.json"));
                String8 dynamic_count = path_join(arena, root, S8("dynamic-time-count.txt"));
                String8 warning_suppression[] = {S8("-w")};
                String8 dynamic_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-paste.c"), S8("Release/time-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-paste.c"), S8("Release/time-a.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-suppressed.c"), S8("Release/suppressed-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-suppressed.c"), S8("Release/suppressed-a.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-pragma-macro.c"), S8("Release/pragma-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-pragma-macro.c"), S8("Release/pragma-a.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-pragma-warning.c"), S8("Release/pragma-warning-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-pragma-warning.c"), S8("Release/pragma-warning-a.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-everything.c"), S8("Release/everything-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-everything.c"), S8("Release/everything-a.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-w.c"), S8("Release/w-z.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_suppression), dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("time-w.c"), S8("Release/w-a.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(warning_suppression), dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("macro-paste.c"), S8("Release/macro-z.o"), clang, (SliceString8){0}, dynamic_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("macro-paste.c"), S8("Release/macro-a.o"), clang, (SliceString8){0}, dynamic_count),
                };
                String8 dynamic_text = string_format(arena, S8("[{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8},{S8}]"),
                    dynamic_rows[0], dynamic_rows[1], dynamic_rows[2], dynamic_rows[3], dynamic_rows[4], dynamic_rows[5], dynamic_rows[6],
                    dynamic_rows[7], dynamic_rows[8], dynamic_rows[9], dynamic_rows[10], dynamic_rows[11], dynamic_rows[12], dynamic_rows[13]);
                ClangAnalyzeOptions dynamic_options = options;
                dynamic_options.database = dynamic_database;
                ClangAnalyzePlan dynamic_plan = {0};
                bool dynamic_planned = clang_analyze_write(arena, dynamic_database, dynamic_text) &&
                    clang_analyze_plan(arena, dynamic_options, &dynamic_plan);
                u64 time_paste_representatives = 0;
                u64 suppressed_representatives = 0;
                u64 macro_pragma_representatives = 0;
                u64 warning_pragma_representatives = 0;
                u64 everything_pragma_representatives = 0;
                u64 w_option_representatives = 0;
                u64 macro_paste_representatives = 0;
                u64 macro_paste_representative = BUSTER_STRING_NO_MATCH;
                bool dynamic_reasons = dynamic_planned && dynamic_plan.count == 14 && dynamic_plan.unique_executions == 13 &&
                    dynamic_plan.aliased_rows == 1;
                for (u64 i = 0; dynamic_reasons && i < dynamic_plan.count; i += 1)
                {
                    ClangAnalyzeUnit unit = dynamic_plan.units[i];
                    if (string_equal(unit.entry.file, S8("time-paste.c")))
                    {
                        time_paste_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_starts_with_sequence(unit.ineligible_reason, S8("dynamic-time"));
                    }
                    else if (string_equal(unit.entry.file, S8("time-suppressed.c")))
                    {
                        suppressed_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_equal(unit.ineligible_reason, S8("date-time-warning-suppressed"));
                    }
                    else if (string_equal(unit.entry.file, S8("time-pragma-macro.c")))
                    {
                        macro_pragma_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_equal(unit.ineligible_reason, S8("date-time-warning-suppressed"));
                    }
                    else if (string_equal(unit.entry.file, S8("time-pragma-warning.c")))
                    {
                        warning_pragma_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_equal(unit.ineligible_reason, S8("date-time-warning-downgraded"));
                    }
                    else if (string_equal(unit.entry.file, S8("time-everything.c")))
                    {
                        everything_pragma_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_equal(unit.ineligible_reason, S8("date-time-warning-suppressed"));
                    }
                    else if (string_equal(unit.entry.file, S8("time-w.c")))
                    {
                        w_option_representatives += unit.representative == i;
                        dynamic_reasons = dynamic_reasons && string_equal(unit.ineligible_reason, S8("warning-suppression-option"));
                    }
                    else if (string_equal(unit.entry.file, S8("macro-paste.c")))
                    {
                        macro_paste_representatives += unit.representative == i;
                        if (string_equal(unit.entry.output, S8("Release/macro-a.o"))) macro_paste_representative = i;
                    }
                }
                dynamic_reasons = dynamic_reasons && time_paste_representatives == 2 && suppressed_representatives == 2 &&
                    macro_pragma_representatives == 2 && warning_pragma_representatives == 2 && everything_pragma_representatives == 2 &&
                    w_option_representatives == 2 &&
                    macro_paste_representatives == 1 && macro_paste_representative != BUSTER_STRING_NO_MATCH;
                clang_analyze_test_check(dynamic_reasons, S8("clang-date-time-probe-handles-token-paste-broad-pragma-and-w-controls"), &state);

                String8 driver_config = path_join(arena, context_directory, S8("driver-w.cfg"));
                String8 config_option = string_format(arena, S8("--config={S8}"), driver_config);
                String8 original_config_arguments[] = {clang, config_option, S8("-x"), S8("c"), S8("time-paste.c")};
                String8 expanded_analyzer_arguments[] = {clang, config_option, S8("--analyze"), S8("-x"), S8("c"), S8("time-paste.c")};
                ClangAnalyzeUnit expanded_analyzer = {.entry = {.directory = context_directory,
                        .arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(original_config_arguments)},
                    .command = (SliceString8)BUSTER_ARRAY_TO_SLICE(expanded_analyzer_arguments)};
                String8 ignored_config_fingerprint = {0};
                bool config_suppression_rejected = !clang_analyze_driver_expansion(arena, expanded_analyzer, &ignored_config_fingerprint);
                SliceString8 config_probe_arguments = clang_analyze_make_dynamic_time_command(arena, expanded_analyzer);
                String8 config_preprocessed = {0};
                String8 config_error = {0};
                bool suppressed_probe_passed = clang_analyze_query(arena, config_probe_arguments,
                    context_directory, &config_preprocessed, &config_error);
                bool pasted_builtin_expanded = !clang_analyze_contains(config_preprocessed, S8("__DATE__")) &&
                    clang_analyze_contains(config_preprocessed, S8("const char *value"));
                bool injected_warning_hidden = suppressed_probe_passed &&
                    !clang_analyze_dynamic_time_warning_emitted(config_error) &&
                    !clang_analyze_dynamic_time_warning_suppressed(config_preprocessed);
                clang_analyze_test_check(config_suppression_rejected && injected_warning_hidden && pasted_builtin_expanded,
                    S8("effective-driver-expansion-rejects-config-injected-w"), &state);

                String8 analyzer_config_path = path_join(arena, context_directory, S8("driver-analyzer-config.cfg"));
                String8 analyzer_config_option = string_format(arena, S8("--config={S8}"), analyzer_config_path);
                String8 analyzer_config_arguments[] = {clang, analyzer_config_option, S8("-x"), S8("c"), S8("time-paste.c")};
                String8 analyzer_config_command[] = {clang, analyzer_config_option, S8("--analyze"), S8("-x"), S8("c"), S8("time-paste.c")};
                ClangAnalyzeUnit analyzer_config_unit = {.entry = {.directory = context_directory,
                        .arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(analyzer_config_arguments)},
                    .command = (SliceString8)BUSTER_ARRAY_TO_SLICE(analyzer_config_command)};
                String8 analyzer_config_fingerprint = {0};
                bool analyzer_config_injected_rejected = !clang_analyze_driver_expansion(arena, analyzer_config_unit,
                                                                                         &analyzer_config_fingerprint);
                clang_analyze_test_check(analyzer_config_injected_rejected,
                    S8("effective-driver-expansion-rejects-injected-analyzer-config"), &state);

                String8 normal_warning_arguments[] = {clang, S8("--analyze"), S8("-Wreserved-module-identifier"),
                    S8("-x"), S8("c"), S8("time-paste.c")};
                String8 normal_warning_expansion = {0};
                bool normal_warning_captured = clang_analyze_capture_driver_expansion(arena,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(normal_warning_arguments), context_directory, &normal_warning_expansion);
                bool normal_warning_kept = normal_warning_captured &&
                    clang_analyze_contains(normal_warning_expansion, S8("\"-Wreserved-module-identifier\"")) &&
                    !clang_analyze_driver_expansion_has_unsupported_option(normal_warning_expansion);
                bool module_options_still_rejected = clang_analyze_driver_expansion_has_unsupported_option(
                    S8("\"-module-file=/tmp/external.pcm\"")) &&
                    clang_analyze_driver_expansion_has_unsupported_option(S8("\"-module\""));
                clang_analyze_test_check(normal_warning_kept && module_options_still_rejected,
                    S8("real-clang-warning-token-is-safe-and-module-options-remain-unsupported"), &state);

                String8 external_ast_configs[] = {S8("driver-ast-merge.cfg"), S8("driver-pch.cfg"), S8("driver-module.cfg")};
                bool external_ast_expansions_rejected = true;
                bool external_module_expansion_rejected = false;
                for (u64 i = 0; external_ast_expansions_rejected && i < BUSTER_ARRAY_LENGTH(external_ast_configs); i += 1)
                {
                    String8 config_path = path_join(arena, context_directory, external_ast_configs[i]);
                    String8 option = string_format(arena, S8("--config={S8}"), config_path);
                    String8 original_arguments[] = {clang, option, S8("-x"), S8("c"), S8("time-paste.c")};
                    String8 analyzer_arguments[] = {clang, option, S8("--analyze"), S8("-x"), S8("c"), S8("time-paste.c")};
                    ClangAnalyzeUnit external_input_unit = {.entry = {.directory = context_directory,
                            .arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(original_arguments)},
                        .command = (SliceString8)BUSTER_ARRAY_TO_SLICE(analyzer_arguments)};
                    String8 fingerprint = {0};
                    external_ast_expansions_rejected = !clang_analyze_driver_expansion(arena, external_input_unit, &fingerprint);
                    if (i == 2) external_module_expansion_rejected = external_ast_expansions_rejected;
                }
                clang_analyze_test_check(external_ast_expansions_rejected,
                    S8("effective-driver-expansion-rejects-injected-ast-pch-and-module-inputs"), &state);
                clang_analyze_test_check(external_module_expansion_rejected,
                    S8("real-clang-config-injected-module-input-is-rejected"), &state);

                String8 analyzer_time_database = path_join(arena, root, S8("analyzer-conditional-time-compile_commands.json"));
                String8 analyzer_time_count = path_join(arena, root, S8("analyzer-conditional-time-count.txt"));
                String8 analyzer_time_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-conditional-time.c"),
                        S8("Release/analyzer-time-z.o"), clang, (SliceString8){0}, analyzer_time_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-conditional-time.c"),
                        S8("Release/analyzer-time-a.o"), clang, (SliceString8){0}, analyzer_time_count),
                };
                String8 analyzer_time_text = string_format(arena, S8("[{S8},{S8}]"), analyzer_time_rows[0], analyzer_time_rows[1]);
                ClangAnalyzeOptions analyzer_time_options = options;
                analyzer_time_options.database = analyzer_time_database;
                ClangAnalyzePlan analyzer_time_plan = {0};
                bool analyzer_time_planned = clang_analyze_write(arena, analyzer_time_database, analyzer_time_text) &&
                    clang_analyze_plan(arena, analyzer_time_options, &analyzer_time_plan);
                bool analyzer_time_falls_back = analyzer_time_planned && analyzer_time_plan.count == 2 &&
                    analyzer_time_plan.unique_executions == 2 && analyzer_time_plan.aliased_rows == 0;
                for (u64 i = 0; analyzer_time_falls_back && i < analyzer_time_plan.count; i += 1)
                {
                    analyzer_time_falls_back = analyzer_time_plan.units[i].ineligible_reason.length &&
                        string_starts_with_sequence(analyzer_time_plan.units[i].ineligible_reason, S8("dynamic-time"));
                }
                clang_analyze_test_check(analyzer_time_falls_back,
                    S8("analyzer-only-dynamic-time-builtin-keeps-rows-independent"), &state);

                String8 analyzer_include_database = path_join(arena, root, S8("analyzer-conditional-include-compile_commands.json"));
                String8 analyzer_include_count = path_join(arena, root, S8("analyzer-conditional-include-count.txt"));
                String8 analyzer_include_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-conditional-include.c"),
                        S8("Release/analyzer-include-z.o"), clang, (SliceString8){0}, analyzer_include_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-conditional-include.c"),
                        S8("Release/analyzer-include-a.o"), clang, (SliceString8){0}, analyzer_include_count),
                };
                String8 analyzer_include_text = string_format(arena, S8("[{S8},{S8}]"), analyzer_include_rows[0], analyzer_include_rows[1]);
                ClangAnalyzeOptions analyzer_include_options = options;
                analyzer_include_options.database = analyzer_include_database;
                ClangAnalyzePlan analyzer_include_plan = {0};
                bool analyzer_include_planned = clang_analyze_write(arena, analyzer_include_database, analyzer_include_text) &&
                    clang_analyze_plan(arena, analyzer_include_options, &analyzer_include_plan);
                u64 analyzer_include_representative = analyzer_include_planned ? analyzer_include_plan.units[0].representative : 0;
                String8 analyzer_only_header = clang_analyze_absolute_from(arena, context_directory, S8("analyzer-only.h"));
                bool analyzer_header_recorded = false;
                for (u64 i = 0; analyzer_include_planned && i < analyzer_include_plan.units[analyzer_include_representative].input_count; i += 1)
                {
                    analyzer_header_recorded = analyzer_header_recorded ||
                        string_equal(analyzer_include_plan.units[analyzer_include_representative].input_paths[i], analyzer_only_header);
                }
                bool analyzer_header_stable = analyzer_include_planned && analyzer_include_plan.aliased_rows == 1 &&
                    analyzer_header_recorded && clang_analyze_context_matches(arena,
                        analyzer_include_plan.units[analyzer_include_representative]);
                bool analyzer_header_changed = analyzer_header_stable &&
                    clang_analyze_write(arena, analyzer_only_header, S8("#define ANALYZER_ONLY_VALUE 43\n"));
                bool analyzer_header_invalidated = analyzer_header_changed && !clang_analyze_context_matches(arena,
                    analyzer_include_plan.units[analyzer_include_representative]);
                clang_analyze_test_check(analyzer_header_invalidated,
                    S8("analyzer-only-include-is-dependency-and-content-rechecked"), &state);

                String8 analyzer_config_file = S8("optin.taint.TaintPropagation:Config=/tmp/taint-rules.yml");
                String8 xanalyzer_config[] = {S8("-Xanalyzer"), S8("-analyzer-config"), S8("-Xanalyzer"), analyzer_config_file};
                String8 xclang_config[] = {S8("-Xclang"), S8("-analyzer-config"), S8("-Xclang"), analyzer_config_file};
                String8 ast_merge_arguments[] = {S8("-Xclang"), S8("-ast-merge"), S8("-Xclang"), S8("/tmp/external.ast")};
                String8 analyzer_config_count = path_join(arena, root, S8("analyzer-config-count.txt"));
                String8 analyzer_config_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/xanalyzer-z.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(xanalyzer_config), analyzer_config_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/xanalyzer-a.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(xanalyzer_config), analyzer_config_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/xclang-z.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(xclang_config), analyzer_config_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/xclang-a.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(xclang_config), analyzer_config_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/ast-merge-z.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(ast_merge_arguments), analyzer_config_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("analyzer-config.c"), S8("Release/ast-merge-a.o"), clang,
                        (SliceString8)BUSTER_ARRAY_TO_SLICE(ast_merge_arguments), analyzer_config_count),
                };
                String8 analyzer_config_text = string_format(arena, S8("[{S8},{S8},{S8},{S8},{S8},{S8}]"), analyzer_config_rows[0],
                    analyzer_config_rows[1], analyzer_config_rows[2], analyzer_config_rows[3], analyzer_config_rows[4], analyzer_config_rows[5]);
                String8 analyzer_config_database = path_join(arena, root, S8("analyzer-config-compile_commands.json"));
                ClangAnalyzeOptions analyzer_config_options = options;
                analyzer_config_options.database = analyzer_config_database;
                ClangAnalyzePlan analyzer_config_plan = {0};
                bool analyzer_config_planned = clang_analyze_write(arena, analyzer_config_database, analyzer_config_text) &&
                    clang_analyze_plan(arena, analyzer_config_options, &analyzer_config_plan);
                bool analyzer_config_fallback = analyzer_config_planned && analyzer_config_plan.count == 6 &&
                    analyzer_config_plan.unique_executions == 6 && analyzer_config_plan.aliased_rows == 0;
                for (u64 i = 0; analyzer_config_fallback && i < analyzer_config_plan.count; i += 1)
                {
                    analyzer_config_fallback = string_equal(analyzer_config_plan.units[i].ineligible_reason,
                                                             S8("plugin-or-mutable-input"));
                }
                clang_analyze_test_check(analyzer_config_fallback,
                    S8("analyzer-config-file-inputs-remain-independent"), &state);
            }
            if (ready)
            {
                String8 absolute_database = path_join(arena, root, S8("absolute-negative-compile_commands.json"));
                String8 absolute_count = path_join(arena, root, S8("absolute-negative-count.txt"));
                String8 absolute_rows[] = {
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("absolute-lookup.c"), S8("Release/absolute-z.o"), clang, (SliceString8){0}, absolute_count),
                    clang_analyze_test_fixture_database_row(arena, context_directory, S8("absolute-lookup.c"), S8("Release/absolute-a.o"), clang, (SliceString8){0}, absolute_count),
                };
                String8 absolute_text = string_format(arena, S8("[{S8},{S8}]"), absolute_rows[0], absolute_rows[1]);
                ClangAnalyzeOptions absolute_options = options;
                absolute_options.database = absolute_database;
                ClangAnalyzePlan absolute_plan = {0};
                bool absolute_planned = !path_exists(arena, absolute_lookup_header) &&
                    clang_analyze_write(arena, absolute_database, absolute_text) && clang_analyze_plan(arena, absolute_options, &absolute_plan);
                u64 absolute_representative = absolute_planned ? absolute_plan.units[0].representative : 0;
                bool absolute_stable = absolute_planned && absolute_plan.aliased_rows == 1 &&
                    absolute_plan.units[absolute_representative].preprocessing_fingerprint.length &&
                    clang_analyze_context_matches(arena, absolute_plan.units[absolute_representative]);
                clang_analyze_test_begin(S8("absolute-negative-lookup-is-bound-by-preprocess-output"), true);
                bool absolute_created = absolute_stable && clang_analyze_write(arena, absolute_lookup_header, S8("/* queried, not included */\n"));
                bool absolute_invalidated = absolute_created &&
                    !clang_analyze_context_matches(arena, absolute_plan.units[absolute_representative]);
                clang_analyze_test_check(absolute_invalidated, S8("absolute-negative-lookup-is-bound-by-preprocess-output"), &state);
                remove_path_recursive(arena, absolute_lookup_header);
            }
#endif
#if BUSTER_LINUX
            if (real_context)
            {
                String8 missing_candidate = path_join(arena, missing_search_directory, S8("buster-analyzer-negative-lookup.h"));
                clang_analyze_test_begin(S8("missing-high-priority-include-root-is-rechecked"), true);
                make_directory_recursive(arena, missing_search_directory);
                bool created = clang_analyze_write(arena, missing_candidate, S8("#define BUSTER_NEGATIVE_LOOKUP 1\n"));
                bool missing_root_invalidated = created &&
                    !clang_analyze_context_matches(arena, real_plan.units[real_plan.units[0].representative]);
                clang_analyze_test_check(missing_root_invalidated, S8("missing-high-priority-include-root-is-rechecked"), &state);
                remove_path_recursive(arena, missing_search_directory);
                String8 added_candidate = path_join(arena, forced_context_directory, S8("forced-next.h"));
                clang_analyze_test_begin(S8("dependency-parent-negative-lookup-is-rechecked"), true);
                bool added = clang_analyze_write(arena, added_candidate, S8("#define FORCED_NEXT_VALUE 1\n"));
                bool invalidated = added && !clang_analyze_context_matches(arena, real_plan.units[real_plan.units[0].representative]);
                clang_analyze_test_check(invalidated, S8("dependency-parent-negative-lookup-is-rechecked"), &state);
                remove_path_recursive(arena, added_candidate);
                String8 symlink_candidate = path_join(arena, context_directory, S8("lookup-link.h"));
                String8 symlink_z = string_duplicate_arena(arena, symlink_candidate, true);
                clang_analyze_test_begin(S8("symlink-target-state-is-rechecked"), true);
                bool linked = symlink("input.h", symlink_z.pointer) == 0;
                ClangAnalyzePlan symlink_plan = {0};
                bool symlink_planned = linked && clang_analyze_plan(arena, options, &symlink_plan);
                u64 symlink_representative = symlink_planned ? symlink_plan.units[symlink_plan.units[0].representative].representative : 0;
                bool stable_link = symlink_planned && clang_analyze_context_matches(arena, symlink_plan.units[symlink_representative]);
                bool retargeted = unlink(symlink_z.pointer) == 0 && symlink("real.c", symlink_z.pointer) == 0;
                bool symlink_invalidated = stable_link && retargeted && !clang_analyze_context_matches(arena, symlink_plan.units[symlink_representative]);
                clang_analyze_test_check(symlink_invalidated, S8("symlink-target-state-is-rechecked"), &state);
                unlink(symlink_z.pointer);
                ClangAnalyzePlan content_plan = {0};
                bool content_planned = clang_analyze_plan(arena, options, &content_plan);
                u64 content_representative = content_planned ? content_plan.units[0].representative : 0;
                clang_analyze_test_begin(S8("in-place-transitive-header-content-is-rechecked"), true);
                bool changed = content_planned && clang_analyze_write(arena, path_join(arena, context_directory, S8("input.h")), S8("#define VALUE 43\n"));
                bool content_invalidated = changed && !clang_analyze_context_matches(arena, content_plan.units[content_representative]);
                clang_analyze_test_check(content_invalidated, S8("in-place-transitive-header-content-is-rechecked"), &state);
                clang_analyze_write(arena, path_join(arena, context_directory, S8("input.h")), S8("#define VALUE 42\n"));
            }
#endif
            clang_analyze_test_begin(S8("real-clang-command"), false);
            clang_analyze_test_check(real_context && clang_analyze_run(arena, options), S8("real-clang-command"), &state);
            header = S8("#define VALUE (*(int*)0)\n");
            options.results = path_join(arena, root, S8("changed-header"));
            bool header_written = clang_analyze_write(arena, path_join(arena, context_directory, S8("input.h")), header);
            clang_analyze_test_begin(S8("transitive-header-is-reanalyzed"), true);
            clang_analyze_test_check(header_written && !clang_analyze_run(arena, options), S8("transitive-header-is-reanalyzed"), &state);
        }
    }
    // The self-test's own verdict, distinct from every nested ANALYZE_RUN or
    // ANALYZE_AGGREGATE record. Passing expected rejections are not failures.
    bool success = ready && state.failures == 0;
    string_print(S8("ANALYZE_SELF_TEST_RESULT checks={u64} expected_rejections={u64} failures={u64} evidence={S8} status={S8}\n"),
                 state.checks, state.expected_rejections, state.failures, root, success ? S8("pass") : S8("fail"));
    return success;
}
