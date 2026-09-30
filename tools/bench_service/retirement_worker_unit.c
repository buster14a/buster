/* Retirement producer of the service worker-unit (#881, PR 1 of 4).
 *
 * Ownership: orchestration of the in-unit retirement steps for
 * bq_worker_unit (worker_linux.c). It cannot run in the recipe executable,
 * which is also the pinned matched-build driver, and it cannot run in the
 * unit process itself: design step 9 requires a process without children
 * (bq_retirement_check_descendants_absent) and the lease keeper is already a
 * child of the unit process. The unit process therefore keeps the lease and
 * the keeper and forks this producer, which is the phase channel's only
 * writer and a child subreaper. Builds still go through the broker stages.
 *
 * Entry points (all reached only from bq_worker_unit):
 *   bq_retirement_profile_complete     admission: every integration pin is
 *                                      present and exactly one
 *                                      status=admitted line
 *   bq_retirement_worker_unit_installed
 *                                      the compiled profile and fixed roots
 *   bq_retirement_worker_unit_run      fork the producer, close the parent's
 *                                      phase descriptor, wait and map the
 *                                      producer's exit to a BqError
 *
 * Map: BqRetirementWorkerUnitSeams carries the profile, census profile,
 * installed root, driver, toolchain, broker and candidate UID the steps'
 * _pinned seams take; production uses the compiled ones, which
 * bq_retirement_profile_complete refuses (the compiled profile is blocked),
 * so bq_worker_unit rejects the job before any directory or child exists.
 * bq_retirement_worker_unit_forward (unit process) forwards SIGTERM to the
 * producer; bq_retirement_worker_unit_cancel (producer) writes the SIGTERM
 * self-pipe whose read end is every step's cancellation descriptor.
 * bq_retirement_worker_unit_child sets up the producer process and
 * bq_retirement_worker_unit_produce runs, in order, store open, prepare,
 * build (which sends PREPARING), project, oracle, gate and ready, and
 * releases everything in reverse on every path, sweeping any surviving
 * descendant (bq_retirement_check_sweep). bq_retirement_worker_unit_wait
 * waits for the producer under the execution deadline plus the stop budget
 * without reaping it; bq_retirement_worker_unit_run then disarms the
 * forwarder, reaps, and consumes a SIGTERM held in that teardown window.
 *
 * PR 1 stops after the ready record: the in-unit campaign, composition and
 * MEASURED are not wired yet, so a producer that wrote its record exits with
 * BQ_UNSUPPORTED (BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED) and sends no further
 * phase message; the job fails closed. Nothing here is a timing fact.
 */
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

/* The operator-installed read-only tree (bq_worker_run's canonical root). */
#define BQ_RETIREMENT_WORKER_UNIT_INSTALLED_ROOT "/opt/buster-bench/installed"
/* The producer's result once the ready record exists: the in-unit campaign
 * (#881 PR 2), composition and MEASURED (PR 3) are not wired yet. */
#define BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED BQ_UNSUPPORTED
/* A candidate UID asking the producer to resolve buster-bench-candidate, as
 * bq_retirement_unit_build does. */
#define BQ_RETIREMENT_WORKER_UNIT_CANDIDATE_LOOKUP ((uid_t)-1)
/* The worker's stop budget (BQ_WORKER_STOP_MILLISECONDS): every step stops
 * within it once its deadline expires, so the unit waits no longer than the
 * execution deadline plus twice this before it kills the producer. */
#define BQ_RETIREMENT_WORKER_UNIT_STOP_NS (10ull * 1000000000ull)

typedef struct BqRetirementWorkerUnitSeams
{
    String8 profile;
    /* "full-census" in production; the fixture census is "self-test". */
    String8 census_profile;
    char const* installed_root;
    char const* driver;
    char const* toolchain_root;
    char const* broker;
    char const* broker_workspaces;
    uid_t candidate_uid;
    /* Test seam of the gate (bq_retirement_unit_gate_pinned): an observation
     * of the pinned row plan instead of running it. NULL in production. */
    BqRetirementRowObserved const* supplied;
} BqRetirementWorkerUnitSeams;

