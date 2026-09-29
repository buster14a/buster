/* In-unit row-evidence producer (#1020/#509 design step 9, #881 lane B).
 *
 * Ownership: running an imported row plan with the unit's held matched
 * binaries and observing its facts. The plan and the canonical evidence live
 * in retirement_row_plan.c; the gate that persists and admits the evidence
 * in retirement_unit.c.
 *
 * Entry point: bq_retirement_row_produce.
 *
 * Map: bq_retirement_row_spawn forks one step through
 * bq_retirement_build_child's normalized state (retirement_check_runner.c's
 * limits: own process group, default signals, empty mask, umask 0077,
 * close-on-exec from 3, RLIMIT_AS, no core) and places the side's held
 * binary, A's two roots and the step's work directory in the canonical slots
 * the plan's digests assume; bq_retirement_row_step runs it as the check
 * runner runs a check (no other child first, subreaper, the step and job
 * bounds, cancellation, the sweep) with the held binaries fstat'ed and
 * rehashed around it; bq_retirement_row_metrics_inputs reads the compiler's
 * per-input metrics records; bq_retirement_row_artifact reads an artifact and
 * its code section with the independent reader; bq_retirement_row_batch,
 * bq_retirement_row_compile observe a batch group and a per-row compile (with
 * its runtime step) for one side.
 */
#include "retirement_row_plan.h"

/* One step's outcome. */
typedef struct BqRetirementRowStepResult
{
    int exit_code;
    u32 timed_out, out_of_memory, failures;
    char command_sha256[SHA256_HEX_CAPACITY];
    char output_sha256[SHA256_HEX_CAPACITY], log_sha256[SHA256_HEX_CAPACITY];
} BqRetirementRowStepResult;

/* Forks one step (see the map). held is the side's binary, A's base and
 * candidate roots and the work directory, in slot order. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_spawn(BqRetirementRowCommand const* command, int const held[4], u32 side,
    u64 memory_bytes, BqRetirementCheckChild* child)
{
    int output[2] = {-1, -1}, log[2] = {-1, -1};
    bool ok = pipe2(output, O_CLOEXEC) == 0 && pipe2(log, O_CLOEXEC) == 0 && output[0] >= 3 && output[1] >= 3 &&
              log[0] >= 3 && log[1] >= 3;
    pid_t process = ok ? fork() : -1;
    if (process == 0)
    {
        struct rlimit memory = {(rlim_t)memory_bytes, (rlim_t)memory_bytes}, core = {0, 0};
        int const slots[4] = {BQ_RETIREMENT_ROW_SLOT_BINARY + (int)side, BQ_RETIREMENT_ROW_SLOT_SOURCE,
                              BQ_RETIREMENT_ROW_SLOT_SOURCE + 1, BQ_RETIREMENT_ROW_SLOT_WORK};
        int moved[4] = {-1, -1, -1, -1};
        int entry = dup(held[3]);
        bool ready = entry >= 3 && bq_retirement_build_child(log[1], -1, entry, true, 0077) &&
                     dup2(output[1], STDOUT_FILENO) == STDOUT_FILENO && setrlimit(RLIMIT_CORE, &core) == 0 &&
                     setrlimit(RLIMIT_AS, &memory) == 0;
        /* Out of the way first, then into the canonical slots; dup2 clears
         * close-on-exec on exactly those four. */
        for (u32 index = 0; ready && index < 4; index += 1)
        {
            moved[index] = fcntl(held[index], F_DUPFD_CLOEXEC, 64);
            ready = moved[index] >= 64;
        }
        for (u32 index = 0; ready && index < 4; index += 1) ready = dup2(moved[index], slots[index]) == slots[index];
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

/* Runs one step in work with the side's held binary. BQ_OK means the child
 * reached a verdict; result then holds how it ended. */
