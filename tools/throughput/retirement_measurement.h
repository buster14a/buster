/* Native observation boundary for #881.
 * executable_init hashes a frozen trusted binary outside timing; run binds the
 * actual argv/environment, executes that descriptor, then reads the actual
 * artifact or runtime output before advancing the paired-sample collector.
 * The service owns immutable source/cwd trees, private output descriptors,
 * independent correctness/oracles, sandboxing, lease and durable publication.
 * There is no request parser, recipe admission or performance verdict here.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_H
#define BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_H
#include "retirement_samples.h"
#include "retirement_artifact.h"

#ifdef __linux__
#define TP_RETIREMENT_COMMAND_ARGUMENTS 256u
#define TP_RETIREMENT_COMMAND_ENVIRONMENT 128u
#define TP_RETIREMENT_COMMAND_BYTES 65536u

typedef struct TpRetirementExecutable
{
    int descriptor, valid;
    struct stat identity;
    char sha256[65];
} TpRetirementExecutable;

typedef struct TpRetirementMeasuredCommand
{
    unsigned row, kind, variant, argument_count, environment_count, timeout_seconds;
    char* const* arguments;
    char* const* environment;
    char const* directory;
    char const* artifact;
    char const* command_sha256;
    char const* output_sha256;
    char const* code_section_sha256;
    uint64_t code_section_bytes;
} TpRetirementMeasuredCommand;

typedef enum TpRetirementMeasurementStatus
{
    TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
    TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED,
    TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID,
    TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED,
    TP_RETIREMENT_MEASUREMENT_COMPLETE
} TpRetirementMeasurementStatus;

typedef struct TpRetirementMeasurementResult
{
    TpRetirementMeasurementStatus status;
    TpProcessObservation observed;
    TpProcess process;
    uint64_t output_bytes;
    char output_sha256[65];
} TpRetirementMeasurementResult;

static int tp_retirement_file_same(struct stat const* a, struct stat const* b)
{
    int same = a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_mode == b->st_mode && a->st_nlink == b->st_nlink &&
        a->st_uid == b->st_uid && a->st_gid == b->st_gid && a->st_size == b->st_size &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
    return same;
}

/* pread leaves the caller's stream offset unchanged. Type/link/size and
 * metadata checks bracket the read; a partial read never yields a digest. */
static int tp_retirement_file_hash(int descriptor, char digest[65], uint64_t* size)
{
    struct stat before, after;
    int ok = digest && size && descriptor >= 3 && fstat(descriptor, &before) == 0 &&
        S_ISREG(before.st_mode) && before.st_nlink == 1 && before.st_size >= 0 &&
        (uint64_t)before.st_size <= TP_RETIREMENT_ARTIFACT_BYTES;
    Sha256 hash;
    sha256_init(&hash);
    uint64_t position = 0;
    while (ok && position < (uint64_t)before.st_size)
    {
        unsigned char bytes[32768];
        size_t wanted = (uint64_t)before.st_size - position < sizeof(bytes) ?
            (size_t)((uint64_t)before.st_size - position) : sizeof(bytes);
        ssize_t count = pread(descriptor, bytes, wanted, (off_t)position);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && (size_t)count <= wanted;
        if (ok)
        {
            sha256_add(&hash, bytes, (u64)count);
            position += (uint64_t)count;
        }
    }
    if (ok) ok = fstat(descriptor, &after) == 0 && tp_retirement_file_same(&before, &after);
    if (digest) digest[0] = 0;
    if (size) *size = 0;
    if (ok)
    {
        sha256_finish_hex(&hash, digest);
        *size = position;
    }
    return ok;
}

static int tp_retirement_executable_init(TpRetirementExecutable* executable, int descriptor,
    char const* expected_sha256)
{
    struct stat before, after;
    uint64_t size = 0;
    char digest[65];
    int flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int ok = executable && tp_retirement_digest(expected_sha256) && flags >= 0 &&
        (flags & FD_CLOEXEC) && fstat(descriptor, &before) == 0 &&
        !(before.st_mode & 0222) && (before.st_mode & 0111) &&
        tp_retirement_file_hash(descriptor, digest, &size) && size &&
        !strcmp(digest, expected_sha256) && fstat(descriptor, &after) == 0 &&
        tp_retirement_file_same(&before, &after);
    if (executable)
    {
        *executable = (TpRetirementExecutable){.descriptor = -1};
        if (ok)
        {
            executable->descriptor = descriptor;
            executable->identity = after;
            memcpy(executable->sha256, digest, sizeof(digest));
            executable->valid = 1;
        }
    }
    return ok;
}

/* Read into owned memory instead of mapping a potentially mutable file: a
 * concurrent truncation must fail this invocation, never SIGBUS the collector.
 * Metadata and the independently computed full digest join this inspection to
 * the bytes hashed by measurement_run. No code count supplied by a command is
 * accepted unless it equals the parsed payload below. */
