/* Private #1018 frozen-binary handoff. The integrator owns the trusted Clang
 * build stages and their complete toolchain/command provenance. This seam
 * binds their frozen outputs to the verified A preparation before B can use
 * either binary; the integrator must authenticate Clang provenance and hold
 * launched binary identity stable. This seam alone cannot authorize timing.
 */
#ifndef BUSTER_BENCH_RETIREMENT_BINARIES_H
#define BUSTER_BENCH_RETIREMENT_BINARIES_H

typedef struct BqRetirementBinaries
{
    char preparation_sha256[SHA256_HEX_CAPACITY];
    char source_sha256[2][SHA256_HEX_CAPACITY];
    char binary_sha256[2][SHA256_HEX_CAPACITY];
    char binary_identity_sha256[2][SHA256_HEX_CAPACITY];
} BqRetirementBinaries;

/* The build producer must first freeze both successful trusted Clang outputs
 * as mode-read-only, single-link files in the service-private trusted-build
 * directory. Both calls independently import A and read the frozen files. */
BUSTER_F_DECL BqError bq_retirement_binaries_record(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char record_sha256[SHA256_HEX_CAPACITY]);
BUSTER_F_DECL BqError bq_retirement_binaries_import(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, char const preparation_sha256[SHA256_HEX_CAPACITY],
    char const record_sha256[SHA256_HEX_CAPACITY], BqRetirementBinaries* verified);

#endif