BUSTER_GLOBAL_LOCAL BqError bq_retirement_row_step(BqRetirementRowRun const* run, BqRetirementRowTemplate const* template,
    BqRetirementRowContext const* context, char* storage, int work, char const* capture, BqRetirementRowStepResult* result)
{
    *result = (BqRetirementRowStepResult){.exit_code = -1};
    BqError status = bq_retirement_check_stop(run->cancellation_fd, run->deadline_ns);
    if (status == BQ_OK && !bq_retirement_check_descendants_absent()) status = BQ_WORKER_MISMATCH;
    BqRetirementRowCommand command;
    if (status == BQ_OK && !(bq_retirement_row_command(template, context, storage, &command) &&
                             bq_retirement_row_command_digest(&command, result->command_sha256)))
        status = BQ_CONFIGURATION_MISMATCH;
    /* The held binaries: the verified bytes now, and the same unchanged
     * files after the child. */
    BqRetirementHeldBinaries const* binaries = run->binaries;
    struct stat before[2] = {{0}}, after[2] = {{0}};
    char digests[2][SHA256_HEX_CAPACITY] = {{0}};
    for (u32 side = 0; status == BQ_OK && side < 2; side += 1)
        if (fstat(binaries->descriptors[side], before + side) != 0) status = BQ_SOURCE_MISMATCH;
    if (status == BQ_OK && !(bq_retirement_check_binaries(binaries, digests) &&
                             !memcmp(digests, binaries->verified.binary_sha256, sizeof(digests))))
        status = BQ_SOURCE_MISMATCH;
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
        status = bq_retirement_row_spawn(&command, held, context->side, (u64)template->memory_mib << 20, &child) ?
                 BQ_OK : BQ_IO;
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
        sha256_finish_hex(&child.output_hash, result->output_sha256);
        sha256_finish_hex(&child.log_hash, result->log_sha256);
    }
    for (u32 side = 0; status == BQ_OK && side < 2; side += 1)
        if (!(fstat(binaries->descriptors[side], after + side) == 0 &&
              bq_retirement_oracle_same_file(before + side, after + side)))
            status = BQ_SOURCE_MISMATCH;
    if (status == BQ_OK && !(bq_retirement_check_binaries(binaries, digests) &&
                             !memcmp(digests, binaries->verified.binary_sha256, sizeof(digests))))
        status = BQ_SOURCE_MISMATCH;
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

/* One observed artifact: its file digest and, with the independent reader,
 * its code section. readable is false when the reader refuses it. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_artifact(int work, char const* name, char digest[SHA256_HEX_CAPACITY],
    TpRetirementArtifact* facts)
{
    u64 size = 0;
    u8* bytes = bq_retirement_row_read(work, name, BQ_RETIREMENT_ROW_ARTIFACT_BYTES_CAP, &size);
    bool present = bytes != NULL;
    if (present) bq_digest(bytes, (u32)size, (char8*)digest);
    bool readable = present && tp_retirement_artifact(bytes, size, facts);
    if (!readable) *facts = (TpRetirementArtifact){0};
    free(bytes);
    return readable;
}

/* The per-input records of a metrics file, in order: each input's status
 * ("ok" or not), diagnostic digest and fallback-function count. count
 * receives the number of input records; false on any record that does not
 * parse. */
