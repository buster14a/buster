/* Private #1020/#509 per-row half of design step 9 (#881 lane B).
 *
 * The row-plan authority is an installed, profile-pinned file
 * (row-plan-sha256=), never taken from the candidate. It names, for the
 * sealed projection of one attempt, how every row is compiled and run: the
 * argv/environment templates, each row's fixture, the (A1) object batch
 * groups with their members, status-checked controls, pinned control
 * statuses, artifact leaves and metrics bounds, and the CPU the unit runs
 * rows on. bq_retirement_row_plan_import derives from it the exact command
 * digests of every row and group (in the canonical child layout below), each
 * row's batch key and batch-control mark, the frozen batch-group skeletons
 * and the second A/A label aggregate, and completes the projection's rows.
 *
 * bq_retirement_row_produce runs the plan in the unit with the held matched
 * binaries inside the bounded runner (retirement_check_runner.c's child
 * normalization, subreaper sweep and deadlines, a per-step Landlock sandbox
 * and per-step fstat identity of the held binaries, rehashed once before and
 * once after the run) and observes the row facts: exit status, artifacts and
 * their code sections (the independent artifact reader), the compiler's
 * per-input metrics records (status, error, diagnostic digest, fallback
 * count), runtime output and the CPU provenance. BqRetirementRowObserved is
 * that observation and has a canonical text form
 * (BQ-RETIREMENT-ROW-EVIDENCE-V1) the gate persists in retirement-rows/ and
 * the coordinator's replay parses again. bq_retirement_row_evidence_join
 * joins the plan and an observation into the BqRetirementRowEvidence the
 * correctness gate admits, so the replay can recompute the row plan and the
 * correctness seal from persisted evidence alone.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_ROW_PLAN_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_ROW_PLAN_H
#include "retirement_check_runner.h"
#include <sched.h>

#define BQ_RETIREMENT_ROW_PLAN_NAME "native-retirement-performance-v1.row-plan"
#define BQ_RETIREMENT_ROW_PLAN_BYTES_CAP (64u * 1024u * 1024u)
#define BQ_RETIREMENT_ROW_PLAN_TEMPLATES_CAP 64u
/* A row field the plan leaves unset, and a row compiled in a batch group. */
#define BQ_RETIREMENT_ROW_PLAN_NONE UINT32_MAX
#define BQ_RETIREMENT_ROW_PLAN_BATCH (UINT32_MAX - 1u)

/* The canonical child layout every row command runs in, and in which the
 * plan derives its digests: the side's held binary at 3 (baseline) or 4
 * (candidate), A's base and candidate roots at 5 and 6, and the step's work
 * directory at 7, which is also the working directory. */
#define BQ_RETIREMENT_ROW_SLOT_BINARY 3
#define BQ_RETIREMENT_ROW_SLOT_SOURCE 5
#define BQ_RETIREMENT_ROW_SLOT_WORK 7
#define BQ_RETIREMENT_ROW_WORK_PATH "/proc/self/fd/7"
/* The native-host timed target's name in batch contracts. */
#define BQ_RETIREMENT_ROW_BATCH_TARGET "x86_64-linux"

/* Design step 9's per-row evidence directory, a sibling of
 * retirement-checks/: one canonical row-evidence file, sealed 0500 once the
 * gate is issued. */
#define BQ_RETIREMENT_ROWS_DIRECTORY "retirement-rows"
#define BQ_RETIREMENT_ROW_EVIDENCE_NAME "row-evidence"
#define BQ_RETIREMENT_ROW_EVIDENCE_BYTES_CAP (256u * 1024u * 1024u)
#define BQ_RETIREMENT_ROW_CPU_MASK_CAPACITY (2u * sizeof(cpu_set_t) + 1u)
/* A step's metrics file and artifact are read up to these bounds. */
#define BQ_RETIREMENT_ROW_ARTIFACT_BYTES_CAP (256u * 1024u * 1024u)

typedef enum BqRetirementRowTemplateKind
{
    BQ_RETIREMENT_ROW_TEMPLATE_COMPILE = 1,
    BQ_RETIREMENT_ROW_TEMPLATE_BATCH,
    BQ_RETIREMENT_ROW_TEMPLATE_RUNTIME,
    BQ_RETIREMENT_ROW_TEMPLATE_COUNT
} BqRetirementRowTemplateKind;

/* One argv/environment template. Fields may hold any number of the tokens
 * {{binary}}, {{source:0}}, {{source:1}}, {{work}}, {{fixture}},
 * {{output}}, {{metrics}}, {{inputs}} and {{label}}, as its kind allows.
 * template_sha256 is the digest of the unresolved template and bounds. */
