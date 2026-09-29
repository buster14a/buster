/* #1020/#509 lane B step 9, per-row half: fixtures for the pinned row-plan
 * authority (retirement_row_plan.c) and the in-unit producer
 * (retirement_row_producer.c). Included by the preparation test runner after
 * the check-runner fixture, whose six-row projection, pinned required-check
 * authority and profile it reuses, and before the unit-oracle fixture, which
 * uses bq_row_test_plan, bq_row_test_install and bq_row_test_observe to issue
 * the pinned gate on the real attempt and to replay it.
 *
 * bq_row_test_runner covers the importer (pin, binding, coverage and A1
 * rules), the derivation (commands in the canonical child layout, batch keys
 * and controls, the second A/A label aggregate), the producer with two
 * stand-in compilers (dash scripts that copy prepared objects, executables
 * and compiler metrics records out of A's candidate root, so they prove the
 * mechanics, not any compiler), the canonical row evidence, the join and the
 * correctness gate on the result, and each refusal: a forged or foreign plan,
 * a row missing or added (plan or evidence), a command, CPU, batch-key or
 * control-status mismatch and a modified evidence file. The producer cases
 * also cover the sandbox (a generated program that tries to read the
 * reference oracle's output and plant a file beside its step is denied and
 * its row refused), per-target artifact checks (wrong machine, wrong kind),
 * prebuilt link inputs, a held binary changed before or during a step, and
 * the second A/A aggregate against lane D's bq_retirement_campaign_bind.
 * bq_row_test_sockets has a row and a required check try to connect to a
 * listening unix socket beside their step directories (the seccomp filter
 * denies it) and refuses both below Landlock ABI
 * BQ_RETIREMENT_SANDBOX_MIN_ABI through bq_retirement_sandbox_abi_ceiling.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ROW_PLAN_TESTS_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ROW_PLAN_TESTS_H

#define BQ_ROW_TEST_PLAN_CAP (4u * 1024u * 1024u)
#define BQ_ROW_TEST_RUNTIME_OUTPUT "fixture-runtime\n"
/* Landlock ABI 5, which lacks the signal and abstract-socket scopes: the
 * sandbox must refuse it (a fixed number, so a lowered floor fails). */
#define BQ_ROW_TEST_OLD_ABI 5u
/* bq_row_test_plan variants. */
#define BQ_ROW_TEST_DROP_ROW 1u
#define BQ_ROW_TEST_ADD_ROW 2u
#define BQ_ROW_TEST_TIMED_SINGLE 4u
#define BQ_ROW_TEST_FOREIGN 8u
#define BQ_ROW_TEST_SPLIT 16u
#define BQ_ROW_TEST_WRONG_FIXTURE 32u
#define BQ_ROW_TEST_RUNTIME_OBJECT 64u
#define BQ_ROW_TEST_PATH_EXECUTABLE 128u
#define BQ_ROW_TEST_CONTROL_FAILED 256u
#define BQ_ROW_TEST_UNKNOWN_TOKEN 512u

BUSTER_GLOBAL_LOCAL bool bq_row_test_control(BqRetirementTrustedRow const* row, u32 native)
{
    bool control = !row->compiler_eligible && row->stage == BQ_RETIREMENT_STAGE_OBJECT && row->target == native;
    return control;
}

/* A plan over projection: one batch group of every timed object row (its
 * native ineligible object rows as rejected controls), one compile template
 * for every other compiler-eligible row and a runtime template for native
 * runtime rows; fixtures are tests/row-<i>.c and objects row-<i>.c.o. */
BUSTER_GLOBAL_LOCAL u32 bq_row_test_plan(char* text, u32 capacity, BqRetirementProjection const* projection,
    char const cpu_model[SHA256_HEX_CAPACITY], u32 cpu, u32 flags)
{
    BqRetirementPrepared const* prepared = &projection->prepared;
    u32 native = prepared->native_target, rows = prepared->rows, first_timed = UINT32_MAX, first_foreign = UINT32_MAX;
    for (u32 index = 0; index < rows; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        if (first_timed == UINT32_MAX && bq_retirement_row_timed_object(row, native)) first_timed = index;
        if (first_foreign == UINT32_MAX && row->compiler_eligible && row->target != native) first_foreign = index;
    }
    char population[SHA256_HEX_CAPACITY];
    memcpy(population, projection->population_sha256, SHA256_HEX_CAPACITY);
    if (flags & BQ_ROW_TEST_FOREIGN) population[0] = population[0] == '0' ? '1' : '0';
    u32 listed = rows - ((flags & BQ_ROW_TEST_DROP_ROW) ? 1u : 0u) + ((flags & BQ_ROW_TEST_ADD_ROW) ? 1u : 0u);
    int used = snprintf(text, capacity, "BQ-RETIREMENT-ROW-PLAN-V1\nsupport=%s\ncensus=%s\npopulation=%s\n"
        "native-target=%u\ncpu=%s %u\ntemplates=3\ntemplate=0 compile 30 1024\nargv=7\narg=%s\narg=compile\n"
        "arg={{source:1}}\narg=%s\narg={{output}}\narg={{metrics}}\narg=--label={{label}}\nenvironment=2\n"
        "env=LC_ALL=C\nenv=PATH=/usr/bin:/bin\ntemplate=1 batch 30 1024\nargv=6\narg={{binary}}\narg=batch\n"
        "arg={{source:1}}\narg=@{{inputs}}\narg={{metrics}}\narg=--label={{label}}\nenvironment=2\nenv=LC_ALL=C\n"
        "env=PATH=/usr/bin:/bin\ntemplate=2 runtime 30 1024\nargv=1\narg=./{{output}}\nenvironment=0\nrows=%u\n",
        prepared->support_sha256, prepared->census_sha256, population, native, cpu_model, cpu,
        (flags & BQ_ROW_TEST_PATH_EXECUTABLE) ? "/bin/sh" : "{{binary}}",
        (flags & BQ_ROW_TEST_UNKNOWN_TOKEN) ? "{{fixture}}{{tool:0}}" : "{{fixture}}", listed);
    bool ok = used > 0 && (u32)used < capacity;
    for (u32 index = 0; ok && index < listed; index += 1)
    {
        BqRetirementTrustedRow const* row = index < rows ? projection->rows + index : NULL;
        bool batch = row && (bq_retirement_row_timed_object(row, native) || bq_row_test_control(row, native)) &&
                     !((flags & BQ_ROW_TEST_TIMED_SINGLE) && index == first_timed);
        bool compile = !row || batch || row->compiler_eligible;
        bool runtime = row && (bq_retirement_row_native_runtime(row, native) ||
                               ((flags & BQ_ROW_TEST_RUNTIME_OBJECT) && index == first_foreign));
        int line = snprintf(text + used, capacity - (u32)used, "row=%u %s %s ", index,
                            batch ? "batch" : compile ? "0" : "-", runtime ? "2" : "-");
        ok = line > 0 && (u32)line < capacity - (u32)used;
        if (ok) used += line;
        line = ok ? (compile ? snprintf(text + used, capacity - (u32)used, "tests/row-%u.c\n", index) :
                               snprintf(text + used, capacity - (u32)used, "-\n")) : -1;
        ok = ok && line > 0 && (u32)line < capacity - (u32)used;
        if (ok) used += line;
    }
    /* The groups: members in row order, then controls; SPLIT puts the first
     * member (with the controls) in its own group under the same template. */
    u32 members = 0, controls = 0;
    for (u32 index = 0; index < rows; index += 1)
    {
        members += bq_retirement_row_timed_object(projection->rows + index, native) &&
                   !((flags & BQ_ROW_TEST_TIMED_SINGLE) && index == first_timed);
        controls += bq_row_test_control(projection->rows + index, native);
    }
    bool split = (flags & BQ_ROW_TEST_SPLIT) && members > 1;
    u32 groups = members ? (split ? 2u : 1u) : 0u;
    int line = ok ? snprintf(text + used, capacity - (u32)used, "groups=%u\n", groups) : -1;
    ok = ok && line > 0 && (u32)line < capacity - (u32)used;
    if (ok) used += line;
    for (u32 group = 0; ok && group < groups; group += 1)
    {
        u32 group_members = split ? (group ? members - 1u : 1u) : members;
        u32 group_controls = group ? 0 : controls;
        /* The reviewed campaign budget's metrics bound for the group's
         * inputs, as the campaign requires of every object contract. */
        TpRetirementCampaignBudget budget = bq_campaign_service_budget();
        uint64_t bound = 0;
        ok = tp_retirement_budget_metrics_bytes(&budget, group_members + group_controls, &bound);
        line = ok ? snprintf(text + used, capacity - (u32)used, "group=%u 1 none batch.metrics %" PRIu64 " %u %u\n",
                             group, (uint64_t)bound, group_controls ? 1u : 0u, group_members + group_controls) : -1;
        ok = line > 0 && (u32)line < capacity - (u32)used;
        if (ok) used += line;
        u32 seen = 0, written = 0;
        for (u32 index = 0; ok && index < rows; index += 1)
        {
            bool member = bq_retirement_row_timed_object(projection->rows + index, native) &&
                          !((flags & BQ_ROW_TEST_TIMED_SINGLE) && index == first_timed);
            bool mine = member && (split ? (group ? seen > 0 : seen == 0) : true);
            if (member) seen += 1;
            if (!mine) continue;
            bool wrong = (flags & BQ_ROW_TEST_WRONG_FIXTURE) && written == 0 && group == 0;
            line = snprintf(text + used, capacity - (u32)used, "input=%u 1 ok driver.none row-%u.c.o tests/%s%u.c\n",
                            index, index, wrong ? "other-" : "row-", index);
            ok = line > 0 && (u32)line < capacity - (u32)used;
            if (ok) used += line;
            written += 1;
        }
        for (u32 index = 0; ok && !group && index < rows; index += 1)
        {
            if (!bq_row_test_control(projection->rows + index, native)) continue;
            bool failed = (flags & BQ_ROW_TEST_CONTROL_FAILED) != 0;
            line = snprintf(text + used, capacity - (u32)used, "input=%u 0 %s %s - tests/row-%u.c\n", index,
                            failed ? "failed" : "rejected", failed ? "driver.failed" : "driver.rejected", index);
            ok = line > 0 && (u32)line < capacity - (u32)used;
            if (ok) used += line;
        }
    }
    return ok ? (u32)used : 0;
}

