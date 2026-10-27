// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Immutable-result cache and a bounded, isolated Git preflight. A bad fork blocks its own pair, not the pool.
#include <dirent.h>
#include <sys/stat.h>

#define BUSTER_CI_GIT_PAIR_MS 30000
#define BUSTER_CI_GIT_TOTAL_MS 120000
#define BUSTER_CI_GIT_STORE_BYTES (UINT64_C(64) * 1024 * 1024)
#define BUSTER_CI_GIT_FILE_BYTES (UINT64_C(16) * 1024 * 1024)
#define BUSTER_CI_GIT_FILES 8192
#define BUSTER_CI_GIT_RETRY_SECONDS 300

typedef struct CiGitComputed CiGitComputed;
struct CiGitComputed { int clean; bool code; char merge[BUSTER_CI_SHA]; };
static CiState* ci_current_state;
static uint64_t ci_git_used_ms;
static unsigned ci_git_computed, ci_git_cached, ci_git_deferred;
static char ci_git_engine[96];
// Fault injection is reachable only through the original in-process self-tests, never an argument/environment.
static bool ci_git_test_stall;
static bool ci_git_test_large_pack;

static uint64_t ci_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) ci_die("monotonic clock unavailable");
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static bool ci_git_store_size(const char* path, unsigned depth, uint64_t* bytes, unsigned* files)
{
    if (depth > 8) return false;
    DIR* directory = opendir(path);
    if (directory == NULL) return errno == ENOENT;
    bool valid = true;
    struct dirent* entry;
    while (valid && (entry = readdir(directory)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[1024];
        int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat metadata;
        if (length < 0 || (size_t)length >= sizeof(child)) { valid = false; break; }
        if (lstat(child, &metadata) != 0) { valid = errno == ENOENT; continue; }
        if (++*files > BUSTER_CI_GIT_FILES) { valid = false; break; }
        if (S_ISDIR(metadata.st_mode)) valid = ci_git_store_size(child, depth + 1, bytes, files);
        else if (S_ISREG(metadata.st_mode) && metadata.st_size >= 0)
        {
            *bytes += (uint64_t)metadata.st_size;
            valid = *bytes <= BUSTER_CI_GIT_STORE_BYTES;
        }
        else valid = false; // The object-only store has no checkout symlinks or special files.
    }
    closedir(directory);
    return valid;
}

static bool ci_git_store_ok(void)
{
    uint64_t bytes = 0;
    unsigned files = 0;
    return ci_git_store_size(ci_git.directory, 0, &bytes, &files);
}

static bool ci_git_bounded(const char* base, const char* head, CiGitComputed* computed, uint64_t allowed_ms, const char* pack_path)
{
    ci_git_init(&ci_git, true);
    FILE* result = tmpfile();
    if (result == NULL) ci_die("cannot prepare bounded preflight result");
    uint64_t started = ci_monotonic_ms();
    pid_t child = fork();
    if (child < 0) ci_die("cannot start bounded preflight");
    if (child == 0)
    {
        if (setpgid(0, 0) != 0) _exit(127);
        ci_git_is_worker = true;
        struct rlimit file = {BUSTER_CI_GIT_FILE_BYTES, BUSTER_CI_GIT_FILE_BYTES};
        struct rlimit cpu = {15, 15};
        struct rlimit core = {0, 0};
        if (setrlimit(RLIMIT_FSIZE, &file) != 0 || setrlimit(RLIMIT_CPU, &cpu) != 0 || setrlimit(RLIMIT_CORE, &core) != 0) _exit(127);
        if (ci_git_test_stall) for (;;) pause();
        if (ci_git_test_large_pack)
        {
            char path[300], bytes[65536] = {0};
            snprintf(path, sizeof(path), "%s/oversized-test-pack", ci_git.directory);
            FILE* oversized = fopen(path, "wb");
            if (oversized == NULL) _exit(127);
            for (unsigned i = 0; i < BUSTER_CI_GIT_FILE_BYTES / sizeof(bytes) + 2; ++i)
                if (fwrite(bytes, 1, sizeof(bytes), oversized) != sizeof(bytes)) _exit(2);
            if (fclose(oversized) != 0) _exit(2);
            _exit(127);
        }
        CiGitComputed value = {0};
        value.clean = ci_git_merge(base, head, value.merge);
        value.code = !value.clean || ci_git_code_in(&ci_git, base, value.merge);
        if (value.clean && pack_path != NULL)
        {
            CiLease lease = {.pr = 1};
            ci_copy(lease.base, sizeof(lease.base), base); ci_copy(lease.head, sizeof(lease.head), head);
            ci_copy(lease.merge, sizeof(lease.merge), value.merge);
            Buffer pack = ci_git_pack_payload(&ci_git, &lease);
            FILE* packed = fopen(pack_path, "wb");
            if (packed == NULL || fwrite(pack.data, 1, pack.length, packed) != pack.length || fclose(packed) != 0) _exit(127);
            free(pack.data);
        }
        if (fwrite(&value, 1, sizeof(value), result) != sizeof(value) || fflush(result) != 0) _exit(127);
        _exit(0);
    }
    // Every Git/helper descendant stays in this worker's process group. Timeout/storage failure kills the group.
    (void)setpgid(child, child);
    int status = 0;
    bool bounded = true;
    for (;;)
    {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) { bounded = false; break; }
        if (ci_monotonic_ms() - started >= allowed_ms || !ci_git_store_ok())
        {
            bounded = false;
            (void)kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        struct timespec delay = {.tv_nsec = 20000000};
        (void)nanosleep(&delay, NULL);
    }
    (void)kill(-child, SIGKILL); // Also reap any helper left after a failed worker.
    ci_git_used_ms += ci_monotonic_ms() - started;
    bool success = bounded && WIFEXITED(status) && WEXITSTATUS(status) == 0 && ci_git_store_ok();
    if (success)
    {
        rewind(result);
        success = fread(computed, 1, sizeof(*computed), result) == sizeof(*computed) &&
                  (computed->clean == 0 || computed->clean == 1) &&
                  (computed->clean ? ci_sha(computed->merge) : *computed->merge == 0);
    }
    fclose(result);
    if (!success) ci_git_dispose(&ci_git); // Discard partial packs/locks before another PR's independent preflight.
    return success;
}

static const char* ci_git_engine_name(void)
{
    if (*ci_git_engine == 0)
    {
        const char* version[] = {"--version", NULL};
        Buffer output = ci_git_ok(NULL, version, NULL, 0);
        if (output.length == 0 || output.length > 80 || output.data[output.length - 1] != '\n') ci_die("unsupported Git engine identity");
        output.data[--output.length] = 0;
        snprintf(ci_git_engine, sizeof(ci_git_engine), "v1/%s", output.data);
        free(output.data);
    }
    return ci_git_engine;
}

static void ci_preflight_identity(CiPr* pr, CiIdentity* current, uint64_t now)
{
    if (!current->open || current->mergeable < 0) return;
    const char* engine = ci_git_engine_name();
    bool same = strcmp(pr->head, current->head) == 0 && strcmp(pr->base, current->base) == 0;
    if (!same) { current->mergeable = -1; return; } // A revalidation observed a new pair; the next reconcile owns it.
    if (strcmp(pr->frozen_engine, engine) != 0)
    {
        pr->frozen_state = 0;
        pr->frozen_retry_at = 0;
        pr->frozen_merge[0] = 0;
        pr->outcome = 0;
        ci_copy(pr->frozen_engine, sizeof(pr->frozen_engine), engine);
    }
    bool clean_cache = pr->frozen_state == 1 && ci_sha(pr->frozen_merge) && strcmp(pr->code_merge, pr->frozen_merge) == 0;
    if (clean_cache || pr->frozen_state == 2)
    {
        ++ci_git_cached;
        current->mergeable = clean_cache ? 1 : 0;
        ci_copy(current->merge, sizeof(current->merge), clean_cache ? pr->frozen_merge : "");
        current->code = pr->cached_code;
        current->frozen = true;
        return;
    }
    if ((pr->frozen_state == 3 && now < pr->frozen_retry_at) || ci_git_used_ms >= BUSTER_CI_GIT_TOTAL_MS)
    {
        ++ci_git_deferred;
        current->mergeable = -1;
        current->preflight_blocked = true;
        current->merge[0] = 0;
        return;
    }
    uint64_t remaining = BUSTER_CI_GIT_TOTAL_MS - ci_git_used_ms;
    uint64_t budget = remaining < BUSTER_CI_GIT_PAIR_MS ? remaining : BUSTER_CI_GIT_PAIR_MS;
    CiGitComputed result = {0};
    ++ci_git_computed;
    if (!ci_git_bounded(current->base, current->head, &result, budget, NULL))
    {
        pr->frozen_state = 3;
        pr->frozen_retry_at = now + BUSTER_CI_GIT_RETRY_SECONDS;
        pr->frozen_merge[0] = 0;
        current->mergeable = -1;
        current->preflight_blocked = true;
        current->merge[0] = 0;
        ++ci_git_deferred;
        return;
    }
    pr->frozen_state = result.clean ? 1 : 2;
    pr->frozen_retry_at = 0;
    ci_copy(pr->frozen_merge, sizeof(pr->frozen_merge), result.merge);
    ci_copy(pr->code_merge, sizeof(pr->code_merge), result.merge);
    pr->cached_code = result.code;
    current->mergeable = result.clean;
    ci_copy(current->merge, sizeof(current->merge), result.merge);
    current->code = result.code;
    current->frozen = true;
}
