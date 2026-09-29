/* In-unit #509 check runner (#1020, #881 lane B, design step 9).
 *
 * Ownership: executing the installed required-check authority's checks with
 * the unit's held matched binaries, and the durable per-check evidence. The
 * authority itself (bq_retirement_required_checks_import) lives in
 * retirement_correctness_service.c; the gate that joins these results and
 * issues the seal lives in retirement_unit.c.
 *
 * Entry points:
 *   bq_retirement_check_run             one check in a bounded child, then
 *                                       its durable receipt and run record
 *   bq_retirement_check_evidence_closed the evidence directory's exact
 *                                       closure against the required receipts,
 *                                       and the ordered run-record and log
 *                                       aggregate
 *   bq_retirement_check_receipts_hash   the ordered receipt aggregate
 *   bq_retirement_check_descendants_absent
 *                                       no child of this process is alive
 *
 * Map: bq_retirement_check_names names one check's four evidence files and
 * its work directory; bq_retirement_check_resolve turns one authority
 * template into a concrete argument with /proc/self/fd paths;
 * bq_retirement_check_spawn forks the child through
 * bq_retirement_build_child's normalized state (own process group, default
 * signal dispositions, empty mask, umask 0077, close-on-exec from 3) plus
 * RLIMIT_AS, no core and only the held descriptors inherited;
 * bq_retirement_check_wait drains stdout and stderr under the check bound,
 * the job deadline and the cancellation descriptor and reaps the child
 * (bq_retirement_check_reap is the blocking wait after a kill);
 * bq_retirement_check_sweep kills and reaps every descendant the child left
 * (bq_retirement_check_children lists this process's children through /proc);
 * bq_retirement_check_snapshot and bq_retirement_check_unchanged require the
 * held binaries, tools and hosted record to be the same unchanged files
 * after the child; bq_retirement_check_hosted records a hosted check;
 * bq_retirement_check_provenance records the CPU the unit ran on;
 * bq_retirement_check_outcome classifies how the child ended.
 *
 * While a check runs, this process is a child subreaper, so every
 * descendant, including one that left the process group or the session, is
 * reparented here as its ancestors die; the sweep after the child kills and
 * reaps them all, and any found fails the check (a descendant still holding
 * a capture pipe, or a capture overflow, fails it too). The runner refuses
 * to start while this process has any other child. A per-check cgroup v2 leaf
 * (cgroup.kill, memory.events) would be stronger but needs delegation the
 * worker unit does not grant yet. A child killed by SIGKILL that the runner
 * did not send is recorded as out of memory (the kernel's OOM killer in the
 * unit's cgroup); RLIMIT_AS exhaustion shows as the child's own failing exit.
 */
#include "retirement_check_runner.h"
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define BQ_RETIREMENT_CHECK_POLL_MS 10
/* How long captured pipes may stay open after the child's own exit. */
#define BQ_RETIREMENT_CHECK_DRAIN_NS (1000ull * 1000000ull)
#define BQ_RETIREMENT_CHECK_CPUINFO_CAP (256u * 1024u)
#define BQ_RETIREMENT_CHECK_SLOT_BYTES (BQ_RETIREMENT_CHECK_FIELD_CAP + 32u)
#define BQ_RETIREMENT_CHECK_INHERIT_CAP (2u + 2u + BQ_RETIREMENT_CHECK_TOOLS_CAP + 1u)

typedef struct BqRetirementCheckNames
{
    char output[32], log[32], receipt[32], run[32], work[32];
} BqRetirementCheckNames;

BUSTER_GLOBAL_LOCAL bool bq_retirement_check_names(u32 index, BqRetirementCheckNames* names)
{
    int lengths[5] = {snprintf(names->output, sizeof(names->output), "check-output-%04u", index),
                      snprintf(names->log, sizeof(names->log), "check-log-%04u", index),
                      snprintf(names->receipt, sizeof(names->receipt), "check-receipt-%04u", index),
                      snprintf(names->run, sizeof(names->run), "check-run-%04u", index),
                      snprintf(names->work, sizeof(names->work), "check-work-%04u", index)};
    bool ok = index < BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP;
    for (u32 slot = 0; ok && slot < BUSTER_ARRAY_LENGTH(lengths); slot += 1) ok = lengths[slot] > 0 && lengths[slot] < 32;
    return ok;
}

