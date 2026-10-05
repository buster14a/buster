/* Fixed native-execute-v1 trusted coordinator and candidate helper. The outer
 * coordinates one broker stage and seals artifacts; only the candidate helper
 * opens/hash-checks/fexecves the uploaded ET_EXEC. Inputs are immutable, argv
 * and environment fixed. The stage's status is an execution outcome, never a
 * benchmark statistic or a claim about exact payload signals. */
#ifdef __linux__
#include <pwd.h>

BUSTER_GLOBAL_LOCAL BqError bq_native_execute(char const* source, char const* identity, uid_t owner)
{
    int executable = bq_native_executable(source, identity, owner);
    if (executable >= 0)
    {
        char* const arguments[] = {"program", NULL};
        char* const environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
        fexecve(executable, arguments, environment);
        close(executable);
    }
    return BQ_CONFIGURATION_MISMATCH;
}

/* Production account boundary: the lower descriptor verifier is shared with
 * disposable fixture tests; no client operation selects an owner identity. */
BUSTER_GLOBAL_LOCAL BqError bq_native_payload(char const* source, char const* identity)
{
    struct passwd* service = getpwnam("buster-bench");
    uid_t owner = service ? service->pw_uid : (uid_t)-1;
    struct passwd* candidate = getpwnam("buster-bench-candidate");
    bool allowed = candidate && candidate->pw_uid != 0 && candidate->pw_uid != owner &&
                   getuid() == candidate->pw_uid && geteuid() == candidate->pw_uid &&
                   getgid() == candidate->pw_gid && getegid() == candidate->pw_gid;
    BqError error = allowed ? bq_native_execute(source, identity, owner) : BQ_CONFIGURATION_MISMATCH;
    return error;
}

/* Only a trusted coordinator calls this fixed publisher. Keeping the real
 * publication path shared with disposable fixtures tests row validation,
 * bundle ordering and manifests without configuring production NSS accounts. */
