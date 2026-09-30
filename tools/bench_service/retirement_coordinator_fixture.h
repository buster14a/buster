/* #881 PR 4 test fixture, shared by tests.c (phase_channel_tests.h) and the
 * preparation runner (retirement_worker_unit_tests.h): a receipt authority
 * and its context chain, published as PR 3's producer will publish them.
 *
 * bq_coordinator_fixture_authority publishes a one-shard execution receipt
 * into the result directory (the store root), issues the authority into the
 * attempt's retirement-authority/ and writes the chain
 * (bq_retirement_context_chain_format) naming the given A, ready and
 * row-plan digests with the authority's plan and context. A test forges a
 * self-consistent authority by passing digests the coordinator does not hold.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_COORDINATOR_FIXTURE_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_COORDINATOR_FIXTURE_H

#define BQ_COORDINATOR_FIXTURE_PLAN "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define BQ_COORDINATOR_FIXTURE_CONTEXT "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"

BUSTER_GLOBAL_LOCAL bool bq_coordinator_fixture_authority(int result_directory, int attempt, u64 job, u64 token,
    char const* preparation_sha256, char const* ready_sha256, char const* row_plan_sha256, bool with_chain,
    char authority_sha256[SHA256_HEX_CAPACITY])
{
    char label[TP_RETIREMENT_STORE_TOKEN_CAPACITY], shard_digest[SHA256_HEX_CAPACITY];
    char receipt_digest[SHA256_HEX_CAPACITY], receipt[1024], chain[BQ_RETIREMENT_COORDINATOR_CHAIN_CAP];
    char chain_name[TP_RETIREMENT_STORE_PATH_BYTES + 1];
    TpRetirementStore store;
    TpRetirementStoredFile files[4];
    TpRetirementReceiptAuthority issued = {0};
    bool ok = tp_retirement_store_job_label(label, job) &&
              (mkdirat(attempt, BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY, 0700) == 0 || errno == EEXIST);
    int authority = ok ? openat(attempt, BQ_RETIREMENT_UNIT_AUTHORITY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC) :
                    -1;
    bool opened = authority >= 0 && tp_retirement_store_open(&store, result_directory, files, 4);
    ok = opened && tp_retirement_store_plan(&store, 2, 2048, 3, 1024);
    char const* bodies[2] = {"{}\n", receipt};
    char const* paths[2] = {"shard.jsonl", TP_RETIREMENT_EXECUTION_RECEIPT_PATH};
    for (u32 index = 0; ok && index < 2; index += 1)
    {
        if (index == 1)
        {
            int length = snprintf(receipt, sizeof(receipt),
                "{\"attempt\":%" PRIu64 ",\"boot_id\":\"boot-1\",\"bound_at_ns\":1000,\"completed_at_ns\":2000,"
                "\"context_sha256\":\"%s\",\"execution_plan_sha256\":\"%s\",\"invocations\":1,"
                "\"job_id\":\"%s\",\"schema\":\"buster-native-retirement-execution-receipt-v1\","
                "\"shards\":[{\"bytes\":3,\"path\":\"shard.jsonl\",\"records\":1,\"sha256\":\"%s\"}],\"version\":1}\n",
                (uint64_t)token, BQ_COORDINATOR_FIXTURE_CONTEXT, BQ_COORDINATOR_FIXTURE_PLAN, label, shard_digest);
            ok = length > 0 && (size_t)length < sizeof(receipt);
        }
        TpRetirementPending pending = {0};
        size_t length = strlen(bodies[index]);
        char* digest = index ? receipt_digest : shard_digest;
        ok = ok && tp_retirement_store_begin(&store, paths[index], 1024, &pending) &&
             fwrite(bodies[index], 1, length, pending.stream) == length;
        if (ok) bq_digest(bodies[index], (u32)length, (char8*)digest);
        if (ok) ok = tp_retirement_store_publish(&store, &pending, length, digest);
        else if (pending.stream) tp_retirement_store_abort(&store, &pending);
    }
    ok = ok && tp_retirement_store_receipt_authority(&store, authority, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, label,
                                                     token, BQ_COORDINATOR_FIXTURE_PLAN, BQ_COORDINATOR_FIXTURE_CONTEXT,
                                                     &issued);
    if (opened) tp_retirement_store_close(&store);
    u32 chain_length = ok ? bq_retirement_context_chain_format(chain, job, token, preparation_sha256, ready_sha256,
                                                               row_plan_sha256, issued.plan_sha256,
                                                               issued.context_sha256) : 0;
    int named = snprintf(chain_name, sizeof(chain_name), "context-chain-job-%" PRIu64 "-%" PRIu64 ".txt",
                         (uint64_t)job, (uint64_t)token);
    int file = ok && with_chain && chain_length && named > 0 ?
               openat(authority, chain_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
    if (with_chain) ok = ok && file >= 0 && write(file, chain, chain_length) == (ssize_t)chain_length;
    if (file >= 0 && close(file) != 0) ok = false;
    if (authority >= 0) close(authority);
    if (ok) memcpy(authority_sha256, issued.authority_sha256, SHA256_HEX_CAPACITY);
    return ok;
}

#endif