BUSTER_GLOBAL_LOCAL int bq_retirement_check_promote(int descriptor)
{
    int result = descriptor;
    if (descriptor >= 0 && descriptor < 3)
    {
        result = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
        close(descriptor);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BqError bq_retirement_check_stop(int cancellation_fd, u64 deadline_ns)
{
    struct pollfd wait = {.fd = cancellation_fd, .events = POLLIN};
    bool cancelled = cancellation_fd >= 0 && poll(&wait, 1, 0) > 0 && (wait.revents & (POLLIN | POLLHUP | POLLERR));
    BqError result = cancelled ? BQ_WORKER_CANCEL_SIGNAL :
                     bq_retirement_build_clock_ns() >= deadline_ns ? BQ_WORKER_TIMEOUT : BQ_OK;
    return result;
}

/* A new, private, write-only file (O_EXCL), close-on-exec and at least 3. */
BUSTER_GLOBAL_LOCAL int bq_retirement_check_create(int directory, char const* name)
{
    int file = bq_retirement_check_promote(openat(directory, name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    return file;
}

/* Rehash a named, read-only regular file in directory. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_hash_file(int directory, char const* name, u64 cap,
    char digest[SHA256_HEX_CAPACITY])
{
    int file = bq_retirement_check_promote(openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    bool ok = file >= 3 && bq_retirement_oracle_file_hash(file, cap, false, digest);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* Both held matched binaries, rehashed through their descriptors. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_binaries(BqRetirementHeldBinaries const* held,
    char digests[2][SHA256_HEX_CAPACITY])
{
    bool ok = held && held->owned;
    for (u32 side = 0; ok && side < 2; side += 1)
        ok = bq_retirement_oracle_file_hash(held->descriptors[side], BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP, true,
                                            digests[side]);
    return ok;
}

typedef struct BqRetirementCheckCommand
{
    char* arguments[BQ_RETIREMENT_CHECK_ARGUMENTS_CAP + 1];
    char* environment[BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP + 1];
    char directory[32];
    char* storage;
    int inherit[BQ_RETIREMENT_CHECK_INHERIT_CAP];
    u32 argument_count, environment_count, inherit_count;
} BqRetirementCheckCommand;

/* The held descriptor a token names. */
BUSTER_GLOBAL_LOCAL int bq_retirement_check_descriptor(BqRetirementCheckRun const* run, int work, u32 kind, u32 index)
{
    int descriptor = kind == BQ_RETIREMENT_CHECK_TOKEN_BINARY ? run->binaries->descriptors[index] :
                     kind == BQ_RETIREMENT_CHECK_TOKEN_SOURCE ? run->sources[index] :
                     kind == BQ_RETIREMENT_CHECK_TOKEN_TOOL ? run->checks->tools[index] :
                     kind == BQ_RETIREMENT_CHECK_TOKEN_WORK ? work : -1;
    return descriptor;
}

/* One template with its (at most one) token replaced by that descriptor's
 * /proc/self/fd path. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_resolve(char const* template, BqRetirementCheckRun const* run, int work,
    char* output)
{
    char const* opening = strstr(template, "{{");
    u32 kind = 0, index = 0;
    u32 token = opening ? bq_retirement_check_token(opening, run->checks->tool_count, &kind, &index) : 0;
    int descriptor = token ? bq_retirement_check_descriptor(run, work, kind, index) : -1;
    int length = !opening ? snprintf(output, BQ_RETIREMENT_CHECK_SLOT_BYTES, "%s", template) :
                 token && descriptor >= 3 ?
                 snprintf(output, BQ_RETIREMENT_CHECK_SLOT_BYTES, "%.*s/proc/self/fd/%d%s", (int)(opening - template),
                          template, descriptor, opening + token) : -1;
    bool ok = length > 0 && (u32)length < BQ_RETIREMENT_CHECK_SLOT_BYTES;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_check_command(BqRetirementCheckRun const* run, BqRetirementCheckPlan const* plan,
    int work, BqRetirementCheckCommand* command)
{
    u32 slots = plan->argument_count + plan->environment_count;
    command->storage = calloc(slots ? slots : 1u, BQ_RETIREMENT_CHECK_SLOT_BYTES);
    int length = snprintf(command->directory, sizeof(command->directory), "/proc/self/fd/%d", work);
    bool ok = command->storage && length > 0 && (size_t)length < sizeof(command->directory);
    for (u32 index = 0; ok && index < plan->argument_count; index += 1)
    {
        command->arguments[index] = command->storage + (size_t)index * BQ_RETIREMENT_CHECK_SLOT_BYTES;
        ok = bq_retirement_check_resolve(plan->arguments[index], run, work, command->arguments[index]);
    }
    for (u32 index = 0; ok && index < plan->environment_count; index += 1)
    {
        command->environment[index] = command->storage +
                                      (size_t)(plan->argument_count + index) * BQ_RETIREMENT_CHECK_SLOT_BYTES;
        ok = bq_retirement_check_resolve(plan->environment[index], run, work, command->environment[index]);
    }
    command->argument_count = plan->argument_count;
    command->environment_count = plan->environment_count;
    /* Only these survive exec; everything else the service holds is
     * close-on-exec. */
    command->inherit_count = 0;
    for (u32 side = 0; side < 2; side += 1)
    {
        command->inherit[command->inherit_count++] = run->binaries->descriptors[side];
        command->inherit[command->inherit_count++] = run->sources[side];
    }
    for (u32 tool = 0; tool < run->checks->tool_count; tool += 1)
        command->inherit[command->inherit_count++] = run->checks->tools[tool];
    command->inherit[command->inherit_count++] = work;
    return ok;
}

typedef struct BqRetirementCheckChild
{
    Sha256 output_hash, log_hash;
    u64 output_bytes, log_bytes;
    pid_t process;
    int output, log, status;
    u32 overflow, lingering, timed_out, killed;
    bool reaped, output_eof, log_eof;
} BqRetirementCheckChild;

/* A blocking wait for a child already sent SIGKILL. */
BUSTER_GLOBAL_LOCAL void bq_retirement_check_reap(pid_t process, int* status)
{
    int ignored = 0;
    pid_t waited = -1;
    do
    {
        waited = waitpid(process, status ? status : &ignored, 0);
    }
    while (waited < 0 && errno == EINTR);
}

/* This process's direct children, through /proc: up to capacity process
 * numbers into found. Returns their count, or UINT32_MAX when /proc cannot
 * be read. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_check_children(pid_t* found, u32 capacity)
{
    DIR* listing = opendir("/proc");
    u32 count = listing ? 0 : UINT32_MAX;
    long self = (long)getpid();
    bool more = listing != NULL;
    while (more)
    {
        struct dirent* entry = readdir(listing);
        more = entry != NULL;
        char path[288], text[512];
        int length = more && entry->d_name[0] >= '1' && entry->d_name[0] <= '9' ?
                     snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name) : -1;
        int file = length > 0 && (size_t)length < sizeof(path) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
        ssize_t read_bytes = file >= 0 ? read(file, text, sizeof(text) - 1u) : -1;
        if (file >= 0) close(file);
        if (read_bytes > 0)
        {
            text[read_bytes] = 0;
            /* pid (comm) state ppid ...; comm may hold spaces and parens. */
            char const* close_paren = strrchr(text, ')');
            long parent = close_paren && close_paren[1] == ' ' && close_paren[2] && close_paren[3] == ' ' ?
                          strtol(close_paren + 4, NULL, 10) : 0;
            if (parent == self)
            {
                if (count < capacity) found[count] = (pid_t)strtol(entry->d_name, NULL, 10);
                count += 1;
            }
        }
    }
    if (listing) closedir(listing);
    return count;
}

