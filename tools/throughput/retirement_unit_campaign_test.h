/* Private functional fixtures for the in-unit #881-D campaign driver
 * (bench_service/retirement_unit_campaign.h). Real fixture children run the
 * whole sequence in order: SETTLING, the untimed production and reproduction
 * batches, MEASURING, the held bind with the plan and pre-sample context
 * derived from test pins, A/A, the fixture A/A admission, the A/B freeze,
 * A/B, the post-sample context and MEASURED, with lane E's store plan made
 * from the frozen capacity before attach. A forked supervisor stand-in
 * acknowledges the phases. The campaign is the one retirement_campaign_test.h
 * binds (object group rows 4 and 5, link singleton row 6 with runtime), plus
 * one untimed singleton group. The pinned rows are an in-memory population
 * (the preparation runner writes the documents from the installed census and
 * runs the validator over them); the driver writes the five workflow
 * documents into a scratch evidence root, and the READY result is mapped
 * onto lane E's composer request. Every launch runs in lane B's canonical
 * child layout and sandbox over stand-in roots (test_unit_campaign_layout
 * checks what a layout child sees and is denied; test_unit_campaign_runtime_rule
 * the runtime shape; test_unit_campaign_runtime_identity that a runtime
 * record names its program, not the binary). The admission, ready
 * digest and pins are
 * test data: no #426 verdict, acceptance measurement or real ready record.
 * Include after retirement_campaign_test.h, which compiles the fixture
 * admission. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_UNIT_CAMPAIGN_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_UNIT_CAMPAIGN_TEST_H
#define BQ_RETIREMENT_UNIT_CAMPAIGN_FIXTURE_AA 1
#include "../bench_service/retirement_unit_handoff.h"

#ifdef __linux__
#include <sys/socket.h>
#include <sys/wait.h>

static char const test_unit_campaign_ready[] = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
static char const test_unit_campaign_receipt[] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

/* Supervisor stand-in for job 1, attempt 2: acknowledges `answers` phase
 * messages in order; the 1-based message `corrupt` gets a wrong reply. */
static pid_t test_unit_campaign_peer(BqPhaseChannel* channel, unsigned answers, unsigned corrupt)
{
    int pair[2] = {-1, -1};
    int paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    pid_t child = paired ? fork() : -1;
    if (child == 0)
    {
        close(pair[0]);
        int ok = 1;
        for (unsigned i = 0; ok && i < answers; ++i)
        {
            unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
            ok = recv(pair[1], message, sizeof(message), 0) == BQ_PHASE_MESSAGE_BYTES;
            bq_phase_put(message + 40, i + 1 == corrupt ? 2u : 1u);
            ok = ok && send(pair[1], message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
        }
        close(pair[1]);
        _exit(ok ? 0 : 1);
    }
    if (pair[1] >= 0) close(pair[1]);
    int ready = child > 0 && bq_phase_init(channel, pair[0], 1, 2);
    if (!ready && pair[0] >= 0) close(pair[0]);
    if (!ready) *channel = (BqPhaseChannel){.descriptor = -1, .failed = 1};
    return child;
}

static int test_unit_campaign_peer_join(BqPhaseChannel* channel, pid_t child)
{
    int ok = channel->descriptor < 0 || close(channel->descriptor) == 0;
    channel->descriptor = -1;
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    return ok && waited == child;
}

typedef struct TestUnitCampaign
{
    char directory[TP_PATH_CAP], binary_path[TP_PATH_CAP], candidate_path[TP_PATH_CAP];
    char other_path[TP_PATH_CAP], code_path[TP_PATH_CAP];
    char list_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP], list_argument[TP_RETIREMENT_INPUT_LIST_LEAF_CAP + 1];
    char leak_text[32], binary_sha[65], artifact_sha[65], code_sha[65], runtime_sha[65];
    /* The link row's program: the fixture child's own bytes (program.bin in
     * A's base root stand-in), its compile's output digest and code facts. */
    char program_path[TP_PATH_CAP], program_output[65];
    TpRetirementCodeSide program_side;
    char batch_output[65], object_output[65], budget_sha[65], profile[1024], documents_path[TP_PATH_CAP];
    /* The #437 A/A admission receipt stand-in and its digest. */
    char aa_receipt[1024], aa_receipt_sha[65];
    char* environment[3];
    /* The second A/A label's commands differ by environment (lane B's plan
     * uses a label token). */
    char* label_environment[4];
    /* A's base and candidate root stand-ins: slots 5 and 6 of every launch. */
    char sources_path[2][TP_PATH_CAP];
    int sources[2];
    char* arguments[2][6][11];
    char command_sha[2][6][65];
    char* untimed_arguments[2][7];
    char* slow_arguments[2][7];
    char* noisy_arguments[2][7];
    char* zero_arguments[7];
    char untimed_sha[2][65], slow_sha[2][65], noisy_sha[2][65], zero_sha[65], zero_output[65];
    TpRetirementMeasuredCommand commands[2][6];
    TpRetirementUntimedBatch untimed[4], slow[4], noisy[4], different[4], running[4];
    TpRetirementCodeRow codes[4];
    TpRetirementCodeSide code_side;
    BqRetirementCampaignReady ready;
    unsigned untimed_rows[1];
    TestBatchFixture batch;
    TpRetirementCampaignBudget budget;
    TpRetirementCampaignReview review;
    unsigned group_stages[2], untimed_inputs[1], untimed_kinds[1], untimed_stages[1];
    BqRetirementTrustedRow trusted[7];
    BqRetirementRowFact facts[7];
    BqRetirementBatchGroup frozen;
    BqRetirementCorrectness gate, gate_copy;
    BqRetirementHeldBinaries held;
    BqRetirementUnitCampaignPins pins;
    /* The pinned rows (7 rows of 15 identity values) and their family. */
    BqRetirementDocumentPopulation population;
    BqRetirementDocumentRow population_rows[7];
    char population_pool[2048];
    BqRetirementDocumentFamily family;
    uint8_t assigned[7];
    int binary, candidate, other, cwd, code, leak, cpu;
} TestUnitCampaign;

/* The campaign test's two stages with a real pre-sample binding time, so the
 * untimed batches (already finished) precede the timed window. */
static int test_unit_campaign_open_stage(TpSampleTest* test, int cpu, uint64_t bound, TpRetirementMetricsShards* metrics,
    FILE** metrics_stream, char const* tag)
{
    static unsigned const ids[] = {4, 5, 6}, row_metrics[] = {0, 0, TP_RETIREMENT_SAMPLE_RUNTIME};
    static unsigned const kinds[] = {TP_RETIREMENT_GROUP_OBJECT, TP_RETIREMENT_GROUP_SINGLETON};
    static unsigned const offsets[] = {0, 2, 3}, members[] = {0, 1, 2};
    memset(test, 0, sizeof(*test));
    test->stream = tmpfile();
    test->spool = tmpfile();
    *metrics_stream = tmpfile();
    memcpy(test->row_ids, ids, sizeof(ids));
    memcpy(test->row_metrics, row_metrics, sizeof(row_metrics));
    memcpy(test->kinds, kinds, sizeof(kinds));
    memcpy(test->offsets, offsets, sizeof(offsets));
    memcpy(test->layout_members, members, sizeof(members));
    test->runtime[0] = 6;
    test->runtime_count = 1;
    test->group_count = 2;
    test->population = 7;
    test->layout = (TpRetirementLayout){3, 2, test->row_ids, test->row_metrics, test->kinds, test->offsets,
        test->layout_members};
    int ok = test->stream && test->spool && *metrics_stream &&
        tp_retirement_execution_init(&test->execution, 1, 2, test->runtime, 1, 7, 60, test->workspace, 7) &&
        tp_retirement_transcript_init(&test->transcript, &test->execution, "job-1", 2, "boot-123", cpu, bound) &&
        tp_retirement_transcript_begin_shard(&test->transcript, test->stream) &&
        tp_retirement_samples_init(&test->samples, &test->transcript, test->spool, &test->layout, test->rows,
            test->groups, test->members) &&
        tp_retirement_metrics_shards_init(metrics, tag, *metrics_stream) &&
        tp_retirement_samples_attach_metrics(&test->samples, metrics);
    return ok;
}

static int test_unit_campaign_open(TestCampaignStages* stages, int cpu, uint64_t bound)
{
    int ok = test_unit_campaign_open_stage(&stages->aa, cpu, bound, &stages->metrics[0], &stages->metrics_streams[0],
            "aa") &&
        test_unit_campaign_open_stage(&stages->ab, cpu, bound, &stages->metrics[1], &stages->metrics_streams[1], "ab");
    return ok;
}

/* Remove what a failed scenario kept as evidence, so the next one starts on
 * an empty scratch directory (besides its markers and response file). */
static void test_unit_campaign_scrub(int cwd, int code)
{
    static char const* const leaves[] = {BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, "untimed-left.bin", "untimed-right.bin", "artifact-left.bin", "artifact-right.bin", "alpha.o", "beta.o",
        "batch.metrics"};
    static char const* const codes[] = {"code-3-0.o", "code-3-1.o"};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(leaves); ++i) unlinkat(cwd, leaves[i], 0);
    for (unsigned i = 0; code >= 3 && i < BUSTER_ARRAY_LENGTH(codes); ++i) unlinkat(code, codes[i], 0);
    /* Each stage's retained runtime programs. */
    static char const* const programs[] = {"program-0-6-0", "program-1-6-0", "program-1-6-1"};
    for (unsigned i = 0; code >= 3 && i < BUSTER_ARRAY_LENGTH(programs); ++i) unlinkat(code, programs[i], 0);
    /* A failed runtime launch keeps its program's step directory. */
    int listed = fcntl(cwd, F_DUPFD_CLOEXEC, 3);
    DIR* listing = listed >= 0 ? fdopendir(listed) : NULL;
    if (!listing && listed >= 0) close(listed);
    /* The duplicate shares the directory offset: start from the first entry. */
    if (listing) rewinddir(listing);
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
    {
        int step = !strncmp(entry->d_name, "runtime-", 8) ?
            openat(cwd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        DIR* inner = step >= 0 ? fdopendir(step) : NULL;
        if (!inner && step >= 0) close(step);
        for (struct dirent* file = inner ? readdir(inner) : NULL; file; file = readdir(inner))
            if (strcmp(file->d_name, ".") && strcmp(file->d_name, "..")) unlinkat(dirfd(inner), file->d_name, 0);
        if (inner)
        {
            closedir(inner);
            unlinkat(cwd, entry->d_name, AT_REMOVEDIR);
        }
    }
    if (listing) closedir(listing);
}

/* The pinned rows in memory: rows 0-2 never compiled, row 3 a cross-target
 * link row (the untimed singleton group), rows 4 and 5 the native object
 * group (the batch's member fixtures), row 6 the native link row with
 * runtime. Its family: 5 aggregates, 7 slices for each of wall and peak
 * memory (two stages), 6 for runtime and for each batch metric (37
 * bootstrap members); 3 + 3 + 1 + 1 + 1 = 9 cells. */