/* Every pin a retirement job needs through the ready record, plus the
 * reviewed campaign budget; PR 2 and PR 4 add the adapter and
 * untimed-command pins. */
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_worker_unit_pins[] = {
    "contract-sha256=", "support-declaration-sha256=", "inventory-sha256=", "toolchain-manifest-sha256=",
    "build-driver-sha256=", "reference-template-sha256=", "reference-inventory-sha256=",
    "validator-source-applicability-sha256=", "census-inputs-sha256=", "census-rows-sha256=",
    "census-manifest-sha256=", "validator-report-sha256=", "validator-applicability-sha256=",
    "validator-skips-sha256=", "performance-rows-sha256=", "required-checks-sha256=", "row-plan-sha256=",
    "campaign-budget-sha256="};

/* The only admitting status line; the compiled profile says status=blocked. */
#define BQ_RETIREMENT_PROFILE_ADMITTED_STATUS "status=admitted"

BUSTER_GLOBAL_LOCAL bool bq_retirement_profile_complete(String8 profile)
{
    char pin[SHA256_HEX_CAPACITY];
    bool complete = profile.pointer && profile.length > 0;
    for (u32 index = 0; complete && index < BUSTER_ARRAY_LENGTH(bq_retirement_worker_unit_pins); index += 1)
        complete = bq_retirement_profile_sha(profile, string_from_pointer(bq_retirement_worker_unit_pins[index]), pin);
    /* Exactly one status line, and it must be the admitting value byte for
     * byte: blocked, a blocked- prefix, a trailing space or carriage return,
     * a second line or no line at all refuses. */
    u64 offset = 0;
    u32 statuses = 0;
    String8 line = {0};
    String8 const status = S8("status=");
    while (complete && bq_next_line(profile, &offset, &line))
    {
        if (line.length >= status.length && !memcmp(line.pointer, status.pointer, (size_t)status.length))
        {
            statuses += 1;
            complete = string_equal(line, S8(BQ_RETIREMENT_PROFILE_ADMITTED_STATUS));
        }
    }
    complete = complete && statuses == 1;
    return complete;
}

BUSTER_GLOBAL_LOCAL BqRetirementWorkerUnitSeams bq_retirement_worker_unit_installed(void)
{
    BqRetirementWorkerUnitSeams seams = {
        .profile = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED), .census_profile = S8("full-census"),
        .installed_root = BQ_RETIREMENT_WORKER_UNIT_INSTALLED_ROOT, .driver = BQ_RETIREMENT_BUILD_DRIVER,
        .toolchain_root = BQ_RETIREMENT_TOOLCHAIN_ROOT, .broker = BQ_RETIREMENT_UNIT_BROKER,
        .broker_workspaces = BQ_RETIREMENT_STAGE_WORKSPACE_ROOT,
        .candidate_uid = BQ_RETIREMENT_WORKER_UNIT_CANDIDATE_LOOKUP};
    return seams;
}