static int tp_retirement_artifact_file(int descriptor, TpRetirementArtifact* facts)
{
    struct stat before, after;
    int ok = facts && descriptor >= 3 && fstat(descriptor, &before) == 0 &&
        S_ISREG(before.st_mode) && before.st_nlink == 1 && before.st_size > 0 &&
        (uint64_t)before.st_size <= TP_RETIREMENT_ARTIFACT_BYTES;
    unsigned char* bytes = ok ? (unsigned char*)malloc((size_t)before.st_size) : NULL;
    ok = ok && bytes;
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)before.st_size)
    {
        uint64_t remaining = (uint64_t)before.st_size - offset;
        size_t wanted = remaining < 32768 ? (size_t)remaining : 32768;
        ssize_t count = pread(descriptor, bytes + offset, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && (size_t)count <= wanted;
        if (ok) offset += (uint64_t)count;
    }
    if (ok) ok = fstat(descriptor, &after) == 0 && tp_retirement_file_same(&before, &after) &&
        tp_retirement_artifact(bytes, offset, facts);
    if (!ok && facts) *facts = (TpRetirementArtifact){0};
    free(bytes);
    return ok;
}

/* Frozen command identity: canonical ASCII JSON with sorted keys argv, cwd,
 * environment. Environment entries are sorted, unique NAME=value strings.
 * No ambient environment participates. All lengths are checked before launch. */
static int tp_retirement_command_string(Sha256* hash, char const* value, unsigned* remaining)
{
    int ok = value && remaining && *remaining;
    if (ok) sha256_add(hash, "\"", 1);
    for (unsigned i = 0; ok && value[i]; ++i)
    {
        unsigned char c = (unsigned char)value[i];
        ok = *remaining > 0 && c >= 32 && c <= 126;
        if (ok)
        {
            --*remaining;
            if (c == '"' || c == '\\') sha256_add(hash, "\\", 1);
            sha256_add(hash, &c, 1);
        }
    }
    if (ok) ok = *remaining > 0;
    if (ok) { --*remaining; sha256_add(hash, "\"", 1); }
    return ok;
}

static int tp_retirement_command_hash(TpRetirementMeasuredCommand const* command, char digest[65])
{
    int ok = command && digest && command->arguments && command->argument_count &&
        command->argument_count <= TP_RETIREMENT_COMMAND_ARGUMENTS &&
        command->environment && command->environment_count <= TP_RETIREMENT_COMMAND_ENVIRONMENT &&
        command->directory && command->directory[0] == '/' &&
        command->arguments[command->argument_count] == NULL &&
        command->environment[command->environment_count] == NULL;
    unsigned remaining = TP_RETIREMENT_COMMAND_BYTES;
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, "{\"argv\":[", 9);
    for (unsigned i = 0; ok && i < command->argument_count; ++i)
    {
        if (i) sha256_add(&hash, ",", 1);
        ok = tp_retirement_command_string(&hash, command->arguments[i], &remaining);
    }
    sha256_add(&hash, "],\"cwd\":", 8);
    if (ok) ok = tp_retirement_command_string(&hash, command->directory, &remaining);
    sha256_add(&hash, ",\"environment\":[", 16);
    size_t previous_name = 0;
    for (unsigned i = 0; ok && i < command->environment_count; ++i)
    {
        char const* entry = command->environment[i];
        if (i) sha256_add(&hash, ",", 1);
        ok = tp_retirement_command_string(&hash, entry, &remaining);
        char const* equal = ok ? strchr(entry, '=') : NULL;
        size_t name = equal ? (size_t)(equal - entry) : 0;
        ok = ok && name;
        for (size_t j = 0; ok && j < name; ++j)
            ok = (entry[j] >= 'A' && entry[j] <= 'Z') || (entry[j] >= 'a' && entry[j] <= 'z') ||
                entry[j] == '_' || (j && entry[j] >= '0' && entry[j] <= '9');
        if (ok && i)
            ok = strcmp(command->environment[i - 1], entry) < 0 &&
                !(name == previous_name && !memcmp(command->environment[i - 1], entry, name));
        previous_name = name;
    }
    sha256_add(&hash, "]}", 2);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