/* Installs (or replaces) the plan under recipes/ and returns its pin line. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_install(char const* recipes, char const* text, u32 length, char pin[128])
{
    char path[256], digest[SHA256_HEX_CAPACITY] = {0};
    int named = snprintf(path, sizeof(path), "%s/" BQ_RETIREMENT_ROW_PLAN_NAME, recipes);
    bool ok = named > 0 && (size_t)named < sizeof(path) && length && chmod(recipes, 0700) == 0 &&
              (unlink(path) == 0 || errno == ENOENT) && bq_prep_test_write_bytes(path, text, length) &&
              chmod(recipes, 0500) == 0;
    if (ok) bq_digest(text, length, (char8*)digest);
    int written = ok ? snprintf(pin, 128, "row-plan-sha256=%s\n", digest) : -1;
    return ok && written > 0 && written < 128;
}

/* The CPU this process may run on first, and the model digest the producer
 * will observe. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_cpu(u32* cpu, char model[SHA256_HEX_CAPACITY])
{
    cpu_set_t set;
    CPU_ZERO(&set);
    bool ok = sched_getaffinity(0, sizeof(set), &set) == 0;
    *cpu = UINT32_MAX;
    for (u32 index = 0; ok && index < CPU_SETSIZE && *cpu == UINT32_MAX; index += 1)
        if (CPU_ISSET(index, &set)) *cpu = index;
    ok = ok && *cpu != UINT32_MAX && bq_retirement_row_cpu_model(*cpu, model);
    return ok;
}

/* An observation of plan as an honest producer would make it, with
 * synthetic artifact, code and diagnostic digests: every fact consistent
 * with the plan's derived commands, statuses and the projection's oracles. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_observe(BqRetirementRowPlan const* plan, BqRetirementRowObserved* observed)
{
    bool ok = bq_retirement_row_observed_init(plan, observed);
    if (ok)
    {
        memcpy(observed->cpu_model_sha256, plan->cpu_model_sha256, SHA256_HEX_CAPACITY);
        observed->cpus = 1;
        observed->sandbox_abi = BQ_RETIREMENT_SANDBOX_MIN_ABI;
        bq_retirement_row_cpu_mask(plan->cpu, observed->cpu_mask);
    }
    char empty[SHA256_HEX_CAPACITY], control[SHA256_HEX_CAPACITY];
    bq_digest("", 0, (char8*)empty);
    bq_digest("control", 7, (char8*)control);
    for (u32 index = 0; ok && index < plan->row_count; index += 1)
    {
        BqRetirementTrustedRow const* row = plan->completed + index;
        BqRetirementRowPlanRow const* planned = plan->rows + index;
        BqRetirementRowFact* fact = observed->facts + index;
        *fact = (BqRetirementRowFact){.row = index, .census_row = row->census_row,
            .compiler_eligible = row->compiler_eligible != 0,
            .runtime_eligible = bq_retirement_row_native_runtime(row, plan->native_target)};
        for (u32 side = 0; side < 2; side += 1)
        {
            BqRetirementObservedSide* facts = fact->side + side;
            BqRetirementRowPlanInput const* input = planned->compile == BQ_RETIREMENT_ROW_PLAN_BATCH ?
                                                    plan->inputs + planned->input : NULL;
            bool compiled = input ? !strcmp(input->status, "ok") : planned->compile != BQ_RETIREMENT_ROW_PLAN_NONE;
            if (planned->compile == BQ_RETIREMENT_ROW_PLAN_NONE)
                continue;
            memcpy(facts->compiler_command_sha256, row->compiler_command_sha256[side], SHA256_HEX_CAPACITY);
            memcpy(facts->diagnostic_sha256, input && !input->member ? control : empty, SHA256_HEX_CAPACITY);
            if (compiled) bq_check_test_digest(facts->artifact_sha256, "row-artifact", index, side);
            facts->compiler_exit = compiled ? 0 : (int)plan->groups[planned->group].exit_status;
            facts->runtime_exit = input && !input->member ? 0 : -1;
            if (row->compiler_eligible)
            {
                facts->semantic_pass = 1;
                if (row->code_obligation)
                {
                    bq_check_test_digest(facts->code_sha256, "row-code", index, side);
                    facts->code_bytes = 16;
                }
            }
            if (planned->runtime != BQ_RETIREMENT_ROW_PLAN_NONE)
            {
                memcpy(facts->runtime_command_sha256, row->runtime_command_sha256[side], SHA256_HEX_CAPACITY);
                memcpy(facts->runtime_output_sha256, row->independent_oracle_sha256, SHA256_HEX_CAPACITY);
                facts->runtime_exit = 0;
            }
            if (input)
            {
                memcpy(observed->diagnostic_sha256[planned->input][side], facts->diagnostic_sha256,
                       SHA256_HEX_CAPACITY);
                memcpy(observed->object_sha256[planned->input][side], facts->artifact_sha256, SHA256_HEX_CAPACITY);
            }
        }
        fact->code_eligible = fact->compiler_eligible && row->code_obligation && fact->side[0].code_bytes > 0;
    }
    return ok;
}

/* The passing results the required checks' receipts prove, as the replay
 * rebuilds them. */
BUSTER_GLOBAL_LOCAL void bq_row_test_passing(BqRetirementRequiredChecks const* checks, BqRetirementCheckResult* results)
{
    for (u32 index = 0; index < checks->count; index += 1)
    {
        BqRetirementRequiredCheck const* required = checks->checks + index;
        BqRetirementCheckResult* check = results + index;
        *check = (BqRetirementCheckResult){.kind = required->kind, .target = required->target, .rows = required->rows};
        memcpy(check->command_sha256, required->command_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->configuration_sha256, required->configuration_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->receipt_sha256, required->receipt_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->preparation_sha256, checks->preparation_sha256, SHA256_HEX_CAPACITY);
        memcpy(check->source_sha256, checks->source_sha256, sizeof(check->source_sha256));
        memcpy(check->binary_sha256, checks->binary_sha256, sizeof(check->binary_sha256));
    }
}

/* A minimal ELF64 file for machine (62 x86-64, 183 AArch64) of type (1 a
 * relocatable object, 2 an executable) whose one code section holds code. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_elf(int directory, char const* name, char const* code, u32 machine, u32 type)
{
    u8 bytes[512] = {0};
    u32 length = (u32)strlen(code), table = (64u + length + 7u) & ~7u;
    bool ok = table + 128u <= sizeof(bytes);
    if (ok)
    {
        memcpy(bytes, "\177ELF\2\1\1", 7);
        bytes[16] = (u8)type;
        bytes[18] = (u8)machine;
        bytes[20] = 1;
        for (u32 index = 0; index < 8; index += 1) bytes[40 + index] = (u8)((u64)table >> (index * 8));
        bytes[52] = 64;
        bytes[58] = 64;
        bytes[60] = 2;
        memcpy(bytes + 64, code, length);
        u8* section = bytes + table + 64;
        section[4] = 1;
        section[8] = 6;
        section[24] = 64;
        for (u32 index = 0; index < 4; index += 1) section[32 + index] = (u8)(length >> (index * 8));
        section[48] = 1;
    }
    int file = ok ? openat(directory, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && file >= 0 && bq_write_all(file, bytes, table + 128u);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_row_test_write_at(int directory, char const* name, char const* bytes, u32 length)
{
    int file = openat(directory, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool ok = file >= 0 && bq_write_all(file, (u8 const*)bytes, length);
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

typedef struct BqRowTestInput
{
    char const* fixture;
    char const* status;
    char const* error;
    char const* diagnostic;
    bool object;
} BqRowTestInput;

/* One compiler metrics file: the header and one record per input, as the
 * batch authentication requires them. */
BUSTER_GLOBAL_LOCAL u32 bq_row_test_metrics(char* text, u32 capacity, u32 exit_status, char const* action,
    BqRowTestInput const* inputs, u32 count)
{
    u32 statuses[4] = {0};
    char const* first_error = "driver.none";
    for (u32 index = 0; index < count; index += 1)
    {
        u32 status = !strcmp(inputs[index].status, "ok") ? 0 : !strcmp(inputs[index].status, "rejected") ? 1 :
                     !strcmp(inputs[index].status, "failed") ? 2 : 3;
        statuses[status] += 1;
        if (status && status < 3 && !strcmp(first_error, "driver.none")) first_error = inputs[index].error;
    }
    int used = snprintf(text, capacity, "CC_METRICS");
    for (u32 field = 0; used > 0 && (u32)used < capacity && field < TP_METRICS_H_COUNT; field += 1)
    {
        char value[80];
        u64 numbers[TP_METRICS_H_COUNT] = {[TP_METRICS_H_VERSION] = 1, [TP_METRICS_H_INPUTS] = count,
            [TP_METRICS_H_RECORDS] = count, [TP_METRICS_H_OK] = statuses[0], [TP_METRICS_H_REJECTED] = statuses[1],
            [TP_METRICS_H_FAILED] = statuses[2], [TP_METRICS_H_PREBUILT] = statuses[3],
            [TP_METRICS_H_EXIT_STATUS] = exit_status,
            [TP_METRICS_H_COMPILE_JOBS] = 1, [TP_METRICS_H_WORKERS] = 1, [TP_METRICS_H_KEEP_GOING] = 1,
            [TP_METRICS_H_WALL_NS] = 1000u * count + 1u, [TP_METRICS_H_PEAK_RSS] = 65536};
        char const* words[TP_METRICS_H_COUNT] = {[TP_METRICS_H_SCHEMA] = "buster-cc-metrics",
            [TP_METRICS_H_ERROR] = first_error, [TP_METRICS_H_ACTION] = action,
            [TP_METRICS_H_TARGET] = BQ_RETIREMENT_ROW_BATCH_TARGET, [TP_METRICS_H_ALLOCATOR] = "none",
            [TP_METRICS_H_INTERVALS] = "serial"};
        if (words[field]) snprintf(value, sizeof(value), "%s", words[field]);
        else snprintf(value, sizeof(value), "%" PRIu64, (uint64_t)numbers[field]);
        used += snprintf(text + used, capacity - (u32)used, " %s=%s", tp_retirement_metrics_header_fields[field], value);
    }
    if (used > 0 && (u32)used < capacity) used += snprintf(text + used, capacity - (u32)used, "\n");
    for (u32 index = 0; used > 0 && (u32)used < capacity && index < count; index += 1)
    {
        BqRowTestInput const* input = inputs + index;
        used += snprintf(text + used, capacity - (u32)used, "CC_METRICS_INPUT");
        for (u32 field = 0; used > 0 && (u32)used < capacity && field < TP_METRICS_I_COUNT; field += 1)
        {
            char value[1100];
            char const* name = tp_retirement_metrics_input_fields[field];
            u64 number = field == TP_METRICS_I_VERSION ? 1 : field == TP_METRICS_I_INDEX ? index :
                         field == TP_METRICS_I_MEASURED ? 1 : field == TP_METRICS_I_START ? 1000u * index + 1u :
                         field == TP_METRICS_I_END ? 1000u * index + 500u : field == TP_METRICS_I_TOTAL ? 499u :
                         field == TP_METRICS_I_ARENA_PEAK ? 4096u :
                         field == TP_METRICS_I_OBJECT_BYTES ? (input->object ? 128u : 0u) : 0u;
            if (field == TP_METRICS_I_STATUS) snprintf(value, sizeof(value), "%s", input->status);
            else if (field == TP_METRICS_I_ERROR) snprintf(value, sizeof(value), "%s", input->error);
            else if (field == TP_METRICS_I_DIAGNOSTIC_DIGEST) snprintf(value, sizeof(value), "%s", input->diagnostic);
            else if (field == TP_METRICS_I_PATH)
            {
                size_t length = strlen(input->fixture);
                for (size_t byte = 0; byte < length && 2u * byte + 2u < sizeof(value); byte += 1)
                    snprintf(value + 2u * byte, 3, "%02x", (u8)input->fixture[byte]);
            }
            else if (tp_retirement_metrics_kind(name) == 3) snprintf(value, sizeof(value), "-");
            else snprintf(value, sizeof(value), "%" PRIu64, (uint64_t)number);
            used += snprintf(text + used, capacity - (u32)used, " %s=%s", name, value);
        }
        if (used > 0 && (u32)used < capacity) used += snprintf(text + used, capacity - (u32)used, "\n");
    }
    return used > 0 && (u32)used < capacity ? (u32)used : 0;
}

