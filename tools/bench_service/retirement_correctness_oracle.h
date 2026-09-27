/* Private #1020 native-runtime oracle producer. A separately admitted,
 * profile-pinned reference ledger covers every eligible native execution row.
 * The service runs its held reference program through the normal deadline-
 * controlled process runner; observed frozen output becomes the expectation.
 * The candidate never supplies expected output bytes or a skip decision.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_ORACLE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_CORRECTNESS_ORACLE_H
#include "retirement_artifact_service.h"

typedef struct BqRetirementOracleReference
{
    uint32_t row, census_row, target;
    char preparation_sha256[65], source_sha256[65], configuration_sha256[65];
    char build_receipt_sha256[65], binary_sha256[65], command_sha256[65];
    char output_name[BQ_RETIREMENT_OUTPUT_NAME_CAP];
} BqRetirementOracleReference;

typedef struct BqRetirementOracleLedger
{
    BqRetirementPrepared prepared;
    BqRetirementTrustedRow* rows;
    BqRetirementOracleReference const* references;
    uint32_t count, done;
    uint32_t failed, finished;
    char pinned_sha256[65], sealed_sha256[65];
} BqRetirementOracleLedger;

/* The caller obtains pinned_sha256 from installed policy, independently of
 * this array, the request, and either measured compiler. The service must
 * authenticate each independent build receipt before calling begin. */
BUSTER_F_DECL bool bq_retirement_oracle_begin(BqRetirementOracleLedger* ledger,
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* rows,
    BqRetirementOracleReference const* references, uint32_t count,
    char const pinned_sha256[65]);

/* A reference binary is a held, frozen file whose bytes match the pinned
 * binary hash. Execute it via /proc/self/fd/<reference_binary>, using the
 * normal runtime_start/launch/poll/finish path and the worker's deadline.
 * Pass its observed frozen output after exit zero. This reads the output
 * through the held descriptor and sets the row's oracle digest on success. */
BUSTER_F_DECL bool bq_retirement_oracle_observe(BqRetirementOracleLedger* ledger,
    int reference_binary, BqRetirementProcessCommand const* command,
    BqRetirementRuntimeStart const* start, int output);
/* Run the next independently pinned native reference through the actual
 * service child boundary. The caller supplies a dedicated cancellation pipe
 * (read end, CLOEXEC), the worker's absolute monotonic deadline, and a held
 * build receipt from its independently authorized producer. It must verify
 * lease/job/attempt continuity before and after each call; the receipt's
 * digest alone does not establish its independent build provenance. */
BUSTER_F_DECL bool bq_retirement_oracle_produce_next(BqRetirementOracleLedger* ledger,
    int reference_binary, int independent_build_receipt,
    BqRetirementProcessCommand const* command, BqRetirementArtifactLocation output,
    int cancellation_fd, uint64_t absolute_deadline_ns);
BUSTER_F_DECL bool bq_retirement_oracle_finish(BqRetirementOracleLedger* ledger);
BUSTER_F_DECL bool bq_retirement_oracle_ready(BqRetirementOracleLedger const* ledger);
#endif