static int test_unit_campaign_population(TestUnitCampaign* fixture)
{
    static char const* const values[7][BQ_RETIREMENT_DOCUMENT_FIELDS] = {
        {"tests/skip0.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "unsupported", "none", "none", "none", "group", "object"},
        {"tests/skip1.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "unsupported", "none", "none", "none", "group", "object"},
        {"tests/skip2.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "unsupported", "none", "none", "none", "group", "object"},
        {"tests/untimed.c", "aarch64-unknown-linux-gnu", "aapcs64", "baseline", "generic", "none", "direct-ssa", "0",
         "compiler-default", "supported", "semantic-gate-509", "none", "none", "group", "link"},
        {"tests/alpha.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "supported", "none", "none", "none", "group", "object"},
        {"tests/beta.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "supported", "none", "none", "none", "group", "object"},
        {"tests/link.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline", "generic", "none", "direct-ssa",
         "0", "compiler-default", "supported", "semantic-gate-509", "semantic-gate-509", "none", "group", "link"}};
    BqRetirementDocumentPopulation* population = &fixture->population;
    *population = (BqRetirementDocumentPopulation){fixture->population_rows, fixture->population_pool, 0,
        sizeof(fixture->population_pool), 7, BQ_RETIREMENT_NATIVE_TIMED_TARGET,
        "4444444444444444444444444444444444444444444444444444444444444444"};
    int ok = 1;
    for (unsigned row = 0; ok && row < 7; ++row)
    {
        BqRetirementDocumentRow* entry = &fixture->population_rows[row];
        for (unsigned field = 0; ok && field < BQ_RETIREMENT_DOCUMENT_FIELDS; ++field)
        {
            size_t length = strlen(values[row][field]);
            ok = length <= population->pool_capacity - population->pool_used;
            if (ok)
            {
                memcpy(population->pool + population->pool_used, values[row][field], length);
                entry->offset[field] = (uint32_t)population->pool_used;
                entry->length[field] = (uint32_t)length;
                population->pool_used += length;
            }
        }
        entry->compile = entry->code = row >= 3;
        entry->runtime = row == 6;
        entry->marker = row >= 3 ? 2u : 0u;
    }
    return ok;
}

/* A canonical #437 A/A admission receipt (the validator's AA_SCHEMA keys,
 * test identities) for `cpu` and `family`, and its digest. */
static int test_unit_campaign_aa_receipt(TestUnitCampaign* fixture, int cpu, char const* family, char const* admitted)
{
    int length = snprintf(fixture->aa_receipt, sizeof(fixture->aa_receipt),
        "{\"aa_decision\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_DECISION "\","
        "\"aa_policy_sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"admitted\":%s,\"baseline_source_commit\":\"%040d\",\"baseline_source_tree\":\"%040d\","
        "\"equivalence_band\":{\"lower\":\"0.98\",\"upper\":\"1.02\"},"
        "\"family_sha256\":\"%s\",\"lease_protocol\":\"server-authoritative-supervisor-lease-v1\","
        "\"logical_cpu\":%d,\"machine_id\":\"fixture-machine\",\"native_only\":true,"
        "\"native_target\":\"x86_64-unknown-linux-gnu\","
        "\"phase_receipt_sha256\":\"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\","
        "\"profile_id\":\"fixture-profile\",\"profile_version\":1,"
        "\"schema\":\"" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_SCHEMA "\",\"service_id\":\"fixture-service\","
        "\"version\":" BQ_RETIREMENT_UNIT_CAMPAIGN_AA_VERSION "}",
        admitted, 1, 2, family, cpu);
    int ok = length > 0 && (size_t)length < sizeof(fixture->aa_receipt);
    if (ok)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, fixture->aa_receipt, (u64)length);
        sha256_finish_hex(&hash, fixture->aa_receipt_sha);
    }
    return ok;
}

/* The campaign test's commands, gate and held pair, the untimed singleton
 * group's four batches, and the test profile's pins. */
static int test_unit_campaign_setup(TestUnitCampaign* fixture, char const* executable_path, char const* root)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->binary = fixture->candidate = fixture->other = fixture->cwd = fixture->code = fixture->leak = -1;
    fixture->sources[0] = fixture->sources[1] = -1;
    int ok = tp_path(fixture->directory, root, "unit-campaign-fixture") && tp_mkdirs(fixture->directory) &&
        chmod(fixture->directory, 0700) == 0 && test_text(fixture->directory, "cwd-marker", "fixed cwd\n") &&
        tp_path(fixture->binary_path, fixture->directory, "fixture-child") &&
        tp_copy_file(executable_path, fixture->binary_path) &&
        tp_path(fixture->candidate_path, fixture->directory, "fixture-candidate-child") &&
        tp_copy_file(executable_path, fixture->candidate_path) && chmod(fixture->binary_path, 0500) == 0 &&
        chmod(fixture->candidate_path, 0500) == 0 &&
        tp_path(fixture->other_path, fixture->directory, "fixture-other-candidate-child") &&
        tp_copy_file(executable_path, fixture->other_path) && chmod(fixture->other_path, 0500) == 0 &&
        tp_path(fixture->code_path, fixture->directory, "unit-campaign-code") && tp_mkdirs(fixture->code_path) &&
        chmod(fixture->code_path, 0700) == 0 &&
        tp_path(fixture->sources_path[0], fixture->directory, "unit-campaign-base") &&
        tp_mkdirs(fixture->sources_path[0]) &&
        tp_path(fixture->sources_path[1], fixture->directory, "unit-campaign-candidate") &&
        tp_mkdirs(fixture->sources_path[1]) &&
        tp_path(fixture->program_path, fixture->sources_path[0], "program.bin") &&
        tp_copy_file(executable_path, fixture->program_path) && chmod(fixture->program_path, 0500) == 0;
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        fixture->sources[side] = open(fixture->sources_path[side], O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ok = fixture->sources[side] >= 3;
    }
    if (ok)
    {
        fixture->binary = open(fixture->binary_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fixture->candidate = open(fixture->candidate_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fixture->other = open(fixture->other_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        fixture->cwd = open(fixture->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        fixture->code = open(fixture->code_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        fixture->leak = fixture->binary >= 3 ? fcntl(fixture->binary, F_DUPFD, 512) : -1;
    }
    uint64_t binary_bytes = 0;
    ok = ok && fixture->binary >= 3 && fixture->candidate >= 3 && fixture->other >= 3 && fixture->cwd >= 3 &&
        fixture->code >= 3 && fixture->leak >= 3 &&
        tp_retirement_file_hash(fixture->binary, fixture->binary_sha, &binary_bytes) && binary_bytes;
    snprintf(fixture->leak_text, sizeof(fixture->leak_text), "%d", fixture->leak);
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, artifact, artifact_bytes); sha256_finish_hex(&hash, fixture->artifact_sha);
    /* The fixture compiler's artifact, parsed once: its code-section facts
     * are the gate's for every code-observed (compile-eligible) row. */
    int probe = ok ? openat(fixture->cwd, "code-probe.bin", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600) : -1;
    ok = ok && probe >= 3 && write(probe, artifact, artifact_bytes) == (ssize_t)artifact_bytes &&
        tp_retirement_code_observe(probe, probe, &fixture->code_side);
    if (probe >= 0) ok = close(probe) == 0 && unlinkat(fixture->cwd, "code-probe.bin", 0) == 0 && ok;
    /* The artifact the child writes for `zero-code`, and its output digest. */
    unsigned char zero[1024];
    memcpy(zero, artifact, artifact_bytes);
    test_artifact_put(zero, 136, 2, 8);
    char zero_artifact[65];
    sha256_init(&hash); sha256_add(&hash, zero, artifact_bytes); sha256_finish_hex(&hash, zero_artifact);
    char const* zero_objects[] = {zero_artifact};
    ok = ok && tp_retirement_batch_output_digest(zero_objects, 1, fixture->zero_output);
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, fixture->code_sha);
    memcpy(fixture->runtime_sha, fixture->code_sha, 65);
    char const* objects[] = {fixture->artifact_sha};
    /* The link row's compile writes the program the runtime step runs: the
     * fixture child itself (the same bytes as the held binary). */
    char const* program_objects[] = {fixture->binary_sha};
    ok = ok && tp_retirement_code_observe(fixture->binary, fixture->binary, &fixture->program_side) &&
        !strcmp(fixture->program_side.artifact_sha256, fixture->binary_sha) &&
        tp_retirement_batch_output_digest(program_objects, 1, fixture->program_output);
    fixture->budget = test_retirement_budget();
    ok = ok && tp_retirement_batch_output_digest(objects, 1, fixture->batch_output) &&
        tp_retirement_budget_digest(&fixture->budget, fixture->budget_sha);
    fixture->group_stages[0] = TP_RETIREMENT_BUDGET_STAGE_OBJECT;
    fixture->group_stages[1] = TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->untimed_inputs[0] = 1;
    fixture->untimed_kinds[0] = TP_RETIREMENT_GROUP_SINGLETON;
    fixture->untimed_stages[0] = TP_RETIREMENT_BUDGET_STAGE_LINK;
    fixture->review = (TpRetirementCampaignReview){&fixture->budget, fixture->group_stages, fixture->untimed_inputs,
        fixture->untimed_kinds, fixture->untimed_stages, 2, 1};
    char* words[] = {"+alpha", "+beta", "-control"};
    ok = ok && test_batch_contract(&fixture->batch, "batch.metrics", words, 3, fixture->artifact_sha);
    fixture->batch.inputs[0].row = 4;
    fixture->batch.inputs[1].row = 5;
    ok = ok && tp_retirement_budget_metrics_bytes(&fixture->budget, 3, &fixture->batch.contract.metrics_bytes_max) &&
        tp_retirement_batch_contract_valid(&fixture->batch.contract) &&
        tp_retirement_batch_contract_output(&fixture->batch.contract, fixture->object_output) &&
        tp_retirement_batch_input_list_write(fixture->cwd, &fixture->batch.contract, fixture->list_leaf);
    snprintf(fixture->list_argument, sizeof(fixture->list_argument), "@%s", fixture->list_leaf);
    fixture->environment[0] = "LC_ALL=C";
    fixture->environment[1] = "TP_RETIREMENT_TEST=explicit";
    fixture->label_environment[0] = "LC_ALL=C";
    fixture->label_environment[1] = "TP_RETIREMENT_LABEL=2";
    fixture->label_environment[2] = "TP_RETIREMENT_TEST=explicit";
    /* The row plan templates' address-space bound; unlimited under the
     * sanitizers, whose shadow memory needs the whole address space. */
#if BUSTER_SANITIZE
    unsigned memory_mib = 0;
#else
    unsigned memory_mib = 8192;
#endif
    /* Per stage: [object group baseline, candidate, link group baseline,
     * candidate, runtime row baseline, candidate]; G = 2, U = 1. Every
     * command is in lane B's canonical layout, cwd the work slot: a compiler
     * command runs the side's binary slot (both A/A labels run the baseline,
     * slot 3), the link compile writing the program (`program`), and a
     * runtime command is lane B's `./{{output}}` shape over that artifact. */
    static char const* const programs[2] = {"./artifact-left.bin", "./artifact-right.bin"};
    for (unsigned stage = 0; ok && stage < 2; ++stage)
        for (unsigned slot = 0; ok && slot < 3; ++slot)
            for (unsigned variant = 0; ok && variant < 2; ++variant)
            {
                unsigned index = slot * 2 + variant;
                char** argv = fixture->arguments[stage][index];
                argv[0] = slot == 2 ? (char*)programs[variant] : stage && variant ? "/proc/self/fd/4" : "/proc/self/fd/3";
                argv[1] = "retirement-child";
                unsigned count = 6;
                if (!slot)
                {
                    char* batch_argv[] = {"batch", "batch.metrics", "ok", fixture->leak_text, fixture->list_argument,
                        "+alpha", "+beta", "-control"};
                    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(batch_argv); ++i) argv[2 + i] = batch_argv[i];
                    count = 10;
                }
                else
                {
                    argv[2] = slot == 2 ? "runtime" : "compiler";
                    argv[3] = variant ? "artifact-right.bin" : "artifact-left.bin";
                    argv[4] = slot == 1 ? "program" : "ok";
                    argv[5] = fixture->leak_text;
                }
                fixture->commands[stage][index] = (TpRetirementMeasuredCommand){.unit = slot == 2 ? 6 : slot,
                    .kind = slot == 2, .variant = variant, .arguments = argv, .argument_count = count,
                    .environment = !stage && variant ? fixture->label_environment : fixture->environment,
                    .environment_count = !stage && variant ? 3 : 2, .directory = BQ_RETIREMENT_ROW_WORK_PATH,
                    .artifact = slot == 1 ? argv[3] : NULL, .batch = slot ? NULL : &fixture->batch.contract,
                    .timeout_seconds = 30, .command_sha256 = fixture->command_sha[stage][index],
                    .output_sha256 = slot == 2 ? fixture->runtime_sha : slot ? fixture->program_output :
                                                                              fixture->object_output,
                    .exit_status = slot ? 0 : 1, .memory_mib = memory_mib};
                ok = tp_retirement_command_hash(&fixture->commands[stage][index], fixture->command_sha[stage][index]);
            }
    /* The untimed singleton group 0: a production and a reproduction batch
     * per variant, each writing its own artifact leaf. */
    for (unsigned variant = 0; ok && variant < 2; ++variant)
    {
        char** argv = fixture->untimed_arguments[variant];
        argv[0] = variant ? "/proc/self/fd/4" : "/proc/self/fd/3";
        argv[1] = "retirement-child";
        argv[2] = "compiler";
        argv[3] = variant ? "untimed-right.bin" : "untimed-left.bin";
        argv[4] = "ok";
        argv[5] = fixture->leak_text;
        TpRetirementMeasuredCommand command = {.unit = 0, .kind = 0, .variant = variant, .arguments = argv,
            .argument_count = 6, .environment = fixture->environment, .environment_count = 2,
            .directory = BQ_RETIREMENT_ROW_WORK_PATH, .artifact = argv[3], .timeout_seconds = 30,
            .command_sha256 = fixture->untimed_sha[variant], .output_sha256 = fixture->batch_output,
            .memory_mib = memory_mib};
        ok = tp_retirement_command_hash(&command, fixture->untimed_sha[variant]);
        for (unsigned purpose = 0; ok && purpose < 2; ++purpose)
            fixture->untimed[variant * 2 + purpose] = (TpRetirementUntimedBatch){command, 0, variant, purpose,
                TP_RETIREMENT_GROUP_SINGLETON};
        /* The same batch whose child sleeps 5 s, for mid-launch cancellation. */
        char** slow = fixture->slow_arguments[variant];
        memcpy(slow, argv, sizeof(fixture->slow_arguments[variant]));
        slow[4] = "timeout";
        TpRetirementMeasuredCommand sleeping = command;
        sleeping.arguments = slow;
        sleeping.timeout_seconds = 10;
        sleeping.command_sha256 = fixture->slow_sha[variant];
        ok = ok && tp_retirement_command_hash(&sleeping, fixture->slow_sha[variant]);
        for (unsigned purpose = 0; ok && purpose < 2; ++purpose)
            fixture->slow[variant * 2 + purpose] = (TpRetirementUntimedBatch){sleeping, 0, variant, purpose,
                TP_RETIREMENT_GROUP_SINGLETON};
        /* The same batch whose child writes 1.5 MiB of output and fails. */
        char** noisy = fixture->noisy_arguments[variant];
        memcpy(noisy, argv, sizeof(fixture->noisy_arguments[variant]));
        noisy[4] = "noisy";
        TpRetirementMeasuredCommand talking = command;
        talking.arguments = noisy;
        talking.command_sha256 = fixture->noisy_sha[variant];
        ok = ok && tp_retirement_command_hash(&talking, fixture->noisy_sha[variant]);
        for (unsigned purpose = 0; ok && purpose < 2; ++purpose)
            fixture->noisy[variant * 2 + purpose] = (TpRetirementUntimedBatch){talking, 0, variant, purpose,
                TP_RETIREMENT_GROUP_SINGLETON};
    }
    /* A reproduction that writes other (valid) bytes than its production. */
    memcpy(fixture->different, fixture->untimed, sizeof(fixture->different));
    memcpy(fixture->zero_arguments, fixture->untimed_arguments[0], sizeof(fixture->zero_arguments));
    fixture->zero_arguments[4] = "zero-code";
    fixture->different[1].command.arguments = fixture->zero_arguments;
    fixture->different[1].command.output_sha256 = fixture->zero_output;
    fixture->different[1].command.command_sha256 = fixture->zero_sha;
    ok = ok && tp_retirement_command_hash(&fixture->different[1].command, fixture->zero_sha);
    /* The untimed singleton group's row: row 3, a cross-target
     * compiler-eligible code row, which the timed projection never counts. */
    fixture->untimed_rows[0] = 3;
    /* The sealed gate: rows 4 and 5 in the frozen object group, row 6 the
     * native link singleton with runtime; the #509 batch authority is the
     * test stand-in, as in retirement_campaign_test.h. */
    static char const identity[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static char const batch_key[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    BqRetirementCorrectness* gate = &fixture->gate;
    gate->prepared.rows = gate->rows_done = 7;
    gate->prepared.native_target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    gate->eligible_rows = 3;
    gate->trusted_rows = fixture->trusted;
    gate->facts = fixture->facts;
    memcpy(gate->prepared.preparation_sha256, identity, 65);
    memcpy(gate->prepared.source_sha256[0], identity, 65);
    memcpy(gate->prepared.source_sha256[1], identity, 65);
    memcpy(gate->prepared.binary_sha256[0], fixture->binary_sha, 65);
    memcpy(gate->prepared.binary_sha256[1], fixture->binary_sha, 65);
    for (unsigned row = 0; row < 7; ++row) fixture->trusted[row].row = fixture->facts[row].row = row;
    /* Each row's sealed identity is its pinned identity's canonical digest. */
    ok = ok && test_unit_campaign_population(fixture);
    for (unsigned row = 0; ok && row < 7; ++row)
        ok = bq_retirement_document_identity(&fixture->population, row, fixture->trusted[row].identity_sha256);
    for (unsigned row = 4; ok && row < 6; ++row)
    {
        fixture->trusted[row].compiler_eligible = fixture->trusted[row].code_obligation = 1;
        fixture->trusted[row].stage = BQ_RETIREMENT_STAGE_OBJECT;
        fixture->trusted[row].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
        memcpy(fixture->trusted[row].batch_key_sha256, batch_key, sizeof(batch_key));
        fixture->facts[row].compiler_eligible = fixture->facts[row].code_eligible = 1;
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            memcpy(fixture->trusted[row].compiler_command_sha256[variant], fixture->command_sha[1][variant], 65);
            memcpy(fixture->facts[row].side[variant].compiler_command_sha256, fixture->command_sha[1][variant], 65);
            memcpy(fixture->facts[row].side[variant].artifact_sha256, fixture->artifact_sha, 65);
            memcpy(fixture->facts[row].side[variant].code_sha256, fixture->code_side.code_sha256, 65);
            fixture->facts[row].side[variant].code_bytes = fixture->code_side.code_bytes;
        }
    }
    fixture->trusted[3].compiler_eligible = fixture->trusted[3].code_obligation = 1;
    fixture->trusted[3].stage = BQ_RETIREMENT_STAGE_LINK;
    fixture->trusted[3].target = 2;
    fixture->facts[3].compiler_eligible = fixture->facts[3].code_eligible = 1;
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        BqRetirementObservedSide* side = &fixture->facts[3].side[variant];
        memcpy(side->artifact_sha256, fixture->artifact_sha, 65);
        memcpy(side->code_sha256, fixture->code_side.code_sha256, 65);
        side->code_bytes = fixture->code_side.code_bytes;
    }
    fixture->trusted[6].compiler_eligible = fixture->trusted[6].code_obligation = 1;
    fixture->trusted[6].stage = BQ_RETIREMENT_STAGE_LINK;
    fixture->trusted[6].target = BQ_RETIREMENT_NATIVE_TIMED_TARGET;
    fixture->facts[6].runtime_eligible = fixture->facts[6].code_eligible = 1;
    memcpy(fixture->trusted[6].independent_oracle_sha256, fixture->runtime_sha, 65);
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        BqRetirementObservedSide* side = &fixture->facts[6].side[variant];
        memcpy(side->compiler_command_sha256, fixture->command_sha[1][2 + variant], 65);
        memcpy(fixture->trusted[6].compiler_command_sha256[variant], fixture->command_sha[1][2 + variant], 65);
        memcpy(side->artifact_sha256, fixture->program_side.artifact_sha256, 65);
        memcpy(side->code_sha256, fixture->program_side.code_sha256, 65);
        memcpy(side->runtime_command_sha256, fixture->command_sha[1][4 + variant], 65);
        memcpy(fixture->trusted[6].runtime_command_sha256[variant], fixture->command_sha[1][4 + variant], 65);
        memcpy(side->runtime_output_sha256, fixture->runtime_sha, 65);
        side->code_bytes = fixture->program_side.code_bytes;
    }
    fixture->frozen = (BqRetirementBatchGroup){{fixture->batch.contract, fixture->batch.contract}, {{0}}};
    memcpy(fixture->frozen.command_sha256[0], fixture->command_sha[1][0], 65);
    memcpy(fixture->frozen.command_sha256[1], fixture->command_sha[1][1], 65);
    ok = ok && bq_retirement_correctness_batches(gate, &fixture->frozen, 1, fixture->assigned, 7);
    gate->finished = 1;
    gate->batch_authority = 1;
    sha256_init(&hash);
    static char const second_domain[] = BQ_RETIREMENT_AA_SECOND_COMMANDS_DOMAIN;
    sha256_add(&hash, second_domain, sizeof(second_domain) - 1);
    uint8_t object_ordinal[4] = {4, 0, 0, 0}, link_ordinal[4] = {6, 0, 0, 0}, no_runtime = 0, applicable_runtime = 1;
    sha256_add(&hash, object_ordinal, sizeof(object_ordinal));
    sha256_add(&hash, fixture->command_sha[0][1], 64);
    sha256_add(&hash, &no_runtime, sizeof(no_runtime));
    sha256_add(&hash, link_ordinal, sizeof(link_ordinal));
    sha256_add(&hash, fixture->command_sha[0][3], 64);
    sha256_add(&hash, &applicable_runtime, sizeof(applicable_runtime));
    sha256_add(&hash, fixture->command_sha[0][5], 64);
    sha256_finish_hex(&hash, gate->prepared.aa_second_commands_sha256);
    bq_retirement_correctness_seal(gate, gate->sealed_sha256);
    ok = ok && bq_retirement_correctness_ready(gate);
    fixture->held = (BqRetirementHeldBinaries){.descriptors = {fixture->binary, fixture->candidate}, .owned = 1};
    memcpy(fixture->held.verified.preparation_sha256, identity, 65);
    memcpy(fixture->held.verified.directory_identity_sha256, identity, 65);
    memcpy(fixture->held.verified.source_sha256, gate->prepared.source_sha256, sizeof(fixture->held.verified.source_sha256));
    memcpy(fixture->held.verified.binary_sha256, gate->prepared.binary_sha256, sizeof(fixture->held.verified.binary_sha256));
    ok = ok && bq_retirement_campaign_descriptor_identity(fixture->binary, fixture->held.verified.binary_identity_sha256[0]) &&
        bq_retirement_campaign_descriptor_identity(fixture->candidate, fixture->held.verified.binary_identity_sha256[1]);
    int length = snprintf(fixture->profile, sizeof(fixture->profile),
        "schema=1\nrecipe=native-retirement-performance-v1\ncampaign-seed=1\ncampaign-pairs=60\n"
        "campaign-resamples=100000\ncampaign-bootstrap-members=37\ncampaign-budget-sha256=%s\n"
        "support-declaration-sha256=1111111111111111111111111111111111111111111111111111111111111111\n"
        "census-manifest-sha256=2222222222222222222222222222222222222222222222222222222222222222\n"
        "census-rows-sha256=3333333333333333333333333333333333333333333333333333333333333333\n"
        "performance-rows-sha256=4444444444444444444444444444444444444444444444444444444444444444\n",
        fixture->budget_sha);
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->profile) &&
        bq_retirement_unit_campaign_pins(string_from_pointer(fixture->profile), &fixture->pins);
    /* A ready record stand-in for job 1, attempt 2; the real import runs in
     * the preparation runner. */
    fixture->ready = (BqRetirementCampaignReady){.job_id = 1, .attempt_token = 2, .owned = 1};
    memcpy(fixture->ready.ready_sha256, test_unit_campaign_ready, 65);
    fixture->cpu = tp_first_allowed_cpu();
    /* The family the documents derive from the pinned rows. */
    BqRetirementDocumentPartition timed = {0};
    ok = ok && bq_retirement_documents_partition(&fixture->population, 0, &timed) &&
        bq_retirement_documents_family(&fixture->population, &timed, &fixture->family) &&
        fixture->family.bootstrap_members == 37 && fixture->family.cell_members == 9 &&
        tp_path(fixture->documents_path, fixture->directory, "unit-campaign-documents") &&
        test_unit_campaign_aa_receipt(fixture, fixture->cpu, fixture->family.sha256, "true");
    bq_retirement_documents_partition_release(&timed);
    return ok && fixture->cpu >= 0;
}

static void test_unit_campaign_teardown(TestUnitCampaign* fixture)
{
    if (fixture->cwd >= 3)
    {
        test_unit_campaign_scrub(fixture->cwd, fixture->code);
        if (fixture->list_leaf[0]) CHECK(unlinkat(fixture->cwd, fixture->list_leaf, 0) == 0);
        CHECK(close(fixture->cwd) == 0);
    }
    if (fixture->code >= 3) CHECK(close(fixture->code) == 0);
    if (fixture->program_path[0]) unlink(fixture->program_path);
    for (unsigned side = 0; side < 2; ++side)
    {
        if (fixture->sources[side] >= 3) CHECK(close(fixture->sources[side]) == 0);
        if (fixture->sources_path[side][0]) rmdir(fixture->sources_path[side]);
    }
    if (fixture->other >= 3) CHECK(close(fixture->other) == 0);
    if (fixture->leak >= 3) CHECK(close(fixture->leak) == 0);
    if (fixture->candidate >= 3) CHECK(close(fixture->candidate) == 0);
    if (fixture->binary >= 3) CHECK(close(fixture->binary) == 0);
}

/* Plan and context: pins from the profile only (the blocked profile has
 * none), the cell count derived from the gate, a candidate-independent plan
 * digest, and a pre-sample context bound to the subjects. */
static void test_unit_campaign_plan(TestUnitCampaign* fixture)
{
    BqRetirementUnitCampaignPins pins;
    CHECK(fixture->pins.seed == 1 && fixture->pins.pairs == 60 && fixture->pins.resamples == 100000 &&
          fixture->pins.bootstrap_members == 37 && !strcmp(fixture->pins.budget_sha256, fixture->budget_sha));
    /* Missing, duplicate, non-canonical or out-of-range pins fail closed. */
    char const* const edits[][2] = {
        {"campaign-seed=1\n", ""}, {"campaign-pairs=60\n", ""}, {"campaign-resamples=100000\n", ""},
        {"campaign-bootstrap-members=37\n", ""}, {"campaign-budget-sha256=", "campaign-budget=",},
        {"campaign-seed=1\n", "campaign-seed=1\ncampaign-seed=1\n"}, {"campaign-seed=1\n", "campaign-seed=0\n"},
        {"campaign-seed=1\n", "campaign-seed=01\n"}, {"campaign-seed=1\n", "campaign-seed=18446744073709551616\n"},
        {"campaign-pairs=60\n", "campaign-pairs=61\n"}, {"campaign-pairs=60\n", "campaign-pairs=58\n"},
        {"campaign-pairs=60\n", "campaign-pairs=256\n"}, {"campaign-pairs=60\n", "campaign-pairs=+60\n"},
        {"campaign-resamples=100000\n", "campaign-resamples=99999\n"},
        {"campaign-resamples=100000\n", "campaign-resamples=1000001\n"},
        {"campaign-bootstrap-members=37\n", "campaign-bootstrap-members=0\n"},
        {"campaign-bootstrap-members=37\n", "campaign-bootstrap-members=81\n"}};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(edits); ++i)
    {
        char mutated[1100];
        char const* at = strstr(fixture->profile, edits[i][0]);
        CHECK(at != NULL);
        if (!at) continue;
        size_t prefix = (size_t)(at - fixture->profile), old_length = strlen(edits[i][0]);
        int length = snprintf(mutated, sizeof(mutated), "%.*s%s%s", (int)prefix, fixture->profile, edits[i][1],
            at + old_length);
        CHECK(length > 0 && !bq_retirement_unit_campaign_pins(string_from_pointer(mutated), &pins) && !pins.seed &&
              !pins.pairs);
    }
    CHECK(!bq_retirement_unit_campaign_pins(S8("schema=1\nrecipe=native-retirement-performance-v1\nstatus=blocked\n"),
                                            &pins));
    CHECK(bq_retirement_unit_campaign_pins(string_from_pointer(fixture->profile), &pins) &&
          !memcmp(&pins, &fixture->pins, sizeof(pins)));
    pins.pairs = 254;
    TpRetirementPlan plan, other;
    char digest[65], changed[65];
    /* Exact cells: 2 * 3 timed rows + 1 runtime row + 2 * 1 object group;
     * cross-target compiler-eligible row 3 is never timed, so never a cell. */
    CHECK(bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &plan) && plan.seed == 1 &&
          plan.pairs_per_round == 60 && plan.resamples == 100000 && plan.bootstrap_members_per_scope == 37 &&
          plan.cell_members_per_scope == 9 && plan.frozen_before_samples == 1 &&
          plan.version == TP_RETIREMENT_STATISTICS_VERSION);
    CHECK(bq_retirement_unit_campaign_plan(&fixture->gate, &pins, &other) && other.pairs_per_round == 254);
    /* A metric without cells (here no runtime-eligible timed row) has no
     * family, as the validator's _derive_statistical_family refuses. */
    fixture->facts[6].runtime_eligible = 0;
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&fixture->gate) &&
          !bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &other) && !other.seed);
    fixture->facts[6].runtime_eligible = 1;
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_plan(&fixture->gate, &pins, &other) && other.pairs_per_round == 254);
    /* Lane E reads an object group's members as its batches' first inputs in
     * ascending row order: members out of order, or after a control input,
     * derive no plan. */
    TpRetirementBatchInput* inputs = (TpRetirementBatchInput*)fixture->batch.contract.inputs;
    for (unsigned trial = 0; trial < 2; ++trial)
    {
        TpRetirementBatchInput saved_inputs[3];
        memcpy(saved_inputs, inputs, sizeof(saved_inputs));
        if (!trial)
        {
            inputs[0].row = 5;
            inputs[1].row = 4;
        }
        else
        {
            inputs[1] = saved_inputs[2];
            inputs[2] = saved_inputs[1];
        }
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
        CHECK(!bq_retirement_unit_campaign_members_first(&fixture->gate) &&
              !bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &other) && !other.seed);
        memcpy(inputs, saved_inputs, sizeof(saved_inputs));
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    CHECK(bq_retirement_unit_campaign_members_first(&fixture->gate) &&
          bq_retirement_unit_campaign_plan(&fixture->gate, &pins, &other) && other.pairs_per_round == 254);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, digest) &&
          bq_retirement_unit_campaign_plan_digest(&fixture->gate, &other, changed) && strcmp(digest, changed));
    /* A changed candidate binary reseals the gate but leaves the plan digest;
     * a changed #508 row identity changes it; an unready gate derives none. */
    char saved = fixture->gate.prepared.binary_sha256[1][0];
    fixture->gate.prepared.binary_sha256[1][0] = saved == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && !strcmp(digest, changed));
    fixture->gate.prepared.binary_sha256[1][0] = saved;
    char saved_identity = fixture->trusted[5].identity_sha256[0];
    fixture->trusted[5].identity_sha256[0] = saved_identity == '1' ? '2' : '1';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && strcmp(digest, changed));
    fixture->trusted[5].identity_sha256[0] = 0;
    CHECK(!bq_retirement_unit_campaign_plan(&fixture->gate, &fixture->pins, &other) && !other.seed &&
          !bq_retirement_unit_campaign_plan_digest(&fixture->gate, &plan, changed) && !changed[0]);
    fixture->trusted[5].identity_sha256[0] = saved_identity;
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_correctness_ready(&fixture->gate));
    /* The pre-sample context binds the subjects the plan does not. */
    TpRetirementShard untimed = {100, 4, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    char commands[65], context[65], context_changed[65];
    CHECK(bq_retirement_unit_campaign_commands_measured(fixture->commands[0], fixture->commands[1], 6, commands));
    BqRetirementUnitCampaignFacts facts = {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed,
        "job-1", "boot-123", 2, 1000, 0};
    CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context));
    /* Exactly these facts, in this order: each subject (the candidate binary
     * too, although the gate seal also covers it), the frozen commands, the
     * untimed stream and the host and transcript identity. */
    {
        Sha256 hash;
        char expected[65];
        sha256_init(&hash);
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_DOMAIN;
        sha256_add(&hash, domain, sizeof(domain) - 1);
        BqRetirementPrepared const* prepared = &fixture->gate.prepared;
        bq_retirement_unit_campaign_text(&hash, "plan", digest);
        bq_retirement_unit_campaign_text(&hash, "ready", test_unit_campaign_ready);
        bq_retirement_unit_campaign_text(&hash, "gate", fixture->gate.sealed_sha256);
        bq_retirement_unit_campaign_text(&hash, "preparation", prepared->preparation_sha256);
        bq_retirement_unit_campaign_text(&hash, "source-base", prepared->source_sha256[0]);
        bq_retirement_unit_campaign_text(&hash, "source-candidate", prepared->source_sha256[1]);
        bq_retirement_unit_campaign_text(&hash, "binary-base", prepared->binary_sha256[0]);
        bq_retirement_unit_campaign_text(&hash, "binary-candidate", prepared->binary_sha256[1]);
        bq_retirement_unit_campaign_text(&hash, "budget", fixture->budget_sha);
        bq_retirement_unit_campaign_text(&hash, "commands", commands);
        bq_retirement_unit_campaign_text(&hash, "untimed", untimed.sha256);
        bq_retirement_unit_campaign_number(&hash, "untimed-records", untimed.records);
        bq_retirement_unit_campaign_number(&hash, "untimed-bytes", untimed.bytes);
        bq_retirement_unit_campaign_text(&hash, "job", "job-1");
        bq_retirement_unit_campaign_number(&hash, "attempt", 2);
        bq_retirement_unit_campaign_text(&hash, "boot", "boot-123");
        bq_retirement_unit_campaign_number(&hash, "cpu", 0);
        bq_retirement_unit_campaign_number(&hash, "bound-at", 1000);
        sha256_finish_hex(&hash, expected);
        CHECK(!strcmp(expected, context));
    }
    fixture->gate.prepared.binary_sha256[1][0] = saved == 'e' ? 'f' : 'e';
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context_changed) &&
          strcmp(context, context_changed));
    fixture->gate.prepared.binary_sha256[1][0] = saved;
    bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    BqRetirementUnitCampaignFacts const variants[] = {
        {test_unit_campaign_receipt, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1",
         "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_receipt, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-2", "boot-123", 2, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 3, 1000, 0},
        {digest, test_unit_campaign_ready, fixture->budget_sha, commands, &untimed, "job-1", "boot-123", 2, 1001, 0}};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(variants); ++i)
        CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &variants[i], context_changed) &&
              strcmp(context, context_changed));
    TpRetirementShard other_untimed = untimed;
    other_untimed.sha256[0] = 'e';
    facts.untimed = &other_untimed;
    CHECK(bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context_changed) &&
          strcmp(context, context_changed));
    facts.untimed = &untimed;
    TpRetirementShard no_untimed = {0, 0, ""};
    facts.untimed = &no_untimed;
    CHECK(!bq_retirement_unit_campaign_pre_context(&fixture->gate, &facts, context_changed) && !context_changed[0]);
    /* A changed command changes the commands digest. */
    char* binary_slot = fixture->arguments[1][1][0];
    fixture->arguments[1][1][0] = "/proc/self/fd/9";
    CHECK(tp_retirement_command_hash(&fixture->commands[1][1], fixture->command_sha[1][1]) &&
          bq_retirement_unit_campaign_commands_measured(fixture->commands[0], fixture->commands[1], 6, changed) &&
          strcmp(changed, commands));
    fixture->arguments[1][1][0] = binary_slot;
    CHECK(tp_retirement_command_hash(&fixture->commands[1][1], fixture->command_sha[1][1]));
}