/* A stand-in compiler: compile copies <fixture>.out and <fixture>.metrics
 * out of A's candidate root (after a second's sleep when slow exists
 * there); batch copies each listed fixture's .o to its basename.o, then
 * batch.metrics, and exits with batch.exit. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_compiler(char const* path, u32 side)
{
    char text[1024];
    int length = snprintf(text, sizeof(text), "#!/bin/sh\n# stand-in compiler %u\nsrc=$2\n"
        "if [ \"$1\" = compile ]; then\n  if [ -f \"$src/slow\" ]; then sleep 1; fi\n"
        "  cat \"$src/$3.out\" > \"$4\" && chmod 0700 \"$4\" && cat \"$src/$3.metrics\" > \"$5\" && exit 0\n"
        "  exit 1\nfi\n"
        "if [ \"$1\" = batch ]; then\n  while IFS= read -r line; do\n    f=${line#\\\"}; f=${f%%\\\"}; b=${f##*/}\n"
        "    if [ -f \"$src/$f.o\" ]; then cat \"$src/$f.o\" > \"$b.o\"; fi\n  done < \"${3#@}\"\n"
        "  cat \"$src/batch.metrics\" > \"$4\" && exit \"$(cat \"$src/batch.exit\")\"\n  exit 1\nfi\nexit 2\n", side);
    int file = length > 0 && (size_t)length < sizeof(text) ?
               open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0700) : -1;
    bool ok = file >= 0 && bq_write_all(file, (u8 const*)text, (u32)length) && fchmod(file, 0500) == 0 &&
              fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* Compiles a C program from source into path with the host compiler. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_program(char const* path, char const* source)
{
    char* const argv[] = {"/usr/bin/cc", "-x", "c", "-o", (char*)path, "-", NULL};
    int input[2] = {-1, -1};
    bool ok = pipe(input) == 0;
    pid_t child = ok ? fork() : -1;
    if (child == 0)
    {
        dup2(input[0], STDIN_FILENO);
        close(input[0]);
        close(input[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    if (input[0] >= 0) close(input[0]);
    size_t length = strlen(source);
    ok = ok && child > 0 && write(input[1], source, length) == (ssize_t)length;
    if (input[1] >= 0) close(input[1]);
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    ok = ok && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return ok;
}

/* The native runtime row's honest program: half of its output on stdout,
 * half on stderr, as one writer captures them. */
BUSTER_GLOBAL_LOCAL char const bq_row_test_honest[] =
    "#include <stdio.h>\nint main(void) { fputs(\"fixture-\", stdout); fflush(stdout); "
    "fputs(\"runtime\\n\", stderr); return 0; }\n";
/* A program that tries to read the reference oracle's output beside its
 * step directory and to plant a file there, printing what it got. */
BUSTER_GLOBAL_LOCAL char const bq_row_test_attack[] =
    "#include <stdio.h>\nint main(void) { FILE* planted = fopen(\"../planted\", \"w\"); if (planted) fclose(planted); "
    "FILE* oracle = fopen(\"../reference-oracle/out\", \"r\"); int c = 0; if (!oracle) { fputs(\"denied\\n\", stdout); "
    "return 0; } while ((c = fgetc(oracle)) != EOF) putchar(c); fclose(oracle); return 0; }\n";

/* A program that tries to connect to the listening unix socket beside its
 * step directory (a stand-in for the broker's and the service's control
 * sockets), printing whether it could. */
BUSTER_GLOBAL_LOCAL char const bq_row_test_socket[] =
    "#include <stdio.h>\n#include <string.h>\n#include <sys/socket.h>\n#include <sys/un.h>\n"
    "int main(void) { struct sockaddr_un address = {.sun_family = AF_UNIX}; "
    "strcpy(address.sun_path, \"../control.sock\"); int s = socket(AF_UNIX, SOCK_STREAM, 0); "
    "int c = s >= 0 ? connect(s, (struct sockaddr*)&address, sizeof(address)) : -1; "
    "fputs(c == 0 ? \"connected\\n\" : \"denied\\n\", stdout); return 0; }\n";

typedef struct BqRowTestFixture
{
    BqCheckTestFixture* checks;
    BqRetirementHeldBinaries compilers;
    char base_profile[256], profile[512], cpu_model[SHA256_HEX_CAPACITY];
    char* plan_text;
    u32 cpu;
    /* The listening socket of a SOCKET run, or -1. */
    int listener;
} BqRowTestFixture;

/* A listening unix socket named name in directory, bound through its
 * /proc/self/fd path, or -1. */
BUSTER_GLOBAL_LOCAL int bq_row_test_listen(int directory, char const* name)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int length = snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/%s", directory, name);
    int listener = length > 0 && (size_t)length < sizeof(address.sun_path) ?
                   socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) : -1;
    bool ok = listener >= 0 && bind(listener, (struct sockaddr const*)&address, sizeof(address)) == 0 &&
              listen(listener, 8) == 0;
    if (!ok && listener >= 0)
    {
        close(listener);
        listener = -1;
    }
    return listener;
}

/* bq_row_test_sources variants. */
#define BQ_ROW_TEST_SOURCES_REJECTED 1u
#define BQ_ROW_TEST_SOURCES_PREBUILT 2u
#define BQ_ROW_TEST_SOURCES_ATTACK 4u
#define BQ_ROW_TEST_SOURCES_WRONG_MACHINE 8u
#define BQ_ROW_TEST_SOURCES_WRONG_KIND 16u
#define BQ_ROW_TEST_SOURCES_SOCKET 32u

/* The candidate root's prepared outputs for the stand-in compilers: the
 * members' x86-64 objects, per-row artifacts of each row's target and kind
 * (row 4, the native runtime row, gets a real executable), single-input
 * metrics with the stage's action, and the batch's metrics and exit status.
 * REJECTED records row 2's input as rejected; PREBUILT adds a prebuilt input
 * to row 4's link; ATTACK builds row 4 from bq_row_test_attack; WRONG_MACHINE
 * gives row 2 (an AArch64 row) an x86-64 object and WRONG_KIND row 5 (a
 * self-host executable) a relocatable object. SOCKET builds row 4 from
 * bq_row_test_socket, and bq_row_test_run listens beside the steps. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_sources(BqRowTestFixture* fixture, char const* candidate, u32 variant)
{
    BqRetirementProjection const* projection = &fixture->checks->projection;
    u32 native = projection->prepared.native_target;
    int root = fixture->checks->sources[1];
    bool ok = mkdirat(root, "tests", 0700) == 0 || errno == EEXIST;
    int tests = ok ? openat(root, "tests", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && tests >= 0;
    char* metrics = malloc(1u << 20);
    ok = ok && metrics;
    char empty[SHA256_HEX_CAPACITY], diagnosed[SHA256_HEX_CAPACITY];
    bq_digest("", 0, (char8*)empty);
    bq_digest("control", 7, (char8*)diagnosed);
    BqRowTestInput batch[BQ_CHECK_TEST_ROWS];
    char fixtures[BQ_CHECK_TEST_ROWS][32];
    u32 batch_count = 0, exit_status = 0;
    /* Members first, then controls, as the plan lists them. */
    for (u32 pass = 0; ok && pass < 2; pass += 1)
        for (u32 index = 0; ok && index < projection->prepared.rows; index += 1)
        {
            BqRetirementTrustedRow const* row = projection->rows + index;
            bool member = bq_retirement_row_timed_object(row, native);
            bool control = bq_row_test_control(row, native);
            if (!(pass ? control : member)) continue;
            snprintf(fixtures[batch_count], sizeof(fixtures[batch_count]), "tests/row-%u.c", index);
            batch[batch_count] = (BqRowTestInput){fixtures[batch_count], member ? "ok" : "rejected",
                member ? "driver.none" : "driver.rejected", member ? empty : diagnosed, member};
            exit_status = exit_status || !member;
            if (member)
            {
                char name[32], code[32];
                snprintf(name, sizeof(name), "row-%u.c.o", index);
                snprintf(code, sizeof(code), "member-%u-code", index);
                ok = bq_row_test_elf(tests, name, code, 62, 1);
            }
            batch_count += 1;
        }
    u32 length = ok ? bq_row_test_metrics(metrics, 1u << 20, exit_status, "object", batch, batch_count) : 0;
    char exit_text[8];
    int exit_length = snprintf(exit_text, sizeof(exit_text), "%u\n", exit_status);
    ok = ok && length && bq_row_test_write_at(root, "batch.metrics", metrics, length) &&
         bq_row_test_write_at(root, "batch.exit", exit_text, (u32)exit_length);
    /* Per-row compiles: every other compiler-eligible row, with an artifact
     * of the row's own target and kind. */
    for (u32 index = 0; ok && index < projection->prepared.rows; index += 1)
    {
        BqRetirementTrustedRow const* row = projection->rows + index;
        if (!row->compiler_eligible || bq_retirement_row_timed_object(row, native)) continue;
        char name[32], metrics_name[40], code[32], fixture_name[32], output[256];
        snprintf(name, sizeof(name), "row-%u.c.out", index);
        snprintf(metrics_name, sizeof(metrics_name), "row-%u.c.metrics", index);
        snprintf(code, sizeof(code), "single-%u-code", index);
        snprintf(fixture_name, sizeof(fixture_name), "tests/row-%u.c", index);
        snprintf(output, sizeof(output), "%s/tests/%s", candidate, name);
        bool object = row->stage == BQ_RETIREMENT_STAGE_OBJECT;
        if (bq_retirement_row_native_runtime(row, native))
        {
            unlinkat(tests, name, 0);
            ok = bq_row_test_program(output, (variant & BQ_ROW_TEST_SOURCES_ATTACK) ? bq_row_test_attack :
                                             (variant & BQ_ROW_TEST_SOURCES_SOCKET) ? bq_row_test_socket :
                                                                                      bq_row_test_honest);
        }
        else
        {
            bool machine = (variant & BQ_ROW_TEST_SOURCES_WRONG_MACHINE) && index == 2;
            bool kind = (variant & BQ_ROW_TEST_SOURCES_WRONG_KIND) && index == 5;
            ok = bq_row_test_elf(tests, name, code, (row->target <= 6) != machine ? 183u : 62u,
                                 object || kind ? 1u : 2u);
        }
        bool rejected = (variant & BQ_ROW_TEST_SOURCES_REJECTED) && index == 2;
        BqRowTestInput single[2] = {{fixture_name, rejected ? "rejected" : "ok",
                                     rejected ? "driver.rejected" : "driver.none", empty, !rejected},
                                    {"lib/prebuilt.o", "prebuilt", "driver.none", empty, false}};
        u32 inputs = (variant & BQ_ROW_TEST_SOURCES_PREBUILT) && index == 4 ? 2u : 1u;
        length = ok ? bq_row_test_metrics(metrics, 1u << 20, 0, object ? "object" : "link", single,
                                          inputs) : 0;
        ok = ok && length && bq_row_test_write_at(tests, metrics_name, metrics, length);
    }
    free(metrics);
    if (tests >= 0) close(tests);
    return ok;
}

