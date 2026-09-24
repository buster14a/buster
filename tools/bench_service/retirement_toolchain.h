/* Private #1018 installed tool bundle check. The integration owner binds the
 * reviewed bundle and keeps the build sandbox from reaching ambient tools. */
#ifndef BUSTER_BENCH_RETIREMENT_TOOLCHAIN_H
#define BUSTER_BENCH_RETIREMENT_TOOLCHAIN_H

#define BQ_RETIREMENT_TOOLCHAIN_ROOT "/opt/buster-bench/installed/toolchain/native-retirement-performance-v1"
#define BQ_RETIREMENT_TOOLCHAIN_PATH_CAP 512u

typedef struct BqRetirementToolchain
{
    char root[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP];
    char path[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP + 9];
    char manifest_sha256[SHA256_HEX_CAPACITY];
    char identity_sha256[SHA256_HEX_CAPACITY];
    u32 entries;
    u64 bytes;
} BqRetirementToolchain;

BUSTER_F_DECL BqError bq_retirement_toolchain_verify(int installed, String8 profile,
    char const* fixed_root, BqRetirementToolchain* observed);
BUSTER_F_DECL bool bq_retirement_toolchain_recheck(BqRetirementToolchain const* expected);

#endif
