/* Research-only #83 probe. Build in the pinned repository; no compiler/UI.
 * Production source is included unchanged. Macros below observe existing API
 * boundaries; --wrap=close independently observes native descriptor release.
 * The byte/presence/identity oracle uses native calls, never Buster file reads.
 */
#define _GNU_SOURCE 1
#include <buster/lib/system_headers.h>
#include <buster/lib/os.h>
#include <buster/lib/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/wait.h>

BUSTER_V_IMPL OsState os_state;
static ProgramState probe_program;
BUSTER_V_IMPL ProgramState* program_state = &probe_program;
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/string.c>
#include <buster/lib/os.c>

#ifndef PROBE_DROP_CLOSE_ERROR
#define PROBE_DROP_CLOSE_ERROR 0
#endif

typedef struct ProbeTrace ProbeTrace;
struct ProbeTrace
{
    unsigned acquired, closes, native_closes, flushes, replaces, deletes;
    unsigned committed, scratch_ends;
    u64 transferred;
    int closed_fd;
    char stage[4096];
};
static ProbeTrace trace;
static unsigned assertions, failures;
static bool observing_close;
#define CHECK(c) do { assertions += 1; if (!(c)) { failures += 1; fprintf(stderr, "FAIL line=%d expression=%s\n", __LINE__, #c); } } while (0)
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "SETUP line=%d expression=%s errno=%d\n", __LINE__, #c, errno); exit(2); } } while (0)

int __real_close(int descriptor);
int __wrap_close(int descriptor)
{
    if (observing_close)
    {
        trace.native_closes += 1;
        trace.closed_fd = descriptor;
    }
    return __real_close(descriptor);
}

static OsFileStagingResult observed_staging(Arena* arena, String8 path, OpenPermissions permissions)
{
    OsFileStagingResult result = os_file_staging_create(arena, path, permissions);
    if (result.file)
    {
        trace.acquired += 1;
        REQUIRE(result.path.length < sizeof(trace.stage));
        memcpy(trace.stage, result.path.pointer, result.path.length);
        trace.stage[result.path.length] = 0;
    }
    return result;
}
static OsFileTransferResult observed_write(OsFileDescriptor* file, ByteSlice bytes)
{
    OsFileTransferResult result = os_file_write_checked(file, bytes);
    trace.transferred += result.transferred;
    return result;
}
static OsError observed_flush(OsFileDescriptor* file)
{
    trace.flushes += 1;
    return os_file_flush(file);
}
static OsError observed_close(OsFileDescriptor* file)
{
    trace.closes += 1;
    observing_close = true;
    OsError result = os_file_close_checked(file);
    observing_close = false;
    if (PROBE_DROP_CLOSE_ERROR) result.v = 0; /* deliberate negative control */
    return result;
}
static OsError observed_replace(String8 source, String8 destination)
{
    trace.replaces += 1;
    CHECK(trace.closes == 1 && trace.native_closes == 1 && trace.flushes == 1);
    OsError result = os_file_replace(source, destination);
    trace.committed += result.v == 0;
    return result;
}
static OsError observed_delete(String8 path)
{
    trace.deletes += 1;
    return os_file_delete_checked(path);
}
static void observed_scratch_end(TemporalArena scratch)
{
    trace.scratch_ends += 1;
    scratch_end(scratch);
}

#define os_file_staging_create observed_staging
#define os_file_write_checked observed_write
#define os_file_flush observed_flush
#define os_file_close_checked observed_close
#define os_file_replace observed_replace
#define os_file_delete_checked observed_delete
#define scratch_end observed_scratch_end
#include <buster/lib/file.c>
#undef os_file_staging_create
#undef os_file_write_checked
#undef os_file_flush
#undef os_file_close_checked
#undef os_file_replace
#undef os_file_delete_checked
#undef scratch_end
#include <buster/lib/hash.c>
#include <buster/lib/time.c>

