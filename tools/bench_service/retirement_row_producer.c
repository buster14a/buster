/* In-unit row-evidence producer (#1020/#509 design step 9, #881 lane B).
 *
 * Ownership: running an imported row plan with the unit's held matched
 * binaries and observing its facts. The plan and the canonical evidence live
 * in retirement_row_plan.c; the gate that persists and admits the evidence
 * in retirement_unit.c.
 *
 * Entry point: bq_retirement_row_produce.
 *
 * Isolation. Every step runs candidate-derived code: the candidate binary
 * compiling, and the program it generated running. Each step enters the
 * check runner's sandbox before exec (bq_retirement_sandbox,
 * bq_retirement_sandbox_enter; its file header states exactly what it
 * covers): Landlock rules that leave the step its side's held binary, A's
 * two roots read-only, the system trees and its own new step directory, and
 * a seccomp filter that refuses every socket system call and io_uring. It
 * does not cover metadata changes (chmod, chown, utimes, xattr). The
 * producer refuses before any step below Landlock ABI
 * BQ_RETIREMENT_SANDBOX_MIN_ABI and records the ABI in the evidence
 * (sandbox-abi=). The steps still run as the service user; the broker's
 * separate candidate UID is not used here.
 *
 * Map: bq_retirement_row_spawn forks one step through
 * bq_retirement_build_child's normalized state (retirement_check_runner.c's
 * limits: own process group, default signals, empty mask, umask 0077,
 * close-on-exec from 3, RLIMIT_AS, no core), places the side's held binary,
 * A's two roots and the step's work directory in the canonical slots the
 * plan's digests assume and enters the sandbox; bq_retirement_row_step runs
 * it as the check runner runs a check (no other child first, subreaper, the
 * step and job bounds, cancellation, the sweep) with the held binaries'
 * fstat identity unchanged around it (bq_retirement_row_produce rehashes
 * them once before and once after the whole run);
 * bq_retirement_row_metrics_parse reads the compiler's metrics records and
 * bq_retirement_row_metrics_single checks a per-row compile's;
 * bq_retirement_row_artifact freezes an artifact and reads it with the
 * service's per-target format, machine and kind check;
 * bq_retirement_row_cpu_model names the pinned CPU; bq_retirement_row_batch
 * and bq_retirement_row_compile observe a batch group and a per-row compile
 * (with its runtime step) for one side.
 */
#include "retirement_row_plan.h"

/* One step's outcome. output_sha256 is the step's stdout, or its combined
 * stdout and stderr for a runtime step, which the oracle captures the same
 * way. */
typedef struct BqRetirementRowStepResult
{
    int exit_code;
    u32 timed_out, out_of_memory, failures;
    char command_sha256[SHA256_HEX_CAPACITY];
    char output_sha256[SHA256_HEX_CAPACITY], log_sha256[SHA256_HEX_CAPACITY];
} BqRetirementRowStepResult;

/* Forks one step (see the map). held is the side's binary, A's base and
 * candidate roots and the work directory, in slot order; ruleset the step's
 * sandbox. combined puts stdout on the log pipe too. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_spawn(BqRetirementRowCommand const* command, int const held[4], u32 side,
    u64 memory_bytes, int ruleset, bool combined, BqRetirementCheckChild* child)
{
    int output[2] = {-1, -1}, log[2] = {-1, -1};
    bool ok = pipe2(output, O_CLOEXEC) == 0 && pipe2(log, O_CLOEXEC) == 0 && output[0] >= 3 && output[1] >= 3 &&
              log[0] >= 3 && log[1] >= 3;
    pid_t process = ok ? fork() : -1;
    if (process == 0)
    {
        struct rlimit memory = {(rlim_t)memory_bytes, (rlim_t)memory_bytes}, core = {0, 0};
        int entry = dup(held[3]);
        bool ready = entry >= 3 && bq_retirement_build_child(log[1], -1, entry, true, 0077) &&
                     (combined || dup2(output[1], STDOUT_FILENO) == STDOUT_FILENO) &&
                     setrlimit(RLIMIT_CORE, &core) == 0 && setrlimit(RLIMIT_AS, &memory) == 0;
        /* Out of the way first (the ruleset too), then into the canonical
         * slots (retirement_sandbox.h, as lane D's measured launches do);
         * dup2 clears close-on-exec on exactly those four. */
        int parked = ready ? bq_retirement_sandbox_slots(held, side, ruleset) : -1;
        ready = parked >= 0 && bq_retirement_sandbox_enter(parked);
        if (ready) execve(command->arguments[0], command->arguments, command->environment);
        _exit(127);
    }
    ok = ok && process > 0;
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