enum
{
    TEST_UNIT_CAMPAIGN_COMPLETE, TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE, TEST_UNIT_CAMPAIGN_SWAPPED_HELD,
    TEST_UNIT_CAMPAIGN_UNTIMED_OUTPUT, TEST_UNIT_CAMPAIGN_UNTIMED_CANCELLED, TEST_UNIT_CAMPAIGN_UNTIMED_DEADLINE,
    TEST_UNIT_CAMPAIGN_MEASURING_ACK, TEST_UNIT_CAMPAIGN_LATE_BINDING, TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE,
    TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE, TEST_UNIT_CAMPAIGN_UNMARKED, TEST_UNIT_CAMPAIGN_FAMILY,
    TEST_UNIT_CAMPAIGN_OTHER_HELD, TEST_UNIT_CAMPAIGN_UNPLANNED_STORE, TEST_UNIT_CAMPAIGN_SHORT_STORE,
    TEST_UNIT_CAMPAIGN_EARLY_FREEZE, TEST_UNIT_CAMPAIGN_CANCELLED, TEST_UNIT_CAMPAIGN_AB_WITHOUT_ADMISSION,
    TEST_UNIT_CAMPAIGN_DENIED, TEST_UNIT_CAMPAIGN_STALE_ADMISSION, TEST_UNIT_CAMPAIGN_POST_AA_ADMISSION,
    TEST_UNIT_CAMPAIGN_AB_WITHOUT_FREEZE, TEST_UNIT_CAMPAIGN_FREEZE_AFTER_LAUNCH, TEST_UNIT_CAMPAIGN_BAD_HANDOFF,
    TEST_UNIT_CAMPAIGN_UNTIMED_REPRODUCTION, TEST_UNIT_CAMPAIGN_UNTIMED_NOISY, TEST_UNIT_CAMPAIGN_UNTIMED_FACT,
    TEST_UNIT_CAMPAIGN_TIMED_FACT, TEST_UNIT_CAMPAIGN_BOOTSTRAP, TEST_UNIT_CAMPAIGN_GATE_COPY,
    TEST_UNIT_CAMPAIGN_NO_DOCUMENTS, TEST_UNIT_CAMPAIGN_DOCUMENT_IDENTITY, TEST_UNIT_CAMPAIGN_DOCUMENT_FAMILY,
    TEST_UNIT_CAMPAIGN_DOCUMENT_EXISTS, TEST_UNIT_CAMPAIGN_DOCUMENT_PIN, TEST_UNIT_CAMPAIGN_NO_POST_DOCUMENT,
    TEST_UNIT_CAMPAIGN_POST_DOCUMENT_PIN, TEST_UNIT_CAMPAIGN_RECEIPT_DIGEST, TEST_UNIT_CAMPAIGN_RECEIPT_FAMILY,
    TEST_UNIT_CAMPAIGN_RECEIPT_CPU, TEST_UNIT_CAMPAIGN_RECEIPT_REFUSED, TEST_UNIT_CAMPAIGN_POST_DOCUMENT_SUPPORT,
    TEST_UNIT_CAMPAIGN_UNTIMED_NO_ARTIFACT,
    TEST_UNIT_CAMPAIGN_SCENARIOS
};