typedef struct BqRetirementRowTemplate
{
    char const* arguments[BQ_RETIREMENT_CHECK_ARGUMENTS_CAP];
    char const* environment[BQ_RETIREMENT_CHECK_ENVIRONMENT_CAP];
    u32 kind, timeout_seconds, memory_mib, argument_count, environment_count;
    char template_sha256[SHA256_HEX_CAPACITY];
} BqRetirementRowTemplate;

/* compile is BQ_RETIREMENT_ROW_PLAN_NONE, BQ_RETIREMENT_ROW_PLAN_BATCH or a
 * compile template; runtime is NONE or a runtime template; group and input
 * name a batch row's group and input. */
typedef struct BqRetirementRowPlanRow
{
    char const* fixture;
    u32 compile, runtime, group, input;
} BqRetirementRowPlanRow;

/* One pinned batch input: its row (TP_RETIREMENT_BATCH_NO_ROW for a control
 * naming no row), member mark, fixture, frozen status and error, and object
 * leaf (NULL when it must not compile). */
typedef struct BqRetirementRowPlanInput
{
    char const* fixture;
    char const* status;
    char const* error;
    char const* artifact;
    u32 member, row;
} BqRetirementRowPlanInput;

/* One (A1) object batch group: its template, allocator, metrics leaf and
 * bound, frozen exit status and inputs (plan inputs first_input ..
 * first_input + input_count - 1). list_leaf is the response file the
 * {{inputs}} token names; key_sha256 every member's and control's batch key;
 * command_sha256 each side's batch command and second_sha256 the baseline's
 * second A/A label command. */
typedef struct BqRetirementRowPlanGroup
{
    char const* allocator;
    char const* metrics;
    u64 metrics_bytes_max;
    u32 template_index, exit_status, first_input, input_count;
    char list_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP];
    char key_sha256[SHA256_HEX_CAPACITY];
    char command_sha256[2][SHA256_HEX_CAPACITY];
    char second_sha256[SHA256_HEX_CAPACITY];
} BqRetirementRowPlanGroup;

/* An imported plan, bound to one attempt's sealed projection. completed is
 * the projection's rows with every command digest, batch key and batch
 * control the plan derives; the strings point into text. Zero-initialize;
 * release on every path. */
typedef struct BqRetirementRowPlan
{
    char* text;
    BqRetirementRowTemplate* templates;
    BqRetirementRowPlanRow* rows;
    BqRetirementRowPlanGroup* groups;
    BqRetirementRowPlanInput* inputs;
    BqRetirementTrustedRow* completed;
    u64 job_id, attempt_token;
    u32 template_count, row_count, group_count, input_count, cpu, native_target;
    char authority_sha256[SHA256_HEX_CAPACITY], population_sha256[SHA256_HEX_CAPACITY];
    char aa_second_commands_sha256[SHA256_HEX_CAPACITY], cpu_model_sha256[SHA256_HEX_CAPACITY];
    u32 owned;
} BqRetirementRowPlan;

/* Imports recipes/native-retirement-performance-v1.row-plan for job's
 * attempt over projection with the compiled profile, whose
 * row-plan-sha256= pin the file must hash to. The blocked profile has no pin:
 * BQ_RECIPE_MISMATCH before any file is read. A plan that does not name this
 * projection's support, census, population, native target and exact rows,
 * or whose rows, templates and groups do not cover the projection as the A1
 * contract requires, is BQ_RECIPE_MISMATCH. */
BUSTER_F_DECL BqError bq_retirement_row_plan_import(int installed, BqJob const* job,
    BqRetirementProjection const* projection, BqRetirementRowPlan* plan);
BUSTER_F_DECL bool bq_retirement_row_plan_release(BqRetirementRowPlan* plan);

/* One attempt's observation of a plan: the row facts in row order, each plan
 * input's observed diagnostic and object digest per side (the object empty
 * when it wrote none) and the CPU provenance the rows ran with. Zero-
 * initialize; release on every path. */
typedef struct BqRetirementRowObserved
{
    BqRetirementRowFact* facts;
    char (*diagnostic_sha256)[2][SHA256_HEX_CAPACITY];
    char (*object_sha256)[2][SHA256_HEX_CAPACITY];
    u64 job_id, attempt_token;
    u32 row_count, input_count, cpus;
    char plan_sha256[SHA256_HEX_CAPACITY], population_sha256[SHA256_HEX_CAPACITY];
    char cpu_model_sha256[SHA256_HEX_CAPACITY];
    char cpu_mask[BQ_RETIREMENT_ROW_CPU_MASK_CAPACITY];
    u32 owned;
} BqRetirementRowObserved;