bool bq_retirement_check_descendants_absent(void)
{
    pid_t found[1];
    bool absent = bq_retirement_check_children(found, 0) == 0;
    return absent;
}

/* Kills and reaps every child of this process until none remain. While the
 * runner is a child subreaper, every descendant of a finished check (a
 * setsid escapee included) is reparented here as its ancestors die, so an
 * empty scan proves the check left nothing running. found receives whether
 * any was found; false means some remain after the drain bound or /proc
 * could not be read. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_sweep(bool* found)
{
    u64 deadline = bq_retirement_build_clock_ns() + BQ_RETIREMENT_CHECK_DRAIN_NS;
    bool clean = false, scanned = true;
    *found = false;
    while (!clean && scanned && bq_retirement_build_clock_ns() < deadline)
    {
        pid_t children[64];
        u32 count = bq_retirement_check_children(children, BUSTER_ARRAY_LENGTH(children));
        scanned = count != UINT32_MAX;
        clean = scanned && count == 0;
        *found = *found || (scanned && count > 0);
        for (u32 index = 0; scanned && index < count && index < BUSTER_ARRAY_LENGTH(children); index += 1)
        {
            kill(children[index], SIGKILL);
            bq_retirement_check_reap(children[index], NULL);
        }
    }
    return clean;
}

/* Forks the child: bq_retirement_build_child's normalized state (own process
 * group, /dev/null stdin, default SIGTERM/SIGINT/SIGPIPE/SIGCHLD, an empty
 * signal mask, umask 0077, every descriptor from 3 close-on-exec, the work
 * directory as cwd) with stderr on the log pipe and stdout on the output
 * pipe, then the memory limit without core dumps, and only the command's
 * held descriptors made inheritable. A setup or exec failure is the child's
 * exit 127. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_spawn(BqRetirementCheckCommand const* command, int work,
    u64 memory_bytes, BqRetirementCheckChild* child)
{
    int output[2] = {-1, -1}, log[2] = {-1, -1};
    bool ok = pipe2(output, O_CLOEXEC) == 0 && pipe2(log, O_CLOEXEC) == 0 && output[0] >= 3 && output[1] >= 3 &&
              log[0] >= 3 && log[1] >= 3;
    pid_t process = ok ? fork() : -1;
    if (process == 0)
    {
        struct rlimit memory = {(rlim_t)memory_bytes, (rlim_t)memory_bytes}, core = {0, 0};
        /* build_child enters and then closes its source descriptor. */
        int entry = dup(work);
        bool ready = entry >= 3 && bq_retirement_build_child(log[1], -1, entry, true, 0077) &&
                     dup2(output[1], STDOUT_FILENO) == STDOUT_FILENO && setrlimit(RLIMIT_CORE, &core) == 0 &&
                     setrlimit(RLIMIT_AS, &memory) == 0;
        for (u32 index = 0; ready && index < command->inherit_count; index += 1)
            ready = fcntl(command->inherit[index], F_SETFD, 0) == 0;
        if (ready) execve(command->arguments[0], command->arguments, command->environment);
        _exit(127);
    }
    ok = ok && process > 0;
    /* Either side may make the group first; after both calls it exists. */
    if (ok) setpgid(process, process);
    if (output[1] >= 0) close(output[1]);
    if (log[1] >= 0) close(log[1]);
    ok = ok && fcntl(output[0], F_SETFL, O_NONBLOCK) == 0 && fcntl(log[0], F_SETFL, O_NONBLOCK) == 0;
    if (ok)
    {
        child->process = process;
        child->output = output[0];
        child->log = log[0];
        sha256_init(&child->output_hash);
        sha256_init(&child->log_hash);
    }
    else
    {
        if (process > 0)
        {
            kill(-process, SIGKILL);
            kill(process, SIGKILL);
            bq_retirement_check_reap(process, NULL);
        }
        if (output[0] >= 0) close(output[0]);
        if (log[0] >= 0) close(log[0]);
    }
    return ok;
}