/* The two held stand-in compilers. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_compilers(BqRowTestFixture* fixture)
{
    BqRetirementHeldBinaries* held = &fixture->compilers;
    *held = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
    bool ok = true;
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        char path[160];
        int length = snprintf(path, sizeof(path), "%s/row-compiler-%u", fixture->checks->workspaces, side);
        ok = length > 0 && (size_t)length < sizeof(path) && bq_row_test_compiler(path, side);
        held->descriptors[side] = ok ? bq_retirement_check_promote(open(path, O_RDONLY | O_CLOEXEC)) : -1;
        ok = ok && held->descriptors[side] >= 3 &&
             bq_retirement_oracle_file_hash(held->descriptors[side], BQ_RETIREMENT_CHECK_TOOL_BYTES_CAP, true,
                                            held->verified.binary_sha256[side]);
    }
    held->owned = ok;
    return ok;
}

/* Imports the currently installed plan with the fixture's profile. */
BUSTER_GLOBAL_LOCAL BqError bq_row_test_import(BqRowTestFixture* fixture, BqRetirementProjection const* projection,
    BqRetirementRowPlan* plan)
{
    BqError result = bq_retirement_row_plan_import_profile(fixture->checks->installed_fd,
        string_from_pointer(fixture->profile), &fixture->checks->job, projection, plan);
    return result;
}

/* Installs the plan variant flags describes and pins it in the profile. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_pin(BqRowTestFixture* fixture, BqRetirementProjection const* projection, u32 flags)
{
    char pin[128];
    u32 length = bq_row_test_plan(fixture->plan_text, BQ_ROW_TEST_PLAN_CAP, projection, fixture->cpu_model,
                                  fixture->cpu, flags);
    bool ok = length && bq_row_test_install(fixture->checks->recipes, fixture->plan_text, length, pin);
    int written = ok ? snprintf(fixture->profile, sizeof(fixture->profile), "%s%s", fixture->base_profile, pin) : -1;
    return ok && written > 0 && (size_t)written < sizeof(fixture->profile);
}

/* The correctness half of the issuer on a joined observation. */
BUSTER_GLOBAL_LOCAL BqError bq_row_test_admit(BqRowTestFixture* fixture, BqRetirementRowPlan const* plan,
    BqRetirementRowObserved const* observed)
{
    BqRetirementRequiredChecks checks = {.hosted = -1};
    BqRetirementCheckResult results[BQ_CHECK_TEST_CHECKS] = {0};
    BqRetirementRowJoined joined = {0};
    BqRetirementUnitGate gate = {0};
    BqError result = bq_check_test_import(fixture->checks, &fixture->checks->job, &fixture->checks->projection,
                                          &checks);
    if (result == BQ_OK) bq_row_test_passing(&checks, results);
    if (result == BQ_OK) result = bq_retirement_row_evidence_join(plan, &fixture->checks->projection, observed, &joined);
    if (result == BQ_OK)
        result = bq_retirement_unit_gate_admit(&fixture->checks->projection, &checks, results, &joined.evidence, &gate);
    BQ_PREP_CHECK(result != BQ_OK || (gate.correctness.batch_authority == 1 &&
                                      bq_retirement_correctness_ready(&gate.correctness) &&
                                      !strcmp(gate.plan_sha256, plan->authority_sha256) &&
                                      !strcmp(gate.correctness.prepared.aa_second_commands_sha256,
                                              plan->aa_second_commands_sha256)));
    bq_retirement_unit_gate_release(&gate);
    bq_retirement_row_joined_release(&joined);
    bq_retirement_required_checks_release(&checks);
    return result;
}

/* A formatted observation with one line edited: the first occurrence of
 * needle becomes replacement; then parsed for plan. */
BUSTER_GLOBAL_LOCAL BqError bq_row_test_reparse(BqRetirementRowObserved const* observed, BqRetirementRowPlan const* plan,
    char const* needle, char const* replacement, BqRetirementRowObserved* parsed)
{
    char* text = NULL;
    u32 length = 0;
    bool ok = bq_retirement_row_observed_format(observed, &text, &length);
    char* found = ok ? strstr(text, needle) : NULL;
    size_t needle_length = strlen(needle), replacement_length = strlen(replacement);
    char* edited = found ? malloc((size_t)length - needle_length + replacement_length + 1u) : NULL;
    u32 edited_length = 0;
    if (edited)
    {
        size_t head = (size_t)(found - text);
        memcpy(edited, text, head);
        memcpy(edited + head, replacement, replacement_length);
        memcpy(edited + head + replacement_length, found + needle_length, (size_t)length - head - needle_length);
        edited_length = (u32)((size_t)length - needle_length + replacement_length);
    }
    BqError result = edited ? bq_retirement_row_observed_parse((u8 const*)edited, edited_length, plan, parsed) :
                     BQ_BAD_REQUEST;
    free(edited);
    free(text);
    return result;
}