/* The untimed step with the scenario's inputs; checks the retained failure
 * of each refused variant. */
static int test_unit_campaign_untimed(TestUnitCampaign* fixture, BqRetirementUnitCampaign* driver,
    TpRetirementUntimed* untimed, unsigned scenario, int cancel_writer)
{
    BqRetirementUnitCampaignStreams none = {0};
    BqRetirementHeldBinaries held = fixture->held;
    /* The driver borrows the batches until its documents are written. */
    TpRetirementUntimedBatch* batches = fixture->running;
    memcpy(batches, scenario == TEST_UNIT_CAMPAIGN_UNTIMED_CANCELLED ? fixture->slow :
                    scenario == TEST_UNIT_CAMPAIGN_UNTIMED_NOISY ? fixture->noisy :
                    scenario == TEST_UNIT_CAMPAIGN_UNTIMED_REPRODUCTION ? fixture->different : fixture->untimed,
           sizeof(fixture->running));
    /* A copy of the gate the attach must refuse (the bind uses the fixture's). */
    fixture->gate_copy = fixture->gate;
    BqRetirementCorrectness const* gate = scenario == TEST_UNIT_CAMPAIGN_GATE_COPY ? &fixture->gate_copy : &fixture->gate;
    BqRetirementUnitCampaignCode code = {fixture->untimed_rows, fixture->codes, BUSTER_ARRAY_LENGTH(fixture->codes),
        fixture->code};
    memset(fixture->codes, 0, sizeof(fixture->codes));
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE) fixture->untimed_inputs[0] = 2;
    if (scenario == TEST_UNIT_CAMPAIGN_SWAPPED_HELD)
    {
        held.descriptors[0] = fixture->candidate;
        held.descriptors[1] = fixture->binary;
    }
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_OUTPUT) batches[0].command.output_sha256 = fixture->runtime_sha;
    pid_t writer = -1;
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_CANCELLED)
    {
        writer = fork();
        if (!writer)
        {
            struct timespec pause = {0, 300000000};
            nanosleep(&pause, NULL);
            char byte = 1;
            _exit(write(cancel_writer, &byte, 1) == 1 ? 0 : 1);
        }
    }
    uint64_t started = tp_process_monotonic_ns();
    int ok = bq_retirement_unit_campaign_untimed(driver, untimed, batches, 4, &held, fixture->sources, gate,
                                                 &fixture->review, &none, &code);
    uint64_t elapsed = tp_process_monotonic_ns() - started;
    fixture->untimed_inputs[0] = 1;
    BqRetirementUnitCampaignFailure failure = bq_retirement_unit_campaign_failure(driver);
    struct stat kept;
    if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_SHAPE || scenario == TEST_UNIT_CAMPAIGN_SWAPPED_HELD)
        CHECK(!ok && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && !driver->launches[0] && untimed->failed &&
              failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED && !failure.launched);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_OUTPUT)
        /* The launched child's facts and its kept log and output. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched && !failure.stage &&
              !failure.group && !failure.variant && failure.purpose == TP_RETIREMENT_UNTIMED_PRODUCTION &&
              !failure.sequence && failure.status == TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID && !failure.exit_code &&
              !failure.signal_number && !failure.cancelled && !driver->launches[0] &&
              fstatat(fixture->cwd, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, &kept, AT_SYMLINK_NOFOLLOW) == 0 &&
              fstatat(fixture->cwd, "untimed-left.bin", &kept, AT_SYMLINK_NOFOLLOW) == 0);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_CANCELLED)
        /* The self-pipe became readable while the 5 s child ran: it was
         * killed at once, not at its timeout. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched &&
              failure.cancelled && failure.signal_number == SIGKILL && !failure.timed_out && !failure.group &&
              elapsed < UINT64_C(4000000000) && !driver->launches[0]);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_DEADLINE)
        /* A 30 s launch cannot finish before a deadline 1.5 s away. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_DEADLINE && !failure.launched &&
              !failure.stage && !failure.group && !failure.variant && !failure.purpose && !driver->launches[0]);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_REPRODUCTION)
        /* The reproduction finished with its own valid output, but other bytes
         * than the retained production object: refused after the launch,
         * with its launch facts and its kept log. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched &&
              failure.after == BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CODE && !failure.stage && !failure.variant &&
              failure.purpose == TP_RETIREMENT_UNTIMED_REPRODUCTION && failure.sequence == 1 &&
              failure.status == TP_RETIREMENT_MEASUREMENT_COMPLETE && !failure.exit_code &&
              driver->launches[0] == 1 && tp_retirement_digest(failure.log_sha256) &&
              !strcmp(failure.log_sha256, failure.retained_sha256) && failure.log_bytes == failure.retained_bytes &&
              fstatat(fixture->cwd, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, &kept, AT_SYMLINK_NOFOLLOW) == 0);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_NOISY)
        /* 1.5 MiB of output: the full size and digest are kept, the file is
         * capped at 1 MiB and its retained digest recorded. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched &&
              !failure.after && failure.exit_code == 7 && failure.log_bytes == UINT64_C(1572864) &&
              tp_retirement_digest(failure.log_sha256) &&
              failure.retained_bytes == BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX &&
              tp_retirement_digest(failure.retained_sha256) && strcmp(failure.log_sha256, failure.retained_sha256) &&
              fstatat(fixture->cwd, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, &kept, AT_SYMLINK_NOFOLLOW) == 0 &&
              (uint64_t)kept.st_size == BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX && !driver->launches[0]);
    else if (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_FACT || scenario == TEST_UNIT_CAMPAIGN_UNTIMED_NO_ARTIFACT)
        /* Every batch ran, but row 3's code size is not the gate's, or the
         * gate has no artifact digest for it to be. */
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED && driver->launches[0] == 4 &&
              driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
    else
    {
        /* Each untimed row's code facts: the retained production object,
         * parsed, and its byte-identical reproduction. */
        char digest[65];
        uint64_t bytes = 0;
        int frozen = openat(fixture->code, "code-3-1.o", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        CHECK(ok && driver->launches[0] == 4 && driver->untimed_records.records == 4 &&
              driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_UNTIMED && driver->code_count == 1 &&
              fixture->codes[0].row == 3 && frozen >= 3 && tp_retirement_file_hash(frozen, digest, &bytes) &&
              !strcmp(digest, fixture->artifact_sha) && tp_retirement_digest(driver->log_chain_sha256[0]) &&
              failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_NONE &&
              fstatat(fixture->cwd, "untimed-left.bin", &kept, AT_SYMLINK_NOFOLLOW) < 0);
        if (frozen >= 0) CHECK(close(frozen) == 0);
        for (unsigned side = 0; side < 2; ++side)
            CHECK(!strcmp(fixture->codes[0].sides[side].artifact_sha256, fixture->artifact_sha) &&
                  !strcmp(fixture->codes[0].sides[side].reproduction_sha256, fixture->artifact_sha) &&
                  fixture->codes[0].sides[side].code_bytes && tp_retirement_digest(fixture->codes[0].sides[side].code_sha256));
    }
    if (writer > 0)
    {
        int status = 0;
        CHECK(waitpid(writer, &status, 0) == writer && WIFEXITED(status) && !WEXITSTATUS(status));
    }
    return ok;
}

/* The pre-sample documents step after a successful attach, with the
 * scenario's fault; each refusal poisons the attempt before any A/A child. */
static int test_unit_campaign_documents(TestUnitCampaign* fixture, BqRetirementUnitCampaign* driver,
    BqRetirementUnitCampaignDocumentSources* sources, unsigned scenario, int ok)
{
    char const* const pin = "performance-rows-sha256=4444";
    char* at = strstr(fixture->profile, pin);
    uint32_t offset = fixture->population_rows[5].offset[BQ_RETIREMENT_DOCUMENT_CPU_FEATURES];
    /* A pinned row whose identity is not the sealed one; a family whose
     * bootstrap count is not the plan's (one row's CPU features differ, so
     * the object group splits); a document already at its path; a
     * performance-rows pin that is not the pinned rows' digest. */
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_IDENTITY) fixture->population_pool[offset] ^= 1;
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_FAMILY)
    {
        fixture->population_pool[offset] ^= 1;
        for (unsigned row = 0; row < 7; ++row)
            CHECK(bq_retirement_document_identity(&fixture->population, row, fixture->trusted[row].identity_sha256));
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_EXISTS)
    {
        int file = openat(sources->directory, bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE],
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        CHECK(file >= 3 && close(file) == 0);
    }
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_PIN && at) at[strlen(pin) - 1] = '5';
    /* Without the documents the A/A stage must refuse (checked there). */
    int written = scenario == TEST_UNIT_CAMPAIGN_NO_DOCUMENTS ? ok :
        ok && bq_retirement_unit_campaign_documents(driver, sources);
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_PIN && at) at[strlen(pin) - 1] = '4';
    if (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_IDENTITY || scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_FAMILY)
    {
        fixture->population_pool[offset] ^= 1;
        for (unsigned row = 0; row < 7; ++row)
            CHECK(bq_retirement_document_identity(&fixture->population, row, fixture->trusted[row].identity_sha256));
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    if (ok && (scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_IDENTITY || scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_FAMILY ||
               scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_EXISTS || scenario == TEST_UNIT_CAMPAIGN_DOCUMENT_PIN))
        CHECK(!written && driver->step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && !driver->documented &&
              !driver->launches[1] &&
              bq_retirement_unit_campaign_failure(driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED);
    else if (ok && scenario != TEST_UNIT_CAMPAIGN_NO_DOCUMENTS)
        CHECK(written && driver->documented == BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA &&
              driver->partition_counts[0] == 1 && driver->partition_counts[1] == 1 &&
              !strcmp(driver->family_sha256, fixture->family.sha256));
    return written;
}

/* The READY result's documents: each file's size and digest are the
 * recorded ones, the post-A/A binding names the pre-sample plan and the
 * admission receipt, and the partitions cover the planned records. */
static void test_unit_campaign_result_documents(TestUnitCampaign* fixture, BqRetirementUnitCampaignResult const* result,
    int documents)
{
    for (unsigned index = 0; index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; ++index)
    {
        char digest[65];
        uint64_t bytes = 0;
        int file = openat(documents, bq_retirement_unit_campaign_document_paths[index], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        CHECK(file >= 3 && tp_retirement_file_hash(file, digest, &bytes) &&
              !strcmp(digest, result->documents[index].sha256) && bytes == result->documents[index].bytes);
        if (file >= 0) CHECK(close(file) == 0);
    }
    char text[4096];
    int file = openat(documents, bq_retirement_unit_campaign_document_paths[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA],
                      O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t length = file >= 3 ? read(file, text, sizeof(text) - 1) : -1;
    if (file >= 0) CHECK(close(file) == 0);
    text[length > 0 ? length : 0] = 0;
    char expected[160];
    snprintf(expected, sizeof(expected), "\"pre_sample_plan_sha256\":\"%s\"",
             result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256);
    CHECK(length > 0 && strstr(text, expected) != NULL &&
          !strncmp(text, "{\"aa_admission_sha256\":\"", 24) && !strncmp(text + 24, fixture->aa_receipt_sha, 64) &&
          !strcmp(result->aa_admission_sha256, fixture->aa_receipt_sha) &&
          !strcmp(result->family_sha256, fixture->family.sha256) &&
          !strcmp(result->source_rows_sha256, "3333333333333333333333333333333333333333333333333333333333333333") &&
          result->partition_counts[0] == 1 && result->partition_counts[1] == 1 &&
          !strcmp(result->partitions[0][0].identity, "rows-0000") && result->partitions[0][0].records == 3u * 120u &&
          !strcmp(result->partitions[1][0].path, "retirement-result-batches-0000.json") &&
          result->partitions[1][0].records == 120u);
}

/* The post-sample record READY wrote: exactly its text, of the recorded
 * size and digest, naming the post-sample digest and all three log chains. */
static void test_unit_campaign_record(BqRetirementUnitCampaign const* driver, BqRetirementUnitCampaignResult const* result,
    FILE* record)
{
    char text[BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_BYTES_MAX + 1], digest[65], line[96];
    size_t length = record && fseek(record, 0, SEEK_SET) == 0 ? fread(text, 1, sizeof(text) - 1, record) : 0;
    text[length] = 0;
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, text, (u64)length);
    sha256_finish_hex(&hash, digest);
    CHECK(length && length == result->record.bytes && !strcmp(digest, result->record.sha256) &&
          !strncmp(text, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER, sizeof(BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD_HEADER) - 1));
    static char const* const keys[5] = {"post-sample", "log-untimed", "log-aa", "log-ab", "timed-rows"};
    char const* const values[5] = {driver->post_context_sha256, driver->log_chain_sha256[0],
        driver->log_chain_sha256[1], driver->log_chain_sha256[2], driver->timed_rows_sha256};
    for (unsigned index = 0; index < 5; ++index)
    {
        snprintf(line, sizeof(line), "\n%s=%s\n", keys[index], values[index]);
        CHECK(tp_retirement_digest(values[index]) && strstr(text, line) != NULL);
    }
}

/* Lane E's request from the READY driver: the timed rows as compose rows
 * (dimension strings borrowed), the groups' kinds, the documents as prior
 * closure entries and every D-owned identity. A timed row that is not the
 * campaign's frozen sample row is refused. */
static void test_unit_campaign_compose_handoff(TestUnitCampaign* fixture, BqRetirementUnitCampaign const* driver)
{
    TpRetirementTimedRow timed[3];
    static unsigned const ids[3] = {4, 5, 6}, groups[3] = {0, 0, 1}, runtime[3] = {0, 0, 1};
    memset(timed, 0, sizeof(timed));
    for (unsigned index = 0; index < 3; ++index)
    {
        timed[index] = (TpRetirementTimedRow){ids[index], groups[index], runtime[index], {{0}}};
        for (unsigned dimension = 0; dimension < TP_RETIREMENT_TIMED_DIMENSIONS; ++dimension)
        {
            String8 value = bq_retirement_document_value(&fixture->population, ids[index],
                                                          bq_retirement_document_dimensions[dimension]);
            memcpy(timed[index].dimensions[dimension], value.pointer, (size_t)value.length);
        }
    }
    TpRetirementComposeRow rows[3];
    TpRetirementComposeCode code[4];
    unsigned kinds[2] = {0, 0};
    BqRetirementUnitHandoff* handoff = (BqRetirementUnitHandoff*)calloc(1, sizeof(*handoff));
    TpRetirementComposeRequest request = {0};
    CHECK(handoff && bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 2, code, 4, handoff, &request));
    BqRetirementUnitCampaignResult const* result = handoff ? &handoff->result : NULL;
    if (handoff)
    {
        CHECK(request.layout == &handoff->layout && handoff->layout.rows == rows && handoff->layout.row_count == 3 &&
              handoff->layout.group_count == 2 && handoff->layout.group_kinds == kinds &&
              kinds[0] == TP_RETIREMENT_GROUP_OBJECT && kinds[1] == TP_RETIREMENT_GROUP_SINGLETON &&
              handoff->layout.population_rows == 7 && handoff->layout.untimed_groups == 1 &&
              rows[2].id == 6 && rows[2].group == 1 && rows[2].runtime == 1 &&
              !strcmp(rows[2].dimensions[5], "link") && rows[0].dimensions[0] == timed[0].dimensions[0] &&
              request.statistics == &result->plan && !strcmp(request.job, "job-1") && request.attempt == 2 &&
              request.completed_at_ns == result->completed_at_ns &&
              !strcmp(request.execution_plan_sha256, result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].sha256) &&
              !strcmp(request.result_input_plan_sha256,
                      result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_RESULT_INPUT_PLAN].sha256) &&
              !strcmp(request.post_aa_binding_sha256, result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].sha256) &&
              !strcmp(request.family_sha256, fixture->family.sha256) &&
              !strcmp(request.source_rows_sha256, result->source_rows_sha256) &&
              request.partition_counts[0] == 1 && request.partition_counts[1] == 1 &&
              !strcmp(request.partitions[0][0].identity, "rows-0000") && request.partitions[1][0].records == 120u &&
              request.code == code && request.code_count == 4 && code[3].row == 6 &&
              !strcmp(code[3].sides[1].code_sha256, fixture->program_side.code_sha256) &&
              !strcmp(handoff->prior[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].name, "workflow.execution_plan") &&
              !strcmp(handoff->prior[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].name, "workflow.phases.post_aa_binding") &&
              !strcmp(handoff->prior[BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA].path, "retirement-post-aa-binding.json") &&
              handoff->prior[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE].bytes ==
                  result->documents[BQ_RETIREMENT_UNIT_CAMPAIGN_ORACLE].bytes &&
              request.aa_metrics_tag && !strcmp(request.aa_metrics_tag, "aa") && !request.store && !request.binding_path &&
              !request.transcript_paths);
        CHECK(!strcmp(handoff->retained[0].prefix, BQ_RETIREMENT_UNIT_CAMPAIGN_RECORD) && !handoff->retained[0].suffix &&
              handoff->retained[0].files_max == 1 && !strcmp(handoff->retained[1].kind, "unitlogs") &&
              handoff->retained[1].bytes_max == BQ_RETIREMENT_UNIT_CAMPAIGN_LOG_BYTES_MAX);
        /* A timed row outside the frozen sample layout, too small a group
         * array, two swapped dimension columns or a foreign value is refused. */
        TpRetirementComposeRequest other = {0};
        timed[1].group = 1;
        CHECK(!bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 2, code, 4, handoff, &other) && !other.layout);
        timed[1].group = 0;
        CHECK(!bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 1, code, 4, handoff, &other) && !other.layout);
        char column[TP_RETIREMENT_TIMED_DIMENSION_BYTES + 1];
        memcpy(column, timed[2].dimensions[4], sizeof(column));
        memcpy(timed[2].dimensions[4], timed[2].dimensions[5], sizeof(column));
        memcpy(timed[2].dimensions[5], column, sizeof(column));
        CHECK(!bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 2, code, 4, handoff, &other) && !other.layout);
        memcpy(timed[2].dimensions[5], timed[2].dimensions[4], sizeof(column));
        memcpy(timed[2].dimensions[4], column, sizeof(column));
        char saved = timed[0].dimensions[1][0];
        timed[0].dimensions[1][0] = saved == 'x' ? 'y' : 'x';
        CHECK(!bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 2, code, 4, handoff, &other) && !other.layout);
        timed[0].dimensions[1][0] = saved;
        CHECK(bq_retirement_unit_handoff(driver, timed, 3, rows, kinds, 2, code, 4, handoff, &other) && other.layout);
    }
    free(handoff);
}