BUSTER_GLOBAL_LOCAL BqError bq_native_result_publish(BqWorkerFinalization* finalization, BqRecipe recipe,
    char const* job_id, char const* attempt_token, char const* workspace, char const* base,
    char const* candidate, char const* result, int stage_status, bool truncated, BqError error)
{
    bool runtime = recipe == BQ_RECIPE_NATIVE_RUNTIME;
    char const* stage_name = runtime ? BQ_RUNTIME_STAGE_NAME : BQ_NATIVE_STAGE_NAME;
    char manifest[BQ_WORKER_RESULT_CAP];
    char bundle[65] = {0}, recursive[65] = {0};
    if (error == BQ_OK && !runtime) error = bq_worker_failure_bundle_publish(finalization, bundle, recursive);
    bool records_valid = true;
    char sample_digest[65] = {0};
    BqRuntimeSummary summary = {0};
    if (runtime && error == BQ_OK)
    {
        char records[BQ_RUNTIME_RECORD_CAP + 1];
        u32 record_length = 0;
        records_valid = stage_status == 0 && bq_worker_result_control_read(finalization->result_directory, "native-stage.log",
                                records, BQ_RUNTIME_RECORD_CAP, &record_length) &&
                        bq_native_runtime_records(records, record_length, candidate, 2, true, &summary);
        if (records_valid) bq_native_hash(records, record_length, sample_digest);
        if (records_valid) error = bq_worker_result_control_publish(finalization, "runtime-samples.txt",
                                                                       records, record_length, 0400);
        /* The sealed bundle must include the validated original rows. */
        if (error == BQ_OK) error = bq_worker_failure_bundle_publish(finalization, bundle, recursive);
    }
    char summary_lines[256] = {0};
    if (runtime && records_valid)
    {
        int summary_length = snprintf(summary_lines, sizeof(summary_lines),
            "median-wall-ns=%" PRIu64 "\nminimum-wall-ns=%" PRIu64 "\nmaximum-wall-ns=%" PRIu64
            "\nmedian-cpu-ns=%" PRIu64 "\nmaximum-rss-bytes=%" PRIu64 "\n",
            (uint64_t)summary.median_wall_ns, (uint64_t)summary.minimum_wall_ns, (uint64_t)summary.maximum_wall_ns,
            (uint64_t)summary.median_cpu_ns, (uint64_t)summary.maximum_rss_bytes);
        if (summary_length <= 0 || (u32)summary_length >= sizeof(summary_lines)) error = BQ_IO;
    }
    bool success = stage_status == 0 && records_valid;
    int length = error == BQ_OK ? snprintf(manifest, sizeof(manifest),
        "schema=1\nrecipe=%s\nstatus=%s\nstage=%s\nprocess-result=%s\n"
        "job-id=%s\nattempt-token=%s\nworkspace-root=%s\nresult-root=%s\nbase-revision=%s\ncandidate-revision=%s\n"
        "program-manifest-sha256=%s\noperation=%s\nstage-exit-status=%d\nlog-truncated=%s\n"
        "compilation=unavailable\nbenchmark-metrics=%s\n%s%ssample-records-sha256=%s\nbundle-sha256=%s\n",
        finalization->recipe.name, success ? "succeeded" : "failed", stage_name, success ? "success" : "stage-failed", job_id, attempt_token,
        workspace, result, base, candidate, candidate, runtime ? "runtime-samples" : "execute-once", stage_status,
        truncated ? "true" : "false", runtime && records_valid ? "process-latency-wall-cpu-rss-wait4" : "unavailable",
        runtime ? records_valid ? "qualification=diagnostic-only\nwarmups=2\nsamples=9\nsample-records=runtime-samples.txt\noracle=exit-zero-unchecked-transcript\n" :
            "qualification=diagnostic-only\nrequested-warmups=2\nrequested-samples=9\nsample-records=unavailable\noracle=exit-zero-unchecked-transcript\n" : "", summary_lines, runtime && records_valid ? sample_digest : "unavailable", bundle) : -1;
    if (error == BQ_OK && (length <= 0 || (u32)length >= sizeof(manifest))) error = BQ_IO;
    if (error == BQ_OK) error = bq_worker_result_control_publish(finalization, finalization->recipe.manifest,
                                                               manifest, (u64)length, 0400);
    if (error == BQ_OK && !success) error = BQ_WORKER_FAILED;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_native_driver(BqRecipe recipe, char const* job_id, char const* attempt_token, char const* workspace,
                                             char const* base, char const* candidate, char const* result)
{
    IntegerParsingU64 job = string8_parse_u64_decimal(string_from_pointer(job_id));
    IntegerParsingU64 token = string8_parse_u64_decimal(string_from_pointer(attempt_token));
    struct passwd* service = getpwnam("buster-bench");
    bool runtime = recipe == BQ_RECIPE_NATIVE_RUNTIME;
    char const* stage_name = runtime ? BQ_RUNTIME_STAGE_NAME : BQ_NATIVE_STAGE_NAME;
    bool identity = (recipe == BQ_RECIPE_NATIVE_EXECUTE || runtime) && service && service->pw_uid != 0 && getuid() == service->pw_uid && geteuid() == service->pw_uid;
    char name[64], attempt[BQ_PATH_CAP + 1], expected[BQ_PATH_CAP + 1];
    bool ok = identity && job.status == INTEGER_PARSING_SUCCESS && job.length == strlen(job_id) && job.value &&
              token.status == INTEGER_PARSING_SUCCESS && token.length == strlen(attempt_token) && token.value &&
              strlen(base) == 64 && bq_native_hex((u8 const*)base) && !strcmp(base, candidate) &&
              bq_workspace_name(name, job.value, token.value);
    int attempt_length = ok ? snprintf(attempt, sizeof(attempt), "%s/%s", workspace, name) : -1;
    int result_length = ok ? snprintf(expected, sizeof(expected), "%s/results/%s", workspace, name) : -1;
    ok = ok && attempt_length > 0 && (u32)attempt_length < sizeof(attempt) && result_length > 0 &&
         (u32)result_length < sizeof(expected) && !strcmp(expected, result);
    int directory = ok ? bq_open_absolute_directory(string_from_pointer(attempt)) : -1;
    bool created = false;
    int scratch = directory >= 0 ? bq_create_inherited_group_directory(directory, "native-scratch", 02770, &created) : -1;
    ok = ok && directory >= 0 && scratch >= 0 && created && fsync(directory) == 0;
    if (scratch >= 0) close(scratch);
    if (directory >= 0) close(directory);
    BqWorkerFinalization finalization = {.result_directory = -1};
    if (ok)
    {
        finalization.result_directory = bq_worker_open_trusted_directory(string_from_pointer(result), true, false);
        struct stat info = {0};
        ok = finalization.result_directory >= 0 && fstat(finalization.result_directory, &info) == 0 &&
             bq_recipe_files(recipe, &finalization.recipe);
        if (ok)
        {
            finalization.result_device = (u64)info.st_dev;
            finalization.result_inode = (u64)info.st_ino;
            memcpy(finalization.result_root, result, strlen(result) + 1);
        }
    }
    int log = ok ? openat(finalization.result_directory, "native-stage.log", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    int pipes[2] = {-1, -1};
    ok = ok && log >= 0 && pipe2(pipes, O_CLOEXEC) == 0;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        bool ready = dup2(pipes[1], STDOUT_FILENO) >= 0 && dup2(pipes[1], STDERR_FILENO) >= 0;
        close(pipes[0]); close(pipes[1]);
        char* const arguments[] = {BQ_SYSTEMD_BROKER, "start-stage", (char*)job_id, (char*)attempt_token,
                                   (char*)stage_name, (char*)base, (char*)candidate, NULL};
        if (ready) execv(BQ_SYSTEMD_BROKER, arguments);
        _exit(126);
    }
    if (pipes[1] >= 0) close(pipes[1]);
    ok = ok && child > 0;
    u64 captured = 0;
    bool truncated = false;
    u8 bytes[4096];
    for (bool reading = ok; reading;)
    {
        ssize_t amount = read(pipes[0], bytes, sizeof(bytes));
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) { reading = false; if (amount < 0) ok = false; }
        else
        {
            u64 retain = (u64)amount < BQ_NATIVE_LOG_CAP - captured ? (u64)amount : BQ_NATIVE_LOG_CAP - captured;
            truncated = truncated || retain < (u64)amount;
            for (u64 offset = 0; ok && offset < retain;)
            {
                ssize_t count = write(log, bytes + offset, (size_t)(retain - offset));
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) ok = false;
                else offset += (u64)count;
            }
            captured += retain;
        }
    }
    if (pipes[0] >= 0) close(pipes[0]);
    int status = 0;
    pid_t waited = -1;
    if (child > 0)
    {
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        ok = ok && waited == child;
    }
    if (log >= 0)
    {
        ok = fchmod(log, 0400) == 0 && fsync(log) == 0 && ok;
        if (close(log) != 0) ok = false;
    }
    int stage_status = waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : 126;
    BqError error = bq_native_result_publish(&finalization, recipe, job_id, attempt_token,
        workspace, base, candidate, result, stage_status, truncated, ok ? BQ_OK : BQ_IO);
    if (finalization.result_directory >= 0) close(finalization.result_directory);
    return error;
}
#else
BUSTER_GLOBAL_LOCAL BqError bq_native_payload(char const* source, char const* identity)
{
    (void)source; (void)identity;
    return BQ_UNSUPPORTED;
}
BUSTER_GLOBAL_LOCAL BqError bq_native_driver(BqRecipe recipe, char const* job, char const* token, char const* workspace,
                                             char const* base, char const* candidate, char const* result)
{
    (void)recipe; (void)job; (void)token; (void)workspace; (void)base; (void)candidate; (void)result;
    return BQ_UNSUPPORTED;
}
#endif
