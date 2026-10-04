/* Benchpress systemd authority boundary.
 *
 * The unprivileged CLI sends only an operation, a job/attempt, a fixed stage,
 * and two source identities.  The root socket instance authenticates the peer,
 * checks the durable worker record and stable lease, then constructs every
 * manager argument itself.  No caller argv, path, property, environment or
 * executable is forwarded to systemd.  `self-test` exercises construction and
 * rejection without contacting the manager.
 *
 * Request version 2 (the #923 layout: `recipe` in place of the former reserved
 * word, then `runtime_max_usec` after the attempt) selects the recipe:
 * BQ_BROKER_RECIPE_SMOKE (0, stages 1..5 of validate-buster-v1), the #1020
 * retirement selector 1 (refused here) and BQ_BROKER_RECIPE_ZEN5 (2, stages
 * BQ_ZEN5_STAGE_FIRST_NUMBER.. of zen5_stage.h, whose argv, writable path,
 * account and perf_event_open allowance come from bq_zen5_stage_command,
 * shared with the recipe driver and the credential gate). runtime_max_usec
 * must be zero for both served recipes; every unit keeps the fixed hour.
 * Signals carry the smoke selector; the unit name binds their stage.
 *
 * This executable is Linux-only and deliberately has no Buster dependency.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <grp.h>
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
#include <sys/xattr.h>
#include "zen5_stage.h"
#include "native_profile.h"

#define BQ_BROKER_SOCKET "/run/buster-bench-systemd-broker/control.sock"
#define BQ_BROKER_QUEUE "/var/lib/buster-bench/queue"
#define BQ_BROKER_LEASE "/var/lib/buster-bench/lease/host.lock"
#define BQ_BROKER_LEASE_RECEIPT "/etc/buster-bench/systemd-broker-lease.identity"
#define BQ_BROKER_WORKSPACES "/var/lib/buster-bench/workspaces"
/* Every stage unit loses the service and broker control sockets: a stage
 * runs revision-controlled code (CMake as buster-bench, the frozen binaries
 * as the candidate), and both sockets authenticate only by peer uid/gid. */
#define BQ_BROKER_CONTROL_DIRECTORIES "/run/buster-bench /run/buster-bench-systemd-broker"
/* The one fixed unit runtime bound; the dispatch wait equals it. */
#define BQ_BROKER_RUNTIME_MAX "--property=RuntimeMaxSec=3600000000us"
#define BQ_BROKER_CGROUP_SLICE "/buster.slice/buster-bench.slice"
#define BQ_BROKER_INSTALLED "/opt/buster-bench/installed"
#define BQ_BROKER_SERVICE "/usr/local/libexec/buster-bench-service"
#define BQ_BROKER_BUILD "/usr/local/libexec/buster-bench-build"
#define BQ_BROKER_THROUGHPUT "/usr/local/libexec/buster-bench-throughput"
#define BQ_BROKER_GATE "/usr/local/libexec/buster-bench-credential-gate"
#define BQ_BROKER_RUN "/usr/bin/systemd-run"
#define BQ_BROKER_CTL "/usr/bin/systemctl"
#define BQ_BROKER_MAGIC 0x42515344u
/* Version 2 added the recipe selector and the runtime field (#923 layout). */
#define BQ_BROKER_VERSION 2u
#define BQ_BROKER_MAX_ARGS 96u
#define BQ_BROKER_TEXT 2048u
#define BQ_BROKER_DIAG_LINE 1200u
#define BQ_BROKER_DIAG_CHUNK 512u
#define BQ_BROKER_DIAG_OUTPUT (512u * 1024u)
#define BQ_BROKER_DIAG_MILLISECONDS 500u
/* A larger NSS group list is refused, never accepted from a truncated buffer. */
#define BQ_BROKER_ACCOUNT_GROUP_LIMIT 32

enum { BQ_BROKER_START = 1, BQ_BROKER_SIGNAL = 2 };
enum { BQ_BROKER_OUTER = 0, BQ_BROKER_BASE_GENERATE = 1, BQ_BROKER_BASE_BUILD = 2,
       BQ_BROKER_CANDIDATE_GENERATE = 3, BQ_BROKER_CANDIDATE_BUILD = 4, BQ_BROKER_THROUGHPUT_STAGE = 5,
       /* zen5-calibration-v1: the fixed stages of zen5_stage.h. */
       BQ_BROKER_ZEN5_FIRST_STAGE = BQ_ZEN5_STAGE_FIRST_NUMBER,
       BQ_BROKER_ZEN5_LAST_STAGE = BQ_ZEN5_STAGE_FIRST_NUMBER + BQ_ZEN5_STAGE_COUNT - 1 };
enum { BQ_BROKER_TERM = 1, BQ_BROKER_KILL = 2, BQ_BROKER_CONT = 3 };
/* The smoke recipe is the zero selector every smoke and signal request keeps;
 * 1 is the #1020 retirement selector, which this broker does not serve. */
enum { BQ_BROKER_RECIPE_SMOKE = 0, BQ_BROKER_RECIPE_RETIREMENT = 1, BQ_BROKER_RECIPE_ZEN5 = 2, BQ_BROKER_RECIPE_NATIVE = 3 };

typedef struct BqBrokerRequest
{
    uint32_t magic;
    uint32_t version;
    uint32_t operation;
    uint32_t stage;
    uint32_t signal_number;
    uint32_t recipe;
    uint64_t job;
    uint64_t attempt;
    uint64_t runtime_max_usec;
    char base[65];
    char candidate[65];
} BqBrokerRequest;
_Static_assert(sizeof(BqBrokerRequest) == 184, "broker request version 2 is 184 bytes (#923 layout)");

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

typedef struct BqBrokerDiagnostic
{
    int stream;
    pid_t pid;
    uint64_t start_ticks;
    uint64_t spent_milliseconds;
    size_t output_bytes;
    bool stream_ok;
    bool snapshot_complete;
} BqBrokerDiagnostic;

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
    char native_scratch[512];
} BqBrokerPaths;

typedef struct BqBrokerAccounts
{
    uid_t service_uid;
    gid_t service_gid;
    uid_t candidate_uid;
    gid_t candidate_gid;
    uid_t runner_uid;
    gid_t runner_gid;
} BqBrokerAccounts;

typedef struct BqBrokerStartGroups
{
    gid_t ids[2][BQ_BROKER_ACCOUNT_GROUP_LIMIT];
    int count[2];
    uid_t uid[2];
    gid_t gid[2];
} BqBrokerStartGroups;

static char const* const bq_broker_stages[] = {
    "", "base-generate", "base-build", "candidate-generate", "candidate-build", "throughput"
};
static char const* const bq_broker_zen5_stages[BQ_ZEN5_STAGE_COUNT] = {BQ_ZEN5_STAGE_NAMES};

static bool bq_broker_zen5_stage(uint32_t stage)
{
    bool zen5 = stage >= BQ_BROKER_ZEN5_FIRST_STAGE && stage <= BQ_BROKER_ZEN5_LAST_STAGE;
    return zen5;
}

static bool bq_broker_known_stage(uint32_t stage)
{
    bool known = stage <= BQ_BROKER_THROUGHPUT_STAGE || bq_broker_zen5_stage(stage) || stage == BQ_NATIVE_STAGE;
    return known;
}

/* The typed stage name of a known nonzero stage, else NULL. */
static char const* bq_broker_stage_name(uint32_t stage)
{
    char const* name = stage >= BQ_BROKER_BASE_GENERATE && stage <= BQ_BROKER_THROUGHPUT_STAGE ?
                       bq_broker_stages[stage] :
                       bq_broker_zen5_stage(stage) ? bq_broker_zen5_stages[stage - BQ_BROKER_ZEN5_FIRST_STAGE] :
                       stage == BQ_NATIVE_STAGE ? BQ_NATIVE_STAGE_NAME : NULL;
    return name;
}

/* Stages that run as buster-bench; every other stage runs as the candidate. */
static bool bq_broker_service_stage(uint32_t stage)
{
    bool service = stage <= BQ_BROKER_BASE_BUILD ||
                   (bq_broker_zen5_stage(stage) && stage - BQ_BROKER_ZEN5_FIRST_STAGE < BQ_ZEN5_STAGE_ORACLE);
    return service;
}

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

/* A start's recipe must own its stage: the outer unit serves smoke or zen5,
 * stages 1..5 only smoke and the zen5 stages only zen5. A zen5 job names one
 * immutable source twice. No served recipe takes a runtime from its caller. */
static bool bq_broker_request_recipe_valid(BqBrokerRequest const* request)
{
    bool zen5 = request->recipe == BQ_BROKER_RECIPE_ZEN5;
    bool native = request->recipe == BQ_BROKER_RECIPE_NATIVE;
    bool ok = request->runtime_max_usec == 0 && (request->recipe == BQ_BROKER_RECIPE_SMOKE || zen5 || native);
    if (ok && request->stage == BQ_BROKER_OUTER)
        ok = (!zen5 && !native) || (!strcmp(request->base, request->candidate) && (!native || strlen(request->base) == 64));
    else if (ok)
        ok = native ? request->stage == BQ_NATIVE_STAGE && strlen(request->base) == 64 &&
                      !strcmp(request->base, request->candidate) :
                      request->stage != BQ_NATIVE_STAGE && zen5 == bq_broker_zen5_stage(request->stage);
    return ok;
}

static bool bq_broker_request_valid(BqBrokerRequest const* request)
{
    bool start = request->operation == BQ_BROKER_START;
    bool signal = request->operation == BQ_BROKER_SIGNAL;
    bool ok = request->magic == BQ_BROKER_MAGIC && request->version == BQ_BROKER_VERSION &&
              (start || signal) && bq_broker_known_stage(request->stage) &&
              request->job != 0 && request->attempt != 0;
    if (ok && start)
        ok = request->signal_number == 0 && bq_broker_revision(request->base) &&
             bq_broker_revision(request->candidate) && bq_broker_request_recipe_valid(request);
    if (ok && signal)
        ok = request->recipe == BQ_BROKER_RECIPE_SMOKE && request->runtime_max_usec == 0;
    if (ok && signal)
    {
        ok = request->signal_number == BQ_BROKER_TERM || request->signal_number == BQ_BROKER_KILL ||
             (request->signal_number == BQ_BROKER_CONT && request->stage == BQ_BROKER_OUTER);
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
        ok = bq_broker_stage_name(request->stage) &&
             bq_broker_format(paths->unit, sizeof(paths->unit), "buster-bench-%" PRIu64 "-%" PRIu64 "-%s.service",
                              request->job, request->attempt, bq_broker_stage_name(request->stage));
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
    if (ok) ok = bq_broker_format(paths->native_scratch, sizeof(paths->native_scratch), "%s/native-scratch", paths->attempt);
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
        "--property=CapabilityBoundingSet=", "--property=AmbientCapabilities=",
        "--property=RestrictAddressFamilies=AF_UNIX", "--property=SystemCallArchitectures=native",
        "--property=SystemCallFilter=@system-service", "--property=SystemCallErrorNumber=EPERM"
    };
    for (unsigned index = 0; index < sizeof(properties) / sizeof(properties[0]); index += 1)
        bq_broker_add(command, properties[index]);
}

static void bq_broker_add_gate(BqBrokerCommand* command, BqBrokerRequest const* request,
                               BqBrokerStartGroups const* groups)
{
    unsigned role = bq_broker_service_stage(request->stage) ? 0u : 1u;
    char list[384] = {0};
    size_t used = 0;
    bool ok = groups && groups->count[role] > 0 &&
              groups->count[role] <= BQ_BROKER_ACCOUNT_GROUP_LIMIT &&
              groups->uid[role] != 0 && groups->gid[role] != 0;
    for (int index = 0; ok && index < groups->count[role]; index += 1)
    {
        int written = snprintf(list + used, sizeof(list) - used, "%s%u",
                               index ? "," : "", (unsigned)groups->ids[role][index]);
        ok = written > 0 && (size_t)written < sizeof(list) - used;
        if (ok) used += (size_t)written;
    }
    if (ok)
    {
        bq_broker_add(command, BQ_BROKER_GATE);
        bq_broker_add_format(command, "%u", request->stage);
        bq_broker_add_format(command, "%u", (unsigned)groups->uid[role]);
        bq_broker_add_format(command, "%u", (unsigned)groups->gid[role]);
        bq_broker_add_format(command, "%s", list);
        bq_broker_add(command, "--");
    }
    else
        command->valid = false;
}