/* Everything available on reader, into file up to cap (the rest is read and
 * dropped, marking overflow), hashing what was kept. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_drain(int reader, int file, Sha256* hash, u64* bytes, u64 cap,
    bool* eof, u32* overflow)
{
    u8 buffer[16384];
    bool ok = true, more = !*eof;
    while (ok && more)
    {
        ssize_t count = read(reader, buffer, sizeof(buffer));
        if (count > 0)
        {
            u64 kept = (u64)count <= cap - *bytes ? (u64)count : cap - *bytes;
            if (kept < (u64)count) *overflow = 1;
            ok = !kept || bq_write_all(file, buffer, (u32)kept);
            if (ok && kept) sha256_add(hash, buffer, kept);
            *bytes += kept;
        }
        else if (count == 0)
        {
            *eof = true;
            more = false;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) more = false;
        else if (errno != EINTR) ok = false;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_check_kill(BqRetirementCheckChild* child)
{
    kill(-child->process, SIGKILL);
    if (!child->reaped) kill(child->process, SIGKILL);
    child->killed = 1;
}

/* Drains both pipes and reaps the child under the check bound, the job
 * deadline and cancellation. BQ_OK means the child ended on its own or by
 * the check bound; otherwise the group is killed and the child reaped. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_check_wait(BqRetirementCheckRun const* run, BqRetirementCheckChild* child,
    u64 check_deadline_ns, int output_file, int log_file)
{
    BqError stop = BQ_OK;
    bool io = true;
    u64 exited_ns = 0;
    while (io && stop == BQ_OK && !(child->reaped && child->output_eof && child->log_eof))
    {
        struct pollfd ready[3] = {{.fd = run->cancellation_fd, .events = POLLIN}};
        nfds_t count = 1;
        if (!child->output_eof) ready[count++] = (struct pollfd){.fd = child->output, .events = POLLIN};
        if (!child->log_eof) ready[count++] = (struct pollfd){.fd = child->log, .events = POLLIN};
        int polled = poll(ready, count, BQ_RETIREMENT_CHECK_POLL_MS);
        io = polled >= 0 || errno == EINTR;
        io = io && bq_retirement_check_drain(child->output, output_file, &child->output_hash, &child->output_bytes,
                                             BQ_RETIREMENT_CHECK_OUTPUT_CAP, &child->output_eof, &child->overflow) &&
             bq_retirement_check_drain(child->log, log_file, &child->log_hash, &child->log_bytes,
                                       BQ_RETIREMENT_CHECK_LOG_CAP, &child->log_eof, &child->overflow);
        u64 now = bq_retirement_build_clock_ns();
        if (io && !child->reaped)
        {
            pid_t waited = waitpid(child->process, &child->status, WNOHANG);
            if (waited == child->process)
            {
                child->reaped = true;
                exited_ns = now;
                /* A descendant left in the group outlived the check. */
                if (kill(-child->process, 0) == 0)
                {
                    child->lingering = 1;
                    kill(-child->process, SIGKILL);
                }
            }
            else io = waited == 0 || errno == EINTR;
        }
        else if (io && now - exited_ns >= BQ_RETIREMENT_CHECK_DRAIN_NS)
        {
            /* A descendant outside the group still holds a pipe. */
            child->lingering = 1;
            child->output_eof = true;
            child->log_eof = true;
        }
        if (io && child->overflow && !child->killed) bq_retirement_check_kill(child);
        stop = bq_retirement_check_stop(run->cancellation_fd, run->deadline_ns);
        if (io && stop == BQ_OK && !child->reaped && now >= check_deadline_ns && !child->timed_out)
        {
            child->timed_out = 1;
            bq_retirement_check_kill(child);
        }
    }
    if ((!io || stop != BQ_OK) && !child->reaped)
    {
        bq_retirement_check_kill(child);
        bq_retirement_check_reap(child->process, &child->status);
        child->reaped = true;
    }
    if (!io || stop != BQ_OK) kill(-child->process, SIGKILL);
    BqError result = !io ? BQ_IO : stop;
    return result;
}

/* The CPU model line and the affinity mask the unit (and so the child) ran
 * with. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_provenance(char model[SHA256_HEX_CAPACITY], u32* cpus,
    char mask[2u * sizeof(cpu_set_t) + 1u])
{
    char* text = malloc(BQ_RETIREMENT_CHECK_CPUINFO_CAP + 1u);
    int file = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
    size_t used = 0;
    bool ok = text && file >= 0;
    bool more = ok;
    while (ok && more && used < BQ_RETIREMENT_CHECK_CPUINFO_CAP)
    {
        ssize_t count = read(file, text + used, BQ_RETIREMENT_CHECK_CPUINFO_CAP - used);
        if (count > 0) used += (size_t)count;
        else if (count == 0) more = false;
        else ok = errno == EINTR;
    }
    if (file >= 0) close(file);
    if (ok)
    {
        text[used] = 0;
        char const* found = strncmp(text, "model name", 10) ? strstr(text, "\nmodel name") : NULL;
        char const* line = found ? found + 1 : strncmp(text, "model name", 10) ? NULL : text;
        char const* value = line ? strchr(line, ':') : NULL;
        if (value && value[1] == ' ') value += 2;
        size_t length = value ? strcspn(value, "\n") : 0;
        bq_digest(value ? value : "", (u32)length, (char8*)model);
    }
    free(text);
    cpu_set_t set;
    CPU_ZERO(&set);
    ok = ok && sched_getaffinity(0, sizeof(set), &set) == 0;
    if (ok)
    {
        u8 bytes[sizeof(cpu_set_t)];
        memcpy(bytes, &set, sizeof(bytes));
        static char const hex[] = "0123456789abcdef";
        for (size_t index = 0; index < sizeof(bytes); index += 1)
        {
            mask[2u * index] = hex[bytes[index] >> 4];
            mask[2u * index + 1u] = hex[bytes[index] & 15u];
        }
        mask[2u * sizeof(bytes)] = 0;
        *cpus = (u32)CPU_COUNT(&set);
    }
    return ok;
}

/* How the reaped child ended, against the check's pinned output. */
BUSTER_GLOBAL_LOCAL void bq_retirement_check_outcome(BqRetirementCheckChild* child, BqRetirementCheckPlan const* plan,
    BqRetirementCheckOutcome* outcome)
{
    bool signalled = WIFSIGNALED(child->status);
    outcome->exit_code = WIFEXITED(child->status) ? WEXITSTATUS(child->status) :
                         signalled ? 128 + WTERMSIG(child->status) : -1;
    outcome->signal = signalled ? WTERMSIG(child->status) : 0;
    outcome->timed_out = child->timed_out;
    outcome->out_of_memory = signalled && WTERMSIG(child->status) == SIGKILL && !child->killed;
    sha256_finish_hex(&child->output_hash, outcome->output_sha256);
    outcome->failures = child->overflow + child->lingering + (strcmp(outcome->output_sha256, plan->output_sha256) != 0);
}

