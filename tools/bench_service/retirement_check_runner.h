/* Private #1020/#509 in-unit check runner (lane B, design step 9).
 * bq_retirement_check_run executes one check of the installed required-check
 * authority in a bounded child: its own process group, the authority's
 * memory limit, the check's wall bound and the job's absolute deadline, the
 * SIGTERM self-pipe, no inherited descriptors but the held binaries, A's
 * source roots, the pinned tools and a fresh work directory, and bounded
 * stdout (the check's summary, compared with the pinned digest) and stderr
 * (its log). It rehashes both held binaries around the child and writes a
 * same-attempt receipt that binds job, token, request, A, the sources and
 * binaries the child actually ran against, how the child ended and its
 * output. The unit never reaches the queue or the lease.
 *
 * BqRetirementRowEvidence is what the per-row half of step 9 hands the
 * correctness gate; retirement_row_plan.c joins it from the pinned row-plan
 * authority and the producer's observation (retirement_row_producer.c).
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CHECK_RUNNER_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CHECK_RUNNER_H
#include "retirement_correctness_service.h"
#include "../throughput/retirement_sandbox.h"

/* The one target the unit times and whose checks may claim native
 * execution: x86_64-unknown-linux-gnu, the A1 native-host timed target.
 * It equals retirement_campaign_binding.h's BQ_RETIREMENT_NATIVE_TIMED_TARGET
 * (lane D; the tests check both are the same), which the service translation
 * unit does not include. */
#define BQ_RETIREMENT_UNIT_NATIVE_TARGET 11u

/* The check's stdout is its complete summary; its stderr is a log. */
#define BQ_RETIREMENT_CHECK_OUTPUT_CAP (1024u * 1024u)
#define BQ_RETIREMENT_CHECK_LOG_CAP (16u * 1024u * 1024u)
#define BQ_RETIREMENT_CHECK_RUN_CAP 1024u

/* BQ_RETIREMENT_SANDBOX_MIN_ABI, the sandbox and the canonical child layout
 * are shared with lane D (retirement_sandbox.h). */

/* Everything one run borrows. evidence is the attempt's new check evidence
 * directory (the runner creates check-output-<i>, check-log-<i>,
 * check-receipt-<i> and check-run-<i> in it, each O_EXCL); work is the
 * attempt's private retirement-work/ directory (the runner creates a new
 * check-work-<i> in it as the child's working directory). sources are A's
 * held materialized roots (base, candidate) and source_sha256 the manifest
 * digests the caller's scan of those roots verified. */
typedef struct BqRetirementCheckRun
{
    BqRetirementRequiredChecks const* checks;
    BqRetirementHeldBinaries const* binaries;
    char const (*source_sha256)[SHA256_HEX_CAPACITY];
    int sources[2];
    int work;
    int evidence;
    int cancellation_fd;
    u64 deadline_ns;
} BqRetirementCheckRun;

/* Runs check index. BQ_OK means the child reached a verdict (pass or fail)
 * and its receipt is durable; result then holds the observed facts, where a
 * failing check has a nonzero exit_code, failures, timed_out or
 * out_of_memory, or a receipt that differs from the required one. A readable
 * cancellation descriptor returns BQ_WORKER_CANCEL_SIGNAL and an expired job
 * deadline BQ_WORKER_TIMEOUT, both after the process group is killed and the
 * child reaped; a held binary, tool or hosted record that is not the same
 * unchanged file afterwards (or whose bytes changed) is BQ_SOURCE_MISMATCH;
 * a planted evidence or work name BQ_WORKSPACE_MISMATCH; another live child
 * of this process BQ_WORKER_MISMATCH before anything runs; a descendant that
 * survives the sweep BQ_CLEANUP_FAILED. A hosted check runs no child: its
 * output is the held hosted acceptance record. */
BUSTER_F_DECL BqError bq_retirement_check_run(BqRetirementCheckRun const* run, u32 index,
    BqRetirementCheckResult* result);

/* evidence holds exactly the four files of each of the count checks, each a
 * single-link, service-owned regular file of mode 0400, in a service-owned
 * private directory (sealed: exactly 0500). Each receipt hashes to
 * checks[i].receipt_sha256, and each run record names that receipt and the
 * output and log digests those files rehash to. evidence_sha256, when given,
 * receives the ordered aggregate of every run record's and log's digest. */
BUSTER_F_DECL bool bq_retirement_check_evidence_closed(int evidence, BqRetirementRequiredCheck const* checks,
    u32 count, bool sealed, char evidence_sha256[SHA256_HEX_CAPACITY]);
/* Whether this process has no child at all (through /proc). Step 9 runs its
 * checks only then, and requires it again before issuing the gate. */
BUSTER_F_DECL bool bq_retirement_check_descendants_absent(void);
/* The ordered aggregate of the required receipts a passing gate joined. */
BUSTER_F_DECL bool bq_retirement_check_receipts_hash(BqRetirementRequiredCheck const* checks, u32 count,
    char digest[SHA256_HEX_CAPACITY]);

/* The per-row half of step 9 (A1): the projection's rows completed with
 * independently derived compiler and runtime command digests (argv, cwd and
 * environment by trusted binary), CPU-bound batch keys and batch controls,
 * the observed row facts (artifacts, codes, diagnostics, runtime outputs
 * against the independent oracle) and the frozen plan-v3 batch groups with
 * each side's object digests, control statuses and diagnostics.
 * aa_second_commands_sha256 is the sealed second A/A label aggregate and
 * plan_sha256 the digest of the row-plan authority that derived all of it.
 * Every pointer is borrowed; groups must stay in place until the gate that
 * froze them is released. bq_retirement_row_evidence_join builds it. */
typedef struct BqRetirementRowEvidence
{
    BqRetirementTrustedRow const* rows;
    BqRetirementRowFact const* facts;
    BqRetirementBatchGroup const* groups;
    u32 row_count, group_count;
    char aa_second_commands_sha256[SHA256_HEX_CAPACITY];
    char plan_sha256[SHA256_HEX_CAPACITY];
} BqRetirementRowEvidence;

#endif