/* Runs one step in work with the side's held binary, sandboxed. BQ_OK means
 * the child reached a verdict; result then holds how it ended. A held binary
 * that is not the same unchanged file afterwards is BQ_SOURCE_MISMATCH. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_step(BqRetirementRowRun const* run, BqRetirementRowTemplate const* template,
    BqRetirementRowContext const* context, char* storage, int work, char const* capture, bool combined,
    BqRetirementRowStepResult* result)
{
    *result = (BqRetirementRowStepResult){.exit_code = -1};
    BqError status = bq_retirement_check_stop(run->cancellation_fd, run->deadline_ns);
    if (status == BQ_OK && !bq_retirement_check_descendants_absent()) status = BQ_WORKER_MISMATCH;
    BqRetirementRowCommand command;
    if (status == BQ_OK && !(bq_retirement_row_command(template, context, storage, &command) &&
                             bq_retirement_row_command_digest(&command, result->command_sha256)))
        status = BQ_CONFIGURATION_MISMATCH;
    BqRetirementHeldBinaries const* binaries = run->binaries;
    struct stat before[2] = {{0}}, after[2] = {{0}};
    for (u32 side = 0; status == BQ_OK && side < 2; side += 1)
        if (fstat(binaries->descriptors[side], before + side) != 0) status = BQ_SOURCE_MISMATCH;
    int ruleset = status == BQ_OK ?
                  bq_retirement_sandbox(binaries->descriptors + context->side, 1, run->sources, work, NULL) : -1;
    if (status == BQ_OK && ruleset < 0) status = BQ_CONFIGURATION_MISMATCH;
    char names[2][96];
    int named[2] = {snprintf(names[0], sizeof(names[0]), "%s.stdout", capture),
                    snprintf(names[1], sizeof(names[1]), "%s.stderr", capture)};
    int output_file = status == BQ_OK && named[0] > 0 && (size_t)named[0] < sizeof(names[0]) ?
                      bq_retirement_check_create(work, names[0]) : -1;
    int log_file = output_file >= 3 && named[1] > 0 && (size_t)named[1] < sizeof(names[1]) ?
                   bq_retirement_check_create(work, names[1]) : -1;
    if (status == BQ_OK && log_file < 3) status = BQ_WORKSPACE_MISMATCH;
    int subreaper = 0;
    bool reaping = status == BQ_OK && prctl(PR_GET_CHILD_SUBREAPER, &subreaper) == 0 &&
                   prctl(PR_SET_CHILD_SUBREAPER, 1) == 0;
    if (status == BQ_OK && !reaping) status = BQ_WORKER_MISMATCH;
    BqRetirementCheckChild child = {.output = -1, .log = -1};
    int const held[4] = {binaries->descriptors[context->side], run->sources[0], run->sources[1], work};
    u64 started = bq_retirement_build_clock_ns();
    if (status == BQ_OK)
        status = bq_retirement_row_spawn(&command, held, context->side, (u64)template->memory_mib << 20, ruleset,
                                         combined, &child) ? BQ_OK : BQ_IO;
    bool spawned = status == BQ_OK;
    BqRetirementCheckRun waiter = {.cancellation_fd = run->cancellation_fd, .deadline_ns = run->deadline_ns};
    if (spawned)
        status = bq_retirement_check_wait(&waiter, &child, started + (u64)template->timeout_seconds * 1000000000ull,
                                          output_file, log_file);
    bool swept = false, clean = !spawned || bq_retirement_check_sweep(&swept);
    if (spawned && swept) child.lingering = 1;
    if (reaping && prctl(PR_SET_CHILD_SUBREAPER, subreaper) != 0 && status == BQ_OK) status = BQ_IO;
    if (status == BQ_OK && !clean) status = BQ_CLEANUP_FAILED;
    if (child.output >= 0) close(child.output);
    if (child.log >= 0) close(child.log);
    if (status == BQ_OK)
    {
        bool signalled = WIFSIGNALED(child.status);
        result->exit_code = WIFEXITED(child.status) ? WEXITSTATUS(child.status) :
                            signalled ? 128 + WTERMSIG(child.status) : -1;
        result->timed_out = child.timed_out;
        result->out_of_memory = signalled && WTERMSIG(child.status) == SIGKILL && !child.killed;
        result->failures = child.overflow + child.lingering;
        sha256_finish_hex(&child.log_hash, result->log_sha256);
        if (combined) memcpy(result->output_sha256, result->log_sha256, SHA256_HEX_CAPACITY);
        else sha256_finish_hex(&child.output_hash, result->output_sha256);
    }
    /* The same unchanged files (device, inode, size, mode, links, owner,
     * mtime and ctime), so a modify-and-restore during the step is caught
     * without rehashing the binaries around every step. */
    for (u32 side = 0; status == BQ_OK && side < 2; side += 1)
        if (!(fstat(binaries->descriptors[side], after + side) == 0 &&
              bq_retirement_oracle_same_file(before + side, after + side)))
            status = BQ_SOURCE_MISMATCH;
    if (ruleset >= 0) close(ruleset);
    if (output_file >= 0) close(output_file);
    if (log_file >= 0) close(log_file);
    return status;
}