static int tp_retirement_artifact_leaf(char const* name)
{
    int ok = name && name[0] && strcmp(name, ".") && strcmp(name, "..");
    for (unsigned i = 0; ok && name[i]; ++i)
        ok = i < 127 && ((name[i] >= 'a' && name[i] <= 'z') ||
            (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') ||
            name[i] == '_' || name[i] == '-' || name[i] == '.');
    return ok;
}

/* output_directory is a service-opened private directory. Compiler artifacts
 * must not exist before launch; no stale output can satisfy the oracle. Runtime
 * output is the fresh log descriptor, which must be empty and positioned at 0.
 * Nothing is removed on either outcome: the caller retains failure evidence
 * and retires successful scratch output before the next invocation.
 * Independent inspection checks actual code-section metrics after timing;
 * full-artifact equality also binds these bytes to the correctness preflight.
 */
static int tp_retirement_measurement_run(TpRetirementSamples* samples,
    TpRetirementMeasuredCommand const* command, TpRetirementExecutable const* executable,
    TpProcessInputs const* inputs, int output_directory, TpRetirementMeasurementResult* result)
{
    TpRetirementMeasurementResult outcome = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
        .process = {.exit_code = -1}};
    TpRetirementInvocation invocation;
    TpRetirementExecution* execution = samples && samples->transcript ? samples->transcript->execution : NULL;
    struct stat binary, cwd, named_cwd, log, output_root, artifact;
    char command_digest[65], output_digest[65];
    int ok = result && samples && !samples->failed && !samples->exporting && command && executable &&
        executable->valid && inputs && inputs->executable == executable->descriptor &&
        inputs->environment == command->environment && command->timeout_seconds &&
        command->timeout_seconds <= 86400 && execution &&
        tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    if (ok) ok = command->row == invocation.row && command->kind == invocation.kind &&
        command->variant == invocation.variant && tp_retirement_digest(command->command_sha256) &&
        tp_retirement_digest(command->output_sha256) && tp_retirement_command_hash(command, command_digest) &&
        !strcmp(command_digest, command->command_sha256) &&
        fstat(executable->descriptor, &binary) == 0 && tp_retirement_file_same(&binary, &executable->identity) &&
        fstat(inputs->directory, &cwd) == 0 && S_ISDIR(cwd.st_mode) &&
        lstat(command->directory, &named_cwd) == 0 && S_ISDIR(named_cwd.st_mode) &&
        cwd.st_dev == named_cwd.st_dev && cwd.st_ino == named_cwd.st_ino &&
        fstat(inputs->log, &log) == 0 && S_ISREG(log.st_mode) && log.st_nlink == 1 && !log.st_size &&
        lseek(inputs->log, 0, SEEK_CUR) == 0 && (fcntl(inputs->log, F_GETFL) & O_ACCMODE) == O_RDWR;
    if (ok && !invocation.kind)
        ok = tp_retirement_artifact_leaf(command->artifact) && output_directory >= 3 &&
            fstat(output_directory, &output_root) == 0 && S_ISDIR(output_root.st_mode) &&
            output_root.st_uid == geteuid() && !(output_root.st_mode & 0022) &&
            fstatat(output_directory, command->artifact, &artifact, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT &&
            ((samples->rows[invocation.dense].metrics & (TP_RETIREMENT_SAMPLE_CODE |
                TP_RETIREMENT_SAMPLE_ZERO_BASELINE_CODE)) ?
                command->code_section_sha256 != NULL : !command->code_section_bytes) &&
            (command->code_section_sha256 ? tp_retirement_digest(command->code_section_sha256) :
                                             !command->code_section_bytes);
    if (ok && invocation.kind)
        ok = command->artifact == NULL && !command->code_section_bytes && !command->code_section_sha256;
    TpProcessObservation observed = {0};
    TpProcess process = {0};
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED;
        process = tp_process_observe_inputs(command->arguments, NULL, NULL,
            command->timeout_seconds, samples->transcript->cpu, 0, &observed, inputs);
        outcome.process = process;
        outcome.observed = observed;
        ok = observed.valid && !process.launch_error && !process.exit_code &&
            !process.signal_number && !process.timed_out;
    }
    int output = -1;
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID;
        output = invocation.kind ? fcntl(inputs->log, F_DUPFD_CLOEXEC, 3) :
            openat(output_directory, command->artifact, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    }
    uint64_t bytes = 0;
    if (ok) ok = tp_retirement_file_hash(output, output_digest, &bytes);
    if (ok)
    {
        outcome.output_bytes = bytes;
        memcpy(outcome.output_sha256, output_digest, sizeof(output_digest));
        ok = (invocation.kind || bytes) && command->code_section_bytes <= bytes &&
            !strcmp(output_digest, command->output_sha256) &&
            fstat(executable->descriptor, &binary) == 0 && tp_retirement_file_same(&binary, &executable->identity);
    }
    if (ok && !invocation.kind)
    {
        TpRetirementArtifact facts;
        ok = tp_retirement_artifact_file(output, &facts) && facts.file_bytes == bytes &&
            !strcmp(facts.file_sha256, output_digest) && facts.code_bytes == command->code_section_bytes &&
            (!command->code_section_sha256 || !strcmp(facts.code_sha256, command->code_section_sha256));
    }
    if (output >= 0 && close(output) != 0) ok = 0;
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED;
        TpRetirementOutput measured = {executable->sha256, command_digest, output_digest,
            command->code_section_sha256, command->code_section_bytes};
        ok = tp_retirement_samples_append(samples, &observed, &process, &measured);
    }
    if (!ok) tp_retirement_samples_poison(samples);
    else outcome.status = TP_RETIREMENT_MEASUREMENT_COMPLETE;
    if (result) *result = outcome;
    return ok;
}
#endif
#endif