/* The held binaries, tools and hosted record, as fstat saw them before the
 * child: the run fails when any is not the same file, unchanged (device,
 * inode, size, mode, links, owner, mtime and ctime), afterwards, so a
 * same-user modify-and-restore during the check is caught even though the
 * bytes match again. */
typedef struct BqRetirementCheckSnapshot
{
    struct stat binaries[2];
    struct stat tools[BQ_RETIREMENT_CHECK_TOOLS_CAP];
    struct stat hosted;
} BqRetirementCheckSnapshot;

BUSTER_GLOBAL_LOCAL bool bq_retirement_check_snapshot(BqRetirementCheckRun const* run,
    BqRetirementCheckSnapshot* snapshot)
{
    bool ok = true;
    for (u32 side = 0; ok && side < 2; side += 1) ok = fstat(run->binaries->descriptors[side], snapshot->binaries + side) == 0;
    for (u32 tool = 0; ok && tool < run->checks->tool_count; tool += 1)
        ok = fstat(run->checks->tools[tool], snapshot->tools + tool) == 0;
    if (ok && run->checks->hosted >= 0) ok = fstat(run->checks->hosted, &snapshot->hosted) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_check_unchanged(BqRetirementCheckRun const* run,
    BqRetirementCheckSnapshot const* before)
{
    BqRetirementCheckSnapshot after = {0};
    bool ok = bq_retirement_check_snapshot(run, &after);
    for (u32 side = 0; ok && side < 2; side += 1)
        ok = bq_retirement_oracle_same_file(before->binaries + side, after.binaries + side);
    for (u32 tool = 0; ok && tool < run->checks->tool_count; tool += 1)
        ok = bq_retirement_oracle_same_file(before->tools + tool, after.tools + tool);
    if (ok && run->checks->hosted >= 0) ok = bq_retirement_oracle_same_file(&before->hosted, &after.hosted);
    return ok;
}

/* A hosted check runs no child: its output is the held hosted record, whose
 * bytes must still hash to the authority's pin, and its log is empty. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_check_hosted(BqRetirementRequiredChecks const* checks,
    BqRetirementCheckPlan const* plan, int output_file, BqRetirementCheckOutcome* outcome, u64* output_bytes)
{
    struct stat info = {0};
    char digest[SHA256_HEX_CAPACITY] = {0};
    bool ok = checks->hosted >= 3 && fstat(checks->hosted, &info) == 0 && info.st_size >= 0 &&
              (u64)info.st_size <= BQ_RETIREMENT_CHECK_OUTPUT_CAP &&
              bq_retirement_oracle_file_hash(checks->hosted, BQ_RETIREMENT_HOSTED_ACCEPTANCE_BYTES_CAP, false, digest) &&
              !strcmp(digest, checks->hosted_sha256);
    u8* bytes = ok ? malloc((size_t)info.st_size + 1u) : NULL;
    u32 length = 0;
    ok = ok && bytes && bq_read_file(checks->hosted, bytes, (u32)info.st_size, &length) &&
         bq_write_all(output_file, bytes, length);
    free(bytes);
    if (ok)
    {
        memcpy(outcome->output_sha256, digest, SHA256_HEX_CAPACITY);
        outcome->failures = strcmp(digest, plan->output_sha256) != 0;
        *output_bytes = length;
    }
    BqError result = ok ? BQ_OK : BQ_SOURCE_MISMATCH;
    return result;
}

BqError bq_retirement_check_run(BqRetirementCheckRun const* run, u32 index, BqRetirementCheckResult* result)
{
    if (result) *result = (BqRetirementCheckResult){0};
    BqRetirementRequiredChecks const* checks = run ? run->checks : NULL;
    BqRetirementCheckPlan const* plan = checks && checks->owned && index < checks->count ? checks->plans + index : NULL;
    BqRetirementRequiredCheck const* required = plan ? checks->checks + index : NULL;
    BqRetirementCheckNames names = {0};
    bool valid = plan && result && run->binaries && run->binaries->owned && run->binaries->descriptors[0] >= 3 &&
                 run->binaries->descriptors[1] >= 3 && run->sources[0] >= 3 && run->sources[1] >= 3 &&
                 run->work >= 3 && run->evidence >= 3 && run->source_sha256 &&
                 bq_retirement_hex(string_from_pointer(run->source_sha256[0]), 64) &&
                 bq_retirement_hex(string_from_pointer(run->source_sha256[1]), 64) &&
                 bq_retirement_check_names(index, &names);
    BqError status = valid ? bq_retirement_check_stop(run->cancellation_fd, run->deadline_ns) : BQ_BAD_REQUEST;
    /* No other child may exist: the sweep after the check kills every child
     * of this process. */
    if (status == BQ_OK && !bq_retirement_check_descendants_absent()) status = BQ_WORKER_MISMATCH;
    bool hosted = status == BQ_OK && plan->evidence == BQ_RETIREMENT_CHECK_EVIDENCE_HOSTED;
    /* The binaries and tools the child will run, rehashed and fstat'ed now;
     * the sources are the scanned digests the caller passes. */
    BqRetirementCheckOutcome outcome = {0};
    BqRetirementCheckSnapshot snapshot = {0};
    if (status == BQ_OK)
    {
        bool same = bq_retirement_check_snapshot(run, &snapshot) &&
                    bq_retirement_check_binaries(run->binaries, outcome.binary_sha256);
        for (u32 tool = 0; same && tool < checks->tool_count; tool += 1)
        {
            char digest[SHA256_HEX_CAPACITY] = {0};
            same = bq_retirement_oracle_file_hash(checks->tools[tool], BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP, true,
                                                  digest) && !strcmp(digest, checks->tool_sha256[tool]);
        }
        memcpy(outcome.source_sha256[0], run->source_sha256[0], SHA256_HEX_CAPACITY);
        memcpy(outcome.source_sha256[1], run->source_sha256[1], SHA256_HEX_CAPACITY);
        if (!same) status = BQ_SOURCE_MISMATCH;
    }
    /* A new work directory and new output and log files. */
    int work = -1, output_file = -1, log_file = -1;
    if (status == BQ_OK)
    {
        work = mkdirat(run->work, names.work, 0700) == 0 ?
               bq_retirement_check_promote(openat(run->work, names.work, O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                                  O_NOFOLLOW)) : -1;
        output_file = work >= 3 ? bq_retirement_check_create(run->evidence, names.output) : -1;
        log_file = output_file >= 3 ? bq_retirement_check_create(run->evidence, names.log) : -1;
        if (log_file < 3) status = BQ_WORKSPACE_MISMATCH;
    }
    BqRetirementCheckCommand command = {0};
    char observed_command[SHA256_HEX_CAPACITY] = {0}, log_sha256[SHA256_HEX_CAPACITY] = {0};
    BqRetirementCheckChild child = {.output = -1, .log = -1};
    if (status == BQ_OK && hosted)
    {
        status = bq_retirement_check_hosted(checks, plan, output_file, &outcome, &child.output_bytes);
        memcpy(observed_command, checks->hosted_sha256, SHA256_HEX_CAPACITY);
        bq_digest("", 0, (char8*)log_sha256);
    }
    if (status == BQ_OK && !hosted)
        status = bq_retirement_check_command(run, plan, work, &command) &&
                 tp_retirement_command_fields_hash(command.arguments, command.argument_count, command.directory,
                                                   command.environment, command.environment_count,
                                                   observed_command) ? BQ_OK : BQ_CONFIGURATION_MISMATCH;
    /* Descendants of the check are reparented to this process while it runs
     * as a child subreaper; the previous setting is restored afterwards. */
    int subreaper = 0;
    bool reaping = status == BQ_OK && !hosted && prctl(PR_GET_CHILD_SUBREAPER, &subreaper) == 0 &&
                   prctl(PR_SET_CHILD_SUBREAPER, 1) == 0;
    if (status == BQ_OK && !hosted && !reaping) status = BQ_WORKER_MISMATCH;
    u64 started = bq_retirement_build_clock_ns();
    if (status == BQ_OK && !hosted)
        status = bq_retirement_check_spawn(&command, work, (u64)plan->memory_mib << 20, &child) ? BQ_OK : BQ_IO;
    bool spawned = status == BQ_OK && !hosted;
    if (spawned)
        status = bq_retirement_check_wait(run, &child, started + (u64)plan->timeout_seconds * 1000000000ull,
                                          output_file, log_file);
    /* Every descendant, found or not, is gone before anything is recorded;
     * one that outlived the check fails it. */
    bool swept = false, clean = !spawned || bq_retirement_check_sweep(&swept);
    if (spawned && swept) child.lingering = 1;
    if (reaping && prctl(PR_SET_CHILD_SUBREAPER, subreaper) != 0 && status == BQ_OK) status = BQ_IO;
    if (status == BQ_OK && !clean) status = BQ_CLEANUP_FAILED;
    if (child.output >= 0) close(child.output);
    if (child.log >= 0) close(child.log);
    free(command.storage);
    if (status == BQ_OK && !hosted)
    {
        bq_retirement_check_outcome(&child, plan, &outcome);
        sha256_finish_hex(&child.log_hash, log_sha256);
    }
    /* Freeze the captured files; the held files must be unchanged. */
    if (status == BQ_OK && !(fchmod(output_file, 0400) == 0 && fsync(output_file) == 0 &&
                             fchmod(log_file, 0400) == 0 && fsync(log_file) == 0))
        status = BQ_IO;
    char after[2][SHA256_HEX_CAPACITY] = {{0}};
    if (status == BQ_OK && !(bq_retirement_check_unchanged(run, &snapshot) &&
                             bq_retirement_check_binaries(run->binaries, after) &&
                             !memcmp(after, outcome.binary_sha256, sizeof(after))))
        status = BQ_SOURCE_MISMATCH;
    /* The receipt and the run record. */
    char receipt[BQ_RETIREMENT_CHECK_RECEIPT_CAP], record[BQ_RETIREMENT_CHECK_RUN_CAP];
    char receipt_sha256[SHA256_HEX_CAPACITY] = {0}, model[SHA256_HEX_CAPACITY] = {0};
    char mask[2u * sizeof(cpu_set_t) + 1u] = {0};
    u32 receipt_length = 0, cpus = 0;
    if (status == BQ_OK)
        status = bq_retirement_check_receipt_format(checks, index, &outcome, receipt, sizeof(receipt),
                                                    &receipt_length) &&
                 bq_retirement_check_provenance(model, &cpus, mask) ? BQ_OK : BQ_CORRUPT;
    if (status == BQ_OK) bq_digest(receipt, receipt_length, (char8*)receipt_sha256);
    int record_length = status == BQ_OK ? snprintf(record, sizeof(record), "BQ-RETIREMENT-CHECK-RUN-V1\ncheck=%u\n"
        "receipt=%s\nobserved-command=%s\noutput=%s\noutput-bytes=%" PRIu64 "\nlog=%s\nlog-bytes=%" PRIu64 "\n"
        "cpu-model=%s\ncpu-affinity=%u %s\n", index, receipt_sha256, observed_command, outcome.output_sha256,
        (uint64_t)child.output_bytes, log_sha256, (uint64_t)child.log_bytes, model, cpus, mask) : -1;
    if (status == BQ_OK && !(record_length > 0 && (size_t)record_length < sizeof(record))) status = BQ_CORRUPT;
    int receipt_file = status == BQ_OK ? bq_retirement_check_create(run->evidence, names.receipt) : -1;
    int record_file = receipt_file >= 3 ? bq_retirement_check_create(run->evidence, names.run) : -1;
    if (status == BQ_OK && record_file < 3) status = BQ_WORKSPACE_MISMATCH;
    if (status == BQ_OK &&
        !(bq_write_all(receipt_file, (u8 const*)receipt, receipt_length) && fchmod(receipt_file, 0400) == 0 &&
          fsync(receipt_file) == 0 && bq_write_all(record_file, (u8 const*)record, (u32)record_length) &&
          fchmod(record_file, 0400) == 0 && fsync(record_file) == 0 && fsync(run->evidence) == 0))
        status = BQ_IO;
    int const opened[] = {receipt_file, record_file, output_file, log_file, work};
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(opened); slot += 1)
        if (opened[slot] >= 0 && close(opened[slot]) != 0 && status == BQ_OK) status = BQ_IO;
    if (status == BQ_OK)
    {
        *result = (BqRetirementCheckResult){.kind = required->kind, .target = required->target,
            .rows = required->rows, .failures = outcome.failures, .timed_out = outcome.timed_out,
            .out_of_memory = outcome.out_of_memory, .exit_code = outcome.exit_code};
        memcpy(result->command_sha256, required->command_sha256, SHA256_HEX_CAPACITY);
        memcpy(result->configuration_sha256, required->configuration_sha256, SHA256_HEX_CAPACITY);
        memcpy(result->receipt_sha256, receipt_sha256, SHA256_HEX_CAPACITY);
        memcpy(result->preparation_sha256, checks->preparation_sha256, SHA256_HEX_CAPACITY);
        memcpy(result->source_sha256, outcome.source_sha256, sizeof(result->source_sha256));
        memcpy(result->binary_sha256, outcome.binary_sha256, sizeof(result->binary_sha256));
    }
    return status;
}