static String8 text_slice(const char* text)
{
    return (String8){.pointer = (char8*)text, .length = strlen(text)};
}
static unsigned native_count(const char* path)
{
    DIR* directory = opendir(path);
    REQUIRE(directory != 0);
    unsigned count = 0;
    errno = 0;
    struct dirent* entry = readdir(directory);
    while (entry)
    {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) count += 1;
        REQUIRE(count < 4096);
        errno = 0;
        entry = readdir(directory);
    }
    REQUIRE(errno == 0 && closedir(directory) == 0);
    return count;
}
static bool native_bytes(const char* path, const u8* bytes, size_t length)
{
    int descriptor = open(path, O_RDONLY);
    bool result = descriptor >= 0;
    if (result)
    {
        u8 buffer[258];
        ssize_t count = read(descriptor, buffer, sizeof(buffer));
        result = count >= 0 && (size_t)count == length && !memcmp(buffer, bytes, length);
        result = close(descriptor) == 0 && result;
    }
    return result;
}
static void seed(const char* path, const u8* bytes, size_t length)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    REQUIRE(descriptor >= 0);
    REQUIRE(write(descriptor, bytes, length) == (ssize_t)length);
    REQUIRE(close(descriptor) == 0);
}
static bool missing(const char* path)
{
    struct stat info;
    return lstat(path, &info) < 0 && errno == ENOENT;
}
static FilePublishResult publish(const char* path, u8* bytes, size_t length)
{
    trace = (ProbeTrace){.closed_fd = -1};
    return file_publish_checked(text_slice(path), (ByteSlice){bytes, length}, (OpenPermissions){.read = 1, .write = 1});
}
static bool held_bytes(int descriptor, const u8* expected, size_t length)
{
    u8 buffer[258];
    ssize_t count = pread(descriptor, buffer, sizeof(buffer), 0);
    return count >= 0 && (size_t)count == length && !memcmp(buffer, expected, length);
}

typedef struct ProbeCase ProbeCase;
struct ProbeCase
{
    const char* name;
    OsFileTestStep steps[3];
    unsigned count, error, cleanup, acquired, flushes, replaces, leftover;
    bool success, existing_only;
};
static const ProbeCase cases[] = {
    {.name = "stats", .steps = {{OS_FILE_TEST_STATS, OS_FILE_TEST_ERROR, 12345}}, .count = 1, .error = 12345},
    {.name = "open", .steps = {{OS_FILE_TEST_OPEN, OS_FILE_TEST_ERROR, 12345}}, .count = 1, .error = 12345},
    {.name = "permissions", .steps = {{OS_FILE_TEST_PERMISSIONS, OS_FILE_TEST_ERROR, 12345}}, .count = 1, .error = 12345, .acquired = 1, .existing_only = true},
    {.name = "prefix-write", .steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_LIMIT, 3}, {OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}}, .count = 2, .error = 12345, .acquired = 1},
    {.name = "flush", .steps = {{OS_FILE_TEST_FLUSH, OS_FILE_TEST_ERROR, 12345}}, .count = 1, .error = 12345, .acquired = 1, .flushes = 1},
    {.name = "close", .steps = {{OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}}, .count = 1, .error = 23456, .acquired = 1, .flushes = 1},
    {.name = "replace", .steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}}, .count = 1, .error = 12345, .acquired = 1, .flushes = 1, .replaces = 1},
    {.name = "replace-delete", .steps = {{OS_FILE_TEST_REPLACE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 34567}}, .count = 2, .error = 12345, .cleanup = 34567, .acquired = 1, .flushes = 1, .replaces = 1, .leftover = 1},
    {.name = "write-close-delete", .steps = {{OS_FILE_TEST_WRITE, OS_FILE_TEST_ERROR, 12345}, {OS_FILE_TEST_CLOSE, OS_FILE_TEST_ERROR, 23456}, {OS_FILE_TEST_DELETE, OS_FILE_TEST_ERROR, 34567}}, .count = 3, .error = 12345, .cleanup = 23456, .acquired = 1, .leftover = 1},
    {.name = "success", .acquired = 1, .flushes = 1, .replaces = 1, .success = true},
};

