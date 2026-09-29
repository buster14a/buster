/* Native observation boundary for #881 (A1).
 * executable_init hashes a frozen trusted binary outside timing; run binds the
 * actual argv/environment, executes that descriptor, then reads the actual
 * outputs before advancing the paired-sample collector. A compiler invocation
 * is one batch process of one frozen group: an object batch must write every
 * frozen object byte-identical to its frozen artifact and a per-input metrics
 * file that the strict reader accepts against the frozen oracle; a singleton
 * link/self-host group writes its one artifact. Code sections are parsed once,
 * outside timing (tp_retirement_code_observe), never per invocation.
 * The service owns immutable source/cwd trees, private output descriptors,
 * independent correctness/oracles, sandboxing, lease and durable publication.
 * There is no request parser, recipe admission or performance verdict here.
 */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_H
#define BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_H
#include "retirement_samples.h"
#include "retirement_artifact.h"

#ifdef __linux__
#include "retirement_command.h"
typedef struct TpRetirementExecutable
{
    int descriptor, valid;
    struct stat identity;
    char sha256[65];
} TpRetirementExecutable;

/* unit is the group ordinal for a compiler batch and the census row for a
 * runtime process. A singleton compiler group names its final `artifact`; an
 * object group supplies its frozen `batch` contract. output_sha256 is the
 * frozen batch output digest (compiler) or independent oracle output (runtime);
 * exit_status is the frozen batch exit status. */
typedef struct TpRetirementMeasuredCommand
{
    unsigned unit, kind, variant, argument_count, environment_count, timeout_seconds;
    char* const* arguments;
    char* const* environment;
    char const* directory;
    char const* artifact;
    TpRetirementBatchContract const* batch;
    char const* command_sha256;
    char const* output_sha256;
    int exit_status;
} TpRetirementMeasuredCommand;

typedef enum TpRetirementMeasurementStatus
{
    TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
    TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED,
    TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID,
    TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED,
    TP_RETIREMENT_MEASUREMENT_COMPLETE
} TpRetirementMeasurementStatus;

/* metrics_path is where the caller must publish the metrics bytes (the
 * transcript names it); it is empty for every other invocation. */