/* A bounded, private regular file the step wrote into work (NULL when
 * absent). */
BUSTER_GLOBAL_LOCAL u8* bq_retirement_row_read(int work, char const* name, u64 cap, u64* size)
{
    int file = bq_retirement_check_promote(openat(work, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    struct stat info = {0};
    bool ok = file >= 3 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
              info.st_uid == geteuid() && info.st_size >= 0 && (u64)info.st_size <= cap && (u64)info.st_size <= UINT32_MAX;
    u8* bytes = ok ? malloc((size_t)info.st_size + 1u) : NULL;
    u32 length = 0;
    ok = ok && bytes && bq_read_file(file, bytes, (u32)info.st_size, &length) && length == (u64)info.st_size;
    if (file >= 0) close(file);
    if (!ok)
    {
        free(bytes);
        bytes = NULL;
    }
    *size = ok ? length : 0;
    return bytes;
}

/* One observed artifact of a row on target at stage: frozen read-only (an
 * executable 0500), its file digest recorded whatever it holds, then read
 * with the service's own check (bq_retirement_artifact_target and
 * bq_retirement_artifact_read): the format and machine of the target and an
 * object at the object stage, an executable otherwise. valid is false when
 * any of that does not hold; facts then stays empty. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_artifact(int work, char const* name, u32 target, u32 stage,
    char digest[SHA256_HEX_CAPACITY], TpRetirementArtifact* facts)
{
    *facts = (TpRetirementArtifact){0};
    bool executable = stage != BQ_RETIREMENT_STAGE_OBJECT;
    struct stat info = {0};
    bool present = fstatat(work, name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode);
    bool frozen = present && fchmodat(work, name, executable ? 0500 : 0400, 0) == 0;
    u64 size = 0;
    u8* bytes = frozen ? bq_retirement_row_read(work, name, BQ_RETIREMENT_ROW_ARTIFACT_BYTES_CAP, &size) : NULL;
    bool hashed = bytes != NULL;
    if (hashed) bq_digest(bytes, (u32)size, (char8*)digest);
    free(bytes);
    unsigned format = 0, machine = 0;
    bool valid = hashed && bq_retirement_artifact_target(target, stage, &format, &machine) &&
                 bq_retirement_artifact_read((BqRetirementArtifactLocation){work, name}, format, machine, executable,
                                             facts) &&
                 !strcmp(facts->file_sha256, digest);
    if (!valid) *facts = (TpRetirementArtifact){0};
    return valid;
}

/* The index of a per-input metrics field, by its name. */
BUSTER_GLOBAL_LOCAL u32 bq_retirement_row_metrics_field(char const* name)
{
    u32 found = TP_METRICS_I_COUNT;
    for (u32 index = 0; found == TP_METRICS_I_COUNT && index < TP_METRICS_I_COUNT; index += 1)
        if (!strcmp(tp_retirement_metrics_input_fields[index], name)) found = index;
    return found;
}

/* One metrics file's header and input records. status is the input's
 * status index in bq_retirement_row_statuses, or BQ_RETIREMENT_ROW_STATUSES
 * for any other. */
#define BQ_RETIREMENT_ROW_STATUSES 5u
BUSTER_GLOBAL_LOCAL char const* const bq_retirement_row_statuses[BQ_RETIREMENT_ROW_STATUSES] = {
    "ok", "rejected", "failed", "not_run", "prebuilt"
};

typedef struct BqRetirementRowMetrics
{
    TpRetirementMetricsValue header[TP_METRICS_H_COUNT];
    u8* status;
    char (*diagnostic)[SHA256_HEX_CAPACITY];
    u64* fallback;
    u8* fixture_path;
    u32 count;
} BqRetirementRowMetrics;

/* Parses a metrics file of at most capacity input records; fixture, when
 * given, marks which inputs record exactly that path. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_metrics_parse(u8 const* bytes, u64 size, u32 capacity, char const* fixture,
    BqRetirementRowMetrics* metrics)
{
    TpRetirementMetricsValue input[TP_METRICS_I_COUNT], function[TP_METRICS_F_COUNT];
    u32 fallback_field = bq_retirement_row_metrics_field("fallback_functions");
    u64 offset = 0;
    char const* line = NULL;
    size_t length = 0;
    bool ok = bytes && size && fallback_field < TP_METRICS_I_COUNT &&
              tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
              tp_retirement_metrics_line(line, length, "CC_METRICS", tp_retirement_metrics_header_fields,
                                         TP_METRICS_H_COUNT, metrics->header) &&
              metrics->header[TP_METRICS_H_INPUTS].number <= capacity;
    u32 inputs = ok ? (u32)metrics->header[TP_METRICS_H_INPUTS].number : 0;
    for (u32 index = 0; ok && index < inputs; index += 1)
    {
        ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
             tp_retirement_metrics_line(line, length, "CC_METRICS_INPUT", tp_retirement_metrics_input_fields,
                                        TP_METRICS_I_COUNT, input) &&
             input[TP_METRICS_I_INDEX].number == index &&
             tp_retirement_metrics_truncation(input, TP_METRICS_I_MESSAGE_BYTES);
        if (ok)
        {
            metrics->status[index] = BQ_RETIREMENT_ROW_STATUSES;
            for (u32 status = 0; status < BQ_RETIREMENT_ROW_STATUSES; status += 1)
                if (tp_retirement_metrics_text(&input[TP_METRICS_I_STATUS], bq_retirement_row_statuses[status]))
                    metrics->status[index] = (u8)status;
            memcpy(metrics->diagnostic[index], input[TP_METRICS_I_DIAGNOSTIC_DIGEST].text, 64);
            metrics->diagnostic[index][64] = 0;
            metrics->fallback[index] = input[fallback_field].number;
            metrics->fixture_path[index] = fixture && tp_retirement_metrics_input_path(&input[TP_METRICS_I_PATH], fixture);
        }
        u64 functions = ok ? input[TP_METRICS_I_FUNCTION_RECORDS].number : 0;
        for (u64 ordinal = 0; ok && ordinal < functions; ordinal += 1)
            ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
                 tp_retirement_metrics_line(line, length, "CC_METRICS_FUNCTION", tp_retirement_metrics_function_fields,
                                            TP_METRICS_F_COUNT, function) &&
                 function[TP_METRICS_F_INPUT].number == index && function[TP_METRICS_F_ORDINAL].number == ordinal;
    }
    ok = ok && offset == size;
    metrics->count = ok ? inputs : 0;
    return ok;
}

/* A per-row compile's metrics, checked as the batch path checks a batch's:
 * the schema, the records and status counts, the process's exit status, the
 * action of the row's stage, and the row's own fixture as the first input.
 * accepted is whether every input compiled, or (linking an executable) was
 * a prebuilt input; diagnostic is the first input's compiler diagnostic
 * digest and fallback the fallback functions over all inputs. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_metrics_single(BqRetirementRowMetrics const* metrics, u32 stage, int exit_code,
    bool* accepted, char diagnostic[SHA256_HEX_CAPACITY], u64* fallback)
{
    TpRetirementMetricsValue const* header = metrics->header;
    u64 counts[BQ_RETIREMENT_ROW_STATUSES + 1u] = {0};
    for (u32 index = 0; index < metrics->count; index += 1) counts[metrics->status[index]] += 1;
    bool ok = metrics->count > 0 && tp_retirement_metrics_text(&header[TP_METRICS_H_SCHEMA], "buster-cc-metrics") &&
              header[TP_METRICS_H_RECORDS].number == metrics->count && !counts[BQ_RETIREMENT_ROW_STATUSES] &&
              header[TP_METRICS_H_OK].number == counts[0] && header[TP_METRICS_H_REJECTED].number == counts[1] &&
              header[TP_METRICS_H_FAILED].number == counts[2] && header[TP_METRICS_H_NOT_RUN].number == counts[3] &&
              header[TP_METRICS_H_PREBUILT].number == counts[4] && exit_code >= 0 &&
              header[TP_METRICS_H_EXIT_STATUS].number == (u64)exit_code &&
              tp_retirement_metrics_text(&header[TP_METRICS_H_ACTION],
                                         stage == BQ_RETIREMENT_STAGE_OBJECT ? "object" : "link") &&
              metrics->fixture_path[0];
    bool all = ok;
    u64 total = 0;
    for (u32 index = 0; ok && index < metrics->count; index += 1)
    {
        all = all && (metrics->status[index] == 0 || (stage != BQ_RETIREMENT_STAGE_OBJECT && metrics->status[index] == 4));
        total += metrics->fallback[index];
    }
    *accepted = ok && all;
    *fallback = total;
    if (ok) memcpy(diagnostic, metrics->diagnostic[0], SHA256_HEX_CAPACITY);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_retirement_row_metrics_allocate(BqRetirementRowMetrics* metrics, u32 capacity)
{
    *metrics = (BqRetirementRowMetrics){0};
    metrics->status = calloc(capacity, 1);
    metrics->diagnostic = calloc(capacity, sizeof(*metrics->diagnostic));
    metrics->fallback = calloc(capacity, sizeof(*metrics->fallback));
    metrics->fixture_path = calloc(capacity, 1);
    bool ok = metrics->status && metrics->diagnostic && metrics->fallback && metrics->fixture_path;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_retirement_row_metrics_free(BqRetirementRowMetrics* metrics)
{
    free(metrics->status);
    free(metrics->diagnostic);
    free(metrics->fallback);
    free(metrics->fixture_path);
    *metrics = (BqRetirementRowMetrics){0};
}

/* A new private step directory under the attempt's retirement-work/. */
BUSTER_GLOBAL_LOCAL int bq_retirement_row_directory(int work, char const* name)
{
    int directory = mkdirat(work, name, 0700) == 0 ?
                    bq_retirement_check_promote(openat(work, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) : -1;
    return directory;
}

/* Fills the code facts of a code-obligated row from its artifact. */
BUSTER_GLOBAL_LOCAL void bq_retirement_row_code(BqRetirementTrustedRow const* row, TpRetirementArtifact const* facts,
    BqRetirementObservedSide* side)
{
    if (row->code_obligation && facts->file_sha256[0])
    {
        memcpy(side->code_sha256, facts->code_sha256, SHA256_HEX_CAPACITY);
        side->code_bytes = facts->code_bytes;
    }
}

/* One batch group on one side: the response file, the batch step, its
 * metrics authenticated against the frozen contract (the plan's statuses,
 * errors, exit status and bound with the observed diagnostics and objects),
 * then each input's observation and its row's facts. Every object is read as
 * an x86_64-linux relocatable, the timed native-host target's. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_batch(BqRetirementRowRun const* run, u32 index, u32 side, char* storage,
    BqRetirementRowObserved* observed)
{
    BqRetirementRowPlan const* plan = run->plan;
    BqRetirementRowPlanGroup const* group = plan->groups + index;
    char name[64];
    snprintf(name, sizeof(name), "group-work-%u-%u", index, side);
    int work = bq_retirement_row_directory(run->work, name);
    BqError status = work >= 3 ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    TpRetirementBatchInput* inputs = calloc(group->input_count, sizeof(*inputs));
    TpRetirementMemberSample* members = calloc(group->input_count, sizeof(*members));
    BqRetirementRowMetrics parsed = {0};
    if (status == BQ_OK && !(inputs && members && bq_retirement_row_metrics_allocate(&parsed, group->input_count)))
        status = BQ_IO;
    /* The response file the {{inputs}} token names, in the work directory. */
    char placeholder[SHA256_HEX_CAPACITY];
    memset(placeholder, '0', 64);
    placeholder[64] = 0;
    TpRetirementBatchContract contract;
    if (status == BQ_OK) bq_retirement_row_skeleton(plan, group, inputs, placeholder, &contract);
    u64 list_size = status == BQ_OK ? tp_retirement_batch_input_list(&contract, NULL, 0) : 0;
    char* list = list_size ? malloc((size_t)list_size) : NULL;
    int list_file = list && tp_retirement_batch_input_list(&contract, list, list_size) == list_size ?
                    bq_retirement_check_create(work, group->list_leaf) : -1;
    if (status == BQ_OK && !(list_file >= 3 && bq_write_all(list_file, (u8 const*)list, (u32)list_size) &&
                             fchmod(list_file, 0400) == 0))
        status = BQ_IO;
    if (list_file >= 0) close(list_file);
    free(list);
    BqRetirementRowStepResult step = {0};
    BqRetirementRowContext context = {.metrics = group->metrics, .inputs = group->list_leaf, .side = side, .label = 1};
    if (status == BQ_OK)
        status = bq_retirement_row_step(run, plan->templates + group->template_index, &context, storage, work, "batch",
                                        false, &step);
    u64 size = 0;
    u8* metrics = status == BQ_OK ? bq_retirement_row_read(work, group->metrics, group->metrics_bytes_max, &size) : NULL;
    if (status == BQ_OK && !(metrics && bq_retirement_row_metrics_parse(metrics, size, group->input_count, NULL, &parsed) &&
                             parsed.count == group->input_count))
        status = BQ_RECIPE_MISMATCH;
    /* The observed contract: every input's diagnostic, and its object when
     * the plan pins one. */
    char (*objects)[SHA256_HEX_CAPACITY] = status == BQ_OK ? calloc(group->input_count, sizeof(*objects)) : NULL;
    TpRetirementArtifact* artifacts = status == BQ_OK ? calloc(group->input_count, sizeof(*artifacts)) : NULL;
    u8* valid = status == BQ_OK ? calloc(group->input_count, 1) : NULL;
    if (status == BQ_OK && !(objects && artifacts && valid)) status = BQ_IO;
    for (u32 slot = 0; status == BQ_OK && slot < group->input_count; slot += 1)
    {
        BqRetirementRowPlanInput const* input = plan->inputs + group->first_input + slot;
        inputs[slot].diagnostic_sha256 = parsed.diagnostic[slot];
        if (input->artifact)
            valid[slot] = bq_retirement_row_artifact(work, input->artifact, BQ_RETIREMENT_UNIT_NATIVE_TARGET,
                                                     BQ_RETIREMENT_STAGE_OBJECT, objects[slot], artifacts + slot);
        inputs[slot].object_sha256 = objects[slot][0] ? objects[slot] : NULL;
    }
    /* The frozen oracle: statuses, errors, diagnostics, object presence and
     * the exit status must be exactly the plan's. */
    if (status == BQ_OK && !(step.exit_code == (int)group->exit_status && !step.timed_out && !step.out_of_memory &&
                             !step.failures &&
                             tp_retirement_metrics_check(metrics, size, &contract, 0, members, group->input_count)))
        status = BQ_RECIPE_MISMATCH;
    for (u32 slot = 0; status == BQ_OK && slot < group->input_count; slot += 1)
    {
        u32 at = group->first_input + slot;
        BqRetirementRowPlanInput const* input = plan->inputs + at;
        bool compiled = parsed.status[slot] == 0;
        memcpy(observed->diagnostic_sha256[at][side], parsed.diagnostic[slot], SHA256_HEX_CAPACITY);
        memcpy(observed->object_sha256[at][side], objects[slot], SHA256_HEX_CAPACITY);
        BqRetirementObservedSide* fact = input->row != TP_RETIREMENT_BATCH_NO_ROW ?
                                         observed->facts[input->row].side + side : NULL;
        if (fact)
        {
            memcpy(fact->compiler_command_sha256, step.command_sha256, SHA256_HEX_CAPACITY);
            memcpy(fact->diagnostic_sha256, parsed.diagnostic[slot], SHA256_HEX_CAPACITY);
            memcpy(fact->artifact_sha256, objects[slot], SHA256_HEX_CAPACITY);
            fact->compiler_exit = compiled ? 0 : step.exit_code;
            fact->runtime_exit = input->member ? -1 : 0;
        }
        if (fact && input->member)
        {
            fact->semantic_pass = compiled && valid[slot];
            fact->fallback_count = parsed.fallback[slot] <= UINT32_MAX ? (u32)parsed.fallback[slot] : UINT32_MAX;
            bq_retirement_row_code(plan->completed + input->row, artifacts + slot, fact);
        }
    }
    free(valid);
    free(objects);
    free(artifacts);
    free(metrics);
    free(inputs);
    free(members);
    bq_retirement_row_metrics_free(&parsed);
    if (work >= 0) close(work);
    return status;
}

/* One per-row compile on one side, and its runtime step when the plan has
 * one. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_compile(BqRetirementRowRun const* run, u32 row, u32 side, char* storage,
    BqRetirementRowObserved* observed)
{
    BqRetirementRowPlan const* plan = run->plan;
    BqRetirementRowPlanRow const* planned = plan->rows + row;
    BqRetirementTrustedRow const* trusted = plan->completed + row;
    BqRetirementObservedSide* fact = observed->facts[row].side + side;
    char name[64], output[32], metrics_name[32];
    snprintf(name, sizeof(name), "row-work-%u-%u", row, side);
    bq_retirement_row_leaves(row, output, metrics_name);
    int work = bq_retirement_row_directory(run->work, name);
    BqError status = work >= 3 ? BQ_OK : BQ_WORKSPACE_MISMATCH;
    BqRetirementRowStepResult step = {0};
    BqRetirementRowContext context = {.fixture = planned->fixture, .output = output, .metrics = metrics_name,
                                      .side = side, .label = 1};
    if (status == BQ_OK)
        status = bq_retirement_row_step(run, plan->templates + planned->compile, &context, storage, work, "compile",
                                        false, &step);
    u64 size = 0;
    u8* metrics = status == BQ_OK ? bq_retirement_row_read(work, metrics_name, TP_RETIREMENT_METRICS_ARTIFACT_BYTES,
                                                           &size) : NULL;
    BqRetirementRowMetrics parsed = {0};
    if (status == BQ_OK && !bq_retirement_row_metrics_allocate(&parsed, TP_RETIREMENT_BATCH_INPUTS)) status = BQ_IO;
    bool accepted = false;
    u64 fallbacks = 0;
    char diagnostic[SHA256_HEX_CAPACITY] = {0};
    bool reported = status == BQ_OK && metrics &&
                    bq_retirement_row_metrics_parse(metrics, size, TP_RETIREMENT_BATCH_INPUTS, planned->fixture,
                                                    &parsed) &&
                    bq_retirement_row_metrics_single(&parsed, trusted->stage, step.exit_code, &accepted, diagnostic,
                                                     &fallbacks);
    TpRetirementArtifact artifact = {0};
    bool valid = status == BQ_OK && bq_retirement_row_artifact(work, output, trusted->target, trusted->stage,
                                                               fact->artifact_sha256, &artifact);
    if (status == BQ_OK)
    {
        memcpy(fact->compiler_command_sha256, step.command_sha256, SHA256_HEX_CAPACITY);
        memcpy(fact->diagnostic_sha256, diagnostic, SHA256_HEX_CAPACITY);
        fact->compiler_exit = step.exit_code;
        fact->timed_out = step.timed_out;
        fact->out_of_memory = step.out_of_memory;
        /* Compile acceptance, not a per-row #509 semantic verdict. */
        fact->semantic_pass = step.exit_code == 0 && !step.failures && reported && accepted && valid;
        fact->fallback_count = reported ? (fallbacks <= UINT32_MAX ? (u32)fallbacks : UINT32_MAX) : UINT32_MAX;
        fact->runtime_exit = -1;
        bq_retirement_row_code(trusted, &artifact, fact);
    }
    /* The generated program, run from the same work directory with stdout
     * and stderr on one writer, as the oracle captured its output. */
    if (status == BQ_OK && planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE && fact->semantic_pass)
    {
        BqRetirementRowStepResult program = {0};
        status = bq_retirement_row_step(run, plan->templates + planned->runtime, &context, storage, work, "runtime",
                                        true, &program);
        if (status == BQ_OK)
        {
            memcpy(fact->runtime_command_sha256, program.command_sha256, SHA256_HEX_CAPACITY);
            memcpy(fact->runtime_output_sha256, program.output_sha256, SHA256_HEX_CAPACITY);
            fact->runtime_exit = program.failures ? -2 : program.exit_code;
            fact->timed_out = fact->timed_out || program.timed_out;
            fact->out_of_memory = fact->out_of_memory || program.out_of_memory;
        }
    }
    free(metrics);
    bq_retirement_row_metrics_free(&parsed);
    if (work >= 0) close(work);
    return status;
}