/* The importer's pin, binding and A1 coverage rules. */
BUSTER_GLOBAL_LOCAL void bq_row_test_importer(BqRowTestFixture* fixture)
{
    BqRetirementProjection* projection = &fixture->checks->projection;
    BqRetirementRowPlan plan = {0};
    /* The blocked profile, and a profile without the key, have no pin. */
    BQ_PREP_CHECK(bq_retirement_row_plan_import(fixture->checks->installed_fd, &fixture->checks->job, projection,
                                                &plan) == BQ_RECIPE_MISMATCH && !plan.owned);
    BQ_PREP_CHECK(bq_retirement_row_plan_import_profile(fixture->checks->installed_fd,
                  string_from_pointer(fixture->base_profile), &fixture->checks->job, projection, &plan) ==
                  BQ_RECIPE_MISMATCH && !plan.owned);
    /* The pinned plan for this projection, derived. */
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, 0) && bq_row_test_import(fixture, projection, &plan) == BQ_OK &&
                  plan.owned && plan.row_count == BQ_CHECK_TEST_ROWS && plan.group_count == 1 &&
                  plan.input_count == 3 && plan.groups[0].exit_status == 1);
    for (u32 row = 0; plan.owned && row < plan.row_count; row += 1)
    {
        BqRetirementTrustedRow const* completed = plan.completed + row;
        bool batch = plan.rows[row].compile == BQ_RETIREMENT_ROW_PLAN_BATCH;
        BQ_PREP_CHECK(!memcmp(completed->identity_sha256, projection->rows[row].identity_sha256, SHA256_HEX_CAPACITY) &&
                      (batch ? !strcmp(completed->batch_key_sha256, plan.groups[0].key_sha256) &&
                               !strcmp(completed->compiler_command_sha256[1], plan.groups[0].command_sha256[1]) :
                               !completed->batch_key_sha256[0]) &&
                      completed->batch_control == (row == 3) &&
                      (!projection->rows[row].compiler_eligible && !batch ? !completed->compiler_command_sha256[0][0] :
                       bq_retirement_unit_hex(completed->compiler_command_sha256[0]) &&
                       strcmp(completed->compiler_command_sha256[0], completed->compiler_command_sha256[1])) &&
                      (row == 4) == (completed->runtime_command_sha256[0][0] != 0));
    }
    BQ_PREP_CHECK(plan.owned && bq_retirement_unit_hex(plan.aa_second_commands_sha256) &&
                  strcmp(plan.groups[0].second_sha256, plan.groups[0].command_sha256[0]) &&
                  !strcmp(plan.groups[0].list_leaf + strlen(plan.groups[0].list_leaf) - 4u, ".rsp"));
    BqRetirementRowPlan again = {0};
    BQ_PREP_CHECK(bq_row_test_import(fixture, projection, &plan) == BQ_BAD_REQUEST && plan.owned);
    BQ_PREP_CHECK(bq_retirement_row_plan_release(&plan) && !plan.owned);
    /* A forged pin; another attempt's projection. */
    char flipped[512];
    memcpy(flipped, fixture->profile, sizeof(flipped));
    bq_prep_test_flip_pin(flipped, "row-plan-sha256=");
    BQ_PREP_CHECK(bq_retirement_row_plan_import_profile(fixture->checks->installed_fd, string_from_pointer(flipped),
                  &fixture->checks->job, projection, &plan) == BQ_RECIPE_MISMATCH && !plan.owned);
    BqJob other = fixture->checks->job;
    other.token += 1;
    BQ_PREP_CHECK(bq_retirement_row_plan_import_profile(fixture->checks->installed_fd,
                  string_from_pointer(fixture->profile), &other, projection, &again) == BQ_BAD_REQUEST);
    BqRetirementProjection moved = *projection;
    moved.prepared.census_sha256[0] ^= 1;
    BQ_PREP_CHECK(bq_row_test_import(fixture, &moved, &plan) == BQ_RECIPE_MISMATCH && !plan.owned);
    /* Plan variants, each installed and pinned. */
    static u32 const refused[] = {BQ_ROW_TEST_DROP_ROW, BQ_ROW_TEST_ADD_ROW, BQ_ROW_TEST_TIMED_SINGLE,
        BQ_ROW_TEST_FOREIGN, BQ_ROW_TEST_WRONG_FIXTURE, BQ_ROW_TEST_RUNTIME_OBJECT, BQ_ROW_TEST_PATH_EXECUTABLE,
        BQ_ROW_TEST_UNKNOWN_TOKEN};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refused); index += 1)
    {
        BqError result = bq_row_test_pin(fixture, projection, refused[index]) ?
                         bq_row_test_import(fixture, projection, &plan) : BQ_IO;
        if (result != BQ_RECIPE_MISMATCH)
            fprintf(stderr, "RETIREMENT_PREP row plan variant %u: %d\n", refused[index], (int)result);
        BQ_PREP_CHECK(result == BQ_RECIPE_MISMATCH && !plan.owned);
    }
    /* Two groups under one template share a batch key: the plan imports,
     * but the correctness gate refuses to freeze them. */
    BqRetirementRowObserved observed = {0};
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, BQ_ROW_TEST_SPLIT) &&
                  bq_row_test_import(fixture, projection, &plan) == BQ_OK && plan.group_count == 2 &&
                  !strcmp(plan.groups[0].key_sha256, plan.groups[1].key_sha256) &&
                  bq_row_test_observe(&plan, &observed) &&
                  bq_row_test_admit(fixture, &plan, &observed) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&observed);
    bq_retirement_row_plan_release(&plan);
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, 0));
}

/* The join and the gate on a synthetic honest observation, the canonical
 * evidence and every refusal that is not the producer's. */
BUSTER_GLOBAL_LOCAL void bq_row_test_join(BqRowTestFixture* fixture)
{
    BqRetirementProjection* projection = &fixture->checks->projection;
    BqRetirementRowPlan plan = {0}, foreign = {0};
    BqRetirementRowObserved observed = {0}, parsed = {0}, other = {0};
    BQ_PREP_CHECK(bq_row_test_import(fixture, projection, &plan) == BQ_OK && bq_row_test_observe(&plan, &observed));
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &observed) == BQ_OK);
    /* Canonical round trip. */
    char* text = NULL;
    u32 length = 0;
    BQ_PREP_CHECK(bq_retirement_row_observed_format(&observed, &text, &length) &&
                  bq_retirement_row_observed_parse((u8 const*)text, length, &plan, &parsed) == BQ_OK &&
                  bq_row_test_admit(fixture, &plan, &parsed) == BQ_OK);
    bq_retirement_row_observed_release(&parsed);
    /* A missing or added row line, a non-canonical number or another
     * attempt do not parse. */
    char fact_zero[64];
    snprintf(fact_zero, sizeof(fact_zero), "rows=%u\n", BQ_CHECK_TEST_ROWS);
    char fewer[64];
    snprintf(fewer, sizeof(fewer), "rows=%u\n", BQ_CHECK_TEST_ROWS - 1u);
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, fact_zero, fewer, &parsed) == BQ_CORRUPT && !parsed.owned);
    char* side = text ? strstr(text, "\nfact=1 ") : NULL;
    char* next = side ? strstr(side + 1, "\nfact=2 ") : NULL;
    char removed[1400] = {0};
    if (side && next && (size_t)(next - side) < sizeof(removed)) memcpy(removed, side + 1, (size_t)(next - side));
    BQ_PREP_CHECK(removed[0] && bq_row_test_reparse(&observed, &plan, removed, "", &parsed) == BQ_CORRUPT);
    char added[3000];
    snprintf(added, sizeof(added), "%s%s", removed, removed);
    BQ_PREP_CHECK(removed[0] && bq_row_test_reparse(&observed, &plan, removed, added, &parsed) == BQ_CORRUPT);
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, " 16 1 0 ", " 016 1 0 ", &parsed) == BQ_CORRUPT);
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, " 0 -1\n", " 0 -01\n", &parsed) == BQ_CORRUPT);
    char job_line[64], other_job[64];
    snprintf(job_line, sizeof(job_line), "job=%" PRIu64 "\n", (uint64_t)fixture->checks->job.id);
    snprintf(other_job, sizeof(other_job), "job=%" PRIu64 "\n", (uint64_t)fixture->checks->job.id + 1u);
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, job_line, other_job, &parsed) == BQ_CORRUPT);
    /* A modified evidence file that still parses: one row's command (the
     * gate's row rule) or a member's object (its batch rule). */
    char command[80], changed_command[80];
    snprintf(command, sizeof(command), "side=%s", plan.completed[5].compiler_command_sha256[0]);
    memcpy(changed_command, command, sizeof(command));
    changed_command[5] = changed_command[5] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, command, changed_command, &parsed) == BQ_OK &&
                  bq_row_test_admit(fixture, &plan, &parsed) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&parsed);
    char object[80], changed_object[80];
    snprintf(object, sizeof(object), " %s ", observed.object_sha256[0][1]);
    memcpy(changed_object, object, sizeof(object));
    changed_object[1] = changed_object[1] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_row_test_reparse(&observed, &plan, object, changed_object, &parsed) == BQ_OK &&
                  bq_row_test_admit(fixture, &plan, &parsed) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&parsed);
    free(text);
    /* CPU provenance: another model, more than one CPU or another CPU. */
    BqRetirementRowJoined joined = {0};
    BqRetirementRowObserved changed = observed;
    changed.cpu_model_sha256[0] = changed.cpu_model_sha256[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_retirement_row_evidence_join(&plan, projection, &changed, &joined) == BQ_CONFIGURATION_MISMATCH &&
                  !joined.owned);
    changed = observed;
    changed.cpus = 2;
    BQ_PREP_CHECK(bq_retirement_row_evidence_join(&plan, projection, &changed, &joined) == BQ_CONFIGURATION_MISMATCH);
    changed = observed;
    bq_retirement_row_cpu_mask(plan.cpu + 1u, changed.cpu_mask);
    BQ_PREP_CHECK(bq_retirement_row_evidence_join(&plan, projection, &changed, &joined) == BQ_CONFIGURATION_MISMATCH);
    /* Steps that ran below the sandbox's lowest Landlock ABI. */
    changed = observed;
    changed.sandbox_abi = BQ_ROW_TEST_OLD_ABI;
    BQ_PREP_CHECK(bq_retirement_row_evidence_join(&plan, projection, &changed, &joined) == BQ_CONFIGURATION_MISMATCH);
    /* A command, a control's status or a batch key that does not match. */
    changed = observed;
    BqRetirementRowFact facts[BQ_CHECK_TEST_ROWS];
    memcpy(facts, observed.facts, sizeof(facts));
    changed.facts = facts;
    facts[4].side[1].runtime_command_sha256[0] = facts[4].side[1].runtime_command_sha256[0] == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &changed) == BQ_RECIPE_MISMATCH);
    memcpy(facts, observed.facts, sizeof(facts));
    facts[3].side[0].compiler_exit = 0;
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &changed) == BQ_RECIPE_MISMATCH);
    memcpy(facts, observed.facts, sizeof(facts));
    char key = plan.completed[1].batch_key_sha256[0];
    plan.completed[1].batch_key_sha256[0] = key == '0' ? '1' : '0';
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &observed) == BQ_RECIPE_MISMATCH);
    plan.completed[1].batch_key_sha256[0] = key;
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &observed) == BQ_OK);
    /* An observation of another plan (the same projection, a changed
     * template) or another attempt. */
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, BQ_ROW_TEST_CONTROL_FAILED) &&
                  bq_row_test_import(fixture, projection, &foreign) == BQ_OK &&
                  bq_row_test_observe(&foreign, &other) &&
                  bq_retirement_row_evidence_join(&plan, projection, &other, &joined) == BQ_RECIPE_MISMATCH &&
                  bq_row_test_admit(fixture, &foreign, &other) == BQ_OK);
    changed = observed;
    changed.attempt_token += 1;
    BQ_PREP_CHECK(bq_retirement_row_evidence_join(&plan, projection, &changed, &joined) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&other);
    bq_retirement_row_plan_release(&foreign);
    bq_retirement_row_observed_release(&observed);
    bq_retirement_row_plan_release(&plan);
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, 0));
}

/* Prepares the candidate root (sources variant), pins and imports the plan
 * variant and runs the producer into a new work directory, whose descriptor
 * work receives. */