typedef struct TpRetirementMeasurementResult
{
    TpRetirementMeasurementStatus status;
    TpProcessObservation observed;
    TpProcess process;
    uint64_t output_bytes, metrics_bytes;
    char output_sha256[65], metrics_sha256[65];
    char metrics_path[TP_RETIREMENT_METRICS_PATH_CAP];
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

/* Read a bounded regular file into owned memory instead of mapping a
 * potentially mutable file: a concurrent truncation must fail this read,
 * never SIGBUS the collector. Metadata brackets the read. */
static unsigned char* tp_retirement_file_read(int descriptor, uint64_t limit, uint64_t* size)
{
    struct stat before, after;
    int ok = size && descriptor >= 3 && fstat(descriptor, &before) == 0 &&
        S_ISREG(before.st_mode) && before.st_nlink == 1 && before.st_size > 0 &&
        (uint64_t)before.st_size <= limit;
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
    if (ok) ok = fstat(descriptor, &after) == 0 && tp_retirement_file_same(&before, &after);
    if (!ok)
    {
        free(bytes);
        bytes = NULL;
    }
    if (size) *size = ok ? offset : 0;
    return bytes;
}

/* Independent inspection of one frozen artifact, outside timing. No code count
 * supplied by a command is accepted unless it equals the parsed payload. */
static int tp_retirement_artifact_file(int descriptor, TpRetirementArtifact* facts)
{
    uint64_t size = 0;
    unsigned char* bytes = tp_retirement_file_read(descriptor, TP_RETIREMENT_ARTIFACT_BYTES, &size);
    int ok = facts && bytes && tp_retirement_artifact(bytes, size, facts);
    if (!ok && facts) *facts = (TpRetirementArtifact){0};
    free(bytes);
    return ok;
}

/* The once-per-(variant, row) code fact: parse the frozen artifact's code
 * sections and require an independent reproduction compile to be
 * byte-identical. A mismatch is nondeterminism, never a code observation. */
static inline int tp_retirement_code_observe(int artifact, int reproduction, TpRetirementCodeSide* side)
{
    TpRetirementArtifact facts;
    uint64_t bytes = 0;
    char digest[65];
    int ok = side && tp_retirement_artifact_file(artifact, &facts) &&
        tp_retirement_file_hash(reproduction, digest, &bytes) && bytes == facts.file_bytes &&
        !strcmp(digest, facts.file_sha256);
    if (side)
    {
        *side = (TpRetirementCodeSide){0};
        if (ok)
        {
            memcpy(side->artifact_sha256, facts.file_sha256, 65);
            memcpy(side->code_sha256, facts.code_sha256, 65);
            memcpy(side->reproduction_sha256, digest, 65);
            side->code_bytes = facts.code_bytes;
            ok = tp_retirement_code_side(side);
        }
        if (!ok) *side = (TpRetirementCodeSide){0};
    }
    return ok;
}

/* Read, hash and strictly check one batch's per-input metrics file. */
static int tp_retirement_metrics_file(int descriptor, TpRetirementBatchContract const* contract,
    uint64_t elapsed_ns, TpRetirementMemberSample* members, unsigned member_capacity,
    char digest[65], uint64_t* size)
{
    uint64_t bytes = 0;
    unsigned char* data = tp_retirement_file_read(descriptor, TP_RETIREMENT_METRICS_ARTIFACT_BYTES, &bytes);
    int ok = data && digest && size &&
        tp_retirement_metrics_check(data, bytes, contract, elapsed_ns, members, member_capacity);
    if (digest) digest[0] = 0;
    if (size) *size = 0;
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, data, (u64)bytes);
        sha256_finish_hex(&hash, digest);
        *size = bytes;
    }
    free(data);
    return ok;
}

static int tp_retirement_command_hash(TpRetirementMeasuredCommand const* command, char digest[65])
{
    int ok = tp_retirement_command_fields_hash(command ? command->arguments : NULL,
        command ? command->argument_count : 0, command ? command->directory : NULL,
        command ? command->environment : NULL, command ? command->environment_count : 0, digest);
    return ok;
}

static int tp_retirement_artifact_leaf(char const* name)
{
    return tp_retirement_metrics_leaf(name);
}

/* A frozen batch names exactly its group's census rows as members, in layout
 * order, and no control names a row of the timed projection. */
static int tp_retirement_batch_rows_match(TpRetirementSamples const* samples,
    TpRetirementSampleGroup const* group, TpRetirementBatchContract const* batch)
{
    unsigned members = 0;
    int ok = samples && group && batch && batch->inputs;
    for (unsigned i = 0; ok && i < batch->input_count; ++i)
    {
        TpRetirementBatchInput const* input = &batch->inputs[i];
        if (input->member)
            ok = members < group->count &&
                input->row == samples->rows[samples->members[group->first + members++]].id;
        else
            ok = input->row == TP_RETIREMENT_BATCH_NO_ROW ||
                tp_retirement_samples_row(samples, input->row) == TP_RETIREMENT_NONE;
    }
    return ok && members == group->count;
}