/* The digest of the named model of one logical CPU from /proc/cpuinfo: its
 * "model name" line, else (AArch64) its "CPU implementer", "CPU variant",
 * "CPU part" and "CPU revision" lines, else "unknown". */
bool bq_retirement_row_cpu_model(u32 cpu, char digest[SHA256_HEX_CAPACITY])
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
    char model[512] = {0}, part[512] = {0};
    bool mine = false, found = false;
    if (ok) text[used] = 0;
    for (char* line = ok ? text : NULL; line && !found && line < text + used;)
    {
        char* end = strchr(line, '\n');
        if (end) *end = 0;
        char* colon = strchr(line, ':');
        size_t key = colon ? (size_t)(colon - line) : 0;
        while (key && (line[key - 1] == ' ' || line[key - 1] == '\t')) key -= 1;
        char const* value = colon ? colon + 1 : "";
        while (*value == ' ') value += 1;
        if (!line[0]) found = mine && (model[0] || part[0]);
        else if (key == 9 && !strncmp(line, "processor", 9))
        {
            char* digits_end = NULL;
            unsigned long number = strtoul(value, &digits_end, 10);
            mine = digits_end != value && number == cpu;
        }
        else if (mine && key == 10 && !strncmp(line, "model name", 10)) snprintf(model, sizeof(model), "%s", value);
        else if (mine && key > 4 && !strncmp(line, "CPU ", 4) && strlen(part) + strlen(line) + 2u < sizeof(part))
        {
            strcat(part, line);
            strcat(part, ";");
        }
        line = end ? end + 1 : NULL;
    }
    found = found || (mine && (model[0] || part[0]));
    char const* name = found && model[0] ? model : found ? part : "unknown";
    if (ok) bq_digest(name, (u32)strlen(name), (char8*)digest);
    free(text);
    return ok;
}