static void trial(const char* root, unsigned index, bool existing)
{
    const ProbeCase* test = &cases[index];
    char directory[1024], destination[2048], guard_path[2048], unrelated[2048];
    REQUIRE(snprintf(directory, sizeof(directory), "%s/%u-%u", root, index, existing) > 0);
    REQUIRE(snprintf(destination, sizeof(destination), "%s/artifact", directory) > 0);
    REQUIRE(snprintf(guard_path, sizeof(guard_path), "%s/guard", directory) > 0);
    REQUIRE(snprintf(unrelated, sizeof(unrelated), "%s/unrelated", directory) > 0);
    REQUIRE(mkdir(directory, 0700) == 0);
    static const u8 old[] = {'o', 'l', 'd', 0, 0xff};
    static const u8 guard_bytes[] = {'g', 'u', 'a', 'r', 'd'};
    u8 proposed[257], saved[257];
    for (unsigned i = 0; i < sizeof(proposed); i += 1) proposed[i] = (u8)(i * 37u);
    memcpy(saved, proposed, sizeof(saved));
    char saved_path[2048];
    memcpy(saved_path, destination, strlen(destination) + 1);
    seed(guard_path, guard_bytes, sizeof(guard_bytes));
    if (existing) seed(destination, old, sizeof(old));
    int guard = open(guard_path, O_RDONLY);
    int held = existing ? open(destination, O_RDONLY) : -1;
    REQUIRE(guard >= 0 && (!existing || held >= 0));
    unsigned descriptors = native_count("/proc/self/fd");
    TemporalArena outer = scratch_begin(0, 0);
    u8* canary = arena_allocate(outer.arena, u8, 32);
    memset(canary, 0xa7, 32);
    u64 mark = outer.arena->position;
    unsigned before_failures = failures;
    os_file_test_begin(text_slice(destination), test->steps, test->count);
    FilePublishResult result = publish(destination, proposed, sizeof(proposed));
    unsigned consumed = os_file_test_end();
    ProbeTrace first = trace;
    CHECK(consumed == test->count);
    CHECK(result.status == (test->success ? FILE_PUBLISH_PUBLISHED : FILE_PUBLISH_FAILED));
    CHECK(result.error.v == test->error && result.cleanup_error.v == test->cleanup);
    CHECK(first.acquired == test->acquired && first.closes == test->acquired);
    CHECK(first.native_closes == test->acquired);
    CHECK(first.flushes == test->flushes && first.replaces == test->replaces);
    CHECK(first.deletes == (test->acquired && !test->success));
    CHECK(first.committed == (unsigned)test->success && first.scratch_ends == 1);
    if (first.closed_fd >= 0) CHECK(fcntl(first.closed_fd, F_GETFD) < 0 && errno == EBADF);
    CHECK(native_count("/proc/self/fd") == descriptors);
    CHECK(outer.arena->position == mark);
    CHECK(!memcmp(saved, proposed, sizeof(saved)) && !strcmp(saved_path, destination));
    for (unsigned i = 0; i < 32; i += 1) CHECK(canary[i] == 0xa7);
    CHECK(held_bytes(guard, guard_bytes, sizeof(guard_bytes)));
    if (existing) CHECK(held_bytes(held, old, sizeof(old)));
    CHECK(test->success ? native_bytes(destination, proposed, sizeof(proposed)) :
          (existing ? native_bytes(destination, old, sizeof(old)) : missing(destination)));
    unsigned present = existing || test->success;
    CHECK(native_count(directory) == 1 + present + test->leftover);
    struct stat orphan = {0};
    if (test->leftover) REQUIRE(lstat(first.stage, &orphan) == 0 && S_ISREG(orphan.st_mode));
    else if (first.acquired) CHECK(missing(first.stage));

    /* No fixture or context reset. Disable only the explicit fault script;
     * test a distinct operation before the caller deliberately retries. */
    result = publish(unrelated, proposed, sizeof(proposed));
    CHECK(result.status == FILE_PUBLISH_PUBLISHED && !result.error.v && !result.cleanup_error.v);
    CHECK(native_bytes(unrelated, proposed, sizeof(proposed)));
    CHECK(native_count("/proc/self/fd") == descriptors && outer.arena->position == mark);
    result = publish(destination, proposed, sizeof(proposed));
    CHECK(result.status == FILE_PUBLISH_PUBLISHED && !result.error.v && !result.cleanup_error.v);
    CHECK(native_bytes(destination, proposed, sizeof(proposed)));
    CHECK(native_count("/proc/self/fd") == descriptors && outer.arena->position == mark);
    CHECK(held_bytes(guard, guard_bytes, sizeof(guard_bytes)));
    if (existing) CHECK(held_bytes(held, old, sizeof(old)));
    CHECK(!memcmp(saved, proposed, sizeof(saved)));
    for (unsigned i = 0; i < 32; i += 1) CHECK(canary[i] == 0xa7);
    CHECK(native_count(directory) == 3 + test->leftover);
    if (test->leftover)
    {
        struct stat after;
        REQUIRE(lstat(first.stage, &after) == 0);
        CHECK(after.st_dev == orphan.st_dev && after.st_ino == orphan.st_ino && after.st_size == orphan.st_size);
    }
    printf("CASE name=%s existing=%u consumed=%u acquired=%u close=%u native_close=%u flush=%u replace=%u delete=%u committed=%u transferred=%llu orphan=%u assertions_failed=%u\n",
           test->name, existing, consumed, first.acquired, first.closes, first.native_closes, first.flushes,
           first.replaces, first.deletes, first.committed, (unsigned long long)first.transferred,
           test->leftover, failures - before_failures);
    /* Cleanup only names owned in this exclusively created fixture, after
     * observing the failed/reused states. No broad directory cleanup. */
    REQUIRE(close(guard) == 0);
    if (existing) REQUIRE(close(held) == 0);
    REQUIRE(unlink(destination) == 0 && unlink(unrelated) == 0 && unlink(guard_path) == 0);
    if (test->leftover) REQUIRE(unlink(first.stage) == 0);
    REQUIRE(rmdir(directory) == 0);
    scratch_end(outer);
}