/* One attempt through the driver. Each fault stops the sequence at its step
 * and must leave no further launch; the complete run must reach MEASURED. */
static void test_unit_campaign_attempt(TestUnitCampaign* fixture, unsigned scenario)
{
    BqPhaseChannel phases;
    unsigned corrupt = scenario == TEST_UNIT_CAMPAIGN_MEASURING_ACK ? 3u : 0u;
    pid_t peer = test_unit_campaign_peer(&phases, 4, corrupt);
    int cancel[2] = {-1, -1};
    CHECK(peer > 0 && pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0 && bq_phase_exchange(&phases, BQ_PHASE_PREPARING));
    uint64_t start = tp_process_monotonic_ns();
    uint64_t deadline = bq_phase_clock() + (scenario == TEST_UNIT_CAMPAIGN_UNTIMED_DEADLINE ? UINT64_C(1500000000) :
                                                                                               UINT64_C(600000000000));
    BqRetirementUnitCampaign driver = {0};
    /* The phase peer is this process's child, not a launch. */
    driver.allowed_child = peer;
    CHECK(bq_retirement_unit_campaign_begin(&driver, &phases, cancel[0], deadline, fixture->cwd, fixture->cwd) &&
          phases.sequence == BQ_PHASE_SETTLING);
    FILE* records = tmpfile();
    unsigned char reproduced[2];
    TpRetirementUntimed untimed;
    CHECK(records && tp_retirement_untimed_init(&untimed, records, NULL, &fixture->budget, 1, reproduced, "job-1", 2,
                                                "boot-123", fixture->cpu, start, UINT64_MAX - 1, 0));
    /* A gate whose code facts the observed code must match: row 3's (untimed)
     * or row 6's candidate (timed) size is changed and the gate resealed. */
    uint64_t* changed = scenario == TEST_UNIT_CAMPAIGN_UNTIMED_FACT ? &fixture->facts[3].side[0].code_bytes :
        scenario == TEST_UNIT_CAMPAIGN_TIMED_FACT ? &fixture->facts[6].side[1].code_bytes : NULL;
    if (changed)
    {
        *changed += 1;
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    char* artifact = scenario == TEST_UNIT_CAMPAIGN_UNTIMED_NO_ARTIFACT ? fixture->facts[3].side[1].artifact_sha256 : NULL;
    char saved_artifact = artifact ? artifact[0] : 0;
    if (artifact)
    {
        artifact[0] = 0;
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    int ok = test_unit_campaign_untimed(fixture, &driver, &untimed, scenario, cancel[1]);
    if (ok && scenario == TEST_UNIT_CAMPAIGN_COMPLETE)
    {
        /* The untimed log chain: four empty logs (the fixture compiler
         * writes no output), recomputed independently. */
        Sha256 chain;
        char expected[65], empty[65];
        sha256_init(&chain);
        static char const domain[] = BQ_RETIREMENT_UNIT_CAMPAIGN_LOGS_DOMAIN;
        sha256_add(&chain, domain, sizeof(domain) - 1);
        bq_retirement_unit_campaign_number(&chain, "stage", 0);
        Sha256 nothing;
        sha256_init(&nothing);
        sha256_finish_hex(&nothing, empty);
        for (unsigned launch = 0; launch < 4; ++launch)
        {
            bq_retirement_unit_campaign_number(&chain, "log", launch);
            bq_retirement_unit_campaign_number(&chain, "bytes", 0);
            bq_retirement_unit_campaign_text(&chain, "sha256", empty);
        }
        bq_retirement_unit_campaign_number(&chain, "logs", 4);
        sha256_finish_hex(&chain, expected);
        CHECK(!strcmp(driver.log_chain_sha256[0], expected));
    }
    ok = ok && bq_retirement_unit_campaign_measuring(&driver);
    if (scenario == TEST_UNIT_CAMPAIGN_MEASURING_ACK)
        CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && phases.failed &&
              phases.sequence == BQ_PHASE_SETTLING &&
              bq_retirement_unit_campaign_failure(&driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CHANNEL);
    TestCampaignStages stages;
    memset(&stages, 0, sizeof(stages));
    uint64_t bound = scenario == TEST_UNIT_CAMPAIGN_LATE_BINDING ? start + 1 : tp_process_monotonic_ns();
    TpRetirementCampaign campaign = {0};
    BqRetirementCampaignBinding binding = {0};
    TpRetirementCampaignCommand snapshots[12];
    unsigned identities[3];
    TpRetirementPlan plan = {0};
    char plan_sha[65] = {0}, context_sha[65] = {0};
    if (ok)
    {
        CHECK(phases.sequence == BQ_PHASE_MEASURING && test_unit_campaign_open(&stages, fixture->cpu, bound));
        CHECK(bq_retirement_unit_campaign_derive(&fixture->gate, &fixture->pins, test_unit_campaign_ready,
            &driver.untimed_records, &stages.aa.samples, &stages.ab.samples, fixture->commands[0], fixture->commands[1],
            6, &plan, plan_sha, context_sha));
        TpRetirementPlan bound_plan = plan;
        if (scenario == TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE) bound_plan.cell_members_per_scope = 1;
        char const* bound_context = scenario == TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE ? test_unit_campaign_receipt :
                                                                                      context_sha;
        /* Another held pair: the same bytes, but not the file the untimed
         * batches ran as the candidate. */
        BqRetirementHeldBinaries other = fixture->held;
        if (scenario == TEST_UNIT_CAMPAIGN_OTHER_HELD)
        {
            other.descriptors[1] = fixture->other;
            CHECK(bq_retirement_campaign_descriptor_identity(fixture->other, other.verified.binary_identity_sha256[1]));
        }
        CHECK(bq_retirement_campaign_bind_held(&binding, &fixture->gate, &campaign, &bound_plan, &stages.aa.samples,
            &stages.ab.samples, scenario == TEST_UNIT_CAMPAIGN_OTHER_HELD ? &other : &fixture->held, 1, 2,
            fixture->commands[0], fixture->commands[1], snapshots, 12, identities, 3, &fixture->review,
            fixture->budget_sha, plan_sha, bound_context));
        /* The store bind's mark, a test stand-in here: the preparation
         * runner's bind_unit sets the real one. */
        if (scenario != TEST_UNIT_CAMPAIGN_UNMARKED)
            memcpy(binding.unit_ready_sha256, test_unit_campaign_ready, 65);
        /* Lane E's pre-timing store plan over the frozen capacity: one
         * owned receipt file at its bound and the minimum external entries. */
        TpRetirementCampaignStorePlan store_plan = {0};
        CHECK(tp_retirement_campaign_store_preflight(&campaign.capacity, 1, TP_RETIREMENT_RECEIPT_BYTES, 3, 0,
                                                     &store_plan));
        if (scenario == TEST_UNIT_CAMPAIGN_SHORT_STORE)
        {
            store_plan.owned_bytes -= TP_RETIREMENT_RECEIPT_BYTES;
            store_plan.bytes -= TP_RETIREMENT_RECEIPT_BYTES;
            store_plan.remaining_bytes += TP_RETIREMENT_RECEIPT_BYTES;
        }
        /* E's family counts for the same layout (37 bootstrap members, 9 cells). */
        TpRetirementFamilyCounts family = {scenario == TEST_UNIT_CAMPAIGN_BOOTSTRAP ? 38u : 37u,
                                           scenario == TEST_UNIT_CAMPAIGN_FAMILY ? 10u : 9u};
        ok = bq_retirement_unit_campaign_attach(&driver, &binding, &fixture->ready, &fixture->pins,
            scenario == TEST_UNIT_CAMPAIGN_UNPLANNED_STORE ? NULL : &store_plan, &family);
        if (scenario == TEST_UNIT_CAMPAIGN_LATE_BINDING || scenario == TEST_UNIT_CAMPAIGN_PLAN_OVERRIDE ||
            scenario == TEST_UNIT_CAMPAIGN_CONTEXT_OVERRIDE || scenario == TEST_UNIT_CAMPAIGN_UNPLANNED_STORE ||
            scenario == TEST_UNIT_CAMPAIGN_SHORT_STORE || scenario == TEST_UNIT_CAMPAIGN_UNMARKED ||
            scenario == TEST_UNIT_CAMPAIGN_FAMILY || scenario == TEST_UNIT_CAMPAIGN_OTHER_HELD ||
            scenario == TEST_UNIT_CAMPAIGN_BOOTSTRAP || scenario == TEST_UNIT_CAMPAIGN_GATE_COPY)
            CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
                  campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.aa.execution.sequence &&
                  bq_retirement_unit_campaign_failure(&driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED);
        else CHECK(ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND && !strcmp(driver.plan_sha256, plan_sha) &&
                   !strcmp(driver.context_sha256, context_sha));
    }
    /* The pre-sample documents, into a fresh scratch evidence root. */
    int documents = mkdir(fixture->documents_path, 0700) == 0 ?
        open(fixture->documents_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    CHECK(documents >= 3);
    BqRetirementUnitCampaignDocumentSources sources = {documents, &fixture->population,
        string_from_pointer(fixture->profile)};
    ok = test_unit_campaign_documents(fixture, &driver, &sources, scenario, ok);
    FILE* sample_streams[2][2] = {{tmpfile(), tmpfile()}, {tmpfile(), tmpfile()}};
    BqRetirementUnitCampaignStreams streams[2] = {{NULL, NULL, sample_streams[0], 0, 0, 2},
                                                  {NULL, NULL, sample_streams[1], 0, 0, 2}};
    if (ok && scenario == TEST_UNIT_CAMPAIGN_EARLY_FREEZE)
    {
        CHECK(!bq_retirement_unit_campaign_freeze(&driver) && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID &&
              !stages.aa.execution.sequence);
        ok = 0;
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_CANCELLED)
    {
        /* Stopped before the first A/A launch, at its coordinates. */
        TpRetirementInvocation first;
        char byte = 1;
        CHECK(tp_retirement_execution_peek(&stages.aa.execution, &first) == TP_RETIREMENT_NEXT_READY &&
              write(cancel[1], &byte, 1) == 1 &&
              !bq_retirement_unit_campaign_stage(&driver, fixture->commands[0], 6, &streams[0]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.aa.execution.sequence && !driver.launches[1]);
        BqRetirementUnitCampaignFailure failure = bq_retirement_unit_campaign_failure(&driver);
        CHECK(failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_CANCELLED && failure.stage == 1 && !failure.launched &&
              failure.sequence == first.sequence && failure.kind == first.kind && failure.group == first.group &&
              failure.variant == first.variant && failure.phase == first.phase &&
              failure.step == BQ_RETIREMENT_UNIT_CAMPAIGN_BOUND);
        ok = 0;
    }
    int attached = ok;
    ok = ok && bq_retirement_unit_campaign_stage(&driver, fixture->commands[0], 6, &streams[0]);
    if (attached && scenario == TEST_UNIT_CAMPAIGN_NO_DOCUMENTS)
        /* No pre-sample documents: the A/A stage refuses before any child. */
        CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && !driver.launches[1] &&
              !stages.aa.execution.sequence &&
              bq_retirement_unit_campaign_failure(&driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_REFUSED);
    if (ok)
        CHECK(driver.launches[1] == 732 && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_AA &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_AWAIT_AA && tp_retirement_campaign_stage_ready(&campaign, 0) &&
              driver.transcript_shards[0] == 1 && driver.metrics_shards[1] == 1 && !stages.ab.execution.sequence &&
              tp_retirement_digest(driver.post_aa_sha256) && tp_retirement_digest(driver.log_chain_sha256[1]) &&
              strcmp(driver.post_aa_sha256, context_sha) &&
              strcmp(driver.log_chain_sha256[0], driver.log_chain_sha256[1]));
    if (ok && scenario == TEST_UNIT_CAMPAIGN_COMPLETE)
    {
        /* The post-A/A digest binds the pre-sample plan document and both
         * launch-log chains. */
        char post[65], chains[2][65];
        char const* pre_sample = driver.documents[BQ_RETIREMENT_UNIT_CAMPAIGN_PRE_SAMPLE].sha256;
        memcpy(chains, driver.log_chain_sha256, sizeof(chains));
        CHECK(bq_retirement_unit_campaign_post_aa(&campaign, context_sha, pre_sample, driver.shard_chain_sha256[0],
                  (char const (*)[65])chains, post) && !strcmp(post, driver.post_aa_sha256));
        CHECK(bq_retirement_unit_campaign_post_aa(&campaign, context_sha,
                  driver.documents[BQ_RETIREMENT_UNIT_CAMPAIGN_EXECUTION_PLAN].sha256, driver.shard_chain_sha256[0],
                  (char const (*)[65])chains, post) && strcmp(post, driver.post_aa_sha256));
        for (unsigned index = 0; index < 2; ++index)
        {
            chains[index][0] = chains[index][0] == '0' ? '1' : '0';
            CHECK(bq_retirement_unit_campaign_post_aa(&campaign, context_sha, pre_sample, driver.shard_chain_sha256[0],
                      (char const (*)[65])chains, post) && strcmp(post, driver.post_aa_sha256));
            chains[index][0] = driver.log_chain_sha256[index][0];
        }
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_AB_WITHOUT_ADMISSION)
    {
        CHECK(!bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence);
        ok = 0;
    }
    /* The #437 receipt: the fixture's, or one whose bytes are not the named
     * digest, over another family or CPU, or not admitted. */
    TestUnitCampaign* receipt = fixture;
    char const* receipt_sha = fixture->aa_receipt_sha;
    char other_family[65];
    memcpy(other_family, fixture->family.sha256, 65);
    other_family[0] = other_family[0] == '0' ? '1' : '0';
    TestUnitCampaign* other = (TestUnitCampaign*)calloc(1, sizeof(*other));
    if (other && scenario == TEST_UNIT_CAMPAIGN_RECEIPT_FAMILY)
        CHECK(test_unit_campaign_aa_receipt(other, fixture->cpu, other_family, "true"));
    if (other && scenario == TEST_UNIT_CAMPAIGN_RECEIPT_CPU)
        CHECK(test_unit_campaign_aa_receipt(other, fixture->cpu + 1, fixture->family.sha256, "true"));
    if (other && scenario == TEST_UNIT_CAMPAIGN_RECEIPT_REFUSED)
        CHECK(test_unit_campaign_aa_receipt(other, fixture->cpu, fixture->family.sha256, "false"));
    if (other && (scenario == TEST_UNIT_CAMPAIGN_RECEIPT_FAMILY || scenario == TEST_UNIT_CAMPAIGN_RECEIPT_CPU ||
                  scenario == TEST_UNIT_CAMPAIGN_RECEIPT_REFUSED))
    {
        receipt = other;
        receipt_sha = other->aa_receipt_sha;
    }
    if (scenario == TEST_UNIT_CAMPAIGN_RECEIPT_DIGEST) receipt_sha = test_unit_campaign_receipt;
    BqRetirementUnitCampaignAdmission admission = {plan_sha, context_sha, driver.post_aa_sha256, receipt_sha,
        (unsigned char const*)receipt->aa_receipt, strlen(receipt->aa_receipt),
        scenario == TEST_UNIT_CAMPAIGN_DENIED ? 0 : 1, NULL};
    if (scenario == TEST_UNIT_CAMPAIGN_STALE_ADMISSION) admission.context_sha256 = test_unit_campaign_ready;
    if (scenario == TEST_UNIT_CAMPAIGN_POST_AA_ADMISSION) admission.post_aa_sha256 = context_sha;
    if (ok)
    {
        ok = bq_retirement_unit_campaign_admit(&driver, &admission);
        if (scenario == TEST_UNIT_CAMPAIGN_DENIED || scenario == TEST_UNIT_CAMPAIGN_STALE_ADMISSION ||
            scenario == TEST_UNIT_CAMPAIGN_POST_AA_ADMISSION || scenario == TEST_UNIT_CAMPAIGN_RECEIPT_DIGEST ||
            scenario == TEST_UNIT_CAMPAIGN_RECEIPT_FAMILY || scenario == TEST_UNIT_CAMPAIGN_RECEIPT_CPU ||
            scenario == TEST_UNIT_CAMPAIGN_RECEIPT_REFUSED)
            CHECK(!ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence &&
                  driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        else CHECK(ok && campaign.phase == TP_RETIREMENT_CAMPAIGN_AB);
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_NO_POST_DOCUMENT)
    {
        /* No post-A/A binding document: the freeze refuses. */
        CHECK(!bq_retirement_unit_campaign_freeze(&driver) && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
              !stages.ab.execution.sequence);
        ok = 0;
    }
    if (ok && (scenario == TEST_UNIT_CAMPAIGN_POST_DOCUMENT_PIN || scenario == TEST_UNIT_CAMPAIGN_POST_DOCUMENT_SUPPORT))
    {
        /* The census rows (or support declaration) pin changed since the
         * pre-sample plan named it. */
        char const* key = scenario == TEST_UNIT_CAMPAIGN_POST_DOCUMENT_PIN ? "census-rows-sha256=3333" :
                                                                            "support-declaration-sha256=1111";
        char* at = strstr(fixture->profile, key);
        size_t digit = strlen(key) - 1;
        char saved = at ? at[digit] : 0;
        if (at) at[digit] = saved == '4' ? '5' : '4';
        CHECK(at && !bq_retirement_unit_campaign_post_aa_document(&driver, &sources) &&
              driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
              driver.documented == BQ_RETIREMENT_UNIT_CAMPAIGN_POST_AA && !stages.ab.execution.sequence);
        if (at) at[digit] = saved;
        ok = 0;
    }
    if (ok)
    {
        /* The post-A/A binding over the admission receipt. */
        CHECK(bq_retirement_unit_campaign_post_aa_document(&driver, &sources) &&
              driver.documented == BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS &&
              !strcmp(driver.aa_admission_sha256, fixture->aa_receipt_sha));
        ok = driver.documented == BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS;
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_AB_WITHOUT_FREEZE)
    {
        CHECK(!bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]) &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID && !stages.ab.execution.sequence);
        ok = 0;
    }
    if (ok && scenario == TEST_UNIT_CAMPAIGN_FREEZE_AFTER_LAUNCH)
    {
        /* One A/B child launched around the driver: the freeze sees the
         * A/B stage already started and refuses. */
        TpRetirementInvocation first;
        CHECK(tp_retirement_execution_peek(&stages.ab.execution, &first) == TP_RETIREMENT_NEXT_READY);
        unsigned index = (first.kind ? 2 : first.group) * 2 + first.variant;
        int log = openat(fixture->cwd, BQ_RETIREMENT_UNIT_CAMPAIGN_LOG, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        int executable = binding.held_executables[first.variant].descriptor;
        int ruleset = bq_retirement_sandbox(&executable, 1, fixture->sources, fixture->cwd, NULL);
        TpProcessInputs inputs = {executable, fixture->cwd, log, fixture->environment, 0, (int)first.variant,
            {fixture->sources[0], fixture->sources[1]}, ruleset,
            (uint64_t)fixture->commands[1][index].memory_mib << 20, NULL};
        TpRetirementMeasurementResult measured;
        CHECK(log >= 3 && bq_retirement_campaign_run(&binding, &fixture->commands[1][index], &inputs, fixture->cwd,
                                                     &measured) && stages.ab.execution.sequence == 1);
        if (log >= 0) CHECK(close(log) == 0);
        if (ruleset >= 0) CHECK(close(ruleset) == 0);
        CHECK(!bq_retirement_unit_campaign_freeze(&driver) && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID);
        ok = 0;
    }
    ok = ok && bq_retirement_unit_campaign_freeze(&driver) &&
        bq_retirement_unit_campaign_stage(&driver, fixture->commands[1], 6, &streams[1]);
    if (scenario == TEST_UNIT_CAMPAIGN_TIMED_FACT)
    {
        /* Row 6's first candidate artifact is not the gate's code size:
         * refused after that A/B launch, with its coordinates. */
        BqRetirementUnitCampaignFailure failure = bq_retirement_unit_campaign_failure(&driver);
        CHECK(!ok && failure.reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_LAUNCH && failure.launched &&
              failure.after == BQ_RETIREMENT_UNIT_CAMPAIGN_AFTER_CODE && failure.stage == 2 && failure.group == 1 &&
              failure.variant == 1 && !failure.kind && failure.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
              driver.launches[2] < 732 && campaign.phase == TP_RETIREMENT_CAMPAIGN_INVALID);
    }
    if (ok)
        CHECK(driver.launches[2] == 732 && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_AB &&
              campaign.phase == TP_RETIREMENT_CAMPAIGN_COLLECTED && phases.sequence == BQ_PHASE_MEASURING);
    /* READY: the post-sample context and record and the result for lane E,
     * while MEASURED waits for the composition and authority handoff. */
    FILE* record = tmpfile();
    ok = ok && bq_retirement_unit_campaign_ready(&driver, record);
    BqRetirementUnitCampaignResult result;
    if (ok)
        CHECK(driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_READY && phases.sequence == BQ_PHASE_MEASURING &&
              bq_retirement_unit_campaign_result(&driver, &result) && !strcmp(result.job, "job-1") &&
              !strcmp(result.boot, "boot-123") && result.attempt == 2 && result.bound_at_ns == bound &&
              result.completed_at_ns > bound && !memcmp(&result.plan, &plan, sizeof(plan)) &&
              !strcmp(result.plan_sha256, plan_sha) && !strcmp(result.context_sha256, context_sha) &&
              !strcmp(result.post_aa_sha256, driver.post_aa_sha256) &&
              !strcmp(result.post_context_sha256, driver.post_context_sha256) && result.code_count == 4 &&
              result.codes[0].row == 3 && result.codes[1].row == 4 && result.codes[2].row == 5 &&
              result.codes[3].row == 6 &&
              !strcmp(result.codes[3].sides[1].code_sha256, fixture->program_side.code_sha256) &&
              result.codes[3].sides[0].code_bytes == fixture->program_side.code_bytes &&
              result.codes == fixture->codes && result.untimed_records.records == 4 && result.launches[0] == 4 &&
              result.launches[1] == 732 && result.launches[2] == 732 &&
              tp_retirement_digest(result.log_chain_sha256[0]) && tp_retirement_digest(result.log_chain_sha256[1]) &&
              tp_retirement_digest(result.log_chain_sha256[2]) &&
              strcmp(result.log_chain_sha256[1], result.log_chain_sha256[2]));
    if (ok && scenario == TEST_UNIT_CAMPAIGN_COMPLETE)
    {
        test_unit_campaign_result_documents(fixture, &result, documents);
        test_unit_campaign_record(&driver, &result, record);
        test_unit_campaign_compose_handoff(fixture, &driver);
    }
    BqRetirementUnitCampaignHandoff handoff = {test_unit_campaign_receipt,
        scenario == TEST_UNIT_CAMPAIGN_BAD_HANDOFF ? "not-a-digest" : test_unit_campaign_ready};
    ok = ok && bq_retirement_unit_campaign_measured(&driver, &handoff);
    if (scenario == TEST_UNIT_CAMPAIGN_BAD_HANDOFF)
        CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && phases.sequence == BQ_PHASE_MEASURING);
    if (scenario == TEST_UNIT_CAMPAIGN_COMPLETE && driver.step != BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED)
    {
        /* Name the step that stopped the complete run. */
        BqRetirementUnitCampaignFailure failure = bq_retirement_unit_campaign_failure(&driver);
        fprintf(stderr, "unit campaign complete run stopped: step=%u reason=%u stage=%u launched=%u after=%u "
                "status=%d exit=%d signal=%d timed_out=%d cancelled=%d group=%u row=%u variant=%u sequence=%" PRIu64
                "\n", failure.step, failure.reason, failure.stage, failure.launched, failure.after, failure.status,
                failure.exit_code, failure.signal_number, failure.timed_out, failure.cancelled, failure.group,
                failure.row, failure.variant, failure.sequence);
    }
    if (scenario == TEST_UNIT_CAMPAIGN_COMPLETE)
    {
        TpRetirementCampaignOutcome outcome = tp_retirement_campaign_outcome(&campaign);
        char post[65];
        char const other_chains[3][65] = {"0000000000000000000000000000000000000000000000000000000000000000",
                                          "0000000000000000000000000000000000000000000000000000000000000000",
                                          "0000000000000000000000000000000000000000000000000000000000000000"};
        CHECK(ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FINISHED && phases.sequence == BQ_PHASE_MEASURED &&
              outcome.execution == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
              outcome.aa_qualification == TP_RETIREMENT_CAMPAIGN_STATE_COMPLETE &&
              outcome.validity == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE &&
              outcome.statistical_decision == TP_RETIREMENT_CAMPAIGN_STATE_UNAVAILABLE &&
              tp_retirement_digest(driver.post_context_sha256) && strcmp(driver.post_context_sha256, context_sha) &&
              bq_retirement_unit_campaign_failure(&driver).reason == BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_NONE);
        /* The post-sample digest chains exactly the frozen pre-sample one. */
        char const (*logs)[65] = (char const (*)[65])driver.log_chain_sha256;
        CHECK(bq_retirement_unit_campaign_post_context(&campaign, context_sha,
                  (char const (*)[65])driver.shard_chain_sha256, logs, post) &&
              !strcmp(post, driver.post_context_sha256));
        CHECK(!bq_retirement_unit_campaign_post_context(&campaign, plan_sha,
                  (char const (*)[65])driver.shard_chain_sha256, logs, post) && !post[0]);
        CHECK(bq_retirement_unit_campaign_post_context(&campaign, context_sha, other_chains, logs, post) &&
              strcmp(post, driver.post_context_sha256));
        /* Each log chain enters the post-sample digest. */
        for (unsigned index = 0; index < 3; ++index)
        {
            char chains[3][65];
            memcpy(chains, driver.log_chain_sha256, sizeof(chains));
            chains[index][0] = chains[index][0] == '0' ? '1' : '0';
            CHECK(bq_retirement_unit_campaign_post_context(&campaign, context_sha,
                      (char const (*)[65])driver.shard_chain_sha256, (char const (*)[65])chains, post) &&
                  strcmp(post, driver.post_context_sha256));
        }
        /* MEASURED recorded the two confirmed digests over the post-sample one. */
        char measured[65];
        CHECK(bq_retirement_unit_campaign_result(&driver, &result) && result.code_count == 4 &&
              !strcmp(result.sealed_result_sha256, test_unit_campaign_receipt) &&
              !strcmp(result.authority_sha256, test_unit_campaign_ready) &&
              bq_retirement_unit_campaign_measured_digest(driver.post_context_sha256, result.record.sha256,
                  test_unit_campaign_receipt, test_unit_campaign_ready, measured) &&
              !strcmp(result.measured_sha256, measured) &&
              bq_retirement_unit_campaign_measured_digest(driver.post_context_sha256, result.record.sha256,
                  test_unit_campaign_ready, test_unit_campaign_receipt, measured) &&
              strcmp(result.measured_sha256, measured) &&
              bq_retirement_unit_campaign_measured_digest(driver.post_context_sha256, driver.post_context_sha256,
                  test_unit_campaign_receipt, test_unit_campaign_ready, measured) &&
              strcmp(result.measured_sha256, measured));
        /* A finished driver runs no further step, and then has no result. */
        CHECK(!bq_retirement_unit_campaign_measured(&driver, &handoff) &&
              driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && !bq_retirement_unit_campaign_result(&driver, &result) &&
              !result.job);
    }
    else CHECK(!ok && driver.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED && phases.sequence != BQ_PHASE_MEASURED &&
               bq_retirement_unit_campaign_failure(&driver).reason != BQ_RETIREMENT_UNIT_CAMPAIGN_STOP_NONE);
    for (unsigned stage = 0; stage < 2; ++stage)
        for (unsigned shard = 0; shard < 2; ++shard)
            if (sample_streams[stage][shard]) CHECK(fclose(sample_streams[stage][shard]) == 0);
    if (stages.aa.stream || stages.ab.stream) test_retirement_campaign_close(&stages);
    if (records) CHECK(fclose(records) == 0);
    if (record) CHECK(fclose(record) == 0);
    free(other);
    for (unsigned side = 0; side < 2; ++side)
        if (cancel[side] >= 0) CHECK(close(cancel[side]) == 0);
    CHECK(test_unit_campaign_peer_join(&phases, peer));
    test_unit_campaign_scrub(fixture->cwd, fixture->code);
    for (unsigned index = 0; documents >= 3 && index < BQ_RETIREMENT_UNIT_CAMPAIGN_DOCUMENTS; ++index)
        unlinkat(documents, bq_retirement_unit_campaign_document_paths[index], 0);
    if (documents >= 0) CHECK(close(documents) == 0);
    CHECK(rmdir(fixture->documents_path) == 0);
    if (changed)
    {
        *changed -= 1;
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
    if (artifact)
    {
        artifact[0] = saved_artifact;
        bq_retirement_correctness_seal(&fixture->gate, fixture->gate.sealed_sha256);
    }
}

/* A layout child that fails before it reports: it reports the failing
 * step's stage and errno and closes its end, so the launch returns at once
 * as a refusal with that stage and errno (TpProcess.refused), never waits
 * for a report that cannot come and starts no timer. mode 0: a CPU beyond CPU_SETSIZE;
 * 1: a CPU the process may not use (sched_setaffinity refuses); 2: a ruleset
 * descriptor that is no Landlock ruleset (landlock_restrict_self refuses in
 * the child); 3: descriptors capped at the parking floor, so the child
 * cannot park its handshake ends above the slots. */
static void test_unit_campaign_layout_refusal(TestUnitCampaign* fixture, unsigned mode)
{
    char* argv[] = {"/proc/self/fd/3", "retirement-child", "layout", "/nonexistent", "/nonexistent",
        fixture->leak_text, NULL};
    int executable = fixture->binary;
    int log = openat(fixture->cwd, "refusal.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int ruleset = log < 3 ? -1 : mode == 2 ? fcntl(log, F_DUPFD_CLOEXEC, 3) :
        bq_retirement_sandbox(&executable, 1, fixture->sources, fixture->cwd, NULL);
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    int usable = mode != 1 || (sched_getaffinity(0, sizeof(allowed), &allowed) == 0 &&
        !CPU_ISSET(CPU_SETSIZE - 1, &allowed));
    int cpu = mode == 0 ? 100000 : mode == 1 ? CPU_SETSIZE - 1 : fixture->cpu;
    struct rlimit files = {0, 0};
    int capped = mode == 3 && getrlimit(RLIMIT_NOFILE, &files) == 0 && files.rlim_cur > BQ_RETIREMENT_ROW_SLOT_HIGH;
    if (capped)
    {
        struct rlimit floor = {BQ_RETIREMENT_ROW_SLOT_HIGH, files.rlim_max};
        capped = setrlimit(RLIMIT_NOFILE, &floor) == 0;
    }
    TpProcessInputs inputs = {executable, fixture->cwd, log, fixture->environment, 0, 0,
        {fixture->sources[0], fixture->sources[1]}, ruleset, 0, NULL};
    TpProcessObservation observed = {0};
    uint64_t begun = tp_process_monotonic_ns();
    TpProcess process = log >= 3 && ruleset >= 3 && usable && (mode != 3 || capped) ?
        tp_process_observe_inputs(argv, NULL, NULL, 5, cpu, 0, &observed, &inputs) : (TpProcess){0};
    uint64_t spent = tp_process_monotonic_ns() - begun;
    if (capped) CHECK(setrlimit(RLIMIT_NOFILE, &files) == 0);
    int expected = mode == 2 ? EBADFD : EINVAL;
    TpLaunchStage stage = mode == 2 ? TP_LAUNCH_SANDBOX : mode == 3 ? TP_LAUNCH_PARK : TP_LAUNCH_CPU;
    if (!(process.refused && process.launch_error == expected && process.launch_stage == stage))
        fprintf(stderr, "unit campaign layout refusal %u: refused %d error %d stage %s exit %d\n", mode,
                process.refused, process.launch_error, tp_launch_stage_name(process.launch_stage), process.exit_code);
    CHECK(log >= 3 && ruleset >= 3 && usable && (mode != 3 || capped) && process.refused &&
          process.launch_error == expected && process.launch_stage == stage && !observed.valid &&
          !process.timed_out && !process.cancelled && spent < UINT64_C(2000000000));
    if (ruleset >= 0) CHECK(close(ruleset) == 0);
    if (log >= 0) CHECK(close(log) == 0 && unlinkat(fixture->cwd, "refusal.log", 0) == 0);
}

/* One launch per side in lane B's canonical layout, as the driver makes it:
 * the child sees exactly stdin, stdout, stderr, its side's binary slot, A's
 * two roots and the work slot, its cwd is the work slot, and its sandbox
 * denies a file beside the work directory (a stand-in for the reference
 * oracle's output) and every socket, so it cannot reach a listening unix
 * socket outside it. Its address-space limit is the launch's bound (8 GiB,
 * none under the sanitizers, whose shadow memory needs the whole address
 * space). The timer starts only after the child entered the sandbox
 * (tp_process_observe_inputs). Then each child-side refusal. */
static void test_unit_campaign_layout(TestUnitCampaign* fixture)
{
    /* The stand-in oracle output and the listening socket live beside the
     * sandbox, in a new directory outside the work directory. */
    char outside[TP_PATH_CAP], socket_directory[] = "/tmp/bq-unit-layout-XXXXXX", socket_path[64];
    int ok = mkdtemp(socket_directory) != NULL && tp_path(outside, socket_directory, "oracle-output") &&
        test_text(socket_directory, "oracle-output", "oracle\n");
    int written = snprintf(socket_path, sizeof(socket_path), "%s/listen", socket_directory);
    ok = ok && written > 0 && (size_t)written < sizeof(socket_path);
    int listener = ok ? socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) : -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);
    ok = ok && listener >= 3 && bind(listener, (struct sockaddr*)&address, sizeof(address)) == 0 &&
        listen(listener, 1) == 0;
    CHECK(ok);
    for (unsigned side = 0; ok && side < 2; ++side)
    {
        char* argv[] = {side ? "/proc/self/fd/4" : "/proc/self/fd/3", "retirement-child", "layout", outside,
            socket_path, fixture->leak_text, NULL};
        int executable = side ? fixture->candidate : fixture->binary;
        int log = openat(fixture->cwd, "layout.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        int ruleset = bq_retirement_sandbox(&executable, 1, fixture->sources, fixture->cwd, NULL);
#if BUSTER_SANITIZE
        uint64_t memory = 0;
#else
        uint64_t memory = UINT64_C(8192) << 20;
#endif
        TpProcessInputs inputs = {executable, fixture->cwd, log, fixture->environment, 0, (int)side,
            {fixture->sources[0], fixture->sources[1]}, ruleset, memory, NULL};
        TpProcessObservation observed;
        TpProcess process = log >= 3 && ruleset >= 3 ?
            tp_process_observe_inputs(argv, NULL, NULL, 30, fixture->cpu, 0, &observed, &inputs) : (TpProcess){0};
        char text[512], expected[512], bound[32];
        ssize_t length = log >= 3 ? pread(log, text, sizeof(text) - 1, 0) : -1;
        text[length > 0 ? length : 0] = 0;
        if (memory) snprintf(bound, sizeof(bound), "%llu", (unsigned long long)memory);
        else snprintf(bound, sizeof(bound), "unlimited");
        snprintf(expected, sizeof(expected), "fds=0,1,2,%u,5,6,7\ncwd=slot\nopen=denied\nsocket=denied\nas=%s\n",
                 3 + side, bound);
        if (strcmp(text, expected)) fprintf(stderr, "unit campaign layout child %u saw:\n%s", side, text);
        CHECK(log >= 3 && ruleset >= 3 && observed.valid && !process.launch_error && !process.refused &&
              process.launch_stage == TP_LAUNCH_NONE && !process.exit_code && !process.signal_number &&
              !strcmp(text, expected));
        /* A binary slot that is not the side's is refused before any child. */
        argv[0] = side ? "/proc/self/fd/3" : "/proc/self/fd/4";
        TpProcess refused = log >= 3 && ruleset >= 3 ?
            tp_process_observe_inputs(argv, NULL, NULL, 30, fixture->cpu, 0, &observed, &inputs) : (TpProcess){0};
        CHECK(refused.launch_error && !observed.valid);
        if (ruleset >= 0) CHECK(close(ruleset) == 0);
        if (log >= 0) CHECK(close(log) == 0 && unlinkat(fixture->cwd, "layout.log", 0) == 0);
    }
    if (listener >= 0) CHECK(close(listener) == 0);
    unlink(socket_path);
    unlink(outside);
    rmdir(socket_directory);
    for (unsigned mode = 0; mode < 4; ++mode) test_unit_campaign_layout_refusal(fixture, mode);
}

/* A copy of the fixture child as `leaf` in `directory`, owner-executable. */
static int test_unit_campaign_program_file(TestUnitCampaign const* fixture, int directory, char const* leaf)
{
    int input = open(fixture->program_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int output = input >= 0 ? openat(directory, leaf, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0700) : -1;
    int ok = output >= 0;
    char block[65536];
    ssize_t count = 1;
    while (ok && count > 0)
    {
        count = read(input, block, sizeof(block));
        ok = count >= 0 && (count == 0 || write(output, block, (size_t)count) == count);
    }
    if (output >= 0 && close(output) != 0) ok = 0;
    if (input >= 0) close(input);
    return ok;
}

/* The file's current identity (fstatat, not following a link); the digest
 * program already holds is kept. */
static int test_unit_campaign_program_identity(int directory, char const* leaf, TpProcessProgram* program)
{
    struct stat info;
    int ok = fstatat(directory, leaf, &info, AT_SYMLINK_NOFOLLOW) == 0;
    if (ok)
    {
        program->leaf = leaf;
        program->device = info.st_dev;
        program->inode = info.st_ino;
        program->size = info.st_size;
        program->changed = info.st_ctim;
    }
    return ok;
}

/* The identity and SHA-256 of a single-link file, both from one descriptor
 * (as the service takes them from the copy it wrote). */
static int test_unit_campaign_program_hashed(int directory, char const* leaf, TpProcessProgram* program)
{
    int file = openat(directory, leaf, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info;
    uint64_t bytes = 0;
    int ok = file >= 0 && fstat(file, &info) == 0 && tp_retirement_file_hash(file, program->sha256, &bytes) &&
        bytes == (uint64_t)info.st_size;
    if (ok)
    {
        program->leaf = leaf;
        program->device = info.st_dev;
        program->inode = info.st_ino;
        program->size = info.st_size;
        program->changed = info.st_ctim;
    }
    if (file >= 0) close(file);
    return ok;
}

/* How many 1 ms waits an in-place rewrite of the same bytes may take to
 * reach the file system's next timestamp tick (a coarse tick is at most
 * 10 ms; the bound leaves room for a loaded host). */
#define TEST_UNIT_CAMPAIGN_TICK_WAITS 2000u

/* One runtime launch of `command` in step directory `step` with `program`
 * through the launch boundary (tp_retirement_launch), on a fresh log. */
static TpRetirementMeasurementResult test_unit_campaign_runtime_launch(TestUnitCampaign* fixture,
    TpRetirementMeasuredCommand const* command, TpRetirementExecutable const* executable, int step,
    TpProcessProgram const* program)
{
    TpRetirementMeasurementResult result = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID,
        .process = {.exit_code = -1}};
    int binary = fixture->binary;
    int log = openat(fixture->cwd, "runtime-rule.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int ruleset = log >= 3 ? bq_retirement_sandbox(&binary, 1, fixture->sources, step, NULL) : -1;
    TpProcessInputs inputs = {binary, step, log, command->environment, 0, 0, {fixture->sources[0], fixture->sources[1]},
        ruleset, (uint64_t)command->memory_mib << 20, program};
    TpRetirementLaunch launch = {command, executable, &inputs, NULL, step, fixture->cpu, TP_RETIREMENT_GROUP_SINGLETON};
    char digest[65];
    if (ruleset >= 3) tp_retirement_launch(&launch, NULL, 0, &result, digest);
    if (ruleset >= 0) CHECK(close(ruleset) == 0);
    if (log >= 0) CHECK(close(log) == 0 && unlinkat(fixture->cwd, "runtime-rule.log", 0) == 0);
    return result;
}

/* A small text file `name` in directory. */
static int test_text_at(int directory, char const* name)
{
    int file = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int ok = file >= 0 && write(file, "extra\n", 6) == 6;
    if (file >= 0 && close(file) != 0) ok = 0;
    return ok;
}

/* The runtime launch of command in directory with identity is refused
 * before any child. */
static void test_unit_campaign_runtime_refused(TestUnitCampaign* fixture, TpRetirementMeasuredCommand const* command,
    TpRetirementExecutable const* executable, int directory, TpProcessProgram const* identity, char const* what)
{
    TpRetirementMeasurementResult result = test_unit_campaign_runtime_launch(fixture, command, executable, directory,
                                                                             identity);
    if (result.status != TP_RETIREMENT_MEASUREMENT_PLAN_INVALID || result.observed.valid)
        fprintf(stderr, "unit campaign runtime rule: %s launched (status %d)\n", what, (int)result.status);
    CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && !result.observed.valid);
}

/* A fresh step directory `name` in the fixture's work directory holding one
 * copy of the fixture child as `leaf` (none when leaf is NULL). */
static int test_unit_campaign_step(TestUnitCampaign const* fixture, char const* name, char const* leaf)
{
    int made = mkdirat(fixture->cwd, name, 0700) == 0;
    int step = made ? openat(fixture->cwd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (step >= 0 && leaf && !test_unit_campaign_program_file(fixture, step, leaf))
    {
        close(step);
        step = -1;
    }
    return step;
}

/* Removes step directory `name` (its files, then itself). */
static void test_unit_campaign_step_remove(TestUnitCampaign const* fixture, int step, char const* name)
{
    static char const* const leaves[] = {"prog.bin", "prog.new", "other.bin", "link.bin", "extra.txt"};
    for (unsigned index = 0; step >= 0 && index < BUSTER_ARRAY_LENGTH(leaves); ++index) unlinkat(step, leaves[index], 0);
    if (step >= 0) close(step);
    unlinkat(fixture->cwd, name, AT_REMOVEDIR);
}

/* The runtime shape at the launch boundary: lane B's `./<leaf>` command
 * runs the observed program in its step directory (slot 7), which holds
 * only that program, and its output is checked. Each of these is refused
 * before any child: another argv[0] (another file, the binary slot, no
 * `./`, a longer path), no program, a symbolic link carrying its target's
 * identity, another step's file and directory, a compiler command naming a
 * program, a replaced (planted) file, a file rewritten in place (same inode
 * and size) with the same bytes (the change time, after waiting out the
 * timestamp tick) and with other bytes (the content digest alone), a second
 * link to the program, a group- or world-writable mode, no owner execute bit
 * and an extra entry in the step directory. A program changed after the
 * launch checked it is refused by the child (ESTALE). Each case differs from
 * the observed identity by construction, never by timing. */
static void test_unit_campaign_runtime_rule(TestUnitCampaign* fixture)
{
    int step = test_unit_campaign_step(fixture, "runtime-rule-a", "prog.bin");
    int other = test_unit_campaign_step(fixture, "runtime-rule-b", "prog.bin");
    int linking = test_unit_campaign_step(fixture, "runtime-rule-l", NULL);
    TpRetirementExecutable executable;
    TpProcessProgram program = {0}, foreign = {0}, linked = {0};
    int ok = step >= 3 && other >= 3 && linking >= 3 &&
        tp_retirement_executable_init(&executable, fixture->binary, fixture->binary_sha) &&
        symlinkat("../runtime-rule-a/prog.bin", linking, "link.bin") == 0 &&
        test_unit_campaign_program_hashed(step, "prog.bin", &program) &&
        test_unit_campaign_program_hashed(other, "prog.bin", &foreign);
    /* The link carries its target's identity: only not following it tells
     * them apart. */
    linked = program;
    linked.leaf = "link.bin";
    CHECK(ok);
    char* argv[] = {"./prog.bin", "retirement-child", "runtime", "prog.bin", "ok", fixture->leak_text, NULL};
    char command_sha[65];
    TpRetirementMeasuredCommand command = {.unit = 6, .kind = 1, .variant = 0, .arguments = argv, .argument_count = 6,
        .environment = fixture->environment, .environment_count = 2, .directory = BQ_RETIREMENT_ROW_WORK_PATH,
        .timeout_seconds = 30, .command_sha256 = command_sha, .output_sha256 = fixture->runtime_sha,
        .memory_mib = fixture->commands[0][4].memory_mib};
    ok = ok && tp_retirement_command_hash(&command, command_sha);
    /* The runtime shape runs and its output is the oracle's. */
    TpRetirementMeasurementResult result = ok ? test_unit_campaign_runtime_launch(fixture, &command, &executable, step,
        &program) : (TpRetirementMeasurementResult){0};
    if (result.status != TP_RETIREMENT_MEASUREMENT_COMPLETE)
        fprintf(stderr, "unit campaign runtime rule: status %d exit %d error %d\n", (int)result.status,
                result.process.exit_code, result.process.launch_error);
    CHECK(ok && result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE && !strcmp(result.output_sha256, fixture->runtime_sha));
    char* const shapes[] = {"./other.bin", "/proc/self/fd/3", "prog.bin", "././prog.bin"};
    for (unsigned index = 0; ok && index < BUSTER_ARRAY_LENGTH(shapes); ++index)
    {
        argv[0] = shapes[index];
        ok = tp_retirement_command_hash(&command, command_sha);
        test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &program, shapes[index]);
    }
    argv[0] = "./link.bin";
    ok = ok && tp_retirement_command_hash(&command, command_sha);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, linking, &linked, "a symbolic link");
    argv[0] = "./prog.bin";
    ok = ok && tp_retirement_command_hash(&command, command_sha);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, step, NULL, "no program");
    test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &foreign, "another step's file");
    test_unit_campaign_runtime_refused(fixture, &command, &executable, other, &program, "another step's directory");
    TpRetirementMeasuredCommand compiler = fixture->commands[0][2];
    result = test_unit_campaign_runtime_launch(fixture, &compiler, &executable, step, &program);
    CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && !result.observed.valid);
    /* An extra entry beside the program. */
    ok = ok && test_text_at(step, "extra.txt");
    test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &program, "an extra entry");
    ok = ok && unlinkat(step, "extra.txt", 0) == 0;
    /* A second link to the program, its identity taken after linking (so
     * only the link count differs). */
    TpProcessProgram relinked = program;
    ok = ok && linkat(step, "prog.bin", fixture->cwd, "runtime-rule-hard.bin", 0) == 0 &&
        test_unit_campaign_program_identity(step, "prog.bin", &relinked);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &relinked, "a second link");
    ok = ok && unlinkat(fixture->cwd, "runtime-rule-hard.bin", 0) == 0;
    /* Modes: group/world-writable and not owner-executable, each observed
     * after the change. */
    static mode_t const modes[] = {0722, 0600};
    for (unsigned index = 0; ok && index < BUSTER_ARRAY_LENGTH(modes); ++index)
    {
        TpProcessProgram moded = program;
        ok = fchmodat(step, "prog.bin", modes[index], 0) == 0 &&
            test_unit_campaign_program_identity(step, "prog.bin", &moded);
        test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &moded, index ? "no owner execute bit" : "a writable mode");
    }
    ok = ok && fchmodat(step, "prog.bin", 0700, 0) == 0 && test_unit_campaign_program_identity(step, "prog.bin", &program);
    /* Rewritten in place after it was observed (the same inode and size), in
     * a fresh step directory whose copy no launch has run. step's program was
     * just executed, and the kernel refuses to open a file for writing while
     * any address space still maps it as its executable (ETXTBSY); the
     * rewrite must not depend on how soon the exited child's is torn down. */
    int rewriting = test_unit_campaign_step(fixture, "runtime-rule-w", "prog.bin");
    TpProcessProgram observed_copy = {0};
    ok = ok && rewriting >= 3 && test_unit_campaign_program_hashed(rewriting, "prog.bin", &observed_copy);
    int rewrite = ok ? openat(rewriting, "prog.bin", O_RDWR | O_CLOEXEC | O_NOFOLLOW) : -1;
    unsigned char first = 0;
    ok = rewrite >= 3 && pread(rewrite, &first, 1, 0) == 1;
    /* The same bytes: only the change time can tell, and it moves only at
     * the file system's timestamp tick, so the rewrite repeats (1 ms apart,
     * bounded) until it has. */
    struct stat rewritten;
    memset(&rewritten, 0, sizeof(rewritten));
    int moved = 0;
    for (unsigned wait = 0; ok && !moved && wait < TEST_UNIT_CAMPAIGN_TICK_WAITS; ++wait)
    {
        ok = pwrite(rewrite, &first, 1, 0) == 1 && fstat(rewrite, &rewritten) == 0;
        moved = ok && (rewritten.st_ctim.tv_sec != observed_copy.changed.tv_sec ||
                       rewritten.st_ctim.tv_nsec != observed_copy.changed.tv_nsec);
        if (ok && !moved) test_delay(1);
    }
    ok = ok && moved && rewritten.st_ino == observed_copy.inode && rewritten.st_size == observed_copy.size;
    if (!ok) fprintf(stderr, "unit campaign runtime rule: in-place rewrite not made (errno %d)\n", errno);
    CHECK(ok);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, rewriting, &observed_copy,
                                       "a file rewritten in place with the same bytes");
    /* Other bytes, with the identity the rewrite left (so the change time
     * matches whatever tick it landed in): only the content digest can
     * tell. */
    TpProcessProgram flipped = observed_copy;
    unsigned char other_byte = (unsigned char)(first ^ 0xffu);
    ok = ok && pwrite(rewrite, &other_byte, 1, 0) == 1 &&
        test_unit_campaign_program_identity(rewriting, "prog.bin", &flipped);
    if (rewrite >= 0) close(rewrite);
    CHECK(ok);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, rewriting, &flipped,
                                       "a file rewritten in place with other bytes");
    test_unit_campaign_step_remove(fixture, rewriting, "runtime-rule-w");
    /* A program replaced after its placement: a planted copy renamed over
     * it. The copy is made while the original still exists, so it cannot
     * reuse the original's inode number, as a file created after the unlink
     * may. */
    struct stat replaced;
    ok = ok && test_unit_campaign_program_identity(step, "prog.bin", &program) &&
        test_unit_campaign_program_file(fixture, step, "prog.new") && renameat(step, "prog.new", step, "prog.bin") == 0 &&
        fstatat(step, "prog.bin", &replaced, AT_SYMLINK_NOFOLLOW) == 0 && replaced.st_ino != program.inode;
    CHECK(ok);
    test_unit_campaign_runtime_refused(fixture, &command, &executable, step, &program, "a replaced file");
    /* The child's own recheck: an identity the parent never compared
     * (straight through tp_process_observe_inputs) is refused in the child. */
    TpProcessProgram stale = program;
    ok = ok && test_unit_campaign_program_identity(step, "prog.bin", &stale);
    stale.size += 1;
    int binary = fixture->binary;
    int log = ok ? openat(fixture->cwd, "runtime-rule.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    int ruleset = log >= 3 ? bq_retirement_sandbox(&binary, 1, fixture->sources, step, NULL) : -1;
    TpProcessInputs inputs = {binary, step, log, command.environment, 0, 0, {fixture->sources[0], fixture->sources[1]},
        ruleset, (uint64_t)command.memory_mib << 20, &stale};
    TpProcessObservation observed = {0};
    TpProcess process = ruleset >= 3 ?
        tp_process_observe_inputs(command.arguments, NULL, NULL, 30, fixture->cpu, 0, &observed, &inputs) : (TpProcess){0};
    CHECK(ruleset >= 3 && process.refused && process.launch_error == ESTALE &&
          process.launch_stage == TP_LAUNCH_PROGRAM && !observed.valid);
    if (ruleset >= 0) CHECK(close(ruleset) == 0);
    if (log >= 0) CHECK(close(log) == 0 && unlinkat(fixture->cwd, "runtime-rule.log", 0) == 0);
    test_unit_campaign_step_remove(fixture, step, "runtime-rule-a");
    test_unit_campaign_step_remove(fixture, other, "runtime-rule-b");
    test_unit_campaign_step_remove(fixture, linking, "runtime-rule-l");
}