BUSTER_GLOBAL_LOCAL bool bq_retirement_row_metrics_inputs(u8 const* bytes, u64 size, u32 capacity, u32* count,
    u8* compiled, char (*diagnostic)[SHA256_HEX_CAPACITY], u64* fallback)
{
    TpRetirementMetricsValue header[TP_METRICS_H_COUNT], input[TP_METRICS_I_COUNT], function[TP_METRICS_F_COUNT];
    u64 offset = 0;
    char const* line = NULL;
    size_t length = 0;
    bool ok = bytes && size && tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
              tp_retirement_metrics_line(line, length, "CC_METRICS", tp_retirement_metrics_header_fields,
                                         TP_METRICS_H_COUNT, header) &&
              header[TP_METRICS_H_INPUTS].number <= capacity;
    u32 inputs = ok ? (u32)header[TP_METRICS_H_INPUTS].number : 0;
    for (u32 index = 0; ok && index < inputs; index += 1)
    {
        ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
             tp_retirement_metrics_line(line, length, "CC_METRICS_INPUT", tp_retirement_metrics_input_fields,
                                        TP_METRICS_I_COUNT, input) &&
             input[TP_METRICS_I_INDEX].number == index;
        if (ok)
        {
            compiled[index] = tp_retirement_metrics_text(&input[TP_METRICS_I_STATUS], "ok");
            memcpy(diagnostic[index], input[TP_METRICS_I_DIAGNOSTIC_DIGEST].text, 64);
            diagnostic[index][64] = 0;
            fallback[index] = input[38].number;
        }
        u64 functions = ok ? input[TP_METRICS_I_FUNCTION_RECORDS].number : 0;
        for (u64 ordinal = 0; ok && ordinal < functions; ordinal += 1)
            ok = tp_retirement_metrics_next(bytes, size, &offset, &line, &length) &&
                 tp_retirement_metrics_line(line, length, "CC_METRICS_FUNCTION", tp_retirement_metrics_function_fields,
                                            TP_METRICS_F_COUNT, function);
    }
    ok = ok && offset == size;
    *count = ok ? inputs : 0;
    return ok;
}
BUSTER_CT_CHECK(TP_METRICS_I_COUNT > 38);

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
 * then each input's observation and its row's facts. */
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
    u8* compiled = calloc(group->input_count, 1);
    char (*diagnostic)[SHA256_HEX_CAPACITY] = calloc(group->input_count, sizeof(*diagnostic));
    u64* fallback = calloc(group->input_count, sizeof(*fallback));
    TpRetirementMemberSample* members = calloc(group->input_count, sizeof(*members));
    if (status == BQ_OK && !(inputs && compiled && diagnostic && fallback && members)) status = BQ_IO;
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
                                        &step);
    u64 size = 0;
    u8* metrics = status == BQ_OK ? bq_retirement_row_read(work, group->metrics, group->metrics_bytes_max, &size) : NULL;
    u32 records = 0;
    if (status == BQ_OK &&
        !(metrics && bq_retirement_row_metrics_inputs(metrics, size, group->input_count, &records, compiled, diagnostic,
                                                      fallback) && records == group->input_count))
        status = BQ_RECIPE_MISMATCH;
    /* The observed contract: every input's diagnostic, and its object when
     * the plan pins one. */
    char (*objects)[SHA256_HEX_CAPACITY] = status == BQ_OK ? calloc(group->input_count, sizeof(*objects)) : NULL;
    TpRetirementArtifact* artifacts = status == BQ_OK ? calloc(group->input_count, sizeof(*artifacts)) : NULL;
    if (status == BQ_OK && !(objects && artifacts)) status = BQ_IO;
    for (u32 slot = 0; status == BQ_OK && slot < group->input_count; slot += 1)
    {
        BqRetirementRowPlanInput const* input = plan->inputs + group->first_input + slot;
        inputs[slot].diagnostic_sha256 = diagnostic[slot];
        if (input->artifact) bq_retirement_row_artifact(work, input->artifact, objects[slot], artifacts + slot);
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
        memcpy(observed->diagnostic_sha256[at][side], diagnostic[slot], SHA256_HEX_CAPACITY);
        memcpy(observed->object_sha256[at][side], objects[slot], SHA256_HEX_CAPACITY);
        BqRetirementObservedSide* fact = input->row != TP_RETIREMENT_BATCH_NO_ROW ?
                                         observed->facts[input->row].side + side : NULL;
        if (fact)
        {
            memcpy(fact->compiler_command_sha256, step.command_sha256, SHA256_HEX_CAPACITY);
            memcpy(fact->diagnostic_sha256, diagnostic[slot], SHA256_HEX_CAPACITY);
            memcpy(fact->artifact_sha256, objects[slot], SHA256_HEX_CAPACITY);
            fact->compiler_exit = compiled[slot] ? 0 : step.exit_code;
            fact->runtime_exit = input->member ? -1 : 0;
        }
        if (fact && input->member)
        {
            fact->semantic_pass = compiled[slot] && artifacts[slot].file_sha256[0];
            fact->fallback_count = fallback[slot] <= UINT32_MAX ? (u32)fallback[slot] : UINT32_MAX;
            bq_retirement_row_code(plan->completed + input->row, artifacts + slot, fact);
        }
    }
    free(objects);
    free(artifacts);
    free(metrics);
    free(inputs);
    free(compiled);
    free(diagnostic);
    free(fallback);
    free(members);
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
                                        &step);
    u64 size = 0;
    u8* metrics = status == BQ_OK ? bq_retirement_row_read(work, metrics_name, TP_RETIREMENT_METRICS_ARTIFACT_BYTES,
                                                           &size) : NULL;
    u8 compiled[TP_RETIREMENT_BATCH_INPUTS] = {0};
    char (*diagnostic)[SHA256_HEX_CAPACITY] = status == BQ_OK ? calloc(TP_RETIREMENT_BATCH_INPUTS,
                                                                       sizeof(*diagnostic)) : NULL;
    u64* fallback = status == BQ_OK ? calloc(TP_RETIREMENT_BATCH_INPUTS, sizeof(*fallback)) : NULL;
    if (status == BQ_OK && !(diagnostic && fallback)) status = BQ_IO;
    u32 records = 0;
    bool reported = status == BQ_OK && metrics &&
                    bq_retirement_row_metrics_inputs(metrics, size, TP_RETIREMENT_BATCH_INPUTS, &records, compiled,
                                                     diagnostic, fallback) && records > 0;
    u64 fallbacks = 0;
    bool all_compiled = reported;
    for (u32 index = 0; reported && index < records; index += 1)
    {
        all_compiled = all_compiled && compiled[index];
        fallbacks += fallback[index];
    }
    TpRetirementArtifact artifact = {0};
    bool readable = status == BQ_OK && bq_retirement_row_artifact(work, output, fact->artifact_sha256, &artifact);
    if (status == BQ_OK)
    {
        memcpy(fact->compiler_command_sha256, step.command_sha256, SHA256_HEX_CAPACITY);
        memcpy(fact->diagnostic_sha256, step.log_sha256, SHA256_HEX_CAPACITY);
        fact->compiler_exit = step.exit_code;
        fact->timed_out = step.timed_out;
        fact->out_of_memory = step.out_of_memory;
        fact->semantic_pass = step.exit_code == 0 && !step.failures && all_compiled && readable;
        fact->fallback_count = reported ? (fallbacks <= UINT32_MAX ? (u32)fallbacks : UINT32_MAX) : UINT32_MAX;
        fact->runtime_exit = -1;
        bq_retirement_row_code(plan->completed + row, &artifact, fact);
    }
    /* The generated program, run from the same work directory. */
    if (status == BQ_OK && planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE && fact->semantic_pass)
    {
        BqRetirementRowStepResult program = {0};
        status = bq_retirement_row_step(run, plan->templates + planned->runtime, &context, storage, work, "runtime",
                                        &program);
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
    free(diagnostic);
    free(fallback);
    if (work >= 0) close(work);
    return status;
}