/* An empty observation shaped for plan (owned on success). */
BUSTER_F_DECL bool bq_retirement_row_observed_init(BqRetirementRowPlan const* plan, BqRetirementRowObserved* observed);
BUSTER_F_DECL bool bq_retirement_row_observed_release(BqRetirementRowObserved* observed);
/* The canonical BQ-RETIREMENT-ROW-EVIDENCE-V1 bytes (malloc'd into *text). */
BUSTER_F_DECL bool bq_retirement_row_observed_format(BqRetirementRowObserved const* observed, char** text,
    u32* length);
/* Parses canonical bytes for plan: anything that does not format back to the
 * same bytes, or names another plan, attempt or shape, is BQ_CORRUPT. */
BUSTER_F_DECL BqError bq_retirement_row_observed_parse(u8 const* bytes, u32 length, BqRetirementRowPlan const* plan,
    BqRetirementRowObserved* observed);

/* The row evidence the correctness gate admits: the plan's completed rows,
 * the observed facts and the frozen batch groups, whose contracts combine
 * the plan's skeleton with the observed diagnostics and objects. The groups,
 * their inputs and every string they name are owned here, independent of the
 * plan and the observation. Zero-initialize; release on every path. */
typedef struct BqRetirementRowJoined
{
    BqRetirementRowEvidence evidence;
    BqRetirementBatchGroup* groups;
    TpRetirementBatchInput* inputs;
    char* strings;
    u32 owned;
} BqRetirementRowJoined;

/* Joins plan and observed for projection. The observation must name this
 * plan, attempt and population and have run on the plan's CPU alone with the
 * plan's CPU model (BQ_CONFIGURATION_MISMATCH otherwise); the facts' commands,
 * outputs and statuses are the correctness gate's to judge. */
BUSTER_F_DECL BqError bq_retirement_row_evidence_join(BqRetirementRowPlan const* plan,
    BqRetirementProjection const* projection, BqRetirementRowObserved const* observed, BqRetirementRowJoined* joined);
BUSTER_F_DECL bool bq_retirement_row_joined_release(BqRetirementRowJoined* joined);

/* The producer runs the native target's (x86_64-linux) executables itself,
 * as runtime rows and as the stand-in steps' artifacts, so it runs only on
 * an x86-64 Linux host. Elsewhere it refuses (BQ_CONFIGURATION_MISMATCH)
 * before any step. */
#if defined(__x86_64__) && defined(__linux__)
#define BQ_RETIREMENT_ROW_HOST_NATIVE 1
#else
#define BQ_RETIREMENT_ROW_HOST_NATIVE 0
#endif

/* Everything the producer borrows: the imported plan, the held binaries, A's
 * held roots (base, candidate), the attempt's private retirement-work/
 * directory (the producer creates row-work-<row>-<side> and
 * group-work-<group>-<side> in it, each new), the cancellation self-pipe and
 * the job's absolute deadline. */
typedef struct BqRetirementRowRun
{
    BqRetirementRowPlan const* plan;
    BqRetirementHeldBinaries const* binaries;
    int sources[2];
    int work;
    int cancellation_fd;
    u64 deadline_ns;
} BqRetirementRowRun;

/* Runs every step of the plan for both sides on the plan's CPU and observes
 * it into observed (which it initializes). BQ_OK means every step ran to a
 * verdict; a failing row is recorded as observed (the gate refuses it). A
 * batch whose metrics do not authenticate against its frozen contract (its
 * pinned statuses, errors and exit status) is BQ_RECIPE_MISMATCH; a held
 * binary that changed is BQ_SOURCE_MISMATCH; a CPU the unit cannot run on, or
 * a host that is not x86-64 Linux (BQ_RETIREMENT_ROW_HOST_NATIVE), is
 * BQ_CONFIGURATION_MISMATCH; cancellation, the deadline and a descendant
 * that survives the sweep map as for the check runner. */
BUSTER_F_DECL BqError bq_retirement_row_produce(BqRetirementRowRun const* run, BqRetirementRowObserved* observed);
/* The digest of logical CPU cpu's model as /proc/cpuinfo names it (see
 * retirement_row_producer.c); the observation records it for the pinned CPU. */
BUSTER_F_DECL bool bq_retirement_row_cpu_model(u32 cpu, char digest[SHA256_HEX_CAPACITY]);

#endif