/* The producer's pid in the unit process, and the self-pipe's write end in
 * the producer; both are read only by the signal handlers below. */
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_retirement_worker_unit_producer;
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_retirement_worker_unit_cancel_fd = -1;
#ifdef BQ_RETIREMENT_CORRECTNESS_TEST_ONLY
/* Test seam (never in the installed service, which retirement_unit.c
 * enforces): raise SIGTERM in the unit process's teardown window, after the
 * producer exited and while SIGTERM is blocked. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_unit_test_late_term;
#endif

BUSTER_GLOBAL_LOCAL void bq_retirement_worker_unit_forward(int signal_number)
{
    int saved = errno;
    if (bq_retirement_worker_unit_producer > 0) kill((pid_t)bq_retirement_worker_unit_producer, signal_number);
    errno = saved;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_worker_unit_cancel(int signal_number)
{
    int saved = errno;
    unsigned char byte = (unsigned char)signal_number;
    if (bq_retirement_worker_unit_cancel_fd >= 0)
    {
        ssize_t written = write(bq_retirement_worker_unit_cancel_fd, &byte, sizeof(byte));
        (void)written;
    }
    errno = saved;
}

/* Design steps 1 to 10 in the producer. Each step checks the cancellation
 * descriptor and the deadline itself; the stop reason is also rechecked
 * between steps. Everything is released in reverse on every path, and only
 * the build sends a phase message (PREPARING). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_unit_produce(BqRetirementWorkerUnitSeams const* seams, u64 job_id,
    u64 attempt_token, char const* workspace_root, int phase_descriptor, int cancellation_fd,
    char const preparation_sha256[SHA256_HEX_CAPACITY], u64 deadline_ns, char ready_sha256[SHA256_HEX_CAPACITY])
{
    BqPhaseChannel phases = {.descriptor = -1, .failed = 1};
    BqRetirementStore store = {-1};
    BqRetirementUnitPrepared prepared = {.policy = {.clang = -1, .inventory = -1}};
    BqRetirementUnitBuilt built = {.verified = {.generated_root = -1}, .binaries = {.descriptors = {-1, -1}}};
    BqRetirementProjection projection = {0};
    BqRetirementUnitOracle oracle = {0};
    BqRetirementUnitGate gate = {0};
    String8 root = string_from_pointer(workspace_root);
    uid_t candidate = seams->candidate_uid;
    if (candidate == BQ_RETIREMENT_WORKER_UNIT_CANDIDATE_LOOKUP)
    {
        struct passwd* entry = getpwnam("buster-bench-candidate");
        candidate = entry && entry->pw_uid != geteuid() ? entry->pw_uid : BQ_RETIREMENT_WORKER_UNIT_CANDIDATE_LOOKUP;
    }
    int workspaces = bq_open_absolute_directory(root);
    int installed = bq_open_absolute_directory(string_from_pointer(seams->installed_root));
    BqError result = workspaces >= 0 && installed >= 0 && candidate != BQ_RETIREMENT_WORKER_UNIT_CANDIDATE_LOOKUP ?
                     BQ_OK : BQ_CONFIGURATION_MISMATCH;
    /* The keeper is the unit process's child, never this one's. */
    if (result == BQ_OK && !bq_retirement_check_descendants_absent()) result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK && !bq_phase_init(&phases, phase_descriptor, job_id, attempt_token)) result = BQ_WORKER_MISMATCH;
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    if (result == BQ_OK) result = bq_retirement_unit_store_open(workspaces, job_id, attempt_token, &store);
    if (result == BQ_OK)
        result = bq_retirement_unit_prepare_pinned(store, workspaces, installed, job_id, attempt_token, seams->profile,
                                                   seams->toolchain_root, preparation_sha256, &prepared);
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    if (result == BQ_OK)
        result = bq_retirement_unit_build_pinned(store, &prepared, workspaces, installed, root, seams->profile,
            seams->driver, seams->toolchain_root, seams->broker, seams->broker_workspaces, candidate, &phases,
            cancellation_fd, deadline_ns, &built);
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    /* The projection holds the nine pinned census files itself
     * (bq_retirement_unit_census_open) and closes them on every path. */
    if (result == BQ_OK)
        result = bq_retirement_unit_project_pinned(store, &prepared, &built, workspaces, installed, root,
            seams->profile, seams->census_profile, seams->driver, seams->toolchain_root, seams->broker,
            seams->broker_workspaces, &projection);
    if (result == BQ_OK)
        result = bq_retirement_unit_oracle_pinned(&prepared, &projection, workspaces, seams->profile, cancellation_fd,
                                                  deadline_ns, &oracle);
    if (result == BQ_OK)
        result = bq_retirement_unit_gate_pinned(&prepared, &built, &projection, &oracle, workspaces, installed,
            seams->profile, seams->supplied, cancellation_fd, deadline_ns, &gate);
    if (result == BQ_OK) result = bq_retirement_unit_stop_reason(cancellation_fd, deadline_ns, BQ_OK);
    if (result == BQ_OK)
        result = bq_retirement_unit_ready_pinned(&prepared, &built, &projection, &oracle, &gate, workspaces,
                                                 seams->profile, ready_sha256);
    if (result == BQ_OK) result = BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED;
    if (!bq_retirement_unit_gate_release(&gate) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_oracle_release(&oracle) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_projection_release(&projection) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_built_release(&built) && result == BQ_OK) result = BQ_IO;
    if (!bq_retirement_unit_release(&prepared) && result == BQ_OK) result = BQ_IO;
    if (store.directory >= 0 && close(store.directory) != 0 && result == BQ_OK) result = BQ_IO;
    if (installed >= 0 && close(installed) != 0 && result == BQ_OK) result = BQ_IO;
    if (workspaces >= 0 && close(workspaces) != 0 && result == BQ_OK) result = BQ_IO;
    /* Every step proves its own children gone. A survivor (a subreaped
     * escapee included) fails the unit and is killed and reaped here, so
     * nothing is reparented past the producer. */
    if (!bq_retirement_check_descendants_absent())
    {
        bool found = false;
        bq_retirement_check_sweep(&found);
        result = BQ_CLEANUP_FAILED;
    }
    return result;
}