BqError bq_retirement_row_produce(BqRetirementRowRun const* run, BqRetirementRowObserved* observed)
{
    BqRetirementRowPlan const* plan = run ? run->plan : NULL;
    bool fresh = observed && !observed->owned;
    BqError result = fresh && plan && plan->owned && run->binaries && run->binaries->owned &&
                     run->binaries->descriptors[0] >= 3 && run->binaries->descriptors[1] >= 3 && run->sources[0] >= 3 &&
                     run->sources[1] >= 3 && run->work >= 3 && bq_retirement_row_observed_init(plan, observed) ?
                     BQ_OK : BQ_BAD_REQUEST;
    /* The rows run on the plan's CPU alone, and the observation records the
     * CPU model and affinity they actually ran with. */
    cpu_set_t previous, pinned;
    CPU_ZERO(&previous);
    CPU_ZERO(&pinned);
    CPU_SET(plan && plan->cpu < CPU_SETSIZE ? plan->cpu : 0, &pinned);
    bool saved = result == BQ_OK && sched_getaffinity(0, sizeof(previous), &previous) == 0;
    bool moved = saved && sched_setaffinity(0, sizeof(pinned), &pinned) == 0;
    if (result == BQ_OK && !moved) result = BQ_CONFIGURATION_MISMATCH;
    if (result == BQ_OK && !bq_retirement_check_provenance(observed->cpu_model_sha256, &observed->cpus,
                                                           observed->cpu_mask))
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
    free(storage);
    if (moved && sched_setaffinity(0, sizeof(previous), &previous) != 0 && result == BQ_OK) result = BQ_IO;
    if (result != BQ_OK && fresh && observed->owned) bq_retirement_row_observed_release(observed);
    return result;
}