/* Whether the transcript line of invocation `sequence` names `digest` as the
 * file it executed. */
static int test_unit_campaign_record_names(char const* transcript, uint64_t sequence, char const* digest)
{
    char key[48], field[96];
    snprintf(key, sizeof(key), "\"sequence\":%" PRIu64 ",", sequence);
    snprintf(field, sizeof(field), "\"executable_sha256\":\"%s\"", digest);
    char const* found = strstr(transcript, key);
    char const* start = found;
    while (start && start > transcript && start[-1] != '\n') --start;
    char const* end = found ? strchr(found, '\n') : NULL;
    char const* named = start ? strstr(start, field) : NULL;
    return end && named && named < end;
}

/* One real layout launch of `command` in step directory `step` through the
 * timed recorder (tp_retirement_measurement_run), on a fresh log; `program`
 * is the runtime launch's program (NULL for a compile). */
static int test_unit_campaign_identity_launch(TestUnitCampaign* fixture, TpSampleTest* test,
    TpRetirementMeasuredCommand const* command, TpRetirementExecutable const* executable, int step,
    TpProcessProgram const* program)
{
    int binary = executable->descriptor;
    int log = openat(fixture->cwd, "runtime-identity.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int ruleset = log >= 3 ? bq_retirement_sandbox(&binary, 1, fixture->sources, step, NULL) : -1;
    TpProcessInputs inputs = {binary, step, log, command->environment, 0, 0, {fixture->sources[0], fixture->sources[1]},
        ruleset, (uint64_t)command->memory_mib << 20, program};
    TpRetirementMeasurementResult result = {.status = TP_RETIREMENT_MEASUREMENT_PLAN_INVALID};
    int ok = ruleset >= 3 && tp_retirement_measurement_run(&test->samples, command, executable, &inputs, step, &result);
    if (!ok)
        fprintf(stderr, "unit campaign runtime identity: kind %u status %d exit %d error %d\n", command->kind,
                (int)result.status, result.process.exit_code, result.process.launch_error);
    if (ruleset >= 0 && close(ruleset) != 0) ok = 0;
    if (log >= 0 && (close(log) != 0 || unlinkat(fixture->cwd, "runtime-identity.log", 0) != 0)) ok = 0;
    return ok;
}

/* The invocation record names the file the launch executed
 * (tp_retirement_executed_sha256), which the validator checks against the
 * frozen plan: through the timed recorder, a real layout compile of the link
 * group records the compiler binary, and the runtime launch of the program
 * that compile wrote records the program's digest (the row's frozen
 * artifact, as the service took it from its copy), not the binary's. The
 * binary here is the fixture child with trailing bytes, so the two digests
 * differ; the fixture's own program has the held binary's bytes, which would
 * hide a runtime record naming the binary. Other invocations are synthetic. */
static void test_unit_campaign_runtime_identity(TestUnitCampaign* fixture)
{
    char path[TP_PATH_CAP], binary_sha[65], program_sha[65] = {0};
    uint64_t bytes = 0;
    int ok = tp_path(path, fixture->directory, "identity-child") && tp_copy_file(fixture->binary_path, path) &&
        chmod(path, 0700) == 0;
    int append = ok ? open(path, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = append >= 3 && write(append, "identity\n", 9) == 9;
    if (append >= 0 && close(append) != 0) ok = 0;
    ok = ok && chmod(path, 0500) == 0;
    int binary = ok ? open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    TpRetirementExecutable executable = {.descriptor = -1};
    ok = binary >= 3 && tp_retirement_file_hash(binary, binary_sha, &bytes) &&
        tp_retirement_executable_init(&executable, binary, binary_sha) && strcmp(binary_sha, fixture->binary_sha) != 0;
    int step = test_unit_campaign_step(fixture, "runtime-identity", NULL);
    TpSampleTest* test = (TpSampleTest*)calloc(1, sizeof(*test));
    ok = ok && step >= 3 && test && test_sample_open_layout(test, 1, 6);
    if (test) test->transcript.cpu = fixture->cpu;
    uint64_t compiled = UINT64_MAX, ran = UINT64_MAX;
    unsigned variant = 2;
    TpRetirementInvocation invocation;
    while (ok && ran == UINT64_MAX &&
           tp_retirement_execution_peek(&test->execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        int compile = !invocation.kind && compiled == UINT64_MAX;
        int run = invocation.kind && compiled != UINT64_MAX && invocation.variant == variant;
        if (compile || run)
        {
            /* The stage-0 link compile (slot 1) writes artifact-<side>.bin,
             * which the runtime command (slot 2) runs as `./<leaf>`. */
            TpRetirementMeasuredCommand command = fixture->commands[0][(run ? 4 : 2) + invocation.variant];
            command.unit = run ? invocation.row : invocation.group;
            TpProcessProgram program = {0};
            ok = !run || test_unit_campaign_program_hashed(step, command.arguments[3], &program);
            /* Synthetic intervals (1 ms each) may run ahead of the clock;
             * a real launch must start after the last recorded end. */
            while (ok && tp_process_monotonic_ns() <= test->transcript.last_end) test_delay(1);
            ok = ok && test_unit_campaign_identity_launch(fixture, test, &command, &executable, step,
                                                          run ? &program : NULL);
            if (run)
            {
                memcpy(program_sha, program.sha256, sizeof(program_sha));
                ran = invocation.sequence;
            }
            else
            {
                variant = invocation.variant;
                compiled = invocation.sequence;
            }
        }
        else ok = test_sample_observe(test, 0, TEST_SAMPLE_VALID);
    }
    CHECK(ok && compiled != UINT64_MAX && ran != UINT64_MAX && !strcmp(program_sha, fixture->binary_sha));
    long size = 0;
    int read_ok = ok && fflush(test->stream) == 0 && fseek(test->stream, 0, SEEK_END) == 0 &&
        (size = ftell(test->stream)) > 0 && fseek(test->stream, 0, SEEK_SET) == 0;
    char* transcript = read_ok ? (char*)malloc((size_t)size + 1) : NULL;
    read_ok = transcript && fread(transcript, 1, (size_t)size, test->stream) == (size_t)size;
    if (read_ok) transcript[size] = 0;
    int compiler_named = read_ok && test_unit_campaign_record_names(transcript, compiled, binary_sha);
    int program_named = read_ok && test_unit_campaign_record_names(transcript, ran, program_sha);
    int binary_named = read_ok && test_unit_campaign_record_names(transcript, ran, binary_sha);
    if (read_ok && (!compiler_named || !program_named || binary_named))
        fprintf(stderr, "unit campaign runtime identity: compiler record %s the binary, runtime record %s its "
                "program and %s the binary\n", compiler_named ? "names" : "does not name",
                program_named ? "names" : "does not name", binary_named ? "names" : "does not name");
    CHECK(read_ok && compiler_named && program_named && !binary_named);
    free(transcript);
    if (test)
    {
        test_sample_close(test);
        free(test);
    }
    if (step >= 0)
    {
        unlinkat(step, "artifact-left.bin", 0);
        unlinkat(step, "artifact-right.bin", 0);
        close(step);
        CHECK(unlinkat(fixture->cwd, "runtime-identity", AT_REMOVEDIR) == 0);
    }
    if (binary >= 0) CHECK(close(binary) == 0);
    CHECK(unlink(path) == 0);
}

static void test_retirement_unit_campaign(char const* executable_path, char const* root)
{
    TestUnitCampaign* fixture = (TestUnitCampaign*)calloc(1, sizeof(*fixture));
    int ready = fixture && test_unit_campaign_setup(fixture, executable_path, root);
    CHECK(ready);
    if (ready)
    {
        test_unit_campaign_plan(fixture);
        test_unit_campaign_layout(fixture);
        test_unit_campaign_runtime_rule(fixture);
        test_unit_campaign_runtime_identity(fixture);
        /* A driver that has not begun runs no step and touches nothing. */
        BqRetirementUnitCampaign idle = {0};
        BqRetirementUnitCampaignStreams none = {0};
        BqRetirementUnitCampaignAdmission admission = {0};
        BqRetirementUnitCampaignHandoff handoff = {test_unit_campaign_receipt, test_unit_campaign_ready};
        BqRetirementUnitCampaignCode code = {fixture->untimed_rows, fixture->codes, 4, fixture->code};
        TpRetirementFamilyCounts family = {37, 9};
        CHECK(!bq_retirement_unit_campaign_measuring(&idle) && idle.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_untimed(&idle, NULL, fixture->untimed, 4, &fixture->held, fixture->sources,
                                                   &fixture->gate, &fixture->review, &none, &code));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_attach(&idle, NULL, &fixture->ready, &fixture->pins, NULL, &family));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_stage(&idle, fixture->commands[0], 6, &none));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_admit(&idle, &admission));
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_freeze(&idle) && !bq_retirement_unit_campaign_ready(&idle, NULL) &&
              !bq_retirement_unit_campaign_measured(&idle, &handoff));
        BqRetirementUnitCampaignDocumentSources sources = {fixture->cwd, &fixture->population,
            string_from_pointer(fixture->profile)};
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_documents(&idle, &sources) && idle.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        idle = (BqRetirementUnitCampaign){0};
        CHECK(!bq_retirement_unit_campaign_post_aa_document(&idle, &sources) &&
              idle.step == BQ_RETIREMENT_UNIT_CAMPAIGN_FAILED);
        /* begin needs the PREPARING acknowledgement already held. */
        BqPhaseChannel fresh;
        pid_t peer = test_unit_campaign_peer(&fresh, 1, 0);
        idle = (BqRetirementUnitCampaign){0};
        CHECK(peer > 0 && !bq_retirement_unit_campaign_begin(&idle, &fresh, -1, bq_phase_clock() + 1000000000,
                                                             fixture->cwd, fixture->cwd) &&
              fresh.sequence == 0 && test_unit_campaign_peer_join(&fresh, peer));
        for (unsigned scenario = 0; scenario < TEST_UNIT_CAMPAIGN_SCENARIOS; ++scenario)
            test_unit_campaign_attempt(fixture, scenario);
    }
    if (fixture) test_unit_campaign_teardown(fixture);
    free(fixture);
}
#endif
#endif
