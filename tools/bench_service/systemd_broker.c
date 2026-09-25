/* Benchpress systemd authority boundary.
 *
 * The unprivileged CLI sends only an operation, a job/attempt, a fixed stage,
 * and two source identities.  The root socket instance authenticates the peer,
 * checks the durable worker record and stable lease, then constructs every
 * manager argument itself.  No caller argv, path, property, environment or
 * executable is forwarded to systemd.  `self-test` exercises construction and
 * rejection without contacting the manager.
 *
 * This executable is Linux-only and deliberately has no Buster dependency.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BQ_BROKER_SOCKET "/run/buster-bench-systemd-broker/control.sock"
#define BQ_BROKER_QUEUE "/var/lib/buster-bench/queue"
#define BQ_BROKER_LEASE "/var/lib/buster-bench/lease/host.lock"
#define BQ_BROKER_LEASE_RECEIPT "/etc/buster-bench/systemd-broker-lease.identity"
#define BQ_BROKER_WORKSPACES "/var/lib/buster-bench/workspaces"
#define BQ_BROKER_INSTALLED "/opt/buster-bench/installed"
#define BQ_BROKER_SERVICE "/usr/local/libexec/buster-bench-service"
#define BQ_BROKER_BUILD "/usr/local/libexec/buster-bench-build"
#define BQ_BROKER_THROUGHPUT "/usr/local/libexec/buster-bench-throughput"
#define BQ_BROKER_RUN "/usr/bin/systemd-run"
#define BQ_BROKER_CTL "/usr/bin/systemctl"
#define BQ_BROKER_MAGIC 0x42515344u
#define BQ_BROKER_MAX_ARGS 96u
#define BQ_BROKER_TEXT 2048u

enum { BQ_BROKER_START = 1, BQ_BROKER_SIGNAL = 2 };
enum { BQ_BROKER_OUTER = 0, BQ_BROKER_BASE_GENERATE = 1, BQ_BROKER_BASE_BUILD = 2,
       BQ_BROKER_CANDIDATE_GENERATE = 3, BQ_BROKER_CANDIDATE_BUILD = 4, BQ_BROKER_THROUGHPUT_STAGE = 5 };
enum { BQ_BROKER_TERM = 1, BQ_BROKER_KILL = 2 };

typedef struct BqBrokerRequest
{
    uint32_t magic;
    uint32_t version;
    uint32_t operation;
    uint32_t stage;
    uint32_t signal_number;
    uint32_t reserved;
    uint64_t job;
    uint64_t attempt;
    char base[65];
    char candidate[65];
} BqBrokerRequest;

typedef struct BqBrokerCommand
{
    char const* argv[BQ_BROKER_MAX_ARGS + 1];
    char text[BQ_BROKER_MAX_ARGS][BQ_BROKER_TEXT];
    unsigned count;
    bool valid;
} BqBrokerCommand;

typedef struct BqBrokerFrame
{
    uint32_t kind;
    uint32_t length;
    unsigned char bytes[4096];
} BqBrokerFrame;

enum { BQ_BROKER_STDOUT = 1, BQ_BROKER_STDERR = 2, BQ_BROKER_STATUS = 3 };

typedef struct BqBrokerPaths
{
    char unit[128];
    char parent[128];
    char attempt[512];
    char result[512];
    char base_source[512];
    char base_build[512];
    char candidate_source[512];
    char candidate_build[512];
    char candidate_staging[512];
    char throughput_output[512];
} BqBrokerPaths;

static char const* const bq_broker_stages[] = {
    "", "base-generate", "base-build", "candidate-generate", "candidate-build", "throughput"
};

static bool bq_broker_format(char* output, size_t capacity, char const* format, ...)
{
    va_list args;
    va_start(args, format);
    int count = vsnprintf(output, capacity, format, args);
    va_end(args);
    bool ok = count >= 0 && (size_t)count < capacity;
    return ok;
}

static bool bq_broker_decimal(char const* text, uint64_t* result)
{
    uint64_t value = 0;
    bool ok = text && text[0] >= '1' && text[0] <= '9';
    for (size_t index = 0; ok && text[index]; index += 1)
    {
        unsigned digit = (unsigned)(text[index] - '0');
        ok = digit <= 9 && value <= (UINT64_MAX - digit) / 10;
        if (ok) value = value * 10 + digit;
    }
    if (ok) *result = value;
    return ok;
}

static bool bq_broker_revision(char const value[65])
{
    size_t length = strnlen(value, 65);
    bool ok = (length == 40 || length == 64) && length < 65;
    for (size_t index = 0; ok && index < length; index += 1)
        ok = (value[index] >= '0' && value[index] <= '9') ||
             (value[index] >= 'a' && value[index] <= 'f');
    return ok;
}

static bool bq_broker_request_valid(BqBrokerRequest const* request)
{
    bool start = request->operation == BQ_BROKER_START;
    bool signal = request->operation == BQ_BROKER_SIGNAL;
    bool ok = request->magic == BQ_BROKER_MAGIC && request->version == 1 && request->reserved == 0 &&
              (start || signal) && request->stage <= BQ_BROKER_THROUGHPUT_STAGE &&
              request->job != 0 && request->attempt != 0;
    if (ok && start)
        ok = request->signal_number == 0 && bq_broker_revision(request->base) &&
             bq_broker_revision(request->candidate);
    if (ok && signal)
    {
        ok = (request->signal_number == BQ_BROKER_TERM || request->signal_number == BQ_BROKER_KILL);
        for (unsigned index = 0; ok && index < sizeof(request->base); index += 1)
            ok = request->base[index] == 0 && request->candidate[index] == 0;
    }
    return ok;
}

static bool bq_broker_paths(BqBrokerRequest const* request, BqBrokerPaths* paths)
{
    memset(paths, 0, sizeof(*paths));
    bool ok = bq_broker_format(paths->parent, sizeof(paths->parent), "buster-bench-%" PRIu64 "-%" PRIu64 ".service",
                               request->job, request->attempt);
    if (ok && request->stage == BQ_BROKER_OUTER)
        ok = bq_broker_format(paths->unit, sizeof(paths->unit), "%s", paths->parent);
    else if (ok)
        ok = bq_broker_format(paths->unit, sizeof(paths->unit), "buster-bench-%" PRIu64 "-%" PRIu64 "-%s.service",
                              request->job, request->attempt, bq_broker_stages[request->stage]);
    if (ok) ok = bq_broker_format(paths->attempt, sizeof(paths->attempt), "%s/job-%" PRIu64 "-attempt-%" PRIu64,
                                  BQ_BROKER_WORKSPACES, request->job, request->attempt);
    if (ok) ok = bq_broker_format(paths->result, sizeof(paths->result), "%s/results/job-%" PRIu64 "-attempt-%" PRIu64,
                                  BQ_BROKER_WORKSPACES, request->job, request->attempt);
    if (ok) ok = bq_broker_format(paths->base_source, sizeof(paths->base_source), "%s/base/source", paths->attempt);
    if (ok) ok = bq_broker_format(paths->base_build, sizeof(paths->base_build), "%s/base/build", paths->attempt);
    if (ok) ok = bq_broker_format(paths->candidate_source, sizeof(paths->candidate_source), "%s/candidate/source", paths->attempt);
    if (ok) ok = bq_broker_format(paths->candidate_build, sizeof(paths->candidate_build), "%s/candidate/build", paths->attempt);
    if (ok) ok = bq_broker_format(paths->candidate_staging, sizeof(paths->candidate_staging), "%s/candidate/staging", paths->attempt);
    if (ok) ok = bq_broker_format(paths->throughput_output, sizeof(paths->throughput_output), "%s/throughput-results",
                                  paths->candidate_staging);
    return ok;
}

static void bq_broker_add(BqBrokerCommand* command, char const* literal)
{
    if (command->valid && command->count < BQ_BROKER_MAX_ARGS)
        command->argv[command->count++] = literal;
    else
        command->valid = false;
}

static void bq_broker_add_format(BqBrokerCommand* command, char const* format, ...)
{
    if (command->valid && command->count < BQ_BROKER_MAX_ARGS)
    {
        va_list args;
        va_start(args, format);
        int count = vsnprintf(command->text[command->count], BQ_BROKER_TEXT, format, args);
        va_end(args);
        if (count >= 0 && (unsigned)count < BQ_BROKER_TEXT)
        {
            command->argv[command->count] = command->text[command->count];
            command->count += 1;
        }
        else
            command->valid = false;
    }
    else
    {
        command->valid = false;
    }
}

static void bq_broker_common_sandbox(BqBrokerCommand* command)
{
    static char const* const properties[] = {
        "--property=KillMode=control-group", "--property=SendSIGKILL=yes", "--property=TimeoutStopSec=10s",
        "--property=NoNewPrivileges=yes", "--property=PrivateTmp=yes", "--property=PrivateDevices=yes",
        "--property=ProtectSystem=strict", "--property=RestrictSUIDSGID=yes", "--property=ProtectHome=yes",
        "--property=ProtectControlGroups=yes", "--property=ProtectKernelTunables=yes",
        "--property=ProtectKernelModules=yes", "--property=ProtectKernelLogs=yes", "--property=ProtectClock=yes",
        "--property=ProtectHostname=yes", "--property=ProtectProc=invisible", "--property=LockPersonality=yes",
        "--property=MemoryDenyWriteExecute=yes", "--property=RemoveIPC=yes", "--property=KeyringMode=private",
        "--property=RestrictNamespaces=yes", "--property=RestrictRealtime=yes",
        "--property=RestrictAddressFamilies=AF_UNIX", "--property=SystemCallArchitectures=native",
        "--property=SystemCallFilter=@system-service", "--property=SystemCallErrorNumber=EPERM"
    };
    for (unsigned index = 0; index < sizeof(properties) / sizeof(properties[0]); index += 1)
        bq_broker_add(command, properties[index]);
}

static bool bq_broker_command(BqBrokerRequest const* request, BqBrokerCommand* command)
{
    BqBrokerPaths paths;
    bool ok = bq_broker_request_valid(request) && bq_broker_paths(request, &paths);
    memset(command, 0, sizeof(*command));
    command->valid = ok;
    if (ok && request->operation == BQ_BROKER_START)
    {
        bq_broker_add(command, BQ_BROKER_RUN);
        bq_broker_add(command, "--quiet");
        bq_broker_add(command, "--wait");
        if (request->stage != BQ_BROKER_OUTER) bq_broker_add(command, "--pipe");
        bq_broker_add(command, "--service-type=exec");
        bq_broker_add(command, "--setenv=PATH=/usr/bin:/bin");
        bq_broker_add(command, "--setenv=LC_ALL=C");
        bq_broker_add_format(command, "--unit=%s", paths.unit);
        bq_broker_add(command, "--slice=buster-bench.slice");
        bq_broker_add(command, "--property=AllowedCPUs=2");
        bq_broker_add(command, "--property=MemoryMax=8589934592");
        bq_broker_add(command, "--property=MemorySwapMax=0");
        bq_broker_add(command, "--property=TasksMax=256");
        bq_broker_add(command, "--property=RuntimeMaxSec=3600000000us");
        if (request->stage != BQ_BROKER_OUTER)
        {
            bq_broker_add_format(command, "--property=PartOf=%s", paths.parent);
            bq_broker_add_format(command, "--property=BindsTo=%s", paths.parent);
            bq_broker_add_format(command, "--property=After=%s", paths.parent);
            bq_broker_add(command, "--collect");
            if (request->stage <= BQ_BROKER_BASE_BUILD)
            {
                bq_broker_add(command, "--uid=buster-bench");
                bq_broker_add(command, "--gid=buster-bench");
                bq_broker_add(command, "--property=UMask=0077");
            }
            else
            {
                bq_broker_add(command, "--uid=buster-bench-candidate");
                bq_broker_add(command, "--gid=buster-bench-candidate");
                bq_broker_add(command, "--property=UMask=0007");
            }
        }
        else
        {
            bq_broker_add(command, "--uid=buster-bench");
            bq_broker_add(command, "--gid=buster-bench");
            bq_broker_add(command, "--property=UMask=0077");
        }
        bq_broker_common_sandbox(command);
        if (request->stage == BQ_BROKER_OUTER)
        {
            bq_broker_add_format(command, "--property=InaccessiblePaths=%s %s", BQ_BROKER_QUEUE, BQ_BROKER_LEASE);
            bq_broker_add_format(command, "--property=ReadOnlyPaths=%s", BQ_BROKER_INSTALLED);
            bq_broker_add_format(command, "--property=ReadWritePaths=%s", BQ_BROKER_WORKSPACES);
        }
        else
        {
            if (request->stage <= BQ_BROKER_BASE_BUILD)
            {
                bq_broker_add_format(command, "--property=ReadOnlyPaths=%s %s", paths.base_source,
                                     paths.candidate_source);
                bq_broker_add_format(command, "--property=ReadWritePaths=%s", paths.base_build);
                bq_broker_add_format(command, "--working-directory=%s", paths.base_source);
            }
            else if (request->stage <= BQ_BROKER_CANDIDATE_BUILD)
            {
                bq_broker_add_format(command, "--property=ReadOnlyPaths=%s %s %s", paths.base_source,
                                     paths.base_build, paths.candidate_source);
                bq_broker_add_format(command, "--property=ReadWritePaths=%s", paths.candidate_staging);
                bq_broker_add_format(command, "--working-directory=%s", paths.candidate_source);
            }
            else
            {
                bq_broker_add_format(command, "--property=ReadOnlyPaths=%s %s %s %s", paths.base_source,
                                     paths.base_build, paths.candidate_source, paths.candidate_build);
                bq_broker_add_format(command, "--property=ReadWritePaths=%s", paths.throughput_output);
                bq_broker_add_format(command, "--working-directory=%s", paths.candidate_source);
            }
            bq_broker_add_format(command, "--property=InaccessiblePaths=%s %s %s", BQ_BROKER_QUEUE,
                                 BQ_BROKER_LEASE, paths.result);
        }
        bq_broker_add(command, "--property=PrivateNetwork=yes");
        if (request->stage == BQ_BROKER_OUTER)
        {
            bq_broker_add(command, BQ_BROKER_SERVICE);
            bq_broker_add(command, "worker-unit");
            bq_broker_add(command, BQ_BROKER_LEASE);
            bq_broker_add_format(command, "%" PRIu64, request->job);
            bq_broker_add_format(command, "%" PRIu64, request->attempt);
            bq_broker_add(command, "validate-buster-v1");
            bq_broker_add(command, BQ_BROKER_WORKSPACES);
            bq_broker_add(command, request->base);
            bq_broker_add(command, request->candidate);
            bq_broker_add_format(command, "%s", paths.result);
        }
        else if (request->stage == BQ_BROKER_THROUGHPUT_STAGE)
        {
            bq_broker_add(command, BQ_BROKER_THROUGHPUT);
            bq_broker_add(command, "run");
            bq_broker_add(command, "--baseline");
            bq_broker_add_format(command, "%s/Release/ide", paths.base_build);
            bq_broker_add(command, "--candidate");
            bq_broker_add_format(command, "%s/Release/ide", paths.candidate_build);
            bq_broker_add(command, "--output");
            bq_broker_add_format(command, "%s", paths.throughput_output);
            bq_broker_add(command, "--baseline-id");
            bq_broker_add(command, request->base);
            bq_broker_add(command, "--candidate-id");
            bq_broker_add(command, request->candidate);
            bq_broker_add(command, "--profile");
            bq_broker_add(command, "smoke");
            bq_broker_add(command, "--mode");
            bq_broker_add(command, "all");
            bq_broker_add(command, "--pairs");
            bq_broker_add(command, "1");
            bq_broker_add(command, "--warmups");
            bq_broker_add(command, "1");
            bq_broker_add(command, "--no-guard");
        }
        else
        {
            char const* directory = request->stage <= BQ_BROKER_BASE_BUILD ? paths.base_build : paths.candidate_staging;
            bq_broker_add(command, BQ_BROKER_BUILD);
            bq_broker_add(command, request->stage == BQ_BROKER_BASE_GENERATE ||
                                   request->stage == BQ_BROKER_CANDIDATE_GENERATE ? "generate" : "build");
            bq_broker_add(command, "--build-directory");
            bq_broker_add_format(command, "%s", directory);
            bq_broker_add(command, "--config");
            bq_broker_add(command, "Release");
            if (request->stage == BQ_BROKER_BASE_GENERATE || request->stage == BQ_BROKER_CANDIDATE_GENERATE)
            {
                static char const* const options[] = {"--cc", "clang", "--no-include-tests", "--no-developer-targets",
                    "--no-check-optional-warnings", "--no-fuzz", "--no-sanitize", "--no-time-trace",
                    "--no-instrument", "--no-lto"};
                for (unsigned index = 0; index < sizeof(options) / sizeof(options[0]); index += 1)
                    bq_broker_add(command, options[index]);
            }
            else
            {
                bq_broker_add(command, "-t");
                bq_broker_add(command, "ide");
                bq_broker_add(command, "--");
                bq_broker_add(command, "-j1");
            }
        }
    }
    else if (ok && request->operation == BQ_BROKER_SIGNAL)
    {
        bq_broker_add(command, BQ_BROKER_CTL);
        bq_broker_add(command, "kill");
        bq_broker_add(command, "--kill-whom=all");
        bq_broker_add(command, request->signal_number == BQ_BROKER_TERM ? "--signal=TERM" : "--signal=KILL");
        bq_broker_add_format(command, "%s", paths.unit);
    }
    ok = ok && command->valid;
    if (ok) command->argv[command->count] = NULL;
    return ok;
}

static int bq_broker_open_directory(char const* path)
{
    int current = path && path[0] == '/' ? open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    size_t offset = 1;
    while (current >= 0 && path[offset])
    {
        size_t end = offset;
        while (path[end] && path[end] != '/') end += 1;
        char name[256];
        size_t length = end - offset;
        bool valid = length > 0 && length < sizeof(name) &&
                     !(length == 1 && path[offset] == '.') &&
                     !(length == 2 && path[offset] == '.' && path[offset + 1] == '.');
        int next = -1;
        if (valid)
        {
            memcpy(name, path + offset, length);
            name[length] = 0;
            next = openat(current, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        close(current);
        current = next;
        offset = path[end] ? end + 1 : end;
    }
    return current;
}

static bool bq_broker_directory(char const* path, uid_t owner, bool writable)
{
    int descriptor = bq_broker_open_directory(path);
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
              info.st_uid == owner && (info.st_mode & 0002) == 0;
    if (ok && !writable) ok = (info.st_mode & 0222) == 0;
    if (descriptor >= 0) close(descriptor);
    return ok;
}

static bool bq_broker_regular(char const* parent, char const* name, uid_t owner,
                              bool writable, unsigned char* output, size_t capacity, size_t* size)
{
    int directory = bq_broker_open_directory(parent);
    int descriptor = directory >= 0 ? openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK) : -1;
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == owner && info.st_nlink == 1 && (info.st_mode & 0022) == 0 &&
              info.st_size >= 0 && (uint64_t)info.st_size <= capacity;
    if (ok && !writable) ok = (info.st_mode & 0200) == 0;
    size_t used = 0;
    while (ok && used < (size_t)info.st_size)
    {
        ssize_t count = read(descriptor, output + used, (size_t)info.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) used += (size_t)count;
    }
    if (ok) *size = used;
    if (descriptor >= 0) close(descriptor);
    if (directory >= 0) close(directory);
    return ok;
}

static uint64_t bq_broker_u64(unsigned char const* bytes)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; index += 1) value |= (uint64_t)bytes[index] << (index * 8);
    return value;
}

static bool bq_broker_boot_matches(unsigned char const* record, size_t length, size_t offset)
{
    char boot[64] = {0};
    int descriptor = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t count = descriptor >= 0 ? read(descriptor, boot, sizeof(boot) - 1) : -1;
    if (descriptor >= 0) close(descriptor);
    bool ok = count >= 36 && offset + 37 <= length && boot[36] == '\n' &&
              !memcmp(record + offset, boot, 36) && record[offset + 36] == 0;
    return ok;
}

static bool bq_broker_worker_record(BqBrokerRequest const* request, BqBrokerPaths const* paths,
                                    uid_t service_uid, bool instance)
{
    char name[64];
    unsigned char record[544] = {0};
    size_t size = 0;
    bool ok = bq_broker_format(name, sizeof(name), instance ? "worker-instance-%" PRIu64 : "worker-%" PRIu64,
                               request->job) &&
              bq_broker_directory(BQ_BROKER_QUEUE, service_uid, true) &&
              bq_broker_regular(BQ_BROKER_QUEUE, name, service_uid, true, record, sizeof(record), &size);
    if (ok)
    {
        ok = size == (instance ? 544u : 232u) &&
             !memcmp(record, instance ? "BQINSTANCE000002" : "BQWORKER00000001", 16) &&
             bq_broker_u64(record + 16) == request->job &&
             bq_broker_u64(record + 24) == request->attempt &&
             bq_broker_boot_matches(record, size, 96) &&
             record[132] == 0 && record[231] == 0 &&
             !memcmp(record + 136, paths->parent, strlen(paths->parent) + 1);
        if (ok && instance)
        {
            ok = record[264] == 0 && record[479] == 0 && record[543] == 0 &&
                 strnlen((char const*)(record + 232), 33) == 32 &&
                 strnlen((char const*)(record + 288), 192) < 192;
            for (unsigned index = 232; ok && index < 264; index += 1)
                ok = (record[index] >= '0' && record[index] <= '9') ||
                     (record[index] >= 'a' && record[index] <= 'f');
        }
    }
    return ok;
}

static bool bq_broker_lease_held(uid_t service_uid)
{
    unsigned char receipt[128] = {0};
    size_t receipt_size = 0;
    bool receipt_ok = bq_broker_directory("/etc/buster-bench", 0, false) &&
                      bq_broker_regular("/etc/buster-bench", "systemd-broker-lease.identity", 0, false,
                                        receipt, sizeof(receipt) - 1, &receipt_size);
    uint64_t device = 0, inode = 0;
    if (receipt_ok)
    {
        receipt[receipt_size] = 0;
        char expected[128];
        receipt_ok = sscanf((char const*)receipt, "device=%" SCNu64 "\ninode=%" SCNu64,
                            &device, &inode) == 2 && device != 0 && inode != 0 &&
                     bq_broker_format(expected, sizeof(expected), "device=%" PRIu64 "\ninode=%" PRIu64 "\n",
                                      device, inode) && !strcmp((char const*)receipt, expected);
    }
    int directory = bq_broker_open_directory("/var/lib/buster-bench/lease");
    int descriptor = directory >= 0 ? openat(directory, "host.lock", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    bool ok = receipt_ok && descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == service_uid && info.st_nlink == 1 && (info.st_mode & 0777) == 0600 &&
              (uint64_t)info.st_dev == device && (uint64_t)info.st_ino == inode;
    if (ok)
    {
        int result = flock(descriptor, LOCK_EX | LOCK_NB);
        ok = result == -1 && (errno == EWOULDBLOCK || errno == EAGAIN);
        if (result == 0) flock(descriptor, LOCK_UN);
    }
    if (descriptor >= 0) close(descriptor);
    if (directory >= 0) close(directory);
    return ok;
}

static bool bq_broker_manifest(BqBrokerRequest const* request, BqBrokerPaths const* paths,
                               uid_t service_uid, bool candidate)
{
    char installed[512];
    char const* revision = candidate ? request->candidate : request->base;
    char const* workspace = candidate ? paths->candidate_source : paths->base_source;
    unsigned char source_bytes[65536], copy_bytes[65536];
    size_t source_size = 0, copy_size = 0;
    bool ok = bq_broker_format(installed, sizeof(installed), "%s/sources/%s", BQ_BROKER_INSTALLED, revision) &&
              bq_broker_directory(installed, 0, false) &&
              bq_broker_regular(installed, "source.manifest", 0, false, source_bytes,
                                sizeof(source_bytes), &source_size) &&
              bq_broker_directory(workspace, service_uid, false) &&
              bq_broker_regular(workspace, ".source-manifest", service_uid, false, copy_bytes,
                                sizeof(copy_bytes), &copy_size);
    char header[128];
    if (ok)
        ok = bq_broker_format(header, sizeof(header),
                              "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n", revision) &&
             source_size >= strlen(header) && !memcmp(source_bytes, header, strlen(header)) &&
             source_size == copy_size && !memcmp(source_bytes, copy_bytes, source_size);
    return ok;
}

static bool bq_broker_installed_binary(char const* path)
{
    char const* slash = strrchr(path, '/');
    char parent[256];
    bool ok = slash && (size_t)(slash - path) < sizeof(parent);
    if (ok)
    {
        memcpy(parent, path, (size_t)(slash - path));
        parent[slash - path] = 0;
        int directory = bq_broker_open_directory(parent);
        int descriptor = directory >= 0 ? openat(directory, slash + 1, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
        struct stat info = {0};
        ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
             info.st_uid == 0 && info.st_nlink == 1 && (info.st_mode & 0022) == 0 &&
             (info.st_mode & 0111) != 0;
        if (descriptor >= 0) close(descriptor);
        if (directory >= 0) close(directory);
    }
    return ok;
}

static bool bq_broker_state(BqBrokerRequest const* request, uid_t service_uid)
{
    BqBrokerPaths paths;
    bool ok = bq_broker_paths(request, &paths) &&
              bq_broker_directory(BQ_BROKER_WORKSPACES, service_uid, true) &&
              bq_broker_directory(paths.result, service_uid, true) &&
              bq_broker_worker_record(request, &paths, service_uid, false) &&
              bq_broker_lease_held(service_uid);
    if (ok && (request->stage != BQ_BROKER_OUTER || request->operation == BQ_BROKER_SIGNAL))
        ok = bq_broker_worker_record(request, &paths, service_uid, true);
    if (ok && request->operation == BQ_BROKER_START)
    {
        ok = bq_broker_manifest(request, &paths, service_uid, false) &&
             bq_broker_manifest(request, &paths, service_uid, true) &&
             bq_broker_installed_binary(BQ_BROKER_SERVICE) &&
             bq_broker_installed_binary(BQ_BROKER_BUILD) &&
             bq_broker_installed_binary(BQ_BROKER_THROUGHPUT);
        if (ok && request->stage == BQ_BROKER_CANDIDATE_GENERATE)
            ok = bq_broker_directory(paths.candidate_staging, service_uid, true);
    }
    return ok;
}

static bool bq_broker_unit_from_text(char const* unit, BqBrokerRequest* request)
{
    BqBrokerRequest candidate = {.magic = BQ_BROKER_MAGIC, .version = 1, .operation = BQ_BROKER_SIGNAL};
    char const* at = unit && !strncmp(unit, "buster-bench-", 13) ? unit + 13 : NULL;
    bool ok = at != NULL;
    if (ok)
    {
        errno = 0;
        char* end = NULL;
        unsigned long long parsed = strtoull(at, &end, 10);
        ok = errno == 0 && end > at && *end == '-' && parsed != 0;
        if (ok)
        {
            candidate.job = (uint64_t)parsed;
            at = end + 1;
            errno = 0;
            parsed = strtoull(at, &end, 10);
            ok = errno == 0 && end > at && parsed != 0;
            if (ok) candidate.attempt = (uint64_t)parsed;
        }
    }
    BqBrokerPaths paths;
    bool found = false;
    for (unsigned stage = 0; ok && !found && stage <= BQ_BROKER_THROUGHPUT_STAGE; stage += 1)
    {
        candidate.stage = stage;
        found = bq_broker_paths(&candidate, &paths) && !strcmp(paths.unit, unit);
    }
    if (ok && found) *request = candidate;
    return ok && found;
}

static char const* bq_broker_field(char const* text, char const* name)
{
    size_t length = strlen(name);
    char const* at = text;
    char const* result = NULL;
    while (at && *at && !result)
    {
        char const* end = strchr(at, '\n');
        size_t available = end ? (size_t)(end - at) : strlen(at);
        if (available > length && !strncmp(at, name, length) && at[length] == '=') result = at + length + 1;
        else at = end ? end + 1 : NULL;
    }
    return result;
}

static bool bq_broker_field_equals(char const* text, char const* name, char const* expected)
{
    char const* value = bq_broker_field(text, name);
    size_t length = strlen(expected);
    char const* end = value ? strchr(value, '\n') : NULL;
    size_t available = end ? (size_t)(end - value) : value ? strlen(value) : 0;
    bool ok = value && available == length && !memcmp(value, expected, length);
    return ok;
}

static bool bq_broker_exec_path(char const* text, char const* expected)
{
    char const* value = bq_broker_field(text, "ExecStart");
    size_t length = strlen(expected);
    bool ok = value && !strncmp(value, "{ path=", 7) &&
              !strncmp(value + 7, expected, length) &&
              !strncmp(value + 7 + length, " ;", 2);
    return ok;
}

static bool bq_broker_field_has_unit(char const* text, char const* name, char const* unit)
{
    char const* value = bq_broker_field(text, name);
    char const* end = value ? strchr(value, '\n') : NULL;
    if (value && !end) end = value + strlen(value);
    size_t length = strlen(unit);
    bool found = false;
    while (value && end && value < end && !found)
    {
        while (value < end && *value == ' ') value += 1;
        char const* next = value;
        while (next < end && *next != ' ') next += 1;
        found = (size_t)(next - value) == length && !memcmp(value, unit, length);
        value = next;
    }
    return found;
}

static uint64_t bq_broker_now_milliseconds(void)
{
    struct timespec now = {0};
    bool ok = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    uint64_t result = ok ? (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000 : UINT64_MAX;
    return result;
}

static bool bq_broker_show(char const* unit, char output[8192])
{
    static char const* const properties[] = {"Id", "LoadState", "Slice", "InvocationID", "ControlGroup",
        "User", "Group", "ExecStart", "PartOf", "BindsTo", "After", "AllowedCPUs", "MemoryMax",
        "MemorySwapMax", "TasksMax", "RuntimeMaxUSec", "NoNewPrivileges", "ProtectSystem",
        "PrivateNetwork"};
    char options[sizeof(properties) / sizeof(properties[0])][64];
    char const* arguments[sizeof(properties) / sizeof(properties[0]) + 5] = {BQ_BROKER_CTL, "show", "--no-pager"};
    bool ok = true;
    unsigned count = 3;
    for (unsigned index = 0; ok && index < sizeof(properties) / sizeof(properties[0]); index += 1)
    {
        ok = bq_broker_format(options[index], sizeof(options[index]), "--property=%s", properties[index]);
        if (ok) arguments[count++] = options[index];
    }
    arguments[count++] = unit;
    arguments[count] = NULL;
    int pipefd[2] = {-1, -1};
    if (ok) ok = pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null >= 0) dup2(null, STDERR_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        if (null >= 0) close(null);
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        setenv("LC_ALL", "C", 1);
        execv(BQ_BROKER_CTL, (char* const*)arguments);
        _exit(127);
    }
    if (pipefd[1] >= 0) close(pipefd[1]);
    ok = ok && child > 0;
    uint64_t deadline = bq_broker_now_milliseconds() + 5000;
    size_t used = 0;
    bool eof = false;
    while (ok && !eof && bq_broker_now_milliseconds() < deadline)
    {
        struct pollfd ready = {.fd = pipefd[0], .events = POLLIN | POLLHUP};
        uint64_t now = bq_broker_now_milliseconds();
        int remaining = now < deadline ? (int)(deadline - now) : 0;
        int polled = poll(&ready, 1, remaining);
        if (polled < 0 && errno == EINTR) continue;
        ok = polled > 0 && used + 1 < 8192;
        if (ok)
        {
            ssize_t got = read(pipefd[0], output + used, 8191 - used);
            if (got < 0 && errno == EAGAIN) continue;
            ok = got >= 0;
            if (ok && got == 0) eof = true;
            if (ok && got > 0) used += (size_t)got;
        }
    }
    if (pipefd[0] >= 0) close(pipefd[0]);
    output[used] = 0;
    int status = 0;
    if (child > 0)
    {
        if (!ok || !eof) kill(child, SIGKILL);
        bool reaped = false;
        while (!reaped && bq_broker_now_milliseconds() < deadline)
        {
            pid_t result = waitpid(child, &status, WNOHANG);
            reaped = result == child;
            if (result < 0 && errno != EINTR) break;
            if (!reaped) poll(NULL, 0, 10);
        }
        if (!reaped)
        {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            ok = false;
        }
    }
    ok = ok && eof && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return ok;
}

static bool bq_broker_signal_identity(BqBrokerRequest const* request)
{
    BqBrokerPaths paths;
    char cgroup[256];
    char output[8192];
    bool ok = bq_broker_paths(request, &paths) &&
              bq_broker_format(cgroup, sizeof(cgroup), "/buster-bench.slice/%s", paths.unit) &&
              bq_broker_show(paths.unit, output) &&
              bq_broker_field_equals(output, "Id", paths.unit) &&
              bq_broker_field_equals(output, "LoadState", "loaded") &&
              bq_broker_field_equals(output, "Slice", "buster-bench.slice") &&
              bq_broker_field_equals(output, "NoNewPrivileges", "yes") &&
              bq_broker_field_equals(output, "ProtectSystem", "strict") &&
              bq_broker_field_equals(output, "PrivateNetwork", "yes") &&
              bq_broker_field_equals(output, "AllowedCPUs", "2") &&
              bq_broker_field_equals(output, "MemoryMax", "8589934592") &&
              bq_broker_field_equals(output, "MemorySwapMax", "0") &&
              bq_broker_field_equals(output, "TasksMax", "256") &&
              (bq_broker_field_equals(output, "RuntimeMaxUSec", "3600000000") ||
               bq_broker_field_equals(output, "RuntimeMaxUSec", "1h")) &&
              bq_broker_field_equals(output, "ControlGroup", cgroup);
    if (ok && request->stage == BQ_BROKER_OUTER)
    {
        unsigned char record[544];
        char name[64];
        size_t size = 0;
        struct passwd* service = getpwnam("buster-bench");
        ok = service && bq_broker_format(name, sizeof(name), "worker-instance-%" PRIu64, request->job) &&
             bq_broker_regular(BQ_BROKER_QUEUE, name, service->pw_uid,
                               true, record, sizeof(record), &size) && size == sizeof(record) &&
             !memcmp(record, "BQINSTANCE000002", 16) &&
             bq_broker_u64(record + 16) == request->job &&
             bq_broker_u64(record + 24) == request->attempt &&
             record[264] == 0 && record[479] == 0 &&
             strnlen((char const*)(record + 232), 33) == 32 &&
             strnlen((char const*)(record + 288), 192) < 192 &&
             bq_broker_field_equals(output, "User", "buster-bench") &&
             bq_broker_field_equals(output, "Group", "buster-bench") &&
             bq_broker_exec_path(output, BQ_BROKER_SERVICE) &&
             bq_broker_field_equals(output, "InvocationID", (char const*)(record + 232)) &&
             bq_broker_field_equals(output, "ControlGroup", (char const*)(record + 288));
    }
    else if (ok)
    {
        char const* identity = request->stage <= BQ_BROKER_BASE_BUILD ? "buster-bench" : "buster-bench-candidate";
        char const* executable = request->stage == BQ_BROKER_THROUGHPUT_STAGE ?
                                 BQ_BROKER_THROUGHPUT : BQ_BROKER_BUILD;
        ok = bq_broker_field_equals(output, "User", identity) &&
             bq_broker_field_equals(output, "Group", identity) &&
             bq_broker_exec_path(output, executable) &&
             bq_broker_field_has_unit(output, "PartOf", paths.parent) &&
             bq_broker_field_has_unit(output, "BindsTo", paths.parent) &&
             bq_broker_field_has_unit(output, "After", paths.parent);
    }
    return ok;
}

static bool bq_broker_send_frame(int connection, uint32_t kind, void const* data, uint32_t length)
{
    BqBrokerFrame frame = {.kind = kind, .length = length};
    bool ok = length <= sizeof(frame.bytes);
    if (ok)
    {
        if (length) memcpy(frame.bytes, data, length);
        size_t size = offsetof(BqBrokerFrame, bytes) + length;
        ok = send(connection, &frame, size, MSG_NOSIGNAL) == (ssize_t)size;
    }
    return ok;
}

static bool bq_broker_write_all(int descriptor, unsigned char const* bytes, size_t length)
{
    size_t written = 0;
    bool ok = true;
    while (ok && written < length)
    {
        ssize_t count = write(descriptor, bytes + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) written += (size_t)count;
    }
    return ok;
}

static int bq_broker_execute(BqBrokerCommand const* command, int connection, bool signal_operation)
{
    int output[2] = {-1, -1}, error_pipe[2] = {-1, -1};
    bool ok = pipe2(output, O_CLOEXEC) == 0;
    if (ok) ok = pipe2(error_pipe, O_CLOEXEC) == 0;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        int null = open("/dev/null", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ok = null >= 0 && dup2(null, STDIN_FILENO) == STDIN_FILENO &&
             dup2(output[1], STDOUT_FILENO) == STDOUT_FILENO &&
             dup2(error_pipe[1], STDERR_FILENO) == STDERR_FILENO;
        if (null >= 0) close(null);
        close(output[0]);
        close(output[1]);
        close(error_pipe[0]);
        close(error_pipe[1]);
        ok = ok && chdir("/") == 0 && clearenv() == 0 &&
             setenv("PATH", "/usr/bin:/bin", 1) == 0 && setenv("LC_ALL", "C", 1) == 0;
        umask(077);
        if (ok) execv(command->argv[0], (char* const*)command->argv);
        _exit(127);
    }
    if (output[1] >= 0) close(output[1]);
    if (error_pipe[1] >= 0) close(error_pipe[1]);
    output[1] = -1;
    error_pipe[1] = -1;
    ok = ok && child > 0;
    if (ok) ok = fcntl(output[0], F_SETFL, fcntl(output[0], F_GETFL) | O_NONBLOCK) == 0 &&
                 fcntl(error_pipe[0], F_SETFL, fcntl(error_pipe[0], F_GETFL) | O_NONBLOCK) == 0;
    uint64_t now = bq_broker_now_milliseconds();
    uint64_t deadline = now + (signal_operation ? 5000u : 3700000u);
    uint64_t total = 0;
    int status = 0;
    bool reaped = false;
    while (ok && child > 0 && bq_broker_now_milliseconds() < deadline &&
           (!reaped || output[0] >= 0 || error_pipe[0] >= 0))
    {
        struct pollfd ready[2] = {{.fd = output[0], .events = POLLIN | POLLHUP},
                                  {.fd = error_pipe[0], .events = POLLIN | POLLHUP}};
        int polled = poll(ready, 2, 100);
        if (polled < 0 && errno == EINTR) continue;
        ok = polled >= 0;
        for (unsigned index = 0; ok && index < 2; index += 1)
        {
            int* descriptor = index == 0 ? &output[0] : &error_pipe[0];
            if (*descriptor >= 0 && (ready[index].revents & (POLLERR | POLLNVAL))) ok = false;
            if (ok && *descriptor >= 0 && (ready[index].revents & (POLLIN | POLLHUP)))
            {
                unsigned char bytes[4096];
                ssize_t count = read(*descriptor, bytes, sizeof(bytes));
                if (count < 0 && errno == EAGAIN) continue;
                ok = count >= 0;
                if (ok && count == 0)
                {
                    close(*descriptor);
                    *descriptor = -1;
                }
                else if (ok)
                {
                    total += (uint64_t)count;
                    ok = total <= 16u * 1024u * 1024u &&
                         bq_broker_send_frame(connection, index == 0 ? BQ_BROKER_STDOUT : BQ_BROKER_STDERR,
                                              bytes, (uint32_t)count);
                }
            }
        }
        if (!reaped)
        {
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) reaped = true;
            else if (result < 0 && errno != EINTR) ok = false;
        }
    }
    if (output[0] >= 0) close(output[0]);
    if (error_pipe[0] >= 0) close(error_pipe[0]);
    if (child > 0 && !reaped)
    {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        ok = false;
    }
    int32_t result = ok && reaped && WIFEXITED(status) ? WEXITSTATUS(status) :
                     ok && reaped && WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 126;
    if (!bq_broker_send_frame(connection, BQ_BROKER_STATUS, &result, sizeof(result))) result = 126;
    return result == 126 ? 1 : 0;
}

static int bq_broker_server(void)
{
    struct ucred peer = {0};
    socklen_t peer_size = sizeof(peer);
    struct passwd* account = getpwnam("buster-bench");
    uid_t service_uid = account ? account->pw_uid : (uid_t)-1;
    account = getpwnam("buster-bench-candidate");
    uid_t candidate_uid = account ? account->pw_uid : (uid_t)-1;
    account = getpwnam("buster-github-runner");
    uid_t runner_uid = account ? account->pw_uid : (uid_t)-1;
    bool ok = geteuid() == 0 && service_uid != (uid_t)-1 && service_uid != 0 &&
              candidate_uid != (uid_t)-1 && candidate_uid != service_uid &&
              runner_uid != (uid_t)-1 && runner_uid != service_uid && runner_uid != candidate_uid &&
              getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) == 0 &&
              peer_size == sizeof(peer) && peer.uid == service_uid;
    BqBrokerRequest request = {0};
    if (ok)
    {
        struct pollfd ready = {.fd = STDIN_FILENO, .events = POLLIN};
        ok = poll(&ready, 1, 5000) == 1;
    }
    struct iovec data = {.iov_base = &request, .iov_len = sizeof(request)};
    struct msghdr message = {.msg_iov = &data, .msg_iovlen = 1};
    ssize_t received = ok ? recvmsg(STDIN_FILENO, &message, 0) : -1;
    ok = ok && received == sizeof(request) && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
         bq_broker_request_valid(&request);
    if (ok) ok = bq_broker_state(&request, service_uid);
    if (ok && request.operation == BQ_BROKER_SIGNAL) ok = bq_broker_signal_identity(&request);
    BqBrokerCommand command;
    if (ok) ok = bq_broker_command(&request, &command);
    struct timeval timeout = {.tv_sec = 5};
    if (ok) ok = setsockopt(STDIN_FILENO, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
    int result = 1;
    if (ok)
        result = bq_broker_execute(&command, STDIN_FILENO, request.operation == BQ_BROKER_SIGNAL);
    else
    {
        int32_t status = 126;
        bq_broker_send_frame(STDIN_FILENO, BQ_BROKER_STATUS, &status, sizeof(status));
    }
    return result;
}

static int bq_broker_client(BqBrokerRequest const* request)
{
    int connection = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    bool ok = connection >= 0 && strlen(BQ_BROKER_SOCKET) < sizeof(address.sun_path);
    if (ok)
    {
        memcpy(address.sun_path, BQ_BROKER_SOCKET, sizeof(BQ_BROKER_SOCKET));
        ok = connect(connection, (struct sockaddr*)&address, sizeof(address)) == 0;
    }
    if (ok) ok = send(connection, request, sizeof(*request), MSG_NOSIGNAL) == sizeof(*request);
    int result = 126;
    bool finished = false;
    while (ok && !finished)
    {
        BqBrokerFrame frame;
        ssize_t received = recv(connection, &frame, sizeof(frame), 0);
        ok = received >= (ssize_t)offsetof(BqBrokerFrame, bytes) &&
             received == (ssize_t)(offsetof(BqBrokerFrame, bytes) + frame.length) &&
             frame.length <= sizeof(frame.bytes);
        if (ok && (frame.kind == BQ_BROKER_STDOUT || frame.kind == BQ_BROKER_STDERR))
            ok = bq_broker_write_all(frame.kind == BQ_BROKER_STDOUT ? STDOUT_FILENO : STDERR_FILENO,
                                     frame.bytes, frame.length);
        else if (ok && frame.kind == BQ_BROKER_STATUS && frame.length == sizeof(int32_t))
        {
            int32_t status;
            memcpy(&status, frame.bytes, sizeof(status));
            ok = status >= 0 && status <= 255;
            if (ok) result = status;
            finished = true;
        }
        else
            ok = false;
    }
    if (connection >= 0) close(connection);
    if (!ok || !finished) result = 126;
    return result;
}

static bool bq_broker_stage(char const* name, uint32_t* stage)
{
    bool found = false;
    for (uint32_t index = 1; !found && index <= BQ_BROKER_THROUGHPUT_STAGE; index += 1)
    {
        if (!strcmp(name, bq_broker_stages[index]))
        {
            *stage = index;
            found = true;
        }
    }
    return found;
}

static bool bq_broker_cli(int argc, char** argv, BqBrokerRequest* request)
{
    memset(request, 0, sizeof(*request));
    request->magic = BQ_BROKER_MAGIC;
    request->version = 1;
    bool ok = false;
    if (argc == 6 && !strcmp(argv[1], "start-outer"))
    {
        request->operation = BQ_BROKER_START;
        ok = bq_broker_decimal(argv[2], &request->job) &&
             bq_broker_decimal(argv[3], &request->attempt) &&
             (strlen(argv[4]) == 40 || strlen(argv[4]) == 64) &&
             (strlen(argv[5]) == 40 || strlen(argv[5]) == 64);
        if (ok)
        {
            memcpy(request->base, argv[4], strlen(argv[4]));
            memcpy(request->candidate, argv[5], strlen(argv[5]));
        }
    }
    else if (argc == 7 && !strcmp(argv[1], "start-stage"))
    {
        request->operation = BQ_BROKER_START;
        ok = bq_broker_decimal(argv[2], &request->job) &&
             bq_broker_decimal(argv[3], &request->attempt) &&
             bq_broker_stage(argv[4], &request->stage) &&
             (strlen(argv[5]) == 40 || strlen(argv[5]) == 64) &&
             (strlen(argv[6]) == 40 || strlen(argv[6]) == 64);
        if (ok)
        {
            memcpy(request->base, argv[5], strlen(argv[5]));
            memcpy(request->candidate, argv[6], strlen(argv[6]));
        }
    }
    else if (argc == 4 && !strcmp(argv[1], "signal"))
    {
        ok = bq_broker_unit_from_text(argv[2], request) &&
             (!strcmp(argv[3], "TERM") || !strcmp(argv[3], "KILL"));
        if (ok) request->signal_number = !strcmp(argv[3], "TERM") ? BQ_BROKER_TERM : BQ_BROKER_KILL;
    }
    ok = ok && bq_broker_request_valid(request);
    return ok;
}

static bool bq_broker_has_argument(BqBrokerCommand const* command, char const* value)
{
    bool found = false;
    for (unsigned index = 0; !found && index < command->count; index += 1)
        found = !strcmp(command->argv[index], value);
    return found;
}

static unsigned bq_broker_argument_count(BqBrokerCommand const* command, char const* value)
{
    unsigned count = 0;
    for (unsigned index = 0; index < command->count; index += 1)
        if (!strcmp(command->argv[index], value)) count += 1;
    return count;
}

static int bq_broker_self_test(void)
{
    BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = 1, .operation = BQ_BROKER_START,
                               .job = 1, .attempt = 2};
    memset(request.base, 'a', 40);
    memset(request.candidate, 'b', 40);
    BqBrokerCommand command;
    unsigned checks = 0;
    bool ok = true;
#define BQ_BROKER_CHECK(condition) do { checks += 1; if (!(condition)) ok = false; } while (0)
    for (uint32_t stage = 0; stage <= BQ_BROKER_THROUGHPUT_STAGE; stage += 1)
    {
        request.stage = stage;
        BQ_BROKER_CHECK(bq_broker_command(&request, &command));
        BqBrokerPaths paths;
        BQ_BROKER_CHECK(bq_broker_paths(&request, &paths));
        char unit_option[256], parent_option[256], result_option[600];
        BQ_BROKER_CHECK(bq_broker_format(unit_option, sizeof(unit_option), "--unit=%s", paths.unit) &&
                        bq_broker_format(parent_option, sizeof(parent_option), "--property=PartOf=%s", paths.parent) &&
                        bq_broker_format(result_option, sizeof(result_option),
                                         "--property=InaccessiblePaths=%s %s %s", BQ_BROKER_QUEUE,
                                         BQ_BROKER_LEASE, paths.result));
        BQ_BROKER_CHECK(command.count < BQ_BROKER_MAX_ARGS && command.argv[0] &&
                        !strcmp(command.argv[0], BQ_BROKER_RUN) && command.argv[command.count] == NULL);
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, unit_option) &&
                        bq_broker_argument_count(&command, unit_option) == 1 &&
                        bq_broker_argument_count(&command, "--property=NoNewPrivileges=yes") == 1);
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--slice=buster-bench.slice") &&
                        bq_broker_has_argument(&command, "--setenv=PATH=/usr/bin:/bin") &&
                        bq_broker_has_argument(&command, "--setenv=LC_ALL=C") &&
                        bq_broker_has_argument(&command, "--property=AllowedCPUs=2") &&
                        bq_broker_has_argument(&command, "--property=NoNewPrivileges=yes") &&
                        bq_broker_has_argument(&command, "--property=ProtectSystem=strict") &&
                        bq_broker_has_argument(&command, "--property=PrivateNetwork=yes"));
        if (stage == BQ_BROKER_OUTER)
        {
            BQ_BROKER_CHECK(bq_broker_has_argument(&command, BQ_BROKER_SERVICE) &&
                            bq_broker_has_argument(&command, "worker-unit") &&
                            bq_broker_has_argument(&command, "--uid=buster-bench") &&
                            bq_broker_has_argument(&command, "--property=UMask=0077") &&
                            !bq_broker_has_argument(&command, "--pipe") &&
                            !bq_broker_has_argument(&command, parent_option));
        }
        else
        {
            BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--pipe") &&
                            bq_broker_has_argument(&command, "--collect") &&
                            bq_broker_has_argument(&command, parent_option) &&
                            bq_broker_has_argument(&command, result_option) &&
                            bq_broker_has_argument(&command, stage == BQ_BROKER_THROUGHPUT_STAGE ?
                                                   BQ_BROKER_THROUGHPUT : BQ_BROKER_BUILD));
            BQ_BROKER_CHECK(bq_broker_has_argument(&command, stage <= BQ_BROKER_BASE_BUILD ?
                                                   "--uid=buster-bench" : "--uid=buster-bench-candidate"));
        }
    }
    request.stage = BQ_BROKER_THROUGHPUT_STAGE + 1;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.stage = BQ_BROKER_OUTER;
    request.base[0] = '/';
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.base[0] = 'a';
    request.candidate[40] = 'c';
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.candidate[40] = 0;
    request.magic = 0;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.magic = BQ_BROKER_MAGIC;
    request.version = 2;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.version = 1;
    request.attempt = 0;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    request.attempt = 2;
    request.operation = BQ_BROKER_SIGNAL;
    request.signal_number = BQ_BROKER_TERM;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    memset(request.base, 0, sizeof(request.base));
    memset(request.candidate, 0, sizeof(request.candidate));
    BQ_BROKER_CHECK(bq_broker_command(&request, &command) &&
                    bq_broker_has_argument(&command, "--signal=TERM") &&
                    !bq_broker_has_argument(&command, "--signal=KILL"));
    request.signal_number = 3;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &command));
    BqBrokerRequest parsed;
    BQ_BROKER_CHECK(bq_broker_unit_from_text("buster-bench-1-2-throughput.service", &parsed) &&
                    parsed.job == 1 && parsed.attempt == 2 && parsed.stage == BQ_BROKER_THROUGHPUT_STAGE);
    BQ_BROKER_CHECK(!bq_broker_unit_from_text("buster-bench-01-2.service", &parsed) &&
                    !bq_broker_unit_from_text("buster-bench-1-2-evil.service", &parsed));
    BQ_BROKER_CHECK(!bq_broker_decimal("0", &parsed.job) &&
                    !bq_broker_decimal("1;id", &parsed.job));
    char* valid_cli[] = {"broker", "start-stage", "1", "2", "throughput", request.base, request.candidate};
    memset(request.base, 'a', 40);
    memset(request.candidate, 'b', 40);
    request.base[40] = 0;
    request.candidate[40] = 0;
    BQ_BROKER_CHECK(bq_broker_cli(7, valid_cli, &parsed) && parsed.stage == BQ_BROKER_THROUGHPUT_STAGE);
    valid_cli[4] = "shell";
    BQ_BROKER_CHECK(!bq_broker_cli(7, valid_cli, &parsed));
    valid_cli[4] = "throughput";
    BQ_BROKER_CHECK(!bq_broker_cli(8, valid_cli, &parsed));
    int connection[2] = {-1, -1};
    bool io_ready = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, connection) == 0;
    BQ_BROKER_CHECK(io_ready);
    if (io_ready)
    {
        BqBrokerCommand test_command = {.argv = {"/usr/bin/printf", "broker-output"}, .count = 2, .valid = true};
        int executed = bq_broker_execute(&test_command, connection[0], false);
        BqBrokerFrame frame;
        ssize_t received = recv(connection[1], &frame, sizeof(frame), 0);
        BQ_BROKER_CHECK(executed == 0 && received == (ssize_t)(offsetof(BqBrokerFrame, bytes) + 13) &&
                        frame.kind == BQ_BROKER_STDOUT && frame.length == 13 &&
                        !memcmp(frame.bytes, "broker-output", 13));
        received = recv(connection[1], &frame, sizeof(frame), 0);
        int32_t status = -1;
        if (received == (ssize_t)(offsetof(BqBrokerFrame, bytes) + sizeof(status)))
            memcpy(&status, frame.bytes, sizeof(status));
        BQ_BROKER_CHECK(frame.kind == BQ_BROKER_STATUS && status == 0);
        test_command.argv[0] = "/usr/bin/false";
        test_command.argv[1] = NULL;
        test_command.count = 1;
        BQ_BROKER_CHECK(bq_broker_execute(&test_command, connection[0], true) == 0);
        received = recv(connection[1], &frame, sizeof(frame), 0);
        status = -1;
        if (received == (ssize_t)(offsetof(BqBrokerFrame, bytes) + sizeof(status)))
            memcpy(&status, frame.bytes, sizeof(status));
        BQ_BROKER_CHECK(frame.kind == BQ_BROKER_STATUS && status == 1);
    }
    if (connection[0] >= 0) close(connection[0]);
    if (connection[1] >= 0) close(connection[1]);
    char temporary[] = "/tmp/buster-systemd-broker-XXXXXX";
    char* root = mkdtemp(temporary);
    BQ_BROKER_CHECK(root != NULL);
    if (root)
    {
        char source[128], alias[128], manifest[128], hardlink[128];
        bool paths = bq_broker_format(source, sizeof(source), "%s/source", root) &&
                     bq_broker_format(alias, sizeof(alias), "%s/alias", root) &&
                     bq_broker_format(manifest, sizeof(manifest), "%s/source/manifest", root) &&
                     bq_broker_format(hardlink, sizeof(hardlink), "%s/hardlink", root);
        int descriptor = paths && mkdir(source, 0700) == 0 ?
                         open(manifest, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600) : -1;
        bool created = descriptor >= 0 && write(descriptor, "ok", 2) == 2;
        if (descriptor >= 0) close(descriptor);
        BQ_BROKER_CHECK(created && symlink(source, alias) == 0 && bq_broker_open_directory(alias) < 0);
        unsigned char buffer[8];
        size_t size = 0;
        BQ_BROKER_CHECK(created && link(manifest, hardlink) == 0 &&
                        !bq_broker_regular(source, "manifest", geteuid(), true, buffer, sizeof(buffer), &size));
        if (paths) unlink(hardlink);
        BQ_BROKER_CHECK(created && bq_broker_regular(source, "manifest", geteuid(), true,
                                                     buffer, sizeof(buffer), &size) && size == 2);
        if (paths)
        {
            unlink(alias);
            unlink(manifest);
            rmdir(source);
        }
        rmdir(root);
    }
#undef BQ_BROKER_CHECK
    printf("BUSTER_SYSTEMD_BROKER_SELF_TEST checks=%u result=%s\n", checks, ok ? "pass" : "fail");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    int result = 126;
    if (argc == 2 && !strcmp(argv[1], "serve-connection"))
        result = bq_broker_server();
    else if (argc == 2 && !strcmp(argv[1], "self-test"))
        result = bq_broker_self_test();
    else
    {
        BqBrokerRequest request;
        if (bq_broker_cli(argc, argv, &request)) result = bq_broker_client(&request);
    }
    return result;
}
