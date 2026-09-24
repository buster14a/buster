/* #1020 frozen-artifact readback. This shares the already reviewed independent
 * ELF/COFF/PE/Mach-O reader used by retirement measurement. It reads a single
 * service-owned file through a held descriptor, checks its name and metadata
 * again after parsing, then commits both sides to the correctness gate.
 * Runtime output is hashed from the completed service-owned process log and
 * compared with an independently established oracle by the gate. Process
 * plans are hashed with the measurement lane's canonical serializer. Compiler
 * and runtime launch/poll bind plans to actual child waits; the runner still
 * owns job deadlines, verified output production and the independent oracle.
 */
#define _POSIX_C_SOURCE 200809L
#include "retirement_artifact_service.h"
#include "../throughput/retirement_artifact.h"
#include "../throughput/retirement_command.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BQ_RETIREMENT_ARTIFACT_READ_CAP (512u * 1024u * 1024u)
#define BQ_RETIREMENT_RUNTIME_LOG_CAP (16u * 1024u * 1024u)

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_name(char const* name)
{
    bool ok = name && name[0] && !(name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])));
    for (char const* at = name; ok && *at; at += 1) ok = *at != '/';
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_target(uint32_t target, unsigned stage,
    unsigned* format, unsigned* machine)
{
    bool ok = target >= 1 && target <= 12 &&
        stage >= BQ_RETIREMENT_STAGE_OBJECT && stage <= BQ_RETIREMENT_STAGE_SELF_HOST;
    if (ok)
    {
        bool apple = target == 1 || target == 2 || target == 7 || target == 8;
        bool windows = target == 4 || target == 6 || target == 10 || target == 12;
        *format = apple ? 4 : windows ? stage == BQ_RETIREMENT_STAGE_OBJECT ? 2 : 3 : 1;
        *machine = target <= 6 ? 2 : 1;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_stable(struct stat const* first, struct stat const* second)
{
    bool ok = first->st_dev == second->st_dev && first->st_ino == second->st_ino &&
        first->st_size == second->st_size && first->st_mode == second->st_mode &&
        first->st_nlink == second->st_nlink && first->st_uid == second->st_uid;
#if defined(__APPLE__)
    ok = ok && first->st_mtimespec.tv_sec == second->st_mtimespec.tv_sec &&
        first->st_mtimespec.tv_nsec == second->st_mtimespec.tv_nsec &&
        first->st_ctimespec.tv_sec == second->st_ctimespec.tv_sec &&
        first->st_ctimespec.tv_nsec == second->st_ctimespec.tv_nsec;
#else
    ok = ok && first->st_mtim.tv_sec == second->st_mtim.tv_sec &&
        first->st_mtim.tv_nsec == second->st_mtim.tv_nsec &&
        first->st_ctim.tv_sec == second->st_ctim.tv_sec &&
        first->st_ctim.tv_nsec == second->st_ctim.tv_nsec;
#endif
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_output_directory(int directory, struct stat* identity)
{
    int flags = directory >= 3 ? fcntl(directory, F_GETFD) : -1;
    bool ok = flags >= 0 && (flags & FD_CLOEXEC) && fstat(directory, identity) == 0 &&
        S_ISDIR(identity->st_mode) && identity->st_uid == geteuid() &&
        !(identity->st_mode & 0022);
    return ok;
}

bool bq_retirement_artifact_start(BqRetirementArtifactLocation location,
    BqRetirementArtifactStart* start)
{
    struct stat directory = {0}, existing = {0};
    bool fresh = start && !start->armed;
    bool ok = fresh && bq_retirement_artifact_name(location.name) &&
        strlen(location.name) < BQ_RETIREMENT_OUTPUT_NAME_CAP &&
        bq_retirement_output_directory(location.directory, &directory) &&
        fstatat(location.directory, location.name, &existing, AT_SYMLINK_NOFOLLOW) < 0 &&
        errno == ENOENT;
    if (fresh)
    {
        *start = (BqRetirementArtifactStart){0};
        if (ok)
        {
            start->directory = location.directory;
            memcpy(start->name, location.name, strlen(location.name) + 1);
            start->directory_device = (uint64_t)directory.st_dev;
            start->directory_inode = (uint64_t)directory.st_ino;
            start->armed = 1;
        }
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_output_started(BqRetirementArtifactLocation location,
    BqRetirementArtifactStart const* start)
{
    struct stat directory = {0};
    bool ok = start && start->armed == 1 && location.name &&
        start->directory == location.directory && !strcmp(start->name, location.name) &&
        bq_retirement_output_directory(location.directory, &directory) &&
        start->directory_device == (uint64_t)directory.st_dev &&
        start->directory_inode == (uint64_t)directory.st_ino;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_output_absent(BqRetirementArtifactStart const* start)
{
    return start && !start->directory && !start->name[0] &&
        !start->directory_device && !start->directory_inode && !start->process &&
        !start->command_sha256[0] && !start->armed && !start->process_state;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_runtime_absent(BqRetirementRuntimeStart const* start)
{
    return start && bq_retirement_output_absent(&start->location) &&
        !start->file_device && !start->file_inode && !start->writer &&
        !start->process && !start->command_sha256[0] && !start->state;
}

bool bq_retirement_runtime_start(BqRetirementArtifactLocation location,
    BqRetirementRuntimeStart* start)
{
    BqRetirementArtifactStart vacant = {0};
    bool fresh = start && !start->state;
    bool ok = fresh && bq_retirement_artifact_start(location, &vacant);
    int writer = ok ? openat(location.directory, location.name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    struct stat file = {0};
    ok = ok && writer >= 3 && fchmod(writer, 0600) == 0 && fstat(writer, &file) == 0 &&
        S_ISREG(file.st_mode) && file.st_uid == geteuid() && file.st_nlink == 1 &&
        file.st_size == 0 && bq_retirement_output_started(location, &vacant);
    if (fresh)
    {
        *start = (BqRetirementRuntimeStart){0};
        if (ok)
        {
            start->location = vacant;
            start->file_device = (uint64_t)file.st_dev;
            start->file_inode = (uint64_t)file.st_ino;
            start->writer = writer;
            start->state = BQ_RETIREMENT_RUNTIME_CREATED;
        }
    }
    if (!ok && writer >= 0) close(writer);
    return ok;
}

void bq_retirement_runtime_abort(BqRetirementRuntimeStart* start)
{
    if (start)
    {
        if (start->state != BQ_RETIREMENT_RUNTIME_RUNNING)
        {
            if (start->state && start->writer >= 3) close(start->writer);
            *start = (BqRetirementRuntimeStart){0};
        }
    }
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_runtime_started(BqRetirementRuntimeStart const* start,
    int descriptor)
{
    struct stat named = {0}, file = {0};
    BqRetirementArtifactLocation location = {0};
    if (start) location = (BqRetirementArtifactLocation){start->location.directory, start->location.name};
    bool ok = start && start->state == BQ_RETIREMENT_RUNTIME_FROZEN && !start->process && start->command_sha256[0] &&
        bq_retirement_output_started(location, &start->location) &&
        descriptor >= 3 && fstat(descriptor, &file) == 0 &&
        fstatat(location.directory, location.name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        bq_retirement_artifact_stable(&file, &named) &&
        start->file_device == (uint64_t)file.st_dev && start->file_inode == (uint64_t)file.st_ino;
    return ok;
}

bool bq_retirement_runtime_finish(BqRetirementRuntimeStart* start, int* read_descriptor)
{
    struct stat file = {0};
    bool live = start && start->state == BQ_RETIREMENT_RUNTIME_RUNNING;
    bool ok = start && read_descriptor && start->state == BQ_RETIREMENT_RUNTIME_REAPED && !start->process &&
        start->command_sha256[0] && start->writer >= 3 &&
        fstat(start->writer, &file) == 0 && S_ISREG(file.st_mode) &&
        file.st_uid == geteuid() && file.st_nlink == 1 &&
        start->file_device == (uint64_t)file.st_dev && start->file_inode == (uint64_t)file.st_ino &&
        file.st_size >= 0 && (uint64_t)file.st_size <= BQ_RETIREMENT_RUNTIME_LOG_CAP &&
        fchmod(start->writer, 0400) == 0 && fsync(start->writer) == 0;
    if (read_descriptor) *read_descriptor = -1;
    if (start && !live && start->state && start->writer >= 3)
    {
        if (close(start->writer) != 0) ok = false;
        start->writer = -1;
    }
    int reader = ok ? openat(start->location.directory, start->location.name,
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (ok) start->state = BQ_RETIREMENT_RUNTIME_FROZEN;
    if (ok) ok = bq_retirement_runtime_started(start, reader);
    if (ok) *read_descriptor = reader;
    else
    {
        if (reader >= 0) close(reader);
        if (start && !live) *start = (BqRetirementRuntimeStart){0};
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_artifact_read(BqRetirementArtifactLocation location,
    unsigned format, unsigned machine, unsigned executable, TpRetirementArtifact* facts)
{
    bool ok = facts && bq_retirement_artifact_name(location.name) &&
        format >= 1 && format <= 4 && machine >= 1 && machine <= 2 && executable <= 1;
    if (facts) *facts = (TpRetirementArtifact){0};
    int file = ok ? openat(location.directory, location.name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat first = {0}, named = {0}, last = {0}, renamed = {0};
    ok = ok && file >= 0 && fstat(file, &first) == 0 &&
        fstatat(location.directory, location.name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(first.st_mode) && first.st_nlink == 1 && first.st_uid == geteuid() &&
        !(first.st_mode & 0222) && first.st_size > 0 &&
        (uint64_t)first.st_size <= BQ_RETIREMENT_ARTIFACT_READ_CAP &&
        bq_retirement_artifact_stable(&first, &named);
    unsigned char* data = ok ? malloc((size_t)first.st_size) : NULL;
    ok = ok && data != NULL;
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)first.st_size)
    {
        size_t wanted = (uint64_t)first.st_size - offset > 16384 ? 16384 :
                        (size_t)((uint64_t)first.st_size - offset);
        ssize_t count = pread(file, data + offset, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) offset += (uint64_t)count;
    }
    TpRetirementArtifact parsed = {0};
    if (ok) ok = tp_retirement_artifact(data, (uint64_t)first.st_size, &parsed) &&
                 parsed.format == format && parsed.machine == machine && parsed.executable == executable;
    if (ok) ok = fstat(file, &last) == 0 &&
                 fstatat(location.directory, location.name, &renamed, AT_SYMLINK_NOFOLLOW) == 0 &&
                 bq_retirement_artifact_stable(&first, &last) &&
                 bq_retirement_artifact_stable(&last, &renamed);
    free(data);
    if (file >= 0 && close(file) != 0) ok = false;
    if (ok) *facts = parsed;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_runtime_read(int descriptor, char digest[65])
{
    struct stat before = {0}, after = {0};
    int descriptor_flags = descriptor >= 3 ? fcntl(descriptor, F_GETFD) : -1;
    int access_flags = descriptor_flags >= 0 ? fcntl(descriptor, F_GETFL) : -1;
    bool ok = digest && descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) &&
        access_flags >= 0 && (access_flags & O_ACCMODE) == O_RDONLY &&
        fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_uid == geteuid() && before.st_nlink == 1 && !(before.st_mode & 0222) &&
        before.st_size >= 0 && (uint64_t)before.st_size <= BQ_RETIREMENT_RUNTIME_LOG_CAP;
    Sha256 hash;
    if (ok) sha256_init(&hash);
    unsigned char bytes[16384];
    uint64_t offset = 0;
    while (ok && offset < (uint64_t)before.st_size)
    {
        size_t wanted = (uint64_t)before.st_size - offset < sizeof(bytes) ?
                        (size_t)((uint64_t)before.st_size - offset) : sizeof(bytes);
        ssize_t count = pread(descriptor, bytes, wanted, (off_t)offset);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0 && (size_t)count <= wanted;
        if (ok)
        {
            sha256_add(&hash, bytes, (uint64_t)count);
            offset += (uint64_t)count;
        }
    }
    if (ok) ok = fstat(descriptor, &after) == 0 && bq_retirement_artifact_stable(&before, &after);
    if (digest) digest[0] = 0;
    if (ok) sha256_finish_hex(&hash, digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_command_absent(BqRetirementProcessCommand const* command)
{
    bool absent = !command->arguments && !command->environment && !command->directory &&
                  !command->argument_count && !command->environment_count;
    return absent;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_command_hash(BqRetirementProcessCommand const* command,
    char digest[65])
{
    bool ok = tp_retirement_command_fields_hash(command->arguments, command->argument_count,
        command->directory, command->environment, command->environment_count, digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL pid_t bq_retirement_execute(BqRetirementProcessCommand const* command,
    int writer)
{
    pid_t child = fork();
    if (child == 0)
    {
        if (writer >= 3 &&
            (dup2(writer, STDOUT_FILENO) < 0 ||
             dup2(writer, STDERR_FILENO) < 0)) _exit(126);
        if (writer >= 3) close(writer);
        if (chdir(command->directory) != 0) _exit(126);
        execve(command->arguments[0], command->arguments, command->environment);
        _exit(127);
    }
    return child;
}

bool bq_retirement_artifact_launch(BqRetirementArtifactStart* start,
    BqRetirementProcessCommand const* command)
{
    struct stat existing = {0};
    char digest[65] = {0};
    BqRetirementArtifactLocation location = {0};
    if (start) location = (BqRetirementArtifactLocation){start->directory, start->name};
    bool ok = start && command && start->armed == 1 && !start->process &&
        !start->process_state && bq_retirement_command_hash(command, digest) &&
        command->arguments[0][0] == '/' && bq_retirement_output_started(location, start) &&
        fstatat(start->directory, start->name, &existing, AT_SYMLINK_NOFOLLOW) < 0 &&
        errno == ENOENT;
    pid_t child = ok ? bq_retirement_execute(command, -1) : -1;
    ok = ok && child > 0;
    if (ok)
    {
        start->process = child;
        memcpy(start->command_sha256, digest, sizeof(digest));
        start->process_state = BQ_RETIREMENT_ARTIFACT_RUNNING;
    }
    return ok;
}

int bq_retirement_artifact_poll(BqRetirementArtifactStart* start)
{
    int result = -1;
    if (start && start->process_state == BQ_RETIREMENT_ARTIFACT_RUNNING &&
        start->process > 0)
    {
        int status = 0;
        pid_t waited = waitpid(start->process, &status, WNOHANG);
        if (waited == 0 || (waited < 0 && errno == EINTR)) result = 0;
        else
        {
            bool passed = waited == start->process && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0;
            start->process = 0;
            start->process_state = passed ? BQ_RETIREMENT_ARTIFACT_REAPED :
                BQ_RETIREMENT_ARTIFACT_FAILED;
            result = passed ? 1 : -1;
        }
    }
    return result;
}

void bq_retirement_artifact_abort(BqRetirementArtifactStart* start)
{
    if (start && start->process_state != BQ_RETIREMENT_ARTIFACT_RUNNING)
        *start = (BqRetirementArtifactStart){0};
}

/* The worker owns the deadline and kills the whole job on cancellation. This
 * private child remains registered until poll observes its actual wait status. */
bool bq_retirement_runtime_launch(BqRetirementRuntimeStart* start,
    BqRetirementProcessCommand const* command)
{
    char digest[65] = {0};
    struct stat file = {0};
    BqRetirementArtifactLocation location = {0};
    if (start) location = (BqRetirementArtifactLocation){start->location.directory, start->location.name};
    int descriptor_flags = start && start->writer >= 3 ? fcntl(start->writer, F_GETFD) : -1;
    int access_flags = descriptor_flags >= 0 ? fcntl(start->writer, F_GETFL) : -1;
    bool ok = start && start->state == BQ_RETIREMENT_RUNTIME_CREATED && command &&
        bq_retirement_command_hash(command, digest) && command->arguments[0][0] == '/' &&
        bq_retirement_output_started(location, &start->location) &&
        descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) &&
        access_flags >= 0 && (access_flags & O_ACCMODE) == O_WRONLY &&
        fstat(start->writer, &file) == 0 && S_ISREG(file.st_mode) &&
        start->file_device == (uint64_t)file.st_dev &&
        start->file_inode == (uint64_t)file.st_ino && file.st_nlink == 1;
    pid_t child = ok ? bq_retirement_execute(command, start->writer) : -1;
    ok = ok && child > 0;
    if (ok)
    {
        start->process = child;
        memcpy(start->command_sha256, digest, sizeof(digest));
        start->state = BQ_RETIREMENT_RUNTIME_RUNNING;
    }
    return ok;
}

int bq_retirement_runtime_poll(BqRetirementRuntimeStart* start)
{
    int result = -1;
    if (start && start->state == BQ_RETIREMENT_RUNTIME_RUNNING && start->process > 0)
    {
        int status = 0;
        pid_t waited = waitpid(start->process, &status, WNOHANG);
        if (waited == 0 || (waited < 0 && errno == EINTR)) result = 0;
        else
        {
            bool passed = waited == start->process && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0;
            start->process = 0;
            start->state = passed ? BQ_RETIREMENT_RUNTIME_REAPED : BQ_RETIREMENT_RUNTIME_FAILED;
            result = passed ? 1 : -1;
        }
    }
    return result;
}

bool bq_retirement_correctness_row_service(BqRetirementCorrectness* gate,
    BqRetirementArtifactLocation artifacts[2], BqRetirementArtifactStart starts[2],
    int runtime_outputs[2], BqRetirementRuntimeStart runtime_starts[2],
    BqRetirementRowCommands const commands[2], BqRetirementRowFact const* observed)
{
    bool ok = gate && !gate->failed && !gate->finished && observed && artifacts && starts &&
        runtime_outputs && runtime_starts && commands &&
        gate->checks_done == gate->check_count && gate->rows_done < gate->prepared.rows;
    BqRetirementRowFact row = {0};
    if (ok)
    {
        row = *observed;
        BqRetirementTrustedRow const* trusted = &gate->trusted_rows[gate->rows_done];
        unsigned format = 0, machine = 0;
        bool runtime = trusted->compiler_eligible && trusted->execution_obligation &&
            trusted->stage != BQ_RETIREMENT_STAGE_OBJECT && trusted->target == gate->prepared.native_target;
        ok = !row.code_eligible && bq_retirement_artifact_target(trusted->target,
            trusted->stage, &format, &machine);
        for (unsigned side = 0; ok && side < 2; side += 1)
        {
            BqRetirementObservedSide* output = &row.side[side];
            ok = !output->artifact_sha256[0] && !output->code_sha256[0] && !output->code_bytes &&
                 !output->runtime_output_sha256[0] && !output->compiler_command_sha256[0] &&
                 !output->runtime_command_sha256[0];
            if (ok) ok = trusted->compiler_eligible ?
                bq_retirement_command_hash(&commands[side].compiler, output->compiler_command_sha256) :
                bq_retirement_command_absent(&commands[side].compiler);
            if (ok && trusted->compiler_eligible)
            {
                TpRetirementArtifact facts = {0};
                ok = bq_retirement_output_started(artifacts[side], &starts[side]) &&
                     starts[side].process_state == BQ_RETIREMENT_ARTIFACT_REAPED &&
                     !starts[side].process &&
                     !strcmp(starts[side].command_sha256, output->compiler_command_sha256) &&
                     bq_retirement_artifact_read(artifacts[side], format, machine,
                         trusted->stage != BQ_RETIREMENT_STAGE_OBJECT, &facts) &&
                     bq_retirement_output_started(artifacts[side], &starts[side]);
                if (ok)
                {
                    memcpy(output->artifact_sha256, facts.file_sha256, 65);
                    if (trusted->code_obligation)
                    {
                        memcpy(output->code_sha256, facts.code_sha256, 65);
                        output->code_bytes = facts.code_bytes;
                    }
                }
            }
            else if (ok) ok = artifacts[side].name == NULL &&
                              bq_retirement_output_absent(&starts[side]);
            if (ok) ok = runtime ?
                bq_retirement_command_hash(&commands[side].runtime, output->runtime_command_sha256) :
                bq_retirement_command_absent(&commands[side].runtime);
            if (ok) ok = runtime ?
                bq_retirement_runtime_started(&runtime_starts[side], runtime_outputs[side]) &&
                !strcmp(runtime_starts[side].command_sha256, output->runtime_command_sha256) &&
                bq_retirement_runtime_read(runtime_outputs[side], output->runtime_output_sha256) &&
                bq_retirement_runtime_started(&runtime_starts[side], runtime_outputs[side]) :
                runtime_outputs[side] == -1 && bq_retirement_runtime_absent(&runtime_starts[side]);
        }
        if (ok) row.code_eligible = trusted->compiler_eligible && trusted->code_obligation &&
                                    row.side[0].code_bytes > 0;
        if (ok) ok = bq_retirement_correctness_row(gate, &row);
    }
    if (starts && runtime_starts)
        for (unsigned side = 0; side < 2; side += 1)
        {
            if (starts[side].process_state != BQ_RETIREMENT_ARTIFACT_RUNNING)
                bq_retirement_artifact_abort(&starts[side]);
            if (runtime_starts[side].state != BQ_RETIREMENT_RUNTIME_RUNNING)
                bq_retirement_runtime_abort(&runtime_starts[side]);
        }
    if (gate && !ok) gate->failed = 1;
    return ok;
}