static void allocation_child(const char* root)
{
    char destination[4096];
    REQUIRE(snprintf(destination, sizeof(destination), "%s/oom-artifact", root) > 0);
    u8 old[] = {'o', 'l', 'd'}, proposed[] = {'n', 'e', 'w'};
    seed(destination, old, sizeof(old));
    int errors[2];
    REQUIRE(pipe(errors) == 0);
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child)
    {
        REQUIRE(close(errors[0]) == 0 && dup2(errors[1], STDERR_FILENO) >= 0 && close(errors[1]) == 0);
        TemporalArena scratch = scratch_begin(0, 0);
        arena_set_position(scratch.arena, scratch.arena->os_position);
        arena_test_fail_next_commit();
        FilePublishResult returned = publish(destination, proposed, sizeof(proposed));
        (void)returned;
        _exit(42); /* Returning is not the existing allocator contract. */
    }
    REQUIRE(close(errors[1]) == 0);
    char diagnostic[4096];
    size_t used = 0;
    ssize_t amount = read(errors[0], diagnostic, sizeof(diagnostic) - 1);
    while (amount > 0)
    {
        used += (size_t)amount;
        REQUIRE(used < sizeof(diagnostic) - 1);
        amount = read(errors[0], diagnostic + used, sizeof(diagnostic) - 1 - used);
    }
    REQUIRE(amount == 0 && close(errors[0]) == 0);
    diagnostic[used] = 0;
    int status;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK((WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 42)) &&
          strstr(diagnostic, "arena commit failed") != 0);
    CHECK(native_bytes(destination, old, sizeof(old)) && native_count(root) == 1);
    printf("ALLOCATION child_status=%d returned=%u old_preserved=1 diagnostic=%s\n", status,
           WIFEXITED(status) && WEXITSTATUS(status) == 42, diagnostic);
    FilePublishResult result = publish(destination, proposed, sizeof(proposed));
    CHECK(result.status == FILE_PUBLISH_PUBLISHED && native_bytes(destination, proposed, sizeof(proposed)));
    REQUIRE(unlink(destination) == 0);
}

int main(int argc, char** argv)
{
    bool close_only = argc == 2 && !strcmp(argv[1], "close-only");
    bool skip_oom = close_only || (argc == 2 && !strcmp(argv[1], "skip-oom"));
    REQUIRE(argc == 1 || close_only || skip_oom);
    setvbuf(stdout, 0, _IONBF, 0);
    os_state.page_size = (u64)sysconf(_SC_PAGESIZE);
    os_state.allocation_granularity = os_state.page_size;
    os_state.logical_thread_count = 1;
    REQUIRE(pthread_mutex_init(&os_state.entity_mutex, 0) == 0);
    os_state.entity_arena = arena_create((ArenaCreation){0});
    probe_program.arena = arena_create((ArenaCreation){0});
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    unsigned initial_descriptors = native_count("/proc/self/fd");
    char root[] = "/tmp/buster-file-atomicity-XXXXXX";
    REQUIRE(mkdtemp(root) != 0);
    unsigned trials = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1)
    {
        for (unsigned existing = 0; existing < 2; existing += 1)
        {
            if ((!cases[i].existing_only || existing) && (!close_only || !strcmp(cases[i].name, "close")))
            {
                trial(root, i, existing != 0);
                trials += 1;
            }
        }
    }
    if (!skip_oom) allocation_child(root);
    CHECK(native_count(root) == 0 && native_count("/proc/self/fd") == initial_descriptors);
    REQUIRE(rmdir(root) == 0);
    thread_context_release(context);
    thread_context_select(0);
    CHECK(arena_destroy(probe_program.arena, 1));
    CHECK(arena_destroy(os_state.entity_arena, 1));
    arena_pool_release_thread();
    REQUIRE(pthread_mutex_destroy(&os_state.entity_mutex) == 0);
    printf("RESULT trials=%u assertions=%u failures=%u mutant=%u\n", trials, assertions, failures, PROBE_DROP_CLOSE_ERROR);
    return failures ? 1 : 0;
}