BUSTER_GLOBAL_LOCAL BqError bq_row_test_run(BqRowTestFixture* fixture, u32 plan_flags, u32 sources,
    BqRetirementRowPlan* plan, BqRetirementRowObserved* observed, int* work)
{
    BqCheckTestFixture* checks = fixture->checks;
    char candidate[160];
    snprintf(candidate, sizeof(candidate), "%s/source-candidate-1", checks->workspaces);
    bq_retirement_row_plan_release(plan);
    *work = -1;
    bool ready = bq_row_test_sources(fixture, candidate, sources) && bq_row_test_pin(fixture, &checks->projection,
                                                                                    plan_flags) &&
                 bq_row_test_import(fixture, &checks->projection, plan) == BQ_OK &&
                 bq_check_test_directory(checks, "row-work", work);
    /* The reference oracle's output beside every step directory, as
     * retirement-work/reference-oracle/ sits beside the unit's steps. */
    if (ready && (sources & BQ_ROW_TEST_SOURCES_ATTACK))
    {
        int oracle = mkdirat(*work, "reference-oracle", 0700) == 0 ?
                     openat(*work, "reference-oracle", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ready = oracle >= 0 && bq_row_test_write_at(oracle, "out", BQ_ROW_TEST_RUNTIME_OUTPUT,
                                                     (u32)strlen(BQ_ROW_TEST_RUNTIME_OUTPUT));
        if (oracle >= 0) close(oracle);
    }
    /* A listening unix socket beside every step directory. */
    if (ready && (sources & BQ_ROW_TEST_SOURCES_SOCKET))
    {
        fixture->listener = bq_row_test_listen(*work, "control.sock");
        ready = fixture->listener >= 0;
    }
    BqRetirementRowRun run = {plan, &fixture->compilers, {checks->sources[0], checks->sources[1]}, *work,
                              checks->cancel[0], bq_retirement_build_clock_ns() + 120ull * 1000000000ull};
    BqError result = ready ? bq_retirement_row_produce(&run, observed) : BQ_IO;
    return result;
}

/* Changes the held compiler at path after delay_ms and restores its bytes,
 * from a process that is not this process's child. */
BUSTER_GLOBAL_LOCAL bool bq_row_test_tamper_later(char const* path, u32 delay_ms)
{
    pid_t middle = fork();
    if (middle == 0)
    {
        pid_t helper = fork();
        if (helper == 0)
        {
            struct timespec pause = {delay_ms / 1000u, (long)(delay_ms % 1000u) * 1000000L};
            nanosleep(&pause, NULL);
            struct stat info = {0};
            int file = stat(path, &info) == 0 && chmod(path, 0700) == 0 ? open(path, O_WRONLY | O_APPEND) : -1;
            bool changed = file >= 0 && write(file, "#", 1) == 1 && ftruncate(file, info.st_size) == 0;
            if (file >= 0) close(file);
            _exit(changed && chmod(path, 0500) == 0 ? 0 : 1);
        }
        _exit(helper > 0 ? 0 : 1);
    }
    int status = 0;
    pid_t waited = -1;
    do { if (middle > 0) waited = waitpid(middle, &status, 0); }
    while (waited < 0 && errno == EINTR);
    bool ok = waited == middle && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return ok;
}

/* The producer with the stand-in compilers on the plan's CPU, each step in
 * its sandbox. */
BUSTER_GLOBAL_LOCAL void bq_row_test_producer_native(BqRowTestFixture* fixture)
{
    BqCheckTestFixture* checks = fixture->checks;
    BqRetirementProjection* projection = &checks->projection;
    /* Row 4's oracle is the stand-in program's combined stdout and stderr
     * (the population seal does not cover oracle digests). */
    bq_digest(BQ_ROW_TEST_RUNTIME_OUTPUT, (u32)strlen(BQ_ROW_TEST_RUNTIME_OUTPUT),
              (char8*)checks->rows[4].independent_oracle_sha256);
    BqRetirementRowPlan plan = {0};
    BqRetirementRowObserved observed = {0};
    int work = -1;
    BQ_PREP_CHECK(bq_row_test_compilers(fixture));
    BqError produced = bq_row_test_run(fixture, 0, 0, &plan, &observed, &work);
    if (produced != BQ_OK) fprintf(stderr, "RETIREMENT_PREP row producer returned %d\n", (int)produced);
    BQ_PREP_CHECK(produced == BQ_OK && observed.owned && observed.cpus == 1 && bq_check_test_no_children() &&
                  !strcmp(observed.cpu_model_sha256, fixture->cpu_model) &&
                  observed.sandbox_abi == bq_retirement_sandbox_abi() &&
                  observed.sandbox_abi >= BQ_RETIREMENT_SANDBOX_MIN_ABI);
    for (u32 row = 0; observed.owned && row < observed.row_count; row += 1)
    {
        BqRetirementRowFact const* fact = observed.facts + row;
        bool compiled = projection->rows[row].compiler_eligible != 0;
        BQ_PREP_CHECK(fact->row == row && (!compiled || (fact->side[0].semantic_pass == 1 &&
                      fact->side[1].semantic_pass == 1 && fact->side[0].compiler_exit == 0 &&
                      fact->side[0].code_bytes > 0 && !fact->side[0].fallback_count &&
                      !strcmp(fact->side[0].compiler_command_sha256, plan.completed[row].compiler_command_sha256[0]) &&
                      !strcmp(fact->side[1].compiler_command_sha256, plan.completed[row].compiler_command_sha256[1]))));
    }
    char empty[SHA256_HEX_CAPACITY];
    bq_digest("", 0, (char8*)empty);
    BQ_PREP_CHECK(observed.owned && observed.facts[3].side[0].compiler_exit == 1 &&
                  !observed.facts[3].side[0].artifact_sha256[0] &&
                  !strcmp(observed.facts[4].side[1].runtime_output_sha256, checks->rows[4].independent_oracle_sha256) &&
                  observed.facts[4].side[1].runtime_exit == 0 &&
                  !strcmp(observed.facts[2].side[0].diagnostic_sha256, empty) &&
                  !strcmp(observed.object_sha256[0][0], observed.facts[0].side[0].artifact_sha256));
    /* The observation joins and the correctness gate admits it. */
    BQ_PREP_CHECK(bq_row_test_admit(fixture, &plan, &observed) == BQ_OK);
    bq_retirement_row_observed_release(&observed);
    /* The same steps again into the same work directory are refused. */
    BqRetirementRowRun run = {&plan, &fixture->compilers, {checks->sources[0], checks->sources[1]}, work,
                              checks->cancel[0], bq_retirement_build_clock_ns() + 120ull * 1000000000ull};
    BQ_PREP_CHECK(bq_retirement_row_produce(&run, &observed) == BQ_WORKSPACE_MISMATCH && !observed.owned);
    if (work >= 0) close(work);

    /* (H1) The generated program tries to read the reference oracle's
     * output beside its step directory and to plant a file there. Its sandbox
     * denies both, so it prints "denied", and the gate refuses the row. */
    BqError attacked = bq_row_test_run(fixture, 0, BQ_ROW_TEST_SOURCES_ATTACK, &plan, &observed, &work);
    char denied[SHA256_HEX_CAPACITY];
    bq_digest("denied\n", 7, (char8*)denied);
    struct stat info = {0};
    BQ_PREP_CHECK(attacked == BQ_OK && observed.owned &&
                  !strcmp(observed.facts[4].side[0].runtime_output_sha256, denied) &&
                  fstatat(work, "planted", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT &&
                  bq_row_test_admit(fixture, &plan, &observed) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&observed);
    if (work >= 0) close(work);

    /* The generated program tries to connect to a listening unix socket
     * beside its step directory, which Landlock does not govern. The seccomp
     * filter denies it, so it prints "denied", and the gate refuses the
     * row. */
    attacked = bq_row_test_run(fixture, 0, BQ_ROW_TEST_SOURCES_SOCKET, &plan, &observed, &work);
    BQ_PREP_CHECK(attacked == BQ_OK && observed.owned &&
                  !strcmp(observed.facts[4].side[0].runtime_output_sha256, denied) &&
                  bq_row_test_admit(fixture, &plan, &observed) == BQ_RECIPE_MISMATCH);
    bq_retirement_row_observed_release(&observed);
    if (fixture->listener >= 0) close(fixture->listener);
    fixture->listener = -1;
    BQ_PREP_CHECK(work >= 0 && unlinkat(work, "control.sock", 0) == 0);
    if (work >= 0) close(work);
    /* A kernel below the sandbox's lowest Landlock ABI: refused before any
     * step. */
    bq_retirement_sandbox_abi_ceiling = BQ_ROW_TEST_OLD_ABI;
    BQ_PREP_CHECK(bq_row_test_run(fixture, 0, 0, &plan, &observed, &work) == BQ_CONFIGURATION_MISMATCH &&
                  !observed.owned && bq_check_test_no_children());
    bq_retirement_sandbox_abi_ceiling = UINT32_MAX;
    if (work >= 0) close(work);

    /* (H2) An artifact of another machine (row 2 is an AArch64 row) or of
     * another kind (row 5 builds an executable) is recorded but not
     * accepted, and the gate refuses it. */
    static u32 const wrong[] = {BQ_ROW_TEST_SOURCES_WRONG_MACHINE, BQ_ROW_TEST_SOURCES_WRONG_KIND,
                                BQ_ROW_TEST_SOURCES_REJECTED};
    static u32 const wrong_row[] = {2, 5, 2};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(wrong); index += 1)
    {
        BqError result = bq_row_test_run(fixture, 0, wrong[index], &plan, &observed, &work);
        BqRetirementObservedSide const* side = observed.owned ? observed.facts[wrong_row[index]].side : NULL;
        BQ_PREP_CHECK(result == BQ_OK && side && side->semantic_pass == 0 && side->artifact_sha256[0] &&
                      bq_row_test_admit(fixture, &plan, &observed) == BQ_RECIPE_MISMATCH);
        bq_retirement_row_observed_release(&observed);
        if (work >= 0) close(work);
    }
    /* (M5) A link that also records a prebuilt input is accepted. */
    BQ_PREP_CHECK(bq_row_test_run(fixture, 0, BQ_ROW_TEST_SOURCES_PREBUILT, &plan, &observed, &work) == BQ_OK &&
                  observed.facts[4].side[0].semantic_pass == 1 &&
                  bq_row_test_admit(fixture, &plan, &observed) == BQ_OK);
    bq_retirement_row_observed_release(&observed);
    if (work >= 0) close(work);

    /* A control whose status is not the pinned one fails the batch's
     * metrics authentication. */
    BQ_PREP_CHECK(bq_row_test_run(fixture, BQ_ROW_TEST_CONTROL_FAILED, 0, &plan, &observed, &work) ==
                  BQ_RECIPE_MISMATCH && !observed.owned && bq_check_test_no_children());
    if (work >= 0) close(work);
    /* A CPU the unit may not run on. */
    char candidate[160];
    snprintf(candidate, sizeof(candidate), "%s/source-candidate-1", checks->workspaces);
    BQ_PREP_CHECK(bq_row_test_sources(fixture, candidate, 0));
    bq_retirement_row_plan_release(&plan);
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, 0) && bq_row_test_import(fixture, projection, &plan) == BQ_OK &&
                  bq_check_test_directory(checks, "row-work", &work));
    plan.cpu = CPU_SETSIZE - 1u;
    run = (BqRetirementRowRun){&plan, &fixture->compilers, {checks->sources[0], checks->sources[1]}, work,
                               checks->cancel[0], bq_retirement_build_clock_ns() + 120ull * 1000000000ull};
    BQ_PREP_CHECK(bq_retirement_row_produce(&run, &observed) == BQ_CONFIGURATION_MISMATCH && !observed.owned);
    plan.cpu = fixture->cpu;
    if (work >= 0) close(work);
    /* (M6) A held compiler whose bytes changed before the run fails the
     * run's rehash; one changed and restored during a step (bytes equal
     * again) fails that step's file identity. */
    char path[160];
    snprintf(path, sizeof(path), "%s/row-compiler-1", checks->workspaces);
    int appender = stat(path, &info) == 0 && chmod(path, 0700) == 0 ? open(path, O_WRONLY | O_APPEND | O_CLOEXEC) : -1;
    BQ_PREP_CHECK(appender >= 0 && write(appender, "\n", 1) == 1 && close(appender) == 0 &&
                  bq_check_test_directory(checks, "row-work", &work));
    run.work = work;
    BQ_PREP_CHECK(bq_retirement_row_produce(&run, &observed) == BQ_SOURCE_MISMATCH && !observed.owned);
    BQ_PREP_CHECK(truncate(path, info.st_size) == 0 && chmod(path, 0500) == 0);
    if (work >= 0) close(work);
    BQ_PREP_CHECK(bq_row_test_write_at(checks->sources[1], "slow", "", 0) &&
                  bq_check_test_directory(checks, "row-work", &work));
    run.work = work;
    BQ_PREP_CHECK(bq_row_test_tamper_later(path, 500));
    BQ_PREP_CHECK(bq_retirement_row_produce(&run, &observed) == BQ_SOURCE_MISMATCH && !observed.owned);
    struct timespec settle = {1, 0};
    nanosleep(&settle, NULL);
    struct stat after = {0};
    BQ_PREP_CHECK(unlinkat(checks->sources[1], "slow", 0) == 0 && stat(path, &after) == 0 &&
                  after.st_size == info.st_size && (after.st_mode & 07777) == 0500);
    if (work >= 0) close(work);
    bq_retirement_row_plan_release(&plan);
    BQ_PREP_CHECK(bq_row_test_pin(fixture, projection, 0) && bq_check_test_no_children());
}