/* The producer process: it drops the lease reference (the unit process keeps
 * the lease), stops with the unit process, becomes a child subreaper and
 * installs the SIGTERM self-pipe before it unblocks SIGTERM. Returns the exit
 * status. */
BUSTER_GLOBAL_LOCAL int bq_retirement_worker_unit_child(BqRetirementWorkerUnitSeams const* seams, u64 job_id,
    u64 attempt_token, char const* workspace_root, int lease_descriptor, int phase_descriptor,
    char const preparation_sha256[SHA256_HEX_CAPACITY], u64 deadline_ns, pid_t parent, sigset_t const* unblocked)
{
    int self_pipe[2] = {-1, -1};
    bool ok = (lease_descriptor < 0 || close(lease_descriptor) == 0) && prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 &&
              getppid() == parent && prctl(PR_SET_CHILD_SUBREAPER, 1) == 0 &&
              pipe2(self_pipe, O_CLOEXEC | O_NONBLOCK) == 0;
    /* The cancellation descriptor must be at least 3 and close-on-exec. */
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        self_pipe[side] = bq_retirement_unit_promote(self_pipe[side]);
        ok = self_pipe[side] >= 3;
    }
    bq_retirement_worker_unit_cancel_fd = ok ? self_pipe[1] : -1;
    struct sigaction cancel = {0};
    cancel.sa_handler = bq_retirement_worker_unit_cancel;
    cancel.sa_flags = SA_RESTART;
    ok = ok && sigemptyset(&cancel.sa_mask) == 0 && sigaction(SIGTERM, &cancel, NULL) == 0 &&
         sigprocmask(SIG_SETMASK, unblocked, NULL) == 0;
    char ready_sha256[SHA256_HEX_CAPACITY] = {0};
    BqError result = ok ? bq_retirement_worker_unit_produce(seams, job_id, attempt_token, workspace_root,
                                                            phase_descriptor, self_pipe[0], preparation_sha256,
                                                            deadline_ns, ready_sha256) : BQ_IO;
    if (result == BQ_RETIREMENT_WORKER_UNIT_NOT_WIRED)
        fprintf(stderr, "retirement worker-unit: ready record %s written; the in-unit campaign is not wired yet\n",
                ready_sha256);
    close(phase_descriptor);
    for (u32 side = 0; side < 2; side += 1)
        if (self_pipe[side] >= 0) close(self_pipe[side]);
    int status = (int)result;
    return status;
}