static bool bq_broker_command(BqBrokerRequest const* request, BqBrokerStartGroups const* groups,
                              BqBrokerCommand* command)
{
    BqBrokerPaths paths;
    BqZen5StageCommand zen5;
    bool ok = bq_broker_request_valid(request) && bq_broker_paths(request, &paths);
    bool zen5_stage = ok && request->operation == BQ_BROKER_START && bq_broker_zen5_stage(request->stage);
    if (zen5_stage) ok = bq_zen5_stage_command(request->stage - BQ_BROKER_ZEN5_FIRST_STAGE, paths.attempt, &zen5);
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
        bq_broker_add(command, BQ_BROKER_RUNTIME_MAX);
        if (request->stage != BQ_BROKER_OUTER)
        {
            bq_broker_add_format(command, "--property=PartOf=%s", paths.parent);
            bq_broker_add_format(command, "--property=BindsTo=%s", paths.parent);
            bq_broker_add_format(command, "--property=After=%s", paths.parent);
            bq_broker_add(command, "--collect");
            if (bq_broker_service_stage(request->stage))
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
        /* Retain failed outer units until their exact manager Result is read.
         * Only stage units use aggressive collection. */
        if (request->stage == BQ_BROKER_OUTER)
            bq_broker_add(command, "--property=CollectMode=inactive");
        bq_broker_common_sandbox(command);
        if (request->stage == BQ_BROKER_OUTER)
        {
            bq_broker_add_format(command, "--property=InaccessiblePaths=%s %s", BQ_BROKER_QUEUE, BQ_BROKER_LEASE);
            bq_broker_add_format(command, "--property=ReadOnlyPaths=%s", BQ_BROKER_INSTALLED);
            bq_broker_add_format(command, "--property=ReadWritePaths=%s", BQ_BROKER_WORKSPACES);
        }
        else
        {
            if (request->stage == BQ_NATIVE_STAGE)
            {
                bq_broker_add_format(command, "--property=ReadOnlyPaths=%s %s", paths.attempt, BQ_BROKER_INSTALLED);
                bq_broker_add_format(command, "--property=ReadWritePaths=%s", paths.native_scratch);
                bq_broker_add_format(command, "--working-directory=%s", paths.native_scratch);
            }
            else if (zen5_stage)
            {
                /* The zen5 stage reads the source and the trusted driver's
                 * frozen binaries, plan and specs, and writes one path. */
                bq_broker_add_format(command, "--property=ReadOnlyPaths=%s %s/zen5", paths.base_source, paths.attempt);
                bq_broker_add_format(command, "--property=ReadWritePaths=%s", zen5.writable);
                bq_broker_add_format(command, "--working-directory=%s", zen5.directory);
                if (zen5.perf) bq_broker_add(command, "--property=SystemCallFilter=" BQ_ZEN5_STAGE_PERF_SYSCALL);
            }
            else if (request->stage <= BQ_BROKER_BASE_BUILD)
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
            bq_broker_add_format(command, "--property=InaccessiblePaths=%s %s %s %s", BQ_BROKER_QUEUE,
                                 BQ_BROKER_LEASE, paths.result, BQ_BROKER_CONTROL_DIRECTORIES);
        }
        bq_broker_add(command, "--property=PrivateNetwork=yes");
        bq_broker_add_gate(command, request, groups);
        if (request->stage == BQ_BROKER_OUTER)
        {
            bq_broker_add(command, BQ_BROKER_SERVICE);
            bq_broker_add(command, "worker-unit");
            bq_broker_add(command, BQ_BROKER_LEASE);
            bq_broker_add_format(command, "%" PRIu64, request->job);
            bq_broker_add_format(command, "%" PRIu64, request->attempt);
            bq_broker_add(command, request->recipe == BQ_BROKER_RECIPE_NATIVE ? BQ_NATIVE_RECIPE :
                                   request->recipe == BQ_BROKER_RECIPE_ZEN5 ? BQ_ZEN5_STAGE_RECIPE :
                                   "validate-buster-v1");
            bq_broker_add(command, BQ_BROKER_WORKSPACES);
            bq_broker_add(command, request->base);
            bq_broker_add(command, request->candidate);
            bq_broker_add_format(command, "%s", paths.result);
        }
        else if (request->stage == BQ_NATIVE_STAGE)
        {
            bq_broker_add(command, BQ_BROKER_SERVICE);
            bq_broker_add(command, "native-payload");
            bq_broker_add_format(command, "%s", paths.candidate_source);
            bq_broker_add(command, request->candidate);
        }
        else if (zen5_stage)
        {
            for (unsigned index = 0; index < zen5.count; index += 1) bq_broker_add_format(command, "%s", zen5.argv[index]);
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
            bq_broker_add(command, "--service-output");
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
        bq_broker_add(command, request->signal_number == BQ_BROKER_TERM ? "--signal=TERM" :
                       request->signal_number == BQ_BROKER_KILL ? "--signal=KILL" : "--signal=CONT");
        bq_broker_add_format(command, "%s", paths.unit);
    }
    ok = ok && command->valid;
    if (ok) command->argv[command->count] = NULL;
    return ok;
}

static int bq_broker_open_directory(char const* path)
{
    int current = path && path[0] == '/' ? open("/", O_PATH | O_DIRECTORY | O_CLOEXEC) : -1;
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
            next = openat(current, name, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
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

static bool bq_broker_private_directory(char const* path, uid_t owner, gid_t group, mode_t mode)
{
    int descriptor = bq_broker_open_directory(path);
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
              info.st_uid == owner && info.st_gid == group && (info.st_mode & 07777) == mode;
    if (descriptor >= 0) close(descriptor);
    return ok;
}

static bool bq_broker_regular(char const* parent, char const* name, uid_t owner, gid_t group,
                              mode_t mode, bool writable, unsigned char* output, size_t capacity, size_t* size)
{
    int directory = bq_broker_open_directory(parent);
    int descriptor = directory >= 0 ? openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK) : -1;
    struct stat info = {0};
    bool ok = descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == owner && (group == (gid_t)-1 || info.st_gid == group) &&
              (mode == 0 || (info.st_mode & 07777) == mode) &&
              info.st_nlink == 1 && (info.st_mode & 0022) == 0 &&
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
                                    uid_t service_uid, gid_t service_gid, bool instance)
{
    char name[64];
    unsigned char record[544] = {0};
    size_t size = 0;
    bool ok = bq_broker_format(name, sizeof(name), instance ? "worker-instance-%" PRIu64 : "worker-%" PRIu64,
                               request->job) &&
              bq_broker_private_directory(BQ_BROKER_QUEUE, service_uid, service_gid, 0710) &&
              bq_broker_regular(BQ_BROKER_QUEUE, name, service_uid, service_gid, 0440,
                                true, record, sizeof(record), &size);
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

static bool bq_broker_lease_held(uid_t service_uid, gid_t service_gid)
{
    unsigned char receipt[128] = {0};
    size_t receipt_size = 0;
    bool receipt_ok = bq_broker_directory("/etc/buster-bench", 0, false) &&
                      bq_broker_regular("/etc/buster-bench", "systemd-broker-lease.identity", 0, (gid_t)-1, 0, false,
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
    bool parent_ok = bq_broker_private_directory("/var/lib/buster-bench/lease", service_uid, service_gid, 0710);
    int directory = parent_ok ? bq_broker_open_directory("/var/lib/buster-bench/lease") : -1;
    int descriptor = directory >= 0 ? openat(directory, "host.lock", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat info = {0};
    bool ok = receipt_ok && descriptor >= 0 && fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == service_uid && info.st_gid == service_gid &&
              info.st_nlink == 1 && (info.st_mode & 07777) == 0640 &&
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
    bool native = request->recipe == BQ_BROKER_RECIPE_NATIVE;
    bool ok = native ?
              bq_broker_format(installed, sizeof(installed), "%s/native-blobs/%s", BQ_BROKER_QUEUE, revision) &&
              bq_broker_directory(installed, service_uid, false) &&
              bq_broker_regular(installed, "manifest", service_uid, (gid_t)-1, 0400, false, source_bytes,
                                sizeof(source_bytes), &source_size) &&
              bq_broker_directory(workspace, service_uid, false) &&
              bq_broker_regular(workspace, ".native-manifest", service_uid, (gid_t)-1, 0440, false, copy_bytes,
                                sizeof(copy_bytes), &copy_size) :
              bq_broker_format(installed, sizeof(installed), "%s/sources/%s", BQ_BROKER_INSTALLED, revision) &&
              bq_broker_directory(installed, 0, false) &&
              bq_broker_regular(installed, "source.manifest", 0, (gid_t)-1, 0, false, source_bytes,
                                sizeof(source_bytes), &source_size) &&
              bq_broker_directory(workspace, service_uid, false) &&
              bq_broker_regular(workspace, ".source-manifest", service_uid, (gid_t)-1, 0, false, copy_bytes,
                                sizeof(copy_bytes), &copy_size);
    char header[128];
    if (ok && native)
        ok = source_size < BQ_NATIVE_MANIFEST_CAP && source_size > 64 &&
             !memcmp(source_bytes, "BQ-NATIVE-V1\noperation=execute-once\n", 35) &&
             source_size == copy_size && !memcmp(source_bytes, copy_bytes, source_size);
    else if (ok)
        ok = bq_broker_format(header, sizeof(header),
                              "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n", revision) &&
             source_size >= strlen(header) && !memcmp(source_bytes, header, strlen(header)) &&
             source_size == copy_size && !memcmp(source_bytes, copy_bytes, source_size);
    return ok;
}

static bool bq_broker_static_elf(int descriptor, off_t length)
{
    Elf64_Ehdr header;
    bool ok = length >= (off_t)sizeof(header) &&
              pread(descriptor, &header, sizeof(header), 0) == sizeof(header) &&
              !memcmp(header.e_ident, ELFMAG, SELFMAG) &&
              header.e_ident[EI_CLASS] == ELFCLASS64 &&
              header.e_ident[EI_DATA] == ELFDATA2LSB &&
              header.e_ident[EI_VERSION] == EV_CURRENT &&
              header.e_type == ET_EXEC && header.e_version == EV_CURRENT &&
              header.e_ehsize == sizeof(header) &&
              header.e_phentsize == sizeof(Elf64_Phdr) &&
              header.e_phnum > 0 && header.e_phnum <= 128 &&
              header.e_phoff <= (uint64_t)length &&
              (uint64_t)header.e_phnum * sizeof(Elf64_Phdr) <= (uint64_t)length - header.e_phoff;
    for (unsigned index = 0; ok && index < header.e_phnum; index += 1)
    {
        Elf64_Phdr program;
        off_t offset = (off_t)(header.e_phoff + (uint64_t)index * sizeof(program));
        ok = pread(descriptor, &program, sizeof(program), offset) == sizeof(program) &&
             program.p_type != PT_INTERP && program.p_type != PT_DYNAMIC;
    }
    return ok;
}

static bool bq_broker_gate_parent(void)
{
    static char const* const components[] = {"usr", "local", "libexec"};
    int descriptor = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = descriptor >= 0;
    for (unsigned index = 0; ok && index <= sizeof(components) / sizeof(components[0]); index += 1)
    {
        struct stat info;
        ok = fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode) &&
             info.st_uid == 0 && (info.st_mode & 0022) == 0;
        if (ok && index < sizeof(components) / sizeof(components[0]))
        {
            int next = openat(descriptor, components[index], O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(descriptor);
            descriptor = next;
            ok = descriptor >= 0;
        }
    }
    if (descriptor >= 0) close(descriptor);
    return ok;
}

static bool bq_broker_installed_binary(char const* path, bool static_gate)
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
        if (ok && static_gate)
        {
            ssize_t capability_size = fgetxattr(descriptor, "security.capability", NULL, 0);
            int capability_errno = errno;
            ok = bq_broker_gate_parent() && !(info.st_mode & (S_ISUID | S_ISGID)) &&
                 capability_size < 0 && capability_errno == ENODATA &&
                 bq_broker_static_elf(descriptor, info.st_size);
        }
        if (descriptor >= 0) close(descriptor);
        if (directory >= 0) close(directory);
    }
    return ok;
}

/* The writable directory a stage start requires to exist already, service
 * owned: the candidate staging of candidate-generate and, because the trusted
 * driver creates each one first, every zen5 stage's writable path. Returns 1
 * with `output` filled, 0 when none is required, -1 on an invalid request. */
static int bq_broker_start_writable(BqBrokerRequest const* request, BqBrokerPaths const* paths, char* output,
                                    size_t capacity)
{
    BqZen5StageCommand zen5;
    int result = 0;
    if (request->stage == BQ_BROKER_CANDIDATE_GENERATE)
        result = bq_broker_format(output, capacity, "%s", paths->candidate_staging) ? 1 : -1;
    else if (request->stage == BQ_NATIVE_STAGE)
        result = bq_broker_format(output, capacity, "%s", paths->native_scratch) ? 1 : -1;
    else if (bq_broker_zen5_stage(request->stage))
        result = bq_zen5_stage_command(request->stage - BQ_BROKER_ZEN5_FIRST_STAGE, paths->attempt, &zen5) &&
                 bq_broker_format(output, capacity, "%s", zen5.writable) ? 1 : -1;
    return result;
}

static bool bq_broker_state(BqBrokerRequest const* request, uid_t service_uid,
                            gid_t service_gid, gid_t candidate_gid)
{
    BqBrokerPaths paths;
    bool ok = bq_broker_paths(request, &paths) &&
              bq_broker_private_directory("/var/lib/buster-bench", service_uid, candidate_gid, 0710) &&
              bq_broker_private_directory(BQ_BROKER_WORKSPACES, service_uid, candidate_gid, 02710) &&
              bq_broker_private_directory(BQ_BROKER_WORKSPACES "/results", service_uid, service_gid, 0710) &&
              bq_broker_private_directory(paths.result, service_uid, service_gid, 0700) &&
              bq_broker_worker_record(request, &paths, service_uid, service_gid, false) &&
              bq_broker_lease_held(service_uid, service_gid);
    if (ok && (request->stage != BQ_BROKER_OUTER || request->operation == BQ_BROKER_SIGNAL))
        ok = bq_broker_worker_record(request, &paths, service_uid, service_gid, true);
    if (ok && request->operation == BQ_BROKER_START)
    {
        ok = bq_broker_manifest(request, &paths, service_uid, false) &&
             bq_broker_manifest(request, &paths, service_uid, true) &&
             bq_broker_installed_binary(BQ_BROKER_SERVICE, false) &&
             (request->recipe == BQ_BROKER_RECIPE_NATIVE ||
              (bq_broker_installed_binary(BQ_BROKER_BUILD, false) &&
               bq_broker_installed_binary(BQ_BROKER_THROUGHPUT, false))) &&
             bq_broker_installed_binary(BQ_BROKER_GATE, true);
        char writable[BQ_ZEN5_STAGE_TEXT] = {0};
        int required = ok ? bq_broker_start_writable(request, &paths, writable, sizeof(writable)) : 0;
        if (ok && required < 0) ok = false;
        else if (ok && required > 0) ok = bq_broker_directory(writable, service_uid, true);
    }
    return ok;
}

static bool bq_broker_unit_from_text(char const* unit, BqBrokerRequest* request)
{
    BqBrokerRequest candidate = {.magic = BQ_BROKER_MAGIC, .version = BQ_BROKER_VERSION, .operation = BQ_BROKER_SIGNAL};
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
    for (unsigned stage = 0; ok && !found && stage <= BQ_NATIVE_STAGE; stage += 1)
    {
        candidate.stage = stage;
        found = bq_broker_known_stage(stage) && bq_broker_paths(&candidate, &paths) && !strcmp(paths.unit, unit);
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

/* The payload program (verb false) or its first argument (verb true) of a
 * stage's unit, as bq_broker_command writes it after the credential gate. */
static char const* bq_broker_stage_program(uint32_t stage, bool verb)
{
    char const* result = NULL;
    if (stage == BQ_BROKER_OUTER) result = verb ? "worker-unit" : BQ_BROKER_SERVICE;
    else if (stage == BQ_NATIVE_STAGE) result = verb ? "native-payload" : BQ_BROKER_SERVICE;
    else if (stage == BQ_BROKER_THROUGHPUT_STAGE) result = verb ? "run" : BQ_BROKER_THROUGHPUT;
    else if (stage <= BQ_BROKER_CANDIDATE_BUILD)
        result = !verb ? BQ_BROKER_BUILD :
                 stage == BQ_BROKER_BASE_GENERATE || stage == BQ_BROKER_CANDIDATE_GENERATE ? "generate" : "build";
    else if (bq_broker_zen5_stage(stage))
    {
        uint32_t index = stage - BQ_BROKER_ZEN5_FIRST_STAGE;
        result = !verb ? (index == BQ_ZEN5_STAGE_PMU ? BQ_ZEN5_STAGE_PYTHON : BQ_ZEN5_STAGE_DRIVER) :
                 index < BQ_ZEN5_STAGE_ORACLE ? (index % 2u == 0 ? "generate" : "build") :
                 index == BQ_ZEN5_STAGE_PMU ? "-B" : BQ_ZEN5_STAGE_CAPTURE_VERB;
    }
    return result;
}

/* systemctl show renders our eight leading fixed arguments without escaping.
 * Parse tokens within the single ExecStart line, never by substring, and bind
 * stage/target/verb even for recovery signals. CONT additionally matches the
 * current authorized numeric credential tuple. TERM/KILL can clean old units. */
static bool bq_broker_gate_exec_identity(char const* text, BqBrokerRequest const* request,
                                         BqBrokerStartGroups const* groups)
{
    char const* value = bq_broker_field(text, "ExecStart");
    char const* end = value ? strchr(value, '\n') : NULL;
    if (value && !end) end = value + strlen(value);
    char const prefix[] = "{ path=" BQ_BROKER_GATE " ; argv[]=";
    size_t prefix_size = sizeof(prefix) - 1;
    bool ok = value && end && (size_t)(end - value) > prefix_size &&
              !memcmp(value, prefix, prefix_size) && bq_broker_known_stage(request->stage);
    char tokens[8][384] = {{0}};
    char const* at = ok ? value + prefix_size : NULL;
    for (unsigned index = 0; ok && index < 8; index += 1)
    {
        char const* first = at;
        while (at < end && *at != ' ' && *at != ';' && *at != '\t' && *at != '\r') at += 1;
        size_t length = (size_t)(at - first);
        ok = length > 0 && length < sizeof(tokens[index]) && at < end && *at == ' ';
        if (ok) { memcpy(tokens[index], first, length); at += 1; }
    }
    char stage[16];
    char const* target = bq_broker_stage_program(request->stage, false);
    char const* verb = bq_broker_stage_program(request->stage, true);
    uint64_t uid = 0, gid = 0;
    ok = ok && bq_broker_format(stage, sizeof(stage), "%u", request->stage) &&
         !strcmp(tokens[0], BQ_BROKER_GATE) && !strcmp(tokens[1], stage) &&
         bq_broker_decimal(tokens[2], &uid) && uid < UINT32_MAX &&
         bq_broker_decimal(tokens[3], &gid) && gid < UINT32_MAX &&
         !strcmp(tokens[5], "--") && !strcmp(tokens[6], target) && !strcmp(tokens[7], verb);
    char list[384];
    memcpy(list, tokens[4], sizeof(list));
    char* current = list;
    uint64_t previous = 0;
    unsigned count = 0;
    while (ok && current)
    {
        char* comma = strchr(current, ',');
        if (comma) *comma = 0;
        uint64_t id = 0;
        ok = count < BQ_BROKER_ACCOUNT_GROUP_LIMIT && bq_broker_decimal(current, &id) &&
             id < UINT32_MAX && id > previous;
        previous = id;
        count += 1;
        current = comma ? comma + 1 : NULL;
    }
    ok = ok && count == (bq_broker_service_stage(request->stage) ? 2u : 1u);
    if (ok && request->signal_number == BQ_BROKER_CONT)
    {
        BqBrokerCommand expected = {.valid = true};
        bq_broker_add_gate(&expected, request, groups);
        ok = expected.valid && expected.count == 6;
        for (unsigned index = 0; ok && index < 6; index += 1)
            ok = !strcmp(tokens[index], expected.argv[index]);
    }
    return ok;
}

static bool bq_broker_exec_identity(char const* text, BqBrokerRequest const* request,
                                    BqBrokerStartGroups const* groups)
{
    bool ok = bq_broker_gate_exec_identity(text, request, groups);
    /* Only the smoke stages predate the credential gate; a zen5 unit always
     * runs gated, so an ungated ExecStart never identifies one. */
    if (!ok && request->signal_number != BQ_BROKER_CONT && request->stage <= BQ_BROKER_THROUGHPUT_STAGE)
    {
        ok = bq_broker_exec_path(text, bq_broker_stage_program(request->stage, false));
    }
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

/* Private evidence only. The socket protocol and broker decision never depend
 * on these records. Journal loss, partial output, or a read bound leaves an
 * incomplete sequence for the external collector to reject. */
static bool bq_broker_diag_charge(BqBrokerDiagnostic* diagnostic, uint64_t before)
{
    uint64_t after = bq_broker_now_milliseconds();
    bool ok = diagnostic->spent_milliseconds <= BQ_BROKER_DIAG_MILLISECONDS &&
              before != UINT64_MAX && after != UINT64_MAX && after >= before &&
              after - before <= BQ_BROKER_DIAG_MILLISECONDS - diagnostic->spent_milliseconds;
    if (ok) diagnostic->spent_milliseconds += after - before;
    return ok;
}

static bool bq_broker_diag_phase_end(BqBrokerDiagnostic* diagnostic, uint64_t before,
                                     uint64_t prior_spent)
{
    uint64_t after = bq_broker_now_milliseconds();
    bool ok = before != UINT64_MAX && after != UINT64_MAX && after >= before &&
              prior_spent <= BQ_BROKER_DIAG_MILLISECONDS &&
              after - before <= BQ_BROKER_DIAG_MILLISECONDS - prior_spent;
    if (ok) diagnostic->spent_milliseconds = prior_spent + after - before;
    else diagnostic->stream_ok = false;
    return ok;
}

static bool bq_broker_diag_line(BqBrokerDiagnostic* diagnostic, char const* line, size_t length)
{
    uint64_t before = bq_broker_now_milliseconds();
    bool ok = diagnostic->stream_ok && diagnostic->start_ticks && before != UINT64_MAX &&
              diagnostic->spent_milliseconds <= BQ_BROKER_DIAG_MILLISECONDS &&
              length > 0 && length < BQ_BROKER_DIAG_LINE &&
              diagnostic->output_bytes <= BQ_BROKER_DIAG_OUTPUT - length;
    if (ok)
    {
        ssize_t written = send(diagnostic->stream, line, length, MSG_DONTWAIT | MSG_NOSIGNAL);
        ok = written == (ssize_t)length;
        if (ok) diagnostic->output_bytes += length;
    }
    if (ok) ok = bq_broker_diag_charge(diagnostic, before);
    diagnostic->stream_ok = ok;
    return ok;
}

static bool bq_broker_diag_format(BqBrokerDiagnostic* diagnostic, char const* format, ...)
{
    char line[BQ_BROKER_DIAG_LINE];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    bool ok = length > 0 && (size_t)length < sizeof(line) &&
              bq_broker_diag_line(diagnostic, line, (size_t)length);
    return ok;
}

static bool bq_broker_diag_read(BqBrokerDiagnostic* diagnostic, char const* path,
                                unsigned char* bytes, size_t capacity, size_t* length)
{
    uint64_t before = bq_broker_now_milliseconds();
    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    bool ok = descriptor >= 0 && capacity > 0;
    size_t size = 0;
    while (ok && size <= capacity)
    {
        ssize_t count = read(descriptor, bytes + size, capacity + 1 - size);
        if (count < 0 && errno == EINTR)
        {
            ok = bq_broker_diag_charge(diagnostic, before);
            if (ok) before = bq_broker_now_milliseconds();
            continue;
        }
        ok = count >= 0;
        if (!ok || count == 0) break;
        size += (size_t)count;
        ok = size <= capacity && diagnostic->spent_milliseconds <= BQ_BROKER_DIAG_MILLISECONDS;
        if (ok) ok = bq_broker_diag_charge(diagnostic, before);
        if (ok) before = bq_broker_now_milliseconds();
    }
    if (descriptor >= 0) close(descriptor);
    ok = ok && size > 0 && size <= capacity && bq_broker_diag_charge(diagnostic, before);
    if (ok) *length = size;
    return ok;
}

static bool bq_broker_diag_ticks(unsigned char const* bytes, size_t length, uint64_t* ticks)
{
    char const* start = (char const*)bytes;
    char const* end = start + length;
    char const* close = NULL;
    for (char const* at = start; at < end; at += 1)
        if (*at == ')') close = at;
    bool ok = close && close + 2 < end && close[1] == ' ';
    char const* field = ok ? close + 2 : end;
    for (unsigned index = 3; ok && index < 22; index += 1)
    {
        while (field < end && *field != ' ') field += 1;
        ok = field < end;
        while (field < end && *field == ' ') field += 1;
    }
    uint64_t value = 0;
    size_t digits = 0;
    while (ok && field < end && *field >= '0' && *field <= '9')
    {
        unsigned digit = (unsigned)(*field - '0');
        ok = value <= (UINT64_MAX - digit) / 10;
        if (ok) value = value * 10 + digit;
        field += 1;
        digits += 1;
    }
    ok = ok && digits > 0 && (field == end || *field == ' ' || *field == '\n') && value > 0;
    if (ok) *ticks = value;
    return ok;
}

static bool bq_broker_diag_data(BqBrokerDiagnostic* diagnostic, char const* field,
                                unsigned char const* bytes, size_t length)
{
    static char const hex[] = "0123456789abcdef";
    bool ok = length > 0;
    for (size_t offset = 0; ok && offset < length; offset += BQ_BROKER_DIAG_CHUNK)
    {
        size_t count = length - offset;
        if (count > BQ_BROKER_DIAG_CHUNK) count = BQ_BROKER_DIAG_CHUNK;
        char line[BQ_BROKER_DIAG_LINE];
        int prefix = snprintf(line, sizeof(line),
                              "BQ-BROKER-DIAG-V1 DATA pid=%ld ticks=%" PRIu64 " field=%s offset=%zu total=%zu hex=",
                              (long)diagnostic->pid, diagnostic->start_ticks, field, offset, length);
        ok = prefix > 0 && (size_t)prefix + count * 2 + 1 < sizeof(line);
        if (ok)
        {
            for (size_t index = 0; index < count; index += 1)
            {
                line[prefix + index * 2] = hex[bytes[offset + index] >> 4];
                line[prefix + index * 2 + 1] = hex[bytes[offset + index] & 15];
            }
            line[prefix + count * 2] = '\n';
            ok = bq_broker_diag_line(diagnostic, line, (size_t)prefix + count * 2 + 1);
        }
    }
    return ok;
}

static bool bq_broker_diag_field(BqBrokerDiagnostic* diagnostic, char const* field,
                                 char const* path, unsigned char* buffer, size_t maximum,
                                 size_t* length)
{
    bool ok = bq_broker_diag_read(diagnostic, path, buffer, maximum, length) &&
              bq_broker_diag_data(diagnostic, field, buffer, *length);
    return ok;
}

static void bq_broker_diag_snapshot(BqBrokerDiagnostic* diagnostic, int stream)
{
    *diagnostic = (BqBrokerDiagnostic){.stream = stream, .pid = getpid(), .stream_ok = true};
    uint64_t phase_before = bq_broker_now_milliseconds();
    unsigned char stat_bytes[4097], buffer[131073];
    size_t stat_size = 0, status_size = 0, mount_size = 0, cgroup_size = 0;
    size_t exe_size = 0, socket_size = 0;
    uint64_t start_ticks = 0;
    bool ok = bq_broker_diag_read(diagnostic, "/proc/self/stat", stat_bytes, 4096, &stat_size) &&
              bq_broker_diag_ticks(stat_bytes, stat_size, &start_ticks);
    if (ok) diagnostic->start_ticks = start_ticks;
    if (ok) ok = bq_broker_diag_format(diagnostic, "BQ-BROKER-DIAG-V1 BEGIN pid=%ld ticks=%" PRIu64 "\n",
                                     (long)diagnostic->pid, diagnostic->start_ticks) &&
                 bq_broker_diag_data(diagnostic, "stat", stat_bytes, stat_size);
    if (ok) ok = bq_broker_diag_field(diagnostic, "status", "/proc/self/status", buffer, 16384, &status_size);
    if (ok) ok = bq_broker_diag_field(diagnostic, "mountinfo", "/proc/self/mountinfo", buffer, 131072, &mount_size);
    if (ok) ok = bq_broker_diag_field(diagnostic, "cgroup", "/proc/self/cgroup", buffer, 4096, &cgroup_size);
    char exe[512];
    ssize_t link_size = ok ? readlink("/proc/self/exe", exe, sizeof(exe) - 1) : -1;
    struct stat executable = {0};
    ok = ok && link_size > 0 && (size_t)link_size < sizeof(exe) - 1 &&
         stat("/proc/self/exe", &executable) == 0 && S_ISREG(executable.st_mode);
    if (ok)
    {
        exe[link_size] = 0;
        char value[768];
        int count = snprintf(value, sizeof(value), "path=%s dev=%ju ino=%ju mode=%jo size=%ju",
                             exe, (uintmax_t)executable.st_dev, (uintmax_t)executable.st_ino,
                             (uintmax_t)executable.st_mode, (uintmax_t)executable.st_size);
        ok = count > 0 && (size_t)count < sizeof(value);
        if (ok) exe_size = (size_t)count;
        if (ok) ok = bq_broker_diag_data(diagnostic, "exe", (unsigned char const*)value, exe_size);
    }
    struct stat socket_info = {0};
    if (ok) ok = fstat(STDIN_FILENO, &socket_info) == 0 && S_ISSOCK(socket_info.st_mode);
    if (ok)
    {
        char value[256];
        int count = snprintf(value, sizeof(value), "dev=%ju ino=%ju mode=%jo",
                             (uintmax_t)socket_info.st_dev, (uintmax_t)socket_info.st_ino,
                             (uintmax_t)socket_info.st_mode);
        ok = count > 0 && (size_t)count < sizeof(value);
        if (ok) socket_size = (size_t)count;
        if (ok) ok = bq_broker_diag_data(diagnostic, "socket", (unsigned char const*)value, socket_size);
    }
    size_t final_stat_size = 0;
    uint64_t final_ticks = 0;
    if (ok) ok = bq_broker_diag_read(diagnostic, "/proc/self/stat", stat_bytes, 4096, &final_stat_size) &&
                 bq_broker_diag_ticks(stat_bytes, final_stat_size, &final_ticks) &&
                 final_ticks == diagnostic->start_ticks;
    bool within_budget = bq_broker_diag_phase_end(diagnostic, phase_before, 0);
    /* Leave time for the final nonblocking journal line; later REQUEST and
     * OUTCOME are required as well, so a late END cannot complete a sequence. */
    if (ok && within_budget && diagnostic->spent_milliseconds <= BQ_BROKER_DIAG_MILLISECONDS - 25u)
        ok = bq_broker_diag_format(diagnostic,
        "BQ-BROKER-DIAG-V1 SNAPSHOT_END pid=%ld ticks=%" PRIu64
        " stat=%zu status=%zu mountinfo=%zu cgroup=%zu exe=%zu socket=%zu elapsed_ms=%" PRIu64 "\n",
        (long)diagnostic->pid, diagnostic->start_ticks, stat_size, status_size,
        mount_size, cgroup_size, exe_size, socket_size, diagnostic->spent_milliseconds);
    else ok = false;
    diagnostic->snapshot_complete = ok && diagnostic->stream_ok;
}

static void bq_broker_diag_request(BqBrokerDiagnostic* diagnostic, bool peer_known,
                                   struct ucred peer, bool poll_called, bool recv_called,
                                   ssize_t received, unsigned flags, bool request_valid,
                                   bool state_checked, bool state_valid, bool signal_checked,
                                   bool signal_valid, bool command_checked, bool command_valid,
                                   BqBrokerRequest const* request)
{
    int saved_errno = errno;
    uint64_t phase_before = bq_broker_now_milliseconds();
    uint64_t prior_spent = diagnostic->spent_milliseconds;
    bool known = request_valid;
    (void)bq_broker_diag_format(diagnostic,
        "BQ-BROKER-DIAG-V1 REQUEST pid=%ld ticks=%" PRIu64
        " peer_known=%u peer_pid=%ld peer_uid=%lu poll_called=%u recv_called=%u recv=%zd flags=%u"
        " parsed=%u state_checked=%u state_valid=%u signal_checked=%u signal_valid=%u"
        " command_checked=%u command_valid=%u request_known=%u operation=%u stage=%u job=%" PRIu64
        " attempt=%" PRIu64 "\n",
        (long)diagnostic->pid, diagnostic->start_ticks, peer_known, peer_known ? (long)peer.pid : 0L,
        peer_known ? (unsigned long)peer.uid : 0UL, poll_called, recv_called, received, flags,
        request_valid, state_checked, state_valid, signal_checked, signal_valid,
        command_checked, command_valid, known, known ? request->operation : 0u,
        known ? request->stage : 0u, known ? request->job : 0u, known ? request->attempt : 0u);
    (void)bq_broker_diag_phase_end(diagnostic, phase_before, prior_spent);
    errno = saved_errno;
}

static void bq_broker_diag_outcome(BqBrokerDiagnostic* diagnostic, int result,
                                   int32_t frame_status, bool frame_sent)
{
    int saved_errno = errno;
    uint64_t phase_before = bq_broker_now_milliseconds();
    uint64_t prior_spent = diagnostic->spent_milliseconds;
    (void)bq_broker_diag_format(diagnostic,
        "BQ-BROKER-DIAG-V1 OUTCOME pid=%ld ticks=%" PRIu64
        " broker_exit=%d frame_status=%" PRId32 " frame_sent=%u\n",
        (long)diagnostic->pid, diagnostic->start_ticks, result, frame_status, frame_sent);
    (void)bq_broker_diag_phase_end(diagnostic, phase_before, prior_spent);
    errno = saved_errno;
}

static bool bq_broker_show(char const* unit, char output[8192])
{
    static char const* const properties[] = {"Id", "LoadState", "Slice", "InvocationID", "ControlGroup",
        "User", "Group", "ExecStart", "PartOf", "BindsTo", "After", "AllowedCPUs", "MemoryMax",
        "MemorySwapMax", "TasksMax", "RuntimeMaxUSec", "NoNewPrivileges", "ProtectSystem",
        "PrivateNetwork", "CollectMode"};
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

static bool bq_broker_signal_identity(BqBrokerRequest const* request,
                                       BqBrokerStartGroups const* groups)
{
    /* Signal only an exact unit identity, including older units whose
     * capability policy predates the current fixed transient command. */
    BqBrokerPaths paths;
    char cgroup[256];
    char output[8192];
    bool ok = bq_broker_paths(request, &paths) &&
              bq_broker_format(cgroup, sizeof(cgroup), BQ_BROKER_CGROUP_SLICE "/%s", paths.unit) &&
              bq_broker_show(paths.unit, output) &&
              bq_broker_field_equals(output, "Id", paths.unit) &&
              bq_broker_field_equals(output, "LoadState", "loaded") &&
              bq_broker_field_equals(output, "Slice", "buster-bench.slice") &&
              bq_broker_field_equals(output, "NoNewPrivileges", "yes") &&
              bq_broker_field_equals(output, "ProtectSystem", "strict") &&
              bq_broker_field_equals(output, "PrivateNetwork", "yes") &&
              bq_broker_field_equals(output, "CollectMode", request->stage == BQ_BROKER_OUTER ?
                                      "inactive" : "inactive-or-failed") &&
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
             bq_broker_regular(BQ_BROKER_QUEUE, name, service->pw_uid, service->pw_gid,
                               0440, true, record, sizeof(record), &size) && size == sizeof(record) &&
             !memcmp(record, "BQINSTANCE000002", 16) &&
             bq_broker_u64(record + 16) == request->job &&
             bq_broker_u64(record + 24) == request->attempt &&
             record[264] == 0 && record[479] == 0 &&
             strnlen((char const*)(record + 232), 33) == 32 &&
             strnlen((char const*)(record + 288), 192) < 192 &&
             bq_broker_field_equals(output, "User", "buster-bench") &&
             bq_broker_field_equals(output, "Group", "buster-bench") &&
             bq_broker_exec_identity(output, request, groups) &&
             bq_broker_field_equals(output, "InvocationID", (char const*)(record + 232)) &&
             bq_broker_field_equals(output, "ControlGroup", (char const*)(record + 288));
    }
    else if (ok)
    {
        char const* identity = bq_broker_service_stage(request->stage) ? "buster-bench" : "buster-bench-candidate";
        ok = bq_broker_field_equals(output, "User", identity) &&
             bq_broker_field_equals(output, "Group", identity) &&
             bq_broker_exec_identity(output, request, groups) &&
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

static int bq_broker_execute(BqBrokerCommand const* command, int connection, bool signal_operation,
                             int32_t* framed_status, bool* frame_delivered)
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
    bool sent = bq_broker_send_frame(connection, BQ_BROKER_STATUS, &result, sizeof(result));
    if (!sent) result = 126;
    if (framed_status) *framed_status = result;
    if (frame_delivered) *frame_delivered = sent;
    return result == 126 ? 1 : 0;
}

static bool bq_broker_accounts_valid(BqBrokerAccounts const* accounts)
{
    /* Root belongs only to this constrained broker process.  At request time,
     * the fixed names must map to distinct non-root primary identities. */
    bool ok = accounts &&
              accounts->service_uid != (uid_t)-1 && accounts->service_uid != 0 &&
              accounts->service_gid != (gid_t)-1 && accounts->service_gid != 0 &&
              accounts->candidate_uid != (uid_t)-1 && accounts->candidate_uid != 0 &&
              accounts->candidate_gid != (gid_t)-1 && accounts->candidate_gid != 0 &&
              accounts->runner_uid != (uid_t)-1 && accounts->runner_uid != 0 &&
              accounts->runner_gid != (gid_t)-1 && accounts->runner_gid != 0 &&
              accounts->service_gid != accounts->candidate_gid &&
              accounts->candidate_uid != accounts->service_uid &&
              accounts->runner_uid != accounts->service_uid &&
              accounts->runner_uid != accounts->candidate_uid &&
              accounts->runner_gid != accounts->service_gid &&
              accounts->runner_gid != accounts->candidate_gid;
    return ok;
}

/* Installation authority is independent of the NSS lookup it constrains.
 * LOCAL installs these exact bytes only while dispatch is disabled and all
 * units are drained. No service, candidate or runner can rewrite this file. */
static bool bq_broker_accounts_receipt_matches(BqBrokerAccounts const* accounts,
                                               unsigned char const* bytes, size_t size)
{
    char expected[256];
    bool ok = bytes && bq_broker_accounts_valid(accounts) &&
              bq_broker_format(expected, sizeof(expected),
                  "BQ-ACCOUNTS-V1\nservice-uid=%" PRIu64 "\nservice-gid=%" PRIu64
                  "\ncandidate-uid=%" PRIu64 "\ncandidate-gid=%" PRIu64
                  "\nrunner-uid=%" PRIu64 "\nrunner-gid=%" PRIu64 "\n",
                  (uint64_t)accounts->service_uid, (uint64_t)accounts->service_gid,
                  (uint64_t)accounts->candidate_uid, (uint64_t)accounts->candidate_gid,
                  (uint64_t)accounts->runner_uid, (uint64_t)accounts->runner_gid);
    if (ok) ok = size == strlen(expected) && !memcmp(bytes, expected, size);
    return ok;
}

static bool bq_broker_accounts_receipt(BqBrokerAccounts const* accounts)
{
    static char const* const components[] = {"etc", "buster-bench"};
    int directory = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = directory >= 0;
    for (unsigned index = 0; ok && index <= sizeof(components) / sizeof(components[0]); index += 1)
    {
        struct stat info;
        ok = fstat(directory, &info) == 0 && S_ISDIR(info.st_mode) &&
             info.st_uid == 0 && !(info.st_mode & 0022);
        if (ok && index < sizeof(components) / sizeof(components[0]))
        {
            int next = openat(directory, components[index], O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            close(directory);
            directory = next;
            ok = directory >= 0;
        }
    }
    int descriptor = ok ? openat(directory, "systemd-broker-accounts.identity",
                                  O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat before = {0}, after = {0};
    unsigned char bytes[256];
    ok = ok && descriptor >= 0 && fstat(descriptor, &before) == 0 &&
         S_ISREG(before.st_mode) && before.st_uid == 0 && before.st_nlink == 1 &&
         (before.st_mode & 07777) == 0444 && before.st_size > 0 &&
         (uint64_t)before.st_size < sizeof(bytes);
    size_t used = 0;
    while (ok && used < (size_t)before.st_size)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)before.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        ok = count > 0;
        if (ok) used += (size_t)count;
    }
    unsigned char extra;
    if (ok) ok = read(descriptor, &extra, 1) == 0 && fstat(descriptor, &after) == 0 &&
                 before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
                 before.st_mode == after.st_mode && before.st_uid == after.st_uid &&
                 before.st_gid == after.st_gid && before.st_nlink == after.st_nlink &&
                 before.st_size == after.st_size &&
                 before.st_mtim.tv_sec == after.st_mtim.tv_sec &&
                 before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
                 before.st_ctim.tv_sec == after.st_ctim.tv_sec &&
                 before.st_ctim.tv_nsec == after.st_ctim.tv_nsec &&
                 bq_broker_accounts_receipt_matches(accounts, bytes, used);
    if (descriptor >= 0) close(descriptor);
    if (directory >= 0) close(directory);
    return ok;
}

enum { BQ_BROKER_SERVICE_ACCOUNT, BQ_BROKER_CANDIDATE_ACCOUNT, BQ_BROKER_RUNNER_ACCOUNT };

static bool bq_broker_account_group_set_valid(BqBrokerAccounts const* accounts, int role,
                                               gid_t const* groups, int count)
{
    bool ok = accounts && bq_broker_accounts_valid(accounts) && groups &&
              role >= BQ_BROKER_SERVICE_ACCOUNT && role <= BQ_BROKER_RUNNER_ACCOUNT &&
              count == (role == BQ_BROKER_SERVICE_ACCOUNT ? 2 : 1);
    gid_t primary = 0;
    if (ok)
    {
        if (role == BQ_BROKER_SERVICE_ACCOUNT) primary = accounts->service_gid;
        else if (role == BQ_BROKER_CANDIDATE_ACCOUNT) primary = accounts->candidate_gid;
        else primary = accounts->runner_gid;
    }
    bool primary_seen = false;
    bool candidate_seen = false;
    for (int index = 0; ok && index < count; index += 1)
    {
        gid_t group = groups[index];
        primary_seen |= group == primary;
        candidate_seen |= group == accounts->candidate_gid;
        ok = group == primary ||
             (role == BQ_BROKER_SERVICE_ACCOUNT && group == accounts->candidate_gid);
    }
    return ok && primary_seen && (role != BQ_BROKER_SERVICE_ACCOUNT || candidate_seen);
}

static bool bq_broker_account_groups_valid(BqBrokerAccounts const* accounts,
                                           BqBrokerStartGroups* snapshot)
{
    static char const* const names[] = {"buster-bench", "buster-bench-candidate", "buster-github-runner"};
    bool ok = bq_broker_accounts_valid(accounts) && snapshot;
    if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
    gid_t primary[3] = {0};
    if (ok)
    {
        primary[0] = accounts->service_gid;
        primary[1] = accounts->candidate_gid;
        primary[2] = accounts->runner_gid;
    }
    for (int role = 0; ok && role < 3; role += 1)
    {
        gid_t groups[BQ_BROKER_ACCOUNT_GROUP_LIMIT];
        int count = BQ_BROKER_ACCOUNT_GROUP_LIMIT;
        int found = getgrouplist(names[role], primary[role], groups, &count);
        ok = found > 0 && found == count &&
             bq_broker_account_group_set_valid(accounts, role, groups, count);
        if (ok && role < 2)
        {
            for (int index = 0; index < count; index += 1)
            {
                int position = index;
                while (position > 0 && snapshot->ids[role][position - 1] > groups[index])
                {
                    snapshot->ids[role][position] = snapshot->ids[role][position - 1];
                    position -= 1;
                }
                snapshot->ids[role][position] = groups[index];
            }
            for (int index = 1; ok && index < count; index += 1)
                ok = snapshot->ids[role][index - 1] < snapshot->ids[role][index];
            if (ok) snapshot->count[role] = count;
        }
    }
    if (ok)
    {
        snapshot->uid[0] = accounts->service_uid;
        snapshot->gid[0] = accounts->service_gid;
        snapshot->uid[1] = accounts->candidate_uid;
        snapshot->gid[1] = accounts->candidate_gid;
    }
    return ok;
}

static bool bq_broker_group_set_valid(gid_t const* groups, int count, gid_t service_gid, gid_t candidate_gid)
{
    bool candidate = false;
    bool ok = groups && count >= 0 && count <= 8;
    for (int index = 0; ok && index < count; index += 1)
    {
        candidate |= groups[index] == candidate_gid;
        ok = groups[index] == 0 || groups[index] == service_gid || groups[index] == candidate_gid;
    }
    return ok && candidate;
}

static bool bq_broker_groups_valid(gid_t service_gid, gid_t candidate_gid)
{
    gid_t groups[8];
    int count = getgroups((int)(sizeof(groups) / sizeof(groups[0])), groups);
    bool ok = bq_broker_group_set_valid(groups, count, service_gid, candidate_gid);
    return ok;
}

static bool bq_broker_peer_valid(bool known, uid_t peer_uid, uid_t service_uid)
{
    bool ok = known && peer_uid == service_uid;
    return ok;
}

static int bq_broker_server(void)
{
    BqBrokerDiagnostic diagnostic;
    int diagnostic_errno = errno;
    bq_broker_diag_snapshot(&diagnostic, STDERR_FILENO);
    errno = diagnostic_errno;
    struct ucred peer = {0};
    socklen_t peer_size = sizeof(peer);
    BqBrokerAccounts accounts = {.service_uid = (uid_t)-1, .service_gid = (gid_t)-1,
                                 .candidate_uid = (uid_t)-1, .candidate_gid = (gid_t)-1,
                                 .runner_uid = (uid_t)-1, .runner_gid = (gid_t)-1};
    struct passwd* account = getpwnam("buster-bench");
    if (account) { accounts.service_uid = account->pw_uid; accounts.service_gid = account->pw_gid; }
    account = getpwnam("buster-bench-candidate");
    if (account) { accounts.candidate_uid = account->pw_uid; accounts.candidate_gid = account->pw_gid; }
    account = getpwnam("buster-github-runner");
    if (account) { accounts.runner_uid = account->pw_uid; accounts.runner_gid = account->pw_gid; }
    bool credentials = geteuid() == 0 && getegid() == accounts.service_gid &&
                       bq_broker_accounts_valid(&accounts) &&
                       bq_broker_groups_valid(accounts.service_gid, accounts.candidate_gid);
    bool peer_known = credentials &&
                      getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) == 0 &&
                      peer_size == sizeof(peer);
    bool ok = bq_broker_peer_valid(peer_known, peer.uid, accounts.service_uid);
    BqBrokerRequest request = {0};
    bool poll_called = ok;
    if (ok)
    {
        struct pollfd ready = {.fd = STDIN_FILENO, .events = POLLIN};
        ok = poll(&ready, 1, 5000) == 1;
    }
    struct iovec data = {.iov_base = &request, .iov_len = sizeof(request)};
    struct msghdr message = {.msg_iov = &data, .msg_iovlen = 1};
    bool recv_called = ok;
    ssize_t received = recv_called ? recvmsg(STDIN_FILENO, &message, 0) : -1;
    bool parsed = ok && received == sizeof(request) && !(message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
                  bq_broker_request_valid(&request);
    ok = parsed;
    /* A signal can still clean an exact old unit if account membership changed.
     * A fresh start must reject contaminated NSS groups before manager launch. */
    BqBrokerStartGroups start_groups = {0};
    if (ok && (request.operation == BQ_BROKER_START || request.signal_number == BQ_BROKER_CONT))
        ok = bq_broker_account_groups_valid(&accounts, &start_groups);
    if (ok && (request.operation == BQ_BROKER_START || request.signal_number == BQ_BROKER_CONT))
        ok = bq_broker_accounts_receipt(&accounts);
    bool state_checked = ok;
    if (ok) ok = bq_broker_state(&request, accounts.service_uid, accounts.service_gid, accounts.candidate_gid);
    bool state_valid = state_checked && ok;
    bool signal_checked = ok && request.operation == BQ_BROKER_SIGNAL;
    if (signal_checked) ok = bq_broker_signal_identity(&request, &start_groups);
    bool signal_valid = signal_checked && ok;
    BqBrokerCommand command;
    bool command_checked = ok;
    if (ok) ok = bq_broker_command(&request, &start_groups, &command);
    bool command_valid = command_checked && ok;
    bq_broker_diag_request(&diagnostic, peer_known, peer, poll_called, recv_called,
                           received, message.msg_flags, parsed, state_checked, state_valid,
                           signal_checked, signal_valid, command_checked, command_valid, &request);
    struct timeval timeout = {.tv_sec = 5};
    if (ok) ok = setsockopt(STDIN_FILENO, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
    int result = 1;
    int32_t framed_status = 126;
    bool frame_sent = false;
    if (ok)
        result = bq_broker_execute(&command, STDIN_FILENO, request.operation == BQ_BROKER_SIGNAL,
                                   &framed_status, &frame_sent);
    else
    {
        int32_t status = 126;
        frame_sent = bq_broker_send_frame(STDIN_FILENO, BQ_BROKER_STATUS, &status, sizeof(status));
    }
    bq_broker_diag_outcome(&diagnostic, result, framed_status, frame_sent);
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
    for (uint32_t index = 1; !found && index <= BQ_NATIVE_STAGE; index += 1)
    {
        if (bq_broker_stage_name(index) && !strcmp(name, bq_broker_stage_name(index)))
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
    request->version = BQ_BROKER_VERSION;
    bool ok = false;
    if (argc == 6 && (!strcmp(argv[1], "start-outer") || !strcmp(argv[1], BQ_ZEN5_STAGE_OUTER_VERB) || !strcmp(argv[1], BQ_NATIVE_OUTER_VERB)))
    {
        request->operation = BQ_BROKER_START;
        request->recipe = !strcmp(argv[1], BQ_NATIVE_OUTER_VERB) ? BQ_BROKER_RECIPE_NATIVE :
                          !strcmp(argv[1], BQ_ZEN5_STAGE_OUTER_VERB) ? BQ_BROKER_RECIPE_ZEN5 : BQ_BROKER_RECIPE_SMOKE;
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
            request->recipe = request->stage == BQ_NATIVE_STAGE ? BQ_BROKER_RECIPE_NATIVE :
                              bq_broker_zen5_stage(request->stage) ? BQ_BROKER_RECIPE_ZEN5 : BQ_BROKER_RECIPE_SMOKE;
        }
    }
    else if (argc == 4 && !strcmp(argv[1], "signal"))
    {
        ok = bq_broker_unit_from_text(argv[2], request) &&
             (!strcmp(argv[3], "TERM") || !strcmp(argv[3], "KILL") || !strcmp(argv[3], "CONT"));
        if (ok) request->signal_number = !strcmp(argv[3], "TERM") ? BQ_BROKER_TERM :
                                         !strcmp(argv[3], "KILL") ? BQ_BROKER_KILL : BQ_BROKER_CONT;
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

static unsigned bq_broker_property_count(BqBrokerCommand const* command, char const* prefix)
{
    unsigned count = 0;
    size_t length = strlen(prefix);
    for (unsigned index = 0; index < command->count; index += 1)
        if (!strncmp(command->argv[index], prefix, length)) count += 1;
    return count;
}

/* zen5-calibration-v1 requests: the recipe owns its stages, one source, the
 * exact zen5_stage.h payload, writable path, account and perf allowance, and
 * the exact stage identity for recovery signals. `request` is restored to a
 * smoke start of job 1 attempt 2 by the caller. */
static bool bq_broker_zen5_self_test(BqBrokerRequest* request, BqBrokerStartGroups const* groups)
{
    unsigned checks = 0;
    bool ok = true;
#define BQ_ZEN5_CHECK(condition) do { checks += 1; if (!(condition)) ok = false; } while (0)
    BqBrokerCommand command;
    BqBrokerPaths paths;
    char revision[65] = {0};
    memset(revision, 'a', 40);
    request->operation = BQ_BROKER_START;
    request->signal_number = 0;
    memcpy(request->base, revision, sizeof(revision));
    memcpy(request->candidate, revision, sizeof(revision));
    request->recipe = BQ_BROKER_RECIPE_ZEN5;
    request->stage = BQ_BROKER_OUTER;
    BQ_ZEN5_CHECK(bq_broker_command(request, groups, &command) && bq_broker_has_argument(&command, "worker-unit") &&
                  bq_broker_has_argument(&command, BQ_ZEN5_STAGE_RECIPE) &&
                  !bq_broker_has_argument(&command, "validate-buster-v1") &&
                  bq_broker_has_argument(&command, BQ_BROKER_RUNTIME_MAX));
    request->candidate[0] = 'b';
    BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
    request->candidate[0] = 'a';
    for (uint32_t stage = BQ_BROKER_ZEN5_FIRST_STAGE; stage <= BQ_BROKER_ZEN5_LAST_STAGE; stage += 1)
    {
        uint32_t index = stage - BQ_BROKER_ZEN5_FIRST_STAGE;
        BqZen5StageCommand expected;
        char number[16], writable[600], directory[600], unit[256], hidden[900];
        request->stage = stage;
        bool built = bq_broker_command(request, groups, &command) && bq_broker_paths(request, &paths) &&
                     bq_zen5_stage_command(index, paths.attempt, &expected) &&
                     bq_broker_format(number, sizeof(number), "%u", stage) &&
                     bq_broker_format(writable, sizeof(writable), "--property=ReadWritePaths=%s", expected.writable) &&
                     bq_broker_format(directory, sizeof(directory), "--working-directory=%s", expected.directory) &&
                     bq_broker_format(unit, sizeof(unit), "--unit=buster-bench-1-2-%s.service",
                                      bq_broker_zen5_stages[index]) &&
                     bq_broker_format(hidden, sizeof(hidden), "--property=InaccessiblePaths=%s %s %s %s",
                                      BQ_BROKER_QUEUE, BQ_BROKER_LEASE, paths.result, BQ_BROKER_CONTROL_DIRECTORIES);
        unsigned gate = 0;
        while (built && gate < command.count && strcmp(command.argv[gate], BQ_BROKER_GATE)) gate += 1;
        BQ_ZEN5_CHECK(built && command.count < BQ_BROKER_MAX_ARGS && gate + 6 + expected.count == command.count &&
                      !strcmp(command.argv[gate + 1], number) &&
                      !strcmp(command.argv[gate + 4], expected.service ? "65000,65001" : "65001") &&
                      bq_broker_has_argument(&command, unit) && bq_broker_has_argument(&command, writable) &&
                      bq_broker_has_argument(&command, hidden) &&
                      bq_broker_property_count(&command, "--property=InaccessiblePaths=") == 1 &&
                      bq_broker_argument_count(&command, writable) == 1 &&
                      bq_broker_property_count(&command, "--property=ReadWritePaths=") == 1 &&
                      bq_broker_has_argument(&command, directory) && bq_broker_has_argument(&command, "--pipe") &&
                      bq_broker_has_argument(&command, expected.service ? "--uid=buster-bench" :
                                                                          "--uid=buster-bench-candidate") &&
                      bq_broker_has_argument(&command, "--property=SystemCallFilter=@system-service") &&
                      bq_broker_has_argument(&command, "--property=SystemCallFilter=" BQ_ZEN5_STAGE_PERF_SYSCALL) ==
                      (index == BQ_ZEN5_STAGE_PMU));
        for (unsigned argument = 0; built && argument < expected.count; argument += 1)
            BQ_ZEN5_CHECK(!strcmp(command.argv[gate + 6 + argument], expected.argv[argument]));
        char required[BQ_ZEN5_STAGE_TEXT] = {0};
        BQ_ZEN5_CHECK(built && bq_broker_start_writable(request, &paths, required, sizeof(required)) == 1 &&
                      !strcmp(required, expected.writable));
        BQ_ZEN5_CHECK(!strcmp(bq_broker_stage_program(stage, false), expected.argv[0]) &&
                      !strcmp(bq_broker_stage_program(stage, true), expected.argv[1]) &&
                      bq_broker_service_stage(stage) == expected.service);
        request->recipe = BQ_BROKER_RECIPE_SMOKE;
        BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
        request->recipe = BQ_BROKER_RECIPE_ZEN5;
    }
    request->stage = BQ_BROKER_BASE_GENERATE;
    BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
    char required[BQ_ZEN5_STAGE_TEXT] = {0};
    BQ_ZEN5_CHECK(bq_broker_paths(request, &paths) &&
                  bq_broker_start_writable(request, &paths, required, sizeof(required)) == 0);
    request->stage = BQ_BROKER_CANDIDATE_GENERATE;
    BQ_ZEN5_CHECK(bq_broker_paths(request, &paths) &&
                  bq_broker_start_writable(request, &paths, required, sizeof(required)) == 1 &&
                  !strcmp(required, paths.candidate_staging));
    request->stage = BQ_BROKER_OUTER;
    BQ_ZEN5_CHECK(bq_broker_paths(request, &paths) &&
                  bq_broker_start_writable(request, &paths, required, sizeof(required)) == 0);
    request->stage = BQ_BROKER_OUTER;
    request->recipe = BQ_BROKER_RECIPE_RETIREMENT;
    BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
    request->recipe = BQ_BROKER_RECIPE_ZEN5 + 1;
    BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
    request->recipe = BQ_BROKER_RECIPE_ZEN5;
    request->runtime_max_usec = 1;
    BQ_ZEN5_CHECK(!bq_broker_command(request, groups, &command));
    request->runtime_max_usec = 0;
    /* Recovery signals name the unit; they always carry the smoke selector. */
    BqBrokerRequest parsed;
    BQ_ZEN5_CHECK(bq_broker_unit_from_text("buster-bench-1-2-zen5-pmu.service", &parsed) &&
                  parsed.stage == BQ_BROKER_ZEN5_FIRST_STAGE + BQ_ZEN5_STAGE_PMU && parsed.recipe == 0 &&
                  parsed.version == BQ_BROKER_VERSION);
    parsed.signal_number = BQ_BROKER_TERM;
    BQ_ZEN5_CHECK(bq_broker_command(&parsed, groups, &command) && bq_broker_has_argument(&command, "--signal=TERM") &&
                  bq_broker_has_argument(&command, "buster-bench-1-2-zen5-pmu.service"));
    parsed.recipe = BQ_BROKER_RECIPE_ZEN5;
    BQ_ZEN5_CHECK(!bq_broker_command(&parsed, groups, &command));
    parsed.recipe = BQ_BROKER_RECIPE_SMOKE;
    parsed.runtime_max_usec = 1;
    BQ_ZEN5_CHECK(!bq_broker_command(&parsed, groups, &command));
    parsed.runtime_max_usec = 0;
    char const* pmu_show = "ExecStart={ path=" BQ_BROKER_GATE " ; argv[]=" BQ_BROKER_GATE
                           " 27 65001 65001 65001 -- " BQ_ZEN5_STAGE_PYTHON " -B fixed ; }\n";
    char const* build_show = "ExecStart={ path=" BQ_BROKER_GATE " ; argv[]=" BQ_BROKER_GATE
                             " 16 65000 65000 65000,65001 -- " BQ_ZEN5_STAGE_DRIVER " generate fixed ; }\n";
    BQ_ZEN5_CHECK(bq_broker_exec_identity(pmu_show, &parsed, groups) &&
                  !bq_broker_exec_identity(build_show, &parsed, groups));
    parsed.stage = BQ_BROKER_ZEN5_FIRST_STAGE;
    BQ_ZEN5_CHECK(bq_broker_exec_identity(build_show, &parsed, groups) &&
                  !bq_broker_exec_identity(pmu_show, &parsed, groups));
    /* An ungated ExecStart identifies only a smoke stage (the legacy path). */
    char const* ungated_build = "ExecStart={ path=" BQ_ZEN5_STAGE_DRIVER " ; argv[]=" BQ_ZEN5_STAGE_DRIVER
                                " generate fixed ; }\n";
    parsed.signal_number = BQ_BROKER_KILL;
    BQ_ZEN5_CHECK(!bq_broker_exec_identity(ungated_build, &parsed, groups));
    parsed.stage = BQ_BROKER_BASE_GENERATE;
    BQ_ZEN5_CHECK(bq_broker_exec_identity(ungated_build, &parsed, groups));
    parsed.stage = BQ_BROKER_ZEN5_FIRST_STAGE;
    parsed.signal_number = BQ_BROKER_TERM;
    char* outer_cli[] = {"broker", BQ_ZEN5_STAGE_OUTER_VERB, "1", "2", revision, revision};
    BQ_ZEN5_CHECK(bq_broker_cli(6, outer_cli, &parsed) && parsed.recipe == BQ_BROKER_RECIPE_ZEN5 &&
                  parsed.stage == BQ_BROKER_OUTER);
    char other[65] = {0};
    memset(other, 'b', 40);
    outer_cli[5] = other;
    BQ_ZEN5_CHECK(!bq_broker_cli(6, outer_cli, &parsed));
    char* stage_cli[] = {"broker", "start-stage", "1", "2", "zen5-captures", revision, revision};
    BQ_ZEN5_CHECK(bq_broker_cli(7, stage_cli, &parsed) && parsed.recipe == BQ_BROKER_RECIPE_ZEN5 &&
                  parsed.stage == BQ_BROKER_ZEN5_FIRST_STAGE + BQ_ZEN5_STAGE_CAPTURES);
    stage_cli[4] = "zen5-shell";
    BQ_ZEN5_CHECK(!bq_broker_cli(7, stage_cli, &parsed));
    char* smoke_cli[] = {"broker", "start-outer", "1", "2", revision, other};
    BQ_ZEN5_CHECK(bq_broker_cli(6, smoke_cli, &parsed) && parsed.recipe == BQ_BROKER_RECIPE_SMOKE);
    printf("BQ_BROKER_ZEN5_SELF_TEST checks=%u result=%s\n", checks, ok ? "pass" : "fail");
    memset(request->base, 'a', 40);
    memset(request->candidate, 'b', 40);
    return ok;
#undef BQ_ZEN5_CHECK
}

static int bq_broker_self_test(void)
{
    BqBrokerRequest request = {.magic = BQ_BROKER_MAGIC, .version = BQ_BROKER_VERSION, .operation = BQ_BROKER_START,
                               .job = 1, .attempt = 2};
    memset(request.base, 'a', 40);
    memset(request.candidate, 'b', 40);
    BqBrokerCommand command;
    unsigned checks = 0;
    bool ok = true;
#define BQ_BROKER_CHECK(condition) do { checks += 1; if (!(condition)) ok = false; } while (0)
    BqBrokerAccounts accounts = {.service_uid = 65000, .service_gid = 65000,
                                 .candidate_uid = 65001, .candidate_gid = 65001,
                                 .runner_uid = 65002, .runner_gid = 65002};
    BqBrokerStartGroups start_groups = {.ids = {{65000, 65001}, {65001}},
                                         .count = {2, 1}, .uid = {65000, 65001},
                                         .gid = {65000, 65001}};
    gid_t permitted_groups[] = {0, 65000, 65001};
    gid_t foreign_groups[] = {65000, 65001, 65002};
    BQ_BROKER_CHECK(bq_broker_accounts_valid(&accounts) &&
                    bq_broker_group_set_valid(permitted_groups, 3, accounts.service_gid, accounts.candidate_gid) &&
                    bq_broker_peer_valid(true, accounts.service_uid, accounts.service_uid));
    BQ_BROKER_CHECK(!bq_broker_peer_valid(true, 0, accounts.service_uid) &&
                    !bq_broker_peer_valid(true, accounts.candidate_uid, accounts.service_uid) &&
                    !bq_broker_peer_valid(false, accounts.service_uid, accounts.service_uid));
    BQ_BROKER_CHECK(!bq_broker_group_set_valid(foreign_groups, 3, accounts.service_gid, accounts.candidate_gid) &&
                    !bq_broker_group_set_valid(permitted_groups, 2, accounts.service_gid, accounts.candidate_gid) &&
                    !bq_broker_group_set_valid(NULL, -1, accounts.service_gid, accounts.candidate_gid));
    gid_t service_groups[] = {65000, 65001};
    gid_t candidate_groups[] = {65001};
    gid_t runner_groups[] = {65002};
    BQ_BROKER_CHECK(bq_broker_account_group_set_valid(&accounts, BQ_BROKER_SERVICE_ACCOUNT,
                                                     service_groups, 2) &&
                    bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                     candidate_groups, 1) &&
                    bq_broker_account_group_set_valid(&accounts, BQ_BROKER_RUNNER_ACCOUNT,
                                                     runner_groups, 1) &&
                    bq_broker_group_set_valid(permitted_groups, 3, accounts.service_gid, accounts.candidate_gid));
    gid_t service_without_staging[] = {65000};
    gid_t service_with_root[] = {65000, 65001, 0};
    gid_t service_with_foreign[] = {65000, 65001, 65003};
    BQ_BROKER_CHECK(!bq_broker_account_group_set_valid(&accounts, BQ_BROKER_SERVICE_ACCOUNT,
                                                      service_without_staging, 1) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_SERVICE_ACCOUNT,
                                                      service_with_root, 3) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_SERVICE_ACCOUNT,
                                                      service_with_foreign, 3));
    gid_t candidate_service[] = {65001, 65000};
    gid_t candidate_root[] = {65001, 0};
    gid_t candidate_without_primary[] = {65003};
    gid_t candidate_with_foreign[] = {65001, 65003};
    BQ_BROKER_CHECK(!bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_service, 2) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_root, 2) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_without_primary, 1) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_with_foreign, 2));
    gid_t runner_service[] = {65002, 65000};
    gid_t runner_root[] = {65002, 0};
    gid_t runner_candidate[] = {65002, 65001};
    gid_t runner_with_foreign[] = {65002, 65003};
    BQ_BROKER_CHECK(!bq_broker_account_group_set_valid(&accounts, BQ_BROKER_RUNNER_ACCOUNT,
                                                      runner_service, 2) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_RUNNER_ACCOUNT,
                                                      runner_root, 2) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_RUNNER_ACCOUNT,
                                                      runner_candidate, 2) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_RUNNER_ACCOUNT,
                                                      runner_with_foreign, 2));
    BQ_BROKER_CHECK(!bq_broker_account_group_set_valid(NULL, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_groups, 1) &&
                    !bq_broker_account_group_set_valid(&accounts, -1, candidate_groups, 1) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT, NULL, 1) &&
                    !bq_broker_account_group_set_valid(&accounts, BQ_BROKER_CANDIDATE_ACCOUNT,
                                                      candidate_groups, BQ_BROKER_ACCOUNT_GROUP_LIMIT + 1));
    accounts.service_uid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.service_uid = 65000;
    accounts.service_gid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.service_gid = 65000;
    accounts.candidate_uid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.candidate_uid = 65001;
    accounts.candidate_gid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.candidate_gid = 65001;
    accounts.runner_uid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_uid = 65002;
    accounts.runner_gid = 0;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_gid = 65002;
    accounts.candidate_uid = accounts.service_uid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.candidate_uid = 65001;
    accounts.candidate_gid = accounts.service_gid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.candidate_gid = 65001;
    accounts.runner_uid = accounts.service_uid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_uid = 65002;
    accounts.runner_uid = accounts.candidate_uid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_uid = 65002;
    accounts.runner_gid = accounts.candidate_gid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_gid = 65002;
    accounts.runner_gid = accounts.service_gid;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_gid = 65002;
    accounts.service_gid = (gid_t)-1;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.service_gid = 65000;
    accounts.candidate_uid = (uid_t)-1;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.candidate_uid = 65001;
    accounts.runner_gid = (gid_t)-1;
    BQ_BROKER_CHECK(!bq_broker_accounts_valid(&accounts));
    accounts.runner_gid = 65002;
    char const* gated_show = "ExecStart={ path=" BQ_BROKER_GATE " ; argv[]=" BQ_BROKER_GATE
                            " 0 65000 65000 65000,65001 -- " BQ_BROKER_SERVICE " worker-unit fixed ; ignore_errors=no }\n";
    char const* legacy_outer_show = "ExecStart={ path=" BQ_BROKER_SERVICE " ; argv[]=fixed }\n";
    char const* legacy_build_show = "ExecStart={ path=" BQ_BROKER_BUILD " ; argv[]=fixed }\n";
    char const* legacy_throughput_show = "ExecStart={ path=" BQ_BROKER_THROUGHPUT " ; argv[]=fixed }\n";
    request.operation = BQ_BROKER_SIGNAL;
    request.signal_number = BQ_BROKER_CONT;
    request.stage = BQ_BROKER_OUTER;
    BQ_BROKER_CHECK(bq_broker_exec_identity(gated_show, &request, &start_groups) &&
                    !bq_broker_exec_identity(legacy_outer_show, &request, &start_groups));
    start_groups.uid[0] = 65003;
    BQ_BROKER_CHECK(!bq_broker_exec_identity(gated_show, &request, &start_groups));
    start_groups.uid[0] = 65000;
    request.stage = BQ_BROKER_BASE_GENERATE;
    BQ_BROKER_CHECK(!bq_broker_exec_identity(gated_show, &request, &start_groups));
    request.stage = BQ_BROKER_OUTER;
    char const* wrong_verb = "ExecStart={ path=" BQ_BROKER_GATE " ; argv[]=" BQ_BROKER_GATE
                            " 0 65000 65000 65000,65001 -- " BQ_BROKER_SERVICE " worker-unit-extra fixed ; }\n";
    BQ_BROKER_CHECK(!bq_broker_exec_identity(wrong_verb, &request, &start_groups));
    char const* split_line = "ExecStart={ path=" BQ_BROKER_GATE " ; argv[]=" BQ_BROKER_GATE
                            " 0 65000 65000 65000,65001 --\n " BQ_BROKER_SERVICE " worker-unit fixed ; }\n";
    BQ_BROKER_CHECK(!bq_broker_exec_identity(split_line, &request, &start_groups));
    request.signal_number = BQ_BROKER_TERM;
    BQ_BROKER_CHECK(bq_broker_exec_identity(gated_show, &request, &start_groups) &&
                    bq_broker_exec_identity(legacy_outer_show, &request, &start_groups) &&
                    !bq_broker_exec_identity(legacy_build_show, &request, &start_groups));
    request.stage = BQ_BROKER_CANDIDATE_BUILD;
    BQ_BROKER_CHECK(bq_broker_exec_identity(legacy_build_show, &request, &start_groups) &&
                    !bq_broker_exec_identity(legacy_throughput_show, &request, &start_groups));
    request.stage = BQ_BROKER_THROUGHPUT_STAGE;
    request.signal_number = BQ_BROKER_KILL;
    BQ_BROKER_CHECK(bq_broker_exec_identity(legacy_throughput_show, &request, &start_groups) &&
                    !bq_broker_exec_identity(legacy_build_show, &request, &start_groups));
    request.operation = BQ_BROKER_START;
    request.signal_number = 0;
    for (uint32_t stage = 0; stage <= BQ_BROKER_THROUGHPUT_STAGE; stage += 1)
    {
        request.stage = stage;
        BQ_BROKER_CHECK(bq_broker_command(&request, &start_groups, &command));
        BqBrokerPaths paths;
        BQ_BROKER_CHECK(bq_broker_paths(&request, &paths));
        char unit_option[256], parent_option[256], result_option[600];
        BQ_BROKER_CHECK(bq_broker_format(unit_option, sizeof(unit_option), "--unit=%s", paths.unit) &&
                        bq_broker_format(parent_option, sizeof(parent_option), "--property=PartOf=%s", paths.parent) &&
                        bq_broker_format(result_option, sizeof(result_option),
                                         "--property=InaccessiblePaths=%s %s %s %s", BQ_BROKER_QUEUE,
                                         BQ_BROKER_LEASE, paths.result, BQ_BROKER_CONTROL_DIRECTORIES));
        BQ_BROKER_CHECK(command.count < BQ_BROKER_MAX_ARGS && command.argv[0] &&
                        !strcmp(command.argv[0], BQ_BROKER_RUN) && command.argv[command.count] == NULL);
        unsigned gate_index = 0;
        while (gate_index < command.count && strcmp(command.argv[gate_index], BQ_BROKER_GATE)) gate_index += 1;
        unsigned role = stage <= BQ_BROKER_BASE_BUILD ? 0u : 1u;
        BQ_BROKER_CHECK(gate_index + 7 < command.count &&
                        !strcmp(command.argv[gate_index + 1], stage == 0 ? "0" :
                                stage == 1 ? "1" : stage == 2 ? "2" :
                                stage == 3 ? "3" : stage == 4 ? "4" : "5") &&
                        !strcmp(command.argv[gate_index + 2], role ? "65001" : "65000") &&
                        !strcmp(command.argv[gate_index + 3], role ? "65001" : "65000") &&
                        !strcmp(command.argv[gate_index + 4], role ? "65001" : "65000,65001") &&
                        !strcmp(command.argv[gate_index + 5], "--") &&
                        !strcmp(command.argv[gate_index + 6], stage == BQ_BROKER_OUTER ?
                                BQ_BROKER_SERVICE : stage == BQ_BROKER_THROUGHPUT_STAGE ?
                                BQ_BROKER_THROUGHPUT : BQ_BROKER_BUILD));
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, unit_option) &&
                        bq_broker_argument_count(&command, unit_option) == 1 &&
                        bq_broker_argument_count(&command, "--property=NoNewPrivileges=yes") == 1);
        BQ_BROKER_CHECK(bq_broker_property_count(&command, "--property=CapabilityBoundingSet=") == 1 &&
                        bq_broker_argument_count(&command, "--property=CapabilityBoundingSet=") == 1 &&
                        bq_broker_property_count(&command, "--property=AmbientCapabilities=") == 1 &&
                        bq_broker_argument_count(&command, "--property=AmbientCapabilities=") == 1);
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--slice=buster-bench.slice") &&
                        bq_broker_has_argument(&command, "--setenv=PATH=/usr/bin:/bin") &&
                        bq_broker_has_argument(&command, "--setenv=LC_ALL=C") &&
                        bq_broker_has_argument(&command, "--property=AllowedCPUs=2") &&
                        bq_broker_has_argument(&command, "--property=NoNewPrivileges=yes") &&
                        bq_broker_has_argument(&command, "--property=ProtectSystem=strict") &&
                        bq_broker_has_argument(&command, "--property=PrivateNetwork=yes"));
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--service-output") ==
                        (stage == BQ_BROKER_THROUGHPUT_STAGE));
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--property=CollectMode=inactive") ==
                        (stage == BQ_BROKER_OUTER));
        BQ_BROKER_CHECK(bq_broker_has_argument(&command, "--collect") ==
                        (stage != BQ_BROKER_OUTER));
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
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.stage = BQ_NATIVE_STAGE + 1;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    BQ_BROKER_CHECK(bq_broker_zen5_self_test(&request, &start_groups));
    BqBrokerRequest native = {.magic = BQ_BROKER_MAGIC, .version = BQ_BROKER_VERSION,
        .operation = BQ_BROKER_START, .recipe = BQ_BROKER_RECIPE_NATIVE, .job = 1, .attempt = 2};
    memset(native.base, 'a', 64); memset(native.candidate, 'a', 64);
    BQ_BROKER_CHECK(bq_broker_command(&native, &start_groups, &command) &&
        bq_broker_has_argument(&command, BQ_NATIVE_RECIPE) && bq_broker_has_argument(&command, "worker-unit") &&
        !bq_broker_has_argument(&command, BQ_BROKER_BUILD));
    native.stage = BQ_NATIVE_STAGE;
    BQ_BROKER_CHECK(bq_broker_command(&native, &start_groups, &command) &&
        bq_broker_has_argument(&command, "--uid=buster-bench-candidate") &&
        bq_broker_has_argument(&command, "native-payload") && !bq_broker_has_argument(&command, "worker-unit") &&
        bq_broker_has_argument(&command, "--property=ReadWritePaths=/var/lib/buster-bench/workspaces/job-1-attempt-2/native-scratch") &&
        bq_broker_has_argument(&command, "--property=ReadOnlyPaths=/var/lib/buster-bench/workspaces/job-1-attempt-2 /opt/buster-bench/installed") &&
        bq_broker_has_argument(&command, "--property=PrivateNetwork=yes") &&
        bq_broker_has_argument(&command, "--property=CapabilityBoundingSet=") &&
        bq_broker_has_argument(&command, "--property=SystemCallFilter=@system-service") &&
        bq_broker_property_count(&command, "--property=ReadWritePaths=") == 1);
    BqBrokerRequest refused = native;
    refused.recipe = BQ_BROKER_RECIPE_SMOKE;
    BQ_BROKER_CHECK(!bq_broker_command(&refused, &start_groups, &command));
    refused = native; refused.stage = BQ_BROKER_THROUGHPUT_STAGE;
    BQ_BROKER_CHECK(!bq_broker_command(&refused, &start_groups, &command));
    refused = native; refused.candidate[0] = 'b';
    BQ_BROKER_CHECK(!bq_broker_command(&refused, &start_groups, &command));
    refused = native; refused.base[40] = 0; refused.candidate[40] = 0;
    BQ_BROKER_CHECK(!bq_broker_command(&refused, &start_groups, &command));
    refused = native; refused.runtime_max_usec = 1;
    BQ_BROKER_CHECK(!bq_broker_command(&refused, &start_groups, &command));
    BqBrokerRequest cleanup;
    BQ_BROKER_CHECK(bq_broker_unit_from_text("buster-bench-1-2-native-execute.service", &cleanup) &&
                   cleanup.stage == BQ_NATIVE_STAGE);
    cleanup.signal_number = BQ_BROKER_TERM;
    BQ_BROKER_CHECK(bq_broker_command(&cleanup, &start_groups, &command) &&
                   bq_broker_has_argument(&command, "--signal=TERM"));
    cleanup.signal_number = BQ_BROKER_CONT;
    BQ_BROKER_CHECK(!bq_broker_command(&cleanup, &start_groups, &command));

    request.recipe = BQ_BROKER_RECIPE_SMOKE;
    request.stage = BQ_BROKER_OUTER;
    request.base[0] = '/';
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.base[0] = 'a';
    request.candidate[40] = 'c';
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.candidate[40] = 0;
    request.magic = 0;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.magic = BQ_BROKER_MAGIC;
    request.version = 1;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.version = BQ_BROKER_VERSION;
    request.attempt = 0;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.attempt = 2;
    request.operation = BQ_BROKER_SIGNAL;
    request.signal_number = BQ_BROKER_TERM;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    memset(request.base, 0, sizeof(request.base));
    memset(request.candidate, 0, sizeof(request.candidate));
    BQ_BROKER_CHECK(bq_broker_command(&request, &start_groups, &command) &&
                    bq_broker_has_argument(&command, "--signal=TERM") &&
                    !bq_broker_has_argument(&command, "--signal=KILL"));
    request.signal_number = BQ_BROKER_CONT;
    BQ_BROKER_CHECK(bq_broker_command(&request, &start_groups, &command) &&
                    bq_broker_has_argument(&command, "--signal=CONT") &&
                    !bq_broker_has_argument(&command, "--signal=KILL"));
    request.stage = BQ_BROKER_BASE_BUILD;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
    request.stage = BQ_BROKER_OUTER;
    request.signal_number = 4;
    BQ_BROKER_CHECK(!bq_broker_command(&request, &start_groups, &command));
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
        int executed = bq_broker_execute(&test_command, connection[0], false, NULL, NULL);
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
        BQ_BROKER_CHECK(bq_broker_execute(&test_command, connection[0], true, NULL, NULL) == 0);
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
                        !bq_broker_regular(source, "manifest", geteuid(), (gid_t)-1, 0,
                                           true, buffer, sizeof(buffer), &size));
        if (paths) unlink(hardlink);
        BQ_BROKER_CHECK(created && bq_broker_regular(source, "manifest", geteuid(), (gid_t)-1, 0, true,
                                                     buffer, sizeof(buffer), &size) && size == 2);
        if (paths)
        {
            unlink(alias);
            unlink(manifest);
            rmdir(source);
        }
        rmdir(root);
    }
    int journal[2] = {-1, -1}, accepted[2] = {-1, -1};
    bool diagnostic_ready = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, journal) == 0 &&
                            socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, accepted) == 0;
    BQ_BROKER_CHECK(diagnostic_ready);
    if (diagnostic_ready)
    {
        int saved_input = dup(STDIN_FILENO);
        BQ_BROKER_CHECK(dup2(accepted[0], STDIN_FILENO) == STDIN_FILENO);
        BqBrokerDiagnostic diagnostic;
        bq_broker_diag_snapshot(&diagnostic, journal[0]);
        BQ_BROKER_CHECK(diagnostic.snapshot_complete && diagnostic.stream_ok &&
                        diagnostic.pid == getpid() && diagnostic.start_ticks > 0 &&
                        diagnostic.output_bytes < BQ_BROKER_DIAG_OUTPUT);
        struct ucred test_peer = {.pid = getpid(), .uid = getuid(), .gid = getgid()};
        bq_broker_diag_request(&diagnostic, true, test_peer, false, false, -1, 0,
                               false, false, false, false, false, false, false, &request);
        bq_broker_diag_outcome(&diagnostic, 1, 126, true);
        char journal_bytes[BQ_BROKER_DIAG_OUTPUT + 1];
        size_t journal_size = 0;
        ssize_t count = 0;
        do
        {
            count = recv(journal[1], journal_bytes + journal_size,
                         sizeof(journal_bytes) - 1 - journal_size, MSG_DONTWAIT);
            if (count > 0) journal_size += (size_t)count;
        } while (count > 0 && journal_size < sizeof(journal_bytes) - 1);
        journal_bytes[journal_size] = 0;
        BQ_BROKER_CHECK(diagnostic.stream_ok && strstr(journal_bytes, "BQ-BROKER-DIAG-V1 BEGIN") &&
                        strstr(journal_bytes, "field=status") && strstr(journal_bytes, "field=mountinfo") &&
                        strstr(journal_bytes, "field=cgroup") && strstr(journal_bytes, "field=stat") &&
                        strstr(journal_bytes, "field=socket") && strstr(journal_bytes, "field=exe") &&
                        strstr(journal_bytes, "SNAPSHOT_END") && strstr(journal_bytes, "elapsed_ms=") &&
                        strstr(journal_bytes, "REQUEST") &&
                        strstr(journal_bytes, "recv_called=0 recv=-1") &&
                        strstr(journal_bytes, "OUTCOME") &&
                        journal_size == diagnostic.output_bytes);
        unsigned char actual[16385];
        size_t actual_size = 0;
        BQ_BROKER_CHECK(bq_broker_diag_read(&diagnostic, "/proc/self/status", actual, 16384,
                                            &actual_size) && actual_size > 0 &&
                        memmem(actual, actual_size, "CapEff:", 7) != NULL &&
                        memmem(actual, actual_size, "NoNewPrivs:", 11) != NULL);
        BQ_BROKER_CHECK(!bq_broker_diag_read(&diagnostic, "/proc/self/mountinfo",
                                              actual, 1, &actual_size));
        if (saved_input >= 0) { dup2(saved_input, STDIN_FILENO); close(saved_input); }
        else close(STDIN_FILENO);
        BqBrokerDiagnostic full = {.stream = journal[0], .pid = getpid(), .start_ticks = 1,
                                   .stream_ok = true, .output_bytes = BQ_BROKER_DIAG_OUTPUT};
        BQ_BROKER_CHECK(!bq_broker_diag_format(&full, "BQ-BROKER-DIAG-V1 test\n"));
        BqBrokerDiagnostic expired = {.stream = journal[0], .pid = getpid(), .start_ticks = 1,
                                      .stream_ok = true,
                                      .spent_milliseconds = BQ_BROKER_DIAG_MILLISECONDS + 1};
        BQ_BROKER_CHECK(!bq_broker_diag_format(&expired, "BQ-BROKER-DIAG-V1 test\n") &&
                        !expired.stream_ok);
        int send_buffer = 4096;
        BQ_BROKER_CHECK(setsockopt(journal[0], SOL_SOCKET, SO_SNDBUF,
                                   &send_buffer, sizeof(send_buffer)) == 0);
        char filler[4096] = {0};
        size_t fill_attempts = 0;
        while (fill_attempts < 256 && send(journal[0], filler, sizeof(filler),
                                           MSG_DONTWAIT | MSG_NOSIGNAL) > 0)
            fill_attempts += 1;
        BqBrokerDiagnostic blocked = {.stream = journal[0], .pid = getpid(), .start_ticks = 1,
                                      .stream_ok = true};
        uint64_t blocked_before = bq_broker_now_milliseconds();
        bool blocked_result = bq_broker_diag_format(&blocked, "BQ-BROKER-DIAG-V1 test\n");
        BQ_BROKER_CHECK(fill_attempts < 256 && !blocked_result && !blocked.stream_ok &&
                        bq_broker_now_milliseconds() - blocked_before < 100);
        BqBrokerDiagnostic blocked_snapshot;
        bq_broker_diag_snapshot(&blocked_snapshot, journal[0]);
        BQ_BROKER_CHECK(!blocked_snapshot.snapshot_complete && !blocked_snapshot.stream_ok);
        close(journal[1]);
        journal[1] = -1;
        BqBrokerDiagnostic closed = {.stream = journal[0], .pid = getpid(), .start_ticks = 1,
                                     .stream_ok = true};
        BQ_BROKER_CHECK(!bq_broker_diag_format(&closed, "BQ-BROKER-DIAG-V1 test\n") &&
                        !closed.stream_ok);
    }
    for (unsigned index = 0; index < 2; index += 1)
    {
        if (journal[index] >= 0) close(journal[index]);
        if (accepted[index] >= 0) close(accepted[index]);
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