BqError bq_retirement_row_produce(BqRetirementRowRun const* run, BqRetirementRowObserved* observed)
{
    BqRetirementRowPlan const* plan = run ? run->plan : NULL;
    bool fresh = observed && !observed->owned;
    BqError result = fresh && plan && plan->owned && run->binaries && run->binaries->owned &&
                     run->binaries->descriptors[0] >= 3 && run->binaries->descriptors[1] >= 3 && run->sources[0] >= 3 &&
                     run->sources[1] >= 3 && run->work >= 3 && bq_retirement_row_observed_init(plan, observed) ?
                     BQ_OK : BQ_BAD_REQUEST;
    /* A host that cannot run the native target's executables, or whose
     * kernel cannot enforce the whole sandbox. */
    if (result == BQ_OK && !BQ_RETIREMENT_ROW_HOST_NATIVE) result = BQ_CONFIGURATION_MISMATCH;
    u32 sandbox_abi = result == BQ_OK ? bq_retirement_sandbox_abi() : 0;
    if (result == BQ_OK && sandbox_abi < BQ_RETIREMENT_SANDBOX_MIN_ABI) result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK) observed->sandbox_abi = sandbox_abi;
    /* The held binaries are the verified bytes before the first step and
     * after the last; each step checks their file identity. */
    char digests[2][SHA256_HEX_CAPACITY] = {{0}};
    if (result == BQ_OK && !(bq_retirement_check_binaries(run->binaries, digests) &&
                             !memcmp(digests, run->binaries->verified.binary_sha256, sizeof(digests))))
        result = BQ_SOURCE_MISMATCH;
    /* The rows run on the plan's CPU alone, and the observation records the
     * CPU model and affinity they actually ran with. */
    cpu_set_t previous, pinned;
    CPU_ZERO(&previous);
    CPU_ZERO(&pinned);
    CPU_SET(plan && plan->cpu < CPU_SETSIZE ? plan->cpu : 0, &pinned);
    bool saved = result == BQ_OK && sched_getaffinity(0, sizeof(previous), &previous) == 0;
    bool moved = saved && sched_setaffinity(0, sizeof(pinned), &pinned) == 0;
    if (result == BQ_OK && !moved) result = BQ_CONFIGURATION_MISMATCH;
    char unused_model[SHA256_HEX_CAPACITY];
    if (result == BQ_OK && !(bq_retirement_check_provenance(unused_model, &observed->cpus, observed->cpu_mask) &&
                             bq_retirement_row_cpu_model(plan->cpu, observed->cpu_model_sha256)))
        result = BQ_IO;
    char* storage = result == BQ_OK ? malloc(BQ_RETIREMENT_ROW_COMMAND_STORAGE) : NULL;
    if (result == BQ_OK && !storage) result = BQ_IO;
    for (u32 row = 0; result == BQ_OK && row < plan->row_count; row += 1)
    {
        BqRetirementTrustedRow const* trusted = plan->completed + row;
        BqRetirementRowFact* fact = observed->facts + row;
        *fact = (BqRetirementRowFact){.row = trusted->row, .census_row = trusted->census_row,
            .compiler_eligible = trusted->compiler_eligible != 0,
            .runtime_eligible = bq_retirement_row_native_runtime(trusted, plan->native_target)};
    }
    for (u32 side = 0; side < 2; side += 1)
    {
        for (u32 group = 0; result == BQ_OK && group < plan->group_count; group += 1)
            result = bq_retirement_row_batch(run, group, side, storage, observed);
        for (u32 row = 0; result == BQ_OK && row < plan->row_count; row += 1)
            if (plan->rows[row].compile != BQ_RETIREMENT_ROW_PLAN_NONE &&
                plan->rows[row].compile != BQ_RETIREMENT_ROW_PLAN_BATCH)
                result = bq_retirement_row_compile(run, row, side, storage, observed);
    }
    for (u32 row = 0; result == BQ_OK && row < plan->row_count; row += 1)
    {
        BqRetirementRowFact* fact = observed->facts + row;
        fact->code_eligible = fact->compiler_eligible && plan->completed[row].code_obligation &&
                              fact->side[0].code_bytes > 0;
    }
    if (result == BQ_OK && !(bq_retirement_check_binaries(run->binaries, digests) &&
                             !memcmp(digests, run->binaries->verified.binary_sha256, sizeof(digests))))
        result = BQ_SOURCE_MISMATCH;
    free(storage);
    if (moved && sched_setaffinity(0, sizeof(previous), &previous) != 0 && result == BQ_OK) result = BQ_IO;
    if (result != BQ_OK && fresh && observed->owned) bq_retirement_row_observed_release(observed);
    return result;
}