/* Waits until the producer has exited without reaping it (WNOWAIT), so its
 * pid stays reserved while the SIGTERM forwarder may still name it. With a
 * pidfd the wait ends at bound_ns (otherwise the unit's RuntimeMax bounds it)
 * and a producer still running then is killed. Returns whether it exited by
 * itself. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_worker_unit_wait(pid_t producer, u64 bound_ns)
{
    int process = -1;
#ifdef SYS_pidfd_open
    process = (int)syscall(SYS_pidfd_open, producer, 0);
#endif
    bool exited = false, expired = false, lost = false;
    while (!exited && !expired && !lost)
    {
        u64 now = bq_phase_clock();
        expired = process >= 0 && (!now || now >= bound_ns);
        u64 remaining = expired || process < 0 ? 0 : (bound_ns - now) / 1000000ull + 1ull;
        struct pollfd waiting = {process, POLLIN, 0};
        if (remaining) poll(&waiting, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
        siginfo_t info = {0};
        int waited = expired ? 0 : waitid(P_PID, (id_t)producer, &info,
                                           WEXITED | WNOWAIT | (process >= 0 ? WNOHANG : 0));
        exited = waited == 0 && info.si_pid == producer;
        lost = waited != 0 && errno != EINTR;
    }
    if (expired) kill(producer, SIGKILL);
    if (process >= 0) close(process);
    return exited;
}

/* Called by bq_worker_unit after its pause and pre-exec lease recheck, with
 * the lease and phase descriptors still close-on-exec. The unit process keeps
 * the lease and the keeper; the producer is the phase channel's only writer,
 * so the parent's copy is closed at once and the coordinator sees EOF when
 * the producer exits. A SIGTERM to the unit process is forwarded. The
 * producer's exit status is its BqError; exit 0 cannot be a success before
 * MEASURED is wired (BQ_WORKER_FAILED), and a producer killed by a signal or
 * at the bound never proved its descendants absent (BQ_CLEANUP_FAILED). */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_worker_unit_run(BqRetirementWorkerUnitSeams const* seams, u64 job_id,
    u64 attempt_token, char const* workspace_root, int lease_descriptor, int* phase_descriptor,
    char const preparation_sha256[SHA256_HEX_CAPACITY], u64 deadline_ns)
{
    sigset_t blocked, prior;
    struct sigaction forward = {0}, previous = {0};
    forward.sa_handler = bq_retirement_worker_unit_forward;
    forward.sa_flags = SA_RESTART;
    bool masked = sigemptyset(&blocked) == 0 && sigaddset(&blocked, SIGTERM) == 0 &&
                  sigprocmask(SIG_BLOCK, &blocked, &prior) == 0;
    bool handled = masked && sigemptyset(&forward.sa_mask) == 0 && sigaction(SIGTERM, &forward, &previous) == 0;
    sigset_t pending;
    BqError result = handled && seams && workspace_root && phase_descriptor && *phase_descriptor >= 3 &&
                     sigemptyset(&pending) == 0 && sigpending(&pending) == 0 ? BQ_OK : BQ_IO;
    if (result == BQ_OK && sigismember(&pending, SIGTERM) == 1) result = BQ_WORKER_CANCEL_SIGNAL;
    if (result == BQ_OK && bq_phase_clock() >= deadline_ns) result = BQ_WORKER_TIMEOUT;
    pid_t parent = getpid();
    pid_t producer = result == BQ_OK ? fork() : -1;
    if (producer == 0)
        _exit(bq_retirement_worker_unit_child(seams, job_id, attempt_token, workspace_root, lease_descriptor,
                                              *phase_descriptor, preparation_sha256, deadline_ns, parent, &prior));
    if (result == BQ_OK && producer < 0) result = BQ_IO;
    if (producer > 0)
    {
        bq_retirement_worker_unit_producer = producer;
        if (close(*phase_descriptor) != 0 && result == BQ_OK) result = BQ_IO;
        *phase_descriptor = -1;
    }
    if (masked && sigprocmask(SIG_SETMASK, &prior, NULL) != 0 && result == BQ_OK) result = BQ_IO;
    u64 bound = deadline_ns <= UINT64_MAX - 2ull * BQ_RETIREMENT_WORKER_UNIT_STOP_NS ?
                deadline_ns + 2ull * BQ_RETIREMENT_WORKER_UNIT_STOP_NS : UINT64_MAX;
    bool exited = producer > 0 && bq_retirement_worker_unit_wait(producer, bound);
    /* Disarm the forwarder with SIGTERM blocked, then reap: the handler never
     * names a reaped (reusable) pid. */
    bool reblocked = masked && sigprocmask(SIG_BLOCK, &blocked, NULL) == 0;
#ifdef BQ_RETIREMENT_CORRECTNESS_TEST_ONLY
    if (reblocked && bq_retirement_worker_unit_test_late_term) raise(SIGTERM);
#endif
    if (handled && sigaction(SIGTERM, &previous, NULL) != 0 && result == BQ_OK) result = BQ_IO;
    bq_retirement_worker_unit_producer = 0;
    int status = 0;
    pid_t reaped = -1;
    if (producer > 0)
    {
        do reaped = waitpid(producer, &status, 0);
        while (reaped < 0 && errno == EINTR);
    }
    int code = reaped == producer && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (result == BQ_OK)
        result = !exited || reaped != producer || WIFSIGNALED(status) ? BQ_CLEANUP_FAILED :
                 code > BQ_OK && code <= BQ_EXPORT_TIMEOUT ? (BqError)code : BQ_WORKER_FAILED;
    /* A SIGTERM held since the forwarder was disarmed would be delivered
     * under the restored disposition and end the unit before it stops its
     * keeper and releases the lease: consume it and report the cancellation
     * (an unproven cleanup still wins). */
    struct timespec immediately = {0, 0};
    bool terminated = reblocked && sigtimedwait(&blocked, NULL, &immediately) == SIGTERM;
    if (terminated && result != BQ_CLEANUP_FAILED) result = BQ_WORKER_CANCEL_SIGNAL;
    if (reblocked && sigprocmask(SIG_SETMASK, &prior, NULL) != 0 && result == BQ_OK) result = BQ_IO;
    return result;
}