/* One A/A or A/B stage of the campaign for the six fixture rows: timed rows
 * 0 and 1 (the object group), 4 (a link singleton with native runtime) and
 * 5 (a self-host singleton). */
typedef struct BqRowTestStage
{
    TpRetirementExecution execution;
    TpRetirementTranscript transcript;
    TpRetirementSamples samples;
    TpRetirementMetricsShards metrics;
    TpRetirementSampleRow rows[4];
    TpRetirementSampleGroup groups[3];
    unsigned workspace[10], members[4];
    FILE* streams[3];
} BqRowTestStage;

BUSTER_GLOBAL_LOCAL bool bq_row_test_stage_open(BqRowTestStage* stage, char const* tag)
{
    static unsigned const ids[] = {0, 1, 4, 5}, metrics[] = {0, 0, TP_RETIREMENT_SAMPLE_RUNTIME, 0};
    static unsigned const kinds[] = {TP_RETIREMENT_GROUP_OBJECT, TP_RETIREMENT_GROUP_SINGLETON,
                                     TP_RETIREMENT_GROUP_SINGLETON};
    static unsigned const offsets[] = {0, 2, 3, 4}, members[] = {0, 1, 2, 3}, runtime[] = {4};
    *stage = (BqRowTestStage){0};
    for (u32 index = 0; index < 3; index += 1) stage->streams[index] = tmpfile();
    TpRetirementLayout layout = {4, 3, ids, metrics, kinds, offsets, members};
    bool ok = stage->streams[0] && stage->streams[1] && stage->streams[2] &&
              tp_retirement_execution_init(&stage->execution, 1, 3, runtime, 1, BQ_CHECK_TEST_ROWS, 60, stage->workspace,
                                           10) &&
              tp_retirement_transcript_init(&stage->transcript, &stage->execution, "job-91", 5, "boot-fixture", 0, 1000) &&
              tp_retirement_transcript_begin_shard(&stage->transcript, stage->streams[0]) &&
              tp_retirement_samples_init(&stage->samples, &stage->transcript, stage->streams[1], &layout, stage->rows,
                                         stage->groups, stage->members) &&
              tp_retirement_metrics_shards_init(&stage->metrics, tag, stage->streams[2]) &&
              tp_retirement_samples_attach_metrics(&stage->samples, &stage->metrics);
    return ok;
}

/* A required check that copies bq_row_test_socket out of A's candidate root
 * into its work directory and runs it is denied the connect and fails its
 * receipt; below the sandbox's lowest Landlock ABI a check is refused before
 * its child starts. The check's run record names the ABI it ran with. */
BUSTER_GLOBAL_LOCAL void bq_row_test_sockets(BqRowTestFixture* fixture)
{
    BqCheckTestFixture* checks = fixture->checks;
    char probe[192];
    snprintf(probe, sizeof(probe), "%s/source-candidate-1/socket-probe", checks->workspaces);
    BqCheckTestSpec specs[BQ_CHECK_TEST_CHECKS];
    memcpy(specs, checks->specs, sizeof(specs));
    specs[1].script = "cp \"$4/socket-probe\" ./probe && ./probe";
    BqRetirementRequiredChecks variant = {.hosted = -1};
    BqRetirementCheckResult observed[BQ_CHECK_TEST_CHECKS] = {0};
    int evidence = -1, work = -1;
    BQ_PREP_CHECK(bq_row_test_program(probe, bq_row_test_socket) &&
                  bq_check_test_import_variant(checks, specs, BQ_CHECK_TEST_CHECKS, 0, &checks->preparation,
                                               &checks->projection, &variant) == BQ_OK &&
                  bq_check_test_directory(checks, "evidence", &evidence) &&
                  bq_check_test_directory(checks, "work", &work));
    int listener = work >= 0 ? bq_row_test_listen(work, "control.sock") : -1;
    BqRetirementCheckRun run = bq_check_test_run_for(checks, &variant, &checks->held, evidence, work);
    bq_retirement_sandbox_abi_ceiling = BQ_ROW_TEST_OLD_ABI;
    BQ_PREP_CHECK(variant.owned && bq_retirement_check_run(&run, 0, observed) == BQ_CONFIGURATION_MISMATCH &&
                  bq_check_test_no_children());
    bq_retirement_sandbox_abi_ceiling = UINT32_MAX;
    char denied[SHA256_HEX_CAPACITY] = {0}, captured[SHA256_HEX_CAPACITY] = {0}, line[32] = {0};
    char record[BQ_RETIREMENT_CHECK_RUN_CAP + 1] = {0};
    u32 record_length = 0;
    bq_digest("denied\n", 7, (char8*)denied);
    snprintf(line, sizeof(line), "\nsandbox-abi=%u\n", bq_retirement_sandbox_abi());
    BQ_PREP_CHECK(listener >= 0 && variant.owned && bq_retirement_check_run(&run, 1, observed + 1) == BQ_OK &&
                  observed[1].exit_code == 0 && observed[1].failures == 1 &&
                  strcmp(observed[1].receipt_sha256, variant.checks[1].receipt_sha256) &&
                  bq_retirement_check_hash_file(evidence, "check-output-0001", BQ_RETIREMENT_CHECK_OUTPUT_CAP,
                                                captured) &&
                  !strcmp(captured, denied) &&
                  bq_record_read_at(evidence, "check-run-0001", (u8*)record, BQ_RETIREMENT_CHECK_RUN_CAP,
                                    &record_length) == BQ_OK &&
                  strstr(record, line) && bq_check_test_no_children());
    if (listener >= 0) close(listener);
    BQ_PREP_CHECK(work >= 0 && unlinkat(work, "control.sock", 0) == 0 && unlink(probe) == 0);
    if (work >= 0) close(work);
    if (evidence >= 0) close(evidence);
    BQ_PREP_CHECK(bq_retirement_required_checks_release(&variant) &&
                  bq_check_test_install(checks->recipes, &checks->preparation, &checks->projection, checks->specs,
                                        BQ_CHECK_TEST_CHECKS, 0, checks->profile, sizeof(checks->profile)));
}

/* The producer's cases on an x86-64 Linux host. Elsewhere it must refuse
 * before any step, and the cases that run steps are skipped, reported. */
BUSTER_GLOBAL_LOCAL void bq_row_test_producer(BqRowTestFixture* fixture)
{
    if (BQ_RETIREMENT_ROW_HOST_NATIVE) bq_row_test_producer_native(fixture);
    else
    {
        BqRetirementRowPlan plan = {0};
        BqRetirementRowObserved observed = {0};
        int work = -1;
        BQ_PREP_CHECK(bq_row_test_compilers(fixture));
        BQ_PREP_CHECK(bq_row_test_run(fixture, 0, 0, &plan, &observed, &work) == BQ_CONFIGURATION_MISMATCH &&
                      !observed.owned && bq_check_test_no_children());
        fprintf(stderr, "RETIREMENT_PREP row producer: host is not x86-64 Linux; the producer refused "
                "(BQ_CONFIGURATION_MISMATCH) and its step cases are skipped\n");
        bq_retirement_row_plan_release(&plan);
        if (work >= 0) close(work);
    }
}