static int tp_retirement_output_absent(int directory, char const* leaf)
{
    struct stat existing;
    int absent = leaf && fstatat(directory, leaf, &existing, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
    return absent;
}

static int tp_retirement_output_hash(int directory, char const* leaf, char digest[65], uint64_t* bytes)
{
    int output = openat(directory, leaf, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    int ok = output >= 0 && tp_retirement_file_hash(output, digest, bytes) && *bytes;
    if (output >= 0 && close(output) != 0) ok = 0;
    if (!ok && digest) digest[0] = 0;
    return ok;
}

/* output_directory is a service-opened private directory. Compiler outputs
 * must not exist before launch; no stale output can satisfy the oracle. Runtime
 * output is the fresh log descriptor, which must be empty and positioned at 0.
 * Nothing is removed on either outcome: the caller retains failure evidence
 * and retires successful scratch output before the next invocation. Every
 * warmup and sample batch must reproduce each frozen object byte for byte;
 * the first mismatch is nondeterminism and invalidates the attempt. */
static int tp_retirement_measurement_run(TpRetirementSamples* samples,
    TpRetirementMeasuredCommand const* command, TpRetirementExecutable const* executable,
    TpProcessInputs const* inputs, int output_directory, TpRetirementMeasurementResult* result)
{
    TpRetirementMeasurementResult outcome = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
        .process = {.exit_code = -1}};
    TpRetirementInvocation invocation;
    TpRetirementExecution* execution = samples && samples->transcript ? samples->transcript->execution : NULL;
    struct stat binary, cwd, named_cwd, log, output_root;
    char command_digest[65], output_digest[65] = {0}, frozen_output[65];
    int ok = result && samples && !samples->failed && !samples->exporting && command && executable &&
        executable->valid && inputs && inputs->executable == executable->descriptor &&
        inputs->environment == command->environment && command->timeout_seconds &&
        command->timeout_seconds <= 86400 && execution &&
        tp_retirement_execution_peek(execution, &invocation) == TP_RETIREMENT_NEXT_READY;
    TpRetirementSampleGroup const* group = ok && !invocation.kind && invocation.group < samples->group_count ?
        &samples->groups[invocation.group] : NULL;
    TpRetirementBatchContract const* batch = command ? command->batch : NULL;
    if (ok) ok = command->unit == (invocation.kind ? invocation.row : invocation.group) &&
        command->kind == invocation.kind && command->variant == invocation.variant &&
        tp_retirement_digest(command->command_sha256) &&
        tp_retirement_digest(command->output_sha256) && tp_retirement_command_hash(command, command_digest) &&
        !strcmp(command_digest, command->command_sha256) &&
        fstat(executable->descriptor, &binary) == 0 && tp_retirement_file_same(&binary, &executable->identity) &&
        fstat(inputs->directory, &cwd) == 0 && S_ISDIR(cwd.st_mode) &&
        lstat(command->directory, &named_cwd) == 0 && S_ISDIR(named_cwd.st_mode) &&
        cwd.st_dev == named_cwd.st_dev && cwd.st_ino == named_cwd.st_ino &&
        fstat(inputs->log, &log) == 0 && S_ISREG(log.st_mode) && log.st_nlink == 1 && !log.st_size &&
        lseek(inputs->log, 0, SEEK_CUR) == 0 && (fcntl(inputs->log, F_GETFL) & O_ACCMODE) == O_RDWR;
    if (ok && !invocation.kind)
    {
        ok = group && output_directory >= 3 && fstat(output_directory, &output_root) == 0 &&
            S_ISDIR(output_root.st_mode) && output_root.st_uid == geteuid() && !(output_root.st_mode & 0022);
        if (ok && group->kind == TP_RETIREMENT_GROUP_OBJECT)
        {
            ok = !command->artifact && tp_retirement_batch_contract_valid(batch) &&
                batch->exit_status == (unsigned)command->exit_status &&
                tp_retirement_batch_contract_output(batch, frozen_output) &&
                !strcmp(frozen_output, command->output_sha256) &&
                tp_retirement_output_absent(output_directory, batch->metrics);
            for (unsigned i = 0; ok && i < batch->input_count; ++i)
                ok = !batch->inputs[i].artifact || tp_retirement_output_absent(output_directory, batch->inputs[i].artifact);
            ok = ok && tp_retirement_batch_rows_match(samples, group, batch);
        }
        else if (ok)
            ok = !batch && !command->exit_status && tp_retirement_artifact_leaf(command->artifact) &&
                tp_retirement_output_absent(output_directory, command->artifact);
    }
    if (ok && invocation.kind) ok = !command->artifact && !batch && !command->exit_status;
    TpProcessObservation observed = {0};
    TpProcess process = {0};
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED;
        process = tp_process_observe_inputs(command->arguments, NULL, NULL,
            command->timeout_seconds, samples->transcript->cpu, 0, &observed, inputs);
        outcome.process = process;
        outcome.observed = observed;
        ok = observed.valid && !process.launch_error && process.exit_code == command->exit_status &&
            !process.signal_number && !process.timed_out;
    }
    TpRetirementMemberSample members[TP_RETIREMENT_BATCH_INPUTS];
    unsigned member_count = 0;
    char metrics_digest[65] = {0};
    uint64_t metrics_bytes = 0, bytes = 0;
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID;
        if (invocation.kind)
        {
            int output = fcntl(inputs->log, F_DUPFD_CLOEXEC, 3);
            ok = output >= 0 && tp_retirement_file_hash(output, output_digest, &bytes);
            if (output >= 0 && close(output) != 0) ok = 0;
        }
        else if (group->kind == TP_RETIREMENT_GROUP_SINGLETON)
        {
            char artifact[65];
            char const* objects[1] = {artifact};
            ok = tp_retirement_output_hash(output_directory, command->artifact, artifact, &bytes) &&
                tp_retirement_batch_output_digest(objects, 1, output_digest);
        }
        else
        {
            char const* objects[TP_RETIREMENT_BATCH_INPUTS];
            char (*digests)[65] = (char (*)[65])malloc((size_t)batch->input_count * 65);
            ok = digests != NULL;
            for (unsigned i = 0; ok && i < batch->input_count; ++i)
            {
                uint64_t object_bytes = 0;
                objects[i] = batch->inputs[i].artifact ? digests[i] : NULL;
                if (batch->inputs[i].artifact)
                    ok = tp_retirement_output_hash(output_directory, batch->inputs[i].artifact, digests[i],
                        &object_bytes) && !strcmp(digests[i], batch->inputs[i].object_sha256);
                if (ok) bytes += object_bytes;
            }
            ok = ok && tp_retirement_batch_output_digest(objects, batch->input_count, output_digest);
            free(digests);
            int metrics = ok ? openat(output_directory, batch->metrics,
                O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1;
            ok = ok && metrics >= 0 && tp_retirement_metrics_file(metrics, batch,
                observed.finished_ns - observed.started_ns, members, group->count, metrics_digest, &metrics_bytes);
            if (metrics >= 0 && close(metrics) != 0) ok = 0;
            member_count = group->count;
        }
    }
    if (ok)
    {
        outcome.output_bytes = bytes;
        memcpy(outcome.output_sha256, output_digest, sizeof(output_digest));
        ok = (invocation.kind || bytes) && !strcmp(output_digest, command->output_sha256) &&
            fstat(executable->descriptor, &binary) == 0 && tp_retirement_file_same(&binary, &executable->identity);
    }
    if (ok && metrics_bytes)
    {
        char instance[65], start[32];
        snprintf(start, sizeof(start), "%" PRIu64, observed.start_token);
        ok = tp_retirement_process_instance(instance, samples->transcript->job, samples->transcript->attempt,
                samples->transcript->boot, observed.pid, start) &&
            tp_retirement_metrics_path(outcome.metrics_path, instance);
        outcome.metrics_bytes = metrics_bytes;
        memcpy(outcome.metrics_sha256, metrics_digest, sizeof(metrics_digest));
    }
    if (ok)
    {
        outcome.status = TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED;
        TpRetirementOutput measured = {executable->sha256, command_digest, output_digest,
            metrics_bytes ? metrics_digest : NULL, metrics_bytes, command->exit_status};
        ok = tp_retirement_samples_append(samples, &observed, &process, &measured,
            member_count ? members : NULL, member_count);
    }
    if (!ok) tp_retirement_samples_poison(samples);
    else outcome.status = TP_RETIREMENT_MEASUREMENT_COMPLETE;
    if (result) *result = outcome;
    return ok;
}
#endif
#endif