bool bq_retirement_check_receipts_hash(BqRetirementRequiredCheck const* checks, u32 count,
    char digest[SHA256_HEX_CAPACITY])
{
    bool ok = checks && count && count <= BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP && digest;
    for (u32 index = 0; ok && index < count; index += 1)
        ok = bq_retirement_hex(string_from_pointer(checks[index].receipt_sha256), 64);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        static char const domain[] = "bq-retirement-check-receipts-v1";
        sha256_add(&hash, domain, sizeof(domain) - 1);
        bq_retirement_correctness_number(&hash, count);
        for (u32 index = 0; index < count; index += 1) sha256_add(&hash, checks[index].receipt_sha256, 64);
        sha256_finish_hex(&hash, digest);
    }
    return ok;
}

/* The 64-hex value after "\n<key>" in text, ending its line. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_value(char const* text, char const* key, char value[SHA256_HEX_CAPACITY])
{
    char pattern[32];
    int length = snprintf(pattern, sizeof(pattern), "\n%s", key);
    char const* found = length > 0 && (size_t)length < sizeof(pattern) ? strstr(text, pattern) : NULL;
    char const* start = found ? found + length : NULL;
    bool ok = start && strnlen(start, 65) == 65 && start[64] == '\n' &&
              bq_retirement_hex((String8){(char8*)start, 64}, 64);
    if (ok)
    {
        memcpy(value, start, 64);
        value[64] = 0;
    }
    return ok;
}

/* Which of the four files of which check name is, or false. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_check_evidence_name(char const* name, u32 count, u32* slot)
{
    static char const* const prefixes[] = {"check-output-", "check-log-", "check-receipt-", "check-run-"};
    bool known = false;
    for (u32 kind = 0; !known && kind < BUSTER_ARRAY_LENGTH(prefixes); kind += 1)
    {
        size_t prefix = strlen(prefixes[kind]);
        u32 index = 0;
        bool digits = !strncmp(name, prefixes[kind], prefix) && strlen(name) == prefix + 4u;
        for (u32 digit = 0; digits && digit < 4; digit += 1)
        {
            char c = name[prefix + digit];
            digits = c >= '0' && c <= '9';
            index = index * 10u + (u32)(c - '0');
        }
        known = digits && index < count;
        if (known) *slot = index * 4u + kind;
    }
    return known;
}

bool bq_retirement_check_evidence_closed(int evidence, BqRetirementRequiredCheck const* checks, u32 count, bool sealed,
    char evidence_sha256[SHA256_HEX_CAPACITY])
{
    Sha256 records;
    sha256_init(&records);
    static char const domain[] = "bq-retirement-check-evidence-v1";
    sha256_add(&records, domain, sizeof(domain) - 1);
    bq_retirement_correctness_number(&records, count);
    struct stat held = {0};
    bool ok = evidence >= 0 && checks && count && count <= BQ_RETIREMENT_CORRECTNESS_CHECKS_CAP &&
              fstat(evidence, &held) == 0 && S_ISDIR(held.st_mode) && held.st_uid == geteuid() &&
              (held.st_mode & 077) == 0 && (!sealed || (held.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE);
    u8* seen = ok ? calloc(4u * (size_t)count, 1) : NULL;
    ok = ok && seen;
    int listing = ok ? openat(evidence, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    DIR* stream = listing >= 0 ? fdopendir(listing) : NULL;
    if (!stream && listing >= 0) close(listing);
    ok = ok && stream != NULL;
    u32 found = 0;
    bool more = ok;
    while (ok && more)
    {
        errno = 0;
        struct dirent* entry = readdir(stream);
        more = entry != NULL;
        if (!more) ok = errno == 0;
        else if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            struct stat info = {0};
            u32 slot = 0;
            ok = bq_retirement_check_evidence_name(entry->d_name, count, &slot) && !seen[slot] &&
                 fstatat(evidence, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode) &&
                 info.st_nlink == 1 && info.st_uid == geteuid() && (info.st_mode & 07777) == 0400;
            if (ok) seen[slot] = 1;
            found += 1;
        }
    }
    if (stream && closedir(stream) != 0) ok = false;
    free(seen);
    ok = ok && found == 4u * count;
    for (u32 index = 0; ok && index < count; index += 1)
    {
        BqRetirementCheckNames names = {0};
        char receipt[BQ_RETIREMENT_CHECK_RECEIPT_CAP + 1], record[BQ_RETIREMENT_CHECK_RUN_CAP + 1];
        char digest[SHA256_HEX_CAPACITY] = {0}, output[SHA256_HEX_CAPACITY] = {0}, log[SHA256_HEX_CAPACITY] = {0};
        char named[SHA256_HEX_CAPACITY] = {0}, prefix[160];
        u32 receipt_length = 0, record_length = 0;
        ok = bq_retirement_check_names(index, &names) &&
             bq_record_read_at(evidence, names.receipt, (u8*)receipt, BQ_RETIREMENT_CHECK_RECEIPT_CAP,
                               &receipt_length) == BQ_OK &&
             bq_record_read_at(evidence, names.run, (u8*)record, BQ_RETIREMENT_CHECK_RUN_CAP, &record_length) == BQ_OK &&
             bq_retirement_check_hash_file(evidence, names.output, BQ_RETIREMENT_CHECK_OUTPUT_CAP, output) &&
             bq_retirement_check_hash_file(evidence, names.log, BQ_RETIREMENT_CHECK_LOG_CAP, log);
        if (ok)
        {
            receipt[receipt_length] = 0;
            record[record_length] = 0;
            bq_digest(receipt, receipt_length, (char8*)digest);
        }
        int length = ok ? snprintf(prefix, sizeof(prefix), "BQ-RETIREMENT-CHECK-RUN-V1\ncheck=%u\nreceipt=%s\n", index,
                                   checks[index].receipt_sha256) : -1;
        ok = ok && strlen(receipt) == receipt_length && strlen(record) == record_length &&
             !strcmp(digest, checks[index].receipt_sha256) && length > 0 && (size_t)length < sizeof(prefix) &&
             !strncmp(record, prefix, (size_t)length) && bq_retirement_check_value(receipt, "output=", named) &&
             !strcmp(named, output) && bq_retirement_check_value(record, "output=", named) && !strcmp(named, output) &&
             bq_retirement_check_value(record, "log=", named) && !strcmp(named, log);
        /* The ordered run records and logs, which only the evidence holds. */
        if (ok)
        {
            char record_sha256[SHA256_HEX_CAPACITY] = {0};
            bq_digest(record, record_length, (char8*)record_sha256);
            sha256_add(&records, record_sha256, 64);
            sha256_add(&records, log, 64);
        }
    }
    if (ok && evidence_sha256) sha256_finish_hex(&records, evidence_sha256);
    return ok;
}