BUSTER_GLOBAL_LOCAL void bq_row_test_stage_close(BqRowTestStage* stage)
{
    for (u32 index = 0; index < 3; index += 1)
        if (stage->streams[index]) fclose(stage->streams[index]);
}

/* Lane D's campaign binding (bq_retirement_campaign_bind) over the gate the
 * row plan and an honest observation issue, with measured commands resolved
 * from the plan's templates in the canonical layout: the second A/A label
 * aggregate the plan derives must be the one the binding computes. */
BUSTER_GLOBAL_LOCAL int bq_row_test_bind(BqRowTestFixture* fixture, BqRetirementRowPlan* plan,
    char const aa_second[SHA256_HEX_CAPACITY])
{
    BqCheckTestFixture* checks = fixture->checks;
    BqRetirementRowObserved observed = {0};
    BqRetirementRowJoined joined = {0};
    BqRetirementRequiredChecks required = {.hosted = -1};
    BqRetirementCheckResult results[BQ_CHECK_TEST_CHECKS] = {0};
    BqRetirementUnitGate gate = {0};
    BqRowTestStage* stages = calloc(2, sizeof(*stages));
    enum { UNITS = 4, COMMANDS = UNITS * TP_RETIREMENT_CAMPAIGN_COMMANDS_PER_UNIT };
    TpRetirementMeasuredCommand commands[2][COMMANDS];
    char digests[2][COMMANDS][SHA256_HEX_CAPACITY], outputs[2][COMMANDS][SHA256_HEX_CAPACITY];
    char* storage = calloc(2u * COMMANDS, BQ_RETIREMENT_ROW_COMMAND_STORAGE);
    BqRetirementRowCommand resolved[2][COMMANDS];
    memcpy(plan->aa_second_commands_sha256, aa_second, SHA256_HEX_CAPACITY);
    bool ok = stages && storage && bq_row_test_observe(plan, &observed) &&
              bq_retirement_row_evidence_join(plan, &checks->projection, &observed, &joined) == BQ_OK &&
              bq_check_test_import(checks, &checks->job, &checks->projection, &required) == BQ_OK;
    if (ok) bq_row_test_passing(&required, results);
    ok = ok && bq_retirement_unit_gate_admit(&checks->projection, &required, results, &joined.evidence, &gate) == BQ_OK &&
         bq_row_test_stage_open(&stages[0], "aa") && bq_row_test_stage_open(&stages[1], "ab");
    /* Slots: the object group, the row 4 and row 5 singletons, then row 4's
     * runtime; per stage [slot * 2 + variant]. The A/A second label resolves
     * {{label}} as 2. */
    static u32 const slot_rows[UNITS] = {0, 4, 5, 4};
    for (u32 stage = 0; ok && stage < 2; stage += 1)
        for (u32 slot = 0; ok && slot < UNITS; slot += 1)
            for (u32 variant = 0; ok && variant < 2; variant += 1)
            {
                u32 index = slot * 2u + variant, row = slot_rows[slot], side = stage ? variant : 0;
                bool runtime = slot == 3, batch = slot == 0;
                BqRetirementRowPlanRow const* planned = plan->rows + row;
                BqRetirementRowPlanGroup const* group = batch ? plan->groups : NULL;
                char output_leaf[32], metrics_leaf[32];
                bq_retirement_row_leaves(row, output_leaf, metrics_leaf);
                BqRetirementRowContext context = {.fixture = planned->fixture, .output = output_leaf,
                    .metrics = batch ? group->metrics : metrics_leaf, .inputs = batch ? group->list_leaf : NULL,
                    .side = side, .label = !stage && variant ? 2u : 1u};
                BqRetirementRowTemplate const* template = plan->templates + (batch ? group->template_index :
                                                                            runtime ? planned->runtime : planned->compile);
                ok = bq_retirement_row_command(template, &context,
                                               storage + (size_t)(stage * COMMANDS + index) * BQ_RETIREMENT_ROW_COMMAND_STORAGE,
                                               &resolved[stage][index]);
                BqRetirementObservedSide const* facts = observed.facts[row].side + side;
                char const* objects[1] = {facts->artifact_sha256};
                ok = ok && (runtime ? (memcpy(outputs[stage][index], checks->rows[4].independent_oracle_sha256,
                                              SHA256_HEX_CAPACITY), true) :
                            batch ? tp_retirement_batch_contract_output(&joined.evidence.groups[0].contract[side],
                                                                        outputs[stage][index]) :
                            tp_retirement_batch_output_digest(objects, 1, outputs[stage][index]));
                commands[stage][index] = (TpRetirementMeasuredCommand){.unit = runtime ? row : slot,
                    .kind = runtime, .variant = variant, .argument_count = resolved[stage][index].argument_count,
                    .environment_count = resolved[stage][index].environment_count,
                    .timeout_seconds = template->timeout_seconds, .arguments = resolved[stage][index].arguments,
                    .environment = resolved[stage][index].environment, .directory = BQ_RETIREMENT_ROW_WORK_PATH,
                    .artifact = batch || runtime ? NULL : "row-artifact.o",
                    .batch = batch ? &joined.evidence.groups[0].contract[side] : NULL,
                    .command_sha256 = digests[stage][index], .output_sha256 = outputs[stage][index],
                    .exit_status = batch ? (int)plan->groups[0].exit_status : 0};
                ok = ok && tp_retirement_command_hash(&commands[stage][index], digests[stage][index]);
            }
    TpRetirementCampaignBudget budget = bq_campaign_service_budget();
    char budget_sha256[SHA256_HEX_CAPACITY] = {0};
    unsigned stages_of[3] = {TP_RETIREMENT_BUDGET_STAGE_OBJECT, TP_RETIREMENT_BUDGET_STAGE_LINK,
                             TP_RETIREMENT_BUDGET_STAGE_SELF_HOST};
    TpRetirementCampaignReview review = {&budget, stages_of, NULL, NULL, NULL, 3, 0};
    TpRetirementPlan statistics = {.version = TP_RETIREMENT_STATISTICS_VERSION, .seed = 1, .pairs_per_round = 60,
        .resamples = TP_RETIREMENT_MIN_RESAMPLES, .bootstrap_members_per_scope = 1, .cell_members_per_scope = 1,
        .frozen_before_samples = 1};
    TpRetirementExecutable executables[2] = {0};
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    TpRetirementCampaignCommand workspace[2 * COMMANDS];
    unsigned identity[UNITS];
    ok = ok && tp_retirement_budget_digest(&budget, budget_sha256) &&
         tp_retirement_executable_init(&executables[0], checks->held.descriptors[0],
                                       checks->projection.prepared.binary_sha256[0]) &&
         tp_retirement_executable_init(&executables[1], checks->held.descriptors[1],
                                       checks->projection.prepared.binary_sha256[1]);
    int bound = ok ? bq_retirement_campaign_bind(&binding, &gate.correctness, &campaign, &statistics,
        &stages[0].samples, &stages[1].samples, &executables[0], &executables[1], commands[0], commands[1], workspace,
        2 * COMMANDS, identity, UNITS, &review, budget_sha256, gate.correctness.sealed_sha256,
        gate.correctness.checks_sha256) : -1;
    if (stages)
    {
        bq_row_test_stage_close(&stages[0]);
        bq_row_test_stage_close(&stages[1]);
    }
    free(stages);
    free(storage);
    bq_retirement_unit_gate_release(&gate);
    bq_retirement_row_joined_release(&joined);
    bq_retirement_row_observed_release(&observed);
    bq_retirement_required_checks_release(&required);
    return bound;
}

/* The plan's second A/A label aggregate is lane D's: the campaign binds with
 * it, and with one changed digit (another aggregate) it refuses. */
BUSTER_GLOBAL_LOCAL void bq_row_test_campaign(BqRowTestFixture* fixture)
{
    BqRetirementRowPlan plan = {0};
    BQ_PREP_CHECK(bq_row_test_pin(fixture, &fixture->checks->projection, 0) &&
                  bq_row_test_import(fixture, &fixture->checks->projection, &plan) == BQ_OK);
    char derived[SHA256_HEX_CAPACITY], changed[SHA256_HEX_CAPACITY];
    memcpy(derived, plan.aa_second_commands_sha256, SHA256_HEX_CAPACITY);
    memcpy(changed, derived, SHA256_HEX_CAPACITY);
    changed[0] = changed[0] == '0' ? '1' : '0';
    int bound = plan.owned ? bq_row_test_bind(fixture, &plan, derived) : -1;
    if (bound != 1) fprintf(stderr, "RETIREMENT_PREP row plan campaign bind returned %d\n", bound);
    BQ_PREP_CHECK(bound == 1);
    BQ_PREP_CHECK(plan.owned && bq_row_test_bind(fixture, &plan, changed) == 0);
    bq_retirement_row_plan_release(&plan);
}

BUSTER_GLOBAL_LOCAL void bq_row_test_runner(void)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    BqRowTestFixture* fixture = calloc(1, sizeof(*fixture));
    BqCheckTestFixture* checks = calloc(1, sizeof(*checks));
    char* text = malloc(BQ_ROW_TEST_PLAN_CAP);
    bool ok = fixture && checks && text && bq_check_test_setup(checks);
    if (fixture)
    {
        fixture->checks = checks;
        fixture->plan_text = text;
        fixture->compilers = (BqRetirementHeldBinaries){.descriptors = {-1, -1}};
        fixture->listener = -1;
    }
    ok = ok && bq_row_test_cpu(&fixture->cpu, fixture->cpu_model) &&
         strlen(checks->profile) < sizeof(fixture->base_profile);
    if (ok) memcpy(fixture->base_profile, checks->profile, strlen(checks->profile) + 1u);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        bq_row_test_importer(fixture);
        bq_row_test_join(fixture);
        bq_row_test_producer(fixture);
        bq_row_test_sockets(fixture);
        bq_row_test_campaign(fixture);
    }
    for (u32 side = 0; fixture && side < 2; side += 1)
        if (fixture->compilers.descriptors[side] >= 0) close(fixture->compilers.descriptors[side]);
    if (checks && checks->installed[0]) bq_check_test_teardown(checks);
    free(text);
    free(checks);
    free(fixture);
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors && bq_check_test_no_children());
}

#endif
